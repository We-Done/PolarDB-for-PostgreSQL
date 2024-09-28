-- sanity check of system catalog
SELECT attrelid, attname, attidentity FROM pg_attribute WHERE attidentity NOT IN ('', 'a', 'd');


CREATE TABLE itest1 (a int generated by default as identity, b text);
CREATE TABLE itest2 (a bigint generated always as identity, b text);
CREATE TABLE itest3 (a smallint generated by default as identity (start with 7 increment by 5), b text);
ALTER TABLE itest3 ALTER COLUMN a ADD GENERATED ALWAYS AS IDENTITY;  -- error

SELECT table_name, column_name, column_default, is_nullable, is_identity, identity_generation, identity_start, identity_increment, identity_maximum, identity_minimum, identity_cycle FROM information_schema.columns WHERE table_name LIKE 'itest_' ORDER BY 1, 2;

-- internal sequences should not be shown here
SELECT sequence_name FROM information_schema.sequences WHERE sequence_name LIKE 'itest%';

SELECT pg_get_serial_sequence('itest1', 'a');

\d itest1_a_seq

CREATE TABLE itest4 (a int, b text);
ALTER TABLE itest4 ALTER COLUMN a ADD GENERATED ALWAYS AS IDENTITY;  -- error, requires NOT NULL
ALTER TABLE itest4 ALTER COLUMN a SET NOT NULL;
ALTER TABLE itest4 ALTER COLUMN a ADD GENERATED ALWAYS AS IDENTITY;  -- ok
ALTER TABLE itest4 ALTER COLUMN a DROP NOT NULL;  -- error, disallowed
ALTER TABLE itest4 ALTER COLUMN a ADD GENERATED ALWAYS AS IDENTITY;  -- error, already set
ALTER TABLE itest4 ALTER COLUMN b ADD GENERATED ALWAYS AS IDENTITY;  -- error, wrong data type

-- for later
ALTER TABLE itest4 ALTER COLUMN b SET DEFAULT '';

-- invalid column type
CREATE TABLE itest_err_1 (a text generated by default as identity);

-- duplicate identity
CREATE TABLE itest_err_2 (a int generated always as identity generated by default as identity);

-- cannot have default and identity
CREATE TABLE itest_err_3 (a int default 5 generated by default as identity);

-- cannot combine serial and identity
CREATE TABLE itest_err_4 (a serial generated by default as identity);

INSERT INTO itest1 DEFAULT VALUES;
INSERT INTO itest1 DEFAULT VALUES;
INSERT INTO itest2 DEFAULT VALUES;
INSERT INTO itest2 DEFAULT VALUES;
INSERT INTO itest3 DEFAULT VALUES;
INSERT INTO itest3 DEFAULT VALUES;
INSERT INTO itest4 DEFAULT VALUES;
INSERT INTO itest4 DEFAULT VALUES;

SELECT * FROM itest1;
SELECT * FROM itest2;
SELECT * FROM itest3;
SELECT * FROM itest4;


-- VALUES RTEs

CREATE TABLE itest5 (a int generated always as identity, b text);
INSERT INTO itest5 VALUES (1, 'a');  -- error
INSERT INTO itest5 VALUES (DEFAULT, 'a');  -- ok
INSERT INTO itest5 VALUES (2, 'b'), (3, 'c');  -- error
INSERT INTO itest5 VALUES (DEFAULT, 'b'), (3, 'c');  -- error
INSERT INTO itest5 VALUES (2, 'b'), (DEFAULT, 'c');  -- error
INSERT INTO itest5 VALUES (DEFAULT, 'b'), (DEFAULT, 'c');  -- ok

INSERT INTO itest5 OVERRIDING SYSTEM VALUE VALUES (-1, 'aa');
INSERT INTO itest5 OVERRIDING SYSTEM VALUE VALUES (-2, 'bb'), (-3, 'cc');
INSERT INTO itest5 OVERRIDING SYSTEM VALUE VALUES (DEFAULT, 'dd'), (-4, 'ee');
INSERT INTO itest5 OVERRIDING SYSTEM VALUE VALUES (-5, 'ff'), (DEFAULT, 'gg');
INSERT INTO itest5 OVERRIDING SYSTEM VALUE VALUES (DEFAULT, 'hh'), (DEFAULT, 'ii');

