/*-------------------------------------------------------------------------
 *
 * nodes.h
 *	  Definitions for tagged nodes.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/nodes/nodes.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODES_H
#define NODES_H

/*
 * The first field of every node is NodeTag. Each node created (with makeNode)
 * will have one of the following tags as the value of its first field.
 *
 * Note that inserting or deleting node types changes the numbers of other
 * node types later in the list.  This is no problem during development, since
 * the node numbers are never stored on disk.  But don't do it in a released
 * branch, because that would represent an ABI break for extensions.
 */
typedef enum NodeTag
{
	T_Invalid = 0,

<<<<<<< HEAD
	/*
	 * TAGS FOR EXECUTOR NODES (execnodes.h)
	 */
	T_IndexInfo,
	T_ExprContext,
	T_ProjectionInfo,
	T_JunkFilter,
	T_OnConflictSetState,
	T_ResultRelInfo,
	T_EState,
	T_TupleTableSlot,

	/*
	 * TAGS FOR PLAN NODES (plannodes.h)
	 */
	T_Plan,
	T_Result,
	/* POLAR px */
	T_Plan_Start = T_Result,
	/* POLAR end */
	T_ProjectSet,
	T_ModifyTable,
	T_Append,
	T_MergeAppend,
	T_RecursiveUnion,
	T_BitmapAnd,
	T_BitmapOr,
	T_Scan,
	T_SeqScan,
	T_SampleScan,
	T_IndexScan,
	T_IndexOnlyScan,
	T_BitmapIndexScan,
	T_BitmapHeapScan,
	T_TidScan,
	T_SubqueryScan,
	T_FunctionScan,
	T_ValuesScan,
	T_TableFuncScan,
	T_CteScan,
	T_NamedTuplestoreScan,
	T_WorkTableScan,
	T_ForeignScan,
	T_CustomScan,
	T_Join,
	T_NestLoop,
	T_MergeJoin,
	T_HashJoin,
	T_Material,
	T_Sort,
	T_Group,
	T_Agg,
	T_WindowAgg,
	T_Unique,
	T_Gather,
	T_GatherMerge,
	T_Hash,
	T_SetOp,
	T_LockRows,
	T_Limit,
	/* POLAR px */
	T_Sequence,
	T_DynamicSeqScan,
	T_ExternalScan,
	T_DynamicIndexScan,
	T_DynamicBitmapIndexScan,
	T_DynamicBitmapHeapScan,
	T_Motion,
	T_ShareInputScan,
	T_Repeat,
	T_SplitUpdate,
	T_RowTrigger,
	T_AssertOp,
	T_PartitionSelector,
	T_Plan_End,
	/* POLAR end */

	/* these aren't subclasses of Plan: */
	T_NestLoopParam,
	T_PlanRowMark,
	T_PartitionPruneInfo,
	T_PartitionedRelPruneInfo,
	T_PartitionPruneStepOp,
	T_PartitionPruneStepCombine,
	T_PlanInvalItem,

	/*
	 * TAGS FOR PLAN STATE NODES (execnodes.h)
	 *
	 * These should correspond one-to-one with Plan node types.
	 */
	T_PlanState,
	T_ResultState,
	T_ProjectSetState,
	T_ModifyTableState,
	T_AppendState,
	T_MergeAppendState,
	T_RecursiveUnionState,
	T_BitmapAndState,
	T_BitmapOrState,
	T_ScanState,
	T_SeqScanState,
	T_SampleScanState,
	T_IndexScanState,
	T_IndexOnlyScanState,
	T_BitmapIndexScanState,
	T_BitmapHeapScanState,
	T_TidScanState,
	T_SubqueryScanState,
	T_FunctionScanState,
	T_TableFuncScanState,
	T_ValuesScanState,
	T_CteScanState,
	T_NamedTuplestoreScanState,
	T_WorkTableScanState,
	T_ForeignScanState,
	T_CustomScanState,
	T_JoinState,
	T_NestLoopState,
	T_MergeJoinState,
	T_HashJoinState,
	T_MaterialState,
	T_SortState,
	T_GroupState,
	T_AggState,
	T_WindowAggState,
	T_UniqueState,
	T_GatherState,
	T_GatherMergeState,
	T_HashState,
	T_SetOpState,
	T_LockRowsState,
	T_LimitState,

	/*
	 * TAGS FOR PRIMITIVE NODES (primnodes.h)
	 */
	T_Alias,
	T_RangeVar,
	T_TableFunc,
	T_Expr,
	T_Var,
	T_Const,
	T_Param,
	T_Aggref,
	T_GroupingFunc,
	T_WindowFunc,
	T_ArrayRef,
	T_FuncExpr,
	T_NamedArgExpr,
	T_OpExpr,
	T_DistinctExpr,
	T_NullIfExpr,
	T_ScalarArrayOpExpr,
	T_BoolExpr,
	T_SubLink,
	T_SubPlan,
	T_AlternativeSubPlan,
	T_FieldSelect,
	T_FieldStore,
	T_RelabelType,
	T_CoerceViaIO,
	T_ArrayCoerceExpr,
	T_ConvertRowtypeExpr,
	T_CollateExpr,
	T_CaseExpr,
	T_CaseWhen,
	T_CaseTestExpr,
	T_ArrayExpr,
	T_RowExpr,
	T_RowCompareExpr,
	T_CoalesceExpr,
	T_MinMaxExpr,
	T_SQLValueFunction,
	T_XmlExpr,
	T_NullTest,
	T_BooleanTest,
	T_CoerceToDomain,
	T_CoerceToDomainValue,
	T_SetToDefault,
	T_CurrentOfExpr,
	T_NextValueExpr,
	T_InferenceElem,
	T_TargetEntry,
	T_RangeTblRef,
	T_JoinExpr,
	T_FromExpr,
	T_OnConflictExpr,
	T_IntoClause,

	/*
	 * TAGS FOR EXPRESSION STATE NODES (execnodes.h)
	 *
	 * ExprState represents the evaluation state for a whole expression tree.
	 * Most Expr-based plan nodes do not have a corresponding expression state
	 * node, they're fully handled within execExpr* - but sometimes the state
	 * needs to be shared with other parts of the executor, as for example
	 * with AggrefExprState, which nodeAgg.c has to modify.
	 */
	T_ExprState,
	T_AggrefExprState,
	T_WindowFuncExprState,
	T_SetExprState,
	T_SubPlanState,
	T_AlternativeSubPlanState,
	T_DomainConstraintState,

	/*
	 * TAGS FOR PLANNER NODES (relation.h)
	 */
	T_PlannerInfo,
	T_PlannerGlobal,
	T_RelOptInfo,
	T_IndexOptInfo,
	T_ForeignKeyOptInfo,
	T_ParamPathInfo,
	T_Path,
	T_IndexPath,
	T_BitmapHeapPath,
	T_BitmapAndPath,
	T_BitmapOrPath,
	T_TidPath,
	T_SubqueryScanPath,
	T_ForeignPath,
	T_CustomPath,
	T_NestPath,
	T_MergePath,
	T_HashPath,
	T_AppendPath,
	T_MergeAppendPath,
	T_ResultPath,
	T_MaterialPath,
	T_UniquePath,
	T_GatherPath,
	T_GatherMergePath,
	T_ProjectionPath,
	T_ProjectSetPath,
	T_SortPath,
	T_GroupPath,
	T_UpperUniquePath,
	T_AggPath,
	T_GroupingSetsPath,
	T_MinMaxAggPath,
	T_WindowAggPath,
	T_SetOpPath,
	T_RecursiveUnionPath,
	T_LockRowsPath,
	T_ModifyTablePath,
	T_LimitPath,
	/* these aren't subclasses of Path: */
	T_EquivalenceClass,
	T_EquivalenceMember,
	T_PathKey,
	T_PathTarget,
	T_RestrictInfo,
	T_PlaceHolderVar,
	T_SpecialJoinInfo,
	T_AppendRelInfo,
	T_PlaceHolderInfo,
	T_MinMaxAggInfo,
	T_PlannerParamItem,
	T_RollupData,
	T_GroupingSetData,
	T_StatisticExtInfo,

	/*
	 * TAGS FOR MEMORY NODES (memnodes.h)
	 */
	T_MemoryContext,
	T_AllocSetContext,
	T_SlabContext,
	T_GenerationContext,
	/* POLAR */
	T_ShmAllocSetContext,

	/*
	 * TAGS FOR VALUE NODES (value.h)
	 */
	T_Value,
	T_Integer,
	T_Float,
	T_String,
	T_BitString,
	T_Null,

	/*
	 * TAGS FOR LIST NODES (pg_list.h)
	 */
	T_List,
	T_IntList,
	T_OidList,

	/*
	 * TAGS FOR EXTENSIBLE NODES (extensible.h)
	 */
	T_ExtensibleNode,

	/*
	 * TAGS FOR STATEMENT NODES (mostly in parsenodes.h)
	 */
	T_RawStmt,
	T_Query,
	T_PlannedStmt,
	T_InsertStmt,
	T_DeleteStmt,
	T_UpdateStmt,
	T_SelectStmt,
	T_AlterTableStmt,
	T_AlterTableCmd,
	T_AlterDomainStmt,
	T_SetOperationStmt,
	T_GrantStmt,
	T_GrantRoleStmt,
	T_AlterDefaultPrivilegesStmt,
	T_ClosePortalStmt,
	T_ClusterStmt,
	T_CopyStmt,
	T_CreateStmt,
	T_DefineStmt,
	T_DropStmt,
	T_TruncateStmt,
	T_CommentStmt,
	T_FetchStmt,
	T_IndexStmt,
	T_CreateFunctionStmt,
	T_AlterFunctionStmt,
	T_DoStmt,
	T_RenameStmt,
	T_RuleStmt,
	T_NotifyStmt,
	T_ListenStmt,
	T_UnlistenStmt,
	T_TransactionStmt,
	T_ViewStmt,
	T_LoadStmt,
	T_CreateDomainStmt,
	T_CreatedbStmt,
	T_DropdbStmt,
	T_VacuumStmt,
	T_ExplainStmt,
	T_CreateTableAsStmt,
	T_CreateSeqStmt,
	T_AlterSeqStmt,
	T_VariableSetStmt,
	T_VariableShowStmt,
	T_DiscardStmt,
	T_CreateTrigStmt,
	T_CreatePLangStmt,
	T_CreateRoleStmt,
	T_AlterRoleStmt,
	T_DropRoleStmt,
	T_LockStmt,
	T_ConstraintsSetStmt,
	T_ReindexStmt,
	T_CheckPointStmt,
	T_CreateSchemaStmt,
	T_AlterDatabaseStmt,
	T_AlterDatabaseSetStmt,
	T_AlterRoleSetStmt,
	T_CreateConversionStmt,
	T_CreateCastStmt,
	T_CreateOpClassStmt,
	T_CreateOpFamilyStmt,
	T_AlterOpFamilyStmt,
	T_PrepareStmt,
	T_ExecuteStmt,
	T_DeallocateStmt,
	T_DeclareCursorStmt,
	T_CreateTableSpaceStmt,
	T_DropTableSpaceStmt,
	T_AlterObjectDependsStmt,
	T_AlterObjectSchemaStmt,
	T_AlterOwnerStmt,
	T_AlterOperatorStmt,
	T_DropOwnedStmt,
	T_ReassignOwnedStmt,
	T_CompositeTypeStmt,
	T_CreateEnumStmt,
	T_CreateRangeStmt,
	T_AlterEnumStmt,
	T_AlterTSDictionaryStmt,
	T_AlterTSConfigurationStmt,
	T_CreateFdwStmt,
	T_AlterFdwStmt,
	T_CreateForeignServerStmt,
	T_AlterForeignServerStmt,
	T_CreateUserMappingStmt,
	T_AlterUserMappingStmt,
	T_DropUserMappingStmt,
	T_AlterTableSpaceOptionsStmt,
	T_AlterTableMoveAllStmt,
	T_SecLabelStmt,
	T_CreateForeignTableStmt,
	T_ImportForeignSchemaStmt,
	T_CreateExtensionStmt,
	T_AlterExtensionStmt,
	T_AlterExtensionContentsStmt,
	T_CreateEventTrigStmt,
	T_AlterEventTrigStmt,
	T_RefreshMatViewStmt,
	T_ReplicaIdentityStmt,
	T_AlterSystemStmt,
	T_CreatePolicyStmt,
	T_AlterPolicyStmt,
	T_CreateTransformStmt,
	T_CreateAmStmt,
	T_CreatePublicationStmt,
	T_AlterPublicationStmt,
	T_CreateSubscriptionStmt,
	T_AlterSubscriptionStmt,
	T_DropSubscriptionStmt,
	T_CreateStatsStmt,
	T_AlterCollationStmt,
	T_CallStmt,

	/*
	 * TAGS FOR PARSE TREE NODES (parsenodes.h)
	 */
	T_A_Expr,
	T_ColumnRef,
	T_ParamRef,
	T_A_Const,
	T_FuncCall,
	T_A_Star,
	T_A_Indices,
	T_A_Indirection,
	T_A_ArrayExpr,
	T_ResTarget,
	T_MultiAssignRef,
	T_TypeCast,
	T_CollateClause,
	T_SortBy,
	T_WindowDef,
	T_RangeSubselect,
	T_RangeFunction,
	T_RangeTableSample,
	T_RangeTableFunc,
	T_RangeTableFuncCol,
	T_TypeName,
	T_ColumnDef,
	T_IndexElem,
	T_Constraint,
	T_DefElem,
	T_RangeTblEntry,
	T_RangeTblFunction,
	T_TableSampleClause,
	T_WithCheckOption,
	T_SortGroupClause,
	T_GroupingSet,
	T_WindowClause,
	T_ObjectWithArgs,
	T_AccessPriv,
	T_CreateOpClassItem,
	T_TableLikeClause,
	T_FunctionParameter,
	T_LockingClause,
	T_RowMarkClause,
	T_XmlSerialize,
	T_WithClause,
	T_InferClause,
	T_OnConflictClause,
	T_CommonTableExpr,
	T_RoleSpec,
	T_TriggerTransition,
	T_PartitionElem,
	T_PartitionSpec,
	T_PartitionBoundSpec,
	T_PartitionRangeDatum,
	T_PartitionCmd,
	T_VacuumRelation,

	/*
	 * TAGS FOR REPLICATION GRAMMAR PARSE NODES (replnodes.h)
	 */
	T_IdentifySystemCmd,
	T_BaseBackupCmd,
	T_CreateReplicationSlotCmd,
	T_DropReplicationSlotCmd,
	T_StartReplicationCmd,
	T_TimeLineHistoryCmd,
	T_SQLCmd,

	/*
	 * TAGS FOR RANDOM OTHER STUFF
	 *
	 * These are objects that aren't part of parse/plan/execute node tree
	 * structures, but we give them NodeTags anyway for identification
	 * purposes (usually because they are involved in APIs where we want to
	 * pass multiple object types through the same pointer).
	 */
	T_TriggerData,				/* in commands/trigger.h */
	T_EventTriggerData,			/* in commands/event_trigger.h */
	T_ReturnSetInfo,			/* in nodes/execnodes.h */
	T_WindowObjectData,			/* private in nodeWindowAgg.c */
	T_TIDBitmap,				/* in nodes/tidbitmap.h */
	T_InlineCodeBlock,			/* in nodes/parsenodes.h */
	T_FdwRoutine,				/* in foreign/fdwapi.h */
	T_IndexAmRoutine,			/* in access/amapi.h */
	T_TsmRoutine,				/* in access/tsmapi.h */
	T_ForeignKeyCacheInfo,		/* in utils/rel.h */
	T_CallContext,				/* in nodes/parsenodes.h */

	/* POLAR: DMA Command */
	T_PolarDMACommandStmt,
	/* POLAR end */

	/* POLAR px */
	T_PartitionBy,
	T_PartitionRangeItem,
	T_PartitionValuesSpec,
	T_AlterPartitionId,
	T_AlterPartitionCmd,
	T_InheritPartitionCmd,
	T_CreateFileSpaceStmt,
	T_FileSpaceEntry,
	T_DropFileSpaceStmt,
	T_TableValueExpr,
	T_DenyLoginInterval,
	T_DenyLoginPoint,
	T_AlterTypeStmt,
	T_ExpandStmtSpec,
	T_AOVacuumPhaseConfig,
	T_MotionState,
	T_TupleDescNode,
	T_SerializedParamExternData,
	T_PxProcess,
	T_SliceTable,
	T_CursorPosInfo,
	T_QueryDispatchDesc,
	T_Flow,
	T_GroupId,
	T_DMLActionExpr,
	T_PartSelectedExpr,
	T_PartDefaultExpr,
	T_PartBoundExpr,
	T_PartBoundInclusionExpr,
	T_PartBoundOpenExpr,
	T_PartListRuleExpr,
	T_PartListNullTestExpr,
	T_PxExplain_StatHdr,             /* in px_explain.c */
	T_PxPolicy,	/* in catalog/px_policy.h */
	T_SequenceState,
	T_ShareInputScanState,
	T_PartitionSelectorState,
	T_AssertOpState,
	/* POLAR px */
	T_SplitUpdateState,
	/* POLAR end */

	/* POLAR: Flashback table stmt */
	T_PolarFlashbackTableStmt,
	/* POLAR end */
=======
#include "nodes/nodetags.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
} NodeTag;

