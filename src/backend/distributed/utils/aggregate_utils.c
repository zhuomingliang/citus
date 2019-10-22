/*-------------------------------------------------------------------------
 *
 * aggregate_utils.c
 *
 * Implementation of UDFs distributing execution of aggregates across workers.
 *
 * When an aggregate has a combinefunc, we use worker_partial_agg to skip
 * calling finalfunc on workers, instead passing state to coordinator where
 * it uses combinefunc in coord_combine_agg & applying finalfunc only at end.
 *
 * As a last resort we use coord_fold_array. It functions by collecting
 * intermediate results & performing all aggregation on the coordinators.
 *
 * Copyright Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */


#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "distributed/version_compat.h"
#include "executor/executor.h"
#include "parser/parse_type.h"
#include "utils/acl.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/sortsupport.h"
#include "utils/syscache.h"
#include "utils/typcache.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "pg_config_manual.h"

#define array_fold_ordering_index 1
#define array_fold_ordering_sortop 2
#define array_fold_ordering_nullsfirst 3
#define array_fold_ordering_Natts 3


PG_FUNCTION_INFO_V1(worker_partial_agg_sfunc);
PG_FUNCTION_INFO_V1(worker_partial_agg_ffunc);
PG_FUNCTION_INFO_V1(coord_combine_agg_sfunc);
PG_FUNCTION_INFO_V1(coord_combine_agg_ffunc);
PG_FUNCTION_INFO_V1(coord_fold_array);

/*
 * internal type for support aggregates to pass transition state alongside
 * aggregation bookkeeping
 */
typedef struct StypeBox
{
	Datum value;
	Oid agg;
	Oid transtype;
	int16_t transtypeLen;
	bool transtypeByVal;
	bool valueNull;
	bool valueInit;
} StypeBox;

typedef struct SortInputRecordsContext
{
	TupleDesc tupleDesc;
	HeapTuple tuple;
	MemoryContext callContext;
	Datum *firstArgValues;
	bool *firstArgNulls;
	Datum *secondArgValues;
	bool *secondArgNulls;
	SortSupport sortKeys;
	int nkeys;
	bool distinct;
} SortInputRecordsContext;

static HeapTuple GetAggregateForm(Oid oid, Form_pg_aggregate *form);
static HeapTuple GetProcForm(Oid oid, Form_pg_proc *form);
static HeapTuple GetTypeForm(Oid oid, Form_pg_type *form);
static void * pallocInAggContext(FunctionCallInfo fcinfo, size_t size);
static void aclcheckAggregate(ObjectType objectType, Oid userOid, Oid funcOid);
static void aclcheckAggform(Form_pg_aggregate aggform);
static void InitializeStypeBox(FunctionCallInfo fcinfo, StypeBox *box, HeapTuple aggTuple,
							   Oid transtype);
static void HandleTransition(StypeBox *box, FunctionCallInfo fcinfo,
							 FunctionCallInfo innerFcinfo);
static void HandleStrictUninit(StypeBox *box, FunctionCallInfo fcinfo, Datum value);
static int CompareRecordData(const void *a, const void *b, void *context);


/*
 * GetAggregateForm loads corresponding tuple & Form_pg_aggregate for oid
 */
static HeapTuple
GetAggregateForm(Oid oid, Form_pg_aggregate *form)
{
	HeapTuple tuple = SearchSysCache1(AGGFNOID, ObjectIdGetDatum(oid));
	if (!HeapTupleIsValid(tuple))
	{
		elog(ERROR, "citus cache lookup failed for aggregate %u", oid);
	}
	*form = (Form_pg_aggregate) GETSTRUCT(tuple);
	return tuple;
}


/*
 * GetProcForm loads corresponding tuple & Form_pg_proc for oid
 */
static HeapTuple
GetProcForm(Oid oid, Form_pg_proc *form)
{
	HeapTuple tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(oid));
	if (!HeapTupleIsValid(tuple))
	{
		elog(ERROR, "citus cache lookup failed for function %u", oid);
	}
	*form = (Form_pg_proc) GETSTRUCT(tuple);
	return tuple;
}


/*
 * GetTypeForm loads corresponding tuple & Form_pg_type for oid
 */
static HeapTuple
GetTypeForm(Oid oid, Form_pg_type *form)
{
	HeapTuple tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(oid));
	if (!HeapTupleIsValid(tuple))
	{
		elog(ERROR, "citus cache lookup failed for type %u", oid);
	}
	*form = (Form_pg_type) GETSTRUCT(tuple);
	return tuple;
}


/*
 * pallocInAggContext calls palloc in fcinfo's aggregate context
 */
