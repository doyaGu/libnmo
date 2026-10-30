/**
 * @file engine_strings.c
 * @brief Parameter values as text, as CK2's string functions write and read
 *        them, and the Logics/Strings building blocks.
 *
 * CKParameter::GetStringValue and SetStringValue call the string function of
 * the parameter's type, or of the type it derives from: "%d" integers, "%g"
 * floats, "T:D" angles, "00m 00s 000ms" times, "x,y,z" vectors, colors in
 * bytes, enum and flag names, object names. The engine does the same for the
 * types it knows; other types have no text.
 */

#include "engine_internal.h"

#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "../runtime/runtime_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef nmo_engine_block_t blk_t;

#define IN_ACTIVE(b, i) nmo_engine_block_is_input_active((b), (i))
#define IN_OFF(b, i) nmo_engine_block_activate_input((b), (i), false)
#define OUT_ON(b, i) nmo_engine_block_activate_output((b), (i), true)

#define DEG_PER_RAD 57.295776f
#define RAD_PER_DEG 0.017453292f

/* A new heap string from a printf format, sized exactly. */
static char *text_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (length < 0) {
        return NULL;
    }
    char *text = (char *)malloc((size_t)length + 1u);
    if (text != NULL) {
        va_start(args, format);
        vsnprintf(text, (size_t)length + 1u, format, args);
        va_end(args);
    }
    return text;
}

