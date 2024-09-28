/*-------------------------------------------------------------------------
 *
 * walreceiver.c
 *
 * The WAL receiver process (walreceiver) is new as of Postgres 9.0. It
 * is the process in the standby server that takes charge of receiving
 * XLOG records from a primary server during streaming replication.
 *
 * When the startup process determines that it's time to start streaming,
 * it instructs postmaster to start walreceiver. Walreceiver first connects
 * to the primary server (it will be served by a walsender process
 * in the primary server), and then keeps receiving XLOG records and
 * writing them to the disk as long as the connection is alive. As XLOG
 * records are received and flushed to disk, it updates the
 * WalRcv->flushedUpto variable in shared memory, to inform the startup
 * process of how far it can proceed with XLOG replay.
 *
 * A WAL receiver cannot directly load GUC parameters used when establishing
 * its connection to the primary. Instead it relies on parameter values
 * that are passed down by the startup process when streaming is requested.
 * This applies, for example, to the replication slot and the connection
 * string to be used for the connection with the primary.
 *
 * If the primary server ends streaming, but doesn't disconnect, walreceiver
 * goes into "waiting" mode, and waits for the startup process to give new
 * instructions. The startup process will treat that the same as
 * disconnection, and will rescan the archive/pg_wal directory. But when the
 * startup process wants to try streaming replication again, it will just
 * nudge the existing walreceiver process that's waiting, instead of launching
 * a new one.
 *
 * Normal termination is by SIGTERM, which instructs the walreceiver to
 * exit(0). Emergency termination is by SIGQUIT; like any postmaster child
 * process, the walreceiver will simply abort and exit on SIGQUIT. A close
 * of the connection and a FATAL error are treated not as a crash but as
 * normal operation.
 *
 * This file contains the server-facing parts of walreceiver. The libpq-
 * specific parts are in the libpqwalreceiver module. It's loaded
 * dynamically to avoid linking the server with libpq.
 *
 * Portions Copyright (c) 2010-2024, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/replication/walreceiver.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>

#include "access/htup_details.h"
#include "access/timeline.h"
#include "access/transam.h"
#include "access/xlog_internal.h"
#include "access/xlogarchive.h"
#include "access/xlogrecovery.h"
#include "catalog/pg_authid.h"
#include "funcapi.h"
#include "lib/ilist.h"
#include "libpq/pqformat.h"
#include "libpq/pqsignal.h"
#include "miscadmin.h"
#include "pgstat.h"
<<<<<<< HEAD
#include "postmaster/startup.h"
=======
#include "postmaster/auxprocess.h"
#include "postmaster/interrupt.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#include "replication/walreceiver.h"
#include "replication/walsender.h"
#include "replication/walsender_private.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/procsignal.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/pg_lsn.h"
#include "utils/polar_coredump.h"
#include "utils/ps_status.h"
#include "utils/timestamp.h"

/* POLAR */
#include "access/polar_logindex_redo.h"
#include "access/polar_async_ddl_lock_replay.h"
#include "pgstat.h"
#include "polar_datamax/polar_datamax.h"
#include "polar_dma/polar_dma.h"
#include "postmaster/postmaster.h"
#include "replication/polar_cluster_info.h"
#include "storage/polar_fd.h"
#include "utils/guc.h"
#include "polar_dma/polar_dma.h"

/*
 * GUC variables.  (Other variables that affect walreceiver are in xlog.c
 * because they're passed down from the startup process, for better
 * synchronization.)
 */
int			wal_receiver_status_interval;
int			wal_receiver_timeout;
bool		hot_standby_feedback;

/*
 * POLAR: in polar standby, use polar_standby_feedback
 * to control hot_standby_feedback. It is set to false
 * by default, so polar standby disable the value of
 * hot_standby_feedback
 */
bool		polar_standby_feedback;

/* libpqwalreceiver connection */
static WalReceiverConn *wrconn = NULL;
WalReceiverFunctionsType *WalReceiverFunctions = NULL;

<<<<<<< HEAD
#define NAPTIME_PER_CYCLE 100	/* max sleep time between cycles (100ms) */

/* POLAR: timeout for send promote request to walsnd */
#define POLAR_SEND_PROMOTE_REQUEST_TIMEOUT	500
/* POLAR end */

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
/*
 * These variables are used similarly to openLogFile/SegNo,
 * but for walreceiver to write the XLOG. recvFileTLI is the TimeLineID
 * corresponding the filename of recvFile.
 */
static int	recvFile = -1;
static TimeLineID recvFileTLI = 0;
static XLogSegNo recvSegNo = 0;
<<<<<<< HEAD
static uint32 recvOff = 0;
/* POLAR: local flag for datamax */
static bool 	polar_is_initial_datamax = false;
/* list for primary's last valid lsn, used for keep xlog consistency between primary and datamax */
static polar_datamax_valid_lsn_list	 *polar_datamax_received_valid_lsn_list = NULL;
/* POLAR end */
/*
 * Flags set by interrupt handlers of walreceiver for later service in the
 * main loop.
 */
static volatile sig_atomic_t got_SIGHUP = false;
static volatile sig_atomic_t got_SIGTERM = false;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/*
 * LogstreamResult indicates the byte positions that we have already
 * written/fsynced.
 */
static struct
{
	XLogRecPtr	Write;			/* last byte + 1 written out in the standby */
	XLogRecPtr	Flush;			/* last byte + 1 flushed in the standby */
}			LogstreamResult;

/*
 * Reasons to wake up and perform periodic tasks.
 */
typedef enum WalRcvWakeupReason
{
	WALRCV_WAKEUP_TERMINATE,
	WALRCV_WAKEUP_PING,
	WALRCV_WAKEUP_REPLY,
	WALRCV_WAKEUP_HSFEEDBACK,
#define NUM_WALRCV_WAKEUPS (WALRCV_WAKEUP_HSFEEDBACK + 1)
} WalRcvWakeupReason;

<<<<<<< HEAD
=======
/*
 * Wake up times for periodic tasks.
 */
static TimestampTz wakeup[NUM_WALRCV_WAKEUPS];

static StringInfoData reply_message;

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
/* Prototypes for private functions */
static void WalRcvFetchTimeLineHistoryFiles(TimeLineID first, TimeLineID last);
static void WalRcvWaitForStartPosition(XLogRecPtr *startpoint, TimeLineID *startpointTLI);
static void WalRcvDie(int code, Datum arg);
static void XLogWalRcvProcessMsg(unsigned char type, char *buf, Size len,
								 TimeLineID tli);
static void XLogWalRcvWrite(char *buf, Size nbytes, XLogRecPtr recptr,
							TimeLineID tli);
static void XLogWalRcvFlush(bool dying, TimeLineID tli);
static void XLogWalRcvClose(XLogRecPtr recptr, TimeLineID tli);
static void XLogWalRcvSendReply(bool force, bool requestReply);
static void XLogWalRcvSendHSFeedback(bool immed);
static void ProcessWalSndrMessage(XLogRecPtr walEnd, TimestampTz sendTime);
static void WalRcvComputeNextWakeup(WalRcvWakeupReason reason, TimestampTz now);

<<<<<<< HEAD
/* Signal handlers */
static void WalRcvSigHupHandler(SIGNAL_ARGS);
static void WalRcvSigUsr1Handler(SIGNAL_ARGS);
static void WalRcvShutdownHandler(SIGNAL_ARGS);
static void WalRcvQuickDieHandler(SIGNAL_ARGS);

/* POLAR: callback function when waiting free space from polar_xlog_queue */
static void polar_receiver_xlog_queue_callback(polar_ringbuf_t rbuf);
static void polar_recv_push_storage_begin_callback(polar_ringbuf_t rbuf);
static void polar_notify_read_wal_file(int code, Datum arg);

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
/*
 * Process any interrupts the walreceiver process may have received.
 * This should be called any time the process's latch has become set.
 *
 * Currently, only SIGTERM is of interest.  We can't just exit(1) within the
 * SIGTERM signal handler, because the signal might arrive in the middle of
 * some critical operation, like while we're holding a spinlock.  Instead, the
 * signal handler sets a flag variable as well as setting the process's latch.
 * We must check the flag (by calling ProcessWalRcvInterrupts) anytime the
 * latch has become set.  Operations that could block for a long time, such as
 * reading from a remote server, must pay attention to the latch too; see
 * libpqrcv_PQgetResult for example.
 */
void
ProcessWalRcvInterrupts(void)
{
	/*
	 * Although walreceiver interrupt handling doesn't use the same scheme as
	 * regular backends, call CHECK_FOR_INTERRUPTS() to make sure we receive
	 * any incoming signals on Win32, and also to make sure we process any
	 * barrier events.
	 */
	CHECK_FOR_INTERRUPTS();

	if (ShutdownRequestPending)
	{
		ereport(FATAL,
				(errcode(ERRCODE_ADMIN_SHUTDOWN),
				 errmsg("terminating walreceiver process due to administrator command")));
	}
}


