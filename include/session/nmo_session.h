/**
 * @file nmo_session.h
 * @brief Session API for per-operation state (Phase 8.2)
 */

#ifndef NMO_SESSION_H
#define NMO_SESSION_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena_array.h"
#include "object/nmo_object_query.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_chunk_pool nmo_chunk_pool_t;
typedef struct nmo_included_file nmo_included_file_t;
typedef struct nmo_reference_resolver nmo_reference_resolver_t;
typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_plugin_dep nmo_plugin_dep_t;
typedef struct nmo_id_sanitizer nmo_id_sanitizer_t;
typedef struct nmo_shadow_storage nmo_shadow_storage_t;
typedef struct nmo_behavior_index nmo_behavior_index_t;
typedef struct nmo_ref_graph nmo_ref_graph_t;
typedef struct nmo_load_options nmo_load_options_t;
typedef struct nmo_save_options nmo_save_options_t;
typedef struct nmo_runtime_report nmo_runtime_report_t;

/*
 * This header is the advanced runtime/session layer:
 * - canonical consumer workflow lives in document/workspace/object/behavior
 * - raw runtime execution, cache wiring, and pipeline state stay here
 */
#define NMO_SESSION_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_MIXED_TIER
#define NMO_SESSION_WORKFLOW_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_SESSION_QUERY_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_SESSION_EXECUTION_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_SESSION_ACCELERATION_CACHE_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_SESSION_INTERNAL_STATE_API_TIER NMO_API_TIER_ADVANCED_C

/*
 * Default consumers should treat this header as advanced C plumbing rather
 * than the primary workflow entry point. Canonical public entry points live in
 * runtime/document/chunk/object/behavior/export. Request shaping, resolver
 * plumbing, and raw pipeline staging remain here.
 */

/**
 * @brief Session structure
 *
 * Single-threaded per-operation state. Owns arena and object repository.
 * Retains context until nmo_session_destroy().
 */
typedef struct nmo_session nmo_session_t;

/**
 * @brief File information structure
 */
typedef struct nmo_file_info {
    uint32_t file_version;    /**< File format version */
    uint32_t file_version2;   /**< Secondary file version field */
    uint32_t ck_version;      /**< CK engine version */
    uint32_t product_version; /**< Product version field */
    uint32_t product_build;   /**< Product build identifier */
    size_t file_size;         /**< File size in bytes */
    uint32_t object_count;    /**< Number of objects */
    uint32_t manager_count;   /**< Number of managers */
    uint32_t write_mode;      /**< Write mode flags */
} nmo_file_info_t;

/* Forward declaration for format-layer types used in file state */
typedef struct nmo_manager_data nmo_manager_data_t;

/**
 * @brief Consolidated file round-trip state
 *
 * Groups all format-layer metadata that must be preserved for lossless
 * round-trip serialization: file info, manager data, and plugin dependencies.
 * All pointer fields are borrowed (arena-allocated during load).
 */
typedef struct nmo_file_state {
    nmo_file_info_t info;                   /**< File header metadata */
    nmo_manager_data_t *manager_data;       /**< Manager serialization data (borrowed) */
    uint32_t manager_data_count;            /**< Number of manager entries */
    nmo_plugin_dep_t *plugin_deps;          /**< Plugin dependency table (borrowed) */
    uint32_t plugin_dep_count;              /**< Number of plugin dependencies */
} nmo_file_state_t;

typedef struct nmo_session_behavior_interface_diagnostics {
    int attempted;
    int available;
    nmo_status_t status;
    size_t attempted_count;
    size_t parsed_count;
    size_t failed_count;
    size_t skipped_no_arena_count;
    size_t allocation_failure_count;
    nmo_object_id_t first_error_object_id;
    uint32_t first_error_file_id;
    uint32_t first_error_chunk_version;
    uint32_t first_error_data_version;
    size_t first_error_reader_offset;
    size_t first_error_chunk_dwords;
} nmo_session_behavior_interface_diagnostics_t;

/**
 * @brief Create session
 *
 * Creates a new session retaining the given context. The session is
 * single-threaded and owns its own arena and object repository.
 *
 * @param ctx Context to retain
 * @return Session or NULL on error
 * @ownership owned
 */
NMO_API nmo_session_t *nmo_session_create(nmo_context_t *ctx);

/**
 * @brief Destroy session
 *
 * Destroys the session and all owned resources (arena, repository).
 * Releases the retained context reference.
 *
 * @param session Session to destroy
 */
NMO_API void nmo_session_destroy(nmo_session_t *session);

/**
 * @brief Get context
 * @ownership borrowed (retained by session until destroy)
 */
NMO_API nmo_context_t *nmo_session_get_context(const nmo_session_t *session);

/**
 * @brief Get extension registry
 * @ownership borrowed (owned by context)
 */