/*
 * pg_node_attr() - Used in node definitions to set extra information for
 * gen_node_support.pl
 *
 * Attributes can be attached to a node as a whole (place the attribute
 * specification on the first line after the struct's opening brace)
 * or to a specific field (place it at the end of that field's line).  The
 * argument is a comma-separated list of attributes.  Unrecognized attributes
 * cause an error.
 *
 * Valid node attributes:
 *
 * - abstract: Abstract types are types that cannot be instantiated but that
 *   can be supertypes of other types.  We track their fields, so that
 *   subtypes can use them, but we don't emit a node tag, so you can't
 *   instantiate them.
 *
 * - custom_copy_equal: Has custom implementations in copyfuncs.c and
 *   equalfuncs.c.
 *
 * - custom_read_write: Has custom implementations in outfuncs.c and
 *   readfuncs.c.
 *
 * - custom_query_jumble: Has custom implementation in queryjumblefuncs.c.
 *
 * - no_copy: Does not support copyObject() at all.
 *
 * - no_equal: Does not support equal() at all.
 *
 * - no_copy_equal: Shorthand for both no_copy and no_equal.
 *
 * - no_query_jumble: Does not support JumbleQuery() at all.
 *
 * - no_read: Does not support nodeRead() at all.
 *
 * - nodetag_only: Does not support copyObject(), equal(), jumbleQuery()
 *   outNode() or nodeRead().
 *
 * - special_read_write: Has special treatment in outNode() and nodeRead().
 *
 * - nodetag_number(VALUE): assign the specified nodetag number instead of
 *   an auto-generated number.  Typically this would only be used in stable
 *   branches, to give a newly-added node type a number without breaking ABI
 *   by changing the numbers of existing node types.
 *
 * Node types can be supertypes of other types whether or not they are marked
 * abstract: if a node struct appears as the first field of another struct
 * type, then it is the supertype of that type.  The no_copy, no_equal,
 * no_query_jumble and no_read node attributes are automatically inherited
 * from the supertype.  (Notice that nodetag_only does not inherit, so it's
 * not quite equivalent to a combination of other attributes.)
 *
 * Valid node field attributes:
 *
 * - array_size(OTHERFIELD): This field is a dynamically allocated array with
 *   size indicated by the mentioned other field.  The other field is either a
 *   scalar or a list, in which case the length of the list is used.
 *
 * - copy_as(VALUE): In copyObject(), replace the field's value with VALUE.
 *
 * - copy_as_scalar: In copyObject(), copy the field as a scalar value
 *   (e.g. a pointer) even if it is a node-type pointer.
 *
 * - equal_as_scalar: In equal(), compare the field as a scalar value
 *   even if it is a node-type pointer.
 *
 * - equal_ignore: Ignore the field for equality.
 *
 * - equal_ignore_if_zero: Ignore the field for equality if it is zero.
 *   (Otherwise, compare normally.)
 *
 * - query_jumble_ignore: Ignore the field for the query jumbling.  Note
 *   that typmod and collation information are usually irrelevant for the
 *   query jumbling.
 *
 * - query_jumble_location: Mark the field as a location to track.  This is
 *   only allowed for integer fields that include "location" in their name.
 *
 * - read_as(VALUE): In nodeRead(), replace the field's value with VALUE.
 *
 * - read_write_ignore: Ignore the field for read/write.  This is only allowed
 *   if the node type is marked no_read or read_as() is also specified.
 *
 * - write_only_relids, write_only_nondefault_pathtarget, write_only_req_outer:
 *   Special handling for Path struct; see there.
 *
 */
