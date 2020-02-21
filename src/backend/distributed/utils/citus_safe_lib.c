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

#include <limits.h>

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


/*
 * SafeStringToInt64 converts a string containing a number to a int64. When it
 * fails it calls ereport.
 *
 * The different error cases are inspired by
 * https://stackoverflow.com/a/26083517/2570866
 */
int64
SafeStringToInt64(const char *str)
{
	char *endptr;
	errno = 0;
	int64 number = strtol(str, &endptr, 10);

	if (str == endptr)
	{
		ereport(ERROR, (errmsg("Error parsing %s as int64, no digits found\n", str)));
	}
	else if (errno == ERANGE && number == LONG_MIN)
	{
		ereport(ERROR, (errmsg("Error parsing %s as int64, underflow occured\n", str)));
	}
	else if (errno == ERANGE && number == LONG_MAX)
	{
		ereport(ERROR, (errmsg("Error parsing %s as int64, overflow occured\n", str)));
	}
	else if (errno == EINVAL)
	{
		ereport(ERROR, (errmsg(
							"Error parsing %s as int64, base contains unsupported value\n",
							str)));
	}
	else if (errno != 0 && number == 0)
	{
		int err = errno;
		ereport(ERROR, (errmsg("Error parsing %s as int64, errno %d\n", str, err)));
	}
	else if (errno == 0 && str && *endptr != '\0')
	{
		ereport(ERROR, (errmsg(
							"Error parsing %s as int64, aditional characters remain after int64\n",
							str)));
	}
	return number;
}


/*
 * SafeStringToUint64 converts a string containing a number to a uint64. When it
 * fails it calls ereport.
 *
 * The different error cases are inspired by
 * https://stackoverflow.com/a/26083517/2570866
 */
uint64
SafeStringToUint64(const char *str)
{
	char *endptr;
	errno = 0;
	uint64 number = strtoul(str, &endptr, 10);

	if (str == endptr)
	{
		ereport(ERROR, (errmsg("Error parsing %s as uint64, no digits found\n", str)));
	}
	else if (errno == ERANGE && number == LONG_MIN)
	{
		ereport(ERROR, (errmsg("Error parsing %s as uint64, underflow occured\n", str)));
	}
	else if (errno == ERANGE && number == LONG_MAX)
	{
		ereport(ERROR, (errmsg("Error parsing %s as uint64, overflow occured\n", str)));
	}
	else if (errno == EINVAL)
	{
		ereport(ERROR, (errmsg(
							"Error parsing %s as uint64, base contains unsupported value\n",
							str)));
	}
	else if (errno != 0 && number == 0)
	{
		int err = errno;
		ereport(ERROR, (errmsg("Error parsing %s as uint64, errno %d\n", str, err)));
	}
	else if (errno == 0 && str && *endptr != '\0')
	{
		ereport(ERROR, (errmsg(
							"Error parsing %s as uint64, aditional characters remain after uint64\n",
							str)));
	}
	return number;
}
