/*-------------------------------------------------------------------------
 *
 * pg_regress --- regression test driver
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
 * src/test/regress/pg_regress.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"

#include <ctype.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include "common/logging.h"
#include "common/restricted_token.h"
#include "common/string.h"
#include "common/username.h"
#include "getopt_long.h"
#include "lib/stringinfo.h"
#include "libpq-fe.h"
#include "libpq/pqcomm.h"		/* needed for UNIXSOCK_PATH() */
#include "pg_config_paths.h"
#include "pg_regress.h"
#include "portability/instr_time.h"

/* for resultmap we need a list of pairs of strings */
typedef struct _resultmap
{
	char	   *test;
	char	   *type;
	char	   *resultfile;
	struct _resultmap *next;
} _resultmap;

/*
 * Values obtained from Makefile.
 */
char	   *host_platform = HOST_TUPLE;

#ifndef WIN32					/* not used in WIN32 case */
static char *shellprog = SHELLPROG;
#endif

/*
 * On Windows we use -w in diff switches to avoid problems with inconsistent
 * newline representation.  The actual result files will generally have
 * Windows-style newlines, but the comparison files might or might not.
 */
#ifndef WIN32
const char *basic_diff_opts = "";
const char *pretty_diff_opts = "-U3";
#else
const char *basic_diff_opts = "--strip-trailing-cr";
const char *pretty_diff_opts = "--strip-trailing-cr -U3";
#endif

/*
 * The width of the testname field when printing to ensure vertical alignment
 * of test runtimes. This number is somewhat arbitrarily chosen to match the
 * older pre-TAP output format.
 */
#define TESTNAME_WIDTH 36

/*
 * The number times per second that pg_regress checks to see if the test
 * instance server has started and is available for connection.
 */
#define WAIT_TICKS_PER_SECOND 20

typedef enum TAPtype
{
	DIAG = 0,
	BAIL,
	NOTE,
	NOTE_DETAIL,
	NOTE_END,
	TEST_STATUS,
	PLAN,
	NONE,
} TAPtype;

/* options settable from command line */
_stringlist *dblist = NULL;
bool		debug = false;
char	   *inputdir = ".";
char	   *outputdir = ".";
char	   *expecteddir = ".";
char	   *bindir = PGBINDIR;
char	   *launcher = NULL;
static _stringlist *loadextension = NULL;
static int	max_connections = 0;
static int	max_concurrent_tests = 0;
static char *encoding = NULL;
static _stringlist *schedulelist = NULL;
static _stringlist *extra_tests = NULL;
static char *temp_instance = NULL;
static _stringlist *temp_configs = NULL;
static bool nolocale = false;
static bool use_existing = false;
static char *hostname = NULL;
static int	port = -1;
static char portstr[16];
static bool port_specified_by_user = false;
static char *dlpath = PKGLIBDIR;
static char *user = NULL;
static _stringlist *extraroles = NULL;
static char *config_auth_datadir = NULL;

static bool dma_test = false;
static bool dma_cluster = false;
static int	dma_election_timeout = 60;

/* internal variables */
static const char *progname;
static char *logfilename;
static FILE *logfile;
static char *difffilename;
static const char *sockdir;
static const char *temp_sockdir;
static char sockself[MAXPGPATH];
static char socklock[MAXPGPATH];
static StringInfo failed_tests = NULL;
static bool in_note = false;

static _resultmap *resultmap = NULL;

static PID_TYPE postmaster_pid = INVALID_PID;
static bool postmaster_running = false;

static int	success_count = 0;
static int	fail_count = 0;

/* POLAR */
static int 	ignore_count = 0;
static int	polar_ignore_count = 0;
/* POLAR end */

static bool directory_exists(const char *dir);
static void make_directory(const char *dir);

static void test_status_print(bool ok, const char *testname, double runtime, bool parallel);
static void test_status_ok(const char *testname, double runtime, bool parallel);
static void test_status_failed(const char *testname, double runtime, bool parallel);
static void bail_out(bool noatexit, const char *fmt,...) pg_attribute_printf(2, 3);
static void emit_tap_output(TAPtype type, const char *fmt,...) pg_attribute_printf(2, 3);
static void emit_tap_output_v(TAPtype type, const char *fmt, va_list argp) pg_attribute_printf(2, 0);

static StringInfo psql_start_command(void);
static void psql_add_command(StringInfo buf, const char *query,...) pg_attribute_printf(2, 3);
static void psql_end_command(StringInfo buf, const char *database);

/*
 * Convenience macros for printing TAP output with a more shorthand syntax
 * aimed at making the code more readable.
 */
#define plan(x)				emit_tap_output(PLAN, "1..%i", (x))
#define note(...)			emit_tap_output(NOTE, __VA_ARGS__)
#define note_detail(...)	emit_tap_output(NOTE_DETAIL, __VA_ARGS__)
#define diag(...)			emit_tap_output(DIAG, __VA_ARGS__)
#define note_end()			emit_tap_output(NOTE_END, "\n");
#define bail_noatexit(...)	bail_out(true, __VA_ARGS__)
#define bail(...)			bail_out(false, __VA_ARGS__)

static void doputenv(const char *var, const char *val);

/* POLAR */
bool polar_is_enable = true;
bool polar_enable_parallel_execution = false;
bool polar_enable_shared_server_mode = false;

/*
 * allow core files if possible.
 */
#if defined(HAVE_GETRLIMIT)
static void
unlimit_core_size(void)
{
	struct rlimit lim;

	getrlimit(RLIMIT_CORE, &lim);
	if (lim.rlim_max == 0)
	{
		diag("could not set core size: disallowed by hard limit");
		return;
	}
	else if (lim.rlim_max == RLIM_INFINITY || lim.rlim_cur < lim.rlim_max)
	{
		lim.rlim_cur = lim.rlim_max;
		setrlimit(RLIMIT_CORE, &lim);
	}
}
#endif


/*
 * Add an item at the end of a stringlist.
 */
void
add_stringlist_item(_stringlist **listhead, const char *str)
{
	_stringlist *newentry = pg_malloc(sizeof(_stringlist));
	_stringlist *oldentry;

	newentry->str = pg_strdup(str);
	newentry->next = NULL;
	if (*listhead == NULL)
		*listhead = newentry;
	else
	{
		for (oldentry = *listhead; oldentry->next; oldentry = oldentry->next)
			 /* skip */ ;
		oldentry->next = newentry;
	}
}

/*
 * Free a stringlist.
 */
static void
free_stringlist(_stringlist **listhead)
{
	if (listhead == NULL || *listhead == NULL)
		return;
	if ((*listhead)->next != NULL)
		free_stringlist(&((*listhead)->next));
	free((*listhead)->str);
	free(*listhead);
	*listhead = NULL;
}

/*
 * Split a delimited string into a stringlist
 */
static void
split_to_stringlist(const char *s, const char *delim, _stringlist **listhead)
{
	char	   *sc = pg_strdup(s);
	char	   *token;

	while ((token = strsep(&sc, delim)))
	{
		add_stringlist_item(listhead, token);
	}
	free(sc);
}

/*
 * Bailing out is for unrecoverable errors which prevents further testing to
 * occur and after which the test run should be aborted. By passing noatexit
 * as true the process will terminate with _exit(2) and skipping registered
 * exit handlers, thus avoid any risk of bottomless recursion calls to exit.
 */
static void
bail_out(bool noatexit, const char *fmt,...)
{
	va_list		ap;

	va_start(ap, fmt);
	emit_tap_output_v(BAIL, fmt, ap);
	va_end(ap);

	if (noatexit)
		_exit(2);

	exit(2);
}

/*
 * Print the result of a test run and associated metadata like runtime. Care
 * is taken to align testnames and runtimes vertically to ensure the output
 * is human readable while still TAP compliant. Tests run in parallel are
 * prefixed with a '+' and sequential tests with a '-'. This distinction was
 * previously indicated by 'test' prefixing sequential tests while parallel
 * tests were indented by four leading spaces. The meson TAP parser consumes
 * leading space however, so a non-whitespace prefix of the same length is
 * required for both.
 */
static void
test_status_print(bool ok, const char *testname, double runtime, bool parallel)
{
	int			testnumber = fail_count + success_count;

	/*
	 * Testnumbers are padded to 5 characters to ensure that testnames align
	 * vertically (assuming at most 9999 tests).  Testnames are prefixed with
	 * a leading character to indicate being run in parallel or not. A leading
	 * '+' indicates a parallel test, '-' indicates a single test.
	 */
	emit_tap_output(TEST_STATUS, "%sok %-5i%*s %c %-*s %8.0f ms",
					(ok ? "" : "not "),
					testnumber,
	/* If ok, indent with four spaces matching "not " */
					(ok ? (int) strlen("not ") : 0), "",
	/* Prefix a parallel test '+' and a single test with '-' */
					(parallel ? '+' : '-'),
	/* Testnames are padded to align runtimes */
					TESTNAME_WIDTH, testname,
					runtime);
}

static void
test_status_ok(const char *testname, double runtime, bool parallel)
{
	success_count++;

	test_status_print(true, testname, runtime, parallel);
}

static void
test_status_failed(const char *testname, double runtime, bool parallel)
{
	/*
	 * Save failed tests in a buffer such that we can print a summary at the
	 * end with diag() to ensure it's shown even under test harnesses.
	 */
	if (!failed_tests)
		failed_tests = makeStringInfo();
	else
		appendStringInfoChar(failed_tests, ',');

	appendStringInfo(failed_tests, " %s", testname);

	fail_count++;

	test_status_print(false, testname, runtime, parallel);
}


static void
emit_tap_output(TAPtype type, const char *fmt,...)
{
	va_list		argp;

	va_start(argp, fmt);
	emit_tap_output_v(type, fmt, argp);
	va_end(argp);
}

static void
emit_tap_output_v(TAPtype type, const char *fmt, va_list argp)
{
	va_list		argp_logfile;
	FILE	   *fp;
	int			save_errno;

	/*
	 * The fprintf() calls used to output TAP-protocol elements might clobber
	 * errno, so save it here and restore it before vfprintf()-ing the user's
	 * format string, in case it contains %m placeholders.
	 */
	save_errno = errno;

	/*
	 * Diagnostic output will be hidden by prove unless printed to stderr. The
	 * Bail message is also printed to stderr to aid debugging under a harness
	 * which might otherwise not emit such an important message.
	 */
	if (type == DIAG || type == BAIL)
		fp = stderr;
	else
		fp = stdout;

	/*
	 * If we are ending a note_detail line we can avoid further processing and
	 * immediately return following a newline.
	 */
	if (type == NOTE_END)
	{
		in_note = false;
		fprintf(fp, "\n");
		if (logfile)
			fprintf(logfile, "\n");
		return;
	}

	/* Make a copy of the va args for printing to the logfile */
	va_copy(argp_logfile, argp);

	/*
	 * Non-protocol output such as diagnostics or notes must be prefixed by a
	 * '#' character. We print the Bail message like this too.
	 */
	if ((type == NOTE || type == DIAG || type == BAIL)
		|| (type == NOTE_DETAIL && !in_note))
	{
		fprintf(fp, "# ");
		if (logfile)
			fprintf(logfile, "# ");
	}
	errno = save_errno;
	vfprintf(fp, fmt, argp);
	if (logfile)
	{
		errno = save_errno;
		vfprintf(logfile, fmt, argp_logfile);
	}

	/*
	 * If we are entering into a note with more details to follow, register
	 * that the leading '#' has been printed such that subsequent details
	 * aren't prefixed as well.
	 */
	if (type == NOTE_DETAIL)
		in_note = true;

	/*
	 * If this was a Bail message, the bail protocol message must go to stdout
	 * separately.
	 */
	if (type == BAIL)
	{
		fprintf(stdout, "Bail out!");
		if (logfile)
			fprintf(logfile, "Bail out!");
	}

	va_end(argp_logfile);

	if (type != NOTE_DETAIL)
	{
		fprintf(fp, "\n");
		if (logfile)
			fprintf(logfile, "\n");
	}
	fflush(NULL);
}

static void 
polar_dma_create_pg_conf(char *node_name)
{
	FILE	   *pg_conf;
	_stringlist *sl;
	char		buf[MAXPGPATH * 4];

	/*
	 * Adjust the default postgresql.conf for regression testing. The user
	 * can specify a file to be appended; in any case we expand logging
	 * and set max_prepared_transactions to enable testing of prepared
	 * xacts.  (Note: to reduce the probability of unexpected shmmax
	 * failures, don't set max_prepared_transactions any higher than
	 * actually needed by the prepared_xacts regression test.)
	 */
	snprintf(buf, sizeof(buf), "%s/%s/data/postgresql.conf", temp_instance, node_name);
	pg_conf = fopen(buf, "a");
	if (pg_conf == NULL)
	{
		fprintf(stderr, _("\n%s: could not open \"%s\" for adding extra config: %s\n"), progname, buf, strerror(errno));
		exit(2);
	}
	fputs("\n# Configuration added by pg_regress\n\n", pg_conf);
	fputs("log_autovacuum_min_duration = 0\n", pg_conf);
	fputs("log_checkpoints = on\n", pg_conf);
	fputs("log_line_prefix = '%m [%p] [%P] %q%a '\n", pg_conf);
	fputs("log_lock_waits = on\n", pg_conf);
	fputs("log_temp_files = 128kB\n", pg_conf);
	fputs("max_prepared_transactions = 2\n", pg_conf);
	fputs("polar_logindex_mem_size = 0\n", pg_conf);
	fputs("polar_enable_copy_buffer = false\n", pg_conf);

	for (sl = temp_configs; sl != NULL; sl = sl->next)
	{
		char	   *temp_config = sl->str;
		FILE	   *extra_conf;
		char		line_buf[1024];

		extra_conf = fopen(temp_config, "r");
		if (extra_conf == NULL)
		{
			fprintf(stderr, _("\n%s: could not open \"%s\" to read extra config: %s\n"), progname, temp_config, strerror(errno));
			exit(2);
		}
		while (fgets(line_buf, sizeof(line_buf), extra_conf) != NULL)
			fputs(line_buf, pg_conf);
		fclose(extra_conf);
	}

	fclose(pg_conf);

#ifdef ENABLE_SSPI

	/*
	 * Since we successfully used the same buffer for the much-longer
	 * "initdb" command, this can't truncate.
	 */
	snprintf(buf, sizeof(buf), "%s/%s/data", temp_instance, node_name);
	config_sspi_auth(buf);
#elif !defined(HAVE_UNIX_SOCKETS)
#error Platform has no means to secure the test installation.
#endif
}

