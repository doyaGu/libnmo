/**
 * @file dynamic_types.h
 * @brief Dynamic type registration API (Phase 6.2)
 *
 * Provides runtime type registration for enums, flags, and structs.
 * Equivalent to Virtools CKParameterManager::RegisterNewEnum/Flags/Struct.
 *
 * Key features:
 * - Parse type names from strings ("int", "float", "MyStruct", "int[10]")
 * - Register enum types with named values
 * - Register flag types with named bits
 * - Register struct types with fields and automatic layout calculation
 * - Support nested structs and arrays
 *
 * Reference: CKParameterManager.cpp, lines 298-360 (RegisterNewEnum/Struct)
 */

#ifndef NMO_TYPE_DYNAMIC_TYPES_H
#define NMO_TYPE_DYNAMIC_TYPES_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "type/type_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Type Name Parsing
 * ============================================================================ */

/**
 * @brief Type name parse result
 */
typedef struct nmo_type_parse_result_t {
    nmo_guid_t base_type_guid;          /**< Base type GUID (resolved from name) */
    const char *type_name;              /**< Original type name */
    bool is_array;                      /**< True if array type (e.g., "int[10]") */
    uint32_t array_count;               /**< Array element count (0 if not array) */
    bool is_pointer;                    /**< True if pointer type (e.g., "int*") */
    uint32_t pointer_depth;             /**< Pointer indirection depth */
} nmo_type_parse_result_t;

/**
 * @brief Parse type name string to type descriptor
 *
 * Supported formats:
 * - Basic types: "int", "float", "bool", "string"
 * - Structs: "MyStruct", "VxVector3"
 * - Arrays: "int[10]", "float[256]"
 * - Pointers: "int*", "CKObject**"
 * - Case-insensitive, whitespace-tolerant
 *
 * @param type_registry Type registry
 * @param type_name Type name string
 * @param result Output parse result
 * @return NMO_OK on success, error code on failure
 *
 * Example:
 * @code
 * nmo_type_parse_result_t result;
 * nmo_status_t res = nmo_type_registry_parse_type_name(
 *     registry, "int[10]", &result);
 * if (res.code == NMO_OK) {
 *     // result.base_type_guid = INT type GUID
 *     // result.is_array = true
 *     // result.array_count = 10
 * }
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_parse_type_name(
    const nmo_type_registry_t *type_registry,
    const char *type_name,
    nmo_type_parse_result_t *result
);

/* ============================================================================
 * Enum Type Registration
 * ============================================================================ */

/**
 * @brief Enum value definition
 */
typedef struct nmo_enum_value_def_t {
    const char *name;                   /**< Enum constant name */
    int64_t value;                      /**< Enum constant value */
    const char *description;            /**< Optional description */
} nmo_enum_value_def_t;

/**
 * @brief Enum type definition
 */
typedef struct nmo_enum_type_def_t {
    const char *name;                   /**< Type name */
    const char *description;            /**< Type description */
    nmo_guid_t guid;                   /**< Type GUID (can be NULL_GUID for auto-generation) */
    const nmo_enum_value_def_t *values; /**< Array of enum values */
    size_t value_count;                 /**< Number of values */
    int64_t default_value;              /**< Default value */
} nmo_enum_type_def_t;

/**
 * @brief Register new enum type
 *
 * Registers an enumeration type with named constants.
 * Similar to Virtools CKParameterManager::RegisterNewEnum.
 *
 * @param type_registry Type registry
 * @param enum_def Enum type definition
 * @param out_type_id Output type ID (optional)
 * @return NMO_OK on success, error code on failure
 *
 * Example:
 * @code
 * nmo_enum_value_def_t colors[] = {
 *     {"Red", 0, "Red color"},
 *     {"Green", 1, "Green color"},
 *     {"Blue", 2, "Blue color"}
 * };
 * nmo_enum_type_def_t color_enum = {
 *     .name = "Color",
 *     .description = "Basic colors",
 *     .guid = {0x12345678, 0x12345678},
 *     .values = colors,
 *     .value_count = 3,
 *     .default_value = 0
 * };
 * nmo_guid_t guid;
 * nmo_status_t res = nmo_type_registry_register_enum(
 *     registry, &color_enum, &guid);
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_register_enum(
    nmo_type_registry_t *type_registry,
    const nmo_enum_type_def_t *enum_def,
    nmo_guid_t *out_guid
);

/* ============================================================================
 * Flags Type Registration
 * ============================================================================ */

