/* ----------
 *	pgstat.h
 *
 *	Definitions for the PostgreSQL cumulative statistics system.
 *
 *	Copyright (c) 2001-2024, PostgreSQL Global Development Group
 *
 *	src/include/pgstat.h
 * ----------
 */
#ifndef PGSTAT_H
#define PGSTAT_H

#include "access/xlogdefs.h"
#include "datatype/timestamp.h"
#include "portability/instr_time.h"
#include "postmaster/pgarch.h"	/* for MAX_XFN_CHARS */
#include "replication/conflict.h"
#include "utils/backend_progress.h" /* for backward compatibility */
#include "utils/backend_status.h"	/* for backward compatibility */
#include "utils/relcache.h"
<<<<<<< HEAD
#include "utils/guc.h"
=======
#include "utils/wait_event.h"	/* for backward compatibility */
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c


/* ----------
 * Paths for the statistics files (relative to installation's $PGDATA).
 * ----------
 */
#define PGSTAT_STAT_PERMANENT_DIRECTORY		"pg_stat"
#define PGSTAT_STAT_PERMANENT_FILENAME		"pg_stat/pgstat.stat"
#define PGSTAT_STAT_PERMANENT_TMPFILE		"pg_stat/pgstat.tmp"

/* Default directory to store temporary statistics data in */
#define PG_STAT_TMP_DIR		"pg_stat_tmp"

<<<<<<< HEAD
/* POLARDB Proxy base virtual pid */
#define POLAR_BASE_SESSION_ID		POLAR_MAX_PROCESS_COUNT
#define POLAR_BASE_VIRTUAL_PID		2 * POLAR_MAX_PROCESS_COUNT

#define POLAR_IS_REAL_PID(pid)		(0 < pid && pid < POLAR_MAX_PROCESS_COUNT)
#define POLAR_IS_SESSION_ID(pid)	(POLAR_BASE_SESSION_ID <= pid && pid < POLAR_BASE_VIRTUAL_PID)
#define POLAR_IS_VIRTUAL_PID(pid)	(POLAR_BASE_VIRTUAL_PID <= pid && pid < 2 * POLAR_BASE_VIRTUAL_PID)

=======
/* The types of statistics entries */
#define PgStat_Kind uint32

/* Range of IDs allowed, for built-in and custom kinds */
#define PGSTAT_KIND_MIN	1		/* Minimum ID allowed */
#define PGSTAT_KIND_MAX	256		/* Maximum ID allowed */

/* use 0 for INVALID, to catch zero-initialized data */
#define PGSTAT_KIND_INVALID 0

/* stats for variable-numbered objects */
#define PGSTAT_KIND_DATABASE	1	/* database-wide statistics */
#define PGSTAT_KIND_RELATION	2	/* per-table statistics */
#define PGSTAT_KIND_FUNCTION	3	/* per-function statistics */
#define PGSTAT_KIND_REPLSLOT	4	/* per-slot statistics */
#define PGSTAT_KIND_SUBSCRIPTION	5	/* per-subscription statistics */

/* stats for fixed-numbered objects */
#define PGSTAT_KIND_ARCHIVER	6
#define PGSTAT_KIND_BGWRITER	7
#define PGSTAT_KIND_CHECKPOINTER	8
#define PGSTAT_KIND_IO	9
#define PGSTAT_KIND_SLRU	10
#define PGSTAT_KIND_WAL	11

#define PGSTAT_KIND_BUILTIN_MIN PGSTAT_KIND_DATABASE
#define PGSTAT_KIND_BUILTIN_MAX PGSTAT_KIND_WAL
#define PGSTAT_KIND_BUILTIN_SIZE (PGSTAT_KIND_BUILTIN_MAX + 1)

/* Custom stats kinds */

/* Range of IDs allowed for custom stats kinds */
#define PGSTAT_KIND_CUSTOM_MIN	128
#define PGSTAT_KIND_CUSTOM_MAX	PGSTAT_KIND_MAX
#define PGSTAT_KIND_CUSTOM_SIZE	(PGSTAT_KIND_CUSTOM_MAX - PGSTAT_KIND_CUSTOM_MIN + 1)

/*
 * PgStat_Kind to use for extensions that require an ID, but are still in
 * development and have not reserved their own unique kind ID yet. See:
 * https://wiki.postgresql.org/wiki/CustomCumulativeStats
 */
#define PGSTAT_KIND_EXPERIMENTAL	128

static inline bool
pgstat_is_kind_builtin(PgStat_Kind kind)
{
	return kind >= PGSTAT_KIND_BUILTIN_MIN && kind <= PGSTAT_KIND_BUILTIN_MAX;
}

static inline bool
pgstat_is_kind_custom(PgStat_Kind kind)
{
	return kind >= PGSTAT_KIND_CUSTOM_MIN && kind <= PGSTAT_KIND_CUSTOM_MAX;
}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/* Values for track_functions GUC variable --- order is significant! */
typedef enum TrackFunctionsLevel
{
	TRACK_FUNC_OFF,
	TRACK_FUNC_PL,
	TRACK_FUNC_ALL,
}			TrackFunctionsLevel;

typedef enum PgStat_FetchConsistency
{
<<<<<<< HEAD
	PGSTAT_MTYPE_DUMMY,
	PGSTAT_MTYPE_INQUIRY,
	PGSTAT_MTYPE_TABSTAT,
	PGSTAT_MTYPE_TABPURGE,
	PGSTAT_MTYPE_DROPDB,
	PGSTAT_MTYPE_RESETCOUNTER,
	PGSTAT_MTYPE_RESETSHAREDCOUNTER,
	PGSTAT_MTYPE_RESETSINGLECOUNTER,
	PGSTAT_MTYPE_AUTOVAC_START,
	PGSTAT_MTYPE_VACUUM,
	PGSTAT_MTYPE_ANALYZE,
	PGSTAT_MTYPE_ARCHIVER,
	PGSTAT_MTYPE_BGWRITER,
	PGSTAT_MTYPE_FUNCSTAT,
	PGSTAT_MTYPE_FUNCPURGE,
	PGSTAT_MTYPE_RECOVERYCONFLICT,
	PGSTAT_MTYPE_TEMPFILE,
	PGSTAT_MTYPE_DEADLOCK,
	/* POLAR */
	POLAR_PGSTAT_MTYPE_DELAY_DML
} StatMsgType;
=======
	PGSTAT_FETCH_CONSISTENCY_NONE,
	PGSTAT_FETCH_CONSISTENCY_CACHE,
	PGSTAT_FETCH_CONSISTENCY_SNAPSHOT,
} PgStat_FetchConsistency;

/* Values to track the cause of session termination */
typedef enum SessionEndType
{
	DISCONNECT_NOT_YET,			/* still active */
	DISCONNECT_NORMAL,
	DISCONNECT_CLIENT_EOF,
	DISCONNECT_FATAL,
	DISCONNECT_KILLED,
} SessionEndType;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/* ----------
 * The data type used for counters.
 * ----------
 */
typedef int64 PgStat_Counter;


/* ------------------------------------------------------------
 * Structures kept in backend local memory while accumulating counts
 * ------------------------------------------------------------
 */

/* ----------
 * PgStat_FunctionCounts	The actual per-function counts kept by a backend
 *
 * This struct should contain only actual event counters, because we memcmp
 * it against zeroes to detect whether there are any pending stats.
 *
 * Note that the time counters are in instr_time format here.  We convert to
 * microseconds in PgStat_Counter format when flushing out pending statistics.
 * ----------
 */
typedef struct PgStat_FunctionCounts
{
	PgStat_Counter numcalls;
	instr_time	total_time;
	instr_time	self_time;
} PgStat_FunctionCounts;

/*
 * Working state needed to accumulate per-function-call timing statistics.
 */
typedef struct PgStat_FunctionCallUsage
{
	/* Link to function's hashtable entry (must still be there at exit!) */
	/* NULL means we are not tracking the current function call */
	PgStat_FunctionCounts *fs;
	/* Total time previously charged to function, as of function start */
	instr_time	save_f_total_time;
	/* Backend-wide total time as of function start */
	instr_time	save_total;
	/* system clock as of function start */
	instr_time	start;
} PgStat_FunctionCallUsage;

/* ----------
 * PgStat_BackendSubEntry	Non-flushed subscription stats.
 * ----------
 */
typedef struct PgStat_BackendSubEntry
{
	PgStat_Counter apply_error_count;
	PgStat_Counter sync_error_count;
	PgStat_Counter conflict_count[CONFLICT_NUM_TYPES];
} PgStat_BackendSubEntry;

/* ----------
 * PgStat_TableCounts			The actual per-table counts kept by a backend
 *
 * This struct should contain only actual event counters, because we memcmp
 * it against zeroes to detect whether there are any stats updates to apply.
 * It is a component of PgStat_TableStatus (within-backend state).
 *
 * Note: for a table, tuples_returned is the number of tuples successfully
 * fetched by heap_getnext, while tuples_fetched is the number of tuples
 * successfully fetched by heap_fetch under the control of bitmap indexscans.
 * For an index, tuples_returned is the number of index entries returned by
 * the index AM, while tuples_fetched is the number of tuples successfully
 * fetched by heap_fetch under the control of simple indexscans for this index.
 *
 * tuples_inserted/updated/deleted/hot_updated/newpage_updated count attempted
 * actions, regardless of whether the transaction committed.  delta_live_tuples,
 * delta_dead_tuples, and changed_tuples are set depending on commit or abort.
 * Note that delta_live_tuples and delta_dead_tuples can be negative!
 * ----------
 */
