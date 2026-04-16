/**
 * @file nmo_deserializer.h
 * @brief Deserializer pipeline for loading NMO files
 *
 * Implements a multi-phase deserialization pipeline:
 *
 * **Phase 1: Parse Header**
 * - Parse file header (magic, version, CRC)
 * - Decompress and parse Header1 (object table, manager list)
 *
 * **Phase 2: Parse Objects**
 * - Start load session (ID remapping)
 * - Check plugin dependencies
 * - Execute manager pre-load hooks
 * - Decompress data section
 * - Parse manager chunks
 * - Create objects from Header1 descriptors
 * - Deserialize object chunks with ID remapping
 *
 * **Phase 3: Finalize**
 * - Execute runtime hooks (prepare, remap, post-load)
 * - Build behavior index
 * - Load included files
 */

#ifndef NMO_DESERIALIZER_H
#define NMO_DESERIALIZER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "app/nmo_perf_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session_t;
typedef struct nmo_allocator nmo_allocator_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_deserializer nmo_deserializer_t;
typedef struct nmo_io_interface nmo_io_interface_t;

/* ============================================================================
 * Load options & stats
 * ============================================================================ */

/** Default maximum included filename length (bytes, excluding NUL). */
#define NMO_LOAD_DEFAULT_MAX_INCLUDED_NAME_LEN 4096u

/** Default maximum included file payload size (bytes). */
#define NMO_LOAD_DEFAULT_MAX_INCLUDED_FILE_SIZE (512u * 1024u * 1024u)

/**
 * @brief Load flags
 */
typedef enum nmo_load_flags {
    NMO_LOAD_DEFAULT            = 0,
    NMO_LOAD_DODIALOG           = 0x0001,
    NMO_LOAD_AUTOMATICMODE      = 0x0002,
    NMO_LOAD_CHECKDUPLICATES    = 0x0004,
    NMO_LOAD_AS_DYNAMIC_OBJECT  = 0x0008,
    NMO_LOAD_ONLYBEHAVIORS      = 0x0010,
    NMO_LOAD_CHECK_DEPENDENCIES = 0x0020,
    NMO_LOAD_PRESERVE_SHADOW    = 0x0080,
    NMO_LOAD_STRICT             = 0x0100, /**< Error on unresolved references */
} nmo_load_flags_t;

/**
 * @brief Amount of file data to materialize during load.
 */
typedef enum nmo_load_profile {
    NMO_LOAD_PROFILE_FULL = 0,       /**< Parse and finalize the complete file */
    NMO_LOAD_PROFILE_METADATA,       /**< Parse file header/Header1 only */
    NMO_LOAD_PROFILE_HEADER_ONLY     /**< Parse file header/Header1 only */
} nmo_load_profile_t;

/**
 * @brief Load pipeline options
 */
typedef struct nmo_load_options {
    nmo_allocator_t *allocator;      /**< Custom allocator (NULL for default) */
    nmo_load_flags_t flags;          /**< Standard load flags */
    nmo_load_profile_t profile;      /**< Load depth profile */
    uint32_t max_included_name_len;  /**< Max included filename length */
    uint32_t max_included_file_size; /**< Max included file payload size */
    bool collect_perf_stats;         /**< Collect phase-level timing stats */
    nmo_load_perf_stats_t *perf_stats; /**< Optional caller-owned stats sink */
} nmo_load_options_t;

/**
 * @brief Default load options
 */
NMO_API nmo_load_options_t nmo_load_options_default(void);

/**
 * @brief Load statistics
 */
typedef struct nmo_load_stats {
    size_t object_count;             /**< Total objects loaded */
    size_t manager_count;            /**< Manager chunks processed */
    size_t included_file_count;      /**< Included files loaded */
    size_t header1_size;             /**< Header1 uncompressed size */
    size_t data_size;                /**< Data section uncompressed size */
    size_t total_file_size;          /**< Total file size in bytes */
    bool header_compressed;          /**< Header1 was compressed */
    bool data_compressed;            /**< Data section was compressed */
    uint32_t file_version;           /**< File format version */
    uint32_t crc;                    /**< File CRC */
} nmo_load_stats_t;

/* ============================================================================
 * Deserializer lifecycle
 * ============================================================================ */

/**
 * @brief Create a deserializer for phased file loading
 *
 * @param session Session to load into
 * @param io      IO interface (ownership transferred to deserializer)
 * @param options Load options (NULL for defaults)
 * @return Deserializer context, or NULL on error
 * @ownership owned
 */
NMO_API nmo_deserializer_t *nmo_deserializer_create(
    nmo_session_t *session,
    nmo_io_interface_t *io,
    const nmo_load_options_t *options);

/**
 * @brief Phase 1: Parse file header and Header1
 *
 * Parses file header, decompresses and parses Header1 (object table,
 * manager list, included file metadata).
 *
 * @param ctx Deserializer context
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_deserializer_parse_header(nmo_deserializer_t *ctx);

/**
 * @brief Phase 2: Parse objects and managers
 *
 * Creates objects, deserializes chunks with ID remapping, dispatches
 * manager chunks, loads included files.
 *
 * @param ctx Deserializer context
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_deserializer_parse_objects(nmo_deserializer_t *ctx);

/**
 * @brief Phase 3: Finalize (runtime hooks, build indices)
 *
 * Executes runtime hooks (prepare, remap, post-load), builds
 * behavior index for O(1) lookups.
 *
 * @param ctx Deserializer context
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_deserializer_finalize(nmo_deserializer_t *ctx);

/**
 * @brief Get load statistics
 *
 * @param ctx Deserializer context
 * @return Load statistics (valid after all phases complete)
 */
NMO_API nmo_load_stats_t nmo_deserializer_get_stats(
    const nmo_deserializer_t *ctx);

/**
 * @brief Get phase-level load performance statistics.
 *
 * @param ctx Deserializer context
 * @return Performance statistics collected so far, or all-zero stats
 */
NMO_API nmo_load_perf_stats_t nmo_deserializer_get_perf_stats(
    const nmo_deserializer_t *ctx);

/**
 * @brief Destroy deserializer
 *
 * Frees the context and closes the IO interface.
 *
 * @param ctx Deserializer context
 */
NMO_API void nmo_deserializer_destroy(nmo_deserializer_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DESERIALIZER_H */
