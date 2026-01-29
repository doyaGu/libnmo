/**
 * @file type_parser.c
 * @brief Type name string parser (Phase 6.2, Task 6.2.2)
 *
 * Parses type name strings like "int", "float[10]", "MyStruct*" into
 * structured type descriptors.
 */

#include "type/dynamic_types.h"
#include "type/type_system.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ============================================================================
 * Builtin Type Name Mapping
 * ============================================================================ */

typedef struct builtin_type_name_t {
    const char *name;
    nmo_guid_t guid;
} builtin_type_name_t;

static const builtin_type_name_t BUILTIN_TYPES[] = {
    /* Basic types */
    {"int", {0x6FED1D00, 0x00000001}},
    {"INT", {0x6FED1D00, 0x00000001}},
    {"float", {0x6FED1D00, 0x00000002}},
    {"FLOAT", {0x6FED1D00, 0x00000002}},
    {"bool", {0x6FED1D00, 0x00000003}},
    {"BOOL", {0x6FED1D00, 0x00000003}},
    {"string", {0x6FED1D00, 0x00000010}},
    {"STRING", {0x6FED1D00, 0x00000010}},
    
    /* Virtools common types */
    {"VxVector2", {0x6FED1D00, 0x00000004}},
    {"VxVector3", {0x6FED1D00, 0x00000005}},
    {"VxVector4", {0x6FED1D00, 0x00000006}},
    {"VxQuaternion", {0x6FED1D00, 0x00000007}},
    {"VxMatrix", {0x6FED1D00, 0x00000008}},
    {"VxColor", {0x6FED1D00, 0x00000009}},
    
    /* Null terminator */
    {NULL, {0, 0}}
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
 * @brief Check if string matches (case-insensitive)
 */
static bool str_equals_ci(const char *a, size_t a_len, const char *b) {
    size_t b_len = strlen(b);
    if (a_len != b_len) {
        return false;
    }
    
    for (size_t i = 0; i < a_len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    
    return true;
}

/* ============================================================================
 * Type Name Parsing
 * ============================================================================ */

/**
 * @brief Parse array suffix "[N]"
 */
static bool parse_array_suffix(const char *str, size_t len, size_t *base_len, uint32_t *array_count) {
    /* Find '[' */
    size_t bracket_pos = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '[') {
            bracket_pos = i;
            break;
        }
    }
    
    if (bracket_pos == 0) {
        *base_len = len;
        *array_count = 0;
        return true;  /* No array */
    }
    
    /* Find ']' */
    size_t close_bracket = 0;
    for (size_t i = bracket_pos + 1; i < len; i++) {
        if (str[i] == ']') {
            close_bracket = i;
            break;
        }
    }
    
    if (close_bracket == 0 || close_bracket != len - 1) {
        return false;  /* Malformed array syntax */
    }
    
    /* Parse number between brackets */
    const char *count_str = str + bracket_pos + 1;
    size_t count_len = close_bracket - bracket_pos - 1;
    
    char count_buf[32];
    if (count_len >= sizeof(count_buf)) {
        return false;  /* Number too long */
    }
    
    memcpy(count_buf, count_str, count_len);
    count_buf[count_len] = '\0';
    
    char *endptr;
    long count = strtol(count_buf, &endptr, 10);
    if (endptr != count_buf + count_len || count <= 0) {
        return false;  /* Invalid number */
    }
    
    *base_len = bracket_pos;
    *array_count = (uint32_t)count;
    return true;
}

/**
 * @brief Parse pointer suffix "*"
 */
static void parse_pointer_suffix(const char *str, size_t len, size_t *base_len, uint32_t *pointer_depth) {
    size_t depth = 0;
    size_t pos = len;
    
    while (pos > 0 && str[pos - 1] == '*') {
        depth++;
        pos--;
    }
    
    *base_len = pos;
    *pointer_depth = (uint32_t)depth;
}

/**
 * @brief Lookup builtin type by name
 */
static bool lookup_builtin_type(const char *name, size_t name_len, nmo_guid_t *out_guid) {
    for (size_t i = 0; BUILTIN_TYPES[i].name != NULL; i++) {
        if (str_equals_ci(name, name_len, BUILTIN_TYPES[i].name)) {
            *out_guid = BUILTIN_TYPES[i].guid;
            return true;
        }
    }
    return false;
}

