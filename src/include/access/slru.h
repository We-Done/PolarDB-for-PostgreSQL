/*-------------------------------------------------------------------------
 *
 * slru.h
 *		Simple LRU buffering for transaction status logfiles
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/slru.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SLRU_H
#define SLRU_H

#include "access/xlogdefs.h"
#include "storage/lwlock.h"
<<<<<<< HEAD
#include "utils/hsearch.h"
#include "utils/polar_local_cache.h"
#include "utils/polar_successor_list.h"
=======
#include "storage/sync.h"

/*
 * To avoid overflowing internal arithmetic and the size_t data type, the
 * number of buffers must not exceed this number.
 */
#define SLRU_MAX_ALLOWED_BUFFERS ((1024 * 1024 * 1024) / BLCKSZ)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/*
 * Define SLRU segment size.  A page is the same BLCKSZ as is used everywhere
 * else in Postgres.  The segment size can be chosen somewhat arbitrarily;
 * we make it 32 pages by default, or 256Kb, i.e. 1M transactions for CLOG
 * or 64K transactions for SUBTRANS.
 *
 * Note: because TransactionIds are 32 bits and wrap around at 0xFFFFFFFF,
 * page numbering also wraps around at 0xFFFFFFFF/xxxx_XACTS_PER_PAGE (where
 * xxxx is CLOG or SUBTRANS, respectively), and segment numbering at
 * 0xFFFFFFFF/xxxx_XACTS_PER_PAGE/SLRU_PAGES_PER_SEGMENT.  We need
 * take no explicit notice of that fact in slru.c, except when comparing
 * segment and page numbers in SimpleLruTruncate (see PagePrecedes()).
 */
#define SLRU_PAGES_PER_SEGMENT	32

/*
 * Page status codes.  Note that these do not include the "dirty" bit.
 * page_dirty can be true only in the VALID or WRITE_IN_PROGRESS states;
 * in the latter case it implies that the page has been re-dirtied since
 * the write started.
 */
typedef enum
{
	SLRU_PAGE_EMPTY,			/* buffer is not in use */
	SLRU_PAGE_READ_IN_PROGRESS, /* page is being read in */
	SLRU_PAGE_VALID,			/* page is valid and not being written */
	SLRU_PAGE_WRITE_IN_PROGRESS,	/* page is being written out */
} SlruPageStatus;

/* POLAR */

/* POLAR: slru stat */
typedef struct polar_slru_stat 
{
	const char *name;					/* slru name */
	uint		n_slots; 				/* buffer slots number */
	uint		n_page_status_stat[4];			/* SLRU_PAGE_VALID status page number */
 	uint 		n_wait_reading_count;   /* waitor number for reading slots */
	uint        n_wait_writing_count;   /* waitor number for writing slots */ 
	uint64_t	n_victim_count;  		/* total victim slot count */
	uint64_t	n_victim_write_count;   /* total write victim slot count */
	uint64_t	n_slru_read_count; 		/* total SimpleLruReadPage calls */
	uint64_t	n_slru_read_only_count; /* total SimpleLruReadPage_ReadOnly but not SimpleLruReadPage calls */
	uint64_t	n_slru_read_upgrade_count; /* total SimpleLruReadPage_ReadOnly upgrade to SimpleLruReadPage calls */
	uint64_t	n_slru_write_count;		/* total SlruInternalWritePage calls */
	uint64_t	n_slru_zero_count;		/* total SimpleLruZeroPage calls */
	uint64_t	n_slru_flush_count;		/* total SimpleLruFlush calls */
	uint64_t	n_slru_truncate_count;  /* total SimpleLruTruncate calls */
	uint64_t	n_storage_read_count;   /* total slru slot read from storage counts, actually SlruPhysicalReadPage calls */
	uint64_t	n_storage_write_count;  /* total slru slot write to storage counts, actually SlruPhysicalWritePage calls */
} polar_slru_stat;

typedef struct polar_slru_hash_entry
{
	int pageno; 						/* hash key */
	int slotno;
} polar_slru_hash_entry;

#define POLAR_SLRU_STATS_NUM 32
extern const polar_slru_stat *polar_slru_stats[POLAR_SLRU_STATS_NUM];
extern int n_polar_slru_stats;

/* POLAR end */

/*
 * Shared-memory state
 *
 * ControlLock is used to protect access to the other fields, except
 * latest_page_number, which uses atomics; see comment in slru.c.
 */
