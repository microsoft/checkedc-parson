/*
 Checked C Parson
 Copyright (c) Microsoft Corporation. All rights reserved.
 Portions Copyright (c) 2012 - 2017 Krzysztof Gabis
 https://github.com/kgabis/parson/
 Licensed under the MIT License

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
*/

#ifndef parson_parson_h
#define parson_parson_h

#ifdef __cplusplus
extern "C"
{
#endif

#pragma CHECKED_SCOPE push
#pragma CHECKED_SCOPE on

#include <stddef.h>   /* size_t */

/* Types and enums */
typedef struct json_object_t JSON_Object;
typedef struct json_array_t  JSON_Array;
typedef struct json_value_t  JSON_Value;

enum json_value_type {
    JSONError   = -1,
    JSONNull    = 1,
    JSONString  = 2,
    JSONNumber  = 3,
    JSONObject  = 4,
    JSONArray   = 5,
    JSONBoolean = 6
};
typedef int JSON_Value_Type;

enum json_result_t {
    JSONSuccess = 0,
    JSONFailure = -1
};
typedef int JSON_Status;


typedef _Itype_for_any(T) void * tmp_malloc_fun(size_t s) : byte_count(s) itype(_Array_ptr<T>);
typedef _Itype_for_any(T) void tmp_free_fun(void * : byte_count(0) itype(_Array_ptr<T>));
typedef _Ptr<tmp_malloc_fun> JSON_Malloc_Function;
typedef _Ptr<tmp_free_fun> JSON_Free_Function;


/* Call only once, before calling any other function from parson API. If not called, malloc and free
   from stdlib will be used for all allocations */
void json_set_allocation_functions(JSON_Malloc_Function malloc_fun, JSON_Free_Function free_fun);

/* Sets if slashes should be escaped or not when serializing JSON. By default slashes are escaped.
 This function sets a global setting and is not thread safe. */
void json_set_escape_slashes(int escape_slashes);

/* Parses first JSON value in a file, returns NULL in case of error */
JSON_Value * json_parse_file(const char *filename : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>);

/* Parses first JSON value in a file and ignores comments (/ * * / and //),
   returns NULL in case of error */
JSON_Value * json_parse_file_with_comments(const char *filename : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>);

/*  Parses first JSON value in a string, returns NULL in case of error */
JSON_Value * json_parse_string(const char *string : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>);

/*  Parses first JSON value in a string and ignores comments (/ * * / and //),
    returns NULL in case of error */
JSON_Value * json_parse_string_with_comments(const char *string : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>);

/* Serialization */
size_t      json_serialization_size(const JSON_Value *value : itype(_Ptr<const JSON_Value>)); /* returns 0 on fail */
JSON_Status json_serialize_to_buffer(const JSON_Value *value : itype(_Ptr<const JSON_Value>), char *buf : itype(_Nt_array_ptr<char>) byte_count(buf_size_in_bytes), size_t buf_size_in_bytes);
JSON_Status json_serialize_to_file(const JSON_Value *value : itype(_Ptr<const JSON_Value>), const char *filename : itype(_Nt_array_ptr<const char>));
char *      json_serialize_to_string(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Nt_array_ptr<char>);

/* Pretty serialization */
size_t      json_serialization_size_pretty(const JSON_Value *value : itype(_Ptr<const JSON_Value>)); /* returns 0 on fail */
JSON_Status json_serialize_to_buffer_pretty(const JSON_Value *value : itype(_Ptr<const JSON_Value>), char *buf : itype(_Nt_array_ptr<char>) byte_count(buf_size_in_bytes), size_t buf_size_in_bytes);
JSON_Status json_serialize_to_file_pretty(const JSON_Value *value : itype(_Ptr<const JSON_Value>), const char *filename : itype(_Nt_array_ptr<const char>));
char *      json_serialize_to_string_pretty(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Nt_array_ptr<char>);

void        json_free_serialized_string(char *string : itype(_Nt_array_ptr<char>)); /* frees string from json_serialize_to_string and json_serialize_to_string_pretty */

/* Comparing */
int  json_value_equals(const JSON_Value *a : itype(_Ptr<const JSON_Value>), const JSON_Value *b : itype(_Ptr<const JSON_Value>));

/* Validation
   This is *NOT* JSON Schema. It validates json by checking if object have identically
   named fields with matching types.
   For example schema {"name":"", "age":0} will validate
   {"name":"Joe", "age":25} and {"name":"Joe", "age":25, "gender":"m"},
   but not {"name":"Joe"} or {"name":"Joe", "age":"Cucumber"}.
   In case of arrays, only first value in schema is checked against all values in tested array.
   Empty objects ({}) validate all objects, empty arrays ([]) validate all arrays,
   null validates values of every type.
 */
JSON_Status json_validate(const JSON_Value *schema : itype(_Ptr<const JSON_Value>), const JSON_Value *value : itype(_Ptr<const JSON_Value>));

/*
 * JSON Object
 */
JSON_Value  * json_object_get_value  (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>);
const char  * json_object_get_string (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Nt_array_ptr<const char>);
JSON_Object * json_object_get_object (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Object>);
JSON_Array  * json_object_get_array  (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Array>);
double        json_object_get_number (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)); /* returns 0 on fail */
int           json_object_get_boolean(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)); /* returns -1 on fail */

