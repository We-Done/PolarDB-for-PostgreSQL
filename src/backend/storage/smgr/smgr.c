/*-------------------------------------------------------------------------
 *
 * smgr.c
 *	  public interface routines to storage manager switch.
 *
 * All file system operations on relations dispatch through these routines.
 * An SMgrRelation represents physical on-disk relation files that are open
 * for reading and writing.
 *
 * When a relation is first accessed through the relation cache, the
 * corresponding SMgrRelation entry is opened by calling smgropen(), and the
 * reference is stored in the relation cache entry.
 *
 * Accesses that don't go through the relation cache open the SMgrRelation
 * directly.  That includes flushing buffers from the buffer cache, as well as
 * all accesses in auxiliary processes like the checkpointer or the WAL redo
 * in the startup process.
 *
 * Operations like CREATE, DROP, ALTER TABLE also hold SMgrRelation references
 * independent of the relation cache.  They need to prepare the physical files
 * before updating the relation cache.
 *
 * There is a hash table that holds all the SMgrRelation entries in the
 * backend.  If you call smgropen() twice for the same rel locator, you get a
 * reference to the same SMgrRelation. The reference is valid until the end of
 * transaction.  This makes repeated access to the same relation efficient,
 * and allows caching things like the relation size in the SMgrRelation entry.
 *
 * At end of transaction, all SMgrRelation entries that haven't been pinned
 * are removed.  An SMgrRelation can hold kernel file system descriptors for
 * the underlying files, and we'd like to close those reasonably soon if the
 * file gets deleted.  The SMgrRelations references held by the relcache are
 * pinned to prevent them from being closed.
 *
 * There is another mechanism to close file descriptors early:
 * PROCSIGNAL_BARRIER_SMGRRELEASE.  It is a request to immediately close all
 * file descriptors.  Upon receiving that signal, the backend closes all file
 * descriptors held open by SMgrRelations, but because it can happen in the
 * middle of a transaction, we cannot destroy the SMgrRelation objects
 * themselves, as there could pointers to them in active use.  See
 * smgrrelease() and smgrreleaseall().
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/smgr/smgr.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

<<<<<<< HEAD
#include "access/xlog.h"
#include "pgstat.h"
#include "commands/tablespace.h"
#include "lib/ilist.h"
#include "miscadmin.h"
#include "port/atomics.h"
#include "port/pg_bitutils.h"
=======
#include "access/xlogutils.h"
#include "lib/ilist.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#include "storage/bufmgr.h"
#include "storage/shmem.h"
#include "storage/ipc.h"
<<<<<<< HEAD
#include "storage/lwlock.h"
#include "storage/s_lock.h"
=======
#include "storage/md.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#include "storage/smgr.h"
#include "utils/hsearch.h"
#include "utils/inval.h"

/* POLAR */
#include "access/polar_logindex_redo.h"
#include "utils/guc.h"
#include "utils/rel.h"

/*
 * An entry in the hash table that allows us to look up objects in the
 * SMgrSharedRelation pool by rnode (+ backend).
 */
typedef struct SMgrSharedRelationMapping
{
	RelFileNodeBackend rnode;
	int				index;
} SMgrSharedRelationMapping;

static SMgrSharedRelationPool *sr_pool;
static HTAB *sr_mapping_table;

/*
 * This struct of function pointers defines the API between smgr.c and
 * any individual storage manager module.  Note that smgr subfunctions are
 * generally expected to report problems via elog(ERROR).  An exception is
 * that smgr_unlink should use elog(WARNING), rather than erroring out,
 * because we normally unlink relations during post-commit/abort cleanup,
 * and so it's too late to raise an error.  Also, various conditions that
 * would normally be errors should be allowed during bootstrap and/or WAL
 * recovery --- see comments in md.c for details.
 */
typedef struct f_smgr
{
	void		(*smgr_init) (void);	/* may be NULL */
	void		(*smgr_shutdown) (void);	/* may be NULL */
	void		(*smgr_open) (SMgrRelation reln);
	void		(*smgr_close) (SMgrRelation reln, ForkNumber forknum);
	void		(*smgr_create) (SMgrRelation reln, ForkNumber forknum,
								bool isRedo);
	bool		(*smgr_exists) (SMgrRelation reln, ForkNumber forknum);
	void		(*smgr_unlink) (RelFileLocatorBackend rlocator, ForkNumber forknum,
								bool isRedo);
	void		(*smgr_extend) (SMgrRelation reln, ForkNumber forknum,
<<<<<<< HEAD
								BlockNumber blocknum, char *buffer, bool skipFsync);
	void		(*smgr_extendbatch) (SMgrRelation reln, ForkNumber forknum,
								BlockNumber blocknum, int blockCount, char *buffer, bool skipFsync);
	void		(*smgr_prefetch) (SMgrRelation reln, ForkNumber forknum,
								  BlockNumber blocknum);
	/* POLAR: bulk read */
	void        (*polar_smgr_bulkread) (SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
										int blockCount, char *buffer);
	/* POLAR end */
	void        (*polar_smgr_bulkwrite) (SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
										int blockCount, char *buffer, bool skipFsync);
	void		(*smgr_read) (SMgrRelation reln, ForkNumber forknum,
							  BlockNumber blocknum, char *buffer);
	void		(*smgr_write) (SMgrRelation reln, ForkNumber forknum,
							   BlockNumber blocknum, char *buffer, bool skipFsync);
=======
								BlockNumber blocknum, const void *buffer, bool skipFsync);
	void		(*smgr_zeroextend) (SMgrRelation reln, ForkNumber forknum,
									BlockNumber blocknum, int nblocks, bool skipFsync);
	bool		(*smgr_prefetch) (SMgrRelation reln, ForkNumber forknum,
								  BlockNumber blocknum, int nblocks);
	void		(*smgr_readv) (SMgrRelation reln, ForkNumber forknum,
							   BlockNumber blocknum,
							   void **buffers, BlockNumber nblocks);
	void		(*smgr_writev) (SMgrRelation reln, ForkNumber forknum,
								BlockNumber blocknum,
								const void **buffers, BlockNumber nblocks,
								bool skipFsync);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	void		(*smgr_writeback) (SMgrRelation reln, ForkNumber forknum,
								   BlockNumber blocknum, BlockNumber nblocks);
	BlockNumber (*smgr_nblocks) (SMgrRelation reln, ForkNumber forknum, bool polar_cache_size);
	void		(*smgr_truncate) (SMgrRelation reln, ForkNumber forknum,
								  BlockNumber nblocks);
	void		(*smgr_immedsync) (SMgrRelation reln, ForkNumber forknum);
	void		(*smgr_registersync) (SMgrRelation reln, ForkNumber forknum);
} f_smgr;

