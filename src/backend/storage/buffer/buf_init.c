/*-------------------------------------------------------------------------
 *
 * buf_init.c
 *	  buffer manager initialization routines
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/buffer/buf_init.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "storage/buf_internals.h"
<<<<<<< HEAD

/* POLAR */
#include "miscadmin.h"
#include "storage/polar_bufmgr.h"
#include "storage/polar_copybuf.h"
#include "storage/polar_flushlist.h"
#include "storage/polar_pbp.h"
#include "storage/polar_shmem.h"
=======
#include "storage/bufmgr.h"
#include "storage/proc.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

BufferDescPadded *BufferDescriptors;
char	   *BufferBlocks;
ConditionVariableMinimallyPadded *BufferIOCVArray;
WritebackContext BackendWritebackContext;
CkptSortItem *CkptBufferIds;


/*
 * Data Structures:
 *		buffers live in a freelist and a lookup data structure.
 *
 *
 * Buffer Lookup:
 *		Two important notes.  First, the buffer has to be
 *		available for lookup BEFORE an IO begins.  Otherwise
 *		a second process trying to read the buffer will
 *		allocate its own copy and the buffer pool will
 *		become inconsistent.
 *
 * Buffer Replacement:
 *		see freelist.c.  A buffer cannot be replaced while in
 *		use either by data manager or during IO.
 *
 *
 * Synchronization/Locking:
 *
 * IO_IN_PROGRESS -- this is a flag in the buffer descriptor.
 *		It must be set when an IO is initiated and cleared at
 *		the end of the IO.  It is there to make sure that one
 *		process doesn't start to use a buffer while another is
 *		faulting it in.  see WaitIO and related routines.
 *
 * refcount --	Counts the number of processes holding pins on a buffer.
 *		A buffer is pinned during IO and immediately after a BufferAlloc().
 *		Pins must be released before end of transaction.  For efficiency the
 *		shared refcount isn't increased if an individual backend pins a buffer
 *		multiple times. Check the PrivateRefCount infrastructure in bufmgr.c.
 */


/*
 * Initialize shared buffer pool
 *
 * This is called once during shared-memory initialization (either in the
 * postmaster, or in a standalone backend).
 */