nmo_result_t nmo_type_registry_parse_type_name(
    const nmo_type_registry_t *type_registry,
    const char *type_name,
    nmo_type_parse_result_t *result
) {
    if (!type_registry || !type_name || !result) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "NULL type_registry, type_name, or result");
    }
    
    /* Initialize result */
    memset(result, 0, sizeof(*result));
    result->type_name = type_name;
    
    /* Get string length and trim */
    const char *str = type_name;
    size_t len = strlen(type_name);
    trim_whitespace(&str, &len);
    
    if (len == 0) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Empty type name");
    }
    
    /* Parse pointer suffix */
    size_t base_len = len;
    uint32_t pointer_depth = 0;
    parse_pointer_suffix(str, len, &base_len, &pointer_depth);
    
    result->is_pointer = (pointer_depth > 0);
    result->pointer_depth = pointer_depth;
    
    /* Parse array suffix */
    uint32_t array_count = 0;
    if (!parse_array_suffix(str, base_len, &base_len, &array_count)) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Malformed array syntax in type name");
    }
    
    result->is_array = (array_count > 0);
    result->array_count = array_count;
    
    /* Trim base type name */
    const char *base_name = str;
    size_t base_name_len = base_len;
    trim_whitespace(&base_name, &base_name_len);
    
    /* Lookup builtin type */
    if (lookup_builtin_type(base_name, base_name_len, &result->base_type_guid)) {
        NMO_RETURN_OK();
    }
    
    /* Lookup registered type by name */
    char name_buf[256];
    if (base_name_len >= sizeof(name_buf)) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Type name too long");
    }
    
    memcpy(name_buf, base_name, base_name_len);
    name_buf[base_name_len] = '\0';
    
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_name(
        type_registry, name_buf);
    
    if (type_desc) {
        result->base_type_guid = type_desc->guid;
        NMO_RETURN_OK();
    }
    
    /* Type not found */
    return nmo_result_errorf(NULL, NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "Type '%s' not found in registry", name_buf);
}

/* ============================================================================
 * GUID Generation
 * ============================================================================ */

nmo_guid_t nmo_type_generate_guid(const char *type_name) {
    if (!type_name) {
        return (nmo_guid_t){0, 0};
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
nmo_result_t nmo_parse_enum_flags_string(
    const char *input_str,
    nmo_enum_value_def_t **out_values,
    size_t *out_count,
    nmo_arena_t *arena
) {
    if (!input_str || !out_values || !out_count || !arena) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Invalid arguments to enum/flags parser");
    }
    
    /* Trim input */
    const char *str = input_str;
    size_t len = strlen(str);
    trim_whitespace(&str, &len);
    
    if (len == 0) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
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
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
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
                return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
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
                return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                         "Missing '=' in enum/flags entry");
            }
            
            /* Parse name */
            const char *name = entry;
            size_t name_len = eq_pos - entry;
            trim_whitespace(&name, &name_len);
            
            if (!is_valid_identifier(name, name_len)) {
                return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                         "Invalid identifier in enum/flags entry");
            }
            
            /* Parse value */
            const char *value_str = eq_pos + 1;
            size_t value_len = entry_len - (value_str - entry);
            trim_whitespace(&value_str, &value_len);
            
            int64_t value;
            if (!parse_integer_value(value_str, value_len, &value)) {
                return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                         "Invalid integer value in enum/flags entry");
            }
            
            /* Check for duplicate names */
            for (size_t j = 0; j < current_entry; j++) {
                if (strncmp(values[j].name, name, name_len) == 0 && 
                    strlen(values[j].name) == name_len) {
                    return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                             "Duplicate name in enum/flags definition");
                }
            }
            
            /* Allocate and copy name */
            char *name_copy = (char*)nmo_arena_alloc(arena, name_len + 1, 1);
            if (!name_copy) {
                return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
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
nmo_result_t nmo_parse_flags_string(
    const char *flags_str,
    nmo_enum_value_def_t **out_values,
    size_t *out_count,
    nmo_arena_t *arena
) {
    /* Use common parser */
    nmo_result_t result = nmo_parse_enum_flags_string(
        flags_str, out_values, out_count, arena);
    
    if (result.code != NMO_OK) {
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
        if (value > 0 && (value & (value - 1)) != 0) {
            /* Not a power of 2 - this is a warning, not an error */
            /* Flags can have combined values like ALL=0xFF */
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Parse enum string: "RED=1,GREEN=2,BLUE=3"
 */
nmo_result_t nmo_parse_enum_string(
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
nmo_result_t nmo_parse_struct_fields(
    const char *field_names,
    char ***out_names,
    size_t *out_count,
    nmo_arena_t *arena
) {
    if (!field_names || !out_names || !out_count || !arena) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                 "Invalid arguments to struct field parser");
    }
    
    /* Trim input */
    const char *str = field_names;
    size_t len = strlen(str);
    trim_whitespace(&str, &len);
    
    if (len == 0) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
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
        return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
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
                return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                         "Empty field name in struct definition");
            }
            
            /* Validate identifier */
            if (!is_valid_identifier(name, name_len)) {
                return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                         "Invalid field name identifier");
            }
            
            /* Check for duplicate names */
            for (size_t j = 0; j < current_field; j++) {
                if (strncmp(names[j], name, name_len) == 0 && 
                    strlen(names[j]) == name_len) {
                    return nmo_result_errorf(NULL, NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                             "Duplicate field name in struct definition");
                }
            }
            
            /* Allocate and copy name */
            char *name_copy = (char*)nmo_arena_alloc(arena, name_len + 1, 1);
            if (!name_copy) {
                return nmo_result_errorf(NULL, NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
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
