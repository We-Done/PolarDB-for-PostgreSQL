/*-------------------------------------------------------------------------
 *
 * misc.c
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/misc.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/file.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>

#include "access/sysattr.h"
#include "access/table.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_type.h"
#include "catalog/system_fk_info.h"
#include "commands/dbcommands.h"
#include "commands/tablespace.h"
#include "common/keywords.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "nodes/miscnodes.h"
#include "parser/parse_type.h"
#include "parser/scansup.h"
#include "pgstat.h"
#include "postmaster/syslogger.h"
#include "rewrite/rewriteHandler.h"
#include "storage/fd.h"
#include "storage/latch.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/ruleutils.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"


/*
 * structure to cache metadata needed in pg_input_is_valid_common
 */
typedef struct ValidIOData
{
	Oid			typoid;
	int32		typmod;
	bool		typname_constant;
	Oid			typiofunc;
	Oid			typioparam;
	FmgrInfo	inputproc;
} ValidIOData;

static bool pg_input_is_valid_common(FunctionCallInfo fcinfo,
									 text *txt, text *typname,
									 ErrorSaveContext *escontext);


/*
 * Common subroutine for num_nulls() and num_nonnulls().
 * Returns true if successful, false if function should return NULL.
 * If successful, total argument count and number of nulls are
 * returned into *nargs and *nulls.
 */
static bool
count_nulls(FunctionCallInfo fcinfo,
			int32 *nargs, int32 *nulls)
{
	int32		count = 0;
	int			i;

	/* Did we get a VARIADIC array argument, or separate arguments? */
	if (get_fn_expr_variadic(fcinfo->flinfo))
	{
		ArrayType  *arr;
		int			ndims,
					nitems,
				   *dims;
		bits8	   *bitmap;

		Assert(PG_NARGS() == 1);

		/*
		 * If we get a null as VARIADIC array argument, we can't say anything
		 * useful about the number of elements, so return NULL.  This behavior
		 * is consistent with other variadic functions - see concat_internal.
		 */
		if (PG_ARGISNULL(0))
			return false;

		/*
		 * Non-null argument had better be an array.  We assume that any call
		 * context that could let get_fn_expr_variadic return true will have
		 * checked that a VARIADIC-labeled parameter actually is an array.  So
		 * it should be okay to just Assert that it's an array rather than
		 * doing a full-fledged error check.
		 */
		Assert(OidIsValid(get_base_element_type(get_fn_expr_argtype(fcinfo->flinfo, 0))));

		/* OK, safe to fetch the array value */
		arr = PG_GETARG_ARRAYTYPE_P(0);

		/* Count the array elements */
		ndims = ARR_NDIM(arr);
		dims = ARR_DIMS(arr);
		nitems = ArrayGetNItems(ndims, dims);

		/* Count those that are NULL */
		bitmap = ARR_NULLBITMAP(arr);
		if (bitmap)
		{
			int			bitmask = 1;

			for (i = 0; i < nitems; i++)
			{
				if ((*bitmap & bitmask) == 0)
					count++;

				bitmask <<= 1;
				if (bitmask == 0x100)
				{
					bitmap++;
					bitmask = 1;
				}
			}
		}

		*nargs = nitems;
		*nulls = count;
	}
	else
	{
		/* Separate arguments, so just count 'em */
		for (i = 0; i < PG_NARGS(); i++)
		{
			if (PG_ARGISNULL(i))
				count++;
		}

		*nargs = PG_NARGS();
		*nulls = count;
	}

	return true;
}

/*
 * num_nulls()
 *	Count the number of NULL arguments
 */
Datum
pg_num_nulls(PG_FUNCTION_ARGS)
{
	int32		nargs,
				nulls;

	if (!count_nulls(fcinfo, &nargs, &nulls))
		PG_RETURN_NULL();

	PG_RETURN_INT32(nulls);
}

/*
 * num_nonnulls()
 *	Count the number of non-NULL arguments
 */
Datum
pg_num_nonnulls(PG_FUNCTION_ARGS)
{
	int32		nargs,
				nulls;

	if (!count_nulls(fcinfo, &nargs, &nulls))
		PG_RETURN_NULL();

	PG_RETURN_INT32(nargs - nulls);
}


/*
 * current_database()
 *	Expose the current database to the user
 */
Datum
current_database(PG_FUNCTION_ARGS)
{
	Name		db;

	db = (Name) palloc(NAMEDATALEN);

	namestrcpy(db, get_database_name(MyDatabaseId));
	PG_RETURN_NAME(db);
}


/*
 * current_query()
 *	Expose the current query to the user (useful in stored procedures)
 *	We might want to use ActivePortal->sourceText someday.
 */
Datum
current_query(PG_FUNCTION_ARGS)
{
	/* there is no easy way to access the more concise 'query_string' */
	if (debug_query_string)
		PG_RETURN_TEXT_P(cstring_to_text(debug_query_string));
	else
		PG_RETURN_NULL();
}

