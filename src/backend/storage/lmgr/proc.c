/*-------------------------------------------------------------------------
 *
 * proc.c
 *	  routines to manage per-process shared memory data structure
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/lmgr/proc.c
 *
 *-------------------------------------------------------------------------
 */
/*
 * Interface (a):
 *		ProcSleep(), ProcWakeup(),
 *
 * Waiting for a lock causes the backend to be put to sleep.  Whoever releases
 * the lock wakes the process up again (and gives it an error code so it knows
 * whether it was awoken on an error condition).
 *
 * Interface (b):
 *
 * ProcReleaseLocks -- frees the locks associated with current transaction
 *
 * ProcKill -- destroys the shared memory state (and locks)
 * associated with the process.
 */
#include "postgres.h"

#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#include "access/transam.h"
#include "access/twophase.h"
#include "access/xlogutils.h"
#include "commands/waitlsn.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/autovacuum.h"
#include "replication/slotsync.h"
#include "replication/syncrep.h"
#include "storage/condition_variable.h"
#include "storage/ipc.h"
#include "storage/lmgr.h"
#include "storage/pmsignal.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/procsignal.h"
#include "storage/spin.h"
#include "storage/standby.h"
#include "utils/timeout.h"
#include "utils/timestamp.h"
/* POLAR */
#include "replication/walsender.h"
#include "polar_dma/polar_dma.h"

<<<<<<< HEAD
/* POLAR px */
#include "px/px_timeout.h"
#include "px/px_vars.h"
#include "access/hash.h"
#include "postmaster/postmaster.h"

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
/* GUC variables */
int			DeadlockTimeout = 1000;
int			StatementTimeout = 0;
int			LockTimeout = 0;
int			IdleInTransactionSessionTimeout = 0;
int			TransactionTimeout = 0;
int			IdleSessionTimeout = 0;
bool		log_lock_waits = false;

/* Pointer to this process's PGPROC struct, if any */
PGPROC	   *MyProc = NULL;

/*
 * This spinlock protects the freelist of recycled PGPROC structures.
 * We cannot use an LWLock because the LWLock manager depends on already
 * having a PGPROC and a wait semaphore!  But these structures are touched
 * relatively infrequently (only at backend startup or shutdown) and not for
 * very long, so a spinlock is okay.
 */
NON_EXEC_STATIC slock_t *ProcStructLock = NULL;

/* Pointers to shared-memory structures */
PROC_HDR   *ProcGlobal = NULL;
NON_EXEC_STATIC PGPROC *AuxiliaryProcs = NULL;
PGPROC	   *PreparedXactProcs = NULL;

/* If we are waiting for a lock, this points to the associated LOCALLOCK */
static LOCALLOCK *lockAwaited = NULL;

static DeadLockState deadlock_state = DS_NOT_YET_CHECKED;

/* Is a deadlock check pending? */
static volatile sig_atomic_t got_deadlock_timeout;

static void RemoveProcFromArray(int code, Datum arg);
static void ProcKill(int code, Datum arg);
static void AuxiliaryProcKill(int code, Datum arg);
static void CheckDeadLock(void);

/* POLAR px */
static void	InitTraceId(const uint32 ip_hash, const uint16 port);


/*
 * Report shared-memory space needed by InitProcGlobal.
 */
Size
ProcGlobalShmemSize(void)
{
	Size		size = 0;
	Size		TotalProcs =
		add_size(MaxBackends, add_size(NUM_AUXILIARY_PROCS, max_prepared_xacts));
	Size		fpLockBitsSize,
				fpRelIdSize;

	/* ProcGlobal */
	size = add_size(size, sizeof(PROC_HDR));
	size = add_size(size, mul_size(TotalProcs, sizeof(PGPROC)));
	size = add_size(size, sizeof(slock_t));

	size = add_size(size, mul_size(TotalProcs, sizeof(*ProcGlobal->xids)));
	size = add_size(size, mul_size(TotalProcs, sizeof(*ProcGlobal->subxidStates)));
	size = add_size(size, mul_size(TotalProcs, sizeof(*ProcGlobal->statusFlags)));

	/*
	 * Memory needed for PGPROC fast-path lock arrays. Make sure the sizes are
	 * nicely aligned in each backend.
	 */
	fpLockBitsSize = MAXALIGN(FastPathLockGroupsPerBackend * sizeof(uint64));
	fpRelIdSize = MAXALIGN(FastPathLockGroupsPerBackend * sizeof(Oid) * FP_LOCK_SLOTS_PER_GROUP);

	size = add_size(size, mul_size(TotalProcs, (fpLockBitsSize + fpRelIdSize)));

	return size;
}

/*
 * Report number of semaphores needed by InitProcGlobal.
 */
int
ProcGlobalSemas(void)
{
	/*
	 * We need a sema per backend (including autovacuum), plus one for each
	 * auxiliary process.
	 */
	return MaxBackends + NUM_AUXILIARY_PROCS;
}

/*
 * InitProcGlobal -
 *	  Initialize the global process table during postmaster or standalone
 *	  backend startup.
 *
 *	  We also create all the per-process semaphores we will need to support
 *	  the requested number of backends.  We used to allocate semaphores
 *	  only when backends were actually started up, but that is bad because
 *	  it lets Postgres fail under load --- a lot of Unix systems are
 *	  (mis)configured with small limits on the number of semaphores, and
 *	  running out when trying to start another backend is a common failure.
 *	  So, now we grab enough semaphores to support the desired max number
 *	  of backends immediately at initialization --- if the sysadmin has set
 *	  MaxConnections, max_worker_processes, max_wal_senders, or
 *	  autovacuum_max_workers higher than his kernel will support, he'll
 *	  find out sooner rather than later.
 *
 *	  Another reason for creating semaphores here is that the semaphore
 *	  implementation typically requires us to create semaphores in the
 *	  postmaster, not in backends.
 *
 * Note: this is NOT called by individual backends under a postmaster,
 * not even in the EXEC_BACKEND case.  The ProcGlobal and AuxiliaryProcs
 * pointers must be propagated specially for EXEC_BACKEND operation.
 */