static void *
pallocInAggContext(FunctionCallInfo fcinfo, size_t size)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		elog(ERROR, "Aggregate function called without an aggregate context");
	}
	return MemoryContextAlloc(aggregateContext, size);
}


/*
 * aclcheckAggregate verifies that the given user has ACL_EXECUTE to the given proc
 */
static void
aclcheckAggregate(ObjectType objectType, Oid userOid, Oid funcOid)
{
	AclResult aclresult;
	if (funcOid != InvalidOid)
	{
		aclresult = pg_proc_aclcheck(funcOid, userOid, ACL_EXECUTE);
		if (aclresult != ACLCHECK_OK)
		{
			aclcheck_error(aclresult, objectType, get_func_name(funcOid));
		}
	}
}


/* aclcheckAggform makes ACL_EXECUTE checks as would be done in nodeAgg.c */
static void
aclcheckAggform(Form_pg_aggregate aggform)
{
	Oid userId = GetUserId();

	aclcheckAggregate(OBJECT_AGGREGATE, userId, aggform->aggfnoid);
	aclcheckAggregate(OBJECT_FUNCTION, userId, aggform->aggfinalfn);
	aclcheckAggregate(OBJECT_FUNCTION, userId, aggform->aggtransfn);
	aclcheckAggregate(OBJECT_FUNCTION, userId, aggform->aggdeserialfn);
	aclcheckAggregate(OBJECT_FUNCTION, userId, aggform->aggserialfn);
	aclcheckAggregate(OBJECT_FUNCTION, userId, aggform->aggcombinefn);
}


/*
 * See GetAggInitVal from pg's nodeAgg.c
 */
static void
InitializeStypeBox(FunctionCallInfo fcinfo, StypeBox *box, HeapTuple aggTuple,
				   Oid transtype)
{
	Form_pg_aggregate aggform = (Form_pg_aggregate) GETSTRUCT(aggTuple);

	aclcheckAggform(aggform);

	Datum textInitVal = SysCacheGetAttr(AGGFNOID, aggTuple,
										Anum_pg_aggregate_agginitval,
										&box->valueNull);
	box->transtype = transtype;
	box->valueInit = !box->valueNull;
	if (box->valueNull)
	{
		box->value = (Datum) 0;
	}
	else
	{
		Oid typinput,
			typioparam;

		MemoryContext aggregateContext;
		if (!AggCheckCallContext(fcinfo, &aggregateContext))
		{
			elog(ERROR, "InitializeStypeBox called from non aggregate context");
		}
		MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);

		getTypeInputInfo(transtype, &typinput, &typioparam);
		char *strInitVal = TextDatumGetCString(textInitVal);
		box->value = OidInputFunctionCall(typinput, strInitVal,
										  typioparam, -1);
		pfree(strInitVal);

		MemoryContextSwitchTo(oldContext);
	}
}


/*
 * HandleTransition copies logic used in nodeAgg's advance_transition_function
 * for handling result of transition function.
 */
static void
HandleTransition(StypeBox *box, FunctionCallInfo fcinfo, FunctionCallInfo innerFcinfo)
{
	Datum newVal = FunctionCallInvoke(innerFcinfo);
	bool newValIsNull = innerFcinfo->isnull;

	if (!box->transtypeByVal &&
		DatumGetPointer(newVal) != DatumGetPointer(box->value))
	{
		if (!newValIsNull)
		{
			MemoryContext aggregateContext;

			if (!AggCheckCallContext(fcinfo, &aggregateContext))
			{
				elog(ERROR,
					 "HandleTransition called from non aggregate context");
			}

			MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);
			if (!(DatumIsReadWriteExpandedObject(newVal,
												 false, box->transtypeLen) &&
				  MemoryContextGetParent(DatumGetEOHP(newVal)->eoh_context) ==
				  CurrentMemoryContext))
			{
				newVal = datumCopy(newVal, box->transtypeByVal, box->transtypeLen);
			}
			MemoryContextSwitchTo(oldContext);
		}

		if (!box->valueNull)
		{
			if (DatumIsReadWriteExpandedObject(box->value,
											   false, box->transtypeLen))
			{
				DeleteExpandedObject(box->value);
			}
			else
			{
				pfree(DatumGetPointer(box->value));
			}
		}
	}

	box->value = newVal;
	box->valueNull = newValIsNull;
}


/*
 * HandleStrictUninit handles initialization of state for when
 * transition function is strict & state has not yet been initialized.
 */
