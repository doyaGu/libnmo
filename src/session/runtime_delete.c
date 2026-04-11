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
#include <string.h>

/* ── Shared trivial helper ─────────────────────────────────────── */

static const nmo_type_descriptor_t *runtime_find_type_for_object(
    const nmo_type_runtime_t *type_rt,
    const nmo_object_t *object)
{
    if (type_rt == NULL || type_rt->types == NULL || object == NULL) {
        return NULL;
    }
    return nmo_type_registry_find_by_class_id_inherited(type_rt->types, object->class_id);
}

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

/* ── Public API ────────────────────────────────────────────────── */

int nmo_runtime_preview_delete(
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
    int result = runtime_collect_delete_set(repo, type_rt, arena, &request, &delete_set);
    if (result != NMO_OK) {
        nmo_bit_array_dispose(&delete_set.bits);
        return result;
    }

    *out_ids = ID_SET_DATA(&delete_set);
    *out_count = ID_SET_COUNT(&delete_set);
    nmo_bit_array_dispose(&delete_set.bits);
    return NMO_OK;
}

int nmo_runtime_execute_delete(
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
    if (repo == NULL || arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    runtime_id_set_t delete_set;
    memset(&delete_set, 0, sizeof(delete_set));
    int collect_result = runtime_collect_delete_set(repo, type_rt, arena, request, &delete_set);
    if (collect_result != NMO_OK) {
        nmo_bit_array_dispose(&delete_set.bits);
        return collect_result;
    }

    /* Two-pass delete: separate detach from destroy to prevent pre_delete
     * hooks from accessing state data of objects already freed in an earlier
     * iteration. Pass 1 runs hooks and detaches all; pass 2 destroys. */

    /* Pass 1: pre_delete hooks + detach from repository */
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
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
        if (obj == NULL) {
            continue;
        }

        const nmo_type_descriptor_t *type = runtime_find_type_for_object(type_rt, obj);
        if (type != NULL &&
            type->vtable != NULL &&
            type->vtable->pre_delete != NULL &&
            obj->state != NULL) {
            int hook_result = type->vtable->pre_delete(obj->state, type, repo);
            if (hook_result != NMO_OK && (request->flags & NMO_RUNTIME_REQUEST_STRICT)) {
                nmo_bit_array_dispose(&delete_set.bits);
                return hook_result;
            }
        }

        nmo_object_t *detached = NULL;
        int remove_result = nmo_object_repository_take(repo, object_id, &detached);
        if (remove_result != NMO_OK && (request->flags & NMO_RUNTIME_REQUEST_STRICT)) {
            nmo_bit_array_dispose(&delete_set.bits);
            return remove_result;
        }

        if (remove_result == NMO_OK && detached != NULL) {
            detached_objects[detached_count++] = detached;
        }
    }

    /* Pass 2: post_delete hooks + destroy (all objects already detached) */
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
            int detach_result = nmo_runtime_remap_all_refs(repo, type_rt, request->flags);
            if (detach_result != NMO_OK) {
                return detach_result;
            }
        }
    }

    return NMO_OK;
}
