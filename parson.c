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
#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif /* _CRT_SECURE_NO_WARNINGS */
#endif /* _MSC_VER */

#pragma CHECKED_SCOPE push
#pragma CHECKED_SCOPE off

#include <ctype.h> /* On Windows this needs a bounds safe interface or to be outside checked scope */
#ifdef isspace
#undef isspace /* Macro causes bounds issues on Linux/Mac systems */
#endif

#include <stdint.h> /* Needed for SIZE_MAX */

#pragma CHECKED_SCOPE on

#include "parson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>


/* Apparently sscanf is not implemented in some "standard" libraries, so don't use it, if you
 * don't have to. */
#define sscanf THINK_TWICE_ABOUT_USING_SSCANF

#define STARTING_CAPACITY 16
#define MAX_NESTING       1000

#define FLOAT_FORMAT "%1.17g" /* do not increase precision without incresing NUM_BUF_SIZE */
#define NUM_BUF_SIZE 64 /* double printed with "%1.17g" shouldn't be longer than 25 bytes so let's be paranoid and use 64 */

#define SIZEOF_TOKEN(a)       (sizeof(a) - 1)
#define SKIP_CHAR(str)        ((*str)++)
#define SKIP_WHITESPACES(str) while (isspace((unsigned char)(**str))) { SKIP_CHAR(str); }
#define MAX(a, b)             ((a) > (b) ? (a) : (b))

#undef malloc
#undef free

#if defined(isnan) && defined(isinf)
#define IS_NUMBER_INVALID(x) (isnan((x)) || isinf((x)))
#else
#define IS_NUMBER_INVALID(x) (((x) * 0.0) != 0.0)
#endif

_Itype_for_any(T) static _Ptr<void*(size_t s)> parson_malloc : itype(_Ptr<_Array_ptr<T> (size_t s) : byte_count(s)>);

_Itype_for_any(T) static _Ptr<void(void*)> parson_free : itype(_Ptr<void (_Array_ptr<T> : byte_count(0))>);

#define parson_malloc(t, sz) (malloc<t>(sz))
#define parson_free(t, p)   (free<t>(p))
// parson_free_dynamic_bounds_cast is only needed in two cases -- related to incomplete types.
#define parson_free_dynamic_bounds_cast(t, p)   (free<t>(_Dynamic_bounds_cast<_Array_ptr<t>>(p, byte_count(0))))
#define parson_free_unchecked(buf) (free(buf))

static _Nt_array_ptr<char> parson_string_malloc(size_t sz) : count(sz) _Unchecked {
  if(sz >= SIZE_MAX)
    return NULL;
  char *p = (char*)parson_malloc(char, sz + 1);
  if (p != NULL)
    p[sz] = 0;
  return _Assume_bounds_cast<_Nt_array_ptr<char>>(p, count(sz));
}

static int parson_escape_slashes = 1;

#define IS_CONT(b) (((unsigned char)(b) & 0xC0) == 0x80) /* is utf-8 continuation byte */

/* Type definitions */
typedef union json_value_value {
    char        *string : itype(_Nt_array_ptr<char>);
    double       number;
    JSON_Object *object : itype(_Ptr<JSON_Object>);
    JSON_Array  *array  : itype(_Ptr<JSON_Array>);
    int          boolean;
    int          null;
} JSON_Value_Value;

struct json_value_t {
    JSON_Value      *parent : itype(_Ptr<JSON_Value>);
    JSON_Value_Type  type;
    JSON_Value_Value value;
};

struct json_object_t {
    JSON_Value  *wrapping_value : itype(_Ptr<JSON_Value>);
    char       **names          : itype(_Array_ptr<_Nt_array_ptr<char>>) count(capacity);
    JSON_Value **values         : itype(_Array_ptr<_Ptr<JSON_Value>>)    count(capacity);
    size_t       count;
    size_t       capacity;
};

struct json_array_t {
    JSON_Value  *wrapping_value : itype(_Ptr<JSON_Value>);
    JSON_Value **items          : itype(_Array_ptr<_Ptr<JSON_Value>>) count(capacity);
    size_t       count;
    size_t       capacity;
};

/* Various */
static _Nt_array_ptr<char> read_file(_Nt_array_ptr<const char> filename);
static void                remove_comments(_Nt_array_ptr<char> string, _Nt_array_ptr<const char> start_token, _Nt_array_ptr<const char> end_token);
static _Nt_array_ptr<char> parson_strndup(_Nt_array_ptr<const char> string : count(n), size_t n);
static _Nt_array_ptr<char> parson_strdup(_Nt_array_ptr<const char> string);
static int                 hex_char_to_int(char c);
static int _Unchecked      parse_utf16_hex(const char* string, unsigned int* result);
static int                 num_bytes_in_utf8_sequence(unsigned char c);
static int                 verify_utf8_sequence(_Nt_array_ptr<const unsigned char> string, _Ptr<int> len); // len is set after, not a constraint on string
static int                 is_valid_utf8(_Nt_array_ptr<const char> string : bounds(string, string + string_len), size_t string_len);
static int                 is_decimal(const char* string : itype(_Nt_array_ptr<const char>) count(length), size_t length);

/* JSON Object */
static _Ptr<JSON_Object> json_object_init(_Ptr<JSON_Value> wrapping_value);
static JSON_Status       json_object_add(_Ptr<JSON_Object> object, _Nt_array_ptr<const char> name, _Ptr<JSON_Value> value);
static JSON_Status       json_object_addn(_Ptr<JSON_Object> object, _Nt_array_ptr<const char> name : count(name_len), size_t name_len, _Ptr<JSON_Value> value);
static JSON_Status       json_object_resize(_Ptr<JSON_Object> object, size_t new_capacity);
static JSON_Value *      json_object_getn_value(_Ptr<const JSON_Object> object, _Nt_array_ptr<const char> name : count(name_len), size_t name_len) : itype(_Ptr<JSON_Value>);
static JSON_Status       json_object_remove_internal(_Ptr<JSON_Object> object, _Nt_array_ptr<const char> name, int free_value);
static JSON_Status       json_object_dotremove_internal(_Ptr<JSON_Object> object, _Nt_array_ptr<const char> name, int free_value);
static void              json_object_free(_Ptr<JSON_Object> object);

/* JSON Array */
static _Ptr<JSON_Array> json_array_init(_Ptr<JSON_Value> wrapping_value);
static JSON_Status      json_array_add(_Ptr<JSON_Array> array, _Ptr<JSON_Value> value);
static JSON_Status      json_array_resize(_Ptr<JSON_Array> array, size_t new_capacity);
static void             json_array_free(_Ptr<JSON_Array> array);

/* JSON Value */
static _Ptr<JSON_Value> json_value_init_string_no_copy(_Nt_array_ptr<char> string);

/* Parser */
static JSON_Status            skip_quotes(_Ptr<_Nt_array_ptr<const char>> string);
static int _Unchecked         parse_utf16(const char** unprocessed : itype(_Ptr<_Nt_array_ptr<const char>>), char** processed : itype(_Ptr<_Nt_array_ptr<char>>));
static _Nt_array_ptr<char>    process_string(_Nt_array_ptr<const char> input : count(len), size_t len);
static _Nt_array_ptr<char>    get_quoted_string(_Ptr<_Nt_array_ptr<const char>> string);
static _Ptr<JSON_Value>       parse_object_value(_Ptr<_Nt_array_ptr<const char>> string, size_t nesting);
static _Ptr<JSON_Value>       parse_array_value(_Ptr<_Nt_array_ptr<const char>> string, size_t nesting);
static _Ptr<JSON_Value>       parse_string_value(_Ptr<_Nt_array_ptr<const char>> string);
static _Ptr<JSON_Value>       parse_boolean_value(_Ptr<_Nt_array_ptr<const char>> string);
static _Ptr<JSON_Value>       parse_number_value(const char** string : itype(_Ptr<_Nt_array_ptr<const char>>));
static _Ptr<JSON_Value>       parse_null_value(_Ptr<_Nt_array_ptr<const char>> string);
static _Ptr<JSON_Value>       parse_value(_Ptr<_Nt_array_ptr<const char>> string, size_t nesting);

/* Serialization */
static int            json_serialize_to_buffer_r(_Ptr<const JSON_Value> value, _Nt_array_ptr<char> buf : bounds(buf_start, buf_start + buf_len), int level, int is_pretty, _Nt_array_ptr<char> num_buf, _Nt_array_ptr<char> buf_start : byte_count(buf_len), size_t buf_len);
static int            json_serialize_string(_Nt_array_ptr<const char> string, _Nt_array_ptr<char> buf : bounds(buf_start, buf_start + buf_len), _Nt_array_ptr<char> buf_start : byte_count(buf_len), size_t buf_len);
static int _Unchecked append_indent(_Nt_array_ptr<char> buf : bounds(buf_start, buf_start + buf_len), int level, _Nt_array_ptr<char> buf_start : byte_count(buf_len), size_t buf_len);
static int _Unchecked append_string(_Nt_array_ptr<char> buf : bounds(buf_start, buf_start + buf_len), _Nt_array_ptr<const char> string, _Nt_array_ptr<char> buf_start : byte_count(buf_len), size_t buf_len);

/* Various */
static _Nt_array_ptr<char> parson_strndup(_Nt_array_ptr<const char> string : count(n), size_t n) {
    _Nt_array_ptr<char> output_string : count(n) = parson_string_malloc(n);
    if (!output_string) {
        return NULL;
    }
    output_string[n] = '\0';
    strncpy(output_string, string, n);
    return output_string;
}

static _Nt_array_ptr<char> parson_strdup(_Nt_array_ptr<const char> string) {
    size_t len = strlen(string);
    _Nt_array_ptr<const char> str_with_len : count(len) = NULL;
    _Unchecked {
        str_with_len = _Assume_bounds_cast<_Nt_array_ptr<const char>>(string, count(len));
    }
    return parson_strndup(str_with_len, len);
}

static int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}


