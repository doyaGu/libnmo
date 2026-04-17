#include "session/nmo_runtime_delete.h"
#include "session/nmo_runtime_ref_remap.h"

#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_bit_array.h"
#include "object/nmo_ref_graph.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "core/nmo_logger.h"
#include "runtime_internal.h"
#include <string.h>

/* ── ID set (private) ──────────────────────────────────────────── */

typedef struct runtime_id_set {
    nmo_arena_array_t ids;  /**< Ordered list of member IDs (for iteration) */
    nmo_bit_array_t bits;   /**< Bit array for O(1) membership test */
} runtime_id_set_t;

#define ID_SET_COUNT(s)   ((s)->ids.count)
#define ID_SET_AT(s, i)   (((nmo_object_id_t *)(s)->ids.data)[(i)])
#define ID_SET_DATA(s)    ((nmo_object_id_t *)(s)->ids.data)

static bool runtime_id_set_contains(const runtime_id_set_t *set, nmo_object_id_t id)
{
    if (set == NULL || id == NMO_OBJECT_ID_NONE) {
        return false;
    }
    return nmo_bit_array_test(&set->bits, (size_t)id) != 0;
}

static int runtime_id_set_init(
    runtime_id_set_t *set,
    nmo_arena_t *arena,
    size_t initial_capacity)
{
    if (set == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(set, 0, sizeof(*set));

    if (initial_capacity == 0) {
        initial_capacity = 1;
    }

    nmo_status_t arr_st = nmo_arena_array_init(
        &set->ids, sizeof(nmo_object_id_t), initial_capacity, arena);
    if (arr_st != NMO_OK) {
        return arr_st;
    }

    size_t bit_cap = initial_capacity > 1024 ? initial_capacity : 1024;
    nmo_status_t bit_st = nmo_bit_array_init(&set->bits, bit_cap, NULL);
    if (bit_st != NMO_OK) {
        return bit_st;
    }

    return NMO_OK;
}

static int runtime_id_set_add(runtime_id_set_t *set, nmo_object_id_t id)
{
    if (set == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (id == NMO_OBJECT_ID_NONE || runtime_id_set_contains(set, id)) {
        return NMO_OK;
    }

    nmo_status_t st = nmo_arena_array_append(&set->ids, &id);
    if (st != NMO_OK) {
        return st;
    }

    nmo_status_t bit_st = nmo_bit_array_set(&set->bits, (size_t)id);
    if (bit_st != NMO_OK) {
        return bit_st;
    }
    return NMO_OK;
}

/* ── Delete-set collection ─────────────────────────────────────── */

static int runtime_collect_delete_set(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    const nmo_runtime_request_t *request,
    runtime_id_set_t *out_set)
{
    if (repo == NULL || arena == NULL || request == NULL || out_set == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int init_result = runtime_id_set_init(out_set, arena, request->payload.destroy.count);
    if (init_result != NMO_OK) {
        return init_result;
    }

    const nmo_object_id_t *ids = request->payload.destroy.ids;
    size_t count = request->payload.destroy.count;
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, ids[i]);
        if (obj == NULL) {
            if (request->flags & NMO_RUNTIME_REQUEST_STRICT) {
                return NMO_ERR_NOT_FOUND;
            }
            continue;
        }

        int add_result = runtime_id_set_add(out_set, obj->id);
        if (add_result != NMO_OK) {
            return add_result;
        }
    }

    if ((request->flags & NMO_RUNTIME_REQUEST_CASCADE) == 0 ||
        type_rt == NULL || type_rt->types == NULL) {
        return NMO_OK;
    }

    /* Worklist-based cascade using reverse reference index.
     * Build the ref graph once (O(E)), then for each deleted ID,
     * find all incoming references (objects that depend on it) and
     * add them to the delete set. Total work: O(E) vs O(rounds * N * E). */
    nmo_ref_graph_t *cascade_graph = nmo_ref_graph_create(repo, type_rt->types, arena);
    if (cascade_graph == NULL) {
        /* Fallback: no cascade if graph creation fails */
        return NMO_OK;
    }

    /* Worklist: index into the id set's array. New IDs appended
     * by runtime_id_set_add are automatically reached. */
    size_t worklist_head = 0;

    while (worklist_head < ID_SET_COUNT(out_set)) {
        nmo_object_id_t target_id = ID_SET_AT(out_set, worklist_head++);

        nmo_ref_edge_t *in_edges = NULL;
        size_t in_count = 0;
        nmo_ref_graph_get_object_edges(cascade_graph, target_id,
                                       NMO_REF_DIR_INCOMING, &in_edges, &in_count);

        for (size_t e = 0; e < in_count; e++) {
            nmo_object_id_t referrer_id = in_edges[e].from;
            if (referrer_id == NMO_OBJECT_ID_NONE ||
                runtime_id_set_contains(out_set, referrer_id)) {
                continue;
            }

            int add_result = runtime_id_set_add(out_set, referrer_id);
            if (add_result != NMO_OK) {
                nmo_ref_graph_destroy(cascade_graph);
                return add_result;
            }
        }
    }

    nmo_ref_graph_destroy(cascade_graph);

    return NMO_OK;
}

/* ── Safe-detach pre-validation ────────────────────────────────── */

/**
 * @brief Validate that safe-detach remap can succeed.
 *
 * For each surviving object that references a delete-set member, verify its
 * type has a remap_dependencies vtable hook.  Without this hook,
 * nmo_runtime_remap_all_refs() silently skips the object, leaving dangling
 * references in the saved file.
 *
 * @return NMO_OK if all referencing objects support remap, error otherwise
 */
static int runtime_validate_safe_detach(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_ref_graph_t *graph,
    const runtime_id_set_t *delete_set,
    uint32_t request_flags,
    nmo_logger_t *logger)
{
    if (graph == NULL || type_rt == NULL || type_rt->types == NULL) {
        return NMO_OK;
    }

    nmo_ref_edge_t *edges = NULL;
    size_t edge_count = 0;
    nmo_ref_graph_get_edges(graph, &edges, &edge_count);

    int result = NMO_OK;
    for (size_t i = 0; i < edge_count; i++) {
        /* Only care about edges TO a deleted object FROM a surviving object */
        if (!runtime_id_set_contains(delete_set, edges[i].to)) {
            continue;
        }
        if (runtime_id_set_contains(delete_set, edges[i].from)) {
            continue;
        }

        nmo_object_t *referrer = nmo_object_repository_find_by_id(repo, edges[i].from);
        if (referrer == NULL || referrer->state == NULL) {
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, referrer);
        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->remap_dependencies != NULL) {
            continue; /* this object can be remapped */
        }

        /* Surviving object references a deleted object but cannot remap */
        if (request_flags & NMO_RUNTIME_REQUEST_STRICT) {
            result = NMO_ERR_VALIDATION_FAILED;
            break;
        }
        if (logger != NULL) {
            nmo_log(logger, NMO_LOG_WARN,
                    "Object %u references deleted object %u but type lacks "
                    "remap_dependencies; dangling reference will persist",
                    edges[i].from, edges[i].to);
        }
    }

    return result;
}

