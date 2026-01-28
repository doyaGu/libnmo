/**
 * @file nmo_save_pipeline.h
 * @brief Two-phase commit save pipeline (Phase 1.4)
 *
 * Implements the Two-Phase Commit architecture for file saving:
 *
 * **Phase 1: Layout & Serialize**
 * - Serialize objects to memory buffer
 * - Build ID mapping (runtime_id -> file_index)
 * - Restore shadow blobs (chunk tails, included files)
 * - Calculate exact Data Section size
 *
 * **Phase 2: Pack & Commit**
 * - Write File Header (with known exact sizes)
 * - Optional zlib compression of Data Section
 * - Calculate full-file Adler-32 CRC
 * - Backfill CRC into File Header
 * - Atomic fsync
 *
 * This architecture solves:
 * - **Forward dependency paradox**: Header needs Data size, but Data isn't serialized yet
 * - **CRC calculation**: Cannot compute in single pass with streaming writes
 * - **Atomicity**: Partial writes don't corrupt files
 */

#ifndef NMO_SAVE_PIPELINE_H
#define NMO_SAVE_PIPELINE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session_t;
typedef struct nmo_save_context nmo_save_context_t;
typedef struct nmo_save_buffer nmo_save_buffer_t;

/**
 * @brief Save pipeline options
 */
typedef struct nmo_save_options {
    uint32_t flags;              /**< nmo_save_flags_t bitmask */
    bool compress_header;        /**< Compress Header1 section */
    bool compress_data;          /**< Compress Data section */
    bool compute_crc;            /**< Compute and write CRC (default: true) */
    bool validate_before_write;  /**< Validate buffer before IO (default: false) */
    int compression_level;       /**< zlib compression level (0-9, default: 6) */
} nmo_save_options_t;

/**
 * @brief Default save options (compression enabled, CRC enabled)
 */
NMO_API nmo_save_options_t nmo_save_options_default(void);

/**
 * @brief Save pipeline statistics
 */
typedef struct nmo_save_stats {
    /* Phase 1 stats */
    size_t object_count;          /**< Total objects serialized */
    size_t reference_count;       /**< Objects saved as references only */
    size_t serialized_count;      /**< Objects with new chunk data */
    size_t reused_count;          /**< Objects with preserved chunks */
    size_t manager_count;         /**< Manager chunks serialized */

    /* Size stats (before compression) */
    size_t header1_unpack_size;   /**< Header1 uncompressed size */
    size_t data_unpack_size;      /**< Data section uncompressed size */

    /* Size stats (after compression) */
    size_t header1_pack_size;     /**< Header1 compressed size (or same if uncompressed) */
    size_t data_pack_size;        /**< Data section compressed size */
    size_t total_file_size;       /**< Total file size in bytes */

    /* CRC */
    uint32_t crc;                 /**< Computed Adler-32 CRC */

    /* Phase 2 stats */
    double compression_ratio;     /**< Overall compression ratio */
    bool header_compressed;       /**< True if header was actually compressed */
    bool data_compressed;         /**< True if data was actually compressed */
} nmo_save_stats_t;

/**
 * @brief Create a save context for two-phase commit
 *
 * The save context holds all intermediate state between Phase 1 and Phase 2.
 * Allocations use the session's arena.
 *
 * @param session Session to save from
 * @param options Save options (NULL for defaults)
 * @return Save context, or NULL on error
 */
NMO_API nmo_save_context_t *nmo_save_context_create(
    nmo_session_t *session,
    const nmo_save_options_t *options);

/**
 * @brief Destroy save context
 *
 * Frees the save context. Does not affect session or arena.
 *
 * @param ctx Save context to destroy
 */
NMO_API void nmo_save_context_destroy(nmo_save_context_t *ctx);

/**
 * @brief Phase 1: Layout and serialize all data to memory
 *
 * This phase performs:
 * 1. Manager pre-save hooks
 * 2. Build ID remap plan (runtime_id -> file_index)
 * 3. Serialize manager chunks
 * 4. Serialize object chunks with ID remapping
 * 5. Build Header1 with object descriptors
 * 6. Calculate exact sizes
 *
 * After this phase, all data is in memory buffers ready for Phase 2.
 *
 * @param ctx Save context
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_save_phase1_layout(nmo_save_context_t *ctx);

/**
 * @brief Phase 2: Pack, compress, and commit to file
 *
 * This phase performs:
 * 1. Optional compression of Header1 and Data sections
 * 2. Build File Header with exact sizes
 * 3. Compute Adler-32 CRC over all sections
 * 4. Write to file atomically
 * 5. Manager post-save hooks
 *
 * @param ctx Save context
 * @param path Output file path
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_save_phase2_commit(nmo_save_context_t *ctx, const char *path);

/**
 * @brief Get save statistics after Phase 2 completes
 *
 * @param ctx Save context
 * @return Save statistics (valid only after Phase 2)
 */
NMO_API nmo_save_stats_t nmo_save_context_get_stats(const nmo_save_context_t *ctx);

/**
 * @brief Convenience function: two-phase save in one call
 *
 * Equivalent to:
 *   ctx = nmo_save_context_create(session, options);
 *   nmo_save_phase1_layout(ctx);
 *   nmo_save_phase2_commit(ctx, path);
 *   nmo_save_context_destroy(ctx);
 *
 * @param session Session to save
 * @param path Output file path
 * @param options Save options (NULL for defaults)
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_save_file_ex(
    nmo_session_t *session,
    const char *path,
    const nmo_save_options_t *options);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SAVE_PIPELINE_H */