NMO_API nmo_extension_registry_t *nmo_session_get_extension_registry(const nmo_session_t *session);

/**
 * @brief Get arena for temporary allocations
 * @ownership borrowed (owned by session)
 */
NMO_API nmo_arena_t *nmo_session_get_arena(const nmo_session_t *session);

/**
 * @brief Get object repository
 * @ownership borrowed (owned by session)
 */
NMO_API nmo_object_repository_t *nmo_session_get_repository(const nmo_session_t *session);

/**
 * @brief Get behavior ownership index.
 *
 * Lazily built on first access after file loading. Returns NULL if the build
 * fails. Rebuilt when stale after any session-execute mutation
 * (nmo_session_create_object, nmo_session_copy_objects,
 * nmo_session_destroy_objects, or any nmo_session_execute call).
 *
 * @ownership borrowed (owned by session, valid until next mutation)
 */
NMO_API nmo_behavior_index_t *nmo_session_get_behavior_index(nmo_session_t *session);

/**
 * @brief Ensure behavior acceleration structures are built.
 *
 * Builds or refreshes the behavior ownership index and parses pending behavior
 * interface chunks into structured interface data. This is used by behavior
 * graph/read/edit APIs and is lazy after load.
 *
 * @param session Session
 * @return NMO_OK on success, otherwise an NMO_ERR_* code
 */
NMO_API nmo_status_t nmo_session_ensure_behavior_acceleration(nmo_session_t *session);

NMO_API void nmo_session_get_behavior_interface_diagnostics(
    const nmo_session_t *session,
    nmo_session_behavior_interface_diagnostics_t *out_diag);

/**
 * @brief Get or lazily build the reference graph for this session.
 *
 * The graph is cached and automatically invalidated on any mutation
 * (create/copy/delete). Returns NULL if the session has no type runtime.
 *
 * @param session Session
 * @return Reference graph, or NULL
 * @ownership borrowed (valid until next mutation)
 */
NMO_API nmo_ref_graph_t *nmo_session_get_ref_graph(nmo_session_t *session);

/**
 * @brief Invalidate the cached reference graph.
 *
 * Called internally after mutations. The graph will be rebuilt lazily
 * on next access via nmo_session_get_ref_graph().
 */
NMO_API void nmo_session_invalidate_ref_graph(nmo_session_t *session);

/**
 * @brief Mark the behavior index as stale.
 *
 * Called internally after mutations (create/copy/delete). The index will
 * be rebuilt lazily on the next behavior acceleration access.
 */
NMO_API void nmo_session_invalidate_behavior_index(nmo_session_t *session);

/**
 * @brief Return non-zero when the session contains partial load data only.
 */
NMO_API int nmo_session_is_partial_load(const nmo_session_t *session);

/**
 * @brief Return non-zero when the session already contains loaded file state.
 *
 * This includes full object data as well as non-object runtime state such as
 * file headers, manager data, plugin dependencies, included files, and cached
 * load diagnostics. Partial metadata/header-only loads require this to be
 * false before they can safely populate a session.
 */
NMO_API int nmo_session_has_materialized_load_state(const nmo_session_t *session);

/**
 * @brief Load file into an existing session via runtime execute.
 */
NMO_API nmo_status_t nmo_session_load_file(
    nmo_session_t *session,
    const char *filename,
    const nmo_load_options_t *options,
    nmo_runtime_report_t *out_report);

/**
 * @brief Save session to file via runtime execute.
 */
NMO_API nmo_status_t nmo_session_save_file(
    nmo_session_t *session,
    const char *filename,
    const nmo_save_options_t *options,
    nmo_runtime_report_t *out_report);

/**
 * @brief Create object via runtime execute.
 */
NMO_API nmo_status_t nmo_session_create_object(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id,
    nmo_runtime_report_t *out_report);

/**
 * @brief Copy objects via runtime execute.
 */
NMO_API nmo_status_t nmo_session_copy_objects(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report);

/**
 * @brief Destroy objects via runtime execute.
 */
NMO_API nmo_status_t nmo_session_destroy_objects(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report);

/**
 * @brief Preview which objects would be destroyed without actually deleting.
 *
 * Computes the expanded delete set (including cascade when flags contain
 * NMO_RUNTIME_REQUEST_CASCADE) and returns the IDs. No objects are removed.
 *
 * @param session           Session
 * @param object_ids        Array of object IDs to preview-delete
 * @param object_count      Number of IDs
 * @param flags             Runtime request flags (e.g. NMO_RUNTIME_REQUEST_CASCADE)
 * @param arena             Arena for output allocation
 * @param out_expanded_ids  Output: arena-allocated array of expanded IDs
 * @param out_expanded_count Output: number of expanded IDs
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_session_preview_destroy(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_expanded_ids,
    size_t *out_expanded_count);

/**
 * @brief Get chunk pool used for chunk allocations
 *
 * Returns the optional chunk pool owned by the session. May be NULL if the
 * pool has not been created yet or failed to initialize.
 * @ownership borrowed
 */