typedef struct PgStat_TableCounts
{
	PgStat_Counter numscans;

	PgStat_Counter tuples_returned;
	PgStat_Counter tuples_fetched;

	PgStat_Counter tuples_inserted;
	PgStat_Counter tuples_updated;
	PgStat_Counter tuples_deleted;
	PgStat_Counter tuples_hot_updated;
	PgStat_Counter tuples_newpage_updated;
	bool		truncdropped;

	PgStat_Counter delta_live_tuples;
	PgStat_Counter delta_dead_tuples;
	PgStat_Counter changed_tuples;

<<<<<<< HEAD
	PgStat_Counter t_blocks_fetched;
	PgStat_Counter t_blocks_hit;

	/* POLAR: bulk read stats */
	/* all bulk read calls count */
	PgStat_Counter polar_t_bulk_read_calls;
	/* bulk read calls times which has IO read */
	PgStat_Counter polar_t_bulk_read_calls_IO;
	/* bulk read calls, IO read blocks counts */
	PgStat_Counter polar_t_bulk_read_blocks_IO;
	/* POLAR: end */
=======
	PgStat_Counter blocks_fetched;
	PgStat_Counter blocks_hit;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
} PgStat_TableCounts;

/* ----------
 * PgStat_TableStatus			Per-table status within a backend
 *
 * Many of the event counters are nontransactional, ie, we count events
 * in committed and aborted transactions alike.  For these, we just count
 * directly in the PgStat_TableStatus.  However, delta_live_tuples,
 * delta_dead_tuples, and changed_tuples must be derived from event counts
 * with awareness of whether the transaction or subtransaction committed or
 * aborted.  Hence, we also keep a stack of per-(sub)transaction status
 * records for every table modified in the current transaction.  At commit
 * or abort, we propagate tuples_inserted/updated/deleted up to the
 * parent subtransaction level, or out to the parent PgStat_TableStatus,
 * as appropriate.
 * ----------
 */
typedef struct PgStat_TableStatus
{
	Oid			id;				/* table's OID */
	bool		shared;			/* is it a shared catalog? */
	struct PgStat_TableXactStatus *trans;	/* lowest subxact's counts */
	PgStat_TableCounts counts;	/* event counts to be sent */
	Relation	relation;		/* rel that is using this entry */
} PgStat_TableStatus;

/* ----------
 * PgStat_TableXactStatus		Per-table, per-subtransaction status
 * ----------
 */
typedef struct PgStat_TableXactStatus
{
	PgStat_Counter tuples_inserted; /* tuples inserted in (sub)xact */
	PgStat_Counter tuples_updated;	/* tuples updated in (sub)xact */
	PgStat_Counter tuples_deleted;	/* tuples deleted in (sub)xact */
	bool		truncdropped;	/* relation truncated/dropped in this
								 * (sub)xact */
	/* tuples i/u/d prior to truncate/drop */
	PgStat_Counter inserted_pre_truncdrop;
	PgStat_Counter updated_pre_truncdrop;
	PgStat_Counter deleted_pre_truncdrop;
	int			nest_level;		/* subtransaction nest level */
	/* links to other structs for same relation: */
	struct PgStat_TableXactStatus *upper;	/* next higher subxact if any */
	PgStat_TableStatus *parent; /* per-table status */
	/* structs of same subxact level are linked here: */
	struct PgStat_TableXactStatus *next;	/* next of same subxact */
} PgStat_TableXactStatus;


/* ------------------------------------------------------------
<<<<<<< HEAD
 * Message formats follow
 * ------------------------------------------------------------
 */


/* ----------
 * PgStat_MsgHdr				The common message header
 * ----------
 */
typedef struct PgStat_MsgHdr
{
	StatMsgType m_type;
	int			m_size;
} PgStat_MsgHdr;

/* ----------
 * Space available in a message.  This will keep the UDP packets below 1K,
 * which should fit unfragmented into the MTU of the loopback interface.
 * (Larger values of PGSTAT_MAX_MSG_SIZE would work for that on most
 * platforms, but we're being conservative here.)
 * ----------
 */
#define PGSTAT_MAX_MSG_SIZE 1000
#define PGSTAT_MSG_PAYLOAD	(PGSTAT_MAX_MSG_SIZE - sizeof(PgStat_MsgHdr))


/* ----------
 * PgStat_MsgDummy				A dummy message, ignored by the collector
 * ----------
 */
typedef struct PgStat_MsgDummy
{
	PgStat_MsgHdr m_hdr;
} PgStat_MsgDummy;


/* ----------
 * PgStat_MsgInquiry			Sent by a backend to ask the collector
 *								to write the stats file(s).
 *
 * Ordinarily, an inquiry message prompts writing of the global stats file,
 * the stats file for shared catalogs, and the stats file for the specified
 * database.  If databaseid is InvalidOid, only the first two are written.
 *
 * New file(s) will be written only if the existing file has a timestamp
 * older than the specified cutoff_time; this prevents duplicated effort
 * when multiple requests arrive at nearly the same time, assuming that
 * backends send requests with cutoff_times a little bit in the past.
 *
 * clock_time should be the requestor's current local time; the collector
 * uses this to check for the system clock going backward, but it has no
 * effect unless that occurs.  We assume clock_time >= cutoff_time, though.
 * ----------
 */

typedef struct PgStat_MsgInquiry
{
	PgStat_MsgHdr m_hdr;
	TimestampTz clock_time;		/* observed local clock time */
	TimestampTz cutoff_time;	/* minimum acceptable file timestamp */
	Oid			databaseid;		/* requested DB (InvalidOid => shared only) */
} PgStat_MsgInquiry;


/* ----------
 * PgStat_TableEntry			Per-table info in a MsgTabstat
 * ----------
 */
typedef struct PgStat_TableEntry
{
	Oid			t_id;
	PgStat_TableCounts t_counts;
} PgStat_TableEntry;

/* ----------
 * PgStat_MsgTabstat			Sent by the backend to report table
 *								and buffer access statistics.
 * ----------
 */
#define PGSTAT_NUM_TABENTRIES  \
	((PGSTAT_MSG_PAYLOAD - sizeof(Oid) - 3 * sizeof(int) - 2 * sizeof(PgStat_Counter))	\
	 / sizeof(PgStat_TableEntry))

typedef struct PgStat_MsgTabstat
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
	int			m_nentries;
	int			m_xact_commit;
	int			m_xact_rollback;
	PgStat_Counter m_block_read_time;	/* times in microseconds */
	PgStat_Counter m_block_write_time;
	PgStat_TableEntry m_entry[PGSTAT_NUM_TABENTRIES];
} PgStat_MsgTabstat;


/* ----------
 * PgStat_MsgTabpurge			Sent by the backend to tell the collector
 *								about dead tables.
 * ----------
 */
#define PGSTAT_NUM_TABPURGE  \
	((PGSTAT_MSG_PAYLOAD - sizeof(Oid) - sizeof(int))  \
	 / sizeof(Oid))

typedef struct PgStat_MsgTabpurge
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
	int			m_nentries;
	Oid			m_tableid[PGSTAT_NUM_TABPURGE];
} PgStat_MsgTabpurge;


/* ----------
 * PgStat_MsgDropdb				Sent by the backend to tell the collector
 *								about a dropped database
 * ----------
 */
typedef struct PgStat_MsgDropdb
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
} PgStat_MsgDropdb;


/* ----------
 * PgStat_MsgResetcounter		Sent by the backend to tell the collector
 *								to reset counters
 * ----------
 */
typedef struct PgStat_MsgResetcounter
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
} PgStat_MsgResetcounter;

/* ----------
 * PgStat_MsgResetsharedcounter Sent by the backend to tell the collector
 *								to reset a shared counter
 * ----------
 */
typedef struct PgStat_MsgResetsharedcounter
{
	PgStat_MsgHdr m_hdr;
	PgStat_Shared_Reset_Target m_resettarget;
} PgStat_MsgResetsharedcounter;

/* ----------
 * PgStat_MsgResetsinglecounter Sent by the backend to tell the collector
 *								to reset a single counter
 * ----------
 */
typedef struct PgStat_MsgResetsinglecounter
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
	PgStat_Single_Reset_Type m_resettype;
	Oid			m_objectid;
} PgStat_MsgResetsinglecounter;

/* ----------
 * PgStat_MsgAutovacStart		Sent by the autovacuum daemon to signal
 *								that a database is going to be processed
 * ----------
 */
typedef struct PgStat_MsgAutovacStart
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
	TimestampTz m_start_time;
} PgStat_MsgAutovacStart;


/* ----------
 * PgStat_MsgVacuum				Sent by the backend or autovacuum daemon
 *								after VACUUM
 * ----------
 */
typedef struct PgStat_MsgVacuum
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
	Oid			m_tableoid;
	bool		m_autovacuum;
	TimestampTz m_vacuumtime;
	PgStat_Counter m_live_tuples;
	PgStat_Counter m_dead_tuples;
} PgStat_MsgVacuum;


/* ----------
 * PgStat_MsgAnalyze			Sent by the backend or autovacuum daemon
 *								after ANALYZE
 * ----------
 */
typedef struct PgStat_MsgAnalyze
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
	Oid			m_tableoid;
	bool		m_autovacuum;
	bool		m_resetcounter;
	TimestampTz m_analyzetime;
	PgStat_Counter m_live_tuples;
	PgStat_Counter m_dead_tuples;
} PgStat_MsgAnalyze;


/* ----------
 * PgStat_MsgArchiver			Sent by the archiver to update statistics.
 * ----------
 */
typedef struct PgStat_MsgArchiver
{
	PgStat_MsgHdr m_hdr;
	bool		m_failed;		/* Failed attempt */
	char		m_xlog[MAX_XFN_CHARS + 1];
	TimestampTz m_timestamp;
} PgStat_MsgArchiver;

/* ----------
 * PgStat_MsgBgWriter			Sent by the bgwriter to update statistics.
 * ----------
 */
