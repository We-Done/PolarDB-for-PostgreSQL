/*-------------------------------------------------------------------------
 *
 * s_lock.c
 *	   Implementation of spinlocks.
 *
 * When waiting for a contended spinlock we loop tightly for awhile, then
 * delay using pg_usleep() and try again.  Preferably, "awhile" should be a
 * small multiple of the maximum time we expect a spinlock to be held.  100
 * iterations seems about right as an initial guess.  However, on a
 * uniprocessor the loop is a waste of cycles, while in a multi-CPU scenario
 * it's usually better to spin a bit longer than to call the kernel, so we try
 * to adapt the spin loop count depending on whether we seem to be in a
 * uniprocessor or multiprocessor.
 *
 * Note: you might think MIN_SPINS_PER_DELAY should be just 1, but you'd
 * be wrong; there are platforms where that can result in a "stuck
 * spinlock" failure.  This has been seen particularly on Alphas; it seems
 * that the first TAS after returning from kernel space will always fail
 * on that hardware.
 *
 * Once we do decide to block, we use randomly increasing pg_usleep()
 * delays. The first delay is 1 msec, then the delay randomly increases to
 * about one second, after which we reset to 1 msec and start again.  The
 * idea here is that in the presence of heavy contention we need to
 * increase the delay, else the spinlock holder may never get to run and
 * release the lock.  (Consider situation where spinlock holder has been
 * nice'd down in priority by the scheduler --- it will not get scheduled
 * until all would-be acquirers are sleeping, so if we always use a 1-msec
 * sleep, there is a real possibility of starvation.)  But we can't just
 * clamp the delay to an upper bound, else it would take a long time to
 * make a reasonable number of tries.
 *
 * We time out and declare error after NUM_DELAYS delays (thus, exactly
 * that many tries).  With the given settings, this will usually take 2 or
 * so minutes.  It seems better to fix the total number of tries (and thus
 * the probability of unintended failure) than to fix the total time
 * spent.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/lmgr/s_lock.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <time.h>
#include <unistd.h>

#include "common/pg_prng.h"
#include "port/atomics.h"
<<<<<<< HEAD
#include "utils/guc.h"

=======
#include "storage/s_lock.h"
#include "utils/wait_event.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

#define MIN_SPINS_PER_DELAY 10
#define MAX_SPINS_PER_DELAY 1000
#define NUM_DELAYS			1000
#define MIN_DELAY_USEC		1000L
#define MAX_DELAY_USEC		1000000L

#ifdef S_LOCK_TEST
/*
 * These are needed by pgstat_report_wait_start in the standalone compile of
 * s_lock_test.
 */
static uint32 local_my_wait_event_info;
uint32	   *my_wait_event_info = &local_my_wait_event_info;
#endif

static int	spins_per_delay = DEFAULT_SPINS_PER_DELAY;

/* POLAR: relax strategies for cas spin */
#ifdef POLAR_ARM_S_LOCK_RELAX
int			polar_spin_nops = 0;
int		   polar_spin_yield = 0;
#endif
/* POLAR end */

/*
 * s_lock_stuck() - complain about a stuck spinlock
 */
static void
s_lock_stuck(const char *file, int line, const char *func)
{
	if (!func)
		func = "(unknown)";
#if defined(S_LOCK_TEST)
	fprintf(stderr,
			"\nStuck spinlock detected at %s, %s:%d.\n",
			func, file, line);
	exit(1);
#else
	elog(PANIC, "stuck spinlock detected at %s, %s:%d",
		 func, file, line);
#endif
}

/*
 * s_lock(lock) - platform-independent portion of waiting for a spinlock.
 */
int
s_lock(volatile slock_t *lock, const char *file, int line, const char *func)
{
	SpinDelayStatus delayStatus;

	init_spin_delay(&delayStatus, file, line, func);

	while (TAS_SPIN(lock))
	{
		perform_spin_delay(&delayStatus);
	}

	finish_spin_delay(&delayStatus);

	return delayStatus.delays;
}

#ifdef USE_DEFAULT_S_UNLOCK
void
s_unlock(volatile slock_t *lock)
{
	*lock = 0;
}
#endif

/*
 * Wait while spinning on a contended spinlock.
 */
