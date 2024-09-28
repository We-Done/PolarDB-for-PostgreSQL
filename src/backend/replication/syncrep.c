/*-------------------------------------------------------------------------
 *
 * syncrep.c
 *
 * Synchronous replication is new as of PostgreSQL 9.1.
 *
 * If requested, transaction commits wait until their commit LSN are
 * acknowledged by the synchronous standbys.
 *
 * This module contains the code for waiting and release of backends.
 * All code in this module executes on the primary. The core streaming
 * replication transport remains within WALreceiver/WALsender modules.
 *
 * The essence of this design is that it isolates all logic about
 * waiting/releasing onto the primary. The primary defines which standbys
 * it wishes to wait for. The standbys are completely unaware of the
 * durability requirements of transactions on the primary, reducing the
 * complexity of the code and streamlining both standby operations and
 * network bandwidth because there is no requirement to ship
 * per-transaction state information.
 *
 * Replication is either synchronous or not synchronous (async). If it is
 * async, we just fastpath out of here. If it is sync, then we wait for
 * the write, flush or apply location on the standby before releasing
 * the waiting backend. Further complexity in that interaction is
 * expected in later releases.
 *
 * The best performing way to manage the waiting backends is to have a
 * single ordered queue of waiting backends, so that we can avoid
 * searching the through all waiters each time we receive a reply.
 *
 * In 9.5 or before only a single standby could be considered as
 * synchronous. In 9.6 we support a priority-based multiple synchronous
 * standbys. In 10.0 a quorum-based multiple synchronous standbys is also
 * supported. The number of synchronous standbys that transactions
 * must wait for replies from is specified in synchronous_standby_names.
 * This parameter also specifies a list of standby names and the method
 * (FIRST and ANY) to choose synchronous standbys from the listed ones.
 *
 * The method FIRST specifies a priority-based synchronous replication
 * and makes transaction commits wait until their WAL records are
 * replicated to the requested number of synchronous standbys chosen based
 * on their priorities. The standbys whose names appear earlier in the list
 * are given higher priority and will be considered as synchronous.
 * Other standby servers appearing later in this list represent potential
 * synchronous standbys. If any of the current synchronous standbys
 * disconnects for whatever reason, it will be replaced immediately with
 * the next-highest-priority standby.
 *
 * The method ANY specifies a quorum-based synchronous replication
 * and makes transaction commits wait until their WAL records are
 * replicated to at least the requested number of synchronous standbys
 * in the list. All the standbys appearing in the list are considered as
 * candidates for quorum synchronous standbys.
 *
 * If neither FIRST nor ANY is specified, FIRST is used as the method.
 * This is for backward compatibility with 9.6 or before where only a
 * priority-based sync replication was supported.
 *
 * Before the standbys chosen from synchronous_standby_names can
 * become the synchronous standbys they must have caught up with
 * the primary; that may take some time. Once caught up,
 * the standbys which are considered as synchronous at that moment
 * will release waiters from the queue.
 *
 * Portions Copyright (c) 2010-2024, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  src/backend/replication/syncrep.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <unistd.h>

#include "access/xact.h"
#include "common/int.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "replication/syncrep.h"
#include "replication/walsender.h"
#include "replication/walsender_private.h"
#include "storage/proc.h"
#include "tcop/tcopprot.h"
#include "utils/guc_hooks.h"
#include "utils/ps_status.h"

/* POLAR */
#include "replication/slot.h"

/* User-settable parameters for sync rep */
char	   *SyncRepStandbyNames;

#define SyncStandbysDefined() \
	(SyncRepStandbyNames != NULL && SyncRepStandbyNames[0] != '\0')

static bool announce_next_takeover = true;

SyncRepConfigData *SyncRepConfig = NULL;
static int	SyncRepWaitMode = SYNC_REP_NO_WAIT;

static bool SyncRepGetSyncRecPtr(XLogRecPtr *writePtr,
								 XLogRecPtr *flushPtr,
								 XLogRecPtr *applyPtr,
								 bool *am_sync);
static void SyncRepGetOldestSyncRecPtr(XLogRecPtr *writePtr,
<<<<<<< HEAD
						   XLogRecPtr *flushPtr,
						   XLogRecPtr *applyPtr,
						   SyncRepStandbyData *sync_standbys,
						   int num_standbys);
static void SyncRepGetNthLatestSyncRecPtr(XLogRecPtr *writePtr,
							  XLogRecPtr *flushPtr,
							  XLogRecPtr *applyPtr,
							  SyncRepStandbyData *sync_standbys,
							  int num_standbys,
							  uint8 nth);
static int	SyncRepGetStandbyPriority(void);
static List *SyncRepGetSyncStandbysPriority(bool *am_sync);
static List *SyncRepGetSyncStandbysQuorum(bool *am_sync);
=======
									   XLogRecPtr *flushPtr,
									   XLogRecPtr *applyPtr,
									   SyncRepStandbyData *sync_standbys,
									   int num_standbys);
static void SyncRepGetNthLatestSyncRecPtr(XLogRecPtr *writePtr,
										  XLogRecPtr *flushPtr,
										  XLogRecPtr *applyPtr,
										  SyncRepStandbyData *sync_standbys,
										  int num_standbys,
										  uint8 nth);
static int	SyncRepGetStandbyPriority(void);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
static int	standby_priority_comparator(const void *a, const void *b);
static int	cmp_lsn(const void *a, const void *b);

/* POLAR */
static bool polar_get_ddl_applyptr(XLogRecPtr *ddl_applyptr, bool *replica_slot_all_active);
/* POLAR: handle the sync replication timeout */
static void polar_sync_rep_update_timeout_flag(void);
static bool polar_semi_sync_within_window(instr_time start_time, long timeout);
static bool polar_semi_sync_within_observation_window(void);
static bool polar_semi_sync_within_backoff_window(void);
static void polar_semi_sync_set_backoff_window(void);

/*
 * ===========================================================
 * Synchronous Replication functions for normal user backends
 * ===========================================================
 */

/*
 * Wait for synchronous replication, if requested by user.
 *
 * Initially backends start in state SYNC_REP_NOT_WAITING and then
 * change that state to SYNC_REP_WAITING before adding ourselves
 * to the wait queue. During SyncRepWakeQueue() a WALSender changes
 * the state to SYNC_REP_WAIT_COMPLETE once replication is confirmed.
 * This backend then resets its state to SYNC_REP_NOT_WAITING.
 *
 * 'lsn' represents the LSN to wait for.  'commit' indicates whether this LSN
 * represents a commit record.  If it doesn't, then we wait only for the WAL
 * to be flushed if synchronous_commit is set to the higher level of
 * remote_apply, because only commit records provide apply feedback.
 *
 * POLAR: polar_standby_lock for sync DDL
 * If and only if using ddl sync replica, polar_force_wait_apply should be true
 */