/* Main entry point for walreceiver process */
void
WalReceiverMain(char *startup_data, size_t startup_data_len)
{
	char		conninfo[MAXCONNINFO];
	char	   *tmp_conninfo;
	char		slotname[NAMEDATALEN];
	bool		is_temp_slot;
	XLogRecPtr	startpoint;
	TimeLineID	startpointTLI;
	TimeLineID	primaryTLI;
	bool		first_stream;
	WalRcvData *walrcv;
	TimestampTz now;
	char	   *err;
	char	   *sender_host = NULL;
	int			sender_port = 0;
	char	   *appname;

	Assert(startup_data_len == 0);

	MyBackendType = B_WAL_RECEIVER;
	AuxiliaryProcessMainCommon();

	/*
	 * WalRcv should be set up already (if we are a backend, we inherit this
	 * by fork() or EXEC_BACKEND mechanism from the postmaster).
	 */
	walrcv = WalRcv;
	Assert(walrcv != NULL);

	/*
	 * Mark walreceiver as running in shared memory.
	 *
	 * Do this as early as possible, so that if we fail later on, we'll set
	 * state to STOPPED. If we die before this, the startup process will keep
	 * waiting for us to start up, until it times out.
	 */
	SpinLockAcquire(&walrcv->mutex);
	Assert(walrcv->pid == 0);
	switch (walrcv->walRcvState)
	{
		case WALRCV_STOPPING:
			/* If we've already been requested to stop, don't start up. */
			walrcv->walRcvState = WALRCV_STOPPED;
			/* fall through */

		case WALRCV_STOPPED:
			SpinLockRelease(&walrcv->mutex);
			ConditionVariableBroadcast(&walrcv->walRcvStoppedCV);
			proc_exit(1);
			break;

		case WALRCV_STARTING:
			/* The usual case */
			break;

		case WALRCV_WAITING:
		case WALRCV_STREAMING:
		case WALRCV_RESTARTING:
		default:
			/* Shouldn't happen */
			SpinLockRelease(&walrcv->mutex);
			elog(PANIC, "walreceiver still running according to shared memory state");
	}
	/* Advertise our PID so that the startup process can kill us */
	walrcv->pid = MyProcPid;
	walrcv->walRcvState = WALRCV_STREAMING;

	/* Fetch information required to start streaming */
	walrcv->ready_to_display = false;
	strlcpy(conninfo, (char *) walrcv->conninfo, MAXCONNINFO);
	strlcpy(slotname, (char *) walrcv->slotname, NAMEDATALEN);
	is_temp_slot = walrcv->is_temp_slot;
	startpoint = walrcv->receiveStart;
	startpointTLI = walrcv->receiveStartTLI;

	/*
	 * At most one of is_temp_slot and slotname can be set; otherwise,
	 * RequestXLogStreaming messed up.
	 */
	Assert(!is_temp_slot || (slotname[0] == '\0'));

	/* Initialise to a sanish value */
	now = GetCurrentTimestamp();
	walrcv->lastMsgSendTime =
		walrcv->lastMsgReceiptTime = walrcv->latestWalEndTime = now;

	/* Report the latch to use to awaken this process */
	walrcv->latch = &MyProc->procLatch;

	/* POLAR: Reset consistent lsn received from primary node while starting up walreceiver. */
	WalRcv->curr_primary_consistent_lsn = InvalidXLogRecPtr;

	SpinLockRelease(&walrcv->mutex);

<<<<<<< HEAD
	/* POLAR:notify startup to read wal file instead of logindex queue */
	before_shmem_exit(polar_notify_read_wal_file, 0);
	/* POLAR: free memory of datamax_valid_lsn_list */
	before_shmem_exit(polar_datamax_free_valid_lsn_list, PointerGetDatum(polar_datamax_received_valid_lsn_list));
	before_shmem_exit(polar_cluster_info_offline, 0);
=======
	pg_atomic_write_u64(&WalRcv->writtenUpto, 0);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* Arrange to clean up at walreceiver exit */
	on_shmem_exit(WalRcvDie, PointerGetDatum(&startpointTLI));

	/* Properly accept or ignore signals the postmaster might send us */
	pqsignal(SIGHUP, SignalHandlerForConfigReload); /* set flag to read config
													 * file */
	pqsignal(SIGINT, SIG_IGN);
	pqsignal(SIGTERM, SignalHandlerForShutdownRequest); /* request shutdown */
	/* SIGQUIT handler was already set up by InitPostmasterChild */
	pqsignal(SIGALRM, SIG_IGN);
	pqsignal(SIGPIPE, SIG_IGN);
	pqsignal(SIGUSR1, procsignal_sigusr1_handler);
	pqsignal(SIGUSR2, SIG_IGN);

	/* Reset some signals that are accepted by postmaster but not here */
	pqsignal(SIGCHLD, SIG_DFL);
<<<<<<< HEAD
	pqsignal(SIGTTIN, SIG_DFL);
	pqsignal(SIGTTOU, SIG_DFL);
	pqsignal(SIGCONT, SIG_DFL);
	pqsignal(SIGWINCH, SIG_DFL);

	/* POLAR : register for coredump print */
#ifndef _WIN32
#ifdef SIGILL
	pqsignal(SIGILL, polar_program_error_handler);
#endif
#ifdef SIGSEGV
	pqsignal(SIGSEGV, polar_program_error_handler);
#endif
#ifdef SIGBUS
	pqsignal(SIGBUS, polar_program_error_handler);
#endif
#endif	/* _WIN32 */
	/* POLAR: end */

	/* We allow SIGQUIT (quickdie) at all times */
	sigdelset(&BlockSig, SIGQUIT);
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* Load the libpq-specific functions */
	load_file("libpqwalreceiver", false);
	if (WalReceiverFunctions == NULL)
		elog(ERROR, "libpqwalreceiver didn't initialize correctly");

	/* Unblock signals (they were blocked when the postmaster forked us) */
	sigprocmask(SIG_SETMASK, &UnBlockSig, NULL);

	/* Establish the connection to the primary for XLOG streaming */
<<<<<<< HEAD
	wrconn = walrcv_connect(conninfo, false, "walreceiver", &err);
	if (!wrconn)
		ereport(ERROR,
				(errmsg("could not connect to the primary server: %s", err)));
=======
	appname = cluster_name[0] ? cluster_name : "walreceiver";
	wrconn = walrcv_connect(conninfo, true, false, false, appname, &err);
	if (!wrconn)
		ereport(ERROR,
				(errcode(ERRCODE_CONNECTION_FAILURE),
				 errmsg("streaming replication receiver \"%s\" could not connect to the primary server: %s",
						appname, err)));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Save user-visible connection string.  This clobbers the original
	 * conninfo, for security. Also save host and port of the sender server
	 * this walreceiver is connected to.
	 */
	tmp_conninfo = walrcv_get_conninfo(wrconn);
	walrcv_get_senderinfo(wrconn, &sender_host, &sender_port);
	SpinLockAcquire(&walrcv->mutex);
	memset(walrcv->conninfo, 0, MAXCONNINFO);
	if (tmp_conninfo)
		strlcpy((char *) walrcv->conninfo, tmp_conninfo, MAXCONNINFO);

	memset(walrcv->sender_host, 0, NI_MAXHOST);
	if (sender_host)
		strlcpy((char *) walrcv->sender_host, sender_host, NI_MAXHOST);

	walrcv->sender_port = sender_port;
	walrcv->ready_to_display = true;
	/* POLAR: we'd like to update first */
	walrcv->polar_hot_standby_state = -1;
	SpinLockRelease(&walrcv->mutex);

	if (tmp_conninfo)
		pfree(tmp_conninfo);

	if (sender_host)
		pfree(sender_host);

	first_stream = true;
	/* POLAR: create and init list for recording primary's valid lsn in datamax mode */
	if (polar_is_datamax() && !POLAR_ENABLE_DMA())
		polar_datamax_received_valid_lsn_list = polar_datamax_create_valid_lsn_list();
	for (;;)
	{
		char	   *primary_sysid;
		char		standby_sysid[32];
		WalRcvStreamOptions options;
		bool		polar_replica = false;

		/* POLAR: enable replica stream replication */
		if (polar_in_replica_mode())
			polar_replica = true;

		/* POLAR: force log */
		if (polar_replica)
			ereport(LOG, (errmsg("Start wal receiver on polardb")));

		/*
		 * Check that we're connected to a valid server using the
		 * IDENTIFY_SYSTEM replication command.
		 */
<<<<<<< HEAD
		primary_sysid = walrcv_identify_system(wrconn, &primaryTLI,
											   &server_version);
=======
		primary_sysid = walrcv_identify_system(wrconn, &primaryTLI);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

		snprintf(standby_sysid, sizeof(standby_sysid), UINT64_FORMAT,
				 GetSystemIdentifier());
		if (strcmp(primary_sysid, standby_sysid) != 0)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("database system identifier differs between the primary and standby"),
					 errdetail("The primary's identifier is %s, the standby's identifier is %s.",
							   primary_sysid, standby_sysid)));
		}
<<<<<<< HEAD

		/* POLAR: Update DataMax timeline if current is a initial one. */
		if (polar_is_datamax() && 
				((!POLAR_ENABLE_DMA() && polar_datamax_is_initial(polar_datamax_ctl)) ||
				 (POLAR_ENABLE_DMA() && startpointTLI == POLAR_INVALID_TIMELINE_ID)))
		{
			Assert(startpointTLI == POLAR_INVALID_TIMELINE_ID);

			polar_is_initial_datamax = true;

			if (startpointTLI == POLAR_INVALID_TIMELINE_ID)
			{
				/* 
				 * POLAR: If we are in DataMax mode and requested timeline is invalid,
				 * it means that we are a initial one, so we need to fetch as much as 
				 * possible  WAL from Primary's current timeline, so update walreceiver's 
				 * receiveStartTLI with primary's current one.  
				 */
				SpinLockAcquire(&walrcv->mutex);
				walrcv->receiveStartTLI = startpointTLI = primaryTLI;
				SpinLockRelease(&walrcv->mutex);
				ereport(LOG,
					(errmsg("initial DataMax node update requested streaming timeline with primary's current one %d", 
						primaryTLI)));
			}
			else
				ereport(ERROR,
					(errmsg("initial datamax requested with certain timeline id: %d", startpointTLI)));
		}
		else
			polar_is_initial_datamax = false;
		/* POLAR end */
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

		/*
		 * Confirm that the current timeline of the primary is the same or
		 * ahead of ours.
		 */
		if (primaryTLI < startpointTLI)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("highest timeline %u of the primary is behind recovery timeline %u",
							primaryTLI, startpointTLI)));

		/*
		 * Get any missing history files. We do this always, even when we're
		 * not interested in that timeline, so that if we're promoted to
		 * become the primary later on, we don't select the same timeline that
		 * was already used in the current primary. This isn't bullet-proof -
		 * you'll need some external software to manage your cluster if you
		 * need to ensure that a unique timeline id is chosen in every case,
		 * but let's avoid the confusion of timeline id collisions where we
		 * can.
		 */
		WalRcvFetchTimeLineHistoryFiles(startpointTLI, primaryTLI);

		/*
		 * Create temporary replication slot if requested, and update slot
		 * name in shared memory.  (Note the slot name cannot already be set
		 * in this case.)
		 */
		if (is_temp_slot)
		{
			snprintf(slotname, sizeof(slotname),
					 "pg_walreceiver_%lld",
					 (long long int) walrcv_get_backend_pid(wrconn));

			walrcv_create_slot(wrconn, slotname, true, false, false, 0, NULL);

			SpinLockAcquire(&walrcv->mutex);
			strlcpy(walrcv->slotname, slotname, NAMEDATALEN);
			SpinLockRelease(&walrcv->mutex);
		}

		/*
		 * Start streaming.
		 *
		 * We'll try to start at the requested starting point and timeline,
		 * even if it's different from the server's latest timeline. In case
		 * we've already reached the end of the old timeline, the server will
		 * finish the streaming immediately, and we will go back to await
		 * orders from the startup process. If recovery_target_timeline is
		 * 'latest', the startup process will scan pg_wal and find the new
		 * history file, bump recovery target timeline, and ask us to restart
		 * on the new timeline.
		 */
		options.logical = false;
		options.startpoint = startpoint;
		options.slotname = slotname[0] != '\0' ? slotname : NULL;
		options.proto.physical.startpointTLI = startpointTLI;
