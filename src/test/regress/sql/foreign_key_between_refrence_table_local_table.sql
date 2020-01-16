CREATE SCHEMA fkey_reference_local_table;
SET search_path TO 'fkey_reference_local_table';

--- ALTER TABLE commands defining fkey constraint between local tables and reference tables ---

-- create test tables

CREATE TABLE local_table(l1 int);
CREATE TABLE reference_table(r1 int primary key);
SELECT create_reference_table('reference_table');

-- foreign key from local table to reference table --

-- this should fail
ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES reference_table(r1);

-- we do not support ALTER TABLE ADD CONSTRAINT fkey between local & reference table
-- within a transaction block, below block should fail
BEGIN;
ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES reference_table(r1);
COMMIT;

-- replicate reference table to coordinator
SELECT master_add_node('localhost', :master_port, groupId => 0);

-- we support ON DELETE CASCADE behaviour in "ALTER TABLE ADD fkey local_table (to reference_table) commands
ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES reference_table(r1) ON DELETE CASCADE;

-- show that on delete cascade works
INSERT INTO reference_table VALUES (11);
INSERT INTO local_table VALUES (11);
DELETE FROM reference_table WHERE r1=11;
SELECT * FROM local_table ORDER BY l1;

-- show that we support drop constraint
ALTER TABLE local_table DROP CONSTRAINT fkey_local_to_ref;

-- we support ON UPDATE CASCADE behaviour in "ALTER TABLE ADD fkey local_table (to reference table)" commands
ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES reference_table(r1) ON UPDATE CASCADE;

-- show that on delete cascade works
INSERT INTO reference_table VALUES (12);
INSERT INTO local_table VALUES (12);
UPDATE reference_table SET r1=13 WHERE r1=12;
SELECT * FROM local_table ORDER BY l1;

-- drop constraint for next commands
ALTER TABLE local_table DROP CONSTRAINT fkey_local_to_ref;

-- show that we are checking for foreign key constraint while defining

INSERT INTO local_table VALUES (2);

-- this should fail
ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES reference_table(r1);

INSERT INTO reference_table VALUES (2);

-- this should work
ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES reference_table(r1);

-- show that we are checking for foreign key constraint after defining

-- this should fail
INSERT INTO local_table VALUES (1);

INSERT INTO reference_table VALUES (1);

-- this should work
INSERT INTO local_table VALUES (1);

-- we do not support ALTER TABLE DROP CONSTRAINT fkey between local & reference table
-- within a transaction block, below block should fail
BEGIN;
ALTER TABLE local_table DROP CONSTRAINT fkey_local_to_ref;
COMMIT;

-- show that we do not allow removing coordinator when we have a fkey constraint
-- between a coordinator local table and a reference table
SELECT master_remove_node('localhost', :master_port);

-- drop and add constraint for next commands
ALTER TABLE local_table DROP CONSTRAINT fkey_local_to_ref;

ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES reference_table(r1);

-- show that drop table without CASCADE does not error as we already append CASCADE
DROP TABLE reference_table;

-- create one reference table and one distributed table for next tests
CREATE TABLE reference_table(r1 int primary key);
SELECT create_reference_table('reference_table');
CREATE TABLE distributed_table(d1 int primary key);
SELECT create_distributed_table('distributed_table', 'd1');

-- chain the tables's fkey constraints to each other
-- ref -> distributed & local -> ref
ALTER TABLE reference_table ADD CONSTRAINT fkey_ref_to_dist FOREIGN KEY(r1) REFERENCES distributed_table(d1);
ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES reference_table(r1);

-- this should fail
INSERT INTO reference_table VALUES (41);
-- this should also fail
INSERT INTO local_table VALUES (41);

INSERT INTO distributed_table VALUES (41);
INSERT INTO reference_table VALUES (41);
-- this should work
INSERT INTO local_table VALUES (41);

-- drop tables finally
DROP TABLE local_table;
DROP TABLE reference_table;
DROP TABLE distributed_table;

-- TODO: drop them together after fixing the bug

-- create test tables

CREATE TABLE local_table(l1 int primary key);
CREATE TABLE reference_table(r1 int);
SELECT create_reference_table('reference_table');

-- remove master node from pg_dist_node
SELECT master_remove_node('localhost', :master_port);

-- foreign key from reference table to local table --

-- this should fail
ALTER TABLE reference_table ADD CONSTRAINT fkey_ref_to_local FOREIGN KEY(r1) REFERENCES local_table(l1);

-- we do not support ALTER TABLE ADD CONSTRAINT fkey between local & reference table
-- within a transaction block, below block should fail
BEGIN;
ALTER TABLE reference_table ADD CONSTRAINT fkey_ref_to_local FOREIGN KEY(r1) REFERENCES local_table(l1);
COMMIT;

-- replicate reference table to coordinator

SELECT master_add_node('localhost', :master_port, groupId => 0);

-- show that we are checking for foreign key constraint while defining

