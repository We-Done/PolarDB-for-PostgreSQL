/*-------------------------------------------------------------------------
 *
 * smgr.h
 *	  storage manager switch public interface declarations.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/storage/smgr.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SMGR_H
#define SMGR_H

<<<<<<< HEAD
#include "access/xlog.h"
#include "fmgr.h"
#include "lib/ilist.h"
#include "port/atomics.h"
#include "storage/block.h"
#include "storage/condition_variable.h"
#include "storage/relfilenode.h"
#include "utils/guc.h"
#include "utils/hsearch.h"

/* GUCs. */
extern int smgr_shared_relations;
extern int smgr_pool_sweep_times;

/*
 * An object in shared memory tracks the size of the forks of a relation.
 */
struct SMgrSharedRelation
{
	RelFileNodeBackend rnode;
	BlockNumber		nblocks[MAX_FORKNUM + 1];
	pg_atomic_uint32 flags;
	pg_atomic_uint64 generation;		/* mapping change */
	int64 usecount;		/* used for clock sweep */
};

/* Definition private to smgr.c. */
struct SMgrSharedRelation;
typedef struct SMgrSharedRelation SMgrSharedRelation;

/* For now, we borrow the buffer managers array of locks.  XXX fixme */
/* Use SR_PARTITIONS instead of buffer managers locks */
#define SR_PARTITION_LOCK(hash) \
	(&MainLWLockArray[SR_LWLOCKS_OFFSET + (hash % SR_PARTITIONS)].lock)

/* Flags. */
#define SR_LOCKED					0x01
#define SR_VALID					0x02

/* Each forknum gets its own dirty, syncing and just dirtied bits. */
#define SR_DIRTY(forknum)			(0x04 << ((forknum) + (MAX_FORKNUM + 1) * 0))
#define SR_SYNCING(forknum)			(0x04 << ((forknum) + (MAX_FORKNUM + 1) * 1))
#define SR_JUST_DIRTIED(forknum)	(0x04 << ((forknum) + (MAX_FORKNUM + 1) * 2))

/* Masks to test if any forknum is currently dirty or syncing. */
#define SR_SYNCING_MASK				(((SR_SYNCING(MAX_FORKNUM + 1) - 1) ^ (SR_SYNCING(0) - 1)))
#define SR_DIRTY_MASK				(((SR_DIRTY(MAX_FORKNUM + 1) - 1) ^ (SR_DIRTY(0) - 1)))
#define SR_JUST_DIRTIED_MASK		(((SR_JUST_DIRTIED(MAX_FORKNUM + 1) - 1) ^ (SR_JUST_DIRTIED(0) - 1)))

/* Extract the lowest dirty forknum from flags (there must be at least one). */
#define SR_GET_ONE_DIRTY(mask)		pg_rightmost_one_pos32((((mask) >> 2) & (SR_DIRTY_MASK >> 2)))
=======
#include "lib/ilist.h"
#include "storage/block.h"
#include "storage/relfilelocator.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/*
 * smgr.c maintains a table of SMgrRelation objects, which are essentially
 * cached file handles.  An SMgrRelation is created (if not already present)
 * by smgropen(), and destroyed by smgrdestroy().  Note that neither of these
 * operations imply I/O, they just create or destroy a hashtable entry.  (But
 * smgrdestroy() may release associated resources, such as OS-level file
 * descriptors.)
 *
 * An SMgrRelation may be "pinned", to prevent it from being destroyed while
 * it's in use.  We use this to prevent pointers relcache to smgr from being
 * invalidated.  SMgrRelations that are not pinned are deleted at end of
 * transaction.
 */
