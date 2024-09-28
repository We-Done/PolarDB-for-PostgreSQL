/*-------------------------------------------------------------------------
 *
 * plpython.h - Python as a procedural language for PostgreSQL
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/pl/plpython/plpython.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PLPYTHON_H
#define PLPYTHON_H

/* postgres.h needs to be included before Python.h, as usual */
#if !defined(POSTGRES_H)
#error postgres.h must be included before plpython.h
#elif defined(Py_PYTHON_H)
#error Python.h must be included via plpython.h
#endif

/*
 * Pull in Python headers via a wrapper header, to control the scope of
 * the system_header pragma therein.
 */
#include "plpython_system.h"

/* define our text domain for translations */
#undef TEXTDOMAIN
#define TEXTDOMAIN PG_TEXTDOMAIN("plpython")

<<<<<<< HEAD
/* put back our snprintf and vsnprintf */
#ifdef USE_REPL_SNPRINTF
#ifdef snprintf
#undef snprintf
#endif
#ifdef vsnprintf
#undef vsnprintf
#endif
#ifdef __GNUC__
#define vsnprintf(...)	pg_vsnprintf(__VA_ARGS__)
#define snprintf(...)	pg_snprintf(__VA_ARGS__)
#else
#define vsnprintf				pg_vsnprintf
#define snprintf				pg_snprintf
#endif							/* __GNUC__ */
#endif							/* USE_REPL_SNPRINTF */

=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
/*
 * Used throughout, so it's easier to just include it everywhere.
 */
#include "plpy_util.h"

#endif							/* PLPYTHON_H */