static void
HandleStrictUninit(StypeBox *box, FunctionCallInfo fcinfo, Datum value)
{
	/* TODO test binary compatible */
	MemoryContext aggregateContext;

	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		elog(ERROR, "HandleStrictUninit called from non aggregate context");
	}

	MemoryContext oldContext = MemoryContextSwitchTo(aggregateContext);
	box->value = datumCopy(value, box->transtypeByVal, box->transtypeLen);
	MemoryContextSwitchTo(oldContext);

	box->valueNull = false;
	box->valueInit = true;
}


/*
 * worker_partial_agg_sfunc advances transition state,
 * essentially implementing the following pseudocode:
 *
 * (box, agg, ...) -> box
 * box.agg = agg;
 * box.value = agg.sfunc(box.value, ...);
 * return box
 */
Datum
worker_partial_agg_sfunc(PG_FUNCTION_ARGS)
{
	StypeBox *box = NULL;
	Form_pg_aggregate aggform;
	LOCAL_FCINFO(innerFcinfo, FUNC_MAX_ARGS);
	FmgrInfo info;
	int argumentIndex = 0;
	bool initialCall = PG_ARGISNULL(0);

	if (initialCall)
	{
		box = pallocInAggContext(fcinfo, sizeof(StypeBox));
		box->agg = PG_GETARG_OID(1);
	}
	else
	{
		box = (StypeBox *) PG_GETARG_POINTER(0);
		Assert(box->agg == PG_GETARG_OID(1));
	}

	HeapTuple aggtuple = GetAggregateForm(box->agg, &aggform);
	Oid aggsfunc = aggform->aggtransfn;
	if (initialCall)
	{
		InitializeStypeBox(fcinfo, box, aggtuple, aggform->aggtranstype);
	}
	ReleaseSysCache(aggtuple);
	if (initialCall)
	{
		get_typlenbyval(box->transtype,
						&box->transtypeLen,
						&box->transtypeByVal);
	}

	fmgr_info(aggsfunc, &info);
	if (info.fn_strict)
	{
		for (argumentIndex = 2; argumentIndex < PG_NARGS(); argumentIndex++)
		{
			if (PG_ARGISNULL(argumentIndex))
			{
				PG_RETURN_POINTER(box);
			}
		}

		if (!box->valueInit)
		{
			HandleStrictUninit(box, fcinfo, PG_GETARG_DATUM(2));
			PG_RETURN_POINTER(box);
		}

		if (box->valueNull)
		{
			PG_RETURN_POINTER(box);
		}
	}

	InitFunctionCallInfoData(*innerFcinfo, &info, fcinfo->nargs - 1, fcinfo->fncollation,
							 fcinfo->context, fcinfo->resultinfo);
	fcSetArgExt(innerFcinfo, 0, box->value, box->valueNull);
	for (argumentIndex = 1; argumentIndex < innerFcinfo->nargs; argumentIndex++)
	{
		fcSetArgExt(innerFcinfo, argumentIndex, fcGetArgValue(fcinfo, argumentIndex + 1),
					fcGetArgNull(fcinfo, argumentIndex + 1));
	}

	HandleTransition(box, fcinfo, innerFcinfo);

	PG_RETURN_POINTER(box);
}


/*
 * worker_partial_agg_ffunc serializes transition state,
 * essentially implementing the following pseudocode:
 *
 * (box) -> text
 * return box.agg.stype.output(box.value)
 */
Datum
worker_partial_agg_ffunc(PG_FUNCTION_ARGS)
{
	LOCAL_FCINFO(innerFcinfo, 1);
	FmgrInfo info;
	StypeBox *box = (StypeBox *) (PG_ARGISNULL(0) ? NULL : PG_GETARG_POINTER(0));
	Form_pg_aggregate aggform;
	Oid typoutput = InvalidOid;
	bool typIsVarlena = false;

	if (box == NULL || box->valueNull)
	{
		PG_RETURN_NULL();
	}

	HeapTuple aggtuple = GetAggregateForm(box->agg, &aggform);

	if (aggform->aggcombinefn == InvalidOid)
	{
		ereport(ERROR, (errmsg(
							"worker_partial_agg_ffunc expects an aggregate with COMBINEFUNC")));
	}

	if (aggform->aggtranstype == INTERNALOID)
	{
		ereport(ERROR,
				(errmsg(
					 "worker_partial_agg_ffunc does not support aggregates with INTERNAL transition state")));
	}

	Oid transtype = aggform->aggtranstype;
	ReleaseSysCache(aggtuple);

	getTypeOutputInfo(transtype, &typoutput, &typIsVarlena);

	fmgr_info(typoutput, &info);

	InitFunctionCallInfoData(*innerFcinfo, &info, 1, fcinfo->fncollation,
							 fcinfo->context, fcinfo->resultinfo);
	fcSetArgExt(innerFcinfo, 0, box->value, box->valueNull);

	Datum result = FunctionCallInvoke(innerFcinfo);

	if (innerFcinfo->isnull)
	{
		PG_RETURN_NULL();
	}
	PG_RETURN_DATUM(result);
}


