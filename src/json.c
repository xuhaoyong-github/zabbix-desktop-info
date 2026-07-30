#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ---- internal parser ---- */

typedef struct {
    const char *p;
    int error;
} Parser;

static void skip_ws(Parser *ps)
{
    while (*ps->p && isspace((unsigned char)*ps->p))
        ps->p++;
}

static JsonValue *parse_value(Parser *ps);

static char *decode_string(Parser *ps)
{
    /* assumes ps->p points at opening quote */
    ps->p++; /* skip opening quote */
    /* First pass: calculate length */
    const char *start = ps->p;
    int len = 0;
    while (*ps->p && *ps->p != '"') {
        if (*ps->p == '\\') {
            ps->p++;
            if (*ps->p) { ps->p++; len++; }
        } else {
            /* count UTF-8 bytes */
            unsigned char c = (unsigned char)*ps->p;
            if (c >= 0x80) {
                int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
                while (n-- && *ps->p) { ps->p++; len++; }
            } else {
                ps->p++;
                len++;
            }
        }
    }
    /* Allocate and copy */
    char *str = (char *)malloc(len + 1);
    if (!str) { ps->error = 1; return NULL; }
    const char *s = start;
    int i = 0;
    while (s < ps->p) {
        if (*s == '\\') {
            s++;
            switch (*s) {
                case 'n': str[i++] = '\n'; break;
                case 't': str[i++] = '\t'; break;
                case 'r': str[i++] = '\r'; break;
                case 'b': str[i++] = '\b'; break;
                case 'f': str[i++] = '\f'; break;
                case '/': str[i++] = '/'; break;
                case '\\': str[i++] = '\\'; break;
                case '"': str[i++] = '"'; break;
                case 'u': {
                    /* skip 4 hex chars, keep as UTF-8 if possible */
                    unsigned int cp = 0;
                    int h;
                    for (h = 0; h < 4 && s[1]; h++) {
                        s++;
                        char hc = *s;
                        if (hc >= '0' && hc <= '9') cp = cp * 16 + (hc - '0');
                        else if (hc >= 'a' && hc <= 'f') cp = cp * 16 + (hc - 'a' + 10);
                        else if (hc >= 'A' && hc <= 'F') cp = cp * 16 + (hc - 'A' + 10);
                    }
                    /* encode as UTF-8 */
                    if (cp < 0x80) {
                        str[i++] = (char)cp;
                    } else if (cp < 0x800) {
                        str[i++] = (char)(0xC0 | (cp >> 6));
                        str[i++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        str[i++] = (char)(0xE0 | (cp >> 12));
                        str[i++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        str[i++] = (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: str[i++] = *s; break;
            }
            s++;
        } else {
            str[i++] = *s++;
        }
    }
    str[i] = '\0';
    if (*ps->p == '"') ps->p++; /* skip closing quote */
    return str;
}

static JsonValue *parse_string(Parser *ps)
{
    char *s = decode_string(ps);
    if (!s) return NULL;
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { free(s); ps->error = 1; return NULL; }
    v->type = JSON_STRING;
    v->string = s;
    return v;
}

static JsonValue *parse_number(Parser *ps)
{
    char *end;
    double d = strtod(ps->p, &end);
    if (end == ps->p) { ps->error = 1; return NULL; }
    ps->p = end;
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { ps->error = 1; return NULL; }
    v->type = JSON_NUMBER;
    v->number = d;
    return v;
}

static JsonValue *parse_object(Parser *ps)
{
    ps->p++; /* skip '{' */
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { ps->error = 1; return NULL; }
    v->type = JSON_OBJECT;
    v->object.count = 0;
    int cap = 4;
    v->object.keys = (char **)malloc(cap * sizeof(char *));
    v->object.values = (JsonValue *)malloc(cap * sizeof(JsonValue));

    skip_ws(ps);
    if (*ps->p == '}') { ps->p++; return v; }

    while (1) {
        skip_ws(ps);
        if (*ps->p != '"') { ps->error = 1; json_free(v); return NULL; }
        char *key = decode_string(ps);
        if (!key) { json_free(v); return NULL; }
        skip_ws(ps);
        if (*ps->p != ':') { ps->error = 1; free(key); json_free(v); return NULL; }
        ps->p++;
        JsonValue *val = parse_value(ps);
        if (!val) { free(key); json_free(v); return NULL; }

        if (v->object.count >= cap) {
            cap *= 2;
            v->object.keys = (char **)realloc(v->object.keys, cap * sizeof(char *));
            v->object.values = (JsonValue *)realloc(v->object.values, cap * sizeof(JsonValue));
        }
        v->object.keys[v->object.count] = key;
        v->object.values[v->object.count] = *val;
        free(val); /* shallow free, data moved */
        v->object.count++;

        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; break; }
        ps->error = 1; json_free(v); return NULL;
    }
    return v;
}

static JsonValue *parse_array(Parser *ps)
{
    ps->p++; /* skip '[' */
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!v) { ps->error = 1; return NULL; }
    v->type = JSON_ARRAY;
    v->array.count = 0;
    int cap = 4;
    v->array.items = (JsonValue *)malloc(cap * sizeof(JsonValue));

    skip_ws(ps);
    if (*ps->p == ']') { ps->p++; return v; }

    while (1) {
        JsonValue *val = parse_value(ps);
        if (!val) { json_free(v); return NULL; }
        if (v->array.count >= cap) {
            cap *= 2;
            v->array.items = (JsonValue *)realloc(v->array.items, cap * sizeof(JsonValue));
        }
        v->array.items[v->array.count] = *val;
        free(val);
        v->array.count++;
        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; break; }
        ps->error = 1; json_free(v); return NULL;
    }
    return v;
}

static JsonValue *parse_value(Parser *ps)
{
    skip_ws(ps);
    if (ps->error) return NULL;
    char c = *ps->p;
    if (c == '{') return parse_object(ps);
    if (c == '[') return parse_array(ps);
    if (c == '"') return parse_string(ps);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(ps);
    if (strncmp(ps->p, "true", 4) == 0) {
        ps->p += 4;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        v->type = JSON_BOOL; v->boolean = 1;
        return v;
    }
    if (strncmp(ps->p, "false", 5) == 0) {
        ps->p += 5;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        v->type = JSON_BOOL; v->boolean = 0;
        return v;
    }
    if (strncmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        v->type = JSON_NULL;
        return v;
    }
    ps->error = 1;
    return NULL;
}

JsonValue *json_parse(const char *text)
{
    if (!text) return NULL;
    Parser ps = { text, 0 };
    JsonValue *v = parse_value(&ps);
    if (ps.error) {
        if (v) json_free(v);
        return NULL;
    }
    return v;
}

/* Free contents of a JsonValue (child strings, nested arrays/objects)
 * but NOT the JsonValue struct itself.
 * This is used for items embedded in a parent array/object's malloc'd block,
 * where calling free() on the item address would corrupt the heap. */
static void json_free_contents(JsonValue *v)
{
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            free(v->string);
            break;
        case JSON_ARRAY:
            for (int i = 0; i < v->array.count; i++)
                json_free_contents(&v->array.items[i]);
            free(v->array.items);
            break;
        case JSON_OBJECT:
            for (int i = 0; i < v->object.count; i++) {
                free(v->object.keys[i]);
                json_free_contents(&v->object.values[i]);
            }
            free(v->object.keys);
            free(v->object.values);
            break;
        default:
            break;
    }
}

void json_free(JsonValue *v)
{
    if (!v) return;
    json_free_contents(v);
    free(v);
}

/* ---- access helpers ---- */

JsonValue *json_object_get(const JsonValue *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < obj->object.count; i++) {
        if (strcmp(obj->object.keys[i], key) == 0)
            return &obj->object.values[i];
    }
    return NULL;
}

