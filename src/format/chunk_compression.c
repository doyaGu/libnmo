// chunk_compression.c - Chunk compression/decompression
// Implements: pack, unpack

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include <miniz.h>
#include <string.h>

// =============================================================================
// Compression
// =============================================================================

nmo_result_t nmo_chunk_pack(nmo_chunk_t *chunk, int compression_level) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (chunk->data_size == 0) {
        return nmo_result_ok();
    }

    if (chunk->chunk_options & NMO_CHUNK_OPTION_PACKED) {
        return nmo_result_ok();
    }

    // Clamp compression level
    if (compression_level < 0) compression_level = 6;
    if (compression_level > 9) compression_level = 9;

    // Calculate maximum compressed size
    mz_ulong src_len = chunk->data_size * sizeof(uint32_t);
    mz_ulong dest_len = mz_compressBound(src_len);

    // Allocate temporary buffer
    uint8_t *compressed = (uint8_t *) nmo_arena_alloc(chunk->arena, dest_len, 4);
    if (!compressed) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate compression buffer"));
    }

    // Compress
    int result = mz_compress2(compressed, &dest_len,
                              (const unsigned char *) chunk->data, src_len,
                              compression_level);

    if (result != MZ_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Compression failed"));
    }

    // Only use compressed data if smaller
    if (dest_len < src_len) {
        chunk->unpack_size = chunk->data_size;

        size_t compressed_dwords = (dest_len + 3) / 4;

        uint32_t *new_data = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                          compressed_dwords * sizeof(uint32_t), sizeof(uint32_t));
        if (!new_data) {
            return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                              NMO_SEVERITY_ERROR, "Failed to allocate packed data buffer"));
        }

        memcpy(new_data, compressed, dest_len);

        chunk->data = new_data;
        chunk->data_size = compressed_dwords;
        chunk->data_capacity = compressed_dwords;
        chunk->chunk_options |= NMO_CHUNK_OPTION_PACKED;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_unpack(nmo_chunk_t *chunk) {
    if (!chunk) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (!(chunk->chunk_options & NMO_CHUNK_OPTION_PACKED)) {
        return nmo_result_ok();
    }

    if (chunk->unpack_size == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_STATE,
                                          NMO_SEVERITY_ERROR, "No unpack size specified"));
    }

    // Allocate buffer for decompressed data
    mz_ulong dest_len = chunk->unpack_size * sizeof(uint32_t);
    uint32_t *decompressed = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                          dest_len, sizeof(uint32_t));
    if (!decompressed) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate decompression buffer"));
    }

    // Decompress
    mz_ulong src_len = chunk->data_size * sizeof(uint32_t);
    int result = mz_uncompress((unsigned char *) decompressed, &dest_len,
                               (const unsigned char *) chunk->data, src_len);

    if (result != MZ_OK) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Decompression failed"));
    }

    // Verify decompressed size
    if (dest_len != chunk->unpack_size * sizeof(uint32_t)) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_CORRUPT,
                                          NMO_SEVERITY_ERROR, "Decompressed size mismatch"));
    }

    // Update chunk
    chunk->data = decompressed;
    chunk->data_size = chunk->unpack_size;
    chunk->data_capacity = chunk->unpack_size;
    chunk->chunk_options &= ~NMO_CHUNK_OPTION_PACKED;
    chunk->unpack_size = 0;

    return nmo_result_ok();
}
