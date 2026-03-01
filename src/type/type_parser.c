/**
 * @file type_parser.c
 * @brief Type name string parser (Phase 6.2, Task 6.2.2)
 *
 * Parses type name strings like "int", "float[10]", "MyStruct*" into
 * structured type descriptors.
 */

#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include "core/nmo_utils.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>

/* ============================================================================
 * Builtin Type Name Mapping
 * ============================================================================ */

typedef struct builtin_type_name_t {
    const char *name;
    nmo_guid_t guid;
} builtin_type_name_t;

static const builtin_type_name_t BUILTIN_TYPES[] = {
    /* Basic types */
    {"none", CKPGUID_NONE_INIT},
    {"voidbuf", CKPGUID_VOIDBUF_INIT},
    {"int", CKPGUID_INT_INIT},
    {"int32", CKPGUID_INT_INIT},
    {"float", CKPGUID_FLOAT_INIT},
    {"angle", CKPGUID_ANGLE_INIT},
    {"percentage", CKPGUID_PERCENTAGE_INIT},
    {"bool", CKPGUID_BOOL_INIT},
    {"string", CKPGUID_STRING_INIT},
    {"double", CKPGUID_DOUBLE_INIT},
    {"uint", CKPGUID_UINT32_INIT},
    {"classid", CKPGUID_CLASSID_INIT},
    {"int8", CKPGUID_INT8_INIT},
    {"uint8", CKPGUID_UINT8_INIT},
    {"int16", CKPGUID_INT16_INIT},
    {"uint16", CKPGUID_UINT16_INIT},
    {"uint32", CKPGUID_UINT32_INIT},
    {"int64", CKPGUID_INT64_INIT},
    {"uint64", CKPGUID_UINT64_INIT},
    {"pointer", CKPGUID_POINTER_INIT},
    {"guid", CKPGUID_GUID_INIT},
    {"object_id", CKPGUID_ID_INIT},
    
    /* Virtools common types */
    {"vector2", CKPGUID_2DVECTOR_INIT},
    {"vector3", CKPGUID_VECTOR_INIT},
    {"vector4", CKPGUID_VECTOR4_INIT},
    {"quaternion", CKPGUID_QUATERNION_INIT},
    {"euler_angles", CKPGUID_EULERANGLES_INIT},
    {"matrix", CKPGUID_MATRIX_INIT},
    {"color", CKPGUID_COLOR_INIT},
    {"rect", CKPGUID_RECT_INIT},
    {"box", CKPGUID_BOX_INIT},
    {"chunk", CKPGUID_STATECHUNK_INIT},
    {NULL, NMO_GUID_INIT(0, 0)},
};

/* ============================================================================
 * String Utilities
 * ============================================================================ */

/**
 * @brief Trim whitespace from string
 */
static void trim_whitespace(const char **str, size_t *len) {
    const char *s = *str;
    const char *end = s + *len;
    
    /* Trim leading whitespace */
    while (s < end && isspace((unsigned char)*s)) {
        s++;
    }
    
    /* Trim trailing whitespace */
    while (end > s && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    
    *str = s;
    *len = (size_t)(end - s);
}

/**
 * @brief Check if string matches (case-sensitive)
 */
static bool str_equals_cs(const char *a, size_t a_len, const char *b) {
    size_t b_len = strlen(b);
    if (a_len != b_len) {
        return false;
    }
    return (memcmp(a, b, a_len) == 0);
}

/**
 * @brief Skip whitespace starting at position
 */
static size_t skip_whitespace(const char *str, size_t pos, size_t len) {
    while (pos < len && isspace((unsigned char)str[pos])) {
        pos++;
    }
    return pos;
}

/**
 * @brief Check if span contains any whitespace
 */
static bool span_has_whitespace(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char)str[i])) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if normalized string looks like a GUID literal
 */
