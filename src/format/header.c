/**
 * @file header.c
 * @brief NMO file header parsing implementation
 */

#include "format/nmo_header.h"

/**
 * Create header context
 */
nmo_header_t* nmo_header_create(void) {
    return NULL;
}

/**
 * Destroy header context
 */
void nmo_header_destroy(nmo_header_t* header) {
    (void)header;
}

/**
 * Parse header from IO
 */
nmo_result_t nmo_header_parse(nmo_header_t* header, void* io) {
    (void)header;
    (void)io;
    return nmo_result_ok();
}

/**
 * Write header to IO
 */
nmo_result_t nmo_header_write(const nmo_header_t* header, void* io) {
    (void)header;
    (void)io;
    return nmo_result_ok();
}

/**
 * Get header info
 */
nmo_result_t nmo_header_get_info(const nmo_header_t* header, nmo_file_header_info_t* out_info) {
    (void)header;
    if (out_info != NULL) {
        out_info->magic[0] = 0;
        out_info->version = 0;
        out_info->flags = 0;
        out_info->file_size = 0;
        out_info->header_size = 0;
    }
    return nmo_result_ok();
}

/**
 * Get header size
 */
uint32_t nmo_header_get_size(const nmo_header_t* header) {
    (void)header;
    return 0;
}

/**
 * Validate header
 */
nmo_result_t nmo_header_validate(const nmo_header_t* header) {
    (void)header;
    return nmo_result_ok();
}