void
SyncRepWaitForLSN(XLogRecPtr lsn, bool commit, bool polar_force_wait_apply)
{
	int			mode;

<<<<<<< HEAD
    /* POLAR: Define some variables to call polar_get_ddl_applyptr(). */
    XLogRecPtr	ddl_applyptr = InvalidXLogRecPtr;
    bool replica_slot_all_active = true;
    bool replica_slot_exist;
	instr_time	semi_sync_wait_start;

    /* Cap the level for anything other than commit to remote flush only. */
=======
	/*
	 * This should be called while holding interrupts during a transaction
	 * commit to prevent the follow-up shared memory queue cleanups to be
	 * influenced by external interruptions.
	 */
	Assert(InterruptHoldoffCount > 0);

	/*
	 * Fast exit if user has not requested sync replication, or there are no
	 * sync replication standby names defined.
	 *
	 * Since this routine gets called every commit time, it's important to
	 * exit quickly if sync replication is not requested. So we check
	 * WalSndCtl->sync_standbys_defined flag without the lock and exit
	 * immediately if it's false. If it's true, we need to check it again
	 * later while holding the lock, to check the flag and operate the sync
	 * rep queue atomically. This is necessary to avoid the race condition
	 * described in SyncRepUpdateSyncStandbysDefined(). On the other hand, if
	 * it's false, the lock is not necessary because we don't touch the queue.
	 */
	if (!SyncRepRequested() ||
		!((volatile WalSndCtlData *) WalSndCtl)->sync_standbys_defined)
		return;

	/* Cap the level for anything other than commit to remote flush only. */
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	if (commit)
		mode = SyncRepWaitMode;
	else
		mode = Min(SyncRepWaitMode, SYNC_REP_WAIT_FLUSH);

<<<<<<< HEAD
	/*
	 * POLAR: when enable polardb(rw+ro node on shared disk), ddl must be sync mode.
	 * We use stream replication stanby lock for ddl sync.
	 */
	if (polar_enable_shared_storage_mode)
	{
		if (polar_force_wait_apply)
			mode = POLAR_SYNC_DDL_WAIT_APPLY;
		else if (polar_enable_transaction_sync_mode == false)
			return;
	}

    /*
     * Fast exit if user has not requested sync replication.
     *
     * POLAR: When enable polar_enable_ddl_sync_mode, no need to exit.
     */
    if (!SyncRepRequested() && !polar_force_wait_apply)
        return;

	Assert(SHMQueueIsDetached(&(MyProc->syncRepLinks)));
=======
	Assert(dlist_node_is_detached(&MyProc->syncRepLinks));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	Assert(WalSndCtl != NULL);

	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
	Assert(MyProc->syncRepState == SYNC_REP_NOT_WAITING);

    /*
     * We don't wait for sync rep if WalSndCtl->sync_standbys_defined is not
     * set.  See SyncRepUpdateSyncStandbysDefined.
     *
     * Also check that the standby hasn't already replied. Unlikely race
     * condition but we'll be fetching that cache line anyway so it's likely
     * to be a low cost check.
     *
     * POLAR: When using ddl sync replica, WalSndCtl->sync_standbys_defined is
     * meaningless. When no replica slots exists, we don't enter waiting queue.
	 * 
	 * POLAR: When synchronous replication is timeout, the synchronous replication
	 * change to asynchronous replication, skip the waiting.
	 * But if the replication catch up, it's time to back to synchronous again.
	 * 
	 * POLAR: sync ddl shouldn't timeout
     */
    replica_slot_exist = polar_get_ddl_applyptr(&ddl_applyptr,
            &replica_slot_all_active);
    if ((!WalSndCtl->sync_standbys_defined && !polar_force_wait_apply) ||
        (polar_force_wait_apply && !replica_slot_exist) ||
        (lsn <= WalSndCtl->lsn[mode]) ||
		(WalSndCtl->is_sync_replication_timeout && !polar_force_wait_apply))
    {
		/*
		 * POLAR: The lag between the WalSndCtl lsn and myproc waitlsn is smaller than polar_sync_rep_timeout_break_lsn_lag,
		 * so we think it's time to be synchronous again. However, this transaction is ignored.
		 */
		if(WalSndCtl->is_sync_replication_timeout && !polar_force_wait_apply &&
		   lsn - WalSndCtl->lsn[mode] <= polar_sync_rep_timeout_break_lsn_lag)
		{
			/*
			 * POLAR: Semi-synchronous replication optimization under network jitter.
			 * If we are now in a backoff window, we should not back to wait for sync, we should keep async.
			 * Otherwise, we back to wait for sync, and open an observation window.
			 */
			if (!polar_semi_sync_within_backoff_window())
			{
				WalSndCtl->is_sync_replication_timeout = false;
				ereport(WARNING,
						(errmsg("the timeout synchronous replication is back to wait for sync"),
						 errdetail("The gap of lsn between synchronous replication and local is less than %d bytes, "
						 		   "then the timeout synchronous replication will be synchronous again.",
								   polar_sync_rep_timeout_break_lsn_lag)));

				if (polar_enable_semi_sync_optimization)
				{
					/* POLAR: Set observation window */
					INSTR_TIME_SET_CURRENT(WalSndCtl->polar_semi_sync_observation_window_start);
					ereport(WARNING,
							(errmsg("start a new %d s observation window.", polar_semi_sync_observation_window / 1000),
							 errdetail("Currently restored temporarily, but need to open an observation window to "
							 		   "observe if there will be network jitter problems, if there are problems "
									   "during this period, the back-off time will be increased.")));
				}
			}
			else
			{
				ereport(WARNING,
						(errmsg("semi-sync optimization under network jitter, keep async since we are within a backoff window.")));
			}
		}

        LWLockRelease(SyncRepLock);
        return;
    }

	/*
	 * Set our waitLSN so WALSender will know when to wake us, and add
	 * ourselves to the queue.
	 */
	MyProc->waitLSN = lsn;
	MyProc->syncRepState = SYNC_REP_WAITING;
	SyncRepQueueInsert(mode);
	Assert(SyncRepQueueIsOrderedByLSN(mode));
	LWLockRelease(SyncRepLock);

	/* Alter ps display to show waiting for sync rep. */
	if (update_process_title)
	{
		char		buffer[32];

		sprintf(buffer, "waiting for %X/%X", LSN_FORMAT_ARGS(lsn));
		set_ps_display_suffix(buffer);
	}

	/*
	 * Wait for specified LSN to be confirmed.
	 *
	 * Each proc has its own wait latch, so we perform a normal latch
	 * check/wait loop here.
	 *
	 * POLAR: extra timeout judgment is needed. See comment below.
	 */
	INSTR_TIME_SET_CURRENT(semi_sync_wait_start);
	for (;;)
	{
		int			rc;

		/* Must reset the latch before testing state. */
		ResetLatch(MyLatch);

		/*
		 * Acquiring the lock is not needed, the latch ensures proper
		 * barriers. If it looks like we're done, we must really be done,
		 * because once walsender changes the state to SYNC_REP_WAIT_COMPLETE,
		 * it will never update it again, so we can't be seeing a stale value
		 * in that case.
		 */
		if (MyProc->syncRepState == SYNC_REP_WAIT_COMPLETE)
			break;

		/*
		 * If a wait for synchronous replication is pending, we can neither
		 * acknowledge the commit nor raise ERROR or FATAL.  The latter would
		 * lead the client to believe that the transaction aborted, which is
		 * not true: it's already committed locally. The former is no good
		 * either: the client has requested synchronous replication, and is
		 * entitled to assume that an acknowledged commit is also replicated,
		 * which might not be true. So in this case we issue a WARNING (which
		 * some clients may be able to interpret) and shut off further output.
		 * We do NOT reset ProcDiePending, so that the process will die after
		 * the commit is cleaned up.
		 */
		if (ProcDiePending && !polar_force_wait_apply)
		{
			ereport(WARNING,
					(errcode(ERRCODE_ADMIN_SHUTDOWN),
					 errmsg("canceling the wait for synchronous replication and terminating connection due to administrator command"),
					 errdetail("The transaction has already committed locally, but might not have been replicated to the standby.")));
			whereToSendOutput = DestNone;
			SyncRepCancelWait();
			break;
		}
		/* POLAR: cancel the synchronous ddl. */
		else if (ProcDiePending && polar_force_wait_apply)
		{
			whereToSendOutput = DestNone;
			SyncRepCancelWait();
			ereport(ERROR,
					(errcode(ERRCODE_ADMIN_SHUTDOWN),
					 errmsg("canceling the wait for synchronous replication and terminating connection due to administrator command"),
					 errdetail("The synchronous ddl was canceled.")));
		}

		/*
		 * It's unclear what to do if a query cancel interrupt arrives.  We
		 * can't actually abort at this point, but ignoring the interrupt
		 * altogether is not helpful, so we just terminate the wait with a
		 * suitable warning.
		 */
		if (QueryCancelPending && !polar_force_wait_apply)
		{
			QueryCancelPending = false;
			ereport(WARNING,
					(errmsg("canceling wait for synchronous replication due to user request"),
					 errdetail("The transaction has already committed locally, but might not have been replicated to the standby.")));
			SyncRepCancelWait();
			break;
		}
		/* POLAR: cancel the synchronous ddl if a query cancel interrupt arrives. */
		else if (QueryCancelPending && polar_force_wait_apply)
		{
			QueryCancelPending = false;
			SyncRepCancelWait();
			ereport(ERROR,
					(errmsg("canceling wait for synchronous replication due to user request"),
					 errdetail("The synchronous ddl was canceled.")));
		}

		/*
		 * Wait on latch.  Any condition that should wake us up will set the
		 * latch, so no need for timeout.
		 */
		rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_POSTMASTER_DEATH, -1,
					   WAIT_EVENT_SYNC_REP);

		/*
		 * If the postmaster dies, we'll probably never get an acknowledgment,
		 * because all the wal sender processes will exit. So just bail out.
		 */
		if (rc & WL_POSTMASTER_DEATH)
		{
			ProcDiePending = true;
			whereToSendOutput = DestNone;
			SyncRepCancelWait();
			break;
		}
<<<<<<< HEAD

		/*
		 * Wait on latch.  Any condition that should wake us up will set the
		 * latch, so no need for timeout.
		 *
		 * POLAR: If polar_sync_replication_timeout is larger than zero,
		 * we will wait on latch with timeout.
		 * When synchronous replication is timeout, wake up any wait currently in progress
		 * and synchronous replication change to be asynchronous.
		 * 
		 * POLAR: sync ddl shouldn't timeout
		 */
		if (WalSndCtl->sync_replication_timeout_enabled && !polar_force_wait_apply)
		{
			int         rc;
			bool		semi_sync_timeout = false;

			rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_POSTMASTER_DEATH | WL_TIMEOUT, polar_sync_replication_timeout,
					  	   WAIT_EVENT_SYNC_REP);
			/*
			 * POLAR: MyLatch may be frequently set by the SIGALRM signal processing function,
			 * so that we can never wait until the latch timeout event, and never get out of the loop.
			 * So add an extra timeout judgment here.
			 */
			semi_sync_timeout = !polar_semi_sync_within_window(semi_sync_wait_start, polar_sync_replication_timeout);
			if (rc == WL_TIMEOUT || semi_sync_timeout)
			{
				ereport(WARNING,
						(errmsg("canceling wait for synchronous replication due to timeout"),
						 errdetail("The transactions will be committed locally, but might not have been replicated to the standby "
								 "until the gap of lsn between synchronous replication and local is less than %d bytes.",
								 polar_sync_rep_timeout_break_lsn_lag)));
				SyncRepCancelWait();
				polar_sync_rep_update_timeout_flag();
				polar_semi_sync_set_backoff_window();
				break;
			}
		}
		else
		{
			WaitLatch(MyLatch, WL_LATCH_SET | WL_POSTMASTER_DEATH, -1,
					  WAIT_EVENT_SYNC_REP);
		}
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	/*
	 * WalSender has checked our LSN and has removed us from queue. Clean up
	 * state and leave.  It's OK to reset these shared memory fields without
	 * holding SyncRepLock, because any walsenders will ignore us anyway when
	 * we're not on the queue.  We need a read barrier to make sure we see the
	 * changes to the queue link (this might be unnecessary without
	 * assertions, but better safe than sorry).
	 */
	pg_read_barrier();
	Assert(dlist_node_is_detached(&MyProc->syncRepLinks));
	MyProc->syncRepState = SYNC_REP_NOT_WAITING;
	MyProc->waitLSN = 0;

	/* reset ps display to remove the suffix */
	if (update_process_title)
		set_ps_display_remove_suffix();
}

