/*-------------------------------------------------------------------------
 *
 * tablecmds.h
 *	  prototypes for tablecmds.c.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/tablecmds.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef TABLECMDS_H
#define TABLECMDS_H

#include "access/htup.h"
#include "catalog/dependency.h"
#include "catalog/objectaddress.h"
#include "nodes/parsenodes.h"
#include "storage/lock.h"
#include "utils/relcache.h"

struct AlterTableUtilityContext;	/* avoid including tcop/utility.h here */


extern ObjectAddress DefineRelation(CreateStmt *stmt, char relkind, Oid ownerId,
									ObjectAddress *typaddress, const char *queryString);

extern TupleDesc BuildDescForRelation(const List *columns);

extern void RemoveRelations(DropStmt *drop);

extern Oid	AlterTableLookupRelation(AlterTableStmt *stmt, LOCKMODE lockmode);

extern void AlterTable(AlterTableStmt *stmt, LOCKMODE lockmode,
					   struct AlterTableUtilityContext *context);

extern LOCKMODE AlterTableGetLockLevel(List *cmds);

extern void ATExecChangeOwner(Oid relationOid, Oid newOwnerId, bool recursing, LOCKMODE lockmode);

extern void AlterTableInternal(Oid relid, List *cmds, bool recurse);

extern Oid	AlterTableMoveAll(AlterTableMoveAllStmt *stmt);

extern ObjectAddress AlterTableNamespace(AlterObjectSchemaStmt *stmt,
										 Oid *oldschema);

extern void AlterTableNamespaceInternal(Relation rel, Oid oldNspOid,
										Oid nspOid, ObjectAddresses *objsMoved);

extern void AlterRelationNamespaceInternal(Relation classRel, Oid relOid,
										   Oid oldNspOid, Oid newNspOid,
										   bool hasDependEntry,
										   ObjectAddresses *objsMoved);

extern void CheckTableNotInUse(Relation rel, const char *stmt);

extern void ExecuteTruncate(TruncateStmt *stmt);
extern void ExecuteTruncateGuts(List *explicit_rels,
								List *relids,
								List *relids_logged,
								DropBehavior behavior,
								bool restart_seqs,
								bool run_as_table_owner);

extern void SetRelationHasSubclass(Oid relationId, bool relhassubclass);

extern bool CheckRelationTableSpaceMove(Relation rel, Oid newTableSpaceId);
extern void SetRelationTableSpace(Relation rel, Oid newTableSpaceId,
								  RelFileNumber newRelFilenumber);

extern ObjectAddress renameatt(RenameStmt *stmt);

extern ObjectAddress RenameConstraint(RenameStmt *stmt);

extern ObjectAddress RenameRelation(RenameStmt *stmt);

extern void RenameRelationInternal(Oid myrelid,
								   const char *newrelname, bool is_internal,
								   bool is_index);

extern void ResetRelRewrite(Oid myrelid);

extern void find_composite_type_dependencies(Oid typeOid,
											 Relation origRelation,
											 const char *origTypeName);

extern void check_of_type(HeapTuple typetuple);

<<<<<<< HEAD
extern void createForeignKeyTriggers(Relation rel, Oid refRelOid,
						 Constraint *fkconstraint, Oid constraintOid,
						 Oid indexOid, bool create_action);
extern void CloneForeignKeyConstraints(Oid parentId, Oid relationId,
						   List **cloned);

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern void register_on_commit_action(Oid relid, OnCommitAction action);
extern void remove_on_commit_action(Oid relid);

extern void PreCommit_on_commit_actions(void);
extern void AtEOXact_on_commit_actions(bool isCommit);
extern void AtEOSubXact_on_commit_actions(bool isCommit,
										  SubTransactionId mySubid,
										  SubTransactionId parentSubid);

extern void RangeVarCallbackMaintainsTable(const RangeVar *relation,
										   Oid relId, Oid oldRelId,
										   void *arg);

extern void RangeVarCallbackOwnsRelation(const RangeVar *relation,
										 Oid relId, Oid oldRelId, void *arg);
extern bool PartConstraintImpliedByRelConstraint(Relation scanrel,
												 List *partConstraint);

/* POLAR */
extern List *recursive_relopts;		/* parsed from GUC  */
extern void polar_px_btbuild_update_pg_class(Relation heap, Relation index);
/* POLAR end */

#endif							/* TABLECMDS_H */