INSERT INTO itest5 OVERRIDING USER VALUE VALUES (-1, 'aaa');
INSERT INTO itest5 OVERRIDING USER VALUE VALUES (-2, 'bbb'), (-3, 'ccc');
INSERT INTO itest5 OVERRIDING USER VALUE VALUES (DEFAULT, 'ddd'), (-4, 'eee');
INSERT INTO itest5 OVERRIDING USER VALUE VALUES (-5, 'fff'), (DEFAULT, 'ggg');
INSERT INTO itest5 OVERRIDING USER VALUE VALUES (DEFAULT, 'hhh'), (DEFAULT, 'iii');

SELECT * FROM itest5;
DROP TABLE itest5;

INSERT INTO itest3 VALUES (DEFAULT, 'a');
INSERT INTO itest3 VALUES (DEFAULT, 'b'), (DEFAULT, 'c');

SELECT * FROM itest3;


-- OVERRIDING tests

-- GENERATED BY DEFAULT

-- This inserts the row as presented:
INSERT INTO itest1 VALUES (10, 'xyz');
-- With GENERATED BY DEFAULT, OVERRIDING SYSTEM VALUE is not allowed
-- by the standard, but we allow it as a no-op, since it is of use if
-- there are multiple identity columns in a table, which is also an
-- extension.
INSERT INTO itest1 OVERRIDING SYSTEM VALUE VALUES (20, 'xyz');
-- This ignores the 30 and uses the sequence value instead:
INSERT INTO itest1 OVERRIDING USER VALUE VALUES (30, 'xyz');

SELECT * FROM itest1;

-- GENERATED ALWAYS

-- This is an error:
INSERT INTO itest2 VALUES (10, 'xyz');
-- This inserts the row as presented:
INSERT INTO itest2 OVERRIDING SYSTEM VALUE VALUES (20, 'xyz');
-- This ignores the 30 and uses the sequence value instead:
INSERT INTO itest2 OVERRIDING USER VALUE VALUES (30, 'xyz');

SELECT * FROM itest2;


-- UPDATE tests

-- GENERATED BY DEFAULT is not restricted.
UPDATE itest1 SET a = 101 WHERE a = 1;
UPDATE itest1 SET a = DEFAULT WHERE a = 2;
SELECT * FROM itest1;

-- GENERATED ALWAYS allows only DEFAULT.
UPDATE itest2 SET a = 101 WHERE a = 1;  -- error
UPDATE itest2 SET a = DEFAULT WHERE a = 2;  -- ok
SELECT * FROM itest2;


-- COPY tests

CREATE TABLE itest9 (a int GENERATED ALWAYS AS IDENTITY, b text, c bigint);

COPY itest9 FROM stdin;
100	foo	200
101	bar	201
\.

COPY itest9 (b, c) FROM stdin;
foo2	202
bar2	203
\.

SELECT * FROM itest9 ORDER BY c;


-- DROP IDENTITY tests

ALTER TABLE itest4 ALTER COLUMN a DROP IDENTITY;
ALTER TABLE itest4 ALTER COLUMN a DROP IDENTITY;  -- error
ALTER TABLE itest4 ALTER COLUMN a DROP IDENTITY IF EXISTS;  -- noop

INSERT INTO itest4 DEFAULT VALUES;  -- fails because NOT NULL is not dropped
ALTER TABLE itest4 ALTER COLUMN a DROP NOT NULL;
INSERT INTO itest4 DEFAULT VALUES;
SELECT * FROM itest4;

-- check that sequence is removed
SELECT sequence_name FROM itest4_a_seq;


-- test views

CREATE TABLE itest10 (a int generated by default as identity, b text);
CREATE TABLE itest11 (a int generated always as identity, b text);

CREATE VIEW itestv10 AS SELECT * FROM itest10;
CREATE VIEW itestv11 AS SELECT * FROM itest11;

INSERT INTO itestv10 DEFAULT VALUES;
INSERT INTO itestv10 DEFAULT VALUES;

INSERT INTO itestv11 DEFAULT VALUES;
INSERT INTO itestv11 DEFAULT VALUES;

