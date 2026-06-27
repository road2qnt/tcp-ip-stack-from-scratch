#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static JsonValue *json_value_alloc(JsonType type)
{
    JsonValue *val = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (val == NULL) return NULL;
    val->type = type;
    return val;
}

// --- PARSER ---

typedef struct {
    const char *pos;
    const char *end;
    int depth;
} JsonParser;

static int json_parse_whitespace(JsonParser *p)
{
    while (p->pos < p->end && isspace((unsigned char)*p->pos)) {
        p->pos++;
    }
    return 0;
}

static int json_parse_char(JsonParser *p, char c)
{
    json_parse_whitespace(p);
    if (p->pos < p->end && *p->pos == c) {
        p->pos++;
        return 1;
    }
    return 0;
}

static JsonValue *json_parse_value(JsonParser *p);

static int json_parse_string_raw(JsonParser *p, char *out, size_t out_size)
{
    size_t i = 0;
    if (!json_parse_char(p, '"')) return 0;

    while (p->pos < p->end && *p->pos != '"' && i + 1 < out_size) {
        if (*p->pos == '\\') {
            p->pos++;
            if (p->pos >= p->end) return 0;
            switch (*p->pos) {
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                case '/': out[i++] = '/'; break;
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case 'r': out[i++] = '\r'; break;
                default: out[i++] = *p->pos; break;
            }
        } else {
            out[i++] = *p->pos;
        }
        p->pos++;
    }
    out[i] = '\0';

    if (!json_parse_char(p, '"')) return 0;
    return 1;
}

static JsonValue *json_parse_string(JsonParser *p)
{
    JsonValue *val = json_value_alloc(JSON_STRING);
    if (val == NULL) return NULL;
    if (!json_parse_string_raw(p, val->data.string, JSON_MAX_STRING)) {
        free(val);
        return NULL;
    }
    return val;
}

static JsonValue *json_parse_number(JsonParser *p)
{
    const char *start = p->pos;
    double num = 0.0;
    int is_negative = 0;

    if (p->pos < p->end && *p->pos == '-') {
        is_negative = 1;
        p->pos++;
    }

    if (p->pos >= p->end || !isdigit((unsigned char)*p->pos)) {
        p->pos = start;
        return NULL;
    }

    while (p->pos < p->end && isdigit((unsigned char)*p->pos)) {
        num = num * 10.0 + (*p->pos - '0');
        p->pos++;
    }

    if (p->pos < p->end && *p->pos == '.') {
        p->pos++;
        double frac = 0.0;
        double div = 1.0;
        while (p->pos < p->end && isdigit((unsigned char)*p->pos)) {
            frac = frac * 10.0 + (*p->pos - '0');
            div *= 10.0;
            p->pos++;
        }
        num += frac / div;
    }

    if (is_negative) num = -num;

    JsonValue *val = json_value_alloc(JSON_NUMBER);
    if (val == NULL) return NULL;
    val->data.number = num;
    return val;
}

static JsonValue *json_parse_object(JsonParser *p)
{
    if (!json_parse_char(p, '{')) return NULL;
    p->depth++;
    if (p->depth > JSON_MAX_DEPTH) return NULL;

    JsonValue *obj = json_value_alloc(JSON_OBJECT);
    if (obj == NULL) return NULL;

    obj->data.object = NULL;
    JsonMember **tail = &obj->data.object;

    if (!json_parse_char(p, '}')) {
        while (1) {
            char key[JSON_MAX_STRING];
            if (!json_parse_string_raw(p, key, JSON_MAX_STRING)) {
                json_free(obj);
                return NULL;
            }
            if (!json_parse_char(p, ':')) {
                json_free(obj);
                return NULL;
            }

            JsonValue *val = json_parse_value(p);
            if (val == NULL) {
                json_free(obj);
                return NULL;
            }

            JsonMember *m = (JsonMember *)calloc(1, sizeof(JsonMember));
            if (m == NULL) {
                json_free(val);
                json_free(obj);
                return NULL;
            }
            strncpy(m->key, key, sizeof(m->key) - 1);
            m->key[sizeof(m->key) - 1] = '\0';
            m->value = val;
            m->next = NULL;
            *tail = m;
            tail = &m->next;

            if (json_parse_char(p, '}')) break;
            if (!json_parse_char(p, ',')) {
                json_free(obj);
                return NULL;
            }
        }
    }

    p->depth--;
    return obj;
}

