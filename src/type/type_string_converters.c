/**
 * @file type_string_converters.c
 * @brief Implementation of type-to-string converters (Phase 6.4.2)
 *
 * Provides concrete implementations for all built-in type string converters.
 *
 * Reference: CKParameterManager.cpp:1345-1435
 */

#include "type/nmo_type_string.h"
#include "type/nmo_type_system.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_guids.h"
#include "object/nmo_param_guids.h"
#include "core/nmo_color.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_hash.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Float Converters
 * ============================================================================ */

nmo_status_t nmo_float_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 16) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for float_to_string");
    }

    float f = *(const float*)value;
    
    // Handle special cases
    if (isnan(f)) {
        snprintf(buffer, buffer_size, "NaN");
    } else if (isinf(f)) {
        snprintf(buffer, buffer_size, f > 0 ? "Infinity" : "-Infinity");
    } else {
        // Use %.6g for compact representation
        snprintf(buffer, buffer_size, "%.6g", f);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_float_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for float_from_string");
    }

    // Handle special cases
    if (strcmp(string, "NaN") == 0) {
        *(float*)value = NAN;
        NMO_RETURN_OK();
    }
    if (strcmp(string, "Infinity") == 0 || strcmp(string, "+Infinity") == 0) {
        *(float*)value = INFINITY;
        NMO_RETURN_OK();
    }
    if (strcmp(string, "-Infinity") == 0) {
        *(float*)value = -INFINITY;
        NMO_RETURN_OK();
    }

    char *endptr;
    errno = 0;
    float result = strtof(string, &endptr);

    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace(*endptr))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid float format");
    }

    *(float*)value = result;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Int Converters
 * ============================================================================ */

nmo_status_t nmo_int_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size,
    bool use_hex)
{
    if (!value || !buffer || buffer_size < 16) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for int_to_string");
    }

    int32_t i = *(const int32_t*)value;

    if (use_hex) {
        snprintf(buffer, buffer_size, "0x%X", (unsigned int)i);
    } else {
        snprintf(buffer, buffer_size, "%d", i);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_int_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for int_from_string");
    }

    char *endptr;
    errno = 0;
    
    // Detect base (hex if starts with 0x/0X, else decimal)
    int base = 0;  // auto-detect
    long result = strtol(string, &endptr, base);

    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace(*endptr))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid int format");
    }

    *(int32_t*)value = (int32_t)result;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Bool Converters
 * ============================================================================ */

nmo_status_t nmo_bool_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 6) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for bool_to_string");
    }

    bool b = *(const bool*)value;
    snprintf(buffer, buffer_size, b ? "true" : "false");

    NMO_RETURN_OK();
}

nmo_status_t nmo_bool_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for bool_from_string");
    }

    // Skip whitespace
    while (*string && isspace(*string)) string++;

    if (strcmp(string, "true") == 0 || strcmp(string, "1") == 0 ||
        strcmp(string, "TRUE") == 0 || strcmp(string, "True") == 0) {
        *(bool*)value = true;
    } else if (strcmp(string, "false") == 0 || strcmp(string, "0") == 0 ||
               strcmp(string, "FALSE") == 0 || strcmp(string, "False") == 0) {
        *(bool*)value = false;
    } else {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid bool format (expected true/false/1/0)");
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vector Converters
 * ============================================================================ */

static nmo_status_t parse_float_tuple(
    const char *kind,
    const char *string,
    float *out,
    int count)
{
    if (!kind || !string || !out || count <= 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for parse_float_tuple");
    }

    // Skip whitespace and opening parenthesis
    while (*string && isspace((unsigned char)*string)) string++;
    if (*string != '(') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                "%s must start with '('", kind);
    }
    string++;

    // Parse N float values separated by ',' (and optionally ';' for readability)
    char *endptr;
    for (int i = 0; i < count; i++) {
        while (*string && isspace((unsigned char)*string)) string++;

        errno = 0;
        out[i] = strtof(string, &endptr);

        if (errno != 0 || endptr == string) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                    "Invalid %s component", kind);
        }

        string = endptr;
        while (*string && isspace((unsigned char)*string)) string++;

        if (i < (count - 1)) {
            if (*string != ',' && *string != ';') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                        "%s components must be separated by ','", kind);
            }
            string++;
        }
    }

    // Expect closing parenthesis
    while (*string && isspace((unsigned char)*string)) string++;
    if (*string != ')') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                "%s must end with ')'", kind);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_vector2_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 24) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for vector2_to_string");
    }

    const float *v = (const float*)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g)", v[0], v[1]);
    NMO_RETURN_OK();
}

nmo_status_t nmo_vector2_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for vector2_from_string");
    }

    float *v = (float*)value;
    return parse_float_tuple("Vector2", string, v, 2);
}

nmo_status_t nmo_vector_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 32) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for vector_to_string");
    }

    const float *v = (const float*)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g, %.6g)", v[0], v[1], v[2]);

    NMO_RETURN_OK();
}

nmo_status_t nmo_vector_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for vector_from_string");
    }

    float *v = (float*)value;
    
    // Skip whitespace and opening parenthesis
    while (*string && isspace(*string)) string++;
    if (*string != '(') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Vector must start with '('");
    }
    string++;

    // Parse three float values separated by commas
    char *endptr;
    for (int i = 0; i < 3; i++) {
        while (*string && isspace(*string)) string++;
        
        errno = 0;
        v[i] = strtof(string, &endptr);
        
        if (errno != 0 || endptr == string) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid vector component");
        }
        
        string = endptr;
        while (*string && isspace(*string)) string++;
        
        if (i < 2) {
            if (*string != ',') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Vector components must be separated by ','");
            }
            string++;
        }
    }

    // Expect closing parenthesis
    while (*string && isspace(*string)) string++;
    if (*string != ')') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Vector must end with ')'");
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_vector4_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 48) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for vector4_to_string");
    }

    const float *v = (const float*)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g, %.6g, %.6g)", v[0], v[1], v[2], v[3]);
    NMO_RETURN_OK();
}

nmo_status_t nmo_vector4_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for vector4_from_string");
    }

    float *v = (float*)value;
    return parse_float_tuple("Vector4", string, v, 4);
}

/* ============================================================================
 * Quaternion Converters
 * ============================================================================ */

nmo_status_t nmo_quaternion_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 48) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for quaternion_to_string");
    }

    const float *q = (const float*)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g, %.6g, %.6g)", 
             q[0], q[1], q[2], q[3]);

    NMO_RETURN_OK();
}

nmo_status_t nmo_quaternion_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for quaternion_from_string");
    }

    float *q = (float*)value;
    
    // Skip whitespace and opening parenthesis
    while (*string && isspace(*string)) string++;
    if (*string != '(') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Quaternion must start with '('");
    }
    string++;

    // Parse four float values separated by commas
    char *endptr;
    for (int i = 0; i < 4; i++) {
        while (*string && isspace(*string)) string++;
        
        errno = 0;
        q[i] = strtof(string, &endptr);
        
        if (errno != 0 || endptr == string) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid quaternion component");
        }
        
        string = endptr;
        while (*string && isspace(*string)) string++;
        
        if (i < 3) {
            if (*string != ',') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Quaternion components must be separated by ','");
            }
            string++;
        }
    }

    // Expect closing parenthesis
    while (*string && isspace(*string)) string++;
    if (*string != ')') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Quaternion must end with ')'");
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Matrix/Color Converters
 * ============================================================================ */

nmo_status_t nmo_matrix_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 128) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for matrix_to_string");
    }

    const nmo_matrix_t *m = (const nmo_matrix_t*)value;
    snprintf(buffer, buffer_size,
             "(%.6g, %.6g, %.6g, %.6g; %.6g, %.6g, %.6g, %.6g; %.6g, %.6g, %.6g, %.6g; %.6g, %.6g, %.6g, %.6g)",
             m->m[0][0], m->m[0][1], m->m[0][2], m->m[0][3],
             m->m[1][0], m->m[1][1], m->m[1][2], m->m[1][3],
             m->m[2][0], m->m[2][1], m->m[2][2], m->m[2][3],
             m->m[3][0], m->m[3][1], m->m[3][2], m->m[3][3]);
    NMO_RETURN_OK();
}

nmo_status_t nmo_matrix_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for matrix_from_string");
    }

    float tmp[16];
    nmo_status_t r = parse_float_tuple("Matrix", string, tmp, 16);
    if (r != NMO_OK) {
        return r;
    }

    nmo_matrix_t *m = (nmo_matrix_t*)value;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            m->m[row][col] = tmp[row * 4 + col];
        }
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_color_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 48) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for color_to_string");
    }

    const float *c = (const float*)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g, %.6g, %.6g)", c[0], c[1], c[2], c[3]);
    NMO_RETURN_OK();
}