INSERT INTO reference_table VALUES (3);

-- this should fail
ALTER TABLE reference_table ADD CONSTRAINT fkey_ref_to_local FOREIGN KEY(r1) REFERENCES local_table(l1);

INSERT INTO local_table VALUES (3);

-- this should work
ALTER TABLE reference_table ADD CONSTRAINT fkey_ref_to_local FOREIGN KEY(r1) REFERENCES local_table(l1);

-- show that we are checking for foreign key constraint after defining

-- this should fail
INSERT INTO reference_table VALUES (4);

INSERT INTO local_table VALUES (4);

-- we do not support ON DELETE/UPDATE CASCADE behaviour in "ALTER TABLE ADD fkey reference_table (to local_table)" commands
ALTER TABLE reference_table ADD CONSTRAINT fkey_ref_to_local FOREIGN KEY(r1) REFERENCES local_table(l1) ON DELETE CASCADE;
ALTER TABLE reference_table ADD CONSTRAINT fkey_ref_to_local FOREIGN KEY(r1) REFERENCES local_table(l1) ON UPDATE CASCADE;

-- this should work
INSERT INTO reference_table VALUES (4);

-- we do not support ALTER TABLE DROP CONSTRAINT fkey between local & reference table
-- within a transaction block, below block should fail
BEGIN;
ALTER TABLE reference_table DROP CONSTRAINT fkey_ref_to_local;
COMMIt;

-- show that we do not allow removing coordinator when we have a fkey constraint
-- between a coordinator local table and a reference table
SELECT master_remove_node('localhost', :master_port);

-- show that we support drop constraint
ALTER TABLE reference_table DROP CONSTRAINT fkey_ref_to_local;

ALTER TABLE reference_table ADD CONSTRAINT fkey_ref_to_local FOREIGN KEY(r1) REFERENCES local_table(l1);

-- show that drop table errors as expected
DROP TABLE local_table;

-- this should work
DROP TABLE local_table CASCADE;

-- drop reference_table finally
DROP TABLE reference_table;

-- TODO: drop them together after fixing the bug

-- show that we can add foreign key constraint from/to a reference table that
-- needs to be escaped

CREATE TABLE local_table(l1 int primary key);
CREATE TABLE "reference'table"(r1 int primary key);
SELECT create_reference_table('reference''table');

-- replicate reference table to coordinator
SELECT master_add_node('localhost', :master_port, groupId => 0);

-- foreign key from local table to reference table --

-- these should work
ALTER TABLE local_table ADD CONSTRAINT fkey_local_to_ref FOREIGN KEY(l1) REFERENCES "reference'table"(r1);
INSERT INTO "reference'table" VALUES (21);
INSERT INTO local_table VALUES (21);
-- this should fail with an appropriate error message like we do for 
-- reference tables that do not need to be escaped
INSERT INTO local_table VALUES (22);

-- drop constraint for next commands
ALTER TABLE local_table DROP CONSTRAINT fkey_local_to_ref;

-- these should also work
ALTER TABLE "reference'table" ADD CONSTRAINT fkey_ref_to_local FOREIGN KEY(r1) REFERENCES local_table(l1);
INSERT INTO local_table VALUES (23);
INSERT INTO "reference'table" VALUES (23);
-- this should fail with an appropriate error message like we do with 
-- reference tables that do not need to be escaped
INSERT INTO local_table VALUES (24);

-- drop tables finally
DROP TABLE local_table;
DROP TABLE "reference'table";

--- CREATE TABLE commands defining fkey constraint between local tables and reference tables ---

-- remove master node from pg_dist_node for next tests to show that 
-- behaviour does not need us to add coordinator to pg_dist_node priorly,
-- as it is not implemented in the ideal way (for now)
SELECT master_remove_node('localhost', :master_port);

-- create tables
CREATE TABLE reference_table (r1 int);
CREATE TABLE local_table (l1 int REFERENCES reference_table(r1));

-- actually, we did not implement upgrading "a local table referenced by another local table"
-- to a reference table yet -in an ideal way-. But it should work producing a warning
SELECT create_reference_table("reference_table");

-- show that we are checking for foreign key constraint after defining

-- this should fail
INSERT INTO local_table VALUES (31);

INSERT INTO reference_table VALUES (31);

-- this should work
INSERT INTO local_table VALUES (31);

-- that amount of test for CREATE TABLE commands defining an fkey constraint
-- from a local table to a reference table is sufficient it is already tested
-- in some other regression tests already

-- drop tables finally
DROP TABLE local_table;
DROP TABLE "reference'table";

-- create tables
CREATE TABLE local_table (l1 int);
CREATE TABLE reference_table (r1 int REFERENCES local_table(l1));

-- we did not implement upgrading "a local table referencing to another 
-- local table" to a reference table yet.
-- this should fail
SELECT create_reference_table("reference_table");

-- finalize the test, clear the schema created for this test --
DROP SCHEMA fkey_reference_local_table;