void
InitProcGlobal(void)
{
	PGPROC	   *procs;
	int			i,
				j;
	bool		found;
	uint32		TotalProcs = MaxBackends + NUM_AUXILIARY_PROCS + max_prepared_xacts;

	/* Used for setup of per-backend fast-path slots. */
	char	   *fpPtr,
			   *fpEndPtr PG_USED_FOR_ASSERTS_ONLY;
	Size		fpLockBitsSize,
				fpRelIdSize;

	/* Create the ProcGlobal shared structure */
	ProcGlobal = (PROC_HDR *)
		ShmemInitStruct("Proc Header", sizeof(PROC_HDR), &found);
	Assert(!found);

	/*
	 * Initialize the data structures.
	 */
	ProcGlobal->spins_per_delay = DEFAULT_SPINS_PER_DELAY;
<<<<<<< HEAD
	ProcGlobal->freeProcs = NULL;
	ProcGlobal->autovacFreeProcs = NULL;
	ProcGlobal->polarDispatcherFreeProcs = NULL;
	ProcGlobal->bgworkerFreeProcs = NULL;
	ProcGlobal->startupProc = NULL;
	ProcGlobal->startupProcPid = 0;
=======
	dlist_init(&ProcGlobal->freeProcs);
	dlist_init(&ProcGlobal->autovacFreeProcs);
	dlist_init(&ProcGlobal->bgworkerFreeProcs);
	dlist_init(&ProcGlobal->walsenderFreeProcs);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	ProcGlobal->startupBufferPinWaitBufId = -1;
	ProcGlobal->walwriterLatch = NULL;
	ProcGlobal->checkpointerLatch = NULL;
	pg_atomic_init_u32(&ProcGlobal->procArrayGroupFirst, INVALID_PROC_NUMBER);
	pg_atomic_init_u32(&ProcGlobal->clogGroupFirst, INVALID_PROC_NUMBER);

	/* POLAR px */
	pg_atomic_init_u32(&ProcGlobal->pxSessionCounter, 0);
	pg_atomic_init_u32(&ProcGlobal->pxWorkerCounter, 0);
	pg_atomic_init_u32(&ProcGlobal->sqlRequestCounter, 0);

	/* POLAR wal pipeline */
	ProcGlobal->polar_wal_pipeliner_latch = NULL;

	/*
	 * Create and initialize all the PGPROC structures we'll need.  There are
	 * five separate consumers: (1) normal backends, (2) autovacuum workers
	 * and the autovacuum launcher, (3) background workers, (4) auxiliary
	 * processes, and (5) prepared transactions.  Each PGPROC structure is
	 * dedicated to exactly one of these purposes, and they do not move
	 * between groups.
	 */
	procs = (PGPROC *) ShmemAlloc(TotalProcs * sizeof(PGPROC));
	MemSet(procs, 0, TotalProcs * sizeof(PGPROC));
	ProcGlobal->allProcs = procs;
	/* XXX allProcCount isn't really all of them; it excludes prepared xacts */
	ProcGlobal->allProcCount = MaxBackends + NUM_AUXILIARY_PROCS;

	/*
	 * Allocate arrays mirroring PGPROC fields in a dense manner. See
	 * PROC_HDR.
	 *
	 * XXX: It might make sense to increase padding for these arrays, given
	 * how hotly they are accessed.
	 */
	ProcGlobal->xids =
		(TransactionId *) ShmemAlloc(TotalProcs * sizeof(*ProcGlobal->xids));
	MemSet(ProcGlobal->xids, 0, TotalProcs * sizeof(*ProcGlobal->xids));
	ProcGlobal->subxidStates = (XidCacheStatus *) ShmemAlloc(TotalProcs * sizeof(*ProcGlobal->subxidStates));
	MemSet(ProcGlobal->subxidStates, 0, TotalProcs * sizeof(*ProcGlobal->subxidStates));
	ProcGlobal->statusFlags = (uint8 *) ShmemAlloc(TotalProcs * sizeof(*ProcGlobal->statusFlags));
	MemSet(ProcGlobal->statusFlags, 0, TotalProcs * sizeof(*ProcGlobal->statusFlags));

	/*
	 * Allocate arrays for fast-path locks. Those are variable-length, so
	 * can't be included in PGPROC directly. We allocate a separate piece of
	 * shared memory and then divide that between backends.
	 */
	fpLockBitsSize = MAXALIGN(FastPathLockGroupsPerBackend * sizeof(uint64));
	fpRelIdSize = MAXALIGN(FastPathLockGroupsPerBackend * sizeof(Oid) * FP_LOCK_SLOTS_PER_GROUP);

	fpPtr = ShmemAlloc(TotalProcs * (fpLockBitsSize + fpRelIdSize));
	MemSet(fpPtr, 0, TotalProcs * (fpLockBitsSize + fpRelIdSize));

	/* For asserts checking we did not overflow. */
	fpEndPtr = fpPtr + (TotalProcs * (fpLockBitsSize + fpRelIdSize));

	for (i = 0; i < TotalProcs; i++)
	{
		PGPROC	   *proc = &procs[i];

		/* Common initialization for all PGPROCs, regardless of type. */

		/*
		 * Set the fast-path lock arrays, and move the pointer. We interleave
		 * the two arrays, to (hopefully) get some locality for each backend.
		 */
		proc->fpLockBits = (uint64 *) fpPtr;
		fpPtr += fpLockBitsSize;

		proc->fpRelId = (Oid *) fpPtr;
		fpPtr += fpRelIdSize;

		Assert(fpPtr <= fpEndPtr);

		/*
		 * Set up per-PGPROC semaphore, latch, and fpInfoLock.  Prepared xact
		 * dummy PGPROCs don't need these though - they're never associated
		 * with a real process
		 */
		if (i < MaxBackends + NUM_AUXILIARY_PROCS)
		{
			proc->sem = PGSemaphoreCreate();
			InitSharedLatch(&(proc->procLatch));
			LWLockInitialize(&(proc->fpInfoLock), LWTRANCHE_LOCK_FASTPATH);
		}

		/*
		 * Newly created PGPROCs for normal backends, autovacuum and bgworkers
		 * must be queued up on the appropriate free list.  Because there can
		 * only ever be a small, fixed number of auxiliary processes, no free
		 * list is used in that case; InitAuxiliaryProcess() instead uses a
		 * linear search.   PGPROCs for prepared transactions are added to a
		 * free list by TwoPhaseShmemInit().
		 */
		if (i < MaxConnections)
		{
			/* PGPROC for normal backend, add to freeProcs list */
			dlist_push_tail(&ProcGlobal->freeProcs, &proc->links);
			proc->procgloballist = &ProcGlobal->freeProcs;
		}
		else if (i < MaxConnections + autovacuum_max_workers + 1)
		{
			/* PGPROC for AV launcher/worker, add to autovacFreeProcs list */
			dlist_push_tail(&ProcGlobal->autovacFreeProcs, &proc->links);
			proc->procgloballist = &ProcGlobal->autovacFreeProcs;
		}
		else if (i < MaxConnections + autovacuum_max_workers + 1 + max_worker_processes)
		{
			/* PGPROC for bgworker, add to bgworkerFreeProcs list */
			dlist_push_tail(&ProcGlobal->bgworkerFreeProcs, &proc->links);
			proc->procgloballist = &ProcGlobal->bgworkerFreeProcs;
		}
		else if (i < MaxConnections + autovacuum_max_workers + 1 + MaxPolarDispatcher)
		{
			/* PGPROC for bgworker, add to polarDispatcherFreeProcs list */
			procs[i].links.next = (SHM_QUEUE *) ProcGlobal->polarDispatcherFreeProcs;
			ProcGlobal->polarDispatcherFreeProcs = &procs[i];
			procs[i].procgloballist = &ProcGlobal->polarDispatcherFreeProcs;
		}
		else if (i < MaxBackends)
		{
			/* PGPROC for walsender, add to walsenderFreeProcs list */
			dlist_push_tail(&ProcGlobal->walsenderFreeProcs, &proc->links);
			proc->procgloballist = &ProcGlobal->walsenderFreeProcs;
		}

		/* Initialize myProcLocks[] shared memory queues. */
		for (j = 0; j < NUM_LOCK_PARTITIONS; j++)
			dlist_init(&(proc->myProcLocks[j]));

		/* Initialize lockGroupMembers list. */
<<<<<<< HEAD
		dlist_init(&procs[i].lockGroupMembers);
=======
		dlist_init(&proc->lockGroupMembers);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

		/*
		 * Initialize the atomic variables, otherwise, it won't be safe to
		 * access them for backends that aren't currently in use.
		 */
<<<<<<< HEAD
		pg_atomic_init_u32(&(procs[i].procArrayGroupNext), INVALID_PGPROCNO);
		pg_atomic_init_u32(&(procs[i].clogGroupNext), INVALID_PGPROCNO);
=======
		pg_atomic_init_u32(&(proc->procArrayGroupNext), INVALID_PROC_NUMBER);
		pg_atomic_init_u32(&(proc->clogGroupNext), INVALID_PROC_NUMBER);
		pg_atomic_init_u64(&(proc->waitStart), 0);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	/* Should have consumed exactly the expected amount of fast-path memory. */
	Assert(fpPtr == fpEndPtr);

	/*
	 * Save pointers to the blocks of PGPROC structures reserved for auxiliary
	 * processes and prepared transactions.
	 */
	AuxiliaryProcs = &procs[MaxBackends];
	PreparedXactProcs = &procs[MaxBackends + NUM_AUXILIARY_PROCS];

	/* Create ProcStructLock spinlock, too */
	ProcStructLock = (slock_t *) ShmemAlloc(sizeof(slock_t));
	SpinLockInit(ProcStructLock);
}

/* POLAR px */
static
void InitTraceId(const uint32 ip_hash, const uint16 port)
{
	sql_trace_id.ip_hash = ip_hash;
	sql_trace_id.port = port;
	sql_trace_id.seq = 0;
}

/*
 * InitProcess -- initialize a per-process PGPROC entry for this backend
 */
void
InitProcess(void)
{
<<<<<<< HEAD
	PGPROC	   *volatile *procgloballist;
	int 		pxLocalSessionId;  	/* this backend's PGPROC serial num */
	uint32 		ip_hash = 0;
=======
	dlist_head *procgloballist;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * ProcGlobal should be set up already (if we are a backend, we inherit
	 * this by fork() or EXEC_BACKEND mechanism from the postmaster).
	 */
	if (ProcGlobal == NULL)
		elog(PANIC, "proc header uninitialized");

	if (MyProc != NULL)
		elog(ERROR, "you already exist");

	/* Decide which list should supply our PGPROC. */
	if (AmAutoVacuumLauncherProcess() || AmAutoVacuumWorkerProcess())
		procgloballist = &ProcGlobal->autovacFreeProcs;
	else if (AmBackgroundWorkerProcess())
		procgloballist = &ProcGlobal->bgworkerFreeProcs;
<<<<<<< HEAD
	else if (IsPolarDispatcher)
		procgloballist = &ProcGlobal->polarDispatcherFreeProcs;
=======
	else if (AmWalSenderProcess())
		procgloballist = &ProcGlobal->walsenderFreeProcs;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	else
		procgloballist = &ProcGlobal->freeProcs;

	/*
	 * Try to get a proc struct from the appropriate free list.  If this
	 * fails, we must be out of PGPROC structures (not to mention semaphores).
	 *
	 * While we are holding the ProcStructLock, also copy the current shared
	 * estimate of spins_per_delay to local storage.
	 */
	SpinLockAcquire(ProcStructLock);

	set_spins_per_delay(ProcGlobal->spins_per_delay);

	if (!dlist_is_empty(procgloballist))
	{
		MyProc = dlist_container(PGPROC, links, dlist_pop_head_node(procgloballist));
		SpinLockRelease(ProcStructLock);
	}
	else
	{
		/*
		 * If we reach here, all the PGPROCs are in use.  This is one of the
		 * possible places to detect "too many backends", so give the standard
		 * error message.  XXX do we need to give a different failure message
		 * in the autovacuum case?
		 */
		SpinLockRelease(ProcStructLock);
		if (AmWalSenderProcess())
			ereport(FATAL,
					(errcode(ERRCODE_TOO_MANY_CONNECTIONS),
					 errmsg("number of requested standby connections exceeds \"max_wal_senders\" (currently %d)",
							max_wal_senders)));
		ereport(FATAL,
				(errcode(ERRCODE_TOO_MANY_CONNECTIONS),
				 errmsg("sorry, too many clients already")));
	}
<<<<<<< HEAD

	MyPgXact = &ProcGlobal->allPgXact[MyProc->pgprocno];
=======
	MyProcNumber = GetNumberFromPGProc(MyProc);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Cross-check that the PGPROC is of the type we expect; if this were not
	 * the case, it would get returned to the wrong list.
	 */
	Assert(MyProc->procgloballist == procgloballist);

	/*
	 * Now that we have a PGPROC, mark ourselves as an active postmaster
	 * child; this is so that the postmaster can detect it if we exit without
	 * cleaning up.  (XXX autovac launcher currently doesn't participate in
	 * this; it probably should.)
	 *
	 * Slot sync worker also does not participate in it, see comments atop
	 * 'struct bkend' in postmaster.c.
	 */
<<<<<<< HEAD
	if (IsUnderPostmaster && !IsAutoVacuumLauncherProcess() && !IsPolarDispatcher)
=======
	if (IsUnderPostmaster && !AmAutoVacuumLauncherProcess() &&
		!AmLogicalSlotSyncWorkerProcess())
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		MarkPostmasterChildActive();

	/*
	 * Initialize all fields of MyProc, except for those previously
	 * initialized by InitProcGlobal.
	 */
	dlist_node_init(&MyProc->links);
	MyProc->waitStatus = PROC_WAIT_STATUS_OK;
	MyProc->fpVXIDLock = false;
	MyProc->fpLocalTransactionId = InvalidLocalTransactionId;
<<<<<<< HEAD
	MyPgXact->xid = InvalidTransactionId;
	MyPgXact->xmin = InvalidTransactionId;
	MyPgXact->polar_csn = InvalidCommitSeqNo;
=======
	MyProc->xid = InvalidTransactionId;
	MyProc->xmin = InvalidTransactionId;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	MyProc->pid = MyProcPid;
	MyProc->vxid.procNumber = MyProcNumber;
	MyProc->vxid.lxid = InvalidLocalTransactionId;
	/* databaseId and roleId will be filled in later */
	MyProc->databaseId = InvalidOid;
	MyProc->roleId = InvalidOid;
	MyProc->tempNamespaceId = InvalidOid;
<<<<<<< HEAD
	MyProc->isBackgroundWorker = IsBackgroundWorker;

	/* POLAR: Shared Server */
	MyProc->isPolarDispatcher = IsPolarDispatcher;
	MyProc->polar_is_backend_dedicated = false;
	MyProc->polar_shared_session = NULL;

	MyPgXact->delayChkpt = false;
	MyPgXact->vacuumFlags = 0;
=======
	MyProc->isBackgroundWorker = AmBackgroundWorkerProcess();
	MyProc->delayChkptFlags = 0;
	MyProc->statusFlags = 0;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	/* NB -- autovac launcher intentionally does not set IS_AUTOVACUUM */
	if (AmAutoVacuumWorkerProcess())
		MyProc->statusFlags |= PROC_IS_AUTOVACUUM;
	MyProc->lwWaiting = LW_WS_NOT_WAITING;
	MyProc->lwWaitMode = 0;
	MyProc->waitLock = NULL;
	MyProc->waitProcLock = NULL;
<<<<<<< HEAD

	/*
	 * POLAR px: initialize the PX execution status.
	 *           It is set to false by default.
	 */
	pg_atomic_init_flag(&MyProc->polar_px_is_executing);
	if (px_role == PX_ROLE_PX)
		pg_atomic_test_set_flag(&MyProc->polar_px_is_executing); /* set to true */
	/* POLAR end */

	/* POLAR px */
	ip_hash = DatumGetUInt32(hash_any(
		(const unsigned char *)ListenAddresses, strlen(ListenAddresses)));
	InitTraceId(ip_hash, (uint16)PostPortNumber);

    pxLocalSessionId = pg_atomic_add_fetch_u32(&ProcGlobal->pxSessionCounter, 1);

    /*
	 * POLAR px
	 * A nonzero px_session_id uniquely identifies an PX client session
	 * over the lifetime of the entry postmaster process. A qDisp passes
	 * its px_session_id down to all of its qExecs. If this is a qExec,
	 * we have already received the px_session_id from the qDisp.
	 */
	if (px_role == PX_ROLE_QC && px_session_id == InvalidPxSessionId)
        px_session_id = pxLocalSessionId;

	elog(DEBUG1,"InitProcess(): px_session_id %d, px_role %d", px_session_id, px_role);
	/* POLAR end */

=======
	pg_atomic_write_u64(&MyProc->waitStart, 0);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#ifdef USE_ASSERT_CHECKING
	{
		int			i;

		/* Last process should have released all locks. */
		for (i = 0; i < NUM_LOCK_PARTITIONS; i++)
			Assert(dlist_is_empty(&(MyProc->myProcLocks[i])));
	}
#endif
	MyProc->recoveryConflictPending = false;

	/* Initialize fields for sync rep */
	MyProc->waitLSN = 0;
	MyProc->syncRepState = SYNC_REP_NOT_WAITING;
	dlist_node_init(&MyProc->syncRepLinks);

	/* POLAR: Initialize fields for read view min lsn*/
	pg_atomic_init_u64(&MyProc->polar_read_min_lsn, InvalidXLogRecPtr);

	/* Initialize fields for consensus rep */
	if (POLAR_ENABLE_DMA())
	{
		ConsensusProcInit(&MyProc->consensusInfo);
	}

	/* Initialize fields for group XID clearing. */
	MyProc->procArrayGroupMember = false;
	MyProc->procArrayGroupMemberXid = InvalidTransactionId;
<<<<<<< HEAD
	Assert(pg_atomic_read_u32(&MyProc->procArrayGroupNext) == INVALID_PGPROCNO);
=======
	Assert(pg_atomic_read_u32(&MyProc->procArrayGroupNext) == INVALID_PROC_NUMBER);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* Check that group locking fields are in a proper initial state. */
	Assert(MyProc->lockGroupLeader == NULL);
	Assert(dlist_is_empty(&MyProc->lockGroupMembers));

	/* Initialize wait event information. */
	{
		int 	i;
		MyProc->wait_event_info = 0;
		for (i = 0; i < PGPROC_WAIT_STACK_LEN; i++)
		{
			MyProc->wait_object[i] = PGPROC_INVAILD_WAIT_OBJ;
			MyProc->wait_type[i] = PGPROC_INVAILD_WAIT_OBJ;
			INSTR_TIME_SET_ZERO(MyProc->wait_time[i]);
		}
		MyProc->cur_wait_stack_index = PGPROC_INVAILD_WAIT_OBJ;
	}

	/* Initialize fields for group transaction status update. */
	MyProc->clogGroupMember = false;
	MyProc->clogGroupMemberXid = InvalidTransactionId;
	MyProc->clogGroupMemberXidStatus = TRANSACTION_STATUS_IN_PROGRESS;
	MyProc->clogGroupMemberPage = -1;
	MyProc->clogGroupMemberLsn = InvalidXLogRecPtr;
<<<<<<< HEAD
	Assert(pg_atomic_read_u32(&MyProc->clogGroupNext) == INVALID_PGPROCNO);
=======
	Assert(pg_atomic_read_u32(&MyProc->clogGroupNext) == INVALID_PROC_NUMBER);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Acquire ownership of the PGPROC's latch, so that we can use WaitLatch
	 * on it.  That allows us to repoint the process latch, which so far
	 * points to process local one, to the shared one.
	 */
	OwnLatch(&MyProc->procLatch);
	SwitchToSharedLatch();

	/* now that we have a proc, report wait events to shared memory */
	pgstat_set_wait_event_storage(&MyProc->wait_event_info);

	/*
	 * We might be reusing a semaphore that belonged to a failed process. So
	 * be careful and reinitialize its value here.  (This is not strictly
	 * necessary anymore, but seems like a good idea for cleanliness.)
	 */
	PGSemaphoreReset(MyProc->sem);

	/*
	 * Arrange to clean up at backend exit.
	 */
	on_shmem_exit(ProcKill, 0);

	/*
	 * Now that we have a PGPROC, we could try to acquire locks, so initialize
	 * local state needed for LWLocks, and the deadlock checker.
	 */
	InitLWLockAccess();
	InitDeadLockChecking();

<<<<<<< HEAD
	/* POLAR: init lock state */
	polar_init_lock_access();
=======
#ifdef EXEC_BACKEND

	/*
	 * Initialize backend-local pointers to all the shared data structures.
	 * (We couldn't do this until now because it needs LWLocks.)
	 */
	if (IsUnderPostmaster)
		AttachSharedMemoryStructs();
#endif
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

/*
 * InitProcessPhase2 -- make MyProc visible in the shared ProcArray.
 *
 * This is separate from InitProcess because we can't acquire LWLocks until
 * we've created a PGPROC, but in the EXEC_BACKEND case ProcArrayAdd won't
 * work until after we've done AttachSharedMemoryStructs.
 */
void
InitProcessPhase2(void)
{
	Assert(MyProc != NULL);

	/*
	 * Add our PGPROC to the PGPROC array in shared memory.
	 */
	ProcArrayAdd(MyProc);

	/*
	 * Arrange to clean that up at backend exit.
	 */
	on_shmem_exit(RemoveProcFromArray, 0);
}

/*
 * InitAuxiliaryProcess -- create a PGPROC entry for an auxiliary process
 *
 * This is called by bgwriter and similar processes so that they will have a
 * MyProc value that's real enough to let them wait for LWLocks.  The PGPROC
 * and sema that are assigned are one of the extra ones created during
 * InitProcGlobal.
 *
 * Auxiliary processes are presently not expected to wait for real (lockmgr)
 * locks, so we need not set up the deadlock checker.  They are never added
 * to the ProcArray or the sinval messaging mechanism, either.  They also
 * don't get a VXID assigned, since this is only useful when we actually
 * hold lockmgr locks.
 *
 * Startup process however uses locks but never waits for them in the
 * normal backend sense. Startup process also takes part in sinval messaging
 * as a sendOnly process, so never reads messages from sinval queue. So
 * Startup process does have a VXID and does show up in pg_locks.
 */
void
InitAuxiliaryProcess(void)
{
	PGPROC	   *auxproc;
	int			proctype;

	/*
	 * ProcGlobal should be set up already (if we are a backend, we inherit
	 * this by fork() or EXEC_BACKEND mechanism from the postmaster).
	 */
	if (ProcGlobal == NULL || AuxiliaryProcs == NULL)
		elog(PANIC, "proc header uninitialized");

	if (MyProc != NULL)
		elog(ERROR, "you already exist");

	/*
	 * We use the ProcStructLock to protect assignment and releasing of
	 * AuxiliaryProcs entries.
	 *
	 * While we are holding the ProcStructLock, also copy the current shared
	 * estimate of spins_per_delay to local storage.
	 */
	SpinLockAcquire(ProcStructLock);

	set_spins_per_delay(ProcGlobal->spins_per_delay);

	/*
	 * Find a free auxproc ... *big* trouble if there isn't one ...
	 */
	for (proctype = 0; proctype < NUM_AUXILIARY_PROCS; proctype++)
	{
		auxproc = &AuxiliaryProcs[proctype];
		if (auxproc->pid == 0)
			break;
	}
	if (proctype >= NUM_AUXILIARY_PROCS)
	{
		SpinLockRelease(ProcStructLock);
		elog(FATAL, "all AuxiliaryProcs are in use");
	}

	/* Mark auxiliary proc as in use by me */
	/* use volatile pointer to prevent code rearrangement */
	((volatile PGPROC *) auxproc)->pid = MyProcPid;

	SpinLockRelease(ProcStructLock);

	MyProc = auxproc;
	MyProcNumber = GetNumberFromPGProc(MyProc);

	/*
	 * Initialize all fields of MyProc, except for those previously
	 * initialized by InitProcGlobal.
	 */
	dlist_node_init(&MyProc->links);
	MyProc->waitStatus = PROC_WAIT_STATUS_OK;
	MyProc->fpVXIDLock = false;
	MyProc->fpLocalTransactionId = InvalidLocalTransactionId;
<<<<<<< HEAD
	MyPgXact->xid = InvalidTransactionId;
	MyPgXact->xmin = InvalidTransactionId;
	MyPgXact->polar_csn = InvalidCommitSeqNo;
	MyProc->backendId = InvalidBackendId;
	MyProc->databaseId = InvalidOid;
	MyProc->roleId = InvalidOid;
	MyProc->tempNamespaceId = InvalidOid;
	MyProc->isBackgroundWorker = IsBackgroundWorker;

	/* POLAR: Shared Server */
	MyProc->isPolarDispatcher = IsPolarDispatcher;
	MyProc->polar_is_backend_dedicated = false;
	MyProc->polar_shared_session = NULL;

	MyPgXact->delayChkpt = false;
	MyPgXact->vacuumFlags = 0;
	MyProc->lwWaiting = false;
	MyProc->lwWaitMode = 0;
	MyProc->waitLock = NULL;
	MyProc->waitProcLock = NULL;
	/* Initialize wait event information. */
	{
		int 	i;
		for (i = 0; i < PGPROC_WAIT_STACK_LEN; i++)
		{
			MyProc->wait_object[i] = PGPROC_INVAILD_WAIT_OBJ;
			MyProc->wait_type[i] = PGPROC_INVAILD_WAIT_OBJ;
			INSTR_TIME_SET_ZERO(MyProc->wait_time[i]);
		}
		MyProc->cur_wait_stack_index = PGPROC_INVAILD_WAIT_OBJ;
	}

=======
	MyProc->xid = InvalidTransactionId;
	MyProc->xmin = InvalidTransactionId;
	MyProc->vxid.procNumber = INVALID_PROC_NUMBER;
	MyProc->vxid.lxid = InvalidLocalTransactionId;
	MyProc->databaseId = InvalidOid;
	MyProc->roleId = InvalidOid;
	MyProc->tempNamespaceId = InvalidOid;
	MyProc->isBackgroundWorker = AmBackgroundWorkerProcess();
	MyProc->delayChkptFlags = 0;
	MyProc->statusFlags = 0;
	MyProc->lwWaiting = LW_WS_NOT_WAITING;
	MyProc->lwWaitMode = 0;
	MyProc->waitLock = NULL;
	MyProc->waitProcLock = NULL;
	pg_atomic_write_u64(&MyProc->waitStart, 0);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#ifdef USE_ASSERT_CHECKING
	{
		int			i;

		/* Last process should have released all locks. */
		for (i = 0; i < NUM_LOCK_PARTITIONS; i++)
			Assert(dlist_is_empty(&(MyProc->myProcLocks[i])));
	}
#endif

	/* Initialize fields for consensus rep */
	if (POLAR_ENABLE_DMA())
	{
		ConsensusProcInit(&MyProc->consensusInfo);
	}

	/*
	 * Acquire ownership of the PGPROC's latch, so that we can use WaitLatch
	 * on it.  That allows us to repoint the process latch, which so far
	 * points to process local one, to the shared one.
	 */
	OwnLatch(&MyProc->procLatch);
	SwitchToSharedLatch();

	/* now that we have a proc, report wait events to shared memory */
	pgstat_set_wait_event_storage(&MyProc->wait_event_info);

	/* Check that group locking fields are in a proper initial state. */
	Assert(MyProc->lockGroupLeader == NULL);
	Assert(dlist_is_empty(&MyProc->lockGroupMembers));

	/*
	 * We might be reusing a semaphore that belonged to a failed process. So
	 * be careful and reinitialize its value here.  (This is not strictly
	 * necessary anymore, but seems like a good idea for cleanliness.)
	 */
	PGSemaphoreReset(MyProc->sem);

	/*
	 * Arrange to clean up at process exit.
	 */
	on_shmem_exit(AuxiliaryProcKill, Int32GetDatum(proctype));

	/*
	 * Now that we have a PGPROC, we could try to acquire lightweight locks.
	 * Initialize local state needed for them.  (Heavyweight locks cannot be
	 * acquired in aux processes.)
	 */
	InitLWLockAccess();

#ifdef EXEC_BACKEND

	/*
	 * Initialize backend-local pointers to all the shared data structures.
	 * (We couldn't do this until now because it needs LWLocks.)
	 */
	if (IsUnderPostmaster)
		AttachSharedMemoryStructs();
#endif
}

/*
 * Used from bufmgr to share the value of the buffer that Startup waits on,
 * or to reset the value to "not waiting" (-1). This allows processing
 * of recovery conflicts for buffer pins. Set is made before backends look
 * at this value, so locking not required, especially since the set is
 * an atomic integer set operation.
 */
void
SetStartupBufferPinWaitBufId(int bufid)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile PROC_HDR *procglobal = ProcGlobal;

	procglobal->startupBufferPinWaitBufId = bufid;
}

/*
 * Used by backends when they receive a request to check for buffer pin waits.
 */
int
GetStartupBufferPinWaitBufId(void)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile PROC_HDR *procglobal = ProcGlobal;

	return procglobal->startupBufferPinWaitBufId;
}

