/**
 * @file type_string_converters.c
 * @brief Implementation of type-to-string converters (Phase 6.4.2)
 *
 * Provides concrete implementations for all built-in type string converters.
 *
 * Reference: CKParameterManager.cpp:1345-1435
 */

#include "type/type_string.h"
#include "type/type_system.h"
#include "type/builtin_operations.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

/* ============================================================================
 * Float Converters
 * ============================================================================ */

nmo_result_t nmo_float_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 16) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for float_to_string"));
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

    return nmo_result_ok();
}

nmo_result_t nmo_float_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for float_from_string"));
    }

    // Handle special cases
    if (strcmp(string, "NaN") == 0) {
        *(float*)value = NAN;
        return nmo_result_ok();
    }
    if (strcmp(string, "Infinity") == 0 || strcmp(string, "+Infinity") == 0) {
        *(float*)value = INFINITY;
        return nmo_result_ok();
    }
    if (strcmp(string, "-Infinity") == 0) {
        *(float*)value = -INFINITY;
        return nmo_result_ok();
    }

    char *endptr;
    errno = 0;
    float result = strtof(string, &endptr);

    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace(*endptr))) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR, "Invalid float format"));
    }

    *(float*)value = result;
    return nmo_result_ok();
}

/* ============================================================================
 * Int Converters
 * ============================================================================ */

nmo_result_t nmo_int_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size,
    bool use_hex)
{
    if (!value || !buffer || buffer_size < 16) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for int_to_string"));
    }

    int32_t i = *(const int32_t*)value;

    if (use_hex) {
        snprintf(buffer, buffer_size, "0x%X", (unsigned int)i);
    } else {
        snprintf(buffer, buffer_size, "%d", i);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_int_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for int_from_string"));
    }

    char *endptr;
    errno = 0;
    
    // Detect base (hex if starts with 0x/0X, else decimal)
    int base = 0;  // auto-detect
    long result = strtol(string, &endptr, base);

    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace(*endptr))) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR, "Invalid int format"));
    }

    *(int32_t*)value = (int32_t)result;
    return nmo_result_ok();
}

/* ============================================================================
 * Bool Converters
 * ============================================================================ */

nmo_result_t nmo_bool_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 6) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for bool_to_string"));
    }

    bool b = *(const bool*)value;
    snprintf(buffer, buffer_size, b ? "true" : "false");

    return nmo_result_ok();
}

nmo_result_t nmo_bool_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for bool_from_string"));
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
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR, "Invalid bool format (expected true/false/1/0)"));
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Vector Converters
 * ============================================================================ */

nmo_result_t nmo_vector_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 32) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for vector_to_string"));
    }

    const float *v = (const float*)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g, %.6g)", v[0], v[1], v[2]);

    return nmo_result_ok();
}

nmo_result_t nmo_vector_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for vector_from_string"));
    }

    float *v = (float*)value;
    
    // Skip whitespace and opening parenthesis
    while (*string && isspace(*string)) string++;
    if (*string != '(') {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR, "Vector must start with '('"));
    }
    string++;

    // Parse three float values separated by commas
    char *endptr;
    for (int i = 0; i < 3; i++) {
        while (*string && isspace(*string)) string++;
        
        errno = 0;
        v[i] = strtof(string, &endptr);
        
        if (errno != 0 || endptr == string) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
                NMO_SEVERITY_ERROR, "Invalid vector component"));
        }
        
        string = endptr;
        while (*string && isspace(*string)) string++;
        
        if (i < 2) {
            if (*string != ',') {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
                    NMO_SEVERITY_ERROR, "Vector components must be separated by ','"));
            }
            string++;
        }
    }

    // Expect closing parenthesis
    while (*string && isspace(*string)) string++;
    if (*string != ')') {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR, "Vector must end with ')'"));
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Quaternion Converters
 * ============================================================================ */

nmo_result_t nmo_quaternion_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 48) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for quaternion_to_string"));
    }

    const float *q = (const float*)value;
    snprintf(buffer, buffer_size, "(%.6g, %.6g, %.6g, %.6g)", 
             q[0], q[1], q[2], q[3]);

    return nmo_result_ok();
}