static void 
polar_dma_create_recovery_conf(char *node_name)
{
	FILE	   	*recovery_conf;
	char		buf[MAXPGPATH * 4];

	snprintf(buf, sizeof(buf), "%s/%s/data/recovery.conf", temp_instance, node_name);
	recovery_conf = fopen(buf, "a");
	if (recovery_conf == NULL)
	{
		fprintf(stderr, _("\n%s: could not open \"%s\" for adding extra config: %s\n"), progname, buf, strerror(errno));
		exit(2);
	}
	fputs("\n# Configuration added by pg_regress\n\n", recovery_conf);
	fputs("polar_datamax_mode = standalone\n", recovery_conf);
	fclose(recovery_conf);
}


static void 
polar_dma_create_dma_conf(char *node_name, bool follower)
{
	FILE	   	*dma_conf;
	char		buf[MAXPGPATH * 4];
	const char	*username;
	char		*errstr;

	snprintf(buf, sizeof(buf), "%s/%s/data/polar_dma.conf", temp_instance, node_name);
	dma_conf = fopen(buf, "a");
	if (dma_conf == NULL)
	{
		fprintf(stderr, _("\n%s: could not open \"%s\" for adding extra config: %s\n"), progname, buf, strerror(errno));
		exit(2);
	}
	fputs("\n# Configuration added by pg_regress\n\n", dma_conf);
	fputs("polar_enable_dma= on\n", dma_conf);
	if (follower)
		fputs("polar_dma_delay_election= on\n", dma_conf);

	if (user == NULL)
	{
		username = get_user_name(&errstr);
		if (username == NULL)
		{
			fprintf(stderr, "%s: %s\n", progname, errstr);
			exit(2);
		}
	}
	else
	{
		username = user;
	}
	snprintf(buf, sizeof(buf), "polar_dma_repl_user= %s\n", username);
	fputs(buf, dma_conf);

	fclose(dma_conf);
}

static int 
polar_dma_start_node(char *node_name, int node_port, bool follower)
{
	PID_TYPE current_postmaster_pid = INVALID_PID;
	char		buf[MAXPGPATH * 4];
	const char *env_wait;
	int			wait_seconds;
	int			i;

	/*
	 * Start the temp postmaster
	 */
	header(_("starting postmaster of %s on port %d"), node_name, node_port);
	snprintf(buf, sizeof(buf),
			"\"%s%spostgres\" -D \"%s/%s/data\" %s "
			"-c \"listen_addresses=%s\" -k \"%s\" -p %d "
			"> \"%s/log/%s/postmaster.log\" 2>&1",
			bindir ? bindir : "",
			bindir ? "/" : "",
			temp_instance, node_name, debug ? " -d 5" : "",
			hostname ? hostname : "localhost", sockdir ? sockdir : "",
			node_port, outputdir, node_name);
	fprintf(stdout, "================================================\n"
			 		"cmd: %s\n"
					"================================================\n", buf);
	current_postmaster_pid = spawn_process(buf);
	if (current_postmaster_pid == INVALID_PID)
	{
		fprintf(stderr, _("\n%s: could not spawn postmaster of %s: %s\n"),
				progname, node_name, strerror(errno));
		exit(2);
	}

	if (follower)
	{
		psql_command("postgres", "alter system dma add follower \'%s:%d\'", 
				hostname ? hostname : "localhost", node_port);
	}	

	/*
	 * Check if there is a postmaster running already.
	 */
	snprintf(buf, sizeof(buf),
			"\"%s%spsql\" -X template1 -p %d <%s 2>%s",
			bindir ? bindir : "",
			bindir ? "/" : "",
			node_port,
			DEVNULL, DEVNULL);

	/*
	 * Wait till postmaster is able to accept connections; normally this
	 * is only a second or so, but Cygwin is reportedly *much* slower, and
	 * test builds using Valgrind or similar tools might be too.  Hence,
	 * allow the default timeout of 60 seconds to be overridden from the
	 * PGCTLTIMEOUT environment variable.
	 */
	env_wait = getenv("PGCTLTIMEOUT");
	if (env_wait != NULL)
	{
		wait_seconds = atoi(env_wait);
		if (wait_seconds <= 0)
			wait_seconds = 60;
	}
	else
		wait_seconds = 60;

	for (i = 0; i < wait_seconds; i++)
	{
		/* Done if psql succeeds */
		if (system(buf) == 0)
			break;

		/*
		 * Fail immediately if postmaster has exited
		 */
#ifndef WIN32
		if (waitpid(current_postmaster_pid, NULL, WNOHANG) == current_postmaster_pid)
#else
			if (WaitForSingleObject(current_postmaster_pid, 0) == WAIT_OBJECT_0)
#endif
			{
				fprintf(stderr, _("\n%s: postmaster failed\nExamine %s/log/%s/postmaster.log for the reason\n"), progname, outputdir, node_name);
				exit(2);
			}

		pg_usleep(1000000L);
	}
	if (i >= wait_seconds)
	{
		fprintf(stderr, _("\n%s: postmaster did not respond within %d seconds\nExamine %s/log/%s/postmaster.log for the reason\n"),
				progname, wait_seconds, outputdir, node_name);

		/*
		 * If we get here, the postmaster is probably wedged somewhere in
		 * startup.  Try to kill it ungracefully rather than leaving a
		 * stuck postmaster that might interfere with subsequent test
		 * attempts.
		 */
#ifndef WIN32
		if (kill(current_postmaster_pid, SIGKILL) != 0 &&
				errno != ESRCH)
			fprintf(stderr, _("\n%s: could not kill failed postmaster: %s\n"),
					progname, strerror(errno));
#else
		if (TerminateProcess(current_postmaster_pid, 255) == 0)
			fprintf(stderr, _("\n%s: could not kill failed postmaster: error code %lu\n"),
					progname, GetLastError());
#endif

		exit(2);
	}

#ifdef _WIN64
	/* need a series of two casts to convert HANDLE without compiler warning */
#define ULONGPID(x) (unsigned long) (unsigned long long) (x)
#else
#define ULONGPID(x) (unsigned long) (x)
#endif
	printf(_("%s running on port %d with PID %lu\n"),
			node_name, port, ULONGPID(current_postmaster_pid));

	return current_postmaster_pid;
}

static int
polar_dma_init_node(char *node_name, int node_port, uint64 sys_identifier, bool logger)
{
	char		buf[MAXPGPATH * 4];
	char		buf2[MAXPGPATH * 4];
	int			i;

	/* initdb */
	header(_("initializing database system"));
	snprintf(buf, sizeof(buf),
			"\"%s%sinitdb\" -D \"%s/%s/data\" --no-clean -i %lu --no-sync%s%s > \"%s/log/initdb.log\" 2>&1",
			bindir ? bindir : "",
			bindir ? "/" : "",
			temp_instance,
			node_name,
			sys_identifier,
			debug ? " --debug" : "",
			nolocale ? " --no-locale" : "",
			outputdir);
	fprintf(stdout, "================================================\n"
			 		"cmd: %s\n"
					"================================================\n", buf);
	if (system(buf))
	{
		fprintf(stderr, _("\n%s: initdb failed\nExamine %s/log/%s/initdb.log for the reason.\nCommand was: %s\n"), progname, outputdir, node_name, buf);
		exit(2);
	}

	polar_dma_create_pg_conf(node_name);
	polar_dma_create_dma_conf(node_name, false);
	if (logger)
		polar_dma_create_recovery_conf(node_name);

	/*
	 * Check if there is a postmaster running already.
	 */
	snprintf(buf2, sizeof(buf2),
			"\"%s%spsql\" -X template1 -p %d <%s 2>%s",
			bindir ? bindir : "",
			bindir ? "/" : "",
			node_port,
			DEVNULL, DEVNULL);


	for (i = 0; i < 16; i++)
	{
		if (system(buf2) == 0)
		{
			char		s[16];

			if (port_specified_by_user || i == 15)
			{
				fprintf(stderr, _("port %d apparently in use\n"), node_port);
				if (!port_specified_by_user)
					fprintf(stderr, _("%s: could not determine an available port\n"), progname);
				fprintf(stderr, _("Specify an unused port using the --port option or shut down any conflicting PostgreSQL servers.\n"));
				exit(2);
			}

			fprintf(stderr, _("port %d apparently in use, trying %d\n"), node_port, node_port + 1);
			node_port++;
			sprintf(s, "%d", node_port);
			doputenv("PGPORT", s);
		}
		else
			break;
	}

	/*
	 * Start the temp postmaster
	 */
	header(_("init dma meta for node: %s"), node_name);
	snprintf(buf, sizeof(buf),
			"\"%s%spostgres\" -D \"%s/%s/data\" %s "
			"-c \"listen_addresses=%s\" -k \"%s\" "
			"-c \"polar_dma_init_meta=ON\" "
			"-c \"%s=\"%s:%d%s\"\" "
			"-p %d "
			"> \"%s/log/%s/postmaster_init_meta.log\" 2>&1",
			bindir ? bindir : "",
			bindir ? "/" : "",
			temp_instance, node_name, debug ? " -d 5" : "",
			hostname ? hostname : "localhost", sockdir ? sockdir : "",
			logger ? "polar_dma_learners_info" : "polar_dma_members_info",
			hostname ? hostname : "localhost", node_port,
			logger ? "" : "@1", node_port,
			outputdir, node_name);
	fprintf(stdout, "================================================\n"
			 		"cmd: %s\n"
					"================================================\n", buf);
	if (system(buf))
	{
		fprintf(stderr, _("\n%s: could not init dma meta of %s: %s\n"),
				progname, node_name, strerror(errno));
		exit(2);
	}
	return node_port;
}

static int 
polar_dma_build_node_from_backup(char *node_name, int start_port)
{
	char		buf[MAXPGPATH * 4];
	char		buf2[MAXPGPATH * 4];
	int			current_port = start_port;
	int 		i;

	/* polar_basebackup */
	header(_("initializing cluster node: %s"), node_name);
	snprintf(buf, sizeof(buf),
			"\"%s%spolar_basebackup\" -D %s/%s/data -p %d > \"%s/log/%s/basebackup.log\" 2>&1",
			bindir ? bindir : "",
			bindir ? "/" : "",
			temp_instance,
			node_name,
			port,
			outputdir,
			node_name);
	fprintf(stdout, "================================================\n"
			 		"cmd: %s\n"
					"================================================\n", buf);
	if (system(buf))
	{
		fprintf(stderr, _("\n%s: polar_basebackup failed\nExamine %s/log/%s/basebackup.log for the reason.\nCommand was: %s\n"), progname, outputdir, node_name, buf);
		exit(2);
	}

	polar_dma_create_pg_conf(node_name);
	polar_dma_create_dma_conf(node_name, true);

	/*
	 * Check if there is a postmaster running already.
	 */
	snprintf(buf2, sizeof(buf2),
			"\"%s%spsql\" -X template1 -p %d <%s 2>%s",
			bindir ? bindir : "",
			bindir ? "/" : "",
			current_port,
			DEVNULL, DEVNULL);

	for (i = 0; i < 16; i++)
	{
		if (current_port > 65535 - 10000)
		{
			fprintf(stderr, _("port %d beyond 55535"), port);
			exit(2);
		}
		if (system(buf2) == 0)
		{
			char		s[16];

			fprintf(stderr, _("port %d apparently in use, trying %d\n"), port, port + 1);
			current_port++;
			sprintf(s, "%d", current_port);
			doputenv("PGPORT", s);
		}
		else
			break;
	}

	/*
	 * Start the temp postmaster
	 */
	header(_("init dma meta of %s"), node_name);
	snprintf(buf, sizeof(buf),
			"\"%s%spostgres\" -D \"%s/%s/data\" %s "
			"-c \"listen_addresses=%s\" -k \"%s\" "
			"-c \"polar_dma_init_meta=ON\" "
			"-c \"polar_dma_learners_info=\"%s:%d\"\" "
			"-p %d "
			"> \"%s/log/%s/postmaster_init_meta.log\" 2>&1",
			bindir ? bindir : "",
			bindir ? "/" : "",
			temp_instance, node_name, debug ? " -d 5" : "",
			hostname ? hostname : "localhost", sockdir ? sockdir : "",
			hostname ? hostname : "localhost", current_port, current_port,
			outputdir, node_name);
	fprintf(stdout, "================================================\n"
			 		"cmd: %s\n"
					"================================================\n", buf);
	if (system(buf))
	{
		fprintf(stderr, _("\n%s: could not init dma meta of %s: %s\n"),
				progname, node_name, strerror(errno));
		exit(2);
	}
	return current_port;
}

static bool 
wait_became_leader(char *node_name, int leader_port)
{
	char		buf[MAXPGPATH * 4];

	/*
	 * Check if there is a postmaster running already.
	 */
	snprintf(buf, sizeof(buf),
			"\"%s%spsql\" -X postgres -p %d -c \"do \\$\\$ "
			" DECLARE "
			"   counter integer := %d; "
			"	BEGIN "
			"	  LOOP "
			"     PERFORM 1 where pg_is_in_recovery() = false; "
			"	  IF FOUND THEN "
			"       RAISE NOTICE \'%s became leader\'; "
			"		    EXIT; "
			"	  END IF;"
			"     PERFORM pg_sleep(1); "
			"		  counter := counter - 1; "
			"		  IF counter = 0 THEN "
			" 		  RAISE EXCEPTION \'became leader timeout\'; "
			"		    EXIT; "
			"	    END IF; "
			"	  END LOOP; "
			" END\\$\\$;\" ",
			bindir ? bindir : "",
			bindir ? "/" : "",
			leader_port,
			dma_election_timeout, node_name);

	fprintf(stdout, "================================================\n"
			 		"cmd: %s\n"
					"================================================\n", buf);

	/* kill postmaster if psql failed */
	if (system(buf) != 0)
	{
		return false;
	}
	return true;
}