#define pg_node_attr(...)

/*
 * The first field of a node of any type is guaranteed to be the NodeTag.
 * Hence the type of any node can be gotten by casting it to Node. Declaring
 * a variable to be of Node * (instead of void *) can also facilitate
 * debugging.
 */
typedef struct Node
{
	NodeTag		type;
} Node;

#define nodeTag(nodeptr)		(((const Node*)(nodeptr))->type)

/*
 * newNode -
 *	  create a new node of the specified size and tag the node with the
 *	  specified tag.
 *
 * !WARNING!: Avoid using newNode directly. You should be using the
 *	  macro makeNode.  eg. to create a Query node, use makeNode(Query)
 */
static inline Node *
newNode(size_t size, NodeTag tag)
{
	Node	   *result;

	Assert(size >= sizeof(Node));	/* need the tag, at least */
	result = (Node *) palloc0(size);
	result->type = tag;

	return result;
}

#define makeNode(_type_)		((_type_ *) newNode(sizeof(_type_),T_##_type_))
#define NodeSetTag(nodeptr,t)	(((Node*)(nodeptr))->type = (t))

#define IsA(nodeptr,_type_)		(nodeTag(nodeptr) == T_##_type_)

/*
 * castNode(type, ptr) casts ptr to "type *", and if assertions are enabled,
 * verifies that the node has the appropriate type (using its nodeTag()).
 *
 * Use an inline function when assertions are enabled, to avoid multiple
 * evaluations of the ptr argument (which could e.g. be a function call).
 */