SELECT * FROM itestv10;
SELECT * FROM itestv11;

INSERT INTO itestv10 VALUES (10, 'xyz');
INSERT INTO itestv10 OVERRIDING USER VALUE VALUES (11, 'xyz');

SELECT * FROM itestv10;

INSERT INTO itestv11 VALUES (10, 'xyz');
INSERT INTO itestv11 OVERRIDING SYSTEM VALUE VALUES (11, 'xyz');

SELECT * FROM itestv11;

DROP VIEW itestv10, itestv11;


-- ADD COLUMN

CREATE TABLE itest13 (a int);
-- add column to empty table
ALTER TABLE itest13 ADD COLUMN b int GENERATED BY DEFAULT AS IDENTITY;
INSERT INTO itest13 VALUES (1), (2), (3);
-- add column to populated table
ALTER TABLE itest13 ADD COLUMN c int GENERATED BY DEFAULT AS IDENTITY;
SELECT * FROM itest13;


-- various ALTER COLUMN tests

-- fail, not allowed for identity columns
ALTER TABLE itest1 ALTER COLUMN a SET DEFAULT 1;

-- fail, not allowed, already has a default
CREATE TABLE itest5 (a serial, b text);
ALTER TABLE itest5 ALTER COLUMN a ADD GENERATED ALWAYS AS IDENTITY;

ALTER TABLE itest3 ALTER COLUMN a TYPE int;
SELECT seqtypid::regtype FROM pg_sequence WHERE seqrelid = 'itest3_a_seq'::regclass;
\d itest3

ALTER TABLE itest3 ALTER COLUMN a TYPE text;  -- error

-- check that unlogged propagates to sequence
CREATE UNLOGGED TABLE itest17 (a int NOT NULL, b text);
ALTER TABLE itest17 ALTER COLUMN a ADD GENERATED ALWAYS AS IDENTITY;
ALTER TABLE itest17 ADD COLUMN c int GENERATED ALWAYS AS IDENTITY;
\d itest17
\d itest17_a_seq
\d itest17_c_seq
CREATE TABLE itest18 (a int NOT NULL, b text);
ALTER TABLE itest18 SET UNLOGGED, ALTER COLUMN a ADD GENERATED ALWAYS AS IDENTITY;
\d itest18
\d itest18_a_seq
ALTER TABLE itest18 SET LOGGED;
\d itest18
\d itest18_a_seq
ALTER TABLE itest18 SET UNLOGGED;
\d itest18
\d itest18_a_seq

-- kinda silly to change property in the same command, but it should work
ALTER TABLE itest3
  ADD COLUMN c int GENERATED BY DEFAULT AS IDENTITY,
  ALTER COLUMN c SET GENERATED ALWAYS;
\d itest3


-- ALTER COLUMN ... SET

CREATE TABLE itest6 (a int GENERATED ALWAYS AS IDENTITY, b text);
INSERT INTO itest6 DEFAULT VALUES;

ALTER TABLE itest6 ALTER COLUMN a SET GENERATED BY DEFAULT SET INCREMENT BY 2 SET START WITH 100 RESTART;
INSERT INTO itest6 DEFAULT VALUES;
INSERT INTO itest6 DEFAULT VALUES;
SELECT * FROM itest6;

SELECT table_name, column_name, is_identity, identity_generation FROM information_schema.columns WHERE table_name = 'itest6' ORDER BY 1, 2;

ALTER TABLE itest6 ALTER COLUMN b SET INCREMENT BY 2;  -- fail, not identity


-- prohibited direct modification of sequence

ALTER SEQUENCE itest6_a_seq OWNED BY NONE;


-- inheritance

CREATE TABLE itest7 (a int GENERATED ALWAYS AS IDENTITY);
INSERT INTO itest7 DEFAULT VALUES;
SELECT * FROM itest7;

-- identity property is not inherited
CREATE TABLE itest7a (b text) INHERITS (itest7);

-- make column identity in child table
CREATE TABLE itest7b (a int);
CREATE TABLE itest7c (a int GENERATED ALWAYS AS IDENTITY) INHERITS (itest7b);
INSERT INTO itest7c DEFAULT VALUES;
SELECT * FROM itest7c;