/* dotget functions enable addressing values with dot notation in nested objects,
 just like in structs or c++/java/c# objects (e.g. objectA.objectB.value).
 Because valid names in JSON can contain dots, some values may be inaccessible
 this way. */
JSON_Value  * json_object_dotget_value  (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>);
const char  * json_object_dotget_string (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Nt_array_ptr<const char>);
JSON_Object * json_object_dotget_object (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Object>);
JSON_Array  * json_object_dotget_array  (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Array>);
double        json_object_dotget_number (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)); /* returns 0 on fail */
int           json_object_dotget_boolean(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)); /* returns -1 on fail */

/* Functions to get available names */
size_t        json_object_get_count   (const JSON_Object *object : itype(_Ptr<const JSON_Object>));
const char  * json_object_get_name    (const JSON_Object *object : itype(_Ptr<const JSON_Object>), size_t index) : itype(_Nt_array_ptr<const char>);
JSON_Value  * json_object_get_value_at(const JSON_Object *object : itype(_Ptr<const JSON_Object>), size_t index) : itype(_Ptr<JSON_Value>);
JSON_Value  * json_object_get_wrapping_value(const JSON_Object *object : itype(_Ptr<const JSON_Object>)) : itype(_Ptr<JSON_Value>);

/* Functions to check if object has a value with a specific name. Returned value is 1 if object has
 * a value and 0 if it doesn't. dothas functions behave exactly like dotget functions. */
int json_object_has_value        (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>));
int json_object_has_value_of_type(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), JSON_Value_Type type);

int json_object_dothas_value        (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>));
int json_object_dothas_value_of_type(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), JSON_Value_Type type);

/* Creates new name-value pair or frees and replaces old value with a new one.
 * json_object_set_value does not copy passed value so it shouldn't be freed afterwards. */
JSON_Status json_object_set_value(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), JSON_Value *value : itype(_Ptr<JSON_Value>));
JSON_Status json_object_set_string(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), const char *string : itype(_Nt_array_ptr<const char>));
JSON_Status json_object_set_number(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), double number);
JSON_Status json_object_set_boolean(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), int boolean);
JSON_Status json_object_set_null(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>));

/* Works like dotget functions, but creates whole hierarchy if necessary.
 * json_object_dotset_value does not copy passed value so it shouldn't be freed afterwards. */
JSON_Status json_object_dotset_value(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), JSON_Value *value : itype(_Ptr<JSON_Value>));
JSON_Status json_object_dotset_string(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), const char *string : itype(_Nt_array_ptr<const char>));
JSON_Status json_object_dotset_number(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), double number);
JSON_Status json_object_dotset_boolean(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), int boolean);
JSON_Status json_object_dotset_null(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>));

/* Frees and removes name-value pair */
JSON_Status json_object_remove(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>));

/* Works like dotget function, but removes name-value pair only on exact match. */
JSON_Status json_object_dotremove(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *key : itype(_Nt_array_ptr<const char>));

/* Removes all name-value pairs in object */
JSON_Status json_object_clear(JSON_Object *object : itype(_Ptr<JSON_Object>));

/*
 *JSON Array
 */