static bool is_guid_literal_candidate(const char *str, size_t len) {
    if (len != 16 && len != 17 && len != 19) {
        return false;
    }

    if (len == 16) {
        for (size_t i = 0; i < len; i++) {
            if (!isxdigit((unsigned char)str[i])) {
                return false;
            }
        }
        return true;
    }

    if (len == 17) {
        if (str[8] != '-') {
            return false;
        }
        for (size_t i = 0; i < len; i++) {
            if (i == 8) {
                continue;
            }
            if (!isxdigit((unsigned char)str[i])) {
                return false;
            }
        }
        return true;
    }

    if (len == 19) {
        if (str[0] != '{' || str[18] != '}' || str[9] != '-') {
            return false;
        }
        for (size_t i = 1; i < 18; i++) {
            if (i == 9) {
                continue;
            }
            if (!isxdigit((unsigned char)str[i])) {
                return false;
            }
        }
        return true;
    }

    return false;
}

/* ============================================================================
 * Type Name Parsing
 * ============================================================================ */

/**
 * @brief Lookup builtin type by name (case-sensitive)
 */
static bool lookup_builtin_type(const char *name, size_t name_len, nmo_guid_t *out_guid) {
    if (str_equals_cs(name, name_len, "uint32_t")) {
        *out_guid = CKPGUID_UINT32;
        return true;
    }

    if (str_equals_cs(name, name_len, "size_t")) {
        if (sizeof(size_t) == 8) {
            *out_guid = CKPGUID_UINT64;
        } else {
            *out_guid = CKPGUID_UINT32;
        }
        return true;
    }

    for (size_t i = 0; BUILTIN_TYPES[i].name != NULL; i++) {
        if (str_equals_cs(name, name_len, BUILTIN_TYPES[i].name)) {
            *out_guid = BUILTIN_TYPES[i].guid;
            return true;
        }
    }
    return false;
}

nmo_status_t nmo_type_registry_parse_type_name(
    const nmo_type_registry_t *type_registry,
    const char *type_name,
    nmo_type_parse_result_t *result
) {
    if (!type_registry || !type_name || !result) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid arguments to parse type name");
    }
    
    /* Initialize result */
    memset(result, 0, sizeof(*result));
    result->type_name = type_name;
    
    /* Get string length and trim */
    const char *str = type_name;
    size_t len = strlen(type_name);
    trim_whitespace(&str, &len);
    
    if (len == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Empty type name: '%s'", type_name);
    }

    size_t start = skip_whitespace(str, 0, len);
    if (start >= len) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Empty type name: '%s'", type_name);
    }

    size_t suffix_start = len;
    for (size_t i = start; i < len; i++) {
        if (str[i] == '*' || str[i] == '[') {
            suffix_start = i;
            break;
        }
    }

    const char *base_name = str + start;
    size_t base_name_len = suffix_start - start;
    trim_whitespace(&base_name, &base_name_len);

    if (base_name_len == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Missing base type name in '%s'", type_name);
    }

    /* Try GUID literal first (whitespace-tolerant) */
    char guid_buf[64];
    size_t guid_len = 0;
    for (size_t i = 0; i < base_name_len; i++) {
        char c = base_name[i];
        if (!isspace((unsigned char)c)) {
            if (guid_len + 1 >= sizeof(guid_buf)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                        "GUID literal too long");
            }
            guid_buf[guid_len++] = c;
        }
    }
    guid_buf[guid_len] = '\0';

    if (is_guid_literal_candidate(guid_buf, guid_len)) {
        nmo_guid_t parsed_guid = nmo_guid_parse(guid_buf);
        if (nmo_guid_is_null(parsed_guid)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Invalid GUID literal");
        }

        const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(
            type_registry, parsed_guid);
        if (!type_desc) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "Type GUID not found in registry");
        }
        result->base_type_guid = parsed_guid;
    } else {
        if (span_has_whitespace(base_name, base_name_len)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                    "Whitespace not allowed inside type name");
        }

        /* Lookup builtin type */
        if (lookup_builtin_type(base_name, base_name_len, &result->base_type_guid)) {
            /* Continue to parse suffixes */
        } else {
            /* Lookup registered type by name */
            char name_buf[256];
            if (base_name_len >= sizeof(name_buf)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                        "Type name too long");
            }

            memcpy(name_buf, base_name, base_name_len);
            name_buf[base_name_len] = '\0';

            const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_name(
                type_registry, name_buf);

            if (type_desc) {
                result->base_type_guid = type_desc->guid;
            } else {
                NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                        "Type '%s' not found in registry", name_buf);
            }
        }
    }

    /* Parse suffixes (single-pass, whitespace-tolerant) */
    uint32_t pointer_depth = 0;
    uint32_t array_count = 0;
    bool has_array = false;
    size_t pos = suffix_start;

    while (pos < len) {
        char c = str[pos];
        if (isspace((unsigned char)c)) {
            pos++;
            continue;
        }

        if (c == '*') {
            pointer_depth++;
            pos++;
            continue;
        }

        if (c == '[') {
            if (has_array) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                        "Multiple array suffixes not supported");
            }

            pos++;
            while (pos < len && isspace((unsigned char)str[pos])) {
                pos++;
            }

            if (pos >= len || !isdigit((unsigned char)str[pos])) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                        "Malformed array syntax in type name");
            }

            uint64_t count_value = 0;
            while (pos < len && isdigit((unsigned char)str[pos])) {
                uint64_t digit = (uint64_t)(str[pos] - '0');
                if (count_value > (UINT32_MAX - digit) / 10) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                            "Array count overflow");
                }
                count_value = (count_value * 10) + digit;
                pos++;
            }

            while (pos < len && isspace((unsigned char)str[pos])) {
                pos++;
            }
            if (pos >= len || str[pos] != ']') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                        "Malformed array syntax in type name");
            }
            pos++;

            if (count_value == 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                        "Array count must be greater than zero");
            }

            array_count = (uint32_t)count_value;
            has_array = true;
            continue;
        }

        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Unexpected token in type suffix");
    }

    result->is_array = has_array;
    result->array_count = array_count;
    result->is_pointer = (pointer_depth > 0);
    result->pointer_depth = pointer_depth;
    NMO_RETURN_OK();
}

