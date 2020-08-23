create extension jsonb_schema;

create table foo(obj jsonb);
-- two schema are equal
insert into foo values (jsonb_pack('{"a": {"b":{"c": "foo"}}, "arr":[1,2,3]}'::jsonb));
insert into foo values (jsonb_pack('{"a": {"b":{"c": "fas"}}, "arr":[1,2,3,5,6]}'::jsonb));

-- tuple
insert into foo values (jsonb_pack('{"tup": [1, {"a":1,"b":2}, "str"]}'::jsonb));
insert into foo values (jsonb_pack('{"tup": [1, {"a":2,"b":3}, "str"]}'::jsonb));
insert into foo values (jsonb_pack('[1, {"a":4,"b":5}, 2]'::jsonb));
insert into foo values (jsonb_pack('[1, 2, "str"]'::jsonb));

-- raw scalar
insert into foo values (jsonb_pack('[1]'::jsonb));
insert into foo values (jsonb_pack('["str"]'::jsonb));

select jsonb_unpack(obj) from foo;
select * from foo;
select * from jsonb_schemes;
