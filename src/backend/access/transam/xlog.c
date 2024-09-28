/*-------------------------------------------------------------------------
 *
 * xlog.c
 *		PostgreSQL write-ahead log manager
 *
 * The Write-Ahead Log (WAL) functionality is split into several source
 * files, in addition to this one:
 *
 * xloginsert.c - Functions for constructing WAL records
 * xlogrecovery.c - WAL recovery and standby code
 * xlogreader.c - Facility for reading WAL files and parsing WAL records
 * xlogutils.c - Helper functions for WAL redo routines
 *
 * This file contains functions for coordinating database startup and
 * checkpointing, and managing the write-ahead log buffers when the
 * system is running.
 *
 * StartupXLOG() is the main entry point of the startup process.  It
 * coordinates database startup, performing WAL recovery, and the
 * transition from WAL recovery into normal operations.
 *
 * XLogInsertRecord() inserts a WAL record into the WAL buffers.  Most
 * callers should not call this directly, but use the functions in
 * xloginsert.c to construct the WAL record.  XLogFlush() can be used
 * to force the WAL to disk.
 *
 * In addition to those, there are many other functions for interrogating
 * the current system state, and for starting/stopping backups.
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/backend/access/transam/xlog.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <ctype.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "access/clog.h"
#include "access/commit_ts.h"
#include "access/heaptoast.h"
#include "access/multixact.h"
#include "access/rewriteheap.h"
#include "access/subtrans.h"
#include "access/timeline.h"
#include "access/transam.h"
#include "access/twophase.h"
#include "access/xact.h"
#include "access/xlog_internal.h"
#include "access/xlogarchive.h"
#include "access/xloginsert.h"
#include "access/xlogreader.h"
#include "access/xlogrecovery.h"
#include "access/xlogutils.h"
<<<<<<< HEAD
#include "bootstrap/bootstrap.h"
=======
#include "backup/basebackup.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#include "catalog/catversion.h"
#include "catalog/pg_control.h"
#include "catalog/pg_database.h"
#include "commands/waitlsn.h"
#include "common/controldata_utils.h"
#include "common/file_utils.h"
#include "executor/instrument.h"
#include "miscadmin.h"
#include "pg_trace.h"
#include "pgstat.h"
#include "port/atomics.h"
#include "port/pg_iovec.h"
#include "postmaster/bgwriter.h"
#include "postmaster/startup.h"
#include "postmaster/walsummarizer.h"
#include "postmaster/walwriter.h"
#include "replication/origin.h"
#include "replication/slot.h"
#include "replication/snapbuild.h"
#include "replication/walreceiver.h"
#include "replication/walsender.h"
#include "storage/bufmgr.h"
#include "storage/encryption.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/kmgr.h"
#include "storage/large_object.h"
#include "storage/latch.h"
<<<<<<< HEAD
#include "storage/pg_shmem.h"
#include "storage/pmsignal.h"
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#include "storage/predicate.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/reinit.h"
#include "storage/spin.h"
#include "storage/sync.h"
#include "utils/guc_hooks.h"
#include "utils/guc_tables.h"
#include "utils/injection_point.h"
#include "utils/memutils.h"
#include "utils/ps_status.h"
#include "utils/relmapper.h"
#include "utils/snapmgr.h"
#include "utils/timeout.h"
#include "utils/timestamp.h"
<<<<<<< HEAD
#include "pg_trace.h"

/* POLAR */
#include "access/heapam_xlog.h"
#include "access/polar_async_ddl_lock_replay.h"
#include "access/polar_csn_mvcc_vars.h"
#include "access/polar_csnlog.h"
#include "access/polar_checkpoint_ringbuf.h"
#include "access/polar_logindex_redo.h"
#include "commands/dbcommands_xlog.h"
#include "commands/tablespace.h"
#include "polar_dma/polar_dma.h"
#include "polar_datamax/polar_datamax.h"
#include "polar_flashback/polar_flashback.h"
#include "portability/instr_time.h"
#include "replication/polar_cluster_info.h"
#include "storage/polar_fd.h"
#include "storage/polar_flushlist.h"
#include "storage/polar_io_fencing.h"
#include "storage/polar_io_stat.h"
#include "storage/polar_pbp.h"
#include "storage/polar_xlogbuf.h"
#include "utils/faultinjector.h"
#include "utils/polar_backtrace.h"
#include "utils/polar_local_cache.h"
#include "utils/timestamp.h"
/* POLAR end */

#ifdef ENABLE_THREAD_SAFETY
/* Use platform-dependent pthread capability */
#include <pthread.h>
#endif

extern uint32 bootstrap_data_checksum_version;
extern uint32 bootstrap_data_encryption_cipher;

/* POLAR */
extern int BgWriterDelay;

/* File path names (all relative to $PGDATA) */
#define RECOVERY_COMMAND_FILE	"recovery.conf"
#define PROMOTE_SIGNAL_FILE		"promote"
#define FALLBACK_PROMOTE_SIGNAL_FILE "fallback_promote"

/*
 * POLAR: For crash recovery, StartupProcess will read emtry buffers, drop them
 * from xlog buffer.
 */
#define POLAR_REMOVE_EMPTY_PREAD_XLOG_BUFFER() \
	(POLAR_ENABLE_XLOG_BUFFER() && AmStartupProcess())
=======
#include "utils/varlena.h"

/* timeline ID to be used when bootstrapping */
#define BootstrapTimeLineID		1
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/* User-settable parameters */
int			max_wal_size_mb = 1024; /* 1 GB */
int			min_wal_size_mb = 80;	/* 80 MB */
int			wal_keep_size_mb = 0;
int			XLOGbuffers = -1;
int			XLogArchiveTimeout = 0;
int			XLogArchiveMode = ARCHIVE_MODE_OFF;
char	   *XLogArchiveCommand = NULL;
bool		EnableHotStandby = false;
bool		fullPageWrites = true;
bool		wal_log_hints = false;
int			wal_compression = WAL_COMPRESSION_NONE;
char	   *wal_consistency_checking_string = NULL;
bool	   *wal_consistency_checking = NULL;
<<<<<<< HEAD
bool		wal_recycle = true;
bool		log_checkpoints = false;
int			sync_method = DEFAULT_SYNC_METHOD;
int			wal_level = WAL_LEVEL_MINIMAL;
int			CommitDelay = 0;	/* precommit delay in microseconds */
int			CommitSiblings = 5; /* # concurrent xacts needed to sleep */
int			wal_retrieve_retry_interval = 5000;
bool		polar_enable_max_slot_wal_keep_size = false;
int			max_slot_wal_keep_size_mb = -1;
=======
bool		wal_init_zero = true;
bool		wal_recycle = true;
bool		log_checkpoints = true;
int			wal_sync_method = DEFAULT_WAL_SYNC_METHOD;
int			wal_level = WAL_LEVEL_REPLICA;
int			CommitDelay = 0;	/* precommit delay in microseconds */
int			CommitSiblings = 5; /* # concurrent xacts needed to sleep */
int			wal_retrieve_retry_interval = 5000;
int			max_slot_wal_keep_size_mb = -1;
int			wal_decode_buffer_size = 512 * 1024;
bool		track_wal_io_timing = false;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

#ifdef WAL_DEBUG
bool		XLOG_DEBUG = false;
#endif

int			wal_segment_size = DEFAULT_XLOG_SEG_SIZE;

/*
 * Number of WAL insertion locks to use. A higher value allows more insertions
 * to happen concurrently, but adds some CPU overhead to flushing the WAL,
 * which needs to iterate all the locks.
 */
/* POLAR */
int		polar_wal_buffer_insert_locks = DEFAULT_XLOG_INSERT_LOCKS;

/*
 * Max distance from last checkpoint, before triggering a new xlog-based
 * checkpoint.
 */
int			CheckPointSegments;

/* Estimated distance between checkpoints, in bytes */
static double CheckPointDistanceEstimate = 0;
static double PrevCheckPointDistance = 0;

/*
 * Track whether there were any deferred checks for custom resource managers
 * specified in wal_consistency_checking.
 */
static bool check_wal_consistency_checking_deferred = false;

/*
 * GUC support
 */
const struct config_enum_entry wal_sync_method_options[] = {
	{"fsync", WAL_SYNC_METHOD_FSYNC, false},
#ifdef HAVE_FSYNC_WRITETHROUGH
	{"fsync_writethrough", WAL_SYNC_METHOD_FSYNC_WRITETHROUGH, false},
#endif
	{"fdatasync", WAL_SYNC_METHOD_FDATASYNC, false},
#ifdef O_SYNC
	{"open_sync", WAL_SYNC_METHOD_OPEN, false},
#endif
#ifdef O_DSYNC
	{"open_datasync", WAL_SYNC_METHOD_OPEN_DSYNC, false},
#endif
	{NULL, 0, false}
};


/*
 * Although only "on", "off", and "always" are documented,
 * we accept all the likely variants of "on" and "off".
 */
const struct config_enum_entry archive_mode_options[] = {
	{"always", ARCHIVE_MODE_ALWAYS, false},
	{"on", ARCHIVE_MODE_ON, false},
	{"off", ARCHIVE_MODE_OFF, false},
	{"true", ARCHIVE_MODE_ON, true},
	{"false", ARCHIVE_MODE_OFF, true},
	{"yes", ARCHIVE_MODE_ON, true},
	{"no", ARCHIVE_MODE_OFF, true},
	{"1", ARCHIVE_MODE_ON, true},
	{"0", ARCHIVE_MODE_OFF, true},
	{NULL, 0, false}
};

/*
 * Statistics for current checkpoint are collected in this global struct.
 * Because only the checkpointer or a stand-alone backend can perform
 * checkpoints, this will be unused in normal backends.
 */
CheckpointStatsData CheckpointStats;

/*
 * During recovery, lastFullPageWrites keeps track of full_page_writes that
 * the replayed WAL records indicate. It's initialized with full_page_writes
 * that the recovery starting checkpoint record indicates, and then updated
 * each time XLOG_FPW_CHANGE record is replayed.
 */
static bool lastFullPageWrites;

/*
 * Local copy of the state tracked by SharedRecoveryState in shared memory,
 * It is false if SharedRecoveryState is RECOVERY_STATE_DONE.  True actually
 * means "not known, need to check the shared state".
 */
static bool LocalRecoveryInProgress = true;

/*
 * Local state for XLogInsertAllowed():
 *		1: unconditionally allowed to insert XLOG
 *		0: unconditionally not allowed to insert XLOG
 *		-1: must check RecoveryInProgress(); disallow until it is false
 * Most processes start with -1 and transition to 1 after seeing that recovery
 * is not in progress.  But we can also force the value for special cases.
 * The coding in XLogInsertAllowed() depends on the first two of these states
 * being numerically the same as bool true and false.
 */
static int	LocalXLogInsertAllowed = -1;

/*
<<<<<<< HEAD
 * When ArchiveRecoveryRequested is set, archive recovery was requested,
 * ie. recovery.conf file was present. When InArchiveRecovery is set, we are
 * currently recovering using offline XLOG archives. These variables are only
 * valid in the startup process.
 *
 * When ArchiveRecoveryRequested is true, but InArchiveRecovery is false, we're
 * currently performing crash recovery using only XLOG files in pg_wal, but
 * will switch to using offline XLOG archives as soon as we reach the end of
 * WAL in pg_wal.
*/
bool		ArchiveRecoveryRequested = false;
bool		InArchiveRecovery = false;

/* Was the last xlog file restored from archive, or local? */
static bool restoredFromArchive = false;

/* Buffers dedicated to consistency checks of size BLCKSZ */
static char *replay_image_masked = NULL;
static char *master_image_masked = NULL;

/* options taken from recovery.conf for archive recovery */
char	   *recoveryRestoreCommand = NULL;
static char *recoveryEndCommand = NULL;
static char *archiveCleanupCommand = NULL;
static RecoveryTargetType recoveryTarget = RECOVERY_TARGET_UNSET;
static bool recoveryTargetInclusive = true;
static RecoveryTargetAction recoveryTargetAction = RECOVERY_TARGET_ACTION_PAUSE;
static TransactionId recoveryTargetXid;
static TimestampTz recoveryTargetTime;
static char *recoveryTargetName;
static XLogRecPtr recoveryTargetLSN;
static int	recovery_min_apply_delay = 0;
static TimestampTz recoveryDelayUntilTime;

/* options taken from recovery.conf for XLOG streaming */
static bool StandbyModeRequested = false;
static char *PrimaryConnInfo = NULL;
static char *PrimarySlotName = NULL;
static char *TriggerFile = NULL;

/* POLAR: replication control based on consensus */
static bool becameLeader = false;
static XLogRecPtr consensusCommittedUpto = 0;
static XLogRecPtr consensusReadableUpto = 0;
static uint64 consensusTerm = 0;
static uint64 consensusLogTerm = 0;
static XLogRecPtr consensusLogUpto = 0;
static TimeLineID consensusLogTLI = 0;
static XLogRecPtr consensusReceivedUpto = 0;
static TimeLineID consensusReceivedTLI = 0;
// recovery target timeline from consensus protocol
static bool consensusRecoveryLatestTimeline = false;

/* are we currently in standby mode? */
bool		StandbyMode = false;

/* whether request for fast promotion has been made yet */
static bool fast_promote = false;

/*
 * if recoveryStopsBefore/After returns true, it saves information of the stop
 * point here
 */
static TransactionId recoveryStopXid;
static TimestampTz recoveryStopTime;
static XLogRecPtr recoveryStopLSN;
static char recoveryStopName[MAXFNAMELEN];
static bool recoveryStopAfter;

/*
 * During normal operation, the only timeline we care about is ThisTimeLineID.
 * During recovery, however, things are more complicated.  To simplify life
 * for rmgr code, we keep ThisTimeLineID set to the "current" timeline as we
 * scan through the WAL history (that is, it is the line that was active when
 * the currently-scanned WAL record was generated).  We also need these
 * timeline values:
 *
 * recoveryTargetTLI: the desired timeline that we want to end in.
 *
 * recoveryTargetIsLatest: was the requested target timeline 'latest'?
 *
 * expectedTLEs: a list of TimeLineHistoryEntries for recoveryTargetTLI and the timelines of
 * its known parents, newest first (so recoveryTargetTLI is always the
 * first list member).  Only these TLIs are expected to be seen in the WAL
 * segments we read, and indeed only these TLIs will be considered as
 * candidate WAL files to open at all.
 *
 * curFileTLI: the TLI appearing in the name of the current input WAL file.
 * (This is not necessarily the same as ThisTimeLineID, because we could
 * be scanning data that was copied from an ancestor timeline when the current
 * file was created.)  During a sequential scan we do not allow this value
 * to decrease.
 */
static TimeLineID recoveryTargetTLI;
static bool recoveryTargetIsLatest = false;
static List *expectedTLEs;
static TimeLineID curFileTLI;

/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 * ProcLastRecPtr points to the start of the last XLOG record inserted by the
 * current backend.  It is updated for all inserts.  XactLastRecEnd points to
 * end+1 of the last record, and is reset when we end a top-level transaction,
 * or start a new one; so it can be used to tell if the current transaction has
 * created any XLOG records.
 *
 * While in parallel mode, this may not be fully up to date.  When committing,
 * a transaction can assume this covers all xlog records written either by the
 * user backend or by any parallel worker which was present at any point during
 * the transaction.  But when aborting, or when still in parallel mode, other
 * parallel backends may have written WAL records at later LSNs than the value
 * stored here.  The parallel leader advances its own copy, when necessary,
 * in WaitForParallelWorkersToFinish.
 */
XLogRecPtr	ProcLastRecPtr = InvalidXLogRecPtr;
XLogRecPtr	XactLastRecEnd = InvalidXLogRecPtr;
XLogRecPtr	XactLastCommitEnd = InvalidXLogRecPtr;

/*
 * RedoRecPtr is this backend's local copy of the REDO record pointer
 * (which is almost but not quite the same as a pointer to the most recent
 * CHECKPOINT record).  We update this from the shared-memory copy,
 * XLogCtl->Insert.RedoRecPtr, whenever we can safely do so (ie, when we
 * hold an insertion lock).  See XLogInsertRecord for details.  We are also
 * allowed to update from XLogCtl->RedoRecPtr if we hold the info_lck;
 * see GetRedoRecPtr.
 *
 * NB: Code that uses this variable must be prepared not only for the
 * possibility that it may be arbitrarily out of date, but also for the
 * possibility that it might be set to InvalidXLogRecPtr. We used to
 * initialize it as a side effect of the first call to RecoveryInProgress(),
 * which meant that most code that might use it could assume that it had a
 * real if perhaps stale value. That's no longer the case.
 */
static XLogRecPtr RedoRecPtr;

/*
 * doPageWrites is this backend's local copy of (fullPageWrites ||
 * runningBackups > 0).  It is used together with RedoRecPtr to decide whether
 * a full-page image of a page need to be taken.
 *
 * NB: Initially this is false, and there's no guarantee that it will be
 * initialized to any other value before it is first used. Any code that
 * makes use of it must recheck the value after obtaining a WALInsertLock,
 * and respond appropriately if it turns out that the previous value wasn't
 * accurate.
 */
static bool doPageWrites;

/*----------
 * Shared-memory data structures for XLOG control
 *
 * LogwrtRqst indicates a byte position that we need to write and/or fsync
 * the log up to (all records before that point must be written or fsynced).
 * The positions already written/fsynced are maintained in logWriteResult
 * and logFlushResult using atomic access.
 * In addition to the shared variable, each backend has a private copy of
 * both in LogwrtResult, which is updated when convenient.
 *
 * The request bookkeeping is simpler: there is a shared XLogCtl->LogwrtRqst
 * (protected by info_lck), but we don't need to cache any copies of it.
 *
 * info_lck is only held long enough to read/update the protected variables,
 * so it's a plain spinlock.  The other locks are held longer (potentially
 * over I/O operations), so we use LWLocks for them.  These locks are:
 *
 * WALBufMappingLock: must be held to replace a page in the WAL buffer cache.
 * It is only held while initializing and changing the mapping.  If the
 * contents of the buffer being replaced haven't been written yet, the mapping
 * lock is released while the write is done, and reacquired afterwards.
 *
 * WALWriteLock: must be held to write WAL buffers to disk (XLogWrite or
 * XLogFlush).
 *
 * ControlFileLock: must be held to read/update control file or create
 * new log file.
 *
 *----------
 */

typedef struct XLogwrtRqst
{
	XLogRecPtr	Write;			/* last byte + 1 to write out */
	XLogRecPtr	Flush;			/* last byte + 1 to flush */
} XLogwrtRqst;

typedef struct XLogwrtResult
{
	XLogRecPtr	Write;			/* last byte + 1 written out */
	XLogRecPtr	Flush;			/* last byte + 1 flushed */
} XLogwrtResult;

/*
 * Inserting to WAL is protected by a small fixed number of WAL insertion
 * locks. To insert to the WAL, you must hold one of the locks - it doesn't
 * matter which one. To lock out other concurrent insertions, you must hold
 * of them. Each WAL insertion lock consists of a lightweight lock, plus an
 * indicator of how far the insertion has progressed (insertingAt).
 *
 * The insertingAt values are read when a process wants to flush WAL from
 * the in-memory buffers to disk, to check that all the insertions to the
 * region the process is about to write out have finished. You could simply
 * wait for all currently in-progress insertions to finish, but the
 * insertingAt indicator allows you to ignore insertions to later in the WAL,
 * so that you only wait for the insertions that are modifying the buffers
 * you're about to write out.
 *
 * This isn't just an optimization. If all the WAL buffers are dirty, an
 * inserter that's holding a WAL insert lock might need to evict an old WAL
 * buffer, which requires flushing the WAL. If it's possible for an inserter
 * to block on another inserter unnecessarily, deadlock can arise when two
 * inserters holding a WAL insert lock wait for each other to finish their
 * insertion.
 *
 * Small WAL records that don't cross a page boundary never update the value,
 * the WAL record is just copied to the page and the lock is released. But
 * to avoid the deadlock-scenario explained above, the indicator is always
 * updated before sleeping while holding an insertion lock.
 *
 * lastImportantAt contains the LSN of the last important WAL record inserted
 * using a given lock. This value is used to detect if there has been
 * important WAL activity since the last time some action, like a checkpoint,
 * was performed - allowing to not repeat the action if not. The LSN is
 * updated for all insertions, unless the XLOG_MARK_UNIMPORTANT flag was
 * set. lastImportantAt is never cleared, only overwritten by the LSN of newer
 * records.  Tracking the WAL activity directly in WALInsertLock has the
 * advantage of not needing any additional locks to update the value.
 */
typedef struct
{
	LWLock		lock;
	pg_atomic_uint64 insertingAt;
	XLogRecPtr	lastImportantAt;
} WALInsertLock;

/*
 * All the WAL insertion locks are allocated as an array in shared memory. We
 * force the array stride to be a power of 2, which saves a few cycles in
 * indexing, but more importantly also ensures that individual slots don't
 * cross cache line boundaries. (Of course, we have to also ensure that the
 * array start address is suitably aligned.)
 */
typedef union WALInsertLockPadded
{
	WALInsertLock l;
	char		pad[PG_CACHE_LINE_SIZE];
} WALInsertLockPadded;

/* PolarDB wal pipeline begin */

#include "postmaster/polar_wal_pipeliner.h"	

typedef enum polar_wal_pipeline_unflushed_xlog_type_t
{	
	UNFLUSHED_XLOG_SLOT_TYPE_ADD,
	UNFLUSHED_XLOG_SLOT_TYPE_DEL
} polar_wal_pipeline_unflushed_xlog_type_t;

typedef struct polar_wal_pipeline_unflushed_xlog_t
{
	int		   		fd;			/* xlog file handle */
	XLogSegNo  		seg_no;		/* xlog file segment number */
	XLogRecPtr 		end_lsn;	/* xlog file segment max lsn */
	bool	   		need_close;	/* whether to close */
} polar_wal_pipeline_unflushed_xlog_t;

typedef struct polar_wal_pipeline_unflushed_xlog_slot_t
{
	volatile bool 				in_use;		/* slot whether in use */
	polar_wal_pipeline_unflushed_xlog_t   	file_node;	/* xlog file node */
} polar_wal_pipeline_unflushed_xlog_slot_t;

typedef struct polar_wal_pipeline_unflushed_xlog_buffer_t
{
	pg_atomic_uint64 					add_slot_no;
	pg_atomic_uint64 					del_slot_no;
	polar_wal_pipeline_unflushed_xlog_slot_t 	 	*unflushed_xlog_slots;
} polar_wal_pipeline_unflushed_xlog_buffer_t;

typedef polar_wait_object_t polar_wal_pipeline_flush_event_t;

typedef struct polar_wal_pipeline_commit_wait_buffer_t
{
	int slot_count;
	int slot_size;
	polar_wal_pipeline_flush_event_t *flush_event_slots;
} polar_wal_pipeline_commit_wait_buffer_t;

typedef struct polar_wal_pipeline_recent_written_position_buffer_t
{
	/* We must use physical position not logical lsn */
	pg_atomic_uint64 ready_write_position;
	pg_atomic_uint64 *recent_written_position_slots;
} polar_wal_pipeline_recent_written_position_buffer_t;

static void polar_wal_pipeline_stats_init(polar_wal_pipeline_stats_t *stats);
static void polar_wait_obj_stats_init(polar_wait_object_stats_t *stats);
static void polar_wait_obj_init(polar_wait_object_t *wait_obj, 
	pthread_mutexattr_t *mutex_attr, pthread_condattr_t *cond_attr);
static bool polar_wal_pipeline_recent_written_advance(void);
static bool polar_wal_pipeline_recent_written_has_space(XLogRecPtr write_lsn);
static void polar_wal_pipeline_unflushed_xlog_append(int fd, XLogSegNo seg_no, XLogRecPtr end_lsn, bool need_close);
static polar_wal_pipeline_unflushed_xlog_slot_t * 
	polar_wal_pipeline_get_curr_unflushed_xlog_slot(polar_wal_pipeline_unflushed_xlog_type_t slot_type);
static polar_wal_pipeline_unflushed_xlog_slot_t * 
	polar_wal_pipeline_get_next_unflushed_xlog_slot(polar_wal_pipeline_unflushed_xlog_type_t slot_type);
static void polar_wal_pipeline_advance_unflushed_xlog_slot_no(polar_wal_pipeline_unflushed_xlog_type_t slot_type);
static void polar_wal_pipeline_flush_internal(void);
static polar_wal_pipeline_flush_event_t * polar_wal_pipeline_flush_event_get_slot(int slot_no);

static XLogRecPtr polar_get_checkpoint_ptr_nolock(void);
static XLogRecPtr polar_get_xlog_insert_rec_ptr_nolock(void);

extern polar_wal_pipeline_stats_t * polar_wal_pipeline_get_stats(void);
extern polar_wait_object_t * polar_wal_pipeline_get_worker_wait_obj(int thread_no);

/* PolarDB wal pipeline end */

/*
 * Session status of running backup, used for sanity checks in SQL-callable
 * functions to start and stop backups.
 */
static SessionBackupState sessionBackupState = SESSION_BACKUP_NONE;

/*
 * Shared state data for WAL insertion.
 */
typedef struct XLogCtlInsert
{
	slock_t		insertpos_lck;	/* protects CurrBytePos and PrevBytePos */

	/*
	 * CurrBytePos is the end of reserved WAL. The next record will be
	 * inserted at that position. PrevBytePos is the start position of the
	 * previously inserted (or rather, reserved) record - it is copied to the
	 * prev-link of the next record. These are stored as "usable byte
	 * positions" rather than XLogRecPtrs (see XLogBytePosToRecPtr()).
	 */
	uint64		CurrBytePos;
	uint64		PrevBytePos;

	/*
	 * Make sure the above heavily-contended spinlock and byte positions are
	 * on their own cache line. In particular, the RedoRecPtr and full page
	 * write variables below should be on a different cache line. They are
	 * read on every WAL insertion, but updated rarely, and we don't want
	 * those reads to steal the cache line containing Curr/PrevBytePos.
	 */
	char		pad[PG_CACHE_LINE_SIZE];

	/*
	 * fullPageWrites is the authoritative value used by all backends to
	 * determine whether to write full-page image to WAL. This shared value,
	 * instead of the process-local fullPageWrites, is required because, when
	 * full_page_writes is changed by SIGHUP, we must WAL-log it before it
	 * actually affects WAL-logging by backends.  Checkpointer sets at startup
	 * or after SIGHUP.
	 *
	 * To read these fields, you must hold an insertion lock. To modify them,
	 * you must hold ALL the locks.
	 */
	XLogRecPtr	RedoRecPtr;		/* current redo point for insertions */
	bool		fullPageWrites;

	/*
	 * runningBackups is a counter indicating the number of backups currently
	 * in progress. lastBackupStart is the latest checkpoint redo location
	 * used as a starting point for an online backup.
	 */
	int			runningBackups;
	XLogRecPtr	lastBackupStart;

	/*
	 * WAL insertion locks.
	 */
	WALInsertLockPadded *WALInsertLocks;
} XLogCtlInsert;


/*
 * Shared state data for Postmaster state change.
 */
typedef struct PMConsensStateData {
	char leaderAddr[NI_MAXHOST];
	int leaderPort;
	uint64 term;
	uint64 xlogTerm;
	XLogRecPtr xlogUpto;
	TimeLineID xlogTLI;
	int state;
} PMConsensStateData;

/*
 * Total shared-memory state for XLOG.
 */
typedef struct XLogCtlData
{
	XLogCtlInsert Insert;

	/* Protected by info_lck: */
	XLogwrtRqst LogwrtRqst;
	XLogRecPtr	RedoRecPtr;		/* a recent copy of Insert->RedoRecPtr */
	FullTransactionId ckptFullXid;	/* nextXid of latest checkpoint */
	XLogRecPtr	asyncXactLSN;	/* LSN of newest async commit/abort */
	XLogRecPtr	replicationSlotMinLSN;	/* oldest LSN needed by any slot */

	XLogSegNo	lastRemovedSegNo;	/* latest removed/recycled XLOG segment */

	/* Fake LSN counter, for unlogged relations. */
	pg_atomic_uint64 unloggedLSN;

	/* Time and LSN of last xlog segment switch. Protected by WALWriteLock. */
	pg_time_t	lastSegSwitchTime;
	XLogRecPtr	lastSegSwitchLSN;

	/* These are accessed using atomics -- info_lck not needed */
	pg_atomic_uint64 logInsertResult;	/* last byte + 1 inserted to buffers */
	pg_atomic_uint64 logWriteResult;	/* last byte + 1 written out */
	pg_atomic_uint64 logFlushResult;	/* last byte + 1 flushed */

	/*
	 * Latest initialized page in the cache (last byte position + 1).
	 *
	 * To change the identity of a buffer (and InitializedUpTo), you need to
	 * hold WALBufMappingLock.  To change the identity of a buffer that's
	 * still dirty, the old page needs to be written out first, and for that
	 * you need WALWriteLock, and you need to ensure that there are no
	 * in-progress insertions to the page by calling
	 * WaitXLogInsertionsToFinish().
	 */
	XLogRecPtr	InitializedUpTo;

	/*
	 * These values do not change after startup, although the pointed-to pages
	 * and xlblocks values certainly do.  xlblocks values are protected by
	 * WALBufMappingLock.
	 */
	char	   *pages;			/* buffers for unwritten XLOG pages */
	pg_atomic_uint64 *xlblocks; /* 1st byte ptr-s + XLOG_BLCKSZ */
	int			XLogCacheBlck;	/* highest allocated xlog buffer index */

	/*
	 * InsertTimeLineID is the timeline into which new WAL is being inserted
	 * and flushed. It is zero during recovery, and does not change once set.
	 *
	 * If we create a new timeline when the system was started up,
	 * PrevTimeLineID is the old timeline's ID that we forked off from.
	 * Otherwise it's equal to InsertTimeLineID.
	 *
	 * We set these fields while holding info_lck. Most that reads these
	 * values knows that recovery is no longer in progress and so can safely
	 * read the value without a lock, but code that could be run either during
	 * or after recovery can take info_lck while reading these values.
	 */
	TimeLineID	InsertTimeLineID;
	TimeLineID	PrevTimeLineID;

	/*
<<<<<<< HEAD
	 * archiveCleanupCommand is read from recovery.conf but needs to be in
	 * shared memory so that the checkpointer process can access it.
	 */
	char		archiveCleanupCommand[MAXPGPATH];

	/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 * SharedRecoveryState indicates if we're still in crash or archive
	 * recovery.  Protected by info_lck.
	 */
	RecoveryState SharedRecoveryState;

	/*
	 * InstallXLogFileSegmentActive indicates whether the checkpointer should
	 * arrange for future segments by recycling and/or PreallocXlogFiles().
	 * Protected by ControlFileLock.  Only the startup process changes it.  If
	 * true, anyone can use InstallXLogFileSegment().  If false, the startup
	 * process owns the exclusive right to install segments, by reading from
	 * the archive and possibly replacing existing files.
	 */
	bool		InstallXLogFileSegmentActive;

	/*
	 * WalWriterSleeping indicates whether the WAL writer is currently in
	 * low-power mode (and hence should be nudged if an async commit occurs).
	 * Protected by info_lck.
	 */
	bool		WalWriterSleeping;

	/*
	 * During recovery, we keep a copy of the latest checkpoint record here.
	 * lastCheckPointRecPtr points to start of checkpoint record and
	 * lastCheckPointEndPtr points to end+1 of checkpoint record.  Used by the
	 * checkpointer when it wants to create a restartpoint.
	 *
	 * Protected by info_lck.
	 */
	XLogRecPtr	lastCheckPointRecPtr;
	XLogRecPtr	lastCheckPointEndPtr;
	CheckPoint	lastCheckPoint;

	/*
<<<<<<< HEAD
	 * lastReplayedEndRecPtr points to end+1 of the last record successfully
	 * replayed. When we're currently replaying a record, ie. in a redo
	 * function, replayEndRecPtr points to the end+1 of the record being
	 * replayed, otherwise it's equal to lastReplayedEndRecPtr.
	 */
	XLogRecPtr	lastReplayedEndRecPtr;
	TimeLineID	lastReplayedTLI;
	XLogRecPtr	replayEndRecPtr;
	TimeLineID	replayEndTLI;

	XLogRecPtr	lockLastReplayedEndRecPtr;

	/* timestamp of last COMMIT/ABORT record replayed (or being replayed) */
	TimestampTz recoveryLastXTime;

	/*
	 * timestamp of when we started replaying the current chunk of WAL data,
	 * only relevant for replication or archive recovery
	 */
	TimestampTz currentChunkStartTime;
	/* Are we requested to pause recovery? */
	bool		recoveryPause;

	/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 * lastFpwDisableRecPtr points to the start of the last replayed
	 * XLOG_FPW_CHANGE record that instructs full_page_writes is disabled.
	 */
	XLogRecPtr	lastFpwDisableRecPtr;

	slock_t		info_lck;		/* locks shared variables shown above */

	/* POLAR */

	/* Oldest LSN applied by any slot */
	XLogRecPtr	replication_slot_oldest_applied_lsn;
	/* Oldest lock LSN applied by any slot */
	XLogRecPtr	replication_slot_oldest_lock_lsn;
	/*
	 * Latest LSN flushed, all pages which latest_lsn <= consistent_lsn have
	 * been flushed to disk.
	 */
	XLogRecPtr	consistent_lsn;
	/* Record polar node type */
	PolarNodeType polar_node_type;

	/* POLAR: put standbyState into shared memory */
	HotStandbyState		polar_hot_standby_state;

	bool		polar_available_state;

	/* POLAR: Protected by info_lck */
	XLogRecPtr	ConsensusCommit;			/* last byte + 1 consensus committed */
	/* POLAR end */

	/*
	 * POLAR: state change info for PostMaster, Protected by pm_state_lck.
	 */
	bool	pmInStateChange; /* set by Consensus main thread and reset by PostMaster */
	PMConsensStateData	pmState; /* just modified by Consensus main thread */
	slock_t		pm_state_lck;		/* locks shared variables shown above */

	/*
	 * POLAR: state change for StartupProcess, modified by PostMaster
	 * Protected by info_lck.
	 */
	char		PrimaryConnInfoStr[MAXCONNINFO];
	uint64  consensusTerm;
	uint64  consensusLogTerm;
	XLogRecPtr consensusLogUpto;
	TimeLineID consensusLogTLI;
	bool  	becameLeader;
	bool  	inLeaderState;
	slock_t	consens_state_lck;		/* locks shared variables shown above */

	/* 
	 * polar wal pipeline 
	 */

	/* Used for polar wal pipeline thread */
	polar_wait_object_t										*polar_wal_pipeline_wait_objs;

	/* Used for pipeline commit/notify */
	polar_wal_pipeline_commit_wait_buffer_t					polar_wal_pipeline_commit_wait_buffer;
	
	/* Used for pipeline advance continuous written lsn */
	polar_wal_pipeline_recent_written_position_buffer_t 	polar_wal_pipeline_recent_written_position_buffer;

	/* Used for pipeline write/flush xlog file */
	polar_wal_pipeline_unflushed_xlog_buffer_t  			polar_wal_pipeline_unflushed_xlog_buffer;

	/* Every notify worker should has its own last notify lsn */
	XLogRecPtr 												polar_wal_pipeline_last_notify_lsn[POLAR_WAL_PIPELINE_NOTIFY_WORKER_NUM_MAX];

	/* Used for pipeline statistics */
	polar_wal_pipeline_stats_t								polar_wal_pipeline_stats;
	
	/* POLAR: lock to prevent getting wal being removed */
	LWLock polar_initial_datamax_lock;

	/* POLAR: replica node update transation status files via redo rather than copy from shared storage when start */
	bool	polar_replica_update_dirs_by_redo;

	/* POLAR: Used for polar IO fencing */
	polar_io_fencing_t polar_io_fencing;

	/*
	 * POLAR:
	 * During parallel replay, we keep an ring of latest checkpoints. When
	 * creating a restart point, we choose the latest checkpoint whose redo is
	 * just less than polar_logindex_redo_instance->bg_replayed_lsn.
	 *
	 * If there is no checkpoint before bg_replayed_lsn, we will skip the
	 * creating restart point procedure.
	 *
	 * Protected by polar_checkpoint_ringbuf->lock.
	 */
	polar_checkpoint_ringbuf_t polar_checkpoint_ringbuf;
} XLogCtlData;

/*
 * Classification of XLogRecordInsert operations.
 */
typedef enum
{
	WALINSERT_NORMAL,
	WALINSERT_SPECIAL_SWITCH,
	WALINSERT_SPECIAL_CHECKPOINT
} WalInsertClass;

static XLogCtlData *XLogCtl = NULL;

/* a private copy of XLogCtl->Insert.WALInsertLocks, for convenience */
static WALInsertLockPadded *WALInsertLocks = NULL;

/*
 * We maintain an image of pg_control in shared memory.
 */
static ControlFileData *ControlFile = NULL;

/*
 * Calculate the amount of space left on the page after 'endptr'. Beware
 * multiple evaluation!
 */
#define INSERT_FREESPACE(endptr)	\
	(((endptr) % XLOG_BLCKSZ == 0) ? 0 : (XLOG_BLCKSZ - (endptr) % XLOG_BLCKSZ))

/* Macro to advance to next buffer index. */
#define NextBufIdx(idx)		\
		(((idx) == XLogCtl->XLogCacheBlck) ? 0 : ((idx) + 1))

/*
 * XLogRecPtrToBufIdx returns the index of the WAL buffer that holds, or
 * would hold if it was in cache, the page containing 'recptr'.
 */
#define XLogRecPtrToBufIdx(recptr)	\
	(((recptr) / XLOG_BLCKSZ) % (XLogCtl->XLogCacheBlck + 1))

/*
 * These are the number of bytes in a WAL page usable for WAL data.
 */
#define UsableBytesInPage (XLOG_BLCKSZ - SizeOfXLogShortPHD)

/*
<<<<<<< HEAD
 * Convert min_wal_size_mb and max wal_size_mb to equivalent segment count.
 * Rounds down.
 */
#define ConvertToXSegs(x, segsize)	\
	((x) / ((segsize) / (1024 * 1024)))
=======
 * Convert values of GUCs measured in megabytes to equiv. segment count.
 * Rounds down.
 */
#define ConvertToXSegs(x, segsize)	XLogMBVarToSegs((x), (segsize))
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/* The number of bytes in a WAL segment usable for WAL data. */
static int	UsableBytesInSegment;

/*
 * Private, possibly out-of-date copy of shared LogwrtResult.
 * See discussion above.
 */
static XLogwrtResult LogwrtResult = {0, 0};

/*
<<<<<<< HEAD
 * POLAR: Private, possibly out-of-date copy of shared ConsensusCommit 
 */
static XLogRecPtr	ConsensusCommit; /* last byte + 1 consensus committed */

/*
 * Codes indicating where we got a WAL file from during recovery, or where
 * to attempt to get one.
=======
 * Update local copy of shared XLogCtl->log{Write,Flush}Result
 *
 * It's critical that Flush always trails Write, so the order of the reads is
 * important, as is the barrier.  See also XLogWrite.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 */
#define RefreshXLogWriteResult(_target) \
	do { \
		_target.Flush = pg_atomic_read_u64(&XLogCtl->logFlushResult); \
		pg_read_barrier(); \
		_target.Write = pg_atomic_read_u64(&XLogCtl->logWriteResult); \
	} while (0)

/*
 * openLogFile is -1 or a kernel FD for an open log file segment.
 * openLogSegNo identifies the segment, and openLogTLI the corresponding TLI.
 * These variables are only used to write the XLOG, and so will normally refer
 * to the active segment.
 *
 * Note: call Reserve/ReleaseExternalFD to track consumption of this FD.
 */
static int	openLogFile = -1;
static XLogSegNo openLogSegNo = 0;
static TimeLineID openLogTLI = 0;

/*
 * Local copies of equivalent fields in the control file.  When running
 * crash recovery, LocalMinRecoveryPoint is set to InvalidXLogRecPtr as we
 * expect to replay all the WAL available, and updateMinRecoveryPoint is
 * switched to false to prevent any updates while replaying records.
 * Those values are kept consistent as long as crash recovery runs.
 */
<<<<<<< HEAD
static int	readFile = -1;
static XLogSegNo readSegNo = 0;
static uint32 readOff = 0;
static uint32 readLen = 0;
static XLogSource readSource = 0;	/* XLOG_FROM_* code */

/*
 * Keeps track of which source we're currently reading from. This is
 * different from readSource in that this is always set, even when we don't
 * currently have a WAL file open. If lastSourceFailed is set, our last
 * attempt to read from currentSource failed, and we should try another source
 * next.
 */
static XLogSource currentSource = 0;	/* XLOG_FROM_* code */
static bool lastSourceFailed = false;

typedef struct XLogPageReadPrivate
{
	int			emode;
	bool		fetching_ckpt;	/* are we fetching a checkpoint record? */
	bool		randAccess;
} XLogPageReadPrivate;

/*
 * These variables track when we last obtained some WAL data to process,
 * and where we got it from.  (XLogReceiptSource is initially the same as
 * readSource, but readSource gets reset to zero when we don't have data
 * to process right now.  It is also different from currentSource, which
 * also changes when we try to read from a source and fail, while
 * XLogReceiptSource tracks where we last successfully read some WAL.)
 */
static TimestampTz XLogReceiptTime = 0;
static XLogSource XLogReceiptSource = 0;	/* XLOG_FROM_* code */

/* State information for XLOG reading */
static XLogRecPtr ReadRecPtr;	/* start of last record read */
static XLogRecPtr EndRecPtr;	/* end+1 of last record read */

/*
 * Local copies of equivalent fields in the control file.  When running
 * crash recovery, minRecoveryPoint is set to InvalidXLogRecPtr as we
 * expect to replay all the WAL available, and updateMinRecoveryPoint is
 * switched to false to prevent any updates while replaying records.
 * Those values are kept consistent as long as crash recovery runs.
 */
static XLogRecPtr minRecoveryPoint;
static TimeLineID minRecoveryPointTLI;
=======
static XLogRecPtr LocalMinRecoveryPoint;
static TimeLineID LocalMinRecoveryPointTLI;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
static bool updateMinRecoveryPoint = true;

/* For WALInsertLockAcquire/Release functions */
static int	MyLockNo = 0;
static bool holdingAllLocks = false;

#ifdef WAL_DEBUG
static MemoryContext walDebugCxt = NULL;
#endif

static void CleanupAfterArchiveRecovery(TimeLineID EndOfLogTLI,
										XLogRecPtr EndOfLog,
										TimeLineID newTLI);
static void CheckRequiredParameterValues(void);
static void XLogReportParameters(void);
static int	LocalSetXLogInsertAllowed(void);
static void CreateEndOfRecoveryRecord(void);
static XLogRecPtr CreateOverwriteContrecordRecord(XLogRecPtr aborted_lsn,
												  XLogRecPtr pagePtr,
												  TimeLineID newTLI);
static void CheckPointGuts(XLogRecPtr checkPointRedo, int flags);
static void KeepLogSeg(XLogRecPtr recptr, XLogSegNo *logSegNo);

static void AdvanceXLInsertBuffer(XLogRecPtr upto, TimeLineID tli,
								  bool opportunistic);
static void XLogWrite(XLogwrtRqst WriteRqst, TimeLineID tli, bool flexible);
static bool InstallXLogFileSegment(XLogSegNo *segno, char *tmppath,
								   bool find_free, XLogSegNo max_segno,
								   TimeLineID tli);
static void XLogFileClose(void);
<<<<<<< HEAD
static void PreallocXlogFiles(XLogRecPtr endptr);
static void RemoveOldXlogFiles(XLogSegNo segno, XLogRecPtr lastredoptr, XLogRecPtr endptr);
static void RemoveXlogFile(const char *segname, XLogRecPtr lastredoptr, XLogRecPtr endptr);
=======
static void PreallocXlogFiles(XLogRecPtr endptr, TimeLineID tli);
static void RemoveTempXlogFiles(void);
static void RemoveOldXlogFiles(XLogSegNo segno, XLogRecPtr lastredoptr,
							   XLogRecPtr endptr, TimeLineID insertTLI);
static void RemoveXlogFile(const struct dirent *segment_de,
						   XLogSegNo recycleSegNo, XLogSegNo *endlogSegNo,
						   TimeLineID insertTLI);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
static void UpdateLastRemovedPtr(char *filename);
static void ValidateXLOGDirectoryStructure(void);
static void CleanupBackupHistory(void);
static void UpdateMinRecoveryPoint(XLogRecPtr lsn, bool force);
static bool PerformRecoveryXLogAction(void);
static void InitControlFile(uint64 sysidentifier, uint32 data_checksum_version);
static void WriteControlFile(void);
static void ReadControlFile(void);
static void UpdateControlFile(void);
static char *str_time(pg_time_t tnow);

<<<<<<< HEAD
#ifdef WAL_DEBUG
static void xlog_outrec(StringInfo buf, XLogReaderState *record);
#endif
static void xlog_outdesc(StringInfo buf, XLogReaderState *record);
static void pg_start_backup_callback(int code, Datum arg);
static void pg_stop_backup_callback(int code, Datum arg);
static bool read_backup_label(XLogRecPtr *checkPointLoc,
				  bool *backupEndRequired, bool *backupFromStandby);
static bool read_tablespace_map(List **tablespaces);

static void rm_redo_error_callback(void *arg);

static void CopyXLogRecordToWAL(int write_len, bool isLogSwitch,
					XLogRecData *rdata,
					XLogRecPtr StartPos, XLogRecPtr EndPos);
static void ReserveXLogInsertLocation(XLogRecData *rdata, int size, XLogRecPtr *StartPos,
						  XLogRecPtr *EndPos, XLogRecPtr *PrevPtr, uint32 RbufLen, size_t *RbufPos);
static bool ReserveXLogSwitch(XLogRecPtr *StartPos, XLogRecPtr *EndPos,
				  XLogRecPtr *PrevPtr, uint32 RbufLen, size_t *RbufPos);
=======
static int	get_sync_bit(int method);

static void CopyXLogRecordToWAL(int write_len, bool isLogSwitch,
								XLogRecData *rdata,
								XLogRecPtr StartPos, XLogRecPtr EndPos,
								TimeLineID tli);
static void ReserveXLogInsertLocation(int size, XLogRecPtr *StartPos,
									  XLogRecPtr *EndPos, XLogRecPtr *PrevPtr);
static bool ReserveXLogSwitch(XLogRecPtr *StartPos, XLogRecPtr *EndPos,
							  XLogRecPtr *PrevPtr);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
static XLogRecPtr WaitXLogInsertionsToFinish(XLogRecPtr upto);
static char *GetXLogBuffer(XLogRecPtr ptr, TimeLineID tli);
static XLogRecPtr XLogBytePosToRecPtr(uint64 bytepos);
static XLogRecPtr XLogBytePosToEndRecPtr(uint64 bytepos);
static uint64 XLogRecPtrToBytePos(XLogRecPtr ptr);

static void WALInsertLockAcquire(void);
static void WALInsertLockAcquireExclusive(void);
static void WALInsertLockRelease(void);
static void WALInsertLockUpdateInsertingAt(XLogRecPtr insertingAt);

/* POLAR */
static void polar_postmaster_reset_local_variables(void);
static bool polar_should_check_checkpoint(void);
static bool polar_is_checkpoint_legal(XLogRecPtr redo);
static void polar_wait_consistent_lsn(XLogRecPtr redo, int flags);
static void polar_flush_buffer_for_shutdown(XLogRecPtr redo, int flags);
static void polar_accept_signal_for_checkpoint(int flags);
static void polar_try_to_wake_walreceiver(XLogRecPtr RecPtr, bool fetching_ckpt, XLogRecPtr tliRecPtr);
static int  polar_pread_xlog_page(TimeLineID *prev_timeline, XLogRecPtr targetPagePtr, char *readBuf);
static int  polar_pread_xlog_page_with_xlog_buffer(
		TimeLineID *prev_timeline, XLogRecPtr target_page_ptr, int64 ahead_size, char *readBuf);
static void polar_remove_empty_pread_xlog_buffer(XLogRecPtr targetPagePtr);
static void polar_request_last_restartpoint(void);
static XLogReaderState * polar_reset_xlogreader(XLogReaderState *xlogreader, XLogPageReadPrivate *private);
static void polar_exit_archive_recovery(void);

static bool polar_check_recovery_state_change(void);
static bool polar_check_and_switch_recovery_state(bool *leaderSwitch, bool *logSwitch);
static bool polar_dma_check_local_recovery(XLogRecPtr RecPtr);
static bool polar_dma_recovery_wait_commit(XLogRecPtr RecPtr);
static bool polar_dma_xlog_commit(XLogRecPtr record);
static int polar_dma_xlog_file_read_any(XLogSegNo segno, int emode, TimeLineID tli, int source);
static bool polar_dma_need_file_switch(XLogRecPtr tliRecPtr, bool fetching_ckpt, XLogRecPtr RecPtr);
static bool polar_dma_beyond_consensus_log(XLogRecPtr RecPtr);
static void polar_dma_xlog_truncate(XLogRecPtr resetPtr, TimeLineID tli, int elevel);

static void polar_clean_smgr_cache_if_needed(XLogReaderState *xlogreader);
static void polar_smgr_drop_all_sr(XLogReaderState *xlogreader);
static void polar_update_controlfile_minrecoverypoint(XLogRecPtr min_recovery_point, TimeLineID min_recovery_point_tli);

static void polar_update_curFileTLI(XLogRecPtr ptr);
/* POLAR end */

/*
 * Insert an XLOG record represented by an already-constructed chain of data
 * chunks.  This is a low-level routine; to construct the WAL record header
 * and data, use the higher-level routines in xloginsert.c.
 *
 * If 'fpw_lsn' is valid, it is the oldest LSN among the pages that this
 * WAL record applies to, that were not included in the record as full page
 * images.  If fpw_lsn <= RedoRecPtr, the function does not perform the
 * insertion and returns InvalidXLogRecPtr.  The caller can then recalculate
 * which pages need a full-page image, and retry.  If fpw_lsn is invalid, the
 * record is always inserted.
 *
 * 'flags' gives more in-depth control on the record being inserted. See
 * XLogSetRecordFlags() for details.
 *
 * 'topxid_included' tells whether the top-transaction id is logged along with
 * current subtransaction. See XLogRecordAssemble().
 *
 * The first XLogRecData in the chain must be for the record header, and its
 * data must be MAXALIGNed.  XLogInsertRecord fills in the xl_prev and
 * xl_crc fields in the header, the rest of the header must already be filled
 * by the caller.
 *
 * Returns XLOG pointer to end of record (beginning of next record).
 * This can be used as LSN for data pages affected by the logged action.
 * (LSN is the XLOG point up to which the XLOG must be flushed to disk
 * before the data page can be written out.  This implements the basic
 * WAL rule "write the log before the data".)
 */
XLogRecPtr
XLogInsertRecord(XLogRecData *rdata,
				 XLogRecPtr fpw_lsn,
				 uint8 flags,
				 int num_fpi,
				 bool topxid_included)
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	pg_crc32c	rdata_crc;
	bool		inserted;
	XLogRecord *rechdr = (XLogRecord *) rdata->data;
	uint8		info = rechdr->xl_info & ~XLR_INFO_MASK;
	WalInsertClass class = WALINSERT_NORMAL;
	XLogRecPtr	StartPos;
	XLogRecPtr	EndPos;
	bool		prevDoPageWrites = doPageWrites;
<<<<<<< HEAD

	/* POLAR: The reserved start point from rbuf */
	size_t		RbufPos = 0;
	/* POLAR: The reserved length for rbuf */
	uint32		RbufLen = 0;
	/* POLAR: The flashback point lsn */
	XLogRecPtr	fbpoint_lsn = InvalidXLogRecPtr;
	/* POLAR: The flashback log enable? */
	bool		flashback_log_enable = polar_is_flog_enabled(flog_instance);
=======
	TimeLineID	insertTLI;

	/* Does this record type require special handling? */
	if (unlikely(rechdr->xl_rmid == RM_XLOG_ID))
	{
		if (info == XLOG_SWITCH)
			class = WALINSERT_SPECIAL_SWITCH;
		else if (info == XLOG_CHECKPOINT_REDO)
			class = WALINSERT_SPECIAL_CHECKPOINT;
	}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* we assume that all of the record header is in the first chunk */
	Assert(rdata->len >= SizeOfXLogRecord);

	/* POLAR: Calc length to reserve from xlog queue */
	if (POLAR_LOGINDEX_ENABLE_XLOG_QUEUE())
		RbufLen = polar_xlog_reserve_size(rdata);
	/* Polar end */

	/* cross-check on whether we should be here or not */
	if (!XLogInsertAllowed())
	{
		elog(ERROR, "cannot make new WAL entries during recovery");
	}


	/*
	 * Given that we're not in recovery, InsertTimeLineID is set and can't
	 * change, so we can read it without a lock.
	 */
	insertTLI = XLogCtl->InsertTimeLineID;

	/*----------
	 *
	 * We have now done all the preparatory work we can without holding a
	 * lock or modifying shared state. From here on, inserting the new WAL
	 * record to the shared WAL buffer cache is a two-step process:
	 *
	 * 1. Reserve the right amount of space from the WAL. The current head of
	 *	  reserved space is kept in Insert->CurrBytePos, and is protected by
	 *	  insertpos_lck.
	 *
	 * 2. Copy the record to the reserved WAL space. This involves finding the
	 *	  correct WAL buffer containing the reserved space, and copying the
	 *	  record in place. This can be done concurrently in multiple processes.
	 *
	 * To keep track of which insertions are still in-progress, each concurrent
	 * inserter acquires an insertion lock. In addition to just indicating that
	 * an insertion is in progress, the lock tells others how far the inserter
	 * has progressed. There is a small fixed number of insertion locks,
	 * determined by polar_wal_buffer_insert_locks. When an inserter crosses a page
	 * boundary, it updates the value stored in the lock to the how far it has
	 * inserted, to allow the previous buffer to be flushed.
	 *
	 * Holding onto an insertion lock also protects RedoRecPtr and
	 * fullPageWrites from changing until the insertion is finished.
	 *
	 * Step 2 can usually be done completely in parallel. If the required WAL
	 * page is not initialized yet, you have to grab WALBufMappingLock to
	 * initialize it, but the WAL writer tries to do that ahead of insertions
	 * to avoid that from happening in the critical path.
	 *
	 *----------
	 */
	START_CRIT_SECTION();

	if (likely(class == WALINSERT_NORMAL))
	{
		WALInsertLockAcquire();

<<<<<<< HEAD
	/*
	 * Check to see if my copy of RedoRecPtr is out of date. If so, may have
	 * to go back and have the caller recompute everything. This can only
	 * happen just after a checkpoint, so it's better to be slow in this case
	 * and fast otherwise.
	 *
	 * Also check to see if fullPageWrites or forcePageWrites was just turned
	 * on; if we weren't already doing full-page writes then go back and
	 * recompute.
	 *
	 * If we aren't doing full-page writes then RedoRecPtr doesn't actually
	 * affect the contents of the XLOG record, so we'll update our local copy
	 * but not force a recomputation.  (If doPageWrites was just turned off,
	 * we could recompute the record without full pages, but we choose not to
	 * bother.)
	 */
	if (RedoRecPtr != Insert->RedoRecPtr)
	{
		Assert(RedoRecPtr < Insert->RedoRecPtr);
		RedoRecPtr = Insert->RedoRecPtr;
	}
	doPageWrites = (Insert->fullPageWrites || Insert->forcePageWrites);

	if (flashback_log_enable)
		fbpoint_lsn = polar_get_curr_fbpoint_lsn(flog_instance->buf_ctl);

	if (doPageWrites &&
		(!prevDoPageWrites ||
		 (fpw_lsn != InvalidXLogRecPtr && fpw_lsn <= RedoRecPtr)))
	{
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		/*
		 * Check to see if my copy of RedoRecPtr is out of date. If so, may
		 * have to go back and have the caller recompute everything. This can
		 * only happen just after a checkpoint, so it's better to be slow in
		 * this case and fast otherwise.
		 *
		 * Also check to see if fullPageWrites was just turned on or there's a
		 * running backup (which forces full-page writes); if we weren't
		 * already doing full-page writes then go back and recompute.
		 *
		 * If we aren't doing full-page writes then RedoRecPtr doesn't
		 * actually affect the contents of the XLOG record, so we'll update
		 * our local copy but not force a recomputation.  (If doPageWrites was
		 * just turned off, we could recompute the record without full pages,
		 * but we choose not to bother.)
		 */
		if (RedoRecPtr != Insert->RedoRecPtr)
		{
			Assert(RedoRecPtr < Insert->RedoRecPtr);
			RedoRecPtr = Insert->RedoRecPtr;
		}
		doPageWrites = (Insert->fullPageWrites || Insert->runningBackups > 0);

<<<<<<< HEAD
	/*
	 * Reserve space for the record in the WAL. This also sets the xl_prev
	 * pointer.
	 */
	if (isLogSwitch)
		inserted = ReserveXLogSwitch(&StartPos, &EndPos, &rechdr->xl_prev, RbufLen, &RbufPos);
	else
	{
		ReserveXLogInsertLocation(rdata, rechdr->xl_tot_len, &StartPos, &EndPos,
								  &rechdr->xl_prev, RbufLen, &RbufPos);
=======
		if (doPageWrites &&
			(!prevDoPageWrites ||
			 (fpw_lsn != InvalidXLogRecPtr && fpw_lsn <= RedoRecPtr)))
		{
			/*
			 * Oops, some buffer now needs to be backed up that the caller
			 * didn't back up.  Start over.
			 */
			WALInsertLockRelease();
			END_CRIT_SECTION();
			return InvalidXLogRecPtr;
		}

		/*
		 * Reserve space for the record in the WAL. This also sets the xl_prev
		 * pointer.
		 */
		ReserveXLogInsertLocation(rechdr->xl_tot_len, &StartPos, &EndPos,
								  &rechdr->xl_prev);

		/* Normal records are always inserted. */
		inserted = true;
	}
	else if (class == WALINSERT_SPECIAL_SWITCH)
	{
		/*
		 * In order to insert an XLOG_SWITCH record, we need to hold all of
		 * the WAL insertion locks, not just one, so that no one else can
		 * begin inserting a record until we've figured out how much space
		 * remains in the current WAL segment and claimed all of it.
		 *
		 * Nonetheless, this case is simpler than the normal cases handled
		 * below, which must check for changes in doPageWrites and RedoRecPtr.
		 * Those checks are only needed for records that can contain buffer
		 * references, and an XLOG_SWITCH record never does.
		 */
		Assert(fpw_lsn == InvalidXLogRecPtr);
		WALInsertLockAcquireExclusive();
		inserted = ReserveXLogSwitch(&StartPos, &EndPos, &rechdr->xl_prev);
	}
	else
	{
		Assert(class == WALINSERT_SPECIAL_CHECKPOINT);

		/*
		 * We need to update both the local and shared copies of RedoRecPtr,
		 * which means that we need to hold all the WAL insertion locks.
		 * However, there can't be any buffer references, so as above, we need
		 * not check RedoRecPtr before inserting the record; we just need to
		 * update it afterwards.
		 */
		Assert(fpw_lsn == InvalidXLogRecPtr);
		WALInsertLockAcquireExclusive();
		ReserveXLogInsertLocation(rechdr->xl_tot_len, &StartPos, &EndPos,
								  &rechdr->xl_prev);
		RedoRecPtr = Insert->RedoRecPtr = StartPos;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		inserted = true;
	}

	if (inserted)
	{
		/*
		 * Now that xl_prev has been filled in, calculate CRC of the record
		 * header.
		 */
		rdata_crc = rechdr->xl_crc;
		COMP_CRC32C(rdata_crc, rechdr, offsetof(XLogRecord, xl_crc));
		FIN_CRC32C(rdata_crc);
		rechdr->xl_crc = rdata_crc;

		/*
		 * All the record data, including the header, is now ready to be
		 * inserted. Copy the record in the space reserved.
		 */
		CopyXLogRecordToWAL(rechdr->xl_tot_len,
							class == WALINSERT_SPECIAL_SWITCH, rdata,
							StartPos, EndPos, insertTLI);

		/*
		 * Unless record is flagged as not important, update LSN of last
		 * important record in the current slot. When holding all locks, just
		 * update the first one.
		 */
		if ((flags & XLOG_MARK_UNIMPORTANT) == 0)
		{
			int			lockno = holdingAllLocks ? 0 : MyLockNo;

			WALInsertLocks[lockno].l.lastImportantAt = StartPos;
		}
	}
	else
	{
		/*
		 * This was an xlog-switch record, but the current insert location was
		 * already exactly at the beginning of a segment, so there was no need
		 * to do anything.
		 */
	}

	/*
	 * Done! Let others know that we're finished.
	 */
	WALInsertLockRelease();

	END_CRIT_SECTION();

	MarkCurrentTransactionIdLoggedIfAny();

<<<<<<< HEAD
	/* POLAR: must be inside CRIT_SECTION.
	 * Hold off signal to avoid mess up queue data.
	 */
	if (inserted && POLAR_LOGINDEX_ENABLE_XLOG_QUEUE())
	{
		if (polar_xlog_send_queue_push(polar_logindex_redo_instance->xlog_queue,
									   RbufPos, rdata, RbufLen, EndPos,
									   EndPos - StartPos)
			&& ProcGlobal->walwriterLatch != NULL)
		{
			SetLatch(ProcGlobal->walwriterLatch);
			if (unlikely(polar_enable_debug))
			{
				ereport(LOG, (errmsg("%s push %X/%X to queue", __func__,
							(uint32) (EndPos >> 32),
							(uint32) EndPos)));
			}

			/* In polar wal pipeline mode, should wakeup wal pipeliner */
			if (POLAR_WAL_PIPELINER_READY())
				polar_wal_pipeliner_wakeup();
		}
	}


	END_CRIT_SECTION();
=======
	/*
	 * Mark top transaction id is logged (if needed) so that we should not try
	 * to log it again with the next WAL record in the current subtransaction.
	 */
	if (topxid_included)
		MarkSubxactTopXidLogged();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Update shared LogwrtRqst.Write, if we crossed page boundary.
	 */
	if (StartPos / XLOG_BLCKSZ != EndPos / XLOG_BLCKSZ)
	{
		SpinLockAcquire(&XLogCtl->info_lck);
		/* advance global request to include new block(s) */
		if (XLogCtl->LogwrtRqst.Write < EndPos)
			XLogCtl->LogwrtRqst.Write = EndPos;
<<<<<<< HEAD
		/* update local result copy while I have the chance */
		LogwrtResult = XLogCtl->LogwrtResult;
		ConsensusCommit = XLogCtl->ConsensusCommit;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		SpinLockRelease(&XLogCtl->info_lck);
		RefreshXLogWriteResult(LogwrtResult);
	}

	/*
	 * If this was an XLOG_SWITCH record, flush the record and the empty
	 * padding space that fills the rest of the segment, and perform
	 * end-of-segment actions (eg, notifying archiver).
	 */
	if (class == WALINSERT_SPECIAL_SWITCH)
	{
		TRACE_POSTGRESQL_WAL_SWITCH();
		XLogFlush(EndPos);

		/*
		 * Even though we reserved the rest of the segment for us, which is
		 * reflected in EndPos, we return a pointer to just the end of the
		 * xlog-switch record.
		 */
		if (inserted)
		{
			EndPos = StartPos + SizeOfXLogRecord;
			if (StartPos / XLOG_BLCKSZ != EndPos / XLOG_BLCKSZ)
			{
				uint64		offset = XLogSegmentOffset(EndPos, wal_segment_size);

				if (offset == EndPos % XLOG_BLCKSZ)
					EndPos += SizeOfXLogLongPHD;
				else
					EndPos += SizeOfXLogShortPHD;
			}
		}
	}

#ifdef WAL_DEBUG
	if (XLOG_DEBUG)
	{
		static XLogReaderState *debug_reader = NULL;
		XLogRecord *record;
		DecodedXLogRecord *decoded;
		StringInfoData buf;
		StringInfoData recordBuf;
		char	   *errormsg = NULL;
		MemoryContext oldCxt;

		oldCxt = MemoryContextSwitchTo(walDebugCxt);

		initStringInfo(&buf);
		appendStringInfo(&buf, "INSERT @ %X/%X: ", LSN_FORMAT_ARGS(EndPos));

		/*
		 * We have to piece together the WAL record data from the XLogRecData
		 * entries, so that we can pass it to the rm_desc function as one
		 * contiguous chunk.
		 */
		initStringInfo(&recordBuf);
		for (; rdata != NULL; rdata = rdata->next)
			appendBinaryStringInfo(&recordBuf, rdata->data, rdata->len);

		/* We also need temporary space to decode the record. */
		record = (XLogRecord *) recordBuf.data;
		decoded = (DecodedXLogRecord *)
			palloc(DecodeXLogRecordRequiredSpace(record->xl_tot_len));

		if (!debug_reader)
			debug_reader = XLogReaderAllocate(wal_segment_size, NULL,
											  XL_ROUTINE(.page_read = NULL,
														 .segment_open = NULL,
														 .segment_close = NULL),
											  NULL);
		if (!debug_reader)
		{
			appendStringInfoString(&buf, "error decoding record: out of memory while allocating a WAL reading processor");
		}
		else if (!DecodeXLogRecord(debug_reader,
								   decoded,
								   record,
								   EndPos,
								   &errormsg))
		{
			appendStringInfo(&buf, "error decoding record: %s",
							 errormsg ? errormsg : "no error message");
		}
		else
		{
			appendStringInfoString(&buf, " - ");

			debug_reader->record = decoded;
			xlog_outdesc(&buf, debug_reader);
			debug_reader->record = NULL;
		}
		elog(LOG, "%s", buf.data);

		pfree(decoded);
		pfree(buf.data);
		pfree(recordBuf.data);
		MemoryContextSwitchTo(oldCxt);
	}
#endif

	/*
	 * Update our global variables
	 */
	ProcLastRecPtr = StartPos;
	XactLastRecEnd = EndPos;

<<<<<<< HEAD
	/* POLAR: Insert the flashback log after everything is ok */
	if (flashback_log_enable && inserted)
	{
		XLogRecord *rechdr = (XLogRecord *) rdata->data;
		uint8 info = rechdr->xl_info & ~XLR_INFO_MASK;

		polar_flashback_log_insert_from_xlog(fbpoint_lsn, info);
=======
	/* Report WAL traffic to the instrumentation. */
	if (inserted)
	{
		pgWalUsage.wal_bytes += rechdr->xl_tot_len;
		pgWalUsage.wal_records++;
		pgWalUsage.wal_fpi += num_fpi;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	return EndPos;
}

/*
 * Reserves the right amount of space for a record of given size from the WAL.
 * *StartPos is set to the beginning of the reserved section, *EndPos to
 * its end+1. *PrevPtr is set to the beginning of the previous record; it is
 * used to set the xl_prev of this record.
 *
 * This is the performance critical part of XLogInsert that must be serialized
 * across backends. The rest can happen mostly in parallel. Try to keep this
 * section as short as possible, insertpos_lck can be heavily contended on a
 * busy system.
 *
 * NB: The space calculation here must match the code in CopyXLogRecordToWAL,
 * where we actually copy the record to the reserved space.
 *
 * NB: Testing shows that XLogInsertRecord runs faster if this code is inlined;
 * however, because there are two call sites, the compiler is reluctant to
 * inline. We use pg_attribute_always_inline here to try to convince it.
 */
<<<<<<< HEAD
static void
ReserveXLogInsertLocation(XLogRecData *rdata, int size, XLogRecPtr *StartPos, XLogRecPtr *EndPos,
						  XLogRecPtr *PrevPtr, uint32 RbufLen, size_t *RbufPos)
=======
static pg_attribute_always_inline void
ReserveXLogInsertLocation(int size, XLogRecPtr *StartPos, XLogRecPtr *EndPos,
						  XLogRecPtr *PrevPtr)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	uint64		startbytepos;
	uint64		endbytepos;
	uint64		prevbytepos;
	/* POLAR: check polar xlog quueue is enabled */
	bool        polar_xlog = POLAR_LOGINDEX_ENABLE_XLOG_QUEUE();

	size = MAXALIGN(size);

	/* All (non xlog-switch) records should contain data. */
	Assert(size > SizeOfXLogRecord);

	/*
	 * The duration the spinlock needs to be held is minimized by minimizing
	 * the calculations that have to be done while holding the lock. The
	 * current tip of reserved WAL is kept in CurrBytePos, as a byte position
	 * that only counts "usable" bytes in WAL, that is, it excludes all WAL
	 * page headers. The mapping between "usable" byte positions and physical
	 * positions (XLogRecPtrs) can be done outside the locked region, and
	 * because the usable byte position doesn't include any headers, reserving
	 * X bytes from WAL is almost as simple as "CurrBytePos += X".
	 */

	for (;;)
	{
		SpinLockAcquire(&Insert->insertpos_lck);
		if (likely(polar_xlog))
		{
			if(!POLAR_XLOG_QUEUE_FREE_SIZE(polar_logindex_redo_instance->xlog_queue, RbufLen))
			{
				SpinLockRelease(&Insert->insertpos_lck);
				POLAR_XLOG_QUEUE_FREE_UP(polar_logindex_redo_instance->xlog_queue, RbufLen);;
				continue;
			}
			*RbufPos = POLAR_XLOG_QUEUE_RESERVE(polar_logindex_redo_instance->xlog_queue, RbufLen);
		}
		startbytepos = Insert->CurrBytePos;
		endbytepos = startbytepos + size;
		prevbytepos = Insert->PrevBytePos;
		Insert->CurrBytePos = endbytepos;
		Insert->PrevBytePos = startbytepos;
		SpinLockRelease(&Insert->insertpos_lck);
		break;
	}

	if (likely(polar_xlog))
		POLAR_XLOG_QUEUE_SET_PKT_LEN(polar_logindex_redo_instance->xlog_queue, *RbufPos, RbufLen);

	*StartPos = XLogBytePosToRecPtr(startbytepos);
	*EndPos = XLogBytePosToEndRecPtr(endbytepos);
	*PrevPtr = XLogBytePosToRecPtr(prevbytepos);

	/*
	 * Check that the conversions between "usable byte positions" and
	 * XLogRecPtrs work consistently in both directions.
	 */
	Assert(XLogRecPtrToBytePos(*StartPos) == startbytepos);
	Assert(XLogRecPtrToBytePos(*EndPos) == endbytepos);
	Assert(XLogRecPtrToBytePos(*PrevPtr) == prevbytepos);
}

/*
 * Like ReserveXLogInsertLocation(), but for an xlog-switch record.
 *
 * A log-switch record is handled slightly differently. The rest of the
 * segment will be reserved for this insertion, as indicated by the returned
 * *EndPos value. However, if we are already at the beginning of the current
 * segment, *StartPos and *EndPos are set to the current location without
 * reserving any space, and the function returns false.
*/
static bool
ReserveXLogSwitch(XLogRecPtr *StartPos, XLogRecPtr *EndPos, XLogRecPtr *PrevPtr, uint32 RbufLen, size_t *RbufPos)
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	uint64		startbytepos;
	uint64		endbytepos;
	uint64		prevbytepos;
	uint32		size = MAXALIGN(SizeOfXLogRecord);
	XLogRecPtr	ptr;
	uint32		segleft;
	/* POLAR: check polar xlog quueue is enabled */
	bool        polar_xlog = POLAR_LOGINDEX_ENABLE_XLOG_QUEUE();

	/*
	 * These calculations are a bit heavy-weight to be done while holding a
	 * spinlock, but since we're holding all the WAL insertion locks, there
	 * are no other inserters competing for it. GetXLogInsertRecPtr() does
	 * compete for it, but that's not called very frequently.
	 */
	for (;;)
	{
		SpinLockAcquire(&Insert->insertpos_lck);

		startbytepos = Insert->CurrBytePos;

		ptr = XLogBytePosToEndRecPtr(startbytepos);
		if (XLogSegmentOffset(ptr, wal_segment_size) == 0)
		{
			SpinLockRelease(&Insert->insertpos_lck);
			*EndPos = *StartPos = ptr;
			return false;
		}

		if (likely(polar_xlog))
		{
			if(!POLAR_XLOG_QUEUE_FREE_SIZE(polar_logindex_redo_instance->xlog_queue, RbufLen))
			{
				SpinLockRelease(&Insert->insertpos_lck);
				POLAR_XLOG_QUEUE_FREE_UP(polar_logindex_redo_instance->xlog_queue, RbufLen);
				continue;
			}
			*RbufPos = POLAR_XLOG_QUEUE_RESERVE(polar_logindex_redo_instance->xlog_queue, RbufLen);
		}

		endbytepos = startbytepos + size;
		prevbytepos = Insert->PrevBytePos;

		*StartPos = XLogBytePosToRecPtr(startbytepos);
		*EndPos = XLogBytePosToEndRecPtr(endbytepos);

		segleft = wal_segment_size - XLogSegmentOffset(*EndPos, wal_segment_size);
		if (segleft != wal_segment_size)
		{
			/* consume the rest of the segment */
			*EndPos += segleft;
			endbytepos = XLogRecPtrToBytePos(*EndPos);
		}
		Insert->CurrBytePos = endbytepos;
		Insert->PrevBytePos = startbytepos;

		SpinLockRelease(&Insert->insertpos_lck);
		break;
	}

	if (likely(polar_xlog))
		POLAR_XLOG_QUEUE_SET_PKT_LEN(polar_logindex_redo_instance->xlog_queue, *RbufPos, RbufLen);

	*PrevPtr = XLogBytePosToRecPtr(prevbytepos);

	Assert(XLogSegmentOffset(*EndPos, wal_segment_size) == 0);
	Assert(XLogRecPtrToBytePos(*EndPos) == endbytepos);
	Assert(XLogRecPtrToBytePos(*StartPos) == startbytepos);
	Assert(XLogRecPtrToBytePos(*PrevPtr) == prevbytepos);

	return true;
}

/*
 * Subroutine of XLogInsertRecord.  Copies a WAL record to an already-reserved
 * area in the WAL.
 */
static void
CopyXLogRecordToWAL(int write_len, bool isLogSwitch, XLogRecData *rdata,
					XLogRecPtr StartPos, XLogRecPtr EndPos, TimeLineID tli)
{
	char	   *currpos;
	int			freespace;
	int			written;
	XLogRecPtr	CurrPos;
	XLogPageHeader pagehdr;

	/*
	 * Get a pointer to the right place in the right WAL buffer to start
	 * inserting to.
	 */
	CurrPos = StartPos;
	currpos = GetXLogBuffer(CurrPos, tli);
	freespace = INSERT_FREESPACE(CurrPos);

	/*
	 * there should be enough space for at least the first field (xl_tot_len)
	 * on this page.
	 */
	Assert(freespace >= sizeof(uint32));

	/* Copy record data */
	written = 0;
	while (rdata != NULL)
	{
		const char *rdata_data = rdata->data;
		int			rdata_len = rdata->len;

		while (rdata_len > freespace)
		{
			/*
			 * Write what fits on this page, and continue on the next page.
			 */
			Assert(CurrPos % XLOG_BLCKSZ >= SizeOfXLogShortPHD || freespace == 0);
			memcpy(currpos, rdata_data, freespace);

			if (POLAR_WAL_PIPELINER_READY())
			{
				if (freespace != 0)
					polar_wal_pipeline_recent_written_add_link(CurrPos, CurrPos + freespace);
			}

			rdata_data += freespace;
			rdata_len -= freespace;
			written += freespace;
			CurrPos += freespace;

			/*
			 * Get pointer to beginning of next page, and set the xlp_rem_len
			 * in the page header. Set XLP_FIRST_IS_CONTRECORD.
			 *
			 * It's safe to set the contrecord flag and xlp_rem_len without a
			 * lock on the page. All the other flags were already set when the
			 * page was initialized, in AdvanceXLInsertBuffer, and we're the
			 * only backend that needs to set the contrecord flag.
			 */
			currpos = GetXLogBuffer(CurrPos, tli);
			pagehdr = (XLogPageHeader) currpos;
			pagehdr->xlp_rem_len = write_len - written;
			pagehdr->xlp_info |= XLP_FIRST_IS_CONTRECORD;

			/* skip over the page header */
			if (XLogSegmentOffset(CurrPos, wal_segment_size) == 0)
			{
				CurrPos += SizeOfXLogLongPHD;
				currpos += SizeOfXLogLongPHD;
			}
			else
			{
				CurrPos += SizeOfXLogShortPHD;
				currpos += SizeOfXLogShortPHD;
			}
			freespace = INSERT_FREESPACE(CurrPos);
		}

		Assert(CurrPos % XLOG_BLCKSZ >= SizeOfXLogShortPHD || rdata_len == 0);
		memcpy(currpos, rdata_data, rdata_len);

		if (POLAR_WAL_PIPELINER_READY())
		{
			if (rdata_len != 0)
				polar_wal_pipeline_recent_written_add_link(CurrPos, CurrPos + rdata_len);
		}

		currpos += rdata_len;
		CurrPos += rdata_len;
		freespace -= rdata_len;
		written += rdata_len;

		rdata = rdata->next;
	}
	Assert(written == write_len);

	/*
	 * If this was an xlog-switch, it's not enough to write the switch record,
	 * we also have to consume all the remaining space in the WAL segment.  We
	 * have already reserved that space, but we need to actually fill it.
	 */
	if (isLogSwitch && XLogSegmentOffset(CurrPos, wal_segment_size) != 0)
	{
		/* An xlog-switch record doesn't contain any data besides the header */
		Assert(write_len == SizeOfXLogRecord);

		/* Assert that we did reserve the right amount of space */
		Assert(XLogSegmentOffset(EndPos, wal_segment_size) == 0);

		if (POLAR_WAL_PIPELINER_READY())
		{
			if (freespace != 0)
				polar_wal_pipeline_recent_written_add_link(CurrPos, CurrPos + freespace);
		}

		/* Use up all the remaining space on the current page */
		CurrPos += freespace;

		/*
		 * Cause all remaining pages in the segment to be flushed, leaving the
		 * XLog position where it should be, at the start of the next segment.
		 * We do this one page at a time, to make sure we don't deadlock
		 * against ourselves if wal_buffers < wal_segment_size.
		 */
		while (CurrPos < EndPos)
		{
			/*
			 * The minimal action to flush the page would be to call
			 * WALInsertLockUpdateInsertingAt(CurrPos) followed by
			 * AdvanceXLInsertBuffer(...).  The page would be left initialized
			 * mostly to zeros, except for the page header (always the short
			 * variant, as this is never a segment's first page).
			 *
			 * The large vistas of zeros are good for compressibility, but the
			 * headers interrupting them every XLOG_BLCKSZ (with values that
			 * differ from page to page) are not.  The effect varies with
			 * compression tool, but bzip2 for instance compresses about an
			 * order of magnitude worse if those headers are left in place.
			 *
			 * Rather than complicating AdvanceXLInsertBuffer itself (which is
			 * called in heavily-loaded circumstances as well as this lightly-
			 * loaded one) with variant behavior, we just use GetXLogBuffer
			 * (which itself calls the two methods we need) to get the pointer
			 * and zero most of the page.  Then we just zero the page header.
			 */
			currpos = GetXLogBuffer(CurrPos, tli);
			MemSet(currpos, 0, SizeOfXLogShortPHD);

			if (POLAR_WAL_PIPELINER_READY())
				polar_wal_pipeline_recent_written_add_link(CurrPos, CurrPos + XLOG_BLCKSZ);

			CurrPos += XLOG_BLCKSZ;
		}
	}
	else
	{
		if (POLAR_WAL_PIPELINER_READY())
		{
			if (CurrPos != EndPos)
				polar_wal_pipeline_recent_written_add_link(CurrPos, EndPos);
		}

		/* Align the end position, so that the next record starts aligned */
		CurrPos = MAXALIGN64(CurrPos);
	}

	if (CurrPos != EndPos)
		ereport(PANIC,
				errcode(ERRCODE_DATA_CORRUPTED),
				errmsg_internal("space reserved for WAL record does not match what was written"));
}

/*
 * Acquire a WAL insertion lock, for inserting to WAL.
 */
static void
WALInsertLockAcquire(void)
{
	bool		immed;

	/*
	 * It doesn't matter which of the WAL insertion locks we acquire, so try
	 * the one we used last time.  If the system isn't particularly busy, it's
	 * a good bet that it's still available, and it's good to have some
	 * affinity to a particular lock so that you don't unnecessarily bounce
	 * cache lines between processes when there's no contention.
	 *
	 * If this is the first time through in this backend, pick a lock
	 * (semi-)randomly.  This allows the locks to be used evenly if you have a
	 * lot of very short connections.
	 */
	static int	lockToTry = -1;

	if (lockToTry == -1)
<<<<<<< HEAD
		lockToTry = MyProc->pgprocno % polar_wal_buffer_insert_locks;
=======
		lockToTry = MyProcNumber % NUM_XLOGINSERT_LOCKS;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	MyLockNo = lockToTry;

	/*
	 * The insertingAt value is initially set to 0, as we don't know our
	 * insert location yet.
	 */
	immed = LWLockAcquire(&WALInsertLocks[MyLockNo].l.lock, LW_EXCLUSIVE);
	if (!immed)
	{
		/*
		 * If we couldn't get the lock immediately, try another lock next
		 * time.  On a system with more insertion locks than concurrent
		 * inserters, this causes all the inserters to eventually migrate to a
		 * lock that no-one else is using.  On a system with more inserters
		 * than locks, it still helps to distribute the inserters evenly
		 * across the locks.
		 */
		lockToTry = (lockToTry + 1) % polar_wal_buffer_insert_locks;
	}
}

/*
 * Acquire all WAL insertion locks, to prevent other backends from inserting
 * to WAL.
 */
static void
WALInsertLockAcquireExclusive(void)
{
	int			i;

	/*
	 * When holding all the locks, all but the last lock's insertingAt
	 * indicator is set to 0xFFFFFFFFFFFFFFFF, which is higher than any real
	 * XLogRecPtr value, to make sure that no-one blocks waiting on those.
	 */
	for (i = 0; i < polar_wal_buffer_insert_locks - 1; i++)
	{
		LWLockAcquire(&WALInsertLocks[i].l.lock, LW_EXCLUSIVE);
		LWLockUpdateVar(&WALInsertLocks[i].l.lock,
						&WALInsertLocks[i].l.insertingAt,
						PG_UINT64_MAX);
	}
	/* Variable value reset to 0 at release */
	LWLockAcquire(&WALInsertLocks[i].l.lock, LW_EXCLUSIVE);

	holdingAllLocks = true;
}

/*
 * Release our insertion lock (or locks, if we're holding them all).
 *
 * NB: Reset all variables to 0, so they cause LWLockWaitForVar to block the
 * next time the lock is acquired.
 */
static void
WALInsertLockRelease(void)
{
	if (holdingAllLocks)
	{
		int			i;

		for (i = 0; i < polar_wal_buffer_insert_locks; i++)
			LWLockReleaseClearVar(&WALInsertLocks[i].l.lock,
								  &WALInsertLocks[i].l.insertingAt,
								  0);

		holdingAllLocks = false;
	}
	else
	{
		LWLockReleaseClearVar(&WALInsertLocks[MyLockNo].l.lock,
							  &WALInsertLocks[MyLockNo].l.insertingAt,
							  0);
	}
}

/*
 * Update our insertingAt value, to let others know that we've finished
 * inserting up to that point.
 */
static void
WALInsertLockUpdateInsertingAt(XLogRecPtr insertingAt)
{
	if (holdingAllLocks)
	{
		/*
		 * We use the last lock to mark our actual position, see comments in
		 * WALInsertLockAcquireExclusive.
		 */
		LWLockUpdateVar(&WALInsertLocks[polar_wal_buffer_insert_locks - 1].l.lock,
						&WALInsertLocks[polar_wal_buffer_insert_locks - 1].l.insertingAt,
						insertingAt);
	}
	else
		LWLockUpdateVar(&WALInsertLocks[MyLockNo].l.lock,
						&WALInsertLocks[MyLockNo].l.insertingAt,
						insertingAt);
}

/*
 * Wait for any WAL insertions < upto to finish.
 *
 * Returns the location of the oldest insertion that is still in-progress.
 * Any WAL prior to that point has been fully copied into WAL buffers, and
 * can be flushed out to disk. Because this waits for any insertions older
 * than 'upto' to finish, the return value is always >= 'upto'.
 *
 * Note: When you are about to write out WAL, you must call this function
 * *before* acquiring WALWriteLock, to avoid deadlocks. This function might
 * need to wait for an insertion to finish (or at least advance to next
 * uninitialized page), and the inserter might need to evict an old WAL buffer
 * to make room for a new one, which in turn requires WALWriteLock.
 */
static XLogRecPtr
WaitXLogInsertionsToFinish(XLogRecPtr upto)
{
	uint64		bytepos;
	XLogRecPtr	inserted;
	XLogRecPtr	reservedUpto;
	XLogRecPtr	finishedUpto;
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	int			i;

	if (MyProc == NULL)
		elog(PANIC, "cannot wait without a PGPROC structure");

	/*
	 * Check if there's any work to do.  Use a barrier to ensure we get the
	 * freshest value.
	 */
	inserted = pg_atomic_read_membarrier_u64(&XLogCtl->logInsertResult);
	if (upto <= inserted)
		return inserted;

	/* Read the current insert position */
	SpinLockAcquire(&Insert->insertpos_lck);
	bytepos = Insert->CurrBytePos;
	SpinLockRelease(&Insert->insertpos_lck);
	reservedUpto = XLogBytePosToEndRecPtr(bytepos);

	/*
	 * No-one should request to flush a piece of WAL that hasn't even been
	 * reserved yet. However, it can happen if there is a block with a bogus
	 * LSN on disk, for example. XLogFlush checks for that situation and
	 * complains, but only after the flush. Here we just assume that to mean
	 * that all WAL that has been reserved needs to be finished. In this
	 * corner-case, the return value can be smaller than 'upto' argument.
	 */
	if (upto > reservedUpto)
	{
		ereport(LOG,
				(errmsg("request to flush past end of generated WAL; request %X/%X, current position %X/%X",
						LSN_FORMAT_ARGS(upto), LSN_FORMAT_ARGS(reservedUpto))));
		upto = reservedUpto;
	}

	/*
	 * Loop through all the locks, sleeping on any in-progress insert older
	 * than 'upto'.
	 *
	 * finishedUpto is our return value, indicating the point upto which all
	 * the WAL insertions have been finished. Initialize it to the head of
	 * reserved WAL, and as we iterate through the insertion locks, back it
	 * out for any insertion that's still in progress.
	 */
	finishedUpto = reservedUpto;
	for (i = 0; i < polar_wal_buffer_insert_locks; i++)
	{
		XLogRecPtr	insertingat = InvalidXLogRecPtr;

		do
		{
			/*
			 * See if this insertion is in progress.  LWLockWaitForVar will
			 * wait for the lock to be released, or for the 'value' to be set
			 * by a LWLockUpdateVar call.  When a lock is initially acquired,
			 * its value is 0 (InvalidXLogRecPtr), which means that we don't
			 * know where it's inserting yet.  We will have to wait for it. If
			 * it's a small insertion, the record will most likely fit on the
			 * same page and the inserter will release the lock without ever
			 * calling LWLockUpdateVar.  But if it has to sleep, it will
			 * advertise the insertion point with LWLockUpdateVar before
			 * sleeping.
			 *
			 * In this loop we are only waiting for insertions that started
			 * before WaitXLogInsertionsToFinish was called.  The lack of
			 * memory barriers in the loop means that we might see locks as
			 * "unused" that have since become used.  This is fine because
			 * they only can be used for later insertions that we would not
			 * want to wait on anyway.  Not taking a lock to acquire the
			 * current insertingAt value means that we might see older
			 * insertingAt values.  This is also fine, because if we read a
			 * value too old, we will add ourselves to the wait queue, which
			 * contains atomic operations.
			 */
			if (LWLockWaitForVar(&WALInsertLocks[i].l.lock,
								 &WALInsertLocks[i].l.insertingAt,
								 insertingat, &insertingat))
			{
				/* the lock was free, so no insertion in progress */
				insertingat = InvalidXLogRecPtr;
				break;
			}

			/*
			 * This insertion is still in progress. Have to wait, unless the
			 * inserter has proceeded past 'upto'.
			 */
		} while (insertingat < upto);

		if (insertingat != InvalidXLogRecPtr && insertingat < finishedUpto)
			finishedUpto = insertingat;
	}

	/*
	 * Advance the limit we know to have been inserted and return the freshest
	 * value we know of, which might be beyond what we requested if somebody
	 * is concurrently doing this with an 'upto' pointer ahead of us.
	 */
	finishedUpto = pg_atomic_monotonic_advance_u64(&XLogCtl->logInsertResult,
												   finishedUpto);

	return finishedUpto;
}

/*
 * Get a pointer to the right location in the WAL buffer containing the
 * given XLogRecPtr.
 *
 * If the page is not initialized yet, it is initialized. That might require
 * evicting an old dirty buffer from the buffer cache, which means I/O.
 *
 * The caller must ensure that the page containing the requested location
 * isn't evicted yet, and won't be evicted. The way to ensure that is to
 * hold onto a WAL insertion lock with the insertingAt position set to
 * something <= ptr. GetXLogBuffer() will update insertingAt if it needs
 * to evict an old page from the buffer. (This means that once you call
 * GetXLogBuffer() with a given 'ptr', you must not access anything before
 * that point anymore, and must not call GetXLogBuffer() with an older 'ptr'
 * later, because older buffers might be recycled already)
 */
static char *
GetXLogBuffer(XLogRecPtr ptr, TimeLineID tli)
{
	int			idx;
	XLogRecPtr	endptr;
	static uint64 cachedPage = 0;
	static char *cachedPos = NULL;
	XLogRecPtr	expectedEndPtr;

	/*
	 * Fast path for the common case that we need to access again the same
	 * page as last time.
	 */
	if (ptr / XLOG_BLCKSZ == cachedPage)
	{
		Assert(((XLogPageHeader) cachedPos)->xlp_magic == XLOG_PAGE_MAGIC);
		Assert(((XLogPageHeader) cachedPos)->xlp_pageaddr == ptr - (ptr % XLOG_BLCKSZ));
		return cachedPos + ptr % XLOG_BLCKSZ;
	}

	/*
	 * The XLog buffer cache is organized so that a page is always loaded to a
	 * particular buffer.  That way we can easily calculate the buffer a given
	 * page must be loaded into, from the XLogRecPtr alone.
	 */
	idx = XLogRecPtrToBufIdx(ptr);

	/*
	 * See what page is loaded in the buffer at the moment. It could be the
	 * page we're looking for, or something older. It can't be anything newer
	 * - that would imply the page we're looking for has already been written
	 * out to disk and evicted, and the caller is responsible for making sure
	 * that doesn't happen.
	 *
	 * We don't hold a lock while we read the value. If someone is just about
	 * to initialize or has just initialized the page, it's possible that we
	 * get InvalidXLogRecPtr. That's ok, we'll grab the mapping lock (in
	 * AdvanceXLInsertBuffer) and retry if we see anything other than the page
	 * we're looking for.
	 */
	expectedEndPtr = ptr;
	expectedEndPtr += XLOG_BLCKSZ - ptr % XLOG_BLCKSZ;

	endptr = pg_atomic_read_u64(&XLogCtl->xlblocks[idx]);
	if (expectedEndPtr != endptr)
	{
		XLogRecPtr	initializedUpto;

		/*
		 * Before calling AdvanceXLInsertBuffer(), which can block, let others
		 * know how far we're finished with inserting the record.
		 *
		 * NB: If 'ptr' points to just after the page header, advertise a
		 * position at the beginning of the page rather than 'ptr' itself. If
		 * there are no other insertions running, someone might try to flush
		 * up to our advertised location. If we advertised a position after
		 * the page header, someone might try to flush the page header, even
		 * though page might actually not be initialized yet. As the first
		 * inserter on the page, we are effectively responsible for making
		 * sure that it's initialized, before we let insertingAt to move past
		 * the page header.
		 */
		if (ptr % XLOG_BLCKSZ == SizeOfXLogShortPHD &&
			XLogSegmentOffset(ptr, wal_segment_size) > XLOG_BLCKSZ)
			initializedUpto = ptr - SizeOfXLogShortPHD;
		else if (ptr % XLOG_BLCKSZ == SizeOfXLogLongPHD &&
				 XLogSegmentOffset(ptr, wal_segment_size) < XLOG_BLCKSZ)
			initializedUpto = ptr - SizeOfXLogLongPHD;
		else
			initializedUpto = ptr;

		WALInsertLockUpdateInsertingAt(initializedUpto);

		AdvanceXLInsertBuffer(ptr, tli, false);
		endptr = pg_atomic_read_u64(&XLogCtl->xlblocks[idx]);

		if (expectedEndPtr != endptr)
			elog(PANIC, "could not find WAL buffer for %X/%X",
				 LSN_FORMAT_ARGS(ptr));
	}
	else
	{
		/*
		 * Make sure the initialization of the page is visible to us, and
		 * won't arrive later to overwrite the WAL data we write on the page.
		 */
		pg_memory_barrier();
	}

	/*
	 * Found the buffer holding this page. Return a pointer to the right
	 * offset within the page.
	 */
	cachedPage = ptr / XLOG_BLCKSZ;
	cachedPos = XLogCtl->pages + idx * (Size) XLOG_BLCKSZ;

	Assert(((XLogPageHeader) cachedPos)->xlp_magic == XLOG_PAGE_MAGIC);
	Assert(((XLogPageHeader) cachedPos)->xlp_pageaddr == ptr - (ptr % XLOG_BLCKSZ));

	return cachedPos + ptr % XLOG_BLCKSZ;
}

/*
 * Read WAL data directly from WAL buffers, if available. Returns the number
 * of bytes read successfully.
 *
 * Fewer than 'count' bytes may be read if some of the requested WAL data has
 * already been evicted.
 *
 * No locks are taken.
 *
 * Caller should ensure that it reads no further than LogwrtResult.Write
 * (which should have been updated by the caller when determining how far to
 * read). The 'tli' argument is only used as a convenient safety check so that
 * callers do not read from WAL buffers on a historical timeline.
 */
Size
WALReadFromBuffers(char *dstbuf, XLogRecPtr startptr, Size count,
				   TimeLineID tli)
{
	char	   *pdst = dstbuf;
	XLogRecPtr	recptr = startptr;
	XLogRecPtr	inserted;
	Size		nbytes = count;

	if (RecoveryInProgress() || tli != GetWALInsertionTimeLine())
		return 0;

	Assert(!XLogRecPtrIsInvalid(startptr));

	/*
	 * Caller should ensure that the requested data has been inserted into WAL
	 * buffers before we try to read it.
	 */
	inserted = pg_atomic_read_u64(&XLogCtl->logInsertResult);
	if (startptr + count > inserted)
		ereport(ERROR,
				errmsg("cannot read past end of generated WAL: requested %X/%X, current position %X/%X",
					   LSN_FORMAT_ARGS(startptr + count),
					   LSN_FORMAT_ARGS(inserted)));

	/*
	 * Loop through the buffers without a lock. For each buffer, atomically
	 * read and verify the end pointer, then copy the data out, and finally
	 * re-read and re-verify the end pointer.
	 *
	 * Once a page is evicted, it never returns to the WAL buffers, so if the
	 * end pointer matches the expected end pointer before and after we copy
	 * the data, then the right page must have been present during the data
	 * copy. Read barriers are necessary to ensure that the data copy actually
	 * happens between the two verification steps.
	 *
	 * If either verification fails, we simply terminate the loop and return
	 * with the data that had been already copied out successfully.
	 */
	while (nbytes > 0)
	{
		uint32		offset = recptr % XLOG_BLCKSZ;
		int			idx = XLogRecPtrToBufIdx(recptr);
		XLogRecPtr	expectedEndPtr;
		XLogRecPtr	endptr;
		const char *page;
		const char *psrc;
		Size		npagebytes;

		/*
		 * Calculate the end pointer we expect in the xlblocks array if the
		 * correct page is present.
		 */
		expectedEndPtr = recptr + (XLOG_BLCKSZ - offset);

		/*
		 * First verification step: check that the correct page is present in
		 * the WAL buffers.
		 */
		endptr = pg_atomic_read_u64(&XLogCtl->xlblocks[idx]);
		if (expectedEndPtr != endptr)
			break;

		/*
		 * The correct page is present (or was at the time the endptr was
		 * read; must re-verify later). Calculate pointer to source data and
		 * determine how much data to read from this page.
		 */
		page = XLogCtl->pages + idx * (Size) XLOG_BLCKSZ;
		psrc = page + offset;
		npagebytes = Min(nbytes, XLOG_BLCKSZ - offset);

		/*
		 * Ensure that the data copy and the first verification step are not
		 * reordered.
		 */
		pg_read_barrier();

		/* data copy */
		memcpy(pdst, psrc, npagebytes);

		/*
		 * Ensure that the data copy and the second verification step are not
		 * reordered.
		 */
		pg_read_barrier();

		/*
		 * Second verification step: check that the page we read from wasn't
		 * evicted while we were copying the data.
		 */
		endptr = pg_atomic_read_u64(&XLogCtl->xlblocks[idx]);
		if (expectedEndPtr != endptr)
			break;

		pdst += npagebytes;
		recptr += npagebytes;
		nbytes -= npagebytes;
	}

	Assert(pdst - dstbuf <= count);

	return pdst - dstbuf;
}

/*
 * Converts a "usable byte position" to XLogRecPtr. A usable byte position
 * is the position starting from the beginning of WAL, excluding all WAL
 * page headers.
 */
static XLogRecPtr
XLogBytePosToRecPtr(uint64 bytepos)
{
	uint64		fullsegs;
	uint64		fullpages;
	uint64		bytesleft;
	uint32		seg_offset;
	XLogRecPtr	result;

	fullsegs = bytepos / UsableBytesInSegment;
	bytesleft = bytepos % UsableBytesInSegment;

	if (bytesleft < XLOG_BLCKSZ - SizeOfXLogLongPHD)
	{
		/* fits on first page of segment */
		seg_offset = bytesleft + SizeOfXLogLongPHD;
	}
	else
	{
		/* account for the first page on segment with long header */
		seg_offset = XLOG_BLCKSZ;
		bytesleft -= XLOG_BLCKSZ - SizeOfXLogLongPHD;

		fullpages = bytesleft / UsableBytesInPage;
		bytesleft = bytesleft % UsableBytesInPage;

		seg_offset += fullpages * XLOG_BLCKSZ + bytesleft + SizeOfXLogShortPHD;
	}

	XLogSegNoOffsetToRecPtr(fullsegs, seg_offset, wal_segment_size, result);

	return result;
}

/*
 * Like XLogBytePosToRecPtr, but if the position is at a page boundary,
 * returns a pointer to the beginning of the page (ie. before page header),
 * not to where the first xlog record on that page would go to. This is used
 * when converting a pointer to the end of a record.
 */
static XLogRecPtr
XLogBytePosToEndRecPtr(uint64 bytepos)
{
	uint64		fullsegs;
	uint64		fullpages;
	uint64		bytesleft;
	uint32		seg_offset;
	XLogRecPtr	result;

	fullsegs = bytepos / UsableBytesInSegment;
	bytesleft = bytepos % UsableBytesInSegment;

	if (bytesleft < XLOG_BLCKSZ - SizeOfXLogLongPHD)
	{
		/* fits on first page of segment */
		if (bytesleft == 0)
			seg_offset = 0;
		else
			seg_offset = bytesleft + SizeOfXLogLongPHD;
	}
	else
	{
		/* account for the first page on segment with long header */
		seg_offset = XLOG_BLCKSZ;
		bytesleft -= XLOG_BLCKSZ - SizeOfXLogLongPHD;

		fullpages = bytesleft / UsableBytesInPage;
		bytesleft = bytesleft % UsableBytesInPage;

		if (bytesleft == 0)
			seg_offset += fullpages * XLOG_BLCKSZ + bytesleft;
		else
			seg_offset += fullpages * XLOG_BLCKSZ + bytesleft + SizeOfXLogShortPHD;
	}

	XLogSegNoOffsetToRecPtr(fullsegs, seg_offset, wal_segment_size, result);

	return result;
}

/*
 * Convert an XLogRecPtr to a "usable byte position".
 */
static uint64
XLogRecPtrToBytePos(XLogRecPtr ptr)
{
	uint64		fullsegs;
	uint32		fullpages;
	uint32		offset;
	uint64		result;

	XLByteToSeg(ptr, fullsegs, wal_segment_size);

	fullpages = (XLogSegmentOffset(ptr, wal_segment_size)) / XLOG_BLCKSZ;
	offset = ptr % XLOG_BLCKSZ;

	if (fullpages == 0)
	{
		result = fullsegs * UsableBytesInSegment;
		if (offset > 0)
		{
			Assert(offset >= SizeOfXLogLongPHD);
			result += offset - SizeOfXLogLongPHD;
		}
	}
	else
	{
		result = fullsegs * UsableBytesInSegment +
			(XLOG_BLCKSZ - SizeOfXLogLongPHD) + /* account for first page */
			(fullpages - 1) * UsableBytesInPage;	/* full pages */
		if (offset > 0)
		{
			Assert(offset >= SizeOfXLogShortPHD);
			result += offset - SizeOfXLogShortPHD;
		}
	}

	return result;
}

/*
 * Initialize XLOG buffers, writing out old buffers if they still contain
 * unwritten data, upto the page containing 'upto'. Or if 'opportunistic' is
 * true, initialize as many pages as we can without having to write out
 * unwritten data. Any new pages are initialized to zeros, with pages headers
 * initialized properly.
 */
static void
AdvanceXLInsertBuffer(XLogRecPtr upto, TimeLineID tli, bool opportunistic)
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	int			nextidx;
	XLogRecPtr	OldPageRqstPtr;
	XLogwrtRqst WriteRqst;
	XLogRecPtr	NewPageEndPtr = InvalidXLogRecPtr;
	XLogRecPtr	NewPageBeginPtr;
	XLogPageHeader NewPage;
	int			npages pg_attribute_unused() = 0;

	LWLockAcquire(WALBufMappingLock, LW_EXCLUSIVE);

	/*
	 * Now that we have the lock, check if someone initialized the page
	 * already.
	 */
	while (upto >= XLogCtl->InitializedUpTo || opportunistic)
	{
		nextidx = XLogRecPtrToBufIdx(XLogCtl->InitializedUpTo);

		/*
		 * Get ending-offset of the buffer page we need to replace (this may
		 * be zero if the buffer hasn't been used yet).  Fall through if it's
		 * already written out.
		 */
		OldPageRqstPtr = pg_atomic_read_u64(&XLogCtl->xlblocks[nextidx]);
		if (LogwrtResult.Write < OldPageRqstPtr)
		{
			/*
			 * Nope, got work to do. If we just want to pre-initialize as much
			 * as we can without flushing, give up now.
			 */
			if (opportunistic)
				break;

			/* Advance shared memory write request position */
			SpinLockAcquire(&XLogCtl->info_lck);
			if (XLogCtl->LogwrtRqst.Write < OldPageRqstPtr)
				XLogCtl->LogwrtRqst.Write = OldPageRqstPtr;
<<<<<<< HEAD
			LogwrtResult = XLogCtl->LogwrtResult;
			ConsensusCommit = XLogCtl->ConsensusCommit;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			SpinLockRelease(&XLogCtl->info_lck);

			/*
			 * Acquire an up-to-date LogwrtResult value and see if we still
			 * need to write it or if someone else already did.
			 */
			RefreshXLogWriteResult(LogwrtResult);
			if (LogwrtResult.Write < OldPageRqstPtr)
			{
				/*
				 * Must acquire write lock. Release WALBufMappingLock first,
				 * to make sure that all insertions that we need to wait for
				 * can finish (up to this same position). Otherwise we risk
				 * deadlock.
				 */
				LWLockRelease(WALBufMappingLock);

<<<<<<< HEAD
				if (POLAR_WAL_PIPELINER_READY())
					polar_wal_pipeline_commit_wait(OldPageRqstPtr);
				else
				{
					WaitXLogInsertionsToFinish(OldPageRqstPtr);

					LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);

					LogwrtResult = XLogCtl->LogwrtResult;
					if (LogwrtResult.Write >= OldPageRqstPtr)
					{
						/* OK, someone wrote it already */
						LWLockRelease(WALWriteLock);
					}
					else
					{
						/* Have to write it ourselves */
						TRACE_POSTGRESQL_WAL_BUFFER_WRITE_DIRTY_START();
						WriteRqst.Write = OldPageRqstPtr;
						WriteRqst.Flush = 0;
						XLogWrite(WriteRqst, false);
						LWLockRelease(WALWriteLock);
						TRACE_POSTGRESQL_WAL_BUFFER_WRITE_DIRTY_DONE();
					}
=======
				WaitXLogInsertionsToFinish(OldPageRqstPtr);

				LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);

				RefreshXLogWriteResult(LogwrtResult);
				if (LogwrtResult.Write >= OldPageRqstPtr)
				{
					/* OK, someone wrote it already */
					LWLockRelease(WALWriteLock);
				}
				else
				{
					/* Have to write it ourselves */
					TRACE_POSTGRESQL_WAL_BUFFER_WRITE_DIRTY_START();
					WriteRqst.Write = OldPageRqstPtr;
					WriteRqst.Flush = 0;
					XLogWrite(WriteRqst, tli, false);
					LWLockRelease(WALWriteLock);
					PendingWalStats.wal_buffers_full++;
					TRACE_POSTGRESQL_WAL_BUFFER_WRITE_DIRTY_DONE();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				}

				/* Re-acquire WALBufMappingLock and retry */
				LWLockAcquire(WALBufMappingLock, LW_EXCLUSIVE);
				continue;
			}
		}

		/*
		 * Now the next buffer slot is free and we can set it up to be the
		 * next output page.
		 */
		NewPageBeginPtr = XLogCtl->InitializedUpTo;
		NewPageEndPtr = NewPageBeginPtr + XLOG_BLCKSZ;

		Assert(XLogRecPtrToBufIdx(NewPageBeginPtr) == nextidx);

		NewPage = (XLogPageHeader) (XLogCtl->pages + nextidx * (Size) XLOG_BLCKSZ);

		/*
		 * Mark the xlblock with InvalidXLogRecPtr and issue a write barrier
		 * before initializing. Otherwise, the old page may be partially
		 * zeroed but look valid.
		 */
		pg_atomic_write_u64(&XLogCtl->xlblocks[nextidx], InvalidXLogRecPtr);
		pg_write_barrier();

		/*
		 * Be sure to re-zero the buffer so that bytes beyond what we've
		 * written will look like zeroes and not valid XLOG records...
		 */
		MemSet((char *) NewPage, 0, XLOG_BLCKSZ);

		/*
		 * Fill the new page's header
		 */
		NewPage->xlp_magic = XLOG_PAGE_MAGIC;

		/* NewPage->xlp_info = 0; */	/* done by memset */
		NewPage->xlp_tli = tli;
		NewPage->xlp_pageaddr = NewPageBeginPtr;

		/* NewPage->xlp_rem_len = 0; */	/* done by memset */

		/*
		 * If online backup is not in progress, mark the header to indicate
		 * that WAL records beginning in this page have removable backup
		 * blocks.  This allows the WAL archiver to know whether it is safe to
		 * compress archived WAL data by transforming full-block records into
		 * the non-full-block format.  It is sufficient to record this at the
		 * page level because we force a page switch (in fact a segment
		 * switch) when starting a backup, so the flag will be off before any
		 * records can be written during the backup.  At the end of a backup,
		 * the last page will be marked as all unsafe when perhaps only part
		 * is unsafe, but at worst the archiver would miss the opportunity to
		 * compress a few records.
		 */
		if (Insert->runningBackups == 0)
			NewPage->xlp_info |= XLP_BKP_REMOVABLE;

		/*
		 * If first page of an XLOG segment file, make it a long header.
		 */
		if ((XLogSegmentOffset(NewPage->xlp_pageaddr, wal_segment_size)) == 0)
		{
			XLogLongPageHeader NewLongPage = (XLogLongPageHeader) NewPage;

			NewLongPage->xlp_sysid = ControlFile->system_identifier;
			NewLongPage->xlp_seg_size = wal_segment_size;
			NewLongPage->xlp_xlog_blcksz = XLOG_BLCKSZ;
			NewPage->xlp_info |= XLP_LONG_HEADER;
		}

		/*
		 * Make sure the initialization of the page becomes visible to others
		 * before the xlblocks update. GetXLogBuffer() reads xlblocks without
		 * holding a lock.
		 */
		pg_write_barrier();

		pg_atomic_write_u64(&XLogCtl->xlblocks[nextidx], NewPageEndPtr);
		XLogCtl->InitializedUpTo = NewPageEndPtr;

		npages++;
	}
	LWLockRelease(WALBufMappingLock);

#ifdef WAL_DEBUG
	if (XLOG_DEBUG && npages > 0)
	{
		elog(DEBUG1, "initialized %d pages, up to %X/%X",
			 npages, LSN_FORMAT_ARGS(NewPageEndPtr));
	}
#endif
}

/*
 * Calculate CheckPointSegments based on max_wal_size_mb and
 * checkpoint_completion_target.
 */
static void
CalculateCheckpointSegments(void)
{
	double		target;

	/*-------
	 * Calculate the distance at which to trigger a checkpoint, to avoid
	 * exceeding max_wal_size_mb. This is based on two assumptions:
	 *
	 * a) we keep WAL for only one checkpoint cycle (prior to PG11 we kept
	 *    WAL for two checkpoint cycles to allow us to recover from the
	 *    secondary checkpoint if the first checkpoint failed, though we
	 *    only did this on the primary anyway, not on standby. Keeping just
	 *    one checkpoint simplifies processing and reduces disk space in
	 *    many smaller databases.)
	 * b) during checkpoint, we consume checkpoint_completion_target *
	 *	  number of segments consumed between checkpoints.
	 *-------
	 */
	target = (double) ConvertToXSegs(max_wal_size_mb, wal_segment_size) /
		(1.0 + CheckPointCompletionTarget);

	/* round down */
	CheckPointSegments = (int) target;

	if (CheckPointSegments < 1)
		CheckPointSegments = 1;
}

void
assign_max_wal_size(int newval, void *extra)
{
	max_wal_size_mb = newval;
	CalculateCheckpointSegments();
}

void
assign_checkpoint_completion_target(double newval, void *extra)
{
	CheckPointCompletionTarget = newval;
	CalculateCheckpointSegments();
}

bool
check_wal_segment_size(int *newval, void **extra, GucSource source)
{
	if (!IsValidWalSegSize(*newval))
	{
		GUC_check_errdetail("The WAL segment size must be a power of two between 1 MB and 1 GB.");
		return false;
	}

	return true;
}

/*
 * GUC check_hook for max_slot_wal_keep_size
 *
 * We don't allow the value of max_slot_wal_keep_size other than -1 during the
 * binary upgrade. See start_postmaster() in pg_upgrade for more details.
 */
bool
check_max_slot_wal_keep_size(int *newval, void **extra, GucSource source)
{
	if (IsBinaryUpgrade && *newval != -1)
	{
		GUC_check_errdetail("\"%s\" must be set to -1 during binary upgrade mode.",
							"max_slot_wal_keep_size");
		return false;
	}

	return true;
}

/*
 * At a checkpoint, how many WAL segments to recycle as preallocated future
 * XLOG segments? Returns the highest segment that should be preallocated.
 */
static XLogSegNo
XLOGfileslop(XLogRecPtr lastredoptr)
{
	XLogSegNo	minSegNo;
	XLogSegNo	maxSegNo;
	double		distance;
	XLogSegNo	recycleSegNo;

	/*
	 * Calculate the segment numbers that min_wal_size_mb and max_wal_size_mb
	 * correspond to. Always recycle enough segments to meet the minimum, and
	 * remove enough segments to stay below the maximum.
	 */
	minSegNo = lastredoptr / wal_segment_size +
		ConvertToXSegs(min_wal_size_mb, wal_segment_size) - 1;
	maxSegNo = lastredoptr / wal_segment_size +
		ConvertToXSegs(max_wal_size_mb, wal_segment_size) - 1;

	/*
	 * Between those limits, recycle enough segments to get us through to the
	 * estimated end of next checkpoint.
	 *
	 * To estimate where the next checkpoint will finish, assume that the
	 * system runs steadily consuming CheckPointDistanceEstimate bytes between
	 * every checkpoint.
	 */
	distance = (1.0 + CheckPointCompletionTarget) * CheckPointDistanceEstimate;
	/* add 10% for good measure. */
	distance *= 1.10;

	recycleSegNo = (XLogSegNo) ceil(((double) lastredoptr + distance) /
									wal_segment_size);

	if (recycleSegNo < minSegNo)
		recycleSegNo = minSegNo;
	if (recycleSegNo > maxSegNo)
		recycleSegNo = maxSegNo;

	return recycleSegNo;
}

/*
 * Check whether we've consumed enough xlog space that a checkpoint is needed.
 *
 * new_segno indicates a log file that has just been filled up (or read
 * during recovery). We measure the distance from RedoRecPtr to new_segno
 * and see if that exceeds CheckPointSegments.
 *
 * Note: it is caller's responsibility that RedoRecPtr is up-to-date.
 */
bool
XLogCheckpointNeeded(XLogSegNo new_segno)
{
	XLogSegNo	old_segno;

	XLByteToSeg(RedoRecPtr, old_segno, wal_segment_size);

	if (new_segno >= old_segno + (uint64) (CheckPointSegments - 1))
		return true;
	return false;
}

/*
 * Write and/or fsync the log at least as far as WriteRqst indicates.
 *
 * If flexible == true, we don't have to write as far as WriteRqst, but
 * may stop at any convenient boundary (such as a cache or logfile boundary).
 * This option allows us to avoid uselessly issuing multiple writes when a
 * single one would do.
 *
 * Must be called with WALWriteLock held. WaitXLogInsertionsToFinish(WriteRqst)
 * must be called before grabbing the lock, to make sure the data is ready to
 * write.
 */
static void
XLogWrite(XLogwrtRqst WriteRqst, TimeLineID tli, bool flexible)
{
	bool		ispartialpage;
	bool		last_iteration;
	bool		finishing_seg;
	int			curridx;
	int			npages;
	int			startidx;
	uint32		startoffset;
	bool		polar_async_pwrite = POLAR_ENABLE_PWRITE();

	/* We should always be inside a critical section here */
	Assert(CritSectionCount > 0);

	/*
	 * Update local LogwrtResult (caller probably did this already, but...)
	 */
<<<<<<< HEAD
	if (!POLAR_WAL_PIPELINER_READY())
		LogwrtResult = XLogCtl->LogwrtResult;
=======
	RefreshXLogWriteResult(LogwrtResult);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Since successive pages in the xlog cache are consecutively allocated,
	 * we can usually gather multiple pages together and issue just one
	 * write() call.  npages is the number of pages we have determined can be
	 * written together; startidx is the cache block index of the first one,
	 * and startoffset is the file offset at which it should go. The latter
	 * two variables are only valid when npages > 0, but we must initialize
	 * all of them to keep the compiler quiet.
	 */
	npages = 0;
	startidx = 0;
	startoffset = 0;

	/*
	 * Within the loop, curridx is the cache block index of the page to
	 * consider writing.  Begin at the buffer containing the next unwritten
	 * page, or last partially written page.
	 */
	curridx = XLogRecPtrToBufIdx(LogwrtResult.Write);

	while (LogwrtResult.Write < WriteRqst.Write)
	{
		/*
		 * Make sure we're not ahead of the insert process.  This could happen
		 * if we're passed a bogus WriteRqst.Write that is past the end of the
		 * last page that's been initialized by AdvanceXLInsertBuffer.
		 */
		XLogRecPtr	EndPtr = pg_atomic_read_u64(&XLogCtl->xlblocks[curridx]);

		if (LogwrtResult.Write >= EndPtr)
			elog(PANIC, "xlog write request %X/%X is past end of log %X/%X",
				 LSN_FORMAT_ARGS(LogwrtResult.Write),
				 LSN_FORMAT_ARGS(EndPtr));

		/* Advance LogwrtResult.Write to end of current buffer page */
		LogwrtResult.Write = EndPtr;
		ispartialpage = WriteRqst.Write < LogwrtResult.Write;

		if (!XLByteInPrevSeg(LogwrtResult.Write, openLogSegNo,
							 wal_segment_size))
		{
			/*
			 * Switch to new logfile segment.  We cannot have any pending
			 * pages here (since we dump what we have at segment end).
			 */
			Assert(npages == 0);
			if (openLogFile >= 0)
			{
				if (!POLAR_WAL_PIPELINER_READY())
					XLogFileClose();
			}
			XLByteToPrevSeg(LogwrtResult.Write, openLogSegNo,
							wal_segment_size);
			openLogTLI = tli;

			/* create/use new log file */
			openLogFile = XLogFileInit(openLogSegNo, tli);
			ReserveExternalFD();
		}

		/* Make sure we have the current logfile open */
		if (openLogFile < 0)
		{
			XLByteToPrevSeg(LogwrtResult.Write, openLogSegNo,
							wal_segment_size);
			openLogTLI = tli;
			openLogFile = XLogFileOpen(openLogSegNo, tli);
			ReserveExternalFD();
		}

		/* Add current page to the set of pending pages-to-dump */
		if (npages == 0)
		{
			/* first of group */
			startidx = curridx;
			startoffset = XLogSegmentOffset(LogwrtResult.Write - XLOG_BLCKSZ,
											wal_segment_size);
		}
		npages++;

		/*
		 * Dump the set if this will be the last loop iteration, or if we are
		 * at the last page of the cache area (since the next page won't be
		 * contiguous in memory), or if we are at the end of the logfile
		 * segment.
		 */
		last_iteration = WriteRqst.Write <= LogwrtResult.Write;

		finishing_seg = !ispartialpage &&
			(startoffset + npages * XLOG_BLCKSZ) >= wal_segment_size;

		if (last_iteration ||
			curridx == XLogCtl->XLogCacheBlck ||
			finishing_seg)
		{
			char	   *from;
			Size		nbytes;
			Size		nleft;
<<<<<<< HEAD
			int			written;

			/* Need to seek in the file? */
			if (openLogOff != startoffset)
			{
				/* POLAR: use pwrite to replace lseek + write */
				if (polar_async_pwrite == false)
				{
					if (polar_lseek(openLogFile, (off_t) startoffset, SEEK_SET) < 0)
						ereport(PANIC,
								(errcode_for_file_access(),
								 errmsg("could not seek in log file %s to offset %u: %m",
										XLogFileNameP(ThisTimeLineID, openLogSegNo),
										startoffset)));
				}
				openLogOff = startoffset;
			}
=======
			ssize_t		written;
			instr_time	start;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

			/* OK to write the page(s) */
			from = XLogCtl->pages + startidx * (Size) XLOG_BLCKSZ;
			nbytes = npages * (Size) XLOG_BLCKSZ;
			nleft = nbytes;
			do
			{
				errno = 0;
<<<<<<< HEAD
				/* POLAR: use pwrite to replace lseek + write */
				if (polar_async_pwrite)
				{
					pgstat_report_wait_start(WAIT_EVENT_WAL_PWRITE);
					written = polar_pwrite(openLogFile, from, nleft, startoffset);
				}
				else
				{
					pgstat_report_wait_start(WAIT_EVENT_WAL_WRITE);
					written = polar_write(openLogFile, from, nleft);
				}
=======

				/* Measure I/O timing to write WAL data */
				if (track_wal_io_timing)
					INSTR_TIME_SET_CURRENT(start);
				else
					INSTR_TIME_SET_ZERO(start);

				pgstat_report_wait_start(WAIT_EVENT_WAL_WRITE);
				written = pg_pwrite(openLogFile, from, nleft, startoffset);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				pgstat_report_wait_end();

				/*
				 * Increment the I/O timing and the number of times WAL data
				 * were written out to disk.
				 */
				if (track_wal_io_timing)
				{
					instr_time	end;

					INSTR_TIME_SET_CURRENT(end);
					INSTR_TIME_ACCUM_DIFF(PendingWalStats.wal_write_time, end, start);
				}

				PendingWalStats.wal_write++;

				if (written <= 0)
				{
					char		xlogfname[MAXFNAMELEN];
					int			save_errno;

					if (errno == EINTR)
						continue;

					save_errno = errno;
					XLogFileName(xlogfname, tli, openLogSegNo,
								 wal_segment_size);
					errno = save_errno;
					ereport(PANIC,
							(errcode_for_file_access(),
							 errmsg("could not write to log file \"%s\" at offset %u, length %zu: %m",
									xlogfname, startoffset, nleft)));
				}
				nleft -= written;
				from += written;
				startoffset += written;
			} while (nleft > 0);

			npages = 0;

			/*
			 * If we just wrote the whole last page of a logfile segment,
			 * fsync the segment immediately.  This avoids having to go back
			 * and re-open prior segments when an fsync request comes along
			 * later. Doing it here ensures that one and only one backend will
			 * perform this fsync.
			 *
			 * This is also the right place to notify the Archiver that the
			 * segment is ready to copy to archival storage, and to update the
			 * timer for archive_timeout, and to signal for a checkpoint if
			 * too many logfile segments have been used since the last
			 * checkpoint.
			 */
			if (finishing_seg)
			{
<<<<<<< HEAD
				if (POLAR_WAL_PIPELINER_READY())
=======
				issue_xlog_fsync(openLogFile, openLogSegNo, tli);

				/* signal that we need to wakeup walsenders later */
				WalSndWakeupRequest();

				LogwrtResult.Flush = LogwrtResult.Write;	/* end of page */

				if (XLogArchivingActive())
					XLogArchiveNotifySeg(openLogSegNo, tli);

				XLogCtl->lastSegSwitchTime = (pg_time_t) time(NULL);
				XLogCtl->lastSegSwitchLSN = LogwrtResult.Flush;

				/*
				 * Request a checkpoint if we've consumed too much xlog since
				 * the last one.  For speed, we first check using the local
				 * copy of RedoRecPtr, which might be out of date; if it looks
				 * like a checkpoint is needed, forcibly update RedoRecPtr and
				 * recheck.
				 */
				if (IsUnderPostmaster && XLogCheckpointNeeded(openLogSegNo))
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				{
					polar_wal_pipeline_unflushed_xlog_append(openLogFile, openLogSegNo, LogwrtResult.Write, true);
					LogwrtResult.Flush = LogwrtResult.Write;
				}
				else
				{
					issue_xlog_fsync(openLogFile, openLogSegNo);

					/* signal that we need to wakeup walsenders later */
					WalSndWakeupRequest();

					LogwrtResult.Flush = LogwrtResult.Write;	/* end of page */

					if (XLogArchivingActive())
						XLogArchiveNotifySeg(openLogSegNo);

					XLogCtl->lastSegSwitchTime = (pg_time_t) time(NULL);
					XLogCtl->lastSegSwitchLSN = LogwrtResult.Flush;

					/*
					* Request a checkpoint if we've consumed too much xlog since
					* the last one.  For speed, we first check using the local
					* copy of RedoRecPtr, which might be out of date; if it looks
					* like a checkpoint is needed, forcibly update RedoRecPtr and
					* recheck.
					*/
					if (IsUnderPostmaster && XLogCheckpointNeeded(openLogSegNo))
					{
						(void) GetRedoRecPtr();
						if (XLogCheckpointNeeded(openLogSegNo))
							RequestCheckpoint(CHECKPOINT_CAUSE_XLOG);
					}
				}
			}
		}

		if (ispartialpage)
		{
			/* Only asked to write a partial page */
			LogwrtResult.Write = WriteRqst.Write;
			break;
		}
		curridx = NextBufIdx(curridx);

		/* If flexible, break out of loop as soon as we wrote something */
		if (flexible && npages == 0)
			break;
	}

	Assert(npages == 0);

	/*
	 * If asked to flush, do so
	 */
	if (LogwrtResult.Flush < WriteRqst.Flush &&
		LogwrtResult.Flush < LogwrtResult.Write)
	{
		/*
		 * Could get here without iterating above loop, in which case we might
		 * have no open file or the wrong one.  However, we do not need to
		 * fsync more than one file.
		 */
		if (wal_sync_method != WAL_SYNC_METHOD_OPEN &&
			wal_sync_method != WAL_SYNC_METHOD_OPEN_DSYNC)
		{
			if (openLogFile >= 0 &&
				!XLByteInPrevSeg(LogwrtResult.Write, openLogSegNo,
								 wal_segment_size))
			{
				if (!POLAR_WAL_PIPELINER_READY())
					XLogFileClose();
			}
			if (openLogFile < 0)
			{
				XLByteToPrevSeg(LogwrtResult.Write, openLogSegNo,
								wal_segment_size);
				openLogTLI = tli;
				openLogFile = XLogFileOpen(openLogSegNo, tli);
				ReserveExternalFD();
			}

<<<<<<< HEAD
			if (POLAR_WAL_PIPELINER_READY())
				polar_wal_pipeline_unflushed_xlog_append(openLogFile, openLogSegNo, LogwrtResult.Write, false);
			else
				issue_xlog_fsync(openLogFile, openLogSegNo);
=======
			issue_xlog_fsync(openLogFile, openLogSegNo, tli);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}

		/* signal that we need to wakeup walsenders later */
		WalSndWakeupRequest();

		LogwrtResult.Flush = LogwrtResult.Write;
	}

	/*
	 * Update shared-memory status
	 *
	 * We make sure that the shared 'request' values do not fall behind the
	 * 'result' values.  This is not absolutely essential, but it saves some
	 * code in a couple of places.
	 */
<<<<<<< HEAD
	if (!POLAR_WAL_PIPELINER_READY())
=======
	SpinLockAcquire(&XLogCtl->info_lck);
	if (XLogCtl->LogwrtRqst.Write < LogwrtResult.Write)
		XLogCtl->LogwrtRqst.Write = LogwrtResult.Write;
	if (XLogCtl->LogwrtRqst.Flush < LogwrtResult.Flush)
		XLogCtl->LogwrtRqst.Flush = LogwrtResult.Flush;
	SpinLockRelease(&XLogCtl->info_lck);

	/*
	 * We write Write first, bar, then Flush.  When reading, the opposite must
	 * be done (with a matching barrier in between), so that we always see a
	 * Flush value that trails behind the Write value seen.
	 */
	pg_atomic_write_u64(&XLogCtl->logWriteResult, LogwrtResult.Write);
	pg_write_barrier();
	pg_atomic_write_u64(&XLogCtl->logFlushResult, LogwrtResult.Flush);

#ifdef USE_ASSERT_CHECKING
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	{
		XLogRecPtr	Flush;
		XLogRecPtr	Write;
		XLogRecPtr	Insert;

		Flush = pg_atomic_read_u64(&XLogCtl->logFlushResult);
		pg_read_barrier();
		Write = pg_atomic_read_u64(&XLogCtl->logWriteResult);
		pg_read_barrier();
		Insert = pg_atomic_read_u64(&XLogCtl->logInsertResult);

		/* WAL written to disk is always ahead of WAL flushed */
		Assert(Write >= Flush);

		/* WAL inserted to buffers is always ahead of WAL written */
		Assert(Insert >= Write);
	}
<<<<<<< HEAD
    else
	{
		SpinLockAcquire(&XLogCtl->info_lck);
		XLogCtl->LogwrtResult.Write = LogwrtResult.Write;
		if (XLogCtl->LogwrtRqst.Write < LogwrtResult.Write)
			XLogCtl->LogwrtRqst.Write = LogwrtResult.Write;
		SpinLockRelease(&XLogCtl->info_lck);
	}
=======
#endif
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Record the LSN for an asynchronous transaction commit/abort
 * and nudge the WALWriter if there is work for it to do.
 * (This should not be called for synchronous commits.)
 */
void
XLogSetAsyncXactLSN(XLogRecPtr asyncXactLSN)
{
	XLogRecPtr	WriteRqstPtr = asyncXactLSN;
	bool		sleeping;
	bool		wakeup = false;
	XLogRecPtr	prevAsyncXactLSN;

	SpinLockAcquire(&XLogCtl->info_lck);
<<<<<<< HEAD
	LogwrtResult = XLogCtl->LogwrtResult;
	ConsensusCommit = XLogCtl->ConsensusCommit;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	sleeping = XLogCtl->WalWriterSleeping;
	prevAsyncXactLSN = XLogCtl->asyncXactLSN;
	if (XLogCtl->asyncXactLSN < asyncXactLSN)
		XLogCtl->asyncXactLSN = asyncXactLSN;
	SpinLockRelease(&XLogCtl->info_lck);

	/*
	 * If somebody else already called this function with a more aggressive
	 * LSN, they will have done what we needed (and perhaps more).
	 */
	if (asyncXactLSN <= prevAsyncXactLSN)
		return;

	/*
	 * If the WALWriter is sleeping, kick it to make it come out of low-power
	 * mode, so that this async commit will reach disk within the expected
	 * amount of time.  Otherwise, determine whether it has enough WAL
	 * available to flush, the same way that XLogBackgroundFlush() does.
	 */
	if (sleeping)
		wakeup = true;
	else
	{
		int			flushblocks;

		RefreshXLogWriteResult(LogwrtResult);

		flushblocks =
			WriteRqstPtr / XLOG_BLCKSZ - LogwrtResult.Flush / XLOG_BLCKSZ;

		if (WalWriterFlushAfter == 0 || flushblocks >= WalWriterFlushAfter)
			wakeup = true;
	}

	if (wakeup && ProcGlobal->walwriterLatch)
		SetLatch(ProcGlobal->walwriterLatch);

	/* In polar wal pipeline mode, should wakeup wal pipeliner */
	if (POLAR_WAL_PIPELINER_READY())
		polar_wal_pipeliner_wakeup();
}

/*
 * Record the LSN up to which we can remove WAL because it's not required by
 * any replication slot.
 */
void
XLogSetReplicationSlotMinimumLSN(XLogRecPtr lsn)
{
	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->replicationSlotMinLSN = lsn;
	SpinLockRelease(&XLogCtl->info_lck);
}


/*
 * Return the oldest LSN we must retain to satisfy the needs of some
 * replication slot.
 */
XLogRecPtr
XLogGetReplicationSlotMinimumLSN(void)
{
	XLogRecPtr	retval;

	SpinLockAcquire(&XLogCtl->info_lck);
	retval = XLogCtl->replicationSlotMinLSN;
	SpinLockRelease(&XLogCtl->info_lck);

	return retval;
}

/*
 * Advance minRecoveryPoint in control file.
 *
 * If we crash during recovery, we must reach this point again before the
 * database is consistent.
 *
 * If 'force' is true, 'lsn' argument is ignored. Otherwise, minRecoveryPoint
 * is only updated if it's not already greater than or equal to 'lsn'.
 */
static void
UpdateMinRecoveryPoint(XLogRecPtr lsn, bool force)
{
	/* Quick check using our local copy of the variable */
	if (!updateMinRecoveryPoint || (!force && lsn <= LocalMinRecoveryPoint))
		return;

	/*
	 * An invalid minRecoveryPoint means that we need to recover all the WAL,
	 * i.e., we're doing crash recovery.  We never modify the control file's
	 * value in that case, so we can short-circuit future checks here too. The
	 * local values of minRecoveryPoint and minRecoveryPointTLI should not be
	 * updated until crash recovery finishes.  We only do this for the startup
	 * process as it should not update its own reference of minRecoveryPoint
	 * until it has finished crash recovery to make sure that all WAL
	 * available is replayed in this case.  This also saves from extra locks
	 * taken on the control file from the startup process.
	 */
<<<<<<< HEAD
	if (XLogRecPtrIsInvalid(minRecoveryPoint) && InRecovery)
	{
		updateMinRecoveryPoint = false;
		return;
	}

	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);

	/* update local copy */
	minRecoveryPoint = ControlFile->minRecoveryPoint;
	minRecoveryPointTLI = ControlFile->minRecoveryPointTLI;

	if (XLogRecPtrIsInvalid(minRecoveryPoint))
=======
	if (XLogRecPtrIsInvalid(LocalMinRecoveryPoint) && InRecovery)
	{
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		updateMinRecoveryPoint = false;
		return;
	}

	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);

	/* update local copy */
	LocalMinRecoveryPoint = ControlFile->minRecoveryPoint;
	LocalMinRecoveryPointTLI = ControlFile->minRecoveryPointTLI;

	if (XLogRecPtrIsInvalid(LocalMinRecoveryPoint))
		updateMinRecoveryPoint = false;
	else if (force || LocalMinRecoveryPoint < lsn)
	{
		XLogRecPtr	newMinRecoveryPoint;
		TimeLineID	newMinRecoveryPointTLI;

		/*
		 * To avoid having to update the control file too often, we update it
		 * all the way to the last record being replayed, even though 'lsn'
		 * would suffice for correctness.  This also allows the 'force' case
		 * to not need a valid 'lsn' value.
		 *
		 * Another important reason for doing it this way is that the passed
		 * 'lsn' value could be bogus, i.e., past the end of available WAL, if
		 * the caller got it from a corrupted heap page.  Accepting such a
		 * value as the min recovery point would prevent us from coming up at
		 * all.  Instead, we just log a warning and continue with recovery.
		 * (See also the comments about corrupt LSNs in XLogFlush.)
		 */
		newMinRecoveryPoint = GetCurrentReplayRecPtr(&newMinRecoveryPointTLI);
		if (!force && newMinRecoveryPoint < lsn)
			elog(WARNING,
				 "xlog min recovery request %X/%X is past current point %X/%X",
				 LSN_FORMAT_ARGS(lsn), LSN_FORMAT_ARGS(newMinRecoveryPoint));

		/* update control file */
		if (ControlFile->minRecoveryPoint < newMinRecoveryPoint)
		{
			ControlFile->minRecoveryPoint = newMinRecoveryPoint;
			ControlFile->minRecoveryPointTLI = newMinRecoveryPointTLI;
			UpdateControlFile();
			LocalMinRecoveryPoint = newMinRecoveryPoint;
			LocalMinRecoveryPointTLI = newMinRecoveryPointTLI;

			ereport(DEBUG2,
					(errmsg_internal("updated min recovery point to %X/%X on timeline %u",
									 LSN_FORMAT_ARGS(newMinRecoveryPoint),
									 newMinRecoveryPointTLI)));
		}
	}
	LWLockRelease(ControlFileLock);
}

/*
 * Ensure that all XLOG data through the given position is flushed to disk.
 *
 * NOTE: this differs from XLogWrite mainly in that the WALWriteLock is not
 * already held, and we try to avoid acquiring it if possible.
 */
void
XLogFlush(XLogRecPtr record)
{
	XLogRecPtr	WriteRqstPtr;
	XLogwrtRqst WriteRqst;
	TimeLineID	insertTLI = XLogCtl->InsertTimeLineID;

	/*
	 * During REDO, we are reading not writing WAL.  Therefore, instead of
	 * trying to flush the WAL, we should update minRecoveryPoint instead. We
	 * test XLogInsertAllowed(), not InRecovery, because we need checkpointer
	 * to act this way too, and because when it tries to write the
	 * end-of-recovery checkpoint, it should indeed flush.
	 */
	if (!XLogInsertAllowed())
	{
		UpdateMinRecoveryPoint(record, false);
		return;
	}

	/* Quick exit if already known flushed */
	if (record <= LogwrtResult.Flush)
	{
		if (polar_is_dma_data_node())
		{
			ConsensusSetXLogFlushedLSN(record, ThisTimeLineID, false);
			if (!POLAR_DMA_ASYNC_COMMIT())
				polar_dma_xlog_commit(record);
		}
		return;
	}

#ifdef WAL_DEBUG
	if (XLOG_DEBUG)
		elog(LOG, "xlog flush request %X/%X; write %X/%X; flush %X/%X",
			 LSN_FORMAT_ARGS(record),
			 LSN_FORMAT_ARGS(LogwrtResult.Write),
			 LSN_FORMAT_ARGS(LogwrtResult.Flush));
#endif

	START_CRIT_SECTION();

	/*
	 * Since fsync is usually a horribly expensive operation, we try to
	 * piggyback as much data as we can on each fsync: if we see any more data
	 * entered into the xlog buffer, we'll write and fsync that too, so that
	 * the final value of LogwrtResult.Flush is as large as possible. This
	 * gives us some chance of avoiding another fsync immediately after.
	 */

	/* initialize to given target; may increase below */
	WriteRqstPtr = record;

	/*
	 * Now wait until we get the write lock, or someone else does the flush
	 * for us.
	 */
	for (;;)
	{
		XLogRecPtr	insertpos;

<<<<<<< HEAD
		/* read LogwrtResult and update local state */
		SpinLockAcquire(&XLogCtl->info_lck);
		if (WriteRqstPtr < XLogCtl->LogwrtRqst.Write)
			WriteRqstPtr = XLogCtl->LogwrtRqst.Write;
		LogwrtResult = XLogCtl->LogwrtResult;
		ConsensusCommit = XLogCtl->ConsensusCommit;
		SpinLockRelease(&XLogCtl->info_lck);

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		/* done already? */
		RefreshXLogWriteResult(LogwrtResult);
		if (record <= LogwrtResult.Flush)
			break;

<<<<<<< HEAD
		if (POLAR_WAL_PIPELINER_READY())
			polar_wal_pipeline_commit_wait(WriteRqstPtr);
		else
=======
		/*
		 * Before actually performing the write, wait for all in-flight
		 * insertions to the pages we're about to write to finish.
		 */
		SpinLockAcquire(&XLogCtl->info_lck);
		if (WriteRqstPtr < XLogCtl->LogwrtRqst.Write)
			WriteRqstPtr = XLogCtl->LogwrtRqst.Write;
		SpinLockRelease(&XLogCtl->info_lck);
		insertpos = WaitXLogInsertionsToFinish(WriteRqstPtr);

		/*
		 * Try to get the write lock. If we can't get it immediately, wait
		 * until it's released, and recheck if we still need to do the flush
		 * or if the backend that held the lock did it for us already. This
		 * helps to maintain a good rate of group committing when the system
		 * is bottlenecked by the speed of fsyncing.
		 */
		if (!LWLockAcquireOrWait(WALWriteLock, LW_EXCLUSIVE))
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		{
			/*
			 * Before actually performing the write, wait for all in-flight
			 * insertions to the pages we're about to write to finish.
			 */
			insertpos = WaitXLogInsertionsToFinish(WriteRqstPtr);

			/*
			 * Try to get the write lock. If we can't get it immediately, wait
			 * until it's released, and recheck if we still need to do the flush
			 * or if the backend that held the lock did it for us already. This
			 * helps to maintain a good rate of group committing when the system
			 * is bottlenecked by the speed of fsyncing.
			 */
			if (!LWLockAcquireOrWait(WALWriteLock, LW_EXCLUSIVE))
			{
				/*
				 * The lock is now free, but we didn't acquire it yet. Before we
				 * do, loop back to check if someone else flushed the record for
				 * us already.
				 */
				continue;
			}

			/* Got the lock; recheck whether request is satisfied */
			LogwrtResult = XLogCtl->LogwrtResult;
			if (record <= LogwrtResult.Flush)
			{
				LWLockRelease(WALWriteLock);
				break;
			}

			/*
			 * Sleep before flush! By adding a delay here, we may give further
			 * backends the opportunity to join the backlog of group commit
			 * followers; this can significantly improve transaction throughput,
			 * at the risk of increasing transaction latency.
			 *
			 * We do not sleep if enableFsync is not turned on, nor if there are
			 * fewer than CommitSiblings other backends with active transactions.
			 */
			if (CommitDelay > 0 && enableFsync &&
				MinimumActiveBackends(CommitSiblings))
			{
				pg_usleep(CommitDelay);

				/*
				 * Re-check how far we can now flush the WAL. It's generally not
				 * safe to call WaitXLogInsertionsToFinish while holding
				 * WALWriteLock, because an in-progress insertion might need to
				 * also grab WALWriteLock to make progress. But we know that all
				 * the insertions up to insertpos have already finished, because
				 * that's what the earlier WaitXLogInsertionsToFinish() returned.
				 * We're only calling it again to allow insertpos to be moved
				 * further forward, not to actually wait for anyone.
				 */
				insertpos = WaitXLogInsertionsToFinish(insertpos);
			}

			/* try to write/flush later additions to XLOG as well */
			WriteRqst.Write = insertpos;
			WriteRqst.Flush = insertpos;

			XLogWrite(WriteRqst, false);

<<<<<<< HEAD
=======
		/* Got the lock; recheck whether request is satisfied */
		RefreshXLogWriteResult(LogwrtResult);
		if (record <= LogwrtResult.Flush)
		{
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			LWLockRelease(WALWriteLock);
		}
<<<<<<< HEAD
		
=======

		/*
		 * Sleep before flush! By adding a delay here, we may give further
		 * backends the opportunity to join the backlog of group commit
		 * followers; this can significantly improve transaction throughput,
		 * at the risk of increasing transaction latency.
		 *
		 * We do not sleep if enableFsync is not turned on, nor if there are
		 * fewer than CommitSiblings other backends with active transactions.
		 */
		if (CommitDelay > 0 && enableFsync &&
			MinimumActiveBackends(CommitSiblings))
		{
			pg_usleep(CommitDelay);

			/*
			 * Re-check how far we can now flush the WAL. It's generally not
			 * safe to call WaitXLogInsertionsToFinish while holding
			 * WALWriteLock, because an in-progress insertion might need to
			 * also grab WALWriteLock to make progress. But we know that all
			 * the insertions up to insertpos have already finished, because
			 * that's what the earlier WaitXLogInsertionsToFinish() returned.
			 * We're only calling it again to allow insertpos to be moved
			 * further forward, not to actually wait for anyone.
			 */
			insertpos = WaitXLogInsertionsToFinish(insertpos);
		}

		/* try to write/flush later additions to XLOG as well */
		WriteRqst.Write = insertpos;
		WriteRqst.Flush = insertpos;

		XLogWrite(WriteRqst, insertTLI, false);

		LWLockRelease(WALWriteLock);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		/* done */
		break;
	}

	END_CRIT_SECTION();

	/* wake up walsenders now that we've released heavily contended locks */
	WalSndWakeupProcessRequests(true, !RecoveryInProgress());

	if (polar_is_dma_data_node())
	{
		ConsensusSetXLogFlushedLSN(record, ThisTimeLineID, false);
		if (!POLAR_DMA_ASYNC_COMMIT())
			polar_dma_xlog_commit(record);
	}

	/*
	 * If we still haven't flushed to the request point then we have a
	 * problem; most likely, the requested flush point is past end of XLOG.
	 * This has been seen to occur when a disk page has a corrupted LSN.
	 *
	 * Formerly we treated this as a PANIC condition, but that hurts the
	 * system's robustness rather than helping it: we do not want to take down
	 * the whole system due to corruption on one data page.  In particular, if
	 * the bad page is encountered again during recovery then we would be
	 * unable to restart the database at all!  (This scenario actually
	 * happened in the field several times with 7.1 releases.)	As of 8.4, bad
	 * LSNs encountered during recovery are UpdateMinRecoveryPoint's problem;
	 * the only time we can reach here during recovery is while flushing the
	 * end-of-recovery checkpoint record, and we don't expect that to have a
	 * bad LSN.
	 *
	 * Note that for calls from xact.c, the ERROR will be promoted to PANIC
	 * since xact.c calls this routine inside a critical section.  However,
	 * calls from bufmgr.c are not within critical sections and so we will not
	 * force a restart for a bad LSN on a data page.
	 */
	if (LogwrtResult.Flush < record)
	{
		elog(ERROR,
			 "xlog flush request %X/%X is not satisfied --- flushed only to %X/%X",
<<<<<<< HEAD
			 (uint32) (record >> 32), (uint32) record,
			 (uint32) (LogwrtResult.Flush >> 32), (uint32) LogwrtResult.Flush);
	
	}

	/* POLAR: record last flush_lsn for persisted buffer pool when reuse buffer */
	polar_buffer_pool_ctl_set_last_flush_lsn(LogwrtResult.Flush);
	/* POLAR end */
=======
			 LSN_FORMAT_ARGS(record),
			 LSN_FORMAT_ARGS(LogwrtResult.Flush));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Write & flush xlog, but without specifying exactly where to.
 *
 * We normally write only completed blocks; but if there is nothing to do on
 * that basis, we check for unwritten async commits in the current incomplete
 * block, and write through the latest one of those.  Thus, if async commits
 * are not being used, we will write complete blocks only.
 *
 * If, based on the above, there's anything to write we do so immediately. But
 * to avoid calling fsync, fdatasync et. al. at a rate that'd impact
 * concurrent IO, we only flush WAL every wal_writer_delay ms, or if there's
 * more than wal_writer_flush_after unflushed blocks.
 *
 * We can guarantee that async commits reach disk after at most three
 * wal_writer_delay cycles. (When flushing complete blocks, we allow XLogWrite
 * to write "flexibly", meaning it can stop at the end of the buffer ring;
 * this makes a difference only with very high load or long wal_writer_delay,
 * but imposes one extra cycle for the worst case for async commits.)
 *
 * This routine is invoked periodically by the background walwriter process.
 *
 * Returns true if there was any work to do, even if we skipped flushing due
 * to wal_writer_delay/wal_writer_flush_after.
 */
bool
XLogBackgroundFlush(void)
{
	XLogwrtRqst WriteRqst;
	bool		flexible = true;
	static TimestampTz lastflush;
	TimestampTz now;
<<<<<<< HEAD
	int			flushbytes;
	static XLogRecPtr lastInsertRecPtr = InvalidXLogRecPtr;
	XLogRecPtr InsertRecPtr = InvalidXLogRecPtr;
=======
	int			flushblocks;
	TimeLineID	insertTLI;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* XLOG doesn't need flushing during recovery */
	if (RecoveryInProgress())
		return false;

	/*
	 * Since we're not in recovery, InsertTimeLineID is set and can't change,
	 * so we can read it without a lock.
	 */
	insertTLI = XLogCtl->InsertTimeLineID;

	/* read updated LogwrtRqst */
	SpinLockAcquire(&XLogCtl->info_lck);
	WriteRqst = XLogCtl->LogwrtRqst;
	ConsensusCommit = XLogCtl->ConsensusCommit;
	SpinLockRelease(&XLogCtl->info_lck);

	if (polar_is_dma_data_node())
	{
		InsertRecPtr = polar_get_faked_latest_lsn();
		if (lastInsertRecPtr == InvalidXLogRecPtr)
			lastInsertRecPtr = InsertRecPtr;
	}

	/* back off to last completed page boundary */
	WriteRqst.Write -= WriteRqst.Write % XLOG_BLCKSZ;

	/* if we have already flushed that far, consider async commit records */
	RefreshXLogWriteResult(LogwrtResult);
	if (WriteRqst.Write <= LogwrtResult.Flush)
	{
		SpinLockAcquire(&XLogCtl->info_lck);
		WriteRqst.Write = XLogCtl->asyncXactLSN;
		SpinLockRelease(&XLogCtl->info_lck);
		flexible = false;		/* ensure it all gets written */
	}

	/*
	 * If already known flushed, we're done. Just need to check if we are
	 * holding an open file handle to a logfile that's no longer in use,
	 * preventing the file from being deleted.
	 */
	if (WriteRqst.Write <= LogwrtResult.Flush)
	{
		if (openLogFile >= 0)
		{
			if (!XLByteInPrevSeg(LogwrtResult.Write, openLogSegNo,
								 wal_segment_size))
			{
				XLogFileClose();
			}
		}
		return false;
	}

	/*
	 * Determine how far to flush WAL, based on the wal_writer_delay and
	 * wal_writer_flush_after GUCs.
	 *
	 * Note that XLogSetAsyncXactLSN() performs similar calculation based on
	 * wal_writer_flush_after, to decide when to wake us up.  Make sure the
	 * logic is the same in both places if you change this.
	 */
	now = GetCurrentTimestamp();
	flushblocks =
		WriteRqst.Write / XLOG_BLCKSZ - LogwrtResult.Flush / XLOG_BLCKSZ;

	if (WalWriterFlushAfter == 0 || lastflush == 0)
	{
		/* first call, or block based limits disabled */
		WriteRqst.Flush = WriteRqst.Write;
		lastflush = now;
	}
	else if (TimestampDifferenceExceeds(lastflush, now, WalWriterDelay))
	{
		/*
		 * Flush the writes at least every WalWriterDelay ms. This is
		 * important to bound the amount of time it takes for an asynchronous
		 * commit to hit disk.
		 */
		WriteRqst.Flush = WriteRqst.Write;
		lastflush = now;
	}
	else if (flushblocks >= WalWriterFlushAfter)
	{
		/* exceeded wal_writer_flush_after blocks, flush */
		WriteRqst.Flush = WriteRqst.Write;
		lastflush = now;
	}
	else
	{
		/* no flushing, this time round */
		WriteRqst.Flush = 0;
	}

#ifdef WAL_DEBUG
	if (XLOG_DEBUG)
		elog(LOG, "xlog bg flush request write %X/%X; flush: %X/%X, current is write %X/%X; flush %X/%X",
			 LSN_FORMAT_ARGS(WriteRqst.Write),
			 LSN_FORMAT_ARGS(WriteRqst.Flush),
			 LSN_FORMAT_ARGS(LogwrtResult.Write),
			 LSN_FORMAT_ARGS(LogwrtResult.Flush));
#endif

<<<<<<< HEAD
	if (POLAR_WAL_PIPELINER_READY())
	{
		if (WriteRqst.Write > LogwrtResult.Write || WriteRqst.Flush > LogwrtResult.Flush)
			polar_wal_pipeline_commit_wait(WriteRqst.Flush);
=======
	START_CRIT_SECTION();

	/* now wait for any in-progress insertions to finish and get write lock */
	WaitXLogInsertionsToFinish(WriteRqst.Write);
	LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);
	RefreshXLogWriteResult(LogwrtResult);
	if (WriteRqst.Write > LogwrtResult.Write ||
		WriteRqst.Flush > LogwrtResult.Flush)
	{
		XLogWrite(WriteRqst, insertTLI, flexible);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
	else
	{
		START_CRIT_SECTION();

		/* now wait for any in-progress insertions to finish and get write lock */
		WaitXLogInsertionsToFinish(WriteRqst.Write);
		LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);
		LogwrtResult = XLogCtl->LogwrtResult;
		if (WriteRqst.Write > LogwrtResult.Write ||
			WriteRqst.Flush > LogwrtResult.Flush)
		{
			XLogWrite(WriteRqst, flexible);
		}
		LWLockRelease(WALWriteLock);

		END_CRIT_SECTION();
	}

	/* wake up walsenders now that we've released heavily contended locks */
	WalSndWakeupProcessRequests(true, !RecoveryInProgress());

	/*
	 * Great, done. To take some work off the critical path, try to initialize
	 * as many of the no-longer-needed WAL buffers for future use as we can.
	 */
	AdvanceXLInsertBuffer(InvalidXLogRecPtr, insertTLI, true);

	if (polar_is_dma_data_node() && LogwrtResult.Flush >= lastInsertRecPtr)
	{
		if (LogwrtResult.Flush >= InsertRecPtr)
			lastInsertRecPtr = InsertRecPtr;

		ConsensusSetXLogFlushedLSN(lastInsertRecPtr, ThisTimeLineID, false);

		if (lastInsertRecPtr > ConsensusCommit)
			ConsensusWakeupCommit(lastInsertRecPtr);

		if (lastInsertRecPtr >= InsertRecPtr)
			lastInsertRecPtr = InvalidXLogRecPtr;
		else
			lastInsertRecPtr = InsertRecPtr;
	}

	/*
	 * If we determined that we need to write data, but somebody else
	 * wrote/flushed already, it should be considered as being active, to
	 * avoid hibernating too early.
	 */
	return true;
}

/*
 * Test whether XLOG data has been flushed up to (at least) the given position.
 *
 * Returns true if a flush is still needed.  (It may be that someone else
 * is already in process of flushing that far, however.)
 */
bool
XLogNeedsFlush(XLogRecPtr record)
{
	/*
	 * During recovery, we don't flush WAL but update minRecoveryPoint
	 * instead. So "needs flush" is taken to mean whether minRecoveryPoint
	 * would need to be updated.
	 */
	if (RecoveryInProgress())
	{
		/*
		 * An invalid minRecoveryPoint means that we need to recover all the
		 * WAL, i.e., we're doing crash recovery.  We never modify the control
		 * file's value in that case, so we can short-circuit future checks
		 * here too.  This triggers a quick exit path for the startup process,
		 * which cannot update its local copy of minRecoveryPoint as long as
		 * it has not replayed all WAL available when doing crash recovery.
		 */
<<<<<<< HEAD
		if (XLogRecPtrIsInvalid(minRecoveryPoint) && InRecovery)
			updateMinRecoveryPoint = false;

		/* Quick exit if already known to be updated or cannot be updated */
		if (record <= minRecoveryPoint || !updateMinRecoveryPoint)
=======
		if (XLogRecPtrIsInvalid(LocalMinRecoveryPoint) && InRecovery)
			updateMinRecoveryPoint = false;

		/* Quick exit if already known to be updated or cannot be updated */
		if (record <= LocalMinRecoveryPoint || !updateMinRecoveryPoint)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			return false;

		/*
		 * Update local copy of minRecoveryPoint. But if the lock is busy,
		 * just return a conservative guess.
		 */
		if (!LWLockConditionalAcquire(ControlFileLock, LW_SHARED))
			return true;
		LocalMinRecoveryPoint = ControlFile->minRecoveryPoint;
		LocalMinRecoveryPointTLI = ControlFile->minRecoveryPointTLI;
		LWLockRelease(ControlFileLock);

		/*
		 * Check minRecoveryPoint for any other process than the startup
		 * process doing crash recovery, which should not update the control
		 * file value if crash recovery is still running.
		 */
<<<<<<< HEAD
		if (XLogRecPtrIsInvalid(minRecoveryPoint))
=======
		if (XLogRecPtrIsInvalid(LocalMinRecoveryPoint))
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			updateMinRecoveryPoint = false;

		/* check again */
		if (record <= LocalMinRecoveryPoint || !updateMinRecoveryPoint)
			return false;
		else
			return true;
	}

	/* Quick exit if already known flushed */
	if (polar_is_dma_data_node() && !POLAR_DMA_ASYNC_COMMIT())
	{
		if (record <= ConsensusCommit)
			return false;
	}
	else if (record <= LogwrtResult.Flush)
	{
		return false;
	}

	/* read LogwrtResult and update local state */
<<<<<<< HEAD
	SpinLockAcquire(&XLogCtl->info_lck);
	LogwrtResult = XLogCtl->LogwrtResult;
	ConsensusCommit = XLogCtl->ConsensusCommit;
	SpinLockRelease(&XLogCtl->info_lck);
=======
	RefreshXLogWriteResult(LogwrtResult);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* check again */
	if (polar_is_dma_data_node() && !POLAR_DMA_ASYNC_COMMIT())
	{
		if (record <= ConsensusCommit)
			return false;
	}
	else if (record <= LogwrtResult.Flush)
	{
		return false;
	}

	return true;
}

/*
 * Try to make a given XLOG file segment exist.
 *
 * logsegno: identify segment.
 *
 * *added: on return, true if this call raised the number of extant segments.
 *
 * path: on return, this char[MAXPGPATH] has the path to the logsegno file.
 *
 * Returns -1 or FD of opened file.  A -1 here is not an error; a caller
 * wanting an open segment should attempt to open "path", which usually will
 * succeed.  (This is weird, but it's efficient for the callers.)
 */
static int
XLogFileInitInternal(XLogSegNo logsegno, TimeLineID logtli,
					 bool *added, char *path)
{
	char		tmppath[MAXPGPATH];
<<<<<<< HEAD
	PGAlignedXLogBlock zbuffer;
	XLogSegNo	installed_segno;
	XLogSegNo	max_segno;
	int			fd;
	int			nbytes;
	char		polar_tmppath[MAXPGPATH];
	instr_time	polar_wal_init_start;
	instr_time	polar_wal_init_end;
	bool		polar_fallocate_walfile = polar_enable_fallocate_walfile;
=======
	XLogSegNo	installed_segno;
	XLogSegNo	max_segno;
	int			fd;
	int			save_errno;
	int			open_flags = O_RDWR | O_CREAT | O_EXCL | PG_BINARY;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	Assert(logtli != 0);

	XLogFilePath(path, logtli, logsegno, wal_segment_size);

	/*
	 * Try to use existent file (checkpoint maker may have created it already)
	 */
	*added = false;
	fd = BasicOpenFile(path, O_RDWR | PG_BINARY | O_CLOEXEC |
					   get_sync_bit(wal_sync_method));
	if (fd < 0)
	{
<<<<<<< HEAD
		fd = BasicOpenFile(polar_path_remove_protocol(path), O_RDWR | PG_BINARY | get_sync_bit(sync_method), true);
		if (fd < 0)
		{
			if (errno != ENOENT)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not open file \"%s\": %m", path)));
		}
		else
			return fd;
=======
		if (errno != ENOENT)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not open file \"%s\": %m", path)));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
	else
		return fd;

	/*
	 * Initialize an empty (all zeroes) segment.  NOTE: it is possible that
	 * another process is doing the same thing.  If so, we will end up
	 * pre-creating an extra log segment.  That seems OK, and better than
	 * holding the lock throughout this lengthy process.
	 */
	elog(DEBUG2, "creating and filling new WAL file");

	snprintf(polar_tmppath, MAXPGPATH, "%s/xlogtemp.%d", POLAR_DATAMAX_WAL_PATH, (int) getpid());

	polar_make_file_path_level2(tmppath, polar_tmppath);
	polar_unlink(tmppath);

	if (io_direct_flags & IO_DIRECT_WAL_INIT)
		open_flags |= PG_O_DIRECT;

	/* do not use get_sync_bit() here --- want to fsync only at end of fill */
<<<<<<< HEAD
	fd = BasicOpenFile(tmppath, O_RDWR | O_CREAT | O_EXCL | PG_BINARY, true);
=======
	fd = BasicOpenFile(tmppath, open_flags);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	if (fd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not create file \"%s\": %m", tmppath)));

<<<<<<< HEAD
	if (IsUnderPostmaster)
		INSTR_TIME_SET_CURRENT(polar_wal_init_start);

	/*
	 * Zero-fill the file.  We have to do this the hard way to ensure that all
	 * the file space has really been allocated --- on platforms that allow
	 * "holes" in files, just seeking to the end doesn't allocate intermediate
	 * space.  This way, we know that we have all the space and (after the
	 * fsync below) that all the indirect blocks are down on disk.  Therefore,
	 * fdatasync(2) or O_DSYNC will be sufficient to sync future writes to the
	 * log file.
	 */

	/* 
	 * POLAR: File allocate, juse change file metadata once.
	 * drop file when it is failed in case of endless empty file. 
	 */
	if(polar_fallocate_walfile &&
		polar_fallocate(fd, 0, wal_segment_size) != 0)
	{
		/* no coverage begin */
		polar_close(fd);
		polar_unlink(tmppath);
		/* no coverage end */
		elog(ERROR, "polar_fallocate fail in XLogFileInit");
	}

	if (IsUnderPostmaster && polar_fallocate_walfile)
	{
		INSTR_TIME_SET_CURRENT(polar_wal_init_end);
		INSTR_TIME_SUBTRACT(polar_wal_init_end, polar_wal_init_start);
		elog(LOG, "done fallocate tmp WAL file %s time=%.3f s",
					tmppath,
					INSTR_TIME_GET_MILLISEC(polar_wal_init_end) / 1000);
	}

	if (polar_fallocate_walfile && polar_skip_fill_walfile_zero_page)
		elog(LOG, "polardb skip fill wal file %s zero page", tmppath);
	else if (POLAR_FILE_IN_SHARED_STORAGE())
		polar_fill_segment_file_zero(fd, tmppath, wal_segment_size,
				WAIT_EVENT_WAL_INIT_WRITE, WAIT_EVENT_WAL_INIT_SYNC, "WAL");
	else
	{
		memset(zbuffer.data, 0, XLOG_BLCKSZ);
		for (nbytes = 0; nbytes < wal_segment_size; nbytes += XLOG_BLCKSZ)
		{
			errno = 0;
			pgstat_report_wait_start(WAIT_EVENT_WAL_INIT_WRITE);
			if ((int) polar_write(fd, zbuffer.data, XLOG_BLCKSZ) != (int) XLOG_BLCKSZ)
			{
				int 		save_errno = errno;
	
				/*
				 * If we fail to make the file, delete it to release disk space
				 */
				polar_unlink(tmppath);
	
				polar_close(fd);
	
				/* if write didn't set errno, assume problem is no disk space */
				errno = save_errno ? save_errno : ENOSPC;
	
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not write to file \"%s\": %m", tmppath)));
			}
			pgstat_report_wait_end();
		}
	
		pgstat_report_wait_start(WAIT_EVENT_WAL_INIT_SYNC);
		if (polar_fsync(fd) != 0)
		{
			int 		save_errno = errno;
	
			polar_close(fd);
			errno = save_errno;
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not fsync file \"%s\": %m", tmppath)));
=======
	pgstat_report_wait_start(WAIT_EVENT_WAL_INIT_WRITE);
	save_errno = 0;
	if (wal_init_zero)
	{
		ssize_t		rc;

		/*
		 * Zero-fill the file.  With this setting, we do this the hard way to
		 * ensure that all the file space has really been allocated.  On
		 * platforms that allow "holes" in files, just seeking to the end
		 * doesn't allocate intermediate space.  This way, we know that we
		 * have all the space and (after the fsync below) that all the
		 * indirect blocks are down on disk.  Therefore, fdatasync(2) or
		 * O_DSYNC will be sufficient to sync future writes to the log file.
		 */
		rc = pg_pwrite_zeros(fd, wal_segment_size, 0);

		if (rc < 0)
			save_errno = errno;
	}
	else
	{
		/*
		 * Otherwise, seeking to the end and writing a solitary byte is
		 * enough.
		 */
		errno = 0;
		if (pg_pwrite(fd, "\0", 1, wal_segment_size - 1) != 1)
		{
			/* if write didn't set errno, assume no disk space */
			save_errno = errno ? errno : ENOSPC;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}
	}
	pgstat_report_wait_end();

	if (save_errno)
	{
		/*
		 * If we fail to make the file, delete it to release disk space
		 */
		unlink(tmppath);

		close(fd);

		errno = save_errno;

		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write to file \"%s\": %m", tmppath)));
	}

<<<<<<< HEAD
	if (polar_close(fd))
=======
	pgstat_report_wait_start(WAIT_EVENT_WAL_INIT_SYNC);
	if (pg_fsync(fd) != 0)
	{
		save_errno = errno;
		close(fd);
		errno = save_errno;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m", tmppath)));
	}
	pgstat_report_wait_end();

	if (close(fd) != 0)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m", tmppath)));

	/*
	 * Now move the segment into place with its final name.  Cope with
	 * possibility that someone else has created the file while we were
	 * filling ours: if so, use ours to pre-create a future log segment.
	 */
	installed_segno = logsegno;

	/*
	 * XXX: What should we use as max_segno? We used to use XLOGfileslop when
	 * that was a constant, but that was always a bit dubious: normally, at a
	 * checkpoint, XLOGfileslop was the offset from the checkpoint record, but
	 * here, it was the offset from the insert location. We can't do the
	 * normal XLOGfileslop calculation here because we don't have access to
	 * the prior checkpoint's redo location. So somewhat arbitrarily, just use
	 * CheckPointSegments.
	 */
	max_segno = logsegno + CheckPointSegments;
	if (InstallXLogFileSegment(&installed_segno, tmppath, true, max_segno,
							   logtli))
	{
		*added = true;
		elog(DEBUG2, "done creating and filling new WAL file");
	}
	else
	{
		/*
		 * No need for any more future segments, or InstallXLogFileSegment()
		 * failed to rename the file into place. If the rename failed, a
		 * caller opening the file may fail.
		 */
<<<<<<< HEAD
		polar_unlink(tmppath);
=======
		unlink(tmppath);
		elog(DEBUG2, "abandoned new WAL file");
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	return -1;
}

/*
 * Create a new XLOG file segment, or open a pre-existing one.
 *
 * logsegno: identify segment to be created/opened.
 *
 * Returns FD of opened file.
 *
 * Note: errors here are ERROR not PANIC because we might or might not be
 * inside a critical section (eg, during checkpoint there is no reason to
 * take down the system on failure).  They will promote to PANIC if we are
 * in a critical section.
 */
int
XLogFileInit(XLogSegNo logsegno, TimeLineID logtli)
{
	bool		ignore_added;
	char		path[MAXPGPATH];
	int			fd;

	Assert(logtli != 0);

	fd = XLogFileInitInternal(logsegno, logtli, &ignore_added, path);
	if (fd >= 0)
		return fd;

	/* Now open original target segment (might not be file I just made) */
<<<<<<< HEAD
	fd = BasicOpenFile(polar_path_remove_protocol(path), O_RDWR | PG_BINARY | get_sync_bit(sync_method), true);
=======
	fd = BasicOpenFile(path, O_RDWR | PG_BINARY | O_CLOEXEC |
					   get_sync_bit(wal_sync_method));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	if (fd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m", path)));
<<<<<<< HEAD

	elog(DEBUG2, "done creating and filling new WAL file %s", path);

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	return fd;
}

/*
 * Create a new XLOG file segment by copying a pre-existing one.
 *
 * destsegno: identify segment to be created.
 *
 * srcTLI, srcsegno: identify segment to be copied (could be from
 *		a different timeline)
 *
 * upto: how much of the source file to copy (the rest is filled with
 *		zeros)
 *
 * Currently this is only used during recovery, and so there are no locking
 * considerations.  But we should be just as tense as XLogFileInit to avoid
 * emplacing a bogus file.
 */
static void
XLogFileCopy(TimeLineID destTLI, XLogSegNo destsegno,
			 TimeLineID srcTLI, XLogSegNo srcsegno,
			 int upto)
{
#define	POLAR_XLOG_SIZE_1MB		1024 * 1024
	char	   *polar_zbuffer;

	char		path[MAXPGPATH];
	char		tmppath[MAXPGPATH];
<<<<<<< HEAD
=======
	PGAlignedXLogBlock buffer;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	int			srcfd;
	int			fd;
	int			nbytes;
	char		polar_tmppath[MAXPGPATH];
	char		*polar_aligned_zbuffer;

	/*
	 * Open the source file
	 */
	XLogFilePath(path, srcTLI, srcsegno, wal_segment_size);
	srcfd = polar_open_transient_file(path, O_RDONLY | PG_BINARY);
	if (srcfd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m", path)));

	/*
	 * Copy into a temp file name.
	 */
	snprintf(polar_tmppath, MAXPGPATH, XLOGDIR "/xlogtemp.%d", (int) getpid());
	polar_make_file_path_level2(tmppath, polar_tmppath);
	polar_unlink(tmppath);

	/* do not use get_sync_bit() here --- want to fsync only at end of fill */
	fd = polar_open_transient_file(tmppath, O_RDWR | O_CREAT | O_EXCL | PG_BINARY);
	if (fd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not create file \"%s\": %m", tmppath)));

	polar_zbuffer = palloc0(POLAR_BUFFER_EXTEND_SIZE(POLAR_XLOG_SIZE_1MB));
	polar_aligned_zbuffer = (char *) POLAR_BUFFER_ALIGN(polar_zbuffer);
	/*
	 * Do the data copying.
	 */
	for (nbytes = 0; nbytes < wal_segment_size; nbytes += POLAR_XLOG_SIZE_1MB)
	{
		int			nread;

		nread = upto - nbytes;

		/*
		 * The part that is not read from the source file is filled with
		 * zeros.
		 */
<<<<<<< HEAD
		if (nread < POLAR_XLOG_SIZE_1MB)
			memset(polar_aligned_zbuffer, 0, POLAR_XLOG_SIZE_1MB);

		if (nread > 0)
		{
			if (nread > POLAR_XLOG_SIZE_1MB)
				nread = POLAR_XLOG_SIZE_1MB;
			errno = 0;
			pgstat_report_wait_start(WAIT_EVENT_WAL_COPY_READ);
			if (polar_read(srcfd, polar_aligned_zbuffer, nread) != nread)
			{
				pfree(polar_zbuffer);
				if (errno != 0)
=======
		if (nread < sizeof(buffer))
			memset(buffer.data, 0, sizeof(buffer));

		if (nread > 0)
		{
			int			r;

			if (nread > sizeof(buffer))
				nread = sizeof(buffer);
			pgstat_report_wait_start(WAIT_EVENT_WAL_COPY_READ);
			r = read(srcfd, buffer.data, nread);
			if (r != nread)
			{
				if (r < 0)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
					ereport(ERROR,
							(errcode_for_file_access(),
							 errmsg("could not read file \"%s\": %m",
									path)));
				else
					ereport(ERROR,
							(errcode(ERRCODE_DATA_CORRUPTED),
							 errmsg("could not read file \"%s\": read %d of %zu",
									path, r, (Size) nread)));
			}
			pgstat_report_wait_end();
		}
		errno = 0;
		pgstat_report_wait_start(WAIT_EVENT_WAL_COPY_WRITE);
<<<<<<< HEAD
		if ((int) polar_write(fd, polar_aligned_zbuffer, POLAR_XLOG_SIZE_1MB) != (int) POLAR_XLOG_SIZE_1MB)
=======
		if ((int) write(fd, buffer.data, sizeof(buffer)) != (int) sizeof(buffer))
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		{
			int			save_errno = errno;

			pfree(polar_zbuffer);
			/*
			 * If we fail to make the file, delete it to release disk space
			 */
			polar_unlink(tmppath);
			/* if write didn't set errno, assume problem is no disk space */
			errno = save_errno ? save_errno : ENOSPC;

			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m", tmppath)));
		}
		pgstat_report_wait_end();
	}

	pfree(polar_zbuffer);

	pgstat_report_wait_start(WAIT_EVENT_WAL_COPY_SYNC);
<<<<<<< HEAD
	if (polar_fsync(fd) != 0)
=======
	if (pg_fsync(fd) != 0)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		ereport(data_sync_elevel(ERROR),
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m", tmppath)));
	pgstat_report_wait_end();

	if (CloseTransientFile(fd) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m", tmppath)));

	if (CloseTransientFile(srcfd) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m", path)));

	/*
	 * Now move the segment into place with its final name.
	 */
	if (!InstallXLogFileSegment(&destsegno, tmppath, false, 0, destTLI))
		elog(ERROR, "InstallXLogFileSegment should not have failed");
}

/*
 * Install a new XLOG segment file as a current or future log segment.
 *
 * This is used both to install a newly-created segment (which has a temp
 * filename while it's being created) and to recycle an old segment.
 *
 * *segno: identify segment to install as (or first possible target).
 * When find_free is true, this is modified on return to indicate the
 * actual installation location or last segment searched.
 *
 * tmppath: initial name of file to install.  It will be renamed into place.
 *
 * find_free: if true, install the new segment at the first empty segno
 * number at or after the passed numbers.  If false, install the new segment
 * exactly where specified, deleting any existing segment file there.
 *
 * max_segno: maximum segment number to install the new file as.  Fail if no
 * free slot is found between *segno and max_segno. (Ignored when find_free
 * is false.)
 *
 * tli: The timeline on which the new segment should be installed.
 *
 * Returns true if the file was installed successfully.  false indicates that
 * max_segno limit was exceeded, the startup process has disabled this
 * function for now, or an error occurred while renaming the file into place.
 */
static bool
InstallXLogFileSegment(XLogSegNo *segno, char *tmppath,
					   bool find_free, XLogSegNo max_segno, TimeLineID tli)
{
	char		path[MAXPGPATH];
	struct stat stat_buf;

	Assert(tli != 0);

	XLogFilePath(path, tli, *segno, wal_segment_size);

	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	if (!XLogCtl->InstallXLogFileSegmentActive)
	{
		LWLockRelease(ControlFileLock);
		return false;
	}

	if (!find_free)
	{
		/* Force installation: get rid of any pre-existing segment file */
		durable_unlink(path, DEBUG1);
	}
	else
	{
		/* Find a free slot to put it in */
		while (polar_stat(path, &stat_buf) == 0)
		{
			if ((*segno) >= max_segno)
			{
				/* Failed to find a free slot within specified range */
				LWLockRelease(ControlFileLock);
				return false;
			}
			(*segno)++;
			XLogFilePath(path, tli, *segno, wal_segment_size);
		}
	}

	Assert(access(path, F_OK) != 0 && errno == ENOENT);
	if (durable_rename(tmppath, path, LOG) != 0)
	{
		LWLockRelease(ControlFileLock);
		/* durable_rename already emitted log message */
		return false;
	}

	LWLockRelease(ControlFileLock);

	return true;
}

/*
 * Open a pre-existing logfile segment for writing.
 */
int
XLogFileOpen(XLogSegNo segno, TimeLineID tli)
{
	char		path[MAXPGPATH];
	int			fd;

	XLogFilePath(path, tli, segno, wal_segment_size);

<<<<<<< HEAD
	fd = BasicOpenFile(polar_path_remove_protocol(path), O_RDWR | PG_BINARY | get_sync_bit(sync_method), true);
=======
	fd = BasicOpenFile(path, O_RDWR | PG_BINARY | O_CLOEXEC |
					   get_sync_bit(wal_sync_method));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	if (fd < 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m", path)));

	return fd;
}

/*
<<<<<<< HEAD
 * Open a logfile segment for reading (during recovery).
 *
 * If source == XLOG_FROM_ARCHIVE, the segment is retrieved from archive.
 * Otherwise, it's assumed to be already available in pg_wal.
 */
static int
XLogFileRead(XLogSegNo segno, int emode, TimeLineID tli,
			 int source, bool notfoundOk)
{
	char		xlogfname[MAXFNAMELEN];
	char		activitymsg[MAXFNAMELEN + 16];
	char		path[MAXPGPATH];
	int			fd;

	XLogFileName(xlogfname, tli, segno, wal_segment_size);

	switch (source)
	{
		case XLOG_FROM_ARCHIVE:
			/* Report recovery progress in PS display */
			snprintf(activitymsg, sizeof(activitymsg), "waiting for %s",
					 xlogfname);
			set_ps_display(activitymsg, false);

			restoredFromArchive = RestoreArchivedFile(path, xlogfname,
													  "RECOVERYXLOG",
													  wal_segment_size,
													  InRedo);
			if (!restoredFromArchive)
				return -1;
			break;

		case XLOG_FROM_PG_WAL:
		case XLOG_FROM_STREAM:
			XLogFilePath(path, tli, segno, wal_segment_size);
			restoredFromArchive = false;
			break;

		default:
			elog(ERROR, "invalid XLogFileRead source %d", source);
	}

	/*
	 * If the segment was fetched from archival storage, replace the existing
	 * xlog segment (if any) with the archival version.
	 */
	if (source == XLOG_FROM_ARCHIVE)
	{
		KeepFileRestoredFromArchive(path, xlogfname);

		/*
		 * Set path to point at the new file in pg_wal.
		 */
		snprintf(path, MAXPGPATH, XLOGDIR "/%s", xlogfname);
	}

	fd = BasicOpenFile(polar_path_remove_protocol(path), O_RDONLY | PG_BINARY, true);
	if (fd >= 0)
	{
		/* Success! */
		curFileTLI = tli;

		/* Report recovery progress in PS display */
		snprintf(activitymsg, sizeof(activitymsg), "recovering %s",
				 xlogfname);
		set_ps_display(activitymsg, false);

		/* Track source of data in assorted state variables */
		readSource = source;
		XLogReceiptSource = source;
		/* In FROM_STREAM case, caller tracks receipt time, not me */
		if (source != XLOG_FROM_STREAM)
			XLogReceiptTime = GetCurrentTimestamp();

		return fd;
	}
	if (errno != ENOENT || !notfoundOk) /* unexpected failure? */
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m", path)));
	return -1;
}

/*
 * Open a logfile segment for reading (during recovery).
 *
 * This version searches for the segment with any TLI listed in expectedTLEs.
 */
static int
XLogFileReadAnyTLI(XLogSegNo segno, int emode, int source)
{
	char		path[MAXPGPATH];
	ListCell   *cell;
	int			fd;
	List	   *tles;

	/*
	 * Loop looking for a suitable timeline ID: we might need to read any of
	 * the timelines listed in expectedTLEs.
	 *
	 * We expect curFileTLI on entry to be the TLI of the preceding file in
	 * sequence, or 0 if there was no predecessor.  We do not allow curFileTLI
	 * to go backwards; this prevents us from picking up the wrong file when a
	 * parent timeline extends to higher segment numbers than the child we
	 * want to read.
	 *
	 * If we haven't read the timeline history file yet, read it now, so that
	 * we know which TLIs to scan.  We don't save the list in expectedTLEs,
	 * however, unless we actually find a valid segment.  That way if there is
	 * neither a timeline history file nor a WAL segment in the archive, and
	 * streaming replication is set up, we'll read the timeline history file
	 * streamed from the master when we start streaming, instead of recovering
	 * with a dummy history generated here.
	 */
	if (expectedTLEs)
		tles = expectedTLEs;
	else
		tles = readTimeLineHistory(recoveryTargetTLI);

	foreach(cell, tles)
	{
		TimeLineHistoryEntry *hent = (TimeLineHistoryEntry *) lfirst(cell);
		TimeLineID	tli = hent->tli;

		if (tli < curFileTLI)
			break;				/* don't bother looking at too-old TLIs */

		/*
		 * Skip scanning the timeline ID that the logfile segment to read
		 * doesn't belong to
		 */
		if (hent->begin != InvalidXLogRecPtr)
		{
			XLogSegNo	beginseg = 0;

			XLByteToSeg(hent->begin, beginseg, wal_segment_size);

			/*
			 * The logfile segment that doesn't belong to the timeline is
			 * older or newer than the segment that the timeline started or
			 * ended at, respectively. It's sufficient to check only the
			 * starting segment of the timeline here. Since the timelines are
			 * scanned in descending order in this loop, any segments newer
			 * than the ending segment should belong to newer timeline and
			 * have already been read before. So it's not necessary to check
			 * the ending segment of the timeline here.
			 */
			if (segno < beginseg)
				continue;
		}

		if (source == XLOG_FROM_ANY || source == XLOG_FROM_ARCHIVE)
		{
			fd = XLogFileRead(segno, emode, tli,
							  XLOG_FROM_ARCHIVE, true);
			if (fd != -1)
			{
				elog(DEBUG1, "got WAL segment from archive");
				if (!expectedTLEs)
					expectedTLEs = tles;
				return fd;
			}
		}

		if (source == XLOG_FROM_ANY || source == XLOG_FROM_PG_WAL)
		{
			fd = XLogFileRead(segno, emode, tli,
							  XLOG_FROM_PG_WAL, true);
			if (fd != -1)
			{
				if (!expectedTLEs)
					expectedTLEs = tles;
				return fd;
			}
		}
	}

	/* Couldn't find it.  For simplicity, complain about front timeline */
	XLogFilePath(path, recoveryTargetTLI, segno, wal_segment_size);
	errno = ENOENT;
	ereport(emode,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": %m", path)));
	return -1;
}

/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 * Close the current logfile segment for writing.
 */
static void
XLogFileClose(void)
{
	Assert(openLogFile >= 0);

	/*
	 * WAL segment files will not be re-read in normal operation, so we advise
	 * the OS to release any cached pages.  But do not do so if WAL archiving
	 * or streaming is active, because archiver and walsender process could
	 * use the cache to read the WAL segment.
	 */
#if defined(USE_POSIX_FADVISE) && defined(POSIX_FADV_DONTNEED)
<<<<<<< HEAD
	/* POLAR: libpfs not support posix_fadvise */
	if (!XLogIsNeeded() && !POLAR_FILE_IN_SHARED_STORAGE())
		(void) posix_fadvise(openLogFile, 0, 0, POSIX_FADV_DONTNEED);
#endif

	if (polar_close(openLogFile))
=======
	if (!XLogIsNeeded() && (io_direct_flags & IO_DIRECT_WAL) == 0)
		(void) posix_fadvise(openLogFile, 0, 0, POSIX_FADV_DONTNEED);
#endif

	if (close(openLogFile) != 0)
	{
		char		xlogfname[MAXFNAMELEN];
		int			save_errno = errno;

		XLogFileName(xlogfname, openLogTLI, openLogSegNo, wal_segment_size);
		errno = save_errno;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m", xlogfname)));
	}

	openLogFile = -1;
	ReleaseExternalFD();
}

/*
 * Preallocate log files beyond the specified log endpoint.
 *
 * XXX this is currently extremely conservative, since it forces only one
 * future log segment to exist, and even that only if we are 75% done with
 * the current one.  This is only appropriate for very low-WAL-volume systems.
 * High-volume systems will be OK once they've built up a sufficient set of
 * recycled log segments, but the startup transient is likely to include
 * a lot of segment creations by foreground processes, which is not so good.
 *
 * XLogFileInitInternal() can ereport(ERROR).  All known causes indicate big
 * trouble; for example, a full filesystem is one cause.  The checkpoint WAL
 * and/or ControlFile updates already completed.  If a RequestCheckpoint()
 * initiated the present checkpoint and an ERROR ends this function, the
 * command that called RequestCheckpoint() fails.  That's not ideal, but it's
 * not worth contorting more functions to use caller-specified elevel values.
 * (With or without RequestCheckpoint(), an ERROR forestalls some inessential
 * reporting and resource reclamation.)
 */
static void
PreallocXlogFiles(XLogRecPtr endptr, TimeLineID tli)
{
	XLogSegNo	_logSegNo;
	int			lf;
	bool		added;
	char		path[MAXPGPATH];
	uint64		offset;

<<<<<<< HEAD
	if (polar_in_replica_mode())
		return;
=======
	if (!XLogCtl->InstallXLogFileSegmentActive)
		return;					/* unlocked check says no */
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	XLByteToPrevSeg(endptr, _logSegNo, wal_segment_size);
	offset = XLogSegmentOffset(endptr - 1, wal_segment_size);
	if (offset >= (uint32) (0.75 * wal_segment_size))
	{
		_logSegNo++;
<<<<<<< HEAD
		use_existent = true;
		lf = XLogFileInit(_logSegNo, &use_existent, true);
		polar_close(lf);
		if (!use_existent)
=======
		lf = XLogFileInitInternal(_logSegNo, tli, &added, path);
		if (lf >= 0)
			close(lf);
		if (added)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			CheckpointStats.ckpt_segs_added++;
	}
}

/*
 * Throws an error if the given log segment has already been removed or
 * recycled. The caller should only pass a segment that it knows to have
 * existed while the server has been running, as this function always
 * succeeds if no WAL segments have been removed since startup.
 * 'tli' is only used in the error message.
 *
 * Note: this function guarantees to keep errno unchanged on return.
 * This supports callers that use this to possibly deliver a better
 * error message about a missing file, while still being able to throw
 * a normal file-access error afterwards, if this does return.
 */
void
CheckXLogRemoved(XLogSegNo segno, TimeLineID tli)
{
	int			save_errno = errno;
	XLogSegNo	lastRemovedSegNo;

	SpinLockAcquire(&XLogCtl->info_lck);
	lastRemovedSegNo = XLogCtl->lastRemovedSegNo;
	SpinLockRelease(&XLogCtl->info_lck);

	if (segno <= lastRemovedSegNo)
	{
		char		filename[MAXFNAMELEN];

		XLogFileName(filename, tli, segno, wal_segment_size);
		errno = save_errno;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("requested WAL segment %s has already been removed",
						filename)));
	}
	errno = save_errno;
}

/*
 * Return the last WAL segment removed, or 0 if no segment has been removed
 * since startup.
 *
 * NB: the result can be out of date arbitrarily fast, the caller has to deal
 * with that.
 */
XLogSegNo
XLogGetLastRemovedSegno(void)
{
	XLogSegNo	lastRemovedSegNo;

	SpinLockAcquire(&XLogCtl->info_lck);
	lastRemovedSegNo = XLogCtl->lastRemovedSegNo;
	SpinLockRelease(&XLogCtl->info_lck);

	return lastRemovedSegNo;
}

/*
 * Return the oldest WAL segment on the given TLI that still exists in
 * XLOGDIR, or 0 if none.
 */
XLogSegNo
XLogGetOldestSegno(TimeLineID tli)
{
	DIR		   *xldir;
	struct dirent *xlde;
	XLogSegNo	oldest_segno = 0;

	xldir = AllocateDir(XLOGDIR);
	while ((xlde = ReadDir(xldir, XLOGDIR)) != NULL)
	{
		TimeLineID	file_tli;
		XLogSegNo	file_segno;

		/* Ignore files that are not XLOG segments. */
		if (!IsXLogFileName(xlde->d_name))
			continue;

		/* Parse filename to get TLI and segno. */
		XLogFromFileName(xlde->d_name, &file_tli, &file_segno,
						 wal_segment_size);

		/* Ignore anything that's not from the TLI of interest. */
		if (tli != file_tli)
			continue;

		/* If it's the oldest so far, update oldest_segno. */
		if (oldest_segno == 0 || file_segno < oldest_segno)
			oldest_segno = file_segno;
	}

	FreeDir(xldir);
	return oldest_segno;
}

/*
 * Update the last removed segno pointer in shared memory, to reflect that the
 * given XLOG file has been removed.
 */
static void
UpdateLastRemovedPtr(char *filename)
{
	uint32		tli;
	XLogSegNo	segno;

	XLogFromFileName(filename, &tli, &segno, wal_segment_size);

	SpinLockAcquire(&XLogCtl->info_lck);
	if (segno > XLogCtl->lastRemovedSegNo)
		XLogCtl->lastRemovedSegNo = segno;
	SpinLockRelease(&XLogCtl->info_lck);
}

/*
 * Remove all temporary log files in pg_wal
 *
<<<<<<< HEAD
 * endptr is current (or recent) end of xlog, and lastredoptr is the
 * redo pointer of the last checkpoint. These are used to determine
 * whether we want to recycle rather than delete no-longer-wanted log files.
 */
static void
RemoveOldXlogFiles(XLogSegNo segno, XLogRecPtr lastredoptr, XLogRecPtr endptr)
=======
 * This is called at the beginning of recovery after a previous crash,
 * at a point where no other processes write fresh WAL data.
 */
static void
RemoveTempXlogFiles(void)
{
	DIR		   *xldir;
	struct dirent *xlde;

	elog(DEBUG2, "removing all temporary WAL segments");

	xldir = AllocateDir(XLOGDIR);
	while ((xlde = ReadDir(xldir, XLOGDIR)) != NULL)
	{
		char		path[MAXPGPATH];

		if (strncmp(xlde->d_name, "xlogtemp.", 9) != 0)
			continue;

		snprintf(path, MAXPGPATH, XLOGDIR "/%s", xlde->d_name);
		unlink(path);
		elog(DEBUG2, "removed temporary WAL segment \"%s\"", path);
	}
	FreeDir(xldir);
}

/*
 * Recycle or remove all log files older or equal to passed segno.
 *
 * endptr is current (or recent) end of xlog, and lastredoptr is the
 * redo pointer of the last checkpoint. These are used to determine
 * whether we want to recycle rather than delete no-longer-wanted log files.
 *
 * insertTLI is the current timeline for XLOG insertion. Any recycled
 * segments should be reused for this timeline.
 */
static void
RemoveOldXlogFiles(XLogSegNo segno, XLogRecPtr lastredoptr, XLogRecPtr endptr,
				   TimeLineID insertTLI)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
{
	DIR		   *xldir;
	struct dirent *xlde;
	char		lastoff[MAXFNAMELEN];
<<<<<<< HEAD
	char		polar_path[MAXPGPATH];
	/* POLAR: parameter segno is the last_segno to remove, add 1 to get the last_segno to retain */
	XLogSegNo	polar_tmp_segno = segno + 1;

	if (polar_in_replica_mode())
		return;

	polar_make_file_path_level2(polar_path, XLOGDIR);

	/* 
	 * POLAR: get polar_initial_datamax_lock to avoid getting the wal being removed while setting datamax restart_lsn 
	 * and caculate the retained last_segno again after we acquired the lwlock
	 * so that we can use the restart_lsn we set for initial datamax
	 * avoid removing wal greater than restart_lsn we just set
	 */
	LWLockAcquire(&XLogCtl->polar_initial_datamax_lock, LW_EXCLUSIVE);
	/*  
	 * POLAR: if the last_segno that we need to retain changed, exit this removal process
	 * wait wal removal until next checkpoint time 
	 */
	KeepLogSeg(endptr, &polar_tmp_segno);
	if (polar_tmp_segno != (segno + 1))
	{
		LWLockRelease(&XLogCtl->polar_initial_datamax_lock);
		return;
	}
=======
	XLogSegNo	endlogSegNo;
	XLogSegNo	recycleSegNo;

	/* Initialize info about where to try to recycle to */
	XLByteToSeg(endptr, endlogSegNo, wal_segment_size);
	recycleSegNo = XLOGfileslop(lastredoptr);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Construct a filename of the last segment to be kept. The timeline ID
	 * doesn't matter, we ignore that in the comparison. (During recovery,
	 * InsertTimeLineID isn't set, so we can't use that.)
	 */
	XLogFileName(lastoff, 0, segno, wal_segment_size);

	elog(LOG, "attempting to remove WAL segments older than log file %s",
		 lastoff);

	xldir = polar_allocate_dir(polar_path);
	while ((xlde = ReadDir(xldir, polar_path)) != NULL)
	{
		/* Ignore files that are not XLOG segments */
		if (!IsXLogFileName(xlde->d_name) &&
			!IsPartialXLogFileName(xlde->d_name))
			continue;

		/*
		 * We ignore the timeline part of the XLOG segment identifiers in
		 * deciding whether a segment is still needed.  This ensures that we
		 * won't prematurely remove a segment from a parent timeline. We could
		 * probably be a little more proactive about removing segments of
		 * non-parent timelines, but that would be a whole lot more
		 * complicated.
		 *
		 * We use the alphanumeric sorting property of the filenames to decide
		 * which ones are earlier than the lastoff segment.
		 */
		if (strcmp(xlde->d_name + 8, lastoff + 8) <= 0)
		{
			if (XLogArchiveCheckDone(xlde->d_name))
			{
				/* Update the last removed location in shared memory first */
				UpdateLastRemovedPtr(xlde->d_name);

<<<<<<< HEAD
				RemoveXlogFile(xlde->d_name, lastredoptr, endptr);
=======
				RemoveXlogFile(xlde, recycleSegNo, &endlogSegNo, insertTLI);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			}
		}
	}
	LWLockRelease(&XLogCtl->polar_initial_datamax_lock);

	FreeDir(xldir);
}

/*
 * Recycle or remove WAL files that are not part of the given timeline's
 * history.
 *
 * This is called during recovery, whenever we switch to follow a new
 * timeline, and at the end of recovery when we create a new timeline. We
 * wouldn't otherwise care about extra WAL files lying in pg_wal, but they
 * might be leftover pre-allocated or recycled WAL segments on the old timeline
 * that we haven't used yet, and contain garbage. If we just leave them in
 * pg_wal, they will eventually be archived, and we can't let that happen.
 * Files that belong to our timeline history are valid, because we have
 * successfully replayed them, but from others we can't be sure.
 *
 * 'switchpoint' is the current point in WAL where we switch to new timeline,
 * and 'newTLI' is the new timeline we switch to.
 */
void
RemoveNonParentXlogFiles(XLogRecPtr switchpoint, TimeLineID newTLI)
{
	DIR		   *xldir;
	struct dirent *xlde;
	char		switchseg[MAXFNAMELEN];
	XLogSegNo	endLogSegNo;
<<<<<<< HEAD
	char		polar_path[MAXPGPATH];

	if(!polar_is_datamax_mode)
		polar_make_file_path_level2(polar_path, XLOGDIR);
	else
		polar_make_file_path_level2(polar_path, POLAR_DATAMAX_WAL_DIR);
=======
	XLogSegNo	switchLogSegNo;
	XLogSegNo	recycleSegNo;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Initialize info about where to begin the work.  This will recycle,
	 * somewhat arbitrarily, 10 future segments.
	 */
	XLByteToPrevSeg(switchpoint, switchLogSegNo, wal_segment_size);
	XLByteToSeg(switchpoint, endLogSegNo, wal_segment_size);
	recycleSegNo = endLogSegNo + 10;

	/*
	 * Construct a filename of the last segment to be kept.
	 */
	XLogFileName(switchseg, newTLI, switchLogSegNo, wal_segment_size);

	/* POLAR: force record wal log */
	elog(LOG, "attempting to remove WAL segments newer than log file %s",
		 switchseg);

	xldir = polar_allocate_dir(polar_path);
	while ((xlde = ReadDir(xldir, polar_path)) != NULL)
	{
		/* Ignore files that are not XLOG segments */
		if (!IsXLogFileName(xlde->d_name))
			continue;

		/*
		 * Remove files that are on a timeline older than the new one we're
		 * switching to, but with a segment number >= the first segment on the
		 * new timeline.
		 */
		if (strncmp(xlde->d_name, switchseg, 8) < 0 &&
			strcmp(xlde->d_name + 8, switchseg + 8) > 0)
		{
			/*
			 * If the file has already been marked as .ready, however, don't
			 * remove it yet. It should be OK to remove it - files that are
			 * not part of our timeline history are not required for recovery
			 * - but seems safer to let them be archived and removed later.
			 */
			if (!XLogArchiveIsReady(xlde->d_name))
				RemoveXlogFile(xlde, recycleSegNo, &endLogSegNo, newTLI);
		}
	}

	FreeDir(xldir);
}

/*
 * Recycle or remove a log file that's no longer needed.
 *
<<<<<<< HEAD
 * endptr is current (or recent) end of xlog, and lastredoptr is the
 * redo pointer of the last checkpoint. These are used to determine
 * whether we want to recycle rather than delete no-longer-wanted log files.
 * If lastredoptr is not known, pass invalid, and the function will recycle,
 * somewhat arbitrarily, 10 future segments.
 */
static void
RemoveXlogFile(const char *segname, XLogRecPtr lastredoptr, XLogRecPtr endptr)
=======
 * segment_de is the dirent structure of the segment to recycle or remove.
 * recycleSegNo is the segment number to recycle up to.  endlogSegNo is
 * the segment number of the current (or recent) end of WAL.
 *
 * endlogSegNo gets incremented if the segment is recycled so as it is not
 * checked again with future callers of this function.
 *
 * insertTLI is the current timeline for XLOG insertion. Any recycled segments
 * should be used for this timeline.
 */
static void
RemoveXlogFile(const struct dirent *segment_de,
			   XLogSegNo recycleSegNo, XLogSegNo *endlogSegNo,
			   TimeLineID insertTLI)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
{
	char		path[MAXPGPATH];
#ifdef WIN32
	char		newpath[MAXPGPATH];
#endif
<<<<<<< HEAD
	struct stat statbuf;
	XLogSegNo	endlogSegNo;
	XLogSegNo	recycleSegNo;

	if (wal_recycle)
	{
		/*
		* Initialize info about where to try to recycle to.
		*/
		XLByteToSeg(endptr, endlogSegNo, wal_segment_size);
		if (lastredoptr == InvalidXLogRecPtr)
			recycleSegNo = endlogSegNo + 10;
		else
			recycleSegNo = XLOGfileslop(lastredoptr);
	}
	else
	{
		recycleSegNo = 0;		/* keep compiler quiet */
	}
=======
	const char *segname = segment_de->d_name;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	if(!polar_is_datamax_mode)
		polar_make_file_path_level3(path, XLOGDIR, (char *)segname);
	else
		polar_make_file_path_level3(path, POLAR_DATAMAX_WAL_DIR, (char *)segname);

	/*
	 * Before deleting the file, see if it can be recycled as a future log
	 * segment. Only recycle normal files, because we don't want to recycle
	 * symbolic links pointing to a separate archive directory.
	 */
<<<<<<< HEAD
	if (!polar_is_datamax_mode && 
		wal_recycle &&
	  endlogSegNo <= recycleSegNo &&
		polar_lstat(path, &statbuf) == 0 && S_ISREG(statbuf.st_mode) &&
		InstallXLogFileSegment(&endlogSegNo, path,
							   true, recycleSegNo, true))
	{
		/* POLAR: force log */
		ereport(LOG,
				(errmsg("recycled write-ahead log file \"%s\"",
						segname)));
=======
	if (wal_recycle &&
		*endlogSegNo <= recycleSegNo &&
		XLogCtl->InstallXLogFileSegmentActive &&	/* callee rechecks this */
		get_dirent_type(path, segment_de, false, DEBUG2) == PGFILETYPE_REG &&
		InstallXLogFileSegment(endlogSegNo, path,
							   true, recycleSegNo, insertTLI))
	{
		ereport(DEBUG2,
				(errmsg_internal("recycled write-ahead log file \"%s\"",
								 segname)));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		CheckpointStats.ckpt_segs_recycled++;
		/* Needn't recheck that slot on future iterations */
		(*endlogSegNo)++;
	}
	else
	{
		/* No need for any more future segments, or recycling failed ... */
		int			rc;

<<<<<<< HEAD
		/* POLAR: force log */
		ereport(LOG,
				(errmsg("removing write-ahead log file \"%s\"",
						segname)));
=======
		ereport(DEBUG2,
				(errmsg_internal("removing write-ahead log file \"%s\"",
								 segname)));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

#ifdef WIN32

		/*
		 * On Windows, if another process (e.g another backend) holds the file
		 * open in FILE_SHARE_DELETE mode, unlink will succeed, but the file
		 * will still show up in directory listing until the last handle is
		 * closed. To avoid confusing the lingering deleted file for a live
		 * WAL file that needs to be archived, rename it before deleting it.
		 *
		 * If another process holds the file open without FILE_SHARE_DELETE
		 * flag, rename will fail. We'll try again at the next checkpoint.
		 */
		snprintf(newpath, MAXPGPATH, "%s.deleted", path);
		if (rename(path, newpath) != 0)
		{
			ereport(LOG,
					(errcode_for_file_access(),
					 errmsg("could not rename file \"%s\": %m",
							path)));
			return;
		}
		rc = durable_unlink(newpath, LOG);
#else
		rc = durable_unlink(path, LOG);
#endif
		if (rc != 0)
		{
			/* Message already logged by durable_unlink() */
			return;
		}
		CheckpointStats.ckpt_segs_removed++;
	}

	XLogArchiveCleanup(segname);
}

/*
 * Verify whether pg_wal, pg_wal/archive_status, and pg_wal/summaries exist.
 * If the latter do not exist, recreate them.
 *
 * It is not the goal of this function to verify the contents of these
 * directories, but to help in cases where someone has performed a cluster
 * copy for PITR purposes but omitted pg_wal from the copy.
 *
 * We could also recreate pg_wal if it doesn't exist, but a deliberate
 * policy decision was made not to.  It is fairly common for pg_wal to be
 * a symlink, and if that was the DBA's intent then automatically making a
 * plain directory would result in degraded performance with no notice.
 */
static void
ValidateXLOGDirectoryStructure(void)
{
	char		path[MAXPGPATH];
	struct stat stat_buf;

	polar_make_file_path_level2(path, XLOGDIR);

	/* Check for pg_wal; if it doesn't exist, error out */
	if (polar_stat(path, &stat_buf) != 0 ||
		!S_ISDIR(stat_buf.st_mode))
		ereport(FATAL,
				(errcode_for_file_access(),
				 errmsg("required WAL directory \"%s\" does not exist",
						XLOGDIR)));

	/* Check for archive_status */
	polar_make_file_path_level3(path, XLOGDIR, "archive_status");
	if (polar_stat(path, &stat_buf) == 0)
	{
		/* Check for weird cases where it exists but isn't a directory */
		if (!S_ISDIR(stat_buf.st_mode))
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("required WAL directory \"%s\" does not exist",
							path)));
	}
	else
	{
		ereport(LOG,
				(errmsg("creating missing WAL directory \"%s\"", path)));
		if (MakePGDirectory(path) < 0)
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("could not create missing directory \"%s\": %m",
							path)));
	}

	/* Check for summaries */
	snprintf(path, MAXPGPATH, XLOGDIR "/summaries");
	if (stat(path, &stat_buf) == 0)
	{
		/* Check for weird cases where it exists but isn't a directory */
		if (!S_ISDIR(stat_buf.st_mode))
			ereport(FATAL,
					(errmsg("required WAL directory \"%s\" does not exist",
							path)));
	}
	else
	{
		ereport(LOG,
				(errmsg("creating missing WAL directory \"%s\"", path)));
		if (polar_make_pg_directory(path) < 0)
			ereport(FATAL,
					(errmsg("could not create missing directory \"%s\": %m",
							path)));
	}
}

/*
 * Remove previous backup history files.  This also retries creation of
 * .ready files for any backup history files for which XLogArchiveNotify
 * failed earlier.
 */
static void
CleanupBackupHistory(void)
{
	DIR		   *xldir;
	struct dirent *xlde;
	char		path[MAXPGPATH + sizeof(XLOGDIR)];
	char		polar_dir_path[MAXPGPATH] = "";

	/* POLAR: this folder is under polar_datadir/ */
	polar_make_file_path_level2(polar_dir_path, XLOGDIR);

	xldir = polar_allocate_dir(polar_dir_path);

	while ((xlde = ReadDir(xldir, polar_dir_path)) != NULL)
	{
		if (IsBackupHistoryFileName(xlde->d_name))
		{
			if (XLogArchiveCheckDone(xlde->d_name))
			{
				elog(DEBUG2, "removing WAL backup history file \"%s\"",
					 xlde->d_name);
				/* POLAR: these files are under polar_datadir/pg_wal/ */
				polar_make_file_path_level3(path, XLOGDIR, xlde->d_name);
				polar_unlink(path);
				XLogArchiveCleanup(xlde->d_name);
			}
		}
	}

	FreeDir(xldir);
}

/*
<<<<<<< HEAD
 * Attempt to read an XLOG record.
 *
 * If RecPtr is valid, try to read a record at that position.  Otherwise
 * try to read a record just after the last one previously read.
 *
 * If no valid record is available, returns NULL, or fails if emode is PANIC.
 * (emode must be either PANIC, LOG). In standby mode, retries until a valid
 * record is available.
 *
 * The record is copied into readRecordBuf, so that on successful return,
 * the returned record pointer always points there.
 */
static XLogRecord *
ReadRecord(XLogReaderState *xlogreader, XLogRecPtr RecPtr, int emode,
		   bool fetching_ckpt)
{
	XLogRecord *record;
	XLogPageReadPrivate *private = (XLogPageReadPrivate *) xlogreader->private_data;

	/* Pass through parameters to XLogPageRead */
	private->fetching_ckpt = fetching_ckpt;
	private->emode = emode;
	private->randAccess = (RecPtr != InvalidXLogRecPtr);

	/* This is the first attempt to read this page. */
	lastSourceFailed = false;

	for (;;)
	{
		char	   *errormsg = NULL;

		/* POLAR: read xlog and put them on shared memory */
		if (polar_in_replica_mode() && StandbyMode)
		{
			if (reachedConsistency && POLAR_LOGINDEX_ENABLE_XLOG_QUEUE())
				record = polar_xlog_recv_queue_pop(polar_logindex_redo_instance->xlog_queue, xlogreader, RecPtr, &errormsg);
			else
				record = XLogReadRecord(xlogreader, RecPtr, &errormsg);
		}
		else
			record = XLogReadRecord(xlogreader, RecPtr, &errormsg);
		/* POLAR end */

		ReadRecPtr = xlogreader->ReadRecPtr;
		EndRecPtr = xlogreader->EndRecPtr;
		if (record == NULL)
		{
			if (readFile >= 0)
			{
				polar_close(readFile);
				readFile = -1;
			}

			/*
			 * We only end up here without a message when XLogPageRead()
			 * failed - in that case we already logged something. In
			 * StandbyMode that only happens if we have been triggered, so we
			 * shouldn't loop anymore in that case.
			 */
			if (errormsg)
				ereport(emode_for_corrupt_record(emode,
												 RecPtr ? RecPtr : EndRecPtr),
						(errmsg_internal("%s", errormsg) /* already translated */ ));
		}

		/*
		 * Check page TLI is one of the expected values.
		 */
		else if (!tliInHistory(xlogreader->latestPageTLI, expectedTLEs))
		{
			char		fname[MAXFNAMELEN];
			XLogSegNo	segno;
			int32		offset;

			XLByteToSeg(xlogreader->latestPagePtr, segno, wal_segment_size);
			offset = XLogSegmentOffset(xlogreader->latestPagePtr,
									   wal_segment_size);
			XLogFileName(fname, xlogreader->readPageTLI, segno,
						 wal_segment_size);
			ereport(emode_for_corrupt_record(emode,
											 RecPtr ? RecPtr : EndRecPtr),
					(errmsg("unexpected timeline ID %u in log segment %s, offset %u",
							xlogreader->latestPageTLI,
							fname,
							offset)));
			record = NULL;
		}

		if (record)
		{
			/* Great, got a record */
			return record;
		}
		else
		{
			/* No valid record available from this source */
			lastSourceFailed = true;

			/*
			 * If archive recovery was requested, but we were still doing
			 * crash recovery, switch to archive recovery and retry using the
			 * offline archive. We have now replayed all the valid WAL in
			 * pg_wal, so we are presumably now consistent.
			 *
			 * We require that there's at least some valid WAL present in
			 * pg_wal, however (!fetching_ckpt).  We could recover using the
			 * WAL from the archive, even if pg_wal is completely empty, but
			 * we'd have no idea how far we'd have to replay to reach
			 * consistency.  So err on the safe side and give up.
			 */
			if (!InArchiveRecovery && ArchiveRecoveryRequested &&
				!fetching_ckpt)
			{
				ereport(DEBUG1,
						(errmsg_internal("reached end of WAL in pg_wal, entering archive recovery")));
				InArchiveRecovery = true;
				if (StandbyModeRequested)
					StandbyMode = true;

				if (!polar_is_dma_data_node())
				{
				/* initialize minRecoveryPoint to this record */
				LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
				ControlFile->state = DB_IN_ARCHIVE_RECOVERY;
				if (ControlFile->minRecoveryPoint < EndRecPtr)
				{
					ControlFile->minRecoveryPoint = EndRecPtr;
					ControlFile->minRecoveryPointTLI = ThisTimeLineID;
				}
				/* update local copy */
				minRecoveryPoint = ControlFile->minRecoveryPoint;
				minRecoveryPointTLI = ControlFile->minRecoveryPointTLI;

				/*
				 * The startup process can update its local copy of
				 * minRecoveryPoint from this point.
				 */
				updateMinRecoveryPoint = true;

				UpdateControlFile();

				/*
				 * We update SharedRecoveryState while holding the lock on
				 * ControlFileLock so both states are consistent in shared
				 * memory.
				 */
				SpinLockAcquire(&XLogCtl->info_lck);
				XLogCtl->SharedRecoveryState = RECOVERY_STATE_ARCHIVE;
				SpinLockRelease(&XLogCtl->info_lck);

				LWLockRelease(ControlFileLock);
				}

				CheckRecoveryConsistency();

				/*
				 * Before we retry, reset lastSourceFailed and currentSource
				 * so that we will check the archive next.
				 */
				lastSourceFailed = false;
				currentSource = 0;

				continue;
			}

			/* In standby mode, loop back to retry. Otherwise, give up. */
			if (polar_is_dma_data_node())
			{
				if (!becameLeader)
					continue;
				else
					return NULL;
			}
			else if (StandbyMode && !CheckForStandbyTrigger() && !polar_is_datamax())
				continue;
			else
				return NULL;
		}
	}
}

/*
 * Scan for new timelines that might have appeared in the archive since we
 * started recovery.
 *
 * If there are any, the function changes recovery target TLI to the latest
 * one and returns 'true'.
 */
static bool
rescanLatestTimeLine(void)
{
	List	   *newExpectedTLEs;
	bool		found;
	ListCell   *cell;
	TimeLineID	newtarget;
	TimeLineID	oldtarget = recoveryTargetTLI;
	TimeLineHistoryEntry *currentTle = NULL;

	newtarget = findNewestTimeLine(recoveryTargetTLI);
	if (newtarget == recoveryTargetTLI)
	{
		/* No new timelines found */
		return false;
	}

	/*
	 * Determine the list of expected TLIs for the new TLI
	 */

	newExpectedTLEs = readTimeLineHistory(newtarget);

	/*
	 * If the current timeline is not part of the history of the new timeline,
	 * we cannot proceed to it.
	 */
	found = false;
	foreach(cell, newExpectedTLEs)
	{
		currentTle = (TimeLineHistoryEntry *) lfirst(cell);

		if (currentTle->tli == recoveryTargetTLI)
		{
			found = true;
			break;
		}
	}
	if (!found)
	{
		ereport(LOG,
				(errmsg("new timeline %u is not a child of database system timeline %u",
						newtarget,
						ThisTimeLineID)));
		return false;
	}

	/*
	 * The current timeline was found in the history file, but check that the
	 * next timeline was forked off from it *after* the current recovery
	 * location.
	 */
	if (!polar_is_datamax_mode && currentTle->end < EndRecPtr)
	{
		ereport(LOG,
				(errmsg("new timeline %u forked off current database system timeline %u before current recovery point %X/%X",
						newtarget,
						ThisTimeLineID,
						(uint32) (EndRecPtr >> 32), (uint32) EndRecPtr)));
		return false;
	}

	/* The new timeline history seems valid. Switch target */
	recoveryTargetTLI = newtarget;
	list_free_deep(expectedTLEs);
	expectedTLEs = newExpectedTLEs;

	/*
	 * As in StartupXLOG(), try to ensure we have all the history files
	 * between the old target and new target in pg_wal.
	 */
	restoreTimeLineHistoryFiles(oldtarget + 1, newtarget);

	ereport(LOG,
			(errmsg("new target timeline is %u",
					recoveryTargetTLI)));

	return true;
}

/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 * I/O routines for pg_control
 *
 * *ControlFile is a buffer in shared memory that holds an image of the
 * contents of pg_control.  WriteControlFile() initializes pg_control
 * given a preloaded buffer, ReadControlFile() loads the buffer from
 * the pg_control file (during postmaster or standalone-backend startup),
 * and UpdateControlFile() rewrites pg_control after we modify xlog state.
 * InitControlFile() fills the buffer with initial values.
 *
 * For simplicity, WriteControlFile() initializes the fields of pg_control
 * that are related to checking backend/database compatibility, and
 * ReadControlFile() verifies they are correct.  We could split out the
 * I/O and compatibility-check functions, but there seems no need currently.
 */

static void
InitControlFile(uint64 sysidentifier, uint32 data_checksum_version)
{
	char		mock_auth_nonce[MOCK_AUTH_NONCE_LEN];

	/*
	 * Generate a random nonce. This is used for authentication requests that
	 * will fail because the user does not exist. The nonce is used to create
	 * a genuine-looking password challenge for the non-existent user, in lieu
	 * of an actual stored password.
	 */
	if (!pg_strong_random(mock_auth_nonce, MOCK_AUTH_NONCE_LEN))
		ereport(PANIC,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("could not generate secret authorization token")));

	memset(ControlFile, 0, sizeof(ControlFileData));
	/* Initialize pg_control status fields */
	ControlFile->system_identifier = sysidentifier;
	memcpy(ControlFile->mock_authentication_nonce, mock_auth_nonce, MOCK_AUTH_NONCE_LEN);
	ControlFile->state = DB_SHUTDOWNED;
	ControlFile->unloggedLSN = FirstNormalUnloggedLSN;

	/* Set important parameter values for use when replaying WAL */
	ControlFile->MaxConnections = MaxConnections;
	ControlFile->max_worker_processes = max_worker_processes;
	ControlFile->max_wal_senders = max_wal_senders;
	ControlFile->max_prepared_xacts = max_prepared_xacts;
	ControlFile->max_locks_per_xact = max_locks_per_xact;
	ControlFile->wal_level = wal_level;
	ControlFile->wal_log_hints = wal_log_hints;
	ControlFile->track_commit_timestamp = track_commit_timestamp;
	ControlFile->data_checksum_version = data_checksum_version;
}

static void
WriteControlFile(void)
{
	int			fd;
	char		buffer[PG_CONTROL_FILE_SIZE];	/* need not be aligned */
	char		polar_ctl_file[MAXPGPATH];

	/*
	 * Initialize version and compatibility-check fields
	 */
	ControlFile->pg_control_version = PG_CONTROL_VERSION;
	ControlFile->catalog_version_no = CATALOG_VERSION_NO;

	ControlFile->maxAlign = MAXIMUM_ALIGNOF;
	ControlFile->floatFormat = FLOATFORMAT_VALUE;

	ControlFile->blcksz = BLCKSZ;
	ControlFile->relseg_size = RELSEG_SIZE;
	ControlFile->xlog_blcksz = XLOG_BLCKSZ;
	ControlFile->xlog_seg_size = wal_segment_size;

	ControlFile->nameDataLen = NAMEDATALEN;
	ControlFile->indexMaxKeys = INDEX_MAX_KEYS;

	ControlFile->toast_max_chunk_size = TOAST_MAX_CHUNK_SIZE;
	ControlFile->loblksize = LOBLKSIZE;

	ControlFile->float8ByVal = FLOAT8PASSBYVAL;

	/* Contents are protected with a CRC */
	INIT_CRC32C(ControlFile->crc);
	COMP_CRC32C(ControlFile->crc,
				(char *) ControlFile,
				offsetof(ControlFileData, crc));
	FIN_CRC32C(ControlFile->crc);

	/*
	 * We write out PG_CONTROL_FILE_SIZE bytes into pg_control, zero-padding
	 * the excess over sizeof(ControlFileData).  This reduces the odds of
	 * premature-EOF errors when reading pg_control.  We'll still fail when we
	 * check the contents of the file, but hopefully with a more specific
	 * error than "couldn't read pg_control".
	 */
	memset(buffer, 0, PG_CONTROL_FILE_SIZE);
	memcpy(buffer, ControlFile, sizeof(ControlFileData));

	polar_make_file_path_level2(polar_ctl_file, XLOG_CONTROL_FILE);

	fd = BasicOpenFile(polar_ctl_file,
						   O_RDWR | O_CREAT | O_EXCL | PG_BINARY, true);
	if (fd < 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not create file \"%s\": %m",
						XLOG_CONTROL_FILE)));

	errno = 0;
	pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_WRITE);
	if (polar_write(fd, buffer, PG_CONTROL_FILE_SIZE) != PG_CONTROL_FILE_SIZE)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not write to file \"%s\": %m",
						XLOG_CONTROL_FILE)));
	}
	pgstat_report_wait_end();

	pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_SYNC);
	if (polar_fsync(fd) != 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m",
						XLOG_CONTROL_FILE)));
	pgstat_report_wait_end();

<<<<<<< HEAD
	if (polar_close(fd))
=======
	if (close(fd) != 0)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m",
						XLOG_CONTROL_FILE)));
}

/* POLAR: BasedOn ReadControlFile function */
bool
polar_read_control_file(char *polar_ctl_file_path, ControlFileData *polar_control_file_buffer, int error_level, int *saved_errno)
{
	pg_crc32c	crc;
	int			fd;
<<<<<<< HEAD
=======
	char		wal_segsz_str[20];
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	int			r;
	int			readflag = PG_BINARY;

	if (polar_ctl_file_path == NULL)
		elog(FATAL, "pg_control file path is NULL");
	if (polar_control_file_buffer == NULL)
		elog(FATAL, "pg_control data buffer is NULL");
	if (saved_errno == NULL)
		elog(FATAL, "saved errno is NULL");

	/* POLAR: read controlfile only need O_RDONLY */
	readflag |= O_RDONLY;
	
	/* POLAR: open control file based on given path */
	fd = BasicOpenFile(polar_ctl_file_path, readflag, true);
	if (fd < 0)
	{
		if (errno == ENOENT)
		{
			*saved_errno = errno;
			/* POLAR: report error message based on given error_level */
			ereport(error_level,
					(errcode_for_file_access(),
				 	errmsg("control file \"%s\" not exist: %m",
							polar_ctl_file_path)));
			return false;
		}
		else
		{
			ereport(PANIC,
				(errcode_for_file_access(),
<<<<<<< HEAD
				 errmsg("could not open control file \"%s\": %m",
						 polar_ctl_file_path)));
		}
	}
=======
				 errmsg("could not open file \"%s\": %m",
						XLOG_CONTROL_FILE)));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_READ);
	r = polar_read(fd, polar_control_file_buffer, sizeof(ControlFileData));
	if (r != sizeof(ControlFileData))
	{
		if (r < 0)
			ereport(PANIC,
					(errcode_for_file_access(),
<<<<<<< HEAD
					 errmsg("could not read from control file \"%s\": %m", 
					 		 polar_ctl_file_path)));
		else
			ereport(PANIC,
					(errmsg("could not read from control file \"%s\": read %d bytes, expected %d", 
					 		 polar_ctl_file_path, r, (int) sizeof(ControlFileData))));
=======
					 errmsg("could not read file \"%s\": %m",
							XLOG_CONTROL_FILE)));
		else
			ereport(PANIC,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("could not read file \"%s\": read %d of %zu",
							XLOG_CONTROL_FILE, r, sizeof(ControlFileData))));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
	pgstat_report_wait_end();

	polar_close(fd);

	elog(LOG, "polardb read control file \"%s\" success", polar_ctl_file_path);

	/*
	 * Check for expected pg_control format version.  If this is wrong, the
	 * CRC check will likely fail because we'll be checking the wrong number
	 * of bytes.  Complaining about wrong version will probably be more
	 * enlightening than complaining about wrong CRC.
	 */

	if (polar_control_file_buffer->pg_control_version != PG_CONTROL_VERSION && polar_control_file_buffer->pg_control_version % 65536 == 0 && polar_control_file_buffer->pg_control_version / 65536 != 0)
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with PG_CONTROL_VERSION %d (0x%08x),"
						   " but the server was compiled with PG_CONTROL_VERSION %d (0x%08x).",
						   polar_control_file_buffer->pg_control_version, polar_control_file_buffer->pg_control_version,
						   PG_CONTROL_VERSION, PG_CONTROL_VERSION),
				 errhint("This could be a problem of mismatched byte ordering.  It looks like you need to initdb.")));

	if (polar_control_file_buffer->pg_control_version != PG_CONTROL_VERSION)
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with PG_CONTROL_VERSION %d,"
						   " but the server was compiled with PG_CONTROL_VERSION %d.",
						   polar_control_file_buffer->pg_control_version, PG_CONTROL_VERSION),
				 errhint("It looks like you need to initdb.")));

	/* Now check the CRC. */
	INIT_CRC32C(crc);
	COMP_CRC32C(crc,
				(char *) polar_control_file_buffer,
				offsetof(ControlFileData, crc));
	FIN_CRC32C(crc);

	if (!EQ_CRC32C(crc, polar_control_file_buffer->crc))
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("incorrect checksum in control file")));

	/*
	 * Do compatibility checking immediately.  If the database isn't
	 * compatible with the backend executable, we want to abort before we can
	 * possibly do any damage.
	 */
	if (polar_control_file_buffer->catalog_version_no != CATALOG_VERSION_NO)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with CATALOG_VERSION_NO %d,"
						   " but the server was compiled with CATALOG_VERSION_NO %d.",
						   polar_control_file_buffer->catalog_version_no, CATALOG_VERSION_NO),
=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "CATALOG_VERSION_NO", ControlFile->catalog_version_no,
						   "CATALOG_VERSION_NO", CATALOG_VERSION_NO),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				 errhint("It looks like you need to initdb.")));
	if (polar_control_file_buffer->maxAlign != MAXIMUM_ALIGNOF)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with MAXALIGN %d,"
						   " but the server was compiled with MAXALIGN %d.",
						   polar_control_file_buffer->maxAlign, MAXIMUM_ALIGNOF),
=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "MAXALIGN", ControlFile->maxAlign,
						   "MAXALIGN", MAXIMUM_ALIGNOF),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				 errhint("It looks like you need to initdb.")));
	if (polar_control_file_buffer->floatFormat != FLOATFORMAT_VALUE)
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
				 errdetail("The database cluster appears to use a different floating-point number format than the server executable."),
				 errhint("It looks like you need to initdb.")));
	if (polar_control_file_buffer->blcksz != BLCKSZ)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with BLCKSZ %d,"
						   " but the server was compiled with BLCKSZ %d.",
						   polar_control_file_buffer->blcksz, BLCKSZ),
=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "BLCKSZ", ControlFile->blcksz,
						   "BLCKSZ", BLCKSZ),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				 errhint("It looks like you need to recompile or initdb.")));
	if (polar_control_file_buffer->relseg_size != RELSEG_SIZE)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with RELSEG_SIZE %d,"
						   " but the server was compiled with RELSEG_SIZE %d.",
						   polar_control_file_buffer->relseg_size, RELSEG_SIZE),
=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "RELSEG_SIZE", ControlFile->relseg_size,
						   "RELSEG_SIZE", RELSEG_SIZE),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				 errhint("It looks like you need to recompile or initdb.")));
	if (polar_control_file_buffer->xlog_blcksz != XLOG_BLCKSZ)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with XLOG_BLCKSZ %d,"
						   " but the server was compiled with XLOG_BLCKSZ %d.",
						   polar_control_file_buffer->xlog_blcksz, XLOG_BLCKSZ),
=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "XLOG_BLCKSZ", ControlFile->xlog_blcksz,
						   "XLOG_BLCKSZ", XLOG_BLCKSZ),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				 errhint("It looks like you need to recompile or initdb.")));
	if (polar_control_file_buffer->nameDataLen != NAMEDATALEN)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with NAMEDATALEN %d,"
						   " but the server was compiled with NAMEDATALEN %d.",
						   polar_control_file_buffer->nameDataLen, NAMEDATALEN),
=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "NAMEDATALEN", ControlFile->nameDataLen,
						   "NAMEDATALEN", NAMEDATALEN),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				 errhint("It looks like you need to recompile or initdb.")));
	if (polar_control_file_buffer->indexMaxKeys != INDEX_MAX_KEYS)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with INDEX_MAX_KEYS %d,"
						   " but the server was compiled with INDEX_MAX_KEYS %d.",
						   polar_control_file_buffer->indexMaxKeys, INDEX_MAX_KEYS),
=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "INDEX_MAX_KEYS", ControlFile->indexMaxKeys,
						   "INDEX_MAX_KEYS", INDEX_MAX_KEYS),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				 errhint("It looks like you need to recompile or initdb.")));
	if (polar_control_file_buffer->toast_max_chunk_size != TOAST_MAX_CHUNK_SIZE)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with TOAST_MAX_CHUNK_SIZE %d,"
						   " but the server was compiled with TOAST_MAX_CHUNK_SIZE %d.",
						   polar_control_file_buffer->toast_max_chunk_size, (int) TOAST_MAX_CHUNK_SIZE),
=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "TOAST_MAX_CHUNK_SIZE", ControlFile->toast_max_chunk_size,
						   "TOAST_MAX_CHUNK_SIZE", (int) TOAST_MAX_CHUNK_SIZE),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				 errhint("It looks like you need to recompile or initdb.")));
	if (polar_control_file_buffer->loblksize != LOBLKSIZE)
		ereport(FATAL,
<<<<<<< HEAD
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with LOBLKSIZE %d,"
						   " but the server was compiled with LOBLKSIZE %d.",
						   polar_control_file_buffer->loblksize, (int) LOBLKSIZE),
				 errhint("It looks like you need to recompile or initdb.")));

#ifdef USE_FLOAT4_BYVAL
	if (polar_control_file_buffer->float4ByVal != true)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized without USE_FLOAT4_BYVAL"
						   " but the server was compiled with USE_FLOAT4_BYVAL."),
				 errhint("It looks like you need to recompile or initdb.")));
#else
	if (polar_control_file_buffer->float4ByVal != false)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with USE_FLOAT4_BYVAL"
						   " but the server was compiled without USE_FLOAT4_BYVAL."),
				 errhint("It looks like you need to recompile or initdb.")));
#endif

=======
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
		/* translator: %s is a variable name and %d is its value */
				 errdetail("The database cluster was initialized with %s %d,"
						   " but the server was compiled with %s %d.",
						   "LOBLKSIZE", ControlFile->loblksize,
						   "LOBLKSIZE", (int) LOBLKSIZE),
				 errhint("It looks like you need to recompile or initdb.")));

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#ifdef USE_FLOAT8_BYVAL
	if (polar_control_file_buffer->float8ByVal != true)
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized without USE_FLOAT8_BYVAL"
						   " but the server was compiled with USE_FLOAT8_BYVAL."),
				 errhint("It looks like you need to recompile or initdb.")));
#else
	if (polar_control_file_buffer->float8ByVal != false)
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with USE_FLOAT8_BYVAL"
						   " but the server was compiled without USE_FLOAT8_BYVAL."),
				 errhint("It looks like you need to recompile or initdb.")));
#endif

	if (!IsValidWalSegSize(polar_control_file_buffer->xlog_seg_size))
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
<<<<<<< HEAD
						errmsg_plural("WAL segment size must be a power of two between 1 MB and 1 GB, but the control file specifies %d byte",
									  "WAL segment size must be a power of two between 1 MB and 1 GB, but the control file specifies %d bytes",
									  polar_control_file_buffer->xlog_seg_size,
									  polar_control_file_buffer->xlog_seg_size)));
	return true;
}
=======
						errmsg_plural("invalid WAL segment size in control file (%d byte)",
									  "invalid WAL segment size in control file (%d bytes)",
									  wal_segment_size,
									  wal_segment_size),
						errdetail("The WAL segment size must be a power of two between 1 MB and 1 GB.")));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

static void
ReadControlFile(void)
{
	static char wal_segsz_str[20];
	char		polar_ctl_file[MAXPGPATH];
	int			saved_errno;

	/*
	 * POLAR: When pg start, in the postmaster,
	 * shared storage has not been mounted in the plugin.
	 * skip to load controlfile, read it later.
	 */
	if (polar_enable_shared_storage_mode &&
		polar_vfs_switch == POLAR_VFS_SWITCH_LOCAL)
	{
		elog(WARNING, "polardb skip readControlfile");
		return;
	}

	/* POLAR: read local pg_control file when in replica mode */
	if (polar_in_replica_mode())
		snprintf(polar_ctl_file, MAXPGPATH, "%s", XLOG_CONTROL_FILE);
	else
		polar_make_file_path_level2(polar_ctl_file, XLOG_CONTROL_FILE);
	
	/* POLAR: read and check control file */
	if (polar_read_control_file(polar_ctl_file, ControlFile, PANIC, &saved_errno))
		elog(LOG, "polardb load and check controlfile success");

	wal_segment_size = ControlFile->xlog_seg_size;
	snprintf(wal_segsz_str, sizeof(wal_segsz_str), "%d", wal_segment_size);
	SetConfigOption("wal_segment_size", wal_segsz_str, PGC_INTERNAL,
					PGC_S_DYNAMIC_DEFAULT);

	/* check and update variables dependent on wal_segment_size */
	if (ConvertToXSegs(min_wal_size_mb, wal_segment_size) < 2)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
<<<<<<< HEAD
						errmsg("\"min_wal_size\" must be at least twice \"wal_segment_size\"")));

	if (ConvertToXSegs(max_wal_size_mb, wal_segment_size) < 2)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("\"max_wal_size\" must be at least twice \"wal_segment_size\"")));
=======
		/* translator: both %s are GUC names */
						errmsg("\"%s\" must be at least twice \"%s\"",
							   "min_wal_size", "wal_segment_size")));

	if (ConvertToXSegs(max_wal_size_mb, wal_segment_size) < 2)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		/* translator: both %s are GUC names */
						errmsg("\"%s\" must be at least twice \"%s\"",
							   "max_wal_size", "wal_segment_size")));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	UsableBytesInSegment =
		(wal_segment_size / XLOG_BLCKSZ * UsableBytesInPage) -
		(SizeOfXLogLongPHD - SizeOfXLogShortPHD);

	CalculateCheckpointSegments();

	/* Make the initdb settings visible as GUC variables, too */
	SetConfigOption("data_checksums", DataChecksumsEnabled() ? "yes" : "no",
					PGC_INTERNAL, PGC_S_DYNAMIC_DEFAULT);
}

/*
 * Utility wrapper to update the control file.  Note that the control
 * file gets flushed.
 */
static void
UpdateControlFile(void)
{
<<<<<<< HEAD
	int			fd;
	char		polar_file_path[MAXPGPATH];
	bool 		polar_double_write = false;

	INIT_CRC32C(ControlFile->crc);
	COMP_CRC32C(ControlFile->crc,
				(char *) ControlFile,
				offsetof(ControlFileData, crc));
	FIN_CRC32C(ControlFile->crc);

	if (polar_in_replica_mode())
		snprintf(polar_file_path, MAXPGPATH, "%s", XLOG_CONTROL_FILE);
	else
	{
		/* POLAR: FATAL after detection of double write in one shared storage. */
		if (polar_enable_io_fencing &&
			POLAR_IO_FENCING_GET_STATE(&XLogCtl->polar_io_fencing) == POLAR_IO_FENCING_WAIT)
		{
			elog(LOG, "state is %d", ControlFile->state);
			POLAR_IO_FENCING_WAIT_FOR(&XLogCtl->polar_io_fencing);
			POLAR_IO_FENCING_SET_STATE(&XLogCtl->polar_io_fencing, POLAR_IO_FENCING_NORMAL);
		}
		polar_check_double_write();
		polar_make_file_path_level2(polar_file_path, XLOG_CONTROL_FILE);
		polar_double_write = polar_is_standby();
	}

write_control_file:
	fd = BasicOpenFile(polar_file_path,
						   O_RDWR | PG_BINARY, true);
	if (fd < 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not open control file \"%s\": %m",
						XLOG_CONTROL_FILE)));

	errno = 0;
	pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_WRITE_UPDATE);
	if (polar_write(fd, ControlFile, sizeof(ControlFileData)) != sizeof(ControlFileData))
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not write to control file: %m")));
	}
	pgstat_report_wait_end();

	pgstat_report_wait_start(WAIT_EVENT_CONTROL_FILE_SYNC_UPDATE);
	if (polar_fsync(fd) != 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not fsync control file: %m")));
	pgstat_report_wait_end();

	if (polar_close(fd))
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not close control file: %m")));

	if (polar_double_write)
	{
		snprintf(polar_file_path, MAXPGPATH, "%s", XLOG_CONTROL_FILE);
		polar_double_write = false;
		goto write_control_file;
	}
=======
	update_controlfile(DataDir, ControlFile, true);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Returns the unique system identifier from control file.
 */
uint64
GetSystemIdentifier(void)
{
	Assert(ControlFile != NULL);
	return ControlFile->system_identifier;
}

/*
 * Returns the random nonce from control file.
 */
char *
GetMockAuthenticationNonce(void)
{
	Assert(ControlFile != NULL);
	return ControlFile->mock_authentication_nonce;
}

/*
 * Are checksums enabled for data pages?
 */
bool
DataChecksumsEnabled(void)
{
	Assert(ControlFile != NULL);
	return (ControlFile->data_checksum_version > 0);
}

/*
 * Returns a fake LSN for unlogged relations.
 *
 * Each call generates an LSN that is greater than any previous value
 * returned. The current counter value is saved and restored across clean
 * shutdowns, but like unlogged relations, does not survive a crash. This can
 * be used in lieu of real LSN values returned by XLogInsert, if you need an
 * LSN-like increasing sequence of numbers without writing any WAL.
 */
XLogRecPtr
GetFakeLSNForUnloggedRel(void)
{
	return pg_atomic_fetch_add_u64(&XLogCtl->unloggedLSN, 1);
}

/*
 * Auto-tune the number of XLOG buffers.
 *
 * The preferred setting for wal_buffers is about 3% of shared_buffers, with
 * a maximum of one XLOG segment (there is little reason to think that more
 * is helpful, at least so long as we force an fsync when switching log files)
 * and a minimum of 8 blocks (which was the default value prior to PostgreSQL
 * 9.1, when auto-tuning was added).
 *
 * This should not be called until NBuffers has received its final value.
 */
static int
XLOGChooseNumBuffers(void)
{
	int			xbuffers;

	xbuffers = NBuffers / 32;
	if (xbuffers > (wal_segment_size / XLOG_BLCKSZ))
		xbuffers = (wal_segment_size / XLOG_BLCKSZ);
	if (xbuffers < 8)
		xbuffers = 8;
	return xbuffers;
}

/*
 * GUC check_hook for wal_buffers
 */
bool
check_wal_buffers(int *newval, void **extra, GucSource source)
{
	/*
	 * -1 indicates a request for auto-tune.
	 */
	if (*newval == -1)
	{
		/*
		 * If we haven't yet changed the boot_val default of -1, just let it
		 * be.  We'll fix it when XLOGShmemSize is called.
		 */
		if (XLOGbuffers == -1)
			return true;

		/* Otherwise, substitute the auto-tune value */
		*newval = XLOGChooseNumBuffers();
	}

	/*
	 * We clamp manually-set values to at least 4 blocks.  Prior to PostgreSQL
	 * 9.1, a minimum of 4 was enforced by guc.c, but since that is no longer
	 * the case, we just silently treat such values as a request for the
	 * minimum.  (We could throw an error instead, but that doesn't seem very
	 * helpful.)
	 */
	if (*newval < 4)
		*newval = 4;

	return true;
}

/*
 * GUC check_hook for wal_consistency_checking
 */
bool
check_wal_consistency_checking(char **newval, void **extra, GucSource source)
{
	char	   *rawstring;
	List	   *elemlist;
	ListCell   *l;
	bool		newwalconsistency[RM_MAX_ID + 1];

	/* Initialize the array */
	MemSet(newwalconsistency, 0, (RM_MAX_ID + 1) * sizeof(bool));

	/* Need a modifiable copy of string */
	rawstring = pstrdup(*newval);

	/* Parse string into list of identifiers */
	if (!SplitIdentifierString(rawstring, ',', &elemlist))
	{
		/* syntax error in list */
		GUC_check_errdetail("List syntax is invalid.");
		pfree(rawstring);
		list_free(elemlist);
		return false;
	}

	foreach(l, elemlist)
	{
		char	   *tok = (char *) lfirst(l);
		int			rmid;

		/* Check for 'all'. */
		if (pg_strcasecmp(tok, "all") == 0)
		{
			for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
				if (RmgrIdExists(rmid) && GetRmgr(rmid).rm_mask != NULL)
					newwalconsistency[rmid] = true;
		}
		else
		{
			/* Check if the token matches any known resource manager. */
			bool		found = false;

			for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
			{
				if (RmgrIdExists(rmid) && GetRmgr(rmid).rm_mask != NULL &&
					pg_strcasecmp(tok, GetRmgr(rmid).rm_name) == 0)
				{
					newwalconsistency[rmid] = true;
					found = true;
					break;
				}
			}
			if (!found)
			{
				/*
				 * During startup, it might be a not-yet-loaded custom
				 * resource manager.  Defer checking until
				 * InitializeWalConsistencyChecking().
				 */
				if (!process_shared_preload_libraries_done)
				{
					check_wal_consistency_checking_deferred = true;
				}
				else
				{
					GUC_check_errdetail("Unrecognized key word: \"%s\".", tok);
					pfree(rawstring);
					list_free(elemlist);
					return false;
				}
			}
		}
	}

	pfree(rawstring);
	list_free(elemlist);

	/* assign new value */
	*extra = guc_malloc(ERROR, (RM_MAX_ID + 1) * sizeof(bool));
	memcpy(*extra, newwalconsistency, (RM_MAX_ID + 1) * sizeof(bool));
	return true;
}

/*
 * GUC assign_hook for wal_consistency_checking
 */
void
assign_wal_consistency_checking(const char *newval, void *extra)
{
	/*
	 * If some checks were deferred, it's possible that the checks will fail
	 * later during InitializeWalConsistencyChecking(). But in that case, the
	 * postmaster will exit anyway, so it's safe to proceed with the
	 * assignment.
	 *
	 * Any built-in resource managers specified are assigned immediately,
	 * which affects WAL created before shared_preload_libraries are
	 * processed. Any custom resource managers specified won't be assigned
	 * until after shared_preload_libraries are processed, but that's OK
	 * because WAL for a custom resource manager can't be written before the
	 * module is loaded anyway.
	 */
	wal_consistency_checking = extra;
}

/*
 * InitializeWalConsistencyChecking: run after loading custom resource managers
 *
 * If any unknown resource managers were specified in the
 * wal_consistency_checking GUC, processing was deferred.  Now that
 * shared_preload_libraries have been loaded, process wal_consistency_checking
 * again.
 */
void
InitializeWalConsistencyChecking(void)
{
	Assert(process_shared_preload_libraries_done);

	if (check_wal_consistency_checking_deferred)
	{
		struct config_generic *guc;

		guc = find_option("wal_consistency_checking", false, false, ERROR);

		check_wal_consistency_checking_deferred = false;

		set_config_option_ext("wal_consistency_checking",
							  wal_consistency_checking_string,
							  guc->scontext, guc->source, guc->srole,
							  GUC_ACTION_SET, true, ERROR, false);

		/* checking should not be deferred again */
		Assert(!check_wal_consistency_checking_deferred);
	}
}

/*
 * GUC show_hook for archive_command
 */
const char *
show_archive_command(void)
{
	if (XLogArchivingActive())
		return XLogArchiveCommand;
	else
		return "(disabled)";
}

/*
 * GUC show_hook for in_hot_standby
 */
const char *
show_in_hot_standby(void)
{
	/*
	 * We display the actual state based on shared memory, so that this GUC
	 * reports up-to-date state if examined intra-query.  The underlying
	 * variable (in_hot_standby_guc) changes only when we transmit a new value
	 * to the client.
	 */
	return RecoveryInProgress() ? "on" : "off";
}

/*
 * Read the control file, set respective GUCs.
 *
 * This is to be called during startup, including a crash recovery cycle,
 * unless in bootstrap mode, where no control file yet exists.  As there's no
 * usable shared memory yet (its sizing can depend on the contents of the
 * control file!), first store the contents in local memory. XLOGShmemInit()
 * will then copy it to shared memory later.
 *
 * reset just controls whether previous contents are to be expected (in the
 * reset case, there's a dangling pointer into old shared memory), or not.
 */
void
LocalProcessControlFile(bool reset)
{
	Assert(reset || ControlFile == NULL);
	/* POLAR: keep memory clean */
	ControlFile = palloc0(sizeof(ControlFileData));
	ReadControlFile();
}

/*
 * Get the wal_level from the control file. For a standby, this value should be
 * considered as its active wal_level, because it may be different from what
 * was originally configured on standby.
 */
WalLevel
GetActiveWalLevelOnStandby(void)
{
	return ControlFile->wal_level;
}

/*
 * Initialization of shared memory for XLOG
 */
Size
XLOGShmemSize(void)
{
	Size		size;

	/*
	 * If the value of wal_buffers is -1, use the preferred auto-tune value.
	 * This isn't an amazingly clean place to do this, but we must wait till
	 * NBuffers has received its final value, and must do it before using the
	 * value of XLOGbuffers to do anything important.
	 *
	 * We prefer to report this value's source as PGC_S_DYNAMIC_DEFAULT.
	 * However, if the DBA explicitly set wal_buffers = -1 in the config file,
	 * then PGC_S_DYNAMIC_DEFAULT will fail to override that and we must force
	 * the matter with PGC_S_OVERRIDE.
	 */
	if (XLOGbuffers == -1)
	{
		char		buf[32];

		snprintf(buf, sizeof(buf), "%d", XLOGChooseNumBuffers());
		SetConfigOption("wal_buffers", buf, PGC_POSTMASTER,
						PGC_S_DYNAMIC_DEFAULT);
		if (XLOGbuffers == -1)	/* failed to apply it? */
			SetConfigOption("wal_buffers", buf, PGC_POSTMASTER,
							PGC_S_OVERRIDE);
	}
	Assert(XLOGbuffers > 0);

	/* XLogCtl */
	size = sizeof(XLogCtlData);

	/* WAL insertion locks, plus alignment */
	size = add_size(size, mul_size(sizeof(WALInsertLockPadded), polar_wal_buffer_insert_locks + 1));
	/* xlblocks array */
	size = add_size(size, mul_size(sizeof(pg_atomic_uint64), XLOGbuffers));
	/* extra alignment padding for XLOG I/O buffers */
	size = add_size(size, Max(XLOG_BLCKSZ, PG_IO_ALIGN_SIZE));
	/* and the buffers themselves */
	size = add_size(size, mul_size(XLOG_BLCKSZ, XLOGbuffers));

	/*
	 * Note: we don't count ControlFileData, it comes out of the "slop factor"
	 * added by CreateSharedMemoryAndSemaphores.  This lets us use this
	 * routine again below to compute the actual allocation size.
	 */

	/* 
	 * POLAR wal pipeline
	 */
	if (POLAR_WAL_PIPELINER_ENABLE())
	{
		/*
		 *	Mutex access of wait object need lock page in OS kernel,
		 *	we should try to align with OS page, or else we may 
		 *  get severely page lock contend, which will raise cpu iowait
		 */
		if (polar_wal_pipeline_wait_object_align)
		{
			int align_size;
			if (huge_pages == HUGE_PAGES_ON)
				align_size = OS_HUGE_PAGE_SIZE;
			else
				align_size = OS_DEFAULT_PAGE_SIZE;
			
			/* extra alignment padding */
			size = add_size(size, align_size);

			size = add_size(size, mul_size(TYPEALIGN(align_size, sizeof(polar_wait_object_t)), POLAR_WAL_PIPELINE_MAX_THREAD_NUM));
			size = add_size(size, mul_size(TYPEALIGN(align_size, sizeof(polar_wal_pipeline_flush_event_t)), polar_wal_pipeline_flush_event_array_size));
		} 
		else
		{
			size = add_size(size, mul_size(sizeof(polar_wait_object_t), POLAR_WAL_PIPELINE_MAX_THREAD_NUM));
			size = add_size(size, mul_size(sizeof(polar_wal_pipeline_flush_event_t), polar_wal_pipeline_flush_event_array_size));
		}
		
		size = add_size(size, mul_size(sizeof(polar_wal_pipeline_unflushed_xlog_slot_t), polar_wal_pipeline_unflushed_xlog_array_size));
		size = add_size(size, mul_size(sizeof(pg_atomic_uint64), polar_wal_pipeline_recent_written_array_size));
	}

	return size;
}

void
XLOGShmemInit(void)
{
	bool		foundCFile,
				foundXLog;
	char	   *allocptr;
	int			i;
	ControlFileData *localControlFile;
	/* POLAR */
	int			polar_lock_tranche;

#ifdef WAL_DEBUG

	/*
	 * Create a memory context for WAL debugging that's exempt from the normal
	 * "no pallocs in critical section" rule. Yes, that can lead to a PANIC if
	 * an allocation fails, but wal_debug is not for production use anyway.
	 */
	if (walDebugCxt == NULL)
	{
		walDebugCxt = AllocSetContextCreate(TopMemoryContext,
											"WAL Debug",
											ALLOCSET_DEFAULT_SIZES);
		MemoryContextAllowInCriticalSection(walDebugCxt, true);
	}
#endif


	XLogCtl = (XLogCtlData *)
		ShmemInitStruct("XLOG Ctl", XLOGShmemSize(), &foundXLog);

	localControlFile = ControlFile;

	ControlFile = (ControlFileData *)
		ShmemInitStruct("Control File", sizeof(ControlFileData), &foundCFile);

	if (foundCFile || foundXLog)
	{
		/* both should be present or neither */
		Assert(foundCFile && foundXLog);

		/* Initialize local copy of WALInsertLocks */
		WALInsertLocks = XLogCtl->Insert.WALInsertLocks;

		if (localControlFile)
			pfree(localControlFile);
		return;
	}
	memset(XLogCtl, 0, sizeof(XLogCtlData));

	/*
	 * POLAR: Before mount shared storage controlfile is not loaded.
	 * At this point shared storage has been mounted.
	 * So, we check and load it.
	 */
	if (POLAR_FILE_IN_SHARED_STORAGE())
		elog(LOG, "polardb delay load controlfile");

	/*
	 * Already have read control file locally, unless in bootstrap mode. Move
	 * contents into shared memory.
	 */
	/*
	 * POLAR: not read controlfile in LocalProcessControlFile,
	 * but init memory here
	 */
	if (localControlFile)
	{
		if (POLAR_FILE_IN_SHARED_STORAGE())
			memset(ControlFile, 0, sizeof(ControlFileData));
		else
			memcpy(ControlFile, localControlFile, sizeof(ControlFileData));

		pfree(localControlFile);
	}

	/*
	 * Since XLogCtlData contains XLogRecPtr fields, its sizeof should be a
	 * multiple of the alignment for same, so no extra alignment padding is
	 * needed here.
	 */
	allocptr = ((char *) XLogCtl) + sizeof(XLogCtlData);
	XLogCtl->xlblocks = (pg_atomic_uint64 *) allocptr;
	allocptr += sizeof(pg_atomic_uint64) * XLOGbuffers;

	for (i = 0; i < XLOGbuffers; i++)
	{
		pg_atomic_init_u64(&XLogCtl->xlblocks[i], InvalidXLogRecPtr);
	}

	/* WAL insertion locks. Ensure they're aligned to the full padded size */
	allocptr += sizeof(WALInsertLockPadded) -
		((uintptr_t) allocptr) % sizeof(WALInsertLockPadded);
	WALInsertLocks = XLogCtl->Insert.WALInsertLocks =
		(WALInsertLockPadded *) allocptr;
	allocptr += sizeof(WALInsertLockPadded) * polar_wal_buffer_insert_locks;

<<<<<<< HEAD
	LWLockRegisterTranche(LWTRANCHE_WAL_INSERT, "wal_insert");
	for (i = 0; i < polar_wal_buffer_insert_locks; i++)
=======
	for (i = 0; i < NUM_XLOGINSERT_LOCKS; i++)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	{
		LWLockInitialize(&WALInsertLocks[i].l.lock, LWTRANCHE_WAL_INSERT);
		pg_atomic_init_u64(&WALInsertLocks[i].l.insertingAt, InvalidXLogRecPtr);
		WALInsertLocks[i].l.lastImportantAt = InvalidXLogRecPtr;
	}

	/*
	 * Align the start of the page buffers to a full xlog block size boundary.
	 * This simplifies some calculations in XLOG insertion. It is also
	 * required for O_DIRECT.
	 */
	allocptr = (char *) TYPEALIGN(XLOG_BLCKSZ, allocptr);
	XLogCtl->pages = allocptr;
	memset(XLogCtl->pages, 0, (Size) XLOG_BLCKSZ * XLOGbuffers);
	allocptr += (Size) XLOG_BLCKSZ * XLOGbuffers;

	/*
	 * Do basic initialization of XLogCtl shared data. (StartupXLOG will fill
	 * in additional info.)
	 */
	XLogCtl->XLogCacheBlck = XLOGbuffers - 1;
	XLogCtl->SharedRecoveryState = RECOVERY_STATE_CRASH;
<<<<<<< HEAD
	XLogCtl->SharedHotStandbyActive = false;
=======
	XLogCtl->InstallXLogFileSegmentActive = false;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	XLogCtl->WalWriterSleeping = false;


	/*
	 * POLAR: When some processes die, forced entry recovery mode and start the startup process
	 * before start startupXLOG, need reset LocalRecoveryInProgress first
	 * Some processes will set the StartupXLOG flag, reset it in polardb and no-polardb.
	 */
	polar_postmaster_reset_local_variables();

	/* POLAR: Init node type. */
	polar_set_node_type(polar_local_node_type);

	/* POLAR: Init hot_standby_state, same as standbyState */
	XLogCtl->polar_hot_standby_state = STANDBY_DISABLED;

	XLogCtl->polar_available_state = true;

	/* POLAR: Init replica_update_dirs_by_redo flag */
	XLogCtl->polar_replica_update_dirs_by_redo = false;

	/* POLAR: Init initial_datamax_lock */
	polar_lock_tranche = LWLockNewTrancheId();
	LWLockRegisterTranche(polar_lock_tranche, "initial datamax lock");
	LWLockInitialize(&XLogCtl->polar_initial_datamax_lock, polar_lock_tranche);

	SpinLockInit(&XLogCtl->Insert.insertpos_lck);
	SpinLockInit(&XLogCtl->info_lck);
<<<<<<< HEAD
	SpinLockInit(&XLogCtl->ulsn_lck);
	InitSharedLatch(&XLogCtl->recoveryWakeupLatch);

	/* 
	 * POLAR wal pipeline
	 */
	if (POLAR_WAL_PIPELINER_ENABLE())
	{
		int i;
		int ret;
		pthread_mutexattr_t mutex_attr;
		pthread_condattr_t cond_attr; 

		ret = pthread_mutexattr_init(&mutex_attr);
		if (ret != 0)
			elog(ERROR, "pthread mutex attribute init failed, errno is %d", ret);
		ret = pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED); 
		if (ret != 0)
			elog(ERROR, "pthread mutex attribute setpshared failed, errno is %d", ret);   
	
		ret = pthread_condattr_init(&cond_attr);
		if (ret != 0)
			elog(ERROR, "pthread condition attribute init failed, errno is %d", ret);		
		ret = pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
		if (ret != 0)
			elog(ERROR, "pthread condition attribute setpshared failed, errno is %d", ret);
		ret = pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
		if (ret != 0)
			elog(ERROR, "pthread condition attribute setclock failed, errno is %d", ret);

		if (polar_wal_pipeline_wait_object_align)
		{
			if (huge_pages == HUGE_PAGES_ON)
				allocptr = (char *)TYPEALIGN(OS_HUGE_PAGE_SIZE, allocptr);
			else
				allocptr = (char *)TYPEALIGN(OS_DEFAULT_PAGE_SIZE, allocptr);
		}

		/* 
		 * wait object init 
		 */

		XLogCtl->polar_wal_pipeline_wait_objs = (polar_wait_object_t *)allocptr;

		for (i = 0; i < POLAR_WAL_PIPELINE_MAX_THREAD_NUM; i++)
			polar_wait_obj_init(polar_wal_pipeline_get_worker_wait_obj(i), &mutex_attr, &cond_attr);

		if (polar_wal_pipeline_wait_object_align)
		{
			if (huge_pages == HUGE_PAGES_ON)
				allocptr += OS_HUGE_PAGE_SIZE * POLAR_WAL_PIPELINE_MAX_THREAD_NUM;
			else
				allocptr += OS_DEFAULT_PAGE_SIZE * POLAR_WAL_PIPELINE_MAX_THREAD_NUM;
		}
		else
			allocptr += sizeof(polar_wait_object_t) * POLAR_WAL_PIPELINE_MAX_THREAD_NUM;

		/* 
		 * commit wait buffer init
		 */

		XLogCtl->polar_wal_pipeline_commit_wait_buffer.slot_count = polar_wal_pipeline_flush_event_array_size;
		XLogCtl->polar_wal_pipeline_commit_wait_buffer.slot_size = polar_wal_pipeline_flush_event_slot_size;
		XLogCtl->polar_wal_pipeline_commit_wait_buffer.flush_event_slots = (polar_wal_pipeline_flush_event_t *)allocptr;
		
		for (i = 0; i < polar_wal_pipeline_flush_event_array_size; i++)
			polar_wait_obj_init(polar_wal_pipeline_flush_event_get_slot(i), &mutex_attr, &cond_attr);
		
		if (polar_wal_pipeline_wait_object_align)
		{
			if (huge_pages == HUGE_PAGES_ON)
				allocptr += OS_HUGE_PAGE_SIZE * polar_wal_pipeline_flush_event_array_size;
			else
				allocptr += OS_DEFAULT_PAGE_SIZE * polar_wal_pipeline_flush_event_array_size;
		}
		else
			allocptr += (Size) polar_wal_pipeline_flush_event_array_size * sizeof(polar_wal_pipeline_flush_event_t);
		
		/* 
		 * recent written position buffer init
		 */

		XLogCtl->polar_wal_pipeline_recent_written_position_buffer.recent_written_position_slots = (pg_atomic_uint64 *)allocptr;
		pg_atomic_init_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.ready_write_position, 0);
		for (i = 0; i < polar_wal_pipeline_recent_written_array_size; i++)
			pg_atomic_init_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.recent_written_position_slots[i], 0);
		allocptr += (Size) polar_wal_pipeline_recent_written_array_size * sizeof(pg_atomic_uint64);

		/* 
		 * unflushed xlog buffer init 
		 */

		XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.unflushed_xlog_slots = (polar_wal_pipeline_unflushed_xlog_slot_t *)allocptr;
		pg_atomic_init_u64(&XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.add_slot_no, 0);
		pg_atomic_init_u64(&XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.del_slot_no, 0);
		for (i = 0; i < polar_wal_pipeline_unflushed_xlog_array_size; i++)
			XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.unflushed_xlog_slots[i].in_use = false;

		/*
		 * wal pipeline stats init
		 */

		polar_wal_pipeline_stats_init(&XLogCtl->polar_wal_pipeline_stats);
	}

	if (polar_in_standby_mode() && polar_enable_parallel_replay_standby_mode)
		polar_checkpoint_ringbuf_init(&XLogCtl->polar_checkpoint_ringbuf);

	/* POLAR: reset polar I/O fencing state. */
	POLAR_IO_FENCING_RESET_STATE(&XLogCtl->polar_io_fencing);
=======
	pg_atomic_init_u64(&XLogCtl->logInsertResult, InvalidXLogRecPtr);
	pg_atomic_init_u64(&XLogCtl->logWriteResult, InvalidXLogRecPtr);
	pg_atomic_init_u64(&XLogCtl->logFlushResult, InvalidXLogRecPtr);
	pg_atomic_init_u64(&XLogCtl->unloggedLSN, InvalidXLogRecPtr);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * This func must be called ONCE on system install.  It creates pg_control
 * and the initial XLOG segment.
 */
void
BootStrapXLOG(uint32 data_checksum_version)
{
	CheckPoint	checkPoint;
	char	   *buffer;
	XLogPageHeader page;
	XLogLongPageHeader longpage;
	XLogRecord *record;
	char	   *recptr;
	uint64		sysidentifier;
	struct timeval tv;
	pg_crc32c	crc;
	/* POLAR csn */
	TransactionId latestCompletedXid;

	/* allow ordinary WAL segment creation, like StartupXLOG() would */
	SetInstallXLogFileSegmentActive();

	/*
	 * Select a hopefully-unique system identifier code for this installation.
	 * We use the result of gettimeofday(), including the fractional seconds
	 * field, as being about as unique as we can easily get.  (Think not to
	 * use random(), since it hasn't been seeded and there's no portable way
	 * to seed it other than the system clock value...)  The upper half of the
	 * uint64 value is just the tv_sec part, while the lower half contains the
	 * tv_usec part (which must fit in 20 bits), plus 12 bits from our current
	 * PID for a little extra uniqueness.  A person knowing this encoding can
	 * determine the initialization time of the installation, which could
	 * perhaps be useful sometimes.
	 */
	/* 
	 * POLAR: if no sysidentifier specified, initial in origin way 
	 * otherwise use specific system identifier
	 */
	if (!polar_sysidentifier)
	{
		gettimeofday(&tv, NULL);
		sysidentifier = ((uint64) tv.tv_sec) << 32;
		sysidentifier |= ((uint64) tv.tv_usec) << 12;
		sysidentifier |= getpid() & 0xFFF;
	}
	else
		sysidentifier = polar_sysidentifier;
	/* POLAR end */

	/* page buffer must be aligned suitably for O_DIRECT */
	buffer = (char *) palloc(XLOG_BLCKSZ + XLOG_BLCKSZ);
	page = (XLogPageHeader) TYPEALIGN(XLOG_BLCKSZ, buffer);
	memset(page, 0, XLOG_BLCKSZ);

	/*
	 * Set up information for the initial checkpoint record
	 *
	 * The initial checkpoint record is written to the beginning of the WAL
	 * segment with logid=0 logseg=1. The very first WAL segment, 0/0, is not
	 * used, so that we can use 0/0 to mean "before any valid WAL segment".
	 */
	checkPoint.redo = wal_segment_size + SizeOfXLogLongPHD;
	checkPoint.ThisTimeLineID = BootstrapTimeLineID;
	checkPoint.PrevTimeLineID = BootstrapTimeLineID;
	checkPoint.fullPageWrites = fullPageWrites;
	checkPoint.wal_level = wal_level;
	checkPoint.nextXid =
		FullTransactionIdFromEpochAndXid(0, FirstNormalTransactionId);
	checkPoint.nextOid = FirstGenbkiObjectId;
	checkPoint.nextMulti = FirstMultiXactId;
	checkPoint.nextMultiOffset = 0;
	checkPoint.oldestXid = FirstNormalTransactionId;
	checkPoint.oldestXidDB = Template1DbOid;
	checkPoint.oldestMulti = FirstMultiXactId;
	checkPoint.oldestMultiDB = Template1DbOid;
	checkPoint.oldestCommitTsXid = InvalidTransactionId;
	checkPoint.newestCommitTsXid = InvalidTransactionId;
	checkPoint.time = (pg_time_t) time(NULL);
	checkPoint.oldestActiveXid = InvalidTransactionId;

<<<<<<< HEAD
	ShmemVariableCache->nextXid = checkPoint.nextXid;
	ShmemVariableCache->nextOid = checkPoint.nextOid;
	ShmemVariableCache->oidCount = 0;

	/* POLAR csn */
	pg_atomic_write_u64(&polar_shmem_csn_mvcc_var_cache->polar_next_csn, POLAR_CSN_FIRST_NORMAL);
	latestCompletedXid = checkPoint.nextXid;
	TransactionIdRetreat(latestCompletedXid);
	pg_atomic_write_u32(&polar_shmem_csn_mvcc_var_cache->polar_latest_completed_xid, latestCompletedXid);
	pg_atomic_write_u32(&polar_shmem_csn_mvcc_var_cache->polar_oldest_active_xid, checkPoint.nextXid);
	/* POLAR end */

=======
	TransamVariables->nextXid = checkPoint.nextXid;
	TransamVariables->nextOid = checkPoint.nextOid;
	TransamVariables->oidCount = 0;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	MultiXactSetNextMXact(checkPoint.nextMulti, checkPoint.nextMultiOffset);
	AdvanceOldestClogXid(checkPoint.oldestXid);
	SetTransactionIdLimit(checkPoint.oldestXid, checkPoint.oldestXidDB);
	SetMultiXactIdLimit(checkPoint.oldestMulti, checkPoint.oldestMultiDB, true);
	SetCommitTsLimit(InvalidTransactionId, InvalidTransactionId);

	/* Set up the XLOG page header */
	page->xlp_magic = XLOG_PAGE_MAGIC;
	page->xlp_info = XLP_LONG_HEADER;
	page->xlp_tli = BootstrapTimeLineID;
	page->xlp_pageaddr = wal_segment_size;
	longpage = (XLogLongPageHeader) page;
	longpage->xlp_sysid = sysidentifier;
	longpage->xlp_seg_size = wal_segment_size;
	longpage->xlp_xlog_blcksz = XLOG_BLCKSZ;

	/* Insert the initial checkpoint record */
	recptr = ((char *) page + SizeOfXLogLongPHD);
	record = (XLogRecord *) recptr;
	record->xl_prev = 0;
	record->xl_xid = InvalidTransactionId;
	record->xl_tot_len = SizeOfXLogRecord + SizeOfXLogRecordDataHeaderShort + sizeof(checkPoint);
	record->xl_info = XLOG_CHECKPOINT_SHUTDOWN;
	record->xl_rmid = RM_XLOG_ID;
	recptr += SizeOfXLogRecord;
	/* fill the XLogRecordDataHeaderShort struct */
	*(recptr++) = (char) XLR_BLOCK_ID_DATA_SHORT;
	*(recptr++) = sizeof(checkPoint);
	memcpy(recptr, &checkPoint, sizeof(checkPoint));
	recptr += sizeof(checkPoint);
	Assert(recptr - (char *) record == record->xl_tot_len);

	INIT_CRC32C(crc);
	COMP_CRC32C(crc, ((char *) record) + SizeOfXLogRecord, record->xl_tot_len - SizeOfXLogRecord);
	COMP_CRC32C(crc, (char *) record, offsetof(XLogRecord, xl_crc));
	FIN_CRC32C(crc);
	record->xl_crc = crc;

	/* Create first XLOG segment file */
	openLogTLI = BootstrapTimeLineID;
	openLogFile = XLogFileInit(1, BootstrapTimeLineID);

	/*
	 * We needn't bother with Reserve/ReleaseExternalFD here, since we'll
	 * close the file again in a moment.
	 */

	/* Write the first page with the initial record */
	errno = 0;
	pgstat_report_wait_start(WAIT_EVENT_WAL_BOOTSTRAP_WRITE);
	if (polar_write(openLogFile, page, XLOG_BLCKSZ) != XLOG_BLCKSZ)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
			errno = ENOSPC;
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not write bootstrap write-ahead log file: %m")));
	}
	pgstat_report_wait_end();

	pgstat_report_wait_start(WAIT_EVENT_WAL_BOOTSTRAP_SYNC);
	if (polar_fsync(openLogFile) != 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not fsync bootstrap write-ahead log file: %m")));
	pgstat_report_wait_end();

<<<<<<< HEAD
	if (polar_close(openLogFile))
=======
	if (close(openLogFile) != 0)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not close bootstrap write-ahead log file: %m")));

	openLogFile = -1;

	/* Now create pg_control */
	InitControlFile(sysidentifier, data_checksum_version);
	ControlFile->time = checkPoint.time;
	ControlFile->checkPoint = checkPoint.redo;
	ControlFile->checkPointCopy = checkPoint;

	/* some additional ControlFile fields are set in WriteControlFile() */
	WriteControlFile();

	/* Bootstrap the commit log, too */
	BootStrapCLOG();
	BootStrapCommitTs();
	BootStrapSUBTRANS();
	BootStrapMultiXact();
	BootStrapKmgr(bootstrap_data_encryption_cipher);
	/* POLAR csn */
	polar_csnlog_bootstrap();

	pfree(buffer);

	/*
	 * Force control file to be read - in contrast to normal processing we'd
	 * otherwise never run the checks and GUC related initializations therein.
	 */
	ReadControlFile();
}

static char *
str_time(pg_time_t tnow)
{
	char	   *buf = palloc(128);

	pg_strftime(buf, 128,
				"%Y-%m-%d %H:%M:%S %Z",
				pg_localtime(&tnow, log_timezone));

	return buf;
}

/*
 * Initialize the first WAL segment on new timeline.
 */
static void
XLogInitNewTimeline(TimeLineID endTLI, XLogRecPtr endOfLog, TimeLineID newTLI)
{
<<<<<<< HEAD
	FILE	   *fd;
	TimeLineID	rtli = 0;
	bool		rtliGiven = false;
	ConfigVariable *item,
			   *head = NULL,
			   *tail = NULL;
	bool		recoveryTargetActionSet = false;


	fd = AllocateFile(RECOVERY_COMMAND_FILE, "r");
	if (fd == NULL)
	{
		if (errno == ENOENT)
			return;				/* not there, so no archive recovery */
		ereport(FATAL,
				(errcode_for_file_access(),
				 errmsg("could not open recovery command file \"%s\": %m",
						RECOVERY_COMMAND_FILE)));
	}

	/*
	 * Since we're asking ParseConfigFp() to report errors as FATAL, there's
	 * no need to check the return value.
	 */
	(void) ParseConfigFp(fd, RECOVERY_COMMAND_FILE, 0, FATAL, &head, &tail);

	FreeFile(fd);

	for (item = head; item; item = item->next)
	{
		if (strcmp(item->name, "restore_command") == 0)
		{
			recoveryRestoreCommand = pstrdup(item->value);
			ereport(DEBUG2,
					(errmsg_internal("restore_command = '%s'",
									 recoveryRestoreCommand)));
		}
		else if (strcmp(item->name, "recovery_end_command") == 0)
		{
			recoveryEndCommand = pstrdup(item->value);
			ereport(DEBUG2,
					(errmsg_internal("recovery_end_command = '%s'",
									 recoveryEndCommand)));
		}
		else if (strcmp(item->name, "archive_cleanup_command") == 0)
		{
			archiveCleanupCommand = pstrdup(item->value);
			ereport(DEBUG2,
					(errmsg_internal("archive_cleanup_command = '%s'",
									 archiveCleanupCommand)));
		}
		else if (strcmp(item->name, "recovery_target_action") == 0)
		{
			if (strcmp(item->value, "pause") == 0)
				recoveryTargetAction = RECOVERY_TARGET_ACTION_PAUSE;
			else if (strcmp(item->value, "promote") == 0)
				recoveryTargetAction = RECOVERY_TARGET_ACTION_PROMOTE;
			else if (strcmp(item->value, "shutdown") == 0)
				recoveryTargetAction = RECOVERY_TARGET_ACTION_SHUTDOWN;
			else
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid value for recovery parameter \"%s\": \"%s\"",
								"recovery_target_action",
								item->value),
						 errhint("Valid values are \"pause\", \"promote\", and \"shutdown\".")));

			ereport(DEBUG2,
					(errmsg_internal("recovery_target_action = '%s'",
									 item->value)));

			recoveryTargetActionSet = true;
		}
		else if (strcmp(item->name, "recovery_target_timeline") == 0)
		{
			rtliGiven = true;
			if (strcmp(item->value, "latest") == 0)
				rtli = 0;
			else
			{
				errno = 0;
				rtli = (TimeLineID) strtoul(item->value, NULL, 0);
				if (errno == EINVAL || errno == ERANGE)
					ereport(FATAL,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("recovery_target_timeline is not a valid number: \"%s\"",
									item->value)));
			}
			if (rtli)
				ereport(DEBUG2,
						(errmsg_internal("recovery_target_timeline = %u", rtli)));
			else
				ereport(DEBUG2,
						(errmsg_internal("recovery_target_timeline = latest")));
		}
		else if (strcmp(item->name, "recovery_target_xid") == 0)
		{
			errno = 0;
			recoveryTargetXid = (TransactionId) strtoul(item->value, NULL, 0);
			if (errno == EINVAL || errno == ERANGE)
				ereport(FATAL,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("recovery_target_xid is not a valid number: \"%s\"",
								item->value)));
			ereport(DEBUG2,
					(errmsg_internal("recovery_target_xid = %u",
									 recoveryTargetXid)));
			recoveryTarget = RECOVERY_TARGET_XID;
		}
		else if (strcmp(item->name, "recovery_target_time") == 0)
		{
			recoveryTarget = RECOVERY_TARGET_TIME;

			if (strcmp(item->value, "epoch") == 0 ||
				strcmp(item->value, "infinity") == 0 ||
				strcmp(item->value, "-infinity") == 0 ||
				strcmp(item->value, "now") == 0 ||
				strcmp(item->value, "today") == 0 ||
				strcmp(item->value, "tomorrow") == 0 ||
				strcmp(item->value, "yesterday") == 0)
				ereport(FATAL,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("recovery_target_time is not a valid timestamp: \"%s\"",
								item->value)));

			/*
			 * Convert the time string given by the user to TimestampTz form.
			 */
			recoveryTargetTime =
				DatumGetTimestampTz(DirectFunctionCall3(timestamptz_in,
														CStringGetDatum(item->value),
														ObjectIdGetDatum(InvalidOid),
														Int32GetDatum(-1)));
			ereport(DEBUG2,
					(errmsg_internal("recovery_target_time = '%s'",
									 timestamptz_to_str(recoveryTargetTime))));
		}
		else if (strcmp(item->name, "recovery_target_name") == 0)
		{
			recoveryTarget = RECOVERY_TARGET_NAME;

			recoveryTargetName = pstrdup(item->value);
			if (strlen(recoveryTargetName) >= MAXFNAMELEN)
				ereport(FATAL,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("recovery_target_name is too long (maximum %d characters)",
								MAXFNAMELEN - 1)));

			ereport(DEBUG2,
					(errmsg_internal("recovery_target_name = '%s'",
									 recoveryTargetName)));
		}
		else if (strcmp(item->name, "recovery_target_lsn") == 0)
		{
			recoveryTarget = RECOVERY_TARGET_LSN;

			/*
			 * Convert the LSN string given by the user to XLogRecPtr form.
			 */
			recoveryTargetLSN =
				DatumGetLSN(DirectFunctionCall3(pg_lsn_in,
												CStringGetDatum(item->value),
												ObjectIdGetDatum(InvalidOid),
												Int32GetDatum(-1)));
			ereport(DEBUG2,
					(errmsg_internal("recovery_target_lsn = '%X/%X'",
									 (uint32) (recoveryTargetLSN >> 32),
									 (uint32) recoveryTargetLSN)));
		}
		else if (strcmp(item->name, "recovery_target") == 0)
		{
			if (strcmp(item->value, "immediate") == 0)
				recoveryTarget = RECOVERY_TARGET_IMMEDIATE;
			else
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid value for recovery parameter \"%s\": \"%s\"",
								"recovery_target",
								item->value),
						 errhint("The only allowed value is \"immediate\".")));
			ereport(DEBUG2,
					(errmsg_internal("recovery_target = '%s'",
									 item->value)));
		}
		else if (strcmp(item->name, "recovery_target_inclusive") == 0)
		{
			/*
			 * does nothing if a recovery_target is not also set
			 */
			if (!parse_bool(item->value, &recoveryTargetInclusive))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("parameter \"%s\" requires a Boolean value",
								"recovery_target_inclusive")));
			ereport(DEBUG2,
					(errmsg_internal("recovery_target_inclusive = %s",
									 item->value)));
		}
		else if (strcmp(item->name, "standby_mode") == 0)
		{
			if (!POLAR_ENABLE_DMA())
			{
				if (!parse_bool(item->value, &StandbyModeRequested))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("parameter \"%s\" requires a Boolean value",
								 "standby_mode")));
				ereport(DEBUG2,
						(errmsg_internal("standby_mode = '%s'", item->value)));
			}
		}
		else if (strcmp(item->name, "polar_replica") == 0)
		{
			bool	polar_replica = false;

			if (!parse_bool(item->value, &polar_replica))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("parameter \"%s\" requires a Boolean value",
								"polar_replica")));
			ereport(LOG,
					(errmsg_internal("replica_mode = '%s'", item->value)));

			/* POLAR: replica mode base on standby mode, just can not write shared storage */
			if (polar_replica && !POLAR_ENABLE_DMA())
				StandbyModeRequested = true;
		}
		else if (strcmp(item->name, "primary_conninfo") == 0)
		{
			if (!POLAR_ENABLE_DMA())
			{
				PrimaryConnInfo = pstrdup(item->value);
				ereport(DEBUG2,
						(errmsg_internal("primary_conninfo = '%s'",
										 PrimaryConnInfo)));
			}
		}
		else if (strcmp(item->name, "primary_slot_name") == 0)
		{
			if (!POLAR_ENABLE_DMA())
			{
				ReplicationSlotValidateName(item->value, ERROR);
				PrimarySlotName = pstrdup(item->value);
				ereport(DEBUG2,
						(errmsg_internal("primary_slot_name = '%s'",
														 PrimarySlotName)));
			}
		}
		else if (strcmp(item->name, "trigger_file") == 0)
		{
			if (!POLAR_ENABLE_DMA())
			{
				TriggerFile = pstrdup(item->value);
				ereport(DEBUG2,
						(errmsg_internal("trigger_file = '%s'",
														 TriggerFile)));
			}
		}
		else if (strcmp(item->name, "recovery_min_apply_delay") == 0)
		{
			const char *hintmsg;

			if (!parse_int(item->value, &recovery_min_apply_delay, GUC_UNIT_MS,
						   &hintmsg))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("parameter \"%s\" requires a temporal value",
								"recovery_min_apply_delay"),
						 hintmsg ? errhint("%s", _(hintmsg)) : 0));
			ereport(DEBUG2,
					(errmsg_internal("recovery_min_apply_delay = '%s'", item->value)));
		}
		/* POLAR: datamax related config item */
		else if (strcmp(item->name, "polar_datamax_mode") == 0)
		{
			if (strcmp(item->value, "standalone") == 0)
					polar_datamax_mode_requested = true;
			else
					ereport(FATAL, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
									errmsg("parameter \"%s\" has unknown value: %s",
											"polar_datamax_mode", item->value)));
		}
		/* POLAR end */
		else
			ereport(FATAL,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("unrecognized recovery parameter \"%s\"",
							item->name)));
	}

	/*
	 * Check for compulsory parameters
	 */
	/* POLAR: DataMax mode need conninfo, too */
	if (StandbyModeRequested || polar_datamax_mode_requested)
	{
		if (!POLAR_ENABLE_DMA() && PrimaryConnInfo == NULL && recoveryRestoreCommand == NULL)
			ereport(WARNING,
					(errmsg("recovery command file \"%s\" specified neither primary_conninfo nor restore_command",
							RECOVERY_COMMAND_FILE),
					 errhint("The database server will regularly poll the pg_wal subdirectory to check for files placed there.")));
	}
	else
	{
		if (!POLAR_ENABLE_DMA() && recoveryRestoreCommand == NULL)
			ereport(FATAL,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("recovery command file \"%s\" must specify restore_command when standby mode is not enabled",
							RECOVERY_COMMAND_FILE)));
	}

	/*
	 * Override any inconsistent requests. Not that this is a change of
	 * behaviour in 9.5; prior to this we simply ignored a request to pause if
	 * hot_standby = off, which was surprising behaviour.
	 */
	if (recoveryTargetAction == RECOVERY_TARGET_ACTION_PAUSE &&
		recoveryTargetActionSet &&
		!EnableHotStandby)
		recoveryTargetAction = RECOVERY_TARGET_ACTION_SHUTDOWN;

	/*
	 * We don't support standby_mode in standalone backends; that requires
	 * other processes such as the WAL receiver to be alive.
	 */
	if (StandbyModeRequested && !IsUnderPostmaster)
		ereport(FATAL,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("standby mode is not supported by single-user servers")));

	/* Enable fetching from archive recovery area */
	ArchiveRecoveryRequested = true;

	/*
	 * If user specified recovery_target_timeline, validate it or compute the
	 * "latest" value.  We can't do this until after we've gotten the restore
	 * command and set InArchiveRecovery, because we need to fetch timeline
	 * history files from the archive.
	 */
	if (rtliGiven)
	{
		if (rtli)
		{
			/* Timeline 1 does not have a history file, all else should */
			if (rtli != 1 && !existsTimeLineHistory(rtli))
				ereport(FATAL,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("recovery target timeline %u does not exist",
								rtli)));
			recoveryTargetTLI = rtli;
			recoveryTargetIsLatest = false;
		}
		else
		{
			/* We start the "latest" search from pg_control's timeline */
			recoveryTargetTLI = findNewestTimeLine(recoveryTargetTLI);
			recoveryTargetIsLatest = true;
		}
	}
	else if (polar_is_dma_data_node())
	{
		recoveryTargetTLI = findNewestTimeLine(recoveryTargetTLI);
		consensusRecoveryLatestTimeline = true;
	}

	FreeConfigVariables(head);
}

/*
 * Exit archive-recovery state
 */
static void
exitArchiveRecovery(TimeLineID endTLI, XLogRecPtr endOfLog)
{
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	char		xlogfname[MAXFNAMELEN];
	XLogSegNo	endLogSegNo;
	XLogSegNo	startLogSegNo;

	/* we always switch to a new timeline after archive recovery */
	Assert(endTLI != newTLI);

	/*
	 * Update min recovery point one last time.
	 */
	UpdateMinRecoveryPoint(InvalidXLogRecPtr, true);

	/*
<<<<<<< HEAD
	 * If the ending log segment is still open, close it (to avoid problems on
	 * Windows with trying to rename or delete an open file).
	 */
	if (readFile >= 0)
	{
		polar_close(readFile);
		readFile = -1;
	}

	/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 * Calculate the last segment on the old timeline, and the first segment
	 * on the new timeline. If the switch happens in the middle of a segment,
	 * they are the same, but if the switch happens exactly at a segment
	 * boundary, startLogSegNo will be endLogSegNo + 1.
	 */
	XLByteToPrevSeg(endOfLog, endLogSegNo, wal_segment_size);
	XLByteToSeg(endOfLog, startLogSegNo, wal_segment_size);

	/*
	 * Initialize the starting WAL segment for the new timeline. If the switch
	 * happens in the middle of a segment, copy data from the last WAL segment
	 * of the old timeline up to the switch point, to the starting WAL segment
	 * on the new timeline.
	 */
	if (endLogSegNo == startLogSegNo)
	{
		/*
		 * Make a copy of the file on the new timeline.
		 *
		 * Writing WAL isn't allowed yet, so there are no locking
		 * considerations. But we should be just as tense as XLogFileInit to
		 * avoid emplacing a bogus file.
		 */
		XLogFileCopy(newTLI, endLogSegNo, endTLI, endLogSegNo,
					 XLogSegmentOffset(endOfLog, wal_segment_size));
	}
	else
	{
		/*
		 * The switch happened at a segment boundary, so just create the next
		 * segment on the new timeline.
		 */
		int			fd;

		fd = XLogFileInit(startLogSegNo, newTLI);

		if (close(fd) != 0)
		{
			int			save_errno = errno;

			XLogFileName(xlogfname, newTLI, startLogSegNo, wal_segment_size);
			errno = save_errno;
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not close file \"%s\": %m", xlogfname)));
		}
	}

	/*
	 * Let's just make real sure there are not .ready or .done flags posted
	 * for the new segment.
	 */
	XLogFileName(xlogfname, newTLI, startLogSegNo, wal_segment_size);
	XLogArchiveCleanup(xlogfname);
<<<<<<< HEAD

	/*
	 * Rename the config file out of the way, so that we don't accidentally
	 * re-enter archive recovery mode in a subsequent crash.
	 */
	unlink(RECOVERY_COMMAND_DONE);
	durable_rename(RECOVERY_COMMAND_FILE, RECOVERY_COMMAND_DONE, INFO, false);

	ereport(LOG,
			(errmsg("archive recovery complete")));
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Perform cleanup actions at the conclusion of archive recovery.
 */
static void
CleanupAfterArchiveRecovery(TimeLineID EndOfLogTLI, XLogRecPtr EndOfLog,
							TimeLineID newTLI)
{
	/*
	 * Execute the recovery_end_command, if any.
	 */
	if (recoveryEndCommand && strcmp(recoveryEndCommand, "") != 0)
		ExecuteRecoveryCommand(recoveryEndCommand,
							   "recovery_end_command",
							   true,
							   WAIT_EVENT_RECOVERY_END_COMMAND);

	/*
	 * We switched to a new timeline. Clean up segments on the old timeline.
	 *
	 * If there are any higher-numbered segments on the old timeline, remove
	 * them. They might contain valid WAL, but they might also be
	 * pre-allocated files containing garbage. In any case, they are not part
	 * of the new timeline's history so we don't need them.
	 */
	RemoveNonParentXlogFiles(EndOfLog, newTLI);

	/*
	 * If the switch happened in the middle of a segment, what to do with the
	 * last, partial segment on the old timeline? If we don't archive it, and
	 * the server that created the WAL never archives it either (e.g. because
	 * it was hit by a meteor), it will never make it to the archive. That's
	 * OK from our point of view, because the new segment that we created with
	 * the new TLI contains all the WAL from the old timeline up to the switch
	 * point. But if you later try to do PITR to the "missing" WAL on the old
	 * timeline, recovery won't find it in the archive. It's physically
	 * present in the new file with new TLI, but recovery won't look there
	 * when it's recovering to the older timeline. On the other hand, if we
	 * archive the partial segment, and the original server on that timeline
	 * is still running and archives the completed version of the same segment
	 * later, it will fail. (We used to do that in 9.4 and below, and it
	 * caused such problems).
	 *
	 * As a compromise, we rename the last segment with the .partial suffix,
	 * and archive it. Archive recovery will never try to read .partial
	 * segments, so they will normally go unused. But in the odd PITR case,
	 * the administrator can copy them manually to the pg_wal directory
	 * (removing the suffix). They can be useful in debugging, too.
	 *
	 * If a .done or .ready file already exists for the old timeline, however,
	 * we had already determined that the segment is complete, so we can let
	 * it be archived normally. (In particular, if it was restored from the
	 * archive to begin with, it's expected to have a .done file).
	 */
	if (XLogSegmentOffset(EndOfLog, wal_segment_size) != 0 &&
		XLogArchivingActive())
	{
		char		origfname[MAXFNAMELEN];
		XLogSegNo	endLogSegNo;

		XLByteToPrevSeg(EndOfLog, endLogSegNo, wal_segment_size);
		XLogFileName(origfname, EndOfLogTLI, endLogSegNo, wal_segment_size);

<<<<<<< HEAD
		if (polar_is_dma_data_node())
		{
			if (!becameLeader)
				break;
		}
		else if (CheckForStandbyTrigger())
			break;
=======
		if (!XLogArchiveIsReadyOrDone(origfname))
		{
			char		origpath[MAXPGPATH];
			char		partialfname[MAXFNAMELEN];
			char		partialpath[MAXPGPATH];
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

			/*
			 * If we're summarizing WAL, we can't rename the partial file
			 * until the summarizer finishes with it, else it will fail.
			 */
			if (summarize_wal)
				WaitForWalSummarization(EndOfLog);

			XLogFilePath(origpath, EndOfLogTLI, endLogSegNo, wal_segment_size);
			snprintf(partialfname, MAXFNAMELEN, "%s.partial", origfname);
			snprintf(partialpath, MAXPGPATH, "%s.partial", origpath);

			/*
			 * Make sure there's no .done or .ready file for the .partial
			 * file.
			 */
			XLogArchiveCleanup(partialfname);

			durable_rename(origpath, partialpath, ERROR);
			XLogArchiveNotify(partialfname);
		}
	}
}

/*
 * Check to see if required parameters are set high enough on this server
 * for various aspects of recovery operation.
 *
 * Note that all the parameters which this function tests need to be
 * listed in Administrator's Overview section in high-availability.sgml.
 * If you change them, don't forget to update the list.
 */
static void
CheckRequiredParameterValues(void)
{
	/*
	 * For archive recovery, the WAL must be generated with at least 'replica'
	 * wal_level.
	 */
	if (ArchiveRecoveryRequested && ControlFile->wal_level == WAL_LEVEL_MINIMAL)
	{
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("WAL was generated with \"wal_level=minimal\", cannot continue recovering"),
				 errdetail("This happens if you temporarily set \"wal_level=minimal\" on the server."),
				 errhint("Use a backup taken after setting \"wal_level\" to higher than \"minimal\".")));
	}

	/*
	 * For Hot Standby, the WAL must be generated with 'replica' mode, and we
	 * must have at least as many backend slots as the primary.
	 */
	if (ArchiveRecoveryRequested && EnableHotStandby)
	{
		/* We ignore autovacuum_max_workers when we make this test. */
		RecoveryRequiresIntParameter("max_connections",
									 MaxConnections,
									 ControlFile->MaxConnections);
		RecoveryRequiresIntParameter("max_worker_processes",
									 max_worker_processes,
									 ControlFile->max_worker_processes);
		RecoveryRequiresIntParameter("max_wal_senders",
									 max_wal_senders,
									 ControlFile->max_wal_senders);
		RecoveryRequiresIntParameter("max_prepared_transactions",
									 max_prepared_xacts,
									 ControlFile->max_prepared_xacts);
		RecoveryRequiresIntParameter("max_locks_per_transaction",
									 max_locks_per_xact,
									 ControlFile->max_locks_per_xact);
	}
}

/*
 * This must be called ONCE during postmaster or standalone-backend startup
 */
void
StartupXLOG(void)
{
	XLogCtlInsert *Insert;
	CheckPoint	checkPoint;
	bool		wasShutdown;
	bool		didCrash;
	bool		haveTblspcMap;
	bool		haveBackupLabel;
	XLogRecPtr	EndOfLog;
	TimeLineID	EndOfLogTLI;
	TimeLineID	newTLI;
	bool		performedWalRecovery;
	EndOfWalRecoveryInfo *endOfRecoveryInfo;
	XLogRecPtr	abortedRecPtr;
	XLogRecPtr	missingContrecPtr;
	TransactionId oldestActiveXID;
<<<<<<< HEAD
	bool		backupEndRequired = false;
	bool		backupFromStandby = false;
	DBState		dbstate_at_startup;
	XLogReaderState *xlogreader;
	XLogPageReadPrivate private;
	bool		fast_promoted = false;
	struct stat st;
	XLogRecPtr  logindex_mini_trans_lsn,
				redo_start_lsn,
				xlog_read_from;
	bool 		enable_logindex_online_promote = POLAR_LOGINDEX_ENABLE_ONLINE_PROMOTE();
	bool 		enable_logindex_online_promote_standby = POLAR_ENABLE_PARALLEL_REPLAY_STANDBY_MODE();
	/* POLAR csn */
	TransactionId latestCompletedXid;

	/* POLAR */
	XLogRecPtr	EndOfCheckPoint;
	bool		polar_lazy_end_of_recovery_checkpoint = false;
=======
	bool		promoted = false;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * We should have an aux process resource owner to use, and we should not
	 * be in a transaction that's installed some other resowner.
	 */
	Assert(AuxProcessResourceOwner != NULL);
	Assert(CurrentResourceOwner == NULL ||
		   CurrentResourceOwner == AuxProcessResourceOwner);
	CurrentResourceOwner = AuxProcessResourceOwner;

	/*
	 * Check that contents look valid.
	 */
	if (!XRecOffIsValid(ControlFile->checkPoint))
		ereport(FATAL,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("control file contains invalid checkpoint location")));

	switch (ControlFile->state)
	{
		case DB_SHUTDOWNED:

			/*
			 * This is the expected case, so don't be chatty in standalone
			 * mode
			 */
			ereport(IsPostmasterEnvironment ? LOG : NOTICE,
					(errmsg("database system was shut down at %s",
							str_time(ControlFile->time))));
			break;

		case DB_SHUTDOWNED_IN_RECOVERY:
			ereport(LOG,
					(errmsg("database system was shut down in recovery at %s",
							str_time(ControlFile->time))));
			break;

		case DB_SHUTDOWNING:
			ereport(LOG,
					(errmsg("database system shutdown was interrupted; last known up at %s",
							str_time(ControlFile->time))));
			break;

		case DB_IN_CRASH_RECOVERY:
			ereport(LOG,
					(errmsg("database system was interrupted while in recovery at %s",
							str_time(ControlFile->time)),
					 errhint("This probably means that some data is corrupted and"
							 " you will have to use the last backup for recovery.")));
			break;

		case DB_IN_ARCHIVE_RECOVERY:
			ereport(LOG,
					(errmsg("database system was interrupted while in recovery at log time %s",
							str_time(ControlFile->checkPointCopy.time)),
					 errhint("If this has occurred more than once some data might be corrupted"
							 " and you might need to choose an earlier recovery target.")));
			break;

		case DB_IN_PRODUCTION:
			ereport(LOG,
					(errmsg("database system was interrupted; last known up at %s",
							str_time(ControlFile->time))));
			break;

		default:
			ereport(FATAL,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("control file contains invalid database cluster state")));
	}

	/* This is just to allow attaching to startup process with a debugger */
#ifdef XLOG_REPLAY_DELAY
	if (ControlFile->state != DB_SHUTDOWNED)
		pg_usleep(60000000L);
#endif

	/*
	 * Verify that pg_wal, pg_wal/archive_status, and pg_wal/summaries exist.
	 * In cases where someone has performed a copy for PITR, these directories
	 * may have been excluded and need to be re-created.
	 */
	ValidateXLOGDirectoryStructure();

<<<<<<< HEAD
	polar_csnlog_validate_dir();
	if (!polar_csn_enable)
		polar_csnlog_remove_all();

	/*
	 * If we previously crashed, there might be data which we had written,
	 * intending to fsync it, but which we had not actually fsync'd yet.
	 * Therefore, a power failure in the near future might cause earlier
	 * unflushed writes to be lost, even though more recent data written to
	 * disk from here on would be persisted.  To avoid that, fsync the entire
	 * data directory.
=======
	/* Set up timeout handler needed to report startup progress. */
	if (!IsBootstrapProcessingMode())
		RegisterTimeout(STARTUP_PROGRESS_TIMEOUT,
						startup_progress_timeout_handler);

	/*----------
	 * If we previously crashed, perform a couple of actions:
	 *
	 * - The pg_wal directory may still include some temporary WAL segments
	 *   used when creating a new segment, so perform some clean up to not
	 *   bloat this path.  This is done first as there is no point to sync
	 *   this temporary data.
	 *
	 * - There might be data which we had written, intending to fsync it, but
	 *   which we had not actually fsync'd yet.  Therefore, a power failure in
	 *   the near future might cause earlier unflushed writes to be lost, even
	 *   though more recent data written to disk from here on would be
	 *   persisted.  To avoid that, fsync the entire data directory.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 */
	if (ControlFile->state != DB_SHUTDOWNED &&
		ControlFile->state != DB_SHUTDOWNED_IN_RECOVERY)
	{
		RemoveTempXlogFiles();
		SyncDataDirectory();
<<<<<<< HEAD

	/* 
	 * POLAR: set minRecoveryPoint of Controlfile as invalid 
	 * when there is no need for replica to copy files from shared storage
	 * so that replica node can reach consistency after having replayed all wal of primary
	 */
	if (polar_in_replica_mode() && polar_replica_update_dirs_by_redo())
		polar_update_controlfile_minrecoverypoint(InvalidXLogRecPtr, 0);

	/*
	 * Initialize on the assumption we want to recover to the latest timeline
	 * that's active according to pg_control.
	 */
	if (ControlFile->minRecoveryPointTLI >
		ControlFile->checkPointCopy.ThisTimeLineID)
		recoveryTargetTLI = ControlFile->minRecoveryPointTLI;
	else
		recoveryTargetTLI = ControlFile->checkPointCopy.ThisTimeLineID;

	/*
	 * Check for recovery control file, and if so set up state for offline
	 * recovery
	 */
	readRecoveryCommandFile();

	if (polar_is_dma_data_node())
	{
		StandbyModeRequested = true;
		if (!ArchiveRecoveryRequested)
		{
			ArchiveRecoveryRequested = true;
			recoveryTargetTLI = findNewestTimeLine(recoveryTargetTLI);
			consensusRecoveryLatestTimeline = true;
		}
	}

	/*
	 * Save archive_cleanup_command in shared memory so that other processes
	 * can see it.
	 */
	strlcpy(XLogCtl->archiveCleanupCommand,
			archiveCleanupCommand ? archiveCleanupCommand : "",
			sizeof(XLogCtl->archiveCleanupCommand));

	if (ArchiveRecoveryRequested)
	{
		if (StandbyModeRequested)
			ereport(LOG,
					(errmsg("entering standby mode")));
		else if (recoveryTarget == RECOVERY_TARGET_XID)
			ereport(LOG,
					(errmsg("starting point-in-time recovery to XID %u",
							recoveryTargetXid)));
		else if (recoveryTarget == RECOVERY_TARGET_TIME)
			ereport(LOG,
					(errmsg("starting point-in-time recovery to %s",
							timestamptz_to_str(recoveryTargetTime))));
		else if (recoveryTarget == RECOVERY_TARGET_NAME)
			ereport(LOG,
					(errmsg("starting point-in-time recovery to \"%s\"",
							recoveryTargetName)));
		else if (recoveryTarget == RECOVERY_TARGET_LSN)
			ereport(LOG,
					(errmsg("starting point-in-time recovery to WAL location (LSN) \"%X/%X\"",
							(uint32) (recoveryTargetLSN >> 32),
							(uint32) recoveryTargetLSN)));
		else if (recoveryTarget == RECOVERY_TARGET_IMMEDIATE)
			ereport(LOG,
					(errmsg("starting point-in-time recovery to earliest consistent point")));
		else
			ereport(LOG,
					(errmsg("starting archive recovery")));
	}

	/*
	 * Take ownership of the wakeup latch if we're going to sleep during
	 * recovery.
	 */
	if (ArchiveRecoveryRequested)
		OwnLatch(&XLogCtl->recoveryWakeupLatch);

	/* Set up XLOG reader facility */
	MemSet(&private, 0, sizeof(XLogPageReadPrivate));
	xlogreader = XLogReaderAllocate(wal_segment_size, &XLogPageRead, &private);
	if (!xlogreader)
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory"),
				 errdetail("Failed while allocating a WAL reading processor.")));
	xlogreader->system_identifier = ControlFile->system_identifier;

	/*
	 * Allocate two page buffers dedicated to WAL consistency checks.  We do
	 * it this way, rather than just making static arrays, for two reasons:
	 * (1) no need to waste the storage in most instantiations of the backend;
	 * (2) a static char array isn't guaranteed to have any particular
	 * alignment, whereas palloc() will provide MAXALIGN'd storage.
	 */
	replay_image_masked = (char *) palloc(BLCKSZ);
	master_image_masked = (char *) palloc(BLCKSZ);

	if (read_backup_label(&checkPointLoc, &backupEndRequired,
						  &backupFromStandby))
	{
		List	   *tablespaces = NIL;

		/*
		 * Archive recovery was requested, and thanks to the backup label
		 * file, we know how far we need to replay to reach consistency. Enter
		 * archive recovery directly.
		 */
		InArchiveRecovery = true;
		if (StandbyModeRequested)
			StandbyMode = true;

		/*
		 * When a backup_label file is present, we want to roll forward from
		 * the checkpoint it identifies, rather than using pg_control.
		 */
		record = ReadCheckpointRecord(xlogreader, checkPointLoc, 0, true);
		if (record != NULL)
		{
			memcpy(&checkPoint, XLogRecGetData(xlogreader), sizeof(CheckPoint));
			wasShutdown = ((record->xl_info & ~XLR_INFO_MASK) == XLOG_CHECKPOINT_SHUTDOWN);
			ereport(DEBUG1,
					(errmsg("checkpoint record is at %X/%X",
							(uint32) (checkPointLoc >> 32), (uint32) checkPointLoc)));
			InRecovery = true;	/* force recovery even if SHUTDOWNED */

			/* POLAR DMA */
			EndOfCheckPoint = EndRecPtr;

			/*
			 * Make sure that REDO location exists. This may not be the case
			 * if there was a crash during an online backup, which left a
			 * backup_label around that references a WAL segment that's
			 * already been archived.
			 */
			if (checkPoint.redo < checkPointLoc)
			{
				if (!ReadRecord(xlogreader, checkPoint.redo, LOG, false))
					ereport(FATAL,
							(errmsg("could not find redo location referenced by checkpoint record"),
							 errhint("If you are not restoring from a backup, try removing the file \"%s/backup_label\".", DataDir)));
			}
		}
		else
		{
			ereport(FATAL,
					(errmsg("could not locate required checkpoint record"),
					 errhint("If you are not restoring from a backup, try removing the file \"%s/backup_label\".", DataDir)));
			wasShutdown = false;	/* keep compiler quiet */
		}

		/* read the tablespace_map file if present and create symlinks. */
		if (read_tablespace_map(&tablespaces))
		{
			ListCell   *lc;

			foreach(lc, tablespaces)
			{
				tablespaceinfo *ti = lfirst(lc);
				char	   *linkloc;

				linkloc = psprintf("pg_tblspc/%s", ti->oid);

				/*
				 * Remove the existing symlink if any and Create the symlink
				 * under PGDATA.
				 */
				remove_tablespace_symlink(linkloc);

				if (symlink(ti->path, linkloc) < 0)
					ereport(ERROR,
							(errcode_for_file_access(),
							 errmsg("could not create symbolic link \"%s\": %m",
									linkloc)));

				pfree(ti->oid);
				pfree(ti->path);
				pfree(ti);
			}

			/* set flag to delete it later */
			haveTblspcMap = true;
		}

		/* set flag to delete it later */
		haveBackupLabel = true;
	}
	else
	{
		/*
		 * If tablespace_map file is present without backup_label file, there
		 * is no use of such file.  There is no harm in retaining it, but it
		 * is better to get rid of the map file so that we don't have any
		 * redundant file in data directory and it will avoid any sort of
		 * confusion.  It seems prudent though to just rename the file out of
		 * the way rather than delete it completely, also we ignore any error
		 * that occurs in rename operation as even if map file is present
		 * without backup_label file, it is harmless.
		 */
		if (stat(TABLESPACE_MAP, &st) == 0)
		{
			unlink(TABLESPACE_MAP_OLD);
			if (durable_rename(TABLESPACE_MAP, TABLESPACE_MAP_OLD, DEBUG1, false) == 0)
				ereport(LOG,
						(errmsg("ignoring file \"%s\" because no file \"%s\" exists",
								TABLESPACE_MAP, BACKUP_LABEL_FILE),
						 errdetail("File \"%s\" was renamed to \"%s\".",
								   TABLESPACE_MAP, TABLESPACE_MAP_OLD)));
			else
				ereport(LOG,
						(errmsg("ignoring file \"%s\" because no file \"%s\" exists",
								TABLESPACE_MAP, BACKUP_LABEL_FILE),
						 errdetail("Could not rename file \"%s\" to \"%s\": %m.",
								   TABLESPACE_MAP, TABLESPACE_MAP_OLD)));
		}
=======
		didCrash = true;
	}
	else
		didCrash = false;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Prepare for WAL recovery if needed.
	 *
	 * InitWalRecovery analyzes the control file and the backup label file, if
	 * any.  It updates the in-memory ControlFile buffer according to the
	 * starting checkpoint, and sets InRecovery and ArchiveRecoveryRequested.
	 * It also applies the tablespace map file, if any.
	 */
	InitWalRecovery(ControlFile, &wasShutdown,
					&haveBackupLabel, &haveTblspcMap);
	checkPoint = ControlFile->checkPointCopy;

<<<<<<< HEAD
		/* Get the last valid checkpoint record. */
		checkPointLoc = ControlFile->checkPoint;
		RedoStartLSN = ControlFile->checkPointCopy.redo;
		record = ReadCheckpointRecord(xlogreader, checkPointLoc, 1, true);
		if (record != NULL)
		{
			ereport(DEBUG1,
					(errmsg("checkpoint record is at %X/%X",
							(uint32) (checkPointLoc >> 32), (uint32) checkPointLoc)));
		}
		else
		{
			/*
			 * We used to attempt to go back to a secondary checkpoint record
			 * here, but only when not in standby_mode. We now just fail if we
			 * can't read the last checkpoint because this allows us to
			 * simplify processing around checkpoints.
			 */
			ereport(PANIC,
					(errmsg("could not locate a valid checkpoint record")));
		}

		/*
		 * POLAR: God bless us will never set polar_force_change_checkpoint to be true.
		 * During development I found a bug that page whose LSN is smaller than checkPoint.redo
		 * is not flushed to the storage, and it crashed during recovery.
		 * In order to recovery from this case, we need to use polar_tools to change pg_control file
		 * and set a new redo position in this file.
		 */
		if (polar_force_change_checkpoint)
			checkPoint = ControlFile->checkPointCopy;
		else
			memcpy(&checkPoint, XLogRecGetData(xlogreader), sizeof(CheckPoint));
		wasShutdown = ((record->xl_info & ~XLR_INFO_MASK) == XLOG_CHECKPOINT_SHUTDOWN);
		/* POLAR DMA */
		EndOfCheckPoint = EndRecPtr;
	}
=======
	/* initialize shared memory variables from the checkpoint record */
	TransamVariables->nextXid = checkPoint.nextXid;
	TransamVariables->nextOid = checkPoint.nextOid;
	TransamVariables->oidCount = 0;
	MultiXactSetNextMXact(checkPoint.nextMulti, checkPoint.nextMultiOffset);
	AdvanceOldestClogXid(checkPoint.oldestXid);
	SetTransactionIdLimit(checkPoint.oldestXid, checkPoint.oldestXidDB);
	SetMultiXactIdLimit(checkPoint.oldestMulti, checkPoint.oldestMultiDB, true);
	SetCommitTsLimit(checkPoint.oldestCommitTsXid,
					 checkPoint.newestCommitTsXid);
	XLogCtl->ckptFullXid = checkPoint.nextXid;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	redo_start_lsn = checkPoint.redo;

	/*
	 * Clear out any old relcache cache files.  This is *necessary* if we do
	 * any WAL replay, since that would probably result in the cache files
	 * being out of sync with database reality.  In theory we could leave them
	 * in place if the database had been cleanly shut down, but it seems
	 * safest to just remove them always and let them be rebuilt during the
	 * first backend startup.  These files needs to be removed from all
	 * directories including pg_tblspc, however the symlinks are created only
	 * after reading tablespace_map file in case of archive recovery from
	 * backup, so needs to clear old relcache files here after creating
	 * symlinks.
	 */
	RelationCacheInitFileRemove();

	/*
<<<<<<< HEAD
	 * If the location of the checkpoint record is not on the expected
	 * timeline in the history of the requested timeline, we cannot proceed:
	 * the backup is not part of the history of the requested timeline.
	 */
	Assert(expectedTLEs);		/* was initialized by reading checkpoint
								 * record */
	if (tliOfPointInHistory(checkPointLoc, expectedTLEs) !=
		checkPoint.ThisTimeLineID)
	{
		XLogRecPtr	switchpoint;

		/*
		 * tliSwitchPoint will throw an error if the checkpoint's timeline is
		 * not in expectedTLEs at all.
		 */
		switchpoint = tliSwitchPoint(ControlFile->checkPointCopy.ThisTimeLineID, expectedTLEs, NULL);
		ereport(FATAL,
				(errmsg("requested timeline %u is not a child of this server's history",
						recoveryTargetTLI),
				 errdetail("Latest checkpoint is at %X/%X on timeline %u, but in the history of the requested timeline, the server forked off from that timeline at %X/%X.",
						   (uint32) (ControlFile->checkPoint >> 32),
						   (uint32) ControlFile->checkPoint,
						   ControlFile->checkPointCopy.ThisTimeLineID,
						   (uint32) (switchpoint >> 32),
						   (uint32) switchpoint)));
	}

	/*
	 * The min recovery point should be part of the requested timeline's
	 * history, too.
	 */
	if (!XLogRecPtrIsInvalid(ControlFile->minRecoveryPoint) &&
		tliOfPointInHistory(ControlFile->minRecoveryPoint - 1, expectedTLEs) !=
		ControlFile->minRecoveryPointTLI)
		ereport(FATAL,
				(errmsg("requested timeline %u does not contain minimum recovery point %X/%X on timeline %u",
						recoveryTargetTLI,
						(uint32) (ControlFile->minRecoveryPoint >> 32),
						(uint32) ControlFile->minRecoveryPoint,
						ControlFile->minRecoveryPointTLI)));

	LastRec = RecPtr = checkPointLoc;

	if (polar_is_dma_data_node())
	{
		ConsensusSetXLogFlushedLSN(EndOfCheckPoint, checkPoint.ThisTimeLineID, true);
		ConsensusSetSyncedLSN(EndOfCheckPoint, checkPoint.ThisTimeLineID);
	}

	ereport(DEBUG1,
			(errmsg_internal("redo record is at %X/%X; shutdown %s",
							 (uint32) (checkPoint.redo >> 32), (uint32) checkPoint.redo,
							 wasShutdown ? "true" : "false")));
	ereport(DEBUG1,
			(errmsg_internal("next transaction ID: %u:%u; next OID: %u",
							 checkPoint.nextXidEpoch, checkPoint.nextXid,
							 checkPoint.nextOid)));
	ereport(DEBUG1,
			(errmsg_internal("next MultiXactId: %u; next MultiXactOffset: %u",
							 checkPoint.nextMulti, checkPoint.nextMultiOffset)));
	ereport(DEBUG1,
			(errmsg_internal("oldest unfrozen transaction ID: %u, in database %u",
							 checkPoint.oldestXid, checkPoint.oldestXidDB)));
	ereport(DEBUG1,
			(errmsg_internal("oldest MultiXactId: %u, in database %u",
							 checkPoint.oldestMulti, checkPoint.oldestMultiDB)));
	ereport(DEBUG1,
			(errmsg_internal("commit timestamp Xid oldest/newest: %u/%u",
							 checkPoint.oldestCommitTsXid,
							 checkPoint.newestCommitTsXid)));
	if (!TransactionIdIsNormal(checkPoint.nextXid))
		ereport(PANIC,
				(errmsg("invalid next transaction ID")));

	/* initialize shared memory variables from the checkpoint record */
	ShmemVariableCache->nextXid = checkPoint.nextXid;
	ShmemVariableCache->nextOid = checkPoint.nextOid;
	ShmemVariableCache->oidCount = 0;
	MultiXactSetNextMXact(checkPoint.nextMulti, checkPoint.nextMultiOffset);
	AdvanceOldestClogXid(checkPoint.oldestXid);
	SetTransactionIdLimit(checkPoint.oldestXid, checkPoint.oldestXidDB);
	SetMultiXactIdLimit(checkPoint.oldestMulti, checkPoint.oldestMultiDB, true);
	SetCommitTsLimit(checkPoint.oldestCommitTsXid,
					 checkPoint.newestCommitTsXid);
	XLogCtl->ckptXidEpoch = checkPoint.nextXidEpoch;
	XLogCtl->ckptXid = checkPoint.nextXid;

	/* POLAR */
	XLogCtl->replication_slot_oldest_applied_lsn = InvalidXLogRecPtr;

	/* POLAR csn */
	pg_atomic_write_u64(&polar_shmem_csn_mvcc_var_cache->polar_next_csn, POLAR_CSN_FIRST_NORMAL);
	latestCompletedXid = checkPoint.nextXid;
	TransactionIdRetreat(latestCompletedXid);
	pg_atomic_write_u32(&polar_shmem_csn_mvcc_var_cache->polar_latest_completed_xid, latestCompletedXid);
	pg_atomic_write_u32(&polar_shmem_csn_mvcc_var_cache->polar_oldest_active_xid, checkPoint.nextXid);
	/* POLAR end */

	/* Startup all about flashback */
	polar_startup_flashback(&checkPoint);

	/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 * Initialize replication slots, before there's a chance to remove
	 * required resources.
	 */
	StartupReplicationSlots();

	/*
	 * Startup logical state, needs to be setup now so we have proper data
	 * during crash recovery.
	 */
	StartupReorderBuffer();

	/*
	 * Startup CLOG. This must be done after TransamVariables->nextXid has
	 * been initialized and before we accept connections or begin WAL replay.
	 */
	StartupCLOG();

	/*
	 * Startup MultiXact. We need to do this early to be able to replay
	 * truncations.
	 */
	StartupMultiXact();

	/*
	 * Ditto for commit timestamps.  Activate the facility if the setting is
	 * enabled in the control file, as there should be no tracking of commit
	 * timestamps done when the setting was disabled.  This facility can be
	 * started or stopped when replaying a XLOG_PARAMETER_CHANGE record.
	 */
	if (ControlFile->track_commit_timestamp)
		StartupCommitTs();

	/*
	 * Recover knowledge about replay progress of known replication partners.
	 */
	StartupReplicationOrigin();

	/*
	 * Initialize unlogged LSN. On a clean shutdown, it's restored from the
	 * control file. On recovery, all unlogged relations are blown away, so
	 * the unlogged LSN counter can be reset too.
	 */
	if (ControlFile->state == DB_SHUTDOWNED)
		pg_atomic_write_membarrier_u64(&XLogCtl->unloggedLSN,
									   ControlFile->unloggedLSN);
	else
		pg_atomic_write_membarrier_u64(&XLogCtl->unloggedLSN,
									   FirstNormalUnloggedLSN);

	/*
	 * Copy any missing timeline history files between 'now' and the recovery
	 * target timeline from archive to pg_wal. While we don't need those files
	 * ourselves - the history file of the recovery target timeline covers all
	 * the previous timelines in the history too - a cascading standby server
	 * might be interested in them. Or, if you archive the WAL from this
	 * server to a different archive than the primary, it'd be good for all
	 * the history files to get archived there after failover, so that you can
	 * use one of the old timelines as a PITR target. Timeline history files
	 * are small, so it's better to copy them unnecessarily than not copy them
	 * and regret later.
	 */
	restoreTimeLineHistoryFiles(checkPoint.ThisTimeLineID, recoveryTargetTLI);

	/*
	 * Before running in recovery, scan pg_twophase and fill in its status to
	 * be able to work on entries generated by redo.  Doing a scan before
	 * taking any recovery action has the merit to discard any 2PC files that
	 * are newer than the first record to replay, saving from any conflicts at
	 * replay.  This avoids as well any subsequent scans when doing recovery
	 * of the on-disk two-phase data.
	 */
	restoreTwoPhaseData();

	/*
	 * When starting with crash recovery, reset pgstat data - it might not be
	 * valid. Otherwise restore pgstat data. It's safe to do this here,
	 * because postmaster will not yet have started any other processes.
	 *
	 * NB: Restoring replication slot stats relies on slot state to have
	 * already been restored from disk.
	 *
	 * TODO: With a bit of extra work we could just start with a pgstat file
	 * associated with the checkpoint redo location we're starting from.
	 */
	if (didCrash)
		pgstat_discard_stats();
	else
		pgstat_restore_stats(checkPoint.redo);

	lastFullPageWrites = checkPoint.fullPageWrites;

	RedoRecPtr = XLogCtl->RedoRecPtr = XLogCtl->Insert.RedoRecPtr = checkPoint.redo;
	doPageWrites = lastFullPageWrites;

<<<<<<< HEAD
	if (RecPtr < checkPoint.redo)
		ereport(PANIC,
				(errmsg("invalid redo in checkpoint record")));

	/*
	 * Check whether we need to force recovery from WAL.  If it appears to
	 * have been a clean shutdown and we did not have a recovery.conf file,
	 * then assume no recovery needed.
	 */
	if (checkPoint.redo < RecPtr)
	{
		if (wasShutdown)
			ereport(PANIC,
					(errmsg("invalid redo record in shutdown checkpoint")));
		InRecovery = true;
	}
	else if (ControlFile->state != DB_SHUTDOWNED)
		InRecovery = true;
	else if (ArchiveRecoveryRequested)
	{
		/* force recovery due to presence of recovery.conf */
		InRecovery = true;
	}

	/* POLAR: If logindex is disabled or only enalbe logindex in replica, we remove all logindex data if we can write storage */
	if (!polar_logindex_redo_instance && !polar_in_replica_mode())
		polar_logindex_remove_all();

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	/* REDO */
	/* POLAR: force recovery due to enable logindex in master and replica mode */
	if (InRecovery || polar_logindex_redo_instance)
	{
		/* Initialize state for RecoveryInProgress() */
		SpinLockAcquire(&XLogCtl->info_lck);
		if (InArchiveRecovery)
			XLogCtl->SharedRecoveryState = RECOVERY_STATE_ARCHIVE;
		else
			XLogCtl->SharedRecoveryState = RECOVERY_STATE_CRASH;
		SpinLockRelease(&XLogCtl->info_lck);

		/* POLAR: init buffer pool check */
		polar_check_buffer_pool_consistency_init();

		/*
		 * Update pg_control to show that we are recovering and to show the
		 * selected checkpoint as the place we are starting from. We also mark
		 * pg_control with any minimum recovery stop point obtained from a
		 * backup history file.
<<<<<<< HEAD
		 */
		dbstate_at_startup = ControlFile->state;
		if (InArchiveRecovery)
		{
			ControlFile->state = DB_IN_ARCHIVE_RECOVERY;

			SpinLockAcquire(&XLogCtl->info_lck);
			XLogCtl->SharedRecoveryState = RECOVERY_STATE_ARCHIVE;
			SpinLockRelease(&XLogCtl->info_lck);
		}
		else
		{
			ereport(LOG,
					(errmsg("database system was not properly shut down; "
							"automatic recovery in progress")));
			if (recoveryTargetTLI > ControlFile->checkPointCopy.ThisTimeLineID)
				ereport(LOG,
						(errmsg("crash recovery starts in timeline %u "
								"and has target timeline %u",
								ControlFile->checkPointCopy.ThisTimeLineID,
								recoveryTargetTLI)));
			ControlFile->state = DB_IN_CRASH_RECOVERY;

			SpinLockAcquire(&XLogCtl->info_lck);
			XLogCtl->SharedRecoveryState = RECOVERY_STATE_CRASH;
			SpinLockRelease(&XLogCtl->info_lck);
		}
		ControlFile->checkPoint = checkPointLoc;
		ControlFile->checkPointCopy = checkPoint;
		if (InArchiveRecovery)
		{
			/* initialize minRecoveryPoint if not set yet */
			if (ControlFile->minRecoveryPoint < checkPoint.redo)
			{
				ControlFile->minRecoveryPoint = checkPoint.redo;
				ControlFile->minRecoveryPointTLI = checkPoint.ThisTimeLineID;
			}
		}

		/*
		 * Set backupStartPoint if we're starting recovery from a base backup.
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		 *
		 * No need to hold ControlFileLock yet, we aren't up far enough.
		 */
		UpdateControlFile();

<<<<<<< HEAD
		/*
		 * Initialize our local copy of minRecoveryPoint.  When doing crash
		 * recovery we want to replay up to the end of WAL.  Particularly, in
		 * the case of a promoted standby minRecoveryPoint value in the
		 * control file is only updated after the first checkpoint.  However,
		 * if the instance crashes before the first post-recovery checkpoint
		 * is completed then recovery will use a stale location causing the
		 * startup process to think that there are still invalid page
		 * references when checking for data consistency.
		 */
		if (InArchiveRecovery)
		{
			minRecoveryPoint = ControlFile->minRecoveryPoint;
			minRecoveryPointTLI = ControlFile->minRecoveryPointTLI;
		}
		else
		{
			minRecoveryPoint = InvalidXLogRecPtr;
			minRecoveryPointTLI = 0;
		}

		/*
		 * Reset pgstat data, because it may be invalid after recovery.
		 */
		pgstat_reset_all();

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		/*
		 * If there was a backup label file, it's done its job and the info
		 * has now been propagated into pg_control.  We must get rid of the
		 * label file so that if we crash during recovery, we'll pick up at
		 * the latest recovery restartpoint instead of going all the way back
		 * to the backup start point.  It seems prudent though to just rename
		 * the file out of the way rather than delete it completely.
		 */
		if (haveBackupLabel)
		{
			unlink(BACKUP_LABEL_OLD);
			durable_rename(BACKUP_LABEL_FILE, BACKUP_LABEL_OLD, FATAL, false);
		}

		/*
		 * If there was a tablespace_map file, it's done its job and the
		 * symlinks have been created.  We must get rid of the map file so
		 * that if we crash during recovery, we don't create symlinks again.
		 * It seems prudent though to just rename the file out of the way
		 * rather than delete it completely.
		 */
		if (haveTblspcMap)
		{
			unlink(TABLESPACE_MAP_OLD);
			durable_rename(TABLESPACE_MAP, TABLESPACE_MAP_OLD, FATAL, false);
		}

		/*
		 * Initialize our local copy of minRecoveryPoint.  When doing crash
		 * recovery we want to replay up to the end of WAL.  Particularly, in
		 * the case of a promoted standby minRecoveryPoint value in the
		 * control file is only updated after the first checkpoint.  However,
		 * if the instance crashes before the first post-recovery checkpoint
		 * is completed then recovery will use a stale location causing the
		 * startup process to think that there are still invalid page
		 * references when checking for data consistency.
		 */
		if (InArchiveRecovery)
		{
			LocalMinRecoveryPoint = ControlFile->minRecoveryPoint;
			LocalMinRecoveryPointTLI = ControlFile->minRecoveryPointTLI;
		}
		else
		{
			LocalMinRecoveryPoint = InvalidXLogRecPtr;
			LocalMinRecoveryPointTLI = 0;
		}

		/* Check that the GUCs used to generate the WAL allow recovery */
		CheckRequiredParameterValues();

		/*
		 * We're in recovery, so unlogged relations may be trashed and must be
		 * reset.  This should be done BEFORE allowing Hot Standby
		 * connections, so that read-only backends don't try to read whatever
		 * garbage is left over from before.
		 */
		ResetUnloggedRelations(UNLOGGED_RELATION_CLEANUP);

		/*
		 * Likewise, delete any saved transaction snapshot files that got left
		 * behind by crashed backends.
		 */
		DeleteAllExportedSnapshotFiles();

		/*
		 * Initialize for Hot Standby, if enabled. We won't let backends in
		 * yet, not until we've reached the min recovery point specified in
		 * control file and we've established a recovery snapshot from a
		 * running-xacts WAL record.
		 */
		if (ArchiveRecoveryRequested && EnableHotStandby)
		{
			TransactionId *xids;
			int			nxids;

			ereport(DEBUG1,
					(errmsg_internal("initializing for hot standby")));

			InitRecoveryTransactionEnvironment();

			if (wasShutdown)
				oldestActiveXID = PrescanPreparedTransactions(&xids, &nxids);
			else
				oldestActiveXID = checkPoint.oldestActiveXid;
			Assert(TransactionIdIsValid(oldestActiveXID));

			/* Tell procarray about the range of xids it has to deal with */
<<<<<<< HEAD
			ProcArrayInitRecovery(ShmemVariableCache->nextXid, oldestActiveXID);
=======
			ProcArrayInitRecovery(XidFromFullTransactionId(TransamVariables->nextXid));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

			/*
			 * Startup subtrans only.  CLOG, MultiXact and commit timestamp
			 * have already been started up and other SLRUs are not maintained
			 * during recovery and need not be started yet.
			 */
<<<<<<< HEAD
			StartupCLOG();
			if (polar_csn_enable)
			{
				polar_csnlog_startup(oldestActiveXID);
			}
			else
			{
				StartupSUBTRANS(oldestActiveXID);
			}
=======
			StartupSUBTRANS(oldestActiveXID);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

			/*
			 * If we're beginning at a shutdown checkpoint, we know that
			 * nothing was running on the primary at this point. So fake-up an
			 * empty running-xacts record and use that here and now. Recover
			 * additional standby state for prepared transactions.
			 */
			if (wasShutdown)
			{
				RunningTransactionsData running;
				TransactionId latestCompletedXid;

				/* Update pg_subtrans entries for any prepared transactions */
				StandbyRecoverPreparedTransactions();

				/*
				 * Construct a RunningTransactions snapshot representing a
				 * shut down server, with only prepared transactions still
				 * alive. We're never overflowed at this point because all
				 * subxids are listed with their parent prepared transactions.
				 */
				running.xcnt = nxids;
				running.subxcnt = 0;
				running.subxid_status = SUBXIDS_IN_SUBTRANS;
				running.nextXid = XidFromFullTransactionId(checkPoint.nextXid);
				running.oldestRunningXid = oldestActiveXID;
				latestCompletedXid = XidFromFullTransactionId(checkPoint.nextXid);
				TransactionIdRetreat(latestCompletedXid);
				Assert(TransactionIdIsNormal(latestCompletedXid));
				running.latestCompletedXid = latestCompletedXid;
				running.xids = xids;

				ProcArrayApplyRecoveryInfo(&running);
			}
		}

		/*
		 * We're all set for replaying the WAL now. Do it.
		 */
<<<<<<< HEAD
		SpinLockAcquire(&XLogCtl->info_lck);
		if (checkPoint.redo < RecPtr)
			XLogCtl->replayEndRecPtr = checkPoint.redo;
		else
			XLogCtl->replayEndRecPtr = EndRecPtr;
		XLogCtl->replayEndTLI = ThisTimeLineID;
		XLogCtl->lastReplayedEndRecPtr = XLogCtl->replayEndRecPtr;
		XLogCtl->lockLastReplayedEndRecPtr = XLogCtl->replayEndRecPtr;
		XLogCtl->lastReplayedTLI = XLogCtl->replayEndTLI;
		XLogCtl->recoveryLastXTime = 0;
		XLogCtl->currentChunkStartTime = 0;
		XLogCtl->recoveryPause = false;
		SpinLockRelease(&XLogCtl->info_lck);

		/* Also ensure XLogReceiptTime has a sane value */
		XLogReceiptTime = GetCurrentTimestamp();

		/*
		 * Let postmaster know we've started redo now, so that it can launch
		 * checkpointer to perform restartpoints.  We don't bother during
		 * crash recovery as restartpoints can only be performed during
		 * archive recovery.  And we'd like to keep crash recovery simple, to
		 * avoid introducing bugs that could affect you when recovering after
		 * crash.
		 *
		 * After this point, we can no longer assume that we're the only
		 * process in addition to postmaster!  Also, fsync requests are
		 * subsequently to be handled by the checkpointer, not locally.
		 *
		 * POLAR: we launch checkpointer and bgwriter now, it's different
		 * from the origin behaviour, but we don't perform restartpoints
		 * for simply, we just want bgwriter to flush dirty pages as soon
		 * as possible to avoid last end of crash checkpoint too slow.
		 */
		if ((polar_enable_early_launch_checkpointer || ArchiveRecoveryRequested) &&
			IsUnderPostmaster)
		{
			PublishStartupProcessInformation();
			SetForwardFsyncRequests();
			SendPostmasterSignal(PMSIGNAL_RECOVERY_STARTED);
			bgwriterLaunched = true;
		}

		/*
		 * POLAR: If logindex is enabled in ro and rw node, we may change start point to read xlog,
		 * so we will not check consistency.
		 */
		if (!polar_logindex_redo_instance)
		{
			/*
			 * Allow read-only connections immediately if we're consistent
			 * already.
			 */
			CheckRecoveryConsistency();
		}


		/*
		 * Find the first record that logically follows the checkpoint --- it
		 * might physically precede it, though.
		 */
		if (checkPoint.redo < RecPtr)
		{
			/* POLAR: The valid lsn of log index start from check point */
			redo_start_lsn = checkPoint.redo;

			 /*
			  * POLAR: Init log index snapshot structure.
			  *
			  * Changing the xlog_read_from may lead to changing the TLI we read
			  * from, so we update curFileTLI after change xlog_read_from. 
			  */
			xlog_read_from = polar_logindex_redo_init(polar_logindex_redo_instance, redo_start_lsn, polar_in_replica_mode());
			polar_update_curFileTLI(xlog_read_from);

			/*
			 * POLAR : If start point to read xlog is not changed, then
			 * allow read-only connections immediately if we're consistent already.
			 */
			if (xlog_read_from == redo_start_lsn)
				CheckRecoveryConsistency();

			/* back up to find the record */
			record = ReadRecord(xlogreader, xlog_read_from, PANIC, false);
		}
		else
		{
			/* POLAR: Check whether the redo point is same as checkpoint position */
			if (checkPoint.redo != RecPtr)
				ereport(PANIC,
						(errmsg("invalid redo in checkpoint record")));

			/* POLAR: The valid lsn of log index start from next record after CheckPoint */
			redo_start_lsn = xlogreader->EndRecPtr;

			 /*
			  * POLAR: Init log index snapshot structure.
			  *
			  * Changing the xlog_read_from may lead to changing the TLI we read
			  * from, so we update curFileTLI after change xlog_read_from. 
			  */
			xlog_read_from = polar_logindex_redo_init(polar_logindex_redo_instance, redo_start_lsn, polar_in_replica_mode());
			polar_update_curFileTLI(xlog_read_from);

			/*
			 * POLAR: The old xlog segment file is renamed for use, it will contain old xlog data.
			 * If we pass valid xlog record position to read then it can't exactly verify the prev-link,
			 * which means if we read a record although its crc is correct, we can't verify that its prev-link
			 * is redo point.
			 * So we force to pass InvalidXLogRecPtr to read, it will read from xlogreader->EndRecPtr
			 * and check read record's prev-link is xlogreader->ReadRecPtr.
			 */
			if (xlog_read_from == xlogreader->EndRecPtr)
			{
				/* 
				 * POLAR: If in the situation where shutdown checkpoint is the last record of WAL,
				 * we will be blocked in ReadRecord because no more wal exists. So we need to check 
				 * consistency here to avoid endless record read retry but cannot reach consistent state,
				 * even we match all consistency conditions.
				 */
				CheckRecoveryConsistency();
				xlog_read_from = InvalidXLogRecPtr;
			}

			/* just have to read next record after CheckPoint */
			record = ReadRecord(xlogreader, xlog_read_from, LOG, false);

		}

		if (record != NULL)
		{
			ErrorContextCallback errcallback;
			TimestampTz xtime;

			/* POLAR */
			TimestampTz	last_handle_interrupts_time = GetCurrentTimestamp();

			/* POLAR: Launch async ddl lock replay worker if enabled */
			polar_launch_async_ddl_lock_replay_workers();
			/* POLAR end */

			InRedo = true;

			ereport(LOG,
					(errmsg("redo starts at %X/%X",
							(uint32) (ReadRecPtr >> 32), (uint32) ReadRecPtr)));

			/*
			 * main redo apply loop
			 */
			do
			{
				bool		switchedTLI = false;

				/* POLAR */
				TimestampTz	current_time = GetCurrentTimestamp();

#ifdef WAL_DEBUG
				if (XLOG_DEBUG ||
					(rmid == RM_XACT_ID && trace_recovery_messages <= DEBUG2) ||
					(rmid != RM_XACT_ID && trace_recovery_messages <= DEBUG3))
				{
					StringInfoData buf;

					initStringInfo(&buf);
					appendStringInfo(&buf, "REDO @ %X/%X; LSN %X/%X: ",
									 (uint32) (ReadRecPtr >> 32), (uint32) ReadRecPtr,
									 (uint32) (EndRecPtr >> 32), (uint32) EndRecPtr);
					xlog_outrec(&buf, xlogreader);
					appendStringInfoString(&buf, " - ");
					xlog_outdesc(&buf, xlogreader);
					elog(LOG, "%s", buf.data);
					pfree(buf.data);
				}
#endif

				/* POLAR: avoid calling HandleStartupProcInterrupts too much */
				if (TimestampDifferenceExceeds(last_handle_interrupts_time, current_time, 5))
				{
					/* Handle interrupt signals of startup process */
					HandleStartupProcInterrupts();
					/* POLAR: every 5 ms call this */
					last_handle_interrupts_time = current_time;
				}
				/* POLAR end */

				/*
				 * Pause WAL replay, if requested by a hot-standby session via
				 * SetRecoveryPause().
				 *
				 * Note that we intentionally don't take the info_lck spinlock
				 * here.  We might therefore read a slightly stale value of
				 * the recoveryPause flag, but it can't be very stale (no
				 * worse than the last spinlock we did acquire).  Since a
				 * pause request is a pretty asynchronous thing anyway,
				 * possibly responding to it one WAL record later than we
				 * otherwise would is a minor issue, so it doesn't seem worth
				 * adding another spinlock cycle to prevent that.
				 */
				if (((volatile XLogCtlData *) XLogCtl)->recoveryPause)
					recoveryPausesHere();

				/*
				 * Have we reached our recovery target?
				 */
				if (recoveryStopsBefore(xlogreader))
				{
					reachedStopPoint = true;	/* see below */
					break;
				}

				/*
				 * If we've been asked to lag the master, wait on latch until
				 * enough time has passed.
				 */
				if (recoveryApplyDelay(xlogreader))
				{
					/*
					 * We test for paused recovery again here. If user sets
					 * delayed apply, it may be because they expect to pause
					 * recovery in case of problems, so we must test again
					 * here otherwise pausing during the delay-wait wouldn't
					 * work.
					 */
					if (((volatile XLogCtlData *) XLogCtl)->recoveryPause)
						recoveryPausesHere();
				}

				/* Setup error traceback support for ereport() */
				errcallback.callback = rm_redo_error_callback;
				errcallback.arg = (void *) xlogreader;
				errcallback.previous = error_context_stack;
				error_context_stack = &errcallback;

				/*
				 * ShmemVariableCache->nextXid must be beyond record's xid.
				 *
				 * We don't expect anyone else to modify nextXid, hence we
				 * don't need to hold a lock while examining it.  We still
				 * acquire the lock to modify it, though.
				 */
				if (TransactionIdFollowsOrEquals(record->xl_xid,
												 ShmemVariableCache->nextXid))
				{
					LWLockAcquire(XidGenLock, LW_EXCLUSIVE);
					ShmemVariableCache->nextXid = record->xl_xid;
					TransactionIdAdvance(ShmemVariableCache->nextXid);
					LWLockRelease(XidGenLock);
				}

				/*
				 * Before replaying this record, check if this record causes
				 * the current timeline to change. The record is already
				 * considered to be part of the new timeline, so we update
				 * ThisTimeLineID before replaying it. That's important so
				 * that replayEndTLI, which is recorded as the minimum
				 * recovery point's TLI if recovery stops after this record,
				 * is set correctly.
				 * POLAR: If this record's lsn is lower than redo_start_lsn,
				 * then it is already replayed before. And ThisTimeLineID may
				 * has already moved forward. So we don't need to replay it
				 * via rm_redo and don't care about timeline here either.
				 */
				if (record->xl_rmid == RM_XLOG_ID &&
					xlogreader->ReadRecPtr >= redo_start_lsn)
				{
					TimeLineID	newTLI = ThisTimeLineID;
					TimeLineID	prevTLI = ThisTimeLineID;
					uint8		info = record->xl_info & ~XLR_INFO_MASK;

					if (info == XLOG_CHECKPOINT_SHUTDOWN)
					{
						CheckPoint	checkPoint;

						memcpy(&checkPoint, XLogRecGetData(xlogreader), sizeof(CheckPoint));
						newTLI = checkPoint.ThisTimeLineID;
						prevTLI = checkPoint.PrevTimeLineID;
					}
					else if (info == XLOG_END_OF_RECOVERY)
					{
						xl_end_of_recovery xlrec;

						memcpy(&xlrec, XLogRecGetData(xlogreader), sizeof(xl_end_of_recovery));
						newTLI = xlrec.ThisTimeLineID;
						prevTLI = xlrec.PrevTimeLineID;
					}

					if (newTLI != ThisTimeLineID)
					{
						/* Check that it's OK to switch to this TLI */
						checkTimeLineSwitch(EndRecPtr, newTLI, prevTLI);

						/* Following WAL records should be run with new TLI */
						ThisTimeLineID = newTLI;
						switchedTLI = true;
					}
				}

				/*
				 * Update shared replayEndRecPtr before replaying this record,
				 * so that XLogFlush will update minRecoveryPoint correctly.
				 */
				SpinLockAcquire(&XLogCtl->info_lck);
				XLogCtl->replayEndRecPtr = EndRecPtr;
				XLogCtl->replayEndTLI = ThisTimeLineID;
				SpinLockRelease(&XLogCtl->info_lck);

				/*
				 * If we are attempting to enter Hot Standby mode, process
				 * XIDs we see
				 * POLAR: If this record's lsn is lower than redo_start_lsn,
				 * then it should not be replayed. So we don't need to add
				 * it's transaction ID onto the KnownAssignedXids array.
				 */
				if (standbyState >= STANDBY_INITIALIZED &&
					TransactionIdIsValid(record->xl_xid) &&
					xlogreader->ReadRecPtr >= redo_start_lsn)
					RecordKnownAssignedTransactionIds(record->xl_xid);

				/*
				 * POLAR: 1. Hook redo function. If it's handled by rm_polar_idx_redo function
				 * rm_redo will not be called.
				 * 2. logindex_mini_trans_lsn is output parameters.After we call polar_log_index_parse_xlog
				 * if logindex_mini_trans_lsn is valid which means we parse xlog during a mini transaction.
				 */
				logindex_mini_trans_lsn = InvalidXLogRecPtr;

				/*
				 * POLAR: if this record related to a lock in getting, means it
				 * is an async ddl, we should wait until get the lock
				 */
				if (polar_allow_async_ddl_lock_replay() &&
					polar_async_ddl_lock_replay_tx_is_replaying(record->xl_xid))
				{
					elog(LOG, "polar async ddl lock replay: startup: record "
							  "protect by lock, but not get, wait for it, xid: %d", record->xl_xid);
					while (polar_async_ddl_lock_replay_tx_is_replaying(record->xl_xid))
					{
						pg_usleep(10000L); /* 10ms */
						/* Handle interrupt signals of startup process */
						HandleStartupProcInterrupts();
					}
				}

				/* POLAR: clean smgr shared cache for replica */
				if (polar_enabled_nblock_cache())
					polar_clean_smgr_cache_if_needed(xlogreader);

				/* Now apply the WAL record itself */
				if (!polar_logindex_parse_xlog(polar_logindex_redo_instance, record->xl_rmid,
							xlogreader, redo_start_lsn, &logindex_mini_trans_lsn))
					RmgrTable[record->xl_rmid].rm_redo(xlogreader);

				/*
				 * POLAR: keep some memory of polar_xlog_queue and push record meta into polar_xlog_queue.
				 */
				if (polar_is_standby() && reachedConsistency && POLAR_LOGINDEX_ENABLE_XLOG_QUEUE())
					polar_standby_xlog_send_queue_push(polar_logindex_redo_instance->xlog_queue, xlogreader);
				/* POLAR end */
				/*
				 * After redo, check whether the backup pages associated with
				 * the WAL record are consistent with the existing pages. This
				 * check is done only if consistency check is enabled for this
				 * record.
				 */
				if ((record->xl_info & XLR_CHECK_CONSISTENCY) != 0)
					checkXLogConsistency(xlogreader);

				/* Pop the error context stack */
				error_context_stack = errcallback.previous;

				/*
				 * POLAR: Update last replayed read record position after this record
				 * has been successfully replayed
				 */
				polar_set_last_replayed_read_ptr(polar_logindex_redo_instance, ReadRecPtr);

				/*
				 * Update lastReplayedEndRecPtr after this record has been
				 * successfully replayed.
				 */
				SpinLockAcquire(&XLogCtl->info_lck);
				XLogCtl->lastReplayedEndRecPtr = EndRecPtr;
				XLogCtl->lastReplayedTLI = ThisTimeLineID;
				SpinLockRelease(&XLogCtl->info_lck);
				/* POLAR: update async ddl replay ptr */
				if (polar_allow_async_ddl_lock_replay())
					polar_async_update_last_ptr();
				/*
				 * POLAR: If logindex_mini_trans_lsn is valid which means we parse xlog during a mini transaction.
				 * And now we finish parsing the xlog record and end this mini transaction.
				 */
				if (logindex_mini_trans_lsn != InvalidXLogRecPtr)
					polar_logindex_mini_trans_end(polar_logindex_redo_instance->mini_trans,  logindex_mini_trans_lsn);

				/*
				 * If rm_redo called XLogRequestWalReceiverReply, then we wake
				 * up the receiver so that it notices the updated
				 * lastReplayedEndRecPtr and sends a reply to the master.
				 */
				if (doRequestWalReceiverReply)
				{
					doRequestWalReceiverReply = false;
					WalRcvForceReply();
				}

				/* Remember this record as the last-applied one */
				LastRec = ReadRecPtr;

				/* Allow read-only connections if we're consistent now */
				/* POLAR: When enable logindex it may change replay from xlog which lsn is smaller than redo_start_lsn
				 * so we have to check consistency when xlog record lsn is larger or equal to previous redo_start_lsn.
				 * Otherwise we may reach unexpected consistency
				 */
				if (EndRecPtr >= redo_start_lsn)
					CheckRecoveryConsistency();
				/* Is this a timeline switch? */
				if (switchedTLI)
				{
					/*
					 * Before we continue on the new timeline, clean up any
					 * (possibly bogus) future WAL segments on the old
					 * timeline.
					 */
					RemoveNonParentXlogFiles(EndRecPtr, ThisTimeLineID);

					/*
					 * Wake up any walsenders to notice that we are on a new
					 * timeline.
					 */
					if (switchedTLI && AllowCascadeReplication())
						WalSndWakeup();
				}

				/* Exit loop if we reached inclusive recovery target */
				if (recoveryStopsAfter(xlogreader))
				{
					reachedStopPoint = true;
					break;
				}

				/* POLAR: wait util startup delay master lag, just for debug test */
				if (unlikely(polar_startup_replay_delay_size > 0))
				{
					while(GetWalRcvWriteRecPtr(NULL, NULL) < EndRecPtr +
						polar_startup_replay_delay_size * 1024 * 1024L)
					{
						pg_usleep(100L); /* 0.1ms */
						/* Handle interrupt signals of startup process */
						HandleStartupProcInterrupts();
					}
				}
				/* POLAR end */

				/* Else, try to fetch the next WAL record */
				record = ReadRecord(xlogreader, InvalidXLogRecPtr, LOG, false);
			} while (record != NULL);

			/*
			 * end of main redo apply loop
			 */

			/* POLAR: stop async ddl lock replay worker */
			polar_stop_async_ddl_lock_replay_workers();
			/* POLAR end */

			if (reachedStopPoint)
			{
				if (!reachedConsistency)
					ereport(FATAL,
							(errmsg("requested recovery stop point is before consistent recovery point")));

				/* POLAR: in DMA mode, recovery stop point must after consensus log point */
				if (polar_is_dma_data_node() &&
						((recoveryStopAfter && 
							!polar_dma_beyond_consensus_log(EndRecPtr)) ||
						 (!recoveryStopAfter && 
							!polar_dma_beyond_consensus_log(LastRec))))
				{
					ereport(FATAL,
							(errmsg("requested recovery stop point is before consensus log point")));
				}
				/* POLAR end */

				/*
				 * This is the last point where we can restart recovery with a
				 * new recovery target, if we shutdown and begin again. After
				 * this, Resource Managers may choose to do permanent
				 * corrective actions at end of recovery.
				 */
				switch (recoveryTargetAction)
				{
					case RECOVERY_TARGET_ACTION_SHUTDOWN:

						/*
						 * exit with special return code to request shutdown
						 * of postmaster.  Log messages issued from
						 * postmaster.
						 */
						proc_exit(3);

					case RECOVERY_TARGET_ACTION_PAUSE:
						SetRecoveryPause(true);
						recoveryPausesHere();

						/* drop into promote */

					case RECOVERY_TARGET_ACTION_PROMOTE:
						break;
				}
			}

			/* Allow resource managers to do any required cleanup. */
			for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
			{
				if (RmgrTable[rmid].rm_cleanup != NULL)
					RmgrTable[rmid].rm_cleanup();
			}

			ereport(LOG,
					(errmsg("redo done at %X/%X",
							(uint32) (ReadRecPtr >> 32), (uint32) ReadRecPtr)));
			xtime = GetLatestXTime();
			if (xtime)
				ereport(LOG,
						(errmsg("last completed transaction was at log time %s",
								timestamptz_to_str(xtime))));

			InRedo = false;
		}
		else
		{
			/* there are no WAL records following the checkpoint */
			ereport(LOG,
					(errmsg("redo is not required")));
		}

		/*
		 * POLAR: if in master mode, force check whether some unexpected reused
		 * buffer left when finishing redo, but for standby/archive mode, only
		 * can check at CheckRecoveryConsistency()
		 */
		if (!ArchiveRecoveryRequested)
			polar_check_buffer_pool_consistency();
		/* POLAR end */
=======
		PerformWalRecovery();
		performedWalRecovery = true;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
	else
		performedWalRecovery = false;

	/* POLAR: If DataMax mode, we need enter datamax main here */
	if (polar_is_datamax())
	{
		polar_datamax_main();
		/* do as StartupProcessMain do  */
		proc_exit(0);
	}
	/* POLAR end */

	/*
	 * Finish WAL recovery.
	 */
	endOfRecoveryInfo = FinishWalRecovery();
	EndOfLog = endOfRecoveryInfo->endOfLog;
	EndOfLogTLI = endOfRecoveryInfo->endOfLogTLI;
	abortedRecPtr = endOfRecoveryInfo->abortedRecPtr;
	missingContrecPtr = endOfRecoveryInfo->missingContrecPtr;

	/*
	 * Reset ps status display, so as no information related to recovery shows
	 * up.
	 */
	set_ps_display("");

	/*
	 * When recovering from a backup (we are in recovery, and archive recovery
	 * was requested), complain if we did not roll forward far enough to reach
	 * the point where the database is consistent.  For regular online
	 * backup-from-primary, that means reaching the end-of-backup WAL record
	 * (at which point we reset backupStartPoint to be Invalid), for
	 * backup-from-replica (which can't inject records into the WAL stream),
	 * that point is when we reach the minRecoveryPoint in pg_control (which
	 * we purposefully copy last when backing up from a replica).  For
	 * pg_rewind (which creates a backup_label with a method of "pg_rewind")
	 * or snapshot-style backups (which don't), backupEndRequired will be set
	 * to false.
	 *
	 * Note: it is indeed okay to look at the local variable
	 * LocalMinRecoveryPoint here, even though ControlFile->minRecoveryPoint
	 * might be further ahead --- ControlFile->minRecoveryPoint cannot have
	 * been advanced beyond the WAL we processed.
	 */
	if (InRecovery &&
		(EndOfLog < LocalMinRecoveryPoint ||
		 !XLogRecPtrIsInvalid(ControlFile->backupStartPoint)))
	{
		/*
		 * Ran off end of WAL before reaching end-of-backup WAL record, or
		 * minRecoveryPoint. That's a bad sign, indicating that you tried to
		 * recover from an online backup but never called pg_backup_stop(), or
		 * you didn't archive all the WAL needed.
		 */
		if (ArchiveRecoveryRequested || ControlFile->backupEndRequired)
		{
			if (!XLogRecPtrIsInvalid(ControlFile->backupStartPoint) || ControlFile->backupEndRequired)
				ereport(FATAL,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("WAL ends before end of online backup"),
						 errhint("All WAL generated while online backup was taken must be available at recovery.")));
			else
				ereport(FATAL,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("WAL ends before consistent recovery point")));
		}
	}

	/*
	 * Reset unlogged relations to the contents of their INIT fork. This is
	 * done AFTER recovery is complete so as to include any unlogged relations
	 * created during recovery, but BEFORE recovery is marked as having
	 * completed successfully. Otherwise we'd not retry if any of the post
	 * end-of-recovery steps fail.
	 */
	if (InRecovery)
		ResetUnloggedRelations(UNLOGGED_RELATION_INIT);

	if (InRecovery && fast_promote && enable_logindex_online_promote)
	{
		elog(LOG, "Before online promote last_replayed_end=%lX", GetXLogReplayRecPtr(NULL));
		/*
		 * POLAR: There's no dirty page in ro mode, so restartpoint should be finished quickly.
		 * Request to create restartpoint if ro does not create for rw last checkpoint
		 */
		polar_request_last_restartpoint();

		/*
		 * POLAR: Wait background process stop replay.It must be called before DisownLatch(&XLogCtl->recoveryWakeupLatch).
		 * Otherwise we can not wait in this function
		 */
		polar_wait_logindex_bg_stop_replay(polar_logindex_redo_instance, &XLogCtl->recoveryWakeupLatch);
	}

	/*
	 * Pre-scan prepared transactions to find out the range of XIDs present.
	 * This information is not quite needed yet, but it is positioned here so
	 * as potential problems are detected before any on-disk change is done.
	 */
<<<<<<< HEAD
	if (ArchiveRecoveryRequested)
		DisownLatch(&XLogCtl->recoveryWakeupLatch);

	/*
	 * We are now done reading the xlog from stream. Turn off streaming
	 * recovery to force fetching the files (which would be required at end of
	 * recovery, e.g., timeline history file) from archive or pg_wal.
	 *
	 * Note that standby mode must be turned off after killing WAL receiver,
	 * i.e., calling ShutdownWalRcv().
	 */
	Assert(!WalRcvStreaming());
	StandbyMode = false;

	/*
	 * POLAR: When read xlog from xlog queue, not all members of XLogReaderState are set
	 * so we have to create a new xlogreader and force it to read last record from storage
	 */
	if (fast_promote && enable_logindex_online_promote)
	{
		elog(LOG, "Last record before online promote is %lX, currentSource=%d", LastRec, currentSource);
		xlogreader = polar_reset_xlogreader(xlogreader, &private);
	}

	/*
	 * Re-fetch the last valid or last applied record, so we can identify the
	 * exact endpoint of what we consider the valid portion of WAL.
	 */
	record = ReadRecord(xlogreader, LastRec, PANIC, false);
	EndOfLog = EndRecPtr;

	/* 
	 * POLAR: record EndOfLog when polar_enable_promote_wait_for_walreceive_done = on
	 * used to verify whether standby has received all wal
	 */
	if (polar_enable_promote_wait_for_walreceive_done && fast_promote)
		elog(LOG,"Last record before promote standby is %lX", EndOfLog);

	/*
	 * EndOfLogTLI is the TLI in the filename of the XLOG segment containing
	 * the end-of-log. It could be different from the timeline that EndOfLog
	 * nominally belongs to, if there was a timeline switch in that segment,
	 * and we were reading the old WAL from a segment belonging to a higher
	 * timeline.
	 */
	EndOfLogTLI = xlogreader->readPageTLI;

	/*
	 * Complain if we did not roll forward far enough to render the backup
	 * dump consistent.  Note: it is indeed okay to look at the local variable
	 * minRecoveryPoint here, even though ControlFile->minRecoveryPoint might
	 * be further ahead --- ControlFile->minRecoveryPoint cannot have been
	 * advanced beyond the WAL we processed.
	 */
	if (InRecovery &&
		(EndOfLog < minRecoveryPoint ||
		 !XLogRecPtrIsInvalid(ControlFile->backupStartPoint)))
	{
		/*
		 * Ran off end of WAL before reaching end-of-backup WAL record, or
		 * minRecoveryPoint. That's usually a bad sign, indicating that you
		 * tried to recover from an online backup but never called
		 * pg_stop_backup(), or you didn't archive all the WAL up to that
		 * point. However, this also happens in crash recovery, if the system
		 * crashes while an online backup is in progress. We must not treat
		 * that as an error, or the database will refuse to start up.
		 */
		if (ArchiveRecoveryRequested || ControlFile->backupEndRequired)
		{
			if (ControlFile->backupEndRequired)
				ereport(FATAL,
						(errmsg("WAL ends before end of online backup"),
						 errhint("All WAL generated while online backup was taken must be available at recovery.")));
			else if (!XLogRecPtrIsInvalid(ControlFile->backupStartPoint))
				ereport(FATAL,
						(errmsg("WAL ends before end of online backup"),
						 errhint("Online backup started with pg_start_backup() must be ended with pg_stop_backup(), and all WAL up to that point must be available at recovery.")));
			else
				ereport(FATAL,
						(errmsg("WAL ends before consistent recovery point")));
		}
	}
=======
	oldestActiveXID = PrescanPreparedTransactions(NULL, NULL);

	/*
	 * Allow ordinary WAL segment creation before possibly switching to a new
	 * timeline, which creates a new segment, and after the last ReadRecord().
	 */
	SetInstallXLogFileSegmentActive();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Pre-scan prepared transactions to find out the range of XIDs present.
	 * This information is not quite needed yet, but it is positioned here so
	 * as potential problems are detected before any on-disk change is done.
	 */
	oldestActiveXID = PrescanPreparedTransactions(NULL, NULL);

	/*
	 * Consider whether we need to assign a new timeline ID.
	 *
	 * If we did archive recovery, we always assign a new ID.  This handles a
	 * couple of issues.  If we stopped short of the end of WAL during
	 * recovery, then we are clearly generating a new timeline and must assign
	 * it a unique new ID.  Even if we ran to the end, modifying the current
	 * last segment is problematic because it may result in trying to
	 * overwrite an already-archived copy of that segment, and we encourage
	 * DBAs to make their archive_commands reject that.  We can dodge the
	 * problem by making the new active segment have a new timeline ID.
	 *
	 * In a normal crash recovery, we can just extend the timeline we were in.
	 */
<<<<<<< HEAD
	/*
	 * POLAR: The data won't be lost when stored in shared disk, so we will not change timeline when promote replica to rw
	 */
	PrevTimeLineID = ThisTimeLineID;
	if (ArchiveRecoveryRequested)
	{
		if (fast_promote && enable_logindex_online_promote)
		{
			elog(LOG, "Before online promote EndOfLog=%lX", EndOfLog);
			polar_exit_archive_recovery();
		}
		else
		{
			char		reason[200];
			char		recoveryPath[MAXPGPATH];

			Assert(InArchiveRecovery);

			ThisTimeLineID = findNewestTimeLine(recoveryTargetTLI) + 1;
			ereport(LOG,
					(errmsg("selected new timeline ID: %u", ThisTimeLineID)));

			/*
			 * Create a comment for the history file to explain why and where
			 * timeline changed.
			 */
			if (recoveryTarget == RECOVERY_TARGET_XID)
				snprintf(reason, sizeof(reason),
						 "%s transaction %u",
						 recoveryStopAfter ? "after" : "before",
						 recoveryStopXid);
			else if (recoveryTarget == RECOVERY_TARGET_TIME)
				snprintf(reason, sizeof(reason),
						 "%s %s\n",
						 recoveryStopAfter ? "after" : "before",
						 timestamptz_to_str(recoveryStopTime));
			else if (recoveryTarget == RECOVERY_TARGET_LSN)
				snprintf(reason, sizeof(reason),
						 "%s LSN %X/%X\n",
						 recoveryStopAfter ? "after" : "before",
						 (uint32) (recoveryStopLSN >> 32),
						 (uint32) recoveryStopLSN);
			else if (recoveryTarget == RECOVERY_TARGET_NAME)
				snprintf(reason, sizeof(reason),
						 "at restore point \"%s\"",
						 recoveryStopName);
			else if (recoveryTarget == RECOVERY_TARGET_IMMEDIATE)
				snprintf(reason, sizeof(reason), "reached consistency");
			else
				snprintf(reason, sizeof(reason), "no recovery target specified");

			/*
			 * We are now done reading the old WAL.  Turn off archive fetching if
			 * it was active, and make a writable copy of the last WAL segment.
			 * (Note that we also have a copy of the last block of the old WAL in
			 * readBuf; we will use that below.)
			 */
			exitArchiveRecovery(EndOfLogTLI, EndOfLog);

		/*
		 * Write the timeline history file, and have it archived. After this
		 * point (or rather, as soon as the file is archived), the timeline
		 * will appear as "taken" in the WAL archive and to any standby
		 * servers.  If we crash before actually switching to the new
		 * timeline, standby servers will nevertheless think that we switched
		 * to the new timeline, and will try to connect to the new timeline.
		 * To minimize the window for that, try to do as little as possible
		 * between here and writing the end-of-recovery record.
		 */
		writeTimeLineHistory(ThisTimeLineID, recoveryTargetTLI,
							 EndRecPtr, reason);

		/*
		 * Since there might be a partial WAL segment named RECOVERYXLOG, get
		 * rid of it.
		 */
		snprintf(recoveryPath, MAXPGPATH, XLOGDIR "/RECOVERYXLOG");
		unlink(recoveryPath);	/* ignore any error */

		/* Get rid of any remaining recovered timeline-history file, too */
		snprintf(recoveryPath, MAXPGPATH, XLOGDIR "/RECOVERYHISTORY");
		unlink(recoveryPath);	/* ignore any error */
		}
=======
	newTLI = endOfRecoveryInfo->lastRecTLI;
	if (ArchiveRecoveryRequested)
	{
		newTLI = findNewestTimeLine(recoveryTargetTLI) + 1;
		ereport(LOG,
				(errmsg("selected new timeline ID: %u", newTLI)));

		/*
		 * Make a writable copy of the last WAL segment.  (Note that we also
		 * have a copy of the last block of the old WAL in
		 * endOfRecovery->lastPage; we will use that below.)
		 */
		XLogInitNewTimeline(EndOfLogTLI, EndOfLog, newTLI);

		/*
		 * Remove the signal files out of the way, so that we don't
		 * accidentally re-enter archive recovery mode in a subsequent crash.
		 */
		if (endOfRecoveryInfo->standby_signal_file_found)
			durable_unlink(STANDBY_SIGNAL_FILE, FATAL);

		if (endOfRecoveryInfo->recovery_signal_file_found)
			durable_unlink(RECOVERY_SIGNAL_FILE, FATAL);

		/*
		 * Write the timeline history file, and have it archived. After this
		 * point (or rather, as soon as the file is archived), the timeline
		 * will appear as "taken" in the WAL archive and to any standby
		 * servers.  If we crash before actually switching to the new
		 * timeline, standby servers will nevertheless think that we switched
		 * to the new timeline, and will try to connect to the new timeline.
		 * To minimize the window for that, try to do as little as possible
		 * between here and writing the end-of-recovery record.
		 */
		writeTimeLineHistory(newTLI, recoveryTargetTLI,
							 EndOfLog, endOfRecoveryInfo->recoveryStopReason);

		ereport(LOG,
				(errmsg("archive recovery complete")));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	/* Save the selected TimeLineID in shared memory, too */
	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->InsertTimeLineID = newTLI;
	XLogCtl->PrevTimeLineID = endOfRecoveryInfo->lastRecTLI;
	SpinLockRelease(&XLogCtl->info_lck);

	/*
<<<<<<< HEAD
=======
	 * Actually, if WAL ended in an incomplete record, skip the parts that
	 * made it through and start writing after the portion that persisted.
	 * (It's critical to first write an OVERWRITE_CONTRECORD message, which
	 * we'll do as soon as we're open for writing new WAL.)
	 */
	if (!XLogRecPtrIsInvalid(missingContrecPtr))
	{
		/*
		 * We should only have a missingContrecPtr if we're not switching to a
		 * new timeline. When a timeline switch occurs, WAL is copied from the
		 * old timeline to the new only up to the end of the last complete
		 * record, so there can't be an incomplete WAL record that we need to
		 * disregard.
		 */
		Assert(newTLI == endOfRecoveryInfo->lastRecTLI);
		Assert(!XLogRecPtrIsInvalid(abortedRecPtr));
		EndOfLog = missingContrecPtr;
	}

	/*
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 * Prepare to write WAL starting at EndOfLog location, and init xlog
	 * buffer cache using the block containing the last record from the
	 * previous incarnation.
	 */
	Insert = &XLogCtl->Insert;
	Insert->PrevBytePos = XLogRecPtrToBytePos(endOfRecoveryInfo->lastRec);
	Insert->CurrBytePos = XLogRecPtrToBytePos(EndOfLog);

	/*
	 * Tricky point here: lastPage contains the *last* block that the LastRec
	 * record spans, not the one it starts in.  The last block is indeed the
	 * one we want to use.
	 */
	if (EndOfLog % XLOG_BLCKSZ != 0)
	{
		char	   *page;
		int			len;
		int			firstIdx;

		firstIdx = XLogRecPtrToBufIdx(EndOfLog);
		len = EndOfLog - endOfRecoveryInfo->lastPageBeginPtr;
		Assert(len < XLOG_BLCKSZ);

		/* Copy the valid part of the last block, and zero the rest */
		page = &XLogCtl->pages[firstIdx * XLOG_BLCKSZ];
		memcpy(page, endOfRecoveryInfo->lastPage, len);
		memset(page + len, 0, XLOG_BLCKSZ - len);

		pg_atomic_write_u64(&XLogCtl->xlblocks[firstIdx], endOfRecoveryInfo->lastPageBeginPtr + XLOG_BLCKSZ);
		XLogCtl->InitializedUpTo = endOfRecoveryInfo->lastPageBeginPtr + XLOG_BLCKSZ;
	}
	else
	{
		/*
		 * There is no partial block to copy. Just set InitializedUpTo, and
		 * let the first attempt to insert a log record to initialize the next
		 * buffer.
		 */
		XLogCtl->InitializedUpTo = EndOfLog;
	}

	/*
	 * Update local and shared status.  This is OK to do without any locks
	 * because no other process can be reading or writing WAL yet.
	 */
	LogwrtResult.Write = LogwrtResult.Flush = EndOfLog;
	pg_atomic_write_u64(&XLogCtl->logInsertResult, EndOfLog);
	pg_atomic_write_u64(&XLogCtl->logWriteResult, EndOfLog);
	pg_atomic_write_u64(&XLogCtl->logFlushResult, EndOfLog);
	XLogCtl->LogwrtRqst.Write = EndOfLog;
	XLogCtl->LogwrtRqst.Flush = EndOfLog;

<<<<<<< HEAD
	if (polar_is_dma_data_node())
		ConsensusSetXLogFlushedLSN(EndOfLog, EndOfLogTLI, true);	

	/*
	 * Update full_page_writes in shared memory and write an XLOG_FPW_CHANGE
	 * record before resource manager writes cleanup WAL records or checkpoint
	 * record is written.
	 */
	Insert->fullPageWrites = lastFullPageWrites;
	LocalSetXLogInsertAllowed();
	UpdateFullPageWrites();
	LocalXLogInsertAllowed = -1;

	if (InRecovery)
	{
		/*
		 * POLAR: check whether support lazy end-of-recovery checkpoint, if yes,
		 * we will ignore the end-of-recovery checkpoint, and create a lazy
		 * online checkpoint at the end.
		 */
		if (!ArchiveRecoveryRequested &&
			polar_lazy_end_of_recovery_checkpoint_enabled() &&
			!fast_promoted && !bgwriterLaunched && !flog_instance)
			polar_lazy_end_of_recovery_checkpoint = true;

		/*
		 * Perform a checkpoint to update all our recovery activity to disk.
		 *
		 * Note that we write a shutdown checkpoint rather than an on-line
		 * one. This is not particularly critical, but since we may be
		 * assigning a new TLI, using a shutdown checkpoint allows us to have
		 * the rule that TLI only changes in shutdown checkpoints, which
		 * allows some extra error checking in xlog_redo.
		 *
		 * In fast promotion, only create a lightweight end-of-recovery record
		 * instead of a full checkpoint. A checkpoint is requested later,
		 * after we're fully out of recovery mode and already accepting
		 * queries.
		 */
		if (bgwriterLaunched)
		{
			if (fast_promote)
			{
				checkPointLoc = ControlFile->checkPoint;

				/*
				 * Confirm the last checkpoint is available for us to recover
				 * from if we fail.
				 */
				record = ReadCheckpointRecord(xlogreader, checkPointLoc, 1, false);
				if (record != NULL)
				{
					fast_promoted = true;

					/*
					 * Insert a special WAL record to mark the end of
					 * recovery, since we aren't doing a checkpoint. That
					 * means that the checkpointer process may likely be in
					 * the middle of a time-smoothed restartpoint and could
					 * continue to be for minutes after this. That sounds
					 * strange, but the effect is roughly the same and it
					 * would be stranger to try to come out of the
					 * restartpoint and then checkpoint. We request a
					 * checkpoint later anyway, just for safety.
					 */
					CreateEndOfRecoveryRecord();
				} else if (enable_logindex_online_promote)
					elog(FATAL, "Failed to read last checkpoint in online promote, checkPointLoc=%lX", checkPointLoc);
			}

			if (!fast_promoted && !polar_lazy_end_of_recovery_checkpoint)
				RequestCheckpoint(CHECKPOINT_END_OF_RECOVERY |
								  CHECKPOINT_IMMEDIATE |
								  CHECKPOINT_WAIT);
		}
		else if (!polar_lazy_end_of_recovery_checkpoint)
			CreateCheckPoint(CHECKPOINT_END_OF_RECOVERY | CHECKPOINT_IMMEDIATE);

		/*
		 * And finally, execute the recovery_end_command, if any.
		 */
		if (recoveryEndCommand)
			ExecuteRecoveryCommand(recoveryEndCommand,
								   "recovery_end_command",
								   true);
	}

	/* POLAR: We don't change timeline when do logindex online promote */
	if (ArchiveRecoveryRequested && !(fast_promote && enable_logindex_online_promote))
	{
		/* POLAR: if in dma mode, nofity history file ready after consensus committed. */
		if (polar_is_dma_data_node() && XLogArchivingActive())
		{
			char		histfname[MAXFNAMELEN];
			TLHistoryFileName(histfname, ThisTimeLineID);
			XLogArchiveNotify(histfname);
		}

		/*
		 * We switched to a new timeline. Clean up segments on the old
		 * timeline.
		 *
		 * If there are any higher-numbered segments on the old timeline,
		 * remove them. They might contain valid WAL, but they might also be
		 * pre-allocated files containing garbage. In any case, they are not
		 * part of the new timeline's history so we don't need them.
		 */
		RemoveNonParentXlogFiles(EndOfLog, ThisTimeLineID);

		/*
		 * If the switch happened in the middle of a segment, what to do with
		 * the last, partial segment on the old timeline? If we don't archive
		 * it, and the server that created the WAL never archives it either
		 * (e.g. because it was hit by a meteor), it will never make it to the
		 * archive. That's OK from our point of view, because the new segment
		 * that we created with the new TLI contains all the WAL from the old
		 * timeline up to the switch point. But if you later try to do PITR to
		 * the "missing" WAL on the old timeline, recovery won't find it in
		 * the archive. It's physically present in the new file with new TLI,
		 * but recovery won't look there when it's recovering to the older
		 * timeline. On the other hand, if we archive the partial segment, and
		 * the original server on that timeline is still running and archives
		 * the completed version of the same segment later, it will fail. (We
		 * used to do that in 9.4 and below, and it caused such problems).
		 *
		 * As a compromise, we rename the last segment with the .partial
		 * suffix, and archive it. Archive recovery will never try to read
		 * .partial segments, so they will normally go unused. But in the odd
		 * PITR case, the administrator can copy them manually to the pg_wal
		 * directory (removing the suffix). They can be useful in debugging,
		 * too.
		 *
		 * If a .done or .ready file already exists for the old timeline,
		 * however, we had already determined that the segment is complete, so
		 * we can let it be archived normally. (In particular, if it was
		 * restored from the archive to begin with, it's expected to have a
		 * .done file).
		 */
		if (XLogSegmentOffset(EndOfLog, wal_segment_size) != 0 &&
			XLogArchivingActive())
		{
			char		origfname[MAXFNAMELEN];
			XLogSegNo	endLogSegNo;

			XLByteToPrevSeg(EndOfLog, endLogSegNo, wal_segment_size);
			XLogFileName(origfname, EndOfLogTLI, endLogSegNo, wal_segment_size);

			if (!XLogArchiveIsReadyOrDone(origfname))
			{
				char		origpath[MAXPGPATH];
				char		partialfname[MAXFNAMELEN];
				char		partialpath[MAXPGPATH];

				XLogFilePath(origpath, EndOfLogTLI, endLogSegNo, wal_segment_size);

				/* POLAR: in DMA mode, the history must be complete. */
				if (polar_is_dma_data_node())
				{
					XLogArchiveNotify(origfname);
				}
				else
				{
					snprintf(partialfname, MAXFNAMELEN, "%s.partial", origfname);
					snprintf(partialpath, MAXPGPATH, "%s.partial", origpath);

					/*
					 * Make sure there's no .done or .ready file for the .partial
					 * file.
					 */
					XLogArchiveCleanup(partialfname);

					polar_durable_rename(origpath, partialpath, ERROR);
					XLogArchiveNotify(partialfname);
				}
			}
		}
	}

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	/*
	 * Preallocate additional log files, if wanted.
	 */
	PreallocXlogFiles(EndOfLog, newTLI);

	/*
	 * Okay, we're officially UP.
	 */
	InRecovery = false;

	/* start the archive_timeout timer and LSN running */
	XLogCtl->lastSegSwitchTime = (pg_time_t) time(NULL);
	XLogCtl->lastSegSwitchLSN = EndOfLog;

	/* also initialize latestCompletedXid, to nextXid - 1 */
<<<<<<< HEAD
	if (polar_csn_enable)
	{
		latestCompletedXid = ShmemVariableCache->nextXid;
		TransactionIdRetreat(latestCompletedXid);
		pg_atomic_write_u32(&polar_shmem_csn_mvcc_var_cache->polar_latest_completed_xid,
							latestCompletedXid);
		pg_atomic_write_u32(&polar_shmem_csn_mvcc_var_cache->polar_oldest_active_xid,
							oldestActiveXID);
	}
	else
	{
		LWLockAcquire(ProcArrayLock, LW_EXCLUSIVE);
		ShmemVariableCache->latestCompletedXid = ShmemVariableCache->nextXid;
		TransactionIdRetreat(ShmemVariableCache->latestCompletedXid);
		LWLockRelease(ProcArrayLock);
	}
=======
	LWLockAcquire(ProcArrayLock, LW_EXCLUSIVE);
	TransamVariables->latestCompletedXid = TransamVariables->nextXid;
	FullTransactionIdRetreat(&TransamVariables->latestCompletedXid);
	LWLockRelease(ProcArrayLock);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Start up subtrans, if not already done for hot standby.  (commit
	 * timestamps are started below, if necessary.)
	 */
	if (standbyState == STANDBY_DISABLED)
<<<<<<< HEAD
	{
		StartupCLOG();
		if (polar_csn_enable)
		{
			polar_csnlog_startup(oldestActiveXID);
		}
		else
		{
			StartupSUBTRANS(oldestActiveXID);
		}
	}
=======
		StartupSUBTRANS(oldestActiveXID);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	if (fast_promoted)
	{
		if (enable_logindex_online_promote)
			polar_online_promote_data(polar_logindex_redo_instance, oldestActiveXID);
		else if (enable_logindex_online_promote_standby)
			polar_standby_promote_data(polar_logindex_redo_instance);
	}


	/*
	 * Perform end of recovery actions for any SLRUs that need it.
	 */
	TrimCLOG();
	TrimMultiXact();

	/*
	 * Reload shared-memory state for prepared transactions.  This needs to
	 * happen before renaming the last partial segment of the old timeline as
	 * it may be possible that we have to recovery some transactions from it.
	 */
	RecoverPreparedTransactions();

	/* Shut down xlogreader */
	ShutdownWalRecovery();

	/* Enable WAL writes for this backend only. */
	LocalSetXLogInsertAllowed();

	/* If necessary, write overwrite-contrecord before doing anything else */
	if (!XLogRecPtrIsInvalid(abortedRecPtr))
	{
<<<<<<< HEAD
		polar_close(readFile);
		readFile = -1;
=======
		Assert(!XLogRecPtrIsInvalid(missingContrecPtr));
		CreateOverwriteContrecordRecord(abortedRecPtr, missingContrecPtr, newTLI);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	/*
	 * Update full_page_writes in shared memory and write an XLOG_FPW_CHANGE
	 * record before resource manager writes cleanup WAL records or checkpoint
	 * record is written.
	 */
	Insert->fullPageWrites = lastFullPageWrites;
	UpdateFullPageWrites();

	/*
	 * Emit checkpoint or end-of-recovery record in XLOG, if required.
	 */
	if (performedWalRecovery)
		promoted = PerformRecoveryXLogAction();

	/*
	 * If any of the critical GUCs have changed, log them before we allow
	 * backends to write WAL.
	 */
	XLogReportParameters();

	/* If this is archive recovery, perform post-recovery cleanup actions. */
	if (ArchiveRecoveryRequested)
		CleanupAfterArchiveRecovery(EndOfLogTLI, EndOfLog, newTLI);

	/*
	 * Local WAL inserts enabled, so it's time to finish initialization of
	 * commit timestamp.
	 */
	CompleteCommitTsInitialization();

	/*
	 * All done with end-of-recovery actions.
	 *
	 * Now allow backends to write WAL and update the control file status in
	 * consequence.  SharedRecoveryState, that controls if backends can write
	 * WAL, is updated while holding ControlFileLock to prevent other backends
	 * to look at an inconsistent state of the control file in shared memory.
	 * There is still a small window during which backends can write WAL and
	 * the control file is still referring to a system not in DB_IN_PRODUCTION
	 * state while looking at the on-disk control file.
	 *
	 * Also, we use info_lck to update SharedRecoveryState to ensure that
	 * there are no race conditions concerning visibility of other recent
	 * updates to shared memory.
	 */

	/* 
	 * POLAR:  When node type is replica, we will update control file in local file system.
	 * We must set control file state to DB_IN_PRODUCTION before set node type to be POLAR_MASTER,
	 * because pg_ctl will check promote state from control file in local file system.
	 * But when do online promote we have to set XLogCtl->SharedRecoveryState = RECOVERY_STATE_DONE after
	 * set node type bo be POLAR_MASTER.Otherwise the other processes like checkpointer will get false from function
	 * RecoveryInProgress(), but see the node type as replica, which will cause data corrupted.
	 */
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	ControlFile->state = DB_IN_PRODUCTION;

	if (fast_promoted)
	{
		if (POLAR_ENABLE_DMA() || enable_logindex_online_promote || enable_logindex_online_promote_standby)
		{
			/* POLAR: crash recovery or recovery after becoming leader in DMA mode 
			 * should always recover to the end of WAL*/
			ControlFile->minRecoveryPoint = InvalidXLogRecPtr;
			ControlFile->minRecoveryPointTLI = 0;
		}
		/* POLAR: save control file to local file system */
		UpdateControlFile();
		/* POLAR: Set node type to be master */
		polar_set_node_type(POLAR_MASTER);
		/* POLAR: Set polar_hot_standby_state to be STANDBY_DISABLED */
		polar_set_hot_standby_state(STANDBY_DISABLED);
	}

	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->SharedRecoveryState = RECOVERY_STATE_DONE;
	SpinLockRelease(&XLogCtl->info_lck);

	UpdateControlFile();
	LWLockRelease(ControlFileLock);

	/*
	 * Wake up all waiters for replay LSN.  They need to report an error that
	 * recovery was ended before reaching the target LSN.
	 */
	WaitLSNSetLatches(InvalidXLogRecPtr);

	/*
	 * Shutdown the recovery environment.  This must occur after
	 * RecoverPreparedTransactions() (see notes in lock_twophase_recover())
	 * and after switching SharedRecoveryState to RECOVERY_STATE_DONE so as
	 * any session building a snapshot will not rely on KnownAssignedXids as
	 * RecoveryInProgress() would return false at this stage.  This is
	 * particularly critical for prepared 2PC transactions, that would still
	 * need to be included in snapshots once recovery has ended.
	 */
	if (standbyState != STANDBY_DISABLED)
		ShutdownRecoveryTransactionEnvironment();

	/*
	 * If there were cascading standby servers connected to us, nudge any wal
	 * sender processes to notice that we've been promoted.
	 */
	WalSndWakeup(true, true);

<<<<<<< HEAD
	if (fast_promoted)
	{
		if (enable_logindex_online_promote)
		{
			/* Start up the flashback log before reset background replayed lsn */
			polar_startup_flashback(&ControlFile->checkPointCopy);
			/* POLAR: Wake up background process to start marking buffer dirty from replayed lsn */
			polar_reset_bg_replayed_lsn(polar_logindex_redo_instance);
		}
		else if (enable_logindex_online_promote_standby)
		{
			polar_set_bg_redo_state(polar_logindex_redo_instance, POLAR_BG_ONLINE_PROMOTE);
		}
		else
		{
			/*
			 * If this was a fast promotion, request an (online) checkpoint now. This
			 * isn't required for consistency, but the last restartpoint might be far
			 * back, and in case of a crash, recovering from it might take a longer
			 * than is appropriate now that we're not in standby mode anymore.
			 */
			RequestCheckpoint(CHECKPOINT_FORCE);
		}

		/* POLAR: remove POLAR_REPLICA_BOOTED_FILE when replica node is promoted */
		if (!polar_in_replica_mode())
			polar_remove_replica_booted_file();
	}

	if (polar_logindex_redo_instance)
	{
		/* POLAR: Set valid information for logindex redo after we parsed all xlog */
		polar_logindex_redo_set_valid_info(polar_logindex_redo_instance, EndOfLog);
		polar_logindex_calc_max_fullpage_no(polar_logindex_redo_instance->fullpage_ctl);
	}

	/* POLAR: create a lazy end-of-recovery checkpoint. */
	if (polar_lazy_end_of_recovery_checkpoint)
	{
		elog(LOG, "Create a lazy online checkpoint for end-of-recovery.");
		CreateCheckPoint(
			CHECKPOINT_IMMEDIATE | CHECKPOINT_WAIT | CHECKPOINT_LAZY);
	}
=======
	/*
	 * If this was a promotion, request an (online) checkpoint now. This isn't
	 * required for consistency, but the last restartpoint might be far back,
	 * and in case of a crash, recovering from it might take a longer than is
	 * appropriate now that we're not in standby mode anymore.
	 */
	if (promoted)
		RequestCheckpoint(CHECKPOINT_FORCE);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Callback from PerformWalRecovery(), called when we switch from crash
 * recovery to archive recovery mode.  Updates the control file accordingly.
 */
void
SwitchIntoArchiveRecovery(XLogRecPtr EndRecPtr, TimeLineID replayTLI)
{
<<<<<<< HEAD
	XLogRecPtr	lastReplayedEndRecPtr;

	/*
	 * During crash recovery, we don't reach a consistent state until we've
	 * replayed all the WAL.
	 */
	if (XLogRecPtrIsInvalid(minRecoveryPoint))
		return;

	Assert(InArchiveRecovery);

	/*
	 * assume that we are called in the startup process, and hence don't need
	 * a lock to read lastReplayedEndRecPtr
	 */
	lastReplayedEndRecPtr = XLogCtl->lastReplayedEndRecPtr;

	/*
	 * Have we reached the point where our base backup was completed?
	 */
	if (!XLogRecPtrIsInvalid(ControlFile->backupEndPoint) &&
		ControlFile->backupEndPoint <= lastReplayedEndRecPtr)
=======
	/* initialize minRecoveryPoint to this record */
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	ControlFile->state = DB_IN_ARCHIVE_RECOVERY;
	if (ControlFile->minRecoveryPoint < EndRecPtr)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	{
		ControlFile->minRecoveryPoint = EndRecPtr;
		ControlFile->minRecoveryPointTLI = replayTLI;
	}
	/* update local copy */
	LocalMinRecoveryPoint = ControlFile->minRecoveryPoint;
	LocalMinRecoveryPointTLI = ControlFile->minRecoveryPointTLI;

	/*
	 * The startup process can update its local copy of minRecoveryPoint from
	 * this point.
	 */
	updateMinRecoveryPoint = true;

	UpdateControlFile();

	/*
	 * We update SharedRecoveryState while holding the lock on ControlFileLock
	 * so both states are consistent in shared memory.
	 */
	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->SharedRecoveryState = RECOVERY_STATE_ARCHIVE;
	SpinLockRelease(&XLogCtl->info_lck);

	LWLockRelease(ControlFileLock);
}

/*
 * Callback from PerformWalRecovery(), called when we reach the end of backup.
 * Updates the control file accordingly.
 */
void
ReachedEndOfBackup(XLogRecPtr EndRecPtr, TimeLineID tli)
{
	/*
	 * We have reached the end of base backup, as indicated by pg_control. The
	 * data on disk is now consistent (unless minRecoveryPoint is further
	 * ahead, which can happen if we crashed during previous recovery).  Reset
	 * backupStartPoint and backupEndPoint, and update minRecoveryPoint to
	 * make sure we don't allow starting up at an earlier point even if
	 * recovery is stopped and restarted soon after this.
	 */
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);

	if (ControlFile->minRecoveryPoint < EndRecPtr)
	{
		ControlFile->minRecoveryPoint = EndRecPtr;
		ControlFile->minRecoveryPointTLI = tli;
	}

	ControlFile->backupStartPoint = InvalidXLogRecPtr;
	ControlFile->backupEndPoint = InvalidXLogRecPtr;
	ControlFile->backupEndRequired = false;
	UpdateControlFile();

	LWLockRelease(ControlFileLock);
}

/*
 * Perform whatever XLOG actions are necessary at end of REDO.
 *
 * The goal here is to make sure that we'll be able to recover properly if
 * we crash again. If we choose to write a checkpoint, we'll write a shutdown
 * checkpoint rather than an on-line one. This is not particularly critical,
 * but since we may be assigning a new TLI, using a shutdown checkpoint allows
 * us to have the rule that TLI only changes in shutdown checkpoints, which
 * allows some extra error checking in xlog_redo.
 */
static bool
PerformRecoveryXLogAction(void)
{
	bool		promoted = false;

	/*
	 * Perform a checkpoint to update all our recovery activity to disk.
	 *
	 * Note that we write a shutdown checkpoint rather than an on-line one.
	 * This is not particularly critical, but since we may be assigning a new
	 * TLI, using a shutdown checkpoint allows us to have the rule that TLI
	 * only changes in shutdown checkpoints, which allows some extra error
	 * checking in xlog_redo.
	 *
	 * In promotion, only create a lightweight end-of-recovery record instead
	 * of a full checkpoint. A checkpoint is requested later, after we're
	 * fully out of recovery mode and already accepting queries.
	 */
	if (ArchiveRecoveryRequested && IsUnderPostmaster &&
		PromoteIsTriggered())
	{
		promoted = true;

		/*
		 * Insert a special WAL record to mark the end of recovery, since we
		 * aren't doing a checkpoint. That means that the checkpointer process
		 * may likely be in the middle of a time-smoothed restartpoint and
		 * could continue to be for minutes after this.  That sounds strange,
		 * but the effect is roughly the same and it would be stranger to try
		 * to come out of the restartpoint and then checkpoint. We request a
		 * checkpoint later anyway, just for safety.
		 */
		CreateEndOfRecoveryRecord();
	}
	else
	{
<<<<<<< HEAD
		/*
		 * Check to see if the XLOG sequence contained any unresolved
		 * references to uninitialized pages.
		 */
		XLogCheckInvalidPages();

		/*
		 * POLAR: if in master mode, force check whether some unexpected reused
		 * buffer left when finishing redo, but for standby/archive mode, only
		 * can check at CheckRecoveryConsistency()
		 */
		if (ArchiveRecoveryRequested)
			polar_check_buffer_pool_consistency();
		/* POLAR end */

		reachedConsistency = true;
		ereport(LOG,
				(errmsg("consistent recovery state reached at %X/%X",
						(uint32) (lastReplayedEndRecPtr >> 32),
						(uint32) lastReplayedEndRecPtr)));

		/*
		 * POLAR: Set consistent lsn with last checkpoint redo ptr to avoid going through
		 * all logindex file when consistent ptr is InvalidXLogRecPtr. While going through
		 * all logindex table file, we may meet file-not-exist error.
		 */
		if (polar_in_replica_mode())
		{
			XLogRecPtr redo_rec_ptr;
			TimeLineID redo_tli;

			GetOldestRestartPoint(&redo_rec_ptr, &redo_tli);
			polar_set_primary_consistent_lsn(redo_rec_ptr);
		}
		/* POLAR end */

		elog(LOG, "After reached consistency %lx,%lx", ReadRecPtr, EndRecPtr);
	}

	/*
	 * Have we got a valid starting snapshot that will allow queries to be
	 * run? If so, we can tell postmaster that the database is consistent now,
	 * enabling connections.
	 */
	if (standbyState == STANDBY_SNAPSHOT_READY &&
		!LocalHotStandbyActive &&
		reachedConsistency &&
		IsUnderPostmaster)
	{
		SpinLockAcquire(&XLogCtl->info_lck);
		XLogCtl->SharedHotStandbyActive = true;
		SpinLockRelease(&XLogCtl->info_lck);

		LocalHotStandbyActive = true;

		SendPostmasterSignal(PMSIGNAL_BEGIN_HOT_STANDBY);

		/* POLAR: Wakeup bgwriter and polar_worker to do replay */
		polar_logindex_wakeup_bg_replay(polar_logindex_redo_instance, ReadRecPtr, EndRecPtr);
	}
=======
		RequestCheckpoint(CHECKPOINT_END_OF_RECOVERY |
						  CHECKPOINT_IMMEDIATE |
						  CHECKPOINT_WAIT);
	}

	return promoted;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Is the system still in recovery?
 *
 * Unlike testing InRecovery, this works in any process that's connected to
 * shared memory.
 */
bool
RecoveryInProgress(void)
{
	/*
	 * We check shared state each time only until we leave recovery mode. We
	 * can't re-enter recovery, so there's no need to keep checking after the
	 * shared variable has once been seen false.
	 */
	if (!LocalRecoveryInProgress)
		return false;
	else
	{
		/*
		 * use volatile pointer to make sure we make a fresh read of the
		 * shared variable.
		 */
		volatile XLogCtlData *xlogctl = XLogCtl;

		LocalRecoveryInProgress = (xlogctl->SharedRecoveryState != RECOVERY_STATE_DONE);
<<<<<<< HEAD

		/*
		 * Initialize TimeLineID and RedoRecPtr when we discover that recovery
		 * is finished. InitPostgres() relies upon this behaviour to ensure
		 * that InitXLOGAccess() is called at backend startup.  (If you change
		 * this, see also LocalSetXLogInsertAllowed.)
		 */
		if (!LocalRecoveryInProgress)
		{
			/*
			 * If we just exited recovery, make sure we read TimeLineID and
			 * RedoRecPtr after SharedRecoveryState (for machines with weak
			 * memory ordering).
			 */
			pg_memory_barrier();
			InitXLOGAccess();
		}
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

		/*
		 * Note: We don't need a memory barrier when we're still in recovery.
		 * We might exit recovery immediately after return, so the caller
		 * can't rely on 'true' meaning that we're still in recovery anyway.
		 */

		return LocalRecoveryInProgress;
	}
}

/*
 * Returns current recovery state from shared memory.
<<<<<<< HEAD
 *
 * This returned state is kept consistent with the contents of the control
 * file.  See details about the possible values of RecoveryState in xlog.h.
 */
RecoveryState
GetRecoveryState(void)
{
	RecoveryState retval;

	SpinLockAcquire(&XLogCtl->info_lck);
	retval = XLogCtl->SharedRecoveryState;
	SpinLockRelease(&XLogCtl->info_lck);

	return retval;
}

/*
 * Is HotStandby active yet? This is only important in special backends
 * since normal backends won't ever be able to connect until this returns
 * true. Postmaster knows this by way of signal, not via shared memory.
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 *
 * This returned state is kept consistent with the contents of the control
 * file.  See details about the possible values of RecoveryState in xlog.h.
 */
RecoveryState
GetRecoveryState(void)
{
	RecoveryState retval;

	SpinLockAcquire(&XLogCtl->info_lck);
	retval = XLogCtl->SharedRecoveryState;
	SpinLockRelease(&XLogCtl->info_lck);

	return retval;
}

/*
 * Is this process allowed to insert new WAL records?
 *
 * Ordinarily this is essentially equivalent to !RecoveryInProgress().
 * But we also have provisions for forcing the result "true" or "false"
 * within specific processes regardless of the global state.
 */
bool
XLogInsertAllowed(void)
{
	/*
	 * If value is "unconditionally true" or "unconditionally false", just
	 * return it.  This provides the normal fast path once recovery is known
	 * done.
	 */
	if (LocalXLogInsertAllowed >= 0)
		return (bool) LocalXLogInsertAllowed;

	/*
	 * Else, must check to see if we're still in recovery.
	 */
	if (RecoveryInProgress())
		return false;

	/*
	 * In wal pipeline mode,
	 * we should wait for wal pipeliner ready before write wal log
	 */
	if (POLAR_WAL_PIPELINER_ENABLE())
		while (!POLAR_WAL_PIPELINER_READY());

	/*
	 * On exit from recovery, reset to "unconditionally true", since there is
	 * no need to keep checking.
	 */
	LocalXLogInsertAllowed = 1;
	return true;
}

/*
 * Make XLogInsertAllowed() return true in the current process only.
 *
 * Note: it is allowed to switch LocalXLogInsertAllowed back to -1 later,
 * and even call LocalSetXLogInsertAllowed() again after that.
 *
 * Returns the previous value of LocalXLogInsertAllowed.
 */
static int
LocalSetXLogInsertAllowed(void)
{
	int			oldXLogAllowed = LocalXLogInsertAllowed;

	LocalXLogInsertAllowed = 1;

	return oldXLogAllowed;
}

/*
 * Return the current Redo pointer from shared memory.
 *
 * As a side-effect, the local RedoRecPtr copy is updated.
 */
XLogRecPtr
GetRedoRecPtr(void)
{
	XLogRecPtr	ptr;

	/*
	 * The possibly not up-to-date copy in XlogCtl is enough. Even if we
	 * grabbed a WAL insertion lock to read the authoritative value in
	 * Insert->RedoRecPtr, someone might update it just after we've released
	 * the lock.
	 */
	SpinLockAcquire(&XLogCtl->info_lck);
	ptr = XLogCtl->RedoRecPtr;
	SpinLockRelease(&XLogCtl->info_lck);

	if (RedoRecPtr < ptr)
		RedoRecPtr = ptr;

	return RedoRecPtr;
}

/*
 * Return information needed to decide whether a modified block needs a
 * full-page image to be included in the WAL record.
 *
 * The returned values are cached copies from backend-private memory, and
 * possibly out-of-date or, indeed, uninitialized, in which case they will
 * be InvalidXLogRecPtr and false, respectively.  XLogInsertRecord will
 * re-check them against up-to-date values, while holding the WAL insert lock.
 */
void
GetFullPageWriteInfo(XLogRecPtr *RedoRecPtr_p, bool *doPageWrites_p)
{
	*RedoRecPtr_p = RedoRecPtr;
	*doPageWrites_p = doPageWrites;
}

/*
 * GetInsertRecPtr -- Returns the current insert position.
 *
 * NOTE: The value *actually* returned is the position of the last full
 * xlog page. It lags behind the real insert position by at most 1 page.
 * For that, we don't need to scan through WAL insertion locks, and an
 * approximation is enough for the current usage of this function.
 */
XLogRecPtr
GetInsertRecPtr(void)
{
	XLogRecPtr	recptr;

	SpinLockAcquire(&XLogCtl->info_lck);
	recptr = XLogCtl->LogwrtRqst.Write;
	SpinLockRelease(&XLogCtl->info_lck);

	return recptr;
}

/*
 * GetFlushRecPtr -- Returns the current flush position, ie, the last WAL
 * position known to be fsync'd to disk. This should only be used on a
 * system that is known not to be in recovery.
 */
XLogRecPtr
GetFlushRecPtr(TimeLineID *insertTLI)
{
	Assert(XLogCtl->SharedRecoveryState == RECOVERY_STATE_DONE);

	RefreshXLogWriteResult(LogwrtResult);

	/*
	 * If we're writing and flushing WAL, the time line can't be changing, so
	 * no lock is required.
	 */
	if (insertTLI)
		*insertTLI = XLogCtl->InsertTimeLineID;

	return LogwrtResult.Flush;
}

/*
 * GetWALInsertionTimeLine -- Returns the current timeline of a system that
 * is not in recovery.
 */
TimeLineID
GetWALInsertionTimeLine(void)
{
	Assert(XLogCtl->SharedRecoveryState == RECOVERY_STATE_DONE);

	/* Since the value can't be changing, no lock is required. */
	return XLogCtl->InsertTimeLineID;
}

/*
 * GetWALInsertionTimeLineIfSet -- If the system is not in recovery, returns
 * the WAL insertion timeline; else, returns 0. Wherever possible, use
 * GetWALInsertionTimeLine() instead, since it's cheaper. Note that this
 * function decides recovery has ended as soon as the insert TLI is set, which
 * happens before we set XLogCtl->SharedRecoveryState to RECOVERY_STATE_DONE.
 */
TimeLineID
GetWALInsertionTimeLineIfSet(void)
{
	TimeLineID	insertTLI;

	SpinLockAcquire(&XLogCtl->info_lck);
	insertTLI = XLogCtl->InsertTimeLineID;
	SpinLockRelease(&XLogCtl->info_lck);

	return insertTLI;
}

/*
 * GetLastImportantRecPtr -- Returns the LSN of the last important record
 * inserted. All records not explicitly marked as unimportant are considered
 * important.
 *
 * The LSN is determined by computing the maximum of
 * WALInsertLocks[i].lastImportantAt.
 */
XLogRecPtr
GetLastImportantRecPtr(void)
{
	XLogRecPtr	res = InvalidXLogRecPtr;
	int			i;

	for (i = 0; i < polar_wal_buffer_insert_locks; i++)
	{
		XLogRecPtr	last_important;

		/*
		 * Need to take a lock to prevent torn reads of the LSN, which are
		 * possible on some of the supported platforms. WAL insert locks only
		 * support exclusive mode, so we have to use that.
		 */
		LWLockAcquire(&WALInsertLocks[i].l.lock, LW_EXCLUSIVE);
		last_important = WALInsertLocks[i].l.lastImportantAt;
		LWLockRelease(&WALInsertLocks[i].l.lock);

		if (res < last_important)
			res = last_important;
	}

	return res;
}

/*
 * Get the time and LSN of the last xlog segment switch
 */
pg_time_t
GetLastSegSwitchData(XLogRecPtr *lastSwitchLSN)
{
	pg_time_t	result;

	/* Need WALWriteLock, but shared lock is sufficient */
	LWLockAcquire(WALWriteLock, LW_SHARED);
	result = XLogCtl->lastSegSwitchTime;
	*lastSwitchLSN = XLogCtl->lastSegSwitchLSN;
	LWLockRelease(WALWriteLock);

	return result;
}

/*
 * This must be called ONCE during postmaster or standalone-backend shutdown
 */
void
ShutdownXLOG(int code, Datum arg)
{
	/*
	 * We should have an aux process resource owner to use, and we should not
	 * be in a transaction that's installed some other resowner.
	 */
	Assert(AuxProcessResourceOwner != NULL);
	Assert(CurrentResourceOwner == NULL ||
		   CurrentResourceOwner == AuxProcessResourceOwner);
	CurrentResourceOwner = AuxProcessResourceOwner;

	/* Don't be chatty in standalone mode */
	ereport(IsPostmasterEnvironment ? LOG : NOTICE,
			(errmsg("shutting down")));

	/*
	 * Signal walsenders to move to stopping state.
	 */
	WalSndInitStopping();

	/*
	 * Wait for WAL senders to be in stopping state.  This prevents commands
	 * from writing new WAL.
	 */
	WalSndWaitStopping();

	if (RecoveryInProgress())
		CreateRestartPoint(CHECKPOINT_IS_SHUTDOWN | CHECKPOINT_IMMEDIATE);
	else
	{
		/*
		 * If archiving is enabled, rotate the last XLOG file so that all the
		 * remaining records are archived (postmaster wakes up the archiver
		 * process one more time at the end of shutdown). The checkpoint
		 * record will go to the next XLOG file and won't be archived (yet).
		 */
		if (XLogArchivingActive())
			RequestXLogSwitch(false);

		CreateCheckPoint(CHECKPOINT_IS_SHUTDOWN | CHECKPOINT_IMMEDIATE);
	}
<<<<<<< HEAD
	ShutdownCLOG();
	ShutdownCommitTs();
	ShutdownSUBTRANS();
	ShutdownMultiXact();
	/* POLAR csn */
	polar_csnlog_shutdown();
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Log start of a checkpoint.
 */
static void
LogCheckpointStart(int flags, bool restartpoint)
{
<<<<<<< HEAD
	elog(LOG, "%s starting:%s%s%s%s%s%s%s%s%s%s",
		 restartpoint ? "restartpoint" : "checkpoint",
		 (flags & CHECKPOINT_IS_SHUTDOWN) ? " shutdown" : "",
		 (flags & CHECKPOINT_END_OF_RECOVERY) ? " end-of-recovery" : "",
		 (flags & CHECKPOINT_IMMEDIATE) ? " immediate" : "",
		 (flags & CHECKPOINT_FORCE) ? " force" : "",
		 (flags & CHECKPOINT_WAIT) ? " wait" : "",
		 (flags & CHECKPOINT_CAUSE_XLOG) ? " xlog" : "",
		 (flags & CHECKPOINT_CAUSE_TIME) ? " time" : "",
		 (flags & CHECKPOINT_FLUSH_ALL) ? " flush-all" : "",
		 (flags & CHECKPOINT_LAZY) ? " lazy" : "",
		 (flags & CHECKPOINT_FLASHBACK) ? " flashback" : "");
=======
	if (restartpoint)
		ereport(LOG,
		/* translator: the placeholders show checkpoint options */
				(errmsg("restartpoint starting:%s%s%s%s%s%s%s%s",
						(flags & CHECKPOINT_IS_SHUTDOWN) ? " shutdown" : "",
						(flags & CHECKPOINT_END_OF_RECOVERY) ? " end-of-recovery" : "",
						(flags & CHECKPOINT_IMMEDIATE) ? " immediate" : "",
						(flags & CHECKPOINT_FORCE) ? " force" : "",
						(flags & CHECKPOINT_WAIT) ? " wait" : "",
						(flags & CHECKPOINT_CAUSE_XLOG) ? " wal" : "",
						(flags & CHECKPOINT_CAUSE_TIME) ? " time" : "",
						(flags & CHECKPOINT_FLUSH_ALL) ? " flush-all" : "")));
	else
		ereport(LOG,
		/* translator: the placeholders show checkpoint options */
				(errmsg("checkpoint starting:%s%s%s%s%s%s%s%s",
						(flags & CHECKPOINT_IS_SHUTDOWN) ? " shutdown" : "",
						(flags & CHECKPOINT_END_OF_RECOVERY) ? " end-of-recovery" : "",
						(flags & CHECKPOINT_IMMEDIATE) ? " immediate" : "",
						(flags & CHECKPOINT_FORCE) ? " force" : "",
						(flags & CHECKPOINT_WAIT) ? " wait" : "",
						(flags & CHECKPOINT_CAUSE_XLOG) ? " wal" : "",
						(flags & CHECKPOINT_CAUSE_TIME) ? " time" : "",
						(flags & CHECKPOINT_FLUSH_ALL) ? " flush-all" : "")));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Log end of a checkpoint.
 */
static void
LogCheckpointEnd(bool restartpoint)
{
	long		write_msecs,
				sync_msecs,
				total_msecs,
				longest_msecs,
				average_msecs;
	uint64		average_sync_time;

	CheckpointStats.ckpt_end_t = GetCurrentTimestamp();

	write_msecs = TimestampDifferenceMilliseconds(CheckpointStats.ckpt_write_t,
												  CheckpointStats.ckpt_sync_t);

	sync_msecs = TimestampDifferenceMilliseconds(CheckpointStats.ckpt_sync_t,
												 CheckpointStats.ckpt_sync_end_t);

	/* Accumulate checkpoint timing summary data, in milliseconds. */
	PendingCheckpointerStats.write_time += write_msecs;
	PendingCheckpointerStats.sync_time += sync_msecs;

	/*
	 * All of the published timing statistics are accounted for.  Only
	 * continue if a log message is to be written.
	 */
	if (!log_checkpoints)
		return;

	total_msecs = TimestampDifferenceMilliseconds(CheckpointStats.ckpt_start_t,
												  CheckpointStats.ckpt_end_t);

	/*
	 * Timing values returned from CheckpointStats are in microseconds.
	 * Convert to milliseconds for consistent printing.
	 */
	longest_msecs = (long) ((CheckpointStats.ckpt_longest_sync + 999) / 1000);

	average_sync_time = 0;
	if (CheckpointStats.ckpt_sync_rels > 0)
		average_sync_time = CheckpointStats.ckpt_agg_sync_time /
			CheckpointStats.ckpt_sync_rels;
	average_msecs = (long) ((average_sync_time + 999) / 1000);

	/*
	 * ControlFileLock is not required to see ControlFile->checkPoint and
	 * ->checkPointCopy here as we are the only updator of those variables at
	 * this moment.
	 */
	if (restartpoint)
		ereport(LOG,
				(errmsg("restartpoint complete: wrote %d buffers (%.1f%%); "
						"%d WAL file(s) added, %d removed, %d recycled; "
						"write=%ld.%03d s, sync=%ld.%03d s, total=%ld.%03d s; "
						"sync files=%d, longest=%ld.%03d s, average=%ld.%03d s; "
						"distance=%d kB, estimate=%d kB; "
						"lsn=%X/%X, redo lsn=%X/%X",
						CheckpointStats.ckpt_bufs_written,
						(double) CheckpointStats.ckpt_bufs_written * 100 / NBuffers,
						CheckpointStats.ckpt_segs_added,
						CheckpointStats.ckpt_segs_removed,
						CheckpointStats.ckpt_segs_recycled,
						write_msecs / 1000, (int) (write_msecs % 1000),
						sync_msecs / 1000, (int) (sync_msecs % 1000),
						total_msecs / 1000, (int) (total_msecs % 1000),
						CheckpointStats.ckpt_sync_rels,
						longest_msecs / 1000, (int) (longest_msecs % 1000),
						average_msecs / 1000, (int) (average_msecs % 1000),
						(int) (PrevCheckPointDistance / 1024.0),
						(int) (CheckPointDistanceEstimate / 1024.0),
						LSN_FORMAT_ARGS(ControlFile->checkPoint),
						LSN_FORMAT_ARGS(ControlFile->checkPointCopy.redo))));
	else
		ereport(LOG,
				(errmsg("checkpoint complete: wrote %d buffers (%.1f%%); "
						"%d WAL file(s) added, %d removed, %d recycled; "
						"write=%ld.%03d s, sync=%ld.%03d s, total=%ld.%03d s; "
						"sync files=%d, longest=%ld.%03d s, average=%ld.%03d s; "
						"distance=%d kB, estimate=%d kB; "
						"lsn=%X/%X, redo lsn=%X/%X",
						CheckpointStats.ckpt_bufs_written,
						(double) CheckpointStats.ckpt_bufs_written * 100 / NBuffers,
						CheckpointStats.ckpt_segs_added,
						CheckpointStats.ckpt_segs_removed,
						CheckpointStats.ckpt_segs_recycled,
						write_msecs / 1000, (int) (write_msecs % 1000),
						sync_msecs / 1000, (int) (sync_msecs % 1000),
						total_msecs / 1000, (int) (total_msecs % 1000),
						CheckpointStats.ckpt_sync_rels,
						longest_msecs / 1000, (int) (longest_msecs % 1000),
						average_msecs / 1000, (int) (average_msecs % 1000),
						(int) (PrevCheckPointDistance / 1024.0),
						(int) (CheckPointDistanceEstimate / 1024.0),
						LSN_FORMAT_ARGS(ControlFile->checkPoint),
						LSN_FORMAT_ARGS(ControlFile->checkPointCopy.redo))));
}

/*
 * Update the estimate of distance between checkpoints.
 *
 * The estimate is used to calculate the number of WAL segments to keep
 * preallocated, see XLOGfileslop().
 */
static void
UpdateCheckPointDistanceEstimate(uint64 nbytes)
{
	/*
	 * To estimate the number of segments consumed between checkpoints, keep a
	 * moving average of the amount of WAL generated in previous checkpoint
	 * cycles. However, if the load is bursty, with quiet periods and busy
	 * periods, we want to cater for the peak load. So instead of a plain
	 * moving average, let the average decline slowly if the previous cycle
	 * used less WAL than estimated, but bump it up immediately if it used
	 * more.
	 *
	 * When checkpoints are triggered by max_wal_size, this should converge to
	 * CheckpointSegments * wal_segment_size,
	 *
	 * Note: This doesn't pay any attention to what caused the checkpoint.
	 * Checkpoints triggered manually with CHECKPOINT command, or by e.g.
	 * starting a base backup, are counted the same as those created
	 * automatically. The slow-decline will largely mask them out, if they are
	 * not frequent. If they are frequent, it seems reasonable to count them
	 * in as any others; if you issue a manual checkpoint every 5 minutes and
	 * never let a timed checkpoint happen, it makes sense to base the
	 * preallocation on that 5 minute interval rather than whatever
	 * checkpoint_timeout is set to.
	 */
	PrevCheckPointDistance = nbytes;
	if (CheckPointDistanceEstimate < nbytes)
		CheckPointDistanceEstimate = nbytes;
	else
		CheckPointDistanceEstimate =
			(0.90 * CheckPointDistanceEstimate + 0.10 * (double) nbytes);
}

/*
 * Update the ps display for a process running a checkpoint.  Note that
 * this routine should not do any allocations so as it can be called
 * from a critical section.
 */
static void
update_checkpoint_display(int flags, bool restartpoint, bool reset)
{
	/*
	 * The status is reported only for end-of-recovery and shutdown
	 * checkpoints or shutdown restartpoints.  Updating the ps display is
	 * useful in those situations as it may not be possible to rely on
	 * pg_stat_activity to see the status of the checkpointer or the startup
	 * process.
	 */
	if ((flags & (CHECKPOINT_END_OF_RECOVERY | CHECKPOINT_IS_SHUTDOWN)) == 0)
		return;

	if (reset)
		set_ps_display("");
	else
	{
		char		activitymsg[128];

		snprintf(activitymsg, sizeof(activitymsg), "performing %s%s%s",
				 (flags & CHECKPOINT_END_OF_RECOVERY) ? "end-of-recovery " : "",
				 (flags & CHECKPOINT_IS_SHUTDOWN) ? "shutdown " : "",
				 restartpoint ? "restartpoint" : "checkpoint");
		set_ps_display(activitymsg);
	}
}


/*
 * Perform a checkpoint --- either during shutdown, or on-the-fly
 *
 * flags is a bitwise OR of the following:
 *	CHECKPOINT_IS_SHUTDOWN: checkpoint is for database shutdown.
 *	CHECKPOINT_END_OF_RECOVERY: checkpoint is for end of WAL recovery.
 *	CHECKPOINT_IMMEDIATE: finish the checkpoint ASAP,
 *		ignoring checkpoint_completion_target parameter.
 *	CHECKPOINT_FORCE: force a checkpoint even if no XLOG activity has occurred
 *		since the last one (implied by CHECKPOINT_IS_SHUTDOWN or
 *		CHECKPOINT_END_OF_RECOVERY).
 *	CHECKPOINT_FLUSH_ALL: also flush buffers of unlogged tables.
 *
 * Note: flags contains other bits, of interest here only for logging purposes.
 * In particular note that this routine is synchronous and does not pay
 * attention to CHECKPOINT_WAIT.
 *
 * If !shutdown then we are writing an online checkpoint. An XLOG_CHECKPOINT_REDO
 * record is inserted into WAL at the logical location of the checkpoint, before
 * flushing anything to disk, and when the checkpoint is eventually completed,
 * and it is from this point that WAL replay will begin in the case of a recovery
 * from this checkpoint. Once everything is written to disk, an
 * XLOG_CHECKPOINT_ONLINE record is written to complete the checkpoint, and
 * points back to the earlier XLOG_CHECKPOINT_REDO record. This mechanism allows
 * other write-ahead log records to be written while the checkpoint is in
 * progress, but we must be very careful about order of operations. This function
 * may take many minutes to execute on a busy system.
 *
 * On the other hand, when shutdown is true, concurrent insertion into the
 * write-ahead log is impossible, so there is no need for two separate records.
 * In this case, we only insert an XLOG_CHECKPOINT_SHUTDOWN record, and it's
 * both the record marking the completion of the checkpoint and the location
 * from which WAL replay would begin if needed.
 */
void
CreateCheckPoint(int flags)
{
	bool		shutdown;
	CheckPoint	checkPoint;
	XLogRecPtr	recptr;
	XLogSegNo	_logSegNo;
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	uint32		freespace;
	XLogRecPtr	PriorRedoPtr;
	XLogRecPtr	last_important_lsn;
	VirtualTransactionId *vxids;
	int			nvxids;
	int			oldXLogAllowed = 0;

	/* POLAR */
	XLogRecPtr	polar_last_lsn;
	bool		polar_is_lazy;
	XLogRecPtr 	polar_lazy_redo = InvalidXLogRecPtr;
	/* POLAR csn */
	TransactionId oldest_active_xid = InvalidTransactionId;

	/* POLAR: The flashback log ptr of the checkpoint start */
	polar_flog_rec_ptr flog_ptr_ckp_start = POLAR_INVALID_FLOG_REC_PTR;
	bool		is_flashback_point = false;
	bool		is_online_promote = false;
	flashback_snapshot_header_t    fbpoint_snapshot = NULL;

	/*
	 * POLAR: Don't do checkpoint during online promote when background process
	 * hasn't start to replay. Otherwise we may set polar_max_valid_lsn() as consistent lsn,
	 * and later we set polar_online_promote_fake_oldest_lsn() as consistent lsn, which
	 * is smaller than previous set
	 */
	if (polar_get_bg_redo_state(polar_logindex_redo_instance) == POLAR_BG_WAITING_RESET)
		return;

	is_online_promote = (polar_get_bg_redo_state(polar_logindex_redo_instance) == POLAR_BG_ONLINE_PROMOTE);

	/*
	 * An end-of-recovery checkpoint is really a shutdown checkpoint, just
	 * issued at a different time.
	 */
	if (flags & (CHECKPOINT_IS_SHUTDOWN | CHECKPOINT_END_OF_RECOVERY))
		shutdown = true;
	else
		shutdown = false;

#ifdef FAULT_INJECTOR
	if (SIMPLE_FAULT_INJECTOR("checkpoint") == FaultInjectorTypeSkip)
		return;
#endif

	/* sanity check */
	if (RecoveryInProgress() && (flags & CHECKPOINT_END_OF_RECOVERY) == 0)
		elog(ERROR, "can't create a checkpoint during recovery");

	/*
	 * Prepare to accumulate statistics.
	 *
	 * Note: because it is possible for log_checkpoints to change while a
	 * checkpoint proceeds, we always accumulate stats, even if
	 * log_checkpoints is currently off.
	 */
	MemSet(&CheckpointStats, 0, sizeof(CheckpointStats));
	CheckpointStats.ckpt_start_t = GetCurrentTimestamp();

	/* POLAR: Is a flashback point? */
	is_flashback_point = !is_online_promote &&
			polar_is_flashback_point(flog_instance, GetXLogInsertRecPtr(),
					InvalidXLogRecPtr, &flags, false);

	/*
	 * POLAR: get current flashback log ptr of the flashback point begining.
	 *
	 * NB: We don't need a precise value, but a little earlier value.
	 * When we search a right flashback log in logindex, sometime we will get
	 * two right flashback log.
	 */
	if (is_flashback_point)
	{
		flog_ptr_ckp_start = polar_get_flog_write_result(flog_instance->buf_ctl);
		/* Get the current snapshot for flashback table */
		fbpoint_snapshot = polar_get_flashback_snapshot_data(fra_instance, GetXLogInsertRecPtr());
	}

	/*
	 * Let smgr prepare for checkpoint; this has to happen outside the
	 * critical section and before we determine the REDO pointer.  Note that
	 * smgr must not do anything that'd have to be undone if we decide no
	 * checkpoint is needed.
	 */
	SyncPreCheckpoint();

	/*
	 * Use a critical section to force system panic if we have trouble.
	 */
	START_CRIT_SECTION();

	/* POLAR: check whether we can use lazy checkpoint. */
	polar_is_lazy = !is_flashback_point && polar_check_lazy_checkpoint(shutdown, &flags, &polar_lazy_redo);

	if (shutdown)
	{
		LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
		ControlFile->state = DB_SHUTDOWNING;
		UpdateControlFile();
		LWLockRelease(ControlFileLock);
	}

	/* Begin filling in the checkpoint WAL record */
	MemSet(&checkPoint, 0, sizeof(checkPoint));
	checkPoint.time = (pg_time_t) time(NULL);

	/*
	 * For Hot Standby, derive the oldestActiveXid before we fix the redo
	 * pointer. This allows us to begin accumulating changes to assemble our
	 * starting snapshot of locks and transactions.
	 */
	if (!shutdown && XLogStandbyInfoActive())
		checkPoint.oldestActiveXid = GetOldestActiveTransactionId();
	else
		checkPoint.oldestActiveXid = InvalidTransactionId;

	/*
	 * POLAR csn
	 * Record polar_oldest_active_xid before checkpoint redo point,
	 * we should make sure truncate csnlog with xid before redo point.
	 */
	if (polar_csn_enable)
		oldest_active_xid = pg_atomic_read_u32(&polar_shmem_csn_mvcc_var_cache->polar_oldest_active_xid);

	/*
	 * Get location of last important record before acquiring insert locks (as
	 * GetLastImportantRecPtr() also locks WAL locks).
	 */
	last_important_lsn = GetLastImportantRecPtr();

	/*
<<<<<<< HEAD
	 * We must block concurrent insertions while examining insert state to
	 * determine the checkpoint REDO pointer.
	 */
	WALInsertLockAcquireExclusive();
	curInsert = XLogBytePosToRecPtr(Insert->CurrBytePos);

	/*
	 * POLAR: we store the end of last record for shutdown checkpoint. When
	 * call polar_flush_buffer_for_shutdown, we will flush wal that is small
	 * than polar_last_lsn. We do not use XLogBytePosToRecPtr, because it will
	 * contain the page header if the position is at a page boundary. We do
	 * not use checkPoint.redo, because it may be added some * extra ***PHD
	 * info, that will cause the XLogFlush raise a FATAL, like 'xlog flush
	 * request ... is not satisfied --- flushed only to ...'.
	 */
	polar_last_lsn = XLogBytePosToEndRecPtr(Insert->CurrBytePos);

	/* POLAR: use lazy redo lsn as the checkpoint redo. */
	if (polar_is_lazy)
	{
		Assert(!is_flashback_point);
		Assert(!shutdown);
		Assert(flags & CHECKPOINT_LAZY);
		Assert(!XLogRecPtrIsInvalid(polar_lazy_redo));
		curInsert = polar_lazy_redo;
	}

	/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 * If this isn't a shutdown or forced checkpoint, and if there has been no
	 * WAL activity requiring a checkpoint, skip it.  The idea here is to
	 * avoid inserting duplicate checkpoints when the system is idle.
	 */
	if ((flags & (CHECKPOINT_IS_SHUTDOWN | CHECKPOINT_END_OF_RECOVERY |
				  CHECKPOINT_FORCE)) == 0)
	{
		if (last_important_lsn == ControlFile->checkPoint)
		{
			END_CRIT_SECTION();
			ereport(DEBUG1,
					(errmsg_internal("checkpoint skipped because system is idle")));
			return;
		}
	}

	/*
	 * An end-of-recovery checkpoint is created before anyone is allowed to
	 * write WAL. To allow us to write the checkpoint record, temporarily
	 * enable XLogInsertAllowed.
	 */
	if (flags & CHECKPOINT_END_OF_RECOVERY)
		oldXLogAllowed = LocalSetXLogInsertAllowed();

	checkPoint.ThisTimeLineID = XLogCtl->InsertTimeLineID;
	if (flags & CHECKPOINT_END_OF_RECOVERY)
		checkPoint.PrevTimeLineID = XLogCtl->PrevTimeLineID;
	else
		checkPoint.PrevTimeLineID = checkPoint.ThisTimeLineID;

	/*
	 * We must block concurrent insertions while examining insert state.
	 */
	WALInsertLockAcquireExclusive();

	checkPoint.fullPageWrites = Insert->fullPageWrites;
	checkPoint.wal_level = wal_level;

	if (shutdown)
	{
		XLogRecPtr	curInsert = XLogBytePosToRecPtr(Insert->CurrBytePos);

<<<<<<< HEAD
	/* POLAR: We must set flashback point info in here to avoid to any origin page lost */
	if (is_flashback_point)
		polar_set_fbpoint_wal_info(flog_instance->buf_ctl, checkPoint.redo, checkPoint.time, InvalidXLogRecPtr, false);

	/*
	 * Here we update the shared RedoRecPtr for future XLogInsert calls; this
	 * must be done while holding all the insertion locks.
	 *
	 * Note: if we fail to complete the checkpoint, RedoRecPtr will be left
	 * pointing past where it really needs to point.  This is okay; the only
	 * consequence is that XLogInsert might back up whole buffers that it
	 * didn't really need to.  We can't postpone advancing RedoRecPtr because
	 * XLogInserts that happen while we are dumping buffers must assume that
	 * their buffer changes are not included in the checkpoint.
	 */
	RedoRecPtr = XLogCtl->Insert.RedoRecPtr = checkPoint.redo;
=======
		/*
		 * Compute new REDO record ptr = location of next XLOG record.
		 *
		 * Since this is a shutdown checkpoint, there can't be any concurrent
		 * WAL insertion.
		 */
		freespace = INSERT_FREESPACE(curInsert);
		if (freespace == 0)
		{
			if (XLogSegmentOffset(curInsert, wal_segment_size) == 0)
				curInsert += SizeOfXLogLongPHD;
			else
				curInsert += SizeOfXLogShortPHD;
		}
		checkPoint.redo = curInsert;

		/*
		 * Here we update the shared RedoRecPtr for future XLogInsert calls;
		 * this must be done while holding all the insertion locks.
		 *
		 * Note: if we fail to complete the checkpoint, RedoRecPtr will be
		 * left pointing past where it really needs to point.  This is okay;
		 * the only consequence is that XLogInsert might back up whole buffers
		 * that it didn't really need to.  We can't postpone advancing
		 * RedoRecPtr because XLogInserts that happen while we are dumping
		 * buffers must assume that their buffer changes are not included in
		 * the checkpoint.
		 */
		RedoRecPtr = XLogCtl->Insert.RedoRecPtr = checkPoint.redo;
	}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Now we can release the WAL insertion locks, allowing other xacts to
	 * proceed while we are flushing disk buffers.
	 */
	WALInsertLockRelease();

	/*
	 * If this is an online checkpoint, we have not yet determined the redo
	 * point. We do so now by inserting the special XLOG_CHECKPOINT_REDO
	 * record; the LSN at which it starts becomes the new redo pointer. We
	 * don't do this for a shutdown checkpoint, because in that case no WAL
	 * can be written between the redo point and the insertion of the
	 * checkpoint record itself, so the checkpoint record itself serves to
	 * mark the redo point.
	 */
	if (!shutdown)
	{
		/* Include WAL level in record for WAL summarizer's benefit. */
		XLogBeginInsert();
		XLogRegisterData((char *) &wal_level, sizeof(wal_level));
		(void) XLogInsert(RM_XLOG_ID, XLOG_CHECKPOINT_REDO);

		/*
		 * XLogInsertRecord will have updated XLogCtl->Insert.RedoRecPtr in
		 * shared memory and RedoRecPtr in backend-local memory, but we need
		 * to copy that into the record that will be inserted when the
		 * checkpoint is complete.
		 */
		checkPoint.redo = RedoRecPtr;
	}

	/* Update the info_lck-protected copy of RedoRecPtr as well */
	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->RedoRecPtr = checkPoint.redo;
	SpinLockRelease(&XLogCtl->info_lck);

	/*
	 * If enabled, log checkpoint start.  We postpone this until now so as not
	 * to log anything if we decided to skip the checkpoint.
	 */
	if (log_checkpoints)
		LogCheckpointStart(flags, false);

	/* Update the process title */
	update_checkpoint_display(flags, false, false);

	TRACE_POSTGRESQL_CHECKPOINT_START(flags);

	/*
	 * Get the other info we need for the checkpoint record.
	 *
	 * We don't need to save oldestClogXid in the checkpoint, it only matters
	 * for the short period in which clog is being truncated, and if we crash
	 * during that we'll redo the clog truncation and fix up oldestClogXid
	 * there.
	 */
	LWLockAcquire(XidGenLock, LW_SHARED);
	checkPoint.nextXid = TransamVariables->nextXid;
	checkPoint.oldestXid = TransamVariables->oldestXid;
	checkPoint.oldestXidDB = TransamVariables->oldestXidDB;
	LWLockRelease(XidGenLock);

	LWLockAcquire(CommitTsLock, LW_SHARED);
	checkPoint.oldestCommitTsXid = TransamVariables->oldestCommitTsXid;
	checkPoint.newestCommitTsXid = TransamVariables->newestCommitTsXid;
	LWLockRelease(CommitTsLock);

	LWLockAcquire(OidGenLock, LW_SHARED);
	checkPoint.nextOid = TransamVariables->nextOid;
	if (!shutdown)
		checkPoint.nextOid += TransamVariables->oidCount;
	LWLockRelease(OidGenLock);

	MultiXactGetCheckptMulti(shutdown,
							 &checkPoint.nextMulti,
							 &checkPoint.nextMultiOffset,
							 &checkPoint.oldestMulti,
							 &checkPoint.oldestMultiDB);

	/*
	 * Having constructed the checkpoint record, ensure all shmem disk buffers
	 * and commit-log buffers are flushed to disk.
	 *
	 * This I/O could fail for various reasons.  If so, we will fail to
	 * complete the checkpoint, but there is no reason to force a system
	 * panic. Accordingly, exit critical section while doing it.
	 */
	END_CRIT_SECTION();

	/*
	 * In some cases there are groups of actions that must all occur on one
	 * side or the other of a checkpoint record. Before flushing the
	 * checkpoint record we must explicitly wait for any backend currently
	 * performing those groups of actions.
	 *
	 * One example is end of transaction, so we must wait for any transactions
	 * that are currently in commit critical sections.  If an xact inserted
	 * its commit record into XLOG just before the REDO point, then a crash
	 * restart from the REDO point would not replay that record, which means
	 * that our flushing had better include the xact's update of pg_xact.  So
	 * we wait till he's out of his commit critical section before proceeding.
	 * See notes in RecordTransactionCommit().
	 *
	 * Because we've already released the insertion locks, this test is a bit
	 * fuzzy: it is possible that we will wait for xacts we didn't really need
	 * to wait for.  But the delay should be short and it seems better to make
	 * checkpoint take a bit longer than to hold off insertions longer than
	 * necessary. (In fact, the whole reason we have this issue is that xact.c
	 * does commit record XLOG insertion and clog update as two separate steps
	 * protected by different locks, but again that seems best on grounds of
	 * minimizing lock contention.)
	 *
	 * A transaction that has not yet set delayChkptFlags when we look cannot
	 * be at risk, since it has not inserted its commit record yet; and one
	 * that's already cleared it is not at risk either, since it's done fixing
	 * clog and we will correctly flush the update below.  So we cannot miss
	 * any xacts we need to wait for.
	 */
	vxids = GetVirtualXIDsDelayingChkpt(&nvxids, DELAY_CHKPT_START);
	if (nvxids > 0)
	{
		do
		{
			/*
			 * Keep absorbing fsync requests while we wait. There could even
			 * be a deadlock if we don't, if the process that prevents the
			 * checkpoint is trying to add a request to the queue.
			 */
			AbsorbSyncRequests();

			pgstat_report_wait_start(WAIT_EVENT_CHECKPOINT_DELAY_START);
			pg_usleep(10000L);	/* wait for 10 msec */
			pgstat_report_wait_end();
		} while (HaveVirtualXIDsDelayingChkpt(vxids, nvxids,
											  DELAY_CHKPT_START));
	}
	pfree(vxids);

	CheckPointGuts(checkPoint.redo, flags);

<<<<<<< HEAD
	/* POLAR: check whether we can create a checkpoint */
	if (polar_should_check_checkpoint())
	{
		if (flags & CHECKPOINT_IS_SHUTDOWN)
			polar_flush_buffer_for_shutdown(polar_last_lsn, flags);
		else
			polar_wait_consistent_lsn(checkPoint.redo, flags);
	}
=======
	vxids = GetVirtualXIDsDelayingChkpt(&nvxids, DELAY_CHKPT_COMPLETE);
	if (nvxids > 0)
	{
		do
		{
			AbsorbSyncRequests();

			pgstat_report_wait_start(WAIT_EVENT_CHECKPOINT_DELAY_COMPLETE);
			pg_usleep(10000L);	/* wait for 10 msec */
			pgstat_report_wait_end();
		} while (HaveVirtualXIDsDelayingChkpt(vxids, nvxids,
											  DELAY_CHKPT_COMPLETE));
	}
	pfree(vxids);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Take a snapshot of running transactions and write this to WAL. This
	 * allows us to reconstruct the state of running transactions during
	 * archive recovery, if required. Skip, if this info disabled.
	 *
	 * If we are shutting down, or Startup process is completing crash
	 * recovery we don't need to write running xact data.
	 */
	if (!shutdown && XLogStandbyInfoActive())
		LogStandbySnapshot();

	START_CRIT_SECTION();

	/*
	 * Now insert the checkpoint record into XLOG.
	 */
	XLogBeginInsert();
	XLogRegisterData((char *) (&checkPoint), sizeof(checkPoint));
	recptr = XLogInsert(RM_XLOG_ID,
						shutdown ? XLOG_CHECKPOINT_SHUTDOWN :
						XLOG_CHECKPOINT_ONLINE);

	XLogFlush(recptr);

	if (polar_is_dma_data_node() && !ConsensusCheckpoint())
		ereport(PANIC, (errmsg("consensus checkpoint failed")));

	/*
	 * We mustn't write any new WAL after a shutdown checkpoint, or it will be
	 * overwritten at next startup.  No-one should even try, this just allows
	 * sanity-checking.  In the case of an end-of-recovery checkpoint, we want
	 * to just temporarily disable writing until the system has exited
	 * recovery.
	 */
	if (shutdown)
	{
		if (flags & CHECKPOINT_END_OF_RECOVERY)
			LocalXLogInsertAllowed = oldXLogAllowed;
		else
			LocalXLogInsertAllowed = 0; /* never again write WAL */
	}

	/*
	 * We now have ProcLastRecPtr = start of actual checkpoint record, recptr
	 * = end of actual checkpoint record.
	 */
	if (shutdown && checkPoint.redo != ProcLastRecPtr)
		ereport(PANIC,
				(errmsg("concurrent write-ahead log activity while database system is shutting down")));

	/*
	 * Remember the prior checkpoint's redo ptr for
	 * UpdateCheckPointDistanceEstimate()
	 */
	PriorRedoPtr = ControlFile->checkPointCopy.redo;

	/*
	 * Update the control file.
	 */
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	if (shutdown)
		ControlFile->state = DB_SHUTDOWNED;
	ControlFile->checkPoint = ProcLastRecPtr;
	ControlFile->checkPointCopy = checkPoint;
	/* crash recovery should always recover to the end of WAL */
	ControlFile->minRecoveryPoint = InvalidXLogRecPtr;
	ControlFile->minRecoveryPointTLI = 0;

	/*
	 * Persist unloggedLSN value. It's reset on crash recovery, so this goes
	 * unused on non-shutdown checkpoints, but seems useful to store it always
	 * for debugging purposes.
	 */
	ControlFile->unloggedLSN = pg_atomic_read_membarrier_u64(&XLogCtl->unloggedLSN);

	UpdateControlFile();
	LWLockRelease(ControlFileLock);

	/* Update shared-memory copy of checkpoint XID/epoch */
	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->ckptFullXid = checkPoint.nextXid;
	SpinLockRelease(&XLogCtl->info_lck);

	/*
	 * We are now done with critical updates; no need for system panic if we
	 * have trouble while fooling with old log segments.
	 */
	END_CRIT_SECTION();

	/*
<<<<<<< HEAD
	 * POLAR: Do something after the flashback point is done:
	 * 1. Flush the flashback data.
	 * 2. remove the old flashback data.
	 */
	if (is_flashback_point)
		polar_do_flashback_point(flog_ptr_ckp_start, fbpoint_snapshot, shutdown);

	/* POLAR: record checkpoint ptr when checkpoint finished */
	polar_buffer_pool_ctl_set_last_checkpoint_lsn(ControlFile->checkPoint);
	/* POLAR end */

	/*
	 * Let smgr do post-checkpoint cleanup (eg, deleting old files).
=======
	 * WAL summaries end when the next XLOG_CHECKPOINT_REDO or
	 * XLOG_CHECKPOINT_SHUTDOWN record is reached. This is the first point
	 * where (a) we're not inside of a critical section and (b) we can be
	 * certain that the relevant record has been flushed to disk, which must
	 * happen before it can be summarized.
	 *
	 * If this is a shutdown checkpoint, then this happens reasonably
	 * promptly: we've only just inserted and flushed the
	 * XLOG_CHECKPOINT_SHUTDOWN record. If this is not a shutdown checkpoint,
	 * then this might not be very prompt at all: the XLOG_CHECKPOINT_REDO
	 * record was written before we began flushing data to disk, and that
	 * could be many minutes ago at this point. However, we don't XLogFlush()
	 * after inserting that record, so we're not guaranteed that it's on disk
	 * until after the above call that flushes the XLOG_CHECKPOINT_ONLINE
	 * record.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 */
	SetWalSummarizerLatch();

	/*
<<<<<<< HEAD
=======
	 * Let smgr do post-checkpoint cleanup (eg, deleting old files).
	 */
	SyncPostCheckpoint();

	/*
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 * Update the average distance between checkpoints if the prior checkpoint
	 * exists.
	 */
	if (PriorRedoPtr != InvalidXLogRecPtr)
		UpdateCheckPointDistanceEstimate(RedoRecPtr - PriorRedoPtr);

<<<<<<< HEAD
	/* POLAR: Truncate logindex before removing wal files. Files may saved in local file system, like relation size cache. */
	polar_logindex_remove_old_files(polar_logindex_redo_instance);

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	/*
	 * Delete old log files, those no longer needed for last checkpoint to
	 * prevent the disk holding the xlog from growing full.
	 */
	XLByteToSeg(RedoRecPtr, _logSegNo, wal_segment_size);
	KeepLogSeg(recptr, &_logSegNo);
<<<<<<< HEAD
	InvalidateObsoleteReplicationSlots(_logSegNo);
	_logSegNo--;
	RemoveOldXlogFiles(_logSegNo, RedoRecPtr, recptr);
=======
	if (InvalidateObsoleteReplicationSlots(RS_INVAL_WAL_REMOVED,
										   _logSegNo, InvalidOid,
										   InvalidTransactionId))
	{
		/*
		 * Some slots have been invalidated; recalculate the old-segment
		 * horizon, starting again from RedoRecPtr.
		 */
		XLByteToSeg(RedoRecPtr, _logSegNo, wal_segment_size);
		KeepLogSeg(recptr, &_logSegNo);
	}
	_logSegNo--;
	RemoveOldXlogFiles(_logSegNo, RedoRecPtr, recptr,
					   checkPoint.ThisTimeLineID);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Make more log segments if needed.  (Do this after recycling old log
	 * segments, since that may supply some of the needed files.)
	 */
	if (!shutdown)
		PreallocXlogFiles(recptr, checkPoint.ThisTimeLineID);

	/*
	 * Truncate pg_subtrans if possible.  We can throw away all data before
	 * the oldest XMIN of any running transaction.  No future transaction will
	 * attempt to reference any pg_subtrans entry older than that (see Asserts
	 * in subtrans.c).  During recovery, though, we mustn't do this because
	 * StartupSUBTRANS hasn't been called yet.
	 *
	 * POLAR csn
	 * CSNLog is larger than Clog in disk size, we want to truncate csnlog
	 * as soon as possible.
	 * Clog truncate in vacuum frozen time, but we want CSNLog truncate in
	 * checkpoint time
	 */
	if (!RecoveryInProgress())
<<<<<<< HEAD
	{
		if (polar_csn_enable)
		{
			/* 
			 * We try to truncate csnlog to reduce csnlog space, but
		 	 * when it's shutdown checkpoint, we can't truncate csnlog,
		 	 * because csnlog truncate need write wal
		 	 */
			if (!shutdown)
			{
				TransactionId truncate_xid = GetOldestXmin(NULL, PROCARRAY_FLAGS_DEFAULT);
			
				/* Make sure truncate csnlog with xid less than polar_oldest_active_xid at redo point */
				if (TransactionIdPrecedes(oldest_active_xid, truncate_xid))
					truncate_xid = oldest_active_xid;

				polar_csnlog_truncate(truncate_xid);
			}
		}
		else
			TruncateSUBTRANS(GetOldestXmin(NULL, PROCARRAY_FLAGS_DEFAULT));
	}
=======
		TruncateSUBTRANS(GetOldestTransactionIdConsideredRunning());
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* Real work is done; log and update stats. */
	LogCheckpointEnd(false);

	/* Reset the process title */
	update_checkpoint_display(flags, false, true);

	TRACE_POSTGRESQL_CHECKPOINT_DONE(CheckpointStats.ckpt_bufs_written,
									 NBuffers,
									 CheckpointStats.ckpt_segs_added,
									 CheckpointStats.ckpt_segs_removed,
									 CheckpointStats.ckpt_segs_recycled);
}

/*
 * Mark the end of recovery in WAL though without running a full checkpoint.
 * We can expect that a restartpoint is likely to be in progress as we
 * do this, though we are unwilling to wait for it to complete.
 *
 * CreateRestartPoint() allows for the case where recovery may end before
 * the restartpoint completes so there is no concern of concurrent behaviour.
 */
static void
CreateEndOfRecoveryRecord(void)
{
	xl_end_of_recovery xlrec;
	XLogRecPtr	recptr;

	/* sanity check */
	if (!RecoveryInProgress())
		elog(ERROR, "can only be used to end recovery");

	xlrec.end_time = GetCurrentTimestamp();
	xlrec.wal_level = wal_level;

	WALInsertLockAcquireExclusive();
	xlrec.ThisTimeLineID = XLogCtl->InsertTimeLineID;
	xlrec.PrevTimeLineID = XLogCtl->PrevTimeLineID;
	WALInsertLockRelease();

	START_CRIT_SECTION();

	XLogBeginInsert();
	XLogRegisterData((char *) &xlrec, sizeof(xl_end_of_recovery));
	recptr = XLogInsert(RM_XLOG_ID, XLOG_END_OF_RECOVERY);

	XLogFlush(recptr);

	/*
	 * Update the control file so that crash recovery can follow the timeline
	 * changes to this point.
	 */
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	ControlFile->minRecoveryPoint = recptr;
	ControlFile->minRecoveryPointTLI = xlrec.ThisTimeLineID;
	UpdateControlFile();
	LWLockRelease(ControlFileLock);

	END_CRIT_SECTION();
}

/*
 * Write an OVERWRITE_CONTRECORD message.
 *
 * When on WAL replay we expect a continuation record at the start of a page
 * that is not there, recovery ends and WAL writing resumes at that point.
 * But it's wrong to resume writing new WAL back at the start of the record
 * that was broken, because downstream consumers of that WAL (physical
 * replicas) are not prepared to "rewind".  So the first action after
 * finishing replay of all valid WAL must be to write a record of this type
 * at the point where the contrecord was missing; to support xlogreader
 * detecting the special case, XLP_FIRST_IS_OVERWRITE_CONTRECORD is also added
 * to the page header where the record occurs.  xlogreader has an ad-hoc
 * mechanism to report metadata about the broken record, which is what we
 * use here.
 *
 * At replay time, XLP_FIRST_IS_OVERWRITE_CONTRECORD instructs xlogreader to
 * skip the record it was reading, and pass back the LSN of the skipped
 * record, so that its caller can verify (on "replay" of that record) that the
 * XLOG_OVERWRITE_CONTRECORD matches what was effectively overwritten.
 *
 * 'aborted_lsn' is the beginning position of the record that was incomplete.
 * It is included in the WAL record.  'pagePtr' and 'newTLI' point to the
 * beginning of the XLOG page where the record is to be inserted.  They must
 * match the current WAL insert position, they're passed here just so that we
 * can verify that.
 */
static XLogRecPtr
CreateOverwriteContrecordRecord(XLogRecPtr aborted_lsn, XLogRecPtr pagePtr,
								TimeLineID newTLI)
{
	xl_overwrite_contrecord xlrec;
	XLogRecPtr	recptr;
	XLogPageHeader pagehdr;
	XLogRecPtr	startPos;

	/* sanity checks */
	if (!RecoveryInProgress())
		elog(ERROR, "can only be used at end of recovery");
	if (pagePtr % XLOG_BLCKSZ != 0)
		elog(ERROR, "invalid position for missing continuation record %X/%X",
			 LSN_FORMAT_ARGS(pagePtr));

	/* The current WAL insert position should be right after the page header */
	startPos = pagePtr;
	if (XLogSegmentOffset(startPos, wal_segment_size) == 0)
		startPos += SizeOfXLogLongPHD;
	else
		startPos += SizeOfXLogShortPHD;
	recptr = GetXLogInsertRecPtr();
	if (recptr != startPos)
		elog(ERROR, "invalid WAL insert position %X/%X for OVERWRITE_CONTRECORD",
			 LSN_FORMAT_ARGS(recptr));

	START_CRIT_SECTION();

	/*
	 * Initialize the XLOG page header (by GetXLogBuffer), and set the
	 * XLP_FIRST_IS_OVERWRITE_CONTRECORD flag.
	 *
	 * No other backend is allowed to write WAL yet, so acquiring the WAL
	 * insertion lock is just pro forma.
	 */
	WALInsertLockAcquire();
	pagehdr = (XLogPageHeader) GetXLogBuffer(pagePtr, newTLI);
	pagehdr->xlp_info |= XLP_FIRST_IS_OVERWRITE_CONTRECORD;
	WALInsertLockRelease();

	/*
	 * Insert the XLOG_OVERWRITE_CONTRECORD record as the first record on the
	 * page.  We know it becomes the first record, because no other backend is
	 * allowed to write WAL yet.
	 */
	XLogBeginInsert();
	xlrec.overwritten_lsn = aborted_lsn;
	xlrec.overwrite_time = GetCurrentTimestamp();
	XLogRegisterData((char *) &xlrec, sizeof(xl_overwrite_contrecord));
	recptr = XLogInsert(RM_XLOG_ID, XLOG_OVERWRITE_CONTRECORD);

	/* check that the record was inserted to the right place */
	if (ProcLastRecPtr != startPos)
		elog(ERROR, "OVERWRITE_CONTRECORD was inserted to unexpected position %X/%X",
			 LSN_FORMAT_ARGS(ProcLastRecPtr));

	XLogFlush(recptr);

	END_CRIT_SECTION();

	return recptr;
}

/*
 * Flush all data in shared memory to disk, and fsync
 *
 * This is the common code shared between regular checkpoints and
 * recovery restartpoints.
 */
static void
CheckPointGuts(XLogRecPtr checkPointRedo, int flags)
{
<<<<<<< HEAD
	/* in shared storage */
	/*
	 * POLAR: Flush logindex table which table's max lsn is
	 * smaller than checkpoint
	 */
	polar_logindex_redo_flush_data(polar_logindex_redo_instance, checkPointRedo);

=======
	CheckPointRelationMap();
	CheckPointReplicationSlots(flags & CHECKPOINT_IS_SHUTDOWN);
	CheckPointSnapBuild();
	CheckPointLogicalRewriteHeap();
	CheckPointReplicationOrigin();

	/* Write out all dirty data in SLRUs and the main buffer pool */
	TRACE_POSTGRESQL_BUFFER_CHECKPOINT_START(flags);
	CheckpointStats.ckpt_write_t = GetCurrentTimestamp();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	CheckPointCLOG();
	CheckPointCommitTs();
	if (polar_csn_enable)
		polar_csnlog_checkpoint();
	else
		CheckPointSUBTRANS();
	CheckPointMultiXact();

	/* in local disk */
	CheckPointSUBTRANS();
	CheckPointPredicate();
<<<<<<< HEAD
	CheckPointRelationMap();
	CheckPointReplicationSlots();
	CheckPointSnapBuild();
	CheckPointLogicalRewriteHeap();

	CheckPointBuffers(flags);

	CheckPointReplicationOrigin();
=======
	CheckPointBuffers(flags);

	/* Perform all queued up fsyncs */
	TRACE_POSTGRESQL_BUFFER_CHECKPOINT_SYNC_START();
	CheckpointStats.ckpt_sync_t = GetCurrentTimestamp();
	ProcessSyncRequests();
	CheckpointStats.ckpt_sync_end_t = GetCurrentTimestamp();
	TRACE_POSTGRESQL_BUFFER_CHECKPOINT_DONE();

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	/* We deliberately delay 2PC checkpointing as long as possible */
	CheckPointTwoPhase(checkPointRedo);

	/*
	 * POLAR: Flashback log do checkpoint.
	 * Now just set flog_instance->buf_ctl->redo_lsn to checkPointRedo.
	 */
	POLAR_CHECK_POINT_FLOG(flog_instance, checkPointRedo);
}

/*
 * Save a checkpoint for recovery restart if appropriate
 *
 * This function is called each time a checkpoint record is read from XLOG.
 * It must determine whether the checkpoint represents a safe restartpoint or
 * not.  If so, the checkpoint record is stashed in shared memory so that
 * CreateRestartPoint can consult it.  (Note that the latter function is
 * executed by the checkpointer, while this one will be executed by the
 * startup process.)
 */
static void
RecoveryRestartPoint(const CheckPoint *checkPoint, XLogReaderState *record)
{
	/*
	 * Also refrain from creating a restartpoint if we have seen any
	 * references to non-existent pages. Restarting recovery from the
	 * restartpoint would not see the references, so we would lose the
	 * cross-check that the pages belonged to a relation that was dropped
	 * later.
	 */
	if (XLogHaveInvalidPages())
	{
		elog(DEBUG2,
			 "could not record restart point at %X/%X because there "
			 "are unresolved references to invalid pages",
			 LSN_FORMAT_ARGS(checkPoint->redo));
		return;
	}

	/*
	 * Copy the checkpoint record to shared memory, so that checkpointer can
	 * work out the next time it wants to perform a restartpoint.
	 */
	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->lastCheckPointRecPtr = record->ReadRecPtr;
	XLogCtl->lastCheckPointEndPtr = record->EndRecPtr;
	XLogCtl->lastCheckPoint = *checkPoint;
	SpinLockRelease(&XLogCtl->info_lck);

	if (POLAR_IN_PARALLEL_REPLAY_STANDBY_MODE(polar_logindex_redo_instance))
	{
		XLogRecPtr replayed = polar_bg_redo_get_replayed_lsn(polar_logindex_redo_instance);

		polar_checkpoint_ringbuf_shrink(&XLogCtl->polar_checkpoint_ringbuf, replayed);
		polar_checkpoint_ringbuf_insert(&XLogCtl->polar_checkpoint_ringbuf, ReadRecPtr, EndRecPtr, checkPoint);
	}
}

/*
 * Establish a restartpoint if possible.
 *
 * This is similar to CreateCheckPoint, but is used during WAL recovery
 * to establish a point from which recovery can roll forward without
 * replaying the entire recovery log.
 *
 * Returns true if a new restartpoint was established. We can only establish
 * a restartpoint if we have replayed a safe checkpoint record since last
 * restartpoint.
 */
bool
CreateRestartPoint(int flags)
{
	XLogRecPtr	lastCheckPointRecPtr;
	XLogRecPtr	lastCheckPointEndPtr;
	CheckPoint	lastCheckPoint;
	XLogRecPtr	PriorRedoPtr;
	XLogRecPtr	receivePtr;
	XLogRecPtr	replayPtr;
	TimeLineID	replayTLI;
	XLogRecPtr	endptr;
	XLogSegNo	_logSegNo;
	TimestampTz xtime;
	/* POLAR: dma */
	bool controlFileUpdated = false;

	/* POLAR: The flashback log ptr of the checkpoint start */
	polar_flog_rec_ptr flog_ptr_ckp_start = POLAR_INVALID_FLOG_REC_PTR;
	bool		is_flashback_point = false;
	pg_time_t	flashback_point_time;
	bool		is_shutdown = flags & CHECKPOINT_IS_SHUTDOWN;
	XLogRecPtr bg_replayed_lsn = InvalidXLogRecPtr;

	bg_replayed_lsn = polar_bg_redo_get_replayed_lsn(polar_logindex_redo_instance);
	/* POLAR: Is it a flashback point? */
	is_flashback_point = polar_is_flashback_point(flog_instance,
			polar_get_replay_end_rec_ptr(NULL), bg_replayed_lsn,
			&flags, true);

	/* Concurrent checkpoint/restartpoint cannot happen */
	Assert(!IsUnderPostmaster || MyBackendType == B_CHECKPOINTER);

	/* Get a local copy of the last safe checkpoint record. */
	if (POLAR_IN_PARALLEL_REPLAY_STANDBY_MODE(polar_logindex_redo_instance)) {
		polar_checkpoint_ringbuf_shrink(&XLogCtl->polar_checkpoint_ringbuf, bg_replayed_lsn);
		polar_checkpoint_ringbuf_front(&XLogCtl->polar_checkpoint_ringbuf, 
                                       &lastCheckPointRecPtr,
                                       &lastCheckPointEndPtr,
                                       &lastCheckPoint);

		elog(polar_trace_logindex(DEBUG3), "%s get checkpoint, redo: %lX, bg_replayed_lsn: %lX",
			 __func__, lastCheckPoint.redo, bg_replayed_lsn);
	}
	else
	{
		SpinLockAcquire(&XLogCtl->info_lck);
		lastCheckPointRecPtr = XLogCtl->lastCheckPointRecPtr;
		lastCheckPointEndPtr = XLogCtl->lastCheckPointEndPtr;
		lastCheckPoint = XLogCtl->lastCheckPoint;
		SpinLockRelease(&XLogCtl->info_lck);
	}

	/*
	 * Check that we're still in recovery mode. It's ok if we exit recovery
	 * mode after this check, the restart point is valid anyway.
	 */
	if (!RecoveryInProgress())
	{
		ereport(DEBUG2,
				(errmsg_internal("skipping restartpoint, recovery has already ended")));
		return false;
	}

	/*
	 * POLAR: we disable restartpoint when polardb rw node is in
	 * crash recovery mod, including standby
	 * Although we perform restartpoint, it's ok, but we want to
	 * simply crash recovery
	 *
	 * Restartpoint is useless in DataMax mode.
	 */
	else if ((POLAR_IS_MASTER_IN_RECOVERY_OR_STANDBY || polar_is_datamax()) &&
		 !HotStandbyActive())
	{
		ereport(LOG,
				(errmsg("skipping restartpoint, rw/standby is in crash recovery mode now")));
		LWLockRelease(CheckpointLock);
		return false;
	}
	/* POLAR end */

	flashback_point_time = (pg_time_t) time(NULL);
	/*
	 * POLAR: get current flashback log ptr of the flashback point begining.
	 *
	 * NB: We don't need a precise value, but a little earlier value.
	 * When we search a right flashback log in logindex, sometime we will get
	 * two right flashback log.
	 */
	if (is_flashback_point)
		flog_ptr_ckp_start = polar_get_flog_write_result(flog_instance->buf_ctl);

	/*
	 * If the last checkpoint record we've replayed is already our last
	 * restartpoint, we can't perform a new restart point. We still update
	 * minRecoveryPoint in that case, so that if this is a shutdown restart
	 * point, we won't start up earlier than before. That's not strictly
	 * necessary, but when hot standby is enabled, it would be rather weird if
	 * the database opened up for read-only connections at a point-in-time
	 * before the last shutdown. Such time travel is still possible in case of
	 * immediate shutdown, though.
	 *
	 * We don't explicitly advance minRecoveryPoint when we do create a
	 * restartpoint. It's assumed that flushing the buffers will do that as a
	 * side-effect.
	 */
	if (XLogRecPtrIsInvalid(lastCheckPointRecPtr) ||
		lastCheckPoint.redo <= ControlFile->checkPointCopy.redo)
	{
		ereport(DEBUG2,
				(errmsg_internal("skipping restartpoint, already performed at %X/%X",
								 LSN_FORMAT_ARGS(lastCheckPoint.redo))));

		UpdateMinRecoveryPoint(InvalidXLogRecPtr, true);
		if (is_shutdown)
		{
			LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
			ControlFile->state = DB_SHUTDOWNED_IN_RECOVERY;
			UpdateControlFile();
			LWLockRelease(ControlFileLock);

			/*
			 * When hot standby is shutdown in this case, do the shutdown checkpoint
			 * things about flashback log.
			 *
			 * NB: Now the fast recovery area is disable for standby, so we don't
			 * need the next xid and snapshot.
			 */
			if (is_flashback_point)
			{
				polar_set_fbpoint_wal_info(flog_instance->buf_ctl, polar_get_replay_end_rec_ptr(NULL), flashback_point_time, bg_replayed_lsn, true);
				polar_do_flashback_point(flog_ptr_ckp_start, NULL, true);
			}
		}
		return false;
	}

	/*
	 * POLAR: Set the flashback point lsn to replayEndRecPtr.
	 *
	 * NB: Must hold the XLogCtl->info_lck to protect XLogCtl->replayEndRecPtr
	 * not change.
	 */
	if (is_flashback_point)
		polar_set_fbpoint_wal_info(flog_instance->buf_ctl, polar_get_replay_end_rec_ptr(NULL), flashback_point_time, bg_replayed_lsn, true);

	/*
	 * Update the shared RedoRecPtr so that the startup process can calculate
	 * the number of segments replayed since last restartpoint, and request a
	 * restartpoint if it exceeds CheckPointSegments.
	 *
	 * Like in CreateCheckPoint(), hold off insertions to update it, although
	 * during recovery this is just pro forma, because no WAL insertions are
	 * happening.
	 */
	WALInsertLockAcquireExclusive();
	RedoRecPtr = XLogCtl->Insert.RedoRecPtr = lastCheckPoint.redo;
	WALInsertLockRelease();

	/* Also update the info_lck-protected copy */
	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->RedoRecPtr = lastCheckPoint.redo;
	SpinLockRelease(&XLogCtl->info_lck);

	/*
	 * Prepare to accumulate statistics.
	 *
	 * Note: because it is possible for log_checkpoints to change while a
	 * checkpoint proceeds, we always accumulate stats, even if
	 * log_checkpoints is currently off.
	 */
	MemSet(&CheckpointStats, 0, sizeof(CheckpointStats));
	CheckpointStats.ckpt_start_t = GetCurrentTimestamp();

	if (log_checkpoints)
		LogCheckpointStart(flags, true);

	/* Update the process title */
	update_checkpoint_display(flags, true, false);

	CheckPointGuts(lastCheckPoint.redo, flags);

	/* POLAR: wait consistent lsn for standby. */
	if (polar_should_check_checkpoint())
		polar_wait_consistent_lsn(lastCheckPoint.redo, flags);

	/*
	 * This location needs to be after CheckPointGuts() to ensure that some
	 * work has already happened during this checkpoint.
	 */
	INJECTION_POINT("create-restart-point");

	/*
	 * Remember the prior checkpoint's redo ptr for
	 * UpdateCheckPointDistanceEstimate()
	 */
	PriorRedoPtr = ControlFile->checkPointCopy.redo;

	/*
	 * Update pg_control, using current time.  Check that it still shows an
	 * older checkpoint, else do nothing; this is a quick hack to make sure
	 * nothing really bad happens if somehow we get here after the
	 * end-of-recovery checkpoint.
	 */
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	if (ControlFile->checkPointCopy.redo < lastCheckPoint.redo)
	{
		/*
		 * Update the checkpoint information.  We do this even if the cluster
		 * does not show DB_IN_ARCHIVE_RECOVERY to match with the set of WAL
		 * segments recycled below.
		 */
		ControlFile->checkPoint = lastCheckPointRecPtr;
		ControlFile->checkPointCopy = lastCheckPoint;

		/*
		 * Ensure minRecoveryPoint is past the checkpoint record and update it
		 * if the control file still shows DB_IN_ARCHIVE_RECOVERY.  Normally,
		 * this will have happened already while writing out dirty buffers,
		 * but not necessarily - e.g. because no buffers were dirtied.  We do
		 * this because a backup performed in recovery uses minRecoveryPoint
		 * to determine which WAL files must be included in the backup, and
		 * the file (or files) containing the checkpoint record must be
		 * included, at a minimum.  Note that for an ordinary restart of
		 * recovery there's no value in having the minimum recovery point any
		 * earlier than this anyway, because redo will begin just after the
		 * checkpoint record.
		 */
		if (ControlFile->state == DB_IN_ARCHIVE_RECOVERY)
		{
			if (ControlFile->minRecoveryPoint < lastCheckPointEndPtr)
			{
				ControlFile->minRecoveryPoint = lastCheckPointEndPtr;
				ControlFile->minRecoveryPointTLI = lastCheckPoint.ThisTimeLineID;

				/* update local copy */
				LocalMinRecoveryPoint = ControlFile->minRecoveryPoint;
				LocalMinRecoveryPointTLI = ControlFile->minRecoveryPointTLI;
			}
			if (flags & CHECKPOINT_IS_SHUTDOWN)
				ControlFile->state = DB_SHUTDOWNED_IN_RECOVERY;
		}
<<<<<<< HEAD
		if (is_shutdown)
			ControlFile->state = DB_SHUTDOWNED_IN_RECOVERY;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		UpdateControlFile();

		/* 
		 * POLAR: ignore remove xlogs if not update pg_control, otherwise 
		 * the checkpint record of pg_control may be not exists 
		 */
		if (POLAR_ENABLE_DMA())
			controlFileUpdated = true;
	}
	LWLockRelease(ControlFileLock);

	/*
<<<<<<< HEAD
	 * POLAR: Do something after the flashback point is done:
	 * 1. Flush the flashback data.
	 * 2. remove the old flashback data.
	 *
	 * NB: Now the fast recovery area is disable for standby, so we don't
	 * need the next xid and snapshot.
	 */
	if (is_flashback_point)
		polar_do_flashback_point(flog_ptr_ckp_start, NULL, is_shutdown);

	/* POLAR: record checkpoint ptr when checkpoint finished */
	polar_buffer_pool_ctl_set_last_checkpoint_lsn(ControlFile->checkPoint);
	/* POLAR end */
 
	/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 * Update the average distance between checkpoints/restartpoints if the
	 * prior checkpoint exists.
	 */
	if (PriorRedoPtr != InvalidXLogRecPtr)
		UpdateCheckPointDistanceEstimate(RedoRecPtr - PriorRedoPtr);

	/*
	 * Delete old log files, those no longer needed for last restartpoint to
	 * prevent the disk holding the xlog from growing full.
	 */
<<<<<<< HEAD
	if (!POLAR_ENABLE_DMA() || controlFileUpdated)
		XLByteToSeg(RedoRecPtr, _logSegNo, wal_segment_size);

	/* POLAR: Truncate logindex before removing wal files. Files may saved in local file system, like relation size cache. */
	polar_logindex_remove_old_files(polar_logindex_redo_instance);

=======
	XLByteToSeg(RedoRecPtr, _logSegNo, wal_segment_size);

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	/*
	 * Retreat _logSegNo using the current end of xlog replayed or received,
	 * whichever is later.
	 */
<<<<<<< HEAD
	receivePtr = GetWalRcvWriteRecPtr(NULL, NULL);
	replayPtr = GetXLogReplayRecPtr(&replayTLI);
	endptr = (receivePtr < replayPtr) ? replayPtr : receivePtr;
	if (!POLAR_ENABLE_DMA() || controlFileUpdated)
	{
		KeepLogSeg(endptr, &_logSegNo);
		InvalidateObsoleteReplicationSlots(_logSegNo);
		_logSegNo--;
=======
	receivePtr = GetWalRcvFlushRecPtr(NULL, NULL);
	replayPtr = GetXLogReplayRecPtr(&replayTLI);
	endptr = (receivePtr < replayPtr) ? replayPtr : receivePtr;
	KeepLogSeg(endptr, &_logSegNo);
	if (InvalidateObsoleteReplicationSlots(RS_INVAL_WAL_REMOVED,
										   _logSegNo, InvalidOid,
										   InvalidTransactionId))
	{
		/*
		 * Some slots have been invalidated; recalculate the old-segment
		 * horizon, starting again from RedoRecPtr.
		 */
		XLByteToSeg(RedoRecPtr, _logSegNo, wal_segment_size);
		KeepLogSeg(endptr, &_logSegNo);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
	_logSegNo--;

	/*
	 * Try to recycle segments on a useful timeline. If we've been promoted
	 * since the beginning of this restartpoint, use the new timeline chosen
	 * at end of recovery.  If we're still in recovery, use the timeline we're
	 * currently replaying.
	 *
	 * There is no guarantee that the WAL segments will be useful on the
	 * current timeline; if recovery proceeds to a new timeline right after
	 * this, the pre-allocated WAL segments on this timeline will not be used,
	 * and will go wasted until recycled on the next restartpoint. We'll live
	 * with that.
	 */
	if (!RecoveryInProgress())
		replayTLI = XLogCtl->InsertTimeLineID;

	RemoveOldXlogFiles(_logSegNo, RedoRecPtr, endptr, replayTLI);

	/*
	 * Make more log segments if needed.  (Do this after recycling old log
	 * segments, since that may supply some of the needed files.)
	 */
	PreallocXlogFiles(endptr, replayTLI);

	/*
	 * Try to recycle segments on a useful timeline. If we've been promoted
	 * since the beginning of this restartpoint, use the new timeline chosen
	 * at end of recovery (RecoveryInProgress() sets ThisTimeLineID in that
	 * case). If we're still in recovery, use the timeline we're currently
	 * replaying.
	 *
	 * There is no guarantee that the WAL segments will be useful on the
	 * current timeline; if recovery proceeds to a new timeline right after
	 * this, the pre-allocated WAL segments on this timeline will not be used,
	 * and will go wasted until recycled on the next restartpoint. We'll live
	 * with that.
	 */
	if (RecoveryInProgress())
		ThisTimeLineID = replayTLI;

	if (!POLAR_ENABLE_DMA() || controlFileUpdated)
		RemoveOldXlogFiles(_logSegNo, RedoRecPtr, endptr);

	/*
	 * Make more log segments if needed.  (Do this after recycling old log
	 * segments, since that may supply some of the needed files.)
	 */
	PreallocXlogFiles(endptr);

	/*
	 * ThisTimeLineID is normally not set when we're still in recovery.
	 * However, recycling/preallocating segments above needed ThisTimeLineID
	 * to determine which timeline to install the segments on. Reset it now,
	 * to restore the normal state of affairs for debugging purposes.
	 */
	if (RecoveryInProgress())
		ThisTimeLineID = 0;

	/*
	 * Truncate pg_subtrans if possible.  We can throw away all data before
	 * the oldest XMIN of any running transaction.  No future transaction will
	 * attempt to reference any pg_subtrans entry older than that (see Asserts
	 * in subtrans.c).  When hot standby is disabled, though, we mustn't do
	 * this because StartupSUBTRANS hasn't been called yet.
	 *
	 * POLAR csn
	 * We write truncate wal log like clog, so need not truncate csnlog in
	 * hot standby.
	 */
	if (EnableHotStandby)
<<<<<<< HEAD
	{
		if (!polar_csn_enable)
			TruncateSUBTRANS(GetOldestXmin(NULL, PROCARRAY_FLAGS_DEFAULT));
	}
=======
		TruncateSUBTRANS(GetOldestTransactionIdConsideredRunning());
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* Real work is done; log and update stats. */
	LogCheckpointEnd(true);

	/* Reset the process title */
	update_checkpoint_display(flags, true, true);

	xtime = GetLatestXTime();
	ereport((log_checkpoints ? LOG : DEBUG2),
			(errmsg("recovery restart point at %X/%X",
					LSN_FORMAT_ARGS(lastCheckPoint.redo)),
			 xtime ? errdetail("Last completed transaction was at log time %s.",
							   timestamptz_to_str(xtime)) : 0));

	/*
	 * Finally, execute archive_cleanup_command, if any.
	 */
	if (archiveCleanupCommand && strcmp(archiveCleanupCommand, "") != 0)
		ExecuteRecoveryCommand(archiveCleanupCommand,
							   "archive_cleanup_command",
							   false,
							   WAIT_EVENT_ARCHIVE_CLEANUP_COMMAND);

	return true;
}

/*
<<<<<<< HEAD
 * Polar: Retreat *logSegNo to the last segment that we need to retain because of
 * wal_keep_segments and replication slots(max_slot_wal_keep_size) and 
 * logindex and dma.
 *
 * The max_slot_wal_keep_size (replication slots) are to count the number of 
 * wal files that need to be trimmed, so put them in the front. The wal_keep_segments 
 * and logindex and dma need to count the number of wal files to keep, so put them back.
=======
 * Report availability of WAL for the given target LSN
 *		(typically a slot's restart_lsn)
 *
 * Returns one of the following enum values:
 *
 * * WALAVAIL_RESERVED means targetLSN is available and it is in the range of
 *   max_wal_size.
 *
 * * WALAVAIL_EXTENDED means it is still available by preserving extra
 *   segments beyond max_wal_size. If max_slot_wal_keep_size is smaller
 *   than max_wal_size, this state is not returned.
 *
 * * WALAVAIL_UNRESERVED means it is being lost and the next checkpoint will
 *   remove reserved segments. The walsender using this slot may return to the
 *   above.
 *
 * * WALAVAIL_REMOVED means it has been removed. A replication stream on
 *   a slot with this LSN cannot continue.  (Any associated walsender
 *   processes should have been terminated already.)
 *
 * * WALAVAIL_INVALID_LSN means the slot hasn't been set to reserve WAL.
 */
WALAvailability
GetWALAvailability(XLogRecPtr targetLSN)
{
	XLogRecPtr	currpos;		/* current write LSN */
	XLogSegNo	currSeg;		/* segid of currpos */
	XLogSegNo	targetSeg;		/* segid of targetLSN */
	XLogSegNo	oldestSeg;		/* actual oldest segid */
	XLogSegNo	oldestSegMaxWalSize;	/* oldest segid kept by max_wal_size */
	XLogSegNo	oldestSlotSeg;	/* oldest segid kept by slot */
	uint64		keepSegs;

	/*
	 * slot does not reserve WAL. Either deactivated, or has never been active
	 */
	if (XLogRecPtrIsInvalid(targetLSN))
		return WALAVAIL_INVALID_LSN;

	/*
	 * Calculate the oldest segment currently reserved by all slots,
	 * considering wal_keep_size and max_slot_wal_keep_size.  Initialize
	 * oldestSlotSeg to the current segment.
	 */
	currpos = GetXLogWriteRecPtr();
	XLByteToSeg(currpos, oldestSlotSeg, wal_segment_size);
	KeepLogSeg(currpos, &oldestSlotSeg);

	/*
	 * Find the oldest extant segment file. We get 1 until checkpoint removes
	 * the first WAL segment file since startup, which causes the status being
	 * wrong under certain abnormal conditions but that doesn't actually harm.
	 */
	oldestSeg = XLogGetLastRemovedSegno() + 1;

	/* calculate oldest segment by max_wal_size */
	XLByteToSeg(currpos, currSeg, wal_segment_size);
	keepSegs = ConvertToXSegs(max_wal_size_mb, wal_segment_size) + 1;

	if (currSeg > keepSegs)
		oldestSegMaxWalSize = currSeg - keepSegs;
	else
		oldestSegMaxWalSize = 1;

	/* the segment we care about */
	XLByteToSeg(targetLSN, targetSeg, wal_segment_size);

	/*
	 * No point in returning reserved or extended status values if the
	 * targetSeg is known to be lost.
	 */
	if (targetSeg >= oldestSlotSeg)
	{
		/* show "reserved" when targetSeg is within max_wal_size */
		if (targetSeg >= oldestSegMaxWalSize)
			return WALAVAIL_RESERVED;

		/* being retained by slots exceeding max_wal_size */
		return WALAVAIL_EXTENDED;
	}

	/* WAL segments are no longer retained but haven't been removed yet */
	if (targetSeg >= oldestSeg)
		return WALAVAIL_UNRESERVED;

	/* Definitely lost */
	return WALAVAIL_REMOVED;
}


/*
 * Retreat *logSegNo to the last segment that we need to retain because of
 * either wal_keep_size or replication slots.
 *
 * This is calculated by subtracting wal_keep_size from the given xlog
 * location, recptr and by making sure that that result is below the
 * requirement of replication slots.  For the latter criterion we do consider
 * the effects of max_slot_wal_keep_size: reserve at most that much space back
 * from recptr.
 *
 * Note about replication slots: if this function calculates a value
 * that's further ahead than what slots need reserved, then affected
 * slots need to be invalidated and this function invoked again.
 * XXX it might be a good idea to rewrite this function so that
 * invalidation is optionally done here, instead.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 */
static void
KeepLogSeg(XLogRecPtr recptr, XLogSegNo *logSegNo)
{
	XLogSegNo	currSegNo;
	XLogSegNo	segno;
	XLogRecPtr	keep;

	XLByteToSeg(recptr, currSegNo, wal_segment_size);
	segno = currSegNo;
<<<<<<< HEAD

	/* compute limit for replication slot */
	keep = XLogGetReplicationSlotMinimumLSN();
	if (keep != InvalidXLogRecPtr)
	{
		XLByteToSeg(keep, segno, wal_segment_size);

		/* compute limit for max_slot_wal_keep_size */
		if (polar_enable_max_slot_wal_keep_size && max_slot_wal_keep_size_mb >= 0)
		{
			uint64		slot_keep_segs;

			slot_keep_segs =
				ConvertToXSegs(max_slot_wal_keep_size_mb, wal_segment_size);

			if (currSegNo - segno > slot_keep_segs)
				segno = currSegNo - slot_keep_segs;
		}
	}
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Calculate how many segments are kept by slots first, adjusting for
	 * max_slot_wal_keep_size.
	 */
	keep = XLogGetReplicationSlotMinimumLSN();
	if (keep != InvalidXLogRecPtr && keep < recptr)
	{
<<<<<<< HEAD
		if (currSegNo - segno < wal_keep_segments)
		{
			/* avoid underflow, don't go below 1 */
			if (currSegNo <= wal_keep_segments)
				segno = 1;
			else
				segno = currSegNo - wal_keep_segments;
		}
	}

	/* compute limit for logindex redo */
	if (polar_logindex_redo_instance)
	{
		keep = polar_calc_min_used_lsn(false);

		if (keep != InvalidXLogRecPtr)
		{
			XLogSegNo	slotSegNo;

			XLByteToSeg(keep, slotSegNo, wal_segment_size);

			if (slotSegNo <= 0)
				segno = 1;
			else if (slotSegNo < segno)
				segno = slotSegNo;
		}
	}

	/* compute limit for dma */
	if (polar_is_dma_data_node())
	{
		XLogRecPtr consensus_keep  = ConsensusGetPurgeLSN();
		if (consensus_keep != InvalidXLogRecPtr)
		{
			XLogSegNo	slotSegNo;

			XLByteToSeg(consensus_keep, slotSegNo, wal_segment_size);

			if (slotSegNo <= 0)
				segno = 1;
			else if (slotSegNo < segno)
				segno = slotSegNo;

			ereport(LOG,
					(errmsg("purged consensus all matched's segno: %lu", segno)));
		}
		else
		{
			segno = 1;
=======
		XLByteToSeg(keep, segno, wal_segment_size);

		/* Cap by max_slot_wal_keep_size ... */
		if (max_slot_wal_keep_size_mb >= 0)
		{
			uint64		slot_keep_segs;

			slot_keep_segs =
				ConvertToXSegs(max_slot_wal_keep_size_mb, wal_segment_size);

			if (currSegNo - segno > slot_keep_segs)
				segno = currSegNo - slot_keep_segs;
		}
	}

	/*
	 * If WAL summarization is in use, don't remove WAL that has yet to be
	 * summarized.
	 */
	keep = GetOldestUnsummarizedLSN(NULL, NULL);
	if (keep != InvalidXLogRecPtr)
	{
		XLogSegNo	unsummarized_segno;

		XLByteToSeg(keep, unsummarized_segno, wal_segment_size);
		if (unsummarized_segno < segno)
			segno = unsummarized_segno;
	}

	/* but, keep at least wal_keep_size if that's set */
	if (wal_keep_size_mb > 0)
	{
		uint64		keep_segs;

		keep_segs = ConvertToXSegs(wal_keep_size_mb, wal_segment_size);
		if (currSegNo - segno < keep_segs)
		{
			/* avoid underflow, don't go below 1 */
			if (currSegNo <= keep_segs)
				segno = 1;
			else
				segno = currSegNo - keep_segs;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}
	}

	/* don't delete WAL segments newer than the calculated segment */
	if (segno < *logSegNo)
		*logSegNo = segno;
}

/*
 * Write a NEXTOID log record
 */
void
XLogPutNextOid(Oid nextOid)
{
	XLogBeginInsert();
	XLogRegisterData((char *) (&nextOid), sizeof(Oid));
	(void) XLogInsert(RM_XLOG_ID, XLOG_NEXTOID);

	/*
	 * We need not flush the NEXTOID record immediately, because any of the
	 * just-allocated OIDs could only reach disk as part of a tuple insert or
	 * update that would have its own XLOG record that must follow the NEXTOID
	 * record.  Therefore, the standard buffer LSN interlock applied to those
	 * records will ensure no such OID reaches disk before the NEXTOID record
	 * does.
	 *
	 * Note, however, that the above statement only covers state "within" the
	 * database.  When we use a generated OID as a file or directory name, we
	 * are in a sense violating the basic WAL rule, because that filesystem
	 * change may reach disk before the NEXTOID WAL record does.  The impact
	 * of this is that if a database crash occurs immediately afterward, we
	 * might after restart re-generate the same OID and find that it conflicts
	 * with the leftover file or directory.  But since for safety's sake we
	 * always loop until finding a nonconflicting filename, this poses no real
	 * problem in practice. See pgsql-hackers discussion 27-Sep-2006.
	 */
}

/*
 * Write an XLOG SWITCH record.
 *
 * Here we just blindly issue an XLogInsert request for the record.
 * All the magic happens inside XLogInsert.
 *
 * The return value is either the end+1 address of the switch record,
 * or the end+1 address of the prior segment if we did not need to
 * write a switch record because we are already at segment start.
 */
XLogRecPtr
RequestXLogSwitch(bool mark_unimportant)
{
	XLogRecPtr	RecPtr;

	/* XLOG SWITCH has no data */
	XLogBeginInsert();

	if (mark_unimportant)
		XLogSetRecordFlags(XLOG_MARK_UNIMPORTANT);
	RecPtr = XLogInsert(RM_XLOG_ID, XLOG_SWITCH);

	return RecPtr;
}

/*
 * Write a RESTORE POINT record
 */
XLogRecPtr
XLogRestorePoint(const char *rpName)
{
	XLogRecPtr	RecPtr;
	xl_restore_point xlrec;

	xlrec.rp_time = GetCurrentTimestamp();
	strlcpy(xlrec.rp_name, rpName, MAXFNAMELEN);

	XLogBeginInsert();
	XLogRegisterData((char *) &xlrec, sizeof(xl_restore_point));

	RecPtr = XLogInsert(RM_XLOG_ID, XLOG_RESTORE_POINT);

	ereport(LOG,
			(errmsg("restore point \"%s\" created at %X/%X",
					rpName, LSN_FORMAT_ARGS(RecPtr))));

	return RecPtr;
}

/*
 * Check if any of the GUC parameters that are critical for hot standby
 * have changed, and update the value in pg_control file if necessary.
 */
static void
XLogReportParameters(void)
{
	if (wal_level != ControlFile->wal_level ||
		wal_log_hints != ControlFile->wal_log_hints ||
		MaxConnections != ControlFile->MaxConnections ||
		max_worker_processes != ControlFile->max_worker_processes ||
		max_wal_senders != ControlFile->max_wal_senders ||
		max_prepared_xacts != ControlFile->max_prepared_xacts ||
		max_locks_per_xact != ControlFile->max_locks_per_xact ||
		track_commit_timestamp != ControlFile->track_commit_timestamp)
	{
		/*
		 * The change in number of backend slots doesn't need to be WAL-logged
		 * if archiving is not enabled, as you can't start archive recovery
		 * with wal_level=minimal anyway. We don't really care about the
		 * values in pg_control either if wal_level=minimal, but seems better
		 * to keep them up-to-date to avoid confusion.
		 */
		if (wal_level != ControlFile->wal_level || XLogIsNeeded())
		{
			xl_parameter_change xlrec;
			XLogRecPtr	recptr;

			xlrec.MaxConnections = MaxConnections;
			xlrec.max_worker_processes = max_worker_processes;
			xlrec.max_wal_senders = max_wal_senders;
			xlrec.max_prepared_xacts = max_prepared_xacts;
			xlrec.max_locks_per_xact = max_locks_per_xact;
			xlrec.wal_level = wal_level;
			xlrec.wal_log_hints = wal_log_hints;
			xlrec.track_commit_timestamp = track_commit_timestamp;

			XLogBeginInsert();
			XLogRegisterData((char *) &xlrec, sizeof(xlrec));

			recptr = XLogInsert(RM_XLOG_ID, XLOG_PARAMETER_CHANGE);
			XLogFlush(recptr);
		}

		LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);

		ControlFile->MaxConnections = MaxConnections;
		ControlFile->max_worker_processes = max_worker_processes;
		ControlFile->max_wal_senders = max_wal_senders;
		ControlFile->max_prepared_xacts = max_prepared_xacts;
		ControlFile->max_locks_per_xact = max_locks_per_xact;
		ControlFile->wal_level = wal_level;
		ControlFile->wal_log_hints = wal_log_hints;
		ControlFile->track_commit_timestamp = track_commit_timestamp;
		UpdateControlFile();

		LWLockRelease(ControlFileLock);
	}
}

/*
 * Update full_page_writes in shared memory, and write an
 * XLOG_FPW_CHANGE record if necessary.
 *
 * Note: this function assumes there is no other process running
 * concurrently that could update it.
 */
void
UpdateFullPageWrites(void)
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	bool		recoveryInProgress;

	/*
	 * Do nothing if full_page_writes has not been changed.
	 *
	 * It's safe to check the shared full_page_writes without the lock,
	 * because we assume that there is no concurrently running process which
	 * can update it.
	 */
	if (fullPageWrites == Insert->fullPageWrites)
		return;

	/*
	 * Perform this outside critical section so that the WAL insert
	 * initialization done by RecoveryInProgress() doesn't trigger an
	 * assertion failure.
	 */
	recoveryInProgress = RecoveryInProgress();

	START_CRIT_SECTION();

	/*
	 * It's always safe to take full page images, even when not strictly
	 * required, but not the other round. So if we're setting full_page_writes
	 * to true, first set it true and then write the WAL record. If we're
	 * setting it to false, first write the WAL record and then set the global
	 * flag.
	 */
	if (fullPageWrites)
	{
		WALInsertLockAcquireExclusive();
		Insert->fullPageWrites = true;
		WALInsertLockRelease();
	}

	/*
	 * Write an XLOG_FPW_CHANGE record. This allows us to keep track of
	 * full_page_writes during archive recovery, if required.
	 */
	if (XLogStandbyInfoActive() && !recoveryInProgress)
	{
		XLogBeginInsert();
		XLogRegisterData((char *) (&fullPageWrites), sizeof(bool));

		XLogInsert(RM_XLOG_ID, XLOG_FPW_CHANGE);
	}

	if (!fullPageWrites)
	{
		WALInsertLockAcquireExclusive();
		Insert->fullPageWrites = false;
		WALInsertLockRelease();
	}
	END_CRIT_SECTION();
}

/*
 * XLOG resource manager's routines
 *
 * Definitions of info values are in include/catalog/pg_control.h, though
 * not all record types are related to control file updates.
 *
 * NOTE: Some XLOG record types that are directly related to WAL recovery
 * are handled in xlogrecovery_redo().
 */
void
xlog_redo(XLogReaderState *record)
{
	uint8		info = XLogRecGetInfo(record) & ~XLR_INFO_MASK;
	XLogRecPtr	lsn = record->EndRecPtr;

	/*
	 * In XLOG rmgr, backup blocks are only used by XLOG_FPI and
	 * XLOG_FPI_FOR_HINT records.
	 */
	Assert(info == XLOG_FPI || info == XLOG_FPI_FOR_HINT ||
		info == XLOG_FPSI ||
		info == XLOG_FPI_MULTI || !XLogRecHasAnyBlockRefs(record));

	if (info == XLOG_NEXTOID)
	{
		Oid			nextOid;

		/*
		 * We used to try to take the maximum of TransamVariables->nextOid and
		 * the recorded nextOid, but that fails if the OID counter wraps
		 * around.  Since no OID allocation should be happening during replay
		 * anyway, better to just believe the record exactly.  We still take
		 * OidGenLock while setting the variable, just in case.
		 */
		memcpy(&nextOid, XLogRecGetData(record), sizeof(Oid));
		LWLockAcquire(OidGenLock, LW_EXCLUSIVE);
		TransamVariables->nextOid = nextOid;
		TransamVariables->oidCount = 0;
		LWLockRelease(OidGenLock);
	}
	else if (info == XLOG_CHECKPOINT_SHUTDOWN)
	{
		CheckPoint	checkPoint;
		TimeLineID	replayTLI;

		memcpy(&checkPoint, XLogRecGetData(record), sizeof(CheckPoint));
		/* In a SHUTDOWN checkpoint, believe the counters exactly */
		LWLockAcquire(XidGenLock, LW_EXCLUSIVE);
		TransamVariables->nextXid = checkPoint.nextXid;
		LWLockRelease(XidGenLock);
		LWLockAcquire(OidGenLock, LW_EXCLUSIVE);
		TransamVariables->nextOid = checkPoint.nextOid;
		TransamVariables->oidCount = 0;
		LWLockRelease(OidGenLock);
		MultiXactSetNextMXact(checkPoint.nextMulti,
							  checkPoint.nextMultiOffset);

		MultiXactAdvanceOldest(checkPoint.oldestMulti,
							   checkPoint.oldestMultiDB);

		/*
		 * No need to set oldestClogXid here as well; it'll be set when we
		 * redo an xl_clog_truncate if it changed since initialization.
		 */
		SetTransactionIdLimit(checkPoint.oldestXid, checkPoint.oldestXidDB);

		/*
		 * If we see a shutdown checkpoint while waiting for an end-of-backup
		 * record, the backup was canceled and the end-of-backup record will
		 * never arrive.
		 */
		if (ArchiveRecoveryRequested &&
			!XLogRecPtrIsInvalid(ControlFile->backupStartPoint) &&
			XLogRecPtrIsInvalid(ControlFile->backupEndPoint))
			ereport(PANIC,
					(errmsg("online backup was canceled, recovery cannot continue")));

		/*
		 * If we see a shutdown checkpoint, we know that nothing was running
		 * on the primary at this point. So fake-up an empty running-xacts
		 * record and use that here and now. Recover additional standby state
		 * for prepared transactions.
		 */
		if (standbyState >= STANDBY_INITIALIZED)
		{
			TransactionId *xids;
			int			nxids;
			TransactionId oldestActiveXID;
			TransactionId latestCompletedXid;
			RunningTransactionsData running;

			oldestActiveXID = PrescanPreparedTransactions(&xids, &nxids);

			/* Update pg_subtrans entries for any prepared transactions */
			StandbyRecoverPreparedTransactions();

			/*
			 * Construct a RunningTransactions snapshot representing a shut
			 * down server, with only prepared transactions still alive. We're
			 * never overflowed at this point because all subxids are listed
			 * with their parent prepared transactions.
			 */
			running.xcnt = nxids;
			running.subxcnt = 0;
			running.subxid_status = SUBXIDS_IN_SUBTRANS;
			running.nextXid = XidFromFullTransactionId(checkPoint.nextXid);
			running.oldestRunningXid = oldestActiveXID;
			latestCompletedXid = XidFromFullTransactionId(checkPoint.nextXid);
			TransactionIdRetreat(latestCompletedXid);
			Assert(TransactionIdIsNormal(latestCompletedXid));
			running.latestCompletedXid = latestCompletedXid;
			running.xids = xids;

			ProcArrayApplyRecoveryInfo(&running);
		}

		/* ControlFile->checkPointCopy always tracks the latest ckpt XID */
		LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
<<<<<<< HEAD
		ControlFile->checkPointCopy.nextXidEpoch = checkPoint.nextXidEpoch;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		ControlFile->checkPointCopy.nextXid = checkPoint.nextXid;
		LWLockRelease(ControlFileLock);

		/* Update shared-memory copy of checkpoint XID/epoch */
		SpinLockAcquire(&XLogCtl->info_lck);
		XLogCtl->ckptFullXid = checkPoint.nextXid;
		SpinLockRelease(&XLogCtl->info_lck);

		/*
		 * We should've already switched to the new TLI before replaying this
		 * record.
		 */
		(void) GetCurrentReplayRecPtr(&replayTLI);
		if (checkPoint.ThisTimeLineID != replayTLI)
			ereport(PANIC,
					(errmsg("unexpected timeline ID %u (should be %u) in shutdown checkpoint record",
							checkPoint.ThisTimeLineID, replayTLI)));

		RecoveryRestartPoint(&checkPoint, record);
	}
	else if (info == XLOG_CHECKPOINT_ONLINE)
	{
		CheckPoint	checkPoint;
		TimeLineID	replayTLI;

		memcpy(&checkPoint, XLogRecGetData(record), sizeof(CheckPoint));
		/* In an ONLINE checkpoint, treat the XID counter as a minimum */
		LWLockAcquire(XidGenLock, LW_EXCLUSIVE);
		if (FullTransactionIdPrecedes(TransamVariables->nextXid,
									  checkPoint.nextXid))
			TransamVariables->nextXid = checkPoint.nextXid;
		LWLockRelease(XidGenLock);

		/*
		 * We ignore the nextOid counter in an ONLINE checkpoint, preferring
		 * to track OID assignment through XLOG_NEXTOID records.  The nextOid
		 * counter is from the start of the checkpoint and might well be stale
		 * compared to later XLOG_NEXTOID records.  We could try to take the
		 * maximum of the nextOid counter and our latest value, but since
		 * there's no particular guarantee about the speed with which the OID
		 * counter wraps around, that's a risky thing to do.  In any case,
		 * users of the nextOid counter are required to avoid assignment of
		 * duplicates, so that a somewhat out-of-date value should be safe.
		 */

		/* Handle multixact */
		MultiXactAdvanceNextMXact(checkPoint.nextMulti,
								  checkPoint.nextMultiOffset);

		/*
		 * NB: This may perform multixact truncation when replaying WAL
		 * generated by an older primary.
		 */
		MultiXactAdvanceOldest(checkPoint.oldestMulti,
							   checkPoint.oldestMultiDB);
		if (TransactionIdPrecedes(TransamVariables->oldestXid,
								  checkPoint.oldestXid))
			SetTransactionIdLimit(checkPoint.oldestXid,
								  checkPoint.oldestXidDB);
		/* ControlFile->checkPointCopy always tracks the latest ckpt XID */
		LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
<<<<<<< HEAD
		ControlFile->checkPointCopy.nextXidEpoch = checkPoint.nextXidEpoch;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		ControlFile->checkPointCopy.nextXid = checkPoint.nextXid;
		LWLockRelease(ControlFileLock);

		/* Update shared-memory copy of checkpoint XID/epoch */
		SpinLockAcquire(&XLogCtl->info_lck);
		XLogCtl->ckptFullXid = checkPoint.nextXid;
		SpinLockRelease(&XLogCtl->info_lck);

		/* TLI should not change in an on-line checkpoint */
		(void) GetCurrentReplayRecPtr(&replayTLI);
		if (checkPoint.ThisTimeLineID != replayTLI)
			ereport(PANIC,
					(errmsg("unexpected timeline ID %u (should be %u) in online checkpoint record",
							checkPoint.ThisTimeLineID, replayTLI)));

		RecoveryRestartPoint(&checkPoint, record);
	}
	else if (info == XLOG_OVERWRITE_CONTRECORD)
	{
		/* nothing to do here, handled in xlogrecovery_redo() */
	}
	else if (info == XLOG_END_OF_RECOVERY)
	{
		xl_end_of_recovery xlrec;
		TimeLineID	replayTLI;

		memcpy(&xlrec, XLogRecGetData(record), sizeof(xl_end_of_recovery));

		/*
		 * For Hot Standby, we could treat this like a Shutdown Checkpoint,
		 * but this case is rarer and harder to test, so the benefit doesn't
		 * outweigh the potential extra cost of maintenance.
		 */

		/*
		 * We should've already switched to the new TLI before replaying this
		 * record.
		 */
		(void) GetCurrentReplayRecPtr(&replayTLI);
		if (xlrec.ThisTimeLineID != replayTLI)
			ereport(PANIC,
					(errmsg("unexpected timeline ID %u (should be %u) in end-of-recovery record",
							xlrec.ThisTimeLineID, replayTLI)));
	}
	else if (info == XLOG_NOOP)
	{
		/* nothing to do here */
	}
	else if (info == XLOG_SWITCH)
	{
		/* nothing to do here */
	}
	else if (info == XLOG_RESTORE_POINT)
	{
		/* nothing to do here, handled in xlogrecovery.c */
	}
	else if (info == XLOG_FPI || info == XLOG_FPI_FOR_HINT ||
			 info == XLOG_FPI_MULTI)
	{
<<<<<<< HEAD
		uint8		block_id;

		/*
		 * Full-page image (FPI) records contain nothing else but a backup
		 * block (or multiple backup blocks). Every block reference must
		 * include a full-page image - otherwise there would be no point in
		 * this record.
=======
		/*
		 * XLOG_FPI records contain nothing else but one or more block
		 * references. Every block reference must include a full-page image
		 * even if full_page_writes was disabled when the record was generated
		 * - otherwise there would be no point in this record.
		 *
		 * XLOG_FPI_FOR_HINT records are generated when a page needs to be
		 * WAL-logged because of a hint bit update. They are only generated
		 * when checksums and/or wal_log_hints are enabled. They may include
		 * no full-page images if full_page_writes was disabled when they were
		 * generated. In this case there is nothing to do here.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		 *
		 * No recovery conflicts are generated by these generic records - if a
		 * resource manager needs to generate conflicts, it has to define a
		 * separate WAL record type and redo routine.
		 */
<<<<<<< HEAD

		/*
		 * POLAR: The error message is triggered in XLogReadBufferForRedo function
		 * when fail to restore full-page image.
		 * Besides we will return BLK_DONE when page lsn is larger than current xlog
		 * record lsn.
		 */
		for (block_id = 0; block_id <= record->max_block_id; block_id++)
		{
			Buffer		buffer;

			XLogReadBufferForRedo(record, block_id, &buffer);

=======
		for (uint8 block_id = 0; block_id <= XLogRecMaxBlockId(record); block_id++)
		{
			Buffer		buffer;

			if (!XLogRecHasBlockImage(record, block_id))
			{
				if (info == XLOG_FPI)
					elog(ERROR, "XLOG_FPI record did not contain a full-page image");
				continue;
			}

			if (XLogReadBufferForRedo(record, block_id, &buffer) != BLK_RESTORED)
				elog(ERROR, "unexpected XLogReadBufferForRedo result when restoring backup block");
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			UnlockReleaseBuffer(buffer);
		}
	}
	else if (info == XLOG_BACKUP_END)
	{
		/* nothing to do here, handled in xlogrecovery_redo() */
	}
	else if (info == XLOG_PARAMETER_CHANGE)
	{
		xl_parameter_change xlrec;

		/* Update our copy of the parameters in pg_control */
		memcpy(&xlrec, XLogRecGetData(record), sizeof(xl_parameter_change));

		/*
		 * Invalidate logical slots if we are in hot standby and the primary
		 * does not have a WAL level sufficient for logical decoding. No need
		 * to search for potentially conflicting logically slots if standby is
		 * running with wal_level lower than logical, because in that case, we
		 * would have either disallowed creation of logical slots or
		 * invalidated existing ones.
		 */
		if (InRecovery && InHotStandby &&
			xlrec.wal_level < WAL_LEVEL_LOGICAL &&
			wal_level >= WAL_LEVEL_LOGICAL)
			InvalidateObsoleteReplicationSlots(RS_INVAL_WAL_LEVEL,
											   0, InvalidOid,
											   InvalidTransactionId);

		LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
		ControlFile->MaxConnections = xlrec.MaxConnections;
		ControlFile->max_worker_processes = xlrec.max_worker_processes;
		ControlFile->max_wal_senders = xlrec.max_wal_senders;
		ControlFile->max_prepared_xacts = xlrec.max_prepared_xacts;
		ControlFile->max_locks_per_xact = xlrec.max_locks_per_xact;
		ControlFile->wal_level = xlrec.wal_level;
		ControlFile->wal_log_hints = xlrec.wal_log_hints;

		/*
		 * Update minRecoveryPoint to ensure that if recovery is aborted, we
		 * recover back up to this point before allowing hot standby again.
		 * This is important if the max_* settings are decreased, to ensure
		 * you don't run queries against the WAL preceding the change. The
		 * local copies cannot be updated as long as crash recovery is
		 * happening and we expect all the WAL to be replayed.
		 */
		if (InArchiveRecovery)
<<<<<<< HEAD
		{
			minRecoveryPoint = ControlFile->minRecoveryPoint;
			minRecoveryPointTLI = ControlFile->minRecoveryPointTLI;
		}
		if (minRecoveryPoint != InvalidXLogRecPtr && minRecoveryPoint < lsn)
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		{
			LocalMinRecoveryPoint = ControlFile->minRecoveryPoint;
			LocalMinRecoveryPointTLI = ControlFile->minRecoveryPointTLI;
		}
		if (LocalMinRecoveryPoint != InvalidXLogRecPtr && LocalMinRecoveryPoint < lsn)
		{
			TimeLineID	replayTLI;

			(void) GetCurrentReplayRecPtr(&replayTLI);
			ControlFile->minRecoveryPoint = lsn;
			ControlFile->minRecoveryPointTLI = replayTLI;
		}

		CommitTsParameterChange(xlrec.track_commit_timestamp,
								ControlFile->track_commit_timestamp);
		ControlFile->track_commit_timestamp = xlrec.track_commit_timestamp;

		UpdateControlFile();
		LWLockRelease(ControlFileLock);

		/* Check to see if any parameter change gives a problem on recovery */
		CheckRequiredParameterValues();
	}
	else if (info == XLOG_FPW_CHANGE)
	{
		bool		fpw;

		memcpy(&fpw, XLogRecGetData(record), sizeof(bool));

		/*
		 * Update the LSN of the last replayed XLOG_FPW_CHANGE record so that
		 * do_pg_backup_start() and do_pg_backup_stop() can check whether
		 * full_page_writes has been disabled during online backup.
		 */
		if (!fpw)
		{
			SpinLockAcquire(&XLogCtl->info_lck);
			if (XLogCtl->lastFpwDisableRecPtr < record->ReadRecPtr)
				XLogCtl->lastFpwDisableRecPtr = record->ReadRecPtr;
			SpinLockRelease(&XLogCtl->info_lck);
		}

		/* Keep track of full_page_writes */
		lastFullPageWrites = fpw;
	}
<<<<<<< HEAD
	/* POLAR: for fullpage snapshot */
	else if (info == XLOG_FPSI && POLAR_LOGINDEX_ENABLE_FULLPAGE())
	{
		uint64	fullpage_no = 0;
		/* get fullpage_no from record */
		memcpy(&fullpage_no, XLogRecGetData(record), sizeof(uint64));
		/* Update max_fullpage_no */
		polar_update_max_fullpage_no(polar_logindex_redo_instance->fullpage_ctl, fullpage_no);
	}
	/* POLAR csnlog */
	else if (polar_csn_enable && info == XLOG_CSNLOG_ZEROPAGE)
	{
		int			pageno;
		memcpy(&pageno, XLogRecGetData(record), sizeof(int));
		polar_csnlog_zero_page_redo(pageno);
	}
	else if (polar_csn_enable && info == XLOG_CSNLOG_TRUNCATE)
	{
		int			pageno;
		memcpy(&pageno, XLogRecGetData(record), sizeof(int));
		polar_csnlog_truncate_redo(pageno);
	}
	/* POLAR end */
}

#ifdef WAL_DEBUG

static void
xlog_outrec(StringInfo buf, XLogReaderState *record)
{
	int			block_id;

	appendStringInfo(buf, "prev %X/%X; xid %u",
					 (uint32) (XLogRecGetPrev(record) >> 32),
					 (uint32) XLogRecGetPrev(record),
					 XLogRecGetXid(record));

	appendStringInfo(buf, "; len %u",
					 XLogRecGetDataLen(record));

	/* decode block references */
	for (block_id = 0; block_id <= record->max_block_id; block_id++)
=======
	else if (info == XLOG_CHECKPOINT_REDO)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	{
		/* nothing to do here, just for informational purposes */
	}
}

/*
 * Return the extra open flags used for opening a file, depending on the
 * value of the GUCs wal_sync_method, fsync and debug_io_direct.
 */
int
get_sync_bit(int method)
{
	int			o_direct_flag = 0;

	/*
	 * Use O_DIRECT if requested, except in walreceiver process.  The WAL
	 * written by walreceiver is normally read by the startup process soon
	 * after it's written.  Also, walreceiver performs unaligned writes, which
	 * don't work with O_DIRECT, so it is required for correctness too.
	 */
	if ((io_direct_flags & IO_DIRECT_WAL) && !AmWalReceiverProcess())
		o_direct_flag = PG_O_DIRECT;

	/* If fsync is disabled, never open in sync mode */
	if (!enableFsync)
		return o_direct_flag;

	switch (method)
	{
			/*
			 * enum values for all sync options are defined even if they are
			 * not supported on the current platform.  But if not, they are
			 * not included in the enum option array, and therefore will never
			 * be seen here.
			 */
		case WAL_SYNC_METHOD_FSYNC:
		case WAL_SYNC_METHOD_FSYNC_WRITETHROUGH:
		case WAL_SYNC_METHOD_FDATASYNC:
			return o_direct_flag;
#ifdef O_SYNC
		case WAL_SYNC_METHOD_OPEN:
			return O_SYNC | o_direct_flag;
#endif
#ifdef O_DSYNC
		case WAL_SYNC_METHOD_OPEN_DSYNC:
			return O_DSYNC | o_direct_flag;
#endif
		default:
			/* can't happen (unless we are out of sync with option array) */
			elog(ERROR, "unrecognized \"wal_sync_method\": %d", method);
			return 0;			/* silence warning */
	}
}

/*
 * GUC support
 */
void
assign_wal_sync_method(int new_wal_sync_method, void *extra)
{
	if (wal_sync_method != new_wal_sync_method)
	{
		/*
		 * To ensure that no blocks escape unsynced, force an fsync on the
		 * currently open log segment (if any).  Also, if the open flag is
		 * changing, close the log file so it will be reopened (with new flag
		 * bit) at next use.
		 */
		if (openLogFile >= 0)
		{
			pgstat_report_wait_start(WAIT_EVENT_WAL_SYNC_METHOD_ASSIGN);
<<<<<<< HEAD
			if (polar_fsync(openLogFile) != 0)
=======
			if (pg_fsync(openLogFile) != 0)
			{
				char		xlogfname[MAXFNAMELEN];
				int			save_errno;

				save_errno = errno;
				XLogFileName(xlogfname, openLogTLI, openLogSegNo,
							 wal_segment_size);
				errno = save_errno;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				ereport(PANIC,
						(errcode_for_file_access(),
						 errmsg("could not fsync file \"%s\": %m", xlogfname)));
			}

			pgstat_report_wait_end();
			if (get_sync_bit(wal_sync_method) != get_sync_bit(new_wal_sync_method))
				XLogFileClose();
		}
	}
}


/*
 * Issue appropriate kind of fsync (if any) for an XLOG output file.
 *
 * 'fd' is a file descriptor for the XLOG file to be fsync'd.
 * 'segno' is for error reporting purposes.
 */
void
issue_xlog_fsync(int fd, XLogSegNo segno, TimeLineID tli)
{
<<<<<<< HEAD
	/* POLAR: use fsync to make sure data flush to disk */
	if (POLAR_FILE_IN_SHARED_STORAGE())
	{
		(void) polar_fsync(fd);
		return;
	}

	switch (sync_method)
=======
	char	   *msg = NULL;
	instr_time	start;

	Assert(tli != 0);

	/*
	 * Quick exit if fsync is disabled or write() has already synced the WAL
	 * file.
	 */
	if (!enableFsync ||
		wal_sync_method == WAL_SYNC_METHOD_OPEN ||
		wal_sync_method == WAL_SYNC_METHOD_OPEN_DSYNC)
		return;

	/* Measure I/O timing to sync the WAL file */
	if (track_wal_io_timing)
		INSTR_TIME_SET_CURRENT(start);
	else
		INSTR_TIME_SET_ZERO(start);

	pgstat_report_wait_start(WAIT_EVENT_WAL_SYNC);
	switch (wal_sync_method)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	{
		case WAL_SYNC_METHOD_FSYNC:
			if (pg_fsync_no_writethrough(fd) != 0)
				msg = _("could not fsync file \"%s\": %m");
			break;
#ifdef HAVE_FSYNC_WRITETHROUGH
		case WAL_SYNC_METHOD_FSYNC_WRITETHROUGH:
			if (pg_fsync_writethrough(fd) != 0)
				msg = _("could not fsync write-through file \"%s\": %m");
			break;
#endif
		case WAL_SYNC_METHOD_FDATASYNC:
			if (pg_fdatasync(fd) != 0)
				msg = _("could not fdatasync file \"%s\": %m");
			break;
		case WAL_SYNC_METHOD_OPEN:
		case WAL_SYNC_METHOD_OPEN_DSYNC:
			/* not reachable */
			Assert(false);
			break;
		default:
			ereport(PANIC,
					errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg_internal("unrecognized \"wal_sync_method\": %d", wal_sync_method));
			break;
	}

	/* PANIC if failed to fsync */
	if (msg)
	{
		char		xlogfname[MAXFNAMELEN];
		int			save_errno = errno;

		XLogFileName(xlogfname, tli, segno, wal_segment_size);
		errno = save_errno;
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg(msg, xlogfname)));
	}

	pgstat_report_wait_end();

	/*
	 * Increment the I/O timing and the number of times WAL files were synced.
	 */
	if (track_wal_io_timing)
	{
		instr_time	end;

		INSTR_TIME_SET_CURRENT(end);
		INSTR_TIME_ACCUM_DIFF(PendingWalStats.wal_sync_time, end, start);
	}

	PendingWalStats.wal_sync++;
}

/*
 * do_pg_backup_start is the workhorse of the user-visible pg_backup_start()
 * function. It creates the necessary starting checkpoint and constructs the
 * backup state and tablespace map.
 *
 * Input parameters are "state" (the backup state), "fast" (if true, we do
 * the checkpoint in immediate mode to make it faster), and "tablespaces"
 * (if non-NULL, indicates a list of tablespaceinfo structs describing the
 * cluster's tablespaces.).
 *
 * The tablespace map contents are appended to passed-in parameter
 * tablespace_map and the caller is responsible for including it in the backup
 * archive as 'tablespace_map'. The tablespace_map file is required mainly for
 * tar format in windows as native windows utilities are not able to create
 * symlinks while extracting files from tar. However for consistency and
 * platform-independence, we do it the same way everywhere.
 *
 * It fills in "state" with the information required for the backup, such
 * as the minimum WAL location that must be present to restore from this
 * backup (starttli) and the corresponding timeline ID (starttli).
 *
 * Every successfully started backup must be stopped by calling
 * do_pg_backup_stop() or do_pg_abort_backup(). There can be many
 * backups active at the same time.
 *
 * It is the responsibility of the caller of this function to verify the
 * permissions of the calling user!
 */
void
do_pg_backup_start(const char *backupidstr, bool fast, List **tablespaces,
				   BackupState *state, StringInfo tblspcmapfile)
{
<<<<<<< HEAD
	bool		exclusive = (labelfile == NULL);
	bool		backup_started_in_recovery = false;
	XLogRecPtr	checkpointloc;
	XLogRecPtr	startpoint;
	TimeLineID	starttli;
	pg_time_t	stamp_time;
	char		strfbuf[128];
	char		xlogfilename[MAXFNAMELEN];
	XLogSegNo	_logSegNo;
	struct stat stat_buf;

	/* POLAR */
	int			fd = 0;
	int			flags = 0;
	char		labelfile_path[MAXPGPATH] = "";
	char		tblspcmapfile_path[MAXPGPATH] = "";

	elog(LOG, "POLAR: begin pg_start_backup: checkpoint: %s, "
				"lazy checkpoint: %s, fullpage write: %s, switch wal: %s",
				polar_enable_checkpoint_in_backup ? "true" : "false",
				polar_enable_lazy_checkpoint_in_backup ? "true" : "false",
				polar_enable_full_page_write_in_backup ? "true" : "false",
				polar_enable_switch_wal_in_backup ? "true" : "false");

	if (!POLAR_FILE_IN_SHARED_STORAGE())
	{
		polar_make_file_path_level2(labelfile_path, BACKUP_LABEL_FILE);
		polar_make_file_path_level2(tblspcmapfile_path, TABLESPACE_MAP);
	}
	else if (exclusive)
	{
		polar_make_file_path_level2(labelfile_path, POLAR_EXCLUSIVE_BACKUP_LABEL_FILE);
		polar_make_file_path_level2(tblspcmapfile_path, POLAR_EXCLUSIVE_TABLESPACE_MAP);
	}
	else
	{
		polar_make_file_path_level2(labelfile_path, POLAR_NON_EXCLUSIVE_BACKUP_LABEL_FILE);
		polar_make_file_path_level2(tblspcmapfile_path, POLAR_NON_EXCLUSIVE_TABLESPACE_MAP);
	}
=======
	bool		backup_started_in_recovery;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	Assert(state != NULL);
	backup_started_in_recovery = RecoveryInProgress();

	/*
	 * During recovery, we don't need to check WAL level. Because, if WAL
	 * level is not sufficient, it's impossible to get here during recovery.
	 */
	if (!backup_started_in_recovery && !XLogIsNeeded())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("WAL level not sufficient for making an online backup"),
				 errhint("\"wal_level\" must be set to \"replica\" or \"logical\" at server start.")));

	if (strlen(backupidstr) > MAXPGPATH)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("backup label too long (max %d bytes)",
						MAXPGPATH)));

	strlcpy(state->name, backupidstr, sizeof(state->name));

	/*
	 * Mark backup active in shared memory.  We must do full-page WAL writes
	 * during an on-line backup even if not doing so at other times, because
	 * it's quite possible for the backup dump to obtain a "torn" (partially
	 * written) copy of a database page if it reads the page concurrently with
	 * our write to the same page.  This can be fixed as long as the first
	 * write to the page in the WAL sequence is a full-page write. Hence, we
	 * increment runningBackups then force a CHECKPOINT, to ensure there are
	 * no dirty pages in shared memory that might get dumped while the backup
	 * is in progress without having a corresponding WAL record.  (Once the
	 * backup is complete, we need not force full-page writes anymore, since
	 * we expect that any pages not modified during the backup interval must
	 * have been correctly captured by the backup.)
	 *
	 * Note that forcing full-page writes has no effect during an online
	 * backup from the standby.
	 *
	 * We must hold all the insertion locks to change the value of
	 * runningBackups, to ensure adequate interlocking against
	 * XLogInsertRecord().
	 */
	WALInsertLockAcquireExclusive();
<<<<<<< HEAD
	if (exclusive)
	{
		/*
		 * At first, mark that we're now starting an exclusive backup, to
		 * ensure that there are no other sessions currently running
		 * pg_start_backup() or pg_stop_backup().
		 */
		if (XLogCtl->Insert.exclusiveBackupState != EXCLUSIVE_BACKUP_NONE)
		{
			WALInsertLockRelease();
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("a backup is already in progress"),
					 errhint("Run pg_stop_backup() and try again.")));
		}
		XLogCtl->Insert.exclusiveBackupState = EXCLUSIVE_BACKUP_STARTING;
	}
	else
		XLogCtl->Insert.nonExclusiveBackups++;
	/* POLAR: do full-page write if polar_enable_full_page_write_in_backup */
	if (polar_enable_full_page_write_in_backup)
		XLogCtl->Insert.forcePageWrites = true;
=======
	XLogCtl->Insert.runningBackups++;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	WALInsertLockRelease();

	/*
	 * Ensure we decrement runningBackups if we fail below. NB -- for this to
	 * work correctly, it is critical that sessionBackupState is only updated
	 * after this block is over.
	 */
	PG_ENSURE_ERROR_CLEANUP(do_pg_abort_backup, DatumGetBool(true));
	{
		bool		gotUniqueStartpoint = false;
		DIR		   *tblspcdir;
		struct dirent *de;
		tablespaceinfo *ti;
		int			datadirpathlen;
		char		polar_path[MAXPGPATH];

		/*
		 * Force an XLOG file switch before the checkpoint, to ensure that the
		 * WAL segment the checkpoint is written to doesn't contain pages with
		 * old timeline IDs.  That would otherwise happen if you called
		 * pg_backup_start() right after restoring from a PITR archive: the
		 * first WAL segment containing the startup checkpoint has pages in
		 * the beginning with the old timeline ID.  That can cause trouble at
		 * recovery: we won't have a history file covering the old timeline if
		 * pg_wal directory was not included in the base backup and the WAL
		 * archive was cleared too before starting the backup.
		 *
		 * This also ensures that we have emitted a WAL page header that has
		 * XLP_BKP_REMOVABLE off before we emit the checkpoint record.
		 * Therefore, if a WAL archiver (such as pglesslog) is trying to
		 * compress out removable backup blocks, it won't remove any that
		 * occur after this point.
		 *
		 * During recovery, we skip forcing XLOG file switch, which means that
		 * the backup taken during recovery is not available for the special
		 * recovery case described above.
		 */
		/* POLAR: do switch xlog if polar_enable_switch_wal_in_backup */
		if (!backup_started_in_recovery && polar_enable_switch_wal_in_backup)
			RequestXLogSwitch(false);

		do
		{
			bool		checkpointfpw;

			/*
			 * Force a CHECKPOINT.  Aside from being necessary to prevent torn
			 * page problems, this guarantees that two successive backup runs
			 * will have different checkpoint positions and hence different
			 * history file names, even if nothing happened in between.
			 *
			 * During recovery, establish a restartpoint if possible. We use
			 * the last restartpoint as the backup starting checkpoint. This
			 * means that two successive backup runs can have same checkpoint
			 * positions.
			 *
			 * Since the fact that we are executing do_pg_backup_start()
			 * during recovery means that checkpointer is running, we can use
			 * RequestCheckpoint() to establish a restartpoint.
			 *
			 * We use CHECKPOINT_IMMEDIATE only if requested by user (via
			 * passing fast = true).  Otherwise this can take awhile.
			 */
			/* POLAR: do checkpoint if polar_enable_checkpoint_in_backup */
			if (polar_enable_checkpoint_in_backup)
			{
				/* POLAR: request lazy checkpoint if enable polar_enable_lazy_checkpoint_in_backup */
				flags = CHECKPOINT_FORCE | CHECKPOINT_WAIT | (fast ? CHECKPOINT_IMMEDIATE : 0);
				if (polar_enable_lazy_checkpoint_in_backup && polar_lazy_checkpoint_enabled())
					RequestCheckpoint(flags | CHECKPOINT_LAZY);
				else
					RequestCheckpoint(flags);
			}

			/*
			 * Now we need to fetch the checkpoint record location, and also
			 * its REDO pointer.  The oldest point in WAL that would be needed
			 * to restore starting from the checkpoint is precisely the REDO
			 * pointer.
			 */
			LWLockAcquire(ControlFileLock, LW_SHARED);
			state->checkpointloc = ControlFile->checkPoint;
			state->startpoint = ControlFile->checkPointCopy.redo;
			state->starttli = ControlFile->checkPointCopy.ThisTimeLineID;
			checkpointfpw = ControlFile->checkPointCopy.fullPageWrites;
			LWLockRelease(ControlFileLock);

			if (backup_started_in_recovery)
			{
				XLogRecPtr	recptr;

				/*
				 * Check to see if all WAL replayed during online backup
				 * (i.e., since last restartpoint used as backup starting
				 * checkpoint) contain full-page writes.
				 */
				SpinLockAcquire(&XLogCtl->info_lck);
				recptr = XLogCtl->lastFpwDisableRecPtr;
				SpinLockRelease(&XLogCtl->info_lck);

				if (!checkpointfpw || state->startpoint <= recptr)
					ereport(ERROR,
							(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
							 errmsg("WAL generated with \"full_page_writes=off\" was replayed "
									"since last restartpoint"),
							 errhint("This means that the backup being taken on the standby "
									 "is corrupt and should not be used. "
									 "Enable \"full_page_writes\" and run CHECKPOINT on the primary, "
									 "and then try an online backup again.")));

				/*
				 * During recovery, since we don't use the end-of-backup WAL
				 * record and don't write the backup history file, the
				 * starting WAL location doesn't need to be unique. This means
				 * that two base backups started at the same time might use
				 * the same checkpoint as starting locations.
				 */
				gotUniqueStartpoint = true;
			}

			/*
			 * If two base backups are started at the same time (in WAL sender
			 * processes), we need to make sure that they use different
			 * checkpoints as starting locations, because we use the starting
			 * WAL location as a unique identifier for the base backup in the
			 * end-of-backup WAL record and when we write the backup history
			 * file. Perhaps it would be better generate a separate unique ID
			 * for each backup instead of forcing another checkpoint, but
			 * taking a checkpoint right after another is not that expensive
			 * either because only few buffers have been dirtied yet.
			 */
			WALInsertLockAcquireExclusive();
			if (XLogCtl->Insert.lastBackupStart < state->startpoint)
			{
				XLogCtl->Insert.lastBackupStart = state->startpoint;
				gotUniqueStartpoint = true;
			}
			WALInsertLockRelease();
		} while (!gotUniqueStartpoint && polar_enable_checkpoint_in_backup);

		/*
		 * Construct tablespace_map file.
		 */
		datadirpathlen = strlen(DataDir);

		/* Collect information about all tablespaces */
<<<<<<< HEAD
		polar_make_file_path_level2(polar_path, "pg_tblspc");
		tblspcdir = polar_allocate_dir(polar_path);
		while ((de = ReadDir(tblspcdir, polar_path)) != NULL)
=======
		tblspcdir = AllocateDir(PG_TBLSPC_DIR);
		while ((de = ReadDir(tblspcdir, PG_TBLSPC_DIR)) != NULL)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		{
			char		fullpath[MAXPGPATH + sizeof(PG_TBLSPC_DIR)];
			char		linkpath[MAXPGPATH];
			char	   *relpath = NULL;
			char	   *s;
			PGFileType	de_type;
			char	   *badp;
			Oid			tsoid;

			/*
			 * Try to parse the directory name as an unsigned integer.
			 *
			 * Tablespace directories should be positive integers that can be
			 * represented in 32 bits, with no leading zeroes or trailing
			 * garbage. If we come across a name that doesn't meet those
			 * criteria, skip it.
			 */
			if (de->d_name[0] < '1' || de->d_name[1] > '9')
				continue;
			errno = 0;
			tsoid = strtoul(de->d_name, &badp, 10);
			if (*badp != '\0' || errno == EINVAL || errno == ERANGE)
				continue;

			snprintf(fullpath, sizeof(fullpath), "%s/%s", PG_TBLSPC_DIR, de->d_name);

			de_type = get_dirent_type(fullpath, de, false, ERROR);

			if (de_type == PGFILETYPE_LNK)
			{
				StringInfoData escapedpath;
				int			rllen;

				rllen = readlink(fullpath, linkpath, sizeof(linkpath));
				if (rllen < 0)
				{
					ereport(WARNING,
							(errmsg("could not read symbolic link \"%s\": %m",
									fullpath)));
					continue;
				}
				else if (rllen >= sizeof(linkpath))
				{
					ereport(WARNING,
							(errmsg("symbolic link \"%s\" target is too long",
									fullpath)));
					continue;
				}
				linkpath[rllen] = '\0';

				/*
				 * Relpath holds the relative path of the tablespace directory
				 * when it's located within PGDATA, or NULL if it's located
				 * elsewhere.
				 */
				if (rllen > datadirpathlen &&
					strncmp(linkpath, DataDir, datadirpathlen) == 0 &&
					IS_DIR_SEP(linkpath[datadirpathlen]))
					relpath = pstrdup(linkpath + datadirpathlen + 1);

				/*
				 * Add a backslash-escaped version of the link path to the
				 * tablespace map file.
				 */
				initStringInfo(&escapedpath);
				for (s = linkpath; *s; s++)
				{
					if (*s == '\n' || *s == '\r' || *s == '\\')
						appendStringInfoChar(&escapedpath, '\\');
					appendStringInfoChar(&escapedpath, *s);
				}
				appendStringInfo(tblspcmapfile, "%s %s\n",
								 de->d_name, escapedpath.data);
				pfree(escapedpath.data);
			}
			else if (de_type == PGFILETYPE_DIR)
			{
				/*
				 * It's possible to use allow_in_place_tablespaces to create
				 * directories directly under pg_tblspc, for testing purposes
				 * only.
				 *
				 * In this case, we store a relative path rather than an
				 * absolute path into the tablespaceinfo.
				 */
				snprintf(linkpath, sizeof(linkpath), "%s/%s",
						 PG_TBLSPC_DIR, de->d_name);
				relpath = pstrdup(linkpath);
			}
			else
			{
				/* Skip any other file type that appears here. */
				continue;
			}

			ti = palloc(sizeof(tablespaceinfo));
<<<<<<< HEAD
			ti->oid = pstrdup(de->d_name);
			ti->path = pstrdup(buflinkpath.data);
			ti->rpath = relpath ? pstrdup(relpath) : NULL;
			ti->size = infotbssize ? sendTablespace(fullpath, true) : -1;
			ti->polar_shared = POLAR_FILE_IN_SHARED_STORAGE();
=======
			ti->oid = tsoid;
			ti->path = pstrdup(linkpath);
			ti->rpath = relpath;
			ti->size = -1;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

			if (tablespaces)
				*tablespaces = lappend(*tablespaces, ti);
		}
		FreeDir(tblspcdir);

<<<<<<< HEAD
		/*
		 * Construct backup label file
		 */
		if (exclusive)
			labelfile = makeStringInfo();

		/* Use the log timezone here, not the session timezone */
		stamp_time = (pg_time_t) time(NULL);
		pg_strftime(strfbuf, sizeof(strfbuf),
					"%Y-%m-%d %H:%M:%S %Z",
					pg_localtime(&stamp_time, log_timezone));
		appendStringInfo(labelfile, "START WAL LOCATION: %X/%X (file %s)\n",
						 (uint32) (startpoint >> 32), (uint32) startpoint, xlogfilename);
		appendStringInfo(labelfile, "CHECKPOINT LOCATION: %X/%X\n",
						 (uint32) (checkpointloc >> 32), (uint32) checkpointloc);
		appendStringInfo(labelfile, "BACKUP METHOD: %s\n",
						 exclusive ? "pg_start_backup" : "streamed");
		appendStringInfo(labelfile, "BACKUP FROM: %s\n",
						 backup_started_in_recovery ? "standby" : "master");
		appendStringInfo(labelfile, "START TIME: %s\n", strfbuf);
		appendStringInfo(labelfile, "LABEL: %s\n", backupidstr);
		appendStringInfo(labelfile, "START TIMELINE: %u\n", starttli);

		/*
		 * Okay, write the file, or return its contents to caller.
		 */
		/* POLAR: write the lablefile in exclusive or (non-exclusive and not-replica) mode */
		if (exclusive || (!exclusive && POLAR_FILE_IN_SHARED_STORAGE() && !polar_in_replica_mode() ))
		{
			/*
			 * Check for existing backup label --- implies a backup is already
			 * running.  (XXX given that we checked exclusiveBackupState
			 * above, maybe it would be OK to just unlink any such label
			 * file?)
			 */
			if (polar_stat(labelfile_path, &stat_buf) != 0)
			{
				if (errno != ENOENT)
					ereport(ERROR,
							(errcode_for_file_access(),
							 errmsg("could not stat file \"%s\": %m",
									labelfile_path)));
			}
			else
				/* POLAR: overwrite labelfile, rather than ERROR, for in backup already checked before */
				ereport(WARNING,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("a labelfile \"%s\" is already on disk", labelfile_path),
						 errhint("POLAR: we overwrite it")));

			fd = polar_open_transient_file(labelfile_path, O_WRONLY | O_CREAT | O_TRUNC);

			if (fd < 0)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not create file \"%s\": %m",
								labelfile_path)));
			if (polar_write(fd, labelfile->data, labelfile->len) != labelfile->len ||
				polar_fsync(fd) != 0 ||
				CloseTransientFile(fd) !=0)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not write file \"%s\": %m",
								labelfile_path)));

			/* Write backup tablespace_map file. */
			if (tblspcmapfile->len > 0)
			{
				if (polar_stat(tblspcmapfile_path, &stat_buf) != 0)
				{
					if (errno != ENOENT)
						ereport(ERROR,
								(errcode_for_file_access(),
								 errmsg("could not stat file \"%s\": %m",
										tblspcmapfile_path)));
				}
				else
					/* POLAR: overwrite tblspcmapfile, rather than ERROR, for in backup already checked before */
					ereport(WARNING,
							(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
							 errmsg("a tblspcmapfile \"%s\" is already on disk", tblspcmapfile_path),
							 errhint("POLAR: we overwrite it")));
				fd = polar_open_transient_file(tblspcmapfile_path, O_WRONLY | O_CREAT | O_TRUNC);

				if (fd < 0)
					ereport(ERROR,
							(errcode_for_file_access(),
							 errmsg("could not create file \"%s\": %m",
									tblspcmapfile_path)));
				if (polar_write(fd, tblspcmapfile->data, tblspcmapfile->len) != tblspcmapfile->len ||
					polar_fsync(fd) != 0 ||
					CloseTransientFile(fd) != 0)
					ereport(ERROR,
							(errcode_for_file_access(),
							 errmsg("could not write file \"%s\": %m",
									tblspcmapfile_path)));
			}
		}
=======
		state->starttime = (pg_time_t) time(NULL);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
	PG_END_ENSURE_ERROR_CLEANUP(do_pg_abort_backup, DatumGetBool(true));

	state->started_in_recovery = backup_started_in_recovery;

	/*
	 * Mark that the start phase has correctly finished for the backup.
	 */
<<<<<<< HEAD
	if (exclusive)
	{
		WALInsertLockAcquireExclusive();
		XLogCtl->Insert.exclusiveBackupState = EXCLUSIVE_BACKUP_IN_PROGRESS;

		/* Set session-level lock */
		sessionBackupState = SESSION_BACKUP_EXCLUSIVE;
		WALInsertLockRelease();
	}
	else
		sessionBackupState = SESSION_BACKUP_NON_EXCLUSIVE;

	/* POLAR: log some infomation, and move pfree to the end */
	elog(LOG, "POLAR: finish pg_start_backup, labelfile:\n%s", labelfile->data);

	/* Allocated locally for exclusive backups, so free separately */
	if (exclusive)
	{
		pfree(labelfile->data);
		pfree(labelfile);
		pfree(tblspcmapfile->data);
		pfree(tblspcmapfile);
	}

	/*
	 * We're done.  As a convenience, return the starting WAL location.
	 */
	if (starttli_p)
		*starttli_p = starttli;
	return startpoint;
}

/* Error cleanup callback for pg_start_backup */
static void
pg_start_backup_callback(int code, Datum arg)
{
	bool		exclusive = DatumGetBool(arg);

	/* Update backup counters and forcePageWrites on failure */
	WALInsertLockAcquireExclusive();
	if (exclusive)
	{
		Assert(XLogCtl->Insert.exclusiveBackupState == EXCLUSIVE_BACKUP_STARTING);
		XLogCtl->Insert.exclusiveBackupState = EXCLUSIVE_BACKUP_NONE;
	}
	else
	{
		Assert(XLogCtl->Insert.nonExclusiveBackups > 0);
		XLogCtl->Insert.nonExclusiveBackups--;
	}

	if (XLogCtl->Insert.exclusiveBackupState == EXCLUSIVE_BACKUP_NONE &&
		XLogCtl->Insert.nonExclusiveBackups == 0)
	{
		XLogCtl->Insert.forcePageWrites = false;
	}
	WALInsertLockRelease();
}

/*
 * Error cleanup callback for pg_stop_backup
 */
static void
pg_stop_backup_callback(int code, Datum arg)
{
	bool		exclusive = DatumGetBool(arg);

	/* Update backup status on failure */
	WALInsertLockAcquireExclusive();
	if (exclusive)
	{
		Assert(XLogCtl->Insert.exclusiveBackupState == EXCLUSIVE_BACKUP_STOPPING);
		XLogCtl->Insert.exclusiveBackupState = EXCLUSIVE_BACKUP_IN_PROGRESS;
	}
	WALInsertLockRelease();
=======
	sessionBackupState = SESSION_BACKUP_RUNNING;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Utility routine to fetch the session-level status of a backup running.
 */
SessionBackupState
get_backup_status(void)
{
	return sessionBackupState;
}

/*
 * do_pg_backup_stop
 *
 * Utility function called at the end of an online backup.  It creates history
 * file (if required), resets sessionBackupState and so on.  It can optionally
 * wait for WAL segments to be archived.
 *
 * "state" is filled with the information necessary to restore from this
 * backup with its stop LSN (stoppoint), its timeline ID (stoptli), etc.
 *
 * It is the responsibility of the caller of this function to verify the
 * permissions of the calling user!
 */
void
do_pg_backup_stop(BackupState *state, bool waitforarchive)
{
	bool		backup_stopped_in_recovery = false;
	char		histfilepath[MAXPGPATH];
	char		lastxlogfilename[MAXFNAMELEN];
	char		histfilename[MAXFNAMELEN];
	XLogSegNo	_logSegNo;
<<<<<<< HEAD
	char		ch;
=======
	FILE	   *fp;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	int			seconds_before_warning;
	int			waits = 0;
	bool		reported_waiting = false;

<<<<<<< HEAD
	/* POLAR */
	int			fd = 0;
	StringInfo	historyfile = makeStringInfo();
	char		labelfile_path[MAXPGPATH] = "";
	char		tblspcmapfile_path[MAXPGPATH] = "";

	elog(LOG, "POLAR: begin pg_stop_backup: checkpoint: %s, "
				"lazy checkpoint: %s, fullpage write: %s, switch wal: %s",
				polar_enable_checkpoint_in_backup ? "true" : "false",
				polar_enable_lazy_checkpoint_in_backup ? "true" : "false",
				polar_enable_full_page_write_in_backup ? "true" : "false",
				polar_enable_switch_wal_in_backup ? "true" : "false");

	if (!POLAR_FILE_IN_SHARED_STORAGE())
	{
		polar_make_file_path_level2(labelfile_path, BACKUP_LABEL_FILE);
		polar_make_file_path_level2(tblspcmapfile_path, TABLESPACE_MAP);
	}
	else if (exclusive)
	{
		polar_make_file_path_level2(labelfile_path, POLAR_EXCLUSIVE_BACKUP_LABEL_FILE);
		polar_make_file_path_level2(tblspcmapfile_path, POLAR_EXCLUSIVE_TABLESPACE_MAP);
	}
	else
	{
		polar_make_file_path_level2(labelfile_path, POLAR_NON_EXCLUSIVE_BACKUP_LABEL_FILE);
		polar_make_file_path_level2(tblspcmapfile_path, POLAR_NON_EXCLUSIVE_TABLESPACE_MAP);
	}

	backup_started_in_recovery = RecoveryInProgress();
=======
	Assert(state != NULL);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	backup_stopped_in_recovery = RecoveryInProgress();

	/*
	 * During recovery, we don't need to check WAL level. Because, if WAL
	 * level is not sufficient, it's impossible to get here during recovery.
	 */
	if (!backup_stopped_in_recovery && !XLogIsNeeded())
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("WAL level not sufficient for making an online backup"),
<<<<<<< HEAD
				 errhint("wal_level must be set to \"replica\" or \"logical\" at server start.")));

	if (exclusive)
	{
		/*
		 * At first, mark that we're now stopping an exclusive backup, to
		 * ensure that there are no other sessions currently running
		 * pg_start_backup() or pg_stop_backup().
		 */
		WALInsertLockAcquireExclusive();
		if (XLogCtl->Insert.exclusiveBackupState != EXCLUSIVE_BACKUP_IN_PROGRESS)
		{
			WALInsertLockRelease();
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("exclusive backup not in progress")));
		}
		XLogCtl->Insert.exclusiveBackupState = EXCLUSIVE_BACKUP_STOPPING;
		WALInsertLockRelease();

		/*
		 * Remove backup_label. In case of failure, the state for an exclusive
		 * backup is switched back to in-progress.
		 */
		PG_ENSURE_ERROR_CLEANUP(pg_stop_backup_callback, (Datum) BoolGetDatum(exclusive));
		{
			/*
			 * Read the existing label file into memory.
			 */
			struct stat statbuf;
			int			r;

			if (polar_stat(labelfile_path, &statbuf))
			{
				/* should not happen per the upper checks */
				if (errno != ENOENT)
					ereport(ERROR,
							(errcode_for_file_access(),
							 errmsg("could not stat file \"%s\": %m",
									labelfile_path)));
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("a backup is not in progress")));
			}

			fd = polar_open_transient_file(labelfile_path, O_RDONLY);
			if (fd < 0)
			{
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not read file \"%s\": %m",
								labelfile_path)));
			}
			labelfile = palloc(statbuf.st_size + 1);
			r = polar_read(fd, labelfile, statbuf.st_size);
			labelfile[statbuf.st_size] = '\0';

			/*
			 * Close and remove the backup label file
			 */
			if (r != statbuf.st_size || CloseTransientFile(fd) != 0)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not read file \"%s\": %m",
								labelfile_path)));
			durable_unlink(labelfile_path, ERROR);

			/*
			 * Remove tablespace_map file if present, it is created only if
			 * there are tablespaces.
			 */
			durable_unlink(tblspcmapfile_path, DEBUG1);
		}
		PG_END_ENSURE_ERROR_CLEANUP(pg_stop_backup_callback, (Datum) BoolGetDatum(exclusive));
	}
=======
				 errhint("\"wal_level\" must be set to \"replica\" or \"logical\" at server start.")));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * OK to update backup counter and session-level lock.
	 *
	 * Note that CHECK_FOR_INTERRUPTS() must not occur while updating them,
	 * otherwise they can be updated inconsistently, which might cause
	 * do_pg_abort_backup() to fail.
	 */
	WALInsertLockAcquireExclusive();

	/*
	 * It is expected that each do_pg_backup_start() call is matched by
	 * exactly one do_pg_backup_stop() call.
	 */
	Assert(XLogCtl->Insert.runningBackups > 0);
	XLogCtl->Insert.runningBackups--;

	/*
	 * Clean up session-level lock.
	 *
	 * You might think that WALInsertLockRelease() can be called before
	 * cleaning up session-level lock because session-level lock doesn't need
	 * to be protected with WAL insertion lock. But since
	 * CHECK_FOR_INTERRUPTS() can occur in it, session-level lock must be
	 * cleaned up before it.
	 */
	sessionBackupState = SESSION_BACKUP_NONE;

	WALInsertLockRelease();

	/*
	 * If we are taking an online backup from the standby, we confirm that the
	 * standby has not been promoted during the backup.
	 */
<<<<<<< HEAD
	if (sscanf(labelfile, "START WAL LOCATION: %X/%X (file %24s)%c",
			   &hi, &lo, startxlogfilename,
			   &ch) != 4 || ch != '\n')
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("invalid data in file \"%s\"", labelfile_path)));
	startpoint = ((uint64) hi) << 32 | lo;
	remaining = strchr(labelfile, '\n') + 1;	/* %n is not portable enough */

	/*
	 * Parse the BACKUP FROM line. If we are taking an online backup from the
	 * standby, we confirm that the standby has not been promoted during the
	 * backup.
	 */
	ptr = strstr(remaining, "BACKUP FROM:");
	if (!ptr || sscanf(ptr, "BACKUP FROM: %19s\n", backupfrom) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("invalid data in file \"%s\"", labelfile_path)));
	if (strcmp(backupfrom, "standby") == 0 && !backup_started_in_recovery)
=======
	if (state->started_in_recovery && !backup_stopped_in_recovery)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("the standby was promoted during online backup"),
				 errhint("This means that the backup being taken is corrupt "
						 "and should not be used. "
						 "Try taking another online backup.")));

	/*
	 * During recovery, we don't write an end-of-backup record. We assume that
	 * pg_control was backed up last and its minimum recovery point can be
	 * available as the backup end location. Since we don't have an
	 * end-of-backup record, we use the pg_control value to check whether
	 * we've reached the end of backup when starting recovery from this
	 * backup. We have no way of checking if pg_control wasn't backed up last
	 * however.
	 *
	 * We don't force a switch to new WAL file but it is still possible to
	 * wait for all the required files to be archived if waitforarchive is
	 * true. This is okay if we use the backup to start a standby and fetch
	 * the missing WAL using streaming replication. But in the case of an
	 * archive recovery, a user should set waitforarchive to true and wait for
	 * them to be archived to ensure that all the required files are
	 * available.
	 *
	 * We return the current minimum recovery point as the backup end
	 * location. Note that it can be greater than the exact backup end
	 * location if the minimum recovery point is updated after the backup of
	 * pg_control. This is harmless for current uses.
	 *
	 * XXX currently a backup history file is for informational and debug
	 * purposes only. It's not essential for an online backup. Furthermore,
	 * even if it's created, it will not be archived during recovery because
	 * an archiver is not invoked. So it doesn't seem worthwhile to write a
	 * backup history file during recovery.
	 */
	if (backup_stopped_in_recovery)
	{
		XLogRecPtr	recptr;

		/*
		 * Check to see if all WAL replayed during online backup contain
		 * full-page writes.
		 */
		SpinLockAcquire(&XLogCtl->info_lck);
		recptr = XLogCtl->lastFpwDisableRecPtr;
		SpinLockRelease(&XLogCtl->info_lck);

		if (state->startpoint <= recptr)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("WAL generated with \"full_page_writes=off\" was replayed "
							"during online backup"),
					 errhint("This means that the backup being taken on the standby "
							 "is corrupt and should not be used. "
							 "Enable \"full_page_writes\" and run CHECKPOINT on the primary, "
							 "and then try an online backup again.")));


		LWLockAcquire(ControlFileLock, LW_SHARED);
		state->stoppoint = ControlFile->minRecoveryPoint;
		state->stoptli = ControlFile->minRecoveryPointTLI;
		LWLockRelease(ControlFileLock);
	}
	else
	{
		char	   *history_file;

		/*
		 * Write the backup-end xlog record
		 */
		XLogBeginInsert();
		XLogRegisterData((char *) (&state->startpoint),
						 sizeof(state->startpoint));
		state->stoppoint = XLogInsert(RM_XLOG_ID, XLOG_BACKUP_END);

		/*
		 * Given that we're not in recovery, InsertTimeLineID is set and can't
		 * change, so we can read it without a lock.
		 */
		state->stoptli = XLogCtl->InsertTimeLineID;

		/*
		 * Force a switch to a new xlog segment file, so that the backup is
		 * valid as soon as archiver moves out the current segment file.
		 */
		/* POLAR: do switch xlog if polar_enable_switch_wal_in_backup */
		if (polar_enable_switch_wal_in_backup)
			RequestXLogSwitch(false);

		state->stoptime = (pg_time_t) time(NULL);

		/*
		 * Write the backup history file
		 */
<<<<<<< HEAD
		/*
		 * POLAR: don't create this file in shared mode, but for safety, we can still
		 * reenable it by set polar_enable_create_backup_history_file_in_backup = on.
		 */
		if (!POLAR_FILE_IN_SHARED_STORAGE() || polar_enable_create_backup_history_file_in_backup)
		{
			XLByteToSeg(startpoint, _logSegNo, wal_segment_size);
			BackupHistoryFilePath(histfilepath, stoptli, _logSegNo,
								startpoint, wal_segment_size);
			fd = polar_open_transient_file(histfilepath, O_WRONLY | O_CREAT);
			if (fd < 0)
				ereport(ERROR,
						(errcode_for_file_access(),
						errmsg("could not create file \"%s\": %m",
								histfilepath)));
			appendStringInfo(historyfile, "START WAL LOCATION: %X/%X (file %s)\n",
							(uint32) (startpoint >> 32), (uint32) startpoint, startxlogfilename);
			appendStringInfo(historyfile, "STOP WAL LOCATION: %X/%X (file %s)\n",
							(uint32) (stoppoint >> 32), (uint32) stoppoint, stopxlogfilename);
			appendStringInfo(historyfile, "%s", remaining);
			appendStringInfo(historyfile, "STOP TIME: %s\n", strfbuf);
			appendStringInfo(historyfile, "STOP TIMELINE: %u\n", stoptli);

			if (polar_write(fd, historyfile->data, historyfile->len) != historyfile->len ||
				polar_fsync(fd) != 0 ||
				CloseTransientFile(fd) !=0)
				ereport(ERROR,
						(errcode_for_file_access(),
						errmsg("could not write file \"%s\": %m",
								histfilepath)));
		}
=======
		XLByteToSeg(state->startpoint, _logSegNo, wal_segment_size);
		BackupHistoryFilePath(histfilepath, state->stoptli, _logSegNo,
							  state->startpoint, wal_segment_size);
		fp = AllocateFile(histfilepath, "w");
		if (!fp)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not create file \"%s\": %m",
							histfilepath)));

		/* Build and save the contents of the backup history file */
		history_file = build_backup_content(state, true);
		fprintf(fp, "%s", history_file);
		pfree(history_file);

		if (fflush(fp) || ferror(fp) || FreeFile(fp))
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write file \"%s\": %m",
							histfilepath)));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

		/*
		 * Clean out any no-longer-needed history files.  As a side effect,
		 * this will post a .ready file for the newly created history file,
		 * notifying the archiver that history file may be archived
		 * immediately.
		 */
		CleanupBackupHistory();
	}

	/*
	 * If archiving is enabled, wait for all the required WAL files to be
	 * archived before returning. If archiving isn't enabled, the required WAL
	 * needs to be transported via streaming replication (hopefully with
	 * wal_keep_size set high enough), or some more exotic mechanism like
	 * polling and copying files from pg_wal with script. We have no knowledge
	 * of those mechanisms, so it's up to the user to ensure that he gets all
	 * the required WAL.
	 *
	 * We wait until both the last WAL file filled during backup and the
	 * history file have been archived, and assume that the alphabetic sorting
	 * property of the WAL files ensures any earlier WAL files are safely
	 * archived as well.
	 *
	 * We wait forever, since archive_command is supposed to work and we
	 * assume the admin wanted his backup to work completely. If you don't
	 * wish to wait, then either waitforarchive should be passed in as false,
	 * or you can set statement_timeout.  Also, some notices are issued to
	 * clue in anyone who might be doing this interactively.
	 */

<<<<<<< HEAD
	if (polar_enable_switch_wal_in_backup && waitforarchive &&
		((!backup_started_in_recovery && XLogArchivingActive()) ||
		 (backup_started_in_recovery && XLogArchivingAlways())))
=======
	if (waitforarchive &&
		((!backup_stopped_in_recovery && XLogArchivingActive()) ||
		 (backup_stopped_in_recovery && XLogArchivingAlways())))
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	{
		XLByteToPrevSeg(state->stoppoint, _logSegNo, wal_segment_size);
		XLogFileName(lastxlogfilename, state->stoptli, _logSegNo,
					 wal_segment_size);

		XLByteToSeg(state->startpoint, _logSegNo, wal_segment_size);
		BackupHistoryFileName(histfilename, state->stoptli, _logSegNo,
							  state->startpoint, wal_segment_size);

		seconds_before_warning = 60;
		waits = 0;

		while (XLogArchiveIsBusy(lastxlogfilename) ||
			   XLogArchiveIsBusy(histfilename))
		{
			CHECK_FOR_INTERRUPTS();

			if (!reported_waiting && waits > 5)
			{
				ereport(NOTICE,
						(errmsg("base backup done, waiting for required WAL segments to be archived")));
				reported_waiting = true;
			}

			(void) WaitLatch(MyLatch,
							 WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
							 1000L,
							 WAIT_EVENT_BACKUP_WAIT_WAL_ARCHIVE);
			ResetLatch(MyLatch);

			if (++waits >= seconds_before_warning)
			{
				seconds_before_warning *= 2;	/* This wraps in >10 years... */
				ereport(WARNING,
						(errmsg("still waiting for all required WAL segments to be archived (%d seconds elapsed)",
								waits),
						 errhint("Check that your \"archive_command\" is executing properly.  "
								 "You can safely cancel this backup, "
								 "but the database backup will not be usable without all the WAL segments.")));
			}
		}

		ereport(NOTICE,
				(errmsg("all required WAL segments have been archived")));
	}
	else if (!polar_enable_switch_wal_in_backup && waitforarchive)
		ereport(WARNING,
				(errmsg("polar_enable_switch_wal_in_backup is off, last WAL may be missing. Please ensure polar_enable_switch_wal_in_backup is set correctly")));
	else if (waitforarchive)
		ereport(NOTICE,
				(errmsg("WAL archiving is not enabled; you must ensure that all required WAL segments are copied through other means to complete the backup")));
<<<<<<< HEAD

	if (!backup_started_in_recovery)
	{
		elog(LOG, "POLAR: finish pg_stop_backup");
		pfree(historyfile->data);
		pfree(historyfile);
	}

	/*
	 * We're done.  As a convenience, return the ending WAL location.
	 */
	if (stoptli_p)
		*stoptli_p = stoptli;
	return stoppoint;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}


/*
 * do_pg_abort_backup: abort a running backup
 *
 * This does just the most basic steps of do_pg_backup_stop(), by taking the
 * system out of backup mode, thus making it a lot more safe to call from
 * an error handler.
 *
<<<<<<< HEAD
 * The caller can pass 'arg' as 'true' or 'false' to control whether a warning
 * is emitted.
 *
 * NB: This is only for aborting a non-exclusive backup that doesn't write
 * backup_label. A backup started with pg_start_backup() needs to be finished
 * with pg_stop_backup().
 *
 * NB: This gets used as a before_shmem_exit handler, hence the odd-looking
 * signature.
=======
 * 'arg' indicates that it's being called during backup setup; so
 * sessionBackupState has not been modified yet, but runningBackups has
 * already been incremented.  When it's false, then it's invoked as a
 * before_shmem_exit handler, and therefore we must not change state
 * unless sessionBackupState indicates that a backup is actually running.
 *
 * NB: This gets used as a PG_ENSURE_ERROR_CLEANUP callback and
 * before_shmem_exit handler, hence the odd-looking signature.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 */
void
do_pg_abort_backup(int code, Datum arg)
{
<<<<<<< HEAD
	bool	emit_warning = DatumGetBool(arg);

	/*
	 * Quick exit if session is not keeping around a non-exclusive backup
	 * already started.
	 */
	if (sessionBackupState != SESSION_BACKUP_NON_EXCLUSIVE)
		return;

	WALInsertLockAcquireExclusive();
	Assert(XLogCtl->Insert.nonExclusiveBackups > 0);
	XLogCtl->Insert.nonExclusiveBackups--;
=======
	bool		during_backup_start = DatumGetBool(arg);

	/* If called during backup start, there shouldn't be one already running */
	Assert(!during_backup_start || sessionBackupState == SESSION_BACKUP_NONE);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	if (during_backup_start || sessionBackupState != SESSION_BACKUP_NONE)
	{
		WALInsertLockAcquireExclusive();
		Assert(XLogCtl->Insert.runningBackups > 0);
		XLogCtl->Insert.runningBackups--;

		sessionBackupState = SESSION_BACKUP_NONE;
		WALInsertLockRelease();

		if (!during_backup_start)
			ereport(WARNING,
					errmsg("aborting backup due to backend exiting before pg_backup_stop was called"));
	}
<<<<<<< HEAD
	WALInsertLockRelease();

	if (emit_warning)
		ereport(WARNING,
				(errmsg("aborting backup due to backend exiting before pg_stop_backup was called")));
}

/*
 * Register a handler that will warn about unterminated backups at end of
 * session, unless this has already been done.
 */
void
register_persistent_abort_backup_handler(void)
{
	static bool already_done = false;

	if (already_done)
		return;
	before_shmem_exit(do_pg_abort_backup, DatumGetBool(true));
	already_done = true;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Register a handler that will warn about unterminated backups at end of
 * session, unless this has already been done.
 */
void
register_persistent_abort_backup_handler(void)
{
	static bool already_done = false;

	if (already_done)
		return;
	before_shmem_exit(do_pg_abort_backup, DatumGetBool(false));
	already_done = true;
}

/*
 * Get latest async lock record apply position.
 *
 * Exported to allow WALReceiver to read the pointer directly.
 */
XLogRecPtr
polar_get_async_lock_replay_rec_ptr(void)
{
	XLogRecPtr	recptr;

	SpinLockAcquire(&XLogCtl->info_lck);
	recptr = XLogCtl->lockLastReplayedEndRecPtr;
	SpinLockRelease(&XLogCtl->info_lck);

	return recptr;
}

/*
 * Get latest WAL insert pointer
 */
XLogRecPtr
GetXLogInsertRecPtr(void)
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	uint64		current_bytepos;

	SpinLockAcquire(&Insert->insertpos_lck);
	current_bytepos = Insert->CurrBytePos;
	SpinLockRelease(&Insert->insertpos_lck);

	return XLogBytePosToRecPtr(current_bytepos);
}

static XLogRecPtr
polar_get_checkpoint_ptr_nolock(void)
{
	XLogRecPtr	checkpointloc;
	checkpointloc = ControlFile->checkPoint;
	return checkpointloc;
}

static XLogRecPtr
polar_get_xlog_insert_rec_ptr_nolock(void)
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	uint64		current_bytepos;
	current_bytepos = Insert->CurrBytePos;
	return XLogBytePosToRecPtr(current_bytepos);
}

/*
 * Get latest WAL write pointer
 */
XLogRecPtr
GetXLogWriteRecPtr(void)
{
	RefreshXLogWriteResult(LogwrtResult);

	return LogwrtResult.Write;
}

/*
 * Returns the redo pointer of the last checkpoint or restartpoint. This is
 * the oldest point in WAL that we still need, if we have to restart recovery.
 */
void
GetOldestRestartPoint(XLogRecPtr *oldrecptr, TimeLineID *oldtli)
{
	LWLockAcquire(ControlFileLock, LW_SHARED);
	*oldrecptr = ControlFile->checkPointCopy.redo;
	*oldtli = ControlFile->checkPointCopy.ThisTimeLineID;
	LWLockRelease(ControlFileLock);
}

/* Thin wrapper around ShutdownWalRcv(). */
void
XLogShutdownWalRcv(void)
{
	ShutdownWalRcv();

	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	XLogCtl->InstallXLogFileSegmentActive = false;
	LWLockRelease(ControlFileLock);
}

/* Enable WAL file recycling and preallocation. */
void
SetInstallXLogFileSegmentActive(void)
{
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	XLogCtl->InstallXLogFileSegmentActive = true;
	LWLockRelease(ControlFileLock);
}

<<<<<<< HEAD
/*
 * Error context callback for errors occurring during rm_redo().
 */
static void
rm_redo_error_callback(void *arg)
{
	XLogReaderState *record = (XLogReaderState *) arg;

	/* POLAR: If read from queue we only have WAL meta, can not get whole record description */
	if (!record->noPayload)
	{
		StringInfoData buf;

		initStringInfo(&buf);
		xlog_outdesc(&buf, record);

		/*
		 * POLAR: Print simple message in log because of the long data
		 * errcontext(): %s is a WAL record description
		 */
		if (polar_enable_simply_redo_error_log)
			errcontext("WAL redo at %X/%X",
					  (uint32) (record->ReadRecPtr >> 32),
					  (uint32) record->ReadRecPtr);
		else
			errcontext("WAL redo at %X/%X for %s",
					  (uint32) (record->ReadRecPtr >> 32),
					  (uint32) record->ReadRecPtr,
					  buf.data);

		pfree(buf.data);
	}
}

/*
 * BackupInProgress: check if online backup mode is active
 *
 * This is done by checking for existence of the "backup_label" file.
 */
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
bool
IsInstallXLogFileSegmentActive(void)
{
	bool		result;

	LWLockAcquire(ControlFileLock, LW_SHARED);
	result = XLogCtl->InstallXLogFileSegmentActive;
	LWLockRelease(ControlFileLock);

<<<<<<< HEAD
/*
 * CancelBackup: rename the "backup_label" and "tablespace_map"
 *				 files to cancel backup mode
 *
 * If the "backup_label" file exists, it will be renamed to "backup_label.old".
 * Similarly, if the "tablespace_map" file exists, it will be renamed to
 * "tablespace_map.old".
 *
 * Note that this will render an online backup in progress
 * useless. To correctly finish an online backup, pg_stop_backup must be
 * called.
 */
void
CancelBackup(void)
{
	struct stat stat_buf;

	/* if the backup_label file is not there, return */
	if (stat(BACKUP_LABEL_FILE, &stat_buf) < 0)
		return;

	/* remove leftover file from previously canceled backup if it exists */
	unlink(BACKUP_LABEL_OLD);

	if (durable_rename(BACKUP_LABEL_FILE, BACKUP_LABEL_OLD, DEBUG1, false) != 0)
	{
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("online backup mode was not canceled"),
				 errdetail("File \"%s\" could not be renamed to \"%s\": %m.",
						   BACKUP_LABEL_FILE, BACKUP_LABEL_OLD)));
		return;
	}

	/* if the tablespace_map file is not there, return */
	if (stat(TABLESPACE_MAP, &stat_buf) < 0)
	{
		ereport(LOG,
				(errmsg("online backup mode canceled"),
				 errdetail("File \"%s\" was renamed to \"%s\".",
						   BACKUP_LABEL_FILE, BACKUP_LABEL_OLD)));
		return;
	}

	/* remove leftover file from previously canceled backup if it exists */
	unlink(TABLESPACE_MAP_OLD);

	if (durable_rename(TABLESPACE_MAP, TABLESPACE_MAP_OLD, DEBUG1, false) == 0)
	{
		ereport(LOG,
				(errmsg("online backup mode canceled"),
				 errdetail("Files \"%s\" and \"%s\" were renamed to "
						   "\"%s\" and \"%s\", respectively.",
						   BACKUP_LABEL_FILE, TABLESPACE_MAP,
						   BACKUP_LABEL_OLD, TABLESPACE_MAP_OLD)));
	}
	else
	{
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("online backup mode canceled"),
				 errdetail("File \"%s\" was renamed to \"%s\", but "
						   "file \"%s\" could not be renamed to \"%s\": %m.",
						   BACKUP_LABEL_FILE, BACKUP_LABEL_OLD,
						   TABLESPACE_MAP, TABLESPACE_MAP_OLD)));
	}
}

/*
 * Read the XLOG page containing RecPtr into readBuf (if not read already).
 * Returns number of bytes read, if the page is read successfully, or -1
 * in case of errors.  When errors occur, they are ereport'ed, but only
 * if they have not been previously reported.
 *
 * This is responsible for restoring files from archive as needed, as well
 * as for waiting for the requested WAL record to arrive in standby mode.
 *
 * 'emode' specifies the log level used for reporting "file not found" or
 * "end of WAL" situations in archive recovery, or in standby mode when a
 * trigger file is found. If set to WARNING or below, XLogPageRead() returns
 * false in those situations, on higher log levels the ereport() won't
 * return.
 *
 * In standby mode, if after a successful return of XLogPageRead() the
 * caller finds the record it's interested in to be broken, it should
 * ereport the error with the level determined by
 * emode_for_corrupt_record(), and then set lastSourceFailed
 * and call XLogPageRead() again with the same arguments. This lets
 * XLogPageRead() to try fetching the record from another source, or to
 * sleep and retry.
 */
static int
XLogPageRead(XLogReaderState *xlogreader, XLogRecPtr targetPagePtr, int reqLen,
			 XLogRecPtr targetRecPtr, char *readBuf, TimeLineID *readTLI)
{
	static TimeLineID	prev_timeline = 0;

	XLogPageReadPrivate *private =
	(XLogPageReadPrivate *) xlogreader->private_data;
	int			emode = private->emode;
	uint32		targetPageOff;
	XLogSegNo	targetSegNo PG_USED_FOR_ASSERTS_ONLY;
	int			polar_read_rc = -1;
	bool			found = false;
	int			buf_id = -1;

	XLByteToSeg(targetPagePtr, targetSegNo, wal_segment_size);
	targetPageOff = XLogSegmentOffset(targetPagePtr, wal_segment_size);

	/*
	 * See if we need to switch to a new segment because the requested record
	 * is not in the currently open one.
	 */
	if (readFile >= 0 &&
		!XLByteInSeg(targetPagePtr, readSegNo, wal_segment_size))
	{
		/*
		 * Request a restartpoint if we've replayed too much xlog since the
		 * last one.
		 */
		if (bgwriterLaunched)
		{
			if (XLogCheckpointNeeded(readSegNo))
			{
				(void) GetRedoRecPtr();
				if (XLogCheckpointNeeded(readSegNo))
					RequestCheckpoint(CHECKPOINT_CAUSE_XLOG);
			}
		}

		polar_close(readFile);
		readFile = -1;
		readSource = 0;
	}

	XLByteToSeg(targetPagePtr, readSegNo, wal_segment_size);

retry:
	/*
	 * POLAR: update receivedUpto in polardb. Because of logindex, the readSource
	 * in RW and RO startup will not be set XLOG_FROM_STREAM. Xlog pread depends
	 * on receivedUpto, so we should update it manually.
	 */
	if (POLAR_LOGINDEX_ENABLE_XLOG_QUEUE() &&
			AmStartupProcess() && receivedUpto < targetPagePtr + reqLen &&
			polar_in_replica_mode())
		receivedUpto = GetWalRcvWriteRecPtr(NULL, &receiveTLI);

	/* See if we need to retrieve more data */
	if (readFile < 0 ||
		(polar_is_dma_data_node() &&
		 consensusReadableUpto < targetPagePtr + reqLen) ||
		(!polar_is_dma_data_node() && 
		 readSource == XLOG_FROM_STREAM &&
		 receivedUpto < targetPagePtr + reqLen))
	{
		if (!WaitForWALToBecomeAvailable(targetPagePtr + reqLen,
										 private->randAccess,
										 private->fetching_ckpt,
										 targetRecPtr))
		{
			if (readFile >= 0)
				polar_close(readFile);
			readFile = -1;
			readLen = 0;
			readSource = 0;

			return -1;
		}
	}

	/*
	 * At this point, we have the right segment open and if we're streaming we
	 * know the requested record is in it.
	 */
	Assert(readFile != -1);

	/*
	 * If the current segment is being streamed from master, calculate how
	 * much of the current page we have received already. We know the
	 * requested record has been received, but this is for the benefit of
	 * future calls, to allow quick exit at the top of this function.
	 */
	if (polar_is_dma_data_node())
	{

		if (XLogRecPtrIsInvalid(consensusReadableUpto))
			readLen = XLOG_BLCKSZ;
		else if (((targetPagePtr) / XLOG_BLCKSZ) != (consensusReadableUpto / XLOG_BLCKSZ))
			readLen = XLOG_BLCKSZ;
		else
			readLen = XLogSegmentOffset(consensusReadableUpto, wal_segment_size) - targetPageOff;
	}
	else if (readSource == XLOG_FROM_STREAM)
	{
		if (((targetPagePtr) / XLOG_BLCKSZ) != (receivedUpto / XLOG_BLCKSZ))
			readLen = XLOG_BLCKSZ;
		else
			readLen = XLogSegmentOffset(receivedUpto, wal_segment_size) -
				targetPageOff;
	}
	else
		readLen = XLOG_BLCKSZ;

	/* Read the requested page */
	readOff = targetPageOff;

	/* POLAR: In master mode, we won't use xlog buffer for now. */
	if (POLAR_ENABLE_XLOG_BUFFER() && ThisTimeLineID == prev_timeline)
	{
		found = polar_xlog_buffer_lookup(targetPagePtr, readLen, false, true, &buf_id);
		if (found)
		{
			memcpy(readBuf, polar_get_xlog_buffer(buf_id), readLen);
			polar_read_rc = readLen;
		}
		if (buf_id >= 0)
			polar_xlog_buffer_unlock(buf_id);
	}

	if (!found)
	{
		/* POLAR: replace lseek + read with pread */
		if (POLAR_ENABLE_PREAD())
		{
			polar_read_rc = polar_pread_xlog_page(&prev_timeline, targetPagePtr, readBuf);
		}
		else
		{
			if (polar_lseek(readFile, (off_t) readOff, SEEK_SET) < 0) 
			{
				char fname[MAXFNAMELEN] = {0};
				int save_errno = errno;

				XLogFileName(fname, curFileTLI, readSegNo, wal_segment_size);
				errno = save_errno;
				ereport(emode_for_corrupt_record(emode, targetPagePtr + reqLen),
						(errcode_for_file_access(),
								errmsg("could not seek in log segment %s to offset %u: %m",
									   fname, readOff)));

				goto next_record_is_invalid;
			}

			pgstat_report_wait_start(WAIT_EVENT_WAL_READ);
			polar_read_rc = polar_read(readFile, readBuf, XLOG_BLCKSZ);
		}
	}

	if (!found && polar_read_rc != XLOG_BLCKSZ)
	{
		char		fname[MAXFNAMELEN];
		int			save_errno = errno;

		pgstat_report_wait_end();
		XLogFileName(fname, curFileTLI, readSegNo, wal_segment_size);
		errno = save_errno;
		ereport(emode_for_corrupt_record(emode, targetPagePtr + reqLen),
				(errcode_for_file_access(),
				 errmsg("could not read from log segment %s, offset %u, polar_read_rc: %u: %m",
						fname, readOff, polar_read_rc)));
		goto next_record_is_invalid;
	}
	pgstat_report_wait_end();

	Assert(targetSegNo == readSegNo);
	Assert(targetPageOff == readOff);
	Assert(reqLen <= readLen);

	*readTLI = curFileTLI;

	/*
	 * Check the page header immediately, so that we can retry immediately if
	 * it's not valid. This may seem unnecessary, because XLogReadRecord()
	 * validates the page header anyway, and would propagate the failure up to
	 * ReadRecord(), which would retry. However, there's a corner case with
	 * continuation records, if a record is split across two pages such that
	 * we would need to read the two pages from different sources. For
	 * example, imagine a scenario where a streaming replica is started up,
	 * and replay reaches a record that's split across two WAL segments. The
	 * first page is only available locally, in pg_wal, because it's already
	 * been recycled in the master. The second page, however, is not present
	 * in pg_wal, and we should stream it from the master. There is a recycled
	 * WAL segment present in pg_wal, with garbage contents, however. We would
	 * read the first page from the local WAL segment, but when reading the
	 * second page, we would read the bogus, recycled, WAL segment. If we
	 * didn't catch that case here, we would never recover, because
	 * ReadRecord() would retry reading the whole record from the beginning.
	 *
	 * Of course, this only catches errors in the page header, which is what
	 * happens in the case of a recycled WAL segment. Other kinds of errors or
	 * corruption still has the same problem. But this at least fixes the
	 * common case, which can happen as part of normal operation.
	 *
	 * Validating the page header is cheap enough that doing it twice
	 * shouldn't be a big deal from a performance point of view.
	 */
	if (!XLogReaderValidatePageHeader(xlogreader, targetPagePtr, readBuf))
	{
		/* reset any error XLogReaderValidatePageHeader() might have set */
		xlogreader->errormsg_buf[0] = '\0';
		goto next_record_is_invalid;
	}

	return readLen;

next_record_is_invalid:
	lastSourceFailed = true;

	if (readFile >= 0)
		polar_close(readFile);
	readFile = -1;
	readLen = 0;
	readSource = 0;

	/*
	 * POLAR: For startup process, remove the emtry xlog buffers in
	 * crash recovery.
	 */
	if (POLAR_REMOVE_EMPTY_PREAD_XLOG_BUFFER())
		polar_remove_empty_pread_xlog_buffer(targetPagePtr);

	/* In standby-mode, keep trying */
	if (StandbyMode)
		goto retry;
	else
		return -1;
}

/*
 * Open the WAL segment containing WAL location 'RecPtr'.
 *
 * The segment can be fetched via restore_command, or via walreceiver having
 * streamed the record, or it can already be present in pg_wal. Checking
 * pg_wal is mainly for crash recovery, but it will be polled in standby mode
 * too, in case someone copies a new segment directly to pg_wal. That is not
 * documented or recommended, though.
 *
 * If 'fetching_ckpt' is true, we're fetching a checkpoint record, and should
 * prepare to read WAL starting from RedoStartLSN after this.
 *
 * 'RecPtr' might not point to the beginning of the record we're interested
 * in, it might also point to the page or segment header. In that case,
 * 'tliRecPtr' is the position of the WAL record we're interested in. It is
 * used to decide which timeline to stream the requested WAL from.
 *
 * If the record is not immediately available, the function returns false
 * if we're not in standby mode. In standby mode, waits for it to become
 * available.
 *
 * When the requested record becomes available, the function opens the file
 * containing it (if not open already), and returns true. When end of standby
 * mode is triggered by the user, and there is no more WAL available, returns
 * false.
 */
static bool
WaitForWALToBecomeAvailable(XLogRecPtr RecPtr, bool randAccess,
							bool fetching_ckpt, XLogRecPtr tliRecPtr)
{
	static TimestampTz last_fail_time = 0;
	TimestampTz now;
	bool		streaming_reply_sent = false;

	/*-------
	 * Standby mode is implemented by a state machine:
	 *
	 * 1. Read from either archive or pg_wal (XLOG_FROM_ARCHIVE), or just
	 *	  pg_wal (XLOG_FROM_PG_WAL)
	 * 2. Check trigger file
	 * 3. Read from primary server via walreceiver (XLOG_FROM_STREAM)
	 * 4. Rescan timelines
	 * 5. Sleep wal_retrieve_retry_interval milliseconds, and loop back to 1.
	 *
	 * Failure to read from the current source advances the state machine to
	 * the next state.
	 *
	 * 'currentSource' indicates the current state. There are no currentSource
	 * values for "check trigger", "rescan timelines", and "sleep" states,
	 * those actions are taken when reading from the previous source fails, as
	 * part of advancing to the next state.
	 *
	 * If standby mode is turned off while reading WAL from stream, we move
	 * to XLOG_FROM_ARCHIVE and reset lastSourceFailed, to force fetching
	 * the files (which would be required at end of recovery, e.g., timeline
	 * history file) from archive or pg_wal. We don't need to kill WAL receiver
	 * here because it's already stopped when standby mode is turned off at
	 * the end of recovery.
	 *-------
	 */
	if (!InArchiveRecovery)
		currentSource = XLOG_FROM_PG_WAL;
	else if (currentSource == 0 ||
			 (!StandbyMode && currentSource == XLOG_FROM_STREAM))
	{
		lastSourceFailed = false;
		currentSource = XLOG_FROM_ARCHIVE;
	}

	for (;;)
	{
		int			oldSource = currentSource;

		/*
		 * First check if we failed to read from the current source, and
		 * advance the state machine if so. The failure to read might've
		 * happened outside this function, e.g when a CRC check fails on a
		 * record, or within this loop.
		 */
		if (lastSourceFailed)
		{
			switch (currentSource)
			{
				case XLOG_FROM_ARCHIVE:
				case XLOG_FROM_PG_WAL:

					/*
					 * Check to see if the trigger file exists. Note that we
					 * do this only after failure, so when you create the
					 * trigger file, we still finish replaying as much as we
					 * can from archive and pg_wal before failover.
					 */
					if (polar_is_dma_data_node())
					{
						if (becameLeader)
						{
							ShutdownWalRcv();
							return false;
						}
					}
					else if (StandbyMode && CheckForStandbyTrigger())
					{
						ShutdownWalRcv();
						return false;
					}

					/*
					 * Not in standby mode, and we've now tried the archive
					 * and pg_wal.
					 */
					if (!StandbyMode)
						return false;

					/*
					 * POLAR: We will pull up walreceiver at next step, for compatibility
					 * we shut down it for now
					 */
					if (WalRcvStreaming())
						ShutdownWalRcv();
					/* POLAR end */

					/*
					 * If primary_conninfo is set, launch walreceiver to try
					 * to stream the missing WAL.
					 *
					 * If fetching_ckpt is true, RecPtr points to the initial
					 * checkpoint location. In that case, we use RedoStartLSN
					 * as the streaming start position instead of RecPtr, so
					 * that when we later jump backwards to start redo at
					 * RedoStartLSN, we will have the logs streamed already.
					 */
					if (PrimaryConnInfo)
					{
						XLogRecPtr	ptr;
						TimeLineID	tli;

						if (polar_is_dma_data_node())
						{
							if (fetching_ckpt)
								ereport(FATAL,
										(errmsg("could not locate required checkpoint record from local WAL Files")));

							ptr = consensusReceivedUpto > RecPtr ? consensusReceivedUpto : RecPtr; 
							tli = tliOfPointInHistory(consensusReceivedUpto > tliRecPtr ?
									consensusReceivedUpto : tliRecPtr, expectedTLEs);
							/* timeline switched, recieve from the switch point. */
							if (consensusReceivedTLI > 0 && tli > consensusReceivedTLI)
							{
								ptr = tliSwitchPoint(consensusReceivedTLI, expectedTLEs, NULL);	
								/* the recieved point overtake the end point of last received timeline */
								if (consensusReceivedUpto > ptr)
								{
									polar_dma_xlog_truncate(ptr, consensusReceivedTLI, ERROR);
									ConsensusSetXLogFlushedLSN(ptr, consensusReceivedTLI, true);
									consensusReceivedUpto = ptr;
									ereport(DEBUG2, 
											(errmsg("consensus recieved reset to %X/%X timeline %u", 
															(uint32) (consensusReceivedUpto >> 32), (uint32)consensusReceivedUpto,
															consensusReceivedTLI)));
								}
							}
						}
						else if (fetching_ckpt)
						{
							ptr = RedoStartLSN;
							tli = ControlFile->checkPointCopy.ThisTimeLineID;
						}
						else
						{
							/* POLAR: set startpoint as the record begin position when in replica mode */
							if (polar_in_replica_mode())
								ptr = tliRecPtr;
							else
								ptr = RecPtr;

							/*
							 * Use the record begin position to determine the
							 * TLI, rather than the position we're reading.
							 */
							tli = tliOfPointInHistory(tliRecPtr, expectedTLEs);

							if (curFileTLI > 0 && tli < curFileTLI)
								elog(ERROR, "according to history file, WAL location %X/%X belongs to timeline %u, but previous recovered WAL file came from timeline %u",
									 (uint32) (tliRecPtr >> 32),
									 (uint32) tliRecPtr,
									 tli, curFileTLI);
						}

						curFileTLI = tli;
						RequestXLogStreaming(tli, ptr, PrimaryConnInfo,
											 polar_is_dma_data_node() ? POLAR_DMA_REPL_SLOT_NAME() : PrimarySlotName);
						receivedUpto = 0;
					}

					/*
					 * Move to XLOG_FROM_STREAM state in either case. We'll
					 * get immediate failure if we didn't launch walreceiver,
					 * and move on to the next state.
					 */
					currentSource = XLOG_FROM_STREAM;
					break;

				case XLOG_FROM_STREAM:

					/*
					 * Failure while streaming. Most likely, we got here
					 * because streaming replication was terminated, or
					 * promotion was triggered. But we also get here if we
					 * find an invalid record in the WAL streamed from master,
					 * in which case something is seriously wrong. There's
					 * little chance that the problem will just go away, but
					 * PANIC is not good for availability either, especially
					 * in hot standby mode. So, we treat that the same as
					 * disconnection, and retry from archive/pg_wal again. The
					 * WAL in the archive should be identical to what was
					 * streamed, so it's unlikely that it helps, but one can
					 * hope...
					 */

					/*
					 * We should be able to move to XLOG_FROM_STREAM
					 * only in standby mode.
					 */
					Assert(StandbyMode);

					/*
					 * Before we leave XLOG_FROM_STREAM state, make sure that
					 * walreceiver is not active, so that it won't overwrite
					 * WAL that we restore from archive.
					 */
					if (WalRcvStreaming())
						ShutdownWalRcv();

					if (polar_is_dma_data_node())
					{
						XLogRecPtr	latestChunkStart;

						receivedUpto = GetWalRcvWriteRecPtr(&latestChunkStart, &receiveTLI);
						/* Advance received point, including LSN and timeline */
						if (receivedUpto > consensusReceivedUpto || receiveTLI > consensusReceivedTLI)
						{
							if (receivedUpto > consensusReceivedUpto)
								consensusReceivedUpto = receivedUpto;
							if (receiveTLI > consensusReceivedTLI)
								consensusReceivedTLI = receiveTLI;
							ereport(DEBUG2, 
									(errmsg("consensus recieved up to %X/%X timeline %u", 
													(uint32) (consensusReceivedUpto >> 32), (uint32)consensusReceivedUpto,
													consensusReceivedTLI)));
						}
					}

					/*
					 * Before we sleep, re-scan for possible new timelines if
					 * we were requested to recover to the latest timeline.
					 */
					if ((polar_is_dma_data_node() && consensusRecoveryLatestTimeline) 
							|| recoveryTargetIsLatest)
					{
						if (rescanLatestTimeLine())
						{
							currentSource = XLOG_FROM_ARCHIVE;
							break;
						}
					}

					/*
					 * XLOG_FROM_STREAM is the last state in our state
					 * machine, so we've exhausted all the options for
					 * obtaining the requested WAL. We're going to loop back
					 * and retry from the archive, but if it hasn't been long
					 * since last attempt, sleep wal_retrieve_retry_interval
					 * milliseconds to avoid busy-waiting.
					 */
					now = GetCurrentTimestamp();
					if (!TimestampDifferenceExceeds(last_fail_time, now,
													wal_retrieve_retry_interval))
					{
						long		secs,
									wait_time;
						int			usecs;

						TimestampDifference(last_fail_time, now, &secs, &usecs);
						wait_time = wal_retrieve_retry_interval -
							(secs * 1000 + usecs / 1000);

						WaitLatch(&XLogCtl->recoveryWakeupLatch,
								  WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
								  wait_time, WAIT_EVENT_RECOVERY_WAL_STREAM);
						ResetLatch(&XLogCtl->recoveryWakeupLatch);
						now = GetCurrentTimestamp();
					}
					last_fail_time = now;
					currentSource = XLOG_FROM_ARCHIVE;
					break;

				default:
					elog(ERROR, "unexpected WAL source %d", currentSource);
			}
		}
		else if (currentSource == XLOG_FROM_PG_WAL)
		{
			/*
			 * We just successfully read a file in pg_wal. We prefer files in
			 * the archive over ones in pg_wal, so try the next file again
			 * from the archive first.
			 */
			if (InArchiveRecovery)
				currentSource = XLOG_FROM_ARCHIVE;
		}

		if (currentSource != oldSource)
			elog(DEBUG2, "switched WAL source from %s to %s after %s",
				 xlogSourceNames[oldSource], xlogSourceNames[currentSource],
				 lastSourceFailed ? "failure" : "success");

		/*
		 * We've now handled possible failure. Try to read from the chosen
		 * source.
		 */
		lastSourceFailed = false;

		switch (currentSource)
		{
			case XLOG_FROM_ARCHIVE:
			case XLOG_FROM_PG_WAL:
				if (polar_is_dma_data_node())
				{
					Assert(!WalRcvStreaming());

					if (fetching_ckpt || polar_dma_check_local_recovery(RecPtr))
					{
						if (!polar_dma_need_file_switch(tliRecPtr, fetching_ckpt, RecPtr))
						{
							readSource = currentSource;
							XLogReceiptSource = currentSource;
							return true;	
						}
					}
					else
					{
						lastSourceFailed = true;
					}
				}

				/* Close any old file we might have open. */
				if (readFile >= 0)
				{
					polar_close(readFile);
					readFile = -1;
				}
				/* Reset curFileTLI if random fetch. */
				if (randAccess)
					curFileTLI = 0;

				if (polar_is_dma_data_node() && lastSourceFailed)
				{
					break;
				}

				/*
				 * Try to restore the file from archive, or read an existing
				 * file from pg_wal.
				 */
				if (polar_is_dma_data_node())
				{
					TimeLineID tli;

					if (!expectedTLEs)
						expectedTLEs = readTimeLineHistory(recoveryTargetTLI);

					/* In DMA mode, we will read history timeline xlog files. find the
					 * real timeline xlog files, the new timeline xlog may be recieving */
					tli = tliOfPointInHistory(tliRecPtr, expectedTLEs);

					readFile = polar_dma_xlog_file_read_any(readSegNo, LOG, 
							tli, XLOG_FROM_ANY);
				}
				else
				{
				readFile = XLogFileReadAnyTLI(readSegNo, DEBUG2,
											  currentSource == XLOG_FROM_ARCHIVE ? XLOG_FROM_ANY :
											  currentSource);
				}
				if (readFile >= 0)
					return true;	/* success! */

				/*
				 * Nope, not found in archive or pg_wal.
				 */
				lastSourceFailed = true;
				break;

			case XLOG_FROM_STREAM:
				{
					bool		havedata;

					/*
					 * We should be able to move to XLOG_FROM_STREAM
					 * only in standby mode.
					 */
					Assert(StandbyMode);

					/*
					 * Check if WAL receiver is still active.
					 */
					if (!WalRcvStreaming())
					{
						/* 
						 * POLAR: If primary node crash while replica node is still running, 
						 * replica node will hold advancing xlog. Because last old record 
						 * that has been read by replica is not completed in primary node 
						 * and it will be overrided after recovery in primary node. So replica 
						 * need to invalidate last old record to read the new one rather 
						 * than waiting for uncoming xlog data.
						 * While primary node crashes, stream replication will be dropped 
						 * and Walreceiver will be closed, so we do the check here. If this 
						 * Walreceiver close is unexpected, the upper caller XLogPageRead 
						 * will return -1 which make XLogReadRecord return NULL and 
						 * record reading will be retried in replica node.
						 */
						if (polar_in_replica_mode())
						{
							lastSourceFailed = true;
							return false;
						}
						else 
						{
							lastSourceFailed = true;
							break;
						}
					}

					/*
					 * Walreceiver is active, so see if new data has arrived.
					 *
					 * We only advance XLogReceiptTime when we obtain fresh
					 * WAL from walreceiver and observe that we had already
					 * processed everything before the most recent "chunk"
					 * that it flushed to disk.  In steady state where we are
					 * keeping up with the incoming data, XLogReceiptTime will
					 * be updated on each cycle. When we are behind,
					 * XLogReceiptTime will not advance, so the grace time
					 * allotted to conflicting queries will decrease.
					 */
					if (!polar_is_dma_data_node() && RecPtr < receivedUpto)
						havedata = true;
					else
					{
						XLogRecPtr	latestChunkStart;
						bool received = false;

						receivedUpto = GetWalRcvWriteRecPtr(&latestChunkStart, &receiveTLI);

						/* Advance received point, including LSN and timeline */
						if (polar_is_dma_data_node() && 
								(receivedUpto > consensusReceivedUpto || receiveTLI > consensusReceivedTLI))
						{
							if (receivedUpto > consensusReceivedUpto)
								consensusReceivedUpto = receivedUpto;
							if (receiveTLI > consensusReceivedTLI)
								consensusReceivedTLI = receiveTLI;
							received = true;

							ereport(DEBUG2, 
									(errmsg("consensus recieved up to %X/%X timeline %u", 
													(uint32) (consensusReceivedUpto >> 32), (uint32)consensusReceivedUpto,
													consensusReceivedTLI)));

							if (recoveryTargetTLI < consensusReceivedTLI)
								rescanLatestTimeLine();
						}

						if (!polar_is_dma_data_node() && 
								RecPtr < receivedUpto && 
								receiveTLI == curFileTLI)
						{
							havedata = true;
							if (latestChunkStart <= RecPtr)
							{
								XLogReceiptTime = GetCurrentTimestamp();
								SetCurrentChunkStartTime(XLogReceiptTime);
							}
						}
						else if (polar_is_dma_data_node() && 
								RecPtr < consensusReceivedUpto &&
								receiveTLI <= consensusReceivedTLI)
						{
							havedata = true;

							if (received && latestChunkStart <= RecPtr)
							{
								XLogReceiptTime = GetCurrentTimestamp();
								SetCurrentChunkStartTime(XLogReceiptTime);
							}
							/* POLAR: if standby replay delay too much, 
							 * XLogReceiptTime will not advance */
							else if (consensusReceivedUpto - RecPtr < 
									polar_dma_max_standby_wait_delay_size_mb * 1024UL * 1024UL)
							{
								XLogReceiptTime = GetCurrentTimestamp();
							}
						}
						else
							havedata = false;
					}
					if (havedata)
					{
						if (polar_is_dma_data_node())
						{
							if(!polar_dma_recovery_wait_commit(RecPtr))
							{
								lastSourceFailed = true;
								break;
							}
							(void) polar_dma_need_file_switch(tliRecPtr, false, RecPtr);
						}

						/*
						 * Great, streamed far enough.  Open the file if it's
						 * not open already.  Also read the timeline history
						 * file if we haven't initialized timeline history
						 * yet; it should be streamed over and present in
						 * pg_wal by now.  Use XLOG_FROM_STREAM so that source
						 * info is set correctly and XLogReceiptTime isn't
						 * changed.
						 */
						if (readFile < 0)
						{
							TimeLineID	tli;
							if (!expectedTLEs)
								expectedTLEs = readTimeLineHistory(receiveTLI);
							if (polar_is_dma_data_node())
							{
								tli = tliOfPointInHistory(tliRecPtr, expectedTLEs);
								readFile = XLogFileRead(readSegNo, PANIC,
										tli,
										XLOG_FROM_STREAM, false);
							}
							else
							{
								readFile = XLogFileRead(readSegNo, PANIC,
										receiveTLI,
										XLOG_FROM_STREAM, false);
							}
							Assert(readFile >= 0);
						}
						else
						{
							/* just make sure source info is correct... */
							readSource = XLOG_FROM_STREAM;
							XLogReceiptSource = XLOG_FROM_STREAM;
							return true;
						}
						break;
					}

					/*
					 * Data not here yet. Check for trigger, then wait for
					 * walreceiver to wake us up when new WAL arrives.
					 */
					if (polar_is_dma_data_node())
					{
						if (polar_check_recovery_state_change())
						{
							lastSourceFailed = true;
							break;
						}
					}
					else if (CheckForStandbyTrigger())
					{
						/*
						 * Note that we don't "return false" immediately here.
						 * After being triggered, we still want to replay all
						 * the WAL that was already streamed. It's in pg_wal
						 * now, so we just treat this as a failure, and the
						 * state machine will move on to replay the streamed
						 * WAL from pg_wal, and then recheck the trigger and
						 * exit replay.
						 */
						lastSourceFailed = true;
						break;
					}

					/*
					 * Since we have replayed everything we have received so
					 * far and are about to start waiting for more WAL, let's
					 * tell the upstream server our replay location now so
					 * that pg_stat_replication doesn't show stale
					 * information.
					 */
					if (!streaming_reply_sent)
					{
						WalRcvForceReply();
						streaming_reply_sent = true;
					}

					/*
					 * Wait for more WAL to arrive. Time out after 5 seconds
					 * to react to a trigger file promptly.
					 */
					WaitLatch(&XLogCtl->recoveryWakeupLatch,
							  WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
							  5000L, WAIT_EVENT_RECOVERY_WAL_ALL);
					ResetLatch(&XLogCtl->recoveryWakeupLatch);
					break;
				}

			default:
				elog(ERROR, "unexpected WAL source %d", currentSource);
		}

		/*
		 * This possibly-long loop needs to handle interrupts of startup
		 * process.
		 */
		HandleStartupProcInterrupts();
	}

	return false;				/* not reached */
}

/*
 * Determine what log level should be used to report a corrupt WAL record
 * in the current WAL page, previously read by XLogPageRead().
 *
 * 'emode' is the error mode that would be used to report a file-not-found
 * or legitimate end-of-WAL situation.   Generally, we use it as-is, but if
 * we're retrying the exact same record that we've tried previously, only
 * complain the first time to keep the noise down.  However, we only do when
 * reading from pg_wal, because we don't expect any invalid records in archive
 * or in records streamed from master. Files in the archive should be complete,
 * and we should never hit the end of WAL because we stop and wait for more WAL
 * to arrive before replaying it.
 *
 * NOTE: This function remembers the RecPtr value it was last called with,
 * to suppress repeated messages about the same record. Only call this when
 * you are about to ereport(), or you might cause a later message to be
 * erroneously suppressed.
 */
static int
emode_for_corrupt_record(int emode, XLogRecPtr RecPtr)
{
	static XLogRecPtr lastComplaint = 0;

	if (readSource == XLOG_FROM_PG_WAL && emode == LOG)
	{
		if (RecPtr == lastComplaint)
			emode = DEBUG1;
		else
			lastComplaint = RecPtr;
	}
	return emode;
}

/*
 * Check to see whether the user-specified trigger file exists and whether a
 * promote request has arrived.  If either condition holds, return true.
 */
bool
CheckForStandbyTrigger(void)
{
	struct stat stat_buf;
	static bool triggered = false;

	if (triggered)
		return true;

	if (IsPromoteTriggered())
	{
		/*
		 * In 9.1 and 9.2 the postmaster unlinked the promote file inside the
		 * signal handler. It now leaves the file in place and lets the
		 * Startup process do the unlink. This allows Startup to know whether
		 * it should create a full checkpoint before starting up (fallback
		 * mode). Fast promotion takes precedence.
		 */
		/* POLAR: force promote */
		if (stat(POLAR_FORCE_PROMOTE_SIGNAL_FILE, &stat_buf) == 0)
		{
			unlink(POLAR_FORCE_PROMOTE_SIGNAL_FILE);
			fast_promote = true;
			ereport(LOG, (errmsg("received force promote request")));
			
			ResetPromoteTriggered();
			triggered = true;
			return true;
		}
		/* POLAR: normal promote,promote is executed until all wal have been received */
		else if (polar_is_promote_ready())
		{
			if (stat(PROMOTE_SIGNAL_FILE, &stat_buf) == 0)
			{
				unlink(PROMOTE_SIGNAL_FILE);
				unlink(FALLBACK_PROMOTE_SIGNAL_FILE);
				fast_promote = true;
			}
			else if (stat(FALLBACK_PROMOTE_SIGNAL_FILE, &stat_buf) == 0)
			{
				unlink(FALLBACK_PROMOTE_SIGNAL_FILE);
				fast_promote = false;
			}

			ereport(LOG, (errmsg("received promote request")));

			ResetPromoteTriggered();
			triggered = true;
			return true;
		}
		/* POLAR end */
	}

	if (TriggerFile == NULL)
		return false;

	/* POLAR: promote with trigger file, treat as force promote */
	if (stat(TriggerFile, &stat_buf) == 0)
	{
		ereport(LOG,
				(errmsg("trigger file found: %s", TriggerFile)));
		unlink(TriggerFile);
		triggered = true;
		fast_promote = true;
		return true;
	}
	else if (errno != ENOENT)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not stat trigger file \"%s\": %m",
						TriggerFile)));

	return false;
}

/*
 * Remove the files signaling a standby promotion request.
 */
void
RemovePromoteSignalFiles(void)
{
	unlink(PROMOTE_SIGNAL_FILE);
	unlink(FALLBACK_PROMOTE_SIGNAL_FILE);
}

/*
 * Check to see if a promote request has arrived. Should be
 * called by postmaster after receiving SIGUSR1.
 */
bool
CheckPromoteSignal(void)
{
	struct stat stat_buf;

	/* POLAR: add force promote */
	if (stat(PROMOTE_SIGNAL_FILE, &stat_buf) == 0 ||
		stat(FALLBACK_PROMOTE_SIGNAL_FILE, &stat_buf) == 0 ||
		stat(POLAR_FORCE_PROMOTE_SIGNAL_FILE, &stat_buf) == 0)
		return true;

	return false;
}

/*
 * Wake up startup process to replay newly arrived WAL, or to notice that
 * failover has been requested.
 */
void
WakeupRecovery(void)
{
	SetLatch(&XLogCtl->recoveryWakeupLatch);
=======
	return result;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * Update the WalWriterSleeping flag.
 */
void
SetWalWriterSleeping(bool sleeping)
{
	SpinLockAcquire(&XLogCtl->info_lck);
	XLogCtl->WalWriterSleeping = sleeping;
	SpinLockRelease(&XLogCtl->info_lck);
}
<<<<<<< HEAD

/*
 * Schedule a walreceiver wakeup in the main recovery loop.
 */
void
XLogRequestWalReceiverReply(void)
{
	doRequestWalReceiverReply = true;
}

/*
 * POLAR: polardb ro mode
 * include
 * 1 mount remote filesystem with readonly mode
 * 2 only allow read data and wal, not allow write any data to remote file system
 * 3 use polardb stream replication protocol, only get wal write position from rw, not inlucde wal data
 */
bool
polar_in_replica_mode(void)
{
	return (polar_enable_shared_storage_mode &&
		polar_node_type() == POLAR_REPLICA);
}

/* POLAR: reset postmaster local variable before start startup process */
static void
polar_postmaster_reset_local_variables(void)
{
	if (!LocalRecoveryInProgress)
	{
		elog(DEBUG1, "LocalRecoveryInProgress is false, reset it");
		LocalRecoveryInProgress = true;
	}
}

XLogRecPtr
polar_get_oldest_applied_lsn(void)
{
	return pg_atomic_read_u64(
		(pg_atomic_uint64 *) &XLogCtl->replication_slot_oldest_applied_lsn);
}

XLogRecPtr
polar_get_oldest_lock_lsn(void)
{
	return pg_atomic_read_u64(
		(pg_atomic_uint64 *) &XLogCtl->replication_slot_oldest_lock_lsn);
}

void
polar_set_oldest_applied_lsn(XLogRecPtr oldest_apply_lsn, XLogRecPtr oldest_lock_lsn)
{
	if (unlikely(!polar_enable_shared_storage_mode))
		return;

	elog(DEBUG1, "Set oldest apply lsn %X/%X ",
		 (uint32) (oldest_apply_lsn >> 32), (uint32) oldest_apply_lsn);

	elog(DEBUG1, "Set oldest lock lsn %X/%X ",
		 (uint32) (oldest_lock_lsn >> 32), (uint32) oldest_lock_lsn);

	if (unlikely(XLogRecPtrIsInvalid(oldest_apply_lsn)))
		POLAR_LOG_BACKTRACE();

	SpinLockAcquire(&XLogCtl->info_lck);
	if (oldest_apply_lsn > XLogCtl->replication_slot_oldest_applied_lsn ||
		XLogRecPtrIsInvalid(oldest_apply_lsn))
		pg_atomic_write_u64(
			(pg_atomic_uint64 *) &XLogCtl->replication_slot_oldest_applied_lsn,
			oldest_apply_lsn);

	if (oldest_lock_lsn > XLogCtl->replication_slot_oldest_lock_lsn ||
		XLogRecPtrIsInvalid(oldest_lock_lsn))
		pg_atomic_write_u64(
			(pg_atomic_uint64 *) &XLogCtl->replication_slot_oldest_lock_lsn,
			oldest_lock_lsn);
	SpinLockRelease(&XLogCtl->info_lck);
}

XLogRecPtr
polar_get_consistent_lsn(void)
{
	return pg_atomic_read_u64((pg_atomic_uint64 *)&XLogCtl->consistent_lsn);
}

/*
 * polar_set_consistent_lsn - Set the global value of consistent LSN.
 *
 * Firstly, get the oldest LSN among all pages in buffer, which updates the pages.
 * But the oldest LSN could be InvalidXLogRecPtr if no page in the buffer has been changed.
 * In that case, we choose the latest LSN among pages as the consistent LSN because it means
 * all changes in buffer has been flushed to disk. So that we can move much further.
 */
void
polar_set_consistent_lsn(XLogRecPtr consistent_lsn)
{
	bool		updated = false;
	XLogRecPtr  cur_consistent_lsn;

	/* For replica or master in recovery, we don't set the consistent lsn. */
	if (polar_in_replica_mode() || polar_is_master_in_recovery())
	{
		elog(DEBUG1,
			 "replica or master in recovery do not set consistent lsn.");
		return;
	}

	SpinLockAcquire(&XLogCtl->info_lck);
	cur_consistent_lsn = XLogCtl->consistent_lsn;

	if (consistent_lsn > cur_consistent_lsn)
	{
		pg_atomic_write_u64((pg_atomic_uint64 *)&XLogCtl->consistent_lsn,
							consistent_lsn);
		updated = true;
	}
	else if (consistent_lsn < cur_consistent_lsn)
	{
		elog(WARNING, "Setting a consistent lsn %X/%X less than current consistent lsn %X/%X",
			 (uint32) (consistent_lsn >> 32), (uint32) consistent_lsn,
			 (uint32) (cur_consistent_lsn >> 32), (uint32) cur_consistent_lsn);
	}

	SpinLockRelease(&XLogCtl->info_lck);

	if (!updated)
	{
		elog(DEBUG1, "Consistent lsn %X/%X not updated, setting to %X/%X.",
			 (uint32) (cur_consistent_lsn >> 32), (uint32) cur_consistent_lsn,
			 (uint32) (consistent_lsn >> 32), (uint32) consistent_lsn);
	}
	else
	{
		elog(DEBUG1, "Set consistent lsn %X/%X ",
			 (uint32) (consistent_lsn >> 32),
			 (uint32) consistent_lsn);
	}
}

/*
 * Whether to check the legality of the checkpoint. Only polardb master
 * that is not in recovery or standby should check it.
 */
static bool
polar_should_check_checkpoint()
{
	return (polar_enable_shared_storage_mode && !RecoveryInProgress() &&
		polar_is_master()) || polar_is_standby();
}

/* Check whether current checkpoint is allowed. */
static bool
polar_is_checkpoint_legal(XLogRecPtr redo)
{
	XLogRecPtr consistent_lsn = polar_get_consistent_lsn();
	XLogRecPtr oldest_applied_lsn = polar_get_oldest_applied_lsn();
	uint32	freespace;

	/*
	 * like checkpoint.redo, we should add extra header for consistent lsn, 
	 * then to compare with checkpoint.redo.
	 */
	freespace = INSERT_FREESPACE(consistent_lsn);
	if (freespace == 0)
	{
		if (XLogSegmentOffset(consistent_lsn, wal_segment_size) == 0)
			consistent_lsn += SizeOfXLogLongPHD;
		else
			consistent_lsn += SizeOfXLogShortPHD;
	}

	if (redo <= consistent_lsn)
	{
		if (log_checkpoints)
			elog(LOG,
				 "Checkpoint approved. Checkpoint redo lsn %X/%X, consistent lsn %X/%X, oldest apply lsn %X/%X",
				 (uint32) (redo >> 32),
				 (uint32) redo,
				 (uint32) (consistent_lsn >> 32),
				 (uint32) consistent_lsn,
				 (uint32) (oldest_applied_lsn >> 32),
				 (uint32) oldest_applied_lsn);
		return true;
	}

	if ((consistent_lsn > oldest_applied_lsn))
		elog(WARNING,
			 "Checkpoint blocked. Checkpoint redo lsn %X/%X, consistent lsn %X/%X, oldest apply lsn %X/%X",
			 (uint32) (redo >> 32),
			 (uint32) redo,
			 (uint32) (consistent_lsn >> 32),
			 (uint32) consistent_lsn,
			 (uint32) (oldest_applied_lsn >> 32),
			 (uint32) oldest_applied_lsn);

	return false;
}

/*
 * POLAR: get startup replay end ptr including replaying now
 */
XLogRecPtr
polar_get_replay_end_rec_ptr(TimeLineID *replayTLI)
{
	XLogRecPtr	recptr;
	TimeLineID	tli;

	SpinLockAcquire(&XLogCtl->info_lck);
	recptr = XLogCtl->replayEndRecPtr;
	tli = XLogCtl->replayEndTLI;
	SpinLockRelease(&XLogCtl->info_lck);

	if (replayTLI)
		*replayTLI = tli;
	return recptr;
}
/*
 * Tell bgwriter to flush buffer, and wait the consistent lsn greater than the
 * consistent lsn. If we accept a force flush signal, just force flush.
 */
static void
polar_wait_consistent_lsn(XLogRecPtr redo, int flags)
{
	bool receive_shut_down = false;

	while (!polar_is_checkpoint_legal(redo))
	{
		polar_try_to_wake_bgwriter();

		polar_accept_signal_for_checkpoint(flags);

		/*
		 * When a shutdown is received, the bgwriter will be terminated, we
		 * should help myself, do not wait bgwriter to update the consistent lsn.
		 */
		if (polar_checkpointer_recv_shutdown_requested())
		{
			receive_shut_down = true;
			break;
		}

		pg_usleep(polar_check_checkpoint_legal_interval * 1000L);
	}

	if (receive_shut_down)
		polar_flush_buffer_for_shutdown(redo, flags);
}


void
polar_wait_primary_xlog_message(XLogReaderState *state)
{
	if (state != NULL && !WalRcvStreaming())
		polar_try_to_wake_walreceiver(state->currRecPtr, false, state->currRecPtr);

	WaitLatch(&XLogCtl->recoveryWakeupLatch,
		  WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
		  1000L, WAIT_EVENT_RECOVERY_WAL_STREAM);
	ResetLatch(&XLogCtl->recoveryWakeupLatch);

	/* Handle interrupts of startup process */
	HandleStartupProcInterrupts();
}

/*
 * When server shut down, the bgwriter already exit, checkpoint progress flush
 * all dirty buffer at flushlist.
 */
static void
polar_flush_buffer_for_shutdown(XLogRecPtr redo, int flags)
{
	WritebackContext wb_context;
	WritebackContextInit(&wb_context, &backend_flush_after);

	/*
	 * The wal writer process has been terminated, we should flush wal records
	 * before redo lsn that still in the wal buffer.
	 *
	 * Otherwise, there may be some buffers can not be flushed because their
	 * latest lsn greater than oldest apply lsn. The replica can not get more
	 * wal records to replay, so the oldest apply lsn will never be updated.
	 * Server will wait forever.
	 */
	XLogFlush(redo);

	while (!polar_is_checkpoint_legal(redo))
	{
		polar_bg_buffer_sync(&wb_context, flags);
		polar_accept_signal_for_checkpoint(flags);
		pg_usleep(BgWriterDelay * 1000L);
	}
}

void
polar_set_read_and_end_rec_ptr(XLogRecPtr read_rec_ptr, XLogRecPtr end_rec_ptr)
{
	ReadRecPtr = read_rec_ptr;
	EndRecPtr = end_rec_ptr;
}

/*
 * We can accept signal to force flush all buffers, do not control it.
 * If there is replica, the replica will read ahead data, it is unsafe,
 * do not recommend.
 */
static void
polar_accept_signal_for_checkpoint(int flags)
{
	polar_checkpointer_do_reload();
	if (polar_force_flush_buffer)
	{
		polar_set_oldest_applied_lsn(InvalidXLogRecPtr, InvalidXLogRecPtr);
		CheckPointBuffers(flags);
	}
}

/*
 * Faked latest lsn is used to set pd_lsn at PageHeader for visibility map buffer.
 * Like XLogInsertRecord, return XLOG pointer to end of record, if the position
 * is at a page boundary, returns a pointer to the beginning of the page.
 */
XLogRecPtr
polar_get_faked_latest_lsn(void)
{
	uint64	bytepos;
	XLogCtlInsert *insert = &XLogCtl->Insert;

	/* Read the current insert position */
	SpinLockAcquire(&insert->insertpos_lck);
	bytepos = insert->CurrBytePos;
	SpinLockRelease(&insert->insertpos_lck);

	return XLogBytePosToEndRecPtr(bytepos);
}

/*
 * POLAR: Try to start Walreceiver while PrimaryConnInfo is set.
 */
static void
polar_try_to_wake_walreceiver(XLogRecPtr RecPtr, bool fetching_ckpt, XLogRecPtr tliRecPtr)
{
	if (PrimaryConnInfo)
	{
		XLogRecPtr ptr;
		TimeLineID tli;

		if (fetching_ckpt)
		{
			ptr = RedoStartLSN;
			tli = ControlFile->checkPointCopy.ThisTimeLineID;
		}
		else
		{
			ptr = RecPtr;

			/*
			 * Use the record begin position to determine the
			 * TLI, rather than the position we're reading.
			 */
			tli = tliOfPointInHistory(tliRecPtr, expectedTLEs);

			if (curFileTLI > 0 && tli < curFileTLI)
				elog(ERROR,
					 "according to history file, WAL location %X/%X belongs to timeline %u, but previous recovered WAL file came from timeline %u",
					 (uint32) (tliRecPtr >> 32),
					 (uint32) tliRecPtr,
					 tli, curFileTLI);
		}
		curFileTLI = tli;
		RequestXLogStreaming(tli, ptr, PrimaryConnInfo,
							 PrimarySlotName);
		receivedUpto = 0;
	}
 
}

/*
 * POLAR: Pre-read xlog page using polar_pread().
 *
 * The requested page will be looked up in xlog buffer and
 * pre-read pages will be saved in xlog buffer.
 *
 * Caller should guarantee readFile, readLen, readOff are set as expected
 * and polar_enable_xlog_buffer is true.
 */
static int
polar_pread_xlog_page(TimeLineID* prev_timeline, XLogRecPtr targetPagePtr, char *readBuf)
{
	int64 ahead_size = 0;
	int64 delta_size = receivedUpto - targetPagePtr;
	int   guc_ahead_size = polar_read_ahead_xlog_num * XLOG_BLCKSZ;
	int   remaining_size = 0;
	int   polar_read_rc = -1;

	Assert(POLAR_ENABLE_PREAD());
	/*
	 * Read ahead xlog policy:
	 * 1. If current context is in StartupProcess and crash recovey (receivedUpto=0), we read
	 * #polar_read_ahead_xlog_num blocks to cache. Don't forget drop these blocks when error
	 * 2. If delta_size is less than 0, we do not read ahead xlog.
	 * 3. If delta_size is greater than guc parameter, we only read
	 * #polar_read_ahead_xlog_num blocks to cache.
	 * 4. If delta_size is less than XLOG_BLCKSZ, do not read ahead xlog.
	 * 5. If delta_size is less than guc parameter and greater than XLOG_BLCKSZ,
	 * we always read ahead one or more blocks which total size is aligned
	 * in XLOG_BLCKSZ, the last block which size is less than XLOG_BLCKSZ
	 * can not be cached.
	 */
	if (!reachedConsistency && AmStartupProcess())
		ahead_size = guc_ahead_size;
	else if (guc_ahead_size == 0 || delta_size <= 0)
		ahead_size = 0;
	else if (delta_size > guc_ahead_size)
		ahead_size = guc_ahead_size;
	else
		ahead_size = (delta_size / XLOG_BLCKSZ) * XLOG_BLCKSZ;

	/* If cross two segments, we only read current segment. */
	remaining_size = wal_segment_size - XLogSegmentOffset(targetPagePtr, wal_segment_size);
	ahead_size = remaining_size > ahead_size ? ahead_size : remaining_size;
	ahead_size = (ahead_size / XLOG_BLCKSZ) * XLOG_BLCKSZ;

	pgstat_report_wait_start(WAIT_EVENT_WAL_PREAD);
	/*
	 * replica and standby read ahead xlog pages.
	 * master read ahead xlog pages depend on polar_enable_master_xlog_read_ahead
	 */
	if (!POLAR_ENABLE_XLOG_BUFFER() || ahead_size <= 0)
		polar_read_rc = polar_pread(readFile, readBuf, XLOG_BLCKSZ, (off_t) readOff);
	else
		polar_read_rc = polar_pread_xlog_page_with_xlog_buffer(prev_timeline, targetPagePtr, ahead_size, readBuf);

	return polar_read_rc;
}

static int
polar_pread_xlog_page_with_xlog_buffer(
		TimeLineID *prev_timeline, 
		XLogRecPtr target_page_ptr, 
		int64 ahead_size, 
		char *readBuf)
{
	/* POLAR: add one more XLOG_BLCKSZ room to make buffer aligned for directio. */
	static char aligned_read_ahead_xlog_buffer[POLAR_BUFFER_EXTEND_SIZE(XLOG_BLCKSZ * MAX_READ_AHEAD_XLOGS)];
	int	polar_read_rc = -1;
	int	buf_id = -1;
	char *read_ahead_xlog_buffer = (char *) POLAR_BUFFER_ALIGN(aligned_read_ahead_xlog_buffer);

	Assert(ahead_size > 0);
	Assert(receivedUpto >= 0);
	Assert(target_page_ptr / wal_segment_size ==
		   (target_page_ptr + ahead_size - 1) / wal_segment_size);

	/* POLAR: Collect all pages need to do IO. */
	polar_read_rc = polar_pread(readFile, read_ahead_xlog_buffer, ahead_size, (off_t) readOff);

	if (polar_read_rc == ahead_size)
	{
		XLogRecPtr cur_page_lsn = target_page_ptr;
		XLogRecPtr ahead_end = target_page_ptr + ahead_size;
		char 	*buffer = read_ahead_xlog_buffer;

		*prev_timeline = ThisTimeLineID;

		while (cur_page_lsn + XLOG_BLCKSZ < ahead_end)
		{
			if (!polar_xlog_buffer_lookup(cur_page_lsn, XLOG_BLCKSZ, true, false, &buf_id) && buf_id >= 0)
			{
				memcpy(polar_get_xlog_buffer(buf_id), buffer, XLOG_BLCKSZ);
			}

			if (buf_id >= 0)
				polar_xlog_buffer_unlock(buf_id);

			cur_page_lsn += XLOG_BLCKSZ;
			buffer += XLOG_BLCKSZ;
		}

		/* Handle last page which is not a full page */
		if (ahead_end > cur_page_lsn)
		{
			if (!polar_xlog_buffer_lookup(cur_page_lsn, ahead_end - cur_page_lsn, true, false, &buf_id) && buf_id >= 0)
			{
				memcpy(polar_get_xlog_buffer(buf_id), buffer, ahead_end - cur_page_lsn);
			}
			
			if (buf_id >= 0)
				polar_xlog_buffer_unlock(buf_id);
		}

		memcpy(readBuf, read_ahead_xlog_buffer, XLOG_BLCKSZ);
		polar_read_rc = XLOG_BLCKSZ;
	} else {
		ereport(LOG,
			(errmsg(
				"Read xlog size %d is not equal to ahead size "
				INT64_FORMAT,
				polar_read_rc, ahead_size)));

		polar_read_rc = polar_pread(readFile, readBuf, XLOG_BLCKSZ, (off_t) readOff);
	}

	return polar_read_rc;
}

void
polar_load_and_check_controlfile(void)
{
	ReadControlFile();
}

XLogRecPtr
polar_calc_min_used_lsn(bool is_contain_replication_slot)
{
	XLogRecPtr min_lsn;
	XLogRecPtr logindex_start_lsn;

	if (!XLogCtl || !polar_logindex_redo_instance)
		return InvalidXLogRecPtr;

	logindex_start_lsn = polar_logindex_redo_start_lsn(polar_logindex_redo_instance);

	SpinLockAcquire(&XLogCtl->info_lck);
	/* POLAR: replicationSlotMinLSN will be invalid when no replication slots are used. */
	if (XLogRecPtrIsInvalid(XLogCtl->replicationSlotMinLSN) || !is_contain_replication_slot)
		min_lsn = XLogCtl->RedoRecPtr;
	else
		min_lsn = Min(XLogCtl->replicationSlotMinLSN, XLogCtl->RedoRecPtr);

	if (polar_bg_redo_state_is_parallel(polar_logindex_redo_instance))
	{
		XLogRecPtr bg_replayed_lsn = polar_bg_redo_get_replayed_lsn(polar_logindex_redo_instance);
		Assert(!XLogRecPtrIsInvalid(bg_replayed_lsn));
		min_lsn = Min(min_lsn, bg_replayed_lsn);
	}
	SpinLockRelease(&XLogCtl->info_lck);

	if (!XLogRecPtrIsInvalid(logindex_start_lsn))
		min_lsn = Min(min_lsn, logindex_start_lsn);

	/* Keep the wal for flashback */
	return polar_get_flashback_keep_wal(min_lsn);
}

void
polar_update_receipt_time(void)
{
	XLogReceiptTime = GetCurrentTimestamp();
	SetCurrentChunkStartTime(XLogReceiptTime);
}

void
polar_set_receipt_time(TimestampTz rtime)
{
	XLogReceiptTime = rtime;
}

void 
polar_keep_wal_receiver_up(XLogRecPtr lsn)
{
	/* POLAR: to ensure walreceiver stopped, we use WalRcvRunning to check. */
	if (reachedConsistency && !WalRcvRunning() && polar_in_replica_mode())
	{
		polar_try_to_wake_walreceiver(lsn, false, lsn);
		/*
		 * POLAR:We've now handled possible failure. Try to read from the streaming
		 * source.
		 */
		currentSource = XLOG_FROM_STREAM;
		lastSourceFailed = false;
	}
}

void
polar_set_node_type(PolarNodeType node_type)
{
	pg_atomic_write_u32((pg_atomic_uint32 *)&XLogCtl->polar_node_type, node_type);
}

PolarNodeType
polar_node_type(void)
{
	return XLogCtl
	       ? pg_atomic_read_u32((pg_atomic_uint32 *) &XLogCtl->polar_node_type)
	       : polar_node_type_by_file();
}

/*
 * POLAR: judge node type is master or not, no matter whether polar_enable_shared_storage_mode
 * is true.
 *
 * Note that if current node is not a master, this function will call the
 * polar_node_type that will access the XLogCtl. So if the XLogCtl has not been
 * initialed, for example the process is in reaper, please DO NOT call this
 * function. It might be a good idea to use the polar_local_node_type.
 */
bool
polar_in_master_mode(void)
{
	/* the node type must not be unknown */
	Assert(polar_local_node_type != POLAR_UNKNOWN);

	/*
	 * For master, we can use local polar node type, because it would not be
	 * changed after start.
	 */
	if (polar_local_node_type == POLAR_MASTER)
		return true;

	/*
	 * If replica promote to master, we should update the type to master in
	 * shared memory. So if local node type is not master, we should check its
	 * value in shared memory, if it is promoted recently, it's time to record
	 * the latest node type to local.
	 */
	if (polar_node_type() == POLAR_MASTER)
	{
		polar_local_node_type = POLAR_MASTER;
		return true;
	}

	return false;
}

bool
polar_is_master(void)
{
	/* If call this method, we must be a polardb node */
	Assert(polar_enable_shared_storage_mode);
	return polar_in_master_mode();
}

bool
polar_is_dma_data_node(void)
{
	/*
	 * For DMA logger, local polar node type would not be changed after start.
	 */
	return POLAR_ENABLE_DMA() && polar_local_node_type != POLAR_STANDALONE_DATAMAX;
}

bool
polar_is_dma_logger_node(void)
{
	/*
	 * For DMA logger, local polar node type would not be changed after start.
	 */
	return POLAR_ENABLE_DMA() && polar_local_node_type == POLAR_STANDALONE_DATAMAX;
}

/* POLAR: get the distance between consistent_lsn and oldest_lsn */
int64
polar_get_diff_consistent_oldest_lsn(void)
{
	XLogRecPtr cosistent_recptr = polar_get_consistent_lsn();
	XLogRecPtr oldest_applied_recptr = polar_get_oldest_applied_lsn();
	return oldest_applied_recptr - cosistent_recptr;
}

/* POLAR: get the distance between checkpoint_lsn and insert_recptr */
XLogRecPtr
polar_get_diff_checkpoint_flush_lsn(void)
{
	XLogRecPtr checkpoint_recptr = polar_get_checkpoint_ptr_nolock();
	XLogRecPtr insert_recptr = polar_get_xlog_insert_rec_ptr_nolock();

	return (XLogRecPtrIsInvalid(checkpoint_recptr) || XLogRecPtrIsInvalid(insert_recptr)) ?
			InvalidXLogRecPtr : (insert_recptr - checkpoint_recptr);
}

/*
 * POLAR: ControlFile is a static variable, we use extern function to get it
 */
ControlFileData*
polar_get_control_file(void)
{
	return ControlFile;
}

/*
 * POLAR: For crash recovery, there will be some emtry xlog buffers because of pread page.
 * It is necessary to remove them if XLogPageRead gets them. The number of pread xlog buffers
 * depends on polar_read_ahead_xlog_num guc parameter. The same thing doing in XLogReadRecord.
 */
static void
polar_remove_empty_pread_xlog_buffer(XLogRecPtr targetPagePtr)
{
	uint32 len = polar_read_ahead_xlog_num * XLOG_BLCKSZ;
	uint64 offset = 0;
	XLogRecPtr currPage = targetPagePtr - (targetPagePtr % XLOG_BLCKSZ);
	while ((currPage + offset) / XLOG_BLCKSZ <= (currPage + len) / XLOG_BLCKSZ)
	{
		polar_xlog_buffer_remove(currPage + offset);
		offset += XLOG_BLCKSZ;
	}
}

/*
 * POLAR: Wait for checkpointer process to create the last restartpoint
 */
static void
polar_request_last_restartpoint(void)
{
	CheckPoint last_checkpoint;
	bool request;

	SpinLockAcquire(&XLogCtl->info_lck);
	last_checkpoint = XLogCtl->lastCheckPoint;
	SpinLockRelease(&XLogCtl->info_lck);

	LWLockAcquire(ControlFileLock, LW_SHARED);
	request = ControlFile->checkPointCopy.redo < last_checkpoint.redo;
	LWLockRelease(ControlFileLock);

	if (request)
		RequestCheckpoint(CHECKPOINT_IMMEDIATE | CHECKPOINT_WAIT);
}

void
polar_reset_xlog_source(void)
{
	if (readFile >= 0)
	{
		polar_close(readFile);
		readFile = -1;
	}

	lastSourceFailed = false;
	readSource = 0;
	currentSource = 0;
}

/*
 * POLAR: 1. Reset xlogreader because it may read xlog meta from xlog queue and not all members of XLogReaderState are
 * set when read from xlog queue
 * 2. Reset xlog buffer
 */
static XLogReaderState *
polar_reset_xlogreader(XLogReaderState *xlogreader, XLogPageReadPrivate *private)
{
	XLogReaderState *newreader;

	XLogReaderFree(xlogreader);
	xlogreader = NULL;

	MemSet(private, 0, sizeof(XLogPageReadPrivate));

	newreader = XLogReaderAllocate(wal_segment_size, &XLogPageRead, private);
	if (!newreader)
		ereport(ERROR,
			(errcode(ERRCODE_OUT_OF_MEMORY),
			 errmsg("out of memory"),
			 errdetail("Failed while allocating a WAL reading processor.")));

	newreader->system_identifier = ControlFile->system_identifier;
	polar_reset_xlog_source();
	polar_xlog_buffer_reset_all_buffer();

	return newreader;
}

static void
polar_exit_archive_recovery(void)
{
	/*
	 * We are no longer in archive recovery state.
	 */
	InArchiveRecovery = false;

	/*
	 * Update min recovery point one last time.
	 */
	UpdateMinRecoveryPoint(InvalidXLogRecPtr, true);

	/*
	 * If the ending log segment is still open, close it (to avoid problems on
	 * Windows with trying to rename or delete an open file).
	 */
	if (readFile >= 0)
	{
		polar_close(readFile);
		readFile = -1;
	}

	/*
	 * Rename the config file out of the way, so that we don't accidentally
	 * re-enter archive recovery mode in a subsequent crash.
	 */
	unlink(RECOVERY_COMMAND_DONE);
	durable_rename(RECOVERY_COMMAND_FILE, RECOVERY_COMMAND_DONE, INFO, false);

	ereport(LOG,
			(errmsg("replica logindex archive recovery complete")));
}

/*
 * POLAR: interface to set and get hot_standby_state.
 */
void
polar_set_hot_standby_state(HotStandbyState state)
{
	if (UsedShmemSegAddr != NULL && XLogCtl != NULL)
		XLogCtl->polar_hot_standby_state = state;
}

HotStandbyState
polar_get_hot_standby_state(void)
{
	if (UsedShmemSegAddr != NULL && XLogCtl != NULL)
		return XLogCtl->polar_hot_standby_state;
	else
		return STANDBY_DISABLED;
}

/*
 * POLAR: interface to set and get polar_available_state.
 */
void
polar_set_available_state(bool state)
{
	if (UsedShmemSegAddr != NULL && XLogCtl != NULL)
		XLogCtl->polar_available_state = state;
	polar_update_cluster_info();
}

bool
polar_get_available_state(void)
{
	if (UsedShmemSegAddr != NULL && XLogCtl != NULL)
		return XLogCtl->polar_available_state;
	else
		return true;
}

/*
 * POLAR: update lockLastReplayedEndRecPtr, which will be sent to RW
 */
void
polar_async_update_last_ptr(void)
{
	XLogRecPtr recPtr = polar_get_async_ddl_lock_replay_oldest_ptr();
	if (recPtr != InvalidXLogRecPtr)
	{
		SpinLockAcquire(&XLogCtl->info_lck);
		XLogCtl->lockLastReplayedEndRecPtr = recPtr;
		SpinLockRelease(&XLogCtl->info_lck);
	}
	else
	{
		/* no async lock in replaying, now it's lastReplayedEndRecPtr */
		SpinLockAcquire(&XLogCtl->info_lck);
		XLogCtl->lockLastReplayedEndRecPtr = XLogCtl->lastReplayedEndRecPtr;
		SpinLockRelease(&XLogCtl->info_lck);
	}
}

/*
 * Test whether XLOG data has been commit up to (at least) the given position. 
 *
 * if became leader or leader switch or XLOG term switch. advance the recovery's
 * state machine, and return false.
 */
static bool 
polar_dma_check_local_recovery(XLogRecPtr RecPtr)
{
	bool leader_switch = false;
	bool log_switch = false;
	bool success = true;
	bool first_leader = false;
	
	if (XLogRecPtrIsInvalid(ConsensusGetSyncedLSN()))
		return true;

	/* advance committedUpto */
	if (!becameLeader && consensusCommittedUpto < RecPtr)
	{
		consensusCommittedUpto = ConsensusGetSyncedLSN();
		ereport(LOG, (errmsg("consensus committed LSN advanced, term %ld, timeline: %u, LSN: %X/%X", 
						consensusLogTerm, ThisTimeLineID, 
						(uint32) (consensusCommittedUpto >> 32), (uint32) consensusCommittedUpto)));
	}
				
	while (!becameLeader)
	{
		if (polar_check_and_switch_recovery_state(&leader_switch, &log_switch))
		{
			ereport(WARNING, 
					(errmsg("consensus became leader, term %ld, consensus log at %X/%X "
									"timeline %u. recieved at %X/%X timeline %u", 
									consensusTerm, 
								  (uint32) (consensusLogUpto >> 32), (uint32)consensusLogUpto, 
									consensusLogTLI,
								  (uint32) (consensusReceivedUpto >> 32), (uint32)consensusReceivedUpto,
									consensusReceivedTLI)));

			if (consensusReceivedUpto == 0)
				first_leader = true;

			consensusReceivedUpto = consensusLogUpto;	
			consensusReceivedTLI = consensusLogTLI;
			break;
		}

		/* if consensus leader changed or log mismatch, restart replication */
		if (leader_switch)
		{
			ereport(WARNING, 
					(errmsg("consensus leader changed or log mismatch, term %ld, log term %ld, "
									"divergence at %X/%X timeline %u, recieved at %X/%X timeline %u", 
							consensusTerm, consensusLogTerm, 
							(uint32) (consensusLogUpto >> 32), (uint32)consensusLogUpto, 
							consensusLogTLI,
						 	(uint32) (consensusReceivedUpto >> 32), (uint32)consensusReceivedUpto,
							consensusReceivedTLI)));

			/* If leader changed, shutdown wal receiver */
			if (WalRcvRunning())
				ShutdownWalRcv();

			if (consensusReceivedUpto == 0)
				first_leader = true;

			if (consensusReceivedUpto == 0 || 
					(log_switch && consensusReceivedUpto > consensusLogUpto))
			{
				polar_dma_xlog_truncate(consensusLogUpto, consensusLogTLI, ERROR);
				consensusReceivedUpto = consensusLogUpto;	
				consensusReceivedTLI = consensusLogTLI;
			}
			success = false;
			break;
		}

		/* if consensus log start appending, switch to WAL_STREAM */
		if (consensusTerm > 0)
		{
			ereport(LOG, (errmsg("start appending consensus log, switch to WAL_STREAM"
							", term %ld, recieved at %X/%X timeline %u", consensusLogTerm,
							(uint32) (consensusReceivedUpto >> 32), (uint32) consensusReceivedUpto,
							consensusReceivedTLI)));

			success = false;
			break;
		}

		/* wait consensus intialize finished */
		if (consensusCommittedUpto < RecPtr && ConsensusWaitForLSN(RecPtr, false))
		{
			uint64 last_committed = ConsensusGetSyncedLSN();
			if (last_committed > consensusCommittedUpto)
			{
				consensusCommittedUpto = last_committed;
				ereport(LOG, (errmsg("consensus committed LSN advanced, term %ld, log "
								"term %ld, LSN: %X/%X", consensusTerm, consensusLogTerm, 
								(uint32) (consensusCommittedUpto >> 32), 
								(uint32) consensusCommittedUpto)));
			}
		}

		HandleStartupProcInterrupts();
	}

	/* First time to get or became leader. initialize minRecoveryPoint to committed ptr */
	if (first_leader)
	{
		XLogRecPtr committed_lsn;
		TimeLineID committed_tli;

		if (!InArchiveRecovery)
			InArchiveRecovery = true;

		if (!StandbyMode)
			StandbyMode = true;

		ConsensusGetSyncedLSNAndTLI(&committed_lsn, &committed_tli);

		LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);

		/* If not became leader, enter archive recovery status */
		if (leader_switch)
			ControlFile->state = DB_IN_ARCHIVE_RECOVERY;

		if (ControlFile->minRecoveryPoint == InvalidXLogRecPtr ||
				ControlFile->minRecoveryPoint > committed_lsn)
		{
			ControlFile->minRecoveryPoint = committed_lsn;
			ControlFile->minRecoveryPointTLI = committed_tli;
		}
		/* update local copy */
		minRecoveryPoint = ControlFile->minRecoveryPoint;
		minRecoveryPointTLI = ControlFile->minRecoveryPointTLI;

		/*
		 * The startup process can update its local copy of
		 * minRecoveryPoint from this point.
		 */
		updateMinRecoveryPoint = true;

		UpdateControlFile();

		/*
		 * We update SharedRecoveryState while holding the lock on
		 * ControlFileLock so both states are consistent in shared
		 * memory.
		 */
		if (leader_switch)
		{
			SpinLockAcquire(&XLogCtl->info_lck);
			XLogCtl->SharedRecoveryState = RECOVERY_STATE_ARCHIVE;
			SpinLockRelease(&XLogCtl->info_lck);
		}

		LWLockRelease(ControlFileLock);

		CheckRecoveryConsistency();

		ereport(LOG, (errmsg("consensus reset minRecoveryPoint, timeline: %u, LSN: %X/%X", 
						minRecoveryPointTLI, 
						(uint32) (minRecoveryPoint >> 32), (uint32) minRecoveryPoint)));
	}

	if (becameLeader)
	{
		if (consensusLogUpto > RecPtr)
		{
			if (consensusCommittedUpto < RecPtr && ConsensusWaitForLSN(RecPtr, true))
			{
				uint64 last_committed = ConsensusGetSyncedLSN();
				if (last_committed > consensusCommittedUpto)
				{
					consensusCommittedUpto = last_committed;
					ereport(LOG, (errmsg("consensus committed LSN advanced, term %ld, log "
									"term %ld, LSN: %X/%X", consensusTerm, consensusLogTerm, 
									(uint32) (consensusCommittedUpto >> 32), 
									(uint32) consensusCommittedUpto)));
				}
			}
		}
		else if (XLogRecPtrIsInvalid(ControlFile->backupStartPoint) &&
				!ControlFile->backupEndRequired &&
				consensusRecoveryLatestTimeline && 
				recoveryTarget == RECOVERY_TARGET_UNSET)
		{
			success = false;
		}
	}

	return success;
}

/*
 * Ensure that all XLOG data through the given position is committed. 
 * if became leader or leader switch or XLOG term switch, return false.
 */
static bool
polar_dma_recovery_wait_commit(XLogRecPtr RecPtr)
{
	uint64 	last_committed;
	bool 		success = true;

	/*
	 * After log term switched. only if swith to XLOG_FROM_ARCHIVE immediate.
	 * local xlog term can switch timely, and then new leader can upgrade
	 */
	if (polar_check_recovery_state_change())
	{
		ereport(WARNING, (errmsg("Consensus recovery state machine move off from "
						"streaming source for reason of state change "
						"term %ld, LSN: %X/%X", 
						consensusLogTerm, (uint32) (RecPtr >> 32), (uint32) RecPtr)));
		return false;
	}

	/* 
	 * Wait committed LSN arrived, if became leader 
	 * or log term switched, swith to XLOG_FROM_ARCHIVE 
	 */
	while (consensusCommittedUpto <= RecPtr)
	{
		if (!ConsensusWaitForLSN(RecPtr, false))
		{
			if (polar_check_recovery_state_change() || !WalRcvStreaming())
			{
				ereport(WARNING, (errmsg("ConsensusWaitForLSN failed for reason of state change "
								"or wal reciever shutdown,"
								"	term %ld, LSN: %X/%X", 
								consensusLogTerm, (uint32) (RecPtr >> 32), (uint32) RecPtr)));
				success = false;
				break;
			}
		}
		else
		{
			last_committed = ConsensusGetSyncedLSN();
			if (last_committed > consensusCommittedUpto)
			{
				consensusCommittedUpto = last_committed;
				ereport(LOG, (errmsg("committed LSN advanced, term %ld, LSN: %X/%X", 
								consensusLogTerm, (uint32) (consensusCommittedUpto >> 32), 
								(uint32) consensusCommittedUpto)));
			}
		}
	}

	return success;
}

bool
polar_check_pm_in_state_change(void)
{
	SpinLockAcquire(&XLogCtl->pm_state_lck);
	if (XLogCtl->pmInStateChange)
	{
		SpinLockRelease(&XLogCtl->pm_state_lck);
		/* signal postmaster by the way */
		kill(PostmasterPid, SIGUSR1);
		return true;
	}
	SpinLockRelease(&XLogCtl->pm_state_lck);
	return false;
}

/*
 * Check to sess if consensus state has changed.
 * Before postmaster handled the previous signal, consensus state
 * change will be ignored. Should be called by Consens Serviece
 */
void 
polar_signal_pm_state_change(int state,
					const char *leaderAddr, int leaderPort, uint64 term, 
					uint64 nextAppendTerm, uint32 tli, uint64 logUpto)
{
	bool inStateChange = false;
	PMSignalReason reason = 0;

	Assert(!XLogCtl->pmInStateChange);

	if (XLogCtl->pmState.state == CONSENSUS_STATE_LEADER &&
			state != CONSENSUS_STATE_LEADER)
	{
		reason = PMSIGNAL_CONSENS_DOWNGRADE;
		inStateChange = true;
	}
	else if (XLogCtl->pmState.state == CONSENSUS_STATE_LEADER &&
			state == CONSENSUS_STATE_LEADER && 
			XLogCtl->pmState.term != term)
	{
		reason = PMSIGNAL_CONSENS_LEADER_RESUME;
		inStateChange = true;
	}
	else if ((XLogCtl->pmState.state == CONSENSUS_STATE_FOLLOWER ||
				XLogCtl->pmState.state == CONSENSUS_STATE_DOWN) &&
			state == CONSENSUS_STATE_LEADER)
	{
		reason = PMSIGNAL_CONSENS_UPGRADE;
		inStateChange = true;
	}
	else if ((XLogCtl->pmState.state == CONSENSUS_STATE_FOLLOWER ||
				XLogCtl->pmState.state == CONSENSUS_STATE_DOWN) &&
			state == CONSENSUS_STATE_FOLLOWER)
	{
		reason = PMSIGNAL_CONSENS_LEADER_CHANGE;
		inStateChange = true;
	}

	if (inStateChange)
	{
		SpinLockAcquire(&XLogCtl->pm_state_lck);
		if (reason == PMSIGNAL_CONSENS_LEADER_CHANGE ||
				reason == PMSIGNAL_CONSENS_DOWNGRADE)
		{
			strlcpy(XLogCtl->pmState.leaderAddr, leaderAddr, NI_MAXHOST);
			XLogCtl->pmState.leaderPort = leaderPort;
		}
		XLogCtl->pmState.state = state;
		XLogCtl->pmState.term = term;
		XLogCtl->pmState.xlogTerm = nextAppendTerm;
		XLogCtl->pmState.xlogUpto = logUpto;
		XLogCtl->pmState.xlogTLI = tli;
		XLogCtl->pmInStateChange = true;
		SpinLockRelease(&XLogCtl->pm_state_lck);

		ereport(WARNING,
				(errmsg("polar_signal_pm_state_change, signal Postmaster for reason: %d, "
								"logTerm: %ld, logUpto: %X/%X", reason, nextAppendTerm, 
									 (uint32) (logUpto >> 32), (uint32) logUpto)));

		SendPostmasterSignal(reason);
	}
}

void
polar_signal_recovery_state_change(bool newLeader, bool resumeLeader)
{
#ifdef USE_DMA
	SpinLockAcquire(&XLogCtl->consens_state_lck);

	if (resumeLeader && XLogCtl->inLeaderState)
	{
		ConsensusSetXLogTerm(XLogCtl->pmState.xlogTerm);	
	}
	else if (newLeader || resumeLeader)
	{
		XLogCtl->becameLeader = true;
		XLogCtl->consensusTerm = XLogCtl->pmState.term;
		XLogCtl->consensusLogTerm = XLogCtl->pmState.xlogTerm;
		XLogCtl->consensusLogUpto = XLogCtl->pmState.xlogUpto;
		XLogCtl->consensusLogTLI = XLogCtl->pmState.xlogTLI;
		WakeupRecovery();
	}
	else
	{
		bool with_user_name = (polar_dma_repl_user != NULL && polar_dma_repl_user[0] != '\0');
		bool with_passwd = (polar_dma_repl_password != NULL && polar_dma_repl_password[0] != '\0');
		bool with_application_name = (polar_dma_repl_app_name != NULL && polar_dma_repl_app_name[0] != '\0');

		XLogCtl->becameLeader = false;
		XLogCtl->consensusTerm = XLogCtl->pmState.term;
		XLogCtl->consensusLogTerm = XLogCtl->pmState.xlogTerm;
		XLogCtl->consensusLogUpto = XLogCtl->pmState.xlogUpto;
		XLogCtl->consensusLogTLI = XLogCtl->pmState.xlogTLI;
		snprintf(XLogCtl->PrimaryConnInfoStr, MAXCONNINFO, "host=%s port=%d %s%s %s%s %s%s ",
				XLogCtl->pmState.leaderAddr,
				XLogCtl->pmState.leaderPort,
			  with_user_name ? "user=" : "",	with_user_name ? polar_dma_repl_user : "",	
			  with_passwd ? "password=" : "",	with_passwd ? polar_dma_repl_password : "",
			  with_application_name ? "application_name=" : "",	with_application_name ? polar_dma_repl_app_name : "");
		WakeupRecovery();
	}
	SpinLockRelease(&XLogCtl->consens_state_lck);

	SpinLockAcquire(&XLogCtl->pm_state_lck);
	XLogCtl->pmInStateChange = false;
	SpinLockRelease(&XLogCtl->pm_state_lck);
#else
	Assert(false);
#endif
}

/*
 * Check to see whether became Leader
 */
static bool
polar_check_recovery_state_change(void)
{
	bool    became_leader;
	uint64  consens_term;
	uint64  consens_log_term;
	XLogRecPtr consens_up_to;

	SpinLockAcquire(&XLogCtl->consens_state_lck);
	became_leader = XLogCtl->becameLeader;
	consens_term = XLogCtl->consensusTerm;
	consens_log_term = XLogCtl->consensusLogTerm;
	consens_up_to = XLogCtl->consensusLogUpto; 
	SpinLockRelease(&XLogCtl->consens_state_lck);

	if (became_leader)
		return true;
	else if (consensusTerm != consens_term)
		return true;
	else if (consensusLogTerm == 0 && consens_log_term > 0)
		return true;
	else if (consensusLogTerm != consens_log_term)
	{
		if (consens_up_to < consensusLogUpto)
		{
			return true;
		}
		consensusLogTerm = consens_log_term;
		ConsensusSetXLogTerm(consens_log_term);	
	}
	return false;
}

/*
 * Check to see whether became Leader, only called by StartupProcess
 */
static bool
polar_check_and_switch_recovery_state(bool *leaderSwitch, bool *logSwitch)
{
	bool new_leader = false;

	*leaderSwitch = false;
	*logSwitch = false;

	SpinLockAcquire(&XLogCtl->consens_state_lck);

	if (XLogCtl->becameLeader && becameLeader)
	{
		/* Do nothing */
	}
	else if (XLogCtl->becameLeader && !becameLeader)
	{
		becameLeader = XLogCtl->becameLeader;
		consensusTerm = XLogCtl->consensusTerm;
		consensusLogTerm = XLogCtl->consensusLogTerm;
		consensusLogUpto = XLogCtl->consensusLogUpto;
		consensusLogTLI = XLogCtl->consensusLogTLI;
		PrimaryConnInfo = NULL;

		if ((consensusRecoveryLatestTimeline || recoveryTargetIsLatest) && 
				recoveryTarget == RECOVERY_TARGET_UNSET)
			fast_promote = true;

		ConsensusSetXLogFlushedLSN(XLogCtl->consensusLogUpto, XLogCtl->consensusLogTLI, true);	
		ConsensusSetXLogTerm(XLogCtl->consensusLogTerm);	

		XLogCtl->inLeaderState = true;

		new_leader = true;
	}
	else if (consensusTerm != XLogCtl->consensusTerm)
	{
		becameLeader = XLogCtl->becameLeader;
		consensusTerm = XLogCtl->consensusTerm;
		consensusLogUpto = XLogCtl->consensusLogUpto;
		consensusLogTLI = XLogCtl->consensusLogTLI;
		PrimaryConnInfo = XLogCtl->PrimaryConnInfoStr;
		*leaderSwitch = true;

		if (consensusLogTerm != XLogCtl->consensusLogTerm)
		{
			consensusLogTerm = XLogCtl->consensusLogTerm;
			ConsensusSetXLogFlushedLSN(XLogCtl->consensusLogUpto, XLogCtl->consensusLogTLI, true);	
			ConsensusSetXLogTerm(XLogCtl->consensusLogTerm);	
			*logSwitch = true;
		}

		XLogCtl->inLeaderState = false;
	}
	else if (consensusLogTerm != XLogCtl->consensusLogTerm)
	{
		becameLeader = XLogCtl->becameLeader;

		/* divergence point go backward after repaired consensus log */
		if (consensusLogUpto == 0 ||
				XLogCtl->consensusLogUpto < consensusLogUpto)
		{
			consensusLogUpto = XLogCtl->consensusLogUpto;
			consensusLogTLI = XLogCtl->consensusLogTLI;
			PrimaryConnInfo = XLogCtl->PrimaryConnInfoStr;
			*leaderSwitch = true;
			*logSwitch = true;
			ConsensusSetXLogFlushedLSN(XLogCtl->consensusLogUpto, XLogCtl->consensusLogTLI, true);	
		}
		consensusLogTerm = XLogCtl->consensusLogTerm;
		ConsensusSetXLogTerm(XLogCtl->consensusLogTerm);	

		XLogCtl->inLeaderState = false;
	}

	SpinLockRelease(&XLogCtl->consens_state_lck);

	return new_leader;
}

/*
 * Ensure that all XLOG data through the given position is committed by consensus.
 */
static bool
polar_dma_xlog_commit(XLogRecPtr record)
{
	bool success = true;
	SpinLockAcquire(&XLogCtl->info_lck);
	ConsensusCommit = XLogCtl->ConsensusCommit;
	SpinLockRelease(&XLogCtl->info_lck);

	if (record <= ConsensusCommit)
	{
		return true;
	}

	START_CRIT_SECTION();
	success = ConsensusWaitForLSN(record, true);
	END_CRIT_SECTION();

	if (success)
	{
		ereport(DEBUG5, (errmsg("polar_dma_xlog_commit committed, LSN: %X/%X", 
						(uint32) (record >> 32),
						(uint32) record)));
	}
	else
	{
		/*
		 * If a wait for consensus commit is pending, we can neither
		 * acknowledge the commit nor raise ERROR or FATAL. And also, 
		 * we can neither abort xact or clean up the state. So in this 
		 * case we issue a WARNING and exit immediately.
		 */
		ereport(WARNING, (errmsg("polar consensus commit failed, LSN: %X/%X", 
						(uint32) (record >> 32),
						(uint32) record)));
		_exit(1);
	}


	ConsensusCommit = record;

	SpinLockAcquire(&XLogCtl->info_lck);
	if (XLogCtl->ConsensusCommit < ConsensusCommit)
		XLogCtl->ConsensusCommit = ConsensusCommit;
	SpinLockRelease(&XLogCtl->info_lck);

	return true;
}

void 
polar_update_last_removed_ptr(char *filename)
{
	UpdateLastRemovedPtr(filename);
}

static int
polar_dma_xlog_file_read_any(XLogSegNo segno, int emode, TimeLineID tli, int source)
{
	char		path[MAXPGPATH];
	int			fd;

	if (source == XLOG_FROM_ANY || source == XLOG_FROM_ARCHIVE)
	{
		fd = XLogFileRead(segno, emode, tli, XLOG_FROM_ARCHIVE, true);
		if (fd != -1)
		{
			elog(DEBUG1, "got WAL segment from archive");
			return fd;
		}
	}

	if (source == XLOG_FROM_ANY || source == XLOG_FROM_PG_WAL)
	{
		fd = XLogFileRead(segno, emode, tli, XLOG_FROM_PG_WAL, true);
		if (fd != -1)
		{
			return fd;
		}
	}

	/* Couldn't find it.  For simplicity, complain about front timeline */
	XLogFilePath(path, tli, segno, wal_segment_size);
	errno = ENOENT;
	ereport(emode,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\": %m", path)));
	return -1;
}

static bool
polar_dma_need_file_switch(XLogRecPtr tliRecPtr, bool fetching_ckpt, XLogRecPtr RecPtr)
{
	TimeLineID tli;
	XLogRecPtr new_readable_upto;

	if (!expectedTLEs)
		expectedTLEs = readTimeLineHistory(recoveryTargetTLI);

	tli = tliOfPointInHistory(tliRecPtr, expectedTLEs);
	
	if (consensusCommittedUpto == 0)
	{
		consensusCommittedUpto = ConsensusGetSyncedLSN();
		ereport(LOG, (errmsg("consensus committed LSN advanced, LSN: %X/%X",
						(uint32) (consensusCommittedUpto >> 32), (uint32) consensusCommittedUpto)));
	}

	/* Fetch checkpoint record, just advance to end of it */
	if (fetching_ckpt)
	{
		new_readable_upto = RecPtr;
	}
	/* Fetch for PITR or complete recovery.
	 * advance to latest point of current timeline */
	else if (becameLeader &&
			(!consensusRecoveryLatestTimeline ||
			 recoveryTarget != RECOVERY_TARGET_UNSET))
	{
		if (tli < recoveryTargetTLI)
			new_readable_upto = tliSwitchPoint(tli, expectedTLEs, NULL);	
		else
			new_readable_upto = InvalidXLogRecPtr;
	}
	/* Fetch historic timeline record.
	 * advance to end of this timeline or commit point*/
	else if (consensusCommittedUpto > 0 && tli < recoveryTargetTLI)
	{
		XLogRecPtr switchUpto = tliSwitchPoint(tli, expectedTLEs, NULL);	
		new_readable_upto = switchUpto > consensusCommittedUpto ? 
			consensusCommittedUpto : switchUpto;
	}
	/* Fetch for backup recovery.
	 * advance to the end of request point at least */
	else if (becameLeader &&
			(!XLogRecPtrIsInvalid(ControlFile->backupStartPoint) ||
			 ControlFile->backupEndRequired))
	{
		new_readable_upto = RecPtr > consensusCommittedUpto ? 
			RecPtr : consensusCommittedUpto;
	}
	/* Normal fetch, advance to commit point */
	else
	{
		new_readable_upto = consensusCommittedUpto;
	}

	if (XLogRecPtrIsInvalid(new_readable_upto))
	{
		consensusReadableUpto = InvalidXLogRecPtr;
		ereport(INFO, (errmsg("consensus readable LSN to latest for PITR "
						"or complete recovery")));
	}
	else if (new_readable_upto > consensusReadableUpto)
	{
		/* Advance readable LSN. But less than one segment */
		if (!XLogRecPtrIsInvalid(consensusReadableUpto) && 
				consensusReadableUpto + wal_segment_size < new_readable_upto)
			consensusReadableUpto = (consensusReadableUpto / wal_segment_size + 1) * wal_segment_size;
		else
			consensusReadableUpto = new_readable_upto;
		ereport(INFO, (errmsg("consensus readable LSN advanced, LSN: %X/%X",
						(uint32) (consensusReadableUpto >> 32), (uint32) consensusReadableUpto)));
	}

	if (readFile < 0)
		return true;

	if (tli == curFileTLI)
		return false;

	polar_close(readFile);
	readFile = -1;
	curFileTLI = 0;
	return true;
}

/* POLAR wal pipeline begin */

static void polar_wal_pipeline_stats_init(polar_wal_pipeline_stats_t *stats)
{
	pg_atomic_init_u64(&stats->total_user_group_commits, 0);
	pg_atomic_init_u64(&stats->total_user_spin_commits, 0);
	pg_atomic_init_u64(&stats->total_user_timeout_commits, 0);
	pg_atomic_init_u64(&stats->total_user_wakeup_commits, 0);
	pg_atomic_init_u64(&stats->total_user_miss_timeouts, 0);
	pg_atomic_init_u64(&stats->total_user_miss_wakeups, 0);
	pg_atomic_init_u64(&stats->total_advance_callups, 0);
	pg_atomic_init_u64(&stats->total_advances, 0);
	pg_atomic_init_u64(&stats->total_write_callups, 0);
	pg_atomic_init_u64(&stats->total_writes, 0);
	pg_atomic_init_u64(&stats->unflushed_xlog_slot_waits, 0);
	pg_atomic_init_u64(&stats->total_flush_callups, 0);
	pg_atomic_init_u64(&stats->total_flushes, 0);
	pg_atomic_init_u64(&stats->total_flush_merges, 0);
	pg_atomic_init_u64(&stats->total_notify_callups, 0);
	pg_atomic_init_u64(&stats->total_notifies, 0);
	pg_atomic_init_u64(&stats->total_notified_users, 0);
}

static void polar_wait_obj_stats_init(polar_wait_object_stats_t *stats)
{
	pg_atomic_init_u64(&stats->waiters, 0);
	pg_atomic_init_u64(&stats->timeout_waits, 0);
	pg_atomic_init_u64(&stats->wakeup_waits, 0);
}

static void polar_wait_obj_init(polar_wait_object_t *wait_obj, 
								pthread_mutexattr_t *mutex_attr, pthread_condattr_t *cond_attr)
{
	int ret;

	ret = pthread_mutex_init(&wait_obj->mutex, mutex_attr);
	if (ret != 0)
		elog(ERROR, "pthread mutext init failed, errno is %d", ret);
	ret = pthread_cond_init(&wait_obj->cond, cond_attr);
	if (ret != 0)
		elog(ERROR, "pthread condition init failed, errno is %d", ret);
	polar_wait_obj_stats_init(&wait_obj->stats);
}					

static int 
polar_wal_pipeline_flush_event_get_slot_no(XLogRecPtr flush_lsn)
{
	/* 
	 * flush_lsn-1 means when lsn is on boundary, we should return previous slot,
	 * because flush_lsn is at the end of wal record
	 */
	return (flush_lsn-1) / polar_wal_pipeline_flush_event_slot_size & (polar_wal_pipeline_flush_event_array_size-1);
}

/*
 * wait XLogCtl->recoveryWakeupLatch to be set
 */
int
polar_wait_recovery_wakeup(int wakeEvents, long timeout, uint32 wait_event_info)
{
	int rc = WaitLatch(&XLogCtl->recoveryWakeupLatch, wakeEvents, 
			timeout, wait_event_info);

	ResetLatch(&XLogCtl->recoveryWakeupLatch);

	return rc;
}

/*
 * Switch logger status by consensus status and return whether to start replicaiton
 *
 * If became leader or leader switch or XLOG term switch, advance the recovery's
 * state machine. 
 *
 */
bool 
polar_dma_check_logger_status(char **primaryConnInfo,
		XLogRecPtr *receivedUpto,
		TimeLineID *receivedTLI,
		bool *requestNextTLI)
{
	bool leader_switch = false;
	bool log_switch = false;
	bool start_repl = false;

	elog(DEBUG5, "consensus check logger status");
	
	if (polar_check_and_switch_recovery_state(&leader_switch, &log_switch))
	{
		/* If consensus became leader. shutdown wal receiver */
		if (WalRcvRunning())
			ShutdownWalRcv();

		ereport(WARNING, 
				(errmsg("consensus became leader, term %ld, purged at %X/%X timeline %u", 
								consensusTerm, 
								(uint32) (consensusLogUpto >> 32), (uint32)consensusLogUpto, consensusLogTLI)));

		/* Reset expectedTLEs to NULL after became leader firstly */
		if (*receivedTLI == 0)
		{
			list_free_deep(expectedTLEs);
			expectedTLEs = NULL;
		}

		*receivedUpto = consensusLogUpto;	
		*receivedTLI = consensusLogTLI;
		recoveryTargetTLI = consensusLogTLI;
		*primaryConnInfo = NULL;
	}
	else if (leader_switch)
	{
		/* If consensus leader changed or log mismatch, shutdown wal receiver and fetch from new leader */
		if (WalRcvRunning())
			ShutdownWalRcv();

		/* Reset expectedTLEs to NULL after leader changed firstly */
		if (*receivedTLI == 0)
		{
			list_free_deep(expectedTLEs);
			expectedTLEs = NULL;
		}

		if (log_switch || *receivedUpto == 0)
		{
			ereport(WARNING, 
					(errmsg("consensus leader changed or log mismatch, term %ld, "
							"log term %ld, divergence at %X/%X timeline %u", 
							consensusTerm, consensusLogTerm, 
							(uint32) (consensusLogUpto >> 32), (uint32)consensusLogUpto, consensusLogTLI)));

			/* Ignore errors if WAL files not exist */
			polar_dma_xlog_truncate(consensusLogUpto, consensusLogTLI, LOG);

			/* Fetch from consensus log point */
			*receivedUpto = consensusLogUpto;	
			*receivedTLI = consensusLogTLI;
			recoveryTargetTLI = consensusLogTLI;
		}
		else
		{
			/* Advance latest received point before restart stream replication */
			XLogRecPtr latestReceivedUpto = InvalidXLogRecPtr;
			TimeLineID  latestReceivedTLI;
			XLogRecPtr latestChunkStart;
			latestReceivedUpto = GetWalRcvWriteRecPtr(&latestChunkStart, &latestReceivedTLI);
			if (latestReceivedUpto > *receivedUpto || latestReceivedTLI > *receivedTLI)
			{
				if (latestReceivedUpto > *receivedUpto)
					*receivedUpto = latestReceivedUpto;
				if (latestReceivedTLI > *receivedTLI)
					*receivedTLI = latestReceivedTLI;
				ereport(DEBUG2, 
						(errmsg("consensus recieved up to %X/%X timeline %u", 
										(uint32) (latestReceivedUpto >> 32), (uint32)latestReceivedUpto,
										latestReceivedTLI)));
			}
			ereport(WARNING, 
					(errmsg("consensus leader changed, term %ld, log term %ld, "
							"recieved up to %X/%X timeline %u", 
							consensusTerm, consensusLogTerm, 
							(uint32) (*receivedUpto >> 32), (uint32)*receivedUpto, *receivedTLI)));
		}
		*primaryConnInfo = PrimaryConnInfo;
		start_repl = true;
	}
	else if (*primaryConnInfo != NULL && !WalRcvStreaming())
	{
		/* If replication stopped, start it from latest received point */
		XLogRecPtr latestReceivedUpto = InvalidXLogRecPtr;
		TimeLineID latestReceivedTLI;
		XLogRecPtr latestChunkStart;
		TimeLineID tli;
		XLogRecPtr ptr;

		/* Advance latest received point before restart stream replication */
		latestReceivedUpto = GetWalRcvWriteRecPtr(&latestChunkStart, &latestReceivedTLI);
		if (latestReceivedUpto > *receivedUpto || latestReceivedTLI > *receivedTLI)
		{
			if (latestReceivedUpto > *receivedUpto)
				*receivedUpto = latestReceivedUpto;
			if (latestReceivedTLI > *receivedTLI)
				*receivedTLI = latestReceivedTLI;
			ereport(DEBUG2, 
					(errmsg("consensus recieved up to %X/%X timeline %u", 
									(uint32) (latestReceivedUpto >> 32), (uint32)latestReceivedUpto,
									latestReceivedTLI)));
		}

		if (*receivedUpto > 0)
		{
			/* Re-scan for possible new timelines */
			if (!expectedTLEs)
				expectedTLEs = readTimeLineHistory(recoveryTargetTLI);
			rescanLatestTimeLine();
			tli = tliOfPointInHistory(*receivedUpto, expectedTLEs);

			/* If timeline switched, recieve from switchpoint, because the 
			 * received point of last timeline from  may beyond the end */
			if (*receivedTLI > 0 && tli > *receivedTLI)
			{
				ptr = tliSwitchPoint(*receivedTLI, expectedTLEs, NULL);	
				/* the recieved point overtake the end point of last received timeline */
				if (*receivedUpto > ptr)
				{
					/* Ignore errors if WAL files not exist */
					polar_dma_xlog_truncate(ptr, *receivedTLI, LOG);
					ConsensusSetXLogFlushedLSN(ptr, *receivedTLI, true);
					*receivedUpto = ptr;
					ereport(LOG, 
							(errmsg("consensus recieved reset to %X/%X timeline %u", 
										(uint32) (ptr >> 32), (uint32)ptr, tli-1)));
				}
				*requestNextTLI = true;
			}
			else
			{
				*receivedTLI = tli;
			}
		}

		start_repl = true;
	}

	return start_repl;
}

/* 
 * truncate the xlog when it's beyond consensus point.
 */
static void
polar_dma_xlog_truncate(XLogRecPtr resetPtr, TimeLineID resetTLI, int elevel)
{
	char			path[MAXPGPATH];
	XLogSegNo segno;
	int				startoff;
	int				writebytes;
	int				byteswritten;
	int				fd;

	ereport(LOG, 
			(errmsg("consensus truncate WAL to %X/%X, timeline %u. "
							"current received at %X/%X timeline %u", 
					(uint32) (resetPtr >> 32), (uint32)resetPtr, resetTLI, 
					(uint32) (consensusReceivedUpto >> 32), (uint32)consensusReceivedUpto,
					consensusReceivedTLI)));

	XLByteToSeg(resetPtr, segno, wal_segment_size);

	startoff = XLogSegmentOffset(resetPtr, wal_segment_size);

	if (startoff % XLOG_BLCKSZ != 0)
		writebytes = startoff % XLOG_BLCKSZ;
	else if (startoff % wal_segment_size != 0)
		writebytes = XLOG_BLCKSZ;
	else
		writebytes = 0;

	if (writebytes > 0)
	{
		PGAlignedXLogBlock buffer;
		XLogFilePath(path, resetTLI, segno, wal_segment_size);

		fd = BasicOpenFile(path, O_RDWR | PG_BINARY, true);
		if (fd < 0)
		{
			ereport(elevel,
					(errcode_for_file_access(),
					 errmsg("could not open write-ahead log file \"%s\": %m", path)));
			return;
		}

		if (polar_lseek(fd, (off_t) startoff, SEEK_SET) < 0)
		{
			ereport(elevel,
					(errcode_for_file_access(),
					 errmsg("could not seek in log segment %s to offset %u: %m",
						 path, startoff)));
			polar_close(fd);
			return;
		}

		memset(buffer.data, 0, XLOG_BLCKSZ);
		byteswritten = polar_write(fd, buffer.data, writebytes);
		if (byteswritten <= 0)
		{
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to log segment %s "
						 "at offset %u, length %lu: %m",
						 path, startoff, (unsigned long) writebytes)));
		}

		issue_xlog_fsync(fd, segno);

		if (polar_close(fd) != 0)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not close log segment %s: %m",
						 path)));
	}

	RemoveNonParentXlogFiles(resetPtr, resetTLI+1);
}

/*
 * POLAR: Returns the latest point in WAL that has been safely flushed, and
 * can be sent to the standby. This should only be called when in recovery,
 * ie. we're streaming to a cascaded standby.
 *
 * As a side-effect, ThisTimeLineID is updated to the TLI of the last
 * replayed WAL record.
 */
XLogRecPtr
polar_dma_get_flush_lsn(bool committed, bool in_recovery)
{
	XLogRecPtr	replayPtr;
	TimeLineID	replayTLI;
	XLogRecPtr	receivePtr;
	TimeLineID	receiveTLI;
	XLogRecPtr	result;

	/*
	 * POLAR: in DMA mode, advance to new timeline from consensus flushed point.
	 * otherwise, the new timeline cannot be sent before exit from recovery status.
	 */
	if (!committed)
		ConsensusGetXLogFlushedLSN(&receivePtr, &receiveTLI);
	else
		ConsensusGetSyncedLSNAndTLI(&receivePtr, &receiveTLI);

	if (in_recovery)
		ThisTimeLineID = receiveTLI;

	result = receivePtr;

	if (in_recovery && !polar_is_dma_logger_node())
	{
		replayPtr = GetXLogReplayRecPtr(&replayTLI);
		if (replayTLI == ThisTimeLineID && replayPtr > receivePtr)
			result = replayPtr;
	}

	return result;
}

/* POLAR end */

static polar_wal_pipeline_flush_event_t *
polar_wal_pipeline_flush_event_get_slot(int slot_no)
{
	if (likely(polar_wal_pipeline_wait_object_align))
	{
		if (likely(huge_pages == HUGE_PAGES_ON))
			return (polar_wal_pipeline_flush_event_t *)((char *)XLogCtl->polar_wal_pipeline_commit_wait_buffer.flush_event_slots + slot_no * TYPEALIGN(OS_HUGE_PAGE_SIZE, sizeof(polar_wal_pipeline_flush_event_t)));
		else
			return (polar_wal_pipeline_flush_event_t *)((char *)XLogCtl->polar_wal_pipeline_commit_wait_buffer.flush_event_slots + slot_no * TYPEALIGN(OS_DEFAULT_PAGE_SIZE, sizeof(polar_wal_pipeline_flush_event_t)));	
	}
	else
		return (polar_wal_pipeline_flush_event_t *)((char *)XLogCtl->polar_wal_pipeline_commit_wait_buffer.flush_event_slots + slot_no * sizeof(polar_wal_pipeline_flush_event_t));
}

void
polar_wal_pipeline_commit_wait(XLogRecPtr flush_lsn)
{
	polar_wait_result_t wait_res = POLAR_WAIT_RES_SPIN;
	polar_spin_delay_status_t status;
	int slot_no = polar_wal_pipeline_flush_event_get_slot_no(flush_lsn);

	if (flush_lsn <= LogwrtResult.Flush)
	{
		pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_user_group_commits, 1);

		return;
	}

	if (CommitDelay > 0 && enableFsync && MinimumActiveBackends(CommitSiblings))
		pg_usleep(CommitDelay);

	SpinLockAcquire(&XLogCtl->info_lck);
	if (XLogCtl->LogwrtRqst.Write < flush_lsn)
		XLogCtl->LogwrtRqst.Write = flush_lsn;
	if (XLogCtl->LogwrtRqst.Flush < flush_lsn)
		XLogCtl->LogwrtRqst.Flush = flush_lsn;
	LogwrtResult = XLogCtl->LogwrtResult;
	SpinLockRelease(&XLogCtl->info_lck);

	if (flush_lsn <= LogwrtResult.Flush)
	{
		pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_user_group_commits, 1);

		return;
	}

	polar_wal_pipeliner_wakeup();

	/*
	 * Begin spin and wait
	 */

	polar_init_spin_delay_mt(&status, polar_wal_pipeline_flush_event_get_slot(slot_no),
							  polar_wal_pipeline_commit_wait_spin_delay, polar_wal_pipeline_commit_wait_timeout);
	pgstat_report_wait_start(WAIT_EVENT_WAL_PIPELINE_COMMIT_WAIT);

	/* First spin */
	while(flush_lsn > LogwrtResult.Flush)
	{
		wait_res = polar_perform_spin_delay_mt(&status, false, false);
		if (wait_res == POLAR_WAIT_RES_SPIN_OVER)
			break;

		/* 
		 * We must update LogwrtResult.Flush,
		 * because caller may use LogwrtResult to recheck the while condition 
		 */
		LogwrtResult.Flush = *(volatile XLogRecPtr *)(&XLogCtl->LogwrtResult.Flush);
	}

	/* Then wait */
	if (wait_res == POLAR_WAIT_RES_SPIN_OVER)
	{
		pthread_mutex_lock(&status.wait_obj->mutex);
		while(flush_lsn > LogwrtResult.Flush)
		{
			if (wait_res == POLAR_WAIT_RES_TIMEOUT)
				pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_user_miss_timeouts, 1);
			else if (wait_res == POLAR_WAIT_RES_WAKEUP)
				pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_user_miss_wakeups, 1);

			wait_res = polar_perform_spin_delay_mt(&status, false, true);

			/* 
			* We must update LogwrtResult.Flush,
			* because caller may use LogwrtResult to recheck the while condition 
			*/
			LogwrtResult.Flush = *(volatile XLogRecPtr *)(&XLogCtl->LogwrtResult.Flush);
		}
		pthread_mutex_unlock(&status.wait_obj->mutex);
	}
	pgstat_report_wait_end();

	/* Update stats */
	if (wait_res == POLAR_WAIT_RES_SPIN)
		pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_user_spin_commits, 1);
	else if (wait_res == POLAR_WAIT_RES_TIMEOUT)
		pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_user_timeout_commits, 1);
	else
		pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_user_wakeup_commits, 1);
}

void polar_wal_pipeline_set_last_notify_lsn(int thread_no, XLogRecPtr last_notify_lsn)
{
	XLogCtl->polar_wal_pipeline_last_notify_lsn[thread_no] = last_notify_lsn;
}

void 
polar_wal_pipeline_set_ready_write_lsn(XLogRecPtr ready_write_lsn)
{
	pg_atomic_write_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.ready_write_position, XLogRecPtrToBytePos(ready_write_lsn));
}

XLogRecPtr
polar_wal_pipeline_get_ready_write_lsn(void)
{
	return XLogBytePosToEndRecPtr(pg_atomic_read_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.ready_write_position));
}

static uint32
polar_wal_pipeline_get_recent_written_slot(uint64 write_pos)
{
	return write_pos & (polar_wal_pipeline_recent_written_array_size - 1);
}

static bool
polar_wal_pipeline_recent_written_has_space(XLogRecPtr start_lsn)
{
	uint64 ready_write_position = pg_atomic_read_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.ready_write_position);
	
	return ready_write_position + polar_wal_pipeline_recent_written_array_size > XLogRecPtrToBytePos(start_lsn);
}

void
polar_wal_pipeline_recent_written_add_link(XLogRecPtr start_lsn, XLogRecPtr end_lsn)
{
	int slot_no;

	pgstat_report_wait_start(WAIT_EVENT_WAL_PIPELINE_WAIT_RECENT_WRITTEN_SPACE);
	while (!polar_wal_pipeline_recent_written_has_space(start_lsn))
		pg_usleep(polar_wal_pipeline_wait_timeout);
	pgstat_report_wait_end();

	slot_no = polar_wal_pipeline_get_recent_written_slot(XLogRecPtrToBytePos(start_lsn));

	pg_atomic_write_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.recent_written_position_slots[slot_no], 
		XLogRecPtrToBytePos(end_lsn) - XLogRecPtrToBytePos(start_lsn));
}

static bool
polar_wal_pipeline_recent_written_advance(void)
{
	uint64 old_pos;
	uint64 tail_pos;
	
	old_pos = tail_pos = pg_atomic_read_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.ready_write_position);

	while (true)
	{
		/*
		* Get current link and the next-offet and check next value is whether non-zero
		* value. 0 means the log buffer is a hole (not filling completely)
		*/
		int slot_no = polar_wal_pipeline_get_recent_written_slot(tail_pos);

		uint64 distance = pg_atomic_read_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.recent_written_position_slots[slot_no]);

		uint64 next = tail_pos + distance;

		if (distance == 0)
			break;

		/*
		* Clear the link mark whos log buffer has been copied done
		*/
		pg_atomic_write_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.recent_written_position_slots[slot_no], 0);
		
		tail_pos = next;

		/* 
		 * No more than polar_wal_pipeline_advance_worker_write_max_size per advance
		 * 0 means no limit
		 */
		if (polar_wal_pipeline_advance_worker_write_max_size !=0
			&& tail_pos - old_pos > polar_wal_pipeline_advance_worker_write_max_size)
			break;
	}

	if (tail_pos > old_pos)
	{
		pg_atomic_write_u64(&XLogCtl->polar_wal_pipeline_recent_written_position_buffer.ready_write_position, tail_pos);

		return true;
	}

	return false;
}

uint64
polar_wal_pipeline_get_unflushed_xlog_add_slot_no(void)
{
	return pg_atomic_read_u64(&XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.add_slot_no);
}

uint64
polar_wal_pipeline_get_unflushed_xlog_del_slot_no(void)
{
	return pg_atomic_read_u64(&XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.del_slot_no);
}

static void
polar_wal_pipeline_advance_unflushed_xlog_slot_no(polar_wal_pipeline_unflushed_xlog_type_t slot_type)
{
	if (slot_type == UNFLUSHED_XLOG_SLOT_TYPE_ADD)
		pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.add_slot_no, 1);
	else
		pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.del_slot_no, 1);
}

static polar_wal_pipeline_unflushed_xlog_slot_t *
polar_wal_pipeline_get_curr_unflushed_xlog_slot(polar_wal_pipeline_unflushed_xlog_type_t slot_type)
{
	uint64 slot_no;

	if (slot_type == UNFLUSHED_XLOG_SLOT_TYPE_ADD)
		slot_no = polar_wal_pipeline_get_unflushed_xlog_add_slot_no();
	else
		slot_no = polar_wal_pipeline_get_unflushed_xlog_del_slot_no();

	slot_no = slot_no % polar_wal_pipeline_unflushed_xlog_array_size;
	
	return &XLogCtl->polar_wal_pipeline_unflushed_xlog_buffer.unflushed_xlog_slots[slot_no];
}

static polar_wal_pipeline_unflushed_xlog_slot_t *
polar_wal_pipeline_get_next_unflushed_xlog_slot(polar_wal_pipeline_unflushed_xlog_type_t slot_type)
{
	polar_wal_pipeline_advance_unflushed_xlog_slot_no(slot_type);

	return polar_wal_pipeline_get_curr_unflushed_xlog_slot(slot_type);
}

static void 
polar_wal_pipeline_unflushed_xlog_append(int fd, XLogSegNo seg_no, XLogRecPtr end_lsn, bool need_close)
{
	polar_wal_pipeline_unflushed_xlog_slot_t *slot = polar_wal_pipeline_get_curr_unflushed_xlog_slot(UNFLUSHED_XLOG_SLOT_TYPE_ADD);
	polar_wal_pipeline_unflushed_xlog_t *file = &slot->file_node;

	pgstat_report_wait_start(WAIT_EVENT_WAL_PIPELINE_WAIT_UNFLUSHED_XLOG_SLOT);
	while (slot->in_use)
	{
		pg_usleep(polar_wal_pipeline_wait_timeout);
		pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.unflushed_xlog_slot_waits, 1);
	}
	pgstat_report_wait_end();

	file->fd = fd;
	file->seg_no = seg_no;
	file->end_lsn = end_lsn;
	file->need_close = need_close;

	/* in_use should be last assigned */
	pg_write_barrier();

	slot->in_use = true;

	polar_wal_pipeline_advance_unflushed_xlog_slot_no(UNFLUSHED_XLOG_SLOT_TYPE_ADD);
}

/*
 * Replacing XLogFileClose
 * use parameters instead of global variables
 * openLogFile and openLogSegNo.
 * 
 * In polar wal pipeline:
 * write worker is responsible for open xlog file
 * flush worker is responsible for fsync and close xlog file
 */
static void
polar_wal_pipeline_xlog_close(int fd, XLogSegNo seg_no)
{
	Assert(fd >= 0);

	/*
	 * WAL segment files will not be re-read in normal operation, so we advise
	 * the OS to release any cached pages.  But do not do so if WAL archiving
	 * or streaming is active, because archiver and walsender process could
	 * use the cache to read the WAL segment.
	 */
#if defined(USE_POSIX_FADVISE) && defined(POSIX_FADV_DONTNEED)
	/* POLAR: libpfs not support posix_fadvise */
	if (!XLogIsNeeded() && !POLAR_FILE_IN_SHARED_STORAGE())
		(void) posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#endif

	if (polar_close(fd))
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not close log file %s: %m",
						XLogFileNameP(ThisTimeLineID, seg_no))));
}

/* 
 * Called by advance worker thread in polar wal pipeliner process.
 * 
 * In polar wal pipeline mode, used to advance max be-written lsn in wal buffer.
 */
bool
polar_wal_pipeline_advance(int ident)
{
	/* No need to work in standby mode */
	if (RecoveryInProgress())
		return false;
	
	pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_advance_callups, 1);

	if (!polar_wal_pipeline_recent_written_advance())
		return false;

	pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_advances, 1);

	return true;
}

/*
 * Replacing: XLogWrite
 * 
 * Called by write worker thread in polar wal pipeliner process.
 * 
 * In polar wal pipeline mode, used to write wal from wal buffer to OS cache.
 */
bool
polar_wal_pipeline_write(int ident)
{
	XLogwrtRqst		write_rqst;
	XLogRecPtr 		ready_write_lsn;
	
	/* No need to work in standby mode */
	if (RecoveryInProgress())
		return false;

	pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_write_callups, 1);

	write_rqst.Write = *(volatile XLogRecPtr *)(&XLogCtl->LogwrtRqst.Write);
	write_rqst.Flush = *(volatile XLogRecPtr *)(&XLogCtl->LogwrtRqst.Flush);

	LogwrtResult.Write = *(volatile XLogRecPtr *)(&XLogCtl->LogwrtResult.Write);
	
	ready_write_lsn = polar_wal_pipeline_get_ready_write_lsn();

	write_rqst.Write = Min(write_rqst.Write, ready_write_lsn);
	write_rqst.Flush = Min(write_rqst.Flush, ready_write_lsn);

	if (LogwrtResult.Write >= write_rqst.Write && LogwrtResult.Flush >= write_rqst.Flush)
		return false;

	pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_writes, 1);

	XLogWrite(write_rqst, false);

	if (polar_wal_pipeline_mode <= 3)
	{
		polar_wal_pipeline_wakeup_notifier();
		WalSndWakeupProcessRequests();
	}

	return true;
}

/* 
 * Do flush work
 * Try best to merge flush request before issue flush 
 */
static void
polar_wal_pipeline_flush_internal(void)
{
	polar_wal_pipeline_unflushed_xlog_slot_t 	*curr_del_slot = polar_wal_pipeline_get_curr_unflushed_xlog_slot(UNFLUSHED_XLOG_SLOT_TYPE_DEL);
	polar_wal_pipeline_unflushed_xlog_t 		*curr_del_file = &curr_del_slot->file_node;

	/* No del slot to process, just return */
	if (!curr_del_slot->in_use)
		return;

	while (true)
	{
		polar_wal_pipeline_unflushed_xlog_slot_t 	*next_del_slot = polar_wal_pipeline_get_next_unflushed_xlog_slot(UNFLUSHED_XLOG_SLOT_TYPE_DEL);
		polar_wal_pipeline_unflushed_xlog_t 		*next_del_file =  &next_del_slot->file_node;
		bool do_file_flush = false;
		bool do_file_close = false;
		bool stop_loop = false;

		if (!next_del_slot->in_use)
		{
			if (!curr_del_file->need_close)
				do_file_flush = true;
			else
			{
				do_file_flush = true;
				do_file_close = true;
			}

			stop_loop = true;
		}
		else
		{
			if (curr_del_file->fd != next_del_file->fd)
			{
				do_file_flush = true;
				do_file_close = true;
			}
			else if (next_del_file->need_close)
				do_file_flush = true;
			else 
			{
				/* Can be merged, just update stat */
				pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_flush_merges, 1);
			}
		}

		if (do_file_flush)
		{
			issue_xlog_fsync(curr_del_file->fd, curr_del_file->seg_no);

			SpinLockAcquire(&XLogCtl->info_lck);
			XLogCtl->LogwrtResult.Flush = curr_del_file->end_lsn;
			if (XLogCtl->LogwrtRqst.Flush < curr_del_file->end_lsn)
				XLogCtl->LogwrtRqst.Flush = curr_del_file->end_lsn;
			SpinLockRelease(&XLogCtl->info_lck);

			/* signal that we need to wakeup walsenders later */
			WalSndWakeupRequest();
		}
		
		if (do_file_close)
		{
			if (XLogArchivingActive())
				XLogArchiveNotifySeg(curr_del_file->seg_no);

			/*
			* Request a checkpoint if we've consumed too much xlog since
			* the last one.  For speed, we first check using the local
			* copy of RedoRecPtr, which might be out of date; if it looks
			* like a checkpoint is needed, forcibly update RedoRecPtr and
			* recheck.
			*/
			if (IsUnderPostmaster && XLogCheckpointNeeded(curr_del_file->seg_no))
			{
				(void) GetRedoRecPtr();
				if (XLogCheckpointNeeded(curr_del_file->seg_no))
					RequestCheckpoint(CHECKPOINT_CAUSE_XLOG);
			}

			polar_wal_pipeline_xlog_close(curr_del_file->fd, curr_del_file->seg_no);
		}

		curr_del_slot->in_use = false;

		curr_del_slot = next_del_slot;
		curr_del_file = next_del_file;

		if (stop_loop)
			break;
	}
}

/*
 * Replacing: XLogWrite
 * 
 * Called by flush worker thread in polar wal pipeliner process.
 * 
 * In polar wal pipeline mode, used to flush wal from os cache to disk.
 */
bool 
polar_wal_pipeline_flush(int ident)
{
	XLogwrtResult write_result;

	/* No need to work in standby mode */
	if (RecoveryInProgress())
		return false;

	pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_flush_callups, 1);

	write_result.Write = *(volatile XLogRecPtr *)(&XLogCtl->LogwrtResult.Write);
	write_result.Flush = *(volatile XLogRecPtr *)(&XLogCtl->LogwrtResult.Flush);

	if (write_result.Flush >= write_result.Write)
		return false;

	pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_flushes, 1);

	polar_wal_pipeline_flush_internal();

	polar_wal_pipeline_wakeup_notifier();

	WalSndWakeupProcessRequests();

	return true;
}

/*
 * Called by notify worker thread in polar wal pipeliner process.
 * 
 * In polar wal pipeline mode, used to notify backend whose wait lsn 
 * less than current flush lsn.
 */
bool
polar_wal_pipeline_notify(int ident)
{
	XLogRecPtr start_lsn = XLogCtl->polar_wal_pipeline_last_notify_lsn[ident];
	XLogRecPtr end_lsn = *(volatile XLogRecPtr *)(&XLogCtl->LogwrtResult.Flush);
	XLogRecPtr aligned_end_lsn;
	uint64 notified_users = 0;
	uint64 total_notified_users;
	polar_wait_object_t *wait_obj;

	/* No need to work in standby mode */
	if (RecoveryInProgress())
		return false;

	pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_notify_callups, 1);

	if (start_lsn == end_lsn)
		return false;

	pg_atomic_fetch_add_u64(&XLogCtl->polar_wal_pipeline_stats.total_notifies, 1);

	/* Up align to slot boundary */
	aligned_end_lsn = (end_lsn+polar_wal_pipeline_flush_event_slot_size-1) & ~(polar_wal_pipeline_flush_event_slot_size-1);
	while (start_lsn <= aligned_end_lsn)
	{
		int slot = polar_wal_pipeline_flush_event_get_slot_no(start_lsn);
		if ((slot % polar_wal_pipeline_notify_worker_num) == ident)
		{
			polar_wal_pipeline_flush_event_t *flush_event = polar_wal_pipeline_flush_event_get_slot(slot);

			pthread_mutex_lock(&flush_event->mutex);

			pthread_cond_broadcast(&flush_event->cond);

			notified_users += pg_atomic_read_u64(&flush_event->stats.waiters);

			pthread_mutex_unlock(&flush_event->mutex);
		}
	
		start_lsn += polar_wal_pipeline_flush_event_slot_size;
	}

	polar_wal_pipeline_set_last_notify_lsn(ident, end_lsn);

	/*
	 * Update stats, use first notify thread's mutex to synchronize notify threads
	 */
	wait_obj = polar_wal_pipeline_get_worker_wait_obj(NOTIFY_WORKER_THREAD_NO);

	pthread_mutex_lock(&wait_obj->mutex);

	total_notified_users = pg_atomic_read_u64(&XLogCtl->polar_wal_pipeline_stats.total_notified_users);

	pg_atomic_write_u64(&XLogCtl->polar_wal_pipeline_stats.total_notified_users, total_notified_users + notified_users);

	pthread_mutex_unlock(&wait_obj->mutex);

	return true;
}

/* 
 * Added for polar wal pipeline stats info
 */

/* Max LSN already reserved fro insert */
XLogRecPtr
polar_wal_pipeline_get_current_insert_lsn(void)
{
	return XLogBytePosToEndRecPtr(XLogCtl->Insert.CurrBytePos);
}

/* 
 * Max continuous wal content already write to wal buffer
 */
XLogRecPtr
polar_wal_pipeline_get_continuous_insert_lsn(void)
{
	return	polar_wal_pipeline_get_ready_write_lsn();
}

/* Max LSN already write to OS cache */
XLogRecPtr
polar_wal_pipeline_get_write_lsn(void)
{
	return XLogCtl->LogwrtResult.Write;
}

/* Max LSN already flushed to disk */
XLogRecPtr
polar_wal_pipeline_get_flush_lsn(void)
{
	return XLogCtl->LogwrtResult.Flush;
}

XLogRecPtr
polar_wal_pipeline_get_last_notify_lsn(int ident)
{
	return XLogCtl->polar_wal_pipeline_last_notify_lsn[ident];
}

polar_wal_pipeline_stats_t * 
polar_wal_pipeline_get_stats(void)
{
	return &XLogCtl->polar_wal_pipeline_stats;
}

polar_wait_object_t * 
polar_wal_pipeline_get_worker_wait_obj(int thread_no)
{
	if (likely(polar_wal_pipeline_wait_object_align))
	{
		if (likely(huge_pages == HUGE_PAGES_ON))
			return (polar_wait_object_t *)((char *)XLogCtl->polar_wal_pipeline_wait_objs + thread_no * TYPEALIGN(OS_HUGE_PAGE_SIZE, sizeof(polar_wait_object_t)));
		else
			return (polar_wait_object_t *)((char *)XLogCtl->polar_wal_pipeline_wait_objs + thread_no * TYPEALIGN(OS_DEFAULT_PAGE_SIZE, sizeof(polar_wait_object_t)));
	}
	else
		return (polar_wait_object_t *)((char *)XLogCtl->polar_wal_pipeline_wait_objs + thread_no * sizeof(polar_wait_object_t));

}

void 
polar_wal_pipeline_stats_reset(void)
{
	int i;

	for (i = 0; i < polar_wal_pipeline_flush_event_array_size; i++)
	{
		polar_wal_pipeline_flush_event_t *flush_event = polar_wal_pipeline_flush_event_get_slot(i);

		polar_wait_obj_stats_init(&flush_event->stats);
	}

	for (i = 0; i < POLAR_WAL_PIPELINE_MAX_THREAD_NUM; i++)
	{
		polar_wait_object_t *wait_obj = polar_wal_pipeline_get_worker_wait_obj(i);

		polar_wait_obj_stats_init(&wait_obj->stats);
	}

	polar_wal_pipeline_stats_init(&XLogCtl->polar_wal_pipeline_stats);
}

/* 
 *	Only for polar wal pipeline test 
 */
void
polar_wal_pipeline_set_local_recovery_mode(bool mode)
{
	LocalRecoveryInProgress = mode;
}

/* POLAR wal pipeline END */

/* POLAR: get the end of the last record */
XLogRecPtr
polar_get_last_valid_lsn(void)
{
	XLogRecPtr polar_last_valid_lsn = InvalidXLogRecPtr;

	/* get last valid lsn from meta when in datamax mode */
	if (polar_is_datamax())
		polar_last_valid_lsn = polar_datamax_get_last_valid_received_lsn(polar_datamax_ctl, NULL);
	else if (polar_in_master_mode())
	{
		XLogCtlInsert *Insert = &XLogCtl->Insert;
		SpinLockAcquire(&Insert->insertpos_lck);
		polar_last_valid_lsn = XLogBytePosToEndRecPtr(Insert->CurrBytePos);
		SpinLockRelease(&Insert->insertpos_lck);
	}
	/* standby mode */
	else
		polar_last_valid_lsn = GetXLogReplayRecPtr(NULL);
	
	return polar_last_valid_lsn;
}

/*
 * POLAR: set the initial restart_lsn of datamax replication slot.
 *
 * It is used while establishing initial replication from Datamax. 
 * To ensure the wal data in datamax is not less than that in primary,
 * replication should start at the smallest lsn in primary.
 */
XLogRecPtr
polar_set_initial_datamax_restart_lsn(ReplicationSlot *slot)
{
	XLogRecPtr	restart_lsn;
	XLogSegNo 	last_removed_segno;

	/* get lwlock to avoid getting the xlog file being removed by checkpoint at the same time */
	LWLockAcquire(&XLogCtl->polar_initial_datamax_lock, LW_SHARED);
	last_removed_segno = XLogGetLastRemovedSegno();

	if (last_removed_segno != 0)
		XLogSegNoOffsetToRecPtr(last_removed_segno + 1, 0, wal_segment_size, restart_lsn);
	else
		restart_lsn = polar_get_smallest_walfile_lsn();

	/* set slot restart_lsn to prevent further wal removal */
	if (slot != NULL)
	{
		SpinLockAcquire(&slot->mutex);
		slot->data.restart_lsn = restart_lsn;
		SpinLockRelease(&slot->mutex);
	}
	ReplicationSlotsComputeRequiredLSN();
	/* release lwlock after having set restart_lsn, so that it can be used while removing old xlog files */
	LWLockRelease(&XLogCtl->polar_initial_datamax_lock);

	return restart_lsn;
}
/* POLAR end */

static void
polar_smgr_drop_all_sr(XLogReaderState *xlogreader)
{
	int block_id;
	RelFileNodeBackend rnode;

	for (block_id = 0; block_id <= XLR_MAX_BLOCK_ID; block_id++)
	{
		if (!xlogreader->blocks[block_id].in_use)
			break;

		/* no temp relation in recovery, so drop relation for backend -1 */
		memcpy(&rnode.node, &xlogreader->blocks[block_id].rnode, sizeof(RelFileNode));
		rnode.backend = -1;
		smgr_drop_sr(&rnode);
	}
}

/*
 * POLAR: if or not to clean smgr shared cache.
 * this function is used to clean relica block cache. We can optimizer
 * function.
 * If current is replica and we can get the real rnode and forknum, then
 * we judge if it is a new page of insert, update, hot update, multi insert
 * on heap page. For non-heap, we don't do the optimizer now, just clean
 * the block cache.
 * polar_nblocks_cache_clean requires a SMgrRelation for the first parameter.
 * RelFileNode address is also the SMgrRelation address, so translate it 
 * to SMgrRelation.
 */
static void
polar_clean_smgr_cache_if_needed(XLogReaderState *xlogreader)
{
	uint8 info;
	RmgrId rmid;

	/*
	 * For master or standby node, just return. For replica node but
	 * disable polar_enable_replica_use_smgr_cache, just return.
	 */
	if (!polar_in_replica_mode() ||
		(polar_in_replica_mode() && !polar_enable_replica_use_smgr_cache))
		return;

	info = XLogRecGetInfo(xlogreader) & ~XLR_INFO_MASK;
	rmid = XLogRecGetRmid(xlogreader);
	switch (rmid)
	{
		case RM_HEAP_ID:
		{
			switch (info)
			{
				case XLOG_HEAP_INSERT:
					break;

				case XLOG_HEAP_UPDATE:
					break;

				case XLOG_HEAP_HOT_UPDATE:
					break;

				default:
					polar_smgr_drop_all_sr(xlogreader);
					break;
			}
			break;
		}

		case RM_HEAP2_ID:
		{
			switch (info)
			{
				case XLOG_HEAP2_MULTI_INSERT:
					break;

				default:
					polar_smgr_drop_all_sr(xlogreader);
					break;
			}
			break;
		}

		case RM_DBASE_ID:
		{
			xl_dbase_drop_rec *xlrec;
			switch (info)
			{
				case XLOG_DBASE_DROP:
					xlrec = (xl_dbase_drop_rec *) XLogRecGetData(xlogreader);
					polar_dropdb_smgr_shared_relation_pool(xlrec->db_id);
					break;

				default:
					break;
			}
			break;
		}

		default:
			polar_smgr_drop_all_sr(xlogreader);
			break;
	}
}

/*
 * Report availability of WAL for the given target LSN
 *		(typically a slot's restart_lsn)
 *
 * Returns one of the following enum values:
 *
 * * WALAVAIL_RESERVED means targetLSN is available and it is in the range of
 *   max_wal_size.
 *
 * * WALAVAIL_EXTENDED means it is still available by preserving extra
 *   segments beyond max_wal_size. If max_slot_wal_keep_size is smaller
 *   than max_wal_size, this state is not returned.
 *
 * * WALAVAIL_UNRESERVED means it is being lost and the next checkpoint will
 *   remove reserved segments. The walsender using this slot may return to the
 *   above.
 *
 * * WALAVAIL_REMOVED means it has been removed. A replication stream on
 *   a slot with this LSN cannot continue after a restart.
 *
 * * WALAVAIL_INVALID_LSN means the slot hasn't been set to reserve WAL.
 */
WALAvailability
GetWALAvailability(XLogRecPtr targetLSN)
{
	XLogRecPtr	currpos;		/* current write LSN */
	XLogSegNo	currSeg;		/* segid of currpos */
	XLogSegNo	targetSeg;		/* segid of targetLSN */
	XLogSegNo	oldestSeg;		/* actual oldest segid */
	XLogSegNo	oldestSegMaxWalSize;	/* oldest segid kept by max_wal_size */
	XLogSegNo	oldestSlotSeg;	/* oldest segid kept by slot */
	uint64		keepSegs;

	/*
	 * slot does not reserve WAL. Either deactivated, or has never been active
	 */
	if (XLogRecPtrIsInvalid(targetLSN))
		return WALAVAIL_INVALID_LSN;

	/*
	 * Calculate the oldest segment currently reserved by all slots,
	 * considering wal_keep_segments and max_slot_wal_keep_size.  Initialize
	 * oldestSlotSeg to the current segment.
	 */
	currpos = GetXLogWriteRecPtr();
	XLByteToSeg(currpos, oldestSlotSeg, wal_segment_size);
	KeepLogSeg(currpos, &oldestSlotSeg);

	/*
	 * Find the oldest extant segment file. We get 1 until checkpoint removes
	 * the first WAL segment file since startup, which causes the status being
	 * wrong under certain abnormal conditions but that doesn't actually harm.
	 */
	oldestSeg = XLogGetLastRemovedSegno() + 1;

	/* calculate oldest segment by max_wal_size */
	XLByteToSeg(currpos, currSeg, wal_segment_size);
	keepSegs = ConvertToXSegs(max_wal_size_mb, wal_segment_size) + 1;

	if (currSeg > keepSegs)
		oldestSegMaxWalSize = currSeg - keepSegs;
	else
		oldestSegMaxWalSize = 1;

	/* the segment we care about */
	XLByteToSeg(targetLSN, targetSeg, wal_segment_size);

	/*
	 * No point in returning reserved or extended status values if the
	 * targetSeg is known to be lost.
	 */
	if (targetSeg >= oldestSlotSeg)
	{
		/* show "reserved" when targetSeg is within max_wal_size */
		if (targetSeg >= oldestSegMaxWalSize)
			return WALAVAIL_RESERVED;

		/* being retained by slots exceeding max_wal_size */
		return WALAVAIL_EXTENDED;
	}

	/* WAL segments are no longer retained but haven't been removed yet */
	if (targetSeg >= oldestSeg)
		return WALAVAIL_UNRESERVED;

	/* Definitely lost */
	return WALAVAIL_REMOVED;
}

/* POLAR: set value of polar_replica_update_dirs_by_redo */
void
polar_set_replica_update_dirs_by_redo(bool value)
{
	XLogCtl->polar_replica_update_dirs_by_redo = value;
}

/* POLAR: return value of polar_replica_update_dirs_by_redo */
bool
polar_replica_update_dirs_by_redo(void)
{
	return XLogCtl->polar_replica_update_dirs_by_redo;
}

/* POLAR: update minRecoveryPoint and minRecoveryPointTLI of controlfile */
static void
polar_update_controlfile_minrecoverypoint(XLogRecPtr min_recovery_point, TimeLineID min_recovery_point_tli)
{
	XLogRecPtr	polar_min_point;
	TimeLineID	polar_min_point_tli;

	Assert(ControlFile != NULL);
	LWLockAcquire(ControlFileLock, LW_SHARED);
	polar_min_point = ControlFile->minRecoveryPoint;
	polar_min_point_tli = ControlFile->minRecoveryPointTLI;
	LWLockRelease(ControlFileLock);

	/* update when minRecoveryPoint or minRecoveryPointTLI is not equal to the parameters */
	if (polar_min_point != min_recovery_point || polar_min_point_tli != min_recovery_point_tli)
	{
		LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
		ControlFile->minRecoveryPoint = min_recovery_point;
		ControlFile->minRecoveryPointTLI = min_recovery_point_tli;
		UpdateControlFile();
		LWLockRelease(ControlFileLock);
	}
}

void
polar_fill_segment_file_zero(int fd, char *tmppath, int segment_size,
		uint32 init_write_event_info, uint32 init_fsync_event_info,
		const char *log_file_info)
{
#define	POLAR_FILL_ZERO_EACH_SIZE		1024 * 1024

	/* POLAR: change palloc0 to static array to avoid memory allocations assert within a critical section */
	static char data[POLAR_FILL_ZERO_EACH_SIZE];

	int		nbytes = 0;
	instr_time	polar_init_start;
	instr_time	polar_init_end;
	int			each_size = POLAR_FILL_ZERO_EACH_SIZE;

	if (each_size > segment_size)
		each_size = segment_size;

	if (IsUnderPostmaster)
		INSTR_TIME_SET_CURRENT(polar_init_start);

	MemSet(data, 0, sizeof(data));
	for (nbytes = 0; nbytes < segment_size; nbytes += each_size)
	{
		int 	rc = 0;

		errno = 0;
		pgstat_report_wait_start(init_write_event_info);
		if (POLAR_ENABLE_PWRITE())
			rc = (int)polar_pwrite(fd, data, each_size, nbytes);
		else
			/*no cover line*/
			rc = (int)polar_write(fd, data, each_size);

		if (rc != each_size)
		{
			/*no cover begin*/
			int 		save_errno = errno;

			/*
			 * If we fail to make the file, delete it to release disk space
			 */
			polar_unlink(tmppath);

			polar_close(fd);

			/* if write didn't set errno, assume problem is no disk space */
			errno = save_errno ? save_errno : ENOSPC;

			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m", tmppath)));
			/*no cover end*/
		}
		pgstat_report_wait_end();
	}

	pgstat_report_wait_start(init_fsync_event_info);
	if (polar_fsync(fd) != 0)
	{
		/*no cover begin*/
		int 		save_errno = errno;

		polar_close(fd);
		errno = save_errno;
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m", tmppath)));
		/*no cover end*/
	}

	pgstat_report_wait_end();

	if (IsUnderPostmaster)
	{
		INSTR_TIME_SET_CURRENT(polar_init_end);
		INSTR_TIME_SUBTRACT(polar_init_end, polar_init_start);
		elog(LOG, "done creating and filling tmp %s file %s time=%.3f s",
				log_file_info, tmppath, INSTR_TIME_GET_MILLISEC(polar_init_end) / 1000);
	}

	return;
}

/* check the record ptr beyond the point of consensus log 
 * For PITR, the recovery stop point must after the consensus log point
 */
static bool
polar_dma_beyond_consensus_log(XLogRecPtr RecPtr)
{
	return (becameLeader && RecPtr > consensusReceivedUpto);
}

polar_io_fencing_t * 
polar_io_fencing_get_instance(void)
{
	return &XLogCtl->polar_io_fencing;
}


/* POLAR: update curFileTLI according to recptr that needs to be read */
static void
polar_update_curFileTLI(XLogRecPtr ptr)
{
	TimeLineID old = curFileTLI;
	curFileTLI = tliOfPointInHistory(ptr, readTimeLineHistory(curFileTLI));

	if (old != curFileTLI)
		elog(LOG, "%s: change curFileTLI from %d to %d", __func__, old, curFileTLI);
}
/* POLAR end */

const char *
polar_node_type_string(PolarNodeType type, int error_level)
{
	switch (type)
	{
		case POLAR_UNKNOWN:
			return "Unknown";
		case POLAR_MASTER:
			return "RW";
		case POLAR_REPLICA:
			return "RO";
		case POLAR_STANDBY:
			return "Standby";
		case POLAR_STANDALONE_DATAMAX:
			return "Standalone DataMax";
		default:
			elog(error_level, "Unknown node type: %d", type);
			return "Unknown";
	}
}

const char *
polar_standby_state_string(int state, int error_level)
{
	switch (state)
	{
		case POLAR_NODE_OFFLINE:
			return "Offline";
		case POLAR_NODE_GOING_OFFLINE:
			return "Going Offline";
		case STANDBY_DISABLED:
			return "Disabled";
		case STANDBY_INITIALIZED:
			return "Initialized";
		case STANDBY_SNAPSHOT_PENDING:
			return "Pending";
		case STANDBY_SNAPSHOT_READY:
			return "Ready";
		default:
			elog(error_level, "Unknown node state: %d", state);
			return "Unknown";
	}
}
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
