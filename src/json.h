#ifndef JSON_H
#define JSON_H

#include <stddef.h>

/* JSON value types */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;
    union {
        int boolean;
        double number;
        char *string;       /* for JSON_STRING */
        struct {
            JsonValue *items;
            int count;
        } array;
        struct {
            char **keys;
            JsonValue *values;
            int count;
        } object;
    };
};

/* Parse a JSON string. Returns NULL on failure. Caller must json_free(). */
JsonValue *json_parse(const char *text);

/* Free a JsonValue tree */
void json_free(JsonValue *v);

/* Access helpers */
JsonValue *json_object_get(const JsonValue *obj, const char *key);
JsonValue *json_array_get(const JsonValue *arr, int index);
const char *json_string(const JsonValue *v);
double json_number(const JsonValue *v);
int json_bool(const JsonValue *v);

/* Serialize a JsonValue to a string (caller frees) */
char *json_stringify(const JsonValue *v, int pretty);

/* Create new values */
JsonValue *json_new_object(void);
JsonValue *json_new_array(void);
JsonValue *json_new_string(const char *s);
JsonValue *json_new_number(double n);
JsonValue *json_new_bool(int b);
JsonValue *json_new_null(void);

/* Modify objects/arrays */
void json_object_set(JsonValue *obj, const char *key, JsonValue *val);
void json_array_push(JsonValue *arr, JsonValue *val);

#endif /* JSON_H */