typedef struct SlruSharedData
{
	/* Number of buffers managed by this SLRU structure */
	int			num_slots;

	/*
	 * Arrays holding info for each buffer slot.  Page number is undefined
	 * when status is EMPTY, as is page_lru_count.
	 */
	char	  **page_buffer;
	SlruPageStatus *page_status;
	bool	   *page_dirty;
	int64	   *page_number;
	int		   *page_lru_count;

	/* The buffer_locks protects the I/O on each buffer slots */
	LWLockPadded *buffer_locks;

	/* Locks to protect the in memory buffer slot access in SLRU bank. */
	LWLockPadded *bank_locks;

	/*----------
	 * A bank-wise LRU counter is maintained because we do a victim buffer
	 * search within a bank. Furthermore, manipulating an individual bank
	 * counter avoids frequent cache invalidation since we update it every time
	 * we access the page.
	 *
	 * We mark a page "most recently used" by setting
	 *		page_lru_count[slotno] = ++bank_cur_lru_count[bankno];
	 * The oldest page in the bank is therefore the one with the highest value
	 * of
	 * 		bank_cur_lru_count[bankno] - page_lru_count[slotno]
	 * The counts will eventually wrap around, but this calculation still
	 * works as long as no page's age exceeds INT_MAX counts.
	 *----------
	 */
	int		   *bank_cur_lru_count;

	/*
	 * Optional array of WAL flush LSNs associated with entries in the SLRU
	 * pages.  If not zero/NULL, we must flush WAL before writing pages (true
	 * for pg_xact, false for everything else).  group_lsn[] has
	 * lsn_groups_per_page entries per buffer slot, each containing the
	 * highest LSN known for a contiguous group of SLRU entries on that slot's
	 * page.
	 */
	XLogRecPtr *group_lsn;
	int			lsn_groups_per_page;

	/*
	 * latest_page_number is the page number of the current end of the log;
	 * this is not critical data, since we use it only to avoid swapping out
	 * the latest page.
	 */
	pg_atomic_uint64 latest_page_number;

<<<<<<< HEAD
	/* LWLocks */
	int			lwlock_tranche_id;
	char		lwlock_tranche_name[SLRU_MAX_NAME_LENGTH];
	LWLockPadded *buffer_locks;

	/* POLAR: the slru file put into shared storage */
	bool            polar_file_in_shared_storage;

    /* POLAR: we do not need to scan the whole hash table to find victim slot */
	int 		 victim_pivot;

	polar_slru_stat stat;
	
	/* POLAR: SLRU Hash Index */
	HTAB 		*polar_hash_index;

	/* POLAR: record free slot number to this list */
	polar_successor_list 		*polar_free_list;
	/* POLAR: Save data to local file as remote files's cache */
	polar_local_cache 			polar_cache;
	/* POLAR: set true when ro is doing online promote and allow this slru to write data to shared storage */
	bool 						polar_ro_promoting;
=======
	/* SLRU's index for statistics purposes (might not be unique) */
	int			slru_stats_idx;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
} SlruSharedData;

typedef SlruSharedData *SlruShared;

/*
 * SlruCtlData is an unshared structure that points to the active information
 * in shared memory.
 */
typedef struct SlruCtlData
{
	SlruShared	shared;

	/*
	 * Bitmask to determine bank number from page number.
	 */
	bits16		bank_mask;

	/*
	 * If true, use long segment file names.  Otherwise, use short file names.
	 *
	 * For details about the file name format, see SlruFileName().
	 */
	bool		long_segment_names;

	/*
	 * Which sync handler function to use when handing sync requests over to
	 * the checkpointer.  SYNC_HANDLER_NONE to disable fsync (eg pg_notify).
	 */
	SyncRequestHandler sync_handler;

	/*
	 * Decide whether a page is "older" for truncation and as a hint for
	 * evicting pages in LRU order.  Return true if every entry of the first
	 * argument is older than every entry of the second argument.  Note that
	 * !PagePrecedes(a,b) && !PagePrecedes(b,a) need not imply a==b; it also
	 * arises when some entries are older and some are not.  For SLRUs using
	 * SimpleLruTruncate(), this must use modular arithmetic.  (For others,
	 * the behavior of this callback has no functional implications.)  Use
	 * SlruPagePrecedesUnitTests() in SLRUs meeting its criteria.
	 */
	bool		(*PagePrecedes) (int64, int64);

	/*
	 * Dir is set during SimpleLruInit and does not change thereafter. Since
	 * it's always the same, it doesn't need to be in shared memory.
	 */
	char		Dir[64];

} SlruCtlData;