// TODO: This function requires bounds widening logic, so is unchecked.
static int _Unchecked parse_utf16_hex(const char* s, unsigned int* result) {
    int x1, x2, x3, x4;
    if (s[0] == '\0' || s[1] == '\0' || s[2] == '\0' || s[3] == '\0') {
        return 0;
    }
    x1 = hex_char_to_int(s[0]);
    x2 = hex_char_to_int(s[1]);
    x3 = hex_char_to_int(s[2]);
    x4 = hex_char_to_int(s[3]);
    if (x1 == -1 || x2 == -1 || x3 == -1 || x4 == -1) {
        return 0;
    }
    *result = (unsigned int)((x1 << 12) | (x2 << 8) | (x3 << 4) | x4);
    return 1;
}

static int num_bytes_in_utf8_sequence(unsigned char c) {
    if (c == 0xC0 || c == 0xC1 || c > 0xF4 || IS_CONT(c)) {
        return 0;
    } else if ((c & 0x80) == 0) {    /* 0xxxxxxx */
        return 1;
    } else if ((c & 0xE0) == 0xC0) { /* 110xxxxx */
        return 2;
    } else if ((c & 0xF0) == 0xE0) { /* 1110xxxx */
        return 3;
    } else if ((c & 0xF8) == 0xF0) { /* 11110xxx */
        return 4;
    }
    return 0; /* won't happen */
}

static int verify_utf8_sequence(_Nt_array_ptr<const unsigned char> s, _Ptr<int> len) {
    unsigned int cp = 0;
    *len = num_bytes_in_utf8_sequence(s[0]);
    // TODO: Requires bounds widening, so left unchecked.
    _Unchecked {
        const unsigned char* string = (const unsigned char*)s;
        if (*len == 1) {
            cp = string[0];
        } else if (*len == 2 && IS_CONT(string[1])) {
            cp = string[0] & 0x1F;
            cp = (cp << 6) | (string[1] & 0x3F);
        } else if (*len == 3 && IS_CONT(string[1]) && IS_CONT(string[2])) {
            cp = ((unsigned char)string[0]) & 0xF;
            cp = (cp << 6) | (string[1] & 0x3F);
            cp = (cp << 6) | (string[2] & 0x3F);
        } else if (*len == 4 && IS_CONT(string[1]) && IS_CONT(string[2]) && IS_CONT(string[3])) {
            cp = string[0] & 0x7;
            cp = (cp << 6) | (string[1] & 0x3F);
            cp = (cp << 6) | (string[2] & 0x3F);
            cp = (cp << 6) | (string[3] & 0x3F);
        } else {
            return 0;
        }
    }

    /* overlong encodings */
    if ((cp < 0x80    && *len > 1) ||
        (cp < 0x800   && *len > 2) ||
        (cp < 0x10000 && *len > 3)) {
        return 0;
    }

    /* invalid unicode */
    if (cp > 0x10FFFF) {
        return 0;
    }

    /* surrogate halves */
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        return 0;
    }

    return 1;
}

static int is_valid_utf8(_Nt_array_ptr<const char> string : bounds(string, string + string_len), size_t string_len) {
    int len = 0;
    _Nt_array_ptr<const char> string_end = _Dynamic_bounds_cast<_Nt_array_ptr<const char>>(string + string_len, count(0));
    while (string < string_end) {
        if (!verify_utf8_sequence((_Dynamic_bounds_cast<_Nt_array_ptr<const unsigned char>>(string, count(0))), &len)) {
            return 0;
        }
        string += len;
    }
    return 1;
}

static int is_decimal(const char* string : itype(_Nt_array_ptr<const char>) count(length), size_t length) {
    if (length > 1 && string[0] == '0' && string[1] != '.') {
        return 0;
    }
    // The following dynamic bounds cast should not be needed; length > 2 > 0
    if (length > 2 && !strncmp(string, "-0", 2) && string[2] != '.') {
        return 0;
    }
    while (length--) {
        if (strchr("xX", string[length])) {
            return 0;
        }
    }
    return 1;
}

static _Nt_array_ptr<char> read_file(_Nt_array_ptr<const char> filename) {
    _Ptr<FILE> fp = fopen(filename, "r");
    size_t size_to_read = 0;
    size_t size_read = 0;
    long pos;
    
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0L, SEEK_END);
    pos = ftell(fp);
    if (pos < 0) {
        fclose(fp);
        return NULL;
    }
    size_to_read = pos;
    rewind(fp);
    // TODO: compiler isn't constant folding when checking bounds, so we need the spurious (size_t) 1 here.
    _Nt_array_ptr<char> file_contents : count((size_t) 1 * size_to_read) = parson_string_malloc((size_t) 1 * size_to_read );
    if (!file_contents) {
        fclose(fp);
        return NULL;
    }
    size_read = fread(file_contents, 1, size_to_read, fp);
    if (size_read == 0 || ferror(fp)) {
        fclose(fp);
        parson_free(char, file_contents);
        return NULL;
    }
    fclose(fp);
    file_contents[size_read] = '\0';
    return file_contents;
}

static void remove_comments(_Nt_array_ptr<char> string, _Nt_array_ptr<const char> start_token, _Nt_array_ptr<const char> end_token) {
    int in_string = 0, escaped = 0;
    size_t i;
    char current_char;
    size_t start_token_len = strlen(start_token);
    size_t end_token_len = strlen(end_token);
    if (start_token_len == 0 || end_token_len == 0) {
        return;
    }
    while ((current_char = *string) != '\0') {
        if (current_char == '\\' && !escaped) {
            escaped = 1;
            string++;
            continue;
        } else if (current_char == '\"' && !escaped) {
            in_string = !in_string;
        // TODO: Can't prove this
        } else {
            _Unchecked {
                char* unchecked_string = (char*)string;
                if (!in_string && strncmp(unchecked_string, start_token, start_token_len) == 0) {
                    for(i = 0; i < start_token_len; i++) {
                        unchecked_string[i] = ' ';
                    }
                    unchecked_string = unchecked_string + start_token_len;
                    char* ptr_ = strstr(unchecked_string, end_token);
                    if (!ptr_) {
                        return;
                    }
                    for (i = 0; i < (ptr_ - unchecked_string) + end_token_len; i++) {
                        unchecked_string[i] = ' ';
                    }
                    
                    string = _Assume_bounds_cast<_Nt_array_ptr<char>>(ptr_ + end_token_len - 1, count(0));
                }
            } // end _Unchecked
        }
        escaped = 0;
        string++;
    }
}

/* JSON Object */
static _Ptr<JSON_Object> json_object_init(_Ptr<JSON_Value> wrapping_value) {
    _Ptr<JSON_Object> new_obj = parson_malloc(JSON_Object, sizeof(JSON_Object));
    if (new_obj == NULL) {
        return NULL;
    }
    new_obj->wrapping_value = wrapping_value;
    new_obj->names = NULL;
    new_obj->values = NULL;
    new_obj->capacity = 0;
    new_obj->count = 0;
    return new_obj;
}

static JSON_Status json_object_add(_Ptr<JSON_Object> object, _Nt_array_ptr<const char> name, _Ptr<JSON_Value> value) {
    if (name == NULL) {
        return JSONFailure;
    }
    size_t nameLen = strlen(name);
    _Nt_array_ptr<const char> name_with_len : count(nameLen) = NULL;
    _Unchecked {
        name_with_len = _Assume_bounds_cast<_Nt_array_ptr<const char>>(name, count(nameLen));
    }

    return json_object_addn(object, name_with_len, nameLen, value);
}

static JSON_Status json_object_addn(_Ptr<JSON_Object> object, _Nt_array_ptr<const char> name : count(name_len), size_t name_len, _Ptr<JSON_Value> value) {
    size_t index = 0;
    if (object == NULL || name == NULL || value == NULL) {
        return JSONFailure;
    }
    if (json_object_getn_value(object, name, name_len) != NULL) {
        return JSONFailure;
    }
    if (object->count >= object->capacity) {
        size_t new_capacity = MAX(object->capacity * 2, STARTING_CAPACITY);
        if (json_object_resize(object, new_capacity) == JSONFailure) {
            return JSONFailure;
        }
    }
    index = object->count;
    object->names[index] = parson_strndup(name, name_len);
    if (object->names[index] == NULL) {
        return JSONFailure;
    }
    value->parent = json_object_get_wrapping_value(object);
    object->values[index] = value;
    object->count++;
    return JSONSuccess;
}

static JSON_Status json_object_resize(_Ptr<JSON_Object> object, size_t new_capacity) {
    if ((object->names == NULL && object->values != NULL) ||
        (object->names != NULL && object->values == NULL) ||
        new_capacity == 0) {
            return JSONFailure; /* Shouldn't happen */
    }

    _Unchecked {
        char** temp_names = (char**)parson_malloc(char*, new_capacity * sizeof(char*));
        if (temp_names == NULL) {
            return JSONFailure;
        }
        JSON_Value** temp_values = (JSON_Value**)parson_malloc(JSON_Value*, new_capacity * sizeof(JSON_Value*));
        if (temp_values == NULL) {
            parson_free_unchecked(temp_names);
            return JSONFailure;
        }

        /* TODO: Memcpy functions below warn "cannot prove argument meets declared 
        * bounds" 1st arg truly won't prove unless new_capacity > object->count, 
        * which isn't checked here! It isn't exactly determined in the caller either.
        * This sort of means we can't prove the second arg either, since we
        * can't really know that count <= capacity. (Even if we could,
        * the compiler would have trouble with "<")
        *  This reasoning applies to both memcpy functions below. */
        if (object->names != NULL && object->values != NULL && object->count > 0) {
            memcpy(temp_names, object->names, object->count * sizeof(char*));
            memcpy(temp_values, object->values, object->count * sizeof(JSON_Value*));
        }
        parson_free(_Nt_array_ptr<char>, object->names);
        parson_free(_Ptr<JSON_Value>, object->values);
        // TODO: The three statements below need to be changed atomically
        object->capacity = new_capacity;
        object->names = temp_names;
        object->values = temp_values;
    } // end _Unchecked

    return JSONSuccess;
}

static JSON_Value* json_object_getn_value(_Ptr<const JSON_Object> object, _Nt_array_ptr<const char> name : count(name_len), size_t name_len) : itype(_Ptr<JSON_Value>) {
    size_t i, name_length;
    for (i = 0; i < json_object_get_count(object); i++) {
        name_length = strlen(object->names[i]);
        if (name_length != name_len) {
            continue;
        }
        if (strncmp(object->names[i], name, name_len) == 0) {
            return object->values[i];
        }
    }
    return NULL;
}

