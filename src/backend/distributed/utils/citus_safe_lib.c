/*-------------------------------------------------------------------------
 *
 * safe_lib.c
 *
 * This file contains a function used by all the SafeXXXX macros in
 * utils/citus_safe_lib.h
 *
 * Copyright (c) Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "safe_lib.h"

#include "distributed/citus_safe_lib.h"

void
ErrnoToEreport(char *functionName, errno_t err)
{
	if (err)
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
							"%s_s failed with errno %d", functionName, err)));
	}
}
