/*
 * xlog.h
 *
 * PostgreSQL write-ahead log manager
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/xlog.h
 */
#ifndef XLOG_H
#define XLOG_H

#include "access/xlogbackup.h"
#include "access/xlogdefs.h"
#include "datatype/timestamp.h"
#include "lib/stringinfo.h"
#include "nodes/pg_list.h"

/* POLAR */
#include "storage/polar_fd.h"

/* Sync methods */
enum WalSyncMethod
{
	WAL_SYNC_METHOD_FSYNC = 0,
	WAL_SYNC_METHOD_FDATASYNC,
	WAL_SYNC_METHOD_OPEN,		/* for O_SYNC */
	WAL_SYNC_METHOD_FSYNC_WRITETHROUGH,
	WAL_SYNC_METHOD_OPEN_DSYNC	/* for O_DSYNC */
};
extern PGDLLIMPORT int wal_sync_method;

extern PGDLLIMPORT XLogRecPtr ProcLastRecPtr;
extern PGDLLIMPORT XLogRecPtr XactLastRecEnd;
extern PGDLLIMPORT XLogRecPtr XactLastCommitEnd;

/* these variables are GUC parameters related to XLOG */
<<<<<<< HEAD
extern int	wal_segment_size;
extern int	min_wal_size_mb;
extern int	max_wal_size_mb;
extern int	wal_keep_segments;
extern bool	polar_enable_max_slot_wal_keep_size;
extern int	max_slot_wal_keep_size_mb;
extern int	XLOGbuffers;
extern int	XLogArchiveTimeout;
extern int	wal_retrieve_retry_interval;
extern char *XLogArchiveCommand;
extern bool EnableHotStandby;
extern bool fullPageWrites;
extern bool wal_log_hints;
extern bool wal_compression;
extern bool wal_recycle;
extern bool *wal_consistency_checking;
extern char *wal_consistency_checking_string;
extern bool log_checkpoints;
/* POLAR */
extern int  polar_wal_buffer_insert_locks;
/* POLAR end*/
=======
extern PGDLLIMPORT int wal_segment_size;
extern PGDLLIMPORT int min_wal_size_mb;
extern PGDLLIMPORT int max_wal_size_mb;
extern PGDLLIMPORT int wal_keep_size_mb;
extern PGDLLIMPORT int max_slot_wal_keep_size_mb;
extern PGDLLIMPORT int XLOGbuffers;
extern PGDLLIMPORT int XLogArchiveTimeout;
extern PGDLLIMPORT int wal_retrieve_retry_interval;
extern PGDLLIMPORT char *XLogArchiveCommand;
extern PGDLLIMPORT bool EnableHotStandby;
extern PGDLLIMPORT bool fullPageWrites;
extern PGDLLIMPORT bool wal_log_hints;
extern PGDLLIMPORT int wal_compression;
extern PGDLLIMPORT bool wal_init_zero;
extern PGDLLIMPORT bool wal_recycle;
extern PGDLLIMPORT bool *wal_consistency_checking;
extern PGDLLIMPORT char *wal_consistency_checking_string;
extern PGDLLIMPORT bool log_checkpoints;
extern PGDLLIMPORT int CommitDelay;
extern PGDLLIMPORT int CommitSiblings;
extern PGDLLIMPORT bool track_wal_io_timing;
extern PGDLLIMPORT int wal_decode_buffer_size;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

extern PGDLLIMPORT int CheckPointSegments;

/* Archive modes */
typedef enum ArchiveMode
{
	ARCHIVE_MODE_OFF = 0,		/* disabled */
	ARCHIVE_MODE_ON,			/* enabled while server is running normally */
	ARCHIVE_MODE_ALWAYS,		/* enabled always (even during recovery) */
} ArchiveMode;
extern PGDLLIMPORT int XLogArchiveMode;

/* WAL levels */
typedef enum WalLevel
{
	WAL_LEVEL_MINIMAL = 0,
	WAL_LEVEL_REPLICA,
	WAL_LEVEL_LOGICAL,
} WalLevel;

