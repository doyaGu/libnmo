/**
 * @file nmo_schema.h
 * @brief Schema system for Virtools data structure description and serialization
 *
 * This module implements the middle layer of libnmo's three-layer model:
 * Chunk → Schema → Object
 *
 * The schema system provides:
 * - Unified type system for describing Virtools data structures
 * - Symmetric read/write operations driven by schema descriptors
 * - Field-level annotations for versioning and migration
 * - Reflection-based generic API with optional vtable fast paths
 * - Partial understanding support for gradual reverse engineering
 */

#ifndef NMO_SCHEMA_H
#define NMO_SCHEMA_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_schema_type nmo_schema_type_t;
typedef struct nmo_schema_field nmo_schema_field_t;
typedef struct nmo_schema_vtable nmo_schema_vtable_t;

/* =============================================================================
 * TYPE SYSTEM
 * ============================================================================= */

/**
 * @brief Type kind enumeration
 *
 * Categorizes schema types into three main groups:
 * 1. Scalar types (u8-u64, i8-i64, f32, f64, bool, string)
 * 2. Composite types (struct, array, fixed_array)
 * 3. Special types (binary, resource_ref, enum)
 */
typedef enum nmo_type_kind {
    /* Scalar types */
    NMO_TYPE_U8,        /**< Unsigned 8-bit integer */
    NMO_TYPE_U16,       /**< Unsigned 16-bit integer */
    NMO_TYPE_U32,       /**< Unsigned 32-bit integer */
    NMO_TYPE_U64,       /**< Unsigned 64-bit integer */
    NMO_TYPE_I8,        /**< Signed 8-bit integer */
    NMO_TYPE_I16,       /**< Signed 16-bit integer */
    NMO_TYPE_I32,       /**< Signed 32-bit integer */
    NMO_TYPE_I64,       /**< Signed 64-bit integer */
    NMO_TYPE_F32,       /**< 32-bit float */
    NMO_TYPE_F64,       /**< 64-bit float (double) */
    NMO_TYPE_BOOL,      /**< Boolean (typically 32-bit in Virtools) */
    NMO_TYPE_STRING,    /**< Null-terminated string */
    
    /* Composite types */
    NMO_TYPE_STRUCT,    /**< Named fields collection */
    NMO_TYPE_ARRAY,     /**< Dynamic array (length prefix + elements) */
    NMO_TYPE_FIXED_ARRAY, /**< Fixed-length array (e.g., matrix) */
    
    /* Special types */
    NMO_TYPE_BINARY,    /**< Raw byte buffer */
    NMO_TYPE_RESOURCE_REF, /**< Reference to Virtools object/resource */
    NMO_TYPE_ENUM,      /**< Integer-based enumeration */
} nmo_type_kind_t;

/**
 * @brief Field annotation flags
 *
 * Annotations provide additional metadata about fields without affecting
 * basic read/write operations. Used for:
 * - Versioning and migration (since/deprecated)
 * - Role identification (id_field, reference_field, editor_only)
 * - Semantic hints (position, rotation, scale, color, normal)
 * - Unit specifications (seconds, degrees, meters)
 */
typedef enum nmo_field_annotation {
    /* Lifecycle annotations */
    NMO_ANNOTATION_SINCE       = 0x0001, /**< Added in specific version */
    NMO_ANNOTATION_DEPRECATED  = 0x0002, /**< Deprecated in specific version */
    NMO_ANNOTATION_EDITOR_ONLY = 0x0004, /**< Editor-only data (can strip) */
    
    /* Role annotations */
    NMO_ANNOTATION_ID_FIELD    = 0x0010, /**< Object ID field */
    NMO_ANNOTATION_REFERENCE   = 0x0020, /**< Reference to another object */
    
    /* Semantic annotations */
    NMO_ANNOTATION_POSITION    = 0x0100, /**< Position/translation vector */
    NMO_ANNOTATION_ROTATION    = 0x0200, /**< Rotation (quaternion/euler) */
    NMO_ANNOTATION_SCALE       = 0x0400, /**< Scale vector */
    NMO_ANNOTATION_COLOR       = 0x0800, /**< Color value */
    NMO_ANNOTATION_NORMAL      = 0x1000, /**< Normal vector */
    
    /* Unit annotations */
    NMO_ANNOTATION_SECONDS     = 0x10000, /**< Time in seconds */
    NMO_ANNOTATION_DEGREES     = 0x20000, /**< Angle in degrees */
    NMO_ANNOTATION_METERS      = 0x40000, /**< Distance in meters */
} nmo_field_annotation_t;

/* =============================================================================
 * TYPE AND FIELD DESCRIPTORS
 * ============================================================================= */

/**
 * @brief Enum value descriptor
 *
 * Maps integer values to symbolic names for enum types.
 */
typedef struct nmo_enum_value {
    const char *name;   /**< Symbolic name */
    int32_t value;      /**< Integer value */
} nmo_enum_value_t;

/**
 * @brief Type descriptor
 *
 * Describes a schema type (scalar, composite, or special).
 * Types form a DAG (struct can reference other struct types).
 */
struct nmo_schema_type {
    const char *name;           /**< Type name (e.g., "Vec3", "Transform") */
    nmo_type_kind_t kind;       /**< Type kind */
    size_t size;                /**< Size in bytes (0 for variable-size) */
    size_t align;               /**< Alignment requirement */
    