<<<<<<< HEAD
/*
 * Send a signal to another backend.
 *
 * The signal is delivered if the user is either a superuser or the same
 * role as the backend being signaled. For "dangerous" signals, an explicit
 * check for superuser needs to be done prior to calling this function.
 *
 * Returns 0 on success, 1 on general failure, 2 on normal permission error
 * and 3 if the caller needs to be a superuser.
 *
 * In the event of a general failure (return code 1), a warning message will
 * be emitted. For permission errors, doing that is the responsibility of
 * the caller.
 */
#define SIGNAL_BACKEND_SUCCESS 0
#define SIGNAL_BACKEND_ERROR 1
#define SIGNAL_BACKEND_NOPERMISSION 2
#define SIGNAL_BACKEND_NOSUPERUSER 3
/* POLAR */
#define SIGNAL_BACKEND_NOPOLARSUPERUSER 4
static int
pg_signal_backend(int pid, int sig)
{
	int			real_pid;
	PGPROC	   *proc;
	ELOG_PSS(DEBUG1, "pg_signal_backend %d, %d", pid, sig);

	if (IS_POLAR_SESSION_SHARED() && POLAR_IS_SESSION_ID(pid))
	{
		Oid roleId;
		if (!polar_get_session_roleid(pid, &roleId))
		{
			ereport(WARNING,
				(errmsg("PID %d is not a PostgreSQL server process", pid)));
			return SIGNAL_BACKEND_ERROR;
		}

		/* Only allow superusers to signal superuser-owned backends. */
		if (superuser_arg(roleId) && !superuser())
			return SIGNAL_BACKEND_NOSUPERUSER;
			
		/* POLAR: only allow superusers and polar_superuser to signal polar_superuser-owned backends. */
		if (polar_superuser_arg(roleId) && 
			!(superuser() || polar_superuser()))
			return SIGNAL_BACKEND_NOPOLARSUPERUSER;

		/* Users can signal backends they have role membership in. */
		if (!has_privs_of_role(GetUserId(), roleId) &&
			!has_privs_of_role(GetUserId(), DEFAULT_ROLE_SIGNAL_BACKENDID))
			return SIGNAL_BACKEND_NOPERMISSION;

		if (!polar_send_signal(pid, NULL, sig))
		{
			/* Again, just a warning to allow loops */
			ereport(WARNING,
					(errmsg("could not send signal to process %d: %m", pid)));
			return SIGNAL_BACKEND_ERROR;
		}

		return SIGNAL_BACKEND_SUCCESS;
	}

	/* POLAR: try to get real pid, the pid here might be a virtual pid */
	real_pid = polar_pgstat_get_real_pid(pid, 0, false, false);
	proc = BackendPidGetProc(real_pid);

	/*
	 * BackendPidGetProc returns NULL if the pid isn't valid; but by the time
	 * we reach kill(), a process for which we get a valid proc here might
	 * have terminated on its own.  There's no way to acquire a lock on an
	 * arbitrary process to prevent that. But since so far all the callers of
	 * this mechanism involve some request for ending the process anyway, that
	 * it might end on its own first is not a problem.
	 */
	if (proc == NULL)
	{
		/*
		 * This is just a warning so a loop-through-resultset will not abort
		 * if one backend terminated on its own during the run.
		 */
		ereport(WARNING,
				(errmsg("PID %d is not a PostgreSQL server process", pid)));
		return SIGNAL_BACKEND_ERROR;
	}

	/* Only allow superusers to signal superuser-owned backends. */
	if (superuser_arg(proc->roleId) && !superuser())
		return SIGNAL_BACKEND_NOSUPERUSER;
		
	/* POLAR: only allow superusers and polar_superuser to signal polar_superuser-owned backends. */
	if (polar_superuser_arg(proc->roleId) && 
		!(superuser() || polar_superuser()))
		return SIGNAL_BACKEND_NOPOLARSUPERUSER;

	/* Users can signal backends they have role membership in. */
	if (!has_privs_of_role(GetUserId(), proc->roleId) &&
		!has_privs_of_role(GetUserId(), DEFAULT_ROLE_SIGNAL_BACKENDID))
		return SIGNAL_BACKEND_NOPERMISSION;

	/*
	 * Can the process we just validated above end, followed by the pid being
	 * recycled for a new process, before reaching here?  Then we'd be trying
	 * to kill the wrong thing.  Seems near impossible when sequential pid
	 * assignment and wraparound is used.  Perhaps it could happen on a system
	 * where pid re-use is randomized.  That race condition possibility seems
	 * too unlikely to worry about.
	 */

	/* If we have setsid(), signal the backend's whole process group */
#ifdef HAVE_SETSID
	if (kill(-real_pid, sig))
#else
	if (kill(real_pid, sig))
#endif
	{
		/* Again, just a warning to allow loops */
		ereport(WARNING,
				(errmsg("could not send signal to process %d: %m", pid)));
		return SIGNAL_BACKEND_ERROR;
	}
	return SIGNAL_BACKEND_SUCCESS;
}