/* ── Public API ────────────────────────────────────────────────── */

nmo_status_t nmo_runtime_preview_delete(
    nmo_object_repository_t *repo,
    const nmo_type_runtime_t *type_rt,
    nmo_arena_t *arena,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_object_id_t **out_ids,
    size_t *out_count)
{
    if (repo == NULL || arena == NULL || object_ids == NULL || object_count == 0 ||
        out_ids == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    *out_ids = NULL;
    *out_count = 0;

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_DELETE;
    request.flags = flags;
    request.payload.destroy.ids = object_ids;
    request.payload.destroy.count = object_count;

    runtime_id_set_t delete_set;
    memset(&delete_set, 0, sizeof(delete_set));
    nmo_status_t result = runtime_collect_delete_set(repo, type_rt, arena, &request, &delete_set);
    if (result != NMO_OK) {
        nmo_bit_array_dispose(&delete_set.bits);
        return result;
    }

    *out_ids = ID_SET_DATA(&delete_set);
    *out_count = ID_SET_COUNT(&delete_set);
    nmo_bit_array_dispose(&delete_set.bits);
    return NMO_OK;
}

nmo_status_t nmo_runtime_execute_delete(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *report)
{
    if (session == NULL || request == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (request->payload.destroy.ids == NULL || request->payload.destroy.count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_arena_t *arena = nmo_session_get_arena(session);
    const nmo_type_runtime_t *type_rt = (ctx != NULL) ? nmo_context_get_type_runtime(ctx) : NULL;
    nmo_logger_t *logger = (ctx != NULL) ? nmo_context_get_logger(ctx) : NULL;
    if (repo == NULL || arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    runtime_id_set_t delete_set;
    memset(&delete_set, 0, sizeof(delete_set));
    nmo_status_t collect_result = runtime_collect_delete_set(repo, type_rt, arena, request, &delete_set);
    if (collect_result != NMO_OK) {
        nmo_bit_array_dispose(&delete_set.bits);
        return collect_result;
    }

    /* Pre-validate safe-detach: ensure all surviving referrers can remap */
    if (request->flags & NMO_RUNTIME_REQUEST_SAFE_DETACH) {
        nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
        nmo_status_t validate_result = runtime_validate_safe_detach(
            repo, type_rt, graph, &delete_set, request->flags, logger);
        if (validate_result != NMO_OK) {
            nmo_bit_array_dispose(&delete_set.bits);
            return validate_result;
        }
    }

    /* Three-phase delete: validate hooks, then detach, then destroy.
     * Phase 1a runs pre_delete hooks without mutating the repository so that
     * all hooks see a fully consistent world.  If any hook fails under STRICT,
     * the operation aborts before any state change.
     * Phase 1b detaches objects from the repository (no destroy yet).
     * Phase 2 runs post_delete hooks and destroys detached objects. */

    /* Phase 1a: validate pre_delete hooks (no mutation) */
    for (size_t i = 0; i < ID_SET_COUNT(&delete_set); i++) {
        nmo_object_id_t object_id = ID_SET_AT(&delete_set, i);
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
        if (obj == NULL) {
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, obj);
        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->pre_delete != NULL &&
            obj->state != NULL) {
            nmo_status_t hook_result = type->vtable->pre_delete(obj->state, type, repo);
            if (hook_result != NMO_OK && (request->flags & NMO_RUNTIME_REQUEST_STRICT)) {
                nmo_bit_array_dispose(&delete_set.bits);
                return hook_result;
            }
        }
    }

    /* Phase 1b: batch detach from repository (all hooks passed) */
    nmo_object_t **detached_objects = (nmo_object_t **)nmo_arena_alloc(
        arena, ID_SET_COUNT(&delete_set) * sizeof(nmo_object_t *),
        _Alignof(nmo_object_t *));
    if (detached_objects == NULL && ID_SET_COUNT(&delete_set) > 0) {
        nmo_bit_array_dispose(&delete_set.bits);
        return NMO_ERR_NOMEM;
    }
    size_t detached_count = 0;

    for (size_t i = 0; i < ID_SET_COUNT(&delete_set); i++) {
        nmo_object_id_t object_id = ID_SET_AT(&delete_set, i);
        nmo_object_t *detached = NULL;
        nmo_status_t remove_result = nmo_object_repository_take(repo, object_id, &detached);
        /* All IDs were validated in Phase 1a and runtime_collect_delete_set.
         * A take failure here indicates an internal consistency error. */
        if (remove_result != NMO_OK || detached == NULL) {
            if (logger != NULL) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Phase 1b: take(%u) failed unexpectedly (status=%d)",
                        object_id, remove_result);
            }
            continue;
        }
        if (remove_result == NMO_OK && detached != NULL) {
            detached_objects[detached_count++] = detached;
        }
    }

    /* Phase 2: post_delete hooks + destroy (all objects already detached) */
    for (size_t i = 0; i < detached_count; i++) {
        nmo_object_t *detached = detached_objects[i];
        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, detached);

        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->post_delete != NULL &&
            detached->state != NULL) {
            type->vtable->post_delete(detached->state, type, repo);
        }
        nmo_object_destroy(detached);

        if (report != NULL) {
            report->deleted_objects++;
            report->affected_objects++;
        }
    }

    nmo_bit_array_dispose(&delete_set.bits);

    if (request->flags & NMO_RUNTIME_REQUEST_SAFE_DETACH) {
        if (type_rt != NULL && type_rt->types != NULL) {
            nmo_status_t detach_result = nmo_runtime_remap_all_refs(repo, type_rt, request->flags);
            if (detach_result != NMO_OK) {
                return detach_result;
            }
        }
    }

    /* Clean up included files whose owners are all deleted.
     * Iterate in reverse so index shifts from remove don't skip entries. */
    if (request->flags & NMO_RUNTIME_REQUEST_CASCADE) {
        uint32_t file_count = 0;
        nmo_included_file_t *files = nmo_session_get_included_files(session, &file_count);
        for (uint32_t fi = file_count; fi > 0; fi--) {
            nmo_included_file_t *f = &files[fi - 1];
            if (f->owner_ids.count == 0) {
                continue; /* no owners recorded — not managed */
            }
            const nmo_object_id_t *owners =
                (const nmo_object_id_t *)f->owner_ids.data;
            bool any_alive = false;
            for (size_t oi = 0; oi < f->owner_ids.count; oi++) {
                if (nmo_object_repository_find_by_id(repo, owners[oi]) != NULL) {
                    any_alive = true;
                    break;
                }
            }
            if (!any_alive) {
                if (logger != NULL) {
                    nmo_log(logger, NMO_LOG_DEBUG,
                            "Removing orphaned included file %u (%s): all owners deleted",
                            fi - 1, f->name ? f->name : "<unnamed>");
                }
                nmo_session_remove_included_file(session, fi - 1);
                if (report != NULL) {
                    report->affected_objects++;
                }
            }
        }
    }

    return NMO_OK;
}