nmo_result_t nmo_quaternion_from_string(
    void *value,
    const char *string)
{
    if (!value || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for quaternion_from_string"));
    }

    float *q = (float*)value;
    
    // Skip whitespace and opening parenthesis
    while (*string && isspace(*string)) string++;
    if (*string != '(') {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR, "Quaternion must start with '('"));
    }
    string++;

    // Parse four float values separated by commas
    char *endptr;
    for (int i = 0; i < 4; i++) {
        while (*string && isspace(*string)) string++;
        
        errno = 0;
        q[i] = strtof(string, &endptr);
        
        if (errno != 0 || endptr == string) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
                NMO_SEVERITY_ERROR, "Invalid quaternion component"));
        }
        
        string = endptr;
        while (*string && isspace(*string)) string++;
        
        if (i < 3) {
            if (*string != ',') {
                return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
                    NMO_SEVERITY_ERROR, "Quaternion components must be separated by ','"));
            }
            string++;
        }
    }

    // Expect closing parenthesis
    while (*string && isspace(*string)) string++;
    if (*string != ')') {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR, "Quaternion must end with ')'"));
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Enum/Flags Converters (require type metadata)
 * ============================================================================ */

nmo_result_t nmo_enum_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    bool use_name)
{
    if (!value || !type || !buffer || buffer_size < 16) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for enum_to_string"));
    }

    if (!(type->category & NMO_TYPE_CATEGORY_ENUM)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Type is not an enum"));
    }

    int32_t enum_value = *(const int32_t*)value;

    // If name not requested or no registry, output numeric value
    if (!use_name || !registry || type->specialized_index == (uint32_t)-1) {
        snprintf(buffer, buffer_size, "%d", enum_value);
        return nmo_result_ok();
    }

    // Access enum metadata from registry
    if (type->specialized_index >= registry->metadata.count) {
        snprintf(buffer, buffer_size, "%d", enum_value);
        return nmo_result_ok();
    }

    const nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t**)nmo_arena_array_get((nmo_arena_array_t*)&registry->metadata, type->specialized_index);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_ENUM) {
        snprintf(buffer, buffer_size, "%d", enum_value);
        return nmo_result_ok();
    }

    // Search for matching enum value
    for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
        if (metadata->enum_meta.values[i].value == enum_value) {
            snprintf(buffer, buffer_size, "%s", metadata->enum_meta.values[i].name);
            return nmo_result_ok();
        }
    }

    // No name found, output numeric value
    snprintf(buffer, buffer_size, "%d", enum_value);
    return nmo_result_ok();
}

nmo_result_t nmo_enum_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for enum_from_string"));
    }

    if (!(type->category & NMO_TYPE_CATEGORY_ENUM)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Type is not an enum"));
    }

    // Try to match name in enum metadata
    if (registry && type->specialized_index != (uint32_t)-1 &&
        type->specialized_index < registry->metadata.count) {
        const nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t**)nmo_arena_array_get((nmo_arena_array_t*)&registry->metadata, type->specialized_index);
        if (metadata && metadata->metadata_type == NMO_METADATA_TYPE_ENUM) {
            for (size_t i = 0; i < metadata->enum_meta.value_count; i++) {
                if (strcmp(metadata->enum_meta.values[i].name, string) == 0) {
                    *(int32_t*)value = (int32_t)metadata->enum_meta.values[i].value;
                    return nmo_result_ok();
                }
            }
        }
    }

    // Try to parse as integer
    char *endptr;
    errno = 0;
    long result = strtol(string, &endptr, 0);

    if (errno != 0 || endptr == string || (*endptr != '\0' && !isspace(*endptr))) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
            NMO_SEVERITY_ERROR, "Invalid enum value"));
    }

    *(int32_t*)value = (int32_t)result;
    return nmo_result_ok();
}

/* ============================================================================
 * Flags Converters
 * ============================================================================ */