/*
 * Insert MyProc into the specified SyncRepQueue, maintaining sorted invariant.
 *
 * Usually we will go at tail of queue, though it's possible that we arrive
 * here out of order, so start at tail and work back to insertion point.
 */
void
SyncRepQueueInsert(int mode)
{
	dlist_head *queue;
	dlist_iter	iter;

<<<<<<< HEAD
	Assert(mode >= 0 && mode < POLAR_NUM_ALL_REP_WAIT_MODE);
	proc = (PGPROC *) SHMQueuePrev(&(WalSndCtl->SyncRepQueue[mode]),
								   &(WalSndCtl->SyncRepQueue[mode]),
								   offsetof(PGPROC, syncRepLinks));
=======
	Assert(mode >= 0 && mode < NUM_SYNC_REP_WAIT_MODE);
	queue = &WalSndCtl->SyncRepQueue[mode];
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	dlist_reverse_foreach(iter, queue)
	{
		PGPROC	   *proc = dlist_container(PGPROC, syncRepLinks, iter.cur);

		/*
		 * Stop at the queue element that we should insert after to ensure the
		 * queue is ordered by LSN.
		 */
		if (proc->waitLSN < MyProc->waitLSN)
		{
			dlist_insert_after(&proc->syncRepLinks, &MyProc->syncRepLinks);
			return;
		}
	}

	/*
	 * If we get here, the list was either empty, or this process needs to be
	 * at the head.
	 */
	dlist_push_head(queue, &MyProc->syncRepLinks);
}

/*
 * Acquire SyncRepLock and cancel any wait currently in progress.
 */
void
SyncRepCancelWait(void)
{
	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
	if (!dlist_node_is_detached(&MyProc->syncRepLinks))
		dlist_delete_thoroughly(&MyProc->syncRepLinks);
	MyProc->syncRepState = SYNC_REP_NOT_WAITING;
	LWLockRelease(SyncRepLock);
}

void
SyncRepCleanupAtProcExit(void)
{
	/*
	 * First check if we are removed from the queue without the lock to not
	 * slow down backend exit.
	 */
<<<<<<< HEAD
	if (!SHMQueueIsDetached(&(MyProc->syncRepLinks)))
=======
	if (!dlist_node_is_detached(&MyProc->syncRepLinks))
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	{
		LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);

		/* maybe we have just been removed, so recheck */
<<<<<<< HEAD
		if (!SHMQueueIsDetached(&(MyProc->syncRepLinks)))
			SHMQueueDelete(&(MyProc->syncRepLinks));
=======
		if (!dlist_node_is_detached(&MyProc->syncRepLinks))
			dlist_delete_thoroughly(&MyProc->syncRepLinks);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

		LWLockRelease(SyncRepLock);
	}
}

/*
 * ===========================================================
 * Synchronous Replication functions for wal sender processes
 * ===========================================================
 */

/*
 * Take any action required to initialise sync rep state from config
 * data. Called at WALSender startup and after each SIGHUP.
 */
void
SyncRepInitConfig(void)
{
	int			priority;

	/*
	 * Determine if we are a potential sync standby and remember the result
	 * for handling replies from standby.
	 */
	priority = SyncRepGetStandbyPriority();
	if (MyWalSnd->sync_standby_priority != priority)
	{
		SpinLockAcquire(&MyWalSnd->mutex);
		MyWalSnd->sync_standby_priority = priority;
		SpinLockRelease(&MyWalSnd->mutex);

		ereport(DEBUG1,
				(errmsg_internal("standby \"%s\" now has synchronous standby priority %d",
								 application_name, priority)));
	}
}