static JSON_Status json_object_remove_internal(_Ptr<JSON_Object> object, _Nt_array_ptr<const char> name, int free_value) {
    size_t i = 0, last_item_index = 0;
    if (object == NULL || json_object_get_value(object, name) == NULL) {
        return JSONFailure;
    }
    last_item_index = json_object_get_count(object) - 1;
    for (i = 0; i < json_object_get_count(object); i++) {
        if (strcmp(object->names[i], name) == 0) {
            parson_free(char, object->names[i]);
            if (free_value) {
                json_value_free(object->values[i]);
            }
            if (i != last_item_index) { /* Replace key value pair with one from the end */
                object->names[i] = object->names[last_item_index];
                object->values[i] = object->values[last_item_index];
            }
            object->count -= 1;
            return JSONSuccess;
        }
    }
    return JSONFailure; /* No execution path should end here */
}

static JSON_Status json_object_dotremove_internal(_Ptr<JSON_Object> object, _Nt_array_ptr<const char> name, int free_value) {
    _Ptr<JSON_Value> temp_value = NULL;
    _Ptr<JSON_Object> temp_object = NULL;
    _Nt_array_ptr<const char> dot_pos = strchr(name, '.');
    if (dot_pos == NULL) {
        return json_object_remove_internal(object, name, free_value);
    }
    _Nt_array_ptr<const char> before_dot : count((size_t)(dot_pos - name)) = NULL;
    _Unchecked {
        before_dot = _Assume_bounds_cast<_Nt_array_ptr<const char>>(name, count((size_t)(dot_pos - name)));
    }
    temp_value = json_object_getn_value(object, before_dot, (size_t)(dot_pos - name));
    if (json_value_get_type(temp_value) != JSONObject) {
        return JSONFailure;
    }
    temp_object = json_value_get_object(temp_value);
    _Nt_array_ptr<const char> after_dot = NULL;
    _Unchecked {
        after_dot = _Assume_bounds_cast<_Nt_array_ptr<const char>>(dot_pos + 1, count(0));
    }
    return json_object_dotremove_internal(temp_object, after_dot, free_value);
}

static void json_object_free(_Ptr<JSON_Object> object) {
    size_t i;
    for (i = 0; i < object->count; i++) {
        parson_free(char, object->names[i]);
        json_value_free(object->values[i]);
    }
    parson_free(_Nt_array_ptr<char>, object->names);
    parson_free_dynamic_bounds_cast(_Array_ptr<JSON_Value>, object->values);
    parson_free(JSON_Object, object);
}

/* JSON Array */
static _Ptr<JSON_Array> json_array_init(_Ptr<JSON_Value> wrapping_value) {
    _Ptr<JSON_Array> new_array = parson_malloc(JSON_Array, sizeof(JSON_Array));
    if (new_array == NULL) {
        return NULL;
    }
    new_array->wrapping_value = wrapping_value;
    new_array->items = NULL;
    new_array->capacity = 0;
    new_array->count = 0;
    return new_array;
}

static JSON_Status json_array_add(_Ptr<JSON_Array> array, _Ptr<JSON_Value> value) {
    if (array->count >= array->capacity) {
        size_t new_capacity = MAX(array->capacity * 2, STARTING_CAPACITY);
        if (json_array_resize(array, new_capacity) == JSONFailure) {
            return JSONFailure;
        }
    }
    value->parent = json_array_get_wrapping_value(array);
    array->items[array->count] = value;
    array->count++;
    return JSONSuccess;
}

static JSON_Status json_array_resize(_Ptr<JSON_Array> array, size_t new_capacity) {
    _Array_ptr<_Ptr<JSON_Value>> new_items : byte_count(new_capacity * sizeof(_Ptr<JSON_Value>)) = NULL;
    if (new_capacity == 0 || new_capacity < array-> count) {
        return JSONFailure;
    }
    new_items = parson_malloc(_Ptr<JSON_Value>, new_capacity * sizeof(_Ptr<JSON_Value>));
    if (new_items == NULL) {
        return JSONFailure;
    }
    // We know that the capacity is bigger than the count from the earlier if statement.
    // TODO: The compiler can't do a >= comparison, so unneeded dynamic bounds cast.
    if (array->items != NULL && array->count > 0) {
        memcpy<_Ptr<JSON_Value>>(_Dynamic_bounds_cast<_Array_ptr<_Ptr<JSON_Value>>>(new_items, byte_count(array->count * sizeof(_Ptr<JSON_Value>))), 
               _Dynamic_bounds_cast<_Array_ptr<_Ptr<JSON_Value>>>(array->items, byte_count(array->count * sizeof(_Ptr<JSON_Value>))),
               array->count * sizeof(_Ptr<JSON_Value>));
    }
    parson_free(_Ptr<JSON_Value>, array->items);

    // TODO: This should be atomic
    array->capacity = new_capacity;
    array->items = _Dynamic_bounds_cast<_Array_ptr<_Ptr<JSON_Value>>>(new_items, count(array->capacity));
    return JSONSuccess;
}

static void json_array_free(_Ptr<JSON_Array> array) {
    size_t i;
    for (i = 0; i < array->count; i++) {
        json_value_free(array->items[i]);
    }
    parson_free_dynamic_bounds_cast(_Array_ptr<JSON_Value>, array->items);
    parson_free(JSON_Array, array);
}

/* JSON Value */
static _Ptr<JSON_Value> json_value_init_string_no_copy(_Nt_array_ptr<char> string) {
    _Ptr<JSON_Value> new_value = parson_malloc(JSON_Value, sizeof(JSON_Value));
    if (!new_value) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONString;
    new_value->value.string = string;
    return new_value;
}

/* Parser */
static JSON_Status skip_quotes(_Ptr<_Nt_array_ptr<const char>> string) {
    if (**string != '\"') {
        return JSONFailure;
    }
    SKIP_CHAR(string);
    while (**string != '\"') {
        if (**string == '\0') {
            return JSONFailure;
        } else if (**string == '\\') {
            SKIP_CHAR(string);
            if (**string == '\0') {
                return JSONFailure;
            }
        }
        SKIP_CHAR(string);
    }
    SKIP_CHAR(string);
    return JSONSuccess;
}

// TODO: Needs bounds-widening to be checkable
static int _Unchecked parse_utf16(const char** unprocessed : itype(_Ptr<_Nt_array_ptr<const char>>), char** processed : itype(_Ptr<_Nt_array_ptr<char>>)) {
    unsigned int cp, lead, trail;
    int parse_succeeded = 0;
    char* processed_ptr = *processed;
    const char* unprocessed_ptr = *unprocessed;
    unprocessed_ptr++; /* skips u */
    parse_succeeded = parse_utf16_hex(unprocessed_ptr, &cp);
    if (!parse_succeeded) {
        return JSONFailure;
    }
    if (cp < 0x80) {
        processed_ptr[0] = (char)cp; /* 0xxxxxxx */
    } else if (cp < 0x800) {
        processed_ptr[0] = ((cp >> 6) & 0x1F) | 0xC0; /* 110xxxxx */
        processed_ptr[1] = ((cp)      & 0x3F) | 0x80; /* 10xxxxxx */
        processed_ptr += 1;
    } else if (cp < 0xD800 || cp > 0xDFFF) {
        processed_ptr[0] = ((cp >> 12) & 0x0F) | 0xE0; /* 1110xxxx */
        processed_ptr[1] = ((cp >> 6)  & 0x3F) | 0x80; /* 10xxxxxx */
        processed_ptr[2] = ((cp)       & 0x3F) | 0x80; /* 10xxxxxx */
        processed_ptr += 2;
    } else if (cp >= 0xD800 && cp <= 0xDBFF) { /* lead surrogate (0xD800..0xDBFF) */
        lead = cp;
        unprocessed_ptr += 4; /* should always be within the buffer, otherwise previous sscanf would fail */
        if (*unprocessed_ptr++ != '\\' || *unprocessed_ptr++ != 'u') {
            return JSONFailure;
        }
        parse_succeeded = parse_utf16_hex(unprocessed_ptr, &trail);
        if (!parse_succeeded || trail < 0xDC00 || trail > 0xDFFF) { /* valid trail surrogate? (0xDC00..0xDFFF) */
            return JSONFailure;
        }
        cp = ((((lead - 0xD800) & 0x3FF) << 10) | ((trail - 0xDC00) & 0x3FF)) + 0x010000;
        processed_ptr[0] = (((cp >> 18) & 0x07) | 0xF0); /* 11110xxx */
        processed_ptr[1] = (((cp >> 12) & 0x3F) | 0x80); /* 10xxxxxx */
        processed_ptr[2] = (((cp >> 6)  & 0x3F) | 0x80); /* 10xxxxxx */
        processed_ptr[3] = (((cp)       & 0x3F) | 0x80); /* 10xxxxxx */
        processed_ptr += 3;
    } else { /* trail surrogate before lead surrogate */
        return JSONFailure;
    }
    unprocessed_ptr += 3;
    *processed = processed_ptr;
    *unprocessed = unprocessed_ptr;
    return JSONSuccess;
}