#ifdef USE_ASSERT_CHECKING
static inline Node *
castNodeImpl(NodeTag type, void *ptr)
{
	Assert(ptr == NULL || nodeTag(ptr) == type);
	return (Node *) ptr;
}
#define castNode(_type_, nodeptr) ((_type_ *) castNodeImpl(T_##_type_, nodeptr))
#else
#define castNode(_type_, nodeptr) ((_type_ *) (nodeptr))
#endif							/* USE_ASSERT_CHECKING */


/* ----------------------------------------------------------------
 *					  extern declarations follow
 * ----------------------------------------------------------------
 */

/*
 * nodes/{outfuncs.c,print.c}
 */
struct Bitmapset;				/* not to include bitmapset.h here */
struct StringInfoData;			/* not to include stringinfo.h here */

extern void outNode(struct StringInfoData *str, const void *obj);
extern void outToken(struct StringInfoData *str, const char *s);
extern void outBitmapset(struct StringInfoData *str,
						 const struct Bitmapset *bms);
extern void outDatum(struct StringInfoData *str, uintptr_t value,
					 int typlen, bool typbyval);
extern char *nodeToString(const void *obj);
extern char *nodeToStringWithLocations(const void *obj);
extern char *bmsToString(const struct Bitmapset *bms);

/*
 * nodes/{readfuncs.c,read.c}
 */
