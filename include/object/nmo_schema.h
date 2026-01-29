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
struct nmo_chunk;

/**
 * @brief Schema type category
 */
typedef enum nmo_schema_category {
    NMO_SCHEMA_CATEGORY_PRIMITIVE = 0,  /**< Primitive type (int, float, etc) */
    NMO_SCHEMA_CATEGORY_STRUCT,         /**< Composite structure */
    NMO_SCHEMA_CATEGORY_ENUM,           /**< Enumeration */
    NMO_SCHEMA_CATEGORY_FLAGS,          /**< Bit flags */
    NMO_SCHEMA_CATEGORY_OBJECT,         /**< CKObject-derived class */
    NMO_SCHEMA_CATEGORY_CUSTOM          /**< Custom/plugin type */
} nmo_schema_category_t;

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
 * @brief Field descriptor
 */
struct nmo_schema_field {
    const char *name;                   /**< Field name */
    nmo_guid_t type_guid;              /**< Field type GUID */
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
    /**
     * @brief Serialize instance to chunk
     * @param instance Pointer to instance
     * @param chunk Chunk to write to
     * @param type Type descriptor
     * @param context User context
     * @return Result
     */
    nmo_result_t (*serialize)(const void *instance, 
                             struct nmo_chunk *chunk,
                             const nmo_schema_type_t *type,
                             void *context);
    
    /**
     * @brief Deserialize instance from chunk
     * @param instance Pointer to instance
     * @param chunk Chunk to read from
     * @param type Type descriptor
     * @param context User context
     * @return Result
     */
    nmo_result_t (*deserialize)(void *instance,
                               struct nmo_chunk *chunk,
                               const nmo_schema_type_t *type,
                               void *context);
    
    /**
     * @brief Validate instance
     * @param instance Pointer to instance
     * @param type Type descriptor
     * @param context User context
     * @return Result
     */
    nmo_result_t (*validate)(const void *instance,
                            const nmo_schema_type_t *type,
                            void *context);
};

/**
 * @brief Schema type descriptor
 */
struct nmo_schema_type {
    nmo_guid_t guid;                   /**< Type GUID (primary key) */
    const char *name;                  /**< Type name */
    uint32_t class_id;                 /**< Virtools CK_CLASSID (for objects) */
    nmo_schema_category_t category;    /**< Type category */
    size_t size;                       /**< Size in bytes */
    size_t alignment;                  /**< Alignment requirement */
    
    nmo_guid_t base_type;             /**< Parent type GUID (for inheritance) */
    
    const nmo_schema_field_t *fields; /**< Field descriptors */
    size_t field_count;                /**< Number of fields */
    
    const nmo_schema_vtable_t *vtable;/**< Virtual table (optional) */
    
    uint32_t version;                  /**< Schema version */
    uint32_t flags;                    /**< Type flags */
};

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_H */