/*
 * coord_combine_agg_sfunc deserializes transition state from worker
 * & advances transition state using combinefunc,
 * essentially implementing the following pseudocode:
 *
 * (box, agg, text) -> box
 * box.agg = agg
 * box.value = agg.combine(box.value, agg.stype.input(text))
 * return box
 */
Datum
coord_combine_agg_sfunc(PG_FUNCTION_ARGS)
{
	LOCAL_FCINFO(innerFcinfo, 3);
	FmgrInfo info;
	Form_pg_aggregate aggform;
	Form_pg_type transtypeform;
	Datum value;
	StypeBox *box = NULL;

	if (PG_ARGISNULL(0))
	{
		box = pallocInAggContext(fcinfo, sizeof(StypeBox));
		box->agg = PG_GETARG_OID(1);
	}
	else
	{
		box = (StypeBox *) PG_GETARG_POINTER(0);
		Assert(box->agg == PG_GETARG_OID(1));
	}

	HeapTuple aggtuple = GetAggregateForm(box->agg, &aggform);

	if (aggform->aggcombinefn == InvalidOid)
	{
		ereport(ERROR, (errmsg(
							"coord_combine_agg_sfunc expects an aggregate with COMBINEFUNC")));
	}

	if (aggform->aggtranstype == INTERNALOID)
	{
		ereport(ERROR,
				(errmsg(
					 "coord_combine_agg_sfunc does not support aggregates with INTERNAL transition state")));
	}

	Oid combine = aggform->aggcombinefn;

	if (PG_ARGISNULL(0))
	{
		InitializeStypeBox(fcinfo, box, aggtuple, aggform->aggtranstype);
	}

	ReleaseSysCache(aggtuple);

	if (PG_ARGISNULL(0))
	{
		get_typlenbyval(box->transtype,
						&box->transtypeLen,
						&box->transtypeByVal);
	}

	bool valueNull = PG_ARGISNULL(2);
	HeapTuple transtypetuple = GetTypeForm(box->transtype, &transtypeform);
	Oid ioparam = getTypeIOParam(transtypetuple);
	Oid deserial = transtypeform->typinput;
	ReleaseSysCache(transtypetuple);

	fmgr_info(deserial, &info);
	if (valueNull && info.fn_strict)
	{
		value = (Datum) 0;
	}
	else
	{
		InitFunctionCallInfoData(*innerFcinfo, &info, 3, fcinfo->fncollation,
								 fcinfo->context, fcinfo->resultinfo);
		fcSetArgExt(innerFcinfo, 0, PG_GETARG_DATUM(2), valueNull);
		fcSetArg(innerFcinfo, 1, ObjectIdGetDatum(ioparam));
		fcSetArg(innerFcinfo, 2, Int32GetDatum(-1)); /* typmod */

		value = FunctionCallInvoke(innerFcinfo);
		valueNull = innerFcinfo->isnull;
	}

	fmgr_info(combine, &info);

	if (info.fn_strict)
	{
		if (valueNull)
		{
			PG_RETURN_POINTER(box);
		}

		if (!box->valueInit)
		{
			HandleStrictUninit(box, fcinfo, value);
			PG_RETURN_POINTER(box);
		}

		if (box->valueNull)
		{
			PG_RETURN_POINTER(box);
		}
	}

	InitFunctionCallInfoData(*innerFcinfo, &info, 2, fcinfo->fncollation,
							 fcinfo->context, fcinfo->resultinfo);
	fcSetArgExt(innerFcinfo, 0, box->value, box->valueNull);
	fcSetArgExt(innerFcinfo, 1, value, valueNull);

	HandleTransition(box, fcinfo, innerFcinfo);

	PG_RETURN_POINTER(box);
}


/*
 * coord_combine_agg_ffunc applies finalfunc of aggregate to state,
 * essentially implementing the following pseudocode:
 *
 * (box, ...) -> fval
 * return box.agg.ffunc(box.value)
 */