/*
 * Check whether there are at least N free PGPROC objects.  If false is
 * returned, *nfree will be set to the number of free PGPROC objects.
 * Otherwise, *nfree will be set to n.
 *
 * Note: this is designed on the assumption that N will generally be small.
 */
bool
HaveNFreeProcs(int n, int *nfree)
{
	dlist_iter	iter;

	Assert(n > 0);
	Assert(nfree);

	SpinLockAcquire(ProcStructLock);

	*nfree = 0;
	dlist_foreach(iter, &ProcGlobal->freeProcs)
	{
		(*nfree)++;
		if (*nfree == n)
			break;
	}

	SpinLockRelease(ProcStructLock);

	return (*nfree == n);
}

/*
 * Check if the current process is awaiting a lock.
 */
bool
IsWaitingForLock(void)
{
	if (lockAwaited == NULL)
		return false;

	return true;
}

/*
 * Cancel any pending wait for lock, when aborting a transaction, and revert
 * any strong lock count acquisition for a lock being acquired.
 *
 * (Normally, this would only happen if we accept a cancel/die
 * interrupt while waiting; but an ereport(ERROR) before or during the lock
 * wait is within the realm of possibility, too.)
 */
void
LockErrorCleanup(void)
{
	LWLock	   *partitionLock;
	DisableTimeoutParams timeouts[2];

	HOLD_INTERRUPTS();

	AbortStrongLockAcquire();

	/* Nothing to do if we weren't waiting for a lock */
	if (lockAwaited == NULL)
	{
		RESUME_INTERRUPTS();
		return;
	}

	/*
	 * Turn off the deadlock and lock timeout timers, if they are still
	 * running (see ProcSleep).  Note we must preserve the LOCK_TIMEOUT
	 * indicator flag, since this function is executed before
	 * ProcessInterrupts when responding to SIGINT; else we'd lose the
	 * knowledge that the SIGINT came from a lock timeout and not an external
	 * source.
	 */
	timeouts[0].id = DEADLOCK_TIMEOUT;
	timeouts[0].keep_indicator = false;
	timeouts[1].id = LOCK_TIMEOUT;
	timeouts[1].keep_indicator = true;
	disable_timeouts(timeouts, 2);

	/* Unlink myself from the wait queue, if on it (might not be anymore!) */
	partitionLock = LockHashPartitionLock(lockAwaited->hashcode);
	LWLockAcquire(partitionLock, LW_EXCLUSIVE);

	if (!dlist_node_is_detached(&MyProc->links))
	{
		/* We could not have been granted the lock yet */
		RemoveFromWaitQueue(MyProc, lockAwaited->hashcode);
	}
	else
	{
		/*
		 * Somebody kicked us off the lock queue already.  Perhaps they
		 * granted us the lock, or perhaps they detected a deadlock. If they
		 * did grant us the lock, we'd better remember it in our local lock
		 * table.
		 */
		if (MyProc->waitStatus == PROC_WAIT_STATUS_OK)
			GrantAwaitedLock();
	}

	lockAwaited = NULL;

	LWLockRelease(partitionLock);

	RESUME_INTERRUPTS();
}