void
perform_spin_delay(SpinDelayStatus *status)
{
	/* CPU-specific delay each time through the loop */
	SPIN_DELAY();
	/* POLAR: nop for cpu relax */
#ifdef POLAR_ARM_S_LOCK_RELAX
	POLAR_S_NOP(status->polar_spin_nops);
#endif
	/* POLAR end */

	/* Block the process every spins_per_delay tries */
	if (++(status->spins) >= spins_per_delay)
	{
		if (++(status->delays) > NUM_DELAYS)
			s_lock_stuck(status->file, status->line, status->func);

		if (status->cur_delay == 0) /* first time to delay? */
			status->cur_delay = MIN_DELAY_USEC;

		/*
		 * Once we start sleeping, the overhead of reporting a wait event is
		 * justified. Actively spinning easily stands out in profilers, but
		 * sleeping with an exponential backoff is harder to spot...
		 *
		 * We might want to report something more granular at some point, but
		 * this is better than nothing.
		 */
		pgstat_report_wait_start(WAIT_EVENT_SPIN_DELAY);
		pg_usleep(status->cur_delay);
		pgstat_report_wait_end();

#if defined(S_LOCK_TEST)
		fprintf(stdout, "*");
		fflush(stdout);
#endif

		/* increase delay by a random fraction between 1X and 2X */
		status->cur_delay += (int) (status->cur_delay *
									pg_prng_double(&pg_global_prng_state) + 0.5);
		/* wrap back to minimum delay when max is exceeded */
		if (status->cur_delay > MAX_DELAY_USEC)
			status->cur_delay = MIN_DELAY_USEC;

		status->spins = 0;
	}

	/* POLAR: yield for cpu relax */
#ifdef POLAR_ARM_S_LOCK_RELAX
	if (status->polar_spin_yield)
		POLAR_S_YIELD();
#endif
	/* POLAR end */
}

/*
 * After acquiring a spinlock, update estimates about how long to loop.
 *
 * If we were able to acquire the lock without delaying, it's a good
 * indication we are in a multiprocessor.  If we had to delay, it's a sign
 * (but not a sure thing) that we are in a uniprocessor. Hence, we
 * decrement spins_per_delay slowly when we had to delay, and increase it
 * rapidly when we didn't.  It's expected that spins_per_delay will
 * converge to the minimum value on a uniprocessor and to the maximum
 * value on a multiprocessor.
 *
 * Note: spins_per_delay is local within our current process. We want to
 * average these observations across multiple backends, since it's
 * relatively rare for this function to even get entered, and so a single
 * backend might not live long enough to converge on a good value.  That
 * is handled by the two routines below.
 */
void
finish_spin_delay(SpinDelayStatus *status)
{
	if (status->cur_delay == 0)
	{
		/* we never had to delay */
		if (spins_per_delay < MAX_SPINS_PER_DELAY)
			spins_per_delay = Min(spins_per_delay + 100, MAX_SPINS_PER_DELAY);
	}
	else
	{
		if (spins_per_delay > MIN_SPINS_PER_DELAY)
			spins_per_delay = Max(spins_per_delay - 1, MIN_SPINS_PER_DELAY);
	}
}

/*
 * Set local copy of spins_per_delay during backend startup.
 *
 * NB: this has to be pretty fast as it is called while holding a spinlock
 */
void
set_spins_per_delay(int shared_spins_per_delay)
{
	spins_per_delay = shared_spins_per_delay;
}

/*
 * Update shared estimate of spins_per_delay during backend exit.
 *
 * NB: this has to be pretty fast as it is called while holding a spinlock
 */
int
update_spins_per_delay(int shared_spins_per_delay)
{
	/*
	 * We use an exponential moving average with a relatively slow adaption
	 * rate, so that noise in any one backend's result won't affect the shared
	 * value too much.  As long as both inputs are within the allowed range,
	 * the result must be too, so we need not worry about clamping the result.
	 *
	 * We deliberately truncate rather than rounding; this is so that single
	 * adjustments inside a backend can affect the shared estimate (see the
	 * asymmetric adjustment rules above).
	 */
	return (shared_spins_per_delay * 15 + spins_per_delay) / 16;
}


/*****************************************************************************/
#if defined(S_LOCK_TEST)

/*
 * test program for verifying a port's spinlock support.
 */

struct test_lock_struct
{
	char		pad1;
	slock_t		lock;
	char		pad2;
};

volatile struct test_lock_struct test_lock;