CREATE TABLE itest7d (a int not null);
CREATE TABLE itest7e () INHERITS (itest7d);
ALTER TABLE itest7d ALTER COLUMN a ADD GENERATED ALWAYS AS IDENTITY;
ALTER TABLE itest7d ADD COLUMN b int GENERATED ALWAYS AS IDENTITY;  -- error

SELECT table_name, column_name, is_nullable, is_identity, identity_generation FROM information_schema.columns WHERE table_name LIKE 'itest7%' ORDER BY 1, 2;

-- These ALTER TABLE variants will not recurse.
ALTER TABLE itest7 ALTER COLUMN a SET GENERATED BY DEFAULT;
ALTER TABLE itest7 ALTER COLUMN a RESTART;
ALTER TABLE itest7 ALTER COLUMN a DROP IDENTITY;

-- privileges
CREATE USER regress_identity_user1;
CREATE TABLE itest8 (a int GENERATED ALWAYS AS IDENTITY, b text);
GRANT SELECT, INSERT ON itest8 TO regress_identity_user1;
SET ROLE regress_identity_user1;
INSERT INTO itest8 DEFAULT VALUES;
SELECT * FROM itest8;
RESET ROLE;
DROP TABLE itest8;
DROP USER regress_identity_user1;

-- multiple steps in ALTER TABLE
CREATE TABLE itest8 (f1 int);

ALTER TABLE itest8
  ADD COLUMN f2 int NOT NULL,
  ALTER COLUMN f2 ADD GENERATED ALWAYS AS IDENTITY;

ALTER TABLE itest8
  ADD COLUMN f3 int NOT NULL,
  ALTER COLUMN f3 ADD GENERATED ALWAYS AS IDENTITY,
  ALTER COLUMN f3 SET GENERATED BY DEFAULT SET INCREMENT 10;

ALTER TABLE itest8
  ADD COLUMN f4 int;

ALTER TABLE itest8
  ALTER COLUMN f4 SET NOT NULL,
  ALTER COLUMN f4 ADD GENERATED ALWAYS AS IDENTITY,
  ALTER COLUMN f4 SET DATA TYPE bigint;

ALTER TABLE itest8
  ADD COLUMN f5 int GENERATED ALWAYS AS IDENTITY;

ALTER TABLE itest8
  ALTER COLUMN f5 DROP IDENTITY,
  ALTER COLUMN f5 DROP NOT NULL,
  ALTER COLUMN f5 SET DATA TYPE bigint;

INSERT INTO itest8 VALUES(0), (1);

-- This does not work when the table isn't empty.  That's intentional,
-- since ADD GENERATED should only affect later insertions:
ALTER TABLE itest8
  ADD COLUMN f22 int NOT NULL,
  ALTER COLUMN f22 ADD GENERATED ALWAYS AS IDENTITY;

TABLE itest8;
\d+ itest8
\d itest8_f2_seq
\d itest8_f3_seq
\d itest8_f4_seq
\d itest8_f5_seq
DROP TABLE itest8;


-- typed tables (currently not supported)

CREATE TYPE itest_type AS (f1 integer, f2 text, f3 bigint);
CREATE TABLE itest12 OF itest_type (f1 WITH OPTIONS GENERATED ALWAYS AS IDENTITY); -- error
DROP TYPE itest_type CASCADE;


-- table partitions