<<<<<<< HEAD
=======
/* Compression algorithms for WAL */
typedef enum WalCompression
{
	WAL_COMPRESSION_NONE = 0,
	WAL_COMPRESSION_PGLZ,
	WAL_COMPRESSION_LZ4,
	WAL_COMPRESSION_ZSTD,
} WalCompression;

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
/* Recovery states */
typedef enum RecoveryState
{
	RECOVERY_STATE_CRASH = 0,	/* crash recovery */
	RECOVERY_STATE_ARCHIVE,		/* archive recovery */
<<<<<<< HEAD
	RECOVERY_STATE_DONE			/* currently in production */
=======
	RECOVERY_STATE_DONE,		/* currently in production */
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
} RecoveryState;

extern PGDLLIMPORT int wal_level;

/* Is WAL archiving enabled (always or only while server is running normally)? */
#define XLogArchivingActive() \
	(AssertMacro(XLogArchiveMode == ARCHIVE_MODE_OFF || wal_level >= WAL_LEVEL_REPLICA), XLogArchiveMode > ARCHIVE_MODE_OFF)
/* Is WAL archiving enabled always (even during recovery)? */
#define XLogArchivingAlways() \
	(AssertMacro(XLogArchiveMode == ARCHIVE_MODE_OFF || wal_level >= WAL_LEVEL_REPLICA), XLogArchiveMode == ARCHIVE_MODE_ALWAYS)

/*
 * Is WAL-logging necessary for archival or log-shipping, or can we skip
 * WAL-logging if we fsync() the data before committing instead?
 */
#define XLogIsNeeded() (wal_level >= WAL_LEVEL_REPLICA)

/*
 * Is a full-page image needed for hint bit updates?
 *
 * Normally, we don't WAL-log hint bit updates, but if checksums are enabled,
 * we have to protect them against torn page writes.  When you only set
 * individual bits on a page, it's still consistent no matter what combination
 * of the bits make it to disk, but the checksum wouldn't match.  Also WAL-log
 * them if forced by wal_log_hints=on.
 */
#define XLogHintBitIsNeeded() (DataChecksumsEnabled() || wal_log_hints)

/* Do we need to WAL-log information required only for Hot Standby and logical replication? */
#define XLogStandbyInfoActive() (wal_level >= WAL_LEVEL_REPLICA)

/* Do we need to WAL-log information required only for logical replication? */
#define XLogLogicalInfoActive() (wal_level >= WAL_LEVEL_LOGICAL)

#ifdef WAL_DEBUG
extern PGDLLIMPORT bool XLOG_DEBUG;
#endif

/*
 * OR-able request flag bits for checkpoints.  The "cause" bits are used only
 * for logging purposes.  Note: the flags must be defined so that it's
 * sensible to OR together request flags arising from different requestors.
 */

/* These directly affect the behavior of CreateCheckPoint and subsidiaries */
#define CHECKPOINT_IS_SHUTDOWN	0x0001	/* Checkpoint is for shutdown */
#define CHECKPOINT_END_OF_RECOVERY	0x0002	/* Like shutdown checkpoint, but
											 * issued at end of WAL recovery */
#define CHECKPOINT_IMMEDIATE	0x0004	/* Do it without delays */
#define CHECKPOINT_FORCE		0x0008	/* Force even if no activity */
#define CHECKPOINT_FLUSH_ALL	0x0010	/* Flush all pages, including those
										 * belonging to unlogged tables */
/* These are important to RequestCheckpoint */
#define CHECKPOINT_WAIT			0x0020	/* Wait for completion */
#define CHECKPOINT_REQUESTED	0x0040	/* Checkpoint request has been made */
/* These indicate the cause of a checkpoint request */
<<<<<<< HEAD
#define CHECKPOINT_CAUSE_XLOG	0x0040	/* XLOG consumption */
#define CHECKPOINT_CAUSE_TIME	0x0080	/* Elapsed time */
/* We set this to ensure that ckpt_flags is not 0 if a request has been made */
#define CHECKPOINT_REQUESTED	0x0100	/* Checkpoint request has been made */

/* POLAR: lazy checkpoint */
#define CHECKPOINT_LAZY         0x1000
/* POLAR: flashback point */
#define CHECKPOINT_FLASHBACK	0x2000
=======
#define CHECKPOINT_CAUSE_XLOG	0x0080	/* XLOG consumption */
#define CHECKPOINT_CAUSE_TIME	0x0100	/* Elapsed time */
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/*
 * Flag bits for the record being inserted, set using XLogSetRecordFlags().
 */