typedef struct SMgrRelationData
{
	/* rlocator is the hashtable lookup key, so it must be first! */
	RelFileLocatorBackend smgr_rlocator;	/* relation physical identifier */

	/* pointer to shared object, valid if non-NULL and generation matches */
	SMgrSharedRelation *smgr_shared;
	uint64		smgr_shared_generation;

	/*
	 * The following fields are reset to InvalidBlockNumber upon a cache flush
	 * event, and hold the last known size for each fork.  This information is
	 * currently only reliable during recovery, since there is no cache
	 * invalidation for fork extension.
	 */
	BlockNumber smgr_targblock; /* current insertion target block */
	BlockNumber smgr_cached_nblocks[MAX_FORKNUM + 1];	/* last known size */

	/*
	 * The following fields are reset to InvalidBlockNumber upon a cache flush
	 * event, and hold the last known size for each fork.  This information is
	 * currently only reliable during recovery, since there is no cache
	 * invalidation for fork extension.
	 */
	BlockNumber smgr_cached_nblocks[MAX_FORKNUM + 1];	/* last known size */

	/* additional public fields may someday exist here */

	/*
	 * Fields below here are intended to be private to smgr.c and its
	 * submodules.  Do not touch them from elsewhere.
	 */
	int			smgr_which;		/* storage manager selector */

	/*
	 * for md.c; per-fork arrays of the number of open segments
	 * (md_num_open_segs) and the segments themselves (md_seg_fds).
	 */
	int			md_num_open_segs[MAX_FORKNUM + 1];
	struct _MdfdVec *md_seg_fds[MAX_FORKNUM + 1];

<<<<<<< HEAD
	/* if unowned, list link in list of all unowned SMgrRelations */
	dlist_node	node;

	/* POLAR: bulk extend */
	bool polar_flag_for_bulk_extend[MAX_FORKNUM + 1];
	BlockNumber polar_nblocks_faked_for_bulk_extend[MAX_FORKNUM + 1];
	/* POLAR end */

=======
	/*
	 * Pinning support.  If unpinned (ie. pincount == 0), 'node' is a list
	 * link in list of all unpinned SMgrRelations.
	 */
	int			pincount;
	dlist_node	node;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
} SMgrRelationData;

typedef SMgrRelationData *SMgrRelation;

typedef struct SMgrSharedRelationPool
{
	ConditionVariable sync_flags_cleared;
	pg_atomic_uint32 next;
	SMgrSharedRelation objects[FLEXIBLE_ARRAY_MEMBER];
} SMgrSharedRelationPool;

#define SmgrIsTemp(smgr) \
	RelFileLocatorBackendIsTemp((smgr)->smgr_rlocator)

extern size_t smgr_shmem_size(void);
extern void smgr_shmem_init(void);

extern void smgrinit(void);
extern SMgrRelation smgropen(RelFileLocator rlocator, ProcNumber backend);
extern bool smgrexists(SMgrRelation reln, ForkNumber forknum);
extern void smgrpin(SMgrRelation reln);
extern void smgrunpin(SMgrRelation reln);
extern void smgrclose(SMgrRelation reln);
extern void smgrdestroyall(void);
extern void smgrrelease(SMgrRelation reln);
extern void smgrreleaseall(void);
extern void smgrreleaserellocator(RelFileLocatorBackend rlocator);
extern void smgrcreate(SMgrRelation reln, ForkNumber forknum, bool isRedo);
extern void smgrdosyncall(SMgrRelation *rels, int nrels);
extern void smgrdounlinkall(SMgrRelation *rels, int nrels, bool isRedo);
extern void smgrextend(SMgrRelation reln, ForkNumber forknum,
<<<<<<< HEAD
		   BlockNumber blocknum, char *buffer, bool skipFsync);
extern void smgrprefetch(SMgrRelation reln, ForkNumber forknum,
			 BlockNumber blocknum);
/* POLAR: bulk read */
extern void polar_smgrbulkread(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
							   int blockCount, char *buffer);
extern void polar_smgrbulkwrite(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
							int blockCount, char *buffer, bool skipFsync);
/* POLAR: end */
extern void smgrread(SMgrRelation reln, ForkNumber forknum,
		 BlockNumber blocknum, char *buffer);
extern void smgrwrite(SMgrRelation reln, ForkNumber forknum,
		  BlockNumber blocknum, char *buffer, bool skipFsync);
=======
					   BlockNumber blocknum, const void *buffer, bool skipFsync);
extern void smgrzeroextend(SMgrRelation reln, ForkNumber forknum,
						   BlockNumber blocknum, int nblocks, bool skipFsync);
extern bool smgrprefetch(SMgrRelation reln, ForkNumber forknum,
						 BlockNumber blocknum, int nblocks);
