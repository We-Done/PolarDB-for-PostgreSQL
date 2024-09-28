<<<<<<< HEAD
/* src/test/modules/test_slru/test_slru--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION test_slru" to load this file. \quit

CREATE FUNCTION test_slru()
RETURNS pg_catalog.void STRICT
AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE FUNCTION test_slru_slot_size_config()
RETURNS pg_catalog.void STRICT
AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE FUNCTION test_slru_hash_index(INTEGER, INTEGER, INTEGER)
RETURNS pg_catalog.void STRICT
AS 'MODULE_PATHNAME' LANGUAGE C;
=======
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION test_slru" to load this file. \quit

CREATE OR REPLACE FUNCTION test_slru_page_write(bigint, text) RETURNS VOID
  AS 'MODULE_PATHNAME', 'test_slru_page_write' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_slru_page_writeall() RETURNS VOID
  AS 'MODULE_PATHNAME', 'test_slru_page_writeall' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_slru_page_sync(bigint) RETURNS VOID
  AS 'MODULE_PATHNAME', 'test_slru_page_sync' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_slru_page_read(bigint, bool DEFAULT true) RETURNS text
  AS 'MODULE_PATHNAME', 'test_slru_page_read' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_slru_page_readonly(bigint) RETURNS text
  AS 'MODULE_PATHNAME', 'test_slru_page_readonly' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_slru_page_exists(bigint) RETURNS bool
  AS 'MODULE_PATHNAME', 'test_slru_page_exists' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_slru_page_delete(bigint) RETURNS VOID
  AS 'MODULE_PATHNAME', 'test_slru_page_delete' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_slru_page_truncate(bigint) RETURNS VOID
  AS 'MODULE_PATHNAME', 'test_slru_page_truncate' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_slru_delete_all() RETURNS VOID
  AS 'MODULE_PATHNAME', 'test_slru_delete_all' LANGUAGE C;


CREATE OR REPLACE FUNCTION test_create_multixact() RETURNS xid
  AS 'MODULE_PATHNAME', 'test_create_multixact' LANGUAGE C;
CREATE OR REPLACE FUNCTION test_read_multixact(xid) RETURNS VOID
  AS 'MODULE_PATHNAME', 'test_read_multixact'LANGUAGE C;
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