int
main()
{
	pg_prng_seed(&pg_global_prng_state, (uint64) time(NULL));

	test_lock.pad1 = test_lock.pad2 = 0x44;

	S_INIT_LOCK(&test_lock.lock);

	if (test_lock.pad1 != 0x44 || test_lock.pad2 != 0x44)
	{
		printf("S_LOCK_TEST: failed, declared datatype is wrong size\n");
		return 1;
	}

	if (!S_LOCK_FREE(&test_lock.lock))
	{
		printf("S_LOCK_TEST: failed, lock not initialized\n");
		return 1;
	}

	S_LOCK(&test_lock.lock);

	if (test_lock.pad1 != 0x44 || test_lock.pad2 != 0x44)
	{
		printf("S_LOCK_TEST: failed, declared datatype is wrong size\n");
		return 1;
	}

	if (S_LOCK_FREE(&test_lock.lock))
	{
		printf("S_LOCK_TEST: failed, lock not locked\n");
		return 1;
	}

	S_UNLOCK(&test_lock.lock);

	if (test_lock.pad1 != 0x44 || test_lock.pad2 != 0x44)
	{
		printf("S_LOCK_TEST: failed, declared datatype is wrong size\n");
		return 1;
	}

	if (!S_LOCK_FREE(&test_lock.lock))
	{
		printf("S_LOCK_TEST: failed, lock not unlocked\n");
		return 1;
	}

	S_LOCK(&test_lock.lock);

	if (test_lock.pad1 != 0x44 || test_lock.pad2 != 0x44)
	{
		printf("S_LOCK_TEST: failed, declared datatype is wrong size\n");
		return 1;
	}

	if (S_LOCK_FREE(&test_lock.lock))
	{
		printf("S_LOCK_TEST: failed, lock not re-locked\n");
		return 1;
	}

	printf("S_LOCK_TEST: this will print %d stars and then\n", NUM_DELAYS);
	printf("             exit with a 'stuck spinlock' message\n");
	printf("             if S_LOCK() and TAS() are working.\n");
	fflush(stdout);

	s_lock(&test_lock.lock, __FILE__, __LINE__, __func__);

	printf("S_LOCK_TEST: failed, lock not locked\n");
	return 1;
}

#endif							/* S_LOCK_TEST */

void
polar_compute_tv_delay(struct timespec *tv, long us)
{
	clock_gettime(CLOCK_MONOTONIC, tv);
	tv->tv_nsec += us * 1000;
	while (tv->tv_nsec >= 1000000000)
	{
		tv->tv_sec++;
		tv->tv_nsec -= 1000000000;
	}
}

void
polar_init_spin_delay_mt(polar_spin_delay_status_t *status, polar_wait_object_t *wait_obj, 
						 int spins_per_delay, int timeout_us)
{
	status->wait_obj = wait_obj;
	status->spins_per_delay = spins_per_delay;
	status->timeout_us = timeout_us;
	status->spins = -1;
	status->cur_delay = -1;
}

polar_wait_result_t
polar_perform_spin_delay_mt(polar_spin_delay_status_t *status, bool need_lock, bool need_wait)
{
	polar_wait_result_t wait_res = POLAR_WAIT_RES_SPIN;

	/* CPU-specific delay each time through the loop */
	SPIN_DELAY();

	/* Block the process every spins_per_delay tries */
	if (++(status->spins) >= status->spins_per_delay)
	{
		int res;
		struct timespec tv = {0, 0};

		if (!need_wait)
		{
			wait_res = POLAR_WAIT_RES_SPIN_OVER;
			return wait_res;
		}

		if (status->cur_delay == -1) 
			status->cur_delay = status->timeout_us;

		if (status->cur_delay != 0) 
			polar_compute_tv_delay(&tv, status->cur_delay);
		
		if (unlikely(need_lock))
			pthread_mutex_lock(&status->wait_obj->mutex);

		pg_atomic_fetch_add_u64(&status->wait_obj->stats.waiters, 1);

		if (status->cur_delay != 0) 
			res = pthread_cond_timedwait(&status->wait_obj->cond, &status->wait_obj->mutex, &tv);
		else
			res = pthread_cond_wait(&status->wait_obj->cond, &status->wait_obj->mutex);

		pg_atomic_fetch_sub_u64(&status->wait_obj->stats.waiters, 1);

		if (unlikely(need_lock))
			pthread_mutex_unlock(&status->wait_obj->mutex);

		if (res == ETIMEDOUT)
		{
			wait_res = POLAR_WAIT_RES_TIMEOUT;

			status->cur_delay = Min((status->cur_delay * 2), POLAR_MAX_WAIT_TIMEOUT_USEC);

			pg_atomic_fetch_add_u64(&status->wait_obj->stats.timeout_waits, 1);
		}
		else
		{
			wait_res = POLAR_WAIT_RES_WAKEUP;

			pg_atomic_fetch_add_u64(&status->wait_obj->stats.wakeup_waits, 1);
		}
	}

	return wait_res;
}

void
polar_reset_spin_delay_mt(polar_spin_delay_status_t *status)
{
	status->spins = -1;
	status->cur_delay = -1;
}
/* POLAR wal pipeline end */