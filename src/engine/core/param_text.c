/**
 * @file param_text.c
 * @brief Parameter values as text, as CK2's string functions write and read
 *        them.
 *
 * CKParameter::GetStringValue and SetStringValue call the string function of
 * the parameter's type, or of the type it derives from: "%d" integers, "%g"
 * floats, "T:D" angles, "00m 00s 000ms" times, "x,y,z" vectors, colors in
 * bytes, enum and flag names, object names. The engine does the same for the
 * types it knows; other types have no text.
 */

#include "../engine_block_util.h"

#include "format/nmo_object.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG_PER_RAD 57.295776f

#define RAD_PER_DEG 0.017453292f

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

void eng_param_read_floats(const eng_param_t *param, float *out, size_t count)
{
    memset(out, 0, count * sizeof(float));
    size_t size = param->size < count * sizeof(float) ? param->size : count * sizeof(float);
    if (size > 0) {
        memcpy(out, param->data, size);
    }
}

int32_t eng_param_read_int(const eng_param_t *param)
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
    int32_t value = eng_param_read_int(param);
    const nmo_specialized_metadata_t *meta = type_metadata(engine, param->type,
                                                           NMO_METADATA_TYPE_ENUM);
    for (size_t i = 0; meta != NULL && i < meta->enum_meta.value_count; i++) {
        if (meta->enum_meta.values[i].value == value) {
            const char *name = meta->enum_meta.values[i].name;
            return eng_text_copy(name != NULL ? name : "", name != NULL ? strlen(name) : 0u);
        }
    }
    return eng_text_printf("%d", value);
}

/* CKFlagsStringFunc: the names of the bits set, comma separated. */
static char *flags_text(const nmo_engine_t *engine, const eng_param_t *param)
{
    uint32_t value = (uint32_t)eng_param_read_int(param);
    const nmo_specialized_metadata_t *meta = type_metadata(engine, param->type,
                                                           NMO_METADATA_TYPE_FLAGS);
    if (meta == NULL) {
        return eng_text_printf("%d", (int32_t)value);
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
        return eng_text_copy((const char *)param->data, strnlen((const char *)param->data, param->size));
    case TEXT_INT:
        return eng_text_printf("%d", eng_param_read_int(param));
    case TEXT_BOOL:
        return eng_text_printf("%s", eng_param_read_int(param) != 0 ? "TRUE" : "FALSE");
    case TEXT_FLOAT:
        eng_param_read_floats(param, f, 1);
        return eng_text_printf("%g", (double)f[0]);
    case TEXT_PERCENTAGE:
        eng_param_read_floats(param, f, 1);
        return eng_text_printf("%g%%", (double)(float)(f[0] * 100.0f));
    case TEXT_ANGLE: {
        int turns = 0;
        double degrees = 0.0;
        eng_param_read_floats(param, f, 1);
        angle_parts(f[0], &turns, &degrees);
        return eng_text_printf("%d:%g", turns, degrees);
    }
    case TEXT_EULER: {
        int turns[3];
        double degrees[3];
        eng_param_read_floats(param, f, 3);
        for (int i = 0; i < 3; i++) {
            angle_parts(f[i], &turns[i], &degrees[i]);
        }
        return eng_text_printf("%d:%g,%d:%g,%d:%g", turns[0], (double)(float)degrees[0], turns[1],
                               (double)(float)degrees[1], turns[2], (double)(float)degrees[2]);
    }
    case TEXT_TIME: {
        eng_param_read_floats(param, f, 1);
        int ms = (int)(int64_t)f[0];
        return eng_text_printf("%.2dm %.2ds %.3dms", ms / 60000, ms % 60000 / 1000, ms % 60000 % 1000);
    }
    case TEXT_VECTOR2:
        eng_param_read_floats(param, f, 2);
        return eng_text_printf("%g,%g", (double)f[0], (double)f[1]);
    case TEXT_VECTOR:
        eng_param_read_floats(param, f, 3);
        return eng_text_printf("%g,%g,%g", (double)f[0], (double)f[1], (double)f[2]);
    case TEXT_QUATERNION:
        eng_param_read_floats(param, f, 4);
        return eng_text_printf("%g,%g,%g,%g", (double)f[0], (double)f[1], (double)f[2], (double)f[3]);
    case TEXT_RECT:
        eng_param_read_floats(param, f, 4);
        return eng_text_printf("(%g,%g),(%g,%g)", (double)f[0], (double)f[1], (double)f[2],
                               (double)f[3]);
    case TEXT_COLOR:
        eng_param_read_floats(param, f, 4);
        return eng_text_printf("%d,%d,%d,%d", (int)(int64_t)(f[0] * 255.0), (int)(int64_t)(f[1] * 255.0),
                               (int)(int64_t)(f[2] * 255.0), (int)(int64_t)(f[3] * 255.0));
    case TEXT_MESSAGE: {
        const char *name = nmo_engine_message_type_name(engine, eng_param_read_int(param));
        return name != NULL ? eng_text_copy(name, strlen(name)) : NULL;
    }
    case TEXT_ENUM:
        return enum_text(engine, param);
    case TEXT_FLAGS:
        return flags_text(engine, param);
    case TEXT_OBJECT: {
        /* CKObjectStringFunc: the object's name; nothing without a name */
        nmo_object_t *object = engine_object(engine, (nmo_object_id_t)eng_param_read_int(param));
        const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
        return name != NULL ? eng_text_copy(name, strlen(name)) : NULL;
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
    return eng_object_named(engine, name, type != NULL ? type->object_class_id : 0u);
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
    eng_param_t *param = block_output(block, pos);
    return param != NULL ? eng_param_set_text(block->engine, param, text) : NMO_ERR_NOT_FOUND;
}