extern void smgrreadv(SMgrRelation reln, ForkNumber forknum,
					  BlockNumber blocknum,
					  void **buffers, BlockNumber nblocks);
extern void smgrwritev(SMgrRelation reln, ForkNumber forknum,
					   BlockNumber blocknum,
					   const void **buffers, BlockNumber nblocks,
					   bool skipFsync);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern void smgrwriteback(SMgrRelation reln, ForkNumber forknum,
						  BlockNumber blocknum, BlockNumber nblocks);
extern BlockNumber smgrnblocks(SMgrRelation reln, ForkNumber forknum);
<<<<<<< HEAD
extern void smgrtruncate(SMgrRelation reln, ForkNumber forknum,
			 BlockNumber nblocks);
extern void polar_smgrtruncate_no_drop_buffer(SMgrRelation reln, ForkNumber forknum,
			 BlockNumber nblocks);
=======
extern BlockNumber smgrnblocks_cached(SMgrRelation reln, ForkNumber forknum);
extern void smgrtruncate(SMgrRelation reln, ForkNumber *forknum,
						 int nforks, BlockNumber *nblocks);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern void smgrimmedsync(SMgrRelation reln, ForkNumber forknum);
extern void smgrregistersync(SMgrRelation reln, ForkNumber forknum);
extern void AtEOXact_SMgr(void);
<<<<<<< HEAD
extern BlockNumber polar_smgrnblocks_use_file_cache(SMgrRelation reln, ForkNumber forknum);

/* POLAR: bulk extend */
extern void smgrextendbatch(SMgrRelation reln, ForkNumber forknum,
		   BlockNumber blocknum, int blockCount, char *buffer, bool skipFsync);
extern void polar_smgr_init_bulk_extend(SMgrRelation reln, ForkNumber forknum);
extern void polar_smgr_clear_bulk_extend(SMgrRelation reln, ForkNumber forknum);
static inline bool polar_smgr_being_bulk_extend(SMgrRelation reln, ForkNumber forknum)
{ return  true == reln->polar_flag_for_bulk_extend[forknum]; }
extern void polar_dropdb_smgr_shared_relation_pool(Oid dbid);
extern void smgr_drop_sr(RelFileNodeBackend *rnode);
extern SMgrSharedRelationPool* polar_get_smgr_shared_pool(void);
extern HTAB* polar_get_smgr_mapping_table(void);
extern uint32 smgr_lock_sr(SMgrSharedRelation *sr);
extern void smgr_unlock_sr(SMgrSharedRelation *sr, uint32 flags);
extern void polar_release_held_smgr_cache(void);
/* POLAR end*/

/* internals: move me elsewhere -- ay 7/94 */

/* in md.c */
extern void mdinit(void);
extern void mdclose(SMgrRelation reln, ForkNumber forknum);
extern void mdcreate(SMgrRelation reln, ForkNumber forknum, bool isRedo);
extern bool mdexists(SMgrRelation reln, ForkNumber forknum);
extern void mdunlink(RelFileNodeBackend rnode, ForkNumber forknum, bool isRedo);
extern void mdextend(SMgrRelation reln, ForkNumber forknum,
		 BlockNumber blocknum, char *buffer, bool skipFsync);
extern void mdprefetch(SMgrRelation reln, ForkNumber forknum,
		   BlockNumber blocknum);
/* POLAR: bulk read */
extern void polar_mdbulkread(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
							 int blockCount, char *buffer);
extern void polar_mdbulkwrite(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
		int blockCount, char *buffer, bool skipFsync);
/* POLAR end */
extern void mdread(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
	   char *buffer);
extern void mdwrite(SMgrRelation reln, ForkNumber forknum,
		BlockNumber blocknum, char *buffer, bool skipFsync);
extern void mdwriteback(SMgrRelation reln, ForkNumber forknum,
			BlockNumber blocknum, BlockNumber nblocks);
extern BlockNumber mdnblocks(SMgrRelation reln, ForkNumber forknum, bool polar_use_file_cache);
extern void mdtruncate(SMgrRelation reln, ForkNumber forknum,
		   BlockNumber nblocks);