/*
 * Signal to cancel a backend process.  This is allowed if you are a member of
 * the role whose process is being canceled.
 *
 * Note that only superusers can signal superuser-owned processes.
 */
Datum
pg_cancel_backend(PG_FUNCTION_ARGS)
{
	int			r = pg_signal_backend(PG_GETARG_INT32(0), SIGINT);

	if (r == SIGNAL_BACKEND_NOSUPERUSER)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be a superuser to cancel superuser query"))));

	if (r == SIGNAL_BACKEND_NOPERMISSION)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be a member of the role whose query is being canceled or member of pg_signal_backend"))));

	PG_RETURN_BOOL(r == SIGNAL_BACKEND_SUCCESS);
}

/*
 * Signal to terminate a backend process.  This is allowed if you are a member
 * of the role whose process is being terminated.
 *
 * Note that only superusers can signal superuser-owned processes.
 */
Datum
pg_terminate_backend(PG_FUNCTION_ARGS)
{
	int			r = pg_signal_backend(PG_GETARG_INT32(0), SIGTERM);

	if (r == SIGNAL_BACKEND_NOSUPERUSER)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be a superuser to terminate superuser process"))));

	if (r == SIGNAL_BACKEND_NOPERMISSION)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be a member of the role whose process is being terminated or member of pg_signal_backend"))));

	PG_RETURN_BOOL(r == SIGNAL_BACKEND_SUCCESS);
}

/*
 * Signal to reload the database configuration
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_reload_conf(PG_FUNCTION_ARGS)
{
	if (kill(PostmasterPid, SIGHUP))
	{
		ereport(WARNING,
				(errmsg("failed to send signal to postmaster: %m")));
		PG_RETURN_BOOL(false);
	}

	PG_RETURN_BOOL(true);
}


/*
 * Rotate log file
 *
 * This function is kept to support adminpack 1.0.
 */
Datum
pg_rotate_logfile(PG_FUNCTION_ARGS)
{
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser to rotate log files with adminpack 1.0"),
				  errhint("Consider using pg_logfile_rotate(), which is part of core, instead."))));

	if (!Logging_collector)
	{
		ereport(WARNING,
				(errmsg("rotation not possible because log collection not active")));
		PG_RETURN_BOOL(false);
	}

	SendPostmasterSignal(PMSIGNAL_ROTATE_LOGFILE);
	PG_RETURN_BOOL(true);
}

/*
 * Rotate log file
 *
 * Permission checking for this function is managed through the normal
 * GRANT system.
 */
Datum
pg_rotate_logfile_v2(PG_FUNCTION_ARGS)
{
	if (!Logging_collector)
	{
		ereport(WARNING,
				(errmsg("rotation not possible because log collection not active")));
		PG_RETURN_BOOL(false);
	}

	SendPostmasterSignal(PMSIGNAL_ROTATE_LOGFILE);
	PG_RETURN_BOOL(true);
}

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
/* Function to find out which databases make use of a tablespace */

Datum
pg_tablespace_databases(PG_FUNCTION_ARGS)
{
	Oid			tablespaceOid = PG_GETARG_OID(0);
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
<<<<<<< HEAD
	bool		randomAccess;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	char	   *location;
	DIR		   *dirdesc;
	struct dirent *de;
	MemoryContext oldcontext;

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("materialize mode required, but it is not allowed in this context")));

	/* The tupdesc and tuplestore must be created in ecxt_per_query_memory */
	oldcontext = MemoryContextSwitchTo(rsinfo->econtext->ecxt_per_query_memory);

	tupdesc = CreateTemplateTupleDesc(1, false);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "pg_tablespace_databases",
					   OIDOID, -1, 0);

	randomAccess = (rsinfo->allowedModes & SFRM_Materialize_Random) != 0;
	tupstore = tuplestore_begin_heap(randomAccess, false, work_mem);

	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);
=======
	char	   *location;
	DIR		   *dirdesc;
	struct dirent *de;

	InitMaterializedSRF(fcinfo, MAT_SRF_USE_EXPECTED_DESC);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	if (tablespaceOid == GLOBALTABLESPACE_OID)
	{
		ereport(WARNING,
				(errmsg("global tablespace never has databases")));
		/* return empty tuplestore */
		return (Datum) 0;
	}

	if (tablespaceOid == DEFAULTTABLESPACE_OID)
<<<<<<< HEAD
		location = psprintf("base");
	else
		location = psprintf("pg_tblspc/%u/%s", tablespaceOid,
							TABLESPACE_VERSION_DIRECTORY);

	dirdesc = polar_allocate_dir(location);