typedef struct PgStat_MsgBgWriter
{
	PgStat_MsgHdr m_hdr;

	PgStat_Counter m_timed_checkpoints;
	PgStat_Counter m_requested_checkpoints;
	PgStat_Counter m_buf_written_checkpoints;
	PgStat_Counter m_buf_written_clean;
	PgStat_Counter m_maxwritten_clean;
	PgStat_Counter m_buf_written_backend;
	PgStat_Counter m_buf_fsync_backend;
	PgStat_Counter m_buf_alloc;
	PgStat_Counter m_checkpoint_write_time; /* times in milliseconds */
	PgStat_Counter m_checkpoint_sync_time;
} PgStat_MsgBgWriter;

/* ----------
 * PgStat_MsgRecoveryConflict	Sent by the backend upon recovery conflict
 * ----------
 */
typedef struct PgStat_MsgRecoveryConflict
{
	PgStat_MsgHdr m_hdr;

	Oid			m_databaseid;
	int			m_reason;
} PgStat_MsgRecoveryConflict;

/* ----------
 * PgStat_MsgTempFile	Sent by the backend upon creating a temp file
 * ----------
 */
typedef struct PgStat_MsgTempFile
{
	PgStat_MsgHdr m_hdr;

	Oid			m_databaseid;
	size_t		m_filesize;
} PgStat_MsgTempFile;


typedef struct PolarStat_MsgDelayDml
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
} PolarStat_MsgDelayDml;

/* ----------
 * PgStat_FunctionCounts	The actual per-function counts kept by a backend
 *
 * This struct should contain only actual event counters, because we memcmp
 * it against zeroes to detect whether there are any counts to transmit.
 *
 * Note that the time counters are in instr_time format here.  We convert to
 * microseconds in PgStat_Counter format when transmitting to the collector.
 * ----------
 */
typedef struct PgStat_FunctionCounts
{
	PgStat_Counter f_numcalls;
	instr_time	f_total_time;
	instr_time	f_self_time;
} PgStat_FunctionCounts;

/* ----------
 * PgStat_BackendFunctionEntry	Entry in backend's per-function hash table
 * ----------
 */
typedef struct PgStat_BackendFunctionEntry
{
	Oid			f_id;
	PgStat_FunctionCounts f_counts;
} PgStat_BackendFunctionEntry;

/* ----------
 * PgStat_FunctionEntry			Per-function info in a MsgFuncstat
 * ----------
 */
typedef struct PgStat_FunctionEntry
{
	Oid			f_id;
	PgStat_Counter f_numcalls;
	PgStat_Counter f_total_time;	/* times in microseconds */
	PgStat_Counter f_self_time;
} PgStat_FunctionEntry;

/* ----------
 * PgStat_MsgFuncstat			Sent by the backend to report function
 *								usage statistics.
 * ----------
 */
#define PGSTAT_NUM_FUNCENTRIES	\
	((PGSTAT_MSG_PAYLOAD - sizeof(Oid) - sizeof(int))  \
	 / sizeof(PgStat_FunctionEntry))

typedef struct PgStat_MsgFuncstat
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
	int			m_nentries;
	PgStat_FunctionEntry m_entry[PGSTAT_NUM_FUNCENTRIES];
} PgStat_MsgFuncstat;

/* ----------
 * PgStat_MsgFuncpurge			Sent by the backend to tell the collector
 *								about dead functions.
 * ----------
 */
#define PGSTAT_NUM_FUNCPURGE  \
	((PGSTAT_MSG_PAYLOAD - sizeof(Oid) - sizeof(int))  \
	 / sizeof(Oid))

typedef struct PgStat_MsgFuncpurge
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
	int			m_nentries;
	Oid			m_functionid[PGSTAT_NUM_FUNCPURGE];
} PgStat_MsgFuncpurge;

/* ----------
 * PgStat_MsgDeadlock			Sent by the backend to tell the collector
 *								about a deadlock that occurred.
 * ----------
 */
typedef struct PgStat_MsgDeadlock
{
	PgStat_MsgHdr m_hdr;
	Oid			m_databaseid;
} PgStat_MsgDeadlock;


/* ----------
 * PgStat_Msg					Union over all possible messages.
 * ----------
 */
typedef union PgStat_Msg
{
	PgStat_MsgHdr msg_hdr;
	PgStat_MsgDummy msg_dummy;
	PgStat_MsgInquiry msg_inquiry;
	PgStat_MsgTabstat msg_tabstat;
	PgStat_MsgTabpurge msg_tabpurge;
	PgStat_MsgDropdb msg_dropdb;
	PgStat_MsgResetcounter msg_resetcounter;
	PgStat_MsgResetsharedcounter msg_resetsharedcounter;
	PgStat_MsgResetsinglecounter msg_resetsinglecounter;
	PgStat_MsgAutovacStart msg_autovacuum;
	PgStat_MsgVacuum msg_vacuum;
	PgStat_MsgAnalyze msg_analyze;
	PgStat_MsgArchiver msg_archiver;
	PgStat_MsgBgWriter msg_bgwriter;
	PgStat_MsgFuncstat msg_funcstat;
	PgStat_MsgFuncpurge msg_funcpurge;
	PgStat_MsgRecoveryConflict msg_recoveryconflict;
	PgStat_MsgDeadlock msg_deadlock;
} PgStat_Msg;


/* ------------------------------------------------------------
 * Statistic collector data structures follow
=======
 * Data structures on disk and in shared memory follow
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 *
 * PGSTAT_FILE_FORMAT_ID should be changed whenever any of these
 * data structures change.
 * ------------------------------------------------------------
 */

<<<<<<< HEAD
#define PGSTAT_FILE_FORMAT_ID	0x11A5BC9E

/* ----------
 * PgStat_StatDBEntry			The collector's data per database
 * ----------
 */
typedef struct PgStat_StatDBEntry
{
	Oid			databaseid;
	PgStat_Counter n_xact_commit;
	PgStat_Counter n_xact_rollback;
	PgStat_Counter n_blocks_fetched;
	PgStat_Counter n_blocks_hit;
	PgStat_Counter n_tuples_returned;
	PgStat_Counter n_tuples_fetched;
	PgStat_Counter n_tuples_inserted;
	PgStat_Counter n_tuples_updated;
	PgStat_Counter n_tuples_deleted;
	TimestampTz last_autovac_time;
	PgStat_Counter n_conflict_tablespace;
	PgStat_Counter n_conflict_lock;
	PgStat_Counter n_conflict_snapshot;
	PgStat_Counter n_conflict_bufferpin;
	PgStat_Counter n_conflict_startup_deadlock;
	PgStat_Counter n_temp_files;
	PgStat_Counter n_temp_bytes;
	PgStat_Counter n_deadlocks;
	PgStat_Counter n_block_read_time;	/* times in microseconds */
	PgStat_Counter n_block_write_time;

	TimestampTz stat_reset_timestamp;
	TimestampTz stats_timestamp;	/* time of db stats file update */

	/* POLAR: bulk read stats */
	/* all bulk read calls count */
	PgStat_Counter polar_n_bulk_read_calls;
	/* bulk read calls times which has IO read */
	PgStat_Counter polar_n_bulk_read_calls_IO;
	/* bulk read calls, IO read blocks counts */
	PgStat_Counter polar_n_bulk_read_blocks_IO;
	/* POLAR: end */

	/* POLAR: delay dml stats */
	PgStat_Counter polar_delay_dml_count;

	/*
	 * tables and functions must be last in the struct, because we don't write
	 * the pointers out to the stats file.
	 */
	HTAB	   *tables;
	HTAB	   *functions;
} PgStat_StatDBEntry;


/* ----------
 * PgStat_StatTabEntry			The collector's data per table (or index)
 * ----------
 */
typedef struct PgStat_StatTabEntry
{
	Oid			tableid;

	PgStat_Counter numscans;

	PgStat_Counter tuples_returned;
	PgStat_Counter tuples_fetched;

	PgStat_Counter tuples_inserted;
	PgStat_Counter tuples_updated;
	PgStat_Counter tuples_deleted;
	PgStat_Counter tuples_hot_updated;

	PgStat_Counter n_live_tuples;
	PgStat_Counter n_dead_tuples;
	PgStat_Counter changes_since_analyze;

	PgStat_Counter blocks_fetched;
	PgStat_Counter blocks_hit;

	TimestampTz vacuum_timestamp;	/* user initiated vacuum */
	PgStat_Counter vacuum_count;
	TimestampTz autovac_vacuum_timestamp;	/* autovacuum initiated */
	PgStat_Counter autovac_vacuum_count;
	TimestampTz analyze_timestamp;	/* user initiated */
	PgStat_Counter analyze_count;
	TimestampTz autovac_analyze_timestamp;	/* autovacuum initiated */
	PgStat_Counter autovac_analyze_count;

	/* POLAR: bulk read stats */
	/* all bulk read calls count */
	PgStat_Counter polar_bulk_read_calls;
	/* bulk read calls times which has IO read */
	PgStat_Counter polar_bulk_read_calls_IO;
	/* bulk read calls, IO read blocks counts */
	PgStat_Counter polar_bulk_read_blocks_IO;
	/* POLAR: end */
} PgStat_StatTabEntry;


/* ----------
 * PgStat_StatFuncEntry			The collector's data per function
 * ----------
 */
typedef struct PgStat_StatFuncEntry
{
	Oid			functionid;

	PgStat_Counter f_numcalls;

	PgStat_Counter f_total_time;	/* times in microseconds */
	PgStat_Counter f_self_time;
} PgStat_StatFuncEntry;


/*
 * Archiver statistics kept in the stats collector
 */
=======
#define PGSTAT_FILE_FORMAT_ID	0x01A5BCAF

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
typedef struct PgStat_ArchiverStats
{
	PgStat_Counter archived_count;	/* archival successes */
	char		last_archived_wal[MAX_XFN_CHARS + 1];	/* last WAL file
														 * archived */
	TimestampTz last_archived_timestamp;	/* last archival success time */
	PgStat_Counter failed_count;	/* failed archival attempts */
	char		last_failed_wal[MAX_XFN_CHARS + 1]; /* WAL file involved in
													 * last failure */
	TimestampTz last_failed_timestamp;	/* last archival failure time */
	TimestampTz stat_reset_timestamp;
} PgStat_ArchiverStats;