<<<<<<< HEAD
		/* POLAR: Set current replication mode */
		options.polar_repl_mode = polar_gen_replication_mode();
		/* POLAR end */
		ThisTimeLineID = startpointTLI;
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		if (walrcv_startstreaming(wrconn, &options))
		{
			if (first_stream)
			{
				/*
				 * POLAR: If logindex meta start lsn is invalid ,standby reach consistency and start to create streaming,
				 * we will set valid information to enable logindex parse.
				 * For rw node this will be set after parsed all xlog in startup process
				 * For ro node, logindex snapshot is readonly, so there is no need to set valid information
				 * For datamax node, logindex won't be enabled
				 */
				if (polar_logindex_redo_instance)
					polar_logindex_redo_set_valid_info(polar_logindex_redo_instance, GetXLogReplayRecPtr(NULL));

				ereport(LOG,
						(errmsg("started streaming WAL from primary at %X/%X on timeline %u",
<<<<<<< HEAD
								(uint32) (startpoint >> 32), (uint32) startpoint,
								startpointTLI)));
			}
=======
								LSN_FORMAT_ARGS(startpoint), startpointTLI)));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			else
				ereport(LOG,
						(errmsg("restarted WAL streaming at %X/%X on timeline %u",
								LSN_FORMAT_ARGS(startpoint), startpointTLI)));
			first_stream = false;
			/* Initialize LogstreamResult and buffers for processing messages */
			/* 
			 * POLAR: Initialize LogstreamResult as last valid received lsn when in datamax mode
			 * otherwise, when datamax_replay_lsn > received_lsn and we set flush_lsn = datamax_replay_lsn
			 * the wal received won't be flushed to disk because flush_lsn > received_lsn(which is write lsn) in this case
			 */
			if (!polar_is_datamax())
				LogstreamResult.Write = LogstreamResult.Flush = GetXLogReplayRecPtr(NULL);
			else if (POLAR_ENABLE_DMA())
				LogstreamResult.Write = LogstreamResult.Flush = polar_dma_get_received_lsn();
			else
				LogstreamResult.Write = LogstreamResult.Flush = polar_datamax_get_last_valid_received_lsn(polar_datamax_ctl, NULL);
			/* POLAR end */
			initStringInfo(&reply_message);

			/* Initialize nap wakeup times. */
			now = GetCurrentTimestamp();
			for (int i = 0; i < NUM_WALRCV_WAKEUPS; ++i)
				WalRcvComputeNextWakeup(i, now);

			/* Send initial reply/feedback messages. */
			XLogWalRcvSendReply(true, false);
			XLogWalRcvSendHSFeedback(true);

			/* Loop until end-of-streaming or error */
			for (;;)
			{
				char	   *buf;
				int			len;
				bool		endofwal = false;
				pgsocket	wait_fd = PGINVALID_SOCKET;
				int			rc;
<<<<<<< HEAD
				int			hot_standby_state;
=======
				TimestampTz nextWakeup;
				long		nap;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

				/*
				 * Exit walreceiver if we're not in recovery. This should not
				 * happen, but cross-check the status here.
				 */
				if (!RecoveryInProgress())
					ereport(FATAL,
							(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
							 errmsg("cannot continue WAL streaming, recovery has already ended")));

				/* Process any requests or signals received recently */
				ProcessWalRcvInterrupts();

				if (ConfigReloadPending)
				{
					ConfigReloadPending = false;
					ProcessConfigFile(PGC_SIGHUP);
					/* recompute wakeup times */
					now = GetCurrentTimestamp();
					for (int i = 0; i < NUM_WALRCV_WAKEUPS; ++i)
						WalRcvComputeNextWakeup(i, now);
					XLogWalRcvSendHSFeedback(true);
				}

				/* See if we can read data immediately */
				len = walrcv_receive(wrconn, &buf, &wait_fd);
				if (len != 0)
				{
					/*
					 * Process the received data, and any subsequent data we
					 * can read without blocking.
					 */
					for (;;)
					{
						if (len > 0)
						{
							/*
							 * Something was received from primary, so adjust
							 * the ping and terminate wakeup times.
							 */
							now = GetCurrentTimestamp();
							WalRcvComputeNextWakeup(WALRCV_WAKEUP_TERMINATE,
													now);
							WalRcvComputeNextWakeup(WALRCV_WAKEUP_PING, now);
							XLogWalRcvProcessMsg(buf[0], &buf[1], len - 1,
												 startpointTLI);
						}
						else if (len == 0)
							break;
						else if (len < 0)
						{
							ereport(LOG,
									(errmsg("replication terminated by primary server"),
									 errdetail("End of WAL reached on timeline %u at %X/%X.",
											   startpointTLI,
											   LSN_FORMAT_ARGS(LogstreamResult.Write))));
							endofwal = true;
							break;
						}

						/* POLAR: ask walsender whether promote can be executed */
						if (polar_send_promote_request())
						{
							elog(LOG,"ask walsender for whether promote can be executed");
							polar_walrcv_send_promote(true);
						}
						/* POLAR end */

						/* Let the master know that we received some data. 
						 *
						 * POLAR: When standby is much slower than primary server,
						 * walreceiver process will be trapped in this loop so that it
						 * can not keep alive with walsender. If walsender can't get
						 * reply message after wal_sender_timeout, it will terminate
						 * replication. In order to avoid this issue, we force to send
						 * reply message after wal_receiver_status_interval seconds.
						 */
						XLogWalRcvSendReply(false, false);

						len = walrcv_receive(wrconn, &buf, &wait_fd);
					}

<<<<<<< HEAD
=======
					/* Let the primary know that we received some data. */
					XLogWalRcvSendReply(false, false);

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
					/*
					 * If we've written some records, flush them to disk and
					 * let the startup process and primary server know about
					 * them.
					 */
					XLogWalRcvFlush(false, startpointTLI);
				}

				/* 
				 * POLAR: ask walsender whether promote can be executed 
				 * when receive nothing from primary, and we haven't send this info before, send it now
				 */
				if (polar_send_promote_request())
				{
					elog(LOG,"ask walsender for whether promote can be executed");
					polar_walrcv_send_promote(true);
				}

				/* POLAR: check whether all wal have been received, if so, promote is allowed */
				polar_promote_check_received_all_wal();
				/* POLAR end */

				hot_standby_state = polar_get_hot_standby_state() + polar_get_available_state() * 10;
				if (walrcv->polar_hot_standby_state != hot_standby_state)
				{
					walrcv->polar_hot_standby_state = hot_standby_state;
					polar_send_node_info(&reply_message);
					walrcv_send(wrconn, reply_message.data, reply_message.len);
				}

				/* Check if we need to exit the streaming loop. */
				if (endofwal)
					break;

				/* Find the soonest wakeup time, to limit our nap. */
				nextWakeup = TIMESTAMP_INFINITY;
				for (int i = 0; i < NUM_WALRCV_WAKEUPS; ++i)
					nextWakeup = Min(wakeup[i], nextWakeup);

				/* Calculate the nap time, clamping as necessary. */
				now = GetCurrentTimestamp();
				nap = TimestampDifferenceMilliseconds(now, nextWakeup);

				/*
				 * Ideally we would reuse a WaitEventSet object repeatedly
				 * here to avoid the overheads of WaitLatchOrSocket on epoll
				 * systems, but we can't be sure that libpq (or any other
				 * walreceiver implementation) has the same socket (even if
				 * the fd is the same number, it may have been closed and
				 * reopened since the last time).  In future, if there is a
				 * function for removing sockets from WaitEventSet, then we
				 * could add and remove just the socket each time, potentially
				 * avoiding some system calls.
				 */
				Assert(wait_fd != PGINVALID_SOCKET);
				rc = WaitLatchOrSocket(MyLatch,
									   WL_EXIT_ON_PM_DEATH | WL_SOCKET_READABLE |
									   WL_TIMEOUT | WL_LATCH_SET,
									   wait_fd,
									   nap,
									   WAIT_EVENT_WAL_RECEIVER_MAIN);
				if (rc & WL_LATCH_SET)
				{
<<<<<<< HEAD
					ResetLatch(walrcv->latch);
=======
					ResetLatch(MyLatch);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
					ProcessWalRcvInterrupts();

					if (walrcv->force_reply)
					{
						/*
						 * The recovery process has asked us to send apply
						 * feedback now.  Make sure the flag is really set to
						 * false in shared memory before sending the reply, so
						 * we don't miss a new request for a reply.
						 */
						walrcv->force_reply = false;
						pg_memory_barrier();
						XLogWalRcvSendReply(true, false);
					}
				}
				if (rc & WL_TIMEOUT)
				{
					/*
					 * We didn't receive anything new. If we haven't heard
					 * anything from the server for more than
					 * wal_receiver_timeout / 2, ping the server. Also, if
					 * it's been longer than wal_receiver_status_interval
					 * since the last update we sent, send a status update to
					 * the primary anyway, to report any progress in applying
					 * WAL.
					 */
					bool		requestReply = false;

					/*
					 * Check if time since last receive from primary has
					 * reached the configured limit.
					 */
					now = GetCurrentTimestamp();
					if (now >= wakeup[WALRCV_WAKEUP_TERMINATE])
						ereport(ERROR,
								(errcode(ERRCODE_CONNECTION_FAILURE),
								 errmsg("terminating walreceiver due to timeout")));

					/*
					 * If we didn't receive anything new for half of receiver
					 * replication timeout, then ping the server.
					 */
					if (now >= wakeup[WALRCV_WAKEUP_PING])
					{
						requestReply = true;
						wakeup[WALRCV_WAKEUP_PING] = TIMESTAMP_INFINITY;
					}

					XLogWalRcvSendReply(requestReply, requestReply);
					XLogWalRcvSendHSFeedback(false);
				}
			}

			/*
			 * The backend finished streaming. Exit streaming COPY-mode from
			 * our side, too.
			 */
			walrcv_endstreaming(wrconn, &primaryTLI);

			/*
			 * If the server had switched to a new timeline that we didn't
			 * know about when we began streaming, fetch its timeline history
			 * file now.
			 */
			WalRcvFetchTimeLineHistoryFiles(startpointTLI, primaryTLI);
		}
		else
			ereport(LOG,
					(errmsg("primary server contains no more WAL on requested timeline %u",
							startpointTLI)));

		/*
		 * End of WAL reached on the requested timeline. Close the last
		 * segment, and await for new orders from the startup process.
		 */
		if (recvFile >= 0)
		{
			char		xlogfname[MAXFNAMELEN];

<<<<<<< HEAD
			XLogWalRcvFlush(false);
			if (polar_close(recvFile) != 0)
=======
			XLogWalRcvFlush(false, startpointTLI);
			XLogFileName(xlogfname, recvFileTLI, recvSegNo, wal_segment_size);
			if (close(recvFile) != 0)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				ereport(PANIC,
						(errcode_for_file_access(),
						 errmsg("could not close WAL segment %s: %m",
								xlogfname)));

			/*
			 * Create .done file forcibly to prevent the streamed segment from
			 * being archived later.
			 */
<<<<<<< HEAD
			polar_is_datamax_mode = polar_is_datamax();		/* POLAR: Enter datamax Mode. */
			XLogFileName(xlogfname, recvFileTLI, recvSegNo, wal_segment_size);
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			if (XLogArchiveMode != ARCHIVE_MODE_ALWAYS)
				XLogArchiveForceDone(xlogfname);
			else if (POLAR_ENABLE_DMA())
				polar_dma_xlog_archive_notify(xlogfname, true);
			else
				XLogArchiveNotify(xlogfname);
			polar_is_datamax_mode = false;				/* POLAR: Leave datamax mode. */
		}
		recvFile = -1;

		elog(DEBUG1, "walreceiver ended streaming and awaits new instructions");
		WalRcvWaitForStartPosition(&startpoint, &startpointTLI);
	}
	/* not reached */
}

/*
 * Wait for startup process to set receiveStart and receiveStartTLI.
 */