nmo_status_t nmo_color_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for color_from_string");
    }

    float *c = (float*)value;
    return parse_float_tuple("Color", string, c, 4);
}

/* ============================================================================
 * Enum/Flags Converters (require type metadata)
 * ============================================================================ */

nmo_status_t nmo_enum_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    bool use_name)
{
    if (!value || !type || !buffer || buffer_size < 16) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for enum_to_string");
    }

    if (!(type->category & NMO_TYPE_CATEGORY_ENUM)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type is not an enum");
    }

    int32_t enum_value = *(const int32_t*)value;

    /* If name not requested or no registry, output numeric value with type context */
    if (!use_name || !registry || type->specialized_index == NMO_SPECIALIZED_INDEX_INVALID) {
        if (type->name) {
            snprintf(buffer, buffer_size, "%s(%d)", type->name, enum_value);
        } else {
            snprintf(buffer, buffer_size, "enum(%d)", enum_value);
        }
        NMO_RETURN_OK();
    }

    /* Access enum metadata from registry */
    if (type->specialized_index >= registry->metadata.count) {
        if (type->name) {
            snprintf(buffer, buffer_size, "%s(%d)", type->name, enum_value);
        } else {
            snprintf(buffer, buffer_size, "enum(%d)", enum_value);
        }
        NMO_RETURN_OK();
    }

    const nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t**)nmo_arena_array_get((nmo_arena_array_t*)&registry->metadata, type->specialized_index);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_ENUM) {
        if (type->name) {
            snprintf(buffer, buffer_size, "%s(%d)", type->name, enum_value);
        } else {
            snprintf(buffer, buffer_size, "enum(%d)", enum_value);
        }
        NMO_RETURN_OK();
    }

    /* Search for matching enum value */
    for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
        if (metadata->enum_meta.values[i].value == enum_value) {
            snprintf(buffer, buffer_size, "%s", metadata->enum_meta.values[i].name);
            NMO_RETURN_OK();
        }
    }

    /* No name found, output numeric value with type context */
    if (type->name) {
        snprintf(buffer, buffer_size, "%s(%d)", type->name, enum_value);
    } else {
        snprintf(buffer, buffer_size, "enum(%d)", enum_value);
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_enum_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for enum_from_string");
    }

    if (!(type->category & NMO_TYPE_CATEGORY_ENUM)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type is not an enum");
    }

    // Try to match name in enum metadata
    if (registry && type->specialized_index != NMO_SPECIALIZED_INDEX_INVALID &&
        type->specialized_index < registry->metadata.count) {
        const nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t**)nmo_arena_array_get((nmo_arena_array_t*)&registry->metadata, type->specialized_index);
        if (metadata && metadata->metadata_type == NMO_METADATA_TYPE_ENUM) {
            for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
                if (strcmp(metadata->enum_meta.values[i].name, string) == 0) {
                    *(int32_t*)value = (int32_t)metadata->enum_meta.values[i].value;
                    NMO_RETURN_OK();
                }
            }
        }
    }

    // Try to parse as integer
    char *endptr;
    errno = 0;
    long result = strtol(string, &endptr, 0);

    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace(*endptr))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid enum value");
    }

    *(int32_t*)value = (int32_t)result;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Flags Converters
 * ============================================================================ */

nmo_status_t nmo_flags_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    bool use_names)
{
    if (!value || !type || !buffer || buffer_size < 16) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for flags_to_string");
    }

    if (!(type->category & NMO_TYPE_CATEGORY_FLAGS)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type is not flags");
    }

    uint32_t flags_value = *(const uint32_t*)value;

    /* If names not requested or no registry, output hex with type context */
    if (!use_names || !registry || type->specialized_index == NMO_SPECIALIZED_INDEX_INVALID) {
        if (type->name) {
            snprintf(buffer, buffer_size, "%s(0x%X)", type->name, flags_value);
        } else {
            snprintf(buffer, buffer_size, "flags(0x%X)", flags_value);
        }
        NMO_RETURN_OK();
    }

    /* Access flags metadata from registry */
    if (type->specialized_index >= registry->metadata.count) {
        if (type->name) {
            snprintf(buffer, buffer_size, "%s(0x%X)", type->name, flags_value);
        } else {
            snprintf(buffer, buffer_size, "flags(0x%X)", flags_value);
        }
        NMO_RETURN_OK();
    }

    const nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t**)nmo_arena_array_get((nmo_arena_array_t*)&registry->metadata, type->specialized_index);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_FLAGS) {
        if (type->name) {
            snprintf(buffer, buffer_size, "%s(0x%X)", type->name, flags_value);
        } else {
            snprintf(buffer, buffer_size, "flags(0x%X)", flags_value);
        }
        NMO_RETURN_OK();
    }

    /* Build name1|name2 format */
    size_t offset = 0;
    bool first = true;
    for (size_t i = 0; i < metadata->flags_meta.bit_count; i++) {
        if ((flags_value & metadata->flags_meta.bits[i].mask) == metadata->flags_meta.bits[i].mask) {
            if (!first && offset < buffer_size) {
                buffer[offset++] = '|';
            }
            first = false;
            const char *name = metadata->flags_meta.bits[i].name;
            while (*name && offset < buffer_size - 1) {
                buffer[offset++] = *name++;
            }
        }
    }

    /* If no flags matched, output hex with type context */
    if (first) {
        if (type->name) {
            snprintf(buffer, buffer_size, "%s(0x%X)", type->name, flags_value);
        } else {
            snprintf(buffer, buffer_size, "flags(0x%X)", flags_value);
        }
    } else {
        buffer[offset] = '\0';
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_flags_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for flags_from_string");
    }

    if (!(type->category & NMO_TYPE_CATEGORY_FLAGS)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Type is not flags");
    }

    // Try hex format first
    if (strncmp(string, "0x", 2) == 0 || strncmp(string, "0X", 2) == 0) {
        char *endptr;
        unsigned long result = strtoul(string, &endptr, 16);
        if (errno == 0 && *endptr == '\0') {
            *(uint32_t*)value = (uint32_t)result;
            NMO_RETURN_OK();
        }
    }

    // Try numeric format
    char *endptr;
    errno = 0;
    unsigned long result = strtoul(string, &endptr, 0);
    if (errno == 0 && *endptr == '\0') {
        *(uint32_t*)value = (uint32_t)result;
        NMO_RETURN_OK();
    }

    // Parse name1|name2 format from metadata
    if (registry && type->specialized_index != NMO_SPECIALIZED_INDEX_INVALID &&
        type->specialized_index < registry->metadata.count) {
        const nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t**)nmo_arena_array_get((nmo_arena_array_t*)&registry->metadata, type->specialized_index);
        if (metadata && metadata->metadata_type == NMO_METADATA_TYPE_FLAGS) {
            uint32_t flags_result = 0;
            const char *start = string;
            
            while (*start) {
                // Skip whitespace
                while (*start && isspace(*start)) start++;
                if (!*start) break;
                
                // Find end of name (| or end of string)
                const char *end = start;
                while (*end && *end != '|') end++;
                
                // Match name
                size_t name_len = end - start;
                bool found = false;
                for (size_t i = 0; i < metadata->flags_meta.bit_count; i++) {
                    const char *bit_name = metadata->flags_meta.bits[i].name;
                    if (strncmp(bit_name, start, name_len) == 0 && bit_name[name_len] == '\0') {
                        flags_result |= (uint32_t)metadata->flags_meta.bits[i].mask;
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Unknown flag name");
                }
                
                // Move to next
                start = (*end == '|') ? end + 1 : end;
            }
            
            *(uint32_t*)value = flags_result;
            NMO_RETURN_OK();
        }
    }

    NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid flags format");
}

/* ============================================================================
 * String Escape/Unescape Utilities
 * ============================================================================ */

size_t nmo_string_escape(
    const char *src,
    char *dst,
    size_t dst_size)
{
    if (!src || !dst || dst_size < 3) {
        return 0;
    }

    size_t offset = 0;
    dst[offset++] = '"';  // Opening quote

    for (const char *p = src; *p && offset + 2 < dst_size; p++) {
        switch (*p) {
            case '"':  dst[offset++] = '\\'; dst[offset++] = '"'; break;
            case '\\': dst[offset++] = '\\'; dst[offset++] = '\\'; break;
            case '\n': dst[offset++] = '\\'; dst[offset++] = 'n'; break;
            case '\r': dst[offset++] = '\\'; dst[offset++] = 'r'; break;
            case '\t': dst[offset++] = '\\'; dst[offset++] = 't'; break;
            default:
                if (*p >= 32 && *p <= 126) {
                    dst[offset++] = *p;
                } else {
                    // Escape non-printable as \xHH
                    if (offset + 4 < dst_size) {
                        offset += snprintf(dst + offset, dst_size - offset, 
                                         "\\x%02X", (unsigned char)*p);
                    }
                }
                break;
        }
    }

    if (offset < dst_size) {
        dst[offset++] = '"';  // Closing quote
    }
    if (offset < dst_size) {
        dst[offset] = '\0';
    } else {
        dst[dst_size - 1] = '\0';
        offset = dst_size - 1;
    }

    return offset;
}