static JsonValue *json_parse_array(JsonParser *p)
{
    if (!json_parse_char(p, '[')) return NULL;
    p->depth++;
    if (p->depth > JSON_MAX_DEPTH) return NULL;

    JsonValue *arr = json_value_alloc(JSON_ARRAY);
    if (arr == NULL) return NULL;

    arr->data.array.count = 0;

    if (!json_parse_char(p, ']')) {
        while (1) {
            JsonValue *val = json_parse_value(p);
            if (val == NULL) {
                json_free(arr);
                return NULL;
            }
            if (arr->data.array.count >= JSON_MAX_ARRAY) {
                json_free(val);
                json_free(arr);
                return NULL;
            }
            arr->data.array.items[arr->data.array.count++] = val;

            if (json_parse_char(p, ']')) break;
            if (!json_parse_char(p, ',')) {
                json_free(arr);
                return NULL;
            }
        }
    }

    p->depth--;
    return arr;
}

static JsonValue *json_parse_keyword(JsonParser *p, const char *kw, JsonType type)
{
    size_t len = strlen(kw);
    if ((size_t)(p->end - p->pos) < len) return NULL;
    if (strncmp(p->pos, kw, len) != 0) return NULL;
    p->pos += len;
    JsonValue *val = json_value_alloc(type);
    if (val == NULL) return NULL;
    if (type == JSON_BOOL) val->data.bool_val = (kw[0] == 't');
    return val;
}

static JsonValue *json_parse_value(JsonParser *p)
{
    json_parse_whitespace(p);
    if (p->pos >= p->end) return NULL;

    char c = *p->pos;
    if (c == '{') return json_parse_object(p);
    if (c == '[') return json_parse_array(p);
    if (c == '"') return json_parse_string(p);
    if (c == '-' || isdigit((unsigned char)c)) return json_parse_number(p);
    if (c == 't') return json_parse_keyword(p, "true", JSON_BOOL);
    if (c == 'f') return json_parse_keyword(p, "false", JSON_BOOL);
    if (c == 'n') return json_parse_keyword(p, "null", JSON_NULL);

    return NULL;
}

JsonValue *json_parse(const char *input)
{
    if (input == NULL) return NULL;

    JsonParser p;
    p.pos = input;
    p.end = input + strlen(input);
    p.depth = 0;

    JsonValue *val = json_parse_value(&p);
    if (val == NULL) return NULL;

    json_parse_whitespace(&p);
    if (p.pos != p.end) {
        json_free(val);
        return NULL;
    }

    return val;
}

void json_free(JsonValue *val)
{
    if (val == NULL) return;

    if (val->type == JSON_OBJECT) {
        JsonMember *m = val->data.object;
        while (m != NULL) {
            JsonMember *next = m->next;
            json_free(m->value);
            free(m);
            m = next;
        }
    } else if (val->type == JSON_ARRAY) {
        for (size_t i = 0; i < val->data.array.count; i++) {
            json_free(val->data.array.items[i]);
        }
    }

    free(val);
}

// --- WRITER ---

static void json_write_indent(FILE *f, int indent)
{
    for (int i = 0; i < indent; i++) {
        fputc(' ', f);
    }
}

static void json_write_string(FILE *f, const char *s)
{
    fputc('"', f);
    while (*s) {
        switch (*s) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\t': fputs("\\t", f); break;
            case '\r': fputs("\\r", f); break;
            default: fputc(*s, f); break;
        }
        s++;
    }
    fputc('"', f);
}

