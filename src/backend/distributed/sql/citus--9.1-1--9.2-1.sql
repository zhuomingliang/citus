ALTER TABLE pg_catalog.pg_dist_colocation ADD distributioncolumncollation oid;
UPDATE pg_catalog.pg_dist_colocation dc SET distributioncolumncollation = t.typcollation
	FROM pg_catalog.pg_type t WHERE t.oid = dc.distributioncolumntype;
UPDATE pg_catalog.pg_dist_colocation dc SET distributioncolumncollation = 0 WHERE distributioncolumncollation IS NULL;
ALTER TABLE pg_catalog.pg_dist_colocation ALTER COLUMN distributioncolumncollation SET NOT NULL;

DROP INDEX pg_dist_colocation_configuration_index;
-- distributioncolumntype should be listed first so that this index can be used for looking up reference tables' colocation id
CREATE INDEX pg_dist_colocation_configuration_index
ON pg_dist_colocation USING btree(distributioncolumntype, shardcount, replicationfactor, distributioncolumncollation);

-- infrastructure for pulling up intermediate records for aggregation on coordinator
CREATE TYPE public.array_fold_ordering AS (index int, sortop oid, nulls_first bool);
ALTER TYPE public.array_fold_ordering SET SCHEMA pg_catalog;
CREATE FUNCTION pg_catalog.coord_fold_array(oid, record[], bool, bool[], pg_catalog.array_fold_ordering[], anyelement)
RETURNS anyelement
AS 'MODULE_PATHNAME'
LANGUAGE C PARALLEL SAFE;
COMMENT ON FUNCTION pg_catalog.coord_fold_array(oid, record[], bool, bool[], pg_catalog.array_fold_ordering[], anyelement)
    IS 'apply aggregate to records in array';
REVOKE ALL ON FUNCTION pg_catalog.coord_fold_array FROM PUBLIC;
GRANT EXECUTE ON FUNCTION pg_catalog.coord_fold_array TO PUBLIC;