#define XLOG_INCLUDE_ORIGIN		0x01	/* include the replication origin */
#define XLOG_MARK_UNIMPORTANT	0x02	/* record not important for durability */


/* Checkpoint statistics */
typedef struct CheckpointStatsData
{
	TimestampTz ckpt_start_t;	/* start of checkpoint */
	TimestampTz ckpt_write_t;	/* start of flushing buffers */
	TimestampTz ckpt_sync_t;	/* start of fsyncs */
	TimestampTz ckpt_sync_end_t;	/* end of fsyncs */
	TimestampTz ckpt_end_t;		/* end of checkpoint */

	int			ckpt_bufs_written;	/* # of buffers written */

	int			ckpt_segs_added;	/* # of new xlog segments created */
	int			ckpt_segs_removed;	/* # of xlog segments deleted */
	int			ckpt_segs_recycled; /* # of xlog segments recycled */

	int			ckpt_sync_rels; /* # of relations synced */
	uint64		ckpt_longest_sync;	/* Longest sync for one relation */
	uint64		ckpt_agg_sync_time; /* The sum of all the individual sync
									 * times, which is not necessarily the
									 * same as the total elapsed time for the
									 * entire sync phase. */
} CheckpointStatsData;

extern PGDLLIMPORT CheckpointStatsData CheckpointStats;

/*
 * GetWALAvailability return codes
 */
typedef enum WALAvailability
{
	WALAVAIL_INVALID_LSN,		/* parameter error */
	WALAVAIL_RESERVED,			/* WAL segment is within max_wal_size */
	WALAVAIL_EXTENDED,			/* WAL segment is reserved by a slot or
								 * wal_keep_size */
	WALAVAIL_UNRESERVED,		/* no longer reserved, but not removed yet */
	WALAVAIL_REMOVED,			/* WAL segment has been removed */
} WALAvailability;

/*
 * GetWALAvailability return codes
 */
typedef enum WALAvailability
{
	WALAVAIL_INVALID_LSN,		/* parameter error */
	WALAVAIL_RESERVED,			/* WAL segment is within max_wal_size */
	WALAVAIL_EXTENDED,			/* WAL segment is reserved by a slot or
								 * wal_keep_segments */
	WALAVAIL_UNRESERVED,		/* no longer reserved, but not removed yet */
	WALAVAIL_REMOVED			/* WAL segment has been removed */
} WALAvailability;

struct XLogRecData;
<<<<<<< HEAD
struct ControlFileData;
=======
struct XLogReaderState;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

extern XLogRecPtr XLogInsertRecord(struct XLogRecData *rdata,
								   XLogRecPtr fpw_lsn,
								   uint8 flags,
								   int num_fpi,
								   bool topxid_included);
extern void XLogFlush(XLogRecPtr record);
extern bool XLogBackgroundFlush(void);
extern bool XLogNeedsFlush(XLogRecPtr record);
extern int	XLogFileInit(XLogSegNo logsegno, TimeLineID logtli);
extern int	XLogFileOpen(XLogSegNo segno, TimeLineID tli);

extern void CheckXLogRemoved(XLogSegNo segno, TimeLineID tli);
extern XLogSegNo XLogGetLastRemovedSegno(void);
extern XLogSegNo XLogGetOldestSegno(TimeLineID tli);
extern void XLogSetAsyncXactLSN(XLogRecPtr asyncXactLSN);
extern void XLogSetReplicationSlotMinimumLSN(XLogRecPtr lsn);

extern void xlog_redo(struct XLogReaderState *record);
extern void xlog_desc(StringInfo buf, struct XLogReaderState *record);
extern const char *xlog_identify(uint8 info);

extern void issue_xlog_fsync(int fd, XLogSegNo segno, TimeLineID tli);

extern bool RecoveryInProgress(void);
extern RecoveryState GetRecoveryState(void);
<<<<<<< HEAD
extern bool HotStandbyActive(void);
extern bool HotStandbyActiveInReplay(void);
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern bool XLogInsertAllowed(void);
extern XLogRecPtr GetXLogInsertRecPtr(void);
extern XLogRecPtr GetXLogWriteRecPtr(void);

