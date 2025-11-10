/**
 * @file header1.c
 * @brief NMO Header1 (object descriptors) implementation
 */

#include "format/nmo_header1.h"

/**
 * Create Header1 context
 */
nmo_header1_t* nmo_header1_create(void) {
    return NULL;
}

/**
 * Destroy Header1 context
 */
void nmo_header1_destroy(nmo_header1_t* header1) {
    (void)header1;
}

/**
 * Parse Header1 from IO
 */
nmo_result_t nmo_header1_parse(nmo_header1_t* header1, void* io) {
    (void)header1;
    (void)io;
    return nmo_result_ok();
}

/**
 * Write Header1 to IO
 */
nmo_result_t nmo_header1_write(const nmo_header1_t* header1, void* io) {
    (void)header1;
    (void)io;
    return nmo_result_ok();
}

/**
 * Add object descriptor
 */
nmo_result_t nmo_header1_add_descriptor(nmo_header1_t* header1, const nmo_object_descriptor_t* descriptor) {
    (void)header1;
    (void)descriptor;
    return nmo_result_ok();
}

/**
 * Get object descriptor count
 */
uint32_t nmo_header1_get_descriptor_count(const nmo_header1_t* header1) {
    (void)header1;
    return 0;
}

/**
 * Get object descriptor by index
 */
nmo_result_t nmo_header1_get_descriptor(
    const nmo_header1_t* header1, uint32_t index, nmo_object_descriptor_t* out_descriptor) {
    (void)header1;
    (void)index;
    if (out_descriptor != NULL) {
        out_descriptor->id = 0;
        out_descriptor->manager_id = 0;
        out_descriptor->object_flags = 0;
        out_descriptor->data_offset = 0;
        out_descriptor->data_size = 0;
    }
    return nmo_result_ok();
}

/**
 * Get object descriptor by ID
 */
nmo_result_t nmo_header1_get_descriptor_by_id(
    const nmo_header1_t* header1, uint32_t object_id, nmo_object_descriptor_t* out_descriptor) {
    (void)header1;
    (void)object_id;
    if (out_descriptor != NULL) {
        out_descriptor->id = 0;
        out_descriptor->manager_id = 0;
        out_descriptor->object_flags = 0;
        out_descriptor->data_offset = 0;
        out_descriptor->data_size = 0;
    }
    return nmo_result_ok();
}