/* Copies and processes passed string up to supplied length.
Example: "\u006Corem ipsum" -> lorem ipsum */
static _Nt_array_ptr<char> process_string(_Nt_array_ptr<const char> input : count(len), size_t len) {
    _Nt_array_ptr<const char> input_ptr : bounds(input, input + len) = input;
    size_t initial_size = (len + 1) * sizeof(char);
    size_t final_size = 0;
    _Nt_array_ptr<char> output : count(initial_size) = NULL;
    output = parson_string_malloc(initial_size);
    _Nt_array_ptr<char> output_ptr : bounds(output, output + initial_size) = NULL;
    if (output == NULL) {
        goto error;
    }
    output_ptr = output;
    while ((*input_ptr != '\0') && (size_t)(input_ptr - input) < len) {
        if (*input_ptr == '\\') {
            input_ptr++;
            switch (*input_ptr) {
                case '\"': *output_ptr = '\"'; break;
                case '\\': *output_ptr = '\\'; break;
                case '/':  *output_ptr = '/';  break;
                case 'b':  *output_ptr = '\b'; break;
                case 'f':  *output_ptr = '\f'; break;
                case 'n':  *output_ptr = '\n'; break;
                case 'r':  *output_ptr = '\r'; break;
                case 't':  *output_ptr = '\t'; break;
                case 'u': /*HACK for C3*/
                    _Unchecked {
                        const char *input_tmp = (const char *) input_ptr;
                        char *output_tmp = (char *) output_ptr;
                        if (parse_utf16(&input_tmp, &output_tmp) == JSONFailure) {
                            goto error;
                        }
                        input_ptr = _Assume_bounds_cast<_Nt_array_ptr<const char>>(input_tmp, bounds(input, input + len));
                        output_ptr = _Assume_bounds_cast<_Nt_array_ptr<char>>(output_tmp, bounds(output, output + initial_size));
                        break;
                    }
                default:
                    goto error;
            }
        } else if ((unsigned char)*input_ptr < 0x20) {
            goto error; /* 0x00-0x19 are invalid characters for json string (http://www.ietf.org/rfc/rfc4627.txt) */
        } else {
            *output_ptr = *input_ptr;
        }
        output_ptr++;
        input_ptr++;
    }
    *output_ptr = '\0';
    /* resize to new length */
    final_size = (size_t)(output_ptr-output) + 1;
    /* todo: don't resize if final_size == initial_size */
    _Nt_array_ptr<char> resized_output : count(final_size) = parson_string_malloc(final_size);
    if (resized_output == NULL) {
        goto error;
    }
    memcpy<char>(resized_output, _Dynamic_bounds_cast<_Nt_array_ptr<char>>(output, count(final_size)), final_size);
    parson_free(char, output);
    return resized_output;
error:
    parson_free(char, output);
    return NULL;
}

/* Return processed contents of a string between quotes and
   skips passed argument to a matching quote. */
static _Nt_array_ptr<char> get_quoted_string(_Ptr<_Nt_array_ptr<const char>> string) {
    _Nt_array_ptr<const char> string_start = *string;

    size_t string_len = 0;
    JSON_Status status = skip_quotes(string);
    if (status != JSONSuccess) {
        return NULL;
    }
    string_len = *string - string_start - 2; /* length without quotes */
    // TODO: We can't figure this out dynamically
    _Nt_array_ptr<const char> one_past_start : count(string_len) = NULL;
    _Unchecked {
        one_past_start = _Assume_bounds_cast<_Nt_array_ptr<const char>>(string_start + 1, count(string_len));
    }
    return process_string(one_past_start, string_len);
}

static _Ptr<JSON_Value> parse_value(_Ptr<_Nt_array_ptr<const char>> string, size_t nesting) {
    if (nesting > MAX_NESTING) {
        return NULL;
    }
    SKIP_WHITESPACES(string);
    switch (**string) {
        case '{':
            return parse_object_value(string, nesting + 1);
        case '[':
            return parse_array_value(string, nesting + 1);
        case '\"':
            return parse_string_value(string);
        case 'f': case 't':
            return parse_boolean_value(string);
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return parse_number_value(string);
        case 'n':
            return parse_null_value(string);
        default:
            return NULL;
    }
}

static _Ptr<JSON_Value> parse_object_value(_Ptr<_Nt_array_ptr<const char>> string, size_t nesting) {
    _Ptr<JSON_Value> output_value = NULL;
    _Ptr<JSON_Value> new_value = NULL;
    _Ptr<JSON_Object> output_object = NULL;
    _Nt_array_ptr<char> new_key = NULL;
    output_value = json_value_init_object();
    if (output_value == NULL) {
        return NULL;
    }
    if (**string != '{') {
        json_value_free(output_value);
        return NULL;
    }
    output_object = json_value_get_object(output_value);
    SKIP_CHAR(string);
    SKIP_WHITESPACES(string);
    if (**string == '}') { /* empty object */
        SKIP_CHAR(string);
        return output_value;
    }
    while (**string != '\0') {
        new_key = get_quoted_string(string);
        if (new_key == NULL) {
            json_value_free(output_value);
            return NULL;
        }
        SKIP_WHITESPACES(string);
        if (**string != ':') {
            parson_free(char, new_key);
            json_value_free(output_value);
            return NULL;
        }
        SKIP_CHAR(string);
        new_value = parse_value(string, nesting);
        if (new_value == NULL) {
            parson_free(char, new_key);
            json_value_free(output_value);
            return NULL;
        }
        if (json_object_add(output_object, new_key, new_value) == JSONFailure) {
            parson_free(char, new_key);
            json_value_free(new_value);
            json_value_free(output_value);
            return NULL;
        }
        parson_free(char, new_key);
        SKIP_WHITESPACES(string);
        if (**string != ',') {
            break;
        }
        SKIP_CHAR(string);
        SKIP_WHITESPACES(string);
    }
    SKIP_WHITESPACES(string);
    if (**string != '}' || /* Trim object after parsing is over */
        json_object_resize(output_object, json_object_get_count(output_object)) == JSONFailure) {
            json_value_free(output_value);
            return NULL;
    }
    SKIP_CHAR(string);
    return output_value;
}

static _Ptr<JSON_Value> parse_array_value(_Ptr<_Nt_array_ptr<const char>> string, size_t nesting) {
    _Ptr<JSON_Value> output_value = NULL;
    _Ptr<JSON_Value> new_array_value = NULL;
    _Ptr<JSON_Array> output_array = NULL;
    output_value = json_value_init_array();
    if (output_value == NULL) {
        return NULL;
    }
    if (**string != '[') {
        json_value_free(output_value);
        return NULL;
    }
    output_array = json_value_get_array(output_value);
    SKIP_CHAR(string);
    SKIP_WHITESPACES(string);
    if (**string == ']') { /* empty array */
        SKIP_CHAR(string);
        return output_value;
    }
    while (**string != '\0') {
        new_array_value = parse_value(string, nesting);
        if (new_array_value == NULL) {
            json_value_free(output_value);
            return NULL;
        }
        if (json_array_add(output_array, new_array_value) == JSONFailure) {
            json_value_free(new_array_value);
            json_value_free(output_value);
            return NULL;
        }
        SKIP_WHITESPACES(string);
        if (**string != ',') {
            break;
        }
        SKIP_CHAR(string);
        SKIP_WHITESPACES(string);
    }
    SKIP_WHITESPACES(string);
    if (**string != ']' || /* Trim array after parsing is over */
        json_array_resize(output_array, json_array_get_count(output_array)) == JSONFailure) {
            json_value_free(output_value);
            return NULL;
    }
    SKIP_CHAR(string);
    return output_value;
}

static _Ptr<JSON_Value> parse_string_value(_Ptr<_Nt_array_ptr<const char>> string) {
    _Ptr<JSON_Value> value = NULL;
    _Nt_array_ptr<char> new_string = get_quoted_string(string);
    if (new_string == NULL) {
        return NULL;
    }
    value = json_value_init_string_no_copy(new_string);
    if (value == NULL) {
        parson_free(char, new_string);
        return NULL;
    }
    return value;
}

static _Ptr<JSON_Value> parse_boolean_value(_Ptr<_Nt_array_ptr<const char>> string) {
    size_t true_token_size = SIZEOF_TOKEN("true");
    size_t false_token_size = SIZEOF_TOKEN("false");
    if (strncmp("true", *string, true_token_size) == 0) {
        *string += true_token_size;
        return json_value_init_boolean(1);
    } else if (strncmp("false", *string, false_token_size) == 0) {
        *string += false_token_size;
        return json_value_init_boolean(0);
    }
    return NULL;
}

/* TODO: The way this function deals with end is not well supported by the compiler. 
 * No initialization, needing to take the address, weird counting.
 * Leaving this function unchecked for now as a result. */
static _Unchecked _Ptr<JSON_Value> parse_number_value(const char** string) {
    char* end = NULL;
    double number = 0;
    errno = 0;
    number = strtod(*string, &end);
    if (errno || !is_decimal(*string, (size_t)(end - *string))) {
        return NULL;
    }
    *string = end;
    return json_value_init_number(number);
}

static _Ptr<JSON_Value> parse_null_value(_Ptr<_Nt_array_ptr<const char>> string) {
    size_t token_size = SIZEOF_TOKEN("null");
    if (strncmp("null", *string, token_size) == 0) {
        *string += token_size;
        return json_value_init_null();
    }
    return NULL;
}

/* Serialization */

#define APPEND_STRING(str) do { written = append_string(buf, (str), buf_start, buf_len);\
                                if (written < 0) { return -1; }\
                                if (buf != NULL) { buf += written; }\
                                written_total += written; } while(0)

#define APPEND_INDENT(level) do { written = append_indent(buf, (level), buf_start, buf_len);\
                                  if (written < 0) { return -1; }\
                                  if (buf != NULL) { buf += written; }\
                                  written_total += written; } while(0)

