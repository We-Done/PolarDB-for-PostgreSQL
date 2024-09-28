/*-------------------------------------------------------------------------
 *
 * pg_regress_main --- regression test for the main backend
 *
 * This is a C implementation of the previous shell script for running
 * the regression tests, and should be mostly compatible with it.
 * Initial author of C translation: Magnus Hagander
 *
 * This code is released under the terms of the PostgreSQL License.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/test/regress/pg_regress_main.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"

#include "lib/stringinfo.h"
#include "pg_regress.h"

/* POLAR */
extern bool polar_is_enable;
extern bool polar_enable_parallel_execution;

/*
 * start a psql test process for specified file (including redirection),
 * and return process ID
 */
static PID_TYPE
psql_start_test(const char *testname,
				_stringlist **resultfiles,
				_stringlist **expectfiles,
				_stringlist **tags)
{
	PID_TYPE	pid;
	char		infile[MAXPGPATH];
	char		outfile[MAXPGPATH];
	char		expectfile[MAXPGPATH];
	StringInfoData psql_cmd;
	char	   *appnameenv;

	/* POLAR */
	char		polar_replicaoutfile[MAXPGPATH];
	char 		polar_expectfile[MAXPGPATH];

	/*
	 * Look for files in the output dir first, consistent with a vpath search.
	 * This is mainly to create more reasonable error messages if the file is
	 * not found.  It also allows local test overrides when running pg_regress
	 * outside of the source tree.
	 */
	snprintf(infile, sizeof(infile), "%s/sql/%s.sql",
			 outputdir, testname);
	if (!file_exists(infile))
		snprintf(infile, sizeof(infile), "%s/sql/%s.sql",
				 inputdir, testname);

	snprintf(outfile, sizeof(outfile), "%s/results/%s.out",
			 outputdir, testname);

<<<<<<< HEAD
	/* POLAR */
	snprintf(polar_replicaoutfile, sizeof(polar_replicaoutfile), "%s/results/%s.out.replica",
			 outputdir, testname);
	/* POLAR end */

	/* POLAR */
	if (!polar_enable_parallel_execution)
		snprintf(polar_expectfile, sizeof(polar_expectfile), "%s/expected/polardb_%s.out",
				outputdir, testname);
	else
		snprintf(polar_expectfile, sizeof(polar_expectfile), "%s/px_expected/%s.out",
				outputdir, testname);

	if (polar_is_enable && file_exists(polar_expectfile))
		snprintf(expectfile, sizeof(expectfile), "%s", polar_expectfile);
	else
	{
=======
	snprintf(expectfile, sizeof(expectfile), "%s/expected/%s.out",
			 expecteddir, testname);
	if (!file_exists(expectfile))
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		snprintf(expectfile, sizeof(expectfile), "%s/expected/%s.out",
				outputdir, testname);
		if (!file_exists(expectfile))
			snprintf(expectfile, sizeof(expectfile), "%s/expected/%s.out",
					inputdir, testname);
	}
	/* POLAR end */

	add_stringlist_item(resultfiles, outfile);
	add_stringlist_item(expectfiles, expectfile);

	initStringInfo(&psql_cmd);

	if (launcher)
<<<<<<< HEAD
	{
		offset += snprintf(psql_cmd + offset, sizeof(psql_cmd) - offset,
						   "%s ", launcher);
		if (offset >= sizeof(psql_cmd))
		{
			fprintf(stderr, _("command too long\n"));
			exit(2);
		}
	}

	/* POLAR */
	offset += snprintf(psql_cmd + offset, sizeof(psql_cmd) - offset,
					   "\"%s%spsql\" -X -a -q %s -O \"%s\" -d \"%s\" < \"%s\" > \"%s\" 2>&1",
					   bindir ? bindir : "",
					   bindir ? "/" : "",
					   polar_enable_parallel_execution ? " --set=COMPARE_PX_RESULT=on" : "",
					   polar_replicaoutfile,
					   dblist->str,
					   infile,
					   outfile);
	if (offset >= sizeof(psql_cmd))
	{
		fprintf(stderr, _("command too long\n"));
		exit(2);
	}

	/* Shared Server, use common appname */
	appnameenv = psprintf("PGAPPNAME=pg_regress");
	putenv(appnameenv);

	pid = spawn_process(psql_cmd);
=======
		appendStringInfo(&psql_cmd, "%s ", launcher);

	/*
	 * Use HIDE_TABLEAM to hide different AMs to allow to use regression tests
	 * against different AMs without unnecessary differences.
	 */
	appendStringInfo(&psql_cmd,
					 "\"%s%spsql\" -X -a -q -d \"%s\" %s < \"%s\" > \"%s\" 2>&1",
					 bindir ? bindir : "",
					 bindir ? "/" : "",
					 dblist->str,
					 "-v HIDE_TABLEAM=on -v HIDE_TOAST_COMPRESSION=on",
					 infile,
					 outfile);

	appnameenv = psprintf("pg_regress/%s", testname);
	setenv("PGAPPNAME", appnameenv, 1);
	free(appnameenv);

	pid = spawn_process(psql_cmd.data);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	if (pid == INVALID_PID)
	{
		fprintf(stderr, _("could not start process for test %s\n"),
				testname);
		exit(2);
	}

	unsetenv("PGAPPNAME");

	pfree(psql_cmd.data);

	return pid;
}

static void
psql_init(int argc, char **argv)
{
	/* set default regression database name */
	add_stringlist_item(&dblist, "regression");
}

int
main(int argc, char *argv[])
{
	return regression_main(argc, argv,
						   psql_init,
						   psql_start_test,
						   NULL /* no postfunc needed */ );
}