typedef struct PgStat_BgWriterStats
{
	PgStat_Counter buf_written_clean;
	PgStat_Counter maxwritten_clean;
	PgStat_Counter buf_alloc;
	TimestampTz stat_reset_timestamp;
} PgStat_BgWriterStats;

typedef struct PgStat_CheckpointerStats
{
<<<<<<< HEAD
	B_AUTOVAC_LAUNCHER,
	B_AUTOVAC_WORKER,
	B_BACKEND,
	B_BG_WORKER,
	B_BG_WRITER,
	B_CHECKPOINTER,
	B_STARTUP,
	B_WAL_RECEIVER,
	B_WAL_SENDER,
	B_WAL_WRITER,
	B_CONSENSUS,
	B_POLAR_WAL_PIPELINER,
	B_BG_LOGINDEX,
	B_BG_FLOG_INSERTER,
	B_BG_FLOG_WRITER,
	B_POLAR_DISPATCHER,
} BackendType;


/* ----------
 * Backend states
 * ----------
 */
typedef enum BackendState
{
	STATE_UNDEFINED,
	STATE_IDLE,
	STATE_RUNNING,
	STATE_IDLEINTRANSACTION,
	STATE_FASTPATH,
	STATE_IDLEINTRANSACTION_ABORTED,
	STATE_DISABLED
} BackendState;


/* ----------
 * Wait Classes
 * ----------
 */
#define PG_WAIT_LWLOCK				0x01000000U
#define PG_WAIT_LOCK				0x03000000U
#define PG_WAIT_BUFFER_PIN			0x04000000U
#define PG_WAIT_ACTIVITY			0x05000000U
#define PG_WAIT_CLIENT				0x06000000U
#define PG_WAIT_EXTENSION			0x07000000U
#define PG_WAIT_IPC					0x08000000U
#define PG_WAIT_TIMEOUT				0x09000000U
#define PG_WAIT_IO					0x0A000000U

/* ----------
 * Wait Events - Activity
 *
 * Use this category when a process is waiting because it has no work to do,
 * unless the "Client" or "Timeout" category describes the situation better.
 * Typically, this should only be used for background processes.
 * ----------
 */
typedef enum
{
	WAIT_EVENT_ARCHIVER_MAIN = PG_WAIT_ACTIVITY,
	WAIT_EVENT_AUTOVACUUM_MAIN,
	WAIT_EVENT_BGWRITER_HIBERNATE,
	WAIT_EVENT_BGWRITER_MAIN,
	WAIT_EVENT_CHECKPOINTER_MAIN,
	WAIT_EVENT_LOGICAL_LAUNCHER_MAIN,
	WAIT_EVENT_LOGICAL_APPLY_MAIN,
	WAIT_EVENT_PGSTAT_MAIN,
	WAIT_EVENT_RECOVERY_WAL_ALL,
	WAIT_EVENT_RECOVERY_WAL_STREAM,
	WAIT_EVENT_SYSLOGGER_MAIN,
	WAIT_EVENT_WAL_RECEIVER_MAIN,
	WAIT_EVENT_WAL_SENDER_MAIN,
	WAIT_EVENT_WAL_WRITER_MAIN,
	/* POLAR */
	WAIT_EVENT_DELAY_DML,
	WAIT_EVENT_ASYNC_REDO_MAIN,
	WAIT_EVENT_ASYNC_DDL_LOCK_REPLAY_MAIN,
	WAIT_EVENT_CONSENSUS_MAIN,
	WAIT_EVENT_DATAMAX_MAIN,
	WAIT_EVENT_LOGINDEX_BG_MAIN,
	WAIT_EVENT_POLAR_SUB_TASK_MAIN,
	WAIT_EVENT_FLOG_WRITE_BG_MAIN,
	WAIT_EVENT_FLOG_INSERT_BG_MAIN
	/* POLAR end */
} WaitEventActivity;

/* ----------
 * Wait Events - Client
 *
 * Use this category when a process is waiting to send data to or receive data
 * from the frontend process to which it is connected.  This is never used for
 * a background process, which has no client connection.
 * ----------
 */
typedef enum
{
	WAIT_EVENT_CLIENT_READ = PG_WAIT_CLIENT,
	WAIT_EVENT_CLIENT_WRITE,
	WAIT_EVENT_LIBPQWALRECEIVER_CONNECT,
	WAIT_EVENT_LIBPQWALRECEIVER_RECEIVE,
	WAIT_EVENT_SSL_OPEN_SERVER,
	WAIT_EVENT_WAL_RECEIVER_WAIT_START,
	WAIT_EVENT_WAL_SENDER_WAIT_WAL,
	WAIT_EVENT_WAL_SENDER_WRITE_DATA,

	/* POLAR px */
	WAIT_EVENT_PX_MOTION_WAIT_RECV
	/* POLAR end */
} WaitEventClient;

/* ----------
 * Wait Events - IPC
 *
 * Use this category when a process cannot complete the work it is doing because
 * it is waiting for a notification from another process.
 * ----------
 */
typedef enum
{
	WAIT_EVENT_BGWORKER_SHUTDOWN = PG_WAIT_IPC,
	WAIT_EVENT_BGWORKER_STARTUP,
	WAIT_EVENT_BTREE_PAGE,
	WAIT_EVENT_EXECUTE_GATHER,
	WAIT_EVENT_HASH_BATCH_ALLOCATING,
	WAIT_EVENT_HASH_BATCH_ELECTING,
	WAIT_EVENT_HASH_BATCH_LOADING,
	WAIT_EVENT_HASH_BUILD_ALLOCATING,
	WAIT_EVENT_HASH_BUILD_ELECTING,
	WAIT_EVENT_HASH_BUILD_HASHING_INNER,
	WAIT_EVENT_HASH_BUILD_HASHING_OUTER,
	WAIT_EVENT_HASH_GROW_BATCHES_ELECTING,
	WAIT_EVENT_HASH_GROW_BATCHES_FINISHING,
	WAIT_EVENT_HASH_GROW_BATCHES_REPARTITIONING,
	WAIT_EVENT_HASH_GROW_BATCHES_ALLOCATING,
	WAIT_EVENT_HASH_GROW_BATCHES_DECIDING,
	WAIT_EVENT_HASH_GROW_BUCKETS_ELECTING,
	WAIT_EVENT_HASH_GROW_BUCKETS_REINSERTING,
	WAIT_EVENT_HASH_GROW_BUCKETS_ALLOCATING,
	WAIT_EVENT_LOGICAL_SYNC_DATA,
	WAIT_EVENT_LOGICAL_SYNC_STATE_CHANGE,
	WAIT_EVENT_MQ_INTERNAL,
	WAIT_EVENT_MQ_PUT_MESSAGE,
	WAIT_EVENT_MQ_RECEIVE,
	WAIT_EVENT_MQ_SEND,
	WAIT_EVENT_PARALLEL_FINISH,
	WAIT_EVENT_PARALLEL_BITMAP_SCAN,
	WAIT_EVENT_PARALLEL_CREATE_INDEX_SCAN,
	WAIT_EVENT_PROCARRAY_GROUP_UPDATE,
	WAIT_EVENT_CLOG_GROUP_UPDATE,
	WAIT_EVENT_REPLICATION_ORIGIN_DROP,
	WAIT_EVENT_REPLICATION_SLOT_DROP,
	WAIT_EVENT_SAFE_SNAPSHOT,
	WAIT_EVENT_SMGR_DROP_SYNC,
	WAIT_EVENT_SYNC_REP,
	WAIT_EVENT_CONSENSUS_COMMIT,
	WAIT_EVENT_CONSENSUS,
	/* POLAR wal pipeline */
	WAIT_EVENT_WAL_PIPELINE_WAIT_RECENT_WRITTEN_SPACE,
	WAIT_EVENT_WAL_PIPELINE_WAIT_UNFLUSHED_XLOG_SLOT
} WaitEventIPC;

/* ----------
 * Wait Events - Timeout
 *
 * Use this category when a process is waiting for a timeout to expire.
 * ----------
 */
typedef enum
{
	WAIT_EVENT_BASE_BACKUP_THROTTLE = PG_WAIT_TIMEOUT,
	WAIT_EVENT_PG_SLEEP,
	WAIT_EVENT_RECOVERY_APPLY_DELAY
} WaitEventTimeout;

/* ----------
 * Wait Events - IO
 *
 * Use this category when a process is waiting for a IO.
 * ----------
 */