/**
 * @brief Flags bit definition
 */
typedef struct nmo_flags_bit_def_t {
    const char *name;                   /**< Flag bit name */
    uint64_t mask;                      /**< Bit mask value */
    const char *description;            /**< Optional description */
} nmo_flags_bit_def_t;

/**
 * @brief Flags type definition
 */
typedef struct nmo_flags_type_def_t {
    const char *name;                   /**< Type name */
    const char *description;            /**< Type description */
    nmo_guid_t guid;                   /**< Type GUID (can be NULL_GUID for auto-generation) */
    const nmo_flags_bit_def_t *bits;    /**< Array of flag bits */
    size_t bit_count;                   /**< Number of bits */
    uint64_t default_value;             /**< Default value */
} nmo_flags_type_def_t;

/**
 * @brief Register new flags type
 *
 * Registers a bit flags type with named bits (combinable values).
 * Similar to Virtools CKParameterManager::RegisterNewFlags.
 *
 * @param type_registry Type registry
 * @param flags_def Flags type definition
 * @param out_type_id Output type ID (optional)
 * @return NMO_OK on success, error code on failure
 *
 * Example:
 * @code
 * nmo_flags_bit_def_t permissions[] = {
 *     {"Read", 0x01, "Read permission"},
 *     {"Write", 0x02, "Write permission"},
 *     {"Execute", 0x04, "Execute permission"}
 * };
 * nmo_flags_type_def_t perm_flags = {
 *     .name = "Permissions",
 *     .description = "File permissions",
 *     .guid = {0x87654321, 0x87654321},
 *     .bits = permissions,
 *     .bit_count = 3,
 *     .default_value = 0x01  // Read-only by default
 * };
 * nmo_status_t res = nmo_type_registry_register_flags(
 *     registry, &perm_flags, NULL);
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_register_flags(
    nmo_type_registry_t *type_registry,
    const nmo_flags_type_def_t *flags_def,
    nmo_guid_t *out_guid
);

/* ============================================================================
 * Struct Type Registration
 * ============================================================================ */

/**
 * @brief Struct field definition
 */
typedef struct nmo_struct_field_def_t {
    const char *name;                   /**< Field name */
    const char *type_name;              /**< Field type name (e.g., "int", "float[10]") */
    nmo_guid_t type_guid;              /**< Field type GUID (alternative to type_name) */
    const char *description;            /**< Optional description */
    uint32_t flags;                     /**< Field flags (NMO_FIELD_*) */
    const void *default_value;          /**< Optional default value */
} nmo_struct_field_def_t;

/**
 * @brief Struct type definition
 */
typedef struct nmo_struct_type_def_t {
    const char *name;                   /**< Type name */
    const char *description;            /**< Type description */
    nmo_guid_t guid;                   /**< Type GUID (can be NULL_GUID for auto-generation) */
    const nmo_struct_field_def_t *fields; /**< Array of fields */
    size_t field_count;                 /**< Number of fields */
    uint32_t alignment;                 /**< Struct alignment (0 = auto-calculate) */
    bool packed;                        /**< Use packed layout (no padding) */
} nmo_struct_type_def_t;

/**
 * @brief Register new struct type
 *
 * Registers a structure type with fields. Automatically calculates
 * field offsets, sizes, and alignment.
 * Similar to Virtools CKParameterManager::RegisterNewStructure.
 *
 * @param type_registry Type registry
 * @param struct_def Struct type definition
 * @param out_type_id Output type ID (optional)
 * @return NMO_OK on success, error code on failure
 *
 * Example:
 * @code
 * nmo_struct_field_def_t vec3_fields[] = {
 *     {"x", "float", {0,0}, "X coordinate", 0, NULL},
 *     {"y", "float", {0,0}, "Y coordinate", 0, NULL},
 *     {"z", "float", {0,0}, "Z coordinate", 0, NULL}
 * };
 * nmo_struct_type_def_t vec3_struct = {
 *     .name = "Vector3",
 *     .description = "3D vector",
 *     .guid = {0xABCDEF01, 0xABCDEF01},
 *     .fields = vec3_fields,
 *     .field_count = 3,
 *     .alignment = 0,  // Auto
 *     .packed = false
 * };
 * nmo_status_t res = nmo_type_registry_register_struct(
 *     registry, &vec3_struct, NULL);
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_register_struct(
    nmo_type_registry_t *type_registry,
    const nmo_struct_type_def_t *struct_def,
    nmo_guid_t *out_guid
);

/* ============================================================================
 * Incremental Struct Building (Alternative API)
 * ============================================================================ */