static const f_smgr smgrsw[] = {
	/* magnetic disk */
	{
		.smgr_init = mdinit,
		.smgr_shutdown = NULL,
<<<<<<< HEAD
=======
		.smgr_open = mdopen,
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		.smgr_close = mdclose,
		.smgr_create = mdcreate,
		.smgr_exists = mdexists,
		.smgr_unlink = mdunlink,
		.smgr_extend = mdextend,
<<<<<<< HEAD
		.smgr_extendbatch = mdextendbatch,
		.smgr_prefetch = mdprefetch,
		/* POLAR: bulk read */
		.polar_smgr_bulkread = polar_mdbulkread,
		.polar_smgr_bulkwrite = polar_mdbulkwrite,
		/* POLAR end */
		.smgr_read = mdread,
		.smgr_write = mdwrite,
=======
		.smgr_zeroextend = mdzeroextend,
		.smgr_prefetch = mdprefetch,
		.smgr_readv = mdreadv,
		.smgr_writev = mdwritev,
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		.smgr_writeback = mdwriteback,
		.smgr_nblocks = mdnblocks,
		.smgr_truncate = mdtruncate,
		.smgr_immedsync = mdimmedsync,
<<<<<<< HEAD
		.smgr_pre_ckpt = mdpreckpt,
		.smgr_sync = mdsync,
		.smgr_post_ckpt = mdpostckpt
=======
		.smgr_registersync = mdregistersync,
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
};

static const int NSmgr = lengthof(smgrsw);

/*
 * Each backend has a hashtable that stores all extant SMgrRelation objects.
 * In addition, "unpinned" SMgrRelation objects are chained together in a list.
 */
static HTAB *SMgrRelationHash = NULL;

<<<<<<< HEAD
static dlist_head	unowned_relns;

/* local function prototypes */
static void smgrshutdown(int code, Datum arg);
=======
static dlist_head unpinned_relns;

/* local function prototypes */
static void smgrshutdown(int code, Datum arg);
static void smgrdestroy(SMgrRelation reln);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/* GUCs. */
int smgr_shared_relations = 10000;
int smgr_pool_sweep_times = 32;

static SMgrSharedRelation *held_sr = NULL;

/*
 * Try to get the size of a relation's fork without locking.
 */
static BlockNumber
smgrnblocks_fast(SMgrRelation reln, ForkNumber forknum)
{
	SMgrSharedRelation *sr = reln->smgr_shared;
	BlockNumber result;

	if (sr)
	{
		pg_read_barrier();

		/* We can load int-sized values atomically without special measures. */
		Assert(sizeof(sr->nblocks[forknum]) == sizeof(uint32));
		result = sr->nblocks[forknum];

		/*
		 * With a read barrier between the loads, we can check that the object
		 * still refers to the same rnode before trusting the answer.
		 */
		pg_read_barrier();

		if (pg_atomic_read_u64(&sr->generation) == reln->smgr_shared_generation)
		{
			/* no necessary to use a atomic operation, usecount can be imprecisely */
			if (sr->usecount < smgr_pool_sweep_times)
				sr->usecount++;
			return result;
		}

		/*
		 * The generation doesn't match, the shared relation must have been
		 * evicted since we got a pointer to it.  We'll need to do more work.
		 */
		reln->smgr_shared = NULL;
	}

	return InvalidBlockNumber;
}

/*
 * Try to get the size of a relation's fork by looking it up in the mapping
 * table with a shared lock.  This will succeed if the SMgrRelation already
 * exists.
 */
static BlockNumber
smgrnblocks_shared(SMgrRelation reln, ForkNumber forknum)
{
	SMgrSharedRelationMapping *mapping;
	SMgrSharedRelation *sr;
	uint32	hash;
	LWLock *mapping_lock;
	BlockNumber result = InvalidBlockNumber;

	hash = get_hash_value(sr_mapping_table, &reln->smgr_rnode);
	mapping_lock = SR_PARTITION_LOCK(hash);

	LWLockAcquire(mapping_lock, LW_SHARED);
	mapping = hash_search_with_hash_value(sr_mapping_table,
										  &reln->smgr_rnode,
										  hash,
										  HASH_FIND,
										  NULL);
	if (mapping)
	{
		sr = &sr_pool->objects[mapping->index];
		result = sr->nblocks[forknum];

		/* no necessary to use a atomic operation, usecount can be imprecisely */
		if (sr->usecount < smgr_pool_sweep_times)
			sr->usecount++;

		/* We can take the fast path until this SR is eventually evicted. */
		reln->smgr_shared = sr;
		reln->smgr_shared_generation = pg_atomic_read_u64(&sr->generation);
	}
	LWLockRelease(mapping_lock);

	return result;
}

/*
 * Lock a SMgrSharedRelation.  The lock is a spinlock that should be held for
 * only a few instructions.  The return value is the current set of flags,
 * which may be modified and then passed to smgr_unlock_sr() to be atomically
 * when the lock is released.
 */
uint32
smgr_lock_sr(SMgrSharedRelation *sr)
{
	SpinDelayStatus delayStatus;
	START_CRIT_SECTION();
	init_local_spin_delay(&delayStatus);

	for (;;)
	{
		uint32	old_flags = pg_atomic_read_u32(&sr->flags);
		uint32	flags;

		if (!(old_flags & SR_LOCKED))
		{
			flags = old_flags | SR_LOCKED;
			if (pg_atomic_compare_exchange_u32(&sr->flags, &old_flags, flags))
			{
				held_sr = sr;
				finish_spin_delay(&delayStatus);
				return flags;
			}
		}
		perform_spin_delay(&delayStatus);
	}
	finish_spin_delay(&delayStatus);
	return 0; /* unreachable */
}

/*
 * Unlock a SMgrSharedRelation, atomically updating its flags at the same
 * time.
 */
void
smgr_unlock_sr(SMgrSharedRelation *sr, uint32 flags)
{
	pg_write_barrier();
	held_sr = NULL;
	pg_atomic_write_u32(&sr->flags, flags & ~SR_LOCKED);
	END_CRIT_SECTION();
}

void
polar_release_held_smgr_cache(void)
{
	if (held_sr)
		smgr_unlock_sr(held_sr, 0);
}

/* LRU: sweep to find a sr to use. Just lock the sr when it returns */
static SMgrSharedRelation *
polar_smgr_pool_sweep(void)
{
	SMgrSharedRelation *sr;
	uint32 index;
	uint32 flags;
	int sr_used_count = 0;

	for (;;)
	{
		/* Lock the next one in clock-hand order. */
		index = pg_atomic_fetch_add_u32(&sr_pool->next, 1) % smgr_shared_relations;
		sr = &sr_pool->objects[index];
		flags = smgr_lock_sr(sr);
		if (--(sr->usecount) <= 0)
		{
			elog(DEBUG5, "find block cache in sweep cache, use it");
			return sr;
		}
		if (++sr_used_count >= smgr_pool_sweep_times)
		{
			elog(DEBUG5, "all the scanned block caches are used frequently, use a random one");
			/* Unlock the old sr */
			smgr_unlock_sr(sr, flags);
			sr = &sr_pool->objects[random() % smgr_shared_relations];
			/* Lock the new sr */
			flags = smgr_lock_sr(sr);
			return sr;
		}
		smgr_unlock_sr(sr, flags);
	}
}

/*
 * Allocate a new invalid SMgrSharedRelation, and return it locked.
 *
 * The replacement algorithm is a simple FIFO design with no second chance for
 * now.
 */
static SMgrSharedRelation *
smgr_alloc_sr(void)
{
	SMgrSharedRelationMapping *mapping;
	SMgrSharedRelation *sr;
	SMgrRelation reln;
	ForkNumber forknum;
	uint32 index;
	LWLock *mapping_lock;
	uint32 flags;
	RelFileNodeBackend rnode;
	uint32 hash;

 retry:
	sr = polar_smgr_pool_sweep();
	flags = pg_atomic_read_u32(&sr->flags);
	/* If it's unused, can return it, still locked, immediately. */
	if (!(flags & SR_VALID))
		return sr;

	/*
	 * Copy the rnode and unlock.  We'll briefly acquire both mapping and SR
	 * locks, but we need to do it in that order, so we'll unlock the SR
	 * first.
	 */
	index = sr - sr_pool->objects;
	rnode = sr->rnode;
	smgr_unlock_sr(sr, flags);

	hash = get_hash_value(sr_mapping_table, &rnode);
	mapping_lock = SR_PARTITION_LOCK(hash);

	LWLockAcquire(mapping_lock, LW_EXCLUSIVE);
	mapping = hash_search_with_hash_value(sr_mapping_table,
										  &rnode,
										  hash,
										  HASH_FIND,
										  NULL);
	if (!mapping || mapping->index != index)
	{
		/* Too slow, it's gone or now points somewhere else.  Go around. */
		LWLockRelease(mapping_lock);
		goto retry;
	}

	/* We will lock the SR for just a few instructions. */
	flags = smgr_lock_sr(sr);
	Assert(flags & SR_VALID);

	/*
	 * If another backend is currently syncing any fork, we aren't allowed to
	 * evict it, and waiting for it would be pointless because that other
	 * backend already plans to allocate it.  So go around.
	 */
	if (flags & SR_SYNCING_MASK)
	{
		smgr_unlock_sr(sr, flags);
		LWLockRelease(mapping_lock);
		goto retry;
	}

	/*
	 * We will sync every fork that is dirty, and then we'll try to
	 * evict it.
	 */
	while (flags & SR_DIRTY_MASK)
	{
		forknum = SR_GET_ONE_DIRTY(flags);

		/* Set the sync bit, clear the just-dirtied bit and unlock. */
		flags |= SR_SYNCING(forknum);
		flags &= ~SR_JUST_DIRTIED(forknum);
		smgr_unlock_sr(sr, flags);
		LWLockRelease(mapping_lock);

		/*
		 * Perform the I/O, with no locks held.
		 * XXX It sucks that we fsync every segment, not just the ones that need it...
		 */
		reln = smgropen(rnode.node, rnode.backend);
		smgrimmedsync(reln, forknum);

		/*
		 * Reacquire the locks.  The object can't have been evicted,
		 * because we set a sync bit.
		 * XXX And what if it's dropped?
		 */
		LWLockAcquire(mapping_lock, LW_EXCLUSIVE);
		flags = smgr_lock_sr(sr);
		Assert(flags & SR_SYNCING(forknum));
		flags &= ~SR_SYNCING(forknum);
		if (flags & SR_JUST_DIRTIED(forknum))
		{
			/*
			 * Someone else dirtied it while we were syncing, so we can't mark
			 * it clean.  Let's give up on this SR and go around again.
			 */
			smgr_unlock_sr(sr, flags);
			LWLockRelease(mapping_lock);
			goto retry;
		}

		/* This fork is clean! */
		flags &= ~SR_DIRTY(forknum);
	}

	/*
	 * If we made it this far, there are no dirty forks, so we're now allowed
	 * to evict the SR from the pool and the mapping table.  Make sure that
	 * smgrnblocks_fast() sees that its pointer is now invalid by bumping the
	 * generation.
	 */
	flags &= ~SR_VALID;
	pg_atomic_write_u64(&sr->generation,
						pg_atomic_read_u64(&sr->generation) + 1);
	pg_write_barrier();
	smgr_unlock_sr(sr, flags);

	/*
	* If any callers to smgr_sr_drop() or smgr_sr_drop_db() had the misfortune
	* to have to wait for us to finish syncing, we can now wake them up.
	*/
	ConditionVariableBroadcast(&sr_pool->sync_flags_cleared);

	/* Remove from the mapping table. */
	hash_search_with_hash_value(sr_mapping_table,
								&rnode,
								hash,
								HASH_REMOVE,
								NULL);
	LWLockRelease(mapping_lock);

	/*
	 * XXX: We unlock while doing HASH_REMOVE on principle.  Maybe it'd be OK
	 * to hold it now that the clock hand is far away and there is no way
	 * anyone can look up this SR through buffer mapping table.
	 */
	flags = smgr_lock_sr(sr);
	if (flags & SR_VALID)
	{
		/* Oops, someone else got it. */
		smgr_unlock_sr(sr, flags);
		goto retry;
	}

	return sr;
}

/*
 * Set the number of blocks in a relation, in shared memory, and optionally
 * also mark the relation as "dirty" (meaning the it must be fsync'd before it
 * can be evicted).
 */
static void
smgrnblocks_update(SMgrRelation reln,
				   ForkNumber forknum,
				   BlockNumber nblocks,
				   bool mark_dirty)
{
	SMgrSharedRelationMapping *mapping;
	SMgrSharedRelation *sr = NULL;
	uint32		hash;
	LWLock *mapping_lock;
	uint32 flags;
	int i;

	if (POLAR_DISABLE_SR_UPDATE(reln))
		return ;

	hash = get_hash_value(sr_mapping_table, &reln->smgr_rnode);
	mapping_lock = SR_PARTITION_LOCK(hash);

 retry:
	LWLockAcquire(mapping_lock, LW_SHARED);
	mapping = hash_search_with_hash_value(sr_mapping_table,
										  &reln->smgr_rnode,
										  hash,
										  HASH_FIND,
										  NULL);
	if (mapping)
	{
		sr = &sr_pool->objects[mapping->index];
		flags = smgr_lock_sr(sr);
		if (mark_dirty)
		{
			/*
			 * Extend and truncate clobber the value, and there are no races
			 * to worry about because they can have higher level exclusive
			 * locking on the relation.
			 */
			sr->nblocks[forknum] = nblocks;

			/*
			 * Mark it dirty, and if it's currently being sync'd, make sure it
			 * stays dirty after that completes.
			 */
			flags |= SR_DIRTY(forknum);
			if (flags & SR_SYNCING(forknum))
				flags |= SR_JUST_DIRTIED(forknum);
		}
		else if (!(flags & SR_DIRTY(forknum)))
		{
			/*
			 * We won't clobber a dirty value with a non-dirty update, to
			 * avoid races against concurrent extend/truncate, but we can
			 * install a new clean value.
			 */
			sr->nblocks[forknum] = nblocks;
		}
		if (sr->usecount < smgr_pool_sweep_times)
			sr->usecount++;
		smgr_unlock_sr(sr, flags);
	}
	LWLockRelease(mapping_lock);

	/* If we didn't find it, then we'll need to allocate one. */
	if (!sr)
	{
		bool found;

		sr = smgr_alloc_sr();

		/* Upgrade to exclusive lock so we can create a mapping. */
		LWLockAcquire(mapping_lock, LW_EXCLUSIVE);
		mapping = hash_search_with_hash_value(sr_mapping_table,
											  &reln->smgr_rnode,
											  hash,
											  HASH_ENTER,
											  &found);
		if (!found)
		{
			/* Success!  Initialize. */
			mapping->index = sr - sr_pool->objects;
			sr->usecount = 1;
			smgr_unlock_sr(sr, SR_VALID);
			sr->rnode = reln->smgr_rnode;
			pg_atomic_write_u64(&sr->generation,
								pg_atomic_read_u64(&sr->generation) + 1);
			for (i = 0; i <= MAX_FORKNUM; ++i)
				sr->nblocks[i] = InvalidBlockNumber;
			LWLockRelease(mapping_lock);
		}
		else
		{
			/* Someone beat us to it.  Go around again. */
			smgr_unlock_sr(sr, 0);		/* = not valid */
			LWLockRelease(mapping_lock);
			goto retry;
		}
	}
}

/*
 * Use this function to drop smgr shared cache.
 */
void
smgr_drop_sr(RelFileNodeBackend *rnode)
{
	SMgrSharedRelationMapping *mapping;
	SMgrSharedRelation *sr;
	uint32	hash;
	LWLock *mapping_lock;
	uint32 flags;
	ForkNumber forknum;

	if (!polar_enabled_nblock_cache())
		return ;

	if (polar_in_replica_mode() && !polar_enable_replica_use_smgr_cache)
		return ;

	if (polar_is_standby() && !polar_enable_standby_use_smgr_cache)
		return ;

	hash = get_hash_value(sr_mapping_table, rnode);
	mapping_lock = SR_PARTITION_LOCK(hash);

retry:
	LWLockAcquire(mapping_lock, LW_EXCLUSIVE);
	mapping = hash_search_with_hash_value(sr_mapping_table,
										  rnode,
										  hash,
										  HASH_FIND,
										  NULL);
	if (mapping)
	{
		sr = &sr_pool->objects[mapping->index];

		flags = smgr_lock_sr(sr);
		Assert(flags & SR_VALID);

		if (flags & SR_SYNCING_MASK)
		{
			/*
			 * Oops, someone's syncing one of its forks; nothing to do but
			 * wait.
			 */
			Assert(!polar_in_replica_mode());
			smgr_unlock_sr(sr, flags);
			LWLockRelease(mapping_lock);
			ConditionVariableSleep(&sr_pool->sync_flags_cleared,
								   WAIT_EVENT_SMGR_DROP_SYNC);
			goto retry;
		}
		ConditionVariableCancelSleep();

		for (forknum = 0; forknum <= MAX_FORKNUM; forknum++)
			sr->nblocks[forknum] = InvalidBlockNumber;

		/* Mark it invalid and drop the mapping. */
		sr->usecount = 0;
		smgr_unlock_sr(sr, ~SR_VALID);
		hash_search_with_hash_value(sr_mapping_table,
									rnode,
									hash,
									HASH_REMOVE,
									NULL);
	}
	LWLockRelease(mapping_lock);
}

size_t
smgr_shmem_size(void)
{
	size_t size = 0;

	size = add_size(size,
					sizeof(offsetof(SMgrSharedRelationPool, objects) +
						   sizeof(SMgrSharedRelation) * smgr_shared_relations));
	size = add_size(size,
					hash_estimate_size(smgr_shared_relations,
									   sizeof(SMgrSharedRelationMapping)));

	return size;
}

void
smgr_shmem_init(void)
{
	HASHCTL		info;
	bool found;
	uint32 i;

	info.keysize = sizeof(RelFileNodeBackend);
	info.entrysize = sizeof(SMgrSharedRelationMapping);
	info.num_partitions = SR_PARTITIONS;
	sr_mapping_table = ShmemInitHash("SMgrSharedRelation Mapping Table",
									 smgr_shared_relations,
									 smgr_shared_relations,
									 &info,
									 HASH_ELEM | HASH_BLOBS | HASH_PARTITION);

	sr_pool = ShmemInitStruct("SMgrSharedRelation Pool",
							  offsetof(SMgrSharedRelationPool, objects) +
							  sizeof(SMgrSharedRelation) * smgr_shared_relations,
							  &found);
	if (!found)
	{
		ConditionVariableInit(&sr_pool->sync_flags_cleared);
		pg_atomic_init_u32(&sr_pool->next, 0);
		for (i = 0; i < smgr_shared_relations; ++i)
		{
			pg_atomic_init_u32(&sr_pool->objects[i].flags, 0);
			pg_atomic_init_u64(&sr_pool->objects[i].generation, 0);
			sr_pool->objects[i].usecount = 0;
		}
	}
}

SMgrSharedRelationPool *
polar_get_smgr_shared_pool(void)
{
	return sr_pool;
}

HTAB *
polar_get_smgr_mapping_table(void)
{
	return sr_mapping_table;
}

/*
 * smgrinit(), smgrshutdown() -- Initialize or shut down storage
 *								 managers.
 *
 * Note: smgrinit is called during backend startup (normal or standalone
 * case), *not* during postmaster start.  Therefore, any resources created
 * here or destroyed in smgrshutdown are backend-local.
 */
void
smgrinit(void)
{
	int			i;

	for (i = 0; i < NSmgr; i++)
	{
		if (smgrsw[i].smgr_init)
			smgrsw[i].smgr_init();
	}

	/* register the shutdown proc */
	on_proc_exit(smgrshutdown, 0);
}

/*
 * on_proc_exit hook for smgr cleanup during backend shutdown
 */
static void
smgrshutdown(int code, Datum arg)
{
	int			i;

	for (i = 0; i < NSmgr; i++)
	{
		if (smgrsw[i].smgr_shutdown)
			smgrsw[i].smgr_shutdown();
	}
}

/*
 * smgropen() -- Return an SMgrRelation object, creating it if need be.
 *
 * In versions of PostgreSQL prior to 17, this function returned an object
 * with no defined lifetime.  Now, however, the object remains valid for the
 * lifetime of the transaction, up to the point where AtEOXact_SMgr() is
 * called, making it much easier for callers to know for how long they can
 * hold on to a pointer to the returned object.  If this function is called
 * outside of a transaction, the object remains valid until smgrdestroy() or
 * smgrdestroyall() is called.  Background processes that use smgr but not
 * transactions typically do this once per checkpoint cycle.
 *
 * This does not attempt to actually open the underlying files.
 */
SMgrRelation
smgropen(RelFileLocator rlocator, ProcNumber backend)
{
	RelFileLocatorBackend brlocator;
	SMgrRelation reln;
	bool		found;

	Assert(RelFileNumberIsValid(rlocator.relNumber));

	if (SMgrRelationHash == NULL)
	{
		/* First time through: initialize the hash table */
		HASHCTL		ctl;

		ctl.keysize = sizeof(RelFileLocatorBackend);
		ctl.entrysize = sizeof(SMgrRelationData);
		SMgrRelationHash = hash_create("smgr relation table", 400,
									   &ctl, HASH_ELEM | HASH_BLOBS);
<<<<<<< HEAD
		dlist_init(&unowned_relns);
=======
		dlist_init(&unpinned_relns);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	/* Look up or create an entry */
	brlocator.locator = rlocator;
	brlocator.backend = backend;
	reln = (SMgrRelation) hash_search(SMgrRelationHash,
									  &brlocator,
									  HASH_ENTER, &found);

	/* Initialize it if not present before */
	if (!found)
	{
		/* hash_search already filled in the lookup key */
		reln->smgr_targblock = InvalidBlockNumber;
<<<<<<< HEAD
		reln->smgr_shared = NULL;
		reln->smgr_shared_generation = 0;
		reln->smgr_fsm_nblocks = InvalidBlockNumber;
		reln->smgr_vm_nblocks = InvalidBlockNumber;
=======
		for (int i = 0; i <= MAX_FORKNUM; ++i)
			reln->smgr_cached_nblocks[i] = InvalidBlockNumber;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		reln->smgr_which = 0;	/* we only have md.c at present */

		/* implementation-specific initialization */
		smgrsw[reln->smgr_which].smgr_open(reln);

<<<<<<< HEAD
		/* POLAR: bulk extend status */
		for (forknum = 0; forknum <= MAX_FORKNUM; forknum++)
			polar_smgr_clear_bulk_extend(reln, forknum);
		/* POLAR end */

		/* it has no owner yet */
		dlist_push_tail(&unowned_relns, &reln->node);
=======
		/* it is not pinned yet */
		reln->pincount = 0;
		dlist_push_tail(&unpinned_relns, &reln->node);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	return reln;
}

/*
 * smgrpin() -- Prevent an SMgrRelation object from being destroyed at end of
 *				transaction
 */
void
smgrpin(SMgrRelation reln)
{
<<<<<<< HEAD
	/* We don't support "disowning" an SMgrRelation here, use smgrclearowner */
	Assert(owner != NULL);

	/*
	 * First, unhook any old owner.  (Normally there shouldn't be any, but it
	 * seems possible that this can happen during swap_relation_files()
	 * depending on the order of processing.  It's ok to close the old
	 * relcache entry early in that case.)
	 *
	 * If there isn't an old owner, then the reln should be in the unowned
	 * list, and we need to remove it.
	 */
	if (reln->smgr_owner)
		*(reln->smgr_owner) = NULL;
	else
		dlist_delete(&reln->node);

	/* Now establish the ownership relationship. */
	reln->smgr_owner = owner;
	*owner = reln;
=======
	if (reln->pincount == 0)
		dlist_delete(&reln->node);
	reln->pincount++;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * smgrunpin() -- Allow an SMgrRelation object to be destroyed at end of
 *				  transaction
 *
 * The object remains valid, but if there are no other pins on it, it is moved
 * to the unpinned list where it will be destroyed by AtEOXact_SMgr().
 */
void
smgrunpin(SMgrRelation reln)
{
<<<<<<< HEAD
	/* Do nothing if the SMgrRelation object is not owned by the owner */
	if (reln->smgr_owner != owner)
		return;

	/* unset the owner's reference */
	*owner = NULL;

	/* unset our reference to the owner */
	reln->smgr_owner = NULL;

	/* add to list of unowned relations */
	dlist_push_tail(&unowned_relns, &reln->node);
}

/*
 *	smgrexists() -- Does the underlying file for a fork exist?
 */
bool
smgrexists(SMgrRelation reln, ForkNumber forknum)
{
	return smgrsw[reln->smgr_which].smgr_exists(reln, forknum);
}

/*
 *	smgrclose() -- Close and delete an SMgrRelation object.
 */
void
smgrclose(SMgrRelation reln)
{
	SMgrRelation *owner;
=======
	Assert(reln->pincount > 0);
	reln->pincount--;
	if (reln->pincount == 0)
		dlist_push_tail(&unpinned_relns, &reln->node);
}

/*
 * smgrdestroy() -- Delete an SMgrRelation object.
 */
static void
smgrdestroy(SMgrRelation reln)
{
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	ForkNumber	forknum;

	Assert(reln->pincount == 0);

	for (forknum = 0; forknum <= MAX_FORKNUM; forknum++)
		smgrsw[reln->smgr_which].smgr_close(reln, forknum);

<<<<<<< HEAD
	owner = reln->smgr_owner;

	if (!owner)
		dlist_delete(&reln->node);
=======
	dlist_delete(&reln->node);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	if (hash_search(SMgrRelationHash,
					&(reln->smgr_rlocator),
					HASH_REMOVE, NULL) == NULL)
		elog(ERROR, "SMgrRelation hashtable corrupted");
}

/*
 * smgrrelease() -- Release all resources used by this object.
 *
 * The object remains valid.
 */
void
smgrrelease(SMgrRelation reln)
{
	for (ForkNumber forknum = 0; forknum <= MAX_FORKNUM; forknum++)
	{
		smgrsw[reln->smgr_which].smgr_close(reln, forknum);
		reln->smgr_cached_nblocks[forknum] = InvalidBlockNumber;
	}
	reln->smgr_targblock = InvalidBlockNumber;
}

/*
 * smgrclose() -- Close an SMgrRelation object.
 *
 * The SMgrRelation reference should not be used after this call.  However,
 * because we don't keep track of the references returned by smgropen(), we
 * don't know if there are other references still pointing to the same object,
 * so we cannot remove the SMgrRelation object yet.  Therefore, this is just a
 * synonym for smgrrelease() at the moment.
 */
void
smgrclose(SMgrRelation reln)
{
	smgrrelease(reln);
}

/*
 * smgrdestroyall() -- Release resources used by all unpinned objects.
 *
 * It must be known that there are no pointers to SMgrRelations, other than
 * those pinned with smgrpin().
 */
void
smgrdestroyall(void)
{
	dlist_mutable_iter iter;

	/*
	 * Zap all unpinned SMgrRelations.  We rely on smgrdestroy() to remove
	 * each one from the list.
	 */
	dlist_foreach_modify(iter, &unpinned_relns)
	{
		SMgrRelation rel = dlist_container(SMgrRelationData, node,
										   iter.cur);

		smgrdestroy(rel);
	}
}

/*
 * smgrreleaseall() -- Release resources used by all objects.
 */
void
smgrreleaseall(void)
{
	HASH_SEQ_STATUS status;
	SMgrRelation reln;

	/* Nothing to do if hashtable not set up */
	if (SMgrRelationHash == NULL)
		return;

	hash_seq_init(&status, SMgrRelationHash);

	while ((reln = (SMgrRelation) hash_seq_search(&status)) != NULL)
	{
		smgrrelease(reln);
	}
}

/*
 * smgrreleaserellocator() -- Release resources for given RelFileLocator, if
 *							  it's open.
 *
 * This has the same effects as smgrrelease(smgropen(rlocator)), but avoids
 * uselessly creating a hashtable entry only to drop it again when no
 * such entry exists already.
 */
void
smgrreleaserellocator(RelFileLocatorBackend rlocator)
{
	SMgrRelation reln;

	/* Nothing to do if hashtable not set up */
	if (SMgrRelationHash == NULL)
		return;

	reln = (SMgrRelation) hash_search(SMgrRelationHash,
									  &rlocator,
									  HASH_FIND, NULL);
	if (reln != NULL)
		smgrrelease(reln);
}

/*
 * smgrexists() -- Does the underlying file for a fork exist?
 */
bool
smgrexists(SMgrRelation reln, ForkNumber forknum)
{
	return smgrsw[reln->smgr_which].smgr_exists(reln, forknum);
}

/*
 * smgrcreate() -- Create a new relation.
 *
 * Given an already-created (but presumably unused) SMgrRelation,
 * cause the underlying disk file or other storage for the fork
 * to be created.
 */
void
smgrcreate(SMgrRelation reln, ForkNumber forknum, bool isRedo)
{
<<<<<<< HEAD
	/*
	 * Exit quickly in WAL replay mode if we've already opened the file. If
	 * it's open, it surely must exist.
	 */
	if (isRedo && reln->md_num_open_segs[forknum] > 0)
		return;

	/*
	 * We may be using the target table space for the first time in this
	 * database, so create a per-database subdirectory if needed.
	 *
	 * XXX this is a fairly ugly violation of module layering, but this seems
	 * to be the best place to put the check.  Maybe TablespaceCreateDbspace
	 * should be here and not in commands/tablespace.c?  But that would imply
	 * importing a lot of stuff that smgr.c oughtn't know, either.
	 */
	TablespaceCreateDbspace(reln->smgr_rnode.node.spcNode,
							reln->smgr_rnode.node.dbNode,
							isRedo,
							SmgrIsTemp(reln));

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	smgrsw[reln->smgr_which].smgr_create(reln, forknum, isRedo);
}

/*
 * smgrdosyncall() -- Immediately sync all forks of all given relations
 *
 * All forks of all given relations are synced out to the store.
 *
 * This is equivalent to FlushRelationBuffers() for each smgr relation,
 * then calling smgrimmedsync() for all forks of each relation, but it's
 * significantly quicker so should be preferred when possible.
 */
void
smgrdosyncall(SMgrRelation *rels, int nrels)
{
	int			i = 0;
	ForkNumber	forknum;

	if (nrels == 0)
		return;

	FlushRelationsAllBuffers(rels, nrels);

	/*
	 * Sync the physical file(s).
	 */
	for (i = 0; i < nrels; i++)
	{
		int			which = rels[i]->smgr_which;

<<<<<<< HEAD
	/*
	 * It'd be nice to tell the stats collector to forget it immediately, too.
	 * But we can't because we don't know the OID (and in cases involving
	 * relfilenode swaps, it's not always clear which table OID to forget,
	 * anyway).
	 */

	/*
	 * Send a shared-inval message to force other backends to close any
	 * dangling smgr references they may have for this rel.  We should do this
	 * before starting the actual unlinking, in case we fail partway through
	 * that step.  Note that the sinval message will eventually come back to
	 * this backend, too, and thereby provide a backstop that we closed our
	 * own smgr rel.
	 */
	CacheInvalidateSmgr(rnode);

	smgr_drop_sr(&rnode);

	/*
	 * Delete the physical file(s).
	 *
	 * Note: smgr_unlink must treat deletion failure as a WARNING, not an
	 * ERROR, because we've already decided to commit or abort the current
	 * xact.
	 */
	smgrsw[which].smgr_unlink(rnode, InvalidForkNumber, isRedo);
=======
		for (forknum = 0; forknum <= MAX_FORKNUM; forknum++)
		{
			if (smgrsw[which].smgr_exists(rels[i], forknum))
				smgrsw[which].smgr_immedsync(rels[i], forknum);
		}
	}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * smgrdounlinkall() -- Immediately unlink all forks of all given relations
 *
 * All forks of all given relations are removed from the store.  This
 * should not be used during transactional operations, since it can't be
 * undone.
 *
 * If isRedo is true, it is okay for the underlying file(s) to be gone
 * already.
 */
void
smgrdounlinkall(SMgrRelation *rels, int nrels, bool isRedo)
{
	int			i = 0;
	RelFileLocatorBackend *rlocators;
	ForkNumber	forknum;

	if (nrels == 0)
		return;

	/*
	 * Get rid of any remaining buffers for the relations.  bufmgr will just
	 * drop them without bothering to write the contents.
	 */
	DropRelationsAllBuffers(rels, nrels);

	/*
	 * create an array which contains all relations to be dropped, and close
	 * each relation's forks at the smgr level while at it
	 */
	rlocators = palloc(sizeof(RelFileLocatorBackend) * nrels);
	for (i = 0; i < nrels; i++)
	{
		RelFileLocatorBackend rlocator = rels[i]->smgr_rlocator;
		int			which = rels[i]->smgr_which;

		rlocators[i] = rlocator;

		/* Close the forks at smgr level */
		for (forknum = 0; forknum <= MAX_FORKNUM; forknum++)
			smgrsw[which].smgr_close(rels[i], forknum);
	}

	/* POLAR: Record relation size change infomation */
	if (polar_in_replica_mode() || polar_bg_redo_state_is_parallel(polar_logindex_redo_instance))
	{
		for (i = 0; i < nrels; i++)
		{
			for (forknum = 0; forknum <= MAX_FORKNUM; forknum++)
				POLAR_RECORD_REL_SIZE(&rnodes[i].node, forknum, 0);
		}
	}

	/*
	 * Send a shared-inval message to force other backends to close any
	 * dangling smgr references they may have for these rels.  We should do
	 * this before starting the actual unlinking, in case we fail partway
	 * through that step.  Note that the sinval messages will eventually come
	 * back to this backend, too, and thereby provide a backstop that we
	 * closed our own smgr rel.
	 */
	for (i = 0; i < nrels; i++)
		CacheInvalidateSmgr(rlocators[i]);

	for (i = 0; i < nrels; i++)
		smgr_drop_sr(&rnodes[i]);

	/*
	 * Delete the physical file(s).
	 *
	 * Note: smgr_unlink must treat deletion failure as a WARNING, not an
	 * ERROR, because we've already decided to commit or abort the current
	 * xact.
	 */
	/* POLAR: rw unlink rel files */
	if (polar_in_replica_mode() == false)
	{
		for (i = 0; i < nrels; i++)
		{
			int			which = rels[i]->smgr_which;

<<<<<<< HEAD
			for (forknum = 0; forknum <= MAX_FORKNUM; forknum++)
				smgrsw[which].smgr_unlink(rnodes[i], forknum, isRedo);
		}
=======
		for (forknum = 0; forknum <= MAX_FORKNUM; forknum++)
			smgrsw[which].smgr_unlink(rlocators[i], forknum, isRedo);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
	/* POLAR end */

	pfree(rlocators);
}

<<<<<<< HEAD
/*
 *	smgrdounlinkfork() -- Immediately unlink one fork of a relation.
 *
 *		The specified fork of the relation is removed from the store.  This
 *		should not be used during transactional operations, since it can't be
 *		undone.
 *
 *		If isRedo is true, it is okay for the underlying file to be gone
 *		already.
 */
void
smgrdounlinkfork(SMgrRelation reln, ForkNumber forknum, bool isRedo)
{
	RelFileNodeBackend rnode = reln->smgr_rnode;
	int			which = reln->smgr_which;

	/* Close the fork at smgr level */
	smgrsw[which].smgr_close(reln, forknum);

	/*
	 * Get rid of any remaining buffers for the fork.  bufmgr will just drop
	 * them without bothering to write the contents.
	 */
	DropRelFileNodeBuffers(rnode, forknum, 0);

	/*
	 * It'd be nice to tell the stats collector to forget it immediately, too.
	 * But we can't because we don't know the OID (and in cases involving
	 * relfilenode swaps, it's not always clear which table OID to forget,
	 * anyway).
	 */

	/*
	 * Send a shared-inval message to force other backends to close any
	 * dangling smgr references they may have for this rel.  We should do this
	 * before starting the actual unlinking, in case we fail partway through
	 * that step.  Note that the sinval message will eventually come back to
	 * this backend, too, and thereby provide a backstop that we closed our
	 * own smgr rel.
	 */
	CacheInvalidateSmgr(rnode);

	smgr_drop_sr(&rnode);

	/*
	 * Delete the physical file(s).
	 *
	 * Note: smgr_unlink must treat deletion failure as a WARNING, not an
	 * ERROR, because we've already decided to commit or abort the current
	 * xact.
	 */
	smgrsw[which].smgr_unlink(rnode, forknum, isRedo);
}
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/*
 * smgrextend() -- Add a new block to a file.
 *
 * The semantics are nearly the same as smgrwrite(): write at the
 * specified position.  However, this is to be used for the case of
 * extending a relation (i.e., blocknum is at or beyond the current
 * EOF).  Note that we assume writing a block beyond current EOF
 * causes intervening file space to become filled with zeroes.
 */
void
smgrextend(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
		   const void *buffer, bool skipFsync)
{
	smgrsw[reln->smgr_which].smgr_extend(reln, forknum, blocknum,
										 buffer, skipFsync);

<<<<<<< HEAD
	/* POLAR: bulk extend will call smgrnblocks_update in smgrextendbatch */
	if (polar_smgr_being_bulk_extend(reln, forknum) == false)
		smgrnblocks_update(reln, forknum, blocknum + 1, true);
=======
	/*
	 * Normally we expect this to increase nblocks by one, but if the cached
	 * value isn't as expected, just invalidate it so the next call asks the
	 * kernel.
	 */
	if (reln->smgr_cached_nblocks[forknum] == blocknum)
		reln->smgr_cached_nblocks[forknum] = blocknum + 1;
	else
		reln->smgr_cached_nblocks[forknum] = InvalidBlockNumber;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * smgrzeroextend() -- Add new zeroed out blocks to a file.
 *
 * Similar to smgrextend(), except the relation can be extended by
 * multiple blocks at once and the added blocks will be filled with
 * zeroes.
 */
void
smgrzeroextend(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
			   int nblocks, bool skipFsync)
{
	smgrsw[reln->smgr_which].smgr_zeroextend(reln, forknum, blocknum,
											 nblocks, skipFsync);

	/*
	 * Normally we expect this to increase the fork size by nblocks, but if
	 * the cached value isn't as expected, just invalidate it so the next call
	 * asks the kernel.
	 */
	if (reln->smgr_cached_nblocks[forknum] == blocknum)
		reln->smgr_cached_nblocks[forknum] = blocknum + nblocks;
	else
		reln->smgr_cached_nblocks[forknum] = InvalidBlockNumber;
}

/*
<<<<<<< HEAD
 *  POLAR: bulk read
 *
 *	polar_smgrbulkread() -- read multi particular block from a relation into the supplied
 *    				  buffer.
 *
 *		This routine is called from the buffer manager in order to
 *		instantiate pages in the shared buffer cache.  All storage managers
 *		return pages in the format that POSTGRES expects.
 */
void
polar_smgrbulkread(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
			 int blockCount, char *buffer)
{
	smgrsw[reln->smgr_which].polar_smgr_bulkread(reln, forknum, blocknum, blockCount, buffer);
}
/* POLAR end */

/*
 *	smgrread() -- read a particular block from a relation into the supplied
 *				  buffer.
=======
 * smgrprefetch() -- Initiate asynchronous read of the specified block of a relation.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 *
 * In recovery only, this can return false to indicate that a file
 * doesn't exist (presumably it has been dropped by a later WAL
 * record).
 */
bool
smgrprefetch(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
			 int nblocks)
{
	return smgrsw[reln->smgr_which].smgr_prefetch(reln, forknum, blocknum, nblocks);
}

/*
 * smgrreadv() -- read a particular block range from a relation into the
 *				 supplied buffers.
 *
 * This routine is called from the buffer manager in order to
 * instantiate pages in the shared buffer cache.  All storage managers
 * return pages in the format that POSTGRES expects.
 */
void
smgrreadv(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
		  void **buffers, BlockNumber nblocks)
{
	smgrsw[reln->smgr_which].smgr_readv(reln, forknum, blocknum, buffers,
										nblocks);
}

/*
<<<<<<< HEAD
 *	POLAR: bulk write
 *	polar_smgrbulkwrite() -- Write the supplied buffer out.
 *
 *		This is to be used only for updating already-existing blocks of a
 *		relation (ie, those before the current EOF).  To extend a relation,
 *		use smgrextendbatch().
 *
 *		This is not a synchronous write -- the block is not necessarily
 *		on disk at return, only dumped out to the kernel.  However,
 *		provisions will be made to fsync the write before the next checkpoint.
 *
 *		skipFsync indicates that the caller will make other provisions to
 *		fsync the relation, so we needn't bother.  Temporary relations also
 *		do not require fsync.
 */
void
polar_smgrbulkwrite(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
				int blockCount, char *buffer, bool skipFsync)
{
	smgrsw[reln->smgr_which].polar_smgr_bulkwrite(reln, forknum, blocknum, blockCount,
		buffer, skipFsync);
}

/*
 *	smgrwrite() -- Write the supplied buffer out.
=======
 * smgrwritev() -- Write the supplied buffers out.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 *
 * This is to be used only for updating already-existing blocks of a
 * relation (ie, those before the current EOF).  To extend a relation,
 * use smgrextend().
 *
 * This is not a synchronous write -- the block is not necessarily
 * on disk at return, only dumped out to the kernel.  However,
 * provisions will be made to fsync the write before the next checkpoint.
 *
 * NB: The mechanism to ensure fsync at next checkpoint assumes that there is
 * something that prevents a concurrent checkpoint from "racing ahead" of the
 * write.  One way to prevent that is by holding a lock on the buffer; the
 * buffer manager's writes are protected by that.  The bulk writer facility
 * in bulk_write.c checks the redo pointer and calls smgrimmedsync() if a
 * checkpoint happened; that relies on the fact that no other backend can be
 * concurrently modifying the page.
 *
 * skipFsync indicates that the caller will make other provisions to
 * fsync the relation, so we needn't bother.  Temporary relations also
 * do not require fsync.
 */
void
smgrwritev(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
		   const void **buffers, BlockNumber nblocks, bool skipFsync)
{
<<<<<<< HEAD
	smgrsw[reln->smgr_which].smgr_write(reln, forknum, blocknum,
		buffer, skipFsync);
=======
	smgrsw[reln->smgr_which].smgr_writev(reln, forknum, blocknum,
										 buffers, nblocks, skipFsync);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * smgrwriteback() -- Trigger kernel writeback for the supplied range of
 *					   blocks.
 */
void
smgrwriteback(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
			  BlockNumber nblocks)
{
	smgrsw[reln->smgr_which].smgr_writeback(reln, forknum, blocknum,
											nblocks);
}

/*
<<<<<<< HEAD
 * When a database is dropped, we have to find and throw away all its
 * SMgrSharedRelation objects.
 */
void
polar_dropdb_smgr_shared_relation_pool(Oid dbid)
{
	int	i;
	uint32 flags;
	RelFileNodeBackend rnode;
	SMgrSharedRelation *sr;

	for (i = 0; i < smgr_shared_relations; i++)
	{
		sr = &sr_pool->objects[i];
		flags = smgr_lock_sr(sr);
		if ((flags & SR_VALID) && sr->rnode.node.dbNode == dbid)
		{
			rnode = sr->rnode;
			smgr_unlock_sr(sr, flags);

			/* Drop, if it's still valid. */
			smgr_drop_sr(&rnode);
		}
		else
			smgr_unlock_sr(sr, flags);
	}
}

/* POLAR: smgr cache search and update */
BlockNumber
polar_nblocks_cache_search_and_update(SMgrRelation reln, ForkNumber forknum, bool need_update)
{
	BlockNumber result;

	/* POLAR: For some scenes, we don't support nblocks cache. */
	if (polar_nouse_nblocks_cache(reln, forknum))
	{
		result = smgrsw[reln->smgr_which].smgr_nblocks(reln, forknum, false);
		return result;
	}

	/* Can we get the answer from shared memory without locking? */
	result = smgrnblocks_fast(reln, forknum);
	if (result != InvalidBlockNumber)
		return result;

	/* Can we get the answer from shared memory with only a share lock? */
	result = smgrnblocks_shared(reln, forknum);
	if (result != InvalidBlockNumber)
		return result;

	/* Ask the kernel. */
	result = smgrsw[reln->smgr_which].smgr_nblocks(reln, forknum, false);

	/* Update the value in shared memory for faster service next time. */
	if (need_update)
		smgrnblocks_update(reln, forknum, result, false);

	return result;
}

/*
 *	smgrnblocks() -- Calculate the number of blocks in the
 *					 supplied relation.
 * polar_enabled_nblock_cache_all() means enable smgr cache and the mode is 
 * cache all.
=======
 * smgrnblocks() -- Calculate the number of blocks in the
 *					supplied relation.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 */
BlockNumber
smgrnblocks(SMgrRelation reln, ForkNumber forknum)
{
	BlockNumber result;

<<<<<<< HEAD
	if (polar_enabled_nblock_cache_all())
		result = polar_nblocks_cache_search_and_update(reln, forknum, true);
	else
		result = smgrsw[reln->smgr_which].smgr_nblocks(reln, forknum, false);

	return result;
}

/*
 * POLAR: Calculate the number of blocks for some scene. Only in nblock cache
 * no-all, we get nblocks from smgr cache.
 * polar_enabled_nblock_cache() means disable smgr cache or the cache mode 
 * is no-all. When smgr cache is disabled, polar_smgrnblocks_cache -> smgrnblocks
 * -> smgrsw[reln->smgr_which].smgr_nblocks, it will get the real number of blocks
 */
BlockNumber
polar_smgrnblocks_cache(SMgrRelation reln, ForkNumber forknum)
{
	BlockNumber result;

	if (polar_enabled_nblock_cache())
		result = polar_nblocks_cache_search_and_update(reln, forknum, true);
	else
		result = smgrnblocks(reln, forknum);
=======
	/* Check and return if we get the cached value for the number of blocks. */
	result = smgrnblocks_cached(reln, forknum);
	if (result != InvalidBlockNumber)
		return result;

	result = smgrsw[reln->smgr_which].smgr_nblocks(reln, forknum);

	reln->smgr_cached_nblocks[forknum] = result;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	return result;
}

/*
 * smgrnblocks_cached() -- Get the cached number of blocks in the supplied
 *						   relation.
 *
 * Returns an InvalidBlockNumber when not in recovery and when the relation
 * fork size is not cached.
 */
BlockNumber
smgrnblocks_cached(SMgrRelation reln, ForkNumber forknum)
{
	/*
	 * For now, this function uses cached values only in recovery due to lack
	 * of a shared invalidation mechanism for changes in file size.  Code
	 * elsewhere reads smgr_cached_nblocks and copes with stale data.
	 */
	if (InRecovery && reln->smgr_cached_nblocks[forknum] != InvalidBlockNumber)
		return reln->smgr_cached_nblocks[forknum];

	return InvalidBlockNumber;
}

/*
 * smgrtruncate() -- Truncate the given forks of supplied relation to
 *					 each specified numbers of blocks
 *
 * The truncation is done immediately, so this can't be rolled back.
 *
 * The caller must hold AccessExclusiveLock on the relation, to ensure that
 * other backends receive the smgr invalidation event that this function sends
 * before they access any forks of the relation again.
 */
void
smgrtruncate(SMgrRelation reln, ForkNumber *forknum, int nforks, BlockNumber *nblocks)
{
<<<<<<< HEAD
	/* POLAR :record the new relation size to the cache */
	POLAR_RECORD_REL_SIZE(&(reln->smgr_rnode.node), forknum, nblocks);
	/* POLAR end */
=======
	int			i;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Get rid of any buffers for the about-to-be-deleted blocks. bufmgr will
	 * just drop them without bothering to write the contents.
	 */
	DropRelationBuffers(reln, forknum, nforks, nblocks);

	/*
	 * Send a shared-inval message to force other backends to close any smgr
	 * references they may have for this rel.  This is useful because they
	 * might have open file pointers to segments that got removed, and/or
	 * smgr_targblock variables pointing past the new rel end.  (The inval
	 * message will come back to our backend, too, causing a
	 * probably-unnecessary local smgr flush.  But we don't expect that this
	 * is a performance-critical path.)  As in the unlink code, we want to be
	 * sure the message is sent before we start changing things on-disk.
	 */
	CacheInvalidateSmgr(reln->smgr_rlocator);

<<<<<<< HEAD
	/*
	 * Do the truncation.
	 */
	smgrsw[reln->smgr_which].smgr_truncate(reln, forknum, nblocks);
	smgrnblocks_update(reln, forknum, nblocks, true);
}

/*
 * POLAR: The same as smgrtruncate except there is no DropRelFileNodeBuffers
 */
void
polar_smgrtruncate_no_drop_buffer(SMgrRelation reln, ForkNumber forknum, BlockNumber nblocks)
{
	smgrsw[reln->smgr_which].smgr_truncate(reln, forknum, nblocks);
	smgrnblocks_update(reln, forknum, nblocks, true);
=======
	/* Do the truncation */
	for (i = 0; i < nforks; i++)
	{
		/* Make the cached size is invalid if we encounter an error. */
		reln->smgr_cached_nblocks[forknum[i]] = InvalidBlockNumber;

		smgrsw[reln->smgr_which].smgr_truncate(reln, forknum[i], nblocks[i]);

		/*
		 * We might as well update the local smgr_cached_nblocks values. The
		 * smgr cache inval message that this function sent will cause other
		 * backends to invalidate their copies of smgr_cached_nblocks, and
		 * these ones too at the next command boundary. But ensure they aren't
		 * outright wrong until then.
		 */
		reln->smgr_cached_nblocks[forknum[i]] = nblocks[i];
	}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * smgrregistersync() -- Request a relation to be sync'd at next checkpoint
 *
 * This can be used after calling smgrwrite() or smgrextend() with skipFsync =
 * true, to register the fsyncs that were skipped earlier.
 *
 * Note: be mindful that a checkpoint could already have happened between the
 * smgrwrite or smgrextend calls and this!  In that case, the checkpoint
 * already missed fsyncing this relation, and you should use smgrimmedsync
 * instead.  Most callers should use the bulk loading facility in bulk_write.c
 * which handles all that.
 */
void
smgrregistersync(SMgrRelation reln, ForkNumber forknum)
{
	smgrsw[reln->smgr_which].smgr_registersync(reln, forknum);
}

/*
 * smgrimmedsync() -- Force the specified relation to stable storage.
 *
 * Synchronously force all previous writes to the specified relation
 * down to disk.
 *
 * This is useful for building completely new relations (eg, new
 * indexes).  Instead of incrementally WAL-logging the index build
 * steps, we can just write completed index pages to disk with smgrwrite
 * or smgrextend, and then fsync the completed index file before
 * committing the transaction.  (This is sufficient for purposes of
 * crash recovery, since it effectively duplicates forcing a checkpoint
 * for the completed index.  But it is *not* sufficient if one wishes
 * to use the WAL log for PITR or replication purposes: in that case
 * we have to make WAL entries as well.)
 *
 * The preceding writes should specify skipFsync = true to avoid
 * duplicative fsyncs.
 *
 * Note that you need to do FlushRelationBuffers() first if there is
 * any possibility that there are dirty buffers for the relation;
 * otherwise the sync is not very meaningful.
 *
 * Most callers should use the bulk loading facility in bulk_write.c
 * instead of calling this directly.
 */
void
smgrimmedsync(SMgrRelation reln, ForkNumber forknum)
{
	smgrsw[reln->smgr_which].smgr_immedsync(reln, forknum);
}

/*
 * AtEOXact_SMgr
 *
 * This routine is called during transaction commit or abort (it doesn't
 * particularly care which).  All unpinned SMgrRelation objects are destroyed.
 *
 * We do this as a compromise between wanting transient SMgrRelations to
 * live awhile (to amortize the costs of blind writes of multiple blocks)
 * and needing them to not live forever (since we're probably holding open
 * a kernel file descriptor for the underlying file, and we need to ensure
 * that gets closed reasonably soon if the file gets deleted).
 */
void
AtEOXact_SMgr(void)
{
<<<<<<< HEAD
	dlist_mutable_iter	iter;

	/* POLAR: unlock the smgr shared cache */
	polar_release_held_smgr_cache();

	/*
	 * Zap all unowned SMgrRelations.  We rely on smgrclose() to remove each
	 * one from the list.
	 */
	dlist_foreach_modify(iter, &unowned_relns)
	{
		SMgrRelation	rel = dlist_container(SMgrRelationData, node,
											  iter.cur);

		Assert(rel->smgr_owner == NULL);

		smgrclose(rel);
	}
=======
	smgrdestroyall();
}

/*
 * This routine is called when we are ordered to release all open files by a
 * ProcSignalBarrier.
 */
bool
ProcessBarrierSmgrRelease(void)
{
	smgrreleaseall();
	return true;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/* POLAR: bulk extend */
void
smgrextendbatch(SMgrRelation reln, ForkNumber forknum, BlockNumber blocknum,
		   int blockCount, char *buffer, bool skipFsync)
{
	Assert(polar_enable_shared_storage_mode);
	Assert(blockCount >= 1);

	smgrsw[reln->smgr_which].smgr_extendbatch(reln, forknum, blocknum, blockCount,
											   buffer, skipFsync);

	smgrnblocks_update(reln, forknum, blocknum + blockCount, true);
}

BlockNumber
polar_smgrnblocks_use_file_cache(SMgrRelation reln, ForkNumber forknum)
{
	return smgrsw[reln->smgr_which].smgr_nblocks(reln, forknum, true);
}

/*
 * polar_smgr_init_bulk_extend() -- Init polar bulk extend backend-local-variable.
 */
void
polar_smgr_init_bulk_extend(SMgrRelation reln, ForkNumber forknum)
{
	Assert(false == polar_smgr_being_bulk_extend(reln, forknum));
	reln->polar_nblocks_faked_for_bulk_extend[forknum] = smgrnblocks(reln, forknum);
	/*
	 * polar_flag_for_bulk_extend must be set after polar_nblocks_faked_for_bulk_extend,
	 * as polar_flag_for_bulk_extend have an effort on result of smgrnblocks().
	 */
	reln->polar_flag_for_bulk_extend[forknum] = true;
}

/*
 * polar_smgr_clear_bulk_extend() -- Clear polar bulk extend backend-local-variable.
 */
void
polar_smgr_clear_bulk_extend(SMgrRelation reln, ForkNumber forknum)
{
	reln->polar_flag_for_bulk_extend[forknum] = false;
}

/* POLAR end */

