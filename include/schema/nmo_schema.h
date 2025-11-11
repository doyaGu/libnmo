/**
 * @file nmo_schema.h
 * @brief Schema descriptors for object structure validation
 */

#ifndef NMO_SCHEMA_H
#define NMO_SCHEMA_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_object nmo_object;
typedef struct nmo_chunk_writer nmo_chunk_writer;
typedef struct nmo_chunk_parser nmo_chunk_parser;
typedef struct nmo_arena nmo_arena;

/**
 * @brief Field types for schema descriptors
 */
typedef enum {
    NMO_FIELD_INT8,
    NMO_FIELD_UINT8,
    NMO_FIELD_INT16,
    NMO_FIELD_UINT16,
    NMO_FIELD_INT32,
    NMO_FIELD_UINT32,
    NMO_FIELD_FLOAT,
    NMO_FIELD_STRING,
    NMO_FIELD_GUID,
    NMO_FIELD_OBJECT_ID,
    NMO_FIELD_STRUCT,
    NMO_FIELD_ARRAY,
} nmo_field_type;

/**
 * @brief Field descriptor
 */
typedef struct nmo_field_descriptor {
    const char*     name;               /**< Field name */
    nmo_field_type  type;               /**< Field type */
    size_t          offset;             /**< Field offset in structure */
    size_t          size;               /**< Field size */
    size_t          count;              /**< Element count (for arrays) */
    nmo_class_id    class_id;           /**< Class ID (for structs/objects) */
    const char*     validation_rule;    /**< Validation rule (optional) */
} nmo_field_descriptor;

/**
 * @brief Schema descriptor
 */
typedef struct nmo_schema_descriptor {
    nmo_class_id           class_id;        /**< Class ID */
    const char*            class_name;      /**< Class name */
    nmo_class_id           parent_class_id; /**< Parent class ID (0 for root) */
    nmo_field_descriptor*  fields;          /**< Field descriptors */
    size_t                 field_count;     /**< Number of fields */
    uint32_t               chunk_version;   /**< Chunk version */

    /* Serialization functions */
    void (*serialize_fn)(nmo_object* obj, nmo_chunk_writer* writer);
    void (*deserialize_fn)(nmo_object* obj, nmo_chunk_parser* parser);
} nmo_schema_descriptor;

/**
 * @brief Get field type name
 * @param type Field type
 * @return Type name string
 */
NMO_API const char* nmo_field_type_name(nmo_field_type type);

/**
 * @brief Get field type size
 * @param type Field type
 * @return Size in bytes (0 for variable-size types)
 */
NMO_API size_t nmo_field_type_size(nmo_field_type type);

/**
 * @brief Validate field descriptor
 * @param field Field descriptor
 * @return NMO_OK if valid
 */
NMO_API int nmo_field_descriptor_validate(const nmo_field_descriptor* field);

/**
 * @brief Validate schema descriptor
 * @param schema Schema descriptor
 * @return NMO_OK if valid
 */
NMO_API int nmo_schema_descriptor_validate(const nmo_schema_descriptor* schema);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_H */