/**
 * @brief Begin defining a new struct type
 *
 * Creates an incomplete struct type that can be populated with fields
 * incrementally. Must call nmo_type_registry_finalize_struct() when done.
 *
 * @param type_registry Type registry
 * @param name Struct name
 * @param guid Struct GUID (or NULL_GUID for auto)
 * @param out_type_id Output type ID
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_type_registry_begin_struct(
    nmo_type_registry_t *type_registry,
    const char *name,
    nmo_guid_t guid,
    nmo_type_id_t *out_type_id
);

/**
 * @brief Add field to struct being defined
 *
 * Adds a field to a struct type started with nmo_type_registry_begin_struct().
 *
 * @param type_registry Type registry
 * @param struct_type_id Struct type ID (from begin_struct)
 * @param field_name Field name
 * @param field_type_name Field type name (e.g., "int", "float[10]")
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_type_registry_add_field(
    nmo_type_registry_t *type_registry,
    nmo_type_id_t struct_type_id,
    const char *field_name,
    const char *field_type_name
);

/**
 * @brief Finalize struct definition
 *
 * Completes struct definition, calculates layout, and makes it available.
 *
 * @param type_registry Type registry
 * @param struct_type_id Struct type ID
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_type_registry_finalize_struct(
    nmo_type_registry_t *type_registry,
    nmo_type_id_t struct_type_id
);

/* ============================================================================
 * String-Based Registration API (Phase 6.2, Task 6.2.2)
 * ============================================================================ */