static void 
start_postmaster_cluster(void)
{
	char		buf[MAXPGPATH * 4];
	int			port_f1, port_f2;
	bool		suc;
	uint64	sysidentifier;
	struct 	timeval tv;

	/*
	 * Prepare the temp instance
	 */

	if (directory_exists(temp_instance))
	{
		header(_("removing existing temp instance"));
		if (!rmtree(temp_instance, true))
		{
			fprintf(stderr, _("\n%s: could not remove temp instance \"%s\"\n"),
					progname, temp_instance);
			exit(2);
		}
	}

	header(_("creating temporary instance"));

	/* make the temp instance top directory */
	make_directory(temp_instance);
	snprintf(buf, sizeof(buf), "%s/node1", temp_instance);
	make_directory(buf);
	if (dma_cluster)
	{
		snprintf(buf, sizeof(buf), "%s/node2", temp_instance);
		make_directory(buf);
		snprintf(buf, sizeof(buf), "%s/node3", temp_instance);
		make_directory(buf);
	}

	/* and a directory for log files */
	snprintf(buf, sizeof(buf), "%s/log", outputdir);
	if (!directory_exists(buf))
		make_directory(buf);
	snprintf(buf, sizeof(buf), "%s/log/node1", outputdir);
	if (!directory_exists(buf))
		make_directory(buf);
	if (dma_cluster)
	{
		snprintf(buf, sizeof(buf), "%s/log/node2", outputdir);
		if (!directory_exists(buf))
			make_directory(buf);
		snprintf(buf, sizeof(buf), "%s/log/node3", outputdir);
		if (!directory_exists(buf))
			make_directory(buf);
	}

	gettimeofday(&tv, NULL);
	sysidentifier = ((uint64) tv.tv_sec) << 32;
	sysidentifier |= ((uint64) tv.tv_usec) << 12;
	sysidentifier |= getpid() & 0xFFF;

	port = polar_dma_init_node("node1", port, sysidentifier, false);
	postmaster_pid = polar_dma_start_node("node1", port, false);
	suc = wait_became_leader("node1", port);
	if (!suc)
	{
#ifndef WIN32
		if (kill(postmaster_pid, SIGKILL) != 0 &&
				errno != ESRCH)
			fprintf(stderr, _("\n%s: could not kill failed postmaster: %s\n"),
					progname, strerror(errno));
#else
		if (TerminateProcess(postmaster_pid, 255) == 0)
			fprintf(stderr, _("\n%s: could not kill failed postmaster: error code %lu\n"),
					progname, GetLastError());
#endif
	}

	postmaster_running = true;

	if (dma_cluster)
	{
		port_f1 = polar_dma_build_node_from_backup("node2", port+1);
		polar_dma_start_node("node2", port_f1, true);
		port_f2 = polar_dma_init_node("node3", port_f1+1, sysidentifier, true);
		polar_dma_start_node("node3", port_f2, true);
	}
}

static int 
stop_postmaster_node(char *node_name)
{
	/* We use pg_ctl to issue the kill and wait for stop */
	char		buf[MAXPGPATH * 2];
	int			r;

	/* On Windows, system() seems not to force fflush, so... */
	fflush(stdout);
	fflush(stderr);

	snprintf(buf, sizeof(buf),
			"\"%s%spg_ctl\" stop -D \"%s/%s/data\" -s",
			bindir ? bindir : "",
			bindir ? "/" : "",
			temp_instance,
			node_name);
	r = system(buf);

	return r;
}

/*
 * shut down temp postmaster
 */
static void
stop_postmaster(void)
{
	if (dma_test)
	{
		if (postmaster_running)
		{
			int			r1, r2 = 0, r3 = 0;
			postmaster_running = false;
			r1 = stop_postmaster_node("node1");
			if (dma_cluster)
			{
				r2 = stop_postmaster_node("node2");
				r3 = stop_postmaster_node("node3");
			}	
			if (r1 != 0 || r2 != 0 || r3 != 0)
			{
				fprintf(stderr, _("\n%s: could not stop postmaster: exit code was %d, %d, %d\n"),
						progname, r1, r2, r3);
				_exit(2);			/* not exit(), that could be recursive */
			}
			postmaster_running = false;
		}
	}
	else if (postmaster_running)
	{
		/* We use pg_ctl to issue the kill and wait for stop */
		char		buf[MAXPGPATH * 2];
		int			r;

		snprintf(buf, sizeof(buf),
				 "\"%s%spg_ctl\" stop -D \"%s/data\" -s",
				 bindir ? bindir : "",
				 bindir ? "/" : "",
				 temp_instance);
		fflush(NULL);
		r = system(buf);
		if (r != 0)
		{
			/* Not using the normal bail() as we want _exit */
			bail_noatexit(_("could not stop postmaster: exit code was %d"), r);
		}

		postmaster_running = false;
	}
}

/*
 * Remove the socket temporary directory.  pg_regress never waits for a
 * postmaster exit, so it is indeterminate whether the postmaster has yet to
 * unlink the socket and lock file.  Unlink them here so we can proceed to
 * remove the directory.  Ignore errors; leaking a temporary directory is
 * unimportant.  This can run from a signal handler.  The code is not
 * acceptable in a Windows signal handler (see initdb.c:trapsig()), but
 * on Windows, pg_regress does not use Unix sockets by default.
 */
static void
remove_temp(void)
{
	Assert(temp_sockdir);
	unlink(sockself);
	unlink(socklock);
	rmdir(temp_sockdir);
}

/*
 * Signal handler that calls remove_temp() and reraises the signal.
 */
static void
signal_remove_temp(SIGNAL_ARGS)
{
	remove_temp();

	pqsignal(postgres_signal_arg, SIG_DFL);
	raise(postgres_signal_arg);
}

/*
 * Create a temporary directory suitable for the server's Unix-domain socket.
 * The directory will have mode 0700 or stricter, so no other OS user can open
 * our socket to exploit our use of trust authentication.  Most systems
 * constrain the length of socket paths well below _POSIX_PATH_MAX, so we
 * place the directory under /tmp rather than relative to the possibly-deep
 * current working directory.
 *
 * Compared to using the compiled-in DEFAULT_PGSOCKET_DIR, this also permits
 * testing to work in builds that relocate it to a directory not writable to
 * the build/test user.
 */
