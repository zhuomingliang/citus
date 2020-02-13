/*-------------------------------------------------------------------------
 *
 * multi_explain.h
 *	  Explain support for Citus.
 *
 * Copyright (c) Citus Data, Inc.
 *-------------------------------------------------------------------------
 */

#ifndef MULTI_EXPLAIN_H
#define MULTI_EXPLAIN_H

#include "executor/executor.h"
#include "commands/explain.h"
#include "distributed/multi_physical_planner.h"

/* Config variables managed via guc.c to explain distributed query plans */
extern bool ExplainDistributedQueries;
extern bool ExplainAllTasks;

ExplainState * GetExplainStateFromExplainStmt(ExplainStmt *stmt);
void ExplainPlan(ExplainState *es, DistributedPlan *distributedPlan);

#endif /* MULTI_EXPLAIN_H */
