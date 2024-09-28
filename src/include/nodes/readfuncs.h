/*-------------------------------------------------------------------------
 *
 * readfuncs.h
 *	  header file for read.c and readfuncs.c. These functions are internal
 *	  to the stringToNode interface and should not be used by anyone else.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/nodes/readfuncs.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef READFUNCS_H
#define READFUNCS_H

#include "nodes/nodes.h"

/*
 * variable in read.c that needs to be accessible to readfuncs.c
 */
#ifdef DEBUG_NODE_TESTS_ENABLED
extern PGDLLIMPORT bool restore_location_fields;
#endif

/*
 * prototypes for functions in read.c (the lisp token parser)
 */
<<<<<<< HEAD
extern char *pg_strtok(int *length);
extern char *polar_pg_strtok(int *length);

extern char *debackslash(char *token, int length);
extern void *nodeRead(char *token, int tok_len);
=======
extern const char *pg_strtok(int *length);
extern char *debackslash(const char *token, int length);
extern void *nodeRead(const char *token, int tok_len);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

/*
 * prototypes for functions in readfuncs.c
 */
extern Node *parseNodeString(void);

/* POLAR px */
extern bool has_px_plangen_filed(void);
extern void read_binary_string_filed(void *data, int size);
extern int polar_get_node_output_version(void);
/* POLAR end */

#endif							/* READFUNCS_H */
