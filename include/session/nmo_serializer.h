/**
 * @file nmo_serializer.h
 * @brief Serializer pipeline API
 *
 * Implements the Two-Phase Commit architecture for file saving:
 *
 * **Phase 1: Layout & Serialize**
 * - Serialize objects to memory buffer
 * - Build ID mappings (runtime_id -> file ID for Header1, runtime_id -> file object index for chunks)
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

#ifndef NMO_SERIALIZER_H
#define NMO_SERIALIZER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "document/nmo_document_perf_stats.h"

#define NMO_SERIALIZER_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SERIALIZER_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session_t;
typedef struct nmo_serializer nmo_serializer_t;
typedef struct nmo_save_buffer nmo_save_buffer_t;

/**
 * @brief Save phase identifiers for progress callbacks
 */
typedef enum nmo_save_phase {
    NMO_SERIALIZE_PHASE_SERIALIZE,  /**< Phase 1: Layout & Serialize */
    NMO_SERIALIZE_PHASE_COMPRESS,   /**< Phase 2.1: Compress sections */
    NMO_SERIALIZE_PHASE_CRC,        /**< Phase 2.2: CRC calculation */
    NMO_SERIALIZE_PHASE_WRITE,      /**< Phase 2.2: Write file */
    NMO_SERIALIZE_PHASE_POST_HOOKS  /**< Phase 2.3: Post-save hooks */
} nmo_serialize_phase_t;

/**
 * @brief Save progress callback
 *
 * Return false to request cancellation when allow_cancel is enabled.
 */
typedef bool (*nmo_save_progress_callback_t)(
    void *user_data,
    nmo_serialize_phase_t phase,
    float progress,
    const char *status_text);

/**
 * @brief Save flags
 */
typedef enum nmo_save_flags {
    NMO_SAVE_DEFAULT          = 0,
    NMO_SAVE_AS_OBJECTS       = 0x0001, /**< Save as referenced objects */
    NMO_SAVE_COMPRESSED       = 0x0002, /**< Force compression on both sections */
    NMO_SAVE_SEQUENTIAL_IDS   = 0x0004, /**< Use sequential file IDs */
    NMO_SAVE_INCLUDE_MANAGERS = 0x0008, /**< Include manager state */
    NMO_SAVE_VALIDATE_BEFORE  = 0x0010, /**< Validate before writing */
    NMO_SAVE_STRIP_INCLUDED_FILES = 0x0020, /**< Drop included payloads during save */
    NMO_SAVE_REQUIRE_SCHEMA   = 0x0040, /**< Require schema serialization (no raw chunk reuse) */
} nmo_save_flags_t;

/**
 * @brief Save transaction durability mode.
 */
typedef enum nmo_save_durability {
    NMO_SAVE_DURABILITY_DEFAULT = 0, /**< Current durable default behavior */
    NMO_SAVE_DURABILITY_FSYNC,       /**< Explicit durable fsync/write-through behavior */
    NMO_SAVE_DURABILITY_FAST         /**< Atomic write without explicit flush/write-through */
} nmo_save_durability_t;

/**
 * @brief Save pipeline options
 */
typedef struct nmo_save_options {
    uint32_t flags;              /**< nmo_save_flags_t bitmask */
    nmo_save_durability_t durability; /**< Transaction durability mode */
    bool compress_header;        /**< Compress Header1 section */
    bool compress_data;          /**< Compress Data section */
    bool compute_crc;            /**< Compute and write CRC (default: true) */
    bool validate_before_write;  /**< Validate buffer before IO (default: false) */
    int compression_level;       /**< zlib compression level (0-9, default: 6) */
    nmo_save_progress_callback_t progress_fn; /**< Progress callback (optional) */
    void *progress_user_data;     /**< User data for progress callback */
    bool allow_cancel;            /**< Allow cancellation via callback */
    bool collect_perf_stats;      /**< Collect phase-level timing stats */
    nmo_save_perf_stats_t *perf_stats; /**< Optional caller-owned stats sink */

    /** Object filter: if non-NULL, only save objects whose IDs appear
     *  in this array. The array must remain valid until save completes. */
    const nmo_object_id_t *include_ids; /**< Object IDs to include (NULL = all) */
    size_t include_count;               /**< Number of entries in include_ids */
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
 * @ownership owned
 */
NMO_API nmo_serializer_t *nmo_serializer_create(
    nmo_session_t *session,
    const nmo_save_options_t *options);

/**
 * @brief Destroy save context
 *
 * Frees the save context. Does not affect session or arena.
 *
 * @param ctx Save context to destroy
 */
NMO_API void nmo_serializer_destroy(nmo_serializer_t *ctx);

/**
 * @brief Phase 1: Layout and serialize all data to memory
 *
 * This phase performs:
 * 1. Manager pre-save hooks
 * 2. Build ID remap plan (runtime_id -> file ID) and file object index remap
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
NMO_API nmo_status_t nmo_serializer_layout(nmo_serializer_t *ctx);

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
NMO_API nmo_status_t nmo_serializer_commit(nmo_serializer_t *ctx, const char *path);

/**
 * @brief Get save statistics after Phase 2 completes
 *
 * @param ctx Save context
 * @return Save statistics (valid only after Phase 2)
 */
NMO_API nmo_save_stats_t nmo_serializer_get_stats(const nmo_serializer_t *ctx);

/**
 * @brief Get phase-level save performance statistics.
 *
 * @param ctx Save context
 * @return Performance statistics collected so far, or all-zero stats
 */
NMO_API nmo_save_perf_stats_t nmo_serializer_get_perf_stats(const nmo_serializer_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SERIALIZER_H */