NMO_API nmo_chunk_pool_t *nmo_session_get_chunk_pool(const nmo_session_t *session);

/**
 * @brief Get ID sanitizer
 *
 * Returns the session-owned ID sanitizer used during load/save remapping.
 *
 * @param session Session
 * @return ID sanitizer or NULL
 * @ownership borrowed
 */
NMO_API nmo_id_sanitizer_t *nmo_session_get_id_sanitizer(const nmo_session_t *session);

/**
 * @brief Get shadow storage
 *
 * Returns the session-owned shadow storage used for included files and chunk tails.
 *
 * @param session Session
 * @return Shadow storage or NULL
 * @ownership borrowed
 */
NMO_API nmo_shadow_storage_t *nmo_session_get_shadow_storage(const nmo_session_t *session);

/**
 * @brief Ensure chunk pool exists
 *
 * Creates the chunk pool on-demand if it does not already exist. The
 * initial_capacity_hint parameter can be zero to use the default size.
 * Returns NULL on allocation failure.
 * @ownership borrowed
 */
NMO_API nmo_chunk_pool_t *nmo_session_ensure_chunk_pool(
    nmo_session_t *session,
    size_t initial_capacity_hint);

/**
 * @brief Get consolidated file round-trip state
 *
 * Returns a read-only view of all format-layer metadata needed for
 * lossless round-trip: file info, manager data, and plugin dependencies.
 * The returned view is invalidated by subsequent load/pipeline staging updates.
 *
 * @param session Session
 * @return File state pointer, or NULL if no file has been loaded
 * @ownership borrowed (session-owned, invalidated by load-state mutation)
 */
NMO_API const nmo_file_state_t *nmo_session_get_file_state(const nmo_session_t *session);

/**
 * @brief Get file info
 * @ownership borrowed (embedded in session file state)
 * @return File information (zero-initialized if no file loaded)
 */
NMO_API nmo_file_info_t nmo_session_get_file_info(const nmo_session_t *session);

/* High-level convenience API */

/**
 * @brief Load NMO file into session
 *
 * High-level function to load a complete NMO file.
 * Creates and populates the object repository.
 *
 * @param ctx Context
 * @param filename Path to NMO file
 * @return Session with loaded data, or NULL on error
 * @ownership owned
 */
NMO_API nmo_session_t *nmo_session_load(nmo_context_t *ctx, const char *filename);

/* Forward declaration */
typedef struct nmo_object nmo_object_t;
typedef struct nmo_header nmo_header_t;
typedef struct nmo_object_index nmo_object_index_t;
typedef struct nmo_index_stats nmo_index_stats_t;

/**
 * @brief Get all objects from session
 *
 * Returns array of all objects in the session's repository.
 * The returned storage is owned by the repository and is invalidated by
 * repository mutation or operations that rebuild/repack the repository array.
 *
 * @param session Session
 * @param out_objects Output object array pointer
 * @param out_count Output object count
 * @return NMO_OK on success, otherwise an NMO_ERR_* code
 * @ownership borrowed (repository-owned, invalidated after mutation)
 */
NMO_API nmo_status_t nmo_session_get_objects(
    nmo_session_t *session,
    nmo_object_t ***out_objects,
    size_t *out_count
);

/**
 * @brief Get file header from session
 *
 * Returns the parsed file header if available.
 *
 * @param session Session
 * @return Header pointer or NULL if not loaded
 * @ownership borrowed
 */
NMO_API const nmo_header_t *nmo_session_get_header(const nmo_session_t *session);

/* ==================== Object Index Management (Phase 5) ==================== */

/**
 * @brief Get object index
 *
 * Returns the object index if available.
 *
 * @param session Session
 * @return Object index or NULL if not built
 * @ownership borrowed (session-owned, invalidated after rebuild or mutation)
 */
NMO_API nmo_object_index_t *nmo_session_get_object_index(const nmo_session_t *session);

/**
 * @brief Rebuild object indexes
 *
 * Rebuilds object indexes after objects have been added/removed.
 *
 * @param session Session
 * @param flags Index types to rebuild (NMO_INDEX_BUILD_*)
 * @return NMO_OK on success, otherwise an NMO_ERR_* code
 */
NMO_API nmo_status_t nmo_session_rebuild_indexes(nmo_session_t *session, uint32_t flags);

/**
 * @brief Retrieve index statistics when available
 *
 * @param session Session
 * @param stats Output statistics buffer
 * @return NMO_OK on success, NMO_ERR_NOT_FOUND if indexes not built yet
 */
