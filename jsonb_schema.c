#include "postgres.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/json.h"
#include "utils/jsonb.h"
#include "utils/jsonfuncs.h"
#include "catalog/pg_type.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

PG_FUNCTION_INFO_V1(jsonb_extract_schema);
PG_FUNCTION_INFO_V1(jsonb_add_schema);

typedef  enum {
	jsbNull = 1, /*  avoid '\0' character */
	jsbObject,
	jsbString,
	jsbNumeric,
	jsbBool,
	jsbTuple, /* fixed length array of elements of different types */
	jsbArray, /* varying length array of elements of the same type */
	jsbEnd,   /* end of object/array indicator */
} JsonbSchemaTag;

/* Store array/string length as 1/3/5 bytes */
static void
jsonb_encode_length(StringInfo str, unsigned len)
{
	char buf[5];
	if (len < 0xFE)
	{
	    appendStringInfoCharMacro(str, (char)len);
	}
	else if (len <= 0xFFFF)
	{
		buf[0] = (char)0xFE;
		buf[1] = (char)(len >> 8);
		buf[2] = (char)len;
		appendBinaryStringInfo(str, buf, 3);
	}
	else
	{
		buf[0] = (char)0xFF;
		buf[1] = (char)(len >> 24);
		buf[2] = (char)(len >> 16);
		buf[3] = (char)(len >> 8);
		buf[4] = (char)len;
		appendBinaryStringInfo(str, buf, 5);
	}
}

static unsigned
jsonb_decode_length(char** data)
{
	unsigned len = *(*data)++ & 0xFF;
	if (len < 0xFE)
	{
		return len;
	}
	else if (len == 0xFE)
	{
		len = ((**data & 0xFF) << 8) | (*(*data + 1) & 0xFF);
		*data += 2;
	}
	else
	{
		len = ((**data & 0xFF) << 24) | ((*(*data + 1) & 0xFF) << 16) | ((*(*data + 2) & 0xFF) << 8) | (*(*data + 3) & 0xFF);
		*data += 4;
	}
	return len;
}

static void jsonb_extract_object_schema(JsonbIterator **it, StringInfo schema, StringInfo data, JsonbValue* v, JsonbIteratorToken type);

static void
jsonb_extract_schema_recursive(JsonbIterator **it, StringInfo schema, StringInfo data)
{
	JsonbValue v;
	JsonbIteratorToken type = JsonbIteratorNext(it, &v, false);
	jsonb_extract_object_schema(it, schema, data, &v, type);
}

static void
jsonb_append_value(StringInfo schema, StringInfo data, JsonbValue* v)
{
	switch (v->type)
	{
	  case jbvNull:
	    appendStringInfoCharMacro(schema, jsbNull);
		break;
	  case jbvString:
	    appendStringInfoCharMacro(schema, jsbString);
		jsonb_encode_length(data, v->val.string.len);
		appendBinaryStringInfo(data, v->val.string.val, v->val.string.len);
		break;
	  case jbvNumeric:
	  {
		  char* num_str = DatumGetCString(DirectFunctionCall1(numeric_out,
															  PointerGetDatum(v->val.numeric)));
		  size_t num_str_len = strlen(num_str);
		  appendStringInfoCharMacro(schema, jsbNumeric);
		  jsonb_encode_length(data, num_str_len);
		  appendBinaryStringInfo(data, num_str, num_str_len);
		  pfree(num_str);
		  break;
	  }
	  case jbvBool:
	    appendStringInfoCharMacro(schema, jsbBool);
	    appendStringInfoCharMacro(data, v->val.boolean ? 't' : 'f');
		break;
	  default:
	    Assert(false);
	}
}


static void
jsonb_extract_object_schema(JsonbIterator **it, StringInfo schema, StringInfo data, JsonbValue* v, JsonbIteratorToken type)
{
	switch (type)
	{
	  case WJB_BEGIN_ARRAY:
	  {
		  int arrayStart = schema->len; /* Start of array schema defintion */
		  int schemaSize = 0; /* zero is used as indicator of first element */
		  bool isTuple = false; /* true is array elements has different schemas */

		  appendStringInfoCharMacro(schema, jsbArray);
		  jsonb_encode_length(data, v->val.array.nElems);

		  while ((type = JsonbIteratorNext(it, v, false)) !=  WJB_END_ARRAY)
		  {
			  int schemaStart = schema->len; /* remember array element schema start position */
			  jsonb_extract_object_schema(it, schema, data, v, type);
			  /*
			   * If all array elements has the same schema, then we store this schema just once (jsbArray).
			   * Otherwise (jsbTuple) we store schemas for each element terminated with jsbEnd byte.
			   */
			  if (!isTuple) /* If we found difference in element schema, no need to repeat this check */
			  {
				  if (schemaSize != 0) /* schemaSize is zero for first element */
				  {
					  isTuple = schema->len - schemaStart != schemaSize /* length of schemas is different... */
						  || memcmp(&schema->data[arrayStart+1], &schema->data[schemaStart], schemaSize) != 0; /* ... or content */
				  }
				  else
				  {
					  schemaSize = schema->len - schemaStart; /* remember length of schema of first element: used in schemas comparison */
				  }
			  }
		  }
		  if (!isTuple)
		  {
			  schema->len = arrayStart + schemaSize + 1; /* keep just one schema */
		  }
		  else
		  {
			  schema->data[arrayStart] = jsbTuple; /* mark it as tuple... */
			  appendStringInfoCharMacro(schema, jsbEnd); /* ... and add terminator */
		  }
		  break;
	  }
	  case WJB_BEGIN_OBJECT:
	  {
		  appendStringInfoCharMacro(schema, jsbObject);
		  while ((type = JsonbIteratorNext(it, v, false)) != WJB_END_OBJECT)
		  {
			  Assert(type == WJB_KEY);
			  jsonb_extract_object_schema(it, schema, data, v, type);
		  }
		  appendStringInfoCharMacro(schema, 0); /* empty key name */
		  appendStringInfoCharMacro(schema, jsbEnd);
		  break;
	  }
	  case WJB_KEY:
	  {
		  jsonb_encode_length(schema, v->val.string.len);
		  appendBinaryStringInfo(schema, v->val.string.val, v->val.string.len);
		  type = JsonbIteratorNext(it, v, false);
		  if (type == WJB_VALUE) /* scalar value */
		  {
			  jsonb_append_value(schema, data, v);
		  }
		  else /* compound value */
		  {
			  Assert(type == WJB_BEGIN_OBJECT || type == WJB_BEGIN_ARRAY);
			  jsonb_extract_object_schema(it, schema, data, v, type);
		  }
		  break;
	  }
	  case WJB_ELEM:
	  {
		  jsonb_append_value(schema, data, v);
		  break;
	  }
	  default:
		elog(ERROR, "unknown jsonb iterator token type %d", type);
	}
}