extern void *stringToNode(const char *str);
#ifdef DEBUG_NODE_TESTS_ENABLED
extern void *stringToNodeWithLocations(const char *str);
#endif
extern struct Bitmapset *readBitmapset(void);
extern uintptr_t readDatum(bool typbyval);
extern bool *readBoolCols(int numCols);
extern int *readIntCols(int numCols);
extern Oid *readOidCols(int numCols);
extern int16 *readAttrNumberCols(int numCols);

/*
 * nodes/outfast.c. This special version of nodeToString is only used by serializeNode.
 * It's a quick hack that allocates 8K buffer for StringInfo struct through initStringIinfoSizeOf
 */
extern char *nodeToBinaryStringFast(void *obj, int *length);

extern Node *readNodeFromBinaryString(const char *str, int len);

/*
 * nodes/copyfuncs.c
 */
extern void *copyObjectImpl(const void *from);

/* cast result back to argument type, if supported by compiler */
#ifdef HAVE_TYPEOF
#define copyObject(obj) ((__typeof__(obj)) copyObjectImpl(obj))
#else
#define copyObject(obj) copyObjectImpl(obj)
#endif

/*
 * nodes/equalfuncs.c
 */
extern bool equal(const void *a, const void *b);


/*
 * Typedef for parse location.  This is just an int, but this way
 * gen_node_support.pl knows which fields should get special treatment for
 * location values.
 *
 * -1 is used for unknown.
 */