typedef enum
{
	WAIT_EVENT_BUFFILE_READ = PG_WAIT_IO,
	WAIT_EVENT_BUFFILE_WRITE,
	WAIT_EVENT_CONTROL_FILE_READ,
	WAIT_EVENT_CONTROL_FILE_SYNC,
	WAIT_EVENT_CONTROL_FILE_SYNC_UPDATE,
	WAIT_EVENT_CONTROL_FILE_WRITE,
	WAIT_EVENT_CONTROL_FILE_WRITE_UPDATE,
	WAIT_EVENT_COPY_FILE_READ,
	WAIT_EVENT_COPY_FILE_WRITE,
	WAIT_EVENT_DATA_FILE_EXTEND,
	WAIT_EVENT_DATA_FILE_FLUSH,
	WAIT_EVENT_DATA_FILE_IMMEDIATE_SYNC,
	WAIT_EVENT_DATA_FILE_PREFETCH,
	WAIT_EVENT_DATA_FILE_READ,
	WAIT_EVENT_DATA_FILE_SYNC,
	WAIT_EVENT_DATA_FILE_TRUNCATE,
	WAIT_EVENT_DATA_FILE_WRITE,
	WAIT_EVENT_DSM_FILL_ZERO_WRITE,
	WAIT_EVENT_KMGR_FILE_READ,
	WAIT_EVENT_KMGR_FILE_SYNC,
	WAIT_EVENT_KMGR_FILE_WRITE,
	WAIT_EVENT_LOCK_FILE_ADDTODATADIR_READ,
	WAIT_EVENT_LOCK_FILE_ADDTODATADIR_SYNC,
	WAIT_EVENT_LOCK_FILE_ADDTODATADIR_WRITE,
	WAIT_EVENT_LOCK_FILE_CREATE_READ,
	WAIT_EVENT_LOCK_FILE_CREATE_SYNC,
	WAIT_EVENT_LOCK_FILE_CREATE_WRITE,
	WAIT_EVENT_LOCK_FILE_RECHECKDATADIR_READ,
	WAIT_EVENT_LOGICAL_REWRITE_CHECKPOINT_SYNC,
	WAIT_EVENT_LOGICAL_REWRITE_MAPPING_SYNC,
	WAIT_EVENT_LOGICAL_REWRITE_MAPPING_WRITE,
	WAIT_EVENT_LOGICAL_REWRITE_SYNC,
	WAIT_EVENT_LOGICAL_REWRITE_TRUNCATE,
	WAIT_EVENT_LOGICAL_REWRITE_WRITE,
	WAIT_EVENT_RELATION_MAP_READ,
	WAIT_EVENT_RELATION_MAP_SYNC,
	WAIT_EVENT_RELATION_MAP_WRITE,
	WAIT_EVENT_REORDER_BUFFER_READ,
	WAIT_EVENT_REORDER_BUFFER_WRITE,
	WAIT_EVENT_REORDER_LOGICAL_MAPPING_READ,
	WAIT_EVENT_REPLICATION_SLOT_READ,
	WAIT_EVENT_REPLICATION_SLOT_RESTORE_SYNC,
	WAIT_EVENT_REPLICATION_SLOT_SYNC,
	WAIT_EVENT_REPLICATION_SLOT_WRITE,
	WAIT_EVENT_SLRU_FLUSH_SYNC,
	WAIT_EVENT_SLRU_READ,
	WAIT_EVENT_SLRU_SYNC,
	WAIT_EVENT_SLRU_WRITE,
	WAIT_EVENT_SNAPBUILD_READ,
	WAIT_EVENT_SNAPBUILD_SYNC,
	WAIT_EVENT_SNAPBUILD_WRITE,
	WAIT_EVENT_TIMELINE_HISTORY_FILE_SYNC,
	WAIT_EVENT_TIMELINE_HISTORY_FILE_WRITE,
	WAIT_EVENT_TIMELINE_HISTORY_READ,
	WAIT_EVENT_TIMELINE_HISTORY_SYNC,
	WAIT_EVENT_TIMELINE_HISTORY_WRITE,
	WAIT_EVENT_TWOPHASE_FILE_READ,
	WAIT_EVENT_TWOPHASE_FILE_SYNC,
	WAIT_EVENT_TWOPHASE_FILE_WRITE,
	WAIT_EVENT_WALSENDER_TIMELINE_HISTORY_READ,
	WAIT_EVENT_WAL_BOOTSTRAP_SYNC,
	WAIT_EVENT_WAL_BOOTSTRAP_WRITE,
	WAIT_EVENT_WAL_COPY_READ,
	WAIT_EVENT_WAL_COPY_SYNC,
	WAIT_EVENT_WAL_COPY_WRITE,
	WAIT_EVENT_WAL_INIT_SYNC,
	WAIT_EVENT_WAL_INIT_WRITE,
	WAIT_EVENT_WAL_READ,
	WAIT_EVENT_WAL_SYNC_METHOD_ASSIGN,
	WAIT_EVENT_WAL_WRITE,
	WAIT_EVENT_WAL_PREAD,
	WAIT_EVENT_WAL_PWRITE,
	WAIT_EVENT_DATA_FILE_PWRITE_EXTEND,
	WAIT_EVENT_DATA_FILE_PWRITE,
	WAIT_EVENT_DATA_FILE_PREAD,
	WAIT_EVENT_DATA_FILE_BATCH_PWRITE_EXTEND,
	WAIT_EVENT_DATA_VFS_FILE_OPEN,
	WAIT_EVENT_DATA_VFS_FILE_LSEEK,
	/* POLAR: Wait Events - logindex */
	WAIT_EVENT_LOGINDEX_META_READ,
	WAIT_EVENT_LOGINDEX_META_WRITE,
	WAIT_EVENT_LOGINDEX_META_FLUSH,
	WAIT_EVENT_LOGINDEX_TBL_READ,
	WAIT_EVENT_LOGINDEX_TBL_WRITE,
	WAIT_EVENT_LOGINDEX_WAIT_ACTIVE,
	WAIT_EVENT_LOGINDEX_TBL_FLUSH,
	WAIT_EVENT_LOGINDEX_QUEUE_SPACE,
	WAIT_EVENT_REL_SIZE_CACHE_WRITE,
	WAIT_EVENT_REL_SIZE_CACHE_READ,
	WAIT_EVENT_FULLPAGE_FILE_INIT_WRITE,
	WAIT_EVENT_LOGINDEX_WAIT_FULLPAGE,
	/* POLAR: Wait Events - syslog pipe write */
	WAIT_EVENT_SYSLOG_PIPE_WRITE,
	WAIT_EVENT_SYSLOG_CHANNEL_WRITE,
	/* POLAR: Wait Events - local cache */
	WAIT_EVENT_CACHE_LOCAL_OPEN,
	WAIT_EVENT_CACHE_LOCAL_READ,
	WAIT_EVENT_CACHE_LOCAL_WRITE,
	WAIT_EVENT_CACHE_LOCAL_LSEEK,
	WAIT_EVENT_CACHE_LOCAL_UNLINK,
	WAIT_EVENT_CACHE_LOCAL_SYNC,
	WAIT_EVENT_CACHE_LOCAL_STAT,
	WAIT_EVENT_CACHE_SHARED_OPEN,
	WAIT_EVENT_CACHE_SHARED_READ,
	WAIT_EVENT_CACHE_SHARED_WRITE,
	WAIT_EVENT_CACHE_SHARED_LSEEK,
	WAIT_EVENT_CACHE_SHARED_UNLINK,
	WAIT_EVENT_CACHE_SHARED_SYNC,
	WAIT_EVENT_CACHE_SHARED_STAT,
	/* POLAR: Wait Events - datamax */
	WAIT_EVENT_DATAMAX_META_READ,
	WAIT_EVENT_DATAMAX_META_WRITE,
	/* POLAR: Wait Events - flashback log */
	WAIT_EVENT_FLASHBACK_LOG_CTL_FILE_WRITE,
	WAIT_EVENT_FLASHBACK_LOG_CTL_FILE_READ,
	WAIT_EVENT_FLASHBACK_LOG_CTL_FILE_SYNC,
	WAIT_EVENT_FLASHBACK_LOG_INIT_WRITE,
	WAIT_EVENT_FLASHBACK_LOG_INIT_SYNC,
	WAIT_EVENT_FLASHBACK_LOG_WRITE,
	WAIT_EVENT_FLASHBACK_LOG_READ,
	WAIT_EVENT_FLASHBACK_LOG_HISTORY_FILE_WRITE,
	WAIT_EVENT_FLASHBACK_LOG_HISTORY_FILE_READ,
	WAIT_EVENT_FLASHBACK_LOG_HISTORY_FILE_SYNC,
	WAIT_EVENT_FLASHBACK_LOG_BUF_READY,
	WAIT_EVENT_FLASHBACK_LOG_INSERT,
	WAIT_EVENT_FLASHBACK_POINT_FILE_WRITE,
	WAIT_EVENT_FLASHBACK_POINT_FILE_READ,
	WAIT_EVENT_FLASHBACK_POINT_FILE_SYNC,
	WAIT_EVENT_FRA_CTL_FILE_READ,
	WAIT_EVENT_FRA_CTL_FILE_WRITE,
	WAIT_EVENT_FRA_CTL_FILE_SYNC,
	/* POLAR end */
	/* POLAR wal pipeline */
	WAIT_EVENT_WAL_PIPELINE_COMMIT_WAIT
} WaitEventIO;

/* ----------
 * Command type for progress reporting purposes
 * ----------
 */
typedef enum ProgressCommandType
{
	PROGRESS_COMMAND_INVALID,
	PROGRESS_COMMAND_VACUUM
} ProgressCommandType;

#define PGSTAT_NUM_PROGRESS_PARAM	10

/* ----------
 * Shared-memory data structures
 * ----------
 */
=======
	PgStat_Counter num_timed;
	PgStat_Counter num_requested;
	PgStat_Counter restartpoints_timed;
	PgStat_Counter restartpoints_requested;
	PgStat_Counter restartpoints_performed;
	PgStat_Counter write_time;	/* times in milliseconds */
	PgStat_Counter sync_time;
	PgStat_Counter buffers_written;
	TimestampTz stat_reset_timestamp;
} PgStat_CheckpointerStats;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c


/*
 * Types related to counting IO operations
 */
typedef enum IOObject
{
	IOOBJECT_RELATION,
	IOOBJECT_TEMP_RELATION,
} IOObject;

#define IOOBJECT_NUM_TYPES (IOOBJECT_TEMP_RELATION + 1)