/*
 * Update the LSNs on each queue based upon our latest state. This
 * implements a simple policy of first-valid-sync-standby-releases-waiter.
 *
 * Other policies are possible, which would change what we do here and
 * perhaps also which information we store as well.
 */
void
SyncRepReleaseWaiters(void)
{
	volatile WalSndCtlData *walsndctl = WalSndCtl;
	XLogRecPtr	writePtr;
	XLogRecPtr	flushPtr;
	XLogRecPtr	applyPtr;
	bool		got_recptr;
	bool		am_sync;
	int			numwrite = 0;
	int			numflush = 0;
	int			numapply = 0;

	/*
	 * If this WALSender is serving a standby that is not on the list of
	 * potential sync standbys then we have nothing to do. If we are still
	 * starting up, still running base backup or the current flush position is
	 * still invalid, then leave quickly also.  Streaming or stopping WAL
	 * senders are allowed to release waiters.
	 */
	if (MyWalSnd->sync_standby_priority == 0 ||
		(MyWalSnd->state != WALSNDSTATE_STREAMING &&
		 MyWalSnd->state != WALSNDSTATE_STOPPING) ||
		XLogRecPtrIsInvalid(MyWalSnd->flush))
	{
		announce_next_takeover = true;
		return;
	}

	/*
	 * We're a potential sync standby. Release waiters if there are enough
	 * sync standbys and we are considered as sync.
	 */
	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);

	/*
	 * Check whether we are a sync standby or not, and calculate the synced
	 * positions among all sync standbys.  (Note: although this step does not
	 * of itself require holding SyncRepLock, it seems like a good idea to do
	 * it after acquiring the lock.  This ensures that the WAL pointers we use
	 * to release waiters are newer than any previous execution of this
	 * routine used.)
	 */
	got_recptr = SyncRepGetSyncRecPtr(&writePtr, &flushPtr, &applyPtr, &am_sync);

	/*
	 * If we are managing a sync standby, though we weren't prior to this,
	 * then announce we are now a sync standby.
	 */
	if (announce_next_takeover && am_sync)
	{
		announce_next_takeover = false;

		if (SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY)
			ereport(LOG,
					(errmsg("standby \"%s\" is now a synchronous standby with priority %d",
							application_name, MyWalSnd->sync_standby_priority)));
		else
			ereport(LOG,
					(errmsg("standby \"%s\" is now a candidate for quorum synchronous standby",
							application_name)));
	}

	/*
	 * If the number of sync standbys is less than requested or we aren't
	 * managing a sync standby then just leave.
	 */
	if (!got_recptr || !am_sync)
	{
		LWLockRelease(SyncRepLock);
		announce_next_takeover = !am_sync;
		return;
	}

	/*
	 * Set the lsn first so that when we wake backends they will release up to
	 * this location.
	 */
	if (walsndctl->lsn[SYNC_REP_WAIT_WRITE] < writePtr)
	{
		walsndctl->lsn[SYNC_REP_WAIT_WRITE] = writePtr;
		numwrite = SyncRepWakeQueue(false, SYNC_REP_WAIT_WRITE);
	}
	if (walsndctl->lsn[SYNC_REP_WAIT_FLUSH] < flushPtr)
	{
		walsndctl->lsn[SYNC_REP_WAIT_FLUSH] = flushPtr;
		numflush = SyncRepWakeQueue(false, SYNC_REP_WAIT_FLUSH);
	}

	if (walsndctl->lsn[SYNC_REP_WAIT_APPLY] < applyPtr)
	{
		walsndctl->lsn[SYNC_REP_WAIT_APPLY] = applyPtr;
		numapply = SyncRepWakeQueue(false, SYNC_REP_WAIT_APPLY);
	}

	LWLockRelease(SyncRepLock);

	elog(DEBUG3, "released %d procs up to write %X/%X, %d procs up to flush %X/%X, %d procs up to apply %X/%X",
		 numwrite, LSN_FORMAT_ARGS(writePtr),
		 numflush, LSN_FORMAT_ARGS(flushPtr),
		 numapply, LSN_FORMAT_ARGS(applyPtr));
}

/*
 * Calculate the synced Write, Flush and Apply positions among sync standbys.
 *
 * Return false if the number of sync standbys is less than
 * synchronous_standby_names specifies. Otherwise return true and
 * store the positions into *writePtr, *flushPtr and *applyPtr.
 *
 * On return, *am_sync is set to true if this walsender is connecting to
 * sync standby. Otherwise it's set to false.
 */
static bool
SyncRepGetSyncRecPtr(XLogRecPtr *writePtr, XLogRecPtr *flushPtr,
					 XLogRecPtr *applyPtr, bool *am_sync)
{
	SyncRepStandbyData *sync_standbys;
	int			num_standbys;
	int			i;

	/* Initialize default results */
	*writePtr = InvalidXLogRecPtr;
	*flushPtr = InvalidXLogRecPtr;
	*applyPtr = InvalidXLogRecPtr;
	*am_sync = false;

	/* Quick out if not even configured to be synchronous */
	if (SyncRepConfig == NULL)
		return false;

	/* Get standbys that are considered as synchronous at this moment */
	num_standbys = SyncRepGetCandidateStandbys(&sync_standbys);

	/* Am I among the candidate sync standbys? */
	for (i = 0; i < num_standbys; i++)
	{
		if (sync_standbys[i].is_me)
		{
			*am_sync = true;
			break;
		}
	}

	/*
	 * Nothing more to do if we are not managing a sync standby or there are
	 * not enough synchronous standbys.
	 */
	if (!(*am_sync) ||
		num_standbys < SyncRepConfig->num_sync)
	{
		pfree(sync_standbys);
		return false;
	}

	/*
	 * In a priority-based sync replication, the synced positions are the
	 * oldest ones among sync standbys. In a quorum-based, they are the Nth
	 * latest ones.
	 *
	 * SyncRepGetNthLatestSyncRecPtr() also can calculate the oldest
	 * positions. But we use SyncRepGetOldestSyncRecPtr() for that calculation
	 * because it's a bit more efficient.
	 *
	 * XXX If the numbers of current and requested sync standbys are the same,
	 * we can use SyncRepGetOldestSyncRecPtr() to calculate the synced
	 * positions even in a quorum-based sync replication.
	 */
	if (SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY)
	{
		SyncRepGetOldestSyncRecPtr(writePtr, flushPtr, applyPtr,
								   sync_standbys, num_standbys);
	}
	else
	{
		SyncRepGetNthLatestSyncRecPtr(writePtr, flushPtr, applyPtr,
									  sync_standbys, num_standbys,
									  SyncRepConfig->num_sync);
	}

	pfree(sync_standbys);
	return true;
}

/*
 * Calculate the oldest Write, Flush and Apply positions among sync standbys.
 */
static void
SyncRepGetOldestSyncRecPtr(XLogRecPtr *writePtr,
						   XLogRecPtr *flushPtr,
						   XLogRecPtr *applyPtr,
						   SyncRepStandbyData *sync_standbys,
						   int num_standbys)
{
	int			i;

	/*
	 * Scan through all sync standbys and calculate the oldest Write, Flush
	 * and Apply positions.  We assume *writePtr et al were initialized to
	 * InvalidXLogRecPtr.
	 */
	for (i = 0; i < num_standbys; i++)
	{
		XLogRecPtr	write = sync_standbys[i].write;
		XLogRecPtr	flush = sync_standbys[i].flush;
		XLogRecPtr	apply = sync_standbys[i].apply;

		if (XLogRecPtrIsInvalid(*writePtr) || *writePtr > write)
			*writePtr = write;
		if (XLogRecPtrIsInvalid(*flushPtr) || *flushPtr > flush)
			*flushPtr = flush;
		if (XLogRecPtrIsInvalid(*applyPtr) || *applyPtr > apply)
			*applyPtr = apply;
	}
}