Datum
coord_combine_agg_ffunc(PG_FUNCTION_ARGS)
{
	StypeBox *box = (StypeBox *) (PG_ARGISNULL(0) ? NULL : PG_GETARG_POINTER(0));
	LOCAL_FCINFO(innerFcinfo, FUNC_MAX_ARGS);
	FmgrInfo info;
	Form_pg_aggregate aggform;
	Form_pg_proc ffuncform;

	if (box == NULL)
	{
		/*
		 * Ideally we'd return initval,
		 * but we don't know which aggregate we're handling here
		 */
		PG_RETURN_NULL();
	}

	HeapTuple aggtuple = GetAggregateForm(box->agg, &aggform);
	Oid ffunc = aggform->aggfinalfn;
	bool fextra = aggform->aggfinalextra;
	ReleaseSysCache(aggtuple);

	if (ffunc == InvalidOid)
	{
		if (box->valueNull)
		{
			PG_RETURN_NULL();
		}
		PG_RETURN_DATUM(box->value);
	}

	HeapTuple ffunctuple = GetProcForm(ffunc, &ffuncform);
	bool finalStrict = ffuncform->proisstrict;
	ReleaseSysCache(ffunctuple);

	if (finalStrict && box->valueNull)
	{
		PG_RETURN_NULL();
	}

	short innerNargs = fextra ? fcinfo->nargs : 1;
	fmgr_info(ffunc, &info);
	InitFunctionCallInfoData(*innerFcinfo, &info, innerNargs, fcinfo->fncollation,
							 fcinfo->context, fcinfo->resultinfo);
	fcSetArgExt(innerFcinfo, 0, box->value, box->valueNull);
	for (short argumentIndex = 1; argumentIndex < innerNargs; argumentIndex++)
	{
		fcSetArgNull(innerFcinfo, argumentIndex);
	}

	Datum result = FunctionCallInvoke(innerFcinfo);
	fcinfo->isnull = innerFcinfo->isnull;
	return result;
}


/*
 * coord_fold_array(
 *  aggregateOid oid,
 *  inputRecords record[],
 *  distinct bool,
 *  resjunk bool[],
 *  orderBy array_fold_ordering[],
 *  nulltag anyelement)
 *
 * The aggregate for aggregateOid is used to reduce input rows from inputRecords.
 * If distinct is true, we deduplicate the input rows.
 */