static const char *
make_temp_sockdir(void)
{
	char	   *template = psprintf("%s/pg_regress-XXXXXX",
									getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");

	temp_sockdir = mkdtemp(template);
	if (temp_sockdir == NULL)
		bail("could not create directory \"%s\": %m", template);

	/* Stage file names for remove_temp().  Unsafe in a signal handler. */
	UNIXSOCK_PATH(sockself, port, temp_sockdir);
	snprintf(socklock, sizeof(socklock), "%s.lock", sockself);

	/* Remove the directory during clean exit. */
	atexit(remove_temp);

	/*
	 * Remove the directory before dying to the usual signals.  Omit SIGQUIT,
	 * preserving it as a quick, untidy exit.
	 */
	pqsignal(SIGHUP, signal_remove_temp);
	pqsignal(SIGINT, signal_remove_temp);
	pqsignal(SIGPIPE, signal_remove_temp);
	pqsignal(SIGTERM, signal_remove_temp);

	return temp_sockdir;
}

/*
 * Check whether string matches pattern
 *
 * In the original shell script, this function was implemented using expr(1),
 * which provides basic regular expressions restricted to match starting at
 * the string start (in conventional regex terms, there's an implicit "^"
 * at the start of the pattern --- but no implicit "$" at the end).
 *
 * For now, we only support "." and ".*" as non-literal metacharacters,
 * because that's all that anyone has found use for in resultmap.  This
 * code could be extended if more functionality is needed.
 */
static bool
string_matches_pattern(const char *str, const char *pattern)
{
	while (*str && *pattern)
	{
		if (*pattern == '.' && pattern[1] == '*')
		{
			pattern += 2;
			/* Trailing .* matches everything. */
			if (*pattern == '\0')
				return true;

			/*
			 * Otherwise, scan for a text position at which we can match the
			 * rest of the pattern.
			 */
			while (*str)
			{
				/*
				 * Optimization to prevent most recursion: don't recurse
				 * unless first pattern char might match this text char.
				 */
				if (*str == *pattern || *pattern == '.')
				{
					if (string_matches_pattern(str, pattern))
						return true;
				}

				str++;
			}

			/*
			 * End of text with no match.
			 */
			return false;
		}
		else if (*pattern != '.' && *str != *pattern)
		{
			/*
			 * Not the single-character wildcard and no explicit match? Then
			 * time to quit...
			 */
			return false;
		}

		str++;
		pattern++;
	}

	if (*pattern == '\0')
		return true;			/* end of pattern, so declare match */

	/* End of input string.  Do we have matching pattern remaining? */
	while (*pattern == '.' && pattern[1] == '*')
		pattern += 2;
	if (*pattern == '\0')
		return true;			/* end of pattern, so declare match */

	return false;
}

/*
<<<<<<< HEAD
 * Replace all occurrences of a string in a string with a different string.
 * NOTE: Assumes there is enough room in the target buffer!
 */
void
replace_string(char *string, const char *replace, const char *replacement)
{
	char	   *ptr;

	while ((ptr = strstr(string, replace)) != NULL)
	{
		char	   *dup = pg_strdup(string);

		strlcpy(string, dup, ptr - string + 1);
		strcat(string, replacement);
		strcat(string, dup + (ptr - string) + strlen(replace));
		free(dup);
	}
}

/*
 * Convert *.source found in the "source" directory, replacing certain tokens
 * in the file contents with their intended values, and put the resulting files
 * in the "dest" directory, replacing the ".source" prefix in their names with
 * the given suffix.
 */
static void
convert_sourcefiles_in(const char *source_subdir, const char *dest_dir, const char *dest_subdir, const char *suffix)
{
	char		testtablespace[MAXPGPATH];
	char		indir[MAXPGPATH];
	struct stat st;
	int			ret;
	char	  **name;
	char	  **names;
	int			count = 0;

	snprintf(indir, MAXPGPATH, "%s/%s", inputdir, source_subdir);

	/* Check that indir actually exists and is a directory */
	ret = stat(indir, &st);
	if (ret != 0 || !S_ISDIR(st.st_mode))
	{
		/*
		 * No warning, to avoid noise in tests that do not have these
		 * directories; for example, ecpg, contrib and src/pl.
		 */
		return;
	}

	names = pgfnames(indir);
	if (!names)
		/* Error logged in pgfnames */
		exit(2);

	snprintf(testtablespace, MAXPGPATH, "%s/testtablespace", outputdir);

#ifdef WIN32

	/*
	 * On Windows only, clean out the test tablespace dir, or create it if it
	 * doesn't exist.  On other platforms we expect the Makefile to take care
	 * of that.  (We don't migrate that functionality in here because it'd be
	 * harder to cope with platform-specific issues such as SELinux.)
	 *
	 * XXX it would be better if pg_regress.c had nothing at all to do with
	 * testtablespace, and this were handled by a .BAT file or similar on
	 * Windows.  See pgsql-hackers discussion of 2008-01-18.
	 */
	if (directory_exists(testtablespace))
		if (!rmtree(testtablespace, true))
		{
			fprintf(stderr, _("\n%s: could not remove test tablespace \"%s\"\n"),
					progname, testtablespace);
			exit(2);
		}
	make_directory(testtablespace);
#endif

	/* finally loop on each file and do the replacement */
	for (name = names; *name; name++)
	{
		char		srcfile[MAXPGPATH];
		char		destfile[MAXPGPATH];
		char		prefix[MAXPGPATH];
		FILE	   *infile,
				   *outfile;
		char		line[1024];

		/* reject filenames not finishing in ".source" */
		if (strlen(*name) < 8)
			continue;
		if (strcmp(*name + strlen(*name) - 7, ".source") != 0)
			continue;

		count++;

		/* POLAR */
		if (strcmp(dest_subdir, "expected") == 0 && strlen(*name) >= 16 && strcmp(*name + strlen(*name) - 15, ".replica.source") == 0)
		{
			snprintf(prefix, strlen(*name) - 14, "%s", *name);
			snprintf(srcfile, MAXPGPATH, "%s/%s", indir, *name);
			snprintf(destfile, MAXPGPATH, "%s/%s/%s.out.replica", dest_dir, dest_subdir, prefix);
		}
		else
		{
			/* build the full actual paths to open */
			snprintf(prefix, strlen(*name) - 6, "%s", *name);
			snprintf(srcfile, MAXPGPATH, "%s/%s", indir, *name);
			snprintf(destfile, MAXPGPATH, "%s/%s/%s.%s", dest_dir, dest_subdir,
					 prefix, suffix);
		}
		/* POLAR end */

		infile = fopen(srcfile, "r");
		if (!infile)
		{
			fprintf(stderr, _("%s: could not open file \"%s\" for reading: %s\n"),
					progname, srcfile, strerror(errno));
			exit(2);
		}
		outfile = fopen(destfile, "w");
		if (!outfile)
		{
			fprintf(stderr, _("%s: could not open file \"%s\" for writing: %s\n"),
					progname, destfile, strerror(errno));
			exit(2);
		}
		while (fgets(line, sizeof(line), infile))
		{
			replace_string(line, "@abs_srcdir@", inputdir);
			replace_string(line, "@abs_builddir@", outputdir);
			replace_string(line, "@testtablespace@", testtablespace);
			replace_string(line, "@libdir@", dlpath);
			replace_string(line, "@DLSUFFIX@", DLSUFFIX);
			fputs(line, outfile);
		}
		fclose(infile);
		fclose(outfile);
	}

	/*
	 * If we didn't process any files, complain because it probably means
	 * somebody neglected to pass the needed --inputdir argument.
	 */
	if (count <= 0)
	{
		fprintf(stderr, _("%s: no *.source files found in \"%s\"\n"),
				progname, indir);
		exit(2);
	}

	pgfnames_cleanup(names);
}

/* Create the .sql and .out files from the .source files, if any */
static void
convert_sourcefiles(void)
{
	convert_sourcefiles_in("input", outputdir, "sql", "sql");
	convert_sourcefiles_in("output", outputdir, "expected", "out");
}

/*
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
 * Scan resultmap file to find which platform-specific expected files to use.
 *
 * The format of each line of the file is
 *		   testname/hostplatformpattern=substitutefile
 * where the hostplatformpattern is evaluated per the rules of expr(1),
 * namely, it is a standard regular expression with an implicit ^ at the start.
 * (We currently support only a very limited subset of regular expressions,
 * see string_matches_pattern() above.)  What hostplatformpattern will be
 * matched against is the config.guess output.  (In the shell-script version,
 * we also provided an indication of whether gcc or another compiler was in
 * use, but that facility isn't used anymore.)
 */
static void
load_resultmap(void)
{
	char		buf[MAXPGPATH];
	FILE	   *f;

	/* scan the file ... */
	snprintf(buf, sizeof(buf), "%s/resultmap", inputdir);
	f = fopen(buf, "r");
	if (!f)
	{
		/* OK if it doesn't exist, else complain */
		if (errno == ENOENT)
			return;
		bail("could not open file \"%s\" for reading: %m", buf);
	}

	while (fgets(buf, sizeof(buf), f))
	{
		char	   *platform;
		char	   *file_type;
		char	   *expected;
		int			i;

		/* strip trailing whitespace, especially the newline */
		i = strlen(buf);
		while (i > 0 && isspace((unsigned char) buf[i - 1]))
			buf[--i] = '\0';

		/* parse out the line fields */
		file_type = strchr(buf, ':');
		if (!file_type)
		{
			bail("incorrectly formatted resultmap entry: %s", buf);
		}
		*file_type++ = '\0';

		platform = strchr(file_type, ':');
		if (!platform)
		{
			bail("incorrectly formatted resultmap entry: %s", buf);
		}
		*platform++ = '\0';
		expected = strchr(platform, '=');
		if (!expected)
		{
			bail("incorrectly formatted resultmap entry: %s", buf);
		}
		*expected++ = '\0';

		/*
		 * if it's for current platform, save it in resultmap list. Note: by
		 * adding at the front of the list, we ensure that in ambiguous cases,
		 * the last match in the resultmap file is used. This mimics the
		 * behavior of the old shell script.
		 */
		if (string_matches_pattern(host_platform, platform))
		{
			_resultmap *entry = pg_malloc(sizeof(_resultmap));

			entry->test = pg_strdup(buf);
			entry->type = pg_strdup(file_type);
			entry->resultfile = pg_strdup(expected);
			entry->next = resultmap;
			resultmap = entry;
		}
	}
	fclose(f);
}

/*
 * Check in resultmap if we should be looking at a different file
 */
static
const char *
get_expectfile(const char *testname, const char *file)
{
	char	   *file_type;
	_resultmap *rm;

	/*
	 * Determine the file type from the file name. This is just what is
	 * following the last dot in the file name.
	 */
	if (!file || !(file_type = strrchr(file, '.')))
		return NULL;

	file_type++;

	for (rm = resultmap; rm != NULL; rm = rm->next)
	{
		if (strcmp(testname, rm->test) == 0 && strcmp(file_type, rm->type) == 0)
		{
			return rm->resultfile;
		}
	}

	return NULL;
}

/*
 * Prepare environment variables for running regression tests
 */
static void
initialize_environment(void)
{
	/*
	 * Set default application_name.  (The test_start_function may choose to
	 * override this, but if it doesn't, we have something useful in place.)
	 */
	setenv("PGAPPNAME", "pg_regress", 1);

	/*
	 * Set variables that the test scripts may need to refer to.
	 */
	setenv("PG_ABS_SRCDIR", inputdir, 1);
	setenv("PG_ABS_BUILDDIR", outputdir, 1);
	setenv("PG_LIBDIR", dlpath, 1);
	setenv("PG_DLSUFFIX", DLSUFFIX, 1);

	if (nolocale)
	{
		/*
		 * Clear out any non-C locale settings
		 */
		unsetenv("LC_COLLATE");
		unsetenv("LC_CTYPE");
		unsetenv("LC_MONETARY");
		unsetenv("LC_NUMERIC");
		unsetenv("LC_TIME");
		unsetenv("LANG");

		/*
		 * Most platforms have adopted the POSIX locale as their
		 * implementation-defined default locale.  Exceptions include native
		 * Windows, macOS with --enable-nls, and Cygwin with --enable-nls.
		 * (Use of --enable-nls matters because libintl replaces setlocale().)
		 * Also, PostgreSQL does not support macOS with locale environment
		 * variables unset; see PostmasterMain().
		 */
#if defined(WIN32) || defined(__CYGWIN__) || defined(__darwin__)
		setenv("LANG", "C", 1);
#endif
	}

	/*
	 * Set translation-related settings to English; otherwise psql will
	 * produce translated messages and produce diffs.  (XXX If we ever support
	 * translation of pg_regress, this needs to be moved elsewhere, where psql
	 * is actually called.)
	 */
	unsetenv("LANGUAGE");
	unsetenv("LC_ALL");
	setenv("LC_MESSAGES", "C", 1);

	/*
	 * Set encoding as requested
	 */
	if (encoding)
		setenv("PGCLIENTENCODING", encoding, 1);
	else
		unsetenv("PGCLIENTENCODING");

	/*
	 * Set timezone and datestyle for datetime-related tests
	 */
	setenv("PGTZ", "America/Los_Angeles", 1);
	setenv("PGDATESTYLE", "Postgres, MDY", 1);

	/*
	 * Likewise set intervalstyle to ensure consistent results.  This is a bit
	 * more painful because we must use PGOPTIONS, and we want to preserve the
	 * user's ability to set other variables through that.
	 */
	{
		const char *my_pgoptions = "-c intervalstyle=postgres_verbose";
		const char *old_pgoptions = getenv("PGOPTIONS");
		char	   *new_pgoptions;

		if (!old_pgoptions)
			old_pgoptions = "";
		new_pgoptions = psprintf("%s %s",
								 old_pgoptions, my_pgoptions);
		setenv("PGOPTIONS", new_pgoptions, 1);
		free(new_pgoptions);
	}

	if (temp_instance)
	{
		/*
		 * Clear out any environment vars that might cause psql to connect to
		 * the wrong postmaster, or otherwise behave in nondefault ways. (Note
		 * we also use psql's -X switch consistently, so that ~/.psqlrc files
		 * won't mess things up.)  Also, set PGPORT to the temp port, and set
		 * PGHOST depending on whether we are using TCP or Unix sockets.
		 *
		 * This list should be kept in sync with PostgreSQL/Test/Utils.pm.
		 */
		unsetenv("PGCHANNELBINDING");
		/* PGCLIENTENCODING, see above */
		unsetenv("PGCONNECT_TIMEOUT");
		unsetenv("PGDATA");
		unsetenv("PGDATABASE");
		unsetenv("PGGSSDELEGATION");
		unsetenv("PGGSSENCMODE");
		unsetenv("PGGSSLIB");
		/* PGHOSTADDR, see below */
		unsetenv("PGKRBSRVNAME");
		unsetenv("PGPASSFILE");
		unsetenv("PGPASSWORD");
		unsetenv("PGREQUIREPEER");
		unsetenv("PGREQUIRESSL");
		unsetenv("PGSERVICE");
		unsetenv("PGSERVICEFILE");
		unsetenv("PGSSLCERT");
		unsetenv("PGSSLCRL");
		unsetenv("PGSSLCRLDIR");
		unsetenv("PGSSLKEY");
		unsetenv("PGSSLMAXPROTOCOLVERSION");
		unsetenv("PGSSLMINPROTOCOLVERSION");
		unsetenv("PGSSLMODE");
		unsetenv("PGSSLROOTCERT");
		unsetenv("PGSSLSNI");
		unsetenv("PGTARGETSESSIONATTRS");
		unsetenv("PGUSER");
		/* PGPORT, see below */
		/* PGHOST, see below */

		if (hostname != NULL)
			setenv("PGHOST", hostname, 1);
		else
		{
			sockdir = getenv("PG_REGRESS_SOCK_DIR");
			if (!sockdir)
				sockdir = make_temp_sockdir();
			setenv("PGHOST", sockdir, 1);
		}
		unsetenv("PGHOSTADDR");
		if (port != -1)
		{
			char		s[16];

			snprintf(s, sizeof(s), "%d", port);
			setenv("PGPORT", s, 1);
		}
	}
	else
	{
		const char *pghost;
		const char *pgport;

		/*
		 * When testing an existing install, we honor existing environment
		 * variables, except if they're overridden by command line options.
		 */
		if (hostname != NULL)
		{
			setenv("PGHOST", hostname, 1);
			unsetenv("PGHOSTADDR");
		}
		if (port != -1)
		{
			char		s[16];

			snprintf(s, sizeof(s), "%d", port);
			setenv("PGPORT", s, 1);
		}
		if (user != NULL)
			setenv("PGUSER", user, 1);

		/*
		 * However, we *don't* honor PGDATABASE, since we certainly don't wish
		 * to connect to whatever database the user might like as default.
		 * (Most tests override PGDATABASE anyway, but there are some ECPG
		 * test cases that don't.)
		 */
		unsetenv("PGDATABASE");

		/*
		 * Report what we're connecting to
		 */
		pghost = getenv("PGHOST");
		pgport = getenv("PGPORT");
		if (!pghost)
		{
			/* Keep this bit in sync with libpq's default host location: */
			if (DEFAULT_PGSOCKET_DIR[0])
				 /* do nothing, we'll print "Unix socket" below */ ;
			else
				pghost = "localhost";	/* DefaultHost in fe-connect.c */
		}

		if (pghost && pgport)
			note("using postmaster on %s, port %s", pghost, pgport);
		if (pghost && !pgport)
			note("using postmaster on %s, default port", pghost);
		if (!pghost && pgport)
			note("using postmaster on Unix socket, port %s", pgport);
		if (!pghost && !pgport)
			note("using postmaster on Unix socket, default port");
	}

	load_resultmap();
}

#ifdef ENABLE_SSPI

/* support for config_sspi_auth() */
static const char *
fmtHba(const char *raw)
{
	static char *ret;
	const char *rp;
	char	   *wp;

	wp = ret = pg_realloc(ret, 3 + strlen(raw) * 2);

	*wp++ = '"';
	for (rp = raw; *rp; rp++)
	{
		if (*rp == '"')
			*wp++ = '"';
		*wp++ = *rp;
	}
	*wp++ = '"';
	*wp++ = '\0';

	return ret;
}

/*
 * Get account and domain/realm names for the current user.  This is based on
 * pg_SSPI_recvauth().  The returned strings use static storage.
 */
static void
current_windows_user(const char **acct, const char **dom)
{
	static char accountname[MAXPGPATH];
	static char domainname[MAXPGPATH];
	HANDLE		token;
	TOKEN_USER *tokenuser;
	DWORD		retlen;
	DWORD		accountnamesize = sizeof(accountname);
	DWORD		domainnamesize = sizeof(domainname);
	SID_NAME_USE accountnameuse;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &token))
	{
		bail("could not open process token: error code %lu", GetLastError());
	}

	if (!GetTokenInformation(token, TokenUser, NULL, 0, &retlen) && GetLastError() != 122)
	{
		bail("could not get token information buffer size: error code %lu",
			 GetLastError());
	}
	tokenuser = pg_malloc(retlen);
	if (!GetTokenInformation(token, TokenUser, tokenuser, retlen, &retlen))
	{
		bail("could not get token information: error code %lu",
			 GetLastError());
	}

	if (!LookupAccountSid(NULL, tokenuser->User.Sid, accountname, &accountnamesize,
						  domainname, &domainnamesize, &accountnameuse))
	{
		bail("could not look up account SID: error code %lu",
			 GetLastError());
	}

	free(tokenuser);

	*acct = accountname;
	*dom = domainname;
}

/*
 * Rewrite pg_hba.conf and pg_ident.conf to use SSPI authentication.  Permit
 * the current OS user to authenticate as the bootstrap superuser and as any
 * user named in a --create-role option.
 *
 * In --config-auth mode, the --user switch can be used to specify the
 * bootstrap superuser's name, otherwise we assume it is the default.
 */
static void
config_sspi_auth(const char *pgdata, const char *superuser_name)
{
	const char *accountname,
			   *domainname;
	char	   *errstr;
	bool		have_ipv6;
	char		fname[MAXPGPATH];
	int			res;
	FILE	   *hba,
			   *ident;
	_stringlist *sl;

	/* Find out the name of the current OS user */
	current_windows_user(&accountname, &domainname);

	/* Determine the bootstrap superuser's name */
	if (superuser_name == NULL)
	{
		/*
		 * Compute the default superuser name the same way initdb does.
		 *
		 * It's possible that this result always matches "accountname", the
		 * value SSPI authentication discovers.  But the underlying system
		 * functions do not clearly guarantee that.
		 */
		superuser_name = get_user_name(&errstr);
		if (superuser_name == NULL)
		{
			bail("%s", errstr);
		}
	}

	/*
	 * Like initdb.c:setup_config(), determine whether the platform recognizes
	 * ::1 (IPv6 loopback) as a numeric host address string.
	 */
	{
		struct addrinfo *gai_result;
		struct addrinfo hints;
		WSADATA		wsaData;

		hints.ai_flags = AI_NUMERICHOST;
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = 0;
		hints.ai_protocol = 0;
		hints.ai_addrlen = 0;
		hints.ai_canonname = NULL;
		hints.ai_addr = NULL;
		hints.ai_next = NULL;

		have_ipv6 = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0 &&
					 getaddrinfo("::1", NULL, &hints, &gai_result) == 0);
	}

	/* Check a Write outcome and report any error. */
