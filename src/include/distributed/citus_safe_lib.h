/*-------------------------------------------------------------------------
 *
 * safe_lib.h
 *
 * This file contains helper macros to make the _s suffix string and buffer
 * functions from safestringlib easier to use.
 *
 * Copyright (c) Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef CITUS_safe_lib_H
#define CITUS_safe_lib_H

#include "safe_lib.h"

/* Helper function that is used by the macro below */
extern void ErrnoToEreport(char *functionName, errno_t err);

#define CallErnnoToEreportForFunction(functionName, ...) \
	ErrnoToEreport(# functionName, (functionName) (__VA_ARGS__))

/*
 * Instead of returning errno_t these macros call ereport in case of failure.
 */
#define SafeStrncpy(...) CallErnnoToEreportForFunction(strncpy_s, __VA_ARGS__)
#define SafeStrcpy(...) CallErnnoToEreportForFunction(strcpy_s, __VA_ARGS__)
#define SafeMemcpy(...) CallErnnoToEreportForFunction(memcpy_s, __VA_ARGS__)

#endif
