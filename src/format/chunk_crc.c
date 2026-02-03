// chunk_crc.c - CRC computation
// Implements: compute_crc

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <miniz.h>

// =============================================================================
// CRC
// =============================================================================

nmo_status_t nmo_chunk_compute_crc(nmo_chunk_t *chunk,
                                   uint32_t initial_crc,
                                   uint32_t *out_crc) {
    NMO_CHUNK_CHECK_ARGS(chunk, out_crc, "Invalid arguments");

    if (chunk->data.count == 0) {
        *out_crc = initial_crc;
        NMO_RETURN_OK();
    }

    // Compute Adler32 CRC
    size_t byte_size = chunk->data.count * sizeof(uint32_t);
    const uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    mz_ulong crc = mz_adler32(initial_crc,
                              (const unsigned char *) data,
                              byte_size);

    *out_crc = (uint32_t) crc;
    NMO_RETURN_OK();
}
