/*
 *	common.h
 *		Common support routines for bin/scripts/
 *
 *	Copyright (c) 2003-2024, PostgreSQL Global Development Group
 *
 *	src/bin/scripts/common.h
 */
#ifndef COMMON_H
#define COMMON_H

#include "common/username.h"
#include "fe_utils/connect_utils.h"
#include "getopt_long.h"		/* pgrminclude ignore */
#include "libpq-fe.h"
#include "pqexpbuffer.h"		/* pgrminclude ignore */

extern void splitTableColumnsSpec(const char *spec, int encoding,
								  char **table, const char **columns);

<<<<<<< HEAD
extern bool CancelRequested;

/* Parameters needed by connectDatabase/connectMaintenanceDatabase */
typedef struct _connParams
{
	/* These fields record the actual command line parameters */
	const char *dbname;			/* this may be a connstring! */
	const char *pghost;
	const char *pgport;
	const char *pguser;
	enum trivalue prompt_password;
	/* If not NULL, this overrides the dbname obtained from command line */
	/* (but *only* the DB name, not anything else in the connstring) */
	const char *override_dbname;
} ConnParams;

typedef void (*help_handler) (const char *progname);

extern void handle_help_version_opts(int argc, char *argv[],
						 const char *fixed_progname,
						 help_handler hlp);

extern PGconn *connectDatabase(const ConnParams *cparams,
							   const char *progname,
							   bool echo, bool fail_ok,
							   bool allow_password_reuse);

extern PGconn *connectMaintenanceDatabase(ConnParams *cparams,
						   const char *progname, bool echo);

extern PGresult *executeQuery(PGconn *conn, const char *query,
			 const char *progname, bool echo);

extern void executeCommand(PGconn *conn, const char *query,
			   const char *progname, bool echo);

extern bool executeMaintenanceCommand(PGconn *conn, const char *query,
						  bool echo);

extern void appendQualifiedRelation(PQExpBuffer buf, const char *name,
						PGconn *conn, const char *progname, bool echo);
=======
extern void appendQualifiedRelation(PQExpBuffer buf, const char *spec,
									PGconn *conn, bool echo);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

extern bool yesno_prompt(const char *question);

#endif							/* COMMON_H */