static int json_serialize_to_buffer_r(_Ptr<const JSON_Value> value, _Nt_array_ptr<char> buf : bounds(buf_start, buf_start + buf_len), int level, int is_pretty, _Nt_array_ptr<char> num_buf, _Nt_array_ptr<char> buf_start : byte_count(buf_len), size_t buf_len) {
    _Nt_array_ptr<const char> key = NULL;
    _Nt_array_ptr<const char> string = NULL;
    _Ptr<JSON_Value> temp_value = NULL;
    _Ptr<JSON_Array> array = NULL;
    _Ptr<JSON_Object> object = NULL;
    size_t i = 0, count = 0;
    double num = 0.0;
    int written = -1, written_total = 0;

    switch (json_value_get_type(value)) {
        case JSONArray:
            array = json_value_get_array(value);
            count = json_array_get_count(array);
            APPEND_STRING("[");
            if (count > 0 && is_pretty) {
                APPEND_STRING("\n");
            }
            for (i = 0; i < count; i++) {
                if (is_pretty) {
                    APPEND_INDENT(level+1);
                }
                temp_value = json_array_get_value(array, i);
                written = json_serialize_to_buffer_r(temp_value, buf, level+1, is_pretty, num_buf, buf_start, buf_len);
                if (written < 0) {
                    return -1;
                }
                if (buf != NULL) {
                    buf += written;
                }
                written_total += written;
                if (i < (count - 1)) {
                    APPEND_STRING(",");
                }
                if (is_pretty) {
                    APPEND_STRING("\n");
                }
            }
            if (count > 0 && is_pretty) {
                APPEND_INDENT(level);
            }
            APPEND_STRING("]");
            return written_total;
        case JSONObject:
            object = json_value_get_object(value);
            count  = json_object_get_count(object);
            APPEND_STRING("{");
            if (count > 0 && is_pretty) {
                APPEND_STRING("\n");
            }
            for (i = 0; i < count; i++) {
                key = json_object_get_name(object, i);
                if (key == NULL) {
                    return -1;
                }
                if (is_pretty) {
                    APPEND_INDENT(level+1);
                }
                written = json_serialize_string(key, buf, buf_start, buf_len);
                if (written < 0) {
                    return -1;
                }
                if (buf != NULL) {
                    buf += written;
                }
                written_total += written;
                APPEND_STRING(":");
                if (is_pretty) {
                    APPEND_STRING(" ");
                }
                temp_value = json_object_get_value(object, key);
                written = json_serialize_to_buffer_r(temp_value, buf, level+1, is_pretty, num_buf, buf_start, buf_len);
                if (written < 0) {
                    return -1;
                }
                if (buf != NULL) {
                    buf += written;
                }
                written_total += written;
                if (i < (count - 1)) {
                    APPEND_STRING(",");
                }
                if (is_pretty) {
                    APPEND_STRING("\n");
                }
            }
            if (count > 0 && is_pretty) {
                APPEND_INDENT(level);
            }
            APPEND_STRING("}");
            return written_total;
        case JSONString:
            string = json_value_get_string(value);
            if (string == NULL) {
                return -1;
            }
            written = json_serialize_string(string, buf, buf_start, buf_len);
            if (written < 0) {
                return -1;
            }
            if (buf != NULL) {
                buf += written;
            }
            written_total += written;
            return written_total;
        case JSONBoolean:
            if (json_value_get_boolean(value)) {
                APPEND_STRING("true");
            } else {
                APPEND_STRING("false");
            }
            return written_total;
        case JSONNumber:
            num = json_value_get_number(value);
            _Unchecked {
                if (buf != NULL) {
                    num_buf = _Assume_bounds_cast<_Nt_array_ptr<char>>(buf, count(0));
                }
                written = sprintf((char*)num_buf, FLOAT_FORMAT, num);
            }
            if (written < 0) {
                return -1;
            }
            if (buf != NULL) {
                buf += written;
            }
            written_total += written;
            return written_total;
        case JSONNull:
            APPEND_STRING("null");
            return written_total;
        case JSONError:
            return -1;
        default:
            return -1;
    }
}

static int json_serialize_string(_Nt_array_ptr<const char> str_unbounded,
                                 _Nt_array_ptr<char> buf : bounds(buf_start, buf_start + buf_len),
                                 _Nt_array_ptr<char> buf_start : byte_count(buf_len),
                                 size_t buf_len) {
    size_t i = 0, len = strlen(str_unbounded);
    _Nt_array_ptr<const char> string : count(len) = NULL;
    _Unchecked {
        string = _Assume_bounds_cast<_Nt_array_ptr<const char>>(str_unbounded, count(len));
    }
    char c = '\0';
    int written = -1, written_total = 0;
    APPEND_STRING("\"");
    for (i = 0; i < len; i++) {
        c = string[i];
        switch (c) {
            case '\"': APPEND_STRING("\\\""); break;
            case '\\': APPEND_STRING("\\\\"); break;
            case '\b': APPEND_STRING("\\b"); break;
            case '\f': APPEND_STRING("\\f"); break;
            case '\n': APPEND_STRING("\\n"); break;
            case '\r': APPEND_STRING("\\r"); break;
            case '\t': APPEND_STRING("\\t"); break;
            case '\x00': APPEND_STRING("\\u0000"); break;
            case '\x01': APPEND_STRING("\\u0001"); break;
            case '\x02': APPEND_STRING("\\u0002"); break;
            case '\x03': APPEND_STRING("\\u0003"); break;
            case '\x04': APPEND_STRING("\\u0004"); break;
            case '\x05': APPEND_STRING("\\u0005"); break;
            case '\x06': APPEND_STRING("\\u0006"); break;
            case '\x07': APPEND_STRING("\\u0007"); break;
            /* '\x08' duplicate: '\b' */
            /* '\x09' duplicate: '\t' */
            /* '\x0a' duplicate: '\n' */
            case '\x0b': APPEND_STRING("\\u000b"); break;
            /* '\x0c' duplicate: '\f' */
            /* '\x0d' duplicate: '\r' */
            case '\x0e': APPEND_STRING("\\u000e"); break;
            case '\x0f': APPEND_STRING("\\u000f"); break;
            case '\x10': APPEND_STRING("\\u0010"); break;
            case '\x11': APPEND_STRING("\\u0011"); break;
            case '\x12': APPEND_STRING("\\u0012"); break;
            case '\x13': APPEND_STRING("\\u0013"); break;
            case '\x14': APPEND_STRING("\\u0014"); break;
            case '\x15': APPEND_STRING("\\u0015"); break;
            case '\x16': APPEND_STRING("\\u0016"); break;
            case '\x17': APPEND_STRING("\\u0017"); break;
            case '\x18': APPEND_STRING("\\u0018"); break;
            case '\x19': APPEND_STRING("\\u0019"); break;
            case '\x1a': APPEND_STRING("\\u001a"); break;
            case '\x1b': APPEND_STRING("\\u001b"); break;
            case '\x1c': APPEND_STRING("\\u001c"); break;
            case '\x1d': APPEND_STRING("\\u001d"); break;
            case '\x1e': APPEND_STRING("\\u001e"); break;
            case '\x1f': APPEND_STRING("\\u001f"); break;
            case '/':
                if (parson_escape_slashes) {
                    APPEND_STRING("\\/");  /* to make json embeddable in xml\/html */
                } else {
                    APPEND_STRING("/");
                }
                break;
            default: /*HACK for C3*/
                _Unchecked {
                    if (buf != NULL) {
                        buf[0] = c;
                        buf += 1;
                    }
                }
                written_total += 1;
                break;
        }
    }
    APPEND_STRING("\"");
    return written_total;
}

static int append_indent(_Nt_array_ptr<char> buf : bounds(buf_start, buf_start + buf_len),
                         int level,
                         _Nt_array_ptr<char> buf_start : byte_count(buf_len),
                         size_t buf_len) {
    int i;
    int written = -1, written_total = 0;
    for (i = 0; i < level; i++) {
        APPEND_STRING("    ");
    }
    return written_total;
}

static int append_string(_Nt_array_ptr<char> buf : bounds(buf_start, buf_start + buf_len),
                         _Nt_array_ptr<const char> string,
                         _Nt_array_ptr<char> buf_start : byte_count(buf_len),
                         size_t buf_len) {
    size_t len = strlen(string);
    if (buf == NULL) {
        return (int) len;
    }

    // TODO: This does not go through properly if bounded string is a const char, as it should be
    _Array_ptr<char> boundedString : count(len) = NULL;
    _Unchecked {
        boundedString = _Assume_bounds_cast<_Array_ptr<char>>(string, count(len));
    }
    _Dynamic_check(buf >= buf_start && buf + len < buf_start + buf_len);
    _Nt_array_ptr<char> buf_tmp : count(len) = _Dynamic_bounds_cast<_Nt_array_ptr<char>>(buf, count(len));
    memcpy<char>(buf_tmp,
                 boundedString,
                 len);
    buf[len] = '\0';
    return len;
}
#undef APPEND_STRING
#undef APPEND_INDENT

/* Parser API */
JSON_Value * json_parse_file(const char *filename : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>) {
    _Nt_array_ptr<char> file_contents = read_file((_Nt_array_ptr<const char>)filename);
    _Ptr<JSON_Value> output_value = NULL;
    if (file_contents == NULL) {
        return NULL;
    }
    output_value = json_parse_string(file_contents);
    parson_free(char, file_contents);
    return output_value;
}

JSON_Value * json_parse_file_with_comments(const char *filename : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>) {
    _Nt_array_ptr<char> file_contents = read_file((_Nt_array_ptr<const char>)filename);
    _Ptr<JSON_Value> output_value = NULL;
    if (file_contents == NULL) {
        return NULL;
    }
    output_value = json_parse_string_with_comments(file_contents);
    parson_free(char, file_contents);
    return output_value;
}

JSON_Value * json_parse_string(const char *string : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>) {
    if (string == NULL) {
        return NULL;
    }
    _Unchecked {
        const char* tmp = string;
        if (tmp[0] == '\xEF' && tmp[1] == '\xBB' && tmp[2] == '\xBF') {
            string = string + 3; /* Support for UTF-8 BOM */
        }
        return parse_value((_Ptr<_Nt_array_ptr<const char>>)&string, 0);
    }
}

JSON_Value * json_parse_string_with_comments(const char *string : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>) {
    _Ptr<JSON_Value> result = NULL;    
    _Nt_array_ptr<char> string_mutable_copy = parson_strdup((_Nt_array_ptr<const char>)string);
    if (string_mutable_copy == NULL) {
        return NULL;
    }
    remove_comments(string_mutable_copy, "/*", "*/");
    remove_comments(string_mutable_copy, "//", "\n");
    _Unchecked {
        const char* string_mutable_copy_ptr[1] = { NULL };
        string_mutable_copy_ptr[0] = (const char*)string_mutable_copy;
        result = parse_value((_Ptr<_Nt_array_ptr<const char>>)string_mutable_copy_ptr, 0);
        parson_free(char, string_mutable_copy);
        return result;
    }
}

/* JSON Object API */

JSON_Value * json_object_get_value(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>) {
    if (object == NULL || name == NULL) {
        return NULL;
    }
    size_t nameLen = strlen(name);
    _Nt_array_ptr<const char> name_with_len : count(nameLen) = NULL;
    _Unchecked {
        name_with_len = _Assume_bounds_cast<_Nt_array_ptr<const char>>(name, count(nameLen));
    }
    return json_object_getn_value(object, name_with_len, nameLen);
}

const char * json_object_get_string(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Nt_array_ptr<const char>) {
    return json_value_get_string(json_object_get_value(object, name));
}

double json_object_get_number(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_value_get_number(json_object_get_value(object, name));
}

JSON_Object * json_object_get_object(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Object>) {
    return json_value_get_object(json_object_get_value(object, name));
}