JsonValue *json_array_get(const JsonValue *arr, int index)
{
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    if (index < 0 || index >= arr->array.count) return NULL;
    return &arr->array.items[index];
}

const char *json_string(const JsonValue *v)
{
    if (!v || v->type != JSON_STRING) return NULL;
    return v->string;
}

double json_number(const JsonValue *v)
{
    if (!v) return 0;
    if (v->type == JSON_NUMBER) return v->number;
    if (v->type == JSON_STRING && v->string) return atof(v->string);
    return 0;
}

int json_bool(const JsonValue *v)
{
    if (!v || v->type != JSON_BOOL) return 0;
    return v->boolean;
}

/* ---- serialization ---- */

static void serialize(const JsonValue *v, char **buf, int *len, int *cap, int indent, int pretty);

static void ensure_cap(char **buf, int *len, int *cap, int need)
{
    if (*len + need >= *cap) {
        while (*len + need >= *cap) *cap *= 2;
        *buf = (char *)realloc(*buf, *cap);
    }
}

static void append_str(char **buf, int *len, int *cap, const char *s)
{
    int n = (int)strlen(s);
    ensure_cap(buf, len, cap, n);
    memcpy(*buf + *len, s, n);
    *len += n;
}

static void append_char(char **buf, int *len, int *cap, char c)
{
    ensure_cap(buf, len, cap, 1);
    (*buf)[(*len)++] = c;
}

static void append_indent(char **buf, int *len, int *cap, int indent)
{
    for (int i = 0; i < indent; i++)
        append_char(buf, len, cap, ' ');
}

static void serialize_string(const char *s, char **buf, int *len, int *cap)
{
    append_char(buf, len, cap, '"');
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c >= 0x80) {
            /* pass UTF-8 through */
            int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
            while (n-- && *s) append_char(buf, len, cap, *s++);
        } else {
            switch (c) {
                case '"': append_str(buf, len, cap, "\\\""); break;
                case '\\': append_str(buf, len, cap, "\\\\"); break;
                case '\n': append_str(buf, len, cap, "\\n"); break;
                case '\t': append_str(buf, len, cap, "\\t"); break;
                case '\r': append_str(buf, len, cap, "\\r"); break;
                default:
                    if (c < 0x20) {
                        char esc[8];
                        snprintf(esc, sizeof(esc), "\\u%04x", c);
                        append_str(buf, len, cap, esc);
                    } else {
                        append_char(buf, len, cap, c);
                    }
                    break;
            }
            s++;
        }
    }
    append_char(buf, len, cap, '"');
}

