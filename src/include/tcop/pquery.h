/*-------------------------------------------------------------------------
 *
 * pquery.h
 *	  prototypes for pquery.c.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/tcop/pquery.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PQUERY_H
#define PQUERY_H

#include "nodes/parsenodes.h"
#include "utils/portal.h"

struct PlannedStmt;				/* avoid including plannodes.h here */


extern PGDLLIMPORT Portal ActivePortal;


extern PortalStrategy ChoosePortalStrategy(List *stmts);

extern List *FetchPortalTargetList(Portal portal);

extern List *FetchStatementTargetList(Node *stmt);

<<<<<<< HEAD
extern void PortalStart(Portal portal, 
						ParamListInfo params,
						int eflags, 
						Snapshot snapshot,
						QueryDispatchDesc *ddesc/* POLAR px */
						);
=======
extern void PortalStart(Portal portal, ParamListInfo params,
						int eflags, Snapshot snapshot);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

extern void PortalSetResultFormat(Portal portal, int nFormats,
								  int16 *formats);

extern bool PortalRun(Portal portal, long count, bool isTopLevel,
					  bool run_once, DestReceiver *dest, DestReceiver *altdest,
					  QueryCompletion *qc);

extern uint64 PortalRunFetch(Portal portal,
							 FetchDirection fdirection,
							 long count,
							 DestReceiver *dest);

extern bool PlannedStmtRequiresSnapshot(struct PlannedStmt *pstmt);

extern void EnsurePortalSnapshotExists(void);

extern bool PlannedStmtRequiresSnapshot(struct PlannedStmt *pstmt);

extern void EnsurePortalSnapshotExists(void);

#endif							/* PQUERY_H */
