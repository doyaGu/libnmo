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
 * - ObjectID:   #id | name                       e.g., "#12345", "Ball_01"
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

/* Forward declarations */
struct nmo_session;

/* ============================================================================
 * Optional Object Name Resolution Hooks
 * ============================================================================ */

/**
 * @brief Resolve an object name from an object ID (optional hook)
 *
 * This is an inversion-of-control hook to avoid lower-layer dependencies.
 * Higher layers (Session/App) may install resolvers at startup.
 *
 * @param session   Opaque session pointer (as passed to object-id converters)
 * @param id        Object ID
 * @param out_name  Receives pointer to a NUL-terminated name (must outlive call)
 */
typedef nmo_status_t (*nmo_object_id_to_name_resolver_fn)(
    const void *session,
    nmo_object_id_t id,
    const char **out_name
);

/**
 * @brief Resolve an object ID from an object name (optional hook)
 *
 * @param session   Opaque session pointer (as passed to object-id converters)
 * @param name      Object name
 * @param out_id    Receives resolved object ID
 */
typedef nmo_status_t (*nmo_object_name_to_id_resolver_fn)(
    const void *session,
    const char *name,
    nmo_object_id_t *out_id
);

/**
 * @brief Set global resolvers for object-id string conversion
 *
 * These are optional. If unset, object IDs will be formatted and parsed as
 * "#<id>" only.
 * @note Resolver function pointers must remain valid until replaced.
 */
NMO_API void nmo_type_string_set_object_resolvers(
    nmo_object_id_to_name_resolver_fn id_to_name,
    nmo_object_name_to_id_resolver_fn name_to_id
);

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
 * Type-Specific String Converters
 *
 * NOTE: These are convenience wrappers around the general-purpose API
 * (nmo_type_value_to_string/from_string). The general API uses vtable
 * callbacks for extensibility - custom types can register their own
 * converters via nmo_type_vtable_t::to_string/from_string.
 *
 * The type-specific functions here are provided for:
 * 1. Performance - avoid vtable indirect call for common types
 * 2. Convenience - direct API without type descriptor lookup
 * 3. Documentation - explicit type signatures for compile-time checking
 *
 * For new custom types, prefer registering vtable callbacks rather than
 * adding new type-specific functions here.
 * ============================================================================ */

/**
 * @brief Float to string: "3.14159" or "-2.5e-3"
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_float_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_float_from_string(
    void *value,
    const char *string
);

/**
 * @brief Int to string: "42" or "0x2A"
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_int_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size,
    bool use_hex  /**< true for "0x..." format */
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_int_from_string(
    void *value,
    const char *string
);

/**
 * @brief Bool to string: "true" or "false"
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_bool_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_bool_from_string(
    void *value,
    const char *string
);

/**
 * @brief Vector (3D) to string: "(1.0, 2.0, 3.0)"
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_vector2_to_string(
    const void *value,  /**< float[2] */
    char *buffer,
    size_t buffer_size
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_vector2_from_string(
    void *value,  /**< float[2] */
    const char *string
);

/**
 * @brief Vector3 to string: "(x, y, z)"
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_vector_to_string(
    const void *value,  /**< float[3] */
    char *buffer,
    size_t buffer_size
);

/**
 * @note value is caller-owned; function does not allocate.
 */
/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_vector_from_string(
    void *value,  /**< float[3] */
    const char *string
);

/**
 * @brief Vector4 to string: "(x, y, z, w)"
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_vector4_to_string(
    const void *value,  /**< float[4] */
    char *buffer,
    size_t buffer_size
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_vector4_from_string(
    void *value,  /**< float[4] */
    const char *string
);

/**
 * @brief Quaternion to string: "(x, y, z, w)"
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_quaternion_to_string(
    const void *value,  /**< float[4] */
    char *buffer,
    size_t buffer_size
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_quaternion_from_string(
    void *value,  /**< float[4] */
    const char *string
);

/**
 * @brief Matrix (4x4) to string
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_matrix_to_string(
    const void *value,  /**< nmo_matrix_t */
    char *buffer,
    size_t buffer_size
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_matrix_from_string(
    void *value,  /**< nmo_matrix_t */
    const char *string
);

/**
 * @brief Color (RGBA) to string
 */
NMO_API nmo_status_t nmo_color_to_string(
    const void *value,  /**< float[4] */
    char *buffer,
    size_t buffer_size
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_color_from_string(
    void *value,  /**< float[4] */
    const char *string
);

/**
 * @brief Enum to string: "RED" (name) or "1" (value)
 *
 * @param value          Pointer to enum value (int32_t)
 * @param type           Type descriptor (must be ENUM category)
 * @param registry       Type registry for metadata access
 * @param buffer         Output buffer
 * @param buffer_size    Buffer capacity
 * @param use_name       true to output name, false for numeric value
 * @return NMO_OK on success
 * @note Buffer is caller-owned; function does not allocate.
 *
 * Reference: CKEnumStruct::GetEnumEntry (CKParameterManager.cpp:298-304)
 */
NMO_API nmo_status_t nmo_enum_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    bool use_name
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_enum_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string
);

/**
 * @brief Flags to string: "READ|WRITE" or "0x03"
 *
 * @param value          Pointer to flags value (uint32_t)
 * @param type           Type descriptor (must be FLAGS category)
 * @param registry       Type registry for metadata access
 * @param buffer         Output buffer
 * @param buffer_size    Buffer capacity
 * @param use_names      true to output names, false for hex value
 * @return NMO_OK on success
 * @note Buffer is caller-owned; function does not allocate.
 *
 * Reference: CKFlagsStruct (CKParameterManager.cpp:298-304)
 */
NMO_API nmo_status_t nmo_flags_to_string(
    const void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    char *buffer,
    size_t buffer_size,
    bool use_names
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_flags_from_string(
    void *value,
    const nmo_type_descriptor_t *type,
    const nmo_type_registry_t *registry,
    const char *string
);

/**
 * @brief String to string: "\"Hello\\nWorld\"" (with escaping)
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_string_to_string(
    const void *value,  /**< const char* */
    char *buffer,
    size_t buffer_size
);

/**
 * @note Output string is arena-owned; free by destroying the arena.
 */
NMO_API nmo_status_t nmo_string_from_string(
    void *value,  /**< char** (arena-allocated) */
    const char *string,
    nmo_arena_t *arena
);

/**
 * @brief Object ID to string: "#12345" or "ObjectName"
 *
 * @param value          Pointer to object ID (nmo_object_id_t)
 * @param buffer         Output buffer
 * @param buffer_size    Buffer capacity
 * @param session        Session for object name lookup (can be NULL)
 * @return NMO_OK on success
 * @note Buffer is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_object_id_to_string(
    const void *value,
    char *buffer,
    size_t buffer_size,
    struct nmo_session *session  /**< Optional - for name lookup */
);

/**
 * @note value is caller-owned; function does not allocate.
 */
NMO_API nmo_status_t nmo_object_id_from_string(
    void *value,
    const char *string,
    struct nmo_session *session  /**< Optional - for name lookup */
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

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_STRING_H */
