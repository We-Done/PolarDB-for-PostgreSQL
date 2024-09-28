/*-------------------------------------------------------------------------
 *
 * analyze.h
 *		parse analysis for optimizable statements
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/parser/analyze.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef ANALYZE_H
#define ANALYZE_H

#include "nodes/params.h"
#include "nodes/queryjumble.h"
#include "parser/parse_node.h"

/* Hook for plugins to get control at end of parse analysis */
typedef void (*post_parse_analyze_hook_type) (ParseState *pstate,
											  Query *query,
											  JumbleState *jstate);
extern PGDLLIMPORT post_parse_analyze_hook_type post_parse_analyze_hook;


extern Query *parse_analyze_fixedparams(RawStmt *parseTree, const char *sourceText,
										const Oid *paramTypes, int numParams, QueryEnvironment *queryEnv);
extern Query *parse_analyze_varparams(RawStmt *parseTree, const char *sourceText,
<<<<<<< HEAD
						Oid **paramTypes, int **paramLocation, int *numParams); /* POLAR: param location */
=======
									  Oid **paramTypes, int *numParams, QueryEnvironment *queryEnv);
extern Query *parse_analyze_withcb(RawStmt *parseTree, const char *sourceText,
								   ParserSetupHook parserSetup,
								   void *parserSetupArg,
								   QueryEnvironment *queryEnv);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

extern Query *parse_sub_analyze(Node *parseTree, ParseState *parentParseState,
								CommonTableExpr *parentCTE,
								bool locked_from_parent,
								bool resolve_unknowns);

extern List *transformInsertRow(ParseState *pstate, List *exprlist,
								List *stmtcols, List *icolumns, List *attrnos,
								bool strip_indirection);
extern List *transformUpdateTargetList(ParseState *pstate,
									   List *origTlist);
extern List *transformReturningList(ParseState *pstate, List *returningList,
									ParseExprKind exprKind);
extern Query *transformTopLevelStmt(ParseState *pstate, RawStmt *parseTree);
extern Query *transformStmt(ParseState *pstate, Node *parseTree);

extern bool stmt_requires_parse_analysis(RawStmt *parseTree);
extern bool analyze_requires_snapshot(RawStmt *parseTree);

extern const char *LCS_asString(LockClauseStrength strength);
extern void CheckSelectLocking(Query *qry, LockClauseStrength strength);
extern void applyLockingClause(Query *qry, Index rtindex,
							   LockClauseStrength strength,
							   LockWaitPolicy waitPolicy, bool pushedDown);

extern List *BuildOnConflictExcludedTargetlist(Relation targetrel,
											   Index exclRelIndex);

extern SortGroupClause *makeSortGroupClauseForSetOp(Oid rescoltype, bool require_hash);

extern List *BuildOnConflictExcludedTargetlist(Relation targetrel,
								  Index exclRelIndex);

#endif							/* ANALYZE_H */