static void serialize(const JsonValue *v, char **buf, int *len, int *cap, int indent, int pretty)
{
    if (!v) { append_str(buf, len, cap, "null"); return; }
    switch (v->type) {
        case JSON_NULL:
            append_str(buf, len, cap, "null");
            break;
        case JSON_BOOL:
            append_str(buf, len, cap, v->boolean ? "true" : "false");
            break;
        case JSON_NUMBER: {
            char numbuf[64];
            snprintf(numbuf, sizeof(numbuf), "%.17g", v->number);
            append_str(buf, len, cap, numbuf);
            break;
        }
        case JSON_STRING:
            serialize_string(v->string, buf, len, cap);
            break;
        case JSON_ARRAY:
            append_char(buf, len, cap, '[');
            if (v->array.count > 0 && pretty) append_char(buf, len, cap, '\n');
            for (int i = 0; i < v->array.count; i++) {
                if (pretty) append_indent(buf, len, cap, indent + 2);
                serialize(&v->array.items[i], buf, len, cap, indent + 2, pretty);
                if (i < v->array.count - 1) append_char(buf, len, cap, ',');
                if (pretty) append_char(buf, len, cap, '\n');
            }
            if (v->array.count > 0 && pretty) append_indent(buf, len, cap, indent);
            append_char(buf, len, cap, ']');
            break;
        case JSON_OBJECT:
            append_char(buf, len, cap, '{');
            if (v->object.count > 0 && pretty) append_char(buf, len, cap, '\n');
            for (int i = 0; i < v->object.count; i++) {
                if (pretty) append_indent(buf, len, cap, indent + 2);
                serialize_string(v->object.keys[i], buf, len, cap);
                append_str(buf, len, cap, pretty ? ": " : ":");
                serialize(&v->object.values[i], buf, len, cap, indent + 2, pretty);
                if (i < v->object.count - 1) append_char(buf, len, cap, ',');
                if (pretty) append_char(buf, len, cap, '\n');
            }
            if (v->object.count > 0 && pretty) append_indent(buf, len, cap, indent);
            append_char(buf, len, cap, '}');
            break;
    }
}

char *json_stringify(const JsonValue *v, int pretty)
{
    int cap = 256;
    int len = 0;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    serialize(v, &buf, &len, &cap, 0, pretty);
    ensure_cap(&buf, &len, &cap, 1);
    buf[len] = '\0';
    return buf;
}

/* ---- constructors ---- */

JsonValue *json_new_object(void)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_OBJECT;
    v->object.keys = (char **)malloc(4 * sizeof(char *));
    v->object.values = (JsonValue *)malloc(4 * sizeof(JsonValue));
    v->object.count = 0;
    return v;
}

JsonValue *json_new_array(void)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_ARRAY;
    v->array.items = (JsonValue *)malloc(4 * sizeof(JsonValue));
    v->array.count = 0;
    return v;
}

JsonValue *json_new_string(const char *s)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_STRING;
    v->string = strdup(s ? s : "");
    return v;
}

JsonValue *json_new_number(double n)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_NUMBER;
    v->number = n;
    return v;
}

JsonValue *json_new_bool(int b)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_BOOL;
    v->boolean = b ? 1 : 0;
    return v;
}

JsonValue *json_new_null(void)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_NULL;
    return v;
}

static int obj_cap(JsonValue *obj)
{
    int cap = 4;
    while (cap <= obj->object.count) cap *= 2;
    return cap;
}

void json_object_set(JsonValue *obj, const char *key, JsonValue *val)
{
    if (!obj || obj->type != JSON_OBJECT) return;
    /* Check if key exists */
    for (int i = 0; i < obj->object.count; i++) {
        if (strcmp(obj->object.keys[i], key) == 0) {
            json_free_contents(&obj->object.values[i]);
            obj->object.values[i] = *val;
            free(val);
            return;
        }
    }
    int cap = obj_cap(obj);
    obj->object.keys = (char **)realloc(obj->object.keys, cap * sizeof(char *));
    obj->object.values = (JsonValue *)realloc(obj->object.values, cap * sizeof(JsonValue));
    obj->object.keys[obj->object.count] = strdup(key);
    obj->object.values[obj->object.count] = *val;
    free(val);
    obj->object.count++;
}

static int arr_cap(JsonValue *arr)
{
    int cap = 4;
    while (cap <= arr->array.count) cap *= 2;
    return cap;
}

void json_array_push(JsonValue *arr, JsonValue *val)
{
    if (!arr || arr->type != JSON_ARRAY) return;
    int cap = arr_cap(arr);
    arr->array.items = (JsonValue *)realloc(arr->array.items, cap * sizeof(JsonValue));
    arr->array.items[arr->array.count] = *val;
    free(val);
    arr->array.count++;
}
