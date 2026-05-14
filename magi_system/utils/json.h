#ifndef JSON_H
#define JSON_H

#include <stdio.h>
#include <stddef.h>

#define JSON_MAX_STRING 2048
#define JSON_MAX_ARRAY 256
#define JSON_MAX_DEPTH 32

typedef struct JsonValue JsonValue;
typedef struct JsonMember JsonMember;

struct JsonMember {
    char key[JSON_MAX_STRING];
    JsonValue *value;
    JsonMember *next;
};

typedef enum {
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_NULL
} JsonType;

struct JsonValue {
    JsonType type;
    union {
        JsonMember *object;
        struct {
            JsonValue *items[JSON_MAX_ARRAY];
            size_t count;
        } array;
        char string[JSON_MAX_STRING];
        double number;
        int bool_val;
    } data;
};

// Parse JSON string into a JsonValue tree. Returns NULL on error.
JsonValue *json_parse(const char *input);

// Free a parsed JSON tree.
void json_free(JsonValue *val);

// Write a JSON value to a file with indentation.
void json_write(FILE *f, const JsonValue *val, int indent);

// Helper: get a member from a JSON object by key. Returns NULL if not found.
JsonValue *json_object_get(const JsonValue *obj, const char *key);

// Helper: get an item from a JSON array by index. Returns NULL if out of bounds.
JsonValue *json_array_get(const JsonValue *arr, size_t index);

// Helper: create simple JSON values (for building output)
JsonValue *json_create_string(const char *s);
JsonValue *json_create_number(double n);
JsonValue *json_create_bool(int b);
JsonValue *json_create_object(void);
JsonValue *json_create_array(void);

// Helper: add member to object or item to array
void json_object_add(JsonValue *obj, const char *key, JsonValue *value);
void json_array_add(JsonValue *arr, JsonValue *value);

#endif