JSON_Array * json_object_get_array(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Array>) {
    return json_value_get_array(json_object_get_value(object, name));
}

int json_object_get_boolean(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_value_get_boolean(json_object_get_value(object, name));
}

JSON_Value * json_object_dotget_value(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>) {
    _Nt_array_ptr<const char> dot_position = strchr(name, '.');
    if (!dot_position) {
        return json_object_get_value(object, name);
    }
    _Nt_array_ptr<const char> after_dot : count((size_t)(dot_position - name)) = NULL;
    _Unchecked {
        after_dot = _Assume_bounds_cast<_Nt_array_ptr<const char>>(name, count((size_t)(dot_position - name)));
    }
    object = json_value_get_object(json_object_getn_value(object, after_dot, (size_t)(dot_position - name)));
    _Nt_array_ptr<const char> one_past_dot : count(0) = NULL;
    _Unchecked {
        one_past_dot = _Assume_bounds_cast<_Nt_array_ptr<const char>>(dot_position + 1, count(0));
    }
    return json_object_dotget_value(object, one_past_dot);
}

const char * json_object_dotget_string(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Nt_array_ptr<const char>) {
    return json_value_get_string(json_object_dotget_value(object, name));
}

double json_object_dotget_number(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_value_get_number(json_object_dotget_value(object, name));
}

JSON_Object * json_object_dotget_object(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Object>) {
    return json_value_get_object(json_object_dotget_value(object, name));
}

JSON_Array * json_object_dotget_array(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Array>) {
    return json_value_get_array(json_object_dotget_value(object, name));
}

int json_object_dotget_boolean(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_value_get_boolean(json_object_dotget_value(object, name));
}

size_t json_object_get_count(const JSON_Object *object : itype(_Ptr<const JSON_Object>)) {
    return object ? object->count : 0;
}

const char * json_object_get_name(const JSON_Object *object : itype(_Ptr<const JSON_Object>), size_t index) : itype(_Nt_array_ptr<const char>) {
    if (object == NULL || index >= json_object_get_count(object)) {
        return NULL;
    }
    return object->names[index];
}

JSON_Value * json_object_get_value_at(const JSON_Object *object : itype(_Ptr<const JSON_Object>), size_t index) : itype(_Ptr<JSON_Value>) {
    if (object == NULL || index >= json_object_get_count(object)) {
        return NULL;
    }
    return object->values[index];
}

JSON_Value *json_object_get_wrapping_value(const JSON_Object *object : itype(_Ptr<const JSON_Object>)) : itype(_Ptr<JSON_Value>) {
    return object->wrapping_value;
}

int json_object_has_value (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_object_get_value(object, name) != NULL;
}

int json_object_has_value_of_type(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), JSON_Value_Type type) {
    _Ptr<JSON_Value> val = json_object_get_value(object, name);
    return val != NULL && json_value_get_type(val) == type;
}

int json_object_dothas_value (const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_object_dotget_value(object, name) != NULL;
}

int json_object_dothas_value_of_type(const JSON_Object *object : itype(_Ptr<const JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), JSON_Value_Type type) {
    _Ptr<JSON_Value> val = json_object_dotget_value(object, name);
    return val != NULL && json_value_get_type(val) == type;
}

/* JSON Array API */
JSON_Value * json_array_get_value(const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) : itype(_Ptr<JSON_Value>) {
    if (array == NULL || index >= json_array_get_count(array)) {
        return NULL;
    }
    return array->items[index];
}

const char * json_array_get_string(const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) : itype(_Nt_array_ptr<const char>) {
    return json_value_get_string(json_array_get_value(array, index));
}

double json_array_get_number(const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) {
    return json_value_get_number(json_array_get_value(array, index));
}

JSON_Object * json_array_get_object(const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) : itype(_Ptr<JSON_Object>) {
    return json_value_get_object(json_array_get_value(array, index));
}

JSON_Array * json_array_get_array(const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) : itype(_Ptr<JSON_Array>) {
    return json_value_get_array(json_array_get_value(array, index));
}

int json_array_get_boolean(const JSON_Array *array : itype(_Ptr<const JSON_Array>), size_t index) {
    return json_value_get_boolean(json_array_get_value(array, index));
}

size_t json_array_get_count(const JSON_Array *array : itype(_Ptr<const JSON_Array>)) {
    return array ? array->count : 0;
}

JSON_Value * json_array_get_wrapping_value(const JSON_Array *array : itype(_Ptr<const JSON_Array>)) : itype(_Ptr<JSON_Value>) {
    return array->wrapping_value;
}

/* JSON Value API */
JSON_Value_Type json_value_get_type(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    return value ? value->type : JSONError;
}

JSON_Object * json_value_get_object(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Object>) {
    return json_value_get_type(value) == JSONObject ? value->value.object : NULL;
}

JSON_Array * json_value_get_array(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Array>) {
    return json_value_get_type(value) == JSONArray ? value->value.array : NULL;
}

const char * json_value_get_string(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Nt_array_ptr<const char>) {
    return json_value_get_type(value) == JSONString ? value->value.string : NULL;
}

double json_value_get_number(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    return json_value_get_type(value) == JSONNumber ? value->value.number : 0;
}

int json_value_get_boolean(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    return json_value_get_type(value) == JSONBoolean ? value->value.boolean : -1;
}

JSON_Value * json_value_get_parent (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Value>) {
    return value ? value->parent : NULL;
}

void json_value_free(JSON_Value *value : itype(_Ptr<JSON_Value>)) {
    switch (json_value_get_type(value)) {
        case JSONObject:
            json_object_free(value->value.object);
            break;
        case JSONString:
            parson_free(char, value->value.string);
            break;
        case JSONArray:
            json_array_free(value->value.array);
            break;
        default:
            break;
    }
    parson_free(JSON_Value, value);
}

JSON_Value * json_value_init_object(void) : itype(_Ptr<JSON_Value>) {
    _Ptr<JSON_Value> new_value = parson_malloc(JSON_Value, sizeof(JSON_Value));
    if (!new_value) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONObject;
    new_value->value.object = json_object_init(new_value);
    if (!new_value->value.object) {
        parson_free(JSON_Value, new_value);
        return NULL;
    }
    return new_value;
}

JSON_Value * json_value_init_array(void) : itype(_Ptr<JSON_Value>) {
    _Ptr<JSON_Value> new_value = parson_malloc(JSON_Value, sizeof(JSON_Value));
    if (!new_value) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONArray;
    new_value->value.array = json_array_init(new_value);
    if (!new_value->value.array) {
        parson_free(JSON_Value, new_value);
        return NULL;
    }
    return new_value;
}

JSON_Value * json_value_init_string(const char *string : itype(_Nt_array_ptr<const char>)) : itype(_Ptr<JSON_Value>) {
    _Nt_array_ptr<char> copy = NULL;
    _Ptr<JSON_Value> value = NULL;
    size_t string_len = 0;
    if (string == NULL) {
        return NULL;
    }
    string_len = strlen(string);
    _Nt_array_ptr<const char> str_with_len : count(string_len) = NULL;
    _Unchecked {
        str_with_len = _Assume_bounds_cast<_Nt_array_ptr<const char>>(string, count(string_len));
    }
    if (!is_valid_utf8(str_with_len, string_len)) {
        return NULL;
    }
    copy = parson_strndup(str_with_len, string_len);
    if (copy == NULL) {
        return NULL;
    }
    value = json_value_init_string_no_copy(copy);
    if (value == NULL) {
        parson_free(char, copy);
    }
    return value;
}

JSON_Value * json_value_init_number(double number) : itype(_Ptr<JSON_Value>) {
    _Ptr<JSON_Value> new_value = NULL;
    if (IS_NUMBER_INVALID(number)) {
        return NULL;
    }
    new_value = parson_malloc(JSON_Value, sizeof(JSON_Value));
    if (new_value == NULL) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONNumber;
    new_value->value.number = number;
    return new_value;
}

JSON_Value * json_value_init_boolean(int boolean) : itype(_Ptr<JSON_Value>) {
    _Ptr<JSON_Value> new_value = parson_malloc(JSON_Value, sizeof(JSON_Value));
    if (!new_value) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONBoolean;
    new_value->value.boolean = boolean ? 1 : 0;
    return new_value;
}

JSON_Value * json_value_init_null(void) : itype(_Ptr<JSON_Value>) {
    _Ptr<JSON_Value> new_value = parson_malloc(JSON_Value, sizeof(JSON_Value));
    if (!new_value) {
        return NULL;
    }
    new_value->parent = NULL;
    new_value->type = JSONNull;
    return new_value;
}

JSON_Value * json_value_deep_copy(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Value>) {
    size_t i = 0;
    _Ptr<JSON_Value> return_value = NULL;
    _Ptr<JSON_Value> temp_value_copy = NULL;
    _Ptr<JSON_Value> temp_value = NULL;
    _Nt_array_ptr<const char> temp_string = NULL;
    _Nt_array_ptr<const char> temp_key = NULL;
    _Nt_array_ptr<char> temp_string_copy = NULL;
    _Ptr<JSON_Array> temp_array = NULL;
    _Ptr<JSON_Array> temp_array_copy = NULL;
    _Ptr<JSON_Object> temp_object = NULL;
    _Ptr<JSON_Object> temp_object_copy = NULL;

    switch (json_value_get_type(value)) {
        case JSONArray:
            temp_array = json_value_get_array(value);
            return_value = json_value_init_array();
            if (return_value == NULL) {
                return NULL;
            }
            temp_array_copy = json_value_get_array(return_value);
            for (i = 0; i < json_array_get_count(temp_array); i++) {
                temp_value = json_array_get_value(temp_array, i);
                temp_value_copy = json_value_deep_copy(temp_value);
                if (temp_value_copy == NULL) {
                    json_value_free(return_value);
                    return NULL;
                }
                if (json_array_add(temp_array_copy, temp_value_copy) == JSONFailure) {
                    json_value_free(return_value);
                    json_value_free(temp_value_copy);
                    return NULL;
                }
            }
            return return_value;
        case JSONObject:
            temp_object = json_value_get_object(value);
            return_value = json_value_init_object();
            if (return_value == NULL) {
                return NULL;
            }
            temp_object_copy = json_value_get_object(return_value);
            for (i = 0; i < json_object_get_count(temp_object); i++) {
                temp_key = json_object_get_name(temp_object, i);
                temp_value = json_object_get_value(temp_object, temp_key);
                temp_value_copy = json_value_deep_copy(temp_value);
                if (temp_value_copy == NULL) {
                    json_value_free(return_value);
                    return NULL;
                }
                if (json_object_add(temp_object_copy, temp_key, temp_value_copy) == JSONFailure) {
                    json_value_free(return_value);
                    json_value_free(temp_value_copy);
                    return NULL;
                }
            }
            return return_value;
        case JSONBoolean:
            return json_value_init_boolean(json_value_get_boolean(value));
        case JSONNumber:
            return json_value_init_number(json_value_get_number(value));
        case JSONString:
            temp_string = (_Nt_array_ptr<const char>)json_value_get_string(value);
            if (temp_string == NULL) {
                return NULL;
            }
            temp_string_copy = parson_strdup(temp_string);
            if (temp_string_copy == NULL) {
                return NULL;
            }
            return_value = json_value_init_string_no_copy(temp_string_copy);
            if (return_value == NULL) {
                parson_free(char, temp_string_copy);
            }
            return return_value;
        case JSONNull:
            return json_value_init_null();
        case JSONError:
            return NULL;
        default:
            return NULL;
    }
}

