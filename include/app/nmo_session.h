/**
 * @file nmo_session.h
 * @brief Session API for per-operation state (Phase 8.2)
 */

#ifndef NMO_SESSION_H
#define NMO_SESSION_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_chunk_pool nmo_chunk_pool_t;
typedef struct nmo_reference_resolver nmo_reference_resolver_t;
typedef struct nmo_included_file nmo_included_file_t;

/**
 * @brief Session structure
 *
 * Single-threaded per-operation state. Owns arena and object repository.
 * Borrows context (does not retain).
 */
typedef struct nmo_session nmo_session_t;

/**
 * @brief File information structure
 */
typedef struct nmo_file_info {
    uint32_t file_version;  /**< File format version */
    uint32_t ck_version;    /**< CK engine version */
    size_t file_size;       /**< File size in bytes */
    uint32_t object_count;  /**< Number of objects */
    uint32_t manager_count; /**< Number of managers */
    uint32_t write_mode;    /**< Write mode flags */
} nmo_file_info_t;

/**
 * @brief Create session
 *
 * Creates a new session borrowing the given context. The session is
 * single-threaded and owns its own arena and object repository.
 *
 * @param ctx Context to borrow (must remain valid for session lifetime)
 * @return Session or NULL on error
 */
NMO_API nmo_session_t *nmo_session_create(nmo_context_t *ctx);

/**
 * @brief Destroy session
 *
 * Destroys the session and all owned resources (arena, repository).
 * Does not affect the borrowed context.
 *
 * @param session Session to destroy
 */
NMO_API void nmo_session_destroy(nmo_session_t *session);

/**
 * @brief Get context
 *
 * Returns the borrowed context.
 *
 * @param session Session
 * @return Context
 */
NMO_API nmo_context_t *nmo_session_get_context(const nmo_session_t *session);

/**
 * @brief Get arena
 *
 * Returns the session-owned arena for temporary allocations.
 *
 * @param session Session
 * @return Arena
 */
NMO_API nmo_arena_t *nmo_session_get_arena(const nmo_session_t *session);

/**
 * @brief Get object repository
 *
 * Returns the session-owned object repository.
 *
 * @param session Session
 * @return Object repository
 */
NMO_API nmo_object_repository_t *nmo_session_get_repository(const nmo_session_t *session);

/**
 * @brief Get chunk pool used for chunk allocations
 *
 * Returns the optional chunk pool owned by the session. May be NULL if the
 * pool has not been created yet or failed to initialize.
 */
NMO_API nmo_chunk_pool_t *nmo_session_get_chunk_pool(const nmo_session_t *session);

/**
 * @brief Ensure chunk pool exists
 *
 * Creates the chunk pool on-demand if it does not already exist. The
 * initial_capacity_hint parameter can be zero to use the default size.
 * Returns NULL on allocation failure.
 */
NMO_API nmo_chunk_pool_t *nmo_session_ensure_chunk_pool(
    nmo_session_t *session,
    size_t initial_capacity_hint);

/**
 * @brief Get file info
 *
 * Returns information about the file associated with this session.
 * Valid after a successful load operation.
 *
 * @param session Session
 * @return File information
 */
NMO_API nmo_file_info_t nmo_session_get_file_info(const nmo_session_t *session);

/**
 * @brief Set file info
 *
 * Sets file information for the session. Used during load/save operations.
 *
 * @param session Session
 * @param info File information to set
 * @return NMO_OK on success
 */
NMO_API int nmo_session_set_file_info(nmo_session_t *session, const nmo_file_info_t *info);

/* Forward declaration for manager data */
typedef struct nmo_manager_data nmo_manager_data_t;

/**
 * @brief Set manager data
 *
 * Sets manager data for the session (for round-trip serialization).
 *
 * @param session Session
 * @param data Manager data array (borrowed, not owned)
 * @param count Number of managers
 */
NMO_API void nmo_session_set_manager_data(nmo_session_t *session, nmo_manager_data_t *data, uint32_t count);

/**
 * @brief Get manager data
 *
 * Gets manager data from the session.
 *
 * @param session Session
 * @param out_count Output manager count (optional)
 * @return Manager data array or NULL
 */
NMO_API nmo_manager_data_t *nmo_session_get_manager_data(const nmo_session_t *session, uint32_t *out_count);

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
 */
NMO_API nmo_session_t *nmo_session_load(nmo_context_t *ctx, const char *filename);

/**
 * @brief Save session to NMO file
 *
 * High-level function to save session to file.
 *
 * @param session Session to save
 * @param filename Output file path
 * @return 0 on success, negative on error
 */
NMO_API int nmo_session_save(nmo_session_t *session, const char *filename);

/* Forward declaration */
typedef struct nmo_object nmo_object_t;
typedef struct nmo_header nmo_header_t;
typedef struct nmo_object_index nmo_object_index_t;
typedef struct nmo_index_stats nmo_index_stats_t;
typedef struct nmo_guid nmo_guid_t;

/**
 * @brief Get all objects from session
 *
 * Returns array of all objects in the session's repository.
 *
 * @param session Session
 * @param out_objects Output object array pointer
 * @param out_count Output object count
 * @return 0 on success, negative on error
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
 */
NMO_API const nmo_header_t *nmo_session_get_header(const nmo_session_t *session);

/* ==================== Object Index Management (Phase 5) ==================== */

/**
 * @brief Set object index
 *
 * Sets the object index for this session. Used by finish_loading phase.
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
 */
NMO_API nmo_object_index_t *nmo_session_get_object_index(const nmo_session_t *session);

/**
 * @brief Rebuild object indexes
 *
 * Rebuilds object indexes after objects have been added/removed.
 *
 * @param session Session
 * @param flags Index types to rebuild (NMO_INDEX_BUILD_*)
 * @return 0 on success, negative on error
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
    const char *name; /**< Filename without path */
    const void *data; /**< Raw payload data */
    uint32_t size;    /**< Payload size in bytes */
} nmo_included_file_t;

NMO_API int nmo_session_add_included_file(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size);

int nmo_session_add_included_file_borrowed(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size);

NMO_API nmo_included_file_t *nmo_session_get_included_files(
    const nmo_session_t *session,
    uint32_t *out_count);

/**
 * @brief Get current reference resolver
 *
 * Returns the resolver instance associated with this session, or NULL if
 * reference resolution has not been initialized yet.
 */
NMO_API nmo_reference_resolver_t *nmo_session_get_reference_resolver(
    const nmo_session_t *session);

/**
 * @brief Ensure reference resolver exists
 */
NMO_API nmo_reference_resolver_t *nmo_session_ensure_reference_resolver(
    nmo_session_t *session);

/**
 * @brief Reset reference resolver pointer
 */
NMO_API void nmo_session_reset_reference_resolver(nmo_session_t *session);

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
void nmo_session_set_file_header(nmo_session_t *session, const void *header, size_t header_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_H */