static void
WalRcvWaitForStartPosition(XLogRecPtr *startpoint, TimeLineID *startpointTLI)
{
	WalRcvData *walrcv = WalRcv;
	int			state;

	SpinLockAcquire(&walrcv->mutex);
	state = walrcv->walRcvState;
	if (state != WALRCV_STREAMING)
	{
		SpinLockRelease(&walrcv->mutex);
		if (state == WALRCV_STOPPING)
			proc_exit(0);
		else
			elog(FATAL, "unexpected walreceiver state");
	}
	walrcv->walRcvState = WALRCV_WAITING;
	walrcv->receiveStart = InvalidXLogRecPtr;
	walrcv->receiveStartTLI = 0;
	SpinLockRelease(&walrcv->mutex);

	set_ps_display("idle");

	/*
	 * nudge startup process to notice that we've stopped streaming and are
	 * now waiting for instructions.
	 */
	WakeupRecovery();
	for (;;)
	{
		ResetLatch(MyLatch);

		ProcessWalRcvInterrupts();

		SpinLockAcquire(&walrcv->mutex);
		Assert(walrcv->walRcvState == WALRCV_RESTARTING ||
			   walrcv->walRcvState == WALRCV_WAITING ||
			   walrcv->walRcvState == WALRCV_STOPPING);
		if (walrcv->walRcvState == WALRCV_RESTARTING)
		{
			/*
			 * No need to handle changes in primary_conninfo or
			 * primary_slot_name here. Startup process will signal us to
			 * terminate in case those change.
			 */
			*startpoint = walrcv->receiveStart;
			*startpointTLI = walrcv->receiveStartTLI;
			walrcv->walRcvState = WALRCV_STREAMING;
			SpinLockRelease(&walrcv->mutex);
			break;
		}
		if (walrcv->walRcvState == WALRCV_STOPPING)
		{
			/*
			 * We should've received SIGTERM if the startup process wants us
			 * to die, but might as well check it here too.
			 */
			SpinLockRelease(&walrcv->mutex);
			exit(1);
		}
		SpinLockRelease(&walrcv->mutex);

		(void) WaitLatch(MyLatch, WL_LATCH_SET | WL_EXIT_ON_PM_DEATH, 0,
						 WAIT_EVENT_WAL_RECEIVER_WAIT_START);
	}

	if (update_process_title)
	{
		char		activitymsg[50];

		snprintf(activitymsg, sizeof(activitymsg), "restarting at %X/%X",
				 LSN_FORMAT_ARGS(*startpoint));
		set_ps_display(activitymsg);
	}
}

/*
 * Fetch any missing timeline history files between 'first' and 'last'
 * (inclusive) from the server.
 */
static void
WalRcvFetchTimeLineHistoryFiles(TimeLineID first, TimeLineID last)
{
	TimeLineID	tli;

	polar_is_datamax_mode = polar_is_datamax();		/* POLAR: Enter datamax Mode. */
	for (tli = first; tli <= last; tli++)
	{
		/* there's no history file for timeline 1 */
		if (tli != 1 && !existsTimeLineHistory(tli))
		{
			char	   *fname;
			char	   *content;
			int			len;
			char		expectedfname[MAXFNAMELEN];

			ereport(LOG,
					(errmsg("fetching timeline history file for timeline %u from primary server",
							tli)));

			walrcv_readtimelinehistoryfile(wrconn, tli, &fname, &content, &len);

			/*
			 * Check that the filename on the primary matches what we
			 * calculated ourselves. This is just a sanity check, it should
			 * always match.
			 */
			TLHistoryFileName(expectedfname, tli);
			if (strcmp(fname, expectedfname) != 0)
				ereport(ERROR,
						(errcode(ERRCODE_PROTOCOL_VIOLATION),
						 errmsg_internal("primary reported unexpected file name for timeline history file of timeline %u",
										 tli)));

			/*
			 * Write the file to pg_wal.
			 */
			writeTimeLineHistoryFile(tli, content, len);

			/*
<<<<<<< HEAD
			 * In DMA mode, archive history file for standby
			 */
			if (POLAR_ENABLE_DMA())
			{
				if (XLogArchiveMode != ARCHIVE_MODE_ALWAYS)
					XLogArchiveForceDone(expectedfname);
				else
					polar_dma_xlog_archive_notify(expectedfname, true);
			}
=======
			 * Mark the streamed history file as ready for archiving if
			 * archive_mode is always.
			 */
			if (XLogArchiveMode != ARCHIVE_MODE_ALWAYS)
				XLogArchiveForceDone(fname);
			else
				XLogArchiveNotify(fname);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

			pfree(fname);
			pfree(content);
		}
	}
	polar_is_datamax_mode = false;				/* POLAR: Leave datamax mode. */
}

/*
 * Mark us as STOPPED in shared memory at exit.
 */
static void
WalRcvDie(int code, Datum arg)
{
	WalRcvData *walrcv = WalRcv;
	TimeLineID *startpointTLI_p = (TimeLineID *) DatumGetPointer(arg);

	Assert(*startpointTLI_p != 0);

	/* Ensure that all WAL records received are flushed to disk */
	XLogWalRcvFlush(true, *startpointTLI_p);

	/* Mark ourselves inactive in shared memory */
	SpinLockAcquire(&walrcv->mutex);
	Assert(walrcv->walRcvState == WALRCV_STREAMING ||
		   walrcv->walRcvState == WALRCV_RESTARTING ||
		   walrcv->walRcvState == WALRCV_STARTING ||
		   walrcv->walRcvState == WALRCV_WAITING ||
		   walrcv->walRcvState == WALRCV_STOPPING);
	Assert(walrcv->pid == MyProcPid);
	walrcv->walRcvState = WALRCV_STOPPED;
	walrcv->pid = 0;
	walrcv->ready_to_display = false;
	walrcv->latch = NULL;
	SpinLockRelease(&walrcv->mutex);

	ConditionVariableBroadcast(&walrcv->walRcvStoppedCV);

	/* Terminate the connection gracefully. */
	if (wrconn != NULL)
		walrcv_disconnect(wrconn);

	/* Wake up the startup process to notice promptly that we're gone */
	WakeupRecovery();
}

<<<<<<< HEAD
/* SIGHUP: set flag to re-read config file at next convenient time */
static void
WalRcvSigHupHandler(SIGNAL_ARGS)
{
	got_SIGHUP = true;
}


/* SIGUSR1: used by latch mechanism */
static void
WalRcvSigUsr1Handler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	latch_sigusr1_handler();

	errno = save_errno;
}

/* SIGTERM: set flag for ProcessWalRcvInterrupts */
static void
WalRcvShutdownHandler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_SIGTERM = true;

	if (WalRcv->latch)
		SetLatch(WalRcv->latch);

	errno = save_errno;
}

/*
 * WalRcvQuickDieHandler() occurs when signalled SIGQUIT by the postmaster.
 *
 * Some backend has bought the farm, so we need to stop what we're doing and
 * exit.
 */
static void
WalRcvQuickDieHandler(SIGNAL_ARGS)
{
	/*
	 * We DO NOT want to run proc_exit() or atexit() callbacks -- we're here
	 * because shared memory may be corrupted, so we don't want to try to
	 * clean up our transaction.  Just nail the windows shut and get out of
	 * town.  The callbacks wouldn't be safe to run from a signal handler,
	 * anyway.
	 *
	 * Note we use _exit(2) not _exit(0).  This is to force the postmaster
	 * into a system reset cycle if someone sends a manual SIGQUIT to a
	 * random backend.  This is necessary precisely because we don't clean up
	 * our shared memory state.  (The "dead man switch" mechanism in
	 * pmsignal.c should ensure the postmaster sees this as a crash, too, but
	 * no harm in being doubly sure.)
	 */
	_exit(2);
}

/* Set new consistent lsn received from primary node */
void
polar_set_primary_consistent_lsn(XLogRecPtr new_consistent_lsn)
{
	if (WalRcv->curr_primary_consistent_lsn < new_consistent_lsn)
	{
		SpinLockAcquire(&WalRcv->mutex);
		WalRcv->curr_primary_consistent_lsn = new_consistent_lsn;
		SpinLockRelease(&WalRcv->mutex);
	}
}

/*
 * Primay instance send the consistant lsn by walsender process,
 * replica walreceiver process receives consistant lsn, and saves
 * it in WalRcv->curr_primary_consistent_lsn
 */
XLogRecPtr
polar_get_primary_consist_ptr(void)
{
	XLogRecPtr curr_primary_consist_lsn = InvalidXLogRecPtr;
	SpinLockAcquire(&WalRcv->mutex);
	curr_primary_consist_lsn = WalRcv->curr_primary_consistent_lsn;
	SpinLockRelease(&WalRcv->mutex);

	return curr_primary_consist_lsn;
}

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
/*
 * Accept the message from XLOG stream, and process it.
 */
