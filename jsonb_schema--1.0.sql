/* contrib/jsonb_schema/jsonb_schema.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "create extension jsonb_schema" to load this file. \quit

create table jsonb_schemes(id serial, schema bytea);
create unique index on jsonb_schemes(schema) include(id);
create unique index on jsonb_schemes(id) include(schema);

create function jsonb_extract_schema(obj jsonb, out schema bytea, out data bytea) as 'MODULE_PATHNAME' language C strict;
create function jsonb_add_schema(schema bytea, data bytea) returns jsonb as 'MODULE_PATHNAME' language C strict;


create function jsonb_pack(obj jsonb) returns jsonb as $$
declare
	obj_schema bytea;
	obj_data bytea;
	schema_id integer;
begin
	select * from  jsonb_extract_schema(obj) into obj_schema,obj_data;
	with ins as (insert into jsonb_schemes (schema) values (obj_schema) on conflict(schema) do nothing returning id) select coalesce((select id from ins),(select id from jsonb_schemes where schema=obj_schema)) into schema_id;
	return jsonb_build_array(schema_id,encode(obj_data, 'escape'));
end;
$$ language plpgsql strict parallel safe;

create function jsonb_unpack(obj jsonb) returns jsonb as $$
	select jsonb_add_schema(schema,decode(obj->>1, 'escape')) from jsonb_schemes where id=(obj->>0)::integer;
$$ language sql strict parallel safe;