JSON_Value  * json_array_get_value  (const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) : itype(_Ptr<JSON_Value>);
const char  * json_array_get_string (const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) : itype(_Nt_array_ptr<const char>);
JSON_Object * json_array_get_object (const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) : itype(_Ptr<JSON_Object>);
JSON_Array  * json_array_get_array  (const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) : itype(_Ptr<JSON_Array>);
double        json_array_get_number (const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index); /* returns 0 on fail */
int           json_array_get_boolean(const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index); /* returns -1 on fail */
size_t        json_array_get_count  (const JSON_Array *array : itype(_Ptr<const JSON_Array>));
JSON_Value  * json_array_get_wrapping_value(const JSON_Array *array : itype(_Ptr<const JSON_Array>)) : itype(_Ptr<JSON_Value>);

/* Frees and removes value at given index, does nothing and returns JSONFailure if index doesn't exist.
 * Order of values in array may change during execution.  */
JSON_Status json_array_remove(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i);

/* Frees and removes from array value at given index and replaces it with given one.
 * Does nothing and returns JSONFailure if index doesn't exist.
 * json_array_replace_value does not copy passed value so it shouldn't be freed afterwards. */
JSON_Status json_array_replace_value(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i, JSON_Value *value : itype(_Ptr<JSON_Value>));
JSON_Status json_array_replace_string(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i, const char* string : itype(_Nt_array_ptr<const char>));
JSON_Status json_array_replace_number(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i, double number);
JSON_Status json_array_replace_boolean(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i, int boolean);
JSON_Status json_array_replace_null(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i);

/* Frees and removes all values from array */
JSON_Status json_array_clear(JSON_Array *array : itype(_Ptr<JSON_Array>));

/* Appends new value at the end of array.
 * json_array_append_value does not copy passed value so it shouldn't be freed afterwards. */
JSON_Status json_array_append_value(JSON_Array *array : itype(_Ptr<JSON_Array>), JSON_Value *value : itype(_Ptr<JSON_Value>));
JSON_Status json_array_append_string(JSON_Array *array : itype(_Ptr<JSON_Array>), const char *string : itype(_Nt_array_ptr<const char>));
JSON_Status json_array_append_number(JSON_Array *array : itype(_Ptr<JSON_Array>), double number);
JSON_Status json_array_append_boolean(JSON_Array *array : itype(_Ptr<JSON_Array>), int boolean);
JSON_Status json_array_append_null(JSON_Array *array : itype(_Ptr<JSON_Array>));

/*
 *JSON Value
 */
JSON_Value * json_value_init_object (void)                                                    : itype(_Ptr<JSON_Value>);
JSON_Value * json_value_init_array  (void)                                                    : itype(_Ptr<JSON_Value>);
JSON_Value * json_value_init_string (const char *string : itype(_Nt_array_ptr<const char>))   : itype(_Ptr<JSON_Value>); /* copies passed string */
JSON_Value * json_value_init_number (double number)                                           : itype(_Ptr<JSON_Value>);
JSON_Value * json_value_init_boolean(int boolean)                                             : itype(_Ptr<JSON_Value>);
JSON_Value * json_value_init_null   (void)                                                    : itype(_Ptr<JSON_Value>);
JSON_Value * json_value_deep_copy   (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Value>);
void         json_value_free        (JSON_Value *value : itype(_Ptr<JSON_Value>));

JSON_Value_Type json_value_get_type   (const JSON_Value *value : itype(_Ptr<const JSON_Value>));
JSON_Object *   json_value_get_object (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Object>);
JSON_Array  *   json_value_get_array  (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Array>);
const char  *   json_value_get_string (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Nt_array_ptr<const char>);
double          json_value_get_number (const JSON_Value *value : itype(_Ptr<const JSON_Value>));
int             json_value_get_boolean(const JSON_Value *value : itype(_Ptr<const JSON_Value>));
JSON_Value  *   json_value_get_parent (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Value>);

/* Same as above, but shorter */
JSON_Value_Type json_type   (const JSON_Value *value : itype(_Ptr<const JSON_Value>));
JSON_Object *   json_object (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Object>);
JSON_Array  *   json_array  (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Array>);
const char  *   json_string (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Nt_array_ptr<const char>);
double          json_number (const JSON_Value *value : itype(_Ptr<const JSON_Value>));
int             json_boolean(const JSON_Value *value : itype(_Ptr<const JSON_Value>));

#pragma CHECKED_SCOPE pop

#ifdef __cplusplus
}
#endif

#endif
