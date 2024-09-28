/*-------------------------------------------------------------------------
 *
 * rewriteManip.h
 *		Querytree manipulation subroutines for query rewriter.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/rewrite/rewriteManip.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef REWRITEMANIP_H
#define REWRITEMANIP_H

#include "nodes/parsenodes.h"

struct AttrMap;					/* avoid including attmap.h here */


typedef struct replace_rte_variables_context replace_rte_variables_context;

typedef Node *(*replace_rte_variables_callback) (Var *var,
												 replace_rte_variables_context *context);

struct replace_rte_variables_context
{
	replace_rte_variables_callback callback;	/* callback function */
	void	   *callback_arg;	/* context data for callback function */
	int			target_varno;	/* RTE index to search for */
	int			sublevels_up;	/* (current) nesting depth */
	bool		inserted_sublink;	/* have we inserted a SubLink? */
};

typedef enum ReplaceVarsNoMatchOption
{
	REPLACEVARS_REPORT_ERROR,	/* throw error if no match */
	REPLACEVARS_CHANGE_VARNO,	/* change the Var's varno, nothing else */
	REPLACEVARS_SUBSTITUTE_NULL,	/* replace with a NULL Const */
} ReplaceVarsNoMatchOption;


extern void CombineRangeTables(List **dst_rtable, List **dst_perminfos,
							   List *src_rtable, List *src_perminfos);
extern void OffsetVarNodes(Node *node, int offset, int sublevels_up);
extern void ChangeVarNodes(Node *node, int rt_index, int new_index,
						   int sublevels_up);
extern void IncrementVarSublevelsUp(Node *node, int delta_sublevels_up,
									int min_sublevels_up);
extern void IncrementVarSublevelsUp_rtable(List *rtable,
<<<<<<< HEAD
							   int delta_sublevels_up, int min_sublevels_up);
/* POLAR px */
extern void polar_IncrementVarSublevelsUpInTransformGroupedWindows(Node *node,
															 int delta_sublevels_up, int min_sublevels_up);
=======
										   int delta_sublevels_up, int min_sublevels_up);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

extern bool rangeTableEntry_used(Node *node, int rt_index,
								 int sublevels_up);

extern Query *getInsertSelectQuery(Query *parsetree, Query ***subquery_ptr);

extern void AddQual(Query *parsetree, Node *qual);
extern void AddInvertedQual(Query *parsetree, Node *qual);

extern bool contain_aggs_of_level(Node *node, int levelsup);
extern int	locate_agg_of_level(Node *node, int levelsup);
extern bool contain_windowfuncs(Node *node);
extern int	locate_windowfunc(Node *node);
extern bool checkExprHasSubLink(Node *node);

extern Node *add_nulling_relids(Node *node,
								const Bitmapset *target_relids,
								const Bitmapset *added_relids);
extern Node *remove_nulling_relids(Node *node,
								   const Bitmapset *removable_relids,
								   const Bitmapset *except_relids);

extern Node *replace_rte_variables(Node *node,
								   int target_varno, int sublevels_up,
								   replace_rte_variables_callback callback,
								   void *callback_arg,
								   bool *outer_hasSubLinks);
extern Node *replace_rte_variables_mutator(Node *node,
										   replace_rte_variables_context *context);

extern Node *map_variable_attnos(Node *node,
								 int target_varno, int sublevels_up,
								 const struct AttrMap *attno_map,
								 Oid to_rowtype, bool *found_whole_row);

extern Node *ReplaceVarsFromTargetList(Node *node,
									   int target_varno, int sublevels_up,
									   RangeTblEntry *target_rte,
									   List *targetlist,
									   ReplaceVarsNoMatchOption nomatch_option,
									   int nomatch_varno,
									   bool *outer_hasSubLinks);

#endif							/* REWRITEMANIP_H */