size_t nmo_string_unescape(
    const char *src,
    char *dst,
    size_t dst_size)
{
    if (!src || !dst || dst_size < 1) {
        return 0;
    }

    // Skip opening quote
    if (*src == '"') src++;

    size_t offset = 0;
    for (const char *p = src; *p && offset + 1 < dst_size; p++) {
        if (*p == '"') break;  // Closing quote
        
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case '"':  dst[offset++] = '"'; break;
                case '\\': dst[offset++] = '\\'; break;
                case 'n':  dst[offset++] = '\n'; break;
                case 'r':  dst[offset++] = '\r'; break;
                case 't':  dst[offset++] = '\t'; break;
                case 'x':  // Hex escape \xHH
                    if (isxdigit(*(p + 1)) && isxdigit(*(p + 2))) {
                        char hex[3] = {*(p + 1), *(p + 2), '\0'};
                        dst[offset++] = (char)strtol(hex, NULL, 16);
                        p += 2;
                    }
                    break;
                default:
                    dst[offset++] = *p;
                    break;
            }
        } else {
            dst[offset++] = *p;
        }
    }

    if (offset < dst_size) {
        dst[offset] = '\0';
    } else {
        dst[dst_size - 1] = '\0';
        offset = dst_size - 1;
    }

    return offset;
}

/* ============================================================================
 * String Converters
 * ============================================================================ */

nmo_status_t nmo_string_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 3) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for string_to_string");
    }

    const char *str = *(const char**)value;
    if (!str) {
        snprintf(buffer, buffer_size, "\"\"");
        NMO_RETURN_OK();
    }

    nmo_string_escape(str, buffer, buffer_size);
    NMO_RETURN_OK();
}

nmo_status_t nmo_string_from_string(
    void *value,
    const char *string,
    nmo_arena_t *arena)
{
    if (!value || !string || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for string_from_string");
    }

    // Estimate unescaped length (worst case: same as input)
    size_t max_len = strlen(string) + 1;
    char *temp = (char*)nmo_arena_alloc(arena, max_len, 1);
    if (!temp) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate string buffer");
    }

    nmo_string_unescape(string, temp, max_len);
    
    // Allocate exact size needed
    size_t actual_len = strlen(temp) + 1;
    char *result = (char*)nmo_arena_alloc(arena, actual_len, 1);
    if (!result) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate final string");
    }

    memcpy(result, temp, actual_len);
    *(char**)value = result;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Object ID Converters
 * ============================================================================ */

static nmo_object_id_to_name_resolver_fn g_object_id_to_name_resolver = NULL;
static nmo_object_name_to_id_resolver_fn g_object_name_to_id_resolver = NULL;

void nmo_type_string_set_object_resolvers(
    nmo_object_id_to_name_resolver_fn id_to_name,
    nmo_object_name_to_id_resolver_fn name_to_id)
{
    g_object_id_to_name_resolver = id_to_name;
    g_object_name_to_id_resolver = name_to_id;
}

static bool nmo_object_name_is_safe_token(const char *name)
{
    if (!name || *name == '\0' || *name == '#') {
        return false;
    }

    for (const char *p = name; *p != '\0'; ++p) {
        unsigned char c = (unsigned char)(*p);
        if (isspace(c) || c == '"' || c == '\\') {
            return false;
        }
    }

    return true;
}

nmo_status_t nmo_object_id_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size,
    struct nmo_session *session)
{
    if (!value || !buffer || buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for object_id_to_string");
    }

    nmo_object_id_t id = *(const nmo_object_id_t*)value;

    if (session && g_object_id_to_name_resolver) {
        const char *name = NULL;
        nmo_status_t resolved = g_object_id_to_name_resolver(session, id, &name);
        if (resolved == NMO_OK && nmo_object_name_is_safe_token(name)) {
            size_t len = strlen(name);
            if (len + 1 > buffer_size) {
                NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for object name");
            }

            memcpy(buffer, name, len + 1);
            NMO_RETURN_OK();
        }
    }

    int written = snprintf(buffer, buffer_size, "#%u", id);
    if (written < 0 || (size_t)written >= buffer_size) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small for object id");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_object_id_from_string(
    void *value,
    const char *string,
    struct nmo_session *session)
{
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for object_id_from_string");
    }

    // Parse #id format
    if (*string == '#') {
        string++;
        char *endptr;
        unsigned long id = strtoul(string, &endptr, 10);
        if (errno == 0 && *endptr == '\0') {
            *(nmo_object_id_t*)value = (nmo_object_id_t)id;
            NMO_RETURN_OK();
        }
    }

    // Name lookup (optional)
    if (session && g_object_name_to_id_resolver) {
        nmo_object_id_t resolved_id = 0;
        nmo_status_t resolved = g_object_name_to_id_resolver(session, string, &resolved_id);
        if (resolved == NMO_OK) {
            *(nmo_object_id_t*)value = resolved_id;
        }
        return resolved;
    }

    NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid object ID format (expected #id)");
}

/* ============================================================================
 * General-Purpose Dispatcher
 * ============================================================================ */

typedef struct nmo_string_builder_t {
    char *buf;
    size_t cap;
    size_t len;
} nmo_string_builder_t;

static nmo_status_t nmo_sb_append(nmo_string_builder_t *sb, const char *fmt, ...) {
    if (!sb || !fmt) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid string builder args");
    }
    if (sb->cap == 0 || sb->len >= sb->cap) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small");
    }

    va_list args;
    va_start(args, fmt);
    int wrote = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, args);
    va_end(args);

    if (wrote < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Failed to format string");
    }
    if ((size_t)wrote >= sb->cap - sb->len) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small");
    }

    sb->len += (size_t)wrote;
    NMO_RETURN_OK();
}

static const nmo_type_descriptor_t *nmo_to_string_resolve_type(
    const nmo_type_registry_t *registry,
    nmo_guid_t guid
) {
    if (!registry) {
        return NULL;
    }

    const nmo_type_descriptor_t *t = nmo_type_registry_find_by_guid(registry, guid);
    return t;
}

typedef nmo_status_t (*nmo_value_to_string_fn)(const void *value, char *buffer, size_t buffer_size);

static nmo_status_t nmo_object_id_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_object_id_to_string(value, buffer, buffer_size, NULL);
}

static nmo_status_t nmo_guid_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    int wrote = nmo_guid_format(*(const nmo_guid_t *)value, buffer, buffer_size);
    if (wrote < 0) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small");
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_string_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_string_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_pointer_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    const void *ptr = *(const void *const *)value;
    if (!ptr) {
        snprintf(buffer, buffer_size, "null");
    } else {
        uintptr_t v = (uintptr_t)ptr;
        snprintf(buffer, buffer_size, "0x%llX", (unsigned long long)v);
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_rect_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    const nmo_rect_t *r = (const nmo_rect_t *)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g, %.6g, %.6g)", r->left, r->top, r->right, r->bottom);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_box_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    const nmo_box_t *b = (const nmo_box_t *)value;
    snprintf(buffer, buffer_size,
             "((%.6g, %.6g, %.6g), (%.6g, %.6g, %.6g))",
             b->min.x, b->min.y, b->min.z,
             b->max.x, b->max.y, b->max.z);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_eulerangles_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    const nmo_eulerangles_t *e = (const nmo_eulerangles_t *)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g, %.6g)", e->x, e->y, e->z);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_int_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_int_to_string(value, buffer, buffer_size, false);
}

static nmo_status_t nmo_uint32_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%u", *(const uint32_t *)value);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_int8_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%d", (int)*(const int8_t *)value);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_uint8_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%u", (unsigned)*(const uint8_t *)value);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_int16_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%d", (int)*(const int16_t *)value);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_uint16_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%u", (unsigned)*(const uint16_t *)value);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_int64_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%lld", (long long)*(const int64_t *)value);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_uint64_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%llu", (unsigned long long)*(const uint64_t *)value);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_double_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    double d = *(const double *)value;
    if (isnan(d)) {
        snprintf(buffer, buffer_size, "NaN");
    } else if (isinf(d)) {
        snprintf(buffer, buffer_size, d > 0 ? "Infinity" : "-Infinity");
    } else {
        snprintf(buffer, buffer_size, "%.6g", d);
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_float_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_float_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_bool_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_bool_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_vector2_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_vector2_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_vector3_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_vector_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_vector4_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_vector4_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_quaternion_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_quaternion_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_matrix_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_matrix_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_color_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    return nmo_color_to_string(value, buffer, buffer_size);
}