extern uint64 GetSystemIdentifier(void);
extern char *GetMockAuthenticationNonce(void);
extern bool DataChecksumsEnabled(void);
extern XLogRecPtr GetFakeLSNForUnloggedRel(void);
extern Size XLOGShmemSize(void);
extern void XLOGShmemInit(void);
extern void BootStrapXLOG(uint32 data_checksum_version);
extern void InitializeWalConsistencyChecking(void);
extern void LocalProcessControlFile(bool reset);
extern WalLevel GetActiveWalLevelOnStandby(void);
extern void StartupXLOG(void);
extern void ShutdownXLOG(int code, Datum arg);
extern void CreateCheckPoint(int flags);
extern bool CreateRestartPoint(int flags);
extern WALAvailability GetWALAvailability(XLogRecPtr targetLSN);
extern void XLogPutNextOid(Oid nextOid);
extern XLogRecPtr XLogRestorePoint(const char *rpName);
extern void UpdateFullPageWrites(void);
extern void GetFullPageWriteInfo(XLogRecPtr *RedoRecPtr_p, bool *doPageWrites_p);
extern XLogRecPtr GetRedoRecPtr(void);
extern XLogRecPtr GetInsertRecPtr(void);
extern XLogRecPtr GetFlushRecPtr(TimeLineID *insertTLI);
extern TimeLineID GetWALInsertionTimeLine(void);
extern TimeLineID GetWALInsertionTimeLineIfSet(void);
extern XLogRecPtr GetLastImportantRecPtr(void);

extern void SetWalWriterSleeping(bool sleeping);

extern Size WALReadFromBuffers(char *dstbuf, XLogRecPtr startptr, Size count,
							   TimeLineID tli);

<<<<<<< HEAD
extern void assign_max_wal_size(int newval, void *extra);
extern void assign_checkpoint_completion_target(double newval, void *extra);
extern bool CheckForStandbyTrigger(void);

/* POLAR: some static exposed as external */
extern XLogRecPtr XLogGetReplicationSlotMinimumLSN(void);
/* POLAR end */

/* POLAR */
extern bool polar_in_replica_mode(void);
extern void polar_set_oldest_applied_lsn(XLogRecPtr oldest_apply_lsn, XLogRecPtr oldest_lock_lsn);
extern XLogRecPtr polar_get_oldest_applied_lsn(void);
extern XLogRecPtr polar_get_oldest_lock_lsn(void);
extern XLogRecPtr polar_get_consistent_lsn(void);
extern void polar_set_consistent_lsn(XLogRecPtr consistent_lsn);
extern void polar_checkpointer_do_reload(void);
extern bool polar_checkpointer_recv_shutdown_requested(void);
extern void polar_load_and_check_controlfile(void);
extern int64 polar_get_diff_consistent_oldest_lsn(void);
extern bool polar_read_control_file(char *polar_ctl_file_path, struct ControlFileData *polar_control_file_buffer, int error_level, int *saved_errno);
extern XLogRecPtr polar_get_diff_checkpoint_flush_lsn(void);
=======
/*
 * Routines used by xlogrecovery.c to call back into xlog.c during recovery.
 */
extern void RemoveNonParentXlogFiles(XLogRecPtr switchpoint, TimeLineID newTLI);
extern bool XLogCheckpointNeeded(XLogSegNo new_segno);
extern void SwitchIntoArchiveRecovery(XLogRecPtr EndRecPtr, TimeLineID replayTLI);
extern void ReachedEndOfBackup(XLogRecPtr EndRecPtr, TimeLineID tli);
extern void SetInstallXLogFileSegmentActive(void);
extern bool IsInstallXLogFileSegmentActive(void);
extern void XLogShutdownWalRcv(void);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/*
 * Routines to start, stop, and get status of a base backup.
 */

/*
 * Session-level status of base backups
 *
 * This is used in parallel with the shared memory status to control parallel
 * execution of base backup functions for a given session, be it a backend
 * dedicated to replication or a normal backend connected to a database. The
 * update of the session-level status happens at the same time as the shared
 * memory counters to keep a consistent global and local state of the backups
 * running.
 */
typedef enum SessionBackupState
{
	SESSION_BACKUP_NONE,
	SESSION_BACKUP_RUNNING,
} SessionBackupState;

<<<<<<< HEAD
extern XLogRecPtr do_pg_start_backup(const char *backupidstr, bool fast,
				   TimeLineID *starttli_p, StringInfo labelfile,
				   List **tablespaces, StringInfo tblspcmapfile, bool infotbssize,
				   bool needtblspcmapfile);