=======
		location = "base";
	else
		location = psprintf("%s/%u/%s", PG_TBLSPC_DIR, tablespaceOid,
							TABLESPACE_VERSION_DIRECTORY);

	dirdesc = AllocateDir(location);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	if (!dirdesc)
	{
		/* the only expected error is ENOENT */
		if (errno != ENOENT)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not open directory \"%s\": %m",
							location)));
		ereport(WARNING,
				(errmsg("%u is not a tablespace OID", tablespaceOid)));
		/* return empty tuplestore */
		return (Datum) 0;
	}

	while ((de = ReadDir(dirdesc, location)) != NULL)
	{
		Oid			datOid = atooid(de->d_name);
		char	   *subdir;
		bool		isempty;
		Datum		values[1];
		bool		nulls[1];

		/* this test skips . and .., but is awfully weak */
		if (!datOid)
			continue;

		/* if database subdir is empty, don't report tablespace as used */

		subdir = psprintf("%s/%s", location, de->d_name);
		isempty = directory_is_empty(subdir);
		pfree(subdir);

		if (isempty)
			continue;			/* indeed, nothing in it */

		values[0] = ObjectIdGetDatum(datOid);
		nulls[0] = false;

<<<<<<< HEAD
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
=======
		tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc,
							 values, nulls);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}

	FreeDir(dirdesc);
	return (Datum) 0;
}


/*
 * pg_tablespace_location - get location for a tablespace
 */
Datum
pg_tablespace_location(PG_FUNCTION_ARGS)
{
	Oid			tablespaceOid = PG_GETARG_OID(0);
	char		sourcepath[MAXPGPATH];
	char		targetpath[MAXPGPATH];
	int			rllen;
	struct stat st;

	/*
	 * POLAR: The user defined tablespace is not really created,
	 * so it directly returns null
	 */
	if (polar_syntactically_compatible_tablespace_mode())
		PG_RETURN_TEXT_P(cstring_to_text(""));

	/*
	 * It's useful to apply this function to pg_class.reltablespace, wherein
	 * zero means "the database's default tablespace".  So, rather than
	 * throwing an error for zero, we choose to assume that's what is meant.
	 */
	if (tablespaceOid == InvalidOid)
		tablespaceOid = MyDatabaseTableSpace;

	/*
	 * Return empty string for the cluster's default tablespaces
	 */
	if (tablespaceOid == DEFAULTTABLESPACE_OID ||
		tablespaceOid == GLOBALTABLESPACE_OID)
		PG_RETURN_TEXT_P(cstring_to_text(""));

	/*
	 * Find the location of the tablespace by reading the symbolic link that
	 * is in pg_tblspc/<oid>.
	 */
	snprintf(sourcepath, sizeof(sourcepath), "%s/%u", PG_TBLSPC_DIR, tablespaceOid);

	/*
	 * Before reading the link, check if the source path is a link or a
	 * junction point.  Note that a directory is possible for a tablespace
	 * created with allow_in_place_tablespaces enabled.  If a directory is
	 * found, a relative path to the data directory is returned.
	 */
	if (lstat(sourcepath, &st) < 0)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not stat file \"%s\": %m",
						sourcepath)));
	}

	if (!S_ISLNK(st.st_mode))
		PG_RETURN_TEXT_P(cstring_to_text(sourcepath));

	/*
	 * In presence of a link or a junction point, return the path pointing to.
	 */
	rllen = readlink(sourcepath, targetpath, sizeof(targetpath));
	if (rllen < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read symbolic link \"%s\": %m",
						sourcepath)));
	if (rllen >= sizeof(targetpath))
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("symbolic link \"%s\" target is too long",
						sourcepath)));
	targetpath[rllen] = '\0';

	PG_RETURN_TEXT_P(cstring_to_text(targetpath));
}

/*
 * pg_sleep - delay for N seconds
 */
Datum
pg_sleep(PG_FUNCTION_ARGS)
{
	float8		secs = PG_GETARG_FLOAT8(0);
	float8		endtime;

	/*
	 * We sleep using WaitLatch, to ensure that we'll wake up promptly if an
	 * important signal (such as SIGALRM or SIGINT) arrives.  Because
	 * WaitLatch's upper limit of delay is INT_MAX milliseconds, and the user
	 * might ask for more than that, we sleep for at most 10 minutes and then
	 * loop.
	 *
	 * By computing the intended stop time initially, we avoid accumulation of
	 * extra delay across multiple sleeps.  This also ensures we won't delay
	 * less than the specified time when WaitLatch is terminated early by a
	 * non-query-canceling signal such as SIGHUP.
	 */
#define GetNowFloat()	((float8) GetCurrentTimestamp() / 1000000.0)

	endtime = GetNowFloat() + secs;

	for (;;)
	{
		float8		delay;
		long		delay_ms;

		CHECK_FOR_INTERRUPTS();

		delay = endtime - GetNowFloat();
		if (delay >= 600.0)
			delay_ms = 600000;
		else if (delay > 0.0)
			delay_ms = (long) ceil(delay * 1000.0);
		else
			break;

		(void) WaitLatch(MyLatch,
						 WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
						 delay_ms,
						 WAIT_EVENT_PG_SLEEP);
		ResetLatch(MyLatch);
	}

	PG_RETURN_VOID();
}