-- partitions inherit identity column and share sequence
CREATE TABLE pitest1 (f1 date NOT NULL, f2 text, f3 bigint generated always as identity) PARTITION BY RANGE (f1);
-- new partition
CREATE TABLE pitest1_p1 PARTITION OF pitest1 FOR VALUES FROM ('2016-07-01') TO ('2016-08-01');
INSERT into pitest1(f1, f2) VALUES ('2016-07-2', 'from pitest1');
INSERT into pitest1_p1 (f1, f2) VALUES ('2016-07-3', 'from pitest1_p1');
-- attached partition
CREATE TABLE pitest1_p2 (f3 bigint, f2 text, f1 date NOT NULL);
INSERT INTO pitest1_p2 (f1, f2, f3) VALUES ('2016-08-2', 'before attaching', 100);
ALTER TABLE pitest1 ATTACH PARTITION pitest1_p2 FOR VALUES FROM ('2016-08-01') TO ('2016-09-01'); -- requires NOT NULL constraint
ALTER TABLE pitest1_p2 ALTER COLUMN f3 SET NOT NULL;
ALTER TABLE pitest1 ATTACH PARTITION pitest1_p2 FOR VALUES FROM ('2016-08-01') TO ('2016-09-01');
INSERT INTO pitest1_p2 (f1, f2) VALUES ('2016-08-3', 'from pitest1_p2');
INSERT INTO pitest1 (f1, f2) VALUES ('2016-08-4', 'from pitest1');
-- LIKE INCLUDING on partition
CREATE TABLE pitest1_p1_like (LIKE pitest1_p1 INCLUDING IDENTITY);
INSERT into pitest1_p1_like(f1, f2) VALUES ('2016-07-2', 'from pitest1_p1_like');
SELECT tableoid::regclass, f1, f2, f3 FROM pitest1;
SELECT tableoid::regclass, f1, f2, f3 FROM pitest1_p1_like;
ALTER TABLE pitest1 ALTER COLUMN f3 SET DATA TYPE bigint;
SELECT tableoid::regclass, f1, f2, f3, pg_typeof(f3) FROM pitest1;
SELECT tableoid::regclass, f1, f2, f3, pg_typeof(f3) FROM pitest1_p2;

-- add identity column
CREATE TABLE pitest2 (f1 date NOT NULL, f2 text) PARTITION BY RANGE (f1);
CREATE TABLE pitest2_p1 PARTITION OF pitest2 FOR VALUES FROM ('2016-07-01') TO ('2016-08-01');
CREATE TABLE pitest2_p2 PARTITION OF pitest2 FOR VALUES FROM ('2016-08-01') TO ('2016-09-01');
INSERT into pitest2(f1, f2) VALUES ('2016-07-2', 'from pitest2');
INSERT INTO pitest2 (f1, f2) VALUES ('2016-08-2', 'from pitest2');
ALTER TABLE pitest2 ADD COLUMN f3 int GENERATED ALWAYS AS IDENTITY;
INSERT into pitest2_p1 (f1, f2) VALUES ('2016-07-3', 'from pitest2_p1');
INSERT INTO pitest2_p2 (f1, f2) VALUES ('2016-08-3', 'from pitest2_p2');
INSERT into pitest2(f1, f2) VALUES ('2016-07-4', 'from pitest2');
INSERT INTO pitest2 (f1, f2) VALUES ('2016-08-4', 'from pitest2');
SELECT tableoid::regclass, f1, f2, f3 FROM pitest2;

-- SET identity column
ALTER TABLE pitest2_p1 ALTER COLUMN f3 SET GENERATED BY DEFAULT; -- fails
ALTER TABLE pitest2_p1 ALTER COLUMN f3 SET INCREMENT BY 2; -- fails
ALTER TABLE ONLY pitest2 ALTER COLUMN f3 SET GENERATED BY DEFAULT SET INCREMENT BY 2 SET START WITH 1000 RESTART; -- fails
ALTER TABLE pitest2 ALTER COLUMN f3 SET GENERATED BY DEFAULT SET INCREMENT BY 2 SET START WITH 1000 RESTART;
INSERT into pitest2(f1, f2, f3) VALUES ('2016-07-5', 'from pitest2', 200);
INSERT INTO pitest2(f1, f2) VALUES ('2016-08-5', 'from pitest2');
INSERT into pitest2_p1 (f1, f2) VALUES ('2016-07-6', 'from pitest2_p1');
INSERT INTO pitest2_p2 (f1, f2, f3) VALUES ('2016-08-6', 'from pitest2_p2', 300);
SELECT tableoid::regclass, f1, f2, f3 FROM pitest2;

-- detaching a partition removes identity property
ALTER TABLE pitest2 DETACH PARTITION pitest2_p1;
INSERT into pitest2(f1, f2) VALUES ('2016-08-7', 'from pitest2');
INSERT into pitest2_p1 (f1, f2) VALUES ('2016-07-7', 'from pitest2_p1'); -- error
INSERT into pitest2_p1 (f1, f2, f3) VALUES ('2016-07-7', 'from pitest2_p1', 2000);
SELECT tableoid::regclass, f1, f2, f3 FROM pitest2;
SELECT tableoid::regclass, f1, f2, f3 FROM pitest2_p1;