    /* For STRUCT: field definitions */
    const nmo_schema_field_t *fields;
    size_t field_count;
    
    /* For ARRAY/FIXED_ARRAY: element type */
    const nmo_schema_type_t *element_type;
    size_t array_length;        /**< For FIXED_ARRAY only */
    
    /* For ENUM: value mappings */
    const nmo_enum_value_t *enum_values;
    size_t enum_value_count;
    nmo_type_kind_t enum_base_type; /**< Usually NMO_TYPE_U32 or NMO_TYPE_I32 */
    
    /* Optional vtable for optimized read/write */
    const nmo_schema_vtable_t *vtable;
};

/**
 * @brief Field descriptor
 *
 * Describes a single field within a struct type.
 */
struct nmo_schema_field {
    const char *name;           /**< Field name */
    const nmo_schema_type_t *type; /**< Field type */
    size_t offset;              /**< Offset in parent struct */
    uint32_t annotations;       /**< Bitset of nmo_field_annotation_t */
    uint32_t since_version;     /**< Version when field was added (0 = always) */
    uint32_t deprecated_version; /**< Version when deprecated (0 = never) */
};

/* =============================================================================
 * VTABLE FOR FAST PATH
 * ============================================================================= */

/**
 * @brief Schema vtable for optimized read/write operations
 *
 * Types can provide custom read/write implementations for performance.
 * When vtable is present, these functions take precedence over generic
 * reflection-based operations.
 */
struct nmo_schema_vtable {
    /**
     * @brief Read struct from chunk
     * @param type Type descriptor
     * @param chunk Chunk to read from
     * @param arena Arena for allocations
     * @param out_ptr Output pointer (must point to valid memory of type->size)
     * @return Result with error on failure
     */
    nmo_result_t (*read)(const nmo_schema_type_t *type, 
                         nmo_chunk_t *chunk,
                         nmo_arena_t *arena,
                         void *out_ptr);
    
    /**
     * @brief Write struct to chunk
     * @param type Type descriptor
     * @param chunk Chunk to write to
     * @param in_ptr Input pointer (must point to valid data)
     * @return Result with error on failure
     */
    nmo_result_t (*write)(const nmo_schema_type_t *type,
                          nmo_chunk_t *chunk,
                          const void *in_ptr);
    
    /**
     * @brief Validate struct data
     * @param type Type descriptor
     * @param data Data to validate
     * @param arena Arena for error messages
     * @return Result with error if validation fails
     */
    nmo_result_t (*validate)(const nmo_schema_type_t *type,
                             const void *data,
                             nmo_arena_t *arena);
};

/* =============================================================================
 * REFLECTION API
 * ============================================================================= */

/**
 * @brief Read struct from chunk using schema
 *
 * Generic reflection-based reading. Traverses fields according to type
 * descriptor and calls appropriate chunk read operations.
 * If type->vtable is present and provides read(), uses fast path instead.
 *
 * @param type Type descriptor (must be NMO_TYPE_STRUCT)
 * @param chunk Chunk to read from
 * @param arena Arena for allocations (strings, arrays, sub-structs)
 * @param out_struct Output buffer (must be type->size bytes)
 * @return Result with error on failure
 */
NMO_API nmo_result_t nmo_schema_read_struct(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_struct);

/**
 * @brief Write struct to chunk using schema
 *
 * Generic reflection-based writing. Symmetric to nmo_schema_read_struct.
 * If type->vtable is present and provides write(), uses fast path instead.
 *
 * @param type Type descriptor (must be NMO_TYPE_STRUCT)
 * @param chunk Chunk to write to
 * @param in_struct Input data (must be valid for type)
 * @return Result with error on failure
 */
NMO_API nmo_result_t nmo_schema_write_struct(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_struct);

/**
 * @brief Validate struct data against schema
 *
 * Checks data consistency (e.g., array bounds, enum values, null pointers).
 * If type->vtable provides validate(), uses custom validation.
 *
 * @param type Type descriptor
 * @param data Data to validate
 * @param arena Arena for error message allocations
 * @return Result with error if validation fails
 */
NMO_API nmo_result_t nmo_schema_validate(
    const nmo_schema_type_t *type,
    const void *data,
    nmo_arena_t *arena);

/* =============================================================================
 * TYPE UTILITIES
 * ============================================================================= */

/**
 * @brief Get type kind name
 * @param kind Type kind
 * @return Human-readable name (e.g., "u32", "struct", "array")
 */
NMO_API const char *nmo_type_kind_name(nmo_type_kind_t kind);

/**
 * @brief Get size for scalar types
 * @param kind Type kind (must be scalar)
 * @return Size in bytes, or 0 for non-scalar types
 */
NMO_API size_t nmo_type_scalar_size(nmo_type_kind_t kind);

/**
 * @brief Check if type is variable-size
 * @param type Type descriptor
 * @return 1 if variable-size (string, array, binary), 0 otherwise
 */
NMO_API int nmo_type_is_variable_size(const nmo_schema_type_t *type);

/**
 * @brief Get annotation flag name
 * @param annotation Single annotation flag
 * @return Human-readable name (e.g., "since", "position", "seconds")
 */
NMO_API const char *nmo_annotation_name(nmo_field_annotation_t annotation);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_H */