size_t json_serialization_size(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    char num_buf _Nt_checked[NUM_BUF_SIZE]; /* recursively allocating buffer on stack is a bad idea, so done statically */
    int res = json_serialize_to_buffer_r(value, NULL, 0, 0, num_buf, NULL, 0);
    return res < 0 ? 0 : (size_t)(res + 1);
}

JSON_Status json_serialize_to_buffer(const JSON_Value *value : itype(_Ptr<const JSON_Value>),  char *buf : itype(_Nt_array_ptr<char>) byte_count(buf_size_in_bytes), size_t buf_size_in_bytes) {
    int written = -1;
    size_t needed_size_in_bytes = json_serialization_size(value);
    if (needed_size_in_bytes == 0 || buf_size_in_bytes < needed_size_in_bytes) {
        return JSONFailure;
    }
    written = json_serialize_to_buffer_r(value, buf, 0, 0, NULL, buf, buf_size_in_bytes);
    if (written < 0) {
        buf[0] = '\0';
        return JSONFailure;
    }
    buf[written] = '\0';
    return JSONSuccess;
}

JSON_Status json_serialize_to_file(const JSON_Value *value : itype(_Ptr<const JSON_Value>), const char *filename : itype(_Nt_array_ptr<const char>)) {
    JSON_Status return_code = JSONSuccess;
    _Ptr<FILE> fp = NULL;
    _Nt_array_ptr<char> serialized_string = json_serialize_to_string(value);
    if (serialized_string == NULL) {
        return JSONFailure;
    }
    fp = fopen(filename, "w");
    if (fp == NULL) {
        json_free_serialized_string(serialized_string);
        return JSONFailure;
    }
    if (fputs(serialized_string, fp) == EOF) {
        return_code = JSONFailure;
    }
    if (fclose(fp) == EOF) {
        return_code = JSONFailure;
    }
    json_free_serialized_string(serialized_string);
    return return_code;
}

char * json_serialize_to_string(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Nt_array_ptr<char>) {
    JSON_Status serialization_result = JSONFailure;
    size_t buf_size_bytes = json_serialization_size(value);
    _Nt_array_ptr<char> buf : byte_count(buf_size_bytes) = NULL;
    if (buf_size_bytes == 0) {
        return NULL;
    }
    buf = parson_string_malloc(buf_size_bytes);
    if (buf == NULL) {
        return NULL;
    }
    serialization_result = json_serialize_to_buffer(value, buf, buf_size_bytes);
    if (serialization_result == JSONFailure) {
        json_free_serialized_string(buf);
        return NULL;
    }
    return buf;
}

size_t json_serialization_size_pretty(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    char num_buf _Nt_checked[NUM_BUF_SIZE]; /* recursively allocating buffer on stack is a bad idea, so let's do it only once */
    int res = json_serialize_to_buffer_r(value, NULL, 0, 1, num_buf, NULL, 0);
    return res < 0 ? 0 : (size_t)(res + 1);
}

JSON_Status json_serialize_to_buffer_pretty(const JSON_Value *value : itype(_Ptr<const JSON_Value>), char *buf : itype(_Nt_array_ptr<char>) byte_count(buf_size_in_bytes), size_t buf_size_in_bytes) {
    int written = -1;
    size_t needed_size_in_bytes = json_serialization_size_pretty(value);
    if (needed_size_in_bytes == 0 || buf_size_in_bytes < needed_size_in_bytes) {
        return JSONFailure;
    }
    written = json_serialize_to_buffer_r(value, buf, 0, 1, NULL, buf, buf_size_in_bytes);
    if (written < 0) {
        buf[0] = '\0';
        return JSONFailure;
    }
    buf[written] = '\0';
    return JSONSuccess;
}

JSON_Status json_serialize_to_file_pretty(const JSON_Value *value : itype(_Ptr<const JSON_Value>), const char *filename : itype(_Nt_array_ptr<const char>)) {
    JSON_Status return_code = JSONSuccess;
    _Ptr<FILE> fp = NULL;
    _Nt_array_ptr<char> serialized_string = json_serialize_to_string_pretty(value);
    if (serialized_string == NULL) {
        return JSONFailure;
    }
    fp = fopen(filename, "w");
    if (fp == NULL) {
        json_free_serialized_string(serialized_string);
        return JSONFailure;
    }
    if (fputs(serialized_string, fp) == EOF) {
        return_code = JSONFailure;
    }
    if (fclose(fp) == EOF) {
        return_code = JSONFailure;
    }
    json_free_serialized_string(serialized_string);
    return return_code;
}

char * json_serialize_to_string_pretty(const JSON_Value* value : itype(_Ptr<const JSON_Value>)) : itype(_Nt_array_ptr<char>) {
    JSON_Status serialization_result = JSONFailure;
    size_t buf_size_bytes = json_serialization_size_pretty(value);
    _Nt_array_ptr<char> buf : byte_count(buf_size_bytes) = NULL;
    if (buf_size_bytes == 0) {
        return NULL;
    }
    buf = parson_string_malloc(buf_size_bytes);
    if (buf == NULL) {
        return NULL;
    }
    serialization_result = json_serialize_to_buffer_pretty(value, buf, buf_size_bytes);
    if (serialization_result == JSONFailure) {
        json_free_serialized_string(buf);
        return NULL;
    }
    return buf;
}

void json_free_serialized_string(char *string : itype(_Nt_array_ptr<char>)) {
    parson_free(char, string);
}

JSON_Status json_array_remove(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t ix) {
    size_t to_move_bytes = 0;
    if (array == NULL || ix >= json_array_get_count(array)) {
        return JSONFailure;
    }
    json_value_free(json_array_get_value(array, ix));
    to_move_bytes = (json_array_get_count(array) - 1 - ix) * sizeof(_Ptr<JSON_Value>);
    // TODO: Unchecked because memmove doesn't yet take a type argument
    _Unchecked {
        memmove((void*)(array->items + ix), (void*)(array->items + ix + 1), to_move_bytes);
    }
    array->count -= 1;
    return JSONSuccess;
}

JSON_Status json_array_replace_value(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t ix, JSON_Value *value : itype(_Ptr<JSON_Value>)) {
    if (array == NULL || value == NULL || value->parent != NULL || ix >= json_array_get_count(array)) {
        return JSONFailure;
    }
    json_value_free(json_array_get_value(array, ix));
    value->parent = json_array_get_wrapping_value(array);
    array->items[ix] = value;
    return JSONSuccess;
}

