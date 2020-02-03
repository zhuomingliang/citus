DROP FUNCTION IF EXISTS pg_catalog.worker_create_schema(jobid bigint);

CREATE FUNCTION pg_catalog.worker_create_schema(jobid bigint, userid oid)
    RETURNS void
    LANGUAGE C STRICT
    AS 'MODULE_PATHNAME', $$worker_create_schema$$;
COMMENT ON FUNCTION pg_catalog.worker_create_schema(bigint, oid)
    IS 'create schema in remote node';

REVOKE ALL ON FUNCTION pg_catalog.worker_create_schema(bigint, oid) FROM PUBLIC;