static nmo_status_t nmo_angle_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    float rad = *(const float *)value;
    double deg = (double)rad * (180.0 / M_PI);
    snprintf(buffer, buffer_size, "%.6g\xC2\xB0", deg);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_percentage_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    float f = *(const float *)value;
    snprintf(buffer, buffer_size, "%.6g%%", (double)f * 100.0);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_time_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    float f = *(const float *)value;
    snprintf(buffer, buffer_size, "%.1f ms", (double)f);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_classid_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    int32_t cid = *(const int32_t *)value;
    snprintf(buffer, buffer_size, "ClassID(%d)", cid);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_none_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    (void)value;
    snprintf(buffer, buffer_size, "(none)");
    NMO_RETURN_OK();
}

static nmo_status_t nmo_voidbuf_value_to_string(const void *value, char *buffer, size_t buffer_size) {
    /* Void buffers have no intrinsic size from the handler signature, show address */
    (void)value;
    snprintf(buffer, buffer_size, "<voidbuf>");
    NMO_RETURN_OK();
}

typedef struct nmo_guid_to_string_entry {
    nmo_guid_t guid;
    nmo_value_to_string_fn fn;
} nmo_guid_to_string_entry_t;

static const nmo_guid_to_string_entry_t nmo_guid_to_string_table[] = {
    {NMO_GUID_INIT(CKPGUID_ID_D1, CKPGUID_ID_D2), nmo_object_id_value_to_string},
    {NMO_GUID_INIT(CKPGUID_GUID_D1, CKPGUID_GUID_D2), nmo_guid_value_to_string},
    {NMO_GUID_INIT(CKPGUID_STRING_D1, CKPGUID_STRING_D2), nmo_string_value_to_string},
    {NMO_GUID_INIT(CKPGUID_POINTER_D1, CKPGUID_POINTER_D2), nmo_pointer_value_to_string},
    {NMO_GUID_INIT(CKPGUID_INT8_D1, CKPGUID_INT8_D2), nmo_int8_value_to_string},
    {NMO_GUID_INIT(CKPGUID_UINT8_D1, CKPGUID_UINT8_D2), nmo_uint8_value_to_string},
    {NMO_GUID_INIT(CKPGUID_INT16_D1, CKPGUID_INT16_D2), nmo_int16_value_to_string},
    {NMO_GUID_INIT(CKPGUID_UINT16_D1, CKPGUID_UINT16_D2), nmo_uint16_value_to_string},
    {NMO_GUID_INIT(CKPGUID_INT_D1, CKPGUID_INT_D2), nmo_int_value_to_string},
    {NMO_GUID_INIT(CKPGUID_UINT32_D1, CKPGUID_UINT32_D2), nmo_uint32_value_to_string},
    {NMO_GUID_INIT(CKPGUID_INT64_D1, CKPGUID_INT64_D2), nmo_int64_value_to_string},
    {NMO_GUID_INIT(CKPGUID_UINT64_D1, CKPGUID_UINT64_D2), nmo_uint64_value_to_string},
    {NMO_GUID_INIT(CKPGUID_DOUBLE_D1, CKPGUID_DOUBLE_D2), nmo_double_value_to_string},
    {NMO_GUID_INIT(CKPGUID_FLOAT_D1, CKPGUID_FLOAT_D2), nmo_float_value_to_string},
    {NMO_GUID_INIT(CKPGUID_BOOL_D1, CKPGUID_BOOL_D2), nmo_bool_value_to_string},
    {NMO_GUID_INIT(CKPGUID_2DVECTOR_D1, CKPGUID_2DVECTOR_D2), nmo_vector2_value_to_string},
    {NMO_GUID_INIT(CKPGUID_VECTOR_D1, CKPGUID_VECTOR_D2), nmo_vector3_value_to_string},
    {NMO_GUID_INIT(CKPGUID_VECTOR4_D1, CKPGUID_VECTOR4_D2), nmo_vector4_value_to_string},
    {NMO_GUID_INIT(CKPGUID_QUATERNION_D1, CKPGUID_QUATERNION_D2), nmo_quaternion_value_to_string},
    {NMO_GUID_INIT(CKPGUID_MATRIX_D1, CKPGUID_MATRIX_D2), nmo_matrix_value_to_string},
    {NMO_GUID_INIT(CKPGUID_COLOR_D1, CKPGUID_COLOR_D2), nmo_color_value_to_string},
    {NMO_GUID_INIT(CKPGUID_RECT_D1, CKPGUID_RECT_D2), nmo_rect_value_to_string},
    {NMO_GUID_INIT(CKPGUID_BOX_D1, CKPGUID_BOX_D2), nmo_box_value_to_string},
    {NMO_GUID_INIT(CKPGUID_EULERANGLES_D1, CKPGUID_EULERANGLES_D2), nmo_eulerangles_value_to_string},
    /* Derived float types with semantic formatting */
    {NMO_GUID_INIT(CKPGUID_ANGLE_D1, CKPGUID_ANGLE_D2), nmo_angle_value_to_string},
    {NMO_GUID_INIT(CKPGUID_PERCENTAGE_D1, CKPGUID_PERCENTAGE_D2), nmo_percentage_value_to_string},
    {CKPGUID_TIME_INIT, nmo_time_value_to_string},
    /* Derived int type */
    {CKPGUID_CLASSID_INIT, nmo_classid_value_to_string},
    /* Special types */
    {CKPGUID_NONE_INIT, nmo_none_value_to_string},
    {CKPGUID_VOIDBUF_INIT, nmo_voidbuf_value_to_string}
};

static bool nmo_value_to_string_by_guid(
    nmo_guid_t guid,
    const void *value,
    char *buffer,
    size_t buffer_size,
    nmo_status_t *out_status)
{
    for (size_t i = 0; i < sizeof(nmo_guid_to_string_table) / sizeof(nmo_guid_to_string_table[0]); i++) {
        if (nmo_guid_equals(guid, nmo_guid_to_string_table[i].guid)) {
            *out_status = nmo_guid_to_string_table[i].fn(value, buffer, buffer_size);
            return true;
        }
    }
    return false;
}

static nmo_status_t nmo_type_value_to_string_impl(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth
);

