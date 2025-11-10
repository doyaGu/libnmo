/**
 * @file schema.c
 * @brief Schema descriptors for object structure validation implementation
 */

#include "schema/nmo_schema.h"

/**
 * Create schema
 */
nmo_schema_t* nmo_schema_create(uint32_t schema_id, const char* name, uint32_t struct_size) {
    (void)schema_id;
    (void)name;
    (void)struct_size;
    return NULL;
}

/**
 * Destroy schema
 */
void nmo_schema_destroy(nmo_schema_t* schema) {
    (void)schema;
}

/**
 * Add field to schema
 */
nmo_result_t nmo_schema_add_field(nmo_schema_t* schema, const nmo_field_descriptor_t* field) {
    (void)schema;
    (void)field;
    return nmo_result_ok();
}

/**
 * Get schema info
 */
nmo_result_t nmo_schema_get_info(const nmo_schema_t* schema, nmo_schema_info_t* out_info) {
    (void)schema;
    if (out_info != NULL) {
        out_info->schema_id = 0;
        out_info->name = NULL;
        out_info->struct_size = 0;
        out_info->field_count = 0;
        out_info->version = 0;
    }
    return nmo_result_ok();
}

/**
 * Get field count
 */
uint32_t nmo_schema_get_field_count(const nmo_schema_t* schema) {
    (void)schema;
    return 0;
}

/**
 * Get field by index
 */
nmo_result_t nmo_schema_get_field(
    const nmo_schema_t* schema, uint32_t index, nmo_field_descriptor_t* out_field) {
    (void)schema;
    (void)index;
    if (out_field != NULL) {
        out_field->name = NULL;
        out_field->type = 0;
        out_field->offset = 0;
        out_field->size = 0;
        out_field->flags = 0;
    }
    return nmo_result_ok();
}

/**
 * Get field by name
 */
nmo_result_t nmo_schema_get_field_by_name(
    const nmo_schema_t* schema, const char* field_name, nmo_field_descriptor_t* out_field) {
    (void)schema;
    (void)field_name;
    if (out_field != NULL) {
        out_field->name = NULL;
        out_field->type = 0;
        out_field->offset = 0;
        out_field->size = 0;
        out_field->flags = 0;
    }
    return nmo_result_ok();
}

/**
 * Validate data against schema
 */
nmo_result_t nmo_schema_validate(const nmo_schema_t* schema, const void* data, size_t data_size) {
    (void)schema;
    (void)data;
    (void)data_size;
    return nmo_result_ok();
}