typedef SlruCtlData *SlruCtl;

<<<<<<< HEAD
=======
/*
 * Get the SLRU bank lock for given SlruCtl and the pageno.
 *
 * This lock needs to be acquired to access the slru buffer slots in the
 * respective bank.
 */
static inline LWLock *
SimpleLruGetBankLock(SlruCtl ctl, int64 pageno)
{
	int			bankno;

	bankno = pageno & ctl->bank_mask;
	return &(ctl->shared->bank_locks[bankno].lock);
}

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern Size SimpleLruShmemSize(int nslots, int nlsns);
extern int	SimpleLruAutotuneBuffers(int divisor, int max);
extern void SimpleLruInit(SlruCtl ctl, const char *name, int nslots, int nlsns,
<<<<<<< HEAD
			  LWLock *ctllock, const char *subdir, int tranche_id, bool polar_shared_file);
extern int	SimpleLruZeroPage(SlruCtl ctl, int pageno);
extern int SimpleLruReadPage(SlruCtl ctl, int pageno, bool write_ok,
				  TransactionId xid);
extern int SimpleLruReadPage_ReadOnly(SlruCtl ctl, int pageno,
						   TransactionId xid);
/* POLAR for csnlog */
extern int SimpleLruReadPage_ReadOnly_Locked(SlruCtl ctl, int pageno,
						   TransactionId xid);
/* POLAR end */
=======
						  const char *subdir, int buffer_tranche_id,
						  int bank_tranche_id, SyncRequestHandler sync_handler,
						  bool long_segment_names);
extern int	SimpleLruZeroPage(SlruCtl ctl, int64 pageno);
extern int	SimpleLruReadPage(SlruCtl ctl, int64 pageno, bool write_ok,
							  TransactionId xid);
extern int	SimpleLruReadPage_ReadOnly(SlruCtl ctl, int64 pageno,
									   TransactionId xid);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern void SimpleLruWritePage(SlruCtl ctl, int slotno);
extern void SimpleLruWriteAll(SlruCtl ctl, bool allow_redirtied);
#ifdef USE_ASSERT_CHECKING
extern void SlruPagePrecedesUnitTests(SlruCtl ctl, int per_page);
#else
#define SlruPagePrecedesUnitTests(ctl, per_page) do {} while (0)
#endif
extern void SimpleLruTruncate(SlruCtl ctl, int64 cutoffPage);
extern bool SimpleLruDoesPhysicalPageExist(SlruCtl ctl, int64 pageno);

typedef bool (*SlruScanCallback) (SlruCtl ctl, char *filename, int64 segpage,
								  void *data);
extern bool SlruScanDirectory(SlruCtl ctl, SlruScanCallback callback, void *data);
extern void SlruDeleteSegment(SlruCtl ctl, int64 segno);

extern int	SlruSyncFileTag(SlruCtl ctl, const FileTag *ftag, char *path);

/* SlruScanDirectory public callbacks */
extern bool SlruScanDirCbReportPresence(SlruCtl ctl, char *filename,
										int64 segpage, void *data);
extern bool SlruScanDirCbDeleteAll(SlruCtl ctl, char *filename, int64 segpage,
								   void *data);
extern bool check_slru_buffers(const char *name, int *newval);

/* POLAR */
bool polar_slru_file_in_shared_storage(bool in_shared_storage);
extern void polar_slru_invalid_page(SlruCtl ctl, int pageno);
extern void polar_slru_append_page(SlruCtl ctl, int slotno, bool update);
extern bool polar_slru_page_physical_exists(SlruCtl ctl, int pageno);
extern void polar_slru_init(void);
extern void polar_slru_reg_local_cache(SlruCtl ctl, polar_local_cache cache);
extern void polar_slru_promote(SlruCtl ctl);
extern void polar_slru_remove_local_cache_file(SlruCtl ctl);
extern bool polar_slru_find_min_seg(SlruCtl ctl, char *filename, int segpage, void *data);
extern void polar_physical_read_fra_slru(const char *slru_dir, int page_no, char *page);
#endif							/* SLRU_H */