#define CW(cond)	\
	do { \
		if (!(cond)) \
			bail("could not write to file \"%s\": %m", fname); \
	} while (0)

	res = snprintf(fname, sizeof(fname), "%s/pg_hba.conf", pgdata);
	if (res < 0 || res >= sizeof(fname))
	{
		/*
		 * Truncating this name is a fatal error, because we must not fail to
		 * overwrite an original trust-authentication pg_hba.conf.
		 */
		bail("directory name too long");
	}
	hba = fopen(fname, "w");
	if (hba == NULL)
	{
		bail("could not open file \"%s\" for writing: %m", fname);
	}
	CW(fputs("# Configuration written by config_sspi_auth()\n", hba) >= 0);
	CW(fputs("host all all 127.0.0.1/32  sspi include_realm=1 map=regress\n",
			 hba) >= 0);
	if (have_ipv6)
		CW(fputs("host all all ::1/128  sspi include_realm=1 map=regress\n",
				 hba) >= 0);
	CW(fclose(hba) == 0);

	snprintf(fname, sizeof(fname), "%s/pg_ident.conf", pgdata);
	ident = fopen(fname, "w");
	if (ident == NULL)
	{
		bail("could not open file \"%s\" for writing: %m", fname);
	}
	CW(fputs("# Configuration written by config_sspi_auth()\n", ident) >= 0);

	/*
	 * Double-quote for the benefit of account names containing whitespace or
	 * '#'.  Windows forbids the double-quote character itself, so don't
	 * bother escaping embedded double-quote characters.
	 */
	CW(fprintf(ident, "regress  \"%s@%s\"  %s\n",
			   accountname, domainname, fmtHba(superuser_name)) >= 0);
	for (sl = extraroles; sl; sl = sl->next)
		CW(fprintf(ident, "regress  \"%s@%s\"  %s\n",
				   accountname, domainname, fmtHba(sl->str)) >= 0);
	CW(fclose(ident) == 0);
}

#endif							/* ENABLE_SSPI */

/*
 * psql_start_command, psql_add_command, psql_end_command
 *
 * Issue one or more commands within one psql call.
 * Set up with psql_start_command, then add commands one at a time
 * with psql_add_command, and finally execute with psql_end_command.
 *
 * Since we use system(), this doesn't return until the operation finishes
 */
static StringInfo
psql_start_command(void)
{
	StringInfo	buf = makeStringInfo();

	appendStringInfo(buf,
					 "\"%s%spsql\" -X -q",
					 bindir ? bindir : "",
					 bindir ? "/" : "");
	return buf;
}

static void
psql_add_command(StringInfo buf, const char *query,...)
{
	StringInfoData cmdbuf;
	const char *cmdptr;

	/* Add each command as a -c argument in the psql call */
	appendStringInfoString(buf, " -c \"");

	/* Generate the query with insertion of sprintf arguments */
	initStringInfo(&cmdbuf);
	for (;;)
	{
		va_list		args;
		int			needed;

		va_start(args, query);
		needed = appendStringInfoVA(&cmdbuf, query, args);
		va_end(args);
		if (needed == 0)
			break;				/* success */
		enlargeStringInfo(&cmdbuf, needed);
	}

	/* Now escape any shell double-quote metacharacters */
	for (cmdptr = cmdbuf.data; *cmdptr; cmdptr++)
	{
		if (strchr("\\\"$`", *cmdptr))
			appendStringInfoChar(buf, '\\');
		appendStringInfoChar(buf, *cmdptr);
	}

	appendStringInfoChar(buf, '"');

	pfree(cmdbuf.data);
}

static void
psql_end_command(StringInfo buf, const char *database)
{
	/* Add the database name --- assume it needs no extra escaping */
	appendStringInfo(buf,
					 " \"%s\"",
					 database);

	/* And now we can execute the shell command */
	fflush(NULL);
	if (system(buf->data) != 0)
	{
		/* psql probably already reported the error */
		bail("command failed: %s", buf->data);
	}

	/* Clean up */
	destroyStringInfo(buf);
}

/*
 * Shorthand macro for the common case of a single command
 */
#define psql_command(database, ...) \
	do { \
		StringInfo cmdbuf = psql_start_command(); \
		psql_add_command(cmdbuf, __VA_ARGS__); \
		psql_end_command(cmdbuf, database); \
	} while (0)

/*
 * Spawn a process to execute the given shell command; don't wait for it
 *
 * Returns the process ID (or HANDLE) so we can wait for it later
 */
PID_TYPE
spawn_process(const char *cmdline)
{
#ifndef WIN32
	pid_t		pid;

	/*
	 * Must flush I/O buffers before fork.
	 */
	fflush(NULL);

#ifdef EXEC_BACKEND
	pg_disable_aslr();
#endif

	pid = fork();
	if (pid == -1)
	{
		bail("could not fork: %m");
	}
	if (pid == 0)
	{
		/*
		 * In child
		 *
		 * Instead of using system(), exec the shell directly, and tell it to
		 * "exec" the command too.  This saves two useless processes per
		 * parallel test case.
		 */
		char	   *cmdline2;

		cmdline2 = psprintf("exec %s", cmdline);
		execl(shellprog, shellprog, "-c", cmdline2, (char *) NULL);
		/* Not using the normal bail() here as we want _exit */
		bail_noatexit("could not exec \"%s\": %m", shellprog);
	}
	/* in parent */
	return pid;
#else
	PROCESS_INFORMATION pi;
	char	   *cmdline2;
	const char *comspec;

	/* Find CMD.EXE location using COMSPEC, if it's set */
	comspec = getenv("COMSPEC");
	if (comspec == NULL)
		comspec = "CMD";

	memset(&pi, 0, sizeof(pi));
	cmdline2 = psprintf("\"%s\" /c \"%s\"", comspec, cmdline);

	if (!CreateRestrictedProcess(cmdline2, &pi))
		exit(2);

	CloseHandle(pi.hThread);
	return pi.hProcess;
#endif
}

/*
 * Count bytes in file
 */
static long
file_size(const char *file)
{
	long		r;
	FILE	   *f = fopen(file, "r");

	if (!f)
	{
		diag("could not open file \"%s\" for reading: %m", file);
		return -1;
	}
	fseek(f, 0, SEEK_END);
	r = ftell(f);
	fclose(f);
	return r;
}

/*
 * Count lines in file
 */
static int
file_line_count(const char *file)
{
	int			c;
	int			l = 0;
	FILE	   *f = fopen(file, "r");

	if (!f)
	{
		diag("could not open file \"%s\" for reading: %m", file);
		return -1;
	}
	while ((c = fgetc(f)) != EOF)
	{
		if (c == '\n')
			l++;
	}
	fclose(f);
	return l;
}

bool
file_exists(const char *file)
{
	FILE	   *f = fopen(file, "r");

	if (!f)
		return false;
	fclose(f);
	return true;
}

static bool
directory_exists(const char *dir)
{
	struct stat st;

	if (stat(dir, &st) != 0)
		return false;
	if (S_ISDIR(st.st_mode))
		return true;
	return false;
}

/* Create a directory */
static void
make_directory(const char *dir)
{
	if (mkdir(dir, S_IRWXU | S_IRWXG | S_IRWXO) < 0)
		bail("could not create directory \"%s\": %m", dir);
}

/*
 * In: filename.ext, Return: filename_i.ext, where 0 < i <= 9
 */
static char *
get_alternative_expectfile(const char *expectfile, int i)
{
	char	   *last_dot;
	int			ssize = strlen(expectfile) + 2 + 1;
	char	   *tmp;
	char	   *s;

	if (!(tmp = (char *) malloc(ssize)))
		return NULL;

	if (!(s = (char *) malloc(ssize)))
	{
		free(tmp);
		return NULL;
	}

	strcpy(tmp, expectfile);
	last_dot = strrchr(tmp, '.');
	if (!last_dot)
	{
		free(tmp);
		free(s);
		return NULL;
	}
	*last_dot = '\0';
	snprintf(s, ssize, "%s_%d.%s", tmp, i, last_dot + 1);
	free(tmp);
	return s;
}

/*
 * Run a "diff" command and also check that it didn't crash
 */
static int
run_diff(const char *cmd, const char *filename)
{
	int			r;

	fflush(NULL);
	r = system(cmd);
	if (!WIFEXITED(r) || WEXITSTATUS(r) > 1)
	{
<<<<<<< HEAD
		fprintf(stderr, _("diff command failed with status %d: %s\n"), r, cmd);
		/* POLAR px: Continue test if file not exist*/
		/* exit(2);*/
=======
		bail("diff command failed with status %d: %s", r, cmd);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	}
#ifdef WIN32

	/*
	 * On WIN32, if the 'diff' command cannot be found, system() returns 1,
	 * but produces nothing to stdout, so we check for that here.
	 */
	if (WEXITSTATUS(r) == 1 && file_size(filename) <= 0)
	{
		bail("diff command not found: %s", cmd);
	}
#endif

	return WEXITSTATUS(r);
}

/*
 * Check the actual result file for the given test against expected results
 *
 * Returns true if different (failure), false if correct match found.
 * In the true case, the diff is appended to the diffs file.
 */
static bool
results_differ(const char *testname, const char *resultsfile, const char *default_expectfile)
{
	char		expectfile[MAXPGPATH];
	char		diff[MAXPGPATH];
	char		cmd[MAXPGPATH * 3];
	char		best_expect_file[MAXPGPATH];
	FILE	   *difffile;
	int			best_line_count;
	int			i;
	int			l;
	const char *platform_expectfile;

	/*
	 * We can pass either the resultsfile or the expectfile, they should have
	 * the same type (filename.type) anyway.
	 */
	platform_expectfile = get_expectfile(testname, resultsfile);

	strlcpy(expectfile, default_expectfile, sizeof(expectfile));
	if (platform_expectfile)
	{
		/*
		 * Replace everything after the last slash in expectfile with what the
		 * platform_expectfile contains.
		 */
		char	   *p = strrchr(expectfile, '/');

		if (p)
			strcpy(++p, platform_expectfile);
	}

	/* Name to use for temporary diff file */
	snprintf(diff, sizeof(diff), "%s.diff", resultsfile);

	/* OK, run the diff */
	snprintf(cmd, sizeof(cmd),
			 "diff %s \"%s\" \"%s\" > \"%s\"",
			 basic_diff_opts, expectfile, resultsfile, diff);

	/* Is the diff file empty? */
	if (run_diff(cmd, diff) == 0)
	{
		unlink(diff);
		return false;
	}

	/* There may be secondary comparison files that match better */
	best_line_count = file_line_count(diff);
	strcpy(best_expect_file, expectfile);

	for (i = 0; i <= 9; i++)
	{
		char	   *alt_expectfile;

		alt_expectfile = get_alternative_expectfile(expectfile, i);
		if (!alt_expectfile)
			bail("Unable to check secondary comparison files: %m");

		if (!file_exists(alt_expectfile))
		{
			free(alt_expectfile);
			continue;
		}

		snprintf(cmd, sizeof(cmd),
				 "diff %s \"%s\" \"%s\" > \"%s\"",
				 basic_diff_opts, alt_expectfile, resultsfile, diff);

		if (run_diff(cmd, diff) == 0)
		{
			unlink(diff);
			free(alt_expectfile);
			return false;
		}

		l = file_line_count(diff);
		if (l < best_line_count)
		{
			/* This diff was a better match than the last one */
			best_line_count = l;
			strlcpy(best_expect_file, alt_expectfile, sizeof(best_expect_file));
		}
		free(alt_expectfile);
	}

	/*
	 * fall back on the canonical results file if we haven't tried it yet and
	 * haven't found a complete match yet.
	 */

	if (platform_expectfile)
	{
		snprintf(cmd, sizeof(cmd),
				 "diff %s \"%s\" \"%s\" > \"%s\"",
				 basic_diff_opts, default_expectfile, resultsfile, diff);

		if (run_diff(cmd, diff) == 0)
		{
			/* No diff = no changes = good */
			unlink(diff);
			return false;
		}

		l = file_line_count(diff);
		if (l < best_line_count)
		{
			/* This diff was a better match than the last one */
			best_line_count = l;
			strlcpy(best_expect_file, default_expectfile, sizeof(best_expect_file));
		}
	}

	/*
	 * Use the best comparison file to generate the "pretty" diff, which we
	 * append to the diffs summary file.
	 */

	/* Write diff header */
	difffile = fopen(difffilename, "a");
	if (difffile)
	{
		fprintf(difffile,
				"diff %s %s %s\n",
				pretty_diff_opts, best_expect_file, resultsfile);
		fclose(difffile);
	}

	/* Run diff */
	snprintf(cmd, sizeof(cmd),
			 "diff %s \"%s\" \"%s\" >> \"%s\"",
			 pretty_diff_opts, best_expect_file, resultsfile, difffilename);
	run_diff(cmd, difffilename);

	unlink(diff);
	return true;
}

/*
 * Wait for specified subprocesses to finish, and return their exit
 * statuses into statuses[] and stop times into stoptimes[]
 *
 * If names isn't NULL, print each subprocess's name as it finishes
 *
 * Note: it's OK to scribble on the pids array, but not on the names array
 */
static void
<<<<<<< HEAD
wait_for_tests(PID_TYPE * pids, int *statuses, char **names, struct timeval *endtimes, int num_tests)
=======
wait_for_tests(PID_TYPE * pids, int *statuses, instr_time *stoptimes,
			   char **names, int num_tests)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
{
	int			tests_left;
	int			i;

#ifdef WIN32
	PID_TYPE   *active_pids = pg_malloc(num_tests * sizeof(PID_TYPE));

	memcpy(active_pids, pids, num_tests * sizeof(PID_TYPE));
#endif

	tests_left = num_tests;
	while (tests_left > 0)
	{
		PID_TYPE	p;
		struct timeval endtime;

#ifndef WIN32
		int			exit_status;

		p = wait(&exit_status);

		if (p == INVALID_PID)
			bail("failed to wait for subprocesses: %m");
#else
		DWORD		exit_status;
		int			r;

		r = WaitForMultipleObjects(tests_left, active_pids, FALSE, INFINITE);
		if (r < WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + tests_left)
		{
			bail("failed to wait for subprocesses: error code %lu",
				 GetLastError());
		}
		p = active_pids[r - WAIT_OBJECT_0];
		/* compact the active_pids array */
		active_pids[r - WAIT_OBJECT_0] = active_pids[tests_left - 1];
#endif							/* WIN32 */

		gettimeofday(&endtime, NULL);

		for (i = 0; i < num_tests; i++)
		{
			if (p == pids[i])
			{
#ifdef WIN32
				GetExitCodeProcess(pids[i], &exit_status);
				CloseHandle(pids[i]);
#endif
				pids[i] = INVALID_PID;
				statuses[i] = (int) exit_status;
<<<<<<< HEAD
				if (endtimes)
					endtimes[i] = endtime;
=======
				INSTR_TIME_SET_CURRENT(stoptimes[i]);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				if (names)
					note_detail(" %s", names[i]);
				tests_left--;
				break;
			}
		}
	}

#ifdef WIN32
	free(active_pids);
#endif
}

