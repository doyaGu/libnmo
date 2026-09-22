/**
 * @file nmo_cli_record.c
 * @brief Format-neutral output records for CLI commands.
 */
#include "nmo_cli_record.h"

#include "nmo_cli_json.h"
#include "nmo_cli_output.h"

#include "export/nmo_hexdump.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum record_kind {
    RECORD_UINT,
    RECORD_INT,
    RECORD_REAL,
    RECORD_BOOL,
    RECORD_STR,
    RECORD_NULL,
    RECORD_REF,
    RECORD_VEC3,
    RECORD_RAW,
    RECORD_HEADING,
    RECORD_BYTES,
    RECORD_REAL_LIST,
    RECORD_UINT_LIST,
    RECORD_STR_LIST,
    RECORD_ARRAY
} record_kind_t;

struct nmo_cli_record_array {
    nmo_cli_record_t **items;
    size_t count;
    size_t capacity;
    char *heading;
    char *empty_text;
    bool omit_empty;
};

typedef struct record_field {
    record_kind_t kind;
    char *key;        /* JSON key, NULL when omitted from JSON */
    char *label;      /* text label, NULL when omitted from text */
    char *text;       /* rendered text value, NULL when omitted from text */
    char *name_key;   /* RECORD_REF: JSON key for the name */
    char *str;        /* RECORD_STR / RECORD_REF name */
    uint64_t u;
    int64_t i;
    double d;
    double v[3];      /* RECORD_VEC3 */
    double *reals;    /* RECORD_REAL_LIST */
    uint64_t *uints;  /* RECORD_UINT_LIST */
    char **strs;      /* RECORD_STR_LIST (entries may be NULL) */
    unsigned char *bytes; /* RECORD_BYTES: the emitted prefix; u = total size */
    size_t list_count;
    bool b;
    nmo_cli_record_array_t *array; /* RECORD_ARRAY; heap-allocated so the
                                      handle survives parent growth */
} record_field_t;

struct nmo_cli_record {
    record_field_t *fields;
    size_t count;
    size_t capacity;
    char *summary;
};

static char *dup_str(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s) + 1u;
    char *copy = (char *)malloc(n);
    if (copy) {
        memcpy(copy, s, n);
    }
    return copy;
}

static bool set_str(char **slot, const char *value)
{
    char *copy = dup_str(value);
    if (value && !copy) {
        return false;
    }
    free(*slot);
    *slot = copy;
    return true;
}

static bool set_formatted(char **slot, const char *format, ...)
{
    char buf[128];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (n < 0) {
        return false;
    }
    if ((size_t)n < sizeof(buf)) {
        return set_str(slot, buf);
    }
    char *big = (char *)malloc((size_t)n + 1u);
    if (!big) {
        return false;
    }
    va_start(args, format);
    vsnprintf(big, (size_t)n + 1u, format, args);
    va_end(args);
    free(*slot);
    *slot = big;
    return true;
}

nmo_cli_record_t *nmo_cli_record_new(void)
{
    return (nmo_cli_record_t *)calloc(1u, sizeof(nmo_cli_record_t));
}

static void field_dispose(record_field_t *field)
{
    free(field->key);
    free(field->label);
    free(field->text);
    free(field->name_key);
    free(field->str);
    free(field->reals);
    free(field->uints);
    free(field->bytes);
    if (field->strs) {
        for (size_t i = 0; i < field->list_count; ++i) {
            free(field->strs[i]);
        }
        free(field->strs);
    }
    if (field->array) {
        for (size_t i = 0; i < field->array->count; ++i) {
            nmo_cli_record_free(field->array->items[i]);
        }
        free(field->array->items);
        free(field->array->heading);
        free(field->array->empty_text);
        free(field->array);
    }
    memset(field, 0, sizeof(*field));
}

void nmo_cli_record_free(nmo_cli_record_t *record)
{
    if (!record) {
        return;
    }
    for (size_t i = 0; i < record->count; ++i) {
        field_dispose(&record->fields[i]);
    }
    free(record->fields);
    free(record->summary);
    free(record);
}

