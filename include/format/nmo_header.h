/**
 * @file nmo_header.h
 * @brief NMO file header parsing
 */

#ifndef NMO_HEADER_H
#define NMO_HEADER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NMO file header
 */
typedef struct nmo_header nmo_header_t;

/**
 * @brief File header magic and version info
 */
typedef struct {
    char magic[4];           /* NMO file magic */
    uint32_t version;        /* File format version */
    uint32_t flags;          /* File flags */
    uint64_t file_size;      /* Total file size */
    uint32_t header_size;    /* Header size in bytes */
} nmo_file_header_info_t;

/**
 * Create header context
 * @return Header context or NULL on error
 */
NMO_API nmo_header_t* nmo_header_create(void);

/**
 * Destroy header context
 * @param header Header context
 */
NMO_API void nmo_header_destroy(nmo_header_t* header);

/**
 * Parse header from IO
 * @param header Header context
 * @param io IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header_parse(nmo_header_t* header, void* io);

/**
 * Write header to IO
 * @param header Header context
 * @param io IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header_write(const nmo_header_t* header, void* io);

/**
 * Get header info
 * @param header Header context
 * @param out_info Output header info
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header_get_info(const nmo_header_t* header, nmo_file_header_info_t* out_info);

/**
 * Get header size
 * @param header Header context
 * @return Header size in bytes
 */
NMO_API uint32_t nmo_header_get_size(const nmo_header_t* header);

/**
 * Validate header
 * @param header Header context
 * @return NMO_OK if valid
 */
NMO_API nmo_result_t nmo_header_validate(const nmo_header_t* header);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HEADER_H */