/* ============================================================================
 * GUID Generation
 * ============================================================================ */

nmo_guid_t nmo_type_generate_guid(const char *type_name) {
    if (!type_name) {
        return NMO_GUID_NULL;
    }
    
    /* Simple hash-based GUID generation */
    /* Using FNV-1a hash algorithm */
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;
    
    uint64_t hash1 = FNV_OFFSET;
    uint64_t hash2 = FNV_OFFSET;
    
    const char *p = type_name;
    size_t i = 0;
    
    while (*p) {
        unsigned char c = (unsigned char)tolower(*p);
        
        if (i % 2 == 0) {
            hash1 ^= c;
            hash1 *= FNV_PRIME;
        } else {
            hash2 ^= c;
            hash2 *= FNV_PRIME;
        }
        
        p++;
        i++;
    }
    
    /* Use high 32 bits from each hash */
    nmo_guid_t guid = {
        (uint32_t)(hash1 >> 32) | 0x80000000,  /* Set bit 31 to mark as auto-generated */
        (uint32_t)(hash2 >> 32)
    };
    
    return guid;
}

/* ============================================================================
 * Enum/Flags String Parsing (Phase 6.2, Task 6.2.1)
 * ============================================================================ */

/**
 * @brief Parse integer value from string (supports decimal and hex)
 */
static bool parse_integer_value(const char *str, size_t len, int64_t *out_value) {
    if (len == 0) {
        return false;
    }
    
    /* Check for hex prefix */
    bool is_hex = false;
    size_t start_idx = 0;
    
    if (len >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        is_hex = true;
        start_idx = 2;
    }
    
    /* Parse value */
    int64_t value = 0;
    bool has_digits = false;
    bool is_negative = false;
    
    for (size_t i = start_idx; i < len; i++) {
        char c = str[i];
        
        /* Handle negative sign (only at start for decimal) */
        if (c == '-' && i == start_idx && !is_hex) {
            is_negative = true;
            continue;
        }
        
        int digit_value;
        
        if (is_hex) {
            if (c >= '0' && c <= '9') {
                digit_value = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit_value = 10 + (c - 'a');
            } else if (c >= 'A' && c <= 'F') {
                digit_value = 10 + (c - 'A');
            } else {
                return false;  /* Invalid hex digit */
            }
            
            value = (value * 16) + digit_value;
        } else {
            if (c >= '0' && c <= '9') {
                digit_value = c - '0';
            } else {
                return false;  /* Invalid decimal digit */
            }
            
            value = (value * 10) + digit_value;
        }
        
        has_digits = true;
    }
    
    if (!has_digits) {
        return false;
    }
    
    *out_value = is_negative ? -value : value;
    return true;
}