/* Function to return the list of grammar keywords */
Datum
pg_get_keywords(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		TupleDesc	tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			elog(ERROR, "return type must be a row type");
		funcctx->tuple_desc = tupdesc;
		funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();

	if (funcctx->call_cntr < ScanKeywords.num_keywords)
	{
		char	   *values[5];
		HeapTuple	tuple;

		/* cast-away-const is ugly but alternatives aren't much better */
		values[0] = unconstify(char *,
							   GetScanKeyword(funcctx->call_cntr,
											  &ScanKeywords));

		switch (ScanKeywordCategories[funcctx->call_cntr])
		{
			case UNRESERVED_KEYWORD:
				values[1] = "U";
				values[3] = _("unreserved");
				break;
			case COL_NAME_KEYWORD:
				values[1] = "C";
				values[3] = _("unreserved (cannot be function or type name)");
				break;
			case TYPE_FUNC_NAME_KEYWORD:
				values[1] = "T";
				values[3] = _("reserved (can be function or type name)");
				break;
			case RESERVED_KEYWORD:
				values[1] = "R";
				values[3] = _("reserved");
				break;
			default:			/* shouldn't be possible */
				values[1] = NULL;
				values[3] = NULL;
				break;
		}

		if (ScanKeywordBareLabel[funcctx->call_cntr])
		{
			values[2] = "true";
			values[4] = _("can be bare label");
		}
		else
		{
			values[2] = "false";
			values[4] = _("requires AS");
		}

		tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);

		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}

	SRF_RETURN_DONE(funcctx);
}


/* Function to return the list of catalog foreign key relationships */
Datum
pg_get_catalog_foreign_keys(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	FmgrInfo   *arrayinp;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		TupleDesc	tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			elog(ERROR, "return type must be a row type");
		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		/*
		 * We use array_in to convert the C strings in sys_fk_relationships[]
		 * to text arrays.  But we cannot use DirectFunctionCallN to call
		 * array_in, and it wouldn't be very efficient if we could.  Fill an
		 * FmgrInfo to use for the call.
		 */
		arrayinp = (FmgrInfo *) palloc(sizeof(FmgrInfo));
		fmgr_info(F_ARRAY_IN, arrayinp);
		funcctx->user_fctx = arrayinp;

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	arrayinp = (FmgrInfo *) funcctx->user_fctx;

	if (funcctx->call_cntr < lengthof(sys_fk_relationships))
	{
		const SysFKRelationship *fkrel = &sys_fk_relationships[funcctx->call_cntr];
		Datum		values[6];
		bool		nulls[6];
		HeapTuple	tuple;

		memset(nulls, false, sizeof(nulls));

		values[0] = ObjectIdGetDatum(fkrel->fk_table);
		values[1] = FunctionCall3(arrayinp,
								  CStringGetDatum(fkrel->fk_columns),
								  ObjectIdGetDatum(TEXTOID),
								  Int32GetDatum(-1));
		values[2] = ObjectIdGetDatum(fkrel->pk_table);
		values[3] = FunctionCall3(arrayinp,
								  CStringGetDatum(fkrel->pk_columns),
								  ObjectIdGetDatum(TEXTOID),
								  Int32GetDatum(-1));
		values[4] = BoolGetDatum(fkrel->is_array);
		values[5] = BoolGetDatum(fkrel->is_opt);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);

		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}

	SRF_RETURN_DONE(funcctx);
}


/*
 * Return the type of the argument.
 */
Datum
pg_typeof(PG_FUNCTION_ARGS)
{
	PG_RETURN_OID(get_fn_expr_argtype(fcinfo->flinfo, 0));
}


/*
 * Return the base type of the argument.
 *		If the given type is a domain, return its base type;
 *		otherwise return the type's own OID.
 *		Return NULL if the type OID doesn't exist or points to a
 *		non-existent base type.
 *
 * This is a SQL-callable version of getBaseType().  Unlike that function,
 * we don't want to fail for a bogus type OID; this is helpful to keep race
 * conditions from turning into query failures when scanning the catalogs.
 * Hence we need our own implementation.
 */
Datum
pg_basetype(PG_FUNCTION_ARGS)
{
	Oid			typid = PG_GETARG_OID(0);

	/*
	 * We loop to find the bottom base type in a stack of domains.
	 */
	for (;;)
	{
		HeapTuple	tup;
		Form_pg_type typTup;

		tup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
		if (!HeapTupleIsValid(tup))
			PG_RETURN_NULL();	/* return NULL for bogus OID */
		typTup = (Form_pg_type) GETSTRUCT(tup);
		if (typTup->typtype != TYPTYPE_DOMAIN)
		{
			/* Not a domain, so done */
			ReleaseSysCache(tup);
			break;
		}

		typid = typTup->typbasetype;
		ReleaseSysCache(tup);
	}

	PG_RETURN_OID(typid);
}


