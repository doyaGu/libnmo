/**
 * @file nmo_session.h
 * @brief Session API for per-operation state (Phase 8.2)
 */

#ifndef NMO_SESSION_H
#define NMO_SESSION_H

#include "document/nmo_document_load.h"
#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_context nmo_context_t;
typedef struct nmo_arena nmo_arena_t;

/*
 * This header is the advanced runtime/session layer:
 * - canonical consumer workflow lives in document/workspace/object/behavior
 * - raw session lifecycle and arena access stay here
 */
#define NMO_SESSION_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_MIXED_TIER
#define NMO_SESSION_STATE_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_SESSION_DIAGNOSTIC_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_SESSION_WORKFLOW_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_SESSION_EXECUTION_API_TIER NMO_API_TIER_ADVANCED_C
#define NMO_SESSION_ACCELERATION_CACHE_API_TIER NMO_API_TIER_ADVANCED_C

/*
 * Default consumers should treat this header as advanced C plumbing rather
 * than the primary workflow entry point. Canonical public entry points live in
 * runtime/document/chunk/object/behavior/export. Raw session lifecycle and
 * arena access remain here.
 */

/**
 * @brief Session structure
 *
 * Single-threaded per-operation state. Owns arena and object repository.
 * Retains context until nmo_session_destroy().
 */
typedef struct nmo_session nmo_session_t;

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
 * @brief Get arena for temporary allocations
 * @ownership borrowed (owned by session)
 */
NMO_API nmo_arena_t *nmo_session_get_arena(const nmo_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_H */