NMO_API nmo_status_t nmo_session_get_object_index_stats(
    const nmo_session_t *session,
    nmo_index_stats_t *stats);

/**
 * @brief Invalidate the session-owned object query index when low-level state changes.
 */
NMO_API void nmo_session_invalidate_object_query(
    nmo_session_t *session,
    uint32_t flags);

/* Included file management */

/**
 * @brief Included file metadata stored in the session
 */
typedef struct nmo_included_file {
    const char *name;           /**< Filename without path */
    const void *data;           /**< Raw payload data */
    uint32_t size;              /**< Payload size in bytes */
    nmo_arena_array_t owner_ids; /**< Owning object IDs (optional, element type: nmo_object_id_t) */
    uint32_t attributes;        /**< Metadata flags (borrowed payload, etc.) */
} nmo_included_file_t;

#define NMO_INCLUDED_FILE_ATTR_BORROWED      0x00000001u
#define NMO_INCLUDED_FILE_ATTR_METADATA_ONLY 0x00000002u

typedef struct nmo_included_file_metadata {
    const nmo_object_id_t *owner_ids;
    uint32_t owner_count;
    uint32_t attributes; /**< Use NMO_INCLUDED_FILE_ATTR_* */
} nmo_included_file_metadata_t;

NMO_API nmo_status_t nmo_session_add_included_file(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size);

NMO_API nmo_status_t nmo_session_add_included_file_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta);

/**
 * @brief Add an included file without copying payload data.
 *
 * The payload pointer is borrowed (caller owns the memory). Use this for
 * metadata-only entries or when data is already lifetime-managed elsewhere.
 */
NMO_API nmo_status_t nmo_session_add_included_file_borrowed(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size);

/**
 * @brief Add an included file without copying payload data, with metadata.
 */
NMO_API nmo_status_t nmo_session_add_included_file_borrowed_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta);

NMO_API nmo_status_t nmo_session_set_included_file_owners(
    nmo_session_t *session,
    uint32_t index,
    const nmo_object_id_t *owner_ids,
    uint32_t owner_count);

/** @ownership borrowed */
NMO_API nmo_included_file_t *nmo_session_get_included_files(
    const nmo_session_t *session,
    uint32_t *out_count);

/**
 * @brief Replace payload of an existing included file.
 *
 * Arena-allocates new payload; preserves name and owners.
 * Old data leaks into arena (freed on session destroy).
 * Clears BORROWED flag so save pipeline serializes individually.
 */
NMO_API nmo_status_t nmo_session_replace_included_file(
    nmo_session_t *session,
    uint32_t index,
    const void *new_data,
    uint32_t new_size);

/**
 * @brief Remove an included file by index.
 *
 * Shifts subsequent entries down. Invalidates the shadow included-files
 * blob so the save pipeline serializes entries individually.
 */
NMO_API nmo_status_t nmo_session_remove_included_file(
    nmo_session_t *session,
    uint32_t index);

typedef struct nmo_runtime_load_stats {
    size_t total_objects;
    uint32_t flags;
    struct {
        uint32_t total;
        uint32_t resolved;
        uint32_t unresolved;
        uint32_t ambiguous;
        uint32_t unresolved_preview_count;
        struct {
            nmo_object_id_t id;
            nmo_class_id_t class_id;
        } unresolved_preview[8];
    } references;
    struct {
        size_t class_entries;
        size_t name_entries;
        size_t guid_entries;
        size_t memory_usage;
    } indexes;
    struct {
        uint32_t invoked;
        uint32_t errors;
    } object_postload;
    uint32_t manager_errors;
} nmo_runtime_load_stats_t;

NMO_API nmo_status_t nmo_session_get_runtime_load_stats(
    const nmo_session_t *session,
    nmo_runtime_load_stats_t *out_stats);

typedef struct nmo_session_plugin_dependency_status {
    nmo_guid_t guid;
    nmo_plugin_category_t category;
    uint32_t required_version;
    uint32_t resolved_version;
    const char *resolved_name;
    uint32_t status_flags;
} nmo_session_plugin_dependency_status_t;

#define NMO_SESSION_PLUGIN_DEP_STATUS_MISSING            0x00000001u
#define NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD    0x00000002u
#define NMO_SESSION_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE 0x00000004u

typedef struct nmo_session_plugin_diagnostics {
    const nmo_session_plugin_dependency_status_t *entries;
    size_t entry_count;
    size_t missing_count;
    size_t outdated_count;
    int extension_registry_available;
} nmo_session_plugin_diagnostics_t;

/** @ownership borrowed (session-owned, invalidated by diagnostics refresh or dependency staging) */
NMO_API const nmo_session_plugin_diagnostics_t *nmo_session_get_plugin_diagnostics(
    const nmo_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_H */