/*
 * report nonzero exit code from a test process
 */
static void
log_child_failure(int exitstatus)
{
	if (WIFEXITED(exitstatus))
		diag("(test process exited with exit code %d)",
			 WEXITSTATUS(exitstatus));
	else if (WIFSIGNALED(exitstatus))
	{
#if defined(WIN32)
<<<<<<< HEAD
		status(_(" (test process was terminated by exception 0x%X)"),
			   WTERMSIG(exitstatus));
#else
		status(_(" (test process was terminated by signal %d: %s)"),
			   WTERMSIG(exitstatus), pg_strsignal(WTERMSIG(exitstatus)));
=======
		diag("(test process was terminated by exception 0x%X)",
			 WTERMSIG(exitstatus));
#else
		diag("(test process was terminated by signal %d: %s)",
			 WTERMSIG(exitstatus), pg_strsignal(WTERMSIG(exitstatus)));
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#endif
	}
	else
		diag("(test process exited with unrecognized status %d)", exitstatus);
}

/*
 * Run all the tests specified in one schedule file
 */
static void
run_schedule(const char *schedule, test_start_function startfunc,
			 postprocess_result_function postfunc)
{
#define MAX_PARALLEL_TESTS 100
	char	   *tests[MAX_PARALLEL_TESTS];
	_stringlist *resultfiles[MAX_PARALLEL_TESTS];
	_stringlist *expectfiles[MAX_PARALLEL_TESTS];
	_stringlist *tags[MAX_PARALLEL_TESTS];
	PID_TYPE	pids[MAX_PARALLEL_TESTS];
	instr_time	starttimes[MAX_PARALLEL_TESTS];
	instr_time	stoptimes[MAX_PARALLEL_TESTS];
	int			statuses[MAX_PARALLEL_TESTS];
<<<<<<< HEAD
	struct timeval endtimes[MAX_PARALLEL_TESTS];
	_stringlist *ignorelist = NULL;

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	char		scbuf[1024];
	FILE	   *scf;
	int			line_num = 0;

	/* POLAR */
	_stringlist *polar_ignorelist = NULL;

	memset(tests, 0, sizeof(tests));
	memset(resultfiles, 0, sizeof(resultfiles));
	memset(expectfiles, 0, sizeof(expectfiles));
	memset(tags, 0, sizeof(tags));

	scf = fopen(schedule, "r");
	if (!scf)
		bail("could not open file \"%s\" for reading: %m", schedule);

	while (fgets(scbuf, sizeof(scbuf), scf))
	{
		char	   *test = NULL;
		char	   *c;
		int			num_tests;
		bool		inword;
		int			i;
		struct timeval starttime;

		line_num++;

		/* strip trailing whitespace, especially the newline */
		i = strlen(scbuf);
		while (i > 0 && isspace((unsigned char) scbuf[i - 1]))
			scbuf[--i] = '\0';

		if (scbuf[0] == '\0' || scbuf[0] == '#')
			continue;
		if (strncmp(scbuf, "test: ", 6) == 0)
			test = scbuf + 6;
<<<<<<< HEAD
		else if (strncmp(scbuf, "ignore: ", 8) == 0)
		{
			c = scbuf + 8;
			while (*c && isspace((unsigned char) *c))
				c++;
			add_stringlist_item(&ignorelist, c);

			/* POLAR */
			ignore_count ++;

			/*
			 * Note: ignore: lines do not run the test, they just say that
			 * failure of this test when run later on is to be ignored. A bit
			 * odd but that's how the shell-script version did it.
			 */
			continue;
		}
		else if (strncmp(scbuf, "polar_ignore: ", 14) == 0)
		{
			/*
			 * polar_ignore: some cases is ignored in master-slave sync
			 * cases will run but will not output to the regression.diff
			 * usage: the same with "ignore"
			 * 1. polar_ignore: case
			 *    test: case
			 * run the case but no output in regression.diff
			 * 2. polar_ignore: case
			 * not run the case
			 */
			c = scbuf + 14;
			while (*c && isspace((unsigned char) *c))
				c++;
			add_stringlist_item(&polar_ignorelist, c);
			if (polar_is_enable)
				polar_ignore_count ++;
			continue;
		}
		else if (strncmp(scbuf, "polar_px_ignore: ", 17) == 0)
		{
			if (polar_enable_parallel_execution)
			{
				c = scbuf + 17;
				while (*c && isspace((unsigned char) *c))
					c++;
				add_stringlist_item(&polar_ignorelist, c);
				polar_ignore_count ++;
			}
			continue;
		}
		else if (strncmp(scbuf, "polar_ss_ignore: ", 17) == 0)
		{
			if (polar_enable_shared_server_mode)
			{
				c = scbuf + 17;
				while (*c && isspace((unsigned char) *c))
					c++;
				add_stringlist_item(&polar_ignorelist, c);
				polar_ignore_count ++;
			}
			continue;
		}
		else if (strncmp(scbuf, "cluster_ignore: ", 16) == 0)
		{
			if (dma_cluster)
			{
				c = scbuf + 16;
				while (*c && isspace((unsigned char) *c))
					c++;
				add_stringlist_item(&ignorelist, c);
				ignore_count ++;
				continue;
			}
			else
			{
				test = scbuf + 16;
			}
		}
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		else
		{
			bail("syntax error in schedule file \"%s\" line %d: %s",
				 schedule, line_num, scbuf);
		}

		num_tests = 0;
		inword = false;
		for (c = test;; c++)
		{
			if (*c == '\0' || isspace((unsigned char) *c))
			{
				if (inword)
				{
					/* Reached end of a test name */
					char		sav;

					if (num_tests >= MAX_PARALLEL_TESTS)
					{
						bail("too many parallel tests (more than %d) in schedule file \"%s\" line %d: %s",
							 MAX_PARALLEL_TESTS, schedule, line_num, scbuf);
					}
					sav = *c;
					*c = '\0';
					tests[num_tests] = pg_strdup(test);
					num_tests++;
					*c = sav;
					inword = false;
				}
				if (*c == '\0')
					break;		/* loop exit is here */
			}
			else if (!inword)
			{
				/* Start of a test name */
				test = c;
				inword = true;
			}
		}

		if (num_tests == 0)
		{
			bail("syntax error in schedule file \"%s\" line %d: %s",
				 schedule, line_num, scbuf);
		}

		gettimeofday(&starttime, NULL);

		if (num_tests == 1)
		{
<<<<<<< HEAD
			status(_("test %-28s ... "), tests[0]);
			pids[0] = (tfunc) (tests[0], &resultfiles[0], &expectfiles[0], &tags[0]);
			wait_for_tests(pids, statuses, NULL, endtimes, 1);
=======
			pids[0] = (startfunc) (tests[0], &resultfiles[0], &expectfiles[0], &tags[0]);
			INSTR_TIME_SET_CURRENT(starttimes[0]);
			wait_for_tests(pids, statuses, stoptimes, NULL, 1);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			/* status line is finished below */
		}
		else if (max_concurrent_tests > 0 && max_concurrent_tests < num_tests)
		{
			bail("too many parallel tests (more than %d) in schedule file \"%s\" line %d: %s",
				 max_concurrent_tests, schedule, line_num, scbuf);
		}
		else if (max_connections > 0 && max_connections < num_tests)
		{
			int			oldest = 0;

			note_detail("parallel group (%d tests, in groups of %d): ",
						num_tests, max_connections);
			for (i = 0; i < num_tests; i++)
			{
				if (i - oldest >= max_connections)
				{
					wait_for_tests(pids + oldest, statuses + oldest,
<<<<<<< HEAD
								   tests + oldest, endtimes + oldest,
								   i - oldest);
=======
								   stoptimes + oldest,
								   tests + oldest, i - oldest);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
					oldest = i;
				}
				pids[i] = (startfunc) (tests[i], &resultfiles[i], &expectfiles[i], &tags[i]);
				INSTR_TIME_SET_CURRENT(starttimes[i]);
			}
			wait_for_tests(pids + oldest, statuses + oldest,
<<<<<<< HEAD
						   tests + oldest, endtimes + oldest,
						   i - oldest);
			status_end();
=======
						   stoptimes + oldest,
						   tests + oldest, i - oldest);
			note_end();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}
		else
		{
			note_detail("parallel group (%d tests): ", num_tests);
			for (i = 0; i < num_tests; i++)
			{
				pids[i] = (startfunc) (tests[i], &resultfiles[i], &expectfiles[i], &tags[i]);
				INSTR_TIME_SET_CURRENT(starttimes[i]);
			}
<<<<<<< HEAD
			wait_for_tests(pids, statuses, tests, endtimes, num_tests);
			status_end();
=======
			wait_for_tests(pids, statuses, stoptimes, tests, num_tests);
			note_end();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		}

		/* Check results for all tests */
		for (i = 0; i < num_tests; i++)
		{
			_stringlist *rl,
					   *el,
					   *tl;
			bool		differ = false;

<<<<<<< HEAD
			/* POLAR */
			bool polar_ignore = false;
			/* POLAR end */

			if (num_tests > 1)
				status(_("     %-28s ... "), tests[i]);
=======
			INSTR_TIME_SUBTRACT(stoptimes[i], starttimes[i]);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

			/*
			 * Advance over all three lists simultaneously.
			 *
			 * Compare resultfiles[j] with expectfiles[j] always. Tags are
			 * optional but if there are tags, the tag list has the same
			 * length as the other two lists.
			 */
			for (rl = resultfiles[i], el = expectfiles[i], tl = tags[i];
				 rl != NULL;	/* rl and el have the same length */
				 rl = rl->next, el = el->next,
				 tl = tl ? tl->next : NULL)
			{
				bool		newdiff;

<<<<<<< HEAD
				/* POLAR */
				char		polar_replicaoutfile[MAXPGPATH];
				char		polar_replicaexpectfile[MAXPGPATH];

				/* POLAR */
				_stringlist *sl;
				if (polar_is_enable || dma_cluster ||
					polar_enable_parallel_execution || polar_enable_shared_server_mode)
				{
					for (sl = polar_ignorelist; sl != NULL; sl = sl->next)
					{
						if (strcmp(tests[i], sl->str) == 0)
						{
							polar_ignore = true;
							break;
						}
					}
				}
				if (polar_ignore)
					continue;
				/* POLAR end */
				
=======
				if (postfunc)
					(*postfunc) (rl->str);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				newdiff = results_differ(tests[i], rl->str, el->str);
				if (newdiff && tl)
				{
					diag("tag: %s", tl->str);
				}
				differ |= newdiff;

				/* POLAR: check replica out diff */
				snprintf(polar_replicaoutfile, sizeof(polar_replicaoutfile), "%s.replica",
						 rl->str);
				snprintf(polar_replicaexpectfile, sizeof(polar_replicaexpectfile), "%s.replica",
						 el->str);
				/* we need check replica out file */
				if (getenv("PGREPLICAHOST") && getenv("PGREPLICAPORT") && file_exists(polar_replicaexpectfile))
				{
					newdiff = results_differ(tests[i], polar_replicaoutfile, polar_replicaexpectfile);
					if (newdiff && tl)
					{
						printf("%s.replica ", tl->str);
					}
					differ |= newdiff;
				}
				/* POLAR end */
			}

			if (statuses[i] != 0)
			{
				test_status_failed(tests[i], INSTR_TIME_GET_MILLISEC(stoptimes[i]), (num_tests > 1));
				log_child_failure(statuses[i]);
			}
			else
			{
<<<<<<< HEAD
				/* POLAR */
				if (polar_ignore)
				{
					status(_("ok (polar ignored)"));
				}
				else
				{
					status(_("ok"));
					success_count++;
				}
				/* POLAR end */
			}

			/* Calculate time spent in test */
			{
				int diff_sec;
				int diff_usec;

				diff_sec = (int) (endtimes[i].tv_sec - starttime.tv_sec);
				if (endtimes[i].tv_usec < starttime.tv_usec)
				{
					endtimes[i].tv_sec--;
					endtimes[i].tv_usec += 1000000;
				}
				diff_sec = (int) (endtimes[i].tv_sec - starttime.tv_sec);
				diff_usec = (int) (endtimes[i].tv_usec - starttime.tv_usec);

				status(" (%d.%02d s)", diff_sec, diff_usec / 10000);
=======
				if (differ)
				{
					test_status_failed(tests[i], INSTR_TIME_GET_MILLISEC(stoptimes[i]), (num_tests > 1));
				}
				else
				{
					test_status_ok(tests[i], INSTR_TIME_GET_MILLISEC(stoptimes[i]), (num_tests > 1));
				}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			}
		}

		for (i = 0; i < num_tests; i++)
		{
			pg_free(tests[i]);
			tests[i] = NULL;
			free_stringlist(&resultfiles[i]);
			free_stringlist(&expectfiles[i]);
			free_stringlist(&tags[i]);
		}
	}

<<<<<<< HEAD
	free_stringlist(&ignorelist);
	free_stringlist(&polar_ignorelist);

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	fclose(scf);
}

/*
 * Run a single test
 */