size_t nmo_cli_record_field_count(const nmo_cli_record_t *record)
{
    return record ? record->count : 0u;
}

static record_field_t *field_append(nmo_cli_record_t *record,
                                    record_kind_t kind,
                                    const char *key,
                                    const char *label)
{
    if (!record) {
        return NULL;
    }
    if (record->count == record->capacity) {
        size_t new_capacity = record->capacity ? record->capacity * 2u : 8u;
        record_field_t *grown = (record_field_t *)realloc(
            record->fields, new_capacity * sizeof(*grown));
        if (!grown) {
            return NULL;
        }
        record->fields = grown;
        record->capacity = new_capacity;
    }
    record_field_t *field = &record->fields[record->count];
    memset(field, 0, sizeof(*field));
    field->kind = kind;
    if (!set_str(&field->key, key) || !set_str(&field->label, label)) {
        field_dispose(field);
        return NULL;
    }
    record->count++;
    return field;
}

static record_field_t *field_last(nmo_cli_record_t *record)
{
    if (!record || record->count == 0u) {
        return NULL;
    }
    return &record->fields[record->count - 1u];
}

/* Undo a partially built trailing field after an allocation failure. */
static bool field_fail(nmo_cli_record_t *record)
{
    record_field_t *field = field_last(record);
    if (field) {
        field_dispose(field);
        record->count--;
    }
    return false;
}