void json_write(FILE *f, const JsonValue *val, int indent)
{
    if (val == NULL || f == NULL) return;

    switch (val->type) {
        case JSON_STRING:
            json_write_string(f, val->data.string);
            break;

        case JSON_NUMBER:
            if (val->data.number == (long long)val->data.number) {
                fprintf(f, "%lld", (long long)val->data.number);
            } else {
                fprintf(f, "%g", val->data.number);
            }
            break;

        case JSON_BOOL:
            fputs(val->data.bool_val ? "true" : "false", f);
            break;

        case JSON_NULL:
            fputs("null", f);
            break;

        case JSON_OBJECT:
            fputs("{\n", f);
            {
                JsonMember *m = val->data.object;
                while (m != NULL) {
                    json_write_indent(f, indent + 2);
                    json_write_string(f, m->key);
                    fputs(": ", f);
                    json_write(f, m->value, indent + 2);
                    if (m->next != NULL) fputc(',', f);
                    fputc('\n', f);
                    m = m->next;
                }
            }
            json_write_indent(f, indent);
            fputc('}', f);
            break;

        case JSON_ARRAY:
            if (val->data.array.count == 0) {
                fputs("[]", f);
            } else {
                fputs("[\n", f);
                for (size_t i = 0; i < val->data.array.count; i++) {
                    json_write_indent(f, indent + 2);
                    json_write(f, val->data.array.items[i], indent + 2);
                    if (i + 1 < val->data.array.count) fputc(',', f);
                    fputc('\n', f);
                }
                json_write_indent(f, indent);
                fputc(']', f);
            }
            break;
    }
}

// --- HELPERS ---

JsonValue *json_object_get(const JsonValue *obj, const char *key)
{
    if (obj == NULL || obj->type != JSON_OBJECT || key == NULL) return NULL;

    JsonMember *m = obj->data.object;
    while (m != NULL) {
        if (strcmp(m->key, key) == 0) return m->value;
        m = m->next;
    }
    return NULL;
}

JsonValue *json_array_get(const JsonValue *arr, size_t index)
{
    if (arr == NULL || arr->type != JSON_ARRAY || index >= arr->data.array.count) {
        return NULL;
    }
    return arr->data.array.items[index];
}

// --- CONSTRUCTORS ---

JsonValue *json_create_string(const char *s)
{
    JsonValue *val = json_value_alloc(JSON_STRING);
    if (val == NULL) return NULL;
    if (s) {
        strncpy(val->data.string, s, JSON_MAX_STRING - 1);
        val->data.string[JSON_MAX_STRING - 1] = '\0';
    }
    return val;
}

JsonValue *json_create_number(double n)
{
    JsonValue *val = json_value_alloc(JSON_NUMBER);
    if (val == NULL) return NULL;
    val->data.number = n;
    return val;
}

JsonValue *json_create_bool(int b)
{
    JsonValue *val = json_value_alloc(JSON_BOOL);
    if (val == NULL) return NULL;
    val->data.bool_val = b;
    return val;
}

JsonValue *json_create_object(void)
{
    return json_value_alloc(JSON_OBJECT);
}

JsonValue *json_create_array(void)
{
    JsonValue *val = json_value_alloc(JSON_ARRAY);
    if (val) val->data.array.count = 0;
    return val;
}

void json_object_add(JsonValue *obj, const char *key, JsonValue *value)
{
    if (obj == NULL || obj->type != JSON_OBJECT || key == NULL || value == NULL) return;

    JsonMember *m = (JsonMember *)calloc(1, sizeof(JsonMember));
    if (m == NULL) return;

    strncpy(m->key, key, sizeof(m->key) - 1);
    m->key[sizeof(m->key) - 1] = '\0';
    m->value = value;

    // Add to end of list
    if (obj->data.object == NULL) {
        obj->data.object = m;
    } else {
        JsonMember *last = obj->data.object;
        while (last->next != NULL) last = last->next;
        last->next = m;
    }
}

void json_array_add(JsonValue *arr, JsonValue *value)
{
    if (arr == NULL || arr->type != JSON_ARRAY || value == NULL) return;
    if (arr->data.array.count >= JSON_MAX_ARRAY) return;
    arr->data.array.items[arr->data.array.count++] = value;
}