/*
 * ProcReleaseLocks() -- release locks associated with current transaction
 *			at main transaction commit or abort
 *
 * At main transaction commit, we release standard locks except session locks.
 * At main transaction abort, we release all locks including session locks.
 *
 * Advisory locks are released only if they are transaction-level;
 * session-level holds remain, whether this is a commit or not.
 *
 * At subtransaction commit, we don't release any locks (so this func is not
 * needed at all); we will defer the releasing to the parent transaction.
 * At subtransaction abort, we release all locks held by the subtransaction;
 * this is implemented by retail releasing of the locks under control of
 * the ResourceOwner mechanism.
 */
void
ProcReleaseLocks(bool isCommit)
{
	if (!MyProc)
		return;
	/* If waiting, get off wait queue (should only be needed after error) */
	LockErrorCleanup();
	/* Release standard locks, including session-level if aborting */
	LockReleaseAll(DEFAULT_LOCKMETHOD, !isCommit);
	/* Release transaction-level advisory locks */
	LockReleaseAll(USER_LOCKMETHOD, false);
}


/*
 * RemoveProcFromArray() -- Remove this process from the shared ProcArray.
 */
static void
RemoveProcFromArray(int code, Datum arg)
{
	Assert(MyProc != NULL);
	ProcArrayRemove(MyProc, InvalidTransactionId);
}

/*
 * ProcKill() -- Destroy the per-proc data structure for
 *		this process. Release any of its held LW locks.
 */