extern XLogRecPtr do_pg_stop_backup(char *labelfile, bool waitforarchive,
				  TimeLineID *stoptli_p);
=======
extern void do_pg_backup_start(const char *backupidstr, bool fast,
							   List **tablespaces, BackupState *state,
							   StringInfo tblspcmapfile);
extern void do_pg_backup_stop(BackupState *state, bool waitforarchive);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern void do_pg_abort_backup(int code, Datum arg);
extern void register_persistent_abort_backup_handler(void);
extern SessionBackupState get_backup_status(void);

/* POLAR */
#define polar_is_datamax() (polar_node_type() == POLAR_STANDALONE_DATAMAX)
#define polar_is_standby() \
	(polar_enable_shared_storage_mode && polar_node_type() == POLAR_STANDBY)
/* POLAR: judge standby node type no matter whether polar_enable_shared_storage_mode is true */
#define polar_in_standby_mode() (polar_node_type() == POLAR_STANDBY)
#define polar_is_master_in_recovery() \
	(RecoveryInProgress() && polar_is_master())
#define polar_max_valid_lsn() \
	((polar_is_master() && !RecoveryInProgress()) ? GetXLogInsertRecPtr() : GetXLogReplayRecPtr(NULL))

/* use XLogWriteRecPtr instead of XLogInsertRecPtr, because XLogInsertRecPtr not equal to XLogReplayRecPtr after switch xlog*/
#define polar_px_max_valid_lsn() \
	((polar_is_master() && !RecoveryInProgress()) ? GetXLogWriteRecPtr() : GetXLogReplayRecPtr(NULL))

extern bool polar_exist_xlog_replaying(void);
extern XLogRecPtr polar_get_replay_end_rec_ptr(TimeLineID *replyTLI);
extern void polar_wait_primary_xlog_message(XLogReaderState *state);
extern void polar_set_read_and_end_rec_ptr(XLogRecPtr read_rec_ptr, XLogRecPtr end_rec_ptr);
extern XLogRecPtr polar_get_faked_latest_lsn(void);
extern XLogRecPtr polar_calc_min_used_lsn(bool is_contain_replication_slot);
extern void polar_update_receipt_time(void);
extern void polar_keep_wal_receiver_up(XLogRecPtr lsn);
extern void polar_set_node_type(PolarNodeType node_type);
extern PolarNodeType polar_node_type(void);
extern bool polar_is_master(void);
extern bool polar_in_master_mode(void);
extern int	get_sync_bit(int method);
extern struct ControlFileData* polar_get_control_file(void);
extern void polar_set_hot_standby_state(HotStandbyState state);
extern HotStandbyState polar_get_hot_standby_state(void);
extern XLogRecPtr polar_get_async_lock_replay_rec_ptr(void);
extern void polar_async_update_last_ptr(void);
extern void polar_set_receipt_time(TimestampTz rtime);
extern void polar_update_last_removed_ptr(char *filename);
extern XLogRecPtr polar_get_last_valid_lsn(void);

/* POLAR Consensus */
extern bool polar_check_pm_in_state_change(void);
extern void polar_signal_pm_state_change(int state,
					const char *leaderAddr, int leaderPort, uint64 term, 
					uint64 nextAppendTerm, uint32 tli, uint64 logUpto);
extern void polar_signal_recovery_state_change(
					bool newLeader, bool resumeLeader);
extern void polar_fill_segment_file_zero(int fd, char *tmppath, int segment_size,
		uint32 init_write_event_info, uint32 init_fsync_event_info,
		const char *log_file_info);
extern bool polar_dma_check_logger_status(char **primaryConnInfo,
					XLogRecPtr *receivedUpto, TimeLineID *receivedTLI, 
					bool *requestNextTLI);
extern int polar_wait_recovery_wakeup(int wakeEvents, long timeout, 
					uint32 wait_event_info);
extern bool polar_is_dma_data_node(void);
extern bool polar_is_dma_logger_node(void);
extern XLogRecPtr polar_dma_get_flush_lsn(bool committed, bool in_recovery);



