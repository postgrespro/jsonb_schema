This extension allows to reduce size of jsonb objects by storing it's schema separately from data.
It is assumed that there are not so much different schema used by applications.
So storing schema separately in `jsonb_schemes` table and keeping only it's ID in JSONB object
allows to significantly reduce space used by JSONB objects.

There are just two functions provided by this extension:
`jsonb_pack` and `jsonb_unpack`. Both take JSONB objects and return JSONB object.

`jsonb_pack` separates schema from data and stores schema in `jsonb_schemes` table (if not present yet).
Then it constructs JSONB array [schema_id,obj_data]

`jsonb_pack` takes JSONB array [schema_id,obj_data], retrieve schema from `jsonb_schemes` table
and merge it with object data, restoring original JSONB object.