static void
XLogWalRcvProcessMsg(unsigned char type, char *buf, Size len, TimeLineID tli)
{
	int			hdrlen;
	XLogRecPtr	dataStart;
	XLogRecPtr	walEnd;
	TimestampTz sendTime;
	bool		replyRequested;

<<<<<<< HEAD
	/* POLAR */
	XLogRecPtr	curr_primary_consist_lsn_new;
	bool		is_promote_allowed;
	XLogRecPtr  end_lsn = InvalidXLogRecPtr;
	TransactionId polar_primary_next_xid;
	uint32		polar_primary_epoch;
	XLogRecPtr	polar_primary_last_lsn;
	XLogSegNo	polar_upstream_last_removed_segno;
	/* POLAR end */

	resetStringInfo(&incoming_message);

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	switch (type)
	{
		case 'w':				/* WAL records */
			{
				StringInfoData incoming_message;

				hdrlen = sizeof(int64) + sizeof(int64) + sizeof(int64);
				if (len < hdrlen)
					ereport(ERROR,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg_internal("invalid WAL message received from primary")));

				/* initialize a StringInfo with the given buffer */
				initReadOnlyStringInfo(&incoming_message, buf, hdrlen);

				/* read the fields */
				dataStart = pq_getmsgint64(&incoming_message);
				walEnd = pq_getmsgint64(&incoming_message);
				sendTime = pq_getmsgint64(&incoming_message);
				ProcessWalSndrMessage(walEnd, sendTime);

				buf += hdrlen;
				len -= hdrlen;
				XLogWalRcvWrite(buf, len, dataStart, tli);
				break;
			}
		case 'k':				/* Keepalive */
			{
				StringInfoData incoming_message;

				hdrlen = sizeof(int64) + sizeof(int64) + sizeof(char);
				if (len != hdrlen)
					ereport(ERROR,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg_internal("invalid keepalive message received from primary")));

				/* initialize a StringInfo with the given buffer */
				initReadOnlyStringInfo(&incoming_message, buf, hdrlen);

				/* read the fields */
				walEnd = pq_getmsgint64(&incoming_message);
				sendTime = pq_getmsgint64(&incoming_message);
				replyRequested = pq_getmsgbyte(&incoming_message);

				ProcessWalSndrMessage(walEnd, sendTime);

				/* If the primary requested a reply, send one immediately */
				if (replyRequested)
					XLogWalRcvSendReply(true, false);
				break;
			}
		case 'p':				/* polardb lsn */
			{
				/*
				 * POLAR: replicamode
				 * Does not contain any wal data
				 * copy message to StringInfo
				 */
				hdrlen = sizeof(int64) + sizeof(int64) + sizeof(int64) + sizeof(int64);
				if (len < hdrlen)
					ereport(ERROR,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg_internal("invalid consist lsn message received from primary")));
				appendBinaryStringInfo(&incoming_message, buf, hdrlen);

				dataStart = pq_getmsgint64(&incoming_message);
				walEnd = pq_getmsgint64(&incoming_message);
				curr_primary_consist_lsn_new = pq_getmsgint64(&incoming_message);
				sendTime = pq_getmsgint64(&incoming_message);
				ProcessWalSndrMessage(walEnd, sendTime);

				if (WalRcv->polar_use_xlog_queue)
				{
					Assert(POLAR_LOGINDEX_ENABLE_XLOG_QUEUE());

					polar_xlog_recv_queue_push_storage_begin(polar_logindex_redo_instance->xlog_queue, polar_recv_push_storage_begin_callback);

					SpinLockAcquire(&WalRcv->mutex);
					WalRcv->polar_use_xlog_queue = false;
					SpinLockRelease(&WalRcv->mutex);

					elog(LOG, "master xlog queue is full, changed to send from file");
				}

				/*
				 * POLAR: As a polardb replica, we do not write the xlog,
				 * we just update the LogstreamResult.write and call XLogWalRcvFlush
				 * to update shared-memory status as the case 'w'.
				 */
				LogstreamResult.Write = walEnd;
				XLogWalRcvFlush(false);

				/* POLAR: Update consistent lsn */
				polar_set_primary_consistent_lsn(curr_primary_consist_lsn_new);

				if (polar_enable_debug)
				{
					elog(LOG, "Receive primary on polardb flush xlog from %X/%X to %X/%X, ",
									(uint32) (dataStart >> 32), (uint32) dataStart,
									(uint32) (walEnd >> 32), (uint32) walEnd);
				}
				break;
			}
		/* POLAR: streaming xlog meta */
		case 'y':
			{
				/* POLAR: copy message to StringInfo */
				hdrlen = sizeof(int64) + sizeof(int64) + sizeof(int64);
				if (len < hdrlen)
					ereport(ERROR,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg_internal("invalid WAL message received from primary")));
				appendBinaryStringInfo(&incoming_message, buf, hdrlen);

				/* POLAR: read the fields */
				walEnd = pq_getmsgint64(&incoming_message);
				curr_primary_consist_lsn_new = pq_getmsgint64(&incoming_message);
				sendTime = pq_getmsgint64(&incoming_message);
				ProcessWalSndrMessage(walEnd, sendTime);

				buf += hdrlen;
				len -= hdrlen;

				if (len > 0)
				{
					polar_xlog_recv_queue_push(polar_logindex_redo_instance->xlog_queue, buf, len,
							polar_receiver_xlog_queue_callback);

					if (!WalRcv->polar_use_xlog_queue)
					{
						SpinLockAcquire(&WalRcv->mutex);
						WalRcv->polar_use_xlog_queue = true;
						SpinLockRelease(&WalRcv->mutex);

						elog(LOG, "master send data changed from file to queue");
					}
				}

				/* POLAR: As a polardb replica, we do not write the xlog,
				 * we just update the LogstreamResult.write and call XLogWalRcvFlush
				 * to update shared-memory status as the case 'w'.
				 */
				LogstreamResult.Write = walEnd;
				XLogWalRcvFlush(false);

				/* POLAR: Update new consistent lsn */
				polar_set_primary_consistent_lsn(curr_primary_consist_lsn_new);

				if (polar_enable_debug)
				{
					elog(LOG, "Receive XLOG without payload, and end lsn is %X/%X",
							(uint32)(walEnd >> 32), (uint32)walEnd);
				}
				break;
			}
		/* POLAR: keepalive with consistent lsn */
		case 'K':
			{
				/* POLAR: Copy message to StringInfo */
				hdrlen = sizeof(int64) + sizeof(int64) + sizeof(int64) + sizeof(char);
				if (len != hdrlen)
				{
					ereport(ERROR,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
									errmsg_internal("invalid keepalive message received from primary")));
				}

				appendBinaryStringInfo(&incoming_message, buf, hdrlen);

				/* POLAR: Read the fields */
				walEnd = pq_getmsgint64(&incoming_message);
				curr_primary_consist_lsn_new = pq_getmsgint64(&incoming_message);
				sendTime = pq_getmsgint64(&incoming_message);
				replyRequested = pq_getmsgbyte(&incoming_message);

				ProcessWalSndrMessage(walEnd, sendTime);

				/*
				 * POLAR: As a polardb replica, we do not write the xlog,
				 * we just update the LogstreamResult.write and call XLogWalRcvFlush
				 * to update shared-memory status as the case 'w'.
				 */
				LogstreamResult.Write = walEnd;
				XLogWalRcvFlush(false);

				/* POLAR: Update consistent lsn */
				polar_set_primary_consistent_lsn(curr_primary_consist_lsn_new);

				if (polar_enable_debug)
				{
					elog(LOG, "Receive primary on polardb keepalive with walEnd %X/%X and consistent lsn %X/%X",
						 (uint32) (walEnd >> 32), (uint32) walEnd,
						 (uint32) (curr_primary_consist_lsn_new >> 32), (uint32) curr_primary_consist_lsn_new);
				}

				/* POLAR: If the primary requested a reply, send one immediately */
				if (replyRequested)
					XLogWalRcvSendReply(true, false);
				break;
			}
		/* POLAR: endlsn from walsender, when receivedlsn = endlsn, promote is ready */
		case 'l':
			{
				hdrlen = sizeof(char) + sizeof(int64);
				if (len != hdrlen)
					ereport(ERROR,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg_internal("invalid endlsn message received from primary")));
				appendBinaryStringInfo(&incoming_message, buf, hdrlen);
			
				/* read the fields */
				is_promote_allowed = pq_getmsgbyte(&incoming_message);
				end_lsn = pq_getmsgint64(&incoming_message);
				elog(LOG,"received reply from walsender, is_promote_allowed:%d, end_lsn:%lx", is_promote_allowed, end_lsn);
				polar_process_walsender_reply(is_promote_allowed, end_lsn);
				break;
			}
		/* 
		 * POLAR: with next_xid and epoch of primary, needed when feedback standby xmin in datamax mode 
		 * with last_valid_lsn of primary, used to keep xlog consistency 
		 * also with last_removed_segno of primary, used to keep wal file those haven't been removed in primary
		 */
		case 'e':
			{
				/* copy message to StringInfo */
				hdrlen = sizeof(int64) + sizeof(int64) + sizeof(int32) + sizeof(int32) + sizeof(int64) + sizeof(int64) + sizeof(int64);
				if (len < hdrlen)
				/*no cover begin*/
					ereport(ERROR,
							(errcode(ERRCODE_PROTOCOL_VIOLATION),
							 errmsg_internal("invalid WAL message received from primary")));
				/*no cover end*/
				appendBinaryStringInfo(&incoming_message, buf, hdrlen);

				/* read the fields */
				dataStart = pq_getmsgint64(&incoming_message);
				walEnd = pq_getmsgint64(&incoming_message);
				polar_primary_next_xid = pq_getmsgint(&incoming_message, 4);
				polar_primary_epoch = pq_getmsgint(&incoming_message, 4);
				polar_primary_last_lsn = pq_getmsgint64(&incoming_message);
				polar_upstream_last_removed_segno = pq_getmsgint64(&incoming_message);
				sendTime = pq_getmsgint64(&incoming_message);
				ProcessWalSndrMessage(walEnd, sendTime);

				/* record next_xid and epoch of primary */
				POLAR_DATAMAX_SET_PRIMARY_NEXTXID(polar_primary_next_xid);
				POLAR_DATAMAX_SET_PRIMARY_NEXTEPOCH(polar_primary_epoch);
				/* record polar_primary_last_lsn into list */
				if (!POLAR_ENABLE_DMA())
					polar_datamax_insert_last_valid_lsn(polar_datamax_received_valid_lsn_list, polar_primary_last_lsn);
				/* record polar_upstream_last_removed_segno */
				polar_datamax_update_upstream_last_removed_segno(polar_datamax_ctl, polar_upstream_last_removed_segno);

				buf += hdrlen;
				len -= hdrlen;
				XLogWalRcvWrite(buf, len, dataStart);

				/* init timeline id and lsn if intial datamax */
				if (unlikely(polar_is_initial_datamax))
				{
					/* POLAR: init timeline and lsn */
					polar_datamax_update_min_received_info(polar_datamax_ctl, ThisTimeLineID, dataStart);
					/* POLAR: write meta to storage */
					polar_datamax_write_meta(polar_datamax_ctl, true);

					polar_is_initial_datamax = false;
				}
				break;
			}
			/* POLAR: 'M' means cluster info */
		case 'M':
			{
				/* copy message to StringInfo */
				appendBinaryStringInfo(&incoming_message, buf, len);
				polar_process_cluster_info(&incoming_message);
				break;
			}
		/* POLAR end */
		default:
			ereport(ERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg_internal("invalid replication message type %d",
									 type)));
	}
}

/*
 * Write XLOG data to disk.
 */
static void
XLogWalRcvWrite(char *buf, Size nbytes, XLogRecPtr recptr, TimeLineID tli)
{
	int			startoff;
	int			byteswritten;

	Assert(tli != 0);

	while (nbytes > 0)
	{
		int			segbytes;

		/* Close the current segment if it's completed */
		if (recvFile >= 0 && !XLByteInSeg(recptr, recvSegNo, wal_segment_size))
			XLogWalRcvClose(recptr, tli);

		if (recvFile < 0)
		{
<<<<<<< HEAD
			bool		use_existent;

			/*
			 * fsync() and close current file before we switch to next one. We
			 * would otherwise have to reopen this file to fsync it later
			 */
			if (recvFile >= 0)
			{
				char		xlogfname[MAXFNAMELEN];

				XLogWalRcvFlush(false);

				/*
				 * XLOG segment files will be re-read by recovery in startup
				 * process soon, so we don't advise the OS to release cache
				 * pages associated with the file like XLogFileClose() does.
				 */
				if (polar_close(recvFile) != 0)
					ereport(PANIC,
							(errcode_for_file_access(),
							 errmsg("could not close log segment %s: %m",
									XLogFileNameP(recvFileTLI, recvSegNo))));

				/*
				 * Create .done file forcibly to prevent the streamed segment
				 * from being archived later.
				 */
				XLogFileName(xlogfname, recvFileTLI, recvSegNo, wal_segment_size);
				polar_is_datamax_mode = polar_is_datamax();		/* POLAR: Enter datamax Mode. */
				if (XLogArchiveMode != ARCHIVE_MODE_ALWAYS)
					XLogArchiveForceDone(xlogfname);
				else if (POLAR_ENABLE_DMA())
					polar_dma_xlog_archive_notify(xlogfname, true);
				else
					XLogArchiveNotify(xlogfname);
				polar_is_datamax_mode = false;				/* POLAR: Leave datamax mode. */
			}
			recvFile = -1;

			/* Create/use new log file */
			XLByteToSeg(recptr, recvSegNo, wal_segment_size);
			use_existent = true;
			polar_is_datamax_mode = polar_is_datamax();		/* POLAR: Enter datamax Mode. */
			recvFile = XLogFileInit(recvSegNo, &use_existent, true);
			polar_is_datamax_mode = false;				/* POLAR: Leave datamax mode. */
			recvFileTLI = ThisTimeLineID;
			recvOff = 0;
=======
			/* Create/use new log file */
			XLByteToSeg(recptr, recvSegNo, wal_segment_size);
			recvFile = XLogFileInit(recvSegNo, tli);
			recvFileTLI = tli;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}

		/* Calculate the start offset of the received logs */
		startoff = XLogSegmentOffset(recptr, wal_segment_size);

		if (startoff + nbytes > wal_segment_size)
			segbytes = wal_segment_size - startoff;
		else
			segbytes = nbytes;

<<<<<<< HEAD
		/* Need to seek in the file? */
		if (recvOff != startoff)
		{
			if (polar_lseek(recvFile, (off_t) startoff, SEEK_SET) < 0)
				ereport(PANIC,
						(errcode_for_file_access(),
						 errmsg("could not seek in log segment %s to offset %u: %m",
								XLogFileNameP(recvFileTLI, recvSegNo),
								startoff)));
			recvOff = startoff;
		}

		/* OK to write the logs */
		errno = 0;

		byteswritten = polar_write(recvFile, buf, segbytes);
=======
		/* OK to write the logs */
		errno = 0;

		byteswritten = pg_pwrite(recvFile, buf, segbytes, (off_t) startoff);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		if (byteswritten <= 0)
		{
			char		xlogfname[MAXFNAMELEN];
			int			save_errno;

			/* if write didn't set errno, assume no disk space */
			if (errno == 0)
				errno = ENOSPC;

			save_errno = errno;
			XLogFileName(xlogfname, recvFileTLI, recvSegNo, wal_segment_size);
			errno = save_errno;
			ereport(PANIC,
					(errcode_for_file_access(),
					 errmsg("could not write to WAL segment %s "
							"at offset %d, length %lu: %m",
							xlogfname, startoff, (unsigned long) segbytes)));
		}

		/* Update state for write */
		recptr += byteswritten;

		nbytes -= byteswritten;
		buf += byteswritten;

		LogstreamResult.Write = recptr;
	}

	/* Update shared-memory status */
	pg_atomic_write_u64(&WalRcv->writtenUpto, LogstreamResult.Write);

	/*
	 * Close the current segment if it's fully written up in the last cycle of
	 * the loop, to create its archive notification file soon. Otherwise WAL
	 * archiving of the segment will be delayed until any data in the next
	 * segment is received and written.
	 */
	if (recvFile >= 0 && !XLByteInSeg(recptr, recvSegNo, wal_segment_size))
		XLogWalRcvClose(recptr, tli);
}