DROP TABLE pitest2_p1;

-- changing a regular column to identity column in a partitioned table
CREATE TABLE pitest3 (f1 date NOT NULL, f2 text, f3 int) PARTITION BY RANGE (f1);
CREATE TABLE pitest3_p1 PARTITION OF pitest3 FOR VALUES FROM ('2016-07-01') TO ('2016-08-01');
INSERT into pitest3 VALUES ('2016-07-2', 'from pitest3', 1);
INSERT into pitest3_p1 VALUES ('2016-07-3', 'from pitest3_p1', 2);
-- fails, changing only a partition not allowed
ALTER TABLE pitest3_p1
            ALTER COLUMN f3 SET NOT NULL,
            ALTER COLUMN f3 ADD GENERATED ALWAYS AS IDENTITY (START WITH 3);
-- fails, changing only the partitioned table not allowed
BEGIN;
ALTER TABLE pitest3_p1 ALTER COLUMN f3 SET NOT NULL;
ALTER TABLE ONLY pitest3
            ALTER COLUMN f3 ADD GENERATED ALWAYS AS IDENTITY (START WITH 3);
ROLLBACK;
ALTER TABLE pitest3
            ALTER COLUMN f3 SET NOT NULL,
            ALTER COLUMN f3 ADD GENERATED ALWAYS AS IDENTITY (START WITH 3);
INSERT into pitest3(f1, f2) VALUES ('2016-07-4', 'from pitest3');
INSERT into pitest3_p1 (f1, f2) VALUES ('2016-07-5', 'from pitest3_p1');
SELECT tableoid::regclass, f1, f2, f3 FROM pitest3;

-- changing an identity column to a non-identity column in a partitioned table
ALTER TABLE pitest3_p1 ALTER COLUMN f3 DROP IDENTITY; -- fails
ALTER TABLE ONLY pitest3 ALTER COLUMN f3 DROP IDENTITY; -- fails
ALTER TABLE pitest3 ALTER COLUMN f3 DROP IDENTITY;
INSERT into pitest3(f1, f2) VALUES ('2016-07-4', 'from pitest3'); -- fails
INSERT into pitest3_p1 (f1, f2) VALUES ('2016-07-5', 'from pitest3_p1'); -- fails
INSERT into pitest3(f1, f2, f3) VALUES ('2016-07-6', 'from pitest3', 5);
INSERT into pitest3_p1 (f1, f2, f3) VALUES ('2016-07-7', 'from pitest3_p1', 6);
SELECT tableoid::regclass, f1, f2, f3 FROM pitest3;

-- Changing NOT NULL constraint of identity columns is not allowed
ALTER TABLE pitest1_p1 ALTER COLUMN f3 DROP NOT NULL;
ALTER TABLE pitest1 ALTER COLUMN f3 DROP NOT NULL;
-- Identity columns have their own default
ALTER TABLE pitest1_p2 ALTER COLUMN f3 SET DEFAULT 10000;
ALTER TABLE pitest1 ALTER COLUMN f3 SET DEFAULT 10000;
-- Adding identity to an identity column is not allowed
ALTER TABLE pitest1_p2 ALTER COLUMN f3 ADD GENERATED BY DEFAULT AS IDENTITY;
ALTER TABLE pitest1 ALTER COLUMN f3 ADD GENERATED BY DEFAULT AS IDENTITY;

-- partitions with their own identity columns are not allowed, even if the
-- partitioned table does not have an identity column.
CREATE TABLE pitest1_pfail PARTITION OF pitest1 (
    f3 WITH OPTIONS GENERATED ALWAYS AS IDENTITY
) FOR VALUES FROM ('2016-11-01') TO ('2016-12-01');

CREATE TABLE pitest_pfail PARTITION OF pitest3 (
    f3 WITH OPTIONS GENERATED ALWAYS AS IDENTITY
) FOR VALUES FROM ('2016-07-01') TO ('2016-08-01');