/*
 * Calculate the Nth latest Write, Flush and Apply positions among sync
 * standbys.
 */
static void
SyncRepGetNthLatestSyncRecPtr(XLogRecPtr *writePtr,
							  XLogRecPtr *flushPtr,
							  XLogRecPtr *applyPtr,
							  SyncRepStandbyData *sync_standbys,
							  int num_standbys,
							  uint8 nth)
{
	XLogRecPtr *write_array;
	XLogRecPtr *flush_array;
	XLogRecPtr *apply_array;
	int			i;

	/* Should have enough candidates, or somebody messed up */
	Assert(nth > 0 && nth <= num_standbys);

	write_array = (XLogRecPtr *) palloc(sizeof(XLogRecPtr) * num_standbys);
	flush_array = (XLogRecPtr *) palloc(sizeof(XLogRecPtr) * num_standbys);
	apply_array = (XLogRecPtr *) palloc(sizeof(XLogRecPtr) * num_standbys);

	for (i = 0; i < num_standbys; i++)
	{
		write_array[i] = sync_standbys[i].write;
		flush_array[i] = sync_standbys[i].flush;
		apply_array[i] = sync_standbys[i].apply;
	}

	/* Sort each array in descending order */
	qsort(write_array, num_standbys, sizeof(XLogRecPtr), cmp_lsn);
	qsort(flush_array, num_standbys, sizeof(XLogRecPtr), cmp_lsn);
	qsort(apply_array, num_standbys, sizeof(XLogRecPtr), cmp_lsn);

	/* Get Nth latest Write, Flush, Apply positions */
	*writePtr = write_array[nth - 1];
	*flushPtr = flush_array[nth - 1];
	*applyPtr = apply_array[nth - 1];

	pfree(write_array);
	pfree(flush_array);
	pfree(apply_array);
}

/*
 * Compare lsn in order to sort array in descending order.
 */
static int
cmp_lsn(const void *a, const void *b)
{
	XLogRecPtr	lsn1 = *((const XLogRecPtr *) a);
	XLogRecPtr	lsn2 = *((const XLogRecPtr *) b);

	return pg_cmp_u64(lsn2, lsn1);
}

/*
 * Return data about walsenders that are candidates to be sync standbys.
<<<<<<< HEAD
 *
 * *standbys is set to a palloc'd array of structs of per-walsender data,
 * and the number of valid entries (candidate sync senders) is returned.
 * (This might be more or fewer than num_sync; caller must check.)
 */
int
SyncRepGetCandidateStandbys(SyncRepStandbyData **standbys)
{
	int			i;
	int			n;

	/* Create result array */
	*standbys = (SyncRepStandbyData *)
		palloc(max_wal_senders * sizeof(SyncRepStandbyData));

	/* Quick exit if sync replication is not requested */
	if (SyncRepConfig == NULL)
		return 0;

	/* Collect raw data from shared memory */
	n = 0;
	for (i = 0; i < max_wal_senders; i++)
	{
		volatile WalSnd *walsnd;	/* Use volatile pointer to prevent code
									 * rearrangement */
		SyncRepStandbyData *stby;
		WalSndState state;		/* not included in SyncRepStandbyData */

		walsnd = &WalSndCtl->walsnds[i];
		stby = *standbys + n;

		SpinLockAcquire(&walsnd->mutex);
		stby->pid = walsnd->pid;
		state = walsnd->state;
		stby->write = walsnd->write;
		stby->flush = walsnd->flush;
		stby->apply = walsnd->apply;
		stby->sync_standby_priority = walsnd->sync_standby_priority;
		SpinLockRelease(&walsnd->mutex);

		/* Must be active */
		if (stby->pid == 0)
			continue;

		/* Must be streaming or stopping */
		if (state != WALSNDSTATE_STREAMING &&
			state != WALSNDSTATE_STOPPING)
			continue;

		/* Must be synchronous */
		if (stby->sync_standby_priority == 0)
			continue;

		/* Must have a valid flush position */
		if (XLogRecPtrIsInvalid(stby->flush))
			continue;

		/* OK, it's a candidate */
		stby->walsnd_index = i;
		stby->is_me = (walsnd == MyWalSnd);
		n++;
	}

	/*
	 * In quorum mode, we return all the candidates.  In priority mode, if we
	 * have too many candidates then return only the num_sync ones of highest
	 * priority.
	 */
	if (SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY &&
		n > SyncRepConfig->num_sync)
	{
		/* Sort by priority ... */
		qsort(*standbys, n, sizeof(SyncRepStandbyData),
			  standby_priority_comparator);
		/* ... then report just the first num_sync ones */
		n = SyncRepConfig->num_sync;
	}

	return n;
}

/*
 * qsort comparator to sort SyncRepStandbyData entries by priority
 */
static int
standby_priority_comparator(const void *a, const void *b)
{
	const SyncRepStandbyData *sa = (const SyncRepStandbyData *) a;
	const SyncRepStandbyData *sb = (const SyncRepStandbyData *) b;

	/* First, sort by increasing priority value */
	if (sa->sync_standby_priority != sb->sync_standby_priority)
		return sa->sync_standby_priority - sb->sync_standby_priority;

	/*
	 * We might have equal priority values; arbitrarily break ties by position
	 * in the WALSnd array.  (This is utterly bogus, since that is arrival
	 * order dependent, but there are regression tests that rely on it.)
	 */
	return sa->walsnd_index - sb->walsnd_index;
}


/*
 * Return the list of sync standbys, or NIL if no sync standby is connected.
 *
 * The caller must hold SyncRepLock.
 *
 * On return, *am_sync is set to true if this walsender is connecting to
 * sync standby. Otherwise it's set to false.
 *
 * XXX This function is BROKEN and should not be used in new code.  It has
 * an inherent race condition, since the returned list of integer indexes
 * might no longer correspond to reality.
=======
 *
 * *standbys is set to a palloc'd array of structs of per-walsender data,
 * and the number of valid entries (candidate sync senders) is returned.
 * (This might be more or fewer than num_sync; caller must check.)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 */
int
SyncRepGetCandidateStandbys(SyncRepStandbyData **standbys)
{
	int			i;
	int			n;

	/* Create result array */
	*standbys = (SyncRepStandbyData *)
		palloc(max_wal_senders * sizeof(SyncRepStandbyData));

	/* Quick exit if sync replication is not requested */
	if (SyncRepConfig == NULL)
		return 0;

	/* Collect raw data from shared memory */
	n = 0;
	for (i = 0; i < max_wal_senders; i++)
	{
		volatile WalSnd *walsnd;	/* Use volatile pointer to prevent code
									 * rearrangement */
		SyncRepStandbyData *stby;
		WalSndState state;		/* not included in SyncRepStandbyData */

		walsnd = &WalSndCtl->walsnds[i];
		stby = *standbys + n;

		SpinLockAcquire(&walsnd->mutex);
		stby->pid = walsnd->pid;
		state = walsnd->state;
		stby->write = walsnd->write;
		stby->flush = walsnd->flush;
		stby->apply = walsnd->apply;
		stby->sync_standby_priority = walsnd->sync_standby_priority;
		SpinLockRelease(&walsnd->mutex);

		/* Must be active */
		if (stby->pid == 0)
			continue;

		/* Must be streaming or stopping */
		if (state != WALSNDSTATE_STREAMING &&
			state != WALSNDSTATE_STOPPING)
			continue;

		/* Must be synchronous */
		if (stby->sync_standby_priority == 0)
			continue;

		/* Must have a valid flush position */
		if (XLogRecPtrIsInvalid(stby->flush))
			continue;

		/* OK, it's a candidate */
		stby->walsnd_index = i;
		stby->is_me = (walsnd == MyWalSnd);
		n++;
	}

	/*
	 * In quorum mode, we return all the candidates.  In priority mode, if we
	 * have too many candidates then return only the num_sync ones of highest
	 * priority.
	 */
	if (SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY &&
		n > SyncRepConfig->num_sync)
	{
		/* Sort by priority ... */
		qsort(*standbys, n, sizeof(SyncRepStandbyData),
			  standby_priority_comparator);
		/* ... then report just the first num_sync ones */
		n = SyncRepConfig->num_sync;
	}

	return n;
}

