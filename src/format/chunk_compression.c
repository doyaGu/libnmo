// chunk_compression.c - Chunk compression/decompression helpers

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "core/nmo_allocator.h"
#include <miniz.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Compression
// =============================================================================

static int clamp_compression_level(int level) {
    if (level < 0) {
        return 6;
    }
    if (level > 9) {
        return 9;
    }
    return level;
}

static nmo_result_t chunk_generate_compressed_bytes(nmo_chunk_t *chunk,
                                                    int compression_level,
                                                    uint8_t **out_bytes,
                                                    size_t *out_size) {
    if (chunk == NULL || out_bytes == NULL || out_size == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    size_t src_size = chunk->data_size * sizeof(uint32_t);
    if (src_size == 0) {
        *out_bytes = NULL;
        *out_size = 0;
        return nmo_result_ok();
    }

    mz_ulong dest_capacity = mz_compressBound((mz_ulong)src_size);
    nmo_allocator_t alloc = nmo_allocator_default();
    uint8_t *buffer = (uint8_t *)nmo_alloc(&alloc, (size_t)dest_capacity, 16);
    if (buffer == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate compression scratch buffer"));
    }

    mz_ulong dest_size = dest_capacity;
    int mz_result = mz_compress2(buffer, &dest_size,
                                 (const unsigned char *)chunk->data, (mz_ulong)src_size,
                                 clamp_compression_level(compression_level));
    if (mz_result != MZ_OK) {
        nmo_free(&alloc, buffer);
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INTERNAL,
                                          NMO_SEVERITY_ERROR, "Compression failed"));
    }

    *out_bytes = buffer;
    *out_size = (size_t)dest_size;
    return nmo_result_ok();
}

static nmo_result_t chunk_commit_compressed_payload(nmo_chunk_t *chunk,
                                                    const uint8_t *compressed,
                                                    size_t compressed_size,
                                                    size_t original_dwords) {
    size_t dest_dwords = (compressed_size + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    size_t dest_bytes = dest_dwords * sizeof(uint32_t);

    uint32_t *new_data = (uint32_t *)nmo_arena_alloc(chunk->arena, dest_bytes, sizeof(uint32_t));
    if (new_data == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_NOMEM,
                                          NMO_SEVERITY_ERROR, "Failed to allocate packed data buffer"));
    }

    memcpy(new_data, compressed, compressed_size);
    if (dest_bytes > compressed_size) {
        memset(((uint8_t *)new_data) + compressed_size, 0, dest_bytes - compressed_size);
    }

    chunk->data = new_data;
    chunk->data_size = dest_dwords;
    chunk->data_capacity = dest_dwords;
    chunk->chunk_options |= NMO_CHUNK_OPTION_PACKED;
    chunk->unpack_size = original_dwords;
    chunk->compressed_size = compressed_size;
    chunk->uncompressed_size = original_dwords * sizeof(uint32_t);
    chunk->is_compressed = 1;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_compress(nmo_chunk_t *chunk, int compression_level) {
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

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    nmo_result_t result = chunk_generate_compressed_bytes(chunk, compression_level,
                                                          &compressed, &compressed_size);
    if (result.code != NMO_OK) {
        return result;
    }

    if (compressed == NULL || compressed_size == 0) {
        if (compressed != NULL) {
            nmo_allocator_t alloc = nmo_allocator_default();
            nmo_free(&alloc, compressed);
        }
        return nmo_result_ok();
    }

    size_t original_dwords = chunk->data_size;
    result = chunk_commit_compressed_payload(chunk, compressed, compressed_size, original_dwords);

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, compressed);
    return result;
}

nmo_result_t nmo_chunk_compress_if_beneficial(nmo_chunk_t *chunk,
                                              int compression_level,
                                              float min_ratio) {
    if (min_ratio <= 0.0f || min_ratio > 1.0f) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "min_ratio must be within (0,1]"));
    }

    if (chunk == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR, "Invalid chunk argument"));
    }

    if (chunk->chunk_options & NMO_CHUNK_OPTION_PACKED) {
        return nmo_result_ok();
    }

    size_t original_size = chunk->data_size * sizeof(uint32_t);
    if (original_size == 0) {
        return nmo_result_ok();
    }

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    nmo_result_t result = chunk_generate_compressed_bytes(chunk, compression_level,
                                                          &compressed, &compressed_size);
    if (result.code != NMO_OK) {
        return result;
    }

    double ratio = (double)compressed_size / (double)original_size;
    if (compressed == NULL || compressed_size == 0) {
        if (compressed != NULL) {
            nmo_allocator_t alloc = nmo_allocator_default();
            nmo_free(&alloc, compressed);
        }
        return nmo_result_ok();
    }

    int should_keep = (ratio <= (double)min_ratio);
    if (!should_keep) {
        nmo_allocator_t alloc = nmo_allocator_default();
        nmo_free(&alloc, compressed);
        return nmo_result_ok();
    }

    result = chunk_commit_compressed_payload(chunk, compressed, compressed_size, chunk->data_size);
    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, compressed);
    return result;
}

nmo_result_t nmo_chunk_decompress(nmo_chunk_t *chunk) {
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

    chunk->data = decompressed;
    chunk->data_size = chunk->unpack_size;
    chunk->data_capacity = chunk->unpack_size;
    chunk->chunk_options &= ~NMO_CHUNK_OPTION_PACKED;
    chunk->compressed_size = 0;
    chunk->uncompressed_size = chunk->data_size * sizeof(uint32_t);
    chunk->is_compressed = 0;
    chunk->unpack_size = 0;

    return nmo_result_ok();
}

nmo_result_t nmo_chunk_pack(nmo_chunk_t *chunk, int compression_level) {
    return nmo_chunk_compress(chunk, compression_level);
}

nmo_result_t nmo_chunk_unpack(nmo_chunk_t *chunk) {
    return nmo_chunk_decompress(chunk);
}