static void
ProcKill(int code, Datum arg)
{
	PGPROC	   *proc;
	dlist_head *procgloballist;

	Assert(MyProc != NULL);

	/* not safe if forked by system(), etc. */
	if (MyProc->pid != (int) getpid())
		elog(PANIC, "ProcKill() called in child process");

	/* Make sure we're out of the sync rep lists */
	SyncRepCleanupAtProcExit();

#ifdef USE_ASSERT_CHECKING
	{
		int			i;

		/* Last process should have released all locks. */
		for (i = 0; i < NUM_LOCK_PARTITIONS; i++)
			Assert(dlist_is_empty(&(MyProc->myProcLocks[i])));
	}
#endif

	/*
	 * Release any LW locks I am holding.  There really shouldn't be any, but
	 * it's cheap to check again before we cut the knees off the LWLock
	 * facility by releasing our PGPROC ...
	 */
	LWLockReleaseAll();

	/*
	 * Cleanup waiting for LSN if any.
	 */
	WaitLSNCleanup();

	/* Cancel any pending condition variable sleep, too */
	ConditionVariableCancelSleep();

	/*
	 * Detach from any lock group of which we are a member.  If the leader
	 * exits before all other group members, its PGPROC will remain allocated
	 * until the last group process exits; that process must return the
	 * leader's PGPROC to the appropriate list.
	 */
	if (MyProc->lockGroupLeader != NULL)
	{
		PGPROC	   *leader = MyProc->lockGroupLeader;
		LWLock	   *leader_lwlock = LockHashPartitionLockByProc(leader);

		LWLockAcquire(leader_lwlock, LW_EXCLUSIVE);
		Assert(!dlist_is_empty(&leader->lockGroupMembers));
		dlist_delete(&MyProc->lockGroupLink);
		if (dlist_is_empty(&leader->lockGroupMembers))
		{
			leader->lockGroupLeader = NULL;
			if (leader != MyProc)
			{
				procgloballist = leader->procgloballist;

				/* Leader exited first; return its PGPROC. */
				SpinLockAcquire(ProcStructLock);
				dlist_push_head(procgloballist, &leader->links);
				SpinLockRelease(ProcStructLock);
			}
		}
		else if (leader != MyProc)
			MyProc->lockGroupLeader = NULL;
		LWLockRelease(leader_lwlock);
	}

	/*
	 * Reset MyLatch to the process local one.  This is so that signal
	 * handlers et al can continue using the latch after the shared latch
	 * isn't ours anymore.
	 *
	 * Similarly, stop reporting wait events to MyProc->wait_event_info.
	 *
	 * After that clear MyProc and disown the shared latch.
	 */
	SwitchBackToLocalLatch();
	pgstat_reset_wait_event_storage();

	proc = MyProc;
	MyProc = NULL;
	MyProcNumber = INVALID_PROC_NUMBER;
	DisownLatch(&proc->procLatch);

	/* Mark the proc no longer in use */
	proc->pid = 0;
	proc->vxid.procNumber = INVALID_PROC_NUMBER;
	proc->vxid.lxid = InvalidTransactionId;

	procgloballist = proc->procgloballist;
	SpinLockAcquire(ProcStructLock);

	/*
	 * If we're still a member of a locking group, that means we're a leader
	 * which has somehow exited before its children.  The last remaining child
	 * will release our PGPROC.  Otherwise, release it now.
	 */
	if (proc->lockGroupLeader == NULL)
	{
		/* Since lockGroupLeader is NULL, lockGroupMembers should be empty. */
		Assert(dlist_is_empty(&proc->lockGroupMembers));

		/* Return PGPROC structure (and semaphore) to appropriate freelist */
		dlist_push_tail(procgloballist, &proc->links);
	}

	/* Update shared estimate of spins_per_delay */
	ProcGlobal->spins_per_delay = update_spins_per_delay(ProcGlobal->spins_per_delay);

	SpinLockRelease(ProcStructLock);

	/*
	 * This process is no longer present in shared memory in any meaningful
	 * way, so tell the postmaster we've cleaned up acceptably well. (XXX
	 * autovac launcher should be included here someday)
	 *
	 * Slot sync worker is also not a postmaster child, so skip this shared
	 * memory related processing here.
	 */
<<<<<<< HEAD
	if (IsUnderPostmaster && !IsAutoVacuumLauncherProcess() && !IsPolarDispatcher)
=======
	if (IsUnderPostmaster && !AmAutoVacuumLauncherProcess() &&
		!AmLogicalSlotSyncWorkerProcess())
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		MarkPostmasterChildInactive();

	/* wake autovac launcher if needed -- see comments in FreeWorkerInfo */
	if (AutovacuumLauncherPid != 0)
		kill(AutovacuumLauncherPid, SIGUSR2);
}

/*
 * AuxiliaryProcKill() -- Cut-down version of ProcKill for auxiliary
 *		processes (bgwriter, etc).  The PGPROC and sema are not released, only
 *		marked as not-in-use.
 */
static void
AuxiliaryProcKill(int code, Datum arg)
{
	int			proctype = DatumGetInt32(arg);
	PGPROC	   *auxproc PG_USED_FOR_ASSERTS_ONLY;
	PGPROC	   *proc;

	Assert(proctype >= 0 && proctype < NUM_AUXILIARY_PROCS);

	/* not safe if forked by system(), etc. */
	if (MyProc->pid != (int) getpid())
		elog(PANIC, "AuxiliaryProcKill() called in child process");

	auxproc = &AuxiliaryProcs[proctype];

	Assert(MyProc == auxproc);

	/* Release any LW locks I am holding (see notes above) */
	LWLockReleaseAll();

	/* Cancel any pending condition variable sleep, too */
	ConditionVariableCancelSleep();

	/* look at the equivalent ProcKill() code for comments */
	SwitchBackToLocalLatch();
	pgstat_reset_wait_event_storage();

	proc = MyProc;
	MyProc = NULL;
	MyProcNumber = INVALID_PROC_NUMBER;
	DisownLatch(&proc->procLatch);

	SpinLockAcquire(ProcStructLock);

	/* Mark auxiliary proc no longer in use */
	proc->pid = 0;
	proc->vxid.procNumber = INVALID_PROC_NUMBER;
	proc->vxid.lxid = InvalidTransactionId;

	/* Update shared estimate of spins_per_delay */
	ProcGlobal->spins_per_delay = update_spins_per_delay(ProcGlobal->spins_per_delay);

	SpinLockRelease(ProcStructLock);
}

/*
 * AuxiliaryPidGetProc -- get PGPROC for an auxiliary process
 * given its PID
 *
 * Returns NULL if not found.
 */
PGPROC *
AuxiliaryPidGetProc(int pid)
{
	PGPROC	   *result = NULL;
	int			index;

	if (pid == 0)				/* never match dummy PGPROCs */
		return NULL;

	for (index = 0; index < NUM_AUXILIARY_PROCS; index++)
	{
		PGPROC	   *proc = &AuxiliaryProcs[index];

		if (proc->pid == pid)
		{
			result = proc;
			break;
		}
	}
	return result;
}


/*
 * ProcSleep -- put a process to sleep on the specified lock
 *
 * Caller must have set MyProc->heldLocks to reflect locks already held
 * on the lockable object by this process (under all XIDs).
 *
 * It's not actually guaranteed that we need to wait when this function is
 * called, because it could be that when we try to find a position at which
 * to insert ourself into the wait queue, we discover that we must be inserted
 * ahead of everyone who wants a lock that conflict with ours. In that case,
 * we get the lock immediately. Because of this, it's sensible for this function
 * to have a dontWait argument, despite the name.
 *
 * The lock table's partition lock must be held at entry, and will be held
 * at exit.
 *
 * Result: PROC_WAIT_STATUS_OK if we acquired the lock, PROC_WAIT_STATUS_ERROR
 * if not (if dontWait = true, we would have had to wait; if dontWait = false,
 * this is a deadlock).
 *
 * ASSUME: that no one will fiddle with the queue until after
 *		we release the partition lock.
 *
 * NOTES: The process queue is now a priority queue for locking.
 */