typedef enum IOContext
{
<<<<<<< HEAD
	/*
	 * To avoid locking overhead, we use the following protocol: a backend
	 * increments st_changecount before modifying its entry, and again after
	 * finishing a modification.  A would-be reader should note the value of
	 * st_changecount, copy the entry into private memory, then check
	 * st_changecount again.  If the value hasn't changed, and if it's even,
	 * the copy is valid; otherwise start over.  This makes updates cheap
	 * while reads are potentially expensive, but that's the tradeoff we want.
	 *
	 * The above protocol needs memory barriers to ensure that the apparent
	 * order of execution is as it desires.  Otherwise, for example, the CPU
	 * might rearrange the code so that st_changecount is incremented twice
	 * before the modification on a machine with weak memory ordering.  Hence,
	 * use the macros defined below for manipulating st_changecount, rather
	 * than touching it directly.
	 */
	int			st_changecount;
=======
	IOCONTEXT_BULKREAD,
	IOCONTEXT_BULKWRITE,
	IOCONTEXT_NORMAL,
	IOCONTEXT_VACUUM,
} IOContext;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

#define IOCONTEXT_NUM_TYPES (IOCONTEXT_VACUUM + 1)

typedef enum IOOp
{
	IOOP_EVICT,
	IOOP_EXTEND,
	IOOP_FSYNC,
	IOOP_HIT,
	IOOP_READ,
	IOOP_REUSE,
	IOOP_WRITE,
	IOOP_WRITEBACK,
} IOOp;

#define IOOP_NUM_TYPES (IOOP_WRITEBACK + 1)

typedef struct PgStat_BktypeIO
{
	PgStat_Counter counts[IOOBJECT_NUM_TYPES][IOCONTEXT_NUM_TYPES][IOOP_NUM_TYPES];
	PgStat_Counter times[IOOBJECT_NUM_TYPES][IOCONTEXT_NUM_TYPES][IOOP_NUM_TYPES];
} PgStat_BktypeIO;

typedef struct PgStat_IO
{
	TimestampTz stat_reset_timestamp;
	PgStat_BktypeIO stats[BACKEND_NUM_TYPES];
} PgStat_IO;


typedef struct PgStat_StatDBEntry
{
	PgStat_Counter xact_commit;
	PgStat_Counter xact_rollback;
	PgStat_Counter blocks_fetched;
	PgStat_Counter blocks_hit;
	PgStat_Counter tuples_returned;
	PgStat_Counter tuples_fetched;
	PgStat_Counter tuples_inserted;
	PgStat_Counter tuples_updated;
	PgStat_Counter tuples_deleted;
	TimestampTz last_autovac_time;
	PgStat_Counter conflict_tablespace;
	PgStat_Counter conflict_lock;
	PgStat_Counter conflict_snapshot;
	PgStat_Counter conflict_logicalslot;
	PgStat_Counter conflict_bufferpin;
	PgStat_Counter conflict_startup_deadlock;
	PgStat_Counter temp_files;
	PgStat_Counter temp_bytes;
	PgStat_Counter deadlocks;
	PgStat_Counter checksum_failures;
	TimestampTz last_checksum_failure;
	PgStat_Counter blk_read_time;	/* times in microseconds */
	PgStat_Counter blk_write_time;
	PgStat_Counter sessions;
	PgStat_Counter session_time;
	PgStat_Counter active_time;
	PgStat_Counter idle_in_transaction_time;
	PgStat_Counter sessions_abandoned;
	PgStat_Counter sessions_fatal;
	PgStat_Counter sessions_killed;

	TimestampTz stat_reset_timestamp;
} PgStat_StatDBEntry;

<<<<<<< HEAD
	/*
	 * Command progress reporting.  Any command which wishes can advertise
	 * that it is running by setting st_progress_command,
	 * st_progress_command_target, and st_progress_param[].
	 * st_progress_command_target should be the OID of the relation which the
	 * command targets (we assume there's just one, as this is meant for
	 * utility commands), but the meaning of each element in the
	 * st_progress_param array is command-specific.
	 */
	ProgressCommandType st_progress_command;
	Oid			st_progress_command_target;
	int64		st_progress_param[PGSTAT_NUM_PROGRESS_PARAM];

	/* POLAR: Origin host/port while proxy exists in front of PG, otherwise they are NULL. */
	bool 		polar_st_proxy;
	SockAddr 	polar_st_origin_addr;

	bool		polar_proxy_ssl_in_use;
	char		polar_proxy_ssl_cipher_name[NAMEDATALEN];
	char		polar_proxy_ssl_version[NAMEDATALEN];

	int32		polar_st_cancel_key;
	int			polar_st_virtual_pid;

	/* POLAR end */

	/* POLAR: this backend index in backend array */
	int 		backendid;
	/* POLAR end */

	/* POLAR: queryid of  the current backend */
	int64 		queryid;

	/* POLAR: Shared Server */
	int32 		dispatcher_pid;
	int32 		session_local_id;
	int32 		last_backend_pid;
	int32 		saved_guc_count;
	TimestampTz last_wait_start_timestamp;
	/* POLAR end */
} PgBackendStatus;

/*
 * Macros to load and store st_changecount with appropriate memory barriers.
 *
 * Use PGSTAT_BEGIN_WRITE_ACTIVITY() before, and PGSTAT_END_WRITE_ACTIVITY()
 * after, modifying the current process's PgBackendStatus data.  Note that,
 * since there is no mechanism for cleaning up st_changecount after an error,
 * THESE MACROS FORM A CRITICAL SECTION.  Any error between them will be
 * promoted to PANIC, causing a database restart to clean up shared memory!
 * Hence, keep the critical section as short and straight-line as possible.
 * Aside from being safer, that minimizes the window in which readers will
 * have to loop.
 *
 * Reader logic should follow this sketch:
 *
 *		for (;;)
 *		{
 *			int before_ct, after_ct;
 *
 *			pgstat_begin_read_activity(beentry, before_ct);
 *			... copy beentry data to local memory ...
 *			pgstat_end_read_activity(beentry, after_ct);
 *			if (pgstat_read_activity_complete(before_ct, after_ct))
 *				break;
 *			CHECK_FOR_INTERRUPTS();
 *		}
 *
 * For extra safety, we generally use volatile beentry pointers, although
 * the memory barriers should theoretically be sufficient.
 */
#define PGSTAT_BEGIN_WRITE_ACTIVITY(beentry) \
	do { \
		START_CRIT_SECTION(); \
		(beentry)->st_changecount++; \
		pg_write_barrier(); \
	} while (0)

#define PGSTAT_END_WRITE_ACTIVITY(beentry) \
	do { \
		pg_write_barrier(); \
		(beentry)->st_changecount++; \
		Assert(((beentry)->st_changecount & 1) == 0); \
		END_CRIT_SECTION(); \
	} while (0)

#define pgstat_begin_read_activity(beentry, before_changecount) \
	do { \
		(before_changecount) = (beentry)->st_changecount; \
		pg_read_barrier(); \
	} while (0)

#define pgstat_end_read_activity(beentry, after_changecount) \
	do { \
		pg_read_barrier(); \
		(after_changecount) = (beentry)->st_changecount; \
	} while (0)

#define pgstat_read_activity_complete(before_changecount, after_changecount) \
	((before_changecount) == (after_changecount) && \
	 ((before_changecount) & 1) == 0)

/* deprecated names for above macros; these are gone in v12 */
#define pgstat_increment_changecount_before(beentry) \
	PGSTAT_BEGIN_WRITE_ACTIVITY(beentry)
#define pgstat_increment_changecount_after(beentry) \
	PGSTAT_END_WRITE_ACTIVITY(beentry)
#define pgstat_save_changecount_before(beentry, save_changecount) \
	pgstat_begin_read_activity(beentry, save_changecount)
#define pgstat_save_changecount_after(beentry, save_changecount) \
	pgstat_end_read_activity(beentry, save_changecount)


/* ----------
 * LocalPgBackendStatus
 *
 * When we build the backend status array, we use LocalPgBackendStatus to be
 * able to add new values to the struct when needed without adding new fields
 * to the shared memory. It contains the backend status as a first member.
 * ----------
 */
typedef struct LocalPgBackendStatus
=======
typedef struct PgStat_StatFuncEntry
{
	PgStat_Counter numcalls;

	PgStat_Counter total_time;	/* times in microseconds */
	PgStat_Counter self_time;
} PgStat_StatFuncEntry;

typedef struct PgStat_StatReplSlotEntry
{
	PgStat_Counter spill_txns;
	PgStat_Counter spill_count;
	PgStat_Counter spill_bytes;
	PgStat_Counter stream_txns;
	PgStat_Counter stream_count;
	PgStat_Counter stream_bytes;
	PgStat_Counter total_txns;
	PgStat_Counter total_bytes;
	TimestampTz stat_reset_timestamp;
} PgStat_StatReplSlotEntry;

typedef struct PgStat_SLRUStats
{
	PgStat_Counter blocks_zeroed;
	PgStat_Counter blocks_hit;
	PgStat_Counter blocks_read;
	PgStat_Counter blocks_written;
	PgStat_Counter blocks_exists;
	PgStat_Counter flush;
	PgStat_Counter truncate;
	TimestampTz stat_reset_timestamp;
} PgStat_SLRUStats;

typedef struct PgStat_StatSubEntry
{
	PgStat_Counter apply_error_count;
	PgStat_Counter sync_error_count;
	PgStat_Counter conflict_count[CONFLICT_NUM_TYPES];
	TimestampTz stat_reset_timestamp;
} PgStat_StatSubEntry;

typedef struct PgStat_StatTabEntry
{
	PgStat_Counter numscans;
	TimestampTz lastscan;

	PgStat_Counter tuples_returned;
	PgStat_Counter tuples_fetched;

	PgStat_Counter tuples_inserted;
	PgStat_Counter tuples_updated;
	PgStat_Counter tuples_deleted;
	PgStat_Counter tuples_hot_updated;
	PgStat_Counter tuples_newpage_updated;

	PgStat_Counter live_tuples;
	PgStat_Counter dead_tuples;
	PgStat_Counter mod_since_analyze;
	PgStat_Counter ins_since_vacuum;

	PgStat_Counter blocks_fetched;
	PgStat_Counter blocks_hit;

	TimestampTz last_vacuum_time;	/* user initiated vacuum */
	PgStat_Counter vacuum_count;
	TimestampTz last_autovacuum_time;	/* autovacuum initiated */
	PgStat_Counter autovacuum_count;
	TimestampTz last_analyze_time;	/* user initiated */
	PgStat_Counter analyze_count;
	TimestampTz last_autoanalyze_time;	/* autovacuum initiated */
	PgStat_Counter autoanalyze_count;
} PgStat_StatTabEntry;

