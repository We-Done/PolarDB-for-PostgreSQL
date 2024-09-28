/* src/test/modules/test_dsa/test_dsa--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION test_dsa" to load this file. \quit

<<<<<<< HEAD
CREATE FUNCTION test_dsa_random(loops int, num_allocs int, min_alloc int, max_alloc int, mode text)
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE TYPE test_dsa_row AS (pid int, allocations bigint, elapsed interval);

CREATE FUNCTION test_dsa_random_parallel(loops int, num_allocs int, min_alloc int, max_alloc int, mode text, workers int)
RETURNS SETOF test_dsa_row
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE FUNCTION test_dsa_oom()
RETURNS VOID
AS 'MODULE_PATHNAME'
LANGUAGE C;
=======
CREATE FUNCTION test_dsa_basic()
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE FUNCTION test_dsa_resowners()
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