JSON_Status json_array_replace_string(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i, const char* string : itype(_Nt_array_ptr<const char>)) {
    _Ptr<JSON_Value> value = json_value_init_string(string);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_replace_number(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i, double number) {
    _Ptr<JSON_Value> value = json_value_init_number(number);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_replace_boolean(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i, int boolean) {
    _Ptr<JSON_Value> value = json_value_init_boolean(boolean);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_replace_null(JSON_Array *array : itype(_Ptr<JSON_Array>), size_t i) {
    _Ptr<JSON_Value> value = json_value_init_null();
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_replace_value(array, i, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_clear(JSON_Array *array : itype(_Ptr<JSON_Array>)) {
    size_t i = 0;
    if (array == NULL) {
        return JSONFailure;
    }
    for (i = 0; i < json_array_get_count(array); i++) {
        json_value_free(json_array_get_value(array, i));
    }
    array->count = 0;
    return JSONSuccess;
}

JSON_Status json_array_append_value(JSON_Array *array : itype(_Ptr<JSON_Array>), JSON_Value *value : itype(_Ptr<JSON_Value>)) {
    if (array == NULL || value == NULL || value->parent != NULL) {
        return JSONFailure;
    }
    return json_array_add(array, value);
}

JSON_Status json_array_append_string(JSON_Array *array : itype(_Ptr<JSON_Array>), const char *string : itype(_Nt_array_ptr<const char>)) {
    _Ptr<JSON_Value> value = json_value_init_string(string);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_append_number(JSON_Array *array : itype(_Ptr<JSON_Array>), double number) {
    _Ptr<JSON_Value> value = json_value_init_number(number);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_append_boolean(JSON_Array *array : itype(_Ptr<JSON_Array>), int boolean) {
    _Ptr<JSON_Value> value = json_value_init_boolean(boolean);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_array_append_null(JSON_Array *array : itype(_Ptr<JSON_Array>)) {
    _Ptr<JSON_Value> value = json_value_init_null();
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_array_append_value(array, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_set_value(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), JSON_Value *value : itype(_Ptr<JSON_Value>)) {
    size_t i = 0;
    _Ptr<JSON_Value> old_value = NULL;
    if (object == NULL || name == NULL || value == NULL || value->parent != NULL) {
        return JSONFailure;
    }
    old_value = json_object_get_value(object, name);
    if (old_value != NULL) { /* free and overwrite old value */
        json_value_free(old_value);
        for (i = 0; i < json_object_get_count(object); i++) {
            if (strcmp(object->names[i], name) == 0) {
                value->parent = json_object_get_wrapping_value(object);
                object->values[i] = value;
                return JSONSuccess;
            }
        }
    }
    /* add new key value pair */
    return json_object_add(object, (_Nt_array_ptr<const char>)name, value);
}

JSON_Status json_object_set_string(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), const char *string : itype(_Nt_array_ptr<const char>)) {
    return json_object_set_value(object, name, json_value_init_string(string));
}

JSON_Status json_object_set_number(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), double number) {
    return json_object_set_value(object, name, json_value_init_number(number));
}

JSON_Status json_object_set_boolean(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), int boolean) {
    return json_object_set_value(object, name, json_value_init_boolean(boolean));
}

JSON_Status json_object_set_null(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_object_set_value(object, name, json_value_init_null());
}

JSON_Status json_object_dotset_value(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), JSON_Value *value : itype(_Ptr<JSON_Value>)) {
    _Nt_array_ptr<const char> dot_pos = NULL;
    _Ptr<JSON_Value> temp_value = NULL;
    _Ptr<JSON_Value> new_value = NULL;
    _Ptr<JSON_Object> temp_object = NULL;
    _Ptr<JSON_Object> new_object = NULL;
    JSON_Status status = JSONFailure;
    size_t name_len = 0;
    if (object == NULL || name == NULL || value == NULL) {
        return JSONFailure;
    }
    dot_pos = (_Nt_array_ptr<const char>)strchr(name, '.');
    if (dot_pos == NULL) {
        return json_object_set_value(object, name, value);
    }
    _Nt_array_ptr<const char> after_dot = NULL;
    _Unchecked {
        after_dot = _Assume_bounds_cast<_Nt_array_ptr<const char>>(dot_pos + 1, count(0));
    }
    name_len = dot_pos - name;
    _Nt_array_ptr<const char> name_with_len : count(name_len) = NULL;
    _Unchecked {
        name_with_len = _Assume_bounds_cast<_Nt_array_ptr<const char>>(name, count(name_len));
    }
    temp_value = json_object_getn_value(object, name_with_len, name_len);
    if (temp_value) {
        /* Don't overwrite existing non-object (unlike json_object_set_value, but it shouldn't be changed at this point) */
        if (json_value_get_type(temp_value) != JSONObject) {
            return JSONFailure;
        }
        temp_object = json_value_get_object(temp_value);
        return json_object_dotset_value(temp_object, after_dot, value);
    }
    new_value = json_value_init_object();
    if (new_value == NULL) {
        return JSONFailure;
    }
    new_object = json_value_get_object(new_value);
    status = json_object_dotset_value(new_object, after_dot, value);
    if (status != JSONSuccess) {
        json_value_free(new_value);
        return JSONFailure;
    }
    status = json_object_addn(object, name_with_len, name_len, new_value);
    if (status != JSONSuccess) {
        json_object_dotremove_internal(new_object, after_dot, 0);
        json_value_free(new_value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_dotset_string(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), const char *string : itype(_Nt_array_ptr<const char>)) {
    _Ptr<JSON_Value> value = json_value_init_string(string);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_dotset_number(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), double number) {
    _Ptr<JSON_Value> value = json_value_init_number(number);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_dotset_boolean(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>), int boolean) {
    _Ptr<JSON_Value> value = json_value_init_boolean(boolean);
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_dotset_null(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    _Ptr<JSON_Value> value = json_value_init_null();
    if (value == NULL) {
        return JSONFailure;
    }
    if (json_object_dotset_value(object, name, value) == JSONFailure) {
        json_value_free(value);
        return JSONFailure;
    }
    return JSONSuccess;
}

JSON_Status json_object_remove(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_object_remove_internal(object, (_Nt_array_ptr<const char>)name, 1);
}

JSON_Status json_object_dotremove(JSON_Object *object : itype(_Ptr<JSON_Object>), const char *name : itype(_Nt_array_ptr<const char>)) {
    return json_object_dotremove_internal(object, (_Nt_array_ptr<const char>)name, 1);
}

JSON_Status json_object_clear(JSON_Object *object : itype(_Ptr<JSON_Object>)) {
    size_t i = 0;
    if (object == NULL) {
        return JSONFailure;
    }
    for (i = 0; i < json_object_get_count(object); i++) {
        parson_free(char, object->names[i]);
        json_value_free(object->values[i]);
    }
    object->count = 0;
    return JSONSuccess;
}

JSON_Status json_validate(const JSON_Value *schema : itype(_Ptr<const JSON_Value>), const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    _Ptr<JSON_Value> temp_schema_value = NULL;
    _Ptr<JSON_Value> temp_value = NULL;
    _Ptr<JSON_Array> schema_array = NULL;
    _Ptr<JSON_Array> value_array = NULL;
    _Ptr<JSON_Object> schema_object = NULL;
    _Ptr<JSON_Object> value_object = NULL;
    JSON_Value_Type schema_type = JSONError, value_type = JSONError;
    _Nt_array_ptr<const char> key = NULL;
    size_t i = 0, count = 0;
    if (schema == NULL || value == NULL) {
        return JSONFailure;
    }
    schema_type = json_value_get_type(schema);
    value_type = json_value_get_type(value);
    if (schema_type != value_type && schema_type != JSONNull) { /* null represents all values */
        return JSONFailure;
    }
    switch (schema_type) {
        case JSONArray:
            schema_array = json_value_get_array(schema);
            value_array = json_value_get_array(value);
            count = json_array_get_count(schema_array);
            if (count == 0) {
                return JSONSuccess; /* Empty array allows all types */
            }
            /* Get first value from array, rest is ignored */
            temp_schema_value = json_array_get_value(schema_array, 0);
            for (i = 0; i < json_array_get_count(value_array); i++) {
                temp_value = json_array_get_value(value_array, i);
                if (json_validate(temp_schema_value, temp_value) == JSONFailure) {
                    return JSONFailure;
                }
            }
            return JSONSuccess;
        case JSONObject:
            schema_object = json_value_get_object(schema);
            value_object = json_value_get_object(value);
            count = json_object_get_count(schema_object);
            if (count == 0) {
                return JSONSuccess; /* Empty object allows all objects */
            } else if (json_object_get_count(value_object) < count) {
                return JSONFailure; /* Tested object mustn't have less name-value pairs than schema */
            }
            for (i = 0; i < count; i++) {
                key = json_object_get_name(schema_object, i);
                temp_schema_value = json_object_get_value(schema_object, key);
                temp_value = json_object_get_value(value_object, key);
                if (temp_value == NULL) {
                    return JSONFailure;
                }
                if (json_validate(temp_schema_value, temp_value) == JSONFailure) {
                    return JSONFailure;
                }
            }
            return JSONSuccess;
        case JSONString: case JSONNumber: case JSONBoolean: case JSONNull:
            return JSONSuccess; /* equality already tested before switch */
        case JSONError: default:
            return JSONFailure;
    }
}

int json_value_equals(const JSON_Value *a : itype(_Ptr<const JSON_Value>), const JSON_Value *b : itype(_Ptr<const JSON_Value>)) {
    _Ptr<JSON_Object> a_object = NULL;
    _Ptr<JSON_Object> b_object = NULL;
    _Ptr<JSON_Array> a_array = NULL;
    _Ptr<JSON_Array> b_array = NULL;
    _Nt_array_ptr<const char> a_string = NULL;
    _Nt_array_ptr<const char> b_string = NULL;
    _Nt_array_ptr<const char> key = NULL;
    size_t a_count = 0, b_count = 0, i = 0;
    JSON_Value_Type a_type, b_type;
    a_type = json_value_get_type(a);
    b_type = json_value_get_type(b);
    if (a_type != b_type) {
        return 0;
    }
    switch (a_type) {
        case JSONArray:
            a_array = json_value_get_array(a);
            b_array = json_value_get_array(b);
            a_count = json_array_get_count(a_array);
            b_count = json_array_get_count(b_array);
            if (a_count != b_count) {
                return 0;
            }
            for (i = 0; i < a_count; i++) {
                if (!json_value_equals(json_array_get_value(a_array, i),
                                       json_array_get_value(b_array, i))) {
                    return 0;
                }
            }
            return 1;
        case JSONObject:
            a_object = json_value_get_object(a);
            b_object = json_value_get_object(b);
            a_count = json_object_get_count(a_object);
            b_count = json_object_get_count(b_object);
            if (a_count != b_count) {
                return 0;
            }
            for (i = 0; i < a_count; i++) {
                key = json_object_get_name(a_object, i);
                if (!json_value_equals(json_object_get_value(a_object, key),
                                       json_object_get_value(b_object, key))) {
                    return 0;
                }
            }
            return 1;
        case JSONString:
            a_string = json_value_get_string(a);
            b_string = json_value_get_string(b);
            if (a_string == NULL || b_string == NULL) {
                return 0; /* shouldn't happen */
            }
            return strcmp(a_string, b_string) == 0;
        case JSONBoolean:
            return json_value_get_boolean(a) == json_value_get_boolean(b);
        case JSONNumber:
            return fabs(json_value_get_number(a) - json_value_get_number(b)) < 0.000001; /* EPSILON */
        case JSONError:
            return 1;
        case JSONNull:
            return 1;
        default:
            return 1;
    }
}

JSON_Value_Type json_type(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    return json_value_get_type(value);
}

JSON_Object * json_object (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Object>) {
    return json_value_get_object(value);
}

JSON_Array * json_array  (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Ptr<JSON_Array>) {
    return json_value_get_array(value);
}

const char * json_string (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) : itype(_Nt_array_ptr<const char>) {
    return json_value_get_string(value);
}

double json_number (const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    return json_value_get_number(value);
}

int json_boolean(const JSON_Value *value : itype(_Ptr<const JSON_Value>)) {
    return json_value_get_boolean(value);
}

_Itype_for_any(T) void json_set_allocation_functions(_Ptr<void* (size_t s) : itype(_Array_ptr<T>) byte_count(s)> malloc_fun,
    _Ptr<void (void* : itype(_Array_ptr<T>) byte_count(0))> free_fun) {
    if(malloc_fun || free_fun) {
        #undef parson_malloc
        #undef parson_free
        parson_malloc = malloc_fun;
        parson_free = free_fun;
    }
    return;
}

void json_set_escape_slashes(int escape_slashes) {
    parson_escape_slashes = escape_slashes;
}

#pragma CHECKED_SCOPE pop