static void
run_single_test(const char *test, test_start_function startfunc,
				postprocess_result_function postfunc)
{
	PID_TYPE	pid;
	instr_time	starttime;
	instr_time	stoptime;
	int			exit_status;
	_stringlist *resultfiles = NULL;
	_stringlist *expectfiles = NULL;
	_stringlist *tags = NULL;
	_stringlist *rl,
			   *el,
			   *tl;
	bool		differ = false;

<<<<<<< HEAD
	status(_("test %-28s ... "), test);
	pid = (tfunc) (test, &resultfiles, &expectfiles, &tags);
	wait_for_tests(&pid, &exit_status, NULL, NULL, 1);
=======
	pid = (startfunc) (test, &resultfiles, &expectfiles, &tags);
	INSTR_TIME_SET_CURRENT(starttime);
	wait_for_tests(&pid, &exit_status, &stoptime, NULL, 1);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	/*
	 * Advance over all three lists simultaneously.
	 *
	 * Compare resultfiles[j] with expectfiles[j] always. Tags are optional
	 * but if there are tags, the tag list has the same length as the other
	 * two lists.
	 */
	for (rl = resultfiles, el = expectfiles, tl = tags;
		 rl != NULL;			/* rl and el have the same length */
		 rl = rl->next, el = el->next,
		 tl = tl ? tl->next : NULL)
	{
		bool		newdiff;

<<<<<<< HEAD
		/* POLAR */
		char		polar_replicaoutfile[MAXPGPATH];
		char		polar_replicaexpectfile[MAXPGPATH];

=======
		if (postfunc)
			(*postfunc) (rl->str);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		newdiff = results_differ(test, rl->str, el->str);
		if (newdiff && tl)
		{
			diag("tag: %s", tl->str);
		}
		differ |= newdiff;

		/* POLAR: check replica out diff */
		snprintf(polar_replicaoutfile, sizeof(polar_replicaoutfile), "%s.replica",
				 rl->str);
		snprintf(polar_replicaexpectfile, sizeof(polar_replicaexpectfile), "%s.replica",
				 el->str);
		/* we need check replica out file */
		if (getenv("PGREPLICAHOST") && getenv("PGREPLICAPORT") && file_exists(polar_replicaexpectfile))
		{
			newdiff = results_differ(test, polar_replicaoutfile, polar_replicaexpectfile);
			if (newdiff && tl)
			{
				printf("%s.replica ", tl->str);
			}
			differ |= newdiff;
		}
		/* POLAR end */
	}

	INSTR_TIME_SUBTRACT(stoptime, starttime);

	if (exit_status != 0)
	{
		test_status_failed(test, INSTR_TIME_GET_MILLISEC(stoptime), false);
		log_child_failure(exit_status);
	}
	else
	{
		if (differ)
		{
			test_status_failed(test, INSTR_TIME_GET_MILLISEC(stoptime), false);
		}
		else
		{
			test_status_ok(test, INSTR_TIME_GET_MILLISEC(stoptime), false);
		}
	}
}

/*
 * Create the summary-output files (making them empty if already existing)
 */
static void
open_result_files(void)
{
	char		file[MAXPGPATH];
	FILE	   *difffile;

	/* create outputdir directory if not present */
	if (!directory_exists(outputdir))
		make_directory(outputdir);

	/* create the log file (copy of running status output) */
	snprintf(file, sizeof(file), "%s/regression.out", outputdir);
	logfilename = pg_strdup(file);
	logfile = fopen(logfilename, "w");
	if (!logfile)
		bail("could not open file \"%s\" for writing: %m", logfilename);

	/* create the diffs file as empty */
	snprintf(file, sizeof(file), "%s/regression.diffs", outputdir);
	difffilename = pg_strdup(file);
	difffile = fopen(difffilename, "w");
	if (!difffile)
		bail("could not open file \"%s\" for writing: %m", difffilename);

	/* we don't keep the diffs file open continuously */
	fclose(difffile);

	/* also create the results directory if not present */
	snprintf(file, sizeof(file), "%s/results", outputdir);
	if (!directory_exists(file))
		make_directory(file);
}

static void
drop_database_if_exists(const char *dbname)
{
	StringInfo	buf = psql_start_command();

	/* Set warning level so we don't see chatter about nonexistent DB */
	psql_add_command(buf, "SET client_min_messages = warning");
	psql_add_command(buf, "DROP DATABASE IF EXISTS \"%s\"", dbname);
	psql_end_command(buf, "postgres");
}

static void
create_database(const char *dbname)
{
	StringInfo	buf = psql_start_command();
	_stringlist *sl;

	/*
	 * We use template0 so that any installation-local cruft in template1 will
	 * not mess up the tests.
	 */
	if (encoding)
		psql_add_command(buf, "CREATE DATABASE \"%s\" TEMPLATE=template0 ENCODING='%s'%s", dbname, encoding,
						 (nolocale) ? " LOCALE='C'" : "");
	else
		psql_add_command(buf, "CREATE DATABASE \"%s\" TEMPLATE=template0%s", dbname,
						 (nolocale) ? " LOCALE='C'" : "");
	psql_add_command(buf,
					 "ALTER DATABASE \"%s\" SET lc_messages TO 'C';"
					 "ALTER DATABASE \"%s\" SET lc_monetary TO 'C';"
					 "ALTER DATABASE \"%s\" SET lc_numeric TO 'C';"
					 "ALTER DATABASE \"%s\" SET lc_time TO 'C';"
					 "ALTER DATABASE \"%s\" SET bytea_output TO 'hex';"
					 "ALTER DATABASE \"%s\" SET timezone_abbreviations TO 'Default';",
					 dbname, dbname, dbname, dbname, dbname, dbname);
	psql_end_command(buf, "postgres");

	/*
	 * Install any requested extensions.  We use CREATE IF NOT EXISTS so that
	 * this will work whether or not the extension is preinstalled.
	 */
	for (sl = loadextension; sl != NULL; sl = sl->next)
		psql_command(dbname, "CREATE EXTENSION IF NOT EXISTS \"%s\"", sl->str);
}

static void
drop_role_if_exists(const char *rolename)
{
	StringInfo	buf = psql_start_command();

	/* Set warning level so we don't see chatter about nonexistent role */
	psql_add_command(buf, "SET client_min_messages = warning");
	psql_add_command(buf, "DROP ROLE IF EXISTS \"%s\"", rolename);
	psql_end_command(buf, "postgres");
}

static void
create_role(const char *rolename, const _stringlist *granted_dbs)
{
	StringInfo	buf = psql_start_command();

	psql_add_command(buf, "CREATE ROLE \"%s\" WITH LOGIN", rolename);
	for (; granted_dbs != NULL; granted_dbs = granted_dbs->next)
	{
		psql_add_command(buf, "GRANT ALL ON DATABASE \"%s\" TO \"%s\"",
						 granted_dbs->str, rolename);
	}
	psql_end_command(buf, "postgres");
}

static void
help(void)
{
	printf(_("PostgreSQL regression test driver\n"));
	printf(_("\n"));
	printf(_("Usage:\n  %s [OPTION]... [EXTRA-TEST]...\n"), progname);
	printf(_("\n"));
	printf(_("Options:\n"));
	printf(_("      --bindir=BINPATH          use BINPATH for programs that are run;\n"));
	printf(_("                                if empty, use PATH from the environment\n"));
	printf(_("      --config-auth=DATADIR     update authentication settings for DATADIR\n"));
	printf(_("      --create-role=ROLE        create the specified role before testing\n"));
	printf(_("      --dbname=DB               use database DB (default \"regression\")\n"));
	printf(_("      --debug                   turn on debug mode in programs that are run\n"));
	printf(_("      --dlpath=DIR              look for dynamic libraries in DIR\n"));
	printf(_("      --encoding=ENCODING       use ENCODING as the encoding\n"));
	printf(_("      --expecteddir=DIR         take expected files from DIR (default \".\")\n"));
	printf(_("  -h, --help                    show this help, then exit\n"));
	printf(_("      --inputdir=DIR            take input files from DIR (default \".\")\n"));
	printf(_("      --launcher=CMD            use CMD as launcher of psql\n"));
	printf(_("      --load-extension=EXT      load the named extension before running the\n"));
	printf(_("                                tests; can appear multiple times\n"));
	printf(_("      --max-connections=N       maximum number of concurrent connections\n"));
	printf(_("                                (default is 0, meaning unlimited)\n"));
	printf(_("      --max-concurrent-tests=N  maximum number of concurrent tests in schedule\n"));
	printf(_("                                (default is 0, meaning unlimited)\n"));
	printf(_("      --outputdir=DIR           place output files in DIR (default \".\")\n"));
	printf(_("      --schedule=FILE           use test ordering schedule from FILE\n"));
	printf(_("                                (can be used multiple times to concatenate)\n"));
	printf(_("      --temp-instance=DIR       create a temporary instance in DIR\n"));
	printf(_("      --use-existing            use an existing installation\n"));
	printf(_("  -V, --version                 output version information, then exit\n"));
	printf(_("\n"));
	printf(_("Options for \"temp-instance\" mode:\n"));
	printf(_("      --no-locale               use C locale\n"));
	printf(_("      --port=PORT               start postmaster on PORT\n"));
	printf(_("      --temp-config=FILE        append contents of FILE to temporary config\n"));
	printf(_("\n"));
	printf(_("Options for using an existing installation:\n"));
	printf(_("      --host=HOST               use postmaster running on HOST\n"));
	printf(_("      --port=PORT               use postmaster running at PORT\n"));
	printf(_("      --user=USER               connect as USER\n"));
	printf(_("\n"));
	printf(_("The exit status is 0 if all tests passed, 1 if some tests failed, and 2\n"));
	printf(_("if the tests could not be run for some reason.\n"));
	printf(_("\n"));
	printf(_("Report bugs to <%s>.\n"), PACKAGE_BUGREPORT);
	printf(_("%s home page: <%s>\n"), PACKAGE_NAME, PACKAGE_URL);
}

int
regression_main(int argc, char *argv[],
				init_function ifunc,
				test_start_function startfunc,
				postprocess_result_function postfunc)
{
	static struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{"dbname", required_argument, NULL, 1},
		{"debug", no_argument, NULL, 2},
		{"inputdir", required_argument, NULL, 3},
		{"max-connections", required_argument, NULL, 5},
		{"encoding", required_argument, NULL, 6},
		{"outputdir", required_argument, NULL, 7},
		{"schedule", required_argument, NULL, 8},
		{"temp-instance", required_argument, NULL, 9},
		{"no-locale", no_argument, NULL, 10},
		{"host", required_argument, NULL, 13},
		{"port", required_argument, NULL, 14},
		{"user", required_argument, NULL, 15},
		{"bindir", required_argument, NULL, 16},
		{"dlpath", required_argument, NULL, 17},
		{"create-role", required_argument, NULL, 18},
		{"temp-config", required_argument, NULL, 19},
		{"use-existing", no_argument, NULL, 20},
		{"launcher", required_argument, NULL, 21},
		{"load-extension", required_argument, NULL, 22},
		{"config-auth", required_argument, NULL, 24},
		{"max-concurrent-tests", required_argument, NULL, 25},
<<<<<<< HEAD
		{"polar-parallel-execution", no_argument, NULL, 26},
		{"polar-shared-server", no_argument, NULL, 27},
		{"dma", required_argument, NULL, 2000},
		{"dma-election-timeout", required_argument, NULL, 2001},
=======
		{"expecteddir", required_argument, NULL, 26},
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		{NULL, 0, NULL, 0}
	};

	bool		use_unix_sockets;
	_stringlist *sl;
	int			c;
	int			i;
	int			option_index;
	char		buf[MAXPGPATH * 4];
<<<<<<< HEAD
	char		buf2[MAXPGPATH * 4];
	/* POLAR */
	char    polar_buf[MAXPGPATH * 4];
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	pg_logging_init(argv[0]);
	progname = get_progname(argv[0]);
	set_pglocale_pgservice(argv[0], PG_TEXTDOMAIN("pg_regress"));

<<<<<<< HEAD
	get_restricted_token(progname);
=======
	get_restricted_token();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	atexit(stop_postmaster);

#if defined(WIN32)

	/*
	 * We don't use Unix-domain sockets on Windows by default (see comment at
	 * remove_temp() for a reason).  Override at your own risk.
	 */
	use_unix_sockets = getenv("PG_TEST_USE_UNIX_SOCKETS") ? true : false;
#else
	use_unix_sockets = true;
