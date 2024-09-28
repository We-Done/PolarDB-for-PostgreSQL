/*-------------------------------------------------------------------------
 *
 * auth.h
 *	  Definitions for network authentication routines
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/libpq/auth.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef AUTH_H
#define AUTH_H

#include "libpq/libpq-be.h"

extern PGDLLIMPORT char *pg_krb_server_keyfile;
extern PGDLLIMPORT bool pg_krb_caseins_users;
extern PGDLLIMPORT bool pg_gss_accept_delegation;

extern void ClientAuthentication(Port *port);
<<<<<<< HEAD
extern void palor_session_client_authentication(Port* port);
=======
extern void sendAuthRequest(Port *port, AuthRequest areq, const char *extradata,
							int extralen);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/* Hook for plugins to get control in ClientAuthentication() */
typedef void (*ClientAuthentication_hook_type) (Port *, int);
extern PGDLLIMPORT ClientAuthentication_hook_type ClientAuthentication_hook;

<<<<<<< HEAD
/* POLAR px */
extern void FakeClientAuthentication(Port *port);
/* POLAR end */
=======
/* hook type for password manglers */
typedef char *(*auth_password_hook_typ) (char *input);

/* Default LDAP password mutator hook, can be overridden by a shared library */
extern PGDLLIMPORT auth_password_hook_typ ldap_password_hook;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

#endif							/* AUTH_H */
