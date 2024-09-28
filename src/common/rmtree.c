/*-------------------------------------------------------------------------
 *
 * rmtree.c
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/common/rmtree.c
 *
 *-------------------------------------------------------------------------
 */

#ifndef FRONTEND
#include "postgres.h"
#else
#include "postgres_fe.h"
#endif

#include <unistd.h>
#include <sys/stat.h>

<<<<<<< HEAD
/* POLAR */
#ifndef FRONTEND
#include "storage/polar_fd.h"
=======
#include "common/file_utils.h"

#ifndef FRONTEND
#include "storage/fd.h"
#define pg_log_warning(...) elog(WARNING, __VA_ARGS__)
#define LOG_LEVEL WARNING
#define OPENDIR(x) AllocateDir(x)
#define CLOSEDIR(x) FreeDir(x)
#else
#include "common/logging.h"
#define LOG_LEVEL PG_LOG_WARNING
#define OPENDIR(x) opendir(x)
#define CLOSEDIR(x) closedir(x)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#endif

/*
 *	rmtree
 *
 *	Delete a directory tree recursively.
 *	Assumes path points to a valid directory.
 *	Deletes everything under path.
 *	If rmtopdir is true deletes the directory too.
 *	Returns true if successful, false if there was any problem.
 *	(The details of the problem are reported already, so caller
 *	doesn't really have to say anything more, but most do.)
 *
 *  POLAR: for BACKEND support local or shared storage
 */
bool
rmtree(const char *path, bool rmtopdir)
{
	char		pathbuf[MAXPGPATH];
	DIR		   *dir;
	struct dirent *de;
	bool		result = true;
	size_t		dirnames_size = 0;
	size_t		dirnames_capacity = 8;
	char	  **dirnames;

	dir = OPENDIR(path);
	if (dir == NULL)
	{
		pg_log_warning("could not open directory \"%s\": %m", path);
		return false;
	}

<<<<<<< HEAD
		/*
		 * It's ok if the file is not there anymore; we were just about to
		 * delete it anyway.
		 *
		 * This is not an academic possibility. One scenario where this
		 * happens is when bgwriter has a pending unlink request for a file in
		 * a database that's being dropped. In dropdb(), we call
		 * ForgetDatabaseFsyncRequests() to flush out any such pending unlink
		 * requests, but because that's asynchronous, it's not guaranteed that
		 * the bgwriter receives the message in time.
		 */
#ifndef FRONTEND
		if (polar_lstat(pathbuf, &statbuf) != 0)
#else
		if (lstat(pathbuf, &statbuf) != 0)
#endif
		{
			if (errno != ENOENT)
			{
#ifndef FRONTEND
				elog(WARNING, "could not stat file or directory \"%s\": %m",
					 pathbuf);
#else
				fprintf(stderr, _("could not stat file or directory \"%s\": %s\n"),
						pathbuf, strerror(errno));
#endif
				result = false;
			}
=======
	dirnames = (char **) palloc(sizeof(char *) * dirnames_capacity);

	while (errno = 0, (de = readdir(dir)))
	{
		if (strcmp(de->d_name, ".") == 0 ||
			strcmp(de->d_name, "..") == 0)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			continue;
		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", path, de->d_name);
		switch (get_dirent_type(pathbuf, de, false, LOG_LEVEL))
		{
			case PGFILETYPE_ERROR:
				/* already logged, press on */
				break;
			case PGFILETYPE_DIR:

<<<<<<< HEAD
		if (S_ISDIR(statbuf.st_mode))
		{
			/* call ourselves recursively for a directory */
			if (!rmtree(pathbuf, true))
			{
				/* we already reported the error */
				result = false;
			}
		}
		else
		{
#ifndef FRONTEND
			if (polar_unlink(pathbuf) != 0)
#else
			if (unlink(pathbuf) != 0)
#endif
			{
				if (errno != ENOENT)
=======
				/*
				 * Defer recursion until after we've closed this directory, to
				 * avoid using more than one file descriptor at a time.
				 */
				if (dirnames_size == dirnames_capacity)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
				{
					dirnames = repalloc(dirnames,
										sizeof(char *) * dirnames_capacity * 2);
					dirnames_capacity *= 2;
				}
				dirnames[dirnames_size++] = pstrdup(pathbuf);
				break;
			default:
				if (unlink(pathbuf) != 0 && errno != ENOENT)
				{
					pg_log_warning("could not remove file \"%s\": %m", pathbuf);
					result = false;
				}
				break;
		}
	}

	if (errno != 0)
	{
		pg_log_warning("could not read directory \"%s\": %m", path);
		result = false;
	}

	CLOSEDIR(dir);

	/* Now recurse into the subdirectories we found. */
	for (size_t i = 0; i < dirnames_size; ++i)
	{
		if (!rmtree(dirnames[i], true))
			result = false;
		pfree(dirnames[i]);
	}

	if (rmtopdir)
	{
#ifndef FRONTEND
		if (polar_rmdir(path) != 0)
#else
		if (rmdir(path) != 0)
#endif
		{
			pg_log_warning("could not remove directory \"%s\": %m", path);
			result = false;
		}
	}

	pfree(dirnames);

	return result;
}

