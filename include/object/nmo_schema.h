/**
 * @file nmo_schema.h
 * @brief Core schema type definitions for libnmo
 *
 * This file provides the foundational types for the schema system.
 * It defines the structures used for type metadata, field descriptors,
 * serialization/deserialization interfaces, and schema validation.
 */

#ifndef NMO_SCHEMA_H
#define NMO_SCHEMA_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_type nmo_schema_type_t;
typedef struct nmo_schema_field nmo_schema_field_t;
typedef struct nmo_schema_vtable nmo_schema_vtable_t;
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_param_meta nmo_param_meta_t;
struct nmo_chunk;

/**
 * @brief Schema type kind
 */
typedef enum nmo_type_kind {
    NMO_TYPE_U8 = 0,
    NMO_TYPE_U16,
    NMO_TYPE_U32,
    NMO_TYPE_U64,
    NMO_TYPE_I8,
    NMO_TYPE_I16,
    NMO_TYPE_I32,
    NMO_TYPE_I64,
    NMO_TYPE_F32,
    NMO_TYPE_F64,
    NMO_TYPE_BOOL,
    NMO_TYPE_STRING,
    NMO_TYPE_STRUCT,
    NMO_TYPE_ARRAY,
    NMO_TYPE_FIXED_ARRAY,
    NMO_TYPE_BINARY,
    NMO_TYPE_RESOURCE_REF,
    NMO_TYPE_ENUM
} nmo_type_kind_t;

/**
 * @brief Field annotation flags
 */
typedef enum nmo_field_annotations {
    NMO_FIELD_NONE        = 0x0000,
    NMO_FIELD_OPTIONAL    = 0x0001,  /**< Field is optional */
    NMO_FIELD_DEPRECATED  = 0x0002,  /**< Field is deprecated */
    NMO_FIELD_ID          = 0x0004,  /**< Field is an ID reference */
    NMO_FIELD_GUID        = 0x0008,  /**< Field is a GUID */
    NMO_FIELD_POINTER     = 0x0010,  /**< Field is a pointer */
    NMO_FIELD_ARRAY       = 0x0020,  /**< Field is an array */
    NMO_FIELD_COMPUTED    = 0x0040   /**< Field is computed (not serialized) */
} nmo_field_annotations_t;

/**
 * @brief Enum value descriptor
 */
typedef struct nmo_enum_value {
    const char *name;                   /**< Enum value name */
    int32_t value;                      /**< Enum value */
} nmo_enum_value_t;

/**
 * @brief Field descriptor
 */
struct nmo_schema_field {
    const char *name;                   /**< Field name */
    const nmo_schema_type_t *type;      /**< Field type */
    size_t offset;                      /**< Offset in bytes from struct start */
    uint32_t annotations;               /**< Field annotation flags */
    uint32_t since_version;             /**< Version when field was added */
    uint32_t deprecated_version;        /**< Version when field was deprecated */
    uint32_t removed_version;           /**< Version when field was removed */
};

/**
 * @brief Schema virtual table for custom serialization
 */
struct nmo_schema_vtable {
    nmo_result_t (*read)(
        const nmo_schema_type_t *type,
        struct nmo_chunk *chunk,
        nmo_arena_t *arena,
        void *out_ptr);

    nmo_result_t (*write)(
        const nmo_schema_type_t *type,
        struct nmo_chunk *chunk,
        const void *in_ptr,
        nmo_arena_t *arena);

    nmo_result_t (*validate)(
        const nmo_schema_type_t *type,
        const void *instance,
        void *context);
};

/**
 * @brief Schema type descriptor
 */
struct nmo_schema_type {
    const char *name;                       /**< Type name */
    nmo_type_kind_t kind;                   /**< Kind */
    size_t size;                            /**< Size in bytes */
    size_t align;                           /**< Alignment requirement */

    nmo_guid_t guid;                        /**< Optional GUID */
    uint32_t class_id;                      /**< Optional class ID */
    nmo_guid_t base_type;                   /**< Parent type GUID (optional) */

    const nmo_schema_field_t *fields;       /**< Field descriptors */
    size_t field_count;                     /**< Number of fields */

    const nmo_enum_value_t *enum_values;    /**< Enum value table */
    size_t enum_value_count;                /**< Enum value count */
    nmo_type_kind_t enum_base_type;         /**< Enum storage type */

    const nmo_schema_type_t *element_type;  /**< Array element type */
    size_t array_length;                    /**< Fixed array length */

    const nmo_schema_vtable_t *vtable;      /**< Virtual table (optional) */

    uint32_t since_version;                 /**< Version when type added */
    uint32_t deprecated_version;            /**< Version when deprecated */
    uint32_t removed_version;               /**< Version when removed */

    const nmo_param_meta_t *param_meta;     /**< Optional parameter metadata */
};

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_H */
