SET citus.next_shard_id TO 20020000;

CREATE USER functionuser;
SELECT run_command_on_workers($$CREATE USER functionuser;$$);

CREATE SCHEMA function_tests AUTHORIZATION functionuser;

SET search_path TO function_tests;
SET citus.shard_count TO 4;


-- Create and distribute a simple function
CREATE FUNCTION add(integer, integer) RETURNS integer
    AS 'select $1 + $2;'
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;
SELECT create_distributed_function('add(int,int)', '$1');
SELECT * FROM run_command_on_workers('SELECT function_tests.add(2,3);') ORDER BY 1,2;


-- Test some combination of functions without ddl propagation
-- This will prevent the workers from having those types created. They are
-- created just-in-time on function distribution
SET citus.enable_ddl_propagation TO off;

CREATE TYPE dup_result AS (f1 int, f2 text);

CREATE FUNCTION dup(int) RETURNS dup_result
    AS $$ SELECT $1, CAST($1 AS text) || ' is text' $$
    LANGUAGE SQL;

SELECT create_distributed_function('dup(int)', '$1');
SELECT * FROM run_command_on_workers('SELECT function_tests.dup(42);') ORDER BY 1,2;

CREATE FUNCTION add_with_param_names(val1 integer, val2 integer) RETURNS integer
    AS 'select $1 + $2;'
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

CREATE FUNCTION add_without_param_names(integer, integer) RETURNS integer
    AS 'select $1 + $2;'
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

CREATE FUNCTION add_mixed_param_names(integer, val1 integer) RETURNS integer
    AS 'select $1 + $2;'
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

-- postgres doesn't accept parameter names in the regprocedure input
SELECT create_distributed_function('add_with_param_names(val1 int, int)', 'val1');

-- invalid distribution_arg_name
SELECT create_distributed_function('add_with_param_names(int, int)', distribution_arg_name:='test');
SELECT create_distributed_function('add_with_param_names(int, int)', distribution_arg_name:='int');

-- invalid distribution_arg_index
SELECT create_distributed_function('add_with_param_names(int, int)', '$0');
SELECT create_distributed_function('add_with_param_names(int, int)', '$-1');
SELECT create_distributed_function('add_with_param_names(int, int)', '$-10');
SELECT create_distributed_function('add_with_param_names(int, int)', '$3');
SELECT create_distributed_function('add_with_param_names(int, int)', '$1a');

-- non existing column name
SELECT create_distributed_function('add_with_param_names(int, int)', 'aaa');

-- NULL function
SELECT create_distributed_function(NULL);

-- NULL colocate_with
SELECT create_distributed_function('add_with_param_names(int, int)', '$1', NULL);

-- empty string distribution_arg_index
SELECT create_distributed_function('add_with_param_names(int, int)', '');

-- valid distribution with distribution_arg_name
SELECT create_distributed_function('add_with_param_names(int, int)', distribution_arg_name:='val1');

-- valid distribution with distribution_arg_name -- case insensitive
SELECT create_distributed_function('add_with_param_names(int, int)', distribution_arg_name:='VaL1');

-- valid distribution with distribution_arg_index
SELECT create_distributed_function('add_with_param_names(int, int)','$1');

-- a function cannot be colocated with a table that is not "streaming" replicated 
SET citus.shard_replication_factor TO 2;
CREATE TABLE replicated_table_func_test (a int);
SET citus.replication_model TO "statement";
SELECT create_distributed_table('replicated_table_func_test', 'a');
SELECT create_distributed_function('add_with_param_names(int, int)', '$1', colocate_with:='replicated_table_func_test');

-- a function cannot be colocated with a different distribution argument type
SET citus.shard_replication_factor TO 1;
CREATE TABLE replicated_table_func_test_2 (a bigint);
SET citus.replication_model TO "streaming";
SELECT create_distributed_table('replicated_table_func_test_2', 'a');
SELECT create_distributed_function('add_with_param_names(int, int)', 'val1', colocate_with:='replicated_table_func_test_2');

-- colocate_with cannot be used without distribution key
SELECT create_distributed_function('add_with_param_names(int, int)', colocate_with:='replicated_table_func_test_2');

-- a function cannot be colocated with a local table
CREATE TABLE replicated_table_func_test_3 (a bigint);
SELECT create_distributed_function('add_with_param_names(int, int)', 'val1', colocate_with:='replicated_table_func_test_3');

-- a function cannot be colocated with a reference table
SELECT create_reference_table('replicated_table_func_test_3');
SELECT create_distributed_function('add_with_param_names(int, int)', 'val1', colocate_with:='replicated_table_func_test_3');

-- finally, colocate the function with a distributed table
SET citus.shard_replication_factor TO 1;
CREATE TABLE replicated_table_func_test_4 (a int);
SET citus.replication_model TO "streaming";
SELECT create_distributed_table('replicated_table_func_test_4', 'a');
SELECT create_distributed_function('add_with_param_names(int, int)', '$1', colocate_with:='replicated_table_func_test_4');

-- show that the colocationIds are the same
SELECT pg_dist_partition.colocationid = objects.colocationid as table_and_function_colocated
FROM pg_dist_partition, citus.pg_dist_object as objects 
WHERE pg_dist_partition.logicalrelid = 'replicated_table_func_test_4'::regclass AND 
	  objects.objid = 'add_with_param_names(int, int)'::regprocedure;

-- now, re-distributed with the default colocation option, we should still see that the same colocation
-- group preserved, because we're using the default shard creationg settings
SELECT create_distributed_function('add_with_param_names(int, int)', 'val1');
SELECT pg_dist_partition.colocationid = objects.colocationid as table_and_function_colocated
FROM pg_dist_partition, citus.pg_dist_object as objects 
WHERE pg_dist_partition.logicalrelid = 'replicated_table_func_test_4'::regclass AND 
	  objects.objid = 'add_with_param_names(int, int)'::regprocedure;

-- if not paremeters are supplied, we'd see that function doesn't have
-- distribution_argument_index and colocationid
SELECT create_distributed_function('add_mixed_param_names(int, int)');
SELECT distribution_argument_index is NULL, colocationid is NULL from citus.pg_dist_object
WHERE objid = 'add_mixed_param_names(int, int)'::regprocedure;

-- also show that we can use the function
SELECT * FROM run_command_on_workers('SELECT function_tests.add_mixed_param_names(2,3);') ORDER BY 1,2;

-- clear objects
SET client_min_messages TO error; -- suppress cascading objects dropping
DROP SCHEMA function_tests CASCADE;
SELECT run_command_on_workers($$DROP SCHEMA function_tests CASCADE;$$);
DROP USER functionuser;
SELECT run_command_on_workers($$DROP USER functionuser;$$);