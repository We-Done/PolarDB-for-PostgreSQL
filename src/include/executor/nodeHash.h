/*-------------------------------------------------------------------------
 *
 * nodeHash.h
 *	  prototypes for nodeHash.c
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeHash.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEHASH_H
#define NODEHASH_H

#include "access/parallel.h"
#include "nodes/execnodes.h"

struct SharedHashJoinBatch;

extern HashState *ExecInitHash(Hash *node, EState *estate, int eflags);
extern Node *MultiExecHash(HashState *node);
extern void ExecEndHash(HashState *node);
extern void ExecReScanHash(HashState *node);

extern HashJoinTable ExecHashTableCreate(HashState *state);
extern void ExecParallelHashTableAlloc(HashJoinTable hashtable,
									   int batchno);
extern void ExecHashTableDestroy(HashJoinTable hashtable);
extern void ExecHashTableDetach(HashJoinTable hashtable);
extern void ExecHashTableDetachBatch(HashJoinTable hashtable);
extern void ExecParallelHashTableSetCurrentBatch(HashJoinTable hashtable,
												 int batchno);

extern void ExecHashTableInsert(HashJoinTable hashtable,
								TupleTableSlot *slot,
								uint32 hashvalue);
extern void ExecParallelHashTableInsert(HashJoinTable hashtable,
										TupleTableSlot *slot,
										uint32 hashvalue);
<<<<<<< HEAD
extern bool ExecHashGetHashValue(HashJoinTable hashtable,
					 ExprContext *econtext,
					 List *hashkeys,
					 bool outer_tuple,
					 bool keep_nulls,
					 uint32 *hashvalue,
					 bool *hashkeys_null/* POLAR px */);
=======
extern void ExecParallelHashTableInsertCurrentBatch(HashJoinTable hashtable,
													TupleTableSlot *slot,
													uint32 hashvalue);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern void ExecHashGetBucketAndBatch(HashJoinTable hashtable,
									  uint32 hashvalue,
									  int *bucketno,
									  int *batchno);
extern bool ExecScanHashBucket(HashJoinState *hjstate, ExprContext *econtext);
extern bool ExecParallelScanHashBucket(HashJoinState *hjstate, ExprContext *econtext);
extern void ExecPrepHashTableForUnmatched(HashJoinState *hjstate);
extern bool ExecParallelPrepHashTableForUnmatched(HashJoinState *hjstate);
extern bool ExecScanHashTableForUnmatched(HashJoinState *hjstate,
										  ExprContext *econtext);
extern bool ExecParallelScanHashTableForUnmatched(HashJoinState *hjstate,
												  ExprContext *econtext);
extern void ExecHashTableReset(HashJoinTable hashtable);
extern void ExecHashTableResetMatchFlags(HashJoinTable hashtable);
extern void ExecChooseHashTableSize(double ntuples, int tupwidth, bool useskew,
									bool try_combined_hash_mem,
									int parallel_workers,
									size_t *space_allowed,
									int *numbuckets,
									int *numbatches,
									int *num_skew_mcvs);
extern int	ExecHashGetSkewBucket(HashJoinTable hashtable, uint32 hashvalue);
extern void ExecHashEstimate(HashState *node, ParallelContext *pcxt);
extern void ExecHashInitializeDSM(HashState *node, ParallelContext *pcxt);
extern void ExecHashInitializeWorker(HashState *node, ParallelWorkerContext *pwcxt);
extern void ExecHashRetrieveInstrumentation(HashState *node);
extern void ExecShutdownHash(HashState *node);
<<<<<<< HEAD
extern void ExecHashGetInstrumentation(HashInstrumentation *instrument,
						   HashJoinTable hashtable);
/* POLAR px */
extern void polar_ExecHashTableExplainInit(HashState *hashState, HashJoinState *hjstate,
                                     HashJoinTable  hashtable);
extern void polar_ExecHashTableExplainBatchEnd(HashState *hashState, HashJoinTable hashtable);
/* POLAR end */
=======
extern void ExecHashAccumInstrumentation(HashInstrumentation *instrument,
										 HashJoinTable hashtable);

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#endif							/* NODEHASH_H */