ProcWaitStatus
ProcSleep(LOCALLOCK *locallock, LockMethod lockMethodTable, bool dontWait)
{
	LOCKMODE	lockmode = locallock->tag.mode;
	LOCK	   *lock = locallock->lock;
	PROCLOCK   *proclock = locallock->proclock;
	uint32		hashcode = locallock->hashcode;
	LWLock	   *partitionLock = LockHashPartitionLock(hashcode);
	dclist_head *waitQueue = &lock->waitProcs;
	PGPROC	   *insert_before = NULL;
	LOCKMASK	myHeldLocks = MyProc->heldLocks;
	TimestampTz standbyWaitStart = 0;
	bool		early_deadlock = false;
	bool		allow_autovacuum_cancel = true;
	bool		logged_recovery_conflict = false;
	ProcWaitStatus myWaitStatus;
	PGPROC	   *leader = MyProc->lockGroupLeader;

	/*
	 * If group locking is in use, locks held by members of my locking group
	 * need to be included in myHeldLocks.  This is not required for relation
	 * extension lock which conflict among group members. However, including
	 * them in myHeldLocks will give group members the priority to get those
	 * locks as compared to other backends which are also trying to acquire
	 * those locks.  OTOH, we can avoid giving priority to group members for
	 * that kind of locks, but there doesn't appear to be a clear advantage of
	 * the same.
	 */
	if (leader != NULL)
	{
		dlist_iter	iter;

		dlist_foreach(iter, &lock->procLocks)
		{
			PROCLOCK   *otherproclock;

			otherproclock = dlist_container(PROCLOCK, lockLink, iter.cur);

			if (otherproclock->groupLeader == leader)
				myHeldLocks |= otherproclock->holdMask;
		}
	}

	/*
	 * Determine where to add myself in the wait queue.
	 *
	 * Normally I should go at the end of the queue.  However, if I already
	 * hold locks that conflict with the request of any previous waiter, put
	 * myself in the queue just in front of the first such waiter. This is not
	 * a necessary step, since deadlock detection would move me to before that
	 * waiter anyway; but it's relatively cheap to detect such a conflict
	 * immediately, and avoid delaying till deadlock timeout.
	 *
	 * Special case: if I find I should go in front of some waiter, check to
	 * see if I conflict with already-held locks or the requests before that
	 * waiter.  If not, then just grant myself the requested lock immediately.
	 * This is the same as the test for immediate grant in LockAcquire, except
	 * we are only considering the part of the wait queue before my insertion
	 * point.
	 */
	if (myHeldLocks != 0 && !dclist_is_empty(waitQueue))
	{
		LOCKMASK	aheadRequests = 0;
		dlist_iter	iter;

		dclist_foreach(iter, waitQueue)
		{
			PGPROC	   *proc = dlist_container(PGPROC, links, iter.cur);

			/*
			 * If we're part of the same locking group as this waiter, its
			 * locks neither conflict with ours nor contribute to
			 * aheadRequests.
			 */
			if (leader != NULL && leader == proc->lockGroupLeader)
				continue;

			/* Must he wait for me? */
			if (lockMethodTable->conflictTab[proc->waitLockMode] & myHeldLocks)
			{
				/* Must I wait for him ? */
				if (lockMethodTable->conflictTab[lockmode] & proc->heldLocks)
				{
					/*
					 * Yes, so we have a deadlock.  Easiest way to clean up
					 * correctly is to call RemoveFromWaitQueue(), but we
					 * can't do that until we are *on* the wait queue. So, set
					 * a flag to check below, and break out of loop.  Also,
					 * record deadlock info for later message.
					 */
					RememberSimpleDeadLock(MyProc, lockmode, lock, proc);
					early_deadlock = true;
					break;
				}
				/* I must go before this waiter.  Check special case. */
				if ((lockMethodTable->conflictTab[lockmode] & aheadRequests) == 0 &&
					!LockCheckConflicts(lockMethodTable, lockmode, lock,
										proclock))
				{
					/* Skip the wait and just grant myself the lock. */
					GrantLock(lock, proclock, lockmode);
					GrantAwaitedLock();
					return PROC_WAIT_STATUS_OK;
				}

				/* Put myself into wait queue before conflicting process */
				insert_before = proc;
				break;
			}
			/* Nope, so advance to next waiter */
			aheadRequests |= LOCKBIT_ON(proc->waitLockMode);
		}
	}

	/*
	 * At this point we know that we'd really need to sleep. If we've been
	 * commanded not to do that, bail out.
	 */
	if (dontWait)
		return PROC_WAIT_STATUS_ERROR;

	/*
	 * Insert self into queue, at the position determined above.
	 */
	if (insert_before)
		dclist_insert_before(waitQueue, &insert_before->links, &MyProc->links);
	else
		dclist_push_tail(waitQueue, &MyProc->links);

	lock->waitMask |= LOCKBIT_ON(lockmode);

	/* Set up wait information in PGPROC object, too */
	MyProc->waitLock = lock;
	MyProc->waitProcLock = proclock;
	MyProc->waitLockMode = lockmode;

	MyProc->waitStatus = PROC_WAIT_STATUS_WAITING;

	/*
	 * If we detected deadlock, give up without waiting.  This must agree with
	 * CheckDeadLock's recovery code.
	 */
	if (early_deadlock)
	{
		RemoveFromWaitQueue(MyProc, hashcode);
		return PROC_WAIT_STATUS_ERROR;
	}

	/* mark that we are waiting for a lock */
	lockAwaited = locallock;

	/*
	 * Release the lock table's partition lock.
	 *
	 * NOTE: this may also cause us to exit critical-section state, possibly
	 * allowing a cancel/die interrupt to be accepted. This is OK because we
	 * have recorded the fact that we are waiting for a lock, and so
	 * LockErrorCleanup will clean up if cancel/die happens.
	 */
	LWLockRelease(partitionLock);

	/*
	 * Also, now that we will successfully clean up after an ereport, it's
	 * safe to check to see if there's a buffer pin deadlock against the
	 * Startup process.  Of course, that's only necessary if we're doing Hot
	 * Standby and are not the Startup process ourselves.
	 */
	if (RecoveryInProgress() && !InRecovery)
		CheckRecoveryConflictDeadlock();

	/* Reset deadlock_state before enabling the timeout handler */
	deadlock_state = DS_NOT_YET_CHECKED;
	got_deadlock_timeout = false;

	/* POLAR px: reset the PX wait lock timeout flag. */
	px_wait_lock_alert = false;
	/* POLAR end */

	/*
	 * Set timer so we can wake up after awhile and check for a deadlock. If a
	 * deadlock is detected, the handler sets MyProc->waitStatus =
<<<<<<< HEAD
	 * STATUS_ERROR, allowing us to know that we must report failure rather
	 * than success.
=======
	 * PROC_WAIT_STATUS_ERROR, allowing us to know that we must report failure
	 * rather than success.
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	 *
	 * By delaying the check until we've waited for a bit, we can avoid
	 * running the rather expensive deadlock-check code in most cases.
	 *
	 * If LockTimeout is set, also enable the timeout for that.  We can save a
	 * few cycles by enabling both timeout sources in one call.
	 *
	 * If InHotStandby we set lock waits slightly later for clarity with other
	 * code.
	 */
	if (!InHotStandby)
	{
		if (LockTimeout > 0)
		{
			EnableTimeoutParams timeouts[2];

			timeouts[0].id = DEADLOCK_TIMEOUT;
			timeouts[0].type = TMPARAM_AFTER;
			timeouts[0].delay_ms = DeadlockTimeout;
			timeouts[1].id = LOCK_TIMEOUT;
			timeouts[1].type = TMPARAM_AFTER;
			timeouts[1].delay_ms = LockTimeout;
			enable_timeouts(timeouts, 2);
		}
		else
			enable_timeout_after(DEADLOCK_TIMEOUT, DeadlockTimeout);

<<<<<<< HEAD
		/* POLAR px: enable timer if timeout interval is specified by GUC. */
		if (px_wait_lock_timeout > 0)
			enable_timeout_after(px_wait_lock_timer_id,
								 px_wait_lock_timeout);
		/* POLAR end */
=======
		/*
		 * Use the current time obtained for the deadlock timeout timer as
		 * waitStart (i.e., the time when this process started waiting for the
		 * lock). Since getting the current time newly can cause overhead, we
		 * reuse the already-obtained time to avoid that overhead.
		 *
		 * Note that waitStart is updated without holding the lock table's
		 * partition lock, to avoid the overhead by additional lock
		 * acquisition. This can cause "waitstart" in pg_locks to become NULL
		 * for a very short period of time after the wait started even though
		 * "granted" is false. This is OK in practice because we can assume
		 * that users are likely to look at "waitstart" when waiting for the
		 * lock for a long time.
		 */
		pg_atomic_write_u64(&MyProc->waitStart,
							get_timeout_start_time(DEADLOCK_TIMEOUT));
	}
	else if (log_recovery_conflict_waits)
	{
		/*
		 * Set the wait start timestamp if logging is enabled and in hot
		 * standby.
		 */
		standbyWaitStart = GetCurrentTimestamp();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	/*
	 * If somebody wakes us between LWLockRelease and WaitLatch, the latch
	 * will not wait. But a set latch does not necessarily mean that the lock
	 * is free now, as there are many other sources for latch sets than
	 * somebody releasing the lock.
	 *
	 * We process interrupts whenever the latch has been set, so cancel/die
	 * interrupts are processed quickly. This means we must not mind losing
	 * control to a cancel/die interrupt here.  We don't, because we have no
	 * shared-state-change work to do after being granted the lock (the
	 * grantor did it all).  We do have to worry about canceling the deadlock
	 * timeout and updating the locallock table, but if we lose control to an
	 * error, LockErrorCleanup will fix that up.
	 */
	do
	{
		if (InHotStandby)
		{
			bool		maybe_log_conflict =
				(standbyWaitStart != 0 && !logged_recovery_conflict);

			/* Set a timer and wait for that or for the lock to be granted */
			ResolveRecoveryConflictWithLock(locallock->tag.lock,
											maybe_log_conflict);

			/*
			 * Emit the log message if the startup process is waiting longer
			 * than deadlock_timeout for recovery conflict on lock.
			 */
			if (maybe_log_conflict)
			{
				TimestampTz now = GetCurrentTimestamp();

				if (TimestampDifferenceExceeds(standbyWaitStart, now,
											   DeadlockTimeout))
				{
					VirtualTransactionId *vxids;
					int			cnt;

					vxids = GetLockConflicts(&locallock->tag.lock,
											 AccessExclusiveLock, &cnt);

					/*
					 * Log the recovery conflict and the list of PIDs of
					 * backends holding the conflicting lock. Note that we do
					 * logging even if there are no such backends right now
					 * because the startup process here has already waited
					 * longer than deadlock_timeout.
					 */
					LogRecoveryConflict(PROCSIG_RECOVERY_CONFLICT_LOCK,
										standbyWaitStart, now,
										cnt > 0 ? vxids : NULL, true);
					logged_recovery_conflict = true;
				}
			}
		}
		else
		{
			(void) WaitLatch(MyLatch, WL_LATCH_SET | WL_EXIT_ON_PM_DEATH, 0,
							 PG_WAIT_LOCK | locallock->tag.lock.locktag_type);
			ResetLatch(MyLatch);

			/* POLAR px: handle the wait lock timeout. */
			if (px_wait_lock_alert)
			{
				px_cancel_wait_lock();
				px_wait_lock_alert = false;
			}
			/* POLAR end */

			/* check for deadlocks first, as that's probably log-worthy */
			if (got_deadlock_timeout)
			{
				CheckDeadLock();
				got_deadlock_timeout = false;
			}
			CHECK_FOR_INTERRUPTS();
		}

		/*
		 * waitStatus could change from PROC_WAIT_STATUS_WAITING to something
		 * else asynchronously.  Read it just once per loop to prevent
		 * surprising behavior (such as missing log messages).
		 */
		myWaitStatus = *((volatile ProcWaitStatus *) &MyProc->waitStatus);

		/*
		 * If we are not deadlocked, but are waiting on an autovacuum-induced
		 * task, send a signal to interrupt it.
		 */
		if (deadlock_state == DS_BLOCKED_BY_AUTOVACUUM && allow_autovacuum_cancel)
		{
			PGPROC	   *autovac = GetBlockingAutoVacuumPgproc();
			uint8		statusFlags;
			uint8		lockmethod_copy;
			LOCKTAG		locktag_copy;

			/*
			 * Grab info we need, then release lock immediately.  Note this
			 * coding means that there is a tiny chance that the process
			 * terminates its current transaction and starts a different one
			 * before we have a change to send the signal; the worst possible
			 * consequence is that a for-wraparound vacuum is canceled.  But
			 * that could happen in any case unless we were to do kill() with
			 * the lock held, which is much more undesirable.
			 */
			LWLockAcquire(ProcArrayLock, LW_EXCLUSIVE);
			statusFlags = ProcGlobal->statusFlags[autovac->pgxactoff];
			lockmethod_copy = lock->tag.locktag_lockmethodid;
			locktag_copy = lock->tag;
			LWLockRelease(ProcArrayLock);

			/*
			 * Only do it if the worker is not working to protect against Xid
			 * wraparound.
			 */
			if ((statusFlags & PROC_IS_AUTOVACUUM) &&
				!(statusFlags & PROC_VACUUM_FOR_WRAPAROUND))
			{
				int			pid = autovac->pid;

				/* report the case, if configured to do so */
				if (message_level_is_interesting(DEBUG1))
				{
					StringInfoData locktagbuf;
					StringInfoData logbuf;	/* errdetail for server log */

					initStringInfo(&locktagbuf);
					initStringInfo(&logbuf);
					DescribeLockTag(&locktagbuf, &locktag_copy);
					appendStringInfo(&logbuf,
									 "Process %d waits for %s on %s.",
									 MyProcPid,
									 GetLockmodeName(lockmethod_copy, lockmode),
									 locktagbuf.data);

					ereport(DEBUG1,
							(errmsg_internal("sending cancel to blocking autovacuum PID %d",
											 pid),
							 errdetail_log("%s", logbuf.data)));

					pfree(locktagbuf.data);
					pfree(logbuf.data);
				}

				/* send the autovacuum worker Back to Old Kent Road */
				if (kill(pid, SIGINT) < 0)
				{
					/*
					 * There's a race condition here: once we release the
					 * ProcArrayLock, it's possible for the autovac worker to
					 * close up shop and exit before we can do the kill().
					 * Therefore, we do not whinge about no-such-process.
					 * Other errors such as EPERM could conceivably happen if
					 * the kernel recycles the PID fast enough, but such cases
					 * seem improbable enough that it's probably best to issue
					 * a warning if we see some other errno.
					 */
					if (errno != ESRCH)
						ereport(WARNING,
								(errmsg("could not send signal to process %d: %m",
										pid)));
				}
			}

			/* prevent signal from being sent again more than once */
			allow_autovacuum_cancel = false;
		}

		/*
		 * If awoken after the deadlock check interrupt has run, and
		 * log_lock_waits is on, then report about the wait.
		 */
		if (log_lock_waits && deadlock_state != DS_NOT_YET_CHECKED)
		{
			StringInfoData buf,
						lock_waiters_sbuf,
						lock_holders_sbuf;
			const char *modename;
			long		secs;
			int			usecs;
			long		msecs;
			dlist_iter	proc_iter;
			PROCLOCK   *curproclock;
			bool		first_holder = true,
						first_waiter = true;
			int			lockHoldersNum = 0;

			initStringInfo(&buf);
			initStringInfo(&lock_waiters_sbuf);
			initStringInfo(&lock_holders_sbuf);

			DescribeLockTag(&buf, &locallock->tag.lock);
			modename = GetLockmodeName(locallock->tag.lock.locktag_lockmethodid,
									   lockmode);
			TimestampDifference(get_timeout_start_time(DEADLOCK_TIMEOUT),
								GetCurrentTimestamp(),
								&secs, &usecs);
			msecs = secs * 1000 + usecs / 1000;
			usecs = usecs % 1000;

			/*
			 * we loop over the lock's procLocks to gather a list of all
			 * holders and waiters. Thus we will be able to provide more
			 * detailed information for lock debugging purposes.
			 *
			 * lock->procLocks contains all processes which hold or wait for
			 * this lock.
			 */

			LWLockAcquire(partitionLock, LW_SHARED);

			dlist_foreach(proc_iter, &lock->procLocks)
			{
				curproclock =
					dlist_container(PROCLOCK, lockLink, proc_iter.cur);

				/*
				 * we are a waiter if myProc->waitProcLock == curproclock; we
				 * are a holder if it is NULL or something different
				 */
				if (curproclock->tag.myProc->waitProcLock == curproclock)
				{
					if (first_waiter)
					{
						appendStringInfo(&lock_waiters_sbuf, "%d",
										 curproclock->tag.myProc->pid);
						first_waiter = false;
					}
					else
						appendStringInfo(&lock_waiters_sbuf, ", %d",
										 curproclock->tag.myProc->pid);
				}
				else
				{
					if (first_holder)
					{
						appendStringInfo(&lock_holders_sbuf, "%d",
										 curproclock->tag.myProc->pid);
						first_holder = false;
					}
					else
						appendStringInfo(&lock_holders_sbuf, ", %d",
										 curproclock->tag.myProc->pid);

					lockHoldersNum++;
				}
			}

			LWLockRelease(partitionLock);

			if (deadlock_state == DS_SOFT_DEADLOCK)
				ereport(LOG,
						(errmsg("process %d avoided deadlock for %s on %s by rearranging queue order after %ld.%03d ms",
								MyProcPid, modename, buf.data, msecs, usecs),
						 (errdetail_log_plural("Process holding the lock: %s. Wait queue: %s.",
											   "Processes holding the lock: %s. Wait queue: %s.",
											   lockHoldersNum, lock_holders_sbuf.data, lock_waiters_sbuf.data))));
			else if (deadlock_state == DS_HARD_DEADLOCK)
			{
				/*
				 * This message is a bit redundant with the error that will be
				 * reported subsequently, but in some cases the error report
				 * might not make it to the log (eg, if it's caught by an
				 * exception handler), and we want to ensure all long-wait
				 * events get logged.
				 */
				ereport(LOG,
						(errmsg("process %d detected deadlock while waiting for %s on %s after %ld.%03d ms",
								MyProcPid, modename, buf.data, msecs, usecs),
						 (errdetail_log_plural("Process holding the lock: %s. Wait queue: %s.",
											   "Processes holding the lock: %s. Wait queue: %s.",
											   lockHoldersNum, lock_holders_sbuf.data, lock_waiters_sbuf.data))));
			}

			if (myWaitStatus == PROC_WAIT_STATUS_WAITING)
				ereport(LOG,
						(errmsg("process %d still waiting for %s on %s after %ld.%03d ms",
								MyProcPid, modename, buf.data, msecs, usecs),
						 (errdetail_log_plural("Process holding the lock: %s. Wait queue: %s.",
											   "Processes holding the lock: %s. Wait queue: %s.",
											   lockHoldersNum, lock_holders_sbuf.data, lock_waiters_sbuf.data))));
			else if (myWaitStatus == PROC_WAIT_STATUS_OK)
				ereport(LOG,
						(errmsg("process %d acquired %s on %s after %ld.%03d ms",
								MyProcPid, modename, buf.data, msecs, usecs)));
			else
			{
				Assert(myWaitStatus == PROC_WAIT_STATUS_ERROR);

				/*
				 * Currently, the deadlock checker always kicks its own
				 * process, which means that we'll only see
				 * PROC_WAIT_STATUS_ERROR when deadlock_state ==
				 * DS_HARD_DEADLOCK, and there's no need to print redundant
				 * messages.  But for completeness and future-proofing, print
				 * a message if it looks like someone else kicked us off the
				 * lock.
				 */
				if (deadlock_state != DS_HARD_DEADLOCK)
					ereport(LOG,
							(errmsg("process %d failed to acquire %s on %s after %ld.%03d ms",
									MyProcPid, modename, buf.data, msecs, usecs),
							 (errdetail_log_plural("Process holding the lock: %s. Wait queue: %s.",
												   "Processes holding the lock: %s. Wait queue: %s.",
												   lockHoldersNum, lock_holders_sbuf.data, lock_waiters_sbuf.data))));
			}

			/*
			 * At this point we might still need to wait for the lock. Reset
			 * state so we don't print the above messages again.
			 */
			deadlock_state = DS_NO_DEADLOCK;

			pfree(buf.data);
			pfree(lock_holders_sbuf.data);
			pfree(lock_waiters_sbuf.data);
		}
	} while (myWaitStatus == PROC_WAIT_STATUS_WAITING);

	/*
	 * Disable the timers, if they are still running.  As in LockErrorCleanup,
	 * we must preserve the LOCK_TIMEOUT indicator flag: if a lock timeout has
	 * already caused QueryCancelPending to become set, we want the cancel to
	 * be reported as a lock timeout, not a user cancel.
	 */
	if (!InHotStandby)
	{
		if (LockTimeout > 0)
		{
			DisableTimeoutParams timeouts[2];

			timeouts[0].id = DEADLOCK_TIMEOUT;
			timeouts[0].keep_indicator = false;
			timeouts[1].id = LOCK_TIMEOUT;
			timeouts[1].keep_indicator = true;
			disable_timeouts(timeouts, 2);
		}
		else
			disable_timeout(DEADLOCK_TIMEOUT, false);

		/* POLAR px: disable the wait lock timer. */
		if (px_wait_lock_timeout > 0)
			disable_timeout(px_wait_lock_timer_id, false);
		/* POLAR end */
	}

	/*
	 * Emit the log message if recovery conflict on lock was resolved but the
	 * startup process waited longer than deadlock_timeout for it.
	 */
	if (InHotStandby && logged_recovery_conflict)
		LogRecoveryConflict(PROCSIG_RECOVERY_CONFLICT_LOCK,
							standbyWaitStart, GetCurrentTimestamp(),
							NULL, false);

	/*
	 * Re-acquire the lock table's partition lock.  We have to do this to hold
	 * off cancel/die interrupts before we can mess with lockAwaited (else we
	 * might have a missed or duplicated locallock update).
	 */
	LWLockAcquire(partitionLock, LW_EXCLUSIVE);

	/*
	 * We no longer want LockErrorCleanup to do anything.
	 */
	lockAwaited = NULL;

	/*
	 * If we got the lock, be sure to remember it in the locallock table.
	 */
	if (MyProc->waitStatus == PROC_WAIT_STATUS_OK)
		GrantAwaitedLock();

	/*
	 * We don't have to do anything else, because the awaker did all the
	 * necessary update of the lock table and MyProc.
	 */
	return MyProc->waitStatus;
}


/*
 * ProcWakeup -- wake up a process by setting its latch.
 *
 *	 Also remove the process from the wait queue and set its links invalid.
 *
 * The appropriate lock partition lock must be held by caller.
 *
 * XXX: presently, this code is only used for the "success" case, and only
 * works correctly for that case.  To clean up in failure case, would need
 * to twiddle the lock's request counts too --- see RemoveFromWaitQueue.
 * Hence, in practice the waitStatus parameter must be PROC_WAIT_STATUS_OK.
 */
void
ProcWakeup(PGPROC *proc, ProcWaitStatus waitStatus)
{
	if (dlist_node_is_detached(&proc->links))
		return;

	Assert(proc->waitStatus == PROC_WAIT_STATUS_WAITING);

	/* Remove process from wait queue */
	dclist_delete_from_thoroughly(&proc->waitLock->waitProcs, &proc->links);

	/* Clean up process' state and pass it the ok/fail signal */
	proc->waitLock = NULL;
	proc->waitProcLock = NULL;
	proc->waitStatus = waitStatus;
	pg_atomic_write_u64(&MyProc->waitStart, 0);

	/* And awaken it */
	SetLatch(&proc->procLatch);
}

/*
 * ProcLockWakeup -- routine for waking up processes when a lock is
 *		released (or a prior waiter is aborted).  Scan all waiters
 *		for lock, waken any that are no longer blocked.
 *
 * The appropriate lock partition lock must be held by caller.
 */
void
ProcLockWakeup(LockMethod lockMethodTable, LOCK *lock)
{
	dclist_head *waitQueue = &lock->waitProcs;
	LOCKMASK	aheadRequests = 0;
	dlist_mutable_iter miter;

	if (dclist_is_empty(waitQueue))
		return;

	dclist_foreach_modify(miter, waitQueue)
	{
		PGPROC	   *proc = dlist_container(PGPROC, links, miter.cur);
		LOCKMODE	lockmode = proc->waitLockMode;

		/*
		 * Waken if (a) doesn't conflict with requests of earlier waiters, and
		 * (b) doesn't conflict with already-held locks.
		 */
		if ((lockMethodTable->conflictTab[lockmode] & aheadRequests) == 0 &&
			!LockCheckConflicts(lockMethodTable, lockmode, lock,
								proc->waitProcLock))
		{
			/* OK to waken */
			GrantLock(lock, proc->waitProcLock, lockmode);
			/* removes proc from the lock's waiting process queue */
			ProcWakeup(proc, PROC_WAIT_STATUS_OK);
		}
		else
		{
			/*
			 * Lock conflicts: Don't wake, but remember requested mode for
			 * later checks.
			 */
			aheadRequests |= LOCKBIT_ON(lockmode);
		}
	}
}

/*
 * CheckDeadLock
 *
 * We only get to this routine, if DEADLOCK_TIMEOUT fired while waiting for a
 * lock to be released by some other process.  Check if there's a deadlock; if
 * not, just return.  (But signal ProcSleep to log a message, if
 * log_lock_waits is true.)  If we have a real deadlock, remove ourselves from
 * the lock's wait queue and signal an error to ProcSleep.
 */
static void
CheckDeadLock(void)
{
	int			i;

	/*
	 * Acquire exclusive lock on the entire shared lock data structures. Must
	 * grab LWLocks in partition-number order to avoid LWLock deadlock.
	 *
	 * Note that the deadlock check interrupt had better not be enabled
	 * anywhere that this process itself holds lock partition locks, else this
	 * will wait forever.  Also note that LWLockAcquire creates a critical
	 * section, so that this routine cannot be interrupted by cancel/die
	 * interrupts.
	 */
	for (i = 0; i < NUM_LOCK_PARTITIONS; i++)
		LWLockAcquire(LockHashPartitionLockByIndex(i), LW_EXCLUSIVE);

	/*
	 * Check to see if we've been awoken by anyone in the interim.
	 *
	 * If we have, we can return and resume our transaction -- happy day.
	 * Before we are awoken the process releasing the lock grants it to us so
	 * we know that we don't have to wait anymore.
	 *
	 * We check by looking to see if we've been unlinked from the wait queue.
	 * This is safe because we hold the lock partition lock.
	 */
	if (MyProc->links.prev == NULL ||
		MyProc->links.next == NULL)
		goto check_done;

#ifdef LOCK_DEBUG
	if (Debug_deadlocks)
		DumpAllLocks();
#endif

	/* Run the deadlock check, and set deadlock_state for use by ProcSleep */
	deadlock_state = DeadLockCheck(MyProc);

	if (deadlock_state == DS_HARD_DEADLOCK)
	{
		/*
		 * Oops.  We have a deadlock.
		 *
		 * Get this process out of wait state. (Note: we could do this more
		 * efficiently by relying on lockAwaited, but use this coding to
		 * preserve the flexibility to kill some other transaction than the
		 * one detecting the deadlock.)
		 *
		 * RemoveFromWaitQueue sets MyProc->waitStatus to
		 * PROC_WAIT_STATUS_ERROR, so ProcSleep will report an error after we
		 * return from the signal handler.
		 */
		Assert(MyProc->waitLock != NULL);
		RemoveFromWaitQueue(MyProc, LockTagHashCode(&(MyProc->waitLock->tag)));

		/*
		 * We're done here.  Transaction abort caused by the error that
		 * ProcSleep will raise will cause any other locks we hold to be
		 * released, thus allowing other processes to wake up; we don't need
		 * to do that here.  NOTE: an exception is that releasing locks we
		 * hold doesn't consider the possibility of waiters that were blocked
		 * behind us on the lock we just failed to get, and might now be
		 * wakable because we're not in front of them anymore.  However,
		 * RemoveFromWaitQueue took care of waking up any such processes.
		 */
	}

	/*
	 * And release locks.  We do this in reverse order for two reasons: (1)
	 * Anyone else who needs more than one of the locks will be trying to lock
	 * them in increasing order; we don't want to release the other process
	 * until it can get all the locks it needs. (2) This avoids O(N^2)
	 * behavior inside LWLockRelease.
	 */
check_done:
	for (i = NUM_LOCK_PARTITIONS; --i >= 0;)
		LWLockRelease(LockHashPartitionLockByIndex(i));
}

/*
 * CheckDeadLockAlert - Handle the expiry of deadlock_timeout.
 *
 * NB: Runs inside a signal handler, be careful.
 */
void
CheckDeadLockAlert(void)
{
	int			save_errno = errno;

	got_deadlock_timeout = true;

	/*
	 * Have to set the latch again, even if handle_sig_alarm already did. Back
	 * then got_deadlock_timeout wasn't yet set... It's unlikely that this
	 * ever would be a problem, but setting a set latch again is cheap.
	 *
	 * Note that, when this function runs inside procsignal_sigusr1_handler(),
	 * the handler function sets the latch again after the latch is set here.
	 */
	SetLatch(MyLatch);
	errno = save_errno;
}

/*
 * ProcWaitForSignal - wait for a signal from another backend.
 *
 * As this uses the generic process latch the caller has to be robust against
 * unrelated wakeups: Always check that the desired state has occurred, and
 * wait again if not.
 */
void
ProcWaitForSignal(uint32 wait_event_info)
{
	(void) WaitLatch(MyLatch, WL_LATCH_SET | WL_EXIT_ON_PM_DEATH, 0,
					 wait_event_info);
	ResetLatch(MyLatch);
	CHECK_FOR_INTERRUPTS();
}

/*
 * ProcSendSignal - set the latch of a backend identified by ProcNumber
 */
void
ProcSendSignal(ProcNumber procNumber)
{
	if (procNumber < 0 || procNumber >= ProcGlobal->allProcCount)
		elog(ERROR, "procNumber out of range");

	SetLatch(&ProcGlobal->allProcs[procNumber].procLatch);
}

/*
 * BecomeLockGroupLeader - designate process as lock group leader
 *
 * Once this function has returned, other processes can join the lock group
 * by calling BecomeLockGroupMember.
 */
void
BecomeLockGroupLeader(void)
{
	LWLock	   *leader_lwlock;

	/* If we already did it, we don't need to do it again. */
	if (MyProc->lockGroupLeader == MyProc)
		return;

	/* We had better not be a follower. */
	Assert(MyProc->lockGroupLeader == NULL);

	/* Create single-member group, containing only ourselves. */
	leader_lwlock = LockHashPartitionLockByProc(MyProc);
	LWLockAcquire(leader_lwlock, LW_EXCLUSIVE);
	MyProc->lockGroupLeader = MyProc;
	dlist_push_head(&MyProc->lockGroupMembers, &MyProc->lockGroupLink);
	LWLockRelease(leader_lwlock);
}

/*
 * BecomeLockGroupMember - designate process as lock group member
 *
 * This is pretty straightforward except for the possibility that the leader
 * whose group we're trying to join might exit before we manage to do so;
 * and the PGPROC might get recycled for an unrelated process.  To avoid
 * that, we require the caller to pass the PID of the intended PGPROC as
 * an interlock.  Returns true if we successfully join the intended lock
 * group, and false if not.
 */
bool
BecomeLockGroupMember(PGPROC *leader, int pid)
{
	LWLock	   *leader_lwlock;
	bool		ok = false;

	/* Group leader can't become member of group */
	Assert(MyProc != leader);

	/* Can't already be a member of a group */
	Assert(MyProc->lockGroupLeader == NULL);

	/* PID must be valid. */
	Assert(pid != 0);

	/*
	 * Get lock protecting the group fields.  Note LockHashPartitionLockByProc
	 * calculates the proc number based on the PGPROC slot without looking at
	 * its contents, so we will acquire the correct lock even if the leader
	 * PGPROC is in process of being recycled.
	 */
	leader_lwlock = LockHashPartitionLockByProc(leader);
	LWLockAcquire(leader_lwlock, LW_EXCLUSIVE);

	/* Is this the leader we're looking for? */
	if (leader->pid == pid && leader->lockGroupLeader == leader)
	{
		/* OK, join the group */
		ok = true;
		MyProc->lockGroupLeader = leader;
		dlist_push_tail(&leader->lockGroupMembers, &MyProc->lockGroupLink);
	}
	LWLockRelease(leader_lwlock);

	return ok;
}
