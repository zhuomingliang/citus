/*-------------------------------------------------------------------------
 * log_utils.h
 *
 * Copyright (c) Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef LOG_UTILS_H
#define LOG_UTILS_H


#include "utils/guc.h"


extern bool IsLoggableLevel(int logLevel);
extern char * HashLogMessage(const char *text);

#define ApplyLogRedaction(text) \
	(log_min_messages <= ereport_loglevel ? HashLogMessage(text) : text)

#undef ereport

/*
 * The AZURE_ORCAS_BREADTH variant of PostgreSQL has two extra parameters for private
 * and internal event logging in ereport_domain..
 */
#ifdef AZURE_ORCAS_BREADTH
#define ereport(elevel, rest) \
	do { \
		int ereport_loglevel = elevel; \
		(void) (ereport_loglevel); \
		ereport_domain(elevel, TEXTDOMAIN, rest, false, false); \
	} while (0)
#else
#define ereport(elevel, rest) \
	do { \
		int ereport_loglevel = elevel; \
		(void) (ereport_loglevel); \
		ereport_domain(elevel, TEXTDOMAIN, rest); \
	} while (0)
#endif

#endif /* LOG_UTILS_H */
