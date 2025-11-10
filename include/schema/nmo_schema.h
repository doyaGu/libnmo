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

/**
 * @brief Schema descriptor
 */
typedef struct nmo_schema nmo_schema_t;

/**
 * @brief Field descriptor
 */
typedef struct {
    const char* name;        /* Field name */
    uint32_t type;           /* Field type */
    uint32_t offset;         /* Field offset in structure */
    uint32_t size;           /* Field size */
    uint32_t flags;          /* Field flags */
} nmo_field_descriptor_t;

/**
 * @brief Schema info
 */
typedef struct {
    uint32_t schema_id;      /* Schema ID */
    const char* name;        /* Schema name */
    uint32_t struct_size;    /* Size of structure */
    uint32_t field_count;    /* Number of fields */
    uint32_t version;        /* Schema version */
} nmo_schema_info_t;

/**
 * Create schema
 * @param schema_id Schema ID
 * @param name Schema name
 * @param struct_size Size of structure
 * @return Schema or NULL on error
 */
NMO_API nmo_schema_t* nmo_schema_create(uint32_t schema_id, const char* name, uint32_t struct_size);

/**
 * Destroy schema
 * @param schema Schema to destroy
 */
NMO_API void nmo_schema_destroy(nmo_schema_t* schema);

/**
 * Add field to schema
 * @param schema Schema
 * @param field Field descriptor
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_schema_add_field(nmo_schema_t* schema, const nmo_field_descriptor_t* field);

/**
 * Get schema info
 * @param schema Schema
 * @param out_info Output schema info
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_schema_get_info(const nmo_schema_t* schema, nmo_schema_info_t* out_info);

/**
 * Get field count
 * @param schema Schema
 * @return Number of fields
 */
NMO_API uint32_t nmo_schema_get_field_count(const nmo_schema_t* schema);

/**
 * Get field by index
 * @param schema Schema
 * @param index Field index
 * @param out_field Output field descriptor
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_schema_get_field(
    const nmo_schema_t* schema, uint32_t index, nmo_field_descriptor_t* out_field);

/**
 * Get field by name
 * @param schema Schema
 * @param field_name Field name
 * @param out_field Output field descriptor
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_schema_get_field_by_name(
    const nmo_schema_t* schema, const char* field_name, nmo_field_descriptor_t* out_field);

/**
 * Validate data against schema
 * @param schema Schema
 * @param data Data to validate
 * @param data_size Data size
 * @return NMO_OK if valid
 */
NMO_API nmo_result_t nmo_schema_validate(const nmo_schema_t* schema, const void* data, size_t data_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCHEMA_H */
