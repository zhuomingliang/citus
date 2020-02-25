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
#include "utils/lsyscache.h"
#include "nodes/makefuncs.h"
#include <netinet/in.h> /* for htons */


#include "distributed/commands/multi_copy.h"
#include "distributed/multi_partitioning_utils.h"
#include "distributed/local_multi_copy.h"
#include "distributed/shard_utils.h"

/*
 * Data size threshold to switch over the active placement for a connection.
 * If this is too low, overhead of starting COPY commands will hurt the
 * performance. If this is too high, buffered data will use lots of memory.
 * 8MB is a good balance between memory usage and performance. Note that this
 * is irrelevant in the common case where we open one connection per placement.
 */
#define LOCAL_COPY_SWITCH_OVER_THRESHOLD (8 * 1024 * 1024)


static int ReadFromLocalBufferCallback(void *outbuf, int minread, int maxread);
static List * make_copy_attnamelist(List *attlist);
static Relation CreateCopiedShard(RangeVar *distributedRel, Relation shard);
static void FillLocalCopyBuffer(TupleTableSlot *slot, CitusCopyDestReceiver *copyDest,
								bool isBinary);

static bool ShouldSendCopyNow(void);
static void DoLocalCopy(Oid relationId, int64 shardId, CopyStmt *copyStatement);
static bool ShouldAddBinaryHeaders(bool isBinary);

StringInfo localCopyBuffer;

void
ProcessLocalCopy(TupleTableSlot *slot, CitusCopyDestReceiver *copyDest, int64 shardId,
				 StringInfo buffer, bool shouldSendNow)
{
	localCopyBuffer = buffer;
	StringInfo previousBuffer = copyDest->copyOutState->fe_msgbuf;
	copyDest->copyOutState->fe_msgbuf = localCopyBuffer;
	bool isBinaryCopy = copyDest->copyOutState->binary;


	FillLocalCopyBuffer(slot, copyDest, isBinaryCopy);

	if (shouldSendNow || ShouldSendCopyNow())
	{
		if (isBinaryCopy)
		{
			AppendCopyBinaryFooters(copyDest->copyOutState);
		}
		DoLocalCopy(copyDest->distributedRelationId, shardId, copyDest->copyStatement);
	}

	copyDest->copyOutState->fe_msgbuf = previousBuffer;
}


static bool
ShouldSendCopyNow()
{
	return localCopyBuffer->len > LOCAL_COPY_SWITCH_OVER_THRESHOLD;
}


static void
DoLocalCopy(Oid relationId, int64 shardId, CopyStmt *copyStatement)
{
	Oid shardOid = GetShardOid(relationId, shardId);
	Relation shard = heap_open(shardOid, RowExclusiveLock);
	Relation copiedShard = CreateCopiedShard(copyStatement->relation, shard);

	CopyState cstate = BeginCopyFrom(NULL, copiedShard, NULL, false,
									 ReadFromLocalBufferCallback, make_copy_attnamelist(
										 copyStatement->attlist), copyStatement->options);									 
	CopyFrom(cstate);
	EndCopyFrom(cstate);
	resetStringInfo(localCopyBuffer);
	heap_close(shard, NoLock);
}


static void
FillLocalCopyBuffer(TupleTableSlot *slot, CitusCopyDestReceiver *copyDest, bool isBinary)
{
	if (ShouldAddBinaryHeaders(isBinary))
	{
		AppendCopyBinaryHeaders(copyDest->copyOutState);
	}

	if (slot != NULL)
	{
		Datum *columnValues = slot->tts_values;
		bool *columnNulls = slot->tts_isnull;
		FmgrInfo *columnOutputFunctions = copyDest->columnOutputFunctions;
		CopyCoercionData *columnCoercionPaths = copyDest->columnCoercionPaths;

		AppendCopyRowData(columnValues, columnNulls, copyDest->tupleDescriptor,
						  copyDest->copyOutState, columnOutputFunctions,
						  columnCoercionPaths);
	}
}


static bool
ShouldAddBinaryHeaders(bool isBinary)
{
	if (!isBinary)
	{
		return false;
	}
	return localCopyBuffer->len == 0;
}


Relation
CreateCopiedShard(RangeVar *distributedRel, Relation shard)
{
	TupleDesc tupleDescriptor = RelationGetDescr(shard);

	Relation copiedDistributedRelation = (Relation) palloc(sizeof(RelationData));
	Form_pg_class copiedDistributedRelationTuple = (Form_pg_class) palloc(
		CLASS_TUPLE_SIZE);

	memcpy(copiedDistributedRelation, shard, sizeof(RelationData));
	memcpy(copiedDistributedRelationTuple, shard->rd_rel,
		   CLASS_TUPLE_SIZE);

	copiedDistributedRelation->rd_rel = copiedDistributedRelationTuple;
	copiedDistributedRelation->rd_att = CreateTupleDescCopyConstr(tupleDescriptor);

	Oid tableId = RangeVarGetRelid(distributedRel, NoLock, false);

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
	return copiedDistributedRelation;
}


/*
 * Create list of columns for COPY.
 */
static List *
make_copy_attnamelist(List *attlist)
{
	List *attnamelist = NIL;
	ListCell *attCell = NULL;

	foreach(attCell, attlist)
	{
		char *attname = (char *) lfirst(attCell);
		attnamelist = lappend(attnamelist, makeString(attname));
	}
	return attnamelist;
}


static int
ReadFromLocalBufferCallback(void *outbuf, int minread, int maxread)
{
	int bytesread = 0;
	int avail = localCopyBuffer->len - localCopyBuffer->cursor;
	int bytesToRead = Min(avail, maxread);

	memcpy(outbuf, &localCopyBuffer->data[localCopyBuffer->cursor], bytesToRead);
	bytesread += bytesToRead;
	localCopyBuffer->cursor += bytesToRead;

	return bytesread;
}