#endif

	if (!use_unix_sockets)
		hostname = "localhost";

	/*
	 * We call the initialization function here because that way we can set
	 * default parameters and let them be overwritten by the commandline.
	 */
	ifunc(argc, argv);

	if (getenv("PG_REGRESS_DIFF_OPTS"))
		pretty_diff_opts = getenv("PG_REGRESS_DIFF_OPTS");

	while ((c = getopt_long(argc, argv, "hV", long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'h':
				help();
				exit(0);
			case 'V':
				puts("pg_regress (PostgreSQL) " PG_VERSION);
				exit(0);
			case 1:

				/*
				 * If a default database was specified, we need to remove it
				 * before we add the specified one.
				 */
				free_stringlist(&dblist);
				split_to_stringlist(optarg, ",", &dblist);
				break;
			case 2:
				debug = true;
				break;
			case 3:
				inputdir = pg_strdup(optarg);
				break;
			case 5:
				max_connections = atoi(optarg);
				break;
			case 6:
				encoding = pg_strdup(optarg);
				break;
			case 7:
				outputdir = pg_strdup(optarg);
				break;
			case 8:
				add_stringlist_item(&schedulelist, optarg);
				break;
			case 9:
				temp_instance = make_absolute_path(optarg);
				/* POLAR */
				polar_is_enable = false;
				/* POLAR end*/
				break;
			case 10:
				nolocale = true;
				break;
			case 13:
				hostname = pg_strdup(optarg);
				break;
			case 14:
				port = atoi(optarg);
				port_specified_by_user = true;
				break;
			case 15:
				user = pg_strdup(optarg);
				break;
			case 16:
				/* "--bindir=" means to use PATH */
				if (strlen(optarg))
					bindir = pg_strdup(optarg);
				else
					bindir = NULL;
				break;
			case 17:
				dlpath = pg_strdup(optarg);
				break;
			case 18:
				split_to_stringlist(optarg, ",", &extraroles);
				break;
			case 19:
				add_stringlist_item(&temp_configs, optarg);
				break;
			case 20:
				use_existing = true;
				break;
			case 21:
				launcher = pg_strdup(optarg);
				break;
			case 22:
				add_stringlist_item(&loadextension, optarg);
				break;
			case 24:
				config_auth_datadir = pg_strdup(optarg);
				break;
			case 25:
				max_concurrent_tests = atoi(optarg);
				break;
			case 26:
<<<<<<< HEAD
				polar_enable_parallel_execution = true;
				break;
			case 27:
				polar_enable_shared_server_mode = true;
				break;
			case 2000:
				dma_test = true;
				if (strcmp(optarg, "cluster") == 0 || strcmp(optarg, "CLUSTER") == 0)
					dma_cluster = true;
				break;
			case 2001:
				dma_election_timeout = atoi(optarg);
=======
				expecteddir = pg_strdup(optarg);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				break;
			default:
				/* getopt_long already emitted a complaint */
				pg_log_error_hint("Try \"%s --help\" for more information.",
								  progname);
				exit(2);
		}
	}

	/*
	 * if we still have arguments, they are extra tests to run
	 */
	while (argc - optind >= 1)
	{
		add_stringlist_item(&extra_tests, argv[optind]);
		optind++;
	}

	/*
	 * We must have a database to run the tests in; either a default name, or
	 * one supplied by the --dbname switch.
	 */
	if (!(dblist && dblist->str && dblist->str[0]))
	{
		bail("no database name was specified");
	}

	if (config_auth_datadir)
	{
#ifdef ENABLE_SSPI
		if (!use_unix_sockets)
			config_sspi_auth(config_auth_datadir, user);
#endif
		exit(0);
	}

	if (temp_instance && !port_specified_by_user)

		/*
		 * To reduce chances of interference with parallel installations, use
		 * a port number starting in the private range (49152-65535)
		 * calculated from the version number.  This aids non-Unix socket mode
		 * systems; elsewhere, the use of a private socket directory already
		 * prevents interference.
		 */
		port = (0xC000 | (PG_VERSION_NUM & 0x3FFF)) - (dma_test ? 10000 : 0);

	inputdir = make_absolute_path(inputdir);
	outputdir = make_absolute_path(outputdir);
	expecteddir = make_absolute_path(expecteddir);
	dlpath = make_absolute_path(dlpath);

	/*
	 * Initialization
	 */
	open_result_files();

	initialize_environment();

#if defined(HAVE_GETRLIMIT)
	unlimit_core_size();
#endif

	if (dma_test && temp_instance)
	{
		start_postmaster_cluster();
	}
	else if (temp_instance)
	{
		StringInfoData cmd;
		FILE	   *pg_conf;
		const char *env_wait;
		int			wait_seconds;
		const char *initdb_template_dir;
		const char *keywords[4];
		const char *values[4];
		PGPing		rv;
		const char *initdb_extra_opts_env;

		/*
		 * Prepare the temp instance
		 */

		if (directory_exists(temp_instance))
		{
			if (!rmtree(temp_instance, true))
			{
				bail("could not remove temp instance \"%s\"", temp_instance);
			}
		}

		/* make the temp instance top directory */
		make_directory(temp_instance);

		/* and a directory for log files */
		snprintf(buf, sizeof(buf), "%s/log", outputdir);
		if (!directory_exists(buf))
			make_directory(buf);

		initdb_extra_opts_env = getenv("PG_TEST_INITDB_EXTRA_OPTS");

		initStringInfo(&cmd);

		/*
		 * Create data directory.
		 *
		 * If available, use a previously initdb'd cluster as a template by
		 * copying it. For a lot of tests, that's substantially cheaper.
		 *
		 * There's very similar code in Cluster.pm, but we can't easily de
		 * duplicate it until we require perl at build time.
		 */
		initdb_template_dir = getenv("INITDB_TEMPLATE");
		if (initdb_template_dir == NULL || nolocale || debug || initdb_extra_opts_env)
		{
			note("initializing database system by running initdb");

			appendStringInfo(&cmd,
							 "\"%s%sinitdb\" -D \"%s/data\" --no-clean --no-sync",
							 bindir ? bindir : "",
							 bindir ? "/" : "",
							 temp_instance);
			if (debug)
				appendStringInfoString(&cmd, " --debug");
			if (nolocale)
				appendStringInfoString(&cmd, " --no-locale");
			if (initdb_extra_opts_env)
				appendStringInfo(&cmd, " %s", initdb_extra_opts_env);
			appendStringInfo(&cmd, " > \"%s/log/initdb.log\" 2>&1", outputdir);
			fflush(NULL);
			if (system(cmd.data))
			{
				bail("initdb failed\n"
					 "# Examine \"%s/log/initdb.log\" for the reason.\n"
					 "# Command was: %s",
					 outputdir, cmd.data);
			}
		}
		else
		{
#ifndef WIN32
			const char *copycmd = "cp -RPp \"%s\" \"%s/data\"";
			int			expected_exitcode = 0;
#else
			const char *copycmd = "robocopy /E /NJS /NJH /NFL /NDL /NP \"%s\" \"%s/data\"";
			int			expected_exitcode = 1;	/* 1 denotes files were copied */
#endif

			note("initializing database system by copying initdb template");

			appendStringInfo(&cmd,
							 copycmd,
							 initdb_template_dir,
							 temp_instance);
			appendStringInfo(&cmd, " > \"%s/log/initdb.log\" 2>&1", outputdir);
			fflush(NULL);
			if (system(cmd.data) != expected_exitcode)
			{
				bail("copying of initdb template failed\n"
					 "# Examine \"%s/log/initdb.log\" for the reason.\n"
					 "# Command was: %s",
					 outputdir, cmd.data);
			}
		}

		pfree(cmd.data);

		/*
		 * Adjust the default postgresql.conf for regression testing. The user
		 * can specify a file to be appended; in any case we expand logging
		 * and set max_prepared_transactions to enable testing of prepared
		 * xacts.  (Note: to reduce the probability of unexpected shmmax
		 * failures, don't set max_prepared_transactions any higher than
		 * actually needed by the prepared_xacts regression test.)
		 */
		snprintf(buf, sizeof(buf), "%s/data/postgresql.conf", temp_instance);
		pg_conf = fopen(buf, "a");
		if (pg_conf == NULL)
			bail("could not open \"%s\" for adding extra config: %m", buf);

		fputs("\n# Configuration added by pg_regress\n\n", pg_conf);
		fputs("log_autovacuum_min_duration = 0\n", pg_conf);
		fputs("log_checkpoints = on\n", pg_conf);
<<<<<<< HEAD
		fputs("log_line_prefix = '%m [%p] [%P] %q%a '\n", pg_conf);
=======
		fputs("log_line_prefix = '%m %b[%p] %q%a '\n", pg_conf);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		fputs("log_lock_waits = on\n", pg_conf);
		fputs("log_temp_files = 128kB\n", pg_conf);
		fputs("max_prepared_transactions = 2\n", pg_conf);

		for (sl = temp_configs; sl != NULL; sl = sl->next)
		{
			char	   *temp_config = sl->str;
			FILE	   *extra_conf;
			char		line_buf[1024];

			extra_conf = fopen(temp_config, "r");
			if (extra_conf == NULL)
			{
				bail("could not open \"%s\" to read extra config: %m",
					 temp_config);
			}
			while (fgets(line_buf, sizeof(line_buf), extra_conf) != NULL)
				fputs(line_buf, pg_conf);
			fclose(extra_conf);
		}

		fclose(pg_conf);

#ifdef ENABLE_SSPI
		if (!use_unix_sockets)
		{
			/*
			 * Since we successfully used the same buffer for the much-longer
			 * "initdb" command, this can't truncate.
			 */
			snprintf(buf, sizeof(buf), "%s/data", temp_instance);
			config_sspi_auth(buf, NULL);
		}
#endif

		/*
		 * Prepare the connection params for checking the state of the server
		 * before starting the tests.
		 */
		sprintf(portstr, "%d", port);
		keywords[0] = "dbname";
		values[0] = "postgres";
		keywords[1] = "port";
		values[1] = portstr;
		keywords[2] = "host";
		values[2] = hostname ? hostname : sockdir;
		keywords[3] = NULL;
		values[3] = NULL;

		/*
		 * Check if there is a postmaster running already.
		 */
		for (i = 0; i < 16; i++)
		{
			rv = PQpingParams(keywords, values, 1);

			if (rv == PQPING_OK)
			{
				if (port_specified_by_user || i == 15)
				{
					note("port %d apparently in use", port);
					if (!port_specified_by_user)
						note("could not determine an available port");
					bail("Specify an unused port using the --port option or shut down any conflicting PostgreSQL servers.");
				}

				note("port %d apparently in use, trying %d", port, port + 1);
				port++;
				sprintf(portstr, "%d", port);
				setenv("PGPORT", portstr, 1);
			}
			else
				break;
		}

		/*
		 * Start the temp postmaster
		 */
		snprintf(buf, sizeof(buf),
				 "\"%s%spostgres\" -D \"%s/data\" -F%s "
				 "-c \"listen_addresses=%s\" -k \"%s\" "
				 "> \"%s/log/postmaster.log\" 2>&1",
				 bindir ? bindir : "",
				 bindir ? "/" : "",
				 temp_instance, debug ? " -d 5" : "",
				 hostname ? hostname : "", sockdir ? sockdir : "",
				 outputdir);
		postmaster_pid = spawn_process(buf);
		if (postmaster_pid == INVALID_PID)
			bail("could not spawn postmaster: %m");

		/*
		 * Wait till postmaster is able to accept connections; normally takes
		 * only a fraction of a second or so, but Cygwin is reportedly *much*
		 * slower, and test builds using Valgrind or similar tools might be
		 * too.  Hence, allow the default timeout of 60 seconds to be
		 * overridden from the PGCTLTIMEOUT environment variable.
		 */
		env_wait = getenv("PGCTLTIMEOUT");
		if (env_wait != NULL)
		{
			wait_seconds = atoi(env_wait);
			if (wait_seconds <= 0)
				wait_seconds = 60;
		}
		else
			wait_seconds = 60;

		for (i = 0; i < wait_seconds * WAIT_TICKS_PER_SECOND; i++)
		{
			/*
			 * It's fairly unlikely that the server is responding immediately
			 * so we start with sleeping before checking instead of the other
			 * way around.
			 */
			pg_usleep(1000000L / WAIT_TICKS_PER_SECOND);

			rv = PQpingParams(keywords, values, 1);

			/* Done if the server is running and accepts connections */
			if (rv == PQPING_OK)
				break;

			if (rv == PQPING_NO_ATTEMPT)
				bail("attempting to connect to postmaster failed");

			/*
			 * Fail immediately if postmaster has exited
			 */
#ifndef WIN32
			if (waitpid(postmaster_pid, NULL, WNOHANG) == postmaster_pid)
#else
			if (WaitForSingleObject(postmaster_pid, 0) == WAIT_OBJECT_0)
#endif
			{
				bail("postmaster failed, examine \"%s/log/postmaster.log\" for the reason",
					 outputdir);
			}
		}
		if (i >= wait_seconds * WAIT_TICKS_PER_SECOND)
		{
			diag("postmaster did not respond within %d seconds, examine \"%s/log/postmaster.log\" for the reason",
				 wait_seconds, outputdir);

			/*
			 * If we get here, the postmaster is probably wedged somewhere in
			 * startup.  Try to kill it ungracefully rather than leaving a
			 * stuck postmaster that might interfere with subsequent test
			 * attempts.
			 */
#ifndef WIN32
			if (kill(postmaster_pid, SIGKILL) != 0 && errno != ESRCH)
				bail("could not kill failed postmaster: %m");
#else
			if (TerminateProcess(postmaster_pid, 255) == 0)
				bail("could not kill failed postmaster: error code %lu",
					 GetLastError());
#endif
			bail("postmaster failed");
		}

		postmaster_running = true;

#ifdef _WIN64
/* need a series of two casts to convert HANDLE without compiler warning */
#define ULONGPID(x) (unsigned long) (unsigned long long) (x)
#else
#define ULONGPID(x) (unsigned long) (x)
#endif
		note("using temp instance on port %d with PID %lu",
			 port, ULONGPID(postmaster_pid));
	}

	/*
	* Using an existing installation, so may need to get rid of
	* pre-existing database(s) and role(s)
	* Create the test database(s) and role(s)
	*/
	if (!use_existing)
	{
		for (sl = dblist; sl; sl = sl->next)
		{
			drop_database_if_exists(sl->str);
			create_database(sl->str);
		}
		for (sl = extraroles; sl; sl = sl->next)
		{
			drop_role_if_exists(sl->str);
			create_role(sl->str, dblist);
		}
	}

	/*
	 * Ready to run the tests
	 */
	for (sl = schedulelist; sl != NULL; sl = sl->next)
	{
		run_schedule(sl->str, startfunc, postfunc);
	}

	for (sl = extra_tests; sl != NULL; sl = sl->next)
	{
		run_single_test(sl->str, startfunc, postfunc);
	}

	/*
	 * Shut down temp installation's postmaster
	 */
	if (temp_instance)
	{
		stop_postmaster();
	}

	/*
	 * If there were no errors, remove the temp instance immediately to
	 * conserve disk space.  (If there were errors, we leave the instance in
	 * place for possible manual investigation.)
	 */
	if (temp_instance && fail_count == 0)
	{
		if (!rmtree(temp_instance, true))
			diag("could not remove temp instance \"%s\"",
				 temp_instance);
	}

	/*
	 * Emit a TAP compliant Plan
	 */
	plan(fail_count + success_count);

	/*
	 * Emit nice-looking summary message
	 */
	if (fail_count == 0)
		note("All %d tests passed.", success_count);
	else
<<<<<<< HEAD
		/* fail_count>0 && fail_ignore_count>0 */
		snprintf(buf, sizeof(buf),
				 _(" %d of %d tests failed, %d of these failures ignored. "),
				 fail_count + fail_ignore_count,
				 success_count + fail_count + fail_ignore_count,
				 fail_ignore_count);

	putchar('\n');
	
	/* POLAR */
	snprintf(polar_buf, sizeof(polar_buf),
			_(" All %d tests, %d tests in ignore, %d tests in polar ignore. "),
			success_count + polar_ignore_count + fail_count + fail_ignore_count,
			ignore_count,
			polar_ignore_count);

	putchar('\n');

	/* POLAR use strlen(polar_buf) */
	for (i = strlen(polar_buf); i > 0; i--)
		putchar('=');
	printf("\n%s\n", buf);
	/* POLAR */
	printf("\n POLARDB:\n%s\n", polar_buf);
	/* POLAR end*/
	for (i = strlen(polar_buf); i > 0; i--)
		putchar('=');
	putchar('\n');
	putchar('\n');
=======
		diag("%d of %d tests failed.", fail_count, success_count + fail_count);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	if (file_size(difffilename) > 0)
	{
		diag("The differences that caused some tests to fail can be viewed in the file \"%s\".",
			 difffilename);
		diag("A copy of the test summary that you see above is saved in the file \"%s\".",
			 logfilename);
	}
	else
	{
		unlink(difffilename);
		unlink(logfilename);
	}

	fclose(logfile);
	logfile = NULL;

	if (fail_count != 0)
		exit(1);
	return 0;
}