nmo_result_t nmo_flags_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    bool use_names)
{
    if (!value || !type || !buffer || buffer_size < 16) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for flags_to_string"));
    }

    if (!(type->category & NMO_TYPE_CATEGORY_FLAGS)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Type is not flags"));
    }

    uint32_t flags_value = *(const uint32_t*)value;

    // If names not requested or no registry, output hex
    if (!use_names || !registry || type->specialized_index == (uint32_t)-1) {
        snprintf(buffer, buffer_size, "0x%X", flags_value);
        return nmo_result_ok();
    }

    // Access flags metadata from registry
    if (type->specialized_index >= registry->metadata.count) {
        snprintf(buffer, buffer_size, "0x%X", flags_value);
        return nmo_result_ok();
    }

    const nmo_specialized_metadata_t *metadata = *(nmo_specialized_metadata_t**)nmo_arena_array_get((nmo_arena_array_t*)&registry->metadata, type->specialized_index);
    if (!metadata || metadata->metadata_type != NMO_METADATA_TYPE_FLAGS) {
        snprintf(buffer, buffer_size, "0x%X", flags_value);
        return nmo_result_ok();
    }

    // Build name1|name2 format
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

    // If no flags matched, output hex
    if (first) {
        snprintf(buffer, buffer_size, "0x%X", flags_value);
    } else {
        buffer[offset] = '\0';
    }

    return nmo_result_ok();
}

nmo_result_t nmo_flags_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for flags_from_string"));
    }

    if (!(type->category & NMO_TYPE_CATEGORY_FLAGS)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Type is not flags"));
    }

    // Try hex format first
    if (strncmp(string, "0x", 2) == 0 || strncmp(string, "0X", 2) == 0) {
        char *endptr;
        unsigned long result = strtoul(string, &endptr, 16);
        if (errno == 0 && *endptr == '\0') {
            *(uint32_t*)value = (uint32_t)result;
            return nmo_result_ok();
        }
    }

    // Try numeric format
    char *endptr;
    errno = 0;
    unsigned long result = strtoul(string, &endptr, 0);
    if (errno == 0 && *endptr == '\0') {
        *(uint32_t*)value = (uint32_t)result;
        return nmo_result_ok();
    }

    // Parse name1|name2 format from metadata
    if (registry && type->specialized_index != (uint32_t)-1 &&
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
                    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
                        NMO_SEVERITY_ERROR, "Unknown flag name"));
                }
                
                // Move to next
                start = (*end == '|') ? end + 1 : end;
            }
            
            *(uint32_t*)value = flags_result;
            return nmo_result_ok();
        }
    }

    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
        NMO_SEVERITY_ERROR, "Invalid flags format"));
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

nmo_result_t nmo_string_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !buffer || buffer_size < 3) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for string_to_string"));
    }

    const char *str = *(const char**)value;
    if (!str) {
        snprintf(buffer, buffer_size, "\"\"");
        return nmo_result_ok();
    }

    nmo_string_escape(str, buffer, buffer_size);
    return nmo_result_ok();
}

nmo_result_t nmo_string_from_string(
    void *value,
    const char *string,
    nmo_arena_t *arena)
{
    if (!value || !string || !arena) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for string_from_string"));
    }

    // Estimate unescaped length (worst case: same as input)
    size_t max_len = strlen(string) + 1;
    char *temp = (char*)nmo_arena_alloc(arena, max_len, 1);
    if (!temp) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate string buffer"));
    }

    nmo_string_unescape(string, temp, max_len);
    
    // Allocate exact size needed
    size_t actual_len = strlen(temp) + 1;
    char *result = (char*)nmo_arena_alloc(arena, actual_len, 1);
    if (!result) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate final string"));
    }

    memcpy(result, temp, actual_len);
    *(char**)value = result;

    return nmo_result_ok();
}

/* ============================================================================
 * Object ID Converters
 * ============================================================================ */

nmo_result_t nmo_object_id_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size,
    struct nmo_session *session)
{
    if (!value || !buffer || buffer_size < 16) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for object_id_to_string"));
    }

    nmo_id_t id = *(const nmo_id_t*)value;
    
    // TODO: Look up object name from session if available
    (void)session;  // Unused for now
    
    snprintf(buffer, buffer_size, "#%u", id);
    return nmo_result_ok();
}

nmo_result_t nmo_object_id_from_string(
    void *value,
    const char *string,
    struct nmo_session *session)
{
    if (!value || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for object_id_from_string"));
    }

    // TODO: Look up object ID by name from session if available
    (void)session;  // Unused for now