void
BufferManagerShmemInit(void)
{
	bool		foundBufs,
				foundDescs,
				foundIOCV,
				foundBufCkpt;

	/* POLAR */
	bool		found_buffer_pool_ctl;

	/* Align descriptors to a cacheline boundary. */
	polar_buffer_pool_ctl = (polar_buffer_pool_ctl_t *)
		ShmemInitStruct("POLAR Buffer Pool Ctl",
						sizeof(polar_buffer_pool_ctl_padded),
						&found_buffer_pool_ctl);
	/* POLAR end */

	/* Align descriptors to a cacheline boundary. */
	BufferDescriptors = (BufferDescPadded *)
		ShmemInitStruct("Buffer Descriptors",
						NBuffers * sizeof(BufferDescPadded),
						&foundDescs);

	/* Align buffer pool on IO page size boundary. */
	BufferBlocks = (char *)
<<<<<<< HEAD
		ShmemInitStruct("Buffer Blocks",
						polar_enable_buffer_alignment ?
							POLAR_BUFFER_EXTEND_SIZE(NBuffers * (Size) BLCKSZ) :
							NBuffers * (Size) BLCKSZ,
						&foundBufs);
	if (polar_enable_buffer_alignment)
		BufferBlocks = (char *) POLAR_BUFFER_ALIGN(BufferBlocks);
=======
		TYPEALIGN(PG_IO_ALIGN_SIZE,
				  ShmemInitStruct("Buffer Blocks",
								  NBuffers * (Size) BLCKSZ + PG_IO_ALIGN_SIZE,
								  &foundBufs));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* Align condition variables to cacheline boundary. */
	BufferIOCVArray = (ConditionVariableMinimallyPadded *)
		ShmemInitStruct("Buffer IO Condition Variables",
						NBuffers * sizeof(ConditionVariableMinimallyPadded),
						&foundIOCV);

	/*
	 * The array used to sort to-be-checkpointed buffer ids is located in
	 * shared memory, to avoid having to allocate significant amounts of
	 * memory at runtime. As that'd be in the middle of a checkpoint, or when
	 * the checkpointer is restarted, memory allocation failures would be
	 * painful.
	 */
	CkptBufferIds = (CkptSortItem *)
		ShmemInitStruct("Checkpoint BufferIds",
						NBuffers * sizeof(CkptSortItem), &foundBufCkpt);

<<<<<<< HEAD
	if (foundDescs || foundBufs || foundIOLocks || foundBufCkpt || found_buffer_pool_ctl)
	{
		/* should find all of these, or none of them */
		Assert(foundDescs && foundBufs && foundIOLocks && foundBufCkpt && found_buffer_pool_ctl);
=======
	if (foundDescs || foundBufs || foundIOCV || foundBufCkpt)
	{
		/* should find all of these, or none of them */
		Assert(foundDescs && foundBufs && foundIOCV && foundBufCkpt);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		/* note: this path is only taken in EXEC_BACKEND case */
	}
	else
	{
		polar_buffer_pool_is_inited = false;
		/*
	 	 * If normal mode nothing to do here, just waiting for pfs ready,
	 	 * we will call polar_try_reuse_buffer_pool to init buffer pool later,
	     * but for bootstrap or standalone mode, we need reset buffer pool anyway
	     */
		if (IsBootstrapProcessingMode() || !IsPostmasterEnvironment)
		{
<<<<<<< HEAD
			Assert(!polar_shmem_reused);
			polar_try_reuse_buffer_pool();
=======
			BufferDesc *buf = GetBufferDescriptor(i);

			ClearBufferTag(&buf->tag);

			pg_atomic_init_u32(&buf->state, 0);
			buf->wait_backend_pgprocno = INVALID_PROC_NUMBER;

			buf->buf_id = i;

			/*
			 * Initially link all the buffers together as unused. Subsequent
			 * management of this list is done by freelist.c.
			 */
			buf->freeNext = i + 1;

			LWLockInitialize(BufferDescriptorGetContentLock(buf),
							 LWTRANCHE_BUFFER_CONTENT);

			ConditionVariableInit(BufferDescriptorGetIOCV(buf));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}
	}

	/* Init other shared buffer-management stuff */
	StrategyInitialize(!foundDescs);

	/* POLAR: init flush list */
	polar_init_flush_list_ctl(!foundDescs);

	/* POLAR: init copy buffer */
	polar_init_copy_buffer_pool();

	/* Initialize per-backend file flush context */
	WritebackContextInit(&BackendWritebackContext,
						 &backend_flush_after);
}

/*
 * BufferManagerShmemSize
 *
 * compute the size of shared memory for the buffer pool including
 * data pages, buffer descriptors, hash tables, etc.
 */
Size
BufferManagerShmemSize(void)
{
	Size		size = 0;

<<<<<<< HEAD
	if (!polar_persisted_buffer_pool_enabled(NULL))
		size = add_size(size, polar_persisted_buffer_pool_size());
=======
	/* size of buffer descriptors */
	size = add_size(size, mul_size(NBuffers, sizeof(BufferDescPadded)));
	/* to allow aligning buffer descriptors */
	size = add_size(size, PG_CACHE_LINE_SIZE);

	/* size of data pages, plus alignment padding */
	size = add_size(size, PG_IO_ALIGN_SIZE);
	size = add_size(size, mul_size(NBuffers, BLCKSZ));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/* size of stuff controlled by freelist.c */
	size = add_size(size, StrategyShmemSize());

	/* size of I/O condition variables */
	size = add_size(size, mul_size(NBuffers,
								   sizeof(ConditionVariableMinimallyPadded)));
	/* to allow aligning the above */
	size = add_size(size, PG_CACHE_LINE_SIZE);

	/* size of checkpoint sort array in bufmgr.c */
	size = add_size(size, mul_size(NBuffers, sizeof(CkptSortItem)));

	/* POLAR: size of flush list */
	size = add_size(size, polar_flush_list_ctl_shmem_size());

	/* POLAR: size of copy buffer */
	size = add_size(size, polar_copy_buffer_shmem_size());

	return size;
}

Size
polar_persisted_buffer_pool_size(void)
{
	Size		size = 0;

	/* POLAR: used for separate buffer pool checking */
	size = add_size(size, sizeof(polar_buffer_pool_ctl_padded));
	/* to allow aligning buffer descriptors */
	size = add_size(size, PG_CACHE_LINE_SIZE);

	/* size of buffer descriptors */
	size = add_size(size, mul_size(NBuffers, sizeof(BufferDescPadded)));
	/* to allow aligning buffer descriptors */
	size = add_size(size, PG_CACHE_LINE_SIZE);

	/* POLAR: extra alignment padding for data I/O buffers */
	if (polar_enable_buffer_alignment)
		size = POLAR_BUFFER_EXTEND_SIZE(size);
	/* size of data pages */
	size = add_size(size, mul_size(NBuffers, BLCKSZ));

	return size;
}

void
polar_reset_buffer_pool(void)
{
	int			i = 0;

	/*
	 * Initialize all the buffer headers.
	 */
	for (i = 0; i < NBuffers; i++)
	{
		BufferDesc *buf = GetBufferDescriptor(i);

		CLEAR_BUFFERTAG(buf->tag);

		pg_atomic_init_u32(&buf->state, 0);
		buf->wait_backend_pid = 0;

		buf->buf_id = i;

		/*
		 * Initially link all the buffers together as unused. Subsequent
		 * management of this list is done by freelist.c.
		 */
		buf->freeNext = i + 1;

		LWLockInitialize(BufferDescriptorGetContentLock(buf),
						 LWTRANCHE_BUFFER_CONTENT);

		LWLockInitialize(BufferDescriptorGetIOLock(buf),
						 LWTRANCHE_BUFFER_IO_IN_PROGRESS);

		/* POLAR */
		buf->oldest_lsn = InvalidXLogRecPtr;
		buf->flush_next = POLAR_FLUSHNEXT_NOT_IN_LIST;
		buf->flush_prev = POLAR_FLUSHNEXT_NOT_IN_LIST;
		buf->copy_buffer = NULL;
		buf->recently_modified_count = 0;
		buf->polar_flags = 0;
		pg_atomic_init_u32(&buf->polar_redo_state, 0);
	}

	/* Correct last entry of linked list */
	GetBufferDescriptor(NBuffers - 1)->freeNext = FREENEXT_END_OF_LIST;
}
