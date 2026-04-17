// chunk_compression.c - Chunk compression/decompression helpers

#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "core/nmo_allocator.h"
#include "core/nmo_utils.h"
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

static nmo_status_t chunk_generate_compressed_bytes(nmo_chunk_t *chunk,
                                                    int compression_level,
                                                    uint8_t **out_bytes,
                                                    size_t *out_size) {
    NMO_CHUNK_CHECK_ARGS2(chunk, out_bytes, out_size, "Invalid arguments");

    size_t src_size = chunk->data.count * sizeof(uint32_t);
    if (src_size == 0) {
        *out_bytes = NULL;
        *out_size = 0;
        NMO_RETURN_OK();
    }

    const uint32_t *src_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    mz_ulong dest_capacity = mz_compressBound((mz_ulong)src_size);
    nmo_allocator_t alloc = nmo_allocator_default();
    uint8_t *buffer = (uint8_t *)nmo_alloc(&alloc, (size_t)dest_capacity, 16);
    if (buffer == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate compression scratch buffer");
    }

    mz_ulong dest_size = dest_capacity;
    int mz_result = mz_compress2(buffer, &dest_size,
                                 (const unsigned char *)src_data, (mz_ulong)src_size,
                                 clamp_compression_level(compression_level));
    if (mz_result != MZ_OK) {
        nmo_free(&alloc, buffer);
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Compression failed");
    }

    *out_bytes = buffer;
    *out_size = (size_t)dest_size;
    NMO_RETURN_OK();
}

static nmo_status_t chunk_commit_compressed_payload(nmo_chunk_t *chunk,
                                                    const uint8_t *compressed,
                                                    size_t compressed_size,
                                                    size_t original_dwords) {
    size_t dest_dwords = (compressed_size + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    size_t dest_bytes;
    if (!nmo_safe_mul_size(dest_dwords, sizeof(uint32_t), &dest_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Compressed payload size overflow");
    }

    nmo_status_t result = nmo_arena_array_resize(&chunk->data, dest_dwords);
    NMO_RETURN_IF_ERROR(result);

    uint32_t *new_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    memcpy(new_data, compressed, compressed_size);
    if (dest_bytes > compressed_size) {
        memset(((uint8_t *)new_data) + compressed_size, 0, dest_bytes - compressed_size);
    }

    chunk->chunk_options |= NMO_CHUNK_OPTION_PACKED;
    chunk->unpack_size = original_dwords;
    chunk->compressed_size = compressed_size;
    size_t uncompressed_bytes;
    if (!nmo_safe_mul_size(original_dwords, sizeof(uint32_t), &uncompressed_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Uncompressed size overflow");
    }
    chunk->uncompressed_size = uncompressed_bytes;
    chunk->is_compressed = 1;

    NMO_RETURN_OK();
}

nmo_status_t nmo_chunk_compress(nmo_chunk_t *chunk, int compression_level) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    if (chunk->data.count == 0) {
        NMO_RETURN_OK();
    }

    if (chunk->chunk_options & NMO_CHUNK_OPTION_PACKED) {
        NMO_RETURN_OK();
    }

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    nmo_status_t result = chunk_generate_compressed_bytes(chunk, compression_level,
                                                          &compressed, &compressed_size);
    NMO_RETURN_IF_ERROR(result);

    if (compressed == NULL || compressed_size == 0) {
        if (compressed != NULL) {
            nmo_allocator_t alloc = nmo_allocator_default();
            nmo_free(&alloc, compressed);
        }
        NMO_RETURN_OK();
    }

    size_t original_dwords = chunk->data.count;
    result = chunk_commit_compressed_payload(chunk, compressed, compressed_size, original_dwords);

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, compressed);
    return result;
}

nmo_status_t nmo_chunk_compress_if_beneficial(nmo_chunk_t *chunk,
                                              int compression_level,
                                              float min_ratio) {
    if (min_ratio <= 0.0f || min_ratio > 1.0f) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("min_ratio must be within (0,1]");
    }

    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    if (chunk->chunk_options & NMO_CHUNK_OPTION_PACKED) {
        NMO_RETURN_OK();
    }

    size_t original_size;
    if (!nmo_safe_mul_size(chunk->data.count, sizeof(uint32_t), &original_size)) {
        NMO_CHUNK_RETURN_INVALID_ARGUMENT("Original data size overflow");
    }
    if (original_size == 0) {
        NMO_RETURN_OK();
    }

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    nmo_status_t result = chunk_generate_compressed_bytes(chunk, compression_level,
                                                          &compressed, &compressed_size);
    NMO_RETURN_IF_ERROR(result);

    double ratio = (double)compressed_size / (double)original_size;
    if (compressed == NULL || compressed_size == 0) {
        if (compressed != NULL) {
            nmo_allocator_t alloc = nmo_allocator_default();
            nmo_free(&alloc, compressed);
        }
        NMO_RETURN_OK();
    }

    int should_keep = (ratio <= (double)min_ratio);
    if (!should_keep) {
        nmo_allocator_t alloc = nmo_allocator_default();
        nmo_free(&alloc, compressed);
        NMO_RETURN_OK();
    }

    result = chunk_commit_compressed_payload(chunk, compressed, compressed_size, chunk->data.count);
    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, compressed);
    return result;
}

nmo_status_t nmo_chunk_decompress(nmo_chunk_t *chunk) {
    NMO_CHUNK_CHECK_ARG(chunk, "Invalid chunk argument");

    if (!(chunk->chunk_options & NMO_CHUNK_OPTION_PACKED)) {
        NMO_RETURN_OK();
    }

    if (chunk->unpack_size == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "No unpack size specified");
    }

    const uint32_t *src_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t available_bytes;
    if (!nmo_safe_mul_size(chunk->data.count, sizeof(uint32_t), &available_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Chunk data size overflow");
    }
    size_t used_bytes = (chunk->compressed_size > 0) ? chunk->compressed_size : available_bytes;
    if (used_bytes > available_bytes) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Compressed size exceeds buffer");
    }
    mz_ulong src_len = (mz_ulong) used_bytes;

    size_t dest_len_check;
    if (!nmo_safe_mul_size(chunk->unpack_size, sizeof(uint32_t), &dest_len_check)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Decompression buffer size overflow");
    }
    mz_ulong dest_len = (mz_ulong)dest_len_check;
    uint32_t *decompressed = (uint32_t *) nmo_arena_alloc(chunk->arena,
                                                          dest_len, sizeof(uint32_t));
    if (!decompressed) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate decompression buffer");
    }

    // Decompress
    int result = mz_uncompress((unsigned char *) decompressed, &dest_len,
                               (const unsigned char *) src_data, src_len);

    if (result != MZ_OK) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR, "Decompression failed");
    }

    // Verify decompressed size
    if (dest_len != (mz_ulong)dest_len_check) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Decompressed size mismatch");
    }

    nmo_status_t set_result = nmo_arena_array_set_data(&chunk->data, decompressed, chunk->unpack_size);
    NMO_RETURN_IF_ERROR(set_result);
    chunk->chunk_options &= ~NMO_CHUNK_OPTION_PACKED;
    chunk->compressed_size = 0;
    size_t final_size;
    if (!nmo_safe_mul_size(chunk->data.count, sizeof(uint32_t), &final_size)) {
        NMO_RETURN_ERROR(NMO_ERR_CORRUPT, NMO_SEVERITY_ERROR, "Final decompressed size overflow");
    }
    chunk->uncompressed_size = final_size;
    chunk->is_compressed = 0;
    chunk->unpack_size = 0;

    NMO_RETURN_OK();
}