/**
 * @brief Check if name is valid identifier
 */
static bool is_valid_identifier(const char *str, size_t len) {
    if (len == 0) {
        return false;
    }
    
    /* First character must be letter or underscore */
    char first = str[0];
    if (!((first >= 'a' && first <= 'z') || 
          (first >= 'A' && first <= 'Z') || 
          first == '_')) {
        return false;
    }
    
    /* Rest can be letters, digits, or underscores */
    for (size_t i = 1; i < len; i++) {
        char c = str[i];
        if (!((c >= 'a' && c <= 'z') || 
              (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || 
              c == '_')) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Parse enum/flags string: "NAME1=VALUE1,NAME2=VALUE2,..."
 */
nmo_status_t nmo_parse_enum_flags_string(
    const char *input_str,
    nmo_enum_value_def_t **out_values,
    size_t *out_count,
    nmo_arena_t *arena
) {
    if (!input_str || !out_values || !out_count || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid arguments to enum/flags parser");
    }
    
    /* Trim input */
    const char *str = input_str;
    size_t len = strlen(str);
    trim_whitespace(&str, &len);
    
    if (len == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                "Empty enum/flags definition string");
    }
    
    /* First pass: count entries */
    size_t entry_count = 1;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == ',') {
            entry_count++;
        }
    }
    
    /* Allocate output array */
    nmo_enum_value_def_t *values = (nmo_enum_value_def_t*)nmo_arena_alloc(
        arena, entry_count * sizeof(nmo_enum_value_def_t), 8);
    
    if (!values) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate enum value array");
    }
    
    /* Initialize entries */
    memset(values, 0, entry_count * sizeof(nmo_enum_value_def_t));
    
    /* Second pass: parse entries */
    size_t current_entry = 0;
    const char *entry_start = str;
    
    for (size_t i = 0; i <= len; i++) {
        /* Check if we hit delimiter or end */
        if (i == len || str[i] == ',') {
            /* Extract entry */
            const char *entry = entry_start;
            size_t entry_len = i - (entry_start - str);
            trim_whitespace(&entry, &entry_len);
            
            if (entry_len == 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                        "Empty entry in enum/flags definition");
            }
            
            /* Find '=' separator */
            const char *eq_pos = NULL;
            for (size_t j = 0; j < entry_len; j++) {
                if (entry[j] == '=') {
                    eq_pos = entry + j;
                    break;
                }
            }
            
            if (!eq_pos) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                        "Missing '=' in enum/flags entry");
            }
            
            /* Parse name */
            const char *name = entry;
            size_t name_len = eq_pos - entry;
            trim_whitespace(&name, &name_len);
            
            if (!is_valid_identifier(name, name_len)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                        "Invalid identifier in enum/flags entry");
            }
            
            /* Parse value */
            const char *value_str = eq_pos + 1;
            size_t value_len = entry_len - (value_str - entry);
            trim_whitespace(&value_str, &value_len);
            
            int64_t value;
            if (!parse_integer_value(value_str, value_len, &value)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                        "Invalid integer value in enum/flags entry");
            }
            
            /* Check for duplicate names */
            for (size_t j = 0; j < current_entry; j++) {
                if (strncmp(values[j].name, name, name_len) == 0 && 
                    strlen(values[j].name) == name_len) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                            "Duplicate name in enum/flags definition");
                }
            }
            
            /* Allocate and copy name */
            char *name_copy = (char*)nmo_arena_alloc(arena, name_len + 1, 1);
            if (!name_copy) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                        "Failed to allocate name string");
            }
            memcpy(name_copy, name, name_len);
            name_copy[name_len] = '\0';
            
            /* Store entry */
            values[current_entry].name = name_copy;
            values[current_entry].value = value;
            values[current_entry].description = NULL;
            current_entry++;
            
            /* Move to next entry */
            if (i < len) {
                entry_start = str + i + 1;
            }
        }
    }
    
    *out_values = values;
    *out_count = current_entry;
    NMO_RETURN_OK();
}