/*
 * qsort comparator to sort SyncRepStandbyData entries by priority
 */
static int
standby_priority_comparator(const void *a, const void *b)
{
	const SyncRepStandbyData *sa = (const SyncRepStandbyData *) a;
	const SyncRepStandbyData *sb = (const SyncRepStandbyData *) b;

	/* First, sort by increasing priority value */
	if (sa->sync_standby_priority != sb->sync_standby_priority)
		return sa->sync_standby_priority - sb->sync_standby_priority;

	/*
	 * We might have equal priority values; arbitrarily break ties by position
	 * in the WalSnd array.  (This is utterly bogus, since that is arrival
	 * order dependent, but there are regression tests that rely on it.)
	 */
<<<<<<< HEAD
	for (i = 0; i < max_wal_senders; i++)
	{
		XLogRecPtr	flush;
		WalSndState state;
		int			pid;

		walsnd = &WalSndCtl->walsnds[i];

		SpinLockAcquire(&walsnd->mutex);
		pid = walsnd->pid;
		flush = walsnd->flush;
		state = walsnd->state;
		SpinLockRelease(&walsnd->mutex);

		/* Must be active */
		if (pid == 0)
			continue;

		/* Must be streaming or stopping */
		if (state != WALSNDSTATE_STREAMING &&
			state != WALSNDSTATE_STOPPING)
			continue;

		/* Must be synchronous */
		this_priority = walsnd->sync_standby_priority;
		if (this_priority == 0)
			continue;

		/* Must have a valid flush position */
		if (XLogRecPtrIsInvalid(flush))
			continue;

		/*
		 * If the priority is equal to 1, consider this standby as sync and
		 * append it to the result. Otherwise append this standby to the
		 * pending list to check if it's actually sync or not later.
		 */
		if (this_priority == 1)
		{
			result = lappend_int(result, i);
			if (am_sync != NULL && walsnd == MyWalSnd)
				*am_sync = true;
			if (list_length(result) == SyncRepConfig->num_sync)
			{
				list_free(pending);
				return result;	/* Exit if got enough sync standbys */
			}
		}
		else
		{
			pending = lappend_int(pending, i);
			if (am_sync != NULL && walsnd == MyWalSnd)
				am_in_pending = true;

			/*
			 * Track the highest priority among the standbys in the pending
			 * list, in order to use it as the starting priority for later
			 * scan of the list. This is useful to find quickly the sync
			 * standbys from the pending list later because we can skip
			 * unnecessary scans for the unused priorities.
			 */
			if (this_priority < next_highest_priority)
				next_highest_priority = this_priority;
		}
	}

	/*
	 * Consider all pending standbys as sync if the number of them plus
	 * already-found sync ones is lower than the configuration requests.
	 */
	if (list_length(result) + list_length(pending) <= SyncRepConfig->num_sync)
	{
		bool		needfree = (result != NIL && pending != NIL);

		/*
		 * Set *am_sync to true if this walsender is in the pending list
		 * because all pending standbys are considered as sync.
		 */
		if (am_sync != NULL && !(*am_sync))
			*am_sync = am_in_pending;

		result = list_concat(result, pending);
		if (needfree)
			pfree(pending);
		return result;
	}

	/*
	 * Find the sync standbys from the pending list.
	 */
	priority = next_highest_priority;
	while (priority <= lowest_priority)
	{
		ListCell   *cell;
		ListCell   *prev = NULL;
		ListCell   *next;

		next_highest_priority = lowest_priority + 1;

		for (cell = list_head(pending); cell != NULL; cell = next)
		{
			i = lfirst_int(cell);
			walsnd = &WalSndCtl->walsnds[i];

			next = lnext(cell);

			this_priority = walsnd->sync_standby_priority;
			if (this_priority == priority)
			{
				result = lappend_int(result, i);
				if (am_sync != NULL && walsnd == MyWalSnd)
					*am_sync = true;

				/*
				 * We should always exit here after the scan of pending list
				 * starts because we know that the list has enough elements to
				 * reach SyncRepConfig->num_sync.
				 */
				if (list_length(result) == SyncRepConfig->num_sync)
				{
					list_free(pending);
					return result;	/* Exit if got enough sync standbys */
				}

				/*
				 * Remove the entry for this sync standby from the list to
				 * prevent us from looking at the same entry again.
				 */
				pending = list_delete_cell(pending, cell, prev);

				continue;
			}

			if (this_priority < next_highest_priority)
				next_highest_priority = this_priority;

			prev = cell;
		}

		priority = next_highest_priority;
	}

	/*
	 * We might get here if the set of sync_standby_priority values in shared
	 * memory is inconsistent, as can happen transiently after a change in the
	 * synchronous_standby_names setting.  In that case, just return the
	 * incomplete list we have so far.  That will cause the caller to decide
	 * there aren't enough synchronous candidates, which should be a safe
	 * choice until the priority values become consistent again.
	 */
	list_free(pending);
	return result;
=======
	return sa->walsnd_index - sb->walsnd_index;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}


/*
 * Check if we are in the list of sync standbys, and if so, determine
 * priority sequence. Return priority if set, or zero to indicate that
 * we are not a potential sync standby.
 *
 * Compare the parameter SyncRepStandbyNames against the application_name
 * for this WALSender, or allow any name if we find a wildcard "*".
 */
static int
SyncRepGetStandbyPriority(void)
{
	const char *standby_name;
	int			priority;
	bool		found = false;

	/*
	 * Since synchronous cascade replication is not allowed, we always set the
	 * priority of cascading walsender to zero.
	 */
	if (am_cascading_walsender)
		return 0;

	if (!SyncStandbysDefined() || SyncRepConfig == NULL)
		return 0;

	standby_name = SyncRepConfig->member_names;
	for (priority = 1; priority <= SyncRepConfig->nmembers; priority++)
	{
		if (pg_strcasecmp(standby_name, application_name) == 0 ||
			strcmp(standby_name, "*") == 0)
		{
			found = true;
			break;
		}
		standby_name += strlen(standby_name) + 1;
	}

	if (!found)
		return 0;

	/*
	 * In quorum-based sync replication, all the standbys in the list have the
	 * same priority, one.
	 */
	return (SyncRepConfig->syncrep_method == SYNC_REP_PRIORITY) ? priority : 1;
}

/*
 * Walk the specified queue from head.  Set the state of any backends that
 * need to be woken, remove them from the queue, and then wake them.
 * Pass all = true to wake whole queue; otherwise, just wake up to
 * the walsender's LSN.
 *
 * The caller must hold SyncRepLock in exclusive mode.
 */
