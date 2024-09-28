/*-------------------------------------------------------------------------
 *
 * postmaster.h
 *	  Exports from postmaster/postmaster.c.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/postmaster/postmaster.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef _POSTMASTER_H
#define _POSTMASTER_H

#include "miscadmin.h"

/* GUC options */
extern PGDLLIMPORT bool EnableSSL;
extern PGDLLIMPORT int SuperuserReservedConnections;
extern PGDLLIMPORT int ReservedConnections;
extern PGDLLIMPORT int PostPortNumber;
extern PGDLLIMPORT int Unix_socket_permissions;
extern PGDLLIMPORT char *Unix_socket_group;
extern PGDLLIMPORT char *Unix_socket_directories;
extern PGDLLIMPORT char *ListenAddresses;
extern PGDLLIMPORT bool ClientAuthInProgress;
extern PGDLLIMPORT int PreAuthDelay;
extern PGDLLIMPORT int AuthenticationTimeout;
extern PGDLLIMPORT bool Log_connections;
extern PGDLLIMPORT bool log_hostname;
extern PGDLLIMPORT bool enable_bonjour;
extern PGDLLIMPORT char *bonjour_name;
extern PGDLLIMPORT bool restart_after_crash;
extern PGDLLIMPORT bool remove_temp_files_after_crash;
extern PGDLLIMPORT bool send_abort_for_crash;
extern PGDLLIMPORT bool send_abort_for_kill;

#ifdef WIN32
extern PGDLLIMPORT HANDLE PostmasterHandle;
#else
extern PGDLLIMPORT int postmaster_alive_fds[2];

/* POLAR */
#include <signal.h>

/*
 * Constants that represent which of postmaster_alive_fds is held by
 * postmaster, and which is used in children to check for postmaster death.
 */
#define POSTMASTER_FD_WATCH		0	/* used in children to check for
									 * postmaster death */
#define POSTMASTER_FD_OWN		1	/* kept open by postmaster only */
#endif

/* POLAR px */
#define POSTMASTER_IN_STARTUP_MSG "the database system is starting up"
#define POSTMASTER_IN_RECOVERY_MSG "the database system is in recovery mode"
#define POSTMASTER_IN_RECOVERY_DETAIL_MSG "last replayed record at"
/* gpstate must be updated if this message changes */
#define POSTMASTER_MIRROR_VERSION_DETAIL_MSG "- VERSION:"
/* POLAR end */

extern PGDLLIMPORT const char *progname;

extern PGDLLIMPORT bool redirection_done;
extern PGDLLIMPORT bool LoadedSSL;

extern void PostmasterMain(int argc, char *argv[]) pg_attribute_noreturn();
extern void ClosePostmasterPorts(bool am_syslogger);
extern void InitProcessGlobals(void);

extern int	MaxLivePostmasterChildren(void);

extern bool PostmasterMarkPIDForWorkerNotify(int);

#ifdef WIN32
extern void pgwin32_register_deadchild_callback(HANDLE procHandle, DWORD procId);
#endif

/* defined in globals.c */
extern PGDLLIMPORT struct ClientSocket *MyClientSocket;

/* prototypes for functions in launch_backend.c */
extern pid_t postmaster_child_launch(BackendType child_type,
									 char *startup_data,
									 size_t startup_data_len,
									 struct ClientSocket *client_sock);
const char *PostmasterChildName(BackendType child_type);
#ifdef EXEC_BACKEND
extern void SubPostmasterMain(int argc, char *argv[]) pg_attribute_noreturn();
#endif


/* POLAR: Shared Server */
extern bool RandomCancelKey(int32 *cancel_key);

typedef struct Port Port;
extern void processCancelRequest(Port *port, void *pkt);
extern int polar_parse_startup_packet(Port* port, MemoryContext memctx, void* pkg_body, int pkg_size, bool SSLdone);
/* POLAR end */

/*
 * Note: MAX_BACKENDS is limited to 2^18-1 because that's the width reserved
 * for buffer references in buf_internals.h.  This limitation could be lifted
 * by using a 64bit state; but it's unlikely to be worthwhile as 2^18-1
 * backends exceed currently realistic configurations. Even if that limitation
 * were removed, we still could not a) exceed 2^23-1 because inval.c stores
 * the ProcNumber as a 3-byte signed integer, b) INT_MAX/4 because some places
 * compute 4*MaxBackends without any overflow check.  This is rechecked in the
 * relevant GUC check hooks and in RegisterBackgroundWorker().
 */
#define MAX_BACKENDS	0x3FFFF

/* POLAR */
extern volatile sig_atomic_t polar_in_error_handling_process;
extern bool polar_have_crashed_worker(void);
extern void polar_assign_enable_send_stop(bool newval, void *extra);

#endif							/* _POSTMASTER_H */