Datum
coord_fold_array(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0) ||
		PG_ARGISNULL(2) ||
		PG_ARGISNULL(3))
	{
		elog(ERROR, "coord_fold_array received an unexpected NULL parameter");
	}

	Oid aggregateOid = PG_GETARG_OID(0);
	ArrayType *inputRecordsArray = PG_ARGISNULL(1) ? NULL : PG_GETARG_ARRAYTYPE_P(1);
	bool distinct = PG_GETARG_BOOL(2);
	ArrayType *resjunkArray = PG_GETARG_ARRAYTYPE_P(3);
	ArrayType *orderby = PG_ARGISNULL(4) ? NULL : PG_GETARG_ARRAYTYPE_P(4);
	LOCAL_FCINFO(innerFcinfo, FUNC_MAX_ARGS);
	FmgrInfo info;

	/* step 1 load aggregate info */
	Form_pg_aggregate aggform;
	HeapTuple aggTuple = GetAggregateForm(aggregateOid, &aggform);

	aclcheckAggform(aggform);

	Oid ffunc = aggform->aggfinalfn;
	Oid fextra = aggform->aggfinalextra;
	Oid transtype = aggform->aggtranstype;
	Oid transfunc = aggform->aggtransfn;
	Datum transValue = (Datum) 0;
	bool transNull = false;
	Datum textInitVal = SysCacheGetAttr(AGGFNOID, aggTuple,
										Anum_pg_aggregate_agginitval,
										&transNull);
	bool transInit = !transNull;
	if (!transNull)
	{
		Oid typinput,
			typioparam;

		getTypeInputInfo(transtype, &typinput, &typioparam);
		char *strInitVal = TextDatumGetCString(textInitVal);
		transValue = OidInputFunctionCall(typinput, strInitVal,
										  typioparam, -1);
		pfree(strInitVal);
	}

	ReleaseSysCache(aggTuple);

	int16_t transtypeLen;
	bool transtypeByVal;
	get_typlenbyval(transtype,
					&transtypeLen,
					&transtypeByVal);

	ExprContext *econtext = CreateStandaloneExprContext();
	AggState *aggState = makeNode(AggState);
	MemoryContext callContext =
		AllocSetContextCreate(CurrentMemoryContext, "Citus Aggregates",
							  ALLOCSET_DEFAULT_SIZES);
	MemoryContext aggregateContext = CurrentMemoryContext;

	aggState->curaggcontext = econtext;
	econtext->ecxt_per_tuple_memory = aggregateContext;

	int arrayLength = inputRecordsArray == NULL ? 0 : ARR_DIMS(inputRecordsArray)[0];
	if (arrayLength > 0)
	{
		ArrayIterator input_iterator = array_create_iterator(inputRecordsArray, 0, NULL);
		Datum *inputRecords = palloc(sizeof(Datum) * arrayLength);
		Datum *inputRecordsCursor = inputRecords;
		Datum cursorValue;
		bool cursorNull;
		while (array_iterate(input_iterator, &cursorValue, &cursorNull))
		{
			Assert(inputRecordsCursor < inputRecords + arrayLength);
			if (cursorNull)
			{
				elog(ERROR, "unexpected null");
			}

			/* TODO check that tuple descriptors are all the same */
			/* TODO don't allow tuple descriptors with dropped columns */
			*inputRecordsCursor++ = cursorValue;
		}

		Assert(inputRecordsCursor == inputRecords + arrayLength);

		Datum firstValue = inputRecords[0];
		HeapTupleHeader rec = DatumGetHeapTupleHeader(firstValue);

		Oid tupType = HeapTupleHeaderGetTypeId(rec);
		int32 tupTypmod = HeapTupleHeaderGetTypMod(rec);
		TupleDesc tupDesc = lookup_rowtype_tupdesc(tupType, tupTypmod);
		HeapTupleData tuple;
		int ncolumns = tupDesc->natts;

		tuple.t_len = HeapTupleHeaderGetDatumLength(rec);
		ItemPointerSetInvalid(&tuple.t_self);
		tuple.t_tableOid = InvalidOid;
		tuple.t_data = NULL;

		Datum *argValues = palloc(ncolumns * sizeof(Datum));
		bool *argNulls = palloc(ncolumns * sizeof(bool));

		if (orderby)
		{
			Datum *secondArgValues = palloc(ncolumns * sizeof(Datum));
			bool *secondArgNulls = palloc(ncolumns * sizeof(bool));
			TypeName *arrayFoldOrderingName =
				typeStringToTypeName("pg_catalog.array_fold_ordering");
			Oid arrayFoldOrderingTypeOid =
				LookupTypeNameOid(NULL, arrayFoldOrderingName, false);
			SortInputRecordsContext sortContext = {
				.tupleDesc = tupDesc,
				.tuple = &tuple,
				.callContext = callContext,
				.firstArgValues = argValues,
				.firstArgNulls = argNulls,
				.secondArgValues = secondArgValues,
				.secondArgNulls = secondArgNulls,
				.sortKeys = palloc0(ARR_DIMS(orderby)[0] * sizeof(SortSupportData)),
				.nkeys = ARR_DIMS(orderby)[0],
				.distinct = distinct,
			};

			/* setup SortSupport array */
			{
				TupleDesc orderDesc =
					lookup_rowtype_tupdesc(arrayFoldOrderingTypeOid, -1);
				HeapTupleData orderTuple = {
					.t_len = orderDesc->natts,
					.t_tableOid = InvalidOid,
					.t_data = NULL,
				};
				ItemPointerSetInvalid(&orderTuple.t_self);
				Datum *orderValues = palloc(orderDesc->natts * sizeof(Datum));
				bool *orderNulls = palloc(orderDesc->natts * sizeof(bool));
				SortSupport sortKeyCursor = sortContext.sortKeys;
				ArrayIterator order_iterator =
					array_create_iterator(orderby, 0, NULL);
				Datum orderValue;
				bool orderNull;

				while (array_iterate(order_iterator, &orderValue, &orderNull))
				{
					if (orderNull)
					{
						elog(ERROR, "unexpected null");
					}

					orderTuple.t_data =
						DatumGetHeapTupleHeader(orderValue);

					heap_deform_tuple(&orderTuple, orderDesc, orderValues, orderNulls);

					for (int i = 0; i < orderDesc->natts; i++)
					{
						if (orderNulls[i])
						{
							elog(ERROR, "unexpected null");
						}
					}

					int32 columnIndex = DatumGetInt32(
						orderValues[array_fold_ordering_index - 1]);
					Oid sortop = DatumGetObjectId(
						orderValues[array_fold_ordering_sortop - 1]);
					bool nullsfirst = DatumGetBool(
						orderValues[array_fold_ordering_nullsfirst - 1]);

					sortKeyCursor->ssup_cxt = aggregateContext;
					sortKeyCursor->ssup_collation = InvalidOid;
					sortKeyCursor->ssup_nulls_first = nullsfirst;
					sortKeyCursor->ssup_attno = columnIndex + 1;

					PrepareSortSupportFromOrderingOp(sortop, sortKeyCursor);

					sortKeyCursor++;
				}

				pfree(orderValues);
				pfree(orderNulls);
				ReleaseTupleDesc(orderDesc);
			}

			MemoryContextSwitchTo(callContext);
			qsort_arg(inputRecords, arrayLength, sizeof(Datum), CompareRecordData,
					  &sortContext);
			MemoryContextSwitchTo(aggregateContext);

			pfree(sortContext.sortKeys);
			pfree(arrayFoldOrderingName);
			pfree(secondArgValues);
			pfree(secondArgNulls);
		}

		fmgr_info(transfunc, &info);
		InitFunctionCallInfoData(*innerFcinfo, &info, info.fn_nargs, fcinfo->fncollation,
								 (Node *) aggState, NULL);

		Datum oldValue = (Datum) 0;
		for (int arrayIndex = 0; arrayIndex < arrayLength; arrayIndex++)
		{
			bool isnull = false;
			Datum value = inputRecords[arrayIndex];
			if (isnull)
			{
				elog(ERROR, "unexpected null");
			}

			if (arrayIndex > 0 && distinct)
			{
				/* TODO figure this all out before looping */
				/* See recordeq code in postgres. We skip resjunk columns of records */

				/* argValues/argNulls hold oldValue's data */

				tuple.t_data = DatumGetHeapTupleHeader(value);
				heap_deform_tuple(&tuple, tupDesc, argValues, argNulls);

				Datum *newValues = palloc(ncolumns * sizeof(Datum));
				bool *newNulls = palloc(ncolumns * sizeof(bool));

				tuple.t_data = DatumGetHeapTupleHeader(oldValue);
				heap_deform_tuple(&tuple, tupDesc, newValues, newNulls);

				bool notDistinct = false;

				for (int columnIndex = 0; columnIndex < ncolumns; columnIndex++)
				{
					int columnIndex1 = columnIndex + 1;
					bool resjunkNull;
					bool resjunk = array_ref(resjunkArray, 1, &columnIndex1, -1, 1, true,
											 'c', &resjunkNull);

					if (resjunkNull)
					{
						elog(ERROR, "unexpected null");
					}

					if (resjunk)
					{
						continue;
					}

					if (argNulls[0] != argNulls[1])
					{
						/* NOT DISTINCT */
						break;
					}

					if (argNulls[0])
					{
						continue;
					}

					Form_pg_attribute attform = TupleDescAttr(tupDesc, columnIndex);
					Oid atttypid = attform->atttypid;
					TypeCacheEntry *typentry =
						typentry = lookup_type_cache(atttypid, TYPECACHE_EQ_OPR_FINFO);
					if (!OidIsValid(typentry->eq_opr_finfo.fn_oid))
					{
						ereport(ERROR,
								(errcode(ERRCODE_UNDEFINED_FUNCTION),
								 errmsg(
									 "could not identify an equality operator for type %s",
									 format_type_be(typentry->type_id))));
					}

					LOCAL_FCINFO(locfcinfo, 2);
					InitFunctionCallInfoData(*locfcinfo, &typentry->eq_opr_finfo,
											 2, attform->attcollation, NULL, NULL);
					fcSetArg(locfcinfo, 0, argValues[columnIndex]);
					fcSetArg(locfcinfo, 1, argValues[columnIndex]);

					bool eqresult = DatumGetBool(FunctionCallInvoke(locfcinfo));

					if (!eqresult)
					{
						notDistinct = true;
						break;
					}
				}

				if (notDistinct)
				{
					pfree(newValues);
					pfree(newNulls);
					goto skipindistinctrecord;
				}

				memcpy(argValues, newValues, ncolumns * sizeof(Datum));
				memcpy(argNulls, newNulls, ncolumns * sizeof(Datum));

				pfree(newValues);
				pfree(newNulls);
			}
			else
			{
				tuple.t_data = DatumGetHeapTupleHeader(value);
				heap_deform_tuple(&tuple, tupDesc, argValues, argNulls);
			}

			if (info.fn_strict)
			{
				for (int argumentIndex = 0; argumentIndex < ncolumns; argumentIndex++)
				{
					if (argNulls[argumentIndex])
					{
						goto skiprecord;
					}
				}

				if (!transInit)
				{
					/* TODO test binary compatible */
					transValue = argValues[0];
					transNull = false;
					transInit = true;
					goto skiprecord;
				}
			}

			fcSetArgExt(innerFcinfo, 0, transValue, transNull);

			/* TODO type check aggregate inputs */
			for (int argumentIndex = 0, columnIndex = 0;
				 columnIndex < ncolumns; columnIndex++)
			{
				int columnIndex1 = columnIndex + 1;
				bool resjunkNull;
				bool resjunk = array_ref(resjunkArray, 1, &columnIndex1, -1, 1, true, 'c',
										 &resjunkNull);
				if (resjunkNull)
				{
					elog(ERROR, "unexpected null");
				}

				if (!resjunk)
				{
					fcSetArgExt(innerFcinfo, argumentIndex + 1, argValues[argumentIndex],
								argNulls[argumentIndex]);
					argumentIndex++;
				}
			}

			MemoryContextSwitchTo(callContext);
			Datum newVal = FunctionCallInvoke(innerFcinfo);
			MemoryContextSwitchTo(aggregateContext);
			bool newValIsNull = innerFcinfo->isnull;

			/* TODO SHARE this logic with HandleTransition */
			if (!transtypeByVal &&
				DatumGetPointer(newVal) != DatumGetPointer(transValue))
			{
				if (!newValIsNull)
				{
					if (!(DatumIsReadWriteExpandedObject(newVal,
														 false, transtypeLen) &&
						  MemoryContextGetParent(DatumGetEOHP(newVal)->eoh_context) ==
						  CurrentMemoryContext))
					{
						newVal = datumCopy(newVal, transtypeByVal, transtypeLen);
					}
				}

				if (!transNull)
				{
					if (DatumIsReadWriteExpandedObject(transValue,
													   false, transtypeLen))
					{
						DeleteExpandedObject(transValue);
					}
					else
					{
						pfree(DatumGetPointer(transValue));
					}
				}
			}

			transValue = newVal;
			transNull = newValIsNull;

			MemoryContextReset(callContext);

skiprecord:
			if (distinct)
			{
				oldValue = value;
			}
skipindistinctrecord:;
		}

		pfree(inputRecords);
		pfree(argValues);
		pfree(argNulls);
		ReleaseTupleDesc(tupDesc);
	}

	if (ffunc != InvalidOid)
	{
		Form_pg_proc ffuncform;
		HeapTuple ffunctuple = GetProcForm(ffunc, &ffuncform);
		bool finalStrict = ffuncform->proisstrict;
		ReleaseSysCache(ffunctuple);

		if (!finalStrict || !transNull)
		{
			fmgr_info(ffunc, &info);

			short innerNargs = fextra ? info.fn_nargs : 1;
			InitFunctionCallInfoData(*innerFcinfo, &info, innerNargs, fcinfo->fncollation,
									 (Node *) aggState, NULL);
			fcSetArgExt(innerFcinfo, 0, transValue, transNull);
			for (int argumentIndex = 1; argumentIndex < innerNargs; argumentIndex++)
			{
				fcSetArgNull(innerFcinfo, argumentIndex);
			}
			transValue = FunctionCallInvoke(innerFcinfo);
			transNull = innerFcinfo->isnull;
		}
	}

	/*
	 * ExprContext expects ecxt_per_tuple_memory to be a context which
	 * it itself isn't allocated within. So we have to trick it a little.
	 */
	econtext->ecxt_per_tuple_memory = callContext;
	ReScanExprContext(econtext);
	MemoryContextDelete(callContext);

	fcinfo->isnull = transNull;
	return transValue;
}