typedef struct PgStat_WalStats
{
	PgStat_Counter wal_records;
	PgStat_Counter wal_fpi;
	uint64		wal_bytes;
	PgStat_Counter wal_buffers_full;
	PgStat_Counter wal_write;
	PgStat_Counter wal_sync;
	PgStat_Counter wal_write_time;
	PgStat_Counter wal_sync_time;
	TimestampTz stat_reset_timestamp;
} PgStat_WalStats;

/*
 * This struct stores wal-related durations as instr_time, which makes it
 * cheaper and easier to accumulate them, by not requiring type
 * conversions. During stats flush instr_time will be converted into
 * microseconds.
 */
typedef struct PgStat_PendingWalStats
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
{
	PgStat_Counter wal_buffers_full;
	PgStat_Counter wal_write;
	PgStat_Counter wal_sync;
	instr_time	wal_write_time;
	instr_time	wal_sync_time;
} PgStat_PendingWalStats;


/*
 * Functions in pgstat.c
 */

<<<<<<< HEAD
/* ----------
 * PolarStat_Proxy
 *
 * Stats for proxy, including reason and count.
 * ----------
 */
/* Unsplittable reasons, none means splittable */
typedef enum polar_unsplittable_reason_t
{
	POLAR_UNSPLITTABLE_FOR_NONE,
	POLAR_UNSPLITTABLE_FOR_ERROR,
	POLAR_UNSPLITTABLE_FOR_LOCK,
	POLAR_UNSPLITTABLE_FOR_COMBOCID,
	POLAR_UNSPLITTABLE_FOR_AUTOXACT,
} polar_unsplittable_reason_t;

/* Global stats for proxy, not atomic variables but that's fine */
typedef struct PolarStat_Proxy
{
	uint64	proxy_total;  			/* Total count of SQL */
	uint64	proxy_splittable;		/* Total count of splittable SQL */
	uint64	proxy_disablesplit;		/* Total count of disablesplit SQL */
	uint64	proxy_unsplittable;		/* Total count of unsplittable SQL */
	uint64	proxy_error;			/* Total count of unsplittable for error SQL */
	uint64	proxy_lock;				/* Total count of unsplittable for lock SQL */
	uint64	proxy_combocid;			/* Total count of unsplittable for combocid SQL */
	uint64	proxy_autoxact;			/* Total count of unsplittable for autoxact SQL */
	uint64	proxy_implicit;			/* Total count of proxy implicit xact SQL */
	uint64	proxy_readbeforewrite;	/* Total count of proxy readbeforewrite SQL */
	uint64	proxy_readafterwrite;	/* Total count of proxy readafterwrite SQL */
} PolarStat_Proxy;


/* ----------
 * GUC parameters
 * ----------
 */
extern PGDLLIMPORT bool pgstat_track_activities;
extern PGDLLIMPORT bool pgstat_track_counts;
extern PGDLLIMPORT int pgstat_track_functions;
extern PGDLLIMPORT int pgstat_track_activity_query_size;
extern char *pgstat_stat_directory;
extern char *pgstat_stat_tmpname;
extern char *pgstat_stat_filename;
=======
/* functions called from postmaster */
extern Size StatsShmemSize(void);
extern void StatsShmemInit(void);

/* Functions called during server startup / shutdown */
extern void pgstat_restore_stats(XLogRecPtr redo);
extern void pgstat_discard_stats(void);
extern void pgstat_before_server_shutdown(int code, Datum arg);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/* Functions for backend initialization */
extern void pgstat_initialize(void);

/* Functions called from backends */
extern long pgstat_report_stat(bool force);
extern void pgstat_force_next_flush(void);

extern void pgstat_reset_counters(void);
extern void pgstat_reset(PgStat_Kind kind, Oid dboid, uint64 objid);
extern void pgstat_reset_of_kind(PgStat_Kind kind);

/* stats accessors */
extern void pgstat_clear_snapshot(void);
extern TimestampTz pgstat_get_stat_snapshot_timestamp(bool *have_snapshot);

/* helpers */
extern PgStat_Kind pgstat_get_kind_from_str(char *kind_str);
extern bool pgstat_have_entry(PgStat_Kind kind, Oid dboid, uint64 objid);


/*
 * Functions in pgstat_archiver.c
 */

extern void pgstat_report_archiver(const char *xlog, bool failed);
extern PgStat_ArchiverStats *pgstat_fetch_stat_archiver(void);


/*
 * Functions in pgstat_bgwriter.c
 */

extern void pgstat_report_bgwriter(void);
extern PgStat_BgWriterStats *pgstat_fetch_stat_bgwriter(void);


/*
 * Functions in pgstat_checkpointer.c
 */

extern void pgstat_report_checkpointer(void);
extern PgStat_CheckpointerStats *pgstat_fetch_stat_checkpointer(void);


/*
 * Functions in pgstat_io.c
 */

extern bool pgstat_bktype_io_stats_valid(PgStat_BktypeIO *backend_io,
										 BackendType bktype);
extern void pgstat_count_io_op(IOObject io_object, IOContext io_context, IOOp io_op);
extern void pgstat_count_io_op_n(IOObject io_object, IOContext io_context, IOOp io_op, uint32 cnt);
extern instr_time pgstat_prepare_io_time(bool track_io_guc);
extern void pgstat_count_io_op_time(IOObject io_object, IOContext io_context,
									IOOp io_op, instr_time start_time, uint32 cnt);

extern PgStat_IO *pgstat_fetch_stat_io(void);
extern const char *pgstat_get_io_context_name(IOContext io_context);
extern const char *pgstat_get_io_object_name(IOObject io_object);

extern bool pgstat_tracks_io_bktype(BackendType bktype);
extern bool pgstat_tracks_io_object(BackendType bktype,
									IOObject io_object, IOContext io_context);
extern bool pgstat_tracks_io_op(BackendType bktype, IOObject io_object,
								IOContext io_context, IOOp io_op);


/*
 * Functions in pgstat_database.c
 */

extern void pgstat_drop_database(Oid databaseid);
extern void pgstat_report_autovac(Oid dboid);
extern void pgstat_report_recovery_conflict(int reason);
extern void pgstat_report_deadlock(void);
extern void pgstat_report_checksum_failures_in_db(Oid dboid, int failurecount);
extern void pgstat_report_checksum_failure(void);
extern void pgstat_report_connect(Oid dboid);

#define pgstat_count_buffer_read_time(n)							\
	(pgStatBlockReadTime += (n))
#define pgstat_count_buffer_write_time(n)							\
	(pgStatBlockWriteTime += (n))
#define pgstat_count_conn_active_time(n)							\
	(pgStatActiveTime += (n))
#define pgstat_count_conn_txn_idle_time(n)							\
	(pgStatTransactionIdleTime += (n))

extern PgStat_StatDBEntry *pgstat_fetch_stat_dbentry(Oid dboid);


/*
 * Functions in pgstat_function.c
 */

extern void pgstat_create_function(Oid proid);
extern void pgstat_drop_function(Oid proid);

struct FunctionCallInfoBaseData;
extern void pgstat_init_function_usage(struct FunctionCallInfoBaseData *fcinfo,
									   PgStat_FunctionCallUsage *fcu);
extern void pgstat_end_function_usage(PgStat_FunctionCallUsage *fcu,
									  bool finalize);

<<<<<<< HEAD
/* 
 * POLAR: stat wait_object and wait_time start
 */
static inline void
polar_stat_wait_obj_and_time_set(int id, const instr_time *start_time, const int8 type)
{
	if (!polar_enable_stat_wait_info)
		return;

	if (!pgstat_track_activities || !MyProc)
		return;

	MyProc->cur_wait_stack_index++;
	/* push stack if not overflow */
	if (MyProc->cur_wait_stack_index > -1 && MyProc->cur_wait_stack_index < PGPROC_WAIT_STACK_LEN)
	{
		/*
	 	 * POLAR:  We do not use pid 0 because it belongs to the root process. 
	 	 * We think -1 is an invalid pid.
	 	 */
		if (type == PGPROC_WAIT_PID && id == 0)
			id = PGPROC_INVAILD_WAIT_OBJ;
		MyProc->wait_object[MyProc->cur_wait_stack_index] = id;
		MyProc->wait_type[MyProc->cur_wait_stack_index] = type;
		INSTR_TIME_SET_ZERO(MyProc->wait_time[MyProc->cur_wait_stack_index]);
		if (!INSTR_TIME_IS_ZERO(*start_time))
		{
			INSTR_TIME_ADD(MyProc->wait_time[MyProc->cur_wait_stack_index], *start_time);
		}
	}
}
static inline void
polar_stat_wait_obj_and_time_clear(void)
{
	if (!polar_enable_stat_wait_info)
		return;

	if (!pgstat_track_activities || !MyProc)
		return;

	/* pop stack if not overflow*/
	if (MyProc->cur_wait_stack_index > -1 && MyProc->cur_wait_stack_index < PGPROC_WAIT_STACK_LEN)
	{
		MyProc->wait_object[MyProc->cur_wait_stack_index] = PGPROC_INVAILD_WAIT_OBJ;
		MyProc->wait_type[MyProc->cur_wait_stack_index] = PGPROC_INVAILD_WAIT_OBJ;
		INSTR_TIME_SET_ZERO(MyProc->wait_time[MyProc->cur_wait_stack_index]);
		
	}
	MyProc->cur_wait_stack_index--;
}
/* 
 * POLAR: stat wait_object and wait_time end
 */

