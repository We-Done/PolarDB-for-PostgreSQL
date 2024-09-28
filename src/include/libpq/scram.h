/*-------------------------------------------------------------------------
 *
 * scram.h
 *	  Interface to libpq/scram.c
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/libpq/scram.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_SCRAM_H
#define PG_SCRAM_H

<<<<<<< HEAD
=======
#include "common/cryptohash.h"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
#include "lib/stringinfo.h"
#include "libpq/libpq-be.h"
#include "libpq/sasl.h"

/* Number of iterations when generating new secrets */
extern PGDLLIMPORT int scram_sha_256_iterations;

<<<<<<< HEAD
/* Routines dedicated to authentication */
extern void pg_be_scram_get_mechanisms(Port *port, StringInfo buf);
extern void *pg_be_scram_init(Port *port, const char *selected_mech, const char *shadow_pass);
extern int pg_be_scram_exchange(void *opaq, char *input, int inputlen,
					 char **output, int *outputlen, char **logdetail);

/* Routines to handle and check SCRAM-SHA-256 verifier */
extern char *pg_be_scram_build_verifier(const char *password);
extern bool parse_scram_verifier(const char *verifier, int *iterations, char **salt,
					 uint8 *stored_key, uint8 *server_key);
=======
/* SASL implementation callbacks */
extern PGDLLIMPORT const pg_be_sasl_mech pg_be_scram_mech;

/* Routines to handle and check SCRAM-SHA-256 secret */
extern char *pg_be_scram_build_secret(const char *password);
extern bool parse_scram_secret(const char *secret,
							   int *iterations,
							   pg_cryptohash_type *hash_type,
							   int *key_length, char **salt,
							   uint8 *stored_key, uint8 *server_key);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
extern bool scram_verify_plain_password(const char *username,
										const char *password, const char *secret);

#endif							/* PG_SCRAM_H */