/*
 * CompareRecordDatum wraps btrecordcmp for use by pg_qsort.
 */
static int
CompareRecordData(const void *a, const void *b, void *context)
{
	SortInputRecordsContext *sortContext = context;
	Datum firstArg = *(Datum *) a;
	Datum secondArg = *(Datum *) b;

	sortContext->tuple->t_data = DatumGetHeapTupleHeader(firstArg);
	heap_deform_tuple(sortContext->tuple, sortContext->tupleDesc,
					  sortContext->firstArgValues, sortContext->firstArgNulls);

	sortContext->tuple->t_data = DatumGetHeapTupleHeader(secondArg);
	heap_deform_tuple(sortContext->tuple, sortContext->tupleDesc,
					  sortContext->secondArgValues, sortContext->secondArgNulls);

	int result = 0;
	for (int keyIndex = 0; keyIndex < sortContext->nkeys; keyIndex++)
	{
		SortSupport sortKey = sortContext->sortKeys + keyIndex;
		int32 columnIndex = sortKey->ssup_attno - 1;

		result = ApplySortComparator(
			sortContext->firstArgValues[columnIndex],
			sortContext->firstArgNulls[columnIndex],
			sortContext->secondArgValues[columnIndex],
			sortContext->secondArgNulls[columnIndex],
			sortKey++);

		if (result != 0)
		{
			goto exitwithresult;
		}
	}

exitwithresult:
	MemoryContextReset(sortContext->callContext);

	return result;
}