/* ----------
 * pgstat_report_wait_end() -
 *
 *	Called to report end of a wait.
 *
 * NB: this *must* be able to survive being called before MyProc has been
 * initialized.
 * ----------
=======
extern PgStat_StatFuncEntry *pgstat_fetch_stat_funcentry(Oid func_id);
extern PgStat_FunctionCounts *find_funcstat_entry(Oid func_id);


/*
 * Functions in pgstat_relation.c
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 */

extern void pgstat_create_relation(Relation rel);
extern void pgstat_drop_relation(Relation rel);
extern void pgstat_copy_relation_stats(Relation dst, Relation src);

extern void pgstat_init_relation(Relation rel);
extern void pgstat_assoc_relation(Relation rel);
extern void pgstat_unlink_relation(Relation rel);

extern void pgstat_report_vacuum(Oid tableoid, bool shared,
								 PgStat_Counter livetuples, PgStat_Counter deadtuples);
extern void pgstat_report_analyze(Relation rel,
								  PgStat_Counter livetuples, PgStat_Counter deadtuples,
								  bool resetcounter);

/*
 * If stats are enabled, but pending data hasn't been prepared yet, call
 * pgstat_assoc_relation() to do so. See its comment for why this is done
 * separately from pgstat_init_relation().
 */
#define pgstat_should_count_relation(rel)                           \
	(likely((rel)->pgstat_info != NULL) ? true :                    \
	 ((rel)->pgstat_enabled ? pgstat_assoc_relation(rel), true : false))

/* nontransactional event counts are simple enough to inline */

#define pgstat_count_heap_scan(rel)									\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->counts.numscans++;					\
	} while (0)
#define pgstat_count_heap_getnext(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->counts.tuples_returned++;			\
	} while (0)
#define pgstat_count_heap_fetch(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->counts.tuples_fetched++;			\
	} while (0)
#define pgstat_count_index_scan(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->counts.numscans++;					\
	} while (0)
#define pgstat_count_index_tuples(rel, n)							\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->counts.tuples_returned += (n);		\
	} while (0)
#define pgstat_count_buffer_read(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->counts.blocks_fetched++;			\
	} while (0)
#define pgstat_count_buffer_hit(rel)								\
	do {															\
		if (pgstat_should_count_relation(rel))						\
			(rel)->pgstat_info->counts.blocks_hit++;				\
	} while (0)

/* POLAR: bulk read stats */
#define polar_pgstat_count_bulk_read_calls(rel)                             \
	do {															        \
		if ((rel)->pgstat_info != NULL)								        \
			(rel)->pgstat_info->t_counts.polar_t_bulk_read_calls++;			\
	} while (0)
#define polar_pgstat_count_bulk_read_calls_IO(rel)                              \
	do {															            \
		if ((rel)->pgstat_info != NULL)								            \
			(rel)->pgstat_info->t_counts.polar_t_bulk_read_calls_IO++;			\
	} while (0)
#define polar_pgstat_count_bulk_read_blocks_IO(rel, n)				        	\
	do {															            \
		if ((rel)->pgstat_info != NULL)								            \
		  (rel)->pgstat_info->t_counts.polar_t_bulk_read_blocks_IO += (n);      \
	} while (0)
/* POLAR: end */

extern void pgstat_count_heap_insert(Relation rel, PgStat_Counter n);
extern void pgstat_count_heap_update(Relation rel, bool hot, bool newpage);
extern void pgstat_count_heap_delete(Relation rel);
extern void pgstat_count_truncate(Relation rel);
extern void pgstat_update_heap_dead_tuples(Relation rel, int delta);

extern void pgstat_twophase_postcommit(TransactionId xid, uint16 info,
									   void *recdata, uint32 len);
extern void pgstat_twophase_postabort(TransactionId xid, uint16 info,
									  void *recdata, uint32 len);

<<<<<<< HEAD
=======
extern PgStat_StatTabEntry *pgstat_fetch_stat_tabentry(Oid relid);
extern PgStat_StatTabEntry *pgstat_fetch_stat_tabentry_ext(bool shared,
														   Oid reloid);
extern PgStat_TableStatus *find_tabstat_entry(Oid rel_id);


/*
 * Functions in pgstat_replslot.c
 */

extern void pgstat_reset_replslot(const char *name);
struct ReplicationSlot;
extern void pgstat_report_replslot(struct ReplicationSlot *slot, const PgStat_StatReplSlotEntry *repSlotStat);
extern void pgstat_create_replslot(struct ReplicationSlot *slot);
extern void pgstat_acquire_replslot(struct ReplicationSlot *slot);
extern void pgstat_drop_replslot(struct ReplicationSlot *slot);
extern PgStat_StatReplSlotEntry *pgstat_fetch_replslot(NameData slotname);


/*
 * Functions in pgstat_slru.c
 */

extern void pgstat_reset_slru(const char *);
extern void pgstat_count_slru_page_zeroed(int slru_idx);
extern void pgstat_count_slru_page_hit(int slru_idx);
extern void pgstat_count_slru_page_read(int slru_idx);
extern void pgstat_count_slru_page_written(int slru_idx);
extern void pgstat_count_slru_page_exists(int slru_idx);
extern void pgstat_count_slru_flush(int slru_idx);
extern void pgstat_count_slru_truncate(int slru_idx);
extern const char *pgstat_get_slru_name(int slru_idx);
extern int	pgstat_get_slru_index(const char *name);
extern PgStat_SLRUStats *pgstat_fetch_slru(void);


/*
 * Functions in pgstat_subscription.c
 */

extern void pgstat_report_subscription_error(Oid subid, bool is_apply_error);
extern void pgstat_report_subscription_conflict(Oid subid, ConflictType type);
extern void pgstat_create_subscription(Oid subid);
extern void pgstat_drop_subscription(Oid subid);
extern PgStat_StatSubEntry *pgstat_fetch_stat_subscription(Oid subid);


/*
 * Functions in pgstat_xact.c
 */

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern void AtEOXact_PgStat(bool isCommit, bool parallel);
extern void AtEOSubXact_PgStat(bool isCommit, int nestDepth);
extern void AtPrepare_PgStat(void);
extern void PostPrepare_PgStat(void);
struct xl_xact_stats_item;
extern int	pgstat_get_transactional_drops(bool isCommit, struct xl_xact_stats_item **items);
extern void pgstat_execute_transactional_drops(int ndrops, struct xl_xact_stats_item *items, bool is_redo);


/*
 * Functions in pgstat_wal.c
 */

extern void pgstat_report_wal(bool force);
extern PgStat_WalStats *pgstat_fetch_stat_wal(void);


/*
 * Variables in pgstat.c
 */

/* GUC parameters */
extern PGDLLIMPORT bool pgstat_track_counts;
extern PGDLLIMPORT int pgstat_track_functions;
extern PGDLLIMPORT int pgstat_fetch_consistency;


/*
 * Variables in pgstat_bgwriter.c
 */

/* updated directly by bgwriter and bufmgr */
extern PGDLLIMPORT PgStat_BgWriterStats PendingBgWriterStats;


/*
 * Variables in pgstat_checkpointer.c
 */

/*
 * Checkpointer statistics counters are updated directly by checkpointer and
 * bufmgr.
 */
extern PGDLLIMPORT PgStat_CheckpointerStats PendingCheckpointerStats;


/*
 * Variables in pgstat_database.c
 */

/* Updated by pgstat_count_buffer_*_time macros */
extern PGDLLIMPORT PgStat_Counter pgStatBlockReadTime;
extern PGDLLIMPORT PgStat_Counter pgStatBlockWriteTime;

/*
 * Updated by pgstat_count_conn_*_time macros, called by
 * pgstat_report_activity().
 */
extern PGDLLIMPORT PgStat_Counter pgStatActiveTime;
extern PGDLLIMPORT PgStat_Counter pgStatTransactionIdleTime;

/* updated by the traffic cop and in errfinish() */
extern PGDLLIMPORT SessionEndType pgStatSessionEndCause;


/*
 * Variables in pgstat_wal.c
 */

/* updated directly by backends and background processes */
extern PGDLLIMPORT PgStat_PendingWalStats PendingWalStats;


/* POLAR */
extern PgStat_Counter polar_get_audit_log_row_count(void);
extern PgStat_Counter polar_delay_dml_count;
extern void polar_update_delay_dml_count(void);

extern int polar_pgstat_get_virtual_pid(int real_pid, bool force);
extern int polar_pgstat_get_virtual_pid_by_beentry(PgBackendStatus *beentry);
extern int polar_pgstat_get_real_pid(int virtual_pid, int32 cancel_key, bool auth_cancel_key, bool force);
extern void polar_pgstat_set_virtual_pid(int virtual_pid);
extern void polar_pgstat_set_cancel_key(int32 cancel_key);
extern void polar_pgstat_set_proxy_mode(bool mode);
extern void polar_enable_proxy_for_unit_test(bool enable);
typedef void (*polar_postmaster_child_init_register) (void);
extern polar_postmaster_child_init_register polar_stat_hook;

/* POLAR: parse attrs for priority replication */
extern bool polar_walsender_parse_attrs(int pid, bool *is_high_pri, bool *is_low_pri);
extern bool polar_walsender_parse_my_attrs(bool *is_high_pri, bool *is_low_pri);

/* POLAR */
extern void polar_report_queryid(int64 queryid);

/* POLAR: proxy stats */
extern PolarStat_Proxy *polar_stat_proxy;
extern bool polar_stat_need_update_proxy_info;
extern polar_unsplittable_reason_t polar_unable_to_split_reason;
#define polar_stat_update_proxy_info(variable) \
{if (polar_stat_need_update_proxy_info) {(variable)++;}}
/* POLAR: end */

extern void polar_pgstat_beshutdown(int id);

#endif							/* PGSTAT_H */