static nmo_status_t nmo_struct_like_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth
) {
    enum { NMO_MAX_TO_STRING_DEPTH = 6 };

    if (!value || !type || !buffer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments for struct_to_string");
    }
    if (buffer_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_BUFFER_OVERRUN, NMO_SEVERITY_ERROR, "Buffer too small");
    }

    if (depth >= NMO_MAX_TO_STRING_DEPTH) {
        if (type->name) {
            snprintf(buffer, buffer_size, "<%s ...>", type->name);
        } else {
            snprintf(buffer, buffer_size, "<%s %u bytes>",
                     (type->category & NMO_TYPE_CATEGORY_UNION) ? "union" : "struct",
                     type->size);
        }
        NMO_RETURN_OK();
    }

    /* If no reflection fields, try specialized struct metadata */
    if ((!type->fields || type->field_count == 0) && registry) {
        const nmo_specialized_metadata_t *meta =
            nmo_type_registry_get_metadata(registry, type->id);
        if (meta &&
            (meta->metadata_type == NMO_METADATA_TYPE_STRUCT ||
             meta->metadata_type == NMO_METADATA_TYPE_UNION)) {
            const nmo_struct_descriptor_t *sfields = NULL;
            size_t scount = 0;
            if (meta->metadata_type == NMO_METADATA_TYPE_STRUCT) {
                sfields = meta->struct_meta.fields;
                scount = meta->struct_meta.field_count;
            } else {
                sfields = meta->union_meta.fields;
                scount = meta->union_meta.field_count;
            }

            if (sfields && scount > 0) {
                nmo_string_builder_t sb = { .buf = buffer, .cap = buffer_size, .len = 0 };
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "{"));

                bool first = true;
                for (size_t i = 0; i < scount; i++) {
                    const nmo_struct_descriptor_t *sf = &sfields[i];
                    if ((uint64_t)sf->offset + sf->size > type->size) continue;

                    const nmo_type_descriptor_t *ft =
                        nmo_to_string_resolve_type(registry, sf->type_guid);
                    const uint8_t *fptr = (const uint8_t *)value + sf->offset;

                    if (!first) {
                        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, ", "));
                    }
                    first = false;
                    NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "%s=",
                        sf->name ? sf->name : "<unnamed>"));

                    if (!ft) {
                        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<unknown>"));
                        continue;
                    }

                    if (sf->array_count > 0) {
                        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<array[%u]>",
                            (unsigned)sf->array_count));
                        continue;
                    }

                    char tmp[256];
                    nmo_status_t r = nmo_type_value_to_string_impl(
                        fptr, ft, registry, tmp, sizeof(tmp), depth + 1);
                    if (r != NMO_OK) {
                        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<error>"));
                        continue;
                    }
                    NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "%s", tmp));
                }

                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "}"));
                NMO_RETURN_OK();
            }
        }
    }

    nmo_string_builder_t sb = { .buf = buffer, .cap = buffer_size, .len = 0 };
    NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "{"));

    for (size_t i = 0; i < type->field_count; i++) {
        const nmo_type_field_t *field = &type->fields[i];
        const nmo_type_descriptor_t *field_type = nmo_to_string_resolve_type(registry, field->type_guid);
        const uint8_t *field_ptr = (const uint8_t *)value + field->offset;

        if (i > 0) {
            NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, ", "));
        }
        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "%s=", field->name ? field->name : "<unnamed>"));

        if (!field_type) {
            NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<unknown>"));
            continue;
        }

        if (field->flags & NMO_FIELD_REPEATED) {
            uint64_t count = 0;

            if (field->name && type->fields && type->field_count > 0) {
                char base_name[96];
                char count_name[112];

                (void)snprintf(base_name, sizeof(base_name), "%s", field->name);
                size_t base_len = strlen(base_name);

                struct {
                    const char *suffix;
                    size_t suffix_len;
                } strip_suffixes[] = {
                    {"_ids", 4},
                    {"_types", 6},
                    {"_indices", 8},
                    {"_chunks", 7},
                };

                bool stripped = false;
                for (size_t s = 0; s < sizeof(strip_suffixes) / sizeof(strip_suffixes[0]); s++) {
                    if (base_len > strip_suffixes[s].suffix_len &&
                        strcmp(base_name + base_len - strip_suffixes[s].suffix_len, strip_suffixes[s].suffix) == 0) {
                        base_name[base_len - strip_suffixes[s].suffix_len] = '\0';
                        stripped = true;
                        break;
                    }
                }
                if (!stripped) {
                    base_len = strlen(base_name);
                    if (base_len > 1 && base_name[base_len - 1] == 's') {
                        base_name[base_len - 1] = '\0';
                    }
                }

                (void)snprintf(count_name, sizeof(count_name), "%s_count", base_name);

                for (size_t j = 0; j < type->field_count; j++) {
                    const nmo_type_field_t *cf = &type->fields[j];
                    if (!cf->name || strcmp(cf->name, count_name) != 0) {
                        continue;
                    }

                    const nmo_type_descriptor_t *count_type = nmo_to_string_resolve_type(registry, cf->type_guid);
                    if (!count_type) {
                        break;
                    }

                    nmo_guid_t count_guid = count_type->guid;

                    const uint8_t *count_ptr = (const uint8_t *)value + cf->offset;
                    if (nmo_guid_equals(count_guid, CKPGUID_UINT32)) {
                        count = *(const uint32_t *)count_ptr;
                    } else if (nmo_guid_equals(count_guid, CKPGUID_INT)) {
                        int32_t v = *(const int32_t *)count_ptr;
                        if (v >= 0) {
                            count = (uint64_t)v;
                        }
                    } else if (nmo_guid_equals(count_guid, CKPGUID_UINT64)) {
                        count = *(const uint64_t *)count_ptr;
                    } else if (nmo_guid_equals(count_guid, CKPGUID_INT64)) {
                        int64_t v = *(const int64_t *)count_ptr;
                        if (v >= 0) {
                            count = (uint64_t)v;
                        }
                    }
                    break;
                }
            }

            if (count > 0) {
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "[%llu]", (unsigned long long)count));
            } else {
                NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "[...]"));
            }
            continue;
        }

        char tmp[256];
        nmo_status_t r = nmo_type_value_to_string_impl(field_ptr, field_type, registry,
                                                       tmp, sizeof(tmp), depth + 1);
        if (r != NMO_OK) {
            NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "<error>"));
            continue;
        }

        NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "%s", tmp));
    }

    NMO_RETURN_IF_ERROR(nmo_sb_append(&sb, "}"));
    NMO_RETURN_OK();
}

static nmo_status_t nmo_type_value_to_string_impl(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    int depth
) {
    if (!value || !type || !buffer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments for type_value_to_string");
    }

    if (type->category & NMO_TYPE_CATEGORY_ENUM) {
        return nmo_enum_to_string(value, type, registry, buffer, buffer_size, true);
    }
    if (type->category & NMO_TYPE_CATEGORY_FLAGS) {
        return nmo_flags_to_string(value, type, registry, buffer, buffer_size, true);
    }

    nmo_guid_t effective_guid = type->guid;
    nmo_status_t result = NMO_OK;

    /* If a type provides a custom to_string implementation, prefer it.
     * This makes vtables authoritative for parameter value types.
     *
     * IMPORTANT: Skip nmo_object_default_to_string (the default trampoline).
     * It calls the PUBLIC nmo_type_value_to_string which resets depth to 0,
     * creating infinite recursion and stack overflow.  Object types with
     * reflection fields are better handled by struct-like rendering below
     * which properly tracks recursion depth. */
    if (type->vtable && type->vtable->to_string) {
        /* Skip the default object trampoline that re-enters this function.
         * Types with reflection fields will fall through to struct-like
         * rendering which respects the depth limit. */
        extern nmo_status_t nmo_object_default_to_string(
            const void *, const nmo_type_descriptor_t *, char *, size_t, void *);
        if (type->vtable->to_string != nmo_object_default_to_string) {
            return type->vtable->to_string(value, type, buffer, buffer_size, (void *)registry);
        }
    }

    /* Prefer explicit built-in GUID formatting over generic struct formatting.
     * Some builtin composites may be registered with STRUCT category. */
    if (nmo_value_to_string_by_guid(effective_guid, value, buffer, buffer_size, &result)) {
        return result;
    }

    if (type->category & (NMO_TYPE_CATEGORY_STRUCT | NMO_TYPE_CATEGORY_UNION)) {
        return nmo_struct_like_to_string(value, type, registry, buffer, buffer_size, depth);
    }

    /* Object types can also carry reflection fields; render those like structs. */
    if (type->fields && type->field_count > 0) {
        return nmo_struct_like_to_string(value, type, registry, buffer, buffer_size, depth);
    }

    if (type->size == sizeof(float) && type->alignment == _Alignof(float)) {
        return nmo_float_to_string(value, buffer, buffer_size);
    }
    if (type->size == sizeof(int32_t) && type->alignment == _Alignof(int32_t)) {
        return nmo_int_to_string(value, buffer, buffer_size, false);
    }
    if (type->size == sizeof(bool)) {
        return nmo_bool_to_string(value, buffer, buffer_size);
    }

    /* Show hex for small values, hex dump for medium, preview for large */
    if (type->size <= 4) {
        uint32_t v = 0;
        memcpy(&v, value, type->size);
        snprintf(buffer, buffer_size, "0x%0*X (%u bytes)",
                 (int)type->size * 2, v, type->size);
    } else if (type->size <= 16) {
        size_t pos = 0;
        for (size_t i = 0; i < type->size && pos + 3 < buffer_size; i++) {
            pos += (size_t)snprintf(buffer + pos, buffer_size - pos, "%s%02X",
                                    i > 0 ? " " : "",
                                    ((const uint8_t *)value)[i]);
        }
    } else {
        size_t pos = (size_t)snprintf(buffer, buffer_size, "<%u bytes: ",
                                      type->size);
        for (size_t i = 0; i < 8 && i < type->size && pos + 3 < buffer_size; i++) {
            pos += (size_t)snprintf(buffer + pos, buffer_size - pos, "%02X ",
                                    ((const uint8_t *)value)[i]);
        }
        if (pos + 4 < buffer_size) {
            snprintf(buffer + pos, buffer_size - pos, "...>");
        }
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_type_value_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !type || !buffer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for type_value_to_string");
    }

    return nmo_type_value_to_string_impl(value, type, registry, buffer, buffer_size, 0);
}

static nmo_status_t parse_i64(const char *string, int64_t *out_value)
{
    if (!string || !out_value) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid integer parse args");
    }

    char *endptr = NULL;
    errno = 0;
    long long parsed = strtoll(string, &endptr, 0);
    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace((unsigned char)*endptr))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid integer format");
    }

    *out_value = (int64_t)parsed;
    NMO_RETURN_OK();
}

static nmo_status_t parse_u64(const char *string, uint64_t *out_value)
{
    if (!string || !out_value) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid unsigned parse args");
    }

    const char *p = string;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '-') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Unsigned value cannot be negative");
    }

    char *endptr = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(string, &endptr, 0);
    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace((unsigned char)*endptr))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid unsigned integer format");
    }

    *out_value = (uint64_t)parsed;
    NMO_RETURN_OK();
}