int
SyncRepWakeQueue(bool all, int mode)
{
	volatile WalSndCtlData *walsndctl = WalSndCtl;
	int			numprocs = 0;
	dlist_mutable_iter iter;

<<<<<<< HEAD
	Assert(mode >= 0 && mode < POLAR_NUM_ALL_REP_WAIT_MODE);
=======
	Assert(mode >= 0 && mode < NUM_SYNC_REP_WAIT_MODE);
	Assert(LWLockHeldByMeInMode(SyncRepLock, LW_EXCLUSIVE));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	Assert(SyncRepQueueIsOrderedByLSN(mode));

	dlist_foreach_modify(iter, &WalSndCtl->SyncRepQueue[mode])
	{
		PGPROC	   *proc = dlist_container(PGPROC, syncRepLinks, iter.cur);

		/*
		 * Assume the queue is ordered by LSN
		 */
		if (!all && walsndctl->lsn[mode] < proc->waitLSN)
			return numprocs;

		/*
		 * Remove from queue.
		 */
		dlist_delete_thoroughly(&proc->syncRepLinks);

		/*
		 * SyncRepWaitForLSN() reads syncRepState without holding the lock, so
		 * make sure that it sees the queue link being removed before the
		 * syncRepState change.
		 */
		pg_write_barrier();

		/*
		 * Set state to complete; see SyncRepWaitForLSN() for discussion of
		 * the various states.
		 */
		proc->syncRepState = SYNC_REP_WAIT_COMPLETE;

		/*
		 * Wake only when we have set state and removed from queue.
		 */
		SetLatch(&(proc->procLatch));

		numprocs++;
	}

	return numprocs;
}

/*
 * The checkpointer calls this as needed to update the shared
 * sync_standbys_defined flag, so that backends don't remain permanently wedged
 * if synchronous_standby_names is unset.  It's safe to check the current value
 * without the lock, because it's only ever updated by one process.  But we
 * must take the lock to change it.
 */
void
SyncRepUpdateSyncStandbysDefined(void)
{
	bool		sync_standbys_defined = SyncStandbysDefined();

	if (sync_standbys_defined != WalSndCtl->sync_standbys_defined)
	{
		LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);

		/*
		 * If synchronous_standby_names has been reset to empty, it's futile
		 * for backends to continue waiting.  Since the user no longer wants
		 * synchronous replication, we'd better wake them up.
		 */
		if (!sync_standbys_defined)
		{
			int			i;

			for (i = 0; i < NUM_SYNC_REP_WAIT_MODE; i++)
				SyncRepWakeQueue(true, i);
		}

		/*
		 * Only allow people to join the queue when there are synchronous
		 * standbys defined.  Without this interlock, there's a race
		 * condition: we might wake up all the current waiters; then, some
		 * backend that hasn't yet reloaded its config might go to sleep on
		 * the queue (and never wake up).  This prevents that.
		 */
		WalSndCtl->sync_standbys_defined = sync_standbys_defined;

		LWLockRelease(SyncRepLock);
	}
}

#ifdef USE_ASSERT_CHECKING
bool
SyncRepQueueIsOrderedByLSN(int mode)
{
	XLogRecPtr	lastLSN;
	dlist_iter	iter;

	Assert(mode >= 0 && mode < POLAR_NUM_ALL_REP_WAIT_MODE);

	lastLSN = 0;

	dlist_foreach(iter, &WalSndCtl->SyncRepQueue[mode])
	{
		PGPROC	   *proc = dlist_container(PGPROC, syncRepLinks, iter.cur);

		/*
		 * Check the queue is ordered by LSN and that multiple procs don't
		 * have matching LSNs
		 */
		if ((mode < NUM_SYNC_REP_WAIT_MODE || mode == POLAR_SYNC_DDL_WAIT_APPLY) && 
			proc->waitLSN <= lastLSN)
			return false;

		/*
		 * POLAR: In priority replication, multiple walsender can wait for 
		 * the same LSN.
		 */
		if (mode == POLAR_PRI_REP_WAIT_FLUSH && proc->waitLSN < lastLSN)
			return false;

		lastLSN = proc->waitLSN;
	}

	return true;
}
#endif

/*
 * ===========================================================
 * Synchronous Replication functions executed by any process
 * ===========================================================
 */

bool
check_synchronous_standby_names(char **newval, void **extra, GucSource source)
{
	if (*newval != NULL && (*newval)[0] != '\0')
	{
		int			parse_rc;
		SyncRepConfigData *pconf;

		/* Reset communication variables to ensure a fresh start */
		syncrep_parse_result = NULL;
		syncrep_parse_error_msg = NULL;

		/* Parse the synchronous_standby_names string */
		syncrep_scanner_init(*newval);
		parse_rc = syncrep_yyparse();
		syncrep_scanner_finish();

		if (parse_rc != 0 || syncrep_parse_result == NULL)
		{
			GUC_check_errcode(ERRCODE_SYNTAX_ERROR);
			if (syncrep_parse_error_msg)
				GUC_check_errdetail("%s", syncrep_parse_error_msg);
			else
				GUC_check_errdetail("\"synchronous_standby_names\" parser failed");
			return false;
		}

		if (syncrep_parse_result->num_sync <= 0)
		{
			GUC_check_errmsg("number of synchronous standbys (%d) must be greater than zero",
							 syncrep_parse_result->num_sync);
			return false;
		}

		/* GUC extra value must be guc_malloc'd, not palloc'd */
		pconf = (SyncRepConfigData *)
			guc_malloc(LOG, syncrep_parse_result->config_size);
		if (pconf == NULL)
			return false;
		memcpy(pconf, syncrep_parse_result, syncrep_parse_result->config_size);

		*extra = (void *) pconf;

		/*
		 * We need not explicitly clean up syncrep_parse_result.  It, and any
		 * other cruft generated during parsing, will be freed when the
		 * current memory context is deleted.  (This code is generally run in
		 * a short-lived context used for config file processing, so that will
		 * not be very long.)
		 */
	}
	else
		*extra = NULL;

	return true;
}

void
assign_synchronous_standby_names(const char *newval, void *extra)
{
	SyncRepConfig = (SyncRepConfigData *) extra;
}

void
assign_synchronous_commit(int newval, void *extra)
{
	switch (newval)
	{
		case SYNCHRONOUS_COMMIT_REMOTE_WRITE:
			SyncRepWaitMode = SYNC_REP_WAIT_WRITE;
			break;
		case SYNCHRONOUS_COMMIT_REMOTE_FLUSH:
			SyncRepWaitMode = SYNC_REP_WAIT_FLUSH;
			break;
		case SYNCHRONOUS_COMMIT_REMOTE_APPLY:
			SyncRepWaitMode = SYNC_REP_WAIT_APPLY;
			break;
		default:
			SyncRepWaitMode = SYNC_REP_NO_WAIT;
			break;
	}
}

/*
 * POLAR: This function is to implement ddl sync replica. Update the LSNs on
 * apply queue upon our latest state and wake backend process.
 *
 * Return false if there is no existed replica replication slot.
 */