typedef int ParseLoc;

/*
 * Typedefs for identifying qualifier selectivities, plan costs, and row
 * counts as such.  These are just plain "double"s, but declaring a variable
 * as Selectivity, Cost, or Cardinality makes the intent more obvious.
 *
 * These could have gone into plannodes.h or some such, but many files
 * depend on them...
 */
typedef double Selectivity;		/* fraction of tuples a qualifier will pass */
typedef double Cost;			/* execution cost (in page-access units) */
typedef double Cardinality;		/* (estimated) number of rows or other integer
								 * count */


/*
 * CmdType -
 *	  enums for type of operation represented by a Query or PlannedStmt
 *
 * This is needed in both parsenodes.h and plannodes.h, so put it here...
 */
typedef enum CmdType
{
	CMD_UNKNOWN,
	CMD_SELECT,					/* select stmt */
	CMD_UPDATE,					/* update stmt */
	CMD_INSERT,					/* insert stmt */
	CMD_DELETE,					/* delete stmt */
	CMD_MERGE,					/* merge stmt */
	CMD_UTILITY,				/* cmds like create, destroy, copy, vacuum,
								 * etc. */
	CMD_NOTHING,				/* dummy command for instead nothing rules
								 * with qual */
} CmdType;


/*
 * JoinType -
 *	  enums for types of relation joins
 *
 * JoinType determines the exact semantics of joining two relations using
 * a matching qualification.  For example, it tells what to do with a tuple
 * that has no match in the other relation.
 *
 * This is needed in both parsenodes.h and plannodes.h, so put it here...
 */