/**
 * @brief Parse flags string: "FLAG1=1,FLAG2=2,FLAG4=4"
 */
nmo_status_t nmo_parse_flags_string(
    const char *flags_str,
    nmo_enum_value_def_t **out_values,
    size_t *out_count,
    nmo_arena_t *arena
) {
    /* Use common parser */
    nmo_status_t result = nmo_parse_enum_flags_string(
        flags_str, out_values, out_count, arena);
    
    if (result != NMO_OK) {
        return result;
    }
    
    /* Additional validation for flags: values should be powers of 2 or 0 */
    for (size_t i = 0; i < *out_count; i++) {
        int64_t value = (*out_values)[i].value;
        
        /* Allow 0 */
        if (value == 0) {
            continue;
        }
        
        /* Check if power of 2 (only one bit set) */
        if (value > 0 && !NMO_IS_POWER_OF_TWO(value)) {
            /* Not a power of 2 - this is a warning, not an error */
            /* Flags can have combined values like ALL=0xFF */
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Parse enum string: "RED=1,GREEN=2,BLUE=3"
 */
nmo_status_t nmo_parse_enum_string(
    const char *enum_str,
    nmo_enum_value_def_t **out_values,
    size_t *out_count,
    nmo_arena_t *arena
) {
    /* Use common parser - no additional validation needed for enums */
    return nmo_parse_enum_flags_string(enum_str, out_values, out_count, arena);
}

/**
 * @brief Parse struct field names: "Field1,Field2,Field3"
 */
nmo_status_t nmo_parse_struct_fields(
    const char *field_names,
    char ***out_names,
    size_t *out_count,
    nmo_arena_t *arena
) {
    if (!field_names || !out_names || !out_count || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid arguments to struct field parser");
    }
    
    /* Trim input */
    const char *str = field_names;
    size_t len = strlen(str);
    trim_whitespace(&str, &len);
    
    if (len == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                "Empty struct field names string");
    }
    
    /* First pass: count fields */
    size_t field_count = 1;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == ',') {
            field_count++;
        }
    }
    
    /* Allocate output array */
    char **names = (char**)nmo_arena_alloc(arena, field_count * sizeof(char*), 8);
    if (!names) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to allocate field name array");
    }
    
    /* Second pass: parse field names */
    size_t current_field = 0;
    const char *field_start = str;
    
    for (size_t i = 0; i <= len; i++) {
        /* Check if we hit delimiter or end */
        if (i == len || str[i] == ',') {
            /* Extract field name */
            const char *name = field_start;
            size_t name_len = i - (field_start - str);
            trim_whitespace(&name, &name_len);
            
            if (name_len == 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                        "Empty field name in struct definition");
            }
            
            /* Validate identifier */
            if (!is_valid_identifier(name, name_len)) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                        "Invalid field name identifier");
            }
            
            /* Check for duplicate names */
            for (size_t j = 0; j < current_field; j++) {
                if (strncmp(names[j], name, name_len) == 0 && 
                    strlen(names[j]) == name_len) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                            "Duplicate field name in struct definition");
                }
            }
            
            /* Allocate and copy name */
            char *name_copy = (char*)nmo_arena_alloc(arena, name_len + 1, 1);
            if (!name_copy) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                        "Failed to allocate field name string");
            }
            memcpy(name_copy, name, name_len);
            name_copy[name_len] = '\0';
            
            /* Store name */
            names[current_field] = name_copy;
            current_field++;
            
            /* Move to next field */
            if (i < len) {
                field_start = str + i + 1;
            }
        }
    }
    
    *out_names = names;
    *out_count = current_field;
    NMO_RETURN_OK();
}