extern void mdimmedsync(SMgrRelation reln, ForkNumber forknum);
extern void mdpreckpt(void);
extern void mdsync(void);
extern void mdpostckpt(void);

extern void SetForwardFsyncRequests(void);
extern void RememberFsyncRequest(RelFileNode rnode, ForkNumber forknum,
					 BlockNumber segno);
extern void ForgetRelationFsyncRequests(RelFileNode rnode, ForkNumber forknum);
extern void ForgetDatabaseFsyncRequests(Oid dbid);
extern void DropRelationFiles(RelFileNode *delrels, int ndelrels, bool isRedo);
=======
extern bool ProcessBarrierSmgrRelease(void);

static inline void
smgrread(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
		 void *buffer)
{
	smgrreadv(reln, forknum, blocknum, &buffer, 1);
}

static inline void
smgrwrite(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
		  const void *buffer, bool skipFsync)
{
	smgrwritev(reln, forknum, blocknum, &buffer, 1, skipFsync);
}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/* POLAR */
extern void mdextendbatch(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
		 int blockCount, char *buffer, bool skipFsync);
extern bool polar_need_skip_request(BlockNumber segno);

/* POLAR: smgr shared cache */
extern BlockNumber polar_smgrnblocks_cache(SMgrRelation reln, ForkNumber forknum);
extern BlockNumber polar_nblocks_cache_search_and_update(SMgrRelation reln, ForkNumber forknum, bool need_update);

static inline bool polar_is_valid_rel_node(RelFileNode rnode)
{
	return rnode.spcNode != 0 && rnode.dbNode != 0 && rnode.relNode != 0;
}

static inline bool polar_enabled_nblock_cache(void)
{
	return polar_nblocks_cache_mode != POLAR_NBLOCKS_CACHE_OFF_MODE;
}

static inline bool polar_enabled_nblock_cache_all(void)
{
	return polar_nblocks_cache_mode == POLAR_NBLOCKS_CACHE_ALL_MODE;
}

static inline bool
polar_enabled_nblock_bitmapscan(void)
{
	return polar_nblocks_cache_mode == POLAR_NBLOCKS_CACHE_ALL_MODE ||
		polar_nblocks_cache_mode == POLAR_NBLOCKS_CACHE_BITMAPSCAN_MODE;
}

static inline bool
polar_enabled_nblock_scan(void)
{
	return polar_nblocks_cache_mode == POLAR_NBLOCKS_CACHE_ALL_MODE ||
		polar_nblocks_cache_mode == POLAR_NBLOCKS_CACHE_SCAN_MODE;
}

/*
 * POLAR: Enabled smgr cache update
 * 1. If not use smgr cache, disable.
 * 2. Temp tables, disable.
 * 3. For replica, if no use smgr cache, disable.
 * 4. For standby, if no use smgr cache, disable.
 */
#define POLAR_DISABLE_SR_UPDATE(reln) \
	(!polar_enabled_nblock_cache() || RelFileNodeBackendIsTemp(reln->smgr_rnode) || \
	(polar_in_replica_mode() && !polar_enable_replica_use_smgr_cache) || \
	(polar_is_standby() && !polar_enable_standby_use_smgr_cache))

/*
 * POLAR: For some scenes, we don't support nblocks cache.
 * 1. If it is in bulk extend, smgrextend will not update the smgr cache. We
 * should get blocknumber from polar_nblocks_faked_for_bulk_extend.
 * 2. Temp tables don't use relation size cache. They are released after the
 * session exits. It is complicated to drop the relation size cache of temp
 * tables.
 * 3. For replica, we disable smgr cache except polar_enable_replica_use_smgr_cache
 * set to on.
 * 4. For standby, we disable smgr cache except polar_enable_replica_use_smgr_cache
 * set to on.
 */
static inline bool
polar_nouse_nblocks_cache(SMgrRelation reln, ForkNumber forknum)
{
	return (polar_smgr_being_bulk_extend(reln, forknum) == true ||
		    RelFileNodeBackendIsTemp(reln->smgr_rnode) ||
		    (polar_in_replica_mode() && !polar_enable_replica_use_smgr_cache) ||
			(polar_is_standby() && !polar_enable_standby_use_smgr_cache));
}
#endif							/* SMGR_H */