typedef enum JoinType
{
	/*
	 * The canonical kinds of joins according to the SQL JOIN syntax. Only
	 * these codes can appear in parser output (e.g., JoinExpr nodes).
	 */
	JOIN_INNER,					/* matching tuple pairs only */
	JOIN_LEFT,					/* pairs + unmatched LHS tuples */
	JOIN_FULL,					/* pairs + unmatched LHS + unmatched RHS */
	JOIN_RIGHT,					/* pairs + unmatched RHS tuples */

	/*
	 * Semijoins and anti-semijoins (as defined in relational theory) do not
	 * appear in the SQL JOIN syntax, but there are standard idioms for
	 * representing them (e.g., using EXISTS).  The planner recognizes these
	 * cases and converts them to joins.  So the planner and executor must
	 * support these codes.  NOTE: in JOIN_SEMI output, it is unspecified
	 * which matching RHS row is joined to.  In JOIN_ANTI output, the row is
	 * guaranteed to be null-extended.
	 */
	JOIN_SEMI,					/* 1 copy of each LHS row that has match(es) */
	JOIN_ANTI,					/* 1 copy of each LHS row that has no match */
	JOIN_RIGHT_SEMI,			/* 1 copy of each RHS row that has match(es) */
	JOIN_RIGHT_ANTI,			/* 1 copy of each RHS row that has no match */

	/*
	 * These codes are used internally in the planner, but are not supported
	 * by the executor (nor, indeed, by most of the planner).
	 */
	JOIN_UNIQUE_OUTER,			/* LHS path must be made unique */
	JOIN_UNIQUE_INNER,			/* RHS path must be made unique */

	JOIN_LASJ_NOTIN,			/* Left Anti Semi Join with Not-In semantics:		*/
								/*   If any NULL values are produced by inner side,  */
								/*   return no join results. Otherwise, same as LASJ */
} JoinType;

/*
 * OUTER joins are those for which pushed-down quals must behave differently
 * from the join's own quals.  This is in fact everything except INNER, SEMI
 * and RIGHT_SEMI joins.  However, this macro must also exclude the
 * JOIN_UNIQUE symbols since those are temporary proxies for what will
 * eventually be an INNER join.
 *
 * Note: semijoins are a hybrid case, but we choose to treat them as not
 * being outer joins.  This is okay principally because the SQL syntax makes
 * it impossible to have a pushed-down qual that refers to the inner relation
 * of a semijoin; so there is no strong need to distinguish join quals from
 * pushed-down quals.  This is convenient because for almost all purposes,
 * quals attached to a semijoin can be treated the same as innerjoin quals.
 */
#define IS_OUTER_JOIN(jointype) \
	(((1 << (jointype)) & \
	  ((1 << JOIN_LEFT) | \
	   (1 << JOIN_FULL) | \
	   (1 << JOIN_RIGHT) | \
	   (1 << JOIN_ANTI) | \
	   (1 << JOIN_RIGHT_ANTI))) != 0)