bool nmo_cli_record_uint(nmo_cli_record_t *record, const char *key,
                         const char *label, uint64_t value)
{
    record_field_t *field = field_append(record, RECORD_UINT, key, label);
    if (!field) {
        return false;
    }
    field->u = value;
    if (!set_formatted(&field->text, "%" PRIu64, value)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_int(nmo_cli_record_t *record, const char *key,
                        const char *label, int64_t value)
{
    record_field_t *field = field_append(record, RECORD_INT, key, label);
    if (!field) {
        return false;
    }
    field->i = value;
    if (!set_formatted(&field->text, "%" PRId64, value)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_real(nmo_cli_record_t *record, const char *key,
                         const char *label, double value,
                         const char *text_format)
{
    record_field_t *field = field_append(record, RECORD_REAL, key, label);
    if (!field) {
        return false;
    }
    field->d = value;
    if (!set_formatted(&field->text, text_format ? text_format : "%g", value)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_bool(nmo_cli_record_t *record, const char *key,
                         const char *label, bool value)
{
    record_field_t *field = field_append(record, RECORD_BOOL, key, label);
    if (!field) {
        return false;
    }
    field->b = value;
    if (!set_str(&field->text, value ? "true" : "false")) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_str(nmo_cli_record_t *record, const char *key,
                        const char *label, const char *value)
{
    record_field_t *field = field_append(record, RECORD_STR, key, label);
    if (!field) {
        return false;
    }
    const char *shown = value ? value : "";
    if (!set_str(&field->str, shown) || !set_str(&field->text, shown)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_str_opt(nmo_cli_record_t *record, const char *key,
                            const char *label, const char *value,
                            const char *text_fallback)
{
    if (value && value[0] != '\0') {
        return nmo_cli_record_str(record, key, label, value);
    }
    if (!text_fallback || !label) {
        return true;
    }
    return nmo_cli_record_text(record, label, text_fallback);
}

bool nmo_cli_record_hex32(nmo_cli_record_t *record, const char *key,
                          const char *label, uint32_t value)
{
    record_field_t *field = field_append(record, RECORD_STR, key, label);
    if (!field) {
        return false;
    }
    if (!set_formatted(&field->str, "0x%08X", value) ||
        !set_str(&field->text, field->str)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_vec3(nmo_cli_record_t *record, const char *key,
                         const char *label, double x, double y, double z,
                         const char *component_format)
{
    record_field_t *field = field_append(record, RECORD_VEC3, key, label);
    if (!field) {
        return false;
    }
    field->v[0] = x;
    field->v[1] = y;
    field->v[2] = z;
    const char *fmt = component_format ? component_format : "%.4f";
    char cx[64], cy[64], cz[64];
    snprintf(cx, sizeof(cx), fmt, x);
    snprintf(cy, sizeof(cy), fmt, y);
    snprintf(cz, sizeof(cz), fmt, z);
    if (!set_formatted(&field->text, "(%s, %s, %s)", cx, cy, cz)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_null(nmo_cli_record_t *record, const char *key,
                         const char *label, const char *text)
{
    record_field_t *field = field_append(record, RECORD_NULL, key, label);
    if (!field) {
        return false;
    }
    if (text && !set_str(&field->text, text)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_text(nmo_cli_record_t *record, const char *label,
                         const char *text)
{
    record_field_t *field = field_append(record, RECORD_NULL, NULL, label);
    if (!field) {
        return false;
    }
    if (!set_str(&field->text, text ? text : "")) {
        return field_fail(record);
    }
    return true;
}

static bool record_ref_impl(nmo_cli_record_t *record, const char *id_key,
                            const char *name_key, const char *label,
                            uint64_t id, const char *name,
                            const char *none_text, bool omit_zero_json)
{
    record_field_t *field = field_append(record, RECORD_REF, id_key, label);
    if (!field) {
        return false;
    }
    field->u = id;
    field->b = omit_zero_json;
    bool ok = set_str(&field->name_key, name_key);
    if (ok && name && name[0] != '\0') {
        ok = set_str(&field->str, name);
    }
    if (ok) {
        if (id == 0u) {
            ok = none_text ? set_str(&field->text, none_text) : true;
        } else if (field->str) {
            ok = set_formatted(&field->text, "#%" PRIu64 " (%s)", id, field->str);
        } else {
            ok = set_formatted(&field->text, "#%" PRIu64, id);
        }
    }
    if (!ok) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_raw(nmo_cli_record_t *record, const char *text)
{
    record_field_t *field = field_append(record, RECORD_RAW, NULL, NULL);
    if (!field) {
        return false;
    }
    if (!set_str(&field->text, text ? text : "")) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_heading(nmo_cli_record_t *record, const char *title)
{
    record_field_t *field = field_append(record, RECORD_HEADING, NULL, NULL);
    if (!field) {
        return false;
    }
    if (!set_str(&field->text, title ? title : "")) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_hex_bytes(nmo_cli_record_t *record, const char *label,
                              const void *bytes, size_t size,
                              size_t max_bytes)
{
    record_field_t *field = field_append(record, RECORD_BYTES, NULL, label);
    if (!field) {
        return false;
    }
    if (!bytes) {
        size = 0u;
    }
    size_t emit = size;
    if (max_bytes > 0u && emit > max_bytes) {
        emit = max_bytes;
    }
    field->u = size;
    field->list_count = emit;
    if (emit > 0u) {
        field->bytes = (unsigned char *)malloc(emit);
        if (!field->bytes) {
            return field_fail(record);
        }
        memcpy(field->bytes, bytes, emit);
    }
    return true;
}

bool nmo_cli_record_real_list(nmo_cli_record_t *record, const char *key,
                              const char *label, const double *values,
                              size_t count, const char *text)
{
    record_field_t *field = field_append(record, RECORD_REAL_LIST, key, label);
    if (!field) {
        return false;
    }
    if (count > 0u) {
        field->reals = (double *)malloc(count * sizeof(double));
        if (!field->reals || !values) {
            return field_fail(record);
        }
        memcpy(field->reals, values, count * sizeof(double));
    }
    field->list_count = count;
    if (text && !set_str(&field->text, text)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_uint_list(nmo_cli_record_t *record, const char *key,
                              const char *label, const uint64_t *values,
                              size_t count, const char *text)
{
    record_field_t *field = field_append(record, RECORD_UINT_LIST, key, label);
    if (!field) {
        return false;
    }
    if (count > 0u) {
        field->uints = (uint64_t *)malloc(count * sizeof(uint64_t));
        if (!field->uints || !values) {
            return field_fail(record);
        }
        memcpy(field->uints, values, count * sizeof(uint64_t));
    }
    field->list_count = count;
    if (text && !set_str(&field->text, text)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_str_list(nmo_cli_record_t *record, const char *key,
                             const char *label, const char *const *values,
                             size_t count, const char *text)
{
    record_field_t *field = field_append(record, RECORD_STR_LIST, key, label);
    if (!field) {
        return false;
    }
    if (count > 0u) {
        field->strs = (char **)calloc(count, sizeof(char *));
        if (!field->strs || !values) {
            return field_fail(record);
        }
        field->list_count = count;
        for (size_t i = 0; i < count; ++i) {
            if (values[i] && !set_str(&field->strs[i], values[i])) {
                return field_fail(record);
            }
        }
    }
    field->list_count = count;
    if (text && !set_str(&field->text, text)) {
        return field_fail(record);
    }
    return true;
}

bool nmo_cli_record_ref(nmo_cli_record_t *record, const char *id_key,
                        const char *name_key, const char *label,
                        uint64_t id, const char *name, const char *none_text)
{
    return record_ref_impl(record, id_key, name_key, label, id, name,
                           none_text, false);
}

bool nmo_cli_record_ref_opt(nmo_cli_record_t *record, const char *id_key,
                            const char *name_key, const char *label,
                            uint64_t id, const char *name,
                            const char *none_text)
{
    return record_ref_impl(record, id_key, name_key, label, id, name,
                           none_text, true);
}

bool nmo_cli_record_set_text(nmo_cli_record_t *record, const char *text)
{
    record_field_t *field = field_last(record);
    if (!field) {
        return false;
    }
    return set_str(&field->text, text);
}

void nmo_cli_record_text_only(nmo_cli_record_t *record)
{
    record_field_t *field = field_last(record);
    if (!field) {
        return;
    }
    free(field->key);
    field->key = NULL;
    free(field->name_key);
    field->name_key = NULL;
}

nmo_cli_record_array_t *nmo_cli_record_array(nmo_cli_record_t *record,
                                             const char *key,
                                             const char *label)
{
    record_field_t *field = field_append(record, RECORD_ARRAY, key, label);
    if (!field) {
        return NULL;
    }
    field->array = (nmo_cli_record_array_t *)calloc(1u, sizeof(*field->array));
    if (!field->array) {
        field_fail(record);
        return NULL;
    }
    return field->array;
}

bool nmo_cli_record_array_add(nmo_cli_record_array_t *array,
                              nmo_cli_record_t *item)
{
    if (!array || !item) {
        nmo_cli_record_free(item);
        return false;
    }
    if (array->count == array->capacity) {
        size_t new_capacity = array->capacity ? array->capacity * 2u : 8u;
        nmo_cli_record_t **grown = (nmo_cli_record_t **)realloc(
            array->items, new_capacity * sizeof(*grown));
        if (!grown) {
            nmo_cli_record_free(item);
            return false;
        }
        array->items = grown;
        array->capacity = new_capacity;
    }
    array->items[array->count++] = item;
    return true;
}

size_t nmo_cli_record_array_count(const nmo_cli_record_array_t *array)
{
    return array ? array->count : 0u;
}

bool nmo_cli_record_array_set_heading(nmo_cli_record_array_t *array,
                                      const char *heading)
{
    if (!array) {
        return false;
    }
    return set_str(&array->heading, heading);
}

void nmo_cli_record_array_omit_empty(nmo_cli_record_array_t *array)
{
    if (array) {
        array->omit_empty = true;
    }
}

bool nmo_cli_record_array_set_empty_text(nmo_cli_record_array_t *array,
                                         const char *text)
{
    if (!array) {
        return false;
    }
    return set_str(&array->empty_text, text);
}

bool nmo_cli_record_set_summary(nmo_cli_record_t *record, const char *text)
{
    if (!record) {
        return false;
    }
    return set_str(&record->summary, text);
}

/* Rendering */

bool nmo_cli_record_to_json(const nmo_cli_record_t *record,
                            yyjson_mut_doc *doc, yyjson_mut_val *obj)
{
    if (!record || !doc || !obj) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < record->count && ok; ++i) {
        const record_field_t *field = &record->fields[i];
        if (!field->key && field->kind != RECORD_REF &&
            field->kind != RECORD_BYTES) {
            continue;
        }
        switch (field->kind) {
        case RECORD_UINT:
            ok = nmo_cli_json_add_uint_safe(doc, obj, field->key, field->u);
            break;
        case RECORD_INT:
            ok = nmo_cli_json_add_int_safe(doc, obj, field->key, field->i);
            break;
        case RECORD_REAL:
            ok = nmo_cli_json_add_real_safe(doc, obj, field->key, field->d);
            break;
        case RECORD_BOOL:
            ok = nmo_cli_json_add_bool_safe(doc, obj, field->key, field->b);
            break;
        case RECORD_STR:
            ok = nmo_cli_json_add_str_safe(doc, obj, field->key,
                                           field->str ? field->str : "");
            break;
        case RECORD_NULL:
            ok = nmo_cli_json_add_null_safe(doc, obj, field->key);
            break;
        case RECORD_REF:
            if (field->b && field->u == 0u) {
                break; /* optional reference: absent in JSON when unset */
            }
            if (field->key) {
                ok = nmo_cli_json_add_uint_safe(doc, obj, field->key, field->u);
            }
            if (ok && field->name_key && field->str) {
                ok = nmo_cli_json_add_str_safe(doc, obj, field->name_key,
                                               field->str);
            }
            break;
        case RECORD_RAW:
        case RECORD_HEADING:
            break;
        case RECORD_BYTES:
            /* The prefix is all that was kept; max_bytes = its length keeps
             * the helper from reading past it. */
            ok = nmo_cli_json_add_data_hex(doc, obj, field->bytes,
                                           (size_t)field->u, field->list_count,
                                           false);
            break;
        case RECORD_REAL_LIST: {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            ok = arr != NULL;
            for (size_t j = 0; ok && j < field->list_count; ++j) {
                ok = yyjson_mut_arr_add_real(doc, arr, field->reals[j]);
            }
            ok = ok && nmo_cli_json_add_val_safe(doc, obj, field->key, arr);
            break;
        }
        case RECORD_UINT_LIST: {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            ok = arr != NULL;
            for (size_t j = 0; ok && j < field->list_count; ++j) {
                ok = yyjson_mut_arr_add_uint(doc, arr, field->uints[j]);
            }
            ok = ok && nmo_cli_json_add_val_safe(doc, obj, field->key, arr);
            break;
        }
        case RECORD_STR_LIST: {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            ok = arr != NULL;
            for (size_t j = 0; ok && j < field->list_count; ++j) {
                if (field->strs[j]) {
                    ok = nmo_cli_json_add_str_safe_to_arr(doc, arr, field->strs[j]);
                } else {
                    ok = yyjson_mut_arr_add_null(doc, arr);
                }
            }
            ok = ok && nmo_cli_json_add_val_safe(doc, obj, field->key, arr);
            break;
        }
        case RECORD_VEC3: {
            yyjson_mut_val *vec = yyjson_mut_obj(doc);
            ok = vec != NULL &&
                 nmo_cli_json_add_real_safe(doc, vec, "x", field->v[0]) &&
                 nmo_cli_json_add_real_safe(doc, vec, "y", field->v[1]) &&
                 nmo_cli_json_add_real_safe(doc, vec, "z", field->v[2]) &&
                 nmo_cli_json_add_val_safe(doc, obj, field->key, vec);
            break;
        }
        case RECORD_ARRAY: {
            if (field->array->omit_empty && field->array->count == 0u) {
                break;
            }
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            if (!arr) {
                ok = false;
                break;
            }
            for (size_t j = 0; j < field->array->count && ok; ++j) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                if (!item ||
                    !nmo_cli_record_to_json(field->array->items[j], doc, item) ||
                    !yyjson_mut_arr_add_val(arr, item)) {
                    ok = false;
                }
            }
            if (ok) {
                ok = nmo_cli_json_add_val_safe(doc, obj, field->key, arr);
            }
            break;
        }
        }
    }
    return ok;
}

static void record_print_bytes(const record_field_t *field, FILE *out,
                               int key_width, bool colorize)
{
    const char *label = field->label ? field->label : "";
    if (!field->bytes || field->list_count == 0u) {
        nmo_cli_print_kv(out, label, "(empty)", key_width, colorize);
        return;
    }
    if (field->list_count < (size_t)field->u) {
        char note[64];
        snprintf(note, sizeof(note), "showing %zu/%zu bytes",
                 field->list_count, (size_t)field->u);
        nmo_cli_print_kv(out, label, note, key_width, colorize);
    }
    nmo_hexdump_options_t hd;
    nmo_hexdump_init_options(&hd);
    hd.colorize = colorize;
    hd.ansi.offset = NMO_CLI_COLOR_DIM;
    hd.ansi.hex = NMO_CLI_COLOR_CYAN;
    hd.ansi.ascii = NMO_CLI_COLOR_GREEN;
    hd.ansi.delim = NMO_CLI_COLOR_DIM;
    hd.ansi.reset = NMO_CLI_COLOR_RESET;
    nmo_hexdump_canonical(out, field->bytes, field->list_count, &hd);
}

void nmo_cli_record_print_kv(const nmo_cli_record_t *record, FILE *out,
                             int key_width, bool colorize)
{
    if (!record || !out) {
        return;
    }
    for (size_t i = 0; i < record->count; ++i) {
        const record_field_t *field = &record->fields[i];
        if (field->kind == RECORD_RAW) {
            if (field->text) {
                fputs(field->text, out);
            }
            continue;
        }
        if (field->kind == RECORD_HEADING) {
            fputc('\n', out);
            nmo_cli_print_heading(out, field->text ? field->text : "", colorize);
            continue;
        }
        if (field->kind == RECORD_BYTES) {
            record_print_bytes(field, out, key_width, colorize);
            continue;
        }
        if (field->kind == RECORD_ARRAY) {
            if (!field->label && !field->array->heading) {
                continue;
            }
            if (field->array->omit_empty && field->array->count == 0u) {
                continue;
            }
            if (field->array->heading) {
                fprintf(out, "\n%s\n", field->array->heading);
            } else {
                fprintf(out, "\n%s (%zu):\n", field->label, field->array->count);
            }
            for (size_t j = 0; j < field->array->count; ++j) {
                const nmo_cli_record_t *item = field->array->items[j];
                if (item && item->summary) {
                    fprintf(out, "%s\n", item->summary);
                }
            }
            if (field->array->count == 0u && field->array->empty_text) {
                fprintf(out, "%s\n", field->array->empty_text);
            }
            continue;
        }
        if (!field->label || !field->text) {
            continue;
        }
        nmo_cli_print_kv(out, field->label, field->text, key_width, colorize);
    }
}

size_t nmo_cli_record_cells(const nmo_cli_record_t *record,
                            const char **cells, size_t capacity)
{
    size_t n = 0;
    if (!record || !cells) {
        return 0u;
    }
    for (size_t i = 0; i < record->count && n < capacity; ++i) {
        const record_field_t *field = &record->fields[i];
        if (field->kind == RECORD_ARRAY || field->kind == RECORD_RAW ||
            field->kind == RECORD_HEADING || field->kind == RECORD_BYTES ||
            !field->label || !field->text) {
            continue;
        }
        cells[n++] = field->text;
    }
    return n;
}