/**
 * @brief Register enum type from string definition
 *
 * Convenient wrapper that parses enum definition string and registers the type.
 * Equivalent to calling nmo_parse_enum_string() followed by nmo_type_registry_register_enum().
 *
 * @param type_registry Type registry
 * @param type_guid Type GUID (if NULL_GUID, auto-generates from name)
 * @param type_name Type name
 * @param enum_data Enum definition string (e.g., "RED=0,GREEN=1,BLUE=2")
 * @return NMO_OK on success, error code on failure
 *
 * Example:
 * @code
 * nmo_status_t res = nmo_type_registry_register_enum_string(
 *     registry,
 *     (nmo_guid_t){0x12345678, 0x12345678},
 *     "ColorEnum",
 *     "RED=0,GREEN=1,BLUE=2"
 * );
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_register_enum_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *type_name,
    const char *enum_data
);

/**
 * @brief Register flags type from string definition
 *
 * Convenient wrapper that parses flags definition string and registers the type.
 * Equivalent to calling nmo_parse_flags_string() followed by nmo_type_registry_register_flags().
 *
 * @param type_registry Type registry
 * @param type_guid Type GUID (if NULL_GUID, auto-generates from name)
 * @param type_name Type name
 * @param flags_data Flags definition string (e.g., "READ=1,WRITE=2,EXECUTE=4")
 * @return NMO_OK on success, error code on failure
 *
 * Example:
 * @code
 * nmo_status_t res = nmo_type_registry_register_flags_string(
 *     registry,
 *     (nmo_guid_t){0x87654321, 0x87654321},
 *     "FilePermissions",
 *     "READ=0x01,WRITE=0x02,EXECUTE=0x04"
 * );
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_register_flags_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *type_name,
    const char *flags_data
);

/**
 * @brief Modify existing enum type definition
 *
 * Updates an existing enum type with new values. The new definition must be
 * a superset of the old definition (can add values, cannot remove or change existing ones).
 *
 * @param type_registry Type registry
 * @param type_guid Type GUID
 * @param new_enum_data New enum definition string
 * @return NMO_OK on success, error code on failure
 *
 * Example:
 * @code
 * // Original: "RED=0,GREEN=1,BLUE=2"
 * // Add YELLOW without breaking compatibility:
 * nmo_status_t res = nmo_type_registry_change_enum_string(
 *     registry,
 *     enum_guid,
 *     "RED=0,GREEN=1,BLUE=2,YELLOW=3"
 * );
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_change_enum_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *new_enum_data
);

/**
 * @brief Modify existing flags type definition
 *
 * Updates an existing flags type with new bits. The new definition must be
 * a superset of the old definition (can add bits, cannot remove or change existing ones).
 *
 * @param type_registry Type registry
 * @param type_guid Type GUID
 * @param new_flags_data New flags definition string
 * @return NMO_OK on success, error code on failure
 *
 * Example:
 * @code
 * // Original: "READ=0x01,WRITE=0x02"
 * // Add EXECUTE without breaking compatibility:
 * nmo_status_t res = nmo_type_registry_change_flags_string(
 *     registry,
 *     flags_guid,
 *     "READ=0x01,WRITE=0x02,EXECUTE=0x04"
 * );
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_change_flags_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *new_flags_data
);

/**
 * @brief Register struct type from type name array (Phase 6.2 Task 6.2.3)
 *
 * Registers a struct type by taking an array of field type names and looking up
 * each type to build the struct definition. Field names are auto-generated as
 * "field0", "field1", etc. Layout is automatically calculated with proper alignment.
 *
 * This is a simpler alternative to nmo_type_registry_register_struct() when you
 * only have type names and don't need custom field names or descriptions.
 *
 * @param type_registry Type registry
 * @param type_guid Struct GUID (or NULL_GUID for auto-generation)
 * @param type_name Struct type name
 * @param field_type_names Array of field type name strings (e.g., ["int", "float", "VxVector3"])
 * @param field_count Number of fields
 * @return NMO_OK on success, error code on failure
 *
 * Error codes:
 * - NMO_ERR_INVALID_ARGUMENT: NULL arguments or empty field list
 * - NMO_ERR_NOT_FOUND: Field type name not found in registry
 * - NMO_ERR_ALREADY_EXISTS: Struct type with this GUID already exists
 * - NMO_ERR_NOMEM: Allocation failure
 *
 * Example:
 * @code
 * const char *field_types[] = {"float", "float", "float"};
 * nmo_status_t res = nmo_type_registry_register_struct_string(
 *     registry,
 *     NMO_NULL_GUID,  // Auto-generate GUID
 *     "Vector3",
 *     field_types,
 *     3
 * );
 * // Creates struct with fields: field0 (float), field1 (float), field2 (float)
 * @endcode
 */
NMO_API nmo_status_t nmo_type_registry_register_struct_string(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *type_name,
    const char **field_type_names,
    size_t field_count
);

/* ============================================================================
 * Layout Calculation Utilities
 * ============================================================================ */

/**
 * @brief Calculate struct layout
 *
 * Computes field offsets and total size based on field types and alignment.
 * Uses platform-specific alignment rules unless packed=true.
 *
 * @param type_registry Type registry
 * @param fields Array of fields
 * @param field_count Number of fields
 * @param alignment Desired alignment (0 = auto)
 * @param packed Use packed layout (no padding)
 * @param out_total_size Output total struct size
 * @param out_alignment Output actual alignment
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_type_calculate_layout(
    const nmo_type_registry_t *type_registry,
    nmo_struct_field_def_t *fields,
    size_t field_count,
    uint32_t alignment,
    bool packed,
    uint32_t *out_total_size,
    uint32_t *out_alignment
);

/**
 * @brief Get alignment requirement for type
 *
 * @param type_registry Type registry
 * @param type_guid Type GUID
 * @return Alignment in bytes (1, 2, 4, 8, etc.)
 */
NMO_API uint32_t nmo_type_get_alignment(
    const nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid
);

/**
 * @brief Get size of type
 *
 * @param type_registry Type registry
 * @param type_guid Type GUID
 * @return Size in bytes
 */
NMO_API uint32_t nmo_type_get_size(
    const nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid
);

/* ============================================================================
 * String Parsers (Phase 6.2, Task 6.2.1)
 * ============================================================================ */