/*
 * DispatchMethod - PX dispatch method.
 *
 * There are currently three possibilties, an initial value of undetermined,
 * and a value for each of the ways the dispatch code implements.
 */
typedef enum DispatchMethod
{
	DISPATCH_UNDETERMINED = 0,	/* Used prior to determination. */
	DISPATCH_SEQUENTIAL,		/* Dispatch on entry postgres process only. */
	DISPATCH_PARALLEL			/* Dispatch on query executor and entry processes. */

} DispatchMethod;


/*
 * AggStrategy -
 *	  overall execution strategies for Agg plan nodes
 *
 * This is needed in both pathnodes.h and plannodes.h, so put it here...
 */
typedef enum AggStrategy
{
	AGG_PLAIN,					/* simple agg across all input rows */
	AGG_SORTED,					/* grouped agg, input must be sorted */
	AGG_HASHED,					/* grouped agg, use internal hashtable */
	AGG_MIXED,					/* grouped agg, hash and sort both used */
} AggStrategy;

/*
 * AggSplit -
 *	  splitting (partial aggregation) modes for Agg plan nodes
 *
 * This is needed in both pathnodes.h and plannodes.h, so put it here...
 */

/* Primitive options supported by nodeAgg.c: */
#define AGGSPLITOP_COMBINE		0x01	/* substitute combinefn for transfn */
#define AGGSPLITOP_SKIPFINAL	0x02	/* skip finalfn, return state as-is */
#define AGGSPLITOP_SERIALIZE	0x04	/* apply serialfn to output */
#define AGGSPLITOP_DESERIALIZE	0x08	/* apply deserialfn to input */

/* Supported operating modes (i.e., useful combinations of these options): */
typedef enum AggSplit
{
	/* Basic, non-split aggregation: */
	AGGSPLIT_SIMPLE = 0,
	/* Initial phase of partial aggregation, with serialization: */
	AGGSPLIT_INITIAL_SERIAL = AGGSPLITOP_SKIPFINAL | AGGSPLITOP_SERIALIZE,
	/* Final phase of partial aggregation, with deserialization: */
	AGGSPLIT_FINAL_DESERIAL = AGGSPLITOP_COMBINE | AGGSPLITOP_DESERIALIZE,
} AggSplit;

/* Test whether an AggSplit value selects each primitive option: */
#define DO_AGGSPLIT_COMBINE(as)		(((as) & AGGSPLITOP_COMBINE) != 0)
#define DO_AGGSPLIT_SKIPFINAL(as)	(((as) & AGGSPLITOP_SKIPFINAL) != 0)
#define DO_AGGSPLIT_SERIALIZE(as)	(((as) & AGGSPLITOP_SERIALIZE) != 0)
#define DO_AGGSPLIT_DESERIALIZE(as) (((as) & AGGSPLITOP_DESERIALIZE) != 0)

/*
 * SetOpCmd and SetOpStrategy -
 *	  overall semantics and execution strategies for SetOp plan nodes
 *
 * This is needed in both pathnodes.h and plannodes.h, so put it here...
 */
typedef enum SetOpCmd
{
	SETOPCMD_INTERSECT,
	SETOPCMD_INTERSECT_ALL,
	SETOPCMD_EXCEPT,
	SETOPCMD_EXCEPT_ALL,
} SetOpCmd;

typedef enum SetOpStrategy
{
	SETOP_SORTED,				/* input must be sorted */
	SETOP_HASHED,				/* use internal hashtable */
} SetOpStrategy;

/*
 * OnConflictAction -
 *	  "ON CONFLICT" clause type of query
 *
 * This is needed in both parsenodes.h and plannodes.h, so put it here...
 */
typedef enum OnConflictAction
{
	ONCONFLICT_NONE,			/* No "ON CONFLICT" clause */
	ONCONFLICT_NOTHING,			/* ON CONFLICT ... DO NOTHING */
	ONCONFLICT_UPDATE,			/* ON CONFLICT ... DO UPDATE */
} OnConflictAction;

/*
 * LimitOption -
 *	LIMIT option of query
 *
 * This is needed in both parsenodes.h and plannodes.h, so put it here...
 */
typedef enum LimitOption
{
	LIMIT_OPTION_COUNT,			/* FETCH FIRST... ONLY */
	LIMIT_OPTION_WITH_TIES,		/* FETCH FIRST... WITH TIES */
} LimitOption;

#endif							/* NODES_H */