/*
 * Implementation of the COLLATE FOR expression; returns the collation
 * of the argument.
 */
Datum
pg_collation_for(PG_FUNCTION_ARGS)
{
	Oid			typeid;
	Oid			collid;

	typeid = get_fn_expr_argtype(fcinfo->flinfo, 0);
	if (!typeid)
		PG_RETURN_NULL();
	if (!type_is_collatable(typeid) && typeid != UNKNOWNOID)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("collations are not supported by type %s",
						format_type_be(typeid))));

	collid = PG_GET_COLLATION();
	if (!collid)
		PG_RETURN_NULL();
	PG_RETURN_TEXT_P(cstring_to_text(generate_collation_name(collid)));
}


/*
 * pg_relation_is_updatable - determine which update events the specified
 * relation supports.
 *
 * This relies on relation_is_updatable() in rewriteHandler.c, which see
 * for additional information.
 */
Datum
pg_relation_is_updatable(PG_FUNCTION_ARGS)
{
	Oid			reloid = PG_GETARG_OID(0);
	bool		include_triggers = PG_GETARG_BOOL(1);

	PG_RETURN_INT32(relation_is_updatable(reloid, NIL, include_triggers, NULL));
}

/*
 * pg_column_is_updatable - determine whether a column is updatable
 *
 * This function encapsulates the decision about just what
 * information_schema.columns.is_updatable actually means.  It's not clear
 * whether deletability of the column's relation should be required, so
 * we want that decision in C code where we could change it without initdb.
 */
Datum
pg_column_is_updatable(PG_FUNCTION_ARGS)
{
	Oid			reloid = PG_GETARG_OID(0);
	AttrNumber	attnum = PG_GETARG_INT16(1);
	AttrNumber	col = attnum - FirstLowInvalidHeapAttributeNumber;
	bool		include_triggers = PG_GETARG_BOOL(2);
	int			events;

	/* System columns are never updatable */
	if (attnum <= 0)
		PG_RETURN_BOOL(false);

	events = relation_is_updatable(reloid, NIL, include_triggers,
								   bms_make_singleton(col));

	/* We require both updatability and deletability of the relation */
#define REQ_EVENTS ((1 << CMD_UPDATE) | (1 << CMD_DELETE))

	PG_RETURN_BOOL((events & REQ_EVENTS) == REQ_EVENTS);
}


/*
 * pg_input_is_valid - test whether string is valid input for datatype.
 *
 * Returns true if OK, false if not.
 *
 * This will only work usefully if the datatype's input function has been
 * updated to return "soft" errors via errsave/ereturn.
 */
Datum
pg_input_is_valid(PG_FUNCTION_ARGS)
{
	text	   *txt = PG_GETARG_TEXT_PP(0);
	text	   *typname = PG_GETARG_TEXT_PP(1);
	ErrorSaveContext escontext = {T_ErrorSaveContext};

	PG_RETURN_BOOL(pg_input_is_valid_common(fcinfo, txt, typname,
											&escontext));
}

/*
 * pg_input_error_info - test whether string is valid input for datatype.
 *
 * Returns NULL if OK, else the primary message, detail message, hint message
 * and sql error code from the error.
 *
 * This will only work usefully if the datatype's input function has been
 * updated to return "soft" errors via errsave/ereturn.
 */
Datum
pg_input_error_info(PG_FUNCTION_ARGS)
{
	text	   *txt = PG_GETARG_TEXT_PP(0);
	text	   *typname = PG_GETARG_TEXT_PP(1);
	ErrorSaveContext escontext = {T_ErrorSaveContext};
	TupleDesc	tupdesc;
	Datum		values[4];
	bool		isnull[4];

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	/* Enable details_wanted */
	escontext.details_wanted = true;

	if (pg_input_is_valid_common(fcinfo, txt, typname,
								 &escontext))
		memset(isnull, true, sizeof(isnull));
	else
	{
		char	   *sqlstate;

		Assert(escontext.error_occurred);
		Assert(escontext.error_data != NULL);
		Assert(escontext.error_data->message != NULL);

		memset(isnull, false, sizeof(isnull));

		values[0] = CStringGetTextDatum(escontext.error_data->message);

		if (escontext.error_data->detail != NULL)
			values[1] = CStringGetTextDatum(escontext.error_data->detail);
		else
			isnull[1] = true;

		if (escontext.error_data->hint != NULL)
			values[2] = CStringGetTextDatum(escontext.error_data->hint);
		else
			isnull[2] = true;

		sqlstate = unpack_sql_state(escontext.error_data->sqlerrcode);
		values[3] = CStringGetTextDatum(sqlstate);
	}

	return HeapTupleGetDatum(heap_form_tuple(tupdesc, values, isnull));
}