/*
 * Split JSONB object into schema and data
 */
Datum
jsonb_extract_schema(PG_FUNCTION_ARGS)
{
	Jsonb *in = PG_GETARG_JSONB_P(0);
	StringInfoData schema;
	StringInfoData data;
	JsonbIterator *it = JsonbIteratorInit(&in->root);
	Datum values[2];
	bool isnull[2];
	TupleDesc resultTupleDesc;

	get_call_result_type(fcinfo, NULL, &resultTupleDesc);
	initStringInfo(&schema);
	initStringInfo(&data);
	schema.len = VARHDRSZ;
	data.len = VARHDRSZ;
	jsonb_extract_schema_recursive(&it, &schema, &data);
	SET_VARSIZE(schema.data, schema.len);
	SET_VARSIZE(data.data, data.len);
	values[0] = PointerGetDatum(schema.data);
	values[1] = PointerGetDatum(data.data);
	isnull[0] = isnull[1] = false;
	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(resultTupleDesc, values, isnull)));
}


static JsonbValue *
jsonb_add_schema_recursive(JsonbParseState **state, JsonbIteratorToken token, char** schema, char** data)
{
	JsonbValue v;
	int len;
	char* arraySchema;
	char* num_str;

	switch (*(*schema)++)
	{
	  case jsbNull:
		v.type = jbvNull;
		return pushJsonbValue(state, token, &v);
	  case jsbBool:
		v.type = jbvBool;
		v.val.boolean = *(*data)++ == 't';
		return pushJsonbValue(state, token, &v);
	  case jsbTuple:
		len = jsonb_decode_length(data);
		pushJsonbValue(state, WJB_BEGIN_ARRAY, NULL);
		while (**schema != jsbEnd)
		{
			jsonb_add_schema_recursive(state, WJB_ELEM, schema, data);
			len -= 1;
		}
		Assert(len == 0);
		*schema += 1; /* skip jsbEnd */
		return pushJsonbValue(state, WJB_END_ARRAY, NULL);
	  case jsbArray:
		len = jsonb_decode_length(data);
		arraySchema = *schema;
		pushJsonbValue(state, WJB_BEGIN_ARRAY, NULL);
		while (--len >= 0)
		{
		    *schema = arraySchema; /* reset pointer to element schema */
			jsonb_add_schema_recursive(state, WJB_ELEM, schema, data);
		}
		return pushJsonbValue(state, WJB_END_ARRAY, NULL);
	  case jsbObject:
		pushJsonbValue(state, WJB_BEGIN_OBJECT, NULL);
		while (true)
		{
			v.type = jbvString;
			v.val.string.len = jsonb_decode_length(schema);
			v.val.string.val = *schema;
			*schema += v.val.string.len;
			if (**schema == jsbEnd)
			{
				*schema += 1; /* skip jsbEnd */
				return pushJsonbValue(state, WJB_END_OBJECT, NULL);
			}
			pushJsonbValue(state, WJB_KEY, &v);
			jsonb_add_schema_recursive(state, WJB_VALUE, schema, data);
		}
	  case jsbString:
		v.type = jbvString;
		v.val.string.len = jsonb_decode_length(data);
		v.val.string.val = *data;
		*data += v.val.string.len;
		return pushJsonbValue(state, token, &v);
	  case jsbNumeric:
		len = jsonb_decode_length(data);
		num_str = (char*)palloc(len+1);
		memcpy(num_str, *data, len);
		num_str[len] = '\0';
		v.type = jbvNumeric;
		v.val.numeric = DatumGetNumeric(DirectFunctionCall3(numeric_in,
															CStringGetDatum(num_str),
															ObjectIdGetDatum(InvalidOid),
															Int32GetDatum(-1)));
		pfree(num_str);
		*data += len;
		return pushJsonbValue(state, token, &v);
	  default:
		Assert(false);
	}
	return NULL;
}

/*
 * Combine schema and data to restore original JSONB object
 */
Datum
jsonb_add_schema(PG_FUNCTION_ARGS)
{
	char* schema = VARDATA(PG_GETARG_BYTEA_P(0));
	char* data = VARDATA(PG_GETARG_BYTEA_P(1));
	JsonbParseState *state = NULL;
	JsonbValue *res = jsonb_add_schema_recursive(&state, WJB_VALUE, &schema, &data);
	PG_RETURN_POINTER(JsonbValueToJsonb(res));
}
