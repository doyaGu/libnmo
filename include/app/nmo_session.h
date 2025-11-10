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
typedef struct nmo_context nmo_context;
typedef struct nmo_arena nmo_arena;
typedef struct nmo_object_repository nmo_object_repository;

/**
 * @brief Session structure
 *
 * Single-threaded per-operation state. Owns arena and object repository.
 * Borrows context (does not retain).
 */
typedef struct nmo_session nmo_session;

/**
 * @brief File information structure
 */
typedef struct {
    uint32_t file_version;     /**< File format version */
    uint32_t ck_version;       /**< CK engine version */
    size_t file_size;          /**< File size in bytes */
    uint32_t object_count;     /**< Number of objects */
    uint32_t manager_count;    /**< Number of managers */
    uint32_t write_mode;       /**< Write mode flags */
} nmo_file_info;

/**
 * @brief Create session
 *
 * Creates a new session borrowing the given context. The session is
 * single-threaded and owns its own arena and object repository.
 *
 * @param ctx Context to borrow (must remain valid for session lifetime)
 * @return Session or NULL on error
 */
NMO_API nmo_session* nmo_session_create(nmo_context* ctx);

/**
 * @brief Destroy session
 *
 * Destroys the session and all owned resources (arena, repository).
 * Does not affect the borrowed context.
 *
 * @param session Session to destroy
 */
NMO_API void nmo_session_destroy(nmo_session* session);

/**
 * @brief Get context
 *
 * Returns the borrowed context.
 *
 * @param session Session
 * @return Context
 */
NMO_API nmo_context* nmo_session_get_context(const nmo_session* session);

/**
 * @brief Get arena
 *
 * Returns the session-owned arena for temporary allocations.
 *
 * @param session Session
 * @return Arena
 */
NMO_API nmo_arena* nmo_session_get_arena(const nmo_session* session);

/**
 * @brief Get object repository
 *
 * Returns the session-owned object repository.
 *
 * @param session Session
 * @return Object repository
 */
NMO_API nmo_object_repository* nmo_session_get_repository(const nmo_session* session);

/**
 * @brief Get file info
 *
 * Returns information about the file associated with this session.
 * Valid after a successful load operation.
 *
 * @param session Session
 * @return File information
 */
NMO_API nmo_file_info nmo_session_get_file_info(const nmo_session* session);

/**
 * @brief Set file info
 *
 * Sets file information for the session. Used during load/save operations.
 *
 * @param session Session
 * @param info File information to set
 * @return NMO_OK on success
 */
NMO_API int nmo_session_set_file_info(nmo_session* session, const nmo_file_info* info);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_H */
