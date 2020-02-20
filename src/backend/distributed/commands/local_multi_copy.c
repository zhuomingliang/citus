/*-------------------------------------------------------------------------
 *
 * local_multi_copy.c
 *    Commands for running a copy locally
 *
 * Copyright (c) Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "commands/copy.h"
#include "catalog/namespace.h"
#include "parser/parse_relation.h"

#include "distributed/commands/multi_copy.h" 
#include "distributed/multi_partitioning_utils.h"
#include "distributed/local_multi_copy.h"


static int
ReadFromLocalBufferCallback(void *outbuf, int minread, int maxread);
static List *
make_copy_attnamelist(List* attlist);

StringInfo copybuf = NULL;

void DoLocaLCopy(TupleTableSlot *slot, CitusCopyDestReceiver *copyDest) {
	CopyStmt* copyStatement = copyDest->copyStatement;
	Oid relationId = copyDest->distributedRelationId;

	copybuf = makeStringInfo();
	// Datum *columnValues = slot->tts_values;
	// bool *columnNulls = slot->tts_isnull;
	// FmgrInfo *columnOutputFunctions = copyDest->columnOutputFunctions;
	// CopyCoercionData *columnCoercionPaths = copyDest->columnCoercionPaths;

	// AppendCopyRowData(columnValues, columnNulls, copyDest->tupleDescriptor,
	// 				copyDest->copyOutState, columnOutputFunctions,
	// 				columnCoercionPaths);

	// copybuf = copyDest->copyOutState->fe_msgbuf;
	appendStringInfoChar(copybuf, '5');
	appendStringInfoChar(copybuf, '\n');

	Oid tableId = RangeVarGetRelid(copyStatement->relation, NoLock, false);
	
	Relation distributedRel = heap_open(relationId, RowExclusiveLock);
	Relation copiedDistributedRelation = NULL;
	Form_pg_class copiedDistributedRelationTuple = NULL;

	TupleDesc tupleDescriptor = RelationGetDescr(distributedRel);

	copiedDistributedRelation = (Relation) palloc(sizeof(RelationData));
	copiedDistributedRelationTuple = (Form_pg_class) palloc(CLASS_TUPLE_SIZE);

	memcpy(copiedDistributedRelation, distributedRel, sizeof(RelationData));
	memcpy(copiedDistributedRelationTuple, distributedRel->rd_rel,
		   CLASS_TUPLE_SIZE);

	copiedDistributedRelation->rd_rel = copiedDistributedRelationTuple;
	copiedDistributedRelation->rd_att = CreateTupleDescCopyConstr(tupleDescriptor);

	/*
	 * BeginCopyFrom opens all partitions of given partitioned table with relation_open
	 * and it expects its caller to close those relations. We do not have direct access
	 * to opened relations, thus we are changing relkind of partitioned tables so that
	 * Postgres will treat those tables as regular relations and will not open its
	 * partitions.
	 */
	if (PartitionedTable(tableId))
	{
		copiedDistributedRelationTuple->relkind = RELKIND_RELATION;
	}

	ParseState *pstate = make_parsestate(NULL);
	addRangeTableEntryForRelation(pstate, copiedDistributedRelation, NULL, false, false);
	CopyState cstate = BeginCopyFrom(pstate, distributedRel, NULL, false,
				ReadFromLocalBufferCallback , make_copy_attnamelist(copyStatement->attlist), copyStatement->options);
	CopyFrom(cstate);
	EndCopyFrom(cstate);
	heap_close(distributedRel, NoLock);

}

/*
 * Create list of columns for COPY.
 */
static List *
make_copy_attnamelist(List* attlist)
{
	List	   *attnamelist = NIL;
	ListCell *attCell = NULL;

	foreach(attCell, attlist)
	{
		char* attname = (char *)lfirst(attCell);
		attnamelist = lappend(attnamelist, makeString(attname));
	}
	return attnamelist;
}


static int
ReadFromLocalBufferCallback(void *outbuf, int minread, int maxread) {
	int			bytesread = 0;

	memcpy(outbuf, &copybuf->data[0], copybuf->len);
	bytesread += copybuf->len;
	copybuf = makeStringInfo();

	return bytesread;
}