static char *text_copy(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

/* ============================================================================
 * Types
 * ============================================================================ */

typedef enum text_kind {
    TEXT_NONE,
    TEXT_STRING,
    TEXT_INT,
    TEXT_BOOL,
    TEXT_FLOAT,
    TEXT_PERCENTAGE,
    TEXT_ANGLE,
    TEXT_TIME,
    TEXT_VECTOR2,
    TEXT_VECTOR,
    TEXT_QUATERNION,
    TEXT_EULER,
    TEXT_RECT,
    TEXT_COLOR,
    TEXT_MESSAGE,
    TEXT_ENUM,
    TEXT_FLAGS,
    TEXT_OBJECT,
} text_kind_t;

static const struct {
    nmo_guid_t guid;
    text_kind_t kind;
} text_kinds[] = {
    {CKPGUID_STRING, TEXT_STRING},
    {CKPGUID_BOOL, TEXT_BOOL},
    {CKPGUID_PERCENTAGE, TEXT_PERCENTAGE},
    {CKPGUID_ANGLE, TEXT_ANGLE},
    {CKPGUID_TIME, TEXT_TIME},
    {CKPGUID_FLOAT, TEXT_FLOAT},
    {CKPGUID_INT, TEXT_INT},
    {CKPGUID_2DVECTOR, TEXT_VECTOR2},
    {CKPGUID_VECTOR, TEXT_VECTOR},
    {CKPGUID_QUATERNION, TEXT_QUATERNION},
    {CKPGUID_EULERANGLES, TEXT_EULER},
    {CKPGUID_RECT, TEXT_RECT},
    {CKPGUID_COLOR, TEXT_COLOR},
    {CKPGUID_MESSAGE, TEXT_MESSAGE},
};

/* The string function a type uses: its own, or the nearest one it derives. */
static text_kind_t text_kind(const nmo_engine_t *engine, nmo_guid_t guid)
{
    for (int depth = 0; depth < 32 && !nmo_guid_is_null(guid); depth++) {
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(engine->types, guid);
        if (type != NULL && (type->category & NMO_TYPE_CATEGORY_OBJECT_REF) != 0u) {
            return TEXT_OBJECT;
        }
        if (type != NULL && (type->category & NMO_TYPE_CATEGORY_ENUM) != 0u) {
            return TEXT_ENUM;
        }
        if (type != NULL && (type->category & NMO_TYPE_CATEGORY_FLAGS) != 0u) {
            return TEXT_FLAGS;
        }
        for (size_t i = 0; i < sizeof(text_kinds) / sizeof(text_kinds[0]); i++) {
            if (nmo_guid_equals(guid, text_kinds[i].guid)) {
                return text_kinds[i].kind;
            }
        }
        if (type == NULL) {
            break;
        }
        guid = type->base_type;
    }
    return TEXT_NONE;
}

static const nmo_specialized_metadata_t *type_metadata(const nmo_engine_t *engine, nmo_guid_t guid,
                                                       uint16_t kind)
{
    for (int depth = 0; depth < 32 && !nmo_guid_is_null(guid); depth++) {
        const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(engine->types, guid);
        if (type == NULL) {
            return NULL;
        }
        const nmo_specialized_metadata_t *meta = nmo_type_registry_get_metadata(engine->types,
                                                                                type->id);
        if (meta != NULL && meta->metadata_type == kind) {
            return meta;
        }
        guid = type->base_type;
    }
    return NULL;
}

static nmo_object_t *engine_object(const nmo_engine_t *engine, nmo_object_id_t id)
{
    return id != 0 ? eng_find_object(engine, id) : NULL;
}

/* ============================================================================
 * GetStringValue
 * ============================================================================ */

static void read_floats(const eng_param_t *param, float *out, size_t count)
{
    memset(out, 0, count * sizeof(float));
    size_t size = param->size < count * sizeof(float) ? param->size : count * sizeof(float);
    if (size > 0) {
        memcpy(out, param->data, size);
    }
}

static int32_t read_int(const eng_param_t *param)
{
    int32_t value = 0;
    if (param->size >= sizeof(value)) {
        memcpy(&value, param->data, sizeof(value));
    }
    return value;
}

/* CKAngleStringFunc: whole turns, then the degrees left ("%d:%g"). */
static void angle_parts(float radians, int *turns, double *degrees)
{
    double total = (double)(radians * DEG_PER_RAD);
    *turns = (int)(int64_t)(0.0027777778 * total);
    *degrees = total - (double)*turns * 360.0;
}

static char *enum_text(const nmo_engine_t *engine, const eng_param_t *param)
{
    int32_t value = read_int(param);
    const nmo_specialized_metadata_t *meta = type_metadata(engine, param->type,
                                                           NMO_METADATA_TYPE_ENUM);
    for (size_t i = 0; meta != NULL && i < meta->enum_meta.value_count; i++) {
        if (meta->enum_meta.values[i].value == value) {
            const char *name = meta->enum_meta.values[i].name;
            return text_copy(name != NULL ? name : "", name != NULL ? strlen(name) : 0u);
        }
    }
    return text_printf("%d", value);
}

/* CKFlagsStringFunc: the names of the bits set, comma separated. */
static char *flags_text(const nmo_engine_t *engine, const eng_param_t *param)
{
    uint32_t value = (uint32_t)read_int(param);
    const nmo_specialized_metadata_t *meta = type_metadata(engine, param->type,
                                                           NMO_METADATA_TYPE_FLAGS);
    if (meta == NULL) {
        return text_printf("%d", (int32_t)value);
    }
    size_t length = 0;
    for (size_t i = 0; i < meta->flags_meta.bit_count; i++) {
        const nmo_flags_descriptor_t *bit = &meta->flags_meta.bits[i];
        if ((value & bit->mask) != 0u && bit->name != NULL) {
            length += strlen(bit->name) + 1u;
        }
    }
    char *text = (char *)malloc(length + 1u);
    if (text == NULL) {
        return NULL;
    }
    text[0] = '\0';
    for (size_t i = 0; i < meta->flags_meta.bit_count; i++) {
        const nmo_flags_descriptor_t *bit = &meta->flags_meta.bits[i];
        if ((value & bit->mask) != 0u && bit->name != NULL) {
            strcat(text, bit->name);
            strcat(text, ",");
        }
    }
    if (length > 0) {
        text[length - 1] = '\0';
    }
    return text;
}

char *eng_param_text(nmo_engine_t *engine, const eng_param_t *param)
{
    if (param == NULL) {
        return NULL;
    }
    float f[6];
    switch (text_kind(engine, param->type)) {
    case TEXT_STRING:
        /* CKStringStringFunc: the bytes; no string without them */
        if (param->data == NULL || param->size == 0) {
            return NULL;
        }
        return text_copy((const char *)param->data, strnlen((const char *)param->data, param->size));
    case TEXT_INT:
        return text_printf("%d", read_int(param));
    case TEXT_BOOL:
        return text_printf("%s", read_int(param) != 0 ? "TRUE" : "FALSE");
    case TEXT_FLOAT:
        read_floats(param, f, 1);
        return text_printf("%g", (double)f[0]);
    case TEXT_PERCENTAGE:
        read_floats(param, f, 1);
        return text_printf("%g%%", (double)(float)(f[0] * 100.0f));
    case TEXT_ANGLE: {
        int turns = 0;
        double degrees = 0.0;
        read_floats(param, f, 1);
        angle_parts(f[0], &turns, &degrees);
        return text_printf("%d:%g", turns, degrees);
    }
    case TEXT_EULER: {
        int turns[3];
        double degrees[3];
        read_floats(param, f, 3);
        for (int i = 0; i < 3; i++) {
            angle_parts(f[i], &turns[i], &degrees[i]);
        }
        return text_printf("%d:%g,%d:%g,%d:%g", turns[0], (double)(float)degrees[0], turns[1],
                           (double)(float)degrees[1], turns[2], (double)(float)degrees[2]);
    }
    case TEXT_TIME: {
        read_floats(param, f, 1);
        int ms = (int)(int64_t)f[0];
        return text_printf("%.2dm %.2ds %.3dms", ms / 60000, ms % 60000 / 1000, ms % 60000 % 1000);
    }
    case TEXT_VECTOR2:
        read_floats(param, f, 2);
        return text_printf("%g,%g", (double)f[0], (double)f[1]);
    case TEXT_VECTOR:
        read_floats(param, f, 3);
        return text_printf("%g,%g,%g", (double)f[0], (double)f[1], (double)f[2]);
    case TEXT_QUATERNION:
        read_floats(param, f, 4);
        return text_printf("%g,%g,%g,%g", (double)f[0], (double)f[1], (double)f[2], (double)f[3]);
    case TEXT_RECT:
        read_floats(param, f, 4);
        return text_printf("(%g,%g),(%g,%g)", (double)f[0], (double)f[1], (double)f[2],
                           (double)f[3]);
    case TEXT_COLOR:
        read_floats(param, f, 4);
        return text_printf("%d,%d,%d,%d", (int)(int64_t)(f[0] * 255.0), (int)(int64_t)(f[1] * 255.0),
                           (int)(int64_t)(f[2] * 255.0), (int)(int64_t)(f[3] * 255.0));
    case TEXT_MESSAGE: {
        const char *name = nmo_engine_message_type_name(engine, read_int(param));
        return name != NULL ? text_copy(name, strlen(name)) : NULL;
    }
    case TEXT_ENUM:
        return enum_text(engine, param);
    case TEXT_FLAGS:
        return flags_text(engine, param);
    case TEXT_OBJECT: {
        /* CKObjectStringFunc: the object's name; nothing without a name */
        nmo_object_t *object = engine_object(engine, (nmo_object_id_t)read_int(param));
        const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
        return name != NULL ? text_copy(name, strlen(name)) : NULL;
    }
    case TEXT_NONE:
        break;
    }
    return NULL;
}

/* ============================================================================
 * SetStringValue
 * ============================================================================ */

/* GetObjectByNameAndParentClass: an object of that name and of the type's class. */
static nmo_object_id_t object_named(nmo_engine_t *engine, nmo_guid_t type_guid, const char *name)
{
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(engine->types, type_guid);
    return eng_object_named(engine, name, type != NULL ? type->class_id : 0u);
}

nmo_status_t eng_param_set_text(nmo_engine_t *engine, eng_param_t *param, const char *text)
{
    if (param == NULL || text == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    float f[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int32_t i = 0;
    switch (text_kind(engine, param->type)) {
    case TEXT_STRING:
        return eng_param_write(engine, param, text, strlen(text) + 1u);
    case TEXT_INT:
        sscanf(text, "%d", &i);
        return eng_param_write(engine, param, &i, sizeof(i));
    case TEXT_BOOL: {
        /* CKBoolStringFunc: the first five characters are "TRUE", in any case */
        size_t k = 0;
        while (k < 4 && text[k] != '\0' && (text[k] | 0x20) == "true"[k]) {
            k++;
        }
        i = k == 4 && text[4] == '\0';
        return eng_param_write(engine, param, &i, sizeof(i));
    }
    case TEXT_FLOAT:
        sscanf(text, "%f", &f[0]);
        return eng_param_write(engine, param, f, sizeof(float));
    case TEXT_PERCENTAGE:
        sscanf(text, "%f%%", &f[0]);
        f[0] *= 0.0099999998f;
        return eng_param_write(engine, param, f, sizeof(float));
    case TEXT_ANGLE: {
        /* CK2 adds the turns as degrees: "1:90" is 91 degrees */
        int turns = 0;
        sscanf(text, "%d:%f", &turns, &f[1]);
        f[0] = ((float)turns + f[1]) * RAD_PER_DEG;
        return eng_param_write(engine, param, f, sizeof(float));
    }
    case TEXT_EULER: {
        int turns[3] = {0, 0, 0};
        float degrees[3] = {0.0f, 0.0f, 0.0f};
        sscanf(text, "%d:%f,%d:%f,%d:%f", &turns[0], &degrees[0], &turns[1], &degrees[1],
               &turns[2], &degrees[2]);
        for (int k = 0; k < 3; k++) {
            f[k] = ((float)turns[k] + degrees[k]) * RAD_PER_DEG;
        }
        return eng_param_write(engine, param, f, 3 * sizeof(float));
    }
    case TEXT_TIME: {
        float minutes = 0.0f, seconds = 0.0f, ms = 0.0f;
        sscanf(text, "%fm %fs %fms", &minutes, &seconds, &ms);
        f[0] = (minutes * 60.0f + seconds) * 1000.0f + ms;
        return eng_param_write(engine, param, f, sizeof(float));
    }
    case TEXT_VECTOR2:
        sscanf(text, "%f,%f", &f[0], &f[1]);
        return eng_param_write(engine, param, f, 2 * sizeof(float));
    case TEXT_VECTOR:
        sscanf(text, "%f,%f,%f", &f[0], &f[1], &f[2]);
        return eng_param_write(engine, param, f, 3 * sizeof(float));
    case TEXT_QUATERNION:
        sscanf(text, "%f,%f,%f,%f", &f[0], &f[1], &f[2], &f[3]);
        return eng_param_write(engine, param, f, 4 * sizeof(float));
    case TEXT_RECT:
        sscanf(text, "(%f,%f),(%f,%f)", &f[0], &f[1], &f[2], &f[3]);
        return eng_param_write(engine, param, f, 4 * sizeof(float));
    case TEXT_COLOR: {
        int bytes[4] = {0, 0, 0, 0};
        sscanf(text, "%d,%d,%d,%d", &bytes[0], &bytes[1], &bytes[2], &bytes[3]);
        for (int k = 0; k < 4; k++) {
            f[k] = (float)bytes[k] * 0.0039215689f;
        }
        return eng_param_write(engine, param, f, 4 * sizeof(float));
    }
    case TEXT_MESSAGE:
        i = eng_message_type_add(engine, text);
        return eng_param_write(engine, param, &i, sizeof(i));
    case TEXT_ENUM: {
        const nmo_specialized_metadata_t *meta = type_metadata(engine, param->type,
                                                               NMO_METADATA_TYPE_ENUM);
        bool found = false;
        for (size_t k = 0; meta != NULL && k < meta->enum_meta.value_count; k++) {
            const char *name = meta->enum_meta.values[k].name;
            if (name != NULL && strcmp(text, name) == 0) {
                i = (int32_t)meta->enum_meta.values[k].value;
                found = true;
                break;
            }
        }
        if (!found) {
            sscanf(text, "%d", &i);
        }
        return eng_param_write(engine, param, &i, sizeof(i));
    }
    case TEXT_FLAGS: {
        const nmo_specialized_metadata_t *meta = type_metadata(engine, param->type,
                                                               NMO_METADATA_TYPE_FLAGS);
        if (meta == NULL) {
            sscanf(text, "%d", &i);
            return eng_param_write(engine, param, &i, sizeof(i));
        }
        uint32_t value = 0;
        for (const char *p = text; *p != '\0';) {
            const char *comma = strchr(p, ',');
            size_t length = comma != NULL ? (size_t)(comma - p) : strlen(p);
            for (size_t k = 0; k < meta->flags_meta.bit_count; k++) {
                const char *name = meta->flags_meta.bits[k].name;
                if (name != NULL && strlen(name) == length) {
                    size_t c = 0;
                    while (c < length && (p[c] | 0x20) == (name[c] | 0x20)) {
                        c++;
                    }
                    if (c == length) {
                        value |= (uint32_t)meta->flags_meta.bits[k].mask;
                        break;
                    }
                }
            }
            if (comma == NULL || length == 0) {
                break;
            }
            p = comma + 1;
        }
        return eng_param_write(engine, param, &value, sizeof(value));
    }
    case TEXT_OBJECT: {
        uint32_t id = (uint32_t)object_named(engine, param->type, text);
        return eng_param_write(engine, param, &id, sizeof(id));
    }
    case TEXT_NONE:
        break;
    }
    return NMO_ERR_NOT_SUPPORTED;
}

/* ============================================================================
 * Block API
 * ============================================================================ */

static const eng_node_t *block_node(const blk_t *b)
{
    return &b->engine->nodes.data[b->node];
}

/* The parameter input `pos` reads, its operation run when `update` (GetValue with update). */
static eng_param_t *input_source(blk_t *b, int pos, bool update)
{
    const eng_node_t *node = block_node(b);
    eng_param_t *source = eng_param_real_source(
        b->engine, eng_node_param(b->engine, node->first_pin, node->pin_count, pos));
    if (update) {
        eng_param_update(b->engine, source);
    }
    return source;
}

static eng_param_t *output_param(blk_t *b, int pos)
{
    const eng_node_t *node = block_node(b);
    return eng_node_param(b->engine, node->first_pout, node->pout_count, pos);
}

size_t nmo_engine_block_input_text(nmo_engine_block_t *block, int pos, char *buffer, size_t size)
{
    char *text = eng_param_text(block->engine, input_source(block, pos, false));
    if (text == NULL) {
        if (buffer != NULL && size > 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    size_t length = strlen(text);
    if (buffer != NULL && size > 0) {
        size_t n = length < size - 1u ? length : size - 1u;
        memcpy(buffer, text, n);
        buffer[n] = '\0';
    }
    free(text);
    return length + 1u;
}

nmo_status_t nmo_engine_block_set_output_text(nmo_engine_block_t *block, int pos,
                                              const char *text)
{
    eng_param_t *param = output_param(block, pos);
    return param != NULL ? eng_param_set_text(block->engine, param, text) : NMO_ERR_NOT_FOUND;
}

/* ============================================================================
 * Logics/Strings
 * ============================================================================ */

/* The string an input reads, or NULL when it reads none. */
static const char *input_string(blk_t *b, int pos)
{
    size_t size = 0;
    const char *text = (const char *)nmo_engine_block_input_data(b, pos, &size);
    return text != NULL && size > 0 && memchr(text, '\0', size) != NULL ? text : NULL;
}

static void set_out_string(blk_t *b, int pos, const char *text)
{
    nmo_engine_block_set_output_value(b, pos, text, strlen(text) + 1u);
}

/*
 * The single conversion of a Create String format, when it fits the value:
 * an integer one for an Integer, a floating one for a Float, %s for text.
 * CK2 hands any format to sprintf; one that does not fit is not used here.
 */
static char format_conversion(const char *format)
{
    char conversion = 0;
    for (const char *p = format; *p != '\0'; p++) {
        if (*p != '%') {
            continue;
        }
        if (p[1] == '%') {
            p++;
            continue;
        }
        if (conversion != 0) {
            return 0;
        }
        p++;
        while (*p != '\0' && strchr("-+ #0123456789.", *p) != NULL) {
            p++;
        }
        while (*p == 'l' || *p == 'h') {
            p++;
        }
        if (*p == '\0' || strchr("diouxXcfeEgGs", *p) == NULL) {
            return 0;
        }
        conversion = *p;
    }
    return conversion;
}

/*
 * An element of Create String: input `pos` with its format (the local before
 * it), its source's operation run first, whatever its type (GetValue).
 */
static char *create_string_element(blk_t *b, int pos)
{
    eng_param_t *source = input_source(b, pos, true);
    if (source == NULL) {
        return NULL;
    }
    const eng_node_t *node = block_node(b);
    eng_param_t *local = eng_node_param(b->engine, node->first_local, node->local_count, pos - 1);
    const char *format = local != NULL && local->data != NULL && local->size > 0 &&
                                 memchr(local->data, '\0', local->size) != NULL
                             ? (const char *)local->data
                             : NULL;
    if (format == NULL || format[0] == '\0') {
        return eng_param_text(b->engine, source);
    }
    char conversion = format_conversion(format);
    if (nmo_guid_equals(source->type, CKPGUID_INT)) {
        if (conversion != 0 && strchr("diouxXc", conversion) != NULL) {
            return text_printf(format, read_int(source));
        }
        return eng_param_text(b->engine, source);
    }
    if (nmo_guid_equals(source->type, CKPGUID_FLOAT)) {
        float value = 0.0f;
        read_floats(source, &value, 1);
        if (conversion != 0 && strchr("feEgG", conversion) != NULL) {
            return text_printf(format, (double)value);
        }
        return eng_param_text(b->engine, source);
    }
    char *text = eng_param_text(b->engine, source);
    if (text == NULL || format[strlen(format) - 1] != 's' || conversion != 's') {
        return text;
    }
    char *formatted = text_printf(format, text);
    free(text);
    return formatted;
}

/* Create String: the inputs after Delimiter as text, joined by it; Add Line appends a line. */
static int bb_create_string(blk_t *b)
{
    if (nmo_engine_block_version(b) < 0x00020000u) {
        OUT_ON(b, 0);
    }
    const char *delimiter = input_string(b, 0);
    if (delimiter == NULL) {
        delimiter = "";
    }
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        set_out_string(b, 0, "");
        if (!IN_ACTIVE(b, 0) && !IN_ACTIVE(b, 1)) {
            return NMO_ENGINE_BR_OK;
        }
    }

    size_t length = 0, capacity = 1;
    char *joined = (char *)calloc(1, capacity);
    int count = nmo_engine_block_input_parameter_count(b);
    for (int i = 1; joined != NULL && i < count; i++) {
        if (input_source(b, i, false) == NULL) {
            continue;
        }
        char *element = create_string_element(b, i);
        size_t add = (i != 1 ? strlen(delimiter) : 0u) + (element != NULL ? strlen(element) : 0u);
        if (length + add + 1u > capacity) {
            capacity = (length + add + 1u) * 2u;
            char *grown = (char *)realloc(joined, capacity);
            if (grown == NULL) {
                free(joined);
                joined = NULL;
                free(element);
                break;
            }
            joined = grown;
        }
        if (i != 1) {
            strcpy(joined + length, delimiter);
            length += strlen(delimiter);
        }
        if (element != NULL) {
            strcpy(joined + length, element);
            length += strlen(element);
        }
        free(element);
    }
    if (joined == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }

    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        set_out_string(b, 0, joined);
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        eng_param_t *out = output_param(b, 0);
        const char *current = out != NULL && out->data != NULL && out->size > 0 &&
                                      memchr(out->data, '\0', out->size) != NULL
                                  ? (const char *)out->data
                                  : "";
        /* Without format locals (the old version), or after text, a new line first */
        char *text = nmo_engine_block_local_parameter_count(b) == 0 || current[0] != '\0'
                         ? text_printf("%s\n%s", current, joined)
                         : text_printf("%s", joined);
        if (text != NULL) {
            set_out_string(b, 0, text);
            free(text);
        }
    }
    free(joined);
    return NMO_ENGINE_BR_OK;
}

/* Get SubString (CutString): Length characters from Start; Length 0 to the end. */
static int bb_get_substring(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    const char *string = input_string(b, 0);
    if (string == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int len = (int)strlen(string);
    int start = 1, cut = 0;
    nmo_engine_block_get_input_value(b, 1, &start, sizeof(start));
    nmo_engine_block_get_input_value(b, 2, &cut, sizeof(cut));
    if (cut == 0 || start + cut > len) {
        cut = len - start;
    }
    if (start < 0) {
        cut += start;
        start = 0;
    }
    char *result = cut <= 0 || start >= len ? text_copy("", 0)
                                            : text_copy(string + start, (size_t)cut);
    if (result != NULL) {
        set_out_string(b, 0, result);
        free(result);
    }
    return NMO_ENGINE_BR_OK;
}

/* Scan String (SplitString): the elements between delimiters into the outputs after the count. */
static int bb_scan_string(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    const char *input = input_string(b, 0);
    if (input == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    char *string = text_copy(input, strlen(input)); /* an output may be the input */
    const char *delimiter = input_string(b, 1);
    if (delimiter == NULL || delimiter[0] == '\0') {
        delimiter = " ";
    }
    size_t delimiter_size = strlen(delimiter);
    if (string == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }

    int32_t elements = 0;
    char *str = string;
    for (char *del; (del = strstr(str, delimiter)) != NULL; str = del + delimiter_size) {
        if (del == str) {
            continue; /* a delimiter at the start: skipped */
        }
        elements++;
        eng_param_t *out = output_param(b, elements);
        if (out == NULL) {
            break;
        }
        *del = '\0';
        eng_param_set_text(b->engine, out, str);
        *del = delimiter[0];
    }
    if (str[0] != '\0') {
        elements++;
        eng_param_t *out = output_param(b, elements);
        if (out == NULL) {
            free(string);
            return NMO_ENGINE_BR_OK;
        }
        eng_param_set_text(b->engine, out, str);
    }
    nmo_engine_block_set_output_value(b, 0, &elements, sizeof(elements));
    free(string);
    return NMO_ENGINE_BR_OK;
}

/* Reverse String: reverses the string its input reads, where it is. */
static int bb_reverse_string(blk_t *b)
{
    IN_OFF(b, 0);
    eng_param_t *source = input_source(b, 0, true);
    if (source != NULL && source->data != NULL && source->size > 0 &&
        memchr(source->data, '\0', source->size) != NULL) {
        char *string = (char *)source->data;
        size_t len = strlen(string);
        for (size_t i = 0; i < len / 2; i++) {
            char c = string[i];
            string[i] = string[len - 1 - i];
            string[len - 1 - i] = c;
        }
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_string_blocks[] = {
    {NMO_GUID(0x4bcd2f9d, 0x382652e2), "Create String", bb_create_string, NULL, NULL},
    {NMO_GUID(0x10b16051, 0x26173b39), "Get SubString", bb_get_substring, NULL, NULL},
    {NMO_GUID(0x4afa2f11, 0x034872e2), "Scan String", bb_scan_string, NULL, NULL},
    {NMO_GUID(0x72b17c2e, 0x644e2563), "Reverse String", bb_reverse_string, NULL, NULL},
};

nmo_status_t eng_register_strings(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_string_blocks) / sizeof(eng_string_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_string_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
