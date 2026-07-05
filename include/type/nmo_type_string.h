/**
 * @file nmo_type_string.h
 * @brief Type-to-string and string-to-type conversion system (Phase 6.4)
 *
 * Provides human-readable string representation for all parameter types.
 * Supports bidirectional conversion for debugging, UI display, and serialization.
 *
 * Design references:
 * - CKParameterTypeDesc::ToString (CKParameterManager.cpp:1345-1389)
 * - CKParameterTypeDesc::FromString (CKParameterManager.cpp:1391-1435)
 *
 * Format specifications:
 * - Float:      [-]digits[.digits][e[+-]digits]  e.g., "3.14159", "-2.5e-3"
 * - Int:        [-]digits | 0xhex                e.g., "42", "-100", "0x2A"
 * - Bool:       true | false | 1 | 0             e.g., "true", "0"
 * - Vector2:    (x, y)                           e.g., "(1.0, 2.0)"
 * - Vector:     (x, y, z)                        e.g., "(1.0, 2.0, 3.0)"
 * - Vector4:    (x, y, z, w)                     e.g., "(1.0, 2.0, 3.0, 4.0)"
 * - Quaternion: (x, y, z, w)                     e.g., "(0.707, 0, 0.707, 0)"
 * - Matrix:     (16 floats; 4 rows separated by ';')
 * - Color:      (r, g, b, a)
 * - Enum:       name | integer                   e.g., "RED", "1"
 * - Flags:      name1|name2 | 0xvalue            e.g., "READ|WRITE", "0x03"
 * - String:     "..." (supports escaping)        e.g., "\"Hello\\nWorld\""
 * - ObjectID:   #id                              e.g., "#12345"
 */

#ifndef NMO_TYPE_STRING_H
#define NMO_TYPE_STRING_H

#include "nmo_types.h"
#include "core/nmo_math.h"
#include "core/nmo_error.h"
#include "type/nmo_type_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * General-Purpose String Conversion API
 * ============================================================================ */

/**
 * @brief Convert a value to its string representation
 *
 * @param value          Pointer to value data
 * @param type           Type descriptor
 * @param registry       Type registry for enum/flags metadata (optional)
 * @param buffer         Output string buffer
 * @param buffer_size    Buffer capacity in bytes
 * @return NMO_OK on success, error code otherwise
 * @note Buffer is caller-owned; function does not allocate.
 *
 * @note
 * - Buffer must be at least 32 bytes for primitive types
 * - Complex types (Vector*, Quaternion, Matrix, Color) need 64+ bytes
 * - Enum/Flags need space for longest name combination
 * - Returns NMO_ERROR_BUFFER_TOO_SMALL if insufficient
 *
 * Reference: CKParameterTypeDesc::ToString (CKParameterManager.cpp:1345)
 */
NMO_API nmo_status_t nmo_type_value_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size
);

/**
 * @brief Parse a string into a typed value
 *
 * @param value          Pointer to value buffer (output)
 * @param type           Type descriptor
 * @param registry       Type registry for enum/flags metadata (optional)
 * @param string         Input string
 * @return NMO_OK on success, error code otherwise
 * @note value is caller-owned; function does not allocate.
 *
 * @note
 * - value must be pre-allocated to type->size bytes
 * - Supports multiple input formats (see format specs above)
 * - Returns NMO_ERROR_PARSE_INVALID_FORMAT on syntax error
 *
 * Reference: CKParameterTypeDesc::FromString (CKParameterManager.cpp:1391)
 */
NMO_API nmo_status_t nmo_type_value_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string
);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Escape string for output (add quotes, escape special chars)
 *
 * @param src            Source string
 * @param dst            Destination buffer
 * @param dst_size       Buffer capacity
 * @return Number of bytes written (excluding null terminator)
 * @note dst is caller-owned; function does not allocate.
 */
NMO_API size_t nmo_string_escape(
    const char *src,
    char *dst,
    size_t dst_size
);

/**
 * @brief Unescape string (remove quotes, process escape sequences)
 *
 * @param src            Source string (with quotes)
 * @param dst            Destination buffer
 * @param dst_size       Buffer capacity
 * @return Number of bytes written (excluding null terminator)
 * @note dst is caller-owned; function does not allocate.
 */
NMO_API size_t nmo_string_unescape(
    const char *src,
    char *dst,
    size_t dst_size
);

/* ============================================================================
 * Field-Level String Conversion API
 *
 * Convenience wrappers that resolve a field by name within a typed state
 * and delegate to nmo_type_value_from_string / nmo_type_value_to_string.
 * ============================================================================ */

/**
 * @brief Set a typed field by name from a string representation.
 *
 * Resolves the field within the type descriptor, resolves the field's type,
 * and calls nmo_type_value_from_string to parse and write at the field offset.
 *
 * @param state      Mutable typed state instance
 * @param type       Type descriptor for the state
 * @param registry   Type registry for field type resolution
 * @param field_name Field name to set
 * @param value_str  String representation of the new value
 * @return NMO_OK on success, NMO_ERR_NOT_FOUND if field not found,
 *         error from nmo_type_value_from_string on parse failure
 */
NMO_API nmo_status_t nmo_type_set_field(
    void *state,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *field_name,
    const char *value_str);

/**
 * @brief Read a typed field value as a string.
 *
 * @param state      Typed state instance (read-only)
 * @param type       Type descriptor
 * @param registry   Type registry
 * @param field_name Field name to read
 * @param out_buf    Output buffer
 * @param buf_size   Buffer size
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_type_get_field(
    const void *state,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *field_name,
    char *out_buf,
    size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_STRING_H */