static nmo_status_t parse_f64(const char *string, double *out_value)
{
    if (!string || !out_value) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid floating parse args");
    }

    if (strcmp(string, "NaN") == 0) {
        *out_value = NAN;
        NMO_RETURN_OK();
    }
    if (strcmp(string, "Infinity") == 0 || strcmp(string, "+Infinity") == 0) {
        *out_value = INFINITY;
        NMO_RETURN_OK();
    }
    if (strcmp(string, "-Infinity") == 0) {
        *out_value = -INFINITY;
        NMO_RETURN_OK();
    }

    char *endptr = NULL;
    errno = 0;
    double parsed = strtod(string, &endptr);
    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace((unsigned char)*endptr))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid floating-point format");
    }

    *out_value = parsed;
    NMO_RETURN_OK();
}

typedef nmo_status_t (*nmo_value_from_string_fn)(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string);

static nmo_status_t nmo_parse_int8(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    int64_t parsed = 0;
    nmo_status_t st = parse_i64(string, &parsed);
    if (st != NMO_OK) return st;
    if (parsed < INT8_MIN || parsed > INT8_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "INT8 out of range");
    }
    *(int8_t *)value = (int8_t)parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_int16(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    int64_t parsed = 0;
    nmo_status_t st = parse_i64(string, &parsed);
    if (st != NMO_OK) return st;
    if (parsed < INT16_MIN || parsed > INT16_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "INT16 out of range");
    }
    *(int16_t *)value = (int16_t)parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_int32(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_int_from_string(value, string);
}

static nmo_status_t nmo_parse_int64(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    int64_t parsed = 0;
    nmo_status_t st = parse_i64(string, &parsed);
    if (st != NMO_OK) return st;
    *(int64_t *)value = parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_uint8(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    uint64_t parsed = 0;
    nmo_status_t st = parse_u64(string, &parsed);
    if (st != NMO_OK) return st;
    if (parsed > UINT8_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "UINT8 out of range");
    }
    *(uint8_t *)value = (uint8_t)parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_uint16(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    uint64_t parsed = 0;
    nmo_status_t st = parse_u64(string, &parsed);
    if (st != NMO_OK) return st;
    if (parsed > UINT16_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "UINT16 out of range");
    }
    *(uint16_t *)value = (uint16_t)parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_uint32(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    uint64_t parsed = 0;
    nmo_status_t st = parse_u64(string, &parsed);
    if (st != NMO_OK) return st;
    if (parsed > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_OUT_OF_BOUNDS, NMO_SEVERITY_ERROR, "UINT32 out of range");
    }
    *(uint32_t *)value = (uint32_t)parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_uint64(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    uint64_t parsed = 0;
    nmo_status_t st = parse_u64(string, &parsed);
    if (st != NMO_OK) return st;
    *(uint64_t *)value = parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_bool(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_bool_from_string(value, string);
}

static nmo_status_t nmo_parse_float(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_float_from_string(value, string);
}

static nmo_status_t nmo_parse_double(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    double parsed = 0.0;
    nmo_status_t st = parse_f64(string, &parsed);
    if (st != NMO_OK) return st;
    *(double *)value = parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_string(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!registry || !registry->arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Registry with arena required for string parsing");
    }
    return nmo_string_from_string(value, string, (nmo_arena_t *)registry->arena);
}

static nmo_status_t nmo_parse_vector2(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_vector2_from_string(value, string);
}

static nmo_status_t nmo_parse_vector3(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_vector_from_string(value, string);
}

static nmo_status_t nmo_parse_vector4(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_vector4_from_string(value, string);
}

static nmo_status_t nmo_parse_quaternion(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_quaternion_from_string(value, string);
}

static nmo_status_t nmo_parse_matrix(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_matrix_from_string(value, string);
}

static nmo_status_t nmo_parse_color(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    return nmo_color_from_string(value, string);
}

static nmo_status_t nmo_parse_guid(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for guid parse");
    }

    while (*string && isspace((unsigned char)*string)) string++;
    nmo_guid_t g = nmo_guid_parse(string);
    if (nmo_guid_is_null(g) && !(string[0] == '0' && string[1] == '\0')) {
        /* Accept {00000000-00000000} as valid null GUID, but reject parse failures */
        if (strcmp(string, "{00000000-00000000}") != 0 &&
            strcmp(string, "00000000-00000000") != 0 &&
            strcmp(string, "0000000000000000") != 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid GUID format");
        }
    }

    *(nmo_guid_t *)value = g;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_pointer(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for pointer parse");
    }

    while (*string && isspace((unsigned char)*string)) string++;
    if (strcmp(string, "null") == 0 || strcmp(string, "NULL") == 0) {
        *(void **)value = NULL;
        NMO_RETURN_OK();
    }

    char *endptr = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(string, &endptr, 0);
    if (errno != 0 || endptr == string) {
        /* Try hex without 0x (common %p style) */
        errno = 0;
        parsed = strtoull(string, &endptr, 16);
    }
    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace((unsigned char)*endptr))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Invalid pointer format");
    }

    *(void **)value = (void *)(uintptr_t)parsed;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_object_id(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for object id parse");
    }

    /* Prefer #id format (and optional name lookup if resolver installed elsewhere) */
    nmo_status_t st = nmo_object_id_from_string(value, string, NULL);
    if (st == NMO_OK) {
        return st;
    }

    /* Back-compat: accept raw integer without '#' */
    return nmo_parse_uint32(value, registry, string);
}

static nmo_status_t nmo_parse_rect(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for rect parse");
    }

    float out[4] = {0};
    nmo_status_t st = parse_float_tuple("Rect", string, out, 4);
    if (st != NMO_OK) return st;

    nmo_rect_t *r = (nmo_rect_t *)value;
    r->left = out[0];
    r->top = out[1];
    r->right = out[2];
    r->bottom = out[3];
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_box(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for box parse");
    }

    /* Format: ((x,y,z), (x,y,z)) with optional whitespace */
    while (*string && isspace((unsigned char)*string)) string++;
    if (*string != '(') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Box must start with '('");
    }
    string++;
    while (*string && isspace((unsigned char)*string)) string++;
    if (*string != '(') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Box must contain '(min, max)'");
    }

    /* Parse first vector3 */
    nmo_vector_t minv = {0};
    NMO_RETURN_IF_ERROR(nmo_vector_from_string(&minv, string));

    /* Advance past the first '(...)' */
    int paren = 0;
    const char *p = string;
    for (; *p; ++p) {
        if (*p == '(') paren++;
        else if (*p == ')') {
            paren--;
            if (paren == 0) {
                p++;
                break;
            }
        }
    }
    if (paren != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Unclosed min vector");
    }

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ',') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Box vectors must be separated by ','");
    }
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '(') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Box max vector must start with '('");
    }

    nmo_vector_t maxv = {0};
    NMO_RETURN_IF_ERROR(nmo_vector_from_string(&maxv, p));

    /* Advance past second '(...)' */
    paren = 0;
    const char *q = p;
    for (; *q; ++q) {
        if (*q == '(') paren++;
        else if (*q == ')') {
            paren--;
            if (paren == 0) {
                q++;
                break;
            }
        }
    }
    if (paren != 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Unclosed max vector");
    }
    while (*q && isspace((unsigned char)*q)) q++;
    if (*q != ')') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR, "Box must end with ')'");
    }

    nmo_box_t *b = (nmo_box_t *)value;
    b->min = minv;
    b->max = maxv;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_parse_eulerangles(
    void *value,
    const nmo_type_registry_t *registry,
    const char *string)
{
    (void)registry;
    if (!value || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for euler parse");
    }

    float out[3] = {0};
    nmo_status_t st = parse_float_tuple("EulerAngles", string, out, 3);
    if (st != NMO_OK) return st;

    nmo_eulerangles_t *e = (nmo_eulerangles_t *)value;
    e->x = out[0];
    e->y = out[1];
    e->z = out[2];
    NMO_RETURN_OK();
}

typedef struct nmo_guid_from_string_entry {
    nmo_guid_t guid;
    nmo_value_from_string_fn fn;
} nmo_guid_from_string_entry_t;