/**
 * @brief Parse flags definition string
 *
 * Parses a flags definition string like "FLAG1=1,FLAG2=2,FLAG4=4" into
 * an array of enum value definitions. Supports both decimal and hexadecimal
 * values (e.g., "0x01", "0xFF").
 *
 * @param flags_str Flags definition string (e.g., "READ=1,WRITE=2,EXECUTE=4")
 * @param out_values Output array of parsed values (arena-allocated)
 * @param out_count Number of values parsed
 * @param arena Arena for allocations
 * @return NMO_OK on success, error code on failure
 *
 * Error codes:
 * - NMO_ERR_INVALID_ARGUMENT: NULL arguments
 * - NMO_ERR_INVALID_FORMAT: Syntax error, invalid identifier, or duplicate name
 * - NMO_ERR_NOMEM: Allocation failure
 *
 * Example:
 * @code
 * nmo_enum_value_def_t *values;
 * size_t count;
 * nmo_status_t res = nmo_parse_flags_string(
 *     "READ=1,WRITE=2,EXECUTE=4", &values, &count, arena);
 * // values[0] = {"READ", 1, NULL}
 * // values[1] = {"WRITE", 2, NULL}
 * // values[2] = {"EXECUTE", 4, NULL}
 * @endcode
 */
NMO_API nmo_status_t nmo_parse_flags_string(
    const char *flags_str,
    nmo_enum_value_def_t **out_values,
    size_t *out_count,
    nmo_arena_t *arena
);

/**
 * @brief Parse enum definition string
 *
 * Parses an enum definition string like "RED=1,GREEN=2,BLUE=3" into
 * an array of enum value definitions. Supports both decimal and hexadecimal
 * values.
 *
 * @param enum_str Enum definition string (e.g., "IDLE=0,RUNNING=1,STOPPED=2")
 * @param out_values Output array of parsed values (arena-allocated)
 * @param out_count Number of values parsed
 * @param arena Arena for allocations
 * @return NMO_OK on success, error code on failure
 *
 * Error codes:
 * - NMO_ERR_INVALID_ARGUMENT: NULL arguments
 * - NMO_ERR_INVALID_FORMAT: Syntax error, invalid identifier, or duplicate name
 * - NMO_ERR_NOMEM: Allocation failure
 *
 * Example:
 * @code
 * nmo_enum_value_def_t *values;
 * size_t count;
 * nmo_status_t res = nmo_parse_enum_string(
 *     "RED=0,GREEN=1,BLUE=2", &values, &count, arena);
 * // values[0] = {"RED", 0, NULL}
 * // values[1] = {"GREEN", 1, NULL}
 * // values[2] = {"BLUE", 2, NULL}
 * @endcode
 */
NMO_API nmo_status_t nmo_parse_enum_string(
    const char *enum_str,
    nmo_enum_value_def_t **out_values,
    size_t *out_count,
    nmo_arena_t *arena
);

/**
 * @brief Parse struct field names string
 *
 * Parses a comma-separated list of field names like "Position,Rotation,Scale"
 * into an array of strings. Field names must be valid identifiers.
 *
 * @param field_names Field names string (e.g., "x,y,z")
 * @param out_names Output array of field name strings (arena-allocated)
 * @param out_count Number of field names parsed
 * @param arena Arena for allocations
 * @return NMO_OK on success, error code on failure
 *
 * Error codes:
 * - NMO_ERR_INVALID_ARGUMENT: NULL arguments
 * - NMO_ERR_INVALID_FORMAT: Empty name, invalid identifier, or duplicate name
 * - NMO_ERR_NOMEM: Allocation failure
 *
 * Example:
 * @code
 * char **names;
 * size_t count;
 * nmo_status_t res = nmo_parse_struct_fields(
 *     "Position,Rotation,Scale", &names, &count, arena);
 * // names[0] = "Position"
 * // names[1] = "Rotation"
 * // names[2] = "Scale"
 * @endcode
 */
NMO_API nmo_status_t nmo_parse_struct_fields(
    const char *field_names,
    char ***out_names,
    size_t *out_count,
    nmo_arena_t *arena
);

/* ============================================================================
 * GUID Generation
 * ============================================================================ */

/**
 * @brief Generate unique GUID for type name
 *
 * Creates a deterministic GUID based on type name (hash-based).
 * Useful for auto-generating GUIDs for user-defined types.
 *
 * @param type_name Type name
 * @return Generated GUID
 */
NMO_API nmo_guid_t nmo_type_generate_guid(const char *type_name);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TYPE_DYNAMIC_TYPES_H */