/*
 * Flush the log to disk.
 *
 * If we're in the midst of dying, it's unwise to do anything that might throw
 * an error, so we skip sending a reply in that case.
 */
static void
XLogWalRcvFlush(bool dying, TimeLineID tli)
{
	Assert(tli != 0);

	if (LogstreamResult.Flush < LogstreamResult.Write)
	{
		WalRcvData *walrcv = WalRcv;

<<<<<<< HEAD
		/* POLAR */
		XLogRecPtr curr_primary_consistent_lsn = InvalidXLogRecPtr;

		/* POLAR: only replica mode not write data */
		if (polar_in_replica_mode() == false)
			issue_xlog_fsync(recvFile, recvSegNo);
=======
		issue_xlog_fsync(recvFile, recvSegNo, tli);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

		LogstreamResult.Flush = LogstreamResult.Write;

		/* Update shared-memory status */
		SpinLockAcquire(&walrcv->mutex);
		if (walrcv->flushedUpto < LogstreamResult.Flush)
		{
<<<<<<< HEAD
			walrcv->latestChunkStart = walrcv->receivedUpto;
			walrcv->receivedUpto = LogstreamResult.Flush;
			walrcv->receivedTLI = ThisTimeLineID;
			curr_primary_consistent_lsn = walrcv->curr_primary_consistent_lsn;
=======
			walrcv->latestChunkStart = walrcv->flushedUpto;
			walrcv->flushedUpto = LogstreamResult.Flush;
			walrcv->receivedTLI = tli;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}
		SpinLockRelease(&walrcv->mutex);

		/* POLAR: update lastet flush lsn */
		pg_atomic_write_u64(&WalRcv->polar_latest_flush_lsn, LogstreamResult.Flush);
		/* POLAR end */

		/* POLAR: update received WAL lsn */
		if (POLAR_ENABLE_DMA())
			ConsensusSetXLogFlushedLSN(LogstreamResult.Flush, ThisTimeLineID, false);	
		else if (polar_is_datamax())
		{
			polar_datamax_update_received_info(
					polar_datamax_ctl, ThisTimeLineID, LogstreamResult.Flush);
			/* update last received valid lsn */
			polar_datamax_update_cur_valid_lsn(polar_datamax_received_valid_lsn_list, LogstreamResult.Flush);
			polar_datamax_write_meta(polar_datamax_ctl, true);
		}
		/* POLAR end */

		/* Signal the startup process and walsender that new WAL has arrived */
		WakeupRecovery();
		if (AllowCascadeReplication())
			WalSndWakeup(true, false);

		/* POLAR: wakeup the fullpage process that new WAL has arrived */
		if (POLAR_LOGINDEX_ENABLE_FULLPAGE())
			polar_fullpage_bgworker_wakeup(polar_logindex_redo_instance->fullpage_ctl);

		/* Report XLOG streaming progress in PS display */
		if (update_process_title)
		{
			char		activitymsg[MAX_REPLICATION_PS_BUFFER_SIZE] = {0};

<<<<<<< HEAD
			/* POLAR */
			if (polar_in_replica_mode())
			{
				snprintf(activitymsg,
						 sizeof(activitymsg),
						 "streaming %X/%X, consistent lsn %X/%X",
						 (uint32) (LogstreamResult.Write >> 32),
						 (uint32) LogstreamResult.Write,
						 (uint32) (curr_primary_consistent_lsn >> 32),
						 (uint32) curr_primary_consistent_lsn);
			}
			else
			{
				snprintf(activitymsg,
						 sizeof(activitymsg),
						 "streaming %X/%X",
						 (uint32) (LogstreamResult.Write >> 32),
						 (uint32) LogstreamResult.Write);
			}
			set_ps_display(activitymsg, false);
=======
			snprintf(activitymsg, sizeof(activitymsg), "streaming %X/%X",
					 LSN_FORMAT_ARGS(LogstreamResult.Write));
			set_ps_display(activitymsg);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}

		/* Also let the primary know that we made some progress */
		if (!dying)
		{
			XLogWalRcvSendReply(false, false);
			XLogWalRcvSendHSFeedback(false);
		}
	}
}

/*
 * Close the current segment.
 *
 * Flush the segment to disk before closing it. Otherwise we have to
 * reopen and fsync it later.
 *
 * Create an archive notification file since the segment is known completed.
 */
static void
XLogWalRcvClose(XLogRecPtr recptr, TimeLineID tli)
{
	char		xlogfname[MAXFNAMELEN];

	Assert(recvFile >= 0 && !XLByteInSeg(recptr, recvSegNo, wal_segment_size));
	Assert(tli != 0);

	/*
	 * fsync() and close current file before we switch to next one. We would
	 * otherwise have to reopen this file to fsync it later
	 */
	XLogWalRcvFlush(false, tli);

	XLogFileName(xlogfname, recvFileTLI, recvSegNo, wal_segment_size);

	/*
	 * XLOG segment files will be re-read by recovery in startup process soon,
	 * so we don't advise the OS to release cache pages associated with the
	 * file like XLogFileClose() does.
	 */
	if (close(recvFile) != 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not close WAL segment %s: %m",
						xlogfname)));

	/*
	 * Create .done file forcibly to prevent the streamed segment from being
	 * archived later.
	 */
	if (XLogArchiveMode != ARCHIVE_MODE_ALWAYS)
		XLogArchiveForceDone(xlogfname);
	else
		XLogArchiveNotify(xlogfname);

	recvFile = -1;
}

/*
 * Send reply message to primary, indicating our current WAL locations, oldest
 * xmin and the current time.
 *
 * If 'force' is not set, the message is only sent if enough time has
 * passed since last status update to reach wal_receiver_status_interval.
 * If wal_receiver_status_interval is disabled altogether and 'force' is
 * false, this is a no-op.
 *
 * If 'requestReply' is true, requests the server to reply immediately upon
 * receiving this message. This is used for heartbeats, when approaching
 * wal_receiver_timeout.
 */
static void
XLogWalRcvSendReply(bool force, bool requestReply)
{
	static XLogRecPtr writePtr = 0;
	static XLogRecPtr flushPtr = 0;
	XLogRecPtr	applyPtr;
	TimestampTz now;

	/* POLAR: */
	XLogRecPtr polar_lock_replay_lsn = 0;
	/* POLAR end */

	/*
	 * If the user doesn't want status to be reported to the primary, be sure
	 * to exit before doing anything at all.
	 */
	if (!force && wal_receiver_status_interval <= 0)
		return;

	/* Get current timestamp. */
	now = GetCurrentTimestamp();

	/*
	 * We can compare the write and flush positions to the last message we
	 * sent without taking any lock, but the apply position requires a spin
	 * lock, so we don't check that unless something else has changed or 10
	 * seconds have passed.  This means that the apply WAL location will
	 * appear, from the primary's point of view, to lag slightly, but since
	 * this is only for reporting purposes and only on idle systems, that's
	 * probably OK.
	 */
	if (!force
		&& writePtr == LogstreamResult.Write
		&& flushPtr == LogstreamResult.Flush
		&& now < wakeup[WALRCV_WAKEUP_REPLY])
		return;

	/* Make sure we wake up when it's time to send another reply. */
	WalRcvComputeNextWakeup(WALRCV_WAKEUP_REPLY, now);

	/* Construct a new message */
	writePtr = LogstreamResult.Write;
	flushPtr = LogstreamResult.Flush;
	applyPtr = polar_is_datamax() ? LogstreamResult.Flush : GetXLogReplayRecPtr(NULL);

	resetStringInfo(&reply_message);
	pq_sendbyte(&reply_message, 'r');
	pq_sendint64(&reply_message, writePtr);
	pq_sendint64(&reply_message, flushPtr);
	pq_sendint64(&reply_message, applyPtr);
	pq_sendint64(&reply_message, GetCurrentTimestamp());
	pq_sendbyte(&reply_message, requestReply ? 1 : 0);

	if (polar_in_replica_mode())
	{
		static XLogRecPtr bg_replayed_lsn = InvalidXLogRecPtr;

		Assert(!XLogRecPtrIsInvalid(applyPtr));

		/* POLAR: return the oldest ddl lock lsn if enable async ddl lock */
		if (polar_allow_async_ddl_lock_replay())
			polar_lock_replay_lsn = polar_get_async_lock_replay_rec_ptr();
		else
			polar_lock_replay_lsn = applyPtr;

		pq_sendint64(&reply_message, polar_lock_replay_lsn);
		elog(DEBUG3, "polar_lock_replay_lsn: %X/%X", (uint32) (polar_lock_replay_lsn >> 32), (uint32) polar_lock_replay_lsn);

		/* POLAR: useless message, original value of polar_ro_is_invalid, keep it for upgrade compatibility */
		pq_sendbyte(&reply_message, 0);

		/* 
		 * POLAR: Send background replay lsn. Even if page outdate is disabled, 
		 * it also send a lsn to keep protocol compatibility.
		 */
		if (polar_in_replica_mode() && POLAR_LOGINDEX_ENABLE_XLOG_QUEUE())
		{
			static TimestampTz last_update_time = 0;
			static XLogRecPtr  last_bg_replayed_lsn = InvalidXLogRecPtr;
			TimestampTz now;

			now = GetCurrentTimestamp();
			/* 
			 * POLAR: Page replay in backend process need xlog after consistent lsn, 
			 * so we should keep xlog after consisten lsn. To avoid holding ProcArrayLock
			 * too frequently, we call polar_get_read_min_lsn() every second.
			 */
			if (TimestampDifferenceExceeds(last_update_time, now, POLAR_UPDATE_BACKEND_LSN_INTERVAL))
			{
				last_bg_replayed_lsn = polar_get_read_min_lsn(polar_get_primary_consist_ptr());
				last_update_time = now;
			}
			bg_replayed_lsn = last_bg_replayed_lsn;
		}
		else
			bg_replayed_lsn = InvalidXLogRecPtr;
		pq_sendint64(&reply_message, bg_replayed_lsn);
	}

	/* Send it */
	elog(DEBUG2, "sending write %X/%X flush %X/%X apply %X/%X%s",
		 LSN_FORMAT_ARGS(writePtr),
		 LSN_FORMAT_ARGS(flushPtr),
		 LSN_FORMAT_ARGS(applyPtr),
		 requestReply ? " (reply requested)" : "");

	walrcv_send(wrconn, reply_message.data, reply_message.len);
}

