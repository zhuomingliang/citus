/*-------------------------------------------------------------------------
 *
 * extended_op_node_utils.h
 *	  General Citus planner code.
 *
 * Copyright (c) Citus Data, Inc.
 *-------------------------------------------------------------------------
 */

#ifndef EXTENDED_OP_NODE_UTILS_H_
#define EXTENDED_OP_NODE_UTILS_H_

#include "distributed/multi_logical_planner.h"

/*
 * PushDownLevel indicates how much of the sql pipeline is being pushed down.
 * See https://jvns.ca/blog/2019/10/03/sql-queries-don-t-start-with-select
 */
typedef enum PushDownLevel
{
	/* push down only the FROM .. WHERE .. clauses */
	PUSH_DOWN_JOIN_TREE,

	/* partially push down GROUP BY and/or aggregates, merge on the coordinator */
	PUSH_DOWN_AGGREGATION_PARTIAL,

	/* fully push down GROUP BY */
	PUSH_DOWN_AGGREGATION_FULL,

	/* fully push down window functions */
	PUSH_DOWN_WINDOW_FUNCTIONS,

	/* fully push down ORDER BY and/or LIMIT */
	PUSH_DOWN_ORDER_BY_LIMIT,
} PushDownBoundary;

/*
 * ExtendedOpNodeProperties is a helper structure that is used to
 * share the common information among the worker and master extended
 * op nodes.
 *
 * It is designed to be a read-only singleton object per extended op node
 * generation and processing.
 */
typedef struct ExtendedOpNodeProperties
{
	PushDownBoundary pushDownBoundary;
	bool pullDistinctColumns;
} ExtendedOpNodeProperties;


extern ExtendedOpNodeProperties BuildExtendedOpNodeProperties(
	MultiExtendedOp *extendedOpNode, bool pullUpIntermediateRows);


#endif /* EXTENDED_OP_NODE_UTILS_H_ */
