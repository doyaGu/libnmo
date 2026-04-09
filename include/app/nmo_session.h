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
#include "session/nmo_runtime_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_chunk_pool nmo_chunk_pool_t;
typedef struct nmo_included_file nmo_included_file_t;
typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_plugin_dep nmo_plugin_dep_t;
typedef struct nmo_id_sanitizer nmo_id_sanitizer_t;
typedef struct nmo_shadow_storage nmo_shadow_storage_t;

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
 * @brief Execute unified runtime operation.
 */
NMO_API int nmo_session_execute(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report);

/**
 * @brief Load file into an existing session via runtime execute.
 */
NMO_API int nmo_session_load_file(
    nmo_session_t *session,
    const char *filename,
    const nmo_load_options_t *options,
    nmo_runtime_report_t *out_report);

/**
 * @brief Save session to file via runtime execute.
 */
NMO_API int nmo_session_save_file(
    nmo_session_t *session,
    const char *filename,
    const nmo_save_options_t *options,
    nmo_runtime_report_t *out_report);

/**
 * @brief Create object via runtime execute.
 */
NMO_API int nmo_session_create_object(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id,
    nmo_runtime_report_t *out_report);

/**
 * @brief Copy objects via runtime execute.
 */
NMO_API int nmo_session_copy_objects(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report);

/**
 * @brief Destroy objects via runtime execute.
 */