/* Common subroutine for the above */
static bool
pg_input_is_valid_common(FunctionCallInfo fcinfo,
						 text *txt, text *typname,
						 ErrorSaveContext *escontext)
{
	char	   *str = text_to_cstring(txt);
	ValidIOData *my_extra;
	Datum		converted;

	/*
	 * We arrange to look up the needed I/O info just once per series of
	 * calls, assuming the data type doesn't change underneath us.
	 */
	my_extra = (ValidIOData *) fcinfo->flinfo->fn_extra;
	if (my_extra == NULL)
	{
		fcinfo->flinfo->fn_extra =
			MemoryContextAlloc(fcinfo->flinfo->fn_mcxt,
							   sizeof(ValidIOData));
		my_extra = (ValidIOData *) fcinfo->flinfo->fn_extra;
		my_extra->typoid = InvalidOid;
		/* Detect whether typname argument is constant. */
		my_extra->typname_constant = get_fn_expr_arg_stable(fcinfo->flinfo, 1);
	}

	/*
	 * If the typname argument is constant, we only need to parse it the first
	 * time through.
	 */
	if (my_extra->typoid == InvalidOid || !my_extra->typname_constant)
	{
		char	   *typnamestr = text_to_cstring(typname);
		Oid			typoid;

		/* Parse type-name argument to obtain type OID and encoded typmod. */
		(void) parseTypeString(typnamestr, &typoid, &my_extra->typmod, NULL);

		/* Update type-specific info if typoid changed. */
		if (my_extra->typoid != typoid)
		{
			getTypeInputInfo(typoid,
							 &my_extra->typiofunc,
							 &my_extra->typioparam);
			fmgr_info_cxt(my_extra->typiofunc, &my_extra->inputproc,
						  fcinfo->flinfo->fn_mcxt);
			my_extra->typoid = typoid;
		}
	}

	/* Now we can try to perform the conversion. */
	return InputFunctionCallSafe(&my_extra->inputproc,
								 str,
								 my_extra->typioparam,
								 my_extra->typmod,
								 (Node *) escontext,
								 &converted);
}


/*
 * Is character a valid identifier start?
 * Must match scan.l's {ident_start} character class.
 */
static bool
is_ident_start(unsigned char c)
{
	/* Underscores and ASCII letters are OK */
	if (c == '_')
		return true;
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		return true;
	/* Any high-bit-set character is OK (might be part of a multibyte char) */
	if (IS_HIGHBIT_SET(c))
		return true;
	return false;
}

/*
 * Is character a valid identifier continuation?
 * Must match scan.l's {ident_cont} character class.
 */
static bool
is_ident_cont(unsigned char c)
{
	/* Can be digit or dollar sign ... */
	if ((c >= '0' && c <= '9') || c == '$')
		return true;
	/* ... or an identifier start character */
	return is_ident_start(c);
}

/*
 * parse_ident - parse a SQL qualified identifier into separate identifiers.
 * When strict mode is active (second parameter), then any chars after
 * the last identifier are disallowed.
 */
Datum
parse_ident(PG_FUNCTION_ARGS)
{
	text	   *qualname = PG_GETARG_TEXT_PP(0);
	bool		strict = PG_GETARG_BOOL(1);
	char	   *qualname_str = text_to_cstring(qualname);
	ArrayBuildState *astate = NULL;
	char	   *nextp;
	bool		after_dot = false;

	/*
	 * The code below scribbles on qualname_str in some cases, so we should
	 * reconvert qualname if we need to show the original string in error
	 * messages.
	 */
	nextp = qualname_str;

	/* skip leading whitespace */
	while (scanner_isspace(*nextp))
		nextp++;

	for (;;)
	{
		char	   *curname;
		bool		missing_ident = true;

		if (*nextp == '"')
		{
			char	   *endp;

			curname = nextp + 1;
			for (;;)
			{
				endp = strchr(nextp + 1, '"');
				if (endp == NULL)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							 errmsg("string is not a valid identifier: \"%s\"",
									text_to_cstring(qualname)),
							 errdetail("String has unclosed double quotes.")));
				if (endp[1] != '"')
					break;
				memmove(endp, endp + 1, strlen(endp));
				nextp = endp;
			}
			nextp = endp + 1;
			*endp = '\0';

			if (endp - curname == 0)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("string is not a valid identifier: \"%s\"",
								text_to_cstring(qualname)),
						 errdetail("Quoted identifier must not be empty.")));

			astate = accumArrayResult(astate, CStringGetTextDatum(curname),
									  false, TEXTOID, CurrentMemoryContext);
			missing_ident = false;
		}
		else if (is_ident_start((unsigned char) *nextp))
		{
			char	   *downname;
			int			len;
			text	   *part;

			curname = nextp++;
			while (is_ident_cont((unsigned char) *nextp))
				nextp++;

			len = nextp - curname;

			/*
			 * We don't implicitly truncate identifiers. This is useful for
			 * allowing the user to check for specific parts of the identifier
			 * being too long. It's easy enough for the user to get the
			 * truncated names by casting our output to name[].
			 */
			downname = downcase_identifier(curname, len, false, false);
			part = cstring_to_text_with_len(downname, len);
			astate = accumArrayResult(astate, PointerGetDatum(part), false,
									  TEXTOID, CurrentMemoryContext);
			missing_ident = false;
		}

		if (missing_ident)
		{
			/* Different error messages based on where we failed. */
			if (*nextp == '.')
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("string is not a valid identifier: \"%s\"",
								text_to_cstring(qualname)),
						 errdetail("No valid identifier before \".\".")));
			else if (after_dot)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("string is not a valid identifier: \"%s\"",
								text_to_cstring(qualname)),
						 errdetail("No valid identifier after \".\".")));
			else
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("string is not a valid identifier: \"%s\"",
								text_to_cstring(qualname))));
		}

		while (scanner_isspace(*nextp))
			nextp++;

		if (*nextp == '.')
		{
			after_dot = true;
			nextp++;
			while (scanner_isspace(*nextp))
				nextp++;
		}
		else if (*nextp == '\0')
		{
			break;
		}
		else
		{
			if (strict)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("string is not a valid identifier: \"%s\"",
								text_to_cstring(qualname))));
			break;
		}
	}

	PG_RETURN_DATUM(makeArrayResult(astate, CurrentMemoryContext));
}

