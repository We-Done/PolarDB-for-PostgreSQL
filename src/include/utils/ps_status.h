/*-------------------------------------------------------------------------
 *
 * ps_status.h
 *
 * Declarations for backend/utils/misc/ps_status.c
 *
 * src/include/utils/ps_status.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef PS_STATUS_H
#define PS_STATUS_H

<<<<<<< HEAD
/* POLAR: define walsender/walreceiver streaming ps status buffer size */
#define MAX_REPLICATION_PS_BUFFER_SIZE (100)

extern bool update_process_title;
=======
/* disabled on Windows as the performance overhead can be significant */
#ifdef WIN32
#define DEFAULT_UPDATE_PROCESS_TITLE false
#else
#define DEFAULT_UPDATE_PROCESS_TITLE true
#endif

extern PGDLLIMPORT bool update_process_title;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

extern char **save_ps_display_args(int argc, char **argv);

extern void init_ps_display(const char *fixed_part);

<<<<<<< HEAD
extern void polar_ss_init_ps_display(const char *username, const char *dbname,
				const uint32 polar_startup_gucs_hash,
				const char *host_info, const char *initial_str);

extern void set_ps_display(const char *activity, bool force);
=======
extern void set_ps_display_suffix(const char *suffix);

extern void set_ps_display_remove_suffix(void);

extern void set_ps_display_with_len(const char *activity, size_t len);

/*
 * set_ps_display
 *		inlined to allow strlen to be evaluated during compilation when
 *		passing string constants.
 */
static inline void
set_ps_display(const char *activity)
{
	set_ps_display_with_len(activity, strlen(activity));
}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

extern const char *get_ps_display(int *displen);

#endif							/* PS_STATUS_H */