static const nmo_guid_from_string_entry_t nmo_guid_from_string_table[] = {
    {NMO_GUID_INIT(CKPGUID_INT8_D1, CKPGUID_INT8_D2), nmo_parse_int8},
    {NMO_GUID_INIT(CKPGUID_INT16_D1, CKPGUID_INT16_D2), nmo_parse_int16},
    {NMO_GUID_INIT(CKPGUID_INT_D1, CKPGUID_INT_D2), nmo_parse_int32},
    {NMO_GUID_INIT(CKPGUID_INT64_D1, CKPGUID_INT64_D2), nmo_parse_int64},
    {NMO_GUID_INIT(CKPGUID_UINT8_D1, CKPGUID_UINT8_D2), nmo_parse_uint8},
    {NMO_GUID_INIT(CKPGUID_UINT16_D1, CKPGUID_UINT16_D2), nmo_parse_uint16},
    {NMO_GUID_INIT(CKPGUID_UINT32_D1, CKPGUID_UINT32_D2), nmo_parse_uint32},
    {NMO_GUID_INIT(CKPGUID_UINT64_D1, CKPGUID_UINT64_D2), nmo_parse_uint64},
    {NMO_GUID_INIT(CKPGUID_ID_D1, CKPGUID_ID_D2), nmo_parse_object_id},
    {NMO_GUID_INIT(CKPGUID_GUID_D1, CKPGUID_GUID_D2), nmo_parse_guid},
    {NMO_GUID_INIT(CKPGUID_POINTER_D1, CKPGUID_POINTER_D2), nmo_parse_pointer},
    {NMO_GUID_INIT(CKPGUID_BOOL_D1, CKPGUID_BOOL_D2), nmo_parse_bool},
    {NMO_GUID_INIT(CKPGUID_FLOAT_D1, CKPGUID_FLOAT_D2), nmo_parse_float},
    {NMO_GUID_INIT(CKPGUID_DOUBLE_D1, CKPGUID_DOUBLE_D2), nmo_parse_double},
    {NMO_GUID_INIT(CKPGUID_STRING_D1, CKPGUID_STRING_D2), nmo_parse_string},
    {NMO_GUID_INIT(CKPGUID_2DVECTOR_D1, CKPGUID_2DVECTOR_D2), nmo_parse_vector2},
    {NMO_GUID_INIT(CKPGUID_VECTOR_D1, CKPGUID_VECTOR_D2), nmo_parse_vector3},
    {NMO_GUID_INIT(CKPGUID_VECTOR4_D1, CKPGUID_VECTOR4_D2), nmo_parse_vector4},
    {NMO_GUID_INIT(CKPGUID_QUATERNION_D1, CKPGUID_QUATERNION_D2), nmo_parse_quaternion},
    {NMO_GUID_INIT(CKPGUID_MATRIX_D1, CKPGUID_MATRIX_D2), nmo_parse_matrix},
    {NMO_GUID_INIT(CKPGUID_COLOR_D1, CKPGUID_COLOR_D2), nmo_parse_color},
    {NMO_GUID_INIT(CKPGUID_RECT_D1, CKPGUID_RECT_D2), nmo_parse_rect},
    {NMO_GUID_INIT(CKPGUID_BOX_D1, CKPGUID_BOX_D2), nmo_parse_box},
    {NMO_GUID_INIT(CKPGUID_EULERANGLES_D1, CKPGUID_EULERANGLES_D2), nmo_parse_eulerangles}
};

static bool nmo_value_from_string_by_guid(
    nmo_guid_t guid,
    void *value,
    const nmo_type_registry_t *registry,
    const char *string,
    nmo_status_t *out_status)
{
    for (size_t i = 0; i < sizeof(nmo_guid_from_string_table) / sizeof(nmo_guid_from_string_table[0]); i++) {
        if (nmo_guid_equals(guid, nmo_guid_from_string_table[i].guid)) {
            *out_status = nmo_guid_from_string_table[i].fn(value, registry, string);
            return true;
        }
    }
    return false;
}

nmo_status_t nmo_type_value_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for type_value_from_string");
    }

    // Dispatch based on type category
    if (type->category & NMO_TYPE_CATEGORY_ENUM) {
        return nmo_enum_from_string(value, type, registry, string);
    }
    if (type->category & NMO_TYPE_CATEGORY_FLAGS) {
        return nmo_flags_from_string(value, type, registry, string);
    }

    /* Custom converter hook (if a type registers one) */
    if (type->vtable && type->vtable->from_string) {
        return type->vtable->from_string(value, type, string, (void *)registry);
    }

    nmo_status_t result = NMO_OK;
    if (nmo_value_from_string_by_guid(type->guid, value, registry, string, &result)) {
        return result;
    }

    /* Scalar fallback for derived types (e.g., ANGLE, PERCENTAGE, KEY, CLASSID) */
    if (type->category & (NMO_TYPE_CATEGORY_SCALAR | NMO_TYPE_CATEGORY_POINTER | NMO_TYPE_CATEGORY_OBJECT_REF)) {
        if (type->size == sizeof(float) && type->alignment == _Alignof(float)) {
            return nmo_float_from_string(value, string);
        }
        if (type->size == sizeof(double) && type->alignment == _Alignof(double)) {
            double parsed = 0.0;
            NMO_RETURN_IF_ERROR(parse_f64(string, &parsed));
            *(double *)value = parsed;
            NMO_RETURN_OK();
        }
        if (type->size == sizeof(int32_t) && type->alignment == _Alignof(int32_t)) {
            return nmo_int_from_string(value, string);
        }
        if (type->size == sizeof(uint32_t) && type->alignment == _Alignof(uint32_t)) {
            return nmo_parse_uint32(value, registry, string);
        }
        if (type->size == sizeof(bool) && type->alignment == _Alignof(bool)) {
            return nmo_bool_from_string(value, string);
        }
        if (type->size == sizeof(nmo_guid_t) && type->alignment == _Alignof(nmo_guid_t)) {
            return nmo_parse_guid(value, registry, string);
        }
        if (type->size == sizeof(void *) && type->alignment == _Alignof(void *)) {
            return nmo_parse_pointer(value, registry, string);
        }
    }

    NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR, "Type-from-string not implemented for this type");
}

/* ============================================================================
 * Builtin Type VTable Helpers
 *
 * These functions are referenced by builtin vtables.
 * The vtable objects themselves are defined in builtin_operations.c.
 * ============================================================================ */

nmo_status_t nmo_builtin_create_zero(void *instance, const nmo_type_descriptor_t *type, void *context)
{
    (void)context;
    if (!instance || !type) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid args for create");
    }
    if (type->size > 0) {
        memset(instance, 0, type->size);
    }
    NMO_RETURN_OK();
}

void nmo_builtin_destroy_noop(void *instance, const nmo_type_descriptor_t *type, void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

nmo_status_t nmo_builtin_copy_memcpy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)arena;
    if (!src || !dst || !type) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid args for copy");
    }
    if (type->size > 0) {
        memcpy(dst, src, type->size);
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_builtin_copy_string(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (!src || !dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid args for string copy");
    }

    const char *s = *(const char *const *)src;
    if (!s) {
        *(char **)dst = NULL;
        NMO_RETURN_OK();
    }

    if (!arena) {
        *(char **)dst = (char *)s;
        NMO_RETURN_OK();
    }

    const char *copy = nmo_arena_strdup(arena, s);
    if (!copy) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to copy string");
    }
    *(const char **)dst = copy;
    NMO_RETURN_OK();
}

static uint32_t nmo_hash_u64_fold(uint64_t v)
{
    uint64_t h = nmo_hash_int64(v);
    return (uint32_t)(h ^ (h >> 32));
}

bool nmo_equals_float_bits(const void *a, const void *b)
{
    uint32_t av = 0;
    uint32_t bv = 0;
    memcpy(&av, a, sizeof(av));
    memcpy(&bv, b, sizeof(bv));
    return av == bv;
}

uint32_t nmo_hash_float_bits(const void *instance)
{
    uint32_t bits = 0;
    memcpy(&bits, instance, sizeof(bits));
    return nmo_hash_int32(bits);
}

bool nmo_equals_double_bits(const void *a, const void *b)
{
    uint64_t av = 0;
    uint64_t bv = 0;
    memcpy(&av, a, sizeof(av));
    memcpy(&bv, b, sizeof(bv));
    return av == bv;
}

uint32_t nmo_hash_double_bits(const void *instance)
{
    uint64_t bits = 0;
    memcpy(&bits, instance, sizeof(bits));
    return nmo_hash_u64_fold(bits);
}

bool nmo_equals_string_value(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    if (sa == sb) {
        return true;
    }
    if (!sa || !sb) {
        return false;
    }
    return strcmp(sa, sb) == 0;
}

uint32_t nmo_hash_string_value(const void *instance)
{
    const char *s = *(const char *const *)instance;
    if (!s) {
        return 0;
    }
    return nmo_murmur3_32(s, strlen(s), 0);
}

#define NMO_DEFINE_EQ_HASH_U32(tag, c_type) \
    bool nmo_vt_equals_##tag(const void *a, const void *b) { \
        return *(const c_type *)a == *(const c_type *)b; \
    } \
    uint32_t nmo_vt_hash_##tag(const void *instance) { \
        return (uint32_t)nmo_hash_int32((uint32_t)(*(const c_type *)instance)); \
    }