CREATE TABLE pitest1_pfail (f1 date NOT NULL, f2 text, f3 bigint GENERATED ALWAYS AS IDENTITY);
ALTER TABLE pitest1 ATTACH PARTITION pitest1_pfail FOR VALUES FROM ('2016-11-01') TO ('2016-12-01');
ALTER TABLE pitest3 ATTACH PARTITION pitest1_pfail FOR VALUES FROM ('2016-11-01') TO ('2016-12-01');

DROP TABLE pitest1_pfail;
DROP TABLE pitest3;

-- test that sequence of half-dropped serial column is properly ignored

CREATE TABLE itest14 (id serial);
ALTER TABLE itest14 ALTER id DROP DEFAULT;
ALTER TABLE itest14 ALTER id ADD GENERATED BY DEFAULT AS IDENTITY;
INSERT INTO itest14 (id) VALUES (DEFAULT);

-- Identity columns must be NOT NULL (cf bug #16913)

CREATE TABLE itest15 (id integer GENERATED ALWAYS AS IDENTITY NULL); -- fail
CREATE TABLE itest15 (id integer NULL GENERATED ALWAYS AS IDENTITY); -- fail
CREATE TABLE itest15 (id integer GENERATED ALWAYS AS IDENTITY NOT NULL);
DROP TABLE itest15;
CREATE TABLE itest15 (id integer NOT NULL GENERATED ALWAYS AS IDENTITY);
DROP TABLE itest15;

-- MERGE tests
CREATE TABLE itest15 (a int GENERATED ALWAYS AS IDENTITY, b text);
CREATE TABLE itest16 (a int GENERATED BY DEFAULT AS IDENTITY, b text);

MERGE INTO itest15 t
USING (SELECT 10 AS s_a, 'inserted by merge' AS s_b) s
ON t.a = s.s_a
WHEN NOT MATCHED THEN
	INSERT (a, b) VALUES (s.s_a, s.s_b);

-- Used to fail, but now it works and ignores the user supplied value
MERGE INTO itest15 t
USING (SELECT 20 AS s_a, 'inserted by merge' AS s_b) s
ON t.a = s.s_a
WHEN NOT MATCHED THEN
	INSERT (a, b) OVERRIDING USER VALUE VALUES (s.s_a, s.s_b);

MERGE INTO itest15 t
USING (SELECT 30 AS s_a, 'inserted by merge' AS s_b) s
ON t.a = s.s_a
WHEN NOT MATCHED THEN
	INSERT (a, b) OVERRIDING SYSTEM VALUE VALUES (s.s_a, s.s_b);

MERGE INTO itest16 t
USING (SELECT 10 AS s_a, 'inserted by merge' AS s_b) s
ON t.a = s.s_a
WHEN NOT MATCHED THEN
	INSERT (a, b) VALUES (s.s_a, s.s_b);

MERGE INTO itest16 t
USING (SELECT 20 AS s_a, 'inserted by merge' AS s_b) s
ON t.a = s.s_a
WHEN NOT MATCHED THEN
	INSERT (a, b) OVERRIDING USER VALUE VALUES (s.s_a, s.s_b);

MERGE INTO itest16 t
USING (SELECT 30 AS s_a, 'inserted by merge' AS s_b) s
ON t.a = s.s_a
WHEN NOT MATCHED THEN
	INSERT (a, b) OVERRIDING SYSTEM VALUE VALUES (s.s_a, s.s_b);

SELECT * FROM itest15;
SELECT * FROM itest16;
DROP TABLE itest15;
DROP TABLE itest16;

-- For testing of pg_dump and pg_upgrade, leave behind some identity
-- sequences whose logged-ness doesn't match their owning table's.
CREATE TABLE identity_dump_logged (a INT GENERATED ALWAYS AS IDENTITY);
ALTER SEQUENCE identity_dump_logged_a_seq SET UNLOGGED;
CREATE UNLOGGED TABLE identity_dump_unlogged (a INT GENERATED ALWAYS AS IDENTITY);
ALTER SEQUENCE identity_dump_unlogged_a_seq SET LOGGED;
SELECT relname, relpersistence FROM pg_class
  WHERE relname ~ '^identity_dump_' ORDER BY 1;