    // Parse #id format
    if (*string == '#') {
        string++;
        char *endptr;
        unsigned long id = strtoul(string, &endptr, 10);
        if (errno == 0 && *endptr == '\0') {
            *(nmo_id_t*)value = (nmo_id_t)id;
            return nmo_result_ok();
        }
    }

    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_FORMAT,
        NMO_SEVERITY_ERROR, "Invalid object ID format (expected #id)"));
}

/* ============================================================================
 * General-Purpose Dispatcher
 * ============================================================================ */

nmo_result_t nmo_type_value_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size)
{
    if (!value || !type || !buffer) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for type_value_to_string"));
    }

    // Dispatch based on type category
    if (type->category & NMO_TYPE_CATEGORY_ENUM) {
        return nmo_enum_to_string(value, type, registry, buffer, buffer_size, true);
    }
    if (type->category & NMO_TYPE_CATEGORY_FLAGS) {
        return nmo_flags_to_string(value, type, registry, buffer, buffer_size, true);
    }

    // Dispatch by GUID for built-in types (using NMO_TYPE_GUID_* constants)
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_FLOAT)) {
        return nmo_float_to_string(value, buffer, buffer_size);
    }
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_INT)) {
        return nmo_int_to_string(value, buffer, buffer_size, false);
    }
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_BOOL)) {
        return nmo_bool_to_string(value, buffer, buffer_size);
    }
    
    // Vector types (Vector2 = 2 floats, Vector3 = 3 floats, Vector4/Quaternion = 4 floats)
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_VECTOR2) ||
        nmo_guid_equals(type->guid, NMO_TYPE_GUID_VECTOR3)) {
        return nmo_vector_to_string(value, buffer, buffer_size);
    }
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_VECTOR4)) {
        return nmo_quaternion_to_string(value, buffer, buffer_size);
    }

    // Fallback: try by size/alignment for unnamed types
    if (type->size == sizeof(float) && type->alignment == _Alignof(float)) {
        return nmo_float_to_string(value, buffer, buffer_size);
    }
    if (type->size == sizeof(int32_t) && type->alignment == _Alignof(int32_t)) {
        return nmo_int_to_string(value, buffer, buffer_size, false);
    }
    if (type->size == sizeof(bool)) {
        return nmo_bool_to_string(value, buffer, buffer_size);
    }

    // Default: hex dump
    snprintf(buffer, buffer_size, "<binary %u bytes>", type->size);
    return nmo_result_ok();
}

nmo_result_t nmo_type_value_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string)
{
    if (!value || !type || !string) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments for type_value_from_string"));
    }

    // Dispatch based on type category
    if (type->category & NMO_TYPE_CATEGORY_ENUM) {
        return nmo_enum_from_string(value, type, registry, string);
    }
    if (type->category & NMO_TYPE_CATEGORY_FLAGS) {
        return nmo_flags_from_string(value, type, registry, string);
    }

    // Dispatch by GUID for built-in types (using NMO_TYPE_GUID_* constants)
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_FLOAT)) {
        return nmo_float_from_string(value, string);
    }
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_INT)) {
        return nmo_int_from_string(value, string);
    }
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_BOOL)) {
        return nmo_bool_from_string(value, string);
    }
    
    // Vector types (Vector2 = 2 floats, Vector3 = 3 floats, Vector4/Quaternion = 4 floats)
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_VECTOR2) ||
        nmo_guid_equals(type->guid, NMO_TYPE_GUID_VECTOR3)) {
        return nmo_vector_from_string(value, string);
    }
    if (nmo_guid_equals(type->guid, NMO_TYPE_GUID_VECTOR4)) {
        return nmo_quaternion_from_string(value, string);
    }

    // Fallback: try by size for unnamed types
    if (type->size == sizeof(float)) {
        return nmo_float_from_string(value, string);
    }
    if (type->size == sizeof(int32_t)) {
        return nmo_int_from_string(value, string);
    }
    if (type->size == sizeof(bool)) {
        return nmo_bool_from_string(value, string);
    }

    return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOT_IMPLEMENTED,
        NMO_SEVERITY_ERROR, "Type-from-string not implemented for this type"));
}