/*
 * pg_current_logfile
 *
 * Report current log file used by log collector by scanning current_logfiles.
 */
Datum
pg_current_logfile(PG_FUNCTION_ARGS)
{
	FILE	   *fd;
	char		lbuffer[MAXPGPATH];
	char	   *logfmt;

	/* The log format parameter is optional */
	if (PG_NARGS() == 0 || PG_ARGISNULL(0))
		logfmt = NULL;
	else
	{
		logfmt = text_to_cstring(PG_GETARG_TEXT_PP(0));

		if (strcmp(logfmt, "stderr") != 0 &&
			strcmp(logfmt, "csvlog") != 0 &&
			strcmp(logfmt, "jsonlog") != 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("log format \"%s\" is not supported", logfmt),
					 errhint("The supported log formats are \"stderr\", \"csvlog\", and \"jsonlog\".")));
	}

	fd = AllocateFile(LOG_METAINFO_DATAFILE, "r");
	if (fd == NULL)
	{
		if (errno != ENOENT)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not read file \"%s\": %m",
							LOG_METAINFO_DATAFILE)));
		PG_RETURN_NULL();
	}

#ifdef WIN32
	/* syslogger.c writes CRLF line endings on Windows */
	_setmode(_fileno(fd), _O_TEXT);
#endif

	/*
	 * Read the file to gather current log filename(s) registered by the
	 * syslogger.
	 */
	while (fgets(lbuffer, sizeof(lbuffer), fd) != NULL)
	{
		char	   *log_format;
		char	   *log_filepath;
		char	   *nlpos;

		/* Extract log format and log file path from the line. */
		log_format = lbuffer;
		log_filepath = strchr(lbuffer, ' ');
		if (log_filepath == NULL)
		{
			/* Uh oh.  No space found, so file content is corrupted. */
			elog(ERROR,
				 "missing space character in \"%s\"", LOG_METAINFO_DATAFILE);
			break;
		}

		*log_filepath = '\0';
		log_filepath++;
		nlpos = strchr(log_filepath, '\n');
		if (nlpos == NULL)
		{
			/* Uh oh.  No newline found, so file content is corrupted. */
			elog(ERROR,
				 "missing newline character in \"%s\"", LOG_METAINFO_DATAFILE);
			break;
		}
		*nlpos = '\0';

		if (logfmt == NULL || strcmp(logfmt, log_format) == 0)
		{
			FreeFile(fd);
			PG_RETURN_TEXT_P(cstring_to_text(log_filepath));
		}
	}

	/* Close the current log filename file. */
	FreeFile(fd);

	PG_RETURN_NULL();
}

/*
 * Report current log file used by log collector (1 argument version)
 *
 * note: this wrapper is necessary to pass the sanity check in opr_sanity,
 * which checks that all built-in functions that share the implementing C
 * function take the same number of arguments
 */
Datum
pg_current_logfile_1arg(PG_FUNCTION_ARGS)
{
	return pg_current_logfile(fcinfo);
}

/*
 * SQL wrapper around RelationGetReplicaIndex().
 */
Datum
pg_get_replica_identity_index(PG_FUNCTION_ARGS)
{
	Oid			reloid = PG_GETARG_OID(0);
	Oid			idxoid;
	Relation	rel;

	rel = table_open(reloid, AccessShareLock);
	idxoid = RelationGetReplicaIndex(rel);
	table_close(rel, AccessShareLock);

	if (OidIsValid(idxoid))
		PG_RETURN_OID(idxoid);
	else
		PG_RETURN_NULL();
}

/*
 * Transition function for the ANY_VALUE aggregate
 */
Datum
any_value_transfn(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(PG_GETARG_DATUM(0));
}
