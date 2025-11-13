// chunk_crc.c - CRC computation
// Implements: compute_crc

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <miniz.h>

// =============================================================================
// CRC
// =============================================================================

nmo_result_t nmo_chunk_compute_crc(nmo_chunk_t *chunk,
                                   uint32_t initial_crc,
                                   uint32_t *out_crc) {
    if (!chunk || !out_crc) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    if (chunk->data_size == 0) {
        *out_crc = initial_crc;
        return nmo_result_ok();
    }

    // Compute Adler32 CRC
    size_t byte_size = chunk->data_size * sizeof(uint32_t);
    mz_ulong crc = mz_adler32(initial_crc,
                              (const unsigned char *) chunk->data,
                              byte_size);

    *out_crc = (uint32_t) crc;
    return nmo_result_ok();
}