NMO_API int nmo_session_destroy_objects(
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
NMO_API int nmo_session_preview_destroy(
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
 *
 * @param session Session
 * @return File state pointer, or NULL if no file has been loaded
 * @ownership borrowed
 */
NMO_API const nmo_file_state_t *nmo_session_get_file_state(const nmo_session_t *session);

/**
 * @brief Get file info
 * @ownership borrowed (embedded in session file state)
 * @return File information (zero-initialized if no file loaded)
 */
NMO_API nmo_file_info_t nmo_session_get_file_info(const nmo_session_t *session);

/**
 * @brief Set file info
 *
 * @param session Session
 * @param info File information to set
 * @return NMO_OK on success
 */
NMO_API int nmo_session_set_file_info(nmo_session_t *session, const nmo_file_info_t *info);

/**
 * @brief Set manager data (borrowed pointers, arena-allocated)
 */
NMO_API void nmo_session_set_manager_data(nmo_session_t *session, nmo_manager_data_t *data, uint32_t count);

/**
 * @brief Set plugin dependencies (borrowed pointers, arena-allocated)
 *
 * Also triggers plugin dependency diagnostics refresh.
 * @return NMO_OK on success
 */
NMO_API int nmo_session_set_plugin_dependencies(nmo_session_t *session, nmo_plugin_dep_t *deps, uint32_t count);

/**
 * @brief Rebuild plugin dependency diagnostics based on current file state.
 */
NMO_API int nmo_session_refresh_plugin_diagnostics(nmo_session_t *session);

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

/**
 * @brief Save session to NMO file
 *
 * High-level function to save session to file.
 *
 * @param session Session to save
 * @param filename Output file path
 * @return NMO_OK on success, otherwise an NMO_ERR_* code
 */
NMO_API int nmo_session_save(nmo_session_t *session, const char *filename);

/* Forward declaration */
typedef struct nmo_object nmo_object_t;
typedef struct nmo_header nmo_header_t;
typedef struct nmo_object_index nmo_object_index_t;
typedef struct nmo_index_stats nmo_index_stats_t;

/**
 * @brief Get all objects from session
 *
 * Returns array of all objects in the session's repository.
 *
 * @param session Session
 * @param out_objects Output object array pointer
 * @param out_count Output object count
 * @return NMO_OK on success, otherwise an NMO_ERR_* code
 */
NMO_API int nmo_session_get_objects(
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
 * @brief Set object index
 *
 * Sets the object index for this session. Used by runtime load pipeline.
 * If an index is already set, it will be destroyed and replaced.
 *
 * @param session Session
 * @param index Object index (session takes ownership)
 */
NMO_API void nmo_session_set_object_index(nmo_session_t *session, nmo_object_index_t *index);

/**
 * @brief Get object index
 *
 * Returns the object index if available.
 *
 * @param session Session
 * @return Object index or NULL if not built
 * @ownership borrowed
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
NMO_API int nmo_session_rebuild_indexes(nmo_session_t *session, uint32_t flags);

/**
 * @brief Retrieve index statistics when available
 *
 * @param session Session
 * @param stats Output statistics buffer
 * @return NMO_OK on success, NMO_ERR_NOT_FOUND if indexes not built yet
 */
NMO_API int nmo_session_get_object_index_stats(
    const nmo_session_t *session,
    nmo_index_stats_t *stats);

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

NMO_API int nmo_session_add_included_file(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size);

NMO_API int nmo_session_add_included_file_ex(
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
NMO_API int nmo_session_add_included_file_borrowed(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size);

/**
 * @brief Add an included file without copying payload data, with metadata.
 */
NMO_API int nmo_session_add_included_file_borrowed_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta);

NMO_API int nmo_session_set_included_file_owners(
    nmo_session_t *session,
    uint32_t index,
    const nmo_object_id_t *owner_ids,
    uint32_t owner_count);

/** @ownership borrowed */
NMO_API nmo_included_file_t *nmo_session_get_included_files(
    const nmo_session_t *session,
    uint32_t *out_count);

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

/**
 * @brief Store runtime load diagnostics for later retrieval.
 */
NMO_API void nmo_session_set_runtime_load_stats(
    nmo_session_t *session,
    const nmo_runtime_load_stats_t *stats);

NMO_API int nmo_session_get_runtime_load_stats(
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

NMO_API void nmo_session_set_plugin_diagnostics(
    nmo_session_t *session,
    const nmo_session_plugin_dependency_status_t *entries,
    size_t entry_count,
    size_t missing_count,
    size_t outdated_count,
    int extension_registry_available);

/** @ownership borrowed */
NMO_API const nmo_session_plugin_diagnostics_t *nmo_session_get_plugin_diagnostics(
    const nmo_session_t *session);

/* ==================== Object Query API (Phase 5) ==================== */

/**
 * @brief Find object by name
 *
 * Searches for an object by name. Uses index if available for fast lookup.
 *
 * @param session Session
 * @param name Object name to search for
 * @param class_id Optional class filter (0 = any class)
 * @return Object pointer or NULL if not found
 * @ownership borrowed
 */
NMO_API nmo_object_t *nmo_session_find_by_name(
    nmo_session_t *session,
    const char *name,
    nmo_class_id_t class_id
);

/**
 * @brief Find object by GUID
 *
 * Searches for an object by type GUID. Uses index if available.
 *
 * @param session Session
 * @param guid GUID to search for
 * @return Object pointer or NULL if not found
 * @ownership borrowed
 */
NMO_API nmo_object_t *nmo_session_find_by_guid(
    nmo_session_t *session,
    nmo_guid_t guid
);

/**
 * @brief Get all objects of a specific class
 *
 * Returns all objects with the specified class ID. Uses index if available.
 *
 * @param session Session
 * @param class_id Class ID to search for
 * @param out_count Output: number of objects found
 * @return Array of object pointers, or NULL if none found
 * @ownership borrowed
 */
NMO_API nmo_object_t **nmo_session_get_objects_by_class(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    size_t *out_count
);

/**
 * @brief Count objects of a specific class
 *
 * Returns the number of objects with the specified class ID.
 *
 * @param session Session
 * @param class_id Class ID
 * @return Number of objects
 */
NMO_API size_t nmo_session_count_objects_by_class(
    nmo_session_t *session,
    nmo_class_id_t class_id
);

/* ==================== Internal API (Used by Parser) ==================== */

/**
 * @brief Set file header (internal use by parser)
 *
 * Stores the parsed file header in session arena as opaque data.
 * This is an internal API used by the parser during file loading.
 * The session stores the header without needing to know its structure,
 * maintaining proper layer separation.
 *
 * @param session Session
 * @param header Opaque file header data to store
 * @param header_size Size of header data in bytes
 */
/**
 * @brief Set file header (internal use by parser)
 *
 * This function is used by the in-library parser. It is exported so consumers
 * including this header do not see an uncallable symbol when building shared.
 */
NMO_API void nmo_session_set_file_header(nmo_session_t *session,
                                        const void *header,
                                        size_t header_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_H */
