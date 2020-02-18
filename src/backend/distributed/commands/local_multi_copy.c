

#include "tuptable.h"

#include "distributed/multi_copy.h" 



bool IsLocalCopy(TupleTableSlot *slot, CitusCopyDestReceiver *copyDest) {
    Datum *columnValues = slot->tts_values;
	bool *columnNulls = slot->tts_isnull;

	int64 shardId = ShardIdForTuple(copyDest, columnValues, columnNulls);
}