/*
 * Send hot standby feedback message to primary, plus the current time,
 * in case they don't have a watch.
 *
 * If the user disables feedback, send one final message to tell sender
 * to forget about the xmin on this standby. We also send this message
 * on first connect because a previous connection might have set xmin
 * on a replication slot. (If we're not using a slot it's harmless to
 * send a feedback message explicitly setting InvalidTransactionId).
 */
static void
XLogWalRcvSendHSFeedback(bool immed)
{
	TimestampTz now;
	FullTransactionId nextFullXid;
	TransactionId nextXid;
	uint32		xmin_epoch,
				catalog_xmin_epoch;
	TransactionId xmin,
				catalog_xmin;

	/* initially true so we always send at least one feedback message */
	static bool primary_has_standby_xmin = true;

	/*
	 * If the user doesn't want status to be reported to the primary, be sure
	 * to exit before doing anything at all.
	 * POLAR: set polar_standby_feedback to false to disable hot_standby_feedback
	 * in polar standby
	 */
<<<<<<< HEAD
	if ((wal_receiver_status_interval <= 0 ||
		!POLAR_ENABLE_FEEDBACK()) &&
		!master_has_standby_xmin)
=======
	if ((wal_receiver_status_interval <= 0 || !hot_standby_feedback) &&
		!primary_has_standby_xmin)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		return;

	/* Get current timestamp. */
	now = GetCurrentTimestamp();

	/* Send feedback at most once per wal_receiver_status_interval. */
	if (!immed && now < wakeup[WALRCV_WAKEUP_HSFEEDBACK])
		return;

	/* Make sure we wake up when it's time to send feedback again. */
	WalRcvComputeNextWakeup(WALRCV_WAKEUP_HSFEEDBACK, now);

	/*
	 * If Hot Standby is not yet accepting connections there is nothing to
	 * send. Check this after the interval has expired to reduce number of
	 * calls.
	 *
	 * Bailing out here also ensures that we don't send feedback until we've
	 * read our own replication slot state, so we don't tell the primary to
	 * discard needed xmin or catalog_xmin from any slots that may exist on
	 * this replica.
	 */
	/* POLAR: Allow to send feedback in datamax mode */
	if (!HotStandbyActive() && !polar_is_datamax())
		return;

	/*
	 * Make the expensive call to get the oldest xmin once we are certain
	 * everything else has been checked.
	 * POLAR: set polar_standby_feedback to true to enable hot_standby_feedback
	 * in polar standby
	 */
	if (POLAR_ENABLE_FEEDBACK())
	{
<<<<<<< HEAD
		TransactionId slot_xmin;

		/*
		 * Usually GetOldestXmin() would include both global replication slot
		 * xmin and catalog_xmin in its calculations, but we want to derive
		 * separate values for each of those. So we ask for an xmin that
		 * excludes the catalog_xmin.
		 */
		xmin = GetOldestXmin(NULL,
							 PROCARRAY_FLAGS_DEFAULT | PROCARRAY_SLOTS_XMIN);

		ProcArrayGetReplicationSlotXmin(&slot_xmin, &catalog_xmin);

		if (TransactionIdIsValid(slot_xmin) &&
			TransactionIdPrecedes(slot_xmin, xmin))
			xmin = slot_xmin;
		
		/* 
		 * POLAR: Don't consider oldestXmin in datamax mode
		 * otherwise when datamax_oldestXmin < slot_xmin, datamax_oldestXmin 
		 * will be sent to primary, which will infect the vacuum process of primary, 
		 * but primary only cares about the xmin of standby in fact
		 * datamax just records and sends them to primary
		 */
		if (polar_is_datamax())
			xmin = slot_xmin;
		/* POLAR end */
=======
		GetReplicationHorizons(&xmin, &catalog_xmin);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
	else
	{
		xmin = InvalidTransactionId;
		catalog_xmin = InvalidTransactionId;
	}

	/*
	 * Get epoch and adjust if nextXid and oldestXmin are different sides of
	 * the epoch boundary.
	 */
<<<<<<< HEAD
	/*
	 * POLAR: get nextXid and epoch from polar_datamax_ctl when in datamax mode
	 * we have checked the sanity of xmin feedbacked by standby in ProcessStandbyHSFeedbackMessage func
	 * and there is no primary's data in datamax node
	 * so we can feedback the epoch according to primary's epoch recorded in polar_datamax_ctl
	 */
	if (!polar_is_datamax())
		GetNextXidAndEpoch(&nextXid, &xmin_epoch);
	else
	{
		nextXid = pg_atomic_read_u32(&polar_datamax_ctl->polar_primary_next_xid);
		xmin_epoch = pg_atomic_read_u32(&polar_datamax_ctl->polar_primary_epoch);
	}
	/* POLAR end */
=======
	nextFullXid = ReadNextFullTransactionId();
	nextXid = XidFromFullTransactionId(nextFullXid);
	xmin_epoch = EpochFromFullTransactionId(nextFullXid);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	catalog_xmin_epoch = xmin_epoch;
	if (nextXid < xmin)
		xmin_epoch--;
	if (nextXid < catalog_xmin)
		catalog_xmin_epoch--;

	elog(DEBUG2, "sending hot standby feedback xmin %u epoch %u catalog_xmin %u catalog_xmin_epoch %u",
		 xmin, xmin_epoch, catalog_xmin, catalog_xmin_epoch);

	/* Construct the message and send it. */
	resetStringInfo(&reply_message);
	pq_sendbyte(&reply_message, 'h');
	pq_sendint64(&reply_message, GetCurrentTimestamp());
	pq_sendint32(&reply_message, xmin);
	pq_sendint32(&reply_message, xmin_epoch);
	pq_sendint32(&reply_message, catalog_xmin);
	pq_sendint32(&reply_message, catalog_xmin_epoch);
	walrcv_send(wrconn, reply_message.data, reply_message.len);
	if (TransactionIdIsValid(xmin) || TransactionIdIsValid(catalog_xmin))
		primary_has_standby_xmin = true;
	else
		primary_has_standby_xmin = false;
}

/*
 * Update shared memory status upon receiving a message from primary.
 *
 * 'walEnd' and 'sendTime' are the end-of-WAL and timestamp of the latest
 * message, reported by primary.
 */
static void
ProcessWalSndrMessage(XLogRecPtr walEnd, TimestampTz sendTime)
{
	WalRcvData *walrcv = WalRcv;
	TimestampTz lastMsgReceiptTime = GetCurrentTimestamp();

	/* Update shared-memory status */
	SpinLockAcquire(&walrcv->mutex);
	if (walrcv->latestWalEnd < walEnd)
		walrcv->latestWalEndTime = sendTime;
	walrcv->latestWalEnd = walEnd;
	walrcv->lastMsgSendTime = sendTime;
	walrcv->lastMsgReceiptTime = lastMsgReceiptTime;
	SpinLockRelease(&walrcv->mutex);

	if (message_level_is_interesting(DEBUG2))
	{
		char	   *sendtime;
		char	   *receipttime;
		int			applyDelay;

		/* Copy because timestamptz_to_str returns a static buffer */
		sendtime = pstrdup(timestamptz_to_str(sendTime));
		receipttime = pstrdup(timestamptz_to_str(lastMsgReceiptTime));
		applyDelay = GetReplicationApplyDelay();

		/* apply delay is not available */
		if (applyDelay == -1)
			elog(DEBUG2, "sendtime %s receipttime %s replication apply delay (N/A) transfer latency %d ms",
				 sendtime,
				 receipttime,
				 GetReplicationTransferLatency());
		else
			elog(DEBUG2, "sendtime %s receipttime %s replication apply delay %d ms transfer latency %d ms",
				 sendtime,
				 receipttime,
				 applyDelay,
				 GetReplicationTransferLatency());

		pfree(sendtime);
		pfree(receipttime);
	}
}

/*
 * Compute the next wakeup time for a given wakeup reason.  Can be called to
 * initialize a wakeup time, to adjust it for the next wakeup, or to
 * reinitialize it when GUCs have changed.  We ask the caller to pass in the
 * value of "now" because this frequently avoids multiple calls of
 * GetCurrentTimestamp().  It had better be a reasonably up-to-date value
 * though.
 */
static void
WalRcvComputeNextWakeup(WalRcvWakeupReason reason, TimestampTz now)
{
	switch (reason)
	{
		case WALRCV_WAKEUP_TERMINATE:
			if (wal_receiver_timeout <= 0)
				wakeup[reason] = TIMESTAMP_INFINITY;
			else
				wakeup[reason] = TimestampTzPlusMilliseconds(now, wal_receiver_timeout);
			break;
		case WALRCV_WAKEUP_PING:
			if (wal_receiver_timeout <= 0)
				wakeup[reason] = TIMESTAMP_INFINITY;
			else
				wakeup[reason] = TimestampTzPlusMilliseconds(now, wal_receiver_timeout / 2);
			break;
		case WALRCV_WAKEUP_HSFEEDBACK:
			if (!hot_standby_feedback || wal_receiver_status_interval <= 0)
				wakeup[reason] = TIMESTAMP_INFINITY;
			else
				wakeup[reason] = TimestampTzPlusSeconds(now, wal_receiver_status_interval);
			break;
		case WALRCV_WAKEUP_REPLY:
			if (wal_receiver_status_interval <= 0)
				wakeup[reason] = TIMESTAMP_INFINITY;
			else
				wakeup[reason] = TimestampTzPlusSeconds(now, wal_receiver_status_interval);
			break;
			/* there's intentionally no default: here */
	}
}

/*
 * Wake up the walreceiver main loop.
 *
 * This is called by the startup process whenever interesting xlog records
 * are applied, so that walreceiver can check if it needs to send an apply
 * notification back to the primary which may be waiting in a COMMIT with
 * synchronous_commit = remote_apply.
 */
void
WalRcvForceReply(void)
{
	Latch	   *latch;

	WalRcv->force_reply = true;
	/* fetching the latch pointer might not be atomic, so use spinlock */
	SpinLockAcquire(&WalRcv->mutex);
	latch = WalRcv->latch;
	SpinLockRelease(&WalRcv->mutex);
	if (latch)
		SetLatch(latch);
}

/*
 * Return a string constant representing the state. This is used
 * in system functions and views, and should *not* be translated.
 */
static const char *
WalRcvGetStateString(WalRcvState state)
{
	switch (state)
	{
		case WALRCV_STOPPED:
			return "stopped";
		case WALRCV_STARTING:
			return "starting";
		case WALRCV_STREAMING:
			return "streaming";
		case WALRCV_WAITING:
			return "waiting";
		case WALRCV_RESTARTING:
			return "restarting";
		case WALRCV_STOPPING:
			return "stopping";
	}
	return "UNKNOWN";
}

/*
 * Returns activity of WAL receiver, including pid, state and xlog locations
 * received from the WAL sender of another server.
 */
Datum
pg_stat_get_wal_receiver(PG_FUNCTION_ARGS)
{
	TupleDesc	tupdesc;
	Datum	   *values;
	bool	   *nulls;
	int			pid;
	bool		ready_to_display;
	WalRcvState state;
	XLogRecPtr	receive_start_lsn;
	TimeLineID	receive_start_tli;
	XLogRecPtr	written_lsn;
	XLogRecPtr	flushed_lsn;
	TimeLineID	received_tli;
	TimestampTz last_send_time;
	TimestampTz last_receipt_time;
	XLogRecPtr	latest_end_lsn;
	TimestampTz latest_end_time;
	char		sender_host[NI_MAXHOST];
	int			sender_port = 0;
	char		slotname[NAMEDATALEN];
	char		conninfo[MAXCONNINFO];

	/* Take a lock to ensure value consistency */
	SpinLockAcquire(&WalRcv->mutex);
	pid = (int) WalRcv->pid;
	ready_to_display = WalRcv->ready_to_display;
	state = WalRcv->walRcvState;
	receive_start_lsn = WalRcv->receiveStart;
	receive_start_tli = WalRcv->receiveStartTLI;
	flushed_lsn = WalRcv->flushedUpto;
	received_tli = WalRcv->receivedTLI;
	last_send_time = WalRcv->lastMsgSendTime;
	last_receipt_time = WalRcv->lastMsgReceiptTime;
	latest_end_lsn = WalRcv->latestWalEnd;
	latest_end_time = WalRcv->latestWalEndTime;
	strlcpy(slotname, (char *) WalRcv->slotname, sizeof(slotname));
	strlcpy(sender_host, (char *) WalRcv->sender_host, sizeof(sender_host));
	sender_port = WalRcv->sender_port;
	strlcpy(conninfo, (char *) WalRcv->conninfo, sizeof(conninfo));
	SpinLockRelease(&WalRcv->mutex);

	/*
	 * No WAL receiver (or not ready yet), just return a tuple with NULL
	 * values
	 */
	if (pid == 0 || !ready_to_display)
		PG_RETURN_NULL();

	/*
	 * Read "writtenUpto" without holding a spinlock.  Note that it may not be
	 * consistent with the other shared variables of the WAL receiver
	 * protected by a spinlock, but this should not be used for data integrity
	 * checks.
	 */
	written_lsn = pg_atomic_read_u64(&WalRcv->writtenUpto);

	/* determine result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	values = palloc0(sizeof(Datum) * tupdesc->natts);
	nulls = palloc0(sizeof(bool) * tupdesc->natts);

	/* Fetch values */
	values[0] = Int32GetDatum(pid);

	if (!has_privs_of_role(GetUserId(), ROLE_PG_READ_ALL_STATS))
	{
		/*
		 * Only superusers and roles with privileges of pg_read_all_stats can
		 * see details. Other users only get the pid value to know whether it
		 * is a WAL receiver, but no details.
		 */
		memset(&nulls[1], true, sizeof(bool) * (tupdesc->natts - 1));
	}
	else
	{
		values[1] = CStringGetTextDatum(WalRcvGetStateString(state));

		if (XLogRecPtrIsInvalid(receive_start_lsn))
			nulls[2] = true;
		else
			values[2] = LSNGetDatum(receive_start_lsn);
		values[3] = Int32GetDatum(receive_start_tli);
		if (XLogRecPtrIsInvalid(written_lsn))
			nulls[4] = true;
		else
			values[4] = LSNGetDatum(written_lsn);
		if (XLogRecPtrIsInvalid(flushed_lsn))
			nulls[5] = true;
		else
			values[5] = LSNGetDatum(flushed_lsn);
		values[6] = Int32GetDatum(received_tli);
		if (last_send_time == 0)
			nulls[7] = true;
		else
			values[7] = TimestampTzGetDatum(last_send_time);
		if (last_receipt_time == 0)
			nulls[8] = true;
		else
			values[8] = TimestampTzGetDatum(last_receipt_time);
		if (XLogRecPtrIsInvalid(latest_end_lsn))
			nulls[9] = true;
		else
			values[9] = LSNGetDatum(latest_end_lsn);
		if (latest_end_time == 0)
			nulls[10] = true;
		else
			values[10] = TimestampTzGetDatum(latest_end_time);
		if (*slotname == '\0')
			nulls[11] = true;
		else
			values[11] = CStringGetTextDatum(slotname);
		if (*sender_host == '\0')
			nulls[12] = true;
		else
			values[12] = CStringGetTextDatum(sender_host);
		if (sender_port == 0)
			nulls[13] = true;
		else
			values[13] = Int32GetDatum(sender_port);
		if (*conninfo == '\0')
			nulls[14] = true;
		else
			values[14] = CStringGetTextDatum(conninfo);
	}

	/* Returns the record as Datum */
	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}

/*
 * POLAR: This is callback function used when waiting free space from
 * polar_xlog_queue.It will send feedback and handle interrupts
 */
static void
polar_receiver_xlog_queue_callback(polar_ringbuf_t rbuf)
{
	ProcessWalRcvInterrupts();
	XLogWalRcvSendReply(false, false);
	XLogWalRcvSendHSFeedback(false);
}

static inline void
polar_recv_push_storage_begin_callback(polar_ringbuf_t rbuf)
{
	ProcessWalRcvInterrupts();
}

static void
polar_notify_read_wal_file(int code, Datum arg)
{
	/*
	 * POLAR: The wal receiver is exiting, tell startup to read from file if it want to read more xlog.
	 */
	if (!got_SIGTERM && polar_in_replica_mode() && POLAR_LOGINDEX_ENABLE_XLOG_QUEUE())
	{
		elog(LOG, "polar replica exit wal receiver and request to read from WAL file");
		polar_xlog_recv_queue_push_storage_begin(polar_logindex_redo_instance->xlog_queue, polar_recv_push_storage_begin_callback);
	}
}

/*
 * POLAR: get lastMsgReceiptTime
 */
TimestampTz
polar_get_walrcv_last_msg_receipt_time(void)
{
	WalRcvData *walrcv = WalRcv;
	TimestampTz last_msg_receipt_time = 0;

	SpinLockAcquire(&walrcv->mutex);
	last_msg_receipt_time = walrcv->lastMsgReceiptTime;
	SpinLockRelease(&walrcv->mutex);
	return last_msg_receipt_time;
}

/*
 * POLAR: Judge whether promote request is necessary to be sent to walsender
 * 1) if promote is triggered in current instance, send request when 
 * polar_enable_promote_wait_for_walreceive_done = on and WalRcv received promote trigger;
 * 2) if promote is triggered in downstream instance, send request when
 * WalSnd received promote trigger from downstream;
 * 3) at last, send request when we haven't received promote reply from walsender after timeout. 
 * 
 * Return true if it is necessary to send request to walsender.
 * Return false if it is unnecessary to send request to walsender.
 */
bool
polar_send_promote_request(void)
{
	static TimestampTz last_send_time = 0;
	
	/* walrcv already exists when call this func */
	if (((polar_enable_promote_wait_for_walreceive_done && POLAR_PROMOTE_IS_TRIGGERED()) || POLAR_WALSNDCTL_RECEIVE_PROMOTE_TRIGGER())
		&& !POLAR_PROMOTE_REPLY_IS_RECEIVED())
	{
		TimestampTz send_now = GetCurrentTimestamp();
		if(TimestampDifferenceExceeds(last_send_time, send_now, POLAR_SEND_PROMOTE_REQUEST_TIMEOUT))
		{
			last_send_time = send_now;
			return true;
		}
	}
	if (POLAR_PROMOTE_REPLY_IS_RECEIVED())
		last_send_time = 0;
	
	return false;
}

/* POLAR: send promote information to walsender */
void
polar_walrcv_send_promote(bool polar_request_reply)
{
	bool polar_promote_trigger = true;

	resetStringInfo(&reply_message);
	pq_sendbyte(&reply_message, 'p');
	pq_sendbyte(&reply_message, polar_promote_trigger);
	pq_sendbyte(&reply_message, polar_request_reply ? 1 : 0);
	elog(LOG,"send promote trigger %d, polar_request_reply:%d", polar_promote_trigger, polar_request_reply);
	walrcv_send(wrconn, reply_message.data, reply_message.len);
}

/* POLAR: process promote reply received from walsender */
void
polar_process_walsender_reply(bool is_promote_allowed, XLogRecPtr end_lsn)
{
	Assert(WalRcv);

	/* already receive and process reply */
	if (POLAR_PROMOTE_REPLY_IS_RECEIVED())
		return;
	else
	{
		/* promote is allowed */
		if (is_promote_allowed)
		{
			if (!XLogRecPtrIsInvalid(end_lsn))
				POLAR_SET_END_LSN(end_lsn);
		}
		/* disable promote */
		else
			POLAR_SET_PROMOTE_NOT_ALLOWED();

		/* having received reply from walsender, don't send promote request to walsender again */
		POLAR_SET_RECEIVE_PROMOTE_REPLY();	
		
		/* tell walsender we have received reply, so walsender won't send reply again */
		polar_walrcv_send_promote(false);
	}
}

/* POLAR: get end_lsn when received promote request from downstream */
XLogRecPtr
polar_promote_get_end_lsn(void)
{
	XLogRecPtr end_lsn = InvalidXLogRecPtr;

	end_lsn = pg_atomic_read_u64(&WalRcv->polar_latest_flush_lsn);
	/* polar_latest_flush_lsn is 0 when datamax/standby restart and no walrcvstream */
	if (XLogRecPtrIsInvalid(end_lsn))
	{
		if (polar_is_datamax())
			end_lsn = polar_datamax_get_last_received_lsn(polar_datamax_ctl, NULL);
		else
			end_lsn = GetXLogReplayRecPtr(NULL);
	}
	return end_lsn;
}

/* 
 * POLAR: judge whether upstream node state is alive via WalStreaming
 * return true when upstream node can be connected rightly, which is walreceiver is ready
 */
bool
polar_upstream_node_is_alive(void)
{
	int			pid = 0;
	bool		ready_to_display = false;

	Assert(WalRcv);

	SpinLockAcquire(&WalRcv->mutex);
	pid = (int) WalRcv->pid;
	ready_to_display = WalRcv->ready_to_display;
	SpinLockRelease(&WalRcv->mutex);

	return (pid != 0 && ready_to_display);
} 

/* 
 * POLAR: judge whether having received all wal
 * if so, set polar_is_promote_allowed = true indicates that promote can be executed now 
 */
void 
polar_promote_check_received_all_wal(void)
{
	Assert(WalRcv);
	if (!POLAR_IS_PROMOTE_NOT_ALLOWED() && 
		!POLAR_IS_END_LSN_INVALID() && 
		!POLAR_IS_PROMOTE_ALLOWED() &&
		pg_atomic_read_u64(&WalRcv->polar_end_lsn) == LogstreamResult.Flush)
	{
		elog(LOG,"polar_endlsn:%lx, flush_lsn:%lx, received all wal, promote is allowed", pg_atomic_read_u64(&WalRcv->polar_end_lsn), LogstreamResult.Flush);
		POLAR_SET_PROMOTE_ALLOWED();
	}
}
/* POLAR end */

/*
 * POLAR: return received LSN in DMA mode
 */
XLogRecPtr polar_dma_get_received_lsn(void)
{
	XLogRecPtr	receivePtr;
	TimeLineID	receiveTLI;

	ConsensusGetXLogFlushedLSN(&receivePtr, &receiveTLI);

	return receivePtr;
}
/* POLAR end */
