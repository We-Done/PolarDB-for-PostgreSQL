/*-------------------------------------------------------------------------
 *
 * evtcache.h
 *	  Special-purpose cache for event trigger data.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/include/utils/evtcache.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef EVTCACHE_H
#define EVTCACHE_H

#include "nodes/bitmapset.h"
#include "nodes/pg_list.h"

typedef enum
{
	EVT_DDLCommandStart,
	EVT_DDLCommandEnd,
	EVT_SQLDrop,
	EVT_TableRewrite,
	EVT_Login,
} EventTriggerEvent;

typedef struct
{
	Oid			fnoid;			/* function to be called */
	char		enabled;		/* as SESSION_REPLICATION_ROLE_* */
<<<<<<< HEAD
	int			ntags;			/* number of command tags */
	char	  **tag;			/* command tags in SORTED order */
	Oid			polar_evtowner;		/* POLAR: owner of event trigger */
=======
	Bitmapset  *tagset;			/* command tags, or NULL if empty */
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
} EventTriggerCacheItem;

extern List *EventCacheLookup(EventTriggerEvent event);

#endif							/* EVTCACHE_H */