/* File path names (all relative to $PGDATA) */
#define RECOVERY_SIGNAL_FILE	"recovery.signal"
#define STANDBY_SIGNAL_FILE		"standby.signal"
#define BACKUP_LABEL_FILE		"backup_label"
#define BACKUP_LABEL_OLD		"backup_label.old"
/* POLAR: filename of polar_backup_label on shared storage */
#define POLAR_EXCLUSIVE_BACKUP_LABEL_FILE		"polar_exclusive_backup_label"
#define POLAR_NON_EXCLUSIVE_BACKUP_LABEL_FILE	"polar_non_exclusive_backup_label"

#define TABLESPACE_MAP			"tablespace_map"
#define TABLESPACE_MAP_OLD		"tablespace_map.old"
/* POLAR: filename of tablespace_map on shared storage */
#define POLAR_EXCLUSIVE_TABLESPACE_MAP		"polar_exclusive_tablespace_map"
#define POLAR_NON_EXCLUSIVE_TABLESPACE_MAP	"polar_non_exclusive_tablespace_map"

/* POLAR: from xlog.c move here */
#define RECOVERY_COMMAND_DONE	"recovery.done"

/* POLAR: filename of force promote */
#define POLAR_FORCE_PROMOTE_SIGNAL_FILE     "polar_force_promote"
#define POLAR_PROMOTE_NOT_ALLOWED_FILE      "polar_promote_not_allowed"

/* POLAR wal pipeline begin */

/*
 * polar wal pipeline is enable when:
 * 1. on instance with right polar instance specification
 * 2. polar_wal_pipeline_enable = true
 * 3. not in bootstrap and single mode
 */
#define POLAR_WAL_PIPELINER_ENABLE() \
	(POLAR_INSTANCE_SPEC_WAL_PIPELINE_IS_AVAILABLE() && polar_wal_pipeline_enable && IsPostmasterEnvironment)
/*
 * polar wal pipeliner is ready to work when:
 * 1. POLAR_WAL_PIPELINE_ENABLE is true
 * 2. not in recovery mode
 * 3. all threads of wal pipeliner have started
 */
#define POLAR_WAL_PIPELINER_READY() \
	(POLAR_WAL_PIPELINER_ENABLE() && !RecoveryInProgress() && ProcGlobal->polar_wal_pipeliner_latch != NULL)

extern bool polar_wal_pipeline_advance(int ident);
extern bool polar_wal_pipeline_write(int ident);
extern bool polar_wal_pipeline_flush(int ident);
extern bool polar_wal_pipeline_notify(int ident);

extern void polar_wal_pipeline_set_last_notify_lsn(int ident, XLogRecPtr last_notify_lsn);
extern void polar_wal_pipeline_set_ready_write_lsn(XLogRecPtr ready_write_lsn);
extern XLogRecPtr polar_wal_pipeline_get_current_insert_lsn(void);
extern XLogRecPtr polar_wal_pipeline_get_continuous_insert_lsn(void);
extern XLogRecPtr polar_wal_pipeline_get_write_lsn(void);
extern XLogRecPtr polar_wal_pipeline_get_flush_lsn(void);
extern XLogRecPtr polar_wal_pipeline_get_last_notify_lsn(int ident);
extern XLogRecPtr polar_wal_pipeline_get_ready_write_lsn(void);
extern uint64 polar_wal_pipeline_get_unflushed_xlog_add_slot_no(void);
extern uint64 polar_wal_pipeline_get_unflushed_xlog_del_slot_no(void);
extern void polar_wal_pipeline_stats_reset(void);
extern void polar_wal_pipeline_recent_written_add_link(XLogRecPtr start_lsn, XLogRecPtr end_lsn);
extern void polar_wal_pipeline_commit_wait(XLogRecPtr flush_lsn);

/* 
 *	Only for polar wal pipeline test 
 */
extern void polar_wal_pipeline_set_local_recovery_mode(bool mode);

/* POLAR wal pipeline end */

/* POLAR */
extern void polar_set_replica_update_dirs_by_redo(bool value);
extern bool polar_replica_update_dirs_by_redo(void);

extern void polar_set_available_state(bool state);
extern bool polar_get_available_state(void);
extern const char *polar_node_type_string(PolarNodeType type, int error_level);
extern const char *polar_standby_state_string(int state, int error_level);

/* files to signal promotion to primary */
#define PROMOTE_SIGNAL_FILE		"promote"

#endif							/* XLOG_H */
