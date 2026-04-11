#ifndef NMO_SESSION_RUNTIME_DELETE_H
#define NMO_SESSION_RUNTIME_DELETE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "session/nmo_runtime_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_session nmo_session_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_runtime nmo_type_runtime_t;
typedef struct nmo_arena nmo_arena_t;

/**
 * @brief Execute a delete operation on the session.
 *
 * Collects the delete set (with optional cascade via
 * NMO_RUNTIME_REQUEST_CASCADE), runs pre_delete/post_delete hooks,
 * performs two-pass delete (detach then destroy), and optionally
 * remaps dangling references (NMO_RUNTIME_REQUEST_SAFE_DETACH).
 *
 * @param session  Active session
 * @param request  Runtime request (kind must be NMO_RUNTIME_OP_DELETE)
 * @param report   Optional report (may be NULL)
 * @return NMO_OK on success
 */
NMO_API int nmo_runtime_execute_delete(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *report);

/**
 * @brief Preview which objects would be deleted without modifying state.
 *
 * Collects the expanded delete set (including cascade if flags contain
 * NMO_RUNTIME_REQUEST_CASCADE) and returns the IDs via out parameters.
 *
 * @param repo         Object repository
 * @param type_rt      Type runtime (may be NULL if no cascade needed)
 * @param arena        Arena for temporary allocations
 * @param object_ids   Array of object IDs to delete
 * @param object_count Number of IDs
 * @param flags        Runtime request flags
 * @param out_ids      Output: arena-allocated array of expanded IDs
 * @param out_count    Output: number of expanded IDs
 * @return NMO_OK on success
 */
NMO_API int nmo_runtime_preview_delete(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_object_id_t **out_ids,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SESSION_RUNTIME_DELETE_H */