bool
polar_release_ddl_waiters(void)
{
    volatile WalSndCtlData *walsndctl = WalSndCtl;
    XLogRecPtr	ddl_applyptr = InvalidXLogRecPtr;
    bool		replica_slot_exist = true;
    bool        replica_slot_all_active = true;
    int			ddl_numapply = 0;

    if (!(polar_enable_ddl_sync_mode && polar_enable_shared_storage_mode))
        return true;

    LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
    /*
     * No need to check whether we are a sync replica or not, just calculate
     * the apply synced positions among all replica(no standby).
     *
     * When no replica replication slot exists, replica_slot_exist is set to
     * false. Then it's futile for ddl backends to continue waiting, so we just
     * wake all backends.
     *
     * When some replica replication slots are inactive, replica_slot_all_active
     * is set to false. And we don't actually wake the ddl backend because some
     * slots may return to active. So we just skip the left procedure, waiting
     * for the next time to call this function.
     *
     * It's safe to check the current value without the lock, because it's only
     * ever updated by one process. But we must take the lock to change it.
     */
    replica_slot_exist = polar_get_ddl_applyptr(&ddl_applyptr,
            &replica_slot_all_active);

    if (!replica_slot_exist)
    {
        SyncRepWakeQueue(true, POLAR_SYNC_DDL_WAIT_APPLY);
        LWLockRelease(SyncRepLock);
        return false;
    }

    if (!replica_slot_all_active)
    {
        LWLockRelease(SyncRepLock);
        return true;
    }

    Assert(!XLogRecPtrIsInvalid(ddl_applyptr));

    /*
     * Set the lsn first so that when we wake backends, they will release up to
     * this location.
     *
     * Wake apply queue. we can use SyncRepWakeQueue to wake apply queue.
     */
    if (walsndctl->lsn[POLAR_SYNC_DDL_WAIT_APPLY] < ddl_applyptr)
    {
        walsndctl->lsn[POLAR_SYNC_DDL_WAIT_APPLY] = ddl_applyptr;
        ddl_numapply = SyncRepWakeQueue(false, POLAR_SYNC_DDL_WAIT_APPLY);
    }

    LWLockRelease(SyncRepLock);

    /* Wake backends which waits for ddl sync replica successfully. */
    if (ddl_numapply > 0)
        ereport(LOG,
                (errmsg("Acquire the smallest apply lsn among all replica and wake %d DDL waiting backends whose lsn are smaller than that.",
                        ddl_numapply)));

    elog(DEBUG3, "ddl sync released %d procs up to apply %X/%X",
         ddl_numapply, (uint32) (ddl_applyptr >> 32), (uint32) ddl_applyptr);

    return true;
}


/*
 * POLAR: Calculate the oldest apply lsn among all replica.
 *
 * By search all replica replication slots, we may find 3 situations:
 * 1: All slots existed are active, then we calculate the oldest apply position.
 * 2: Some slots existed are inactive, then set replica_slot_all_active = false.
 * 3: No slots exists, then we return false.
 *
 * Return false if there is no replica replication slot existed.
 * Otherwise return true and store the lsn positions into ddl_applyptr.
 *
 * On return, replica_slot_all_active is set to true if there are
 * some replica replication slots inactive.
 */
static bool
polar_get_ddl_applyptr(XLogRecPtr *ddl_applyptr, bool *replica_slot_all_active)
{
    bool        replica_slot_exist = false;
    uint32 		slotno = 0;

    /* Compute the oldest apply lsn among all replicas. */
    LWLockAcquire(ReplicationSlotControlLock, LW_SHARED);
    for (slotno = 0; slotno < max_replication_slots; slotno++)
    {
        ReplicationSlot *s = &ReplicationSlotCtl->replication_slots[slotno];
        XLogRecPtr	apply_lsn;

		/*
		 * 1. The slot must exist(s->in_use != 0)
		 * 2. The slot must be replica rather than standby(For standby,
		 * apply_lsn = slot->data.polar_replica_apply_lsn is InvalidRecPtr)
		 */
		if (!s->in_use)
			continue;

		SpinLockAcquire(&s->mutex);
		if (polar_enable_async_ddl_lock_replay)
			apply_lsn = s->polar_replica_lock_lsn;
		else
			apply_lsn = s->data.polar_replica_apply_lsn;
		SpinLockRelease(&s->mutex);

		if (XLogRecPtrIsInvalid(apply_lsn))
			continue;

        replica_slot_exist = true;

        /* Some replica slots are inactive. */
        if (s->active_pid == 0)
        {
            *replica_slot_all_active = false;
            ereport(LOG, (errmsg("Replica replication slot '%s' is inactive.", (char *) &s->data.name)));
        }

        if (!XLogRecPtrIsInvalid(apply_lsn) &&
            (XLogRecPtrIsInvalid(*ddl_applyptr)
             || *ddl_applyptr > apply_lsn))
            *ddl_applyptr = apply_lsn;

    }
    LWLockRelease(ReplicationSlotControlLock);
    return replica_slot_exist;
}

/*
 * POLAR: Cancel any wait currently in progress due to timeout.
 * The code is like SyncRepUpdateSyncStandbysDefined.
 */
static void
polar_sync_rep_update_timeout_flag(void)
{
	int			i;
	
	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
	if (WalSndCtl->sync_replication_timeout_enabled)
	{
		WalSndCtl->is_sync_replication_timeout = true;
		for (i = 0; i < NUM_SYNC_REP_WAIT_MODE; i++)
			SyncRepWakeQueue(true, i);
	}
	LWLockRelease(SyncRepLock);
}

void
polar_sync_rep_update_timeout_enabled_flag(void)
{
	bool		sync_replication_timeout_enabled = polar_sync_replication_timeout > 0;

	if (sync_replication_timeout_enabled != WalSndCtl->sync_replication_timeout_enabled)
	{
		LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);
		WalSndCtl->sync_replication_timeout_enabled = sync_replication_timeout_enabled;
		/*
		 * When the polar_sync_replication_timeout is set to zero,
	 	 * don't need to check for the timeout of synchronous replication.
		 */
		if (!WalSndCtl->sync_replication_timeout_enabled)
			WalSndCtl->is_sync_replication_timeout = false;
		LWLockRelease(SyncRepLock);
	}
}

static bool
polar_semi_sync_within_window(instr_time start_time, long timeout)
{
	instr_time	cur_time;

	INSTR_TIME_SET_CURRENT(cur_time);
	INSTR_TIME_SUBTRACT(cur_time, start_time);

	return (long) INSTR_TIME_GET_MILLISEC(cur_time) <= timeout;

}

static bool
polar_semi_sync_within_observation_window()
{
	if (!polar_enable_semi_sync_optimization)
		return false;

	return polar_semi_sync_within_window(WalSndCtl->polar_semi_sync_observation_window_start,
										 Max(polar_semi_sync_observation_window, 
										 	 2 * polar_sync_replication_timeout));
}

static bool
polar_semi_sync_within_backoff_window()
{
	if (!polar_enable_semi_sync_optimization)
		return false;

	if (WalSndCtl->polar_semi_sync_backoff_window == 0)
		return false;

	return polar_semi_sync_within_window(WalSndCtl->polar_semi_sync_backoff_window_start,
										 WalSndCtl->polar_semi_sync_backoff_window);
}

/*
 * POLAR: Semi-synchronous replication optimization under network jitter.
 * If we are now in an observation window, we should open or double the backoff window.
 * Otherwise, reset the backoff window to 0. 
 */
static void
polar_semi_sync_set_backoff_window()
{
	if (!polar_enable_semi_sync_optimization)
		return;

	LWLockAcquire(SyncRepLock, LW_EXCLUSIVE);

	if (polar_semi_sync_within_observation_window())
	{
		if (WalSndCtl->polar_semi_sync_backoff_window == 0)
			WalSndCtl->polar_semi_sync_backoff_window = polar_semi_sync_min_backoff_window;
		else
		{
			if ((WalSndCtl->polar_semi_sync_backoff_window << 1) <= polar_semi_sync_max_backoff_window)
				WalSndCtl->polar_semi_sync_backoff_window <<= 1;
		}

		INSTR_TIME_SET_CURRENT(WalSndCtl->polar_semi_sync_backoff_window_start);
		ereport(WARNING,
				(errmsg("start a new %ld s backoff window.", WalSndCtl->polar_semi_sync_backoff_window / 1000)));
	}
	else
	{
		WalSndCtl->polar_semi_sync_backoff_window = 0;
		ereport(WARNING,
				(errmsg("reset backoff window to 0.")));
	}

	LWLockRelease(SyncRepLock);
}