#define NMO_DEFINE_EQ_HASH_U64(tag, c_type) \
    bool nmo_vt_equals_##tag(const void *a, const void *b) { \
        return *(const c_type *)a == *(const c_type *)b; \
    } \
    uint32_t nmo_vt_hash_##tag(const void *instance) { \
        return nmo_hash_u64_fold((uint64_t)(*(const c_type *)instance)); \
    }

NMO_DEFINE_EQ_HASH_U32(int32, int32_t)
NMO_DEFINE_EQ_HASH_U32(uint32, uint32_t)
NMO_DEFINE_EQ_HASH_U32(int8, int8_t)
NMO_DEFINE_EQ_HASH_U32(uint8, uint8_t)
NMO_DEFINE_EQ_HASH_U32(int16, int16_t)
NMO_DEFINE_EQ_HASH_U32(uint16, uint16_t)
NMO_DEFINE_EQ_HASH_U64(int64, int64_t)
NMO_DEFINE_EQ_HASH_U64(uint64, uint64_t)

bool nmo_equals_bool(const void *a, const void *b)
{
    return *(const bool *)a == *(const bool *)b;
}

uint32_t nmo_hash_bool(const void *instance)
{
    return nmo_hash_int32(*(const bool *)instance ? 1u : 0u);
}

bool nmo_equals_pointer(const void *a, const void *b)
{
    return *(const void *const *)a == *(const void *const *)b;
}

uint32_t nmo_hash_pointer(const void *instance)
{
    uintptr_t v = (uintptr_t)(*(const void *const *)instance);
    return nmo_hash_u64_fold((uint64_t)v);
}

bool nmo_equals_guid(const void *a, const void *b)
{
    return nmo_guid_equals(*(const nmo_guid_t *)a, *(const nmo_guid_t *)b);
}

uint32_t nmo_hash_guid(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_guid_t), 0);
}

bool nmo_equals_object_id(const void *a, const void *b)
{
    return *(const nmo_object_id_t *)a == *(const nmo_object_id_t *)b;
}

uint32_t nmo_hash_object_id(const void *instance)
{
    return nmo_hash_int32((uint32_t)(*(const nmo_object_id_t *)instance));
}

bool nmo_equals_bytes_vector2(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_vector2_t)) == 0;
}

uint32_t nmo_hash_bytes_vector2(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_vector2_t), 0);
}

bool nmo_equals_bytes_vector3(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_vector_t)) == 0;
}

uint32_t nmo_hash_bytes_vector3(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_vector_t), 0);
}

bool nmo_equals_bytes_vector4(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_vector4_t)) == 0;
}

uint32_t nmo_hash_bytes_vector4(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_vector4_t), 0);
}

bool nmo_equals_bytes_quaternion(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_quaternion_t)) == 0;
}

uint32_t nmo_hash_bytes_quaternion(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_quaternion_t), 0);
}

bool nmo_equals_bytes_matrix(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_matrix_t)) == 0;
}

uint32_t nmo_hash_bytes_matrix(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_matrix_t), 0);
}

bool nmo_equals_bytes_color(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_color_t)) == 0;
}

uint32_t nmo_hash_bytes_color(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_color_t), 0);
}

bool nmo_equals_bytes_rect(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_rect_t)) == 0;
}

uint32_t nmo_hash_bytes_rect(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_rect_t), 0);
}

bool nmo_equals_bytes_eulerangles(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_eulerangles_t)) == 0;
}

uint32_t nmo_hash_bytes_eulerangles(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_eulerangles_t), 0);
}

bool nmo_equals_bytes_box(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(nmo_box_t)) == 0;
}

uint32_t nmo_hash_bytes_box(const void *instance)
{
    return nmo_murmur3_32(instance, sizeof(nmo_box_t), 0);
}

#define NMO_DEFINE_VT_TO_STRING(name, value_to_string_fn) \
    nmo_status_t nmo_vt_to_string_##name( \
        const void *value, const nmo_type_descriptor_t *type, \
        char *buffer, size_t buffer_size, void *context) \
    { \
        (void)type; \
        (void)context; \
        return (value_to_string_fn)(value, buffer, buffer_size); \
    }

#define NMO_DEFINE_VT_FROM_STRING(name, parse_fn) \
    nmo_status_t nmo_vt_from_string_##name( \
        void *value, const nmo_type_descriptor_t *type, \
        const char *string, void *context) \
    { \
        (void)type; \
        return (parse_fn)(value, (const nmo_type_registry_t *)context, string); \
    }

NMO_DEFINE_VT_TO_STRING(int32, nmo_int_value_to_string)
NMO_DEFINE_VT_FROM_STRING(int32, nmo_parse_int32)

NMO_DEFINE_VT_TO_STRING(uint32, nmo_uint32_value_to_string)
NMO_DEFINE_VT_FROM_STRING(uint32, nmo_parse_uint32)

NMO_DEFINE_VT_TO_STRING(int8, nmo_int8_value_to_string)
NMO_DEFINE_VT_FROM_STRING(int8, nmo_parse_int8)

NMO_DEFINE_VT_TO_STRING(uint8, nmo_uint8_value_to_string)
NMO_DEFINE_VT_FROM_STRING(uint8, nmo_parse_uint8)

NMO_DEFINE_VT_TO_STRING(int16, nmo_int16_value_to_string)
NMO_DEFINE_VT_FROM_STRING(int16, nmo_parse_int16)

NMO_DEFINE_VT_TO_STRING(uint16, nmo_uint16_value_to_string)
NMO_DEFINE_VT_FROM_STRING(uint16, nmo_parse_uint16)

NMO_DEFINE_VT_TO_STRING(int64, nmo_int64_value_to_string)
NMO_DEFINE_VT_FROM_STRING(int64, nmo_parse_int64)

NMO_DEFINE_VT_TO_STRING(uint64, nmo_uint64_value_to_string)
NMO_DEFINE_VT_FROM_STRING(uint64, nmo_parse_uint64)

NMO_DEFINE_VT_TO_STRING(float, nmo_float_value_to_string)
NMO_DEFINE_VT_FROM_STRING(float, nmo_parse_float)

NMO_DEFINE_VT_TO_STRING(double, nmo_double_value_to_string)
NMO_DEFINE_VT_FROM_STRING(double, nmo_parse_double)

NMO_DEFINE_VT_TO_STRING(bool, nmo_bool_value_to_string)
NMO_DEFINE_VT_FROM_STRING(bool, nmo_parse_bool)

NMO_DEFINE_VT_TO_STRING(string, nmo_string_value_to_string)
NMO_DEFINE_VT_FROM_STRING(string, nmo_parse_string)

NMO_DEFINE_VT_TO_STRING(pointer, nmo_pointer_value_to_string)
NMO_DEFINE_VT_FROM_STRING(pointer, nmo_parse_pointer)

NMO_DEFINE_VT_TO_STRING(guid, nmo_guid_value_to_string)
NMO_DEFINE_VT_FROM_STRING(guid, nmo_parse_guid)

NMO_DEFINE_VT_TO_STRING(object_id, nmo_object_id_value_to_string)
NMO_DEFINE_VT_FROM_STRING(object_id, nmo_parse_object_id)

NMO_DEFINE_VT_TO_STRING(vector2, nmo_vector2_value_to_string)
NMO_DEFINE_VT_FROM_STRING(vector2, nmo_parse_vector2)

NMO_DEFINE_VT_TO_STRING(vector3, nmo_vector3_value_to_string)
NMO_DEFINE_VT_FROM_STRING(vector3, nmo_parse_vector3)

NMO_DEFINE_VT_TO_STRING(vector4, nmo_vector4_value_to_string)
NMO_DEFINE_VT_FROM_STRING(vector4, nmo_parse_vector4)

NMO_DEFINE_VT_TO_STRING(quaternion, nmo_quaternion_value_to_string)
NMO_DEFINE_VT_FROM_STRING(quaternion, nmo_parse_quaternion)

NMO_DEFINE_VT_TO_STRING(matrix, nmo_matrix_value_to_string)
NMO_DEFINE_VT_FROM_STRING(matrix, nmo_parse_matrix)

NMO_DEFINE_VT_TO_STRING(color, nmo_color_value_to_string)
NMO_DEFINE_VT_FROM_STRING(color, nmo_parse_color)

NMO_DEFINE_VT_TO_STRING(rect, nmo_rect_value_to_string)
NMO_DEFINE_VT_FROM_STRING(rect, nmo_parse_rect)

NMO_DEFINE_VT_TO_STRING(eulerangles, nmo_eulerangles_value_to_string)
NMO_DEFINE_VT_FROM_STRING(eulerangles, nmo_parse_eulerangles)

NMO_DEFINE_VT_TO_STRING(box, nmo_box_value_to_string)
NMO_DEFINE_VT_FROM_STRING(box, nmo_parse_box)
