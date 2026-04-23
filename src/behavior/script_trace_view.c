#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_behavior_analyze.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_bridge.h"

#include <stdlib.h>
#include <string.h>

static void nmo_script_trace_chain_view_clear(nmo_behavior_trace_chain_view_t *view)
{
    if (view == NULL) {
        return;
    }

    free(view->steps);
    memset(view, 0, sizeof(*view));
}

static void nmo_script_tree_view_clear(nmo_behavior_tree_view_t *view)
{
    size_t i = 0u;

    if (view == NULL) {
        return;
    }

    for (i = 0u; i < view->node_count; ++i) {
        free((void *)view->nodes[i].name);
    }
    free(view->nodes);
    memset(view, 0, sizeof(*view));
}

static nmo_behavior_trace_step_kind_t nmo_script_trace_map_step_kind(
    nmo_behavior_trace_step_type_t type)
{
    switch (type) {
        case NMO_BEHAVIOR_TRACE_STEP_SHARED_SOURCE:
            return NMO_BEHAVIOR_TRACE_STEP_KIND_SHARED_SOURCE;
        case NMO_BEHAVIOR_TRACE_STEP_DIRECT_SOURCE:
            return NMO_BEHAVIOR_TRACE_STEP_KIND_DIRECT_SOURCE;
        case NMO_BEHAVIOR_TRACE_STEP_START:
        default:
            return NMO_BEHAVIOR_TRACE_STEP_KIND_START;
    }
}

typedef struct nmo_script_tree_collect_ctx {
    nmo_session_t *session;
    nmo_object_repository_t *repository;
    nmo_behavior_tree_node_view_t *nodes;
    size_t count;
    size_t capacity;
    uint32_t max_depth;
    bool alloc_failed;
} nmo_script_tree_collect_ctx_t;

static bool nmo_script_trace_collect_tree_node(
    nmo_object_id_t behavior_id,
    const nmo_behavior_state_t *state,
    uint32_t depth,
    bool is_building_block,
    void *user_data)
{
    nmo_script_tree_collect_ctx_t *ctx =
        (nmo_script_tree_collect_ctx_t *)user_data;
    nmo_object_t *object = NULL;
    nmo_behavior_tree_node_view_t *node = NULL;
    char *name_copy = NULL;

    (void)state;

    if (ctx == NULL || ctx->repository == NULL) {
        return false;
    }
    if (depth > ctx->max_depth) {
        return true;
    }
    if (ctx->count == ctx->capacity) {
        size_t new_capacity = ctx->capacity == 0u ? 8u : ctx->capacity * 2u;
        nmo_behavior_tree_node_view_t *new_nodes =
            (nmo_behavior_tree_node_view_t *)realloc(ctx->nodes,
                                                   new_capacity * sizeof(*new_nodes));
        if (new_nodes == NULL) {
            ctx->alloc_failed = true;
            return false;
        }
        ctx->nodes = new_nodes;
        ctx->capacity = new_capacity;
    }

    node = &ctx->nodes[ctx->count];
    memset(node, 0, sizeof(*node));
    node->behavior_id = behavior_id;
    node->depth = depth;
    node->is_building_block = is_building_block;

    object = nmo_object_repository_find_by_id(ctx->repository, behavior_id);
    if (object != NULL) {
        const char *name = nmo_object_get_name(object);
        node->class_id = nmo_object_get_class_id(object);
        if (name != NULL) {
            size_t len = strlen(name);
            name_copy = (char *)malloc(len + 1u);
            if (name_copy == NULL) {
                ctx->alloc_failed = true;
                return false;
            }
            memcpy(name_copy, name, len + 1u);
            node->name = name_copy;
        }
    }

    ctx->count++;
    return true;
}

NMO_API nmo_status_t nmo_behavior_trace_parameter_chain(
    nmo_workspace_t *workspace,
    nmo_object_id_t parameter_id,
    uint32_t max_depth,
    nmo_behavior_trace_chain_view_t *out_view)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_array_t chain;
    const nmo_behavior_trace_step_t *steps = NULL;
    size_t i = 0u;
    nmo_status_t status = NMO_OK;

    if (workspace == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(out_view, 0, sizeof(*out_view));
    if (parameter_id == 0u) {
        return NMO_ERR_NOT_FOUND;
    }

    session = nmo_session_from_workspace(workspace);
    ctx = session != NULL ? nmo_session_get_context(session) : NULL;
    if (ctx == NULL || session == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_array_init(&chain, sizeof(nmo_behavior_trace_step_t), 8u, NULL);
    status = nmo_behavior_analyze_trace_param_chain(
        ctx, session, parameter_id, &chain, max_depth);
    if (status != NMO_OK) {
        nmo_array_dispose(&chain);
        return status;
    }
    if (chain.count == 0u) {
        nmo_array_dispose(&chain);
        return NMO_ERR_NOT_FOUND;
    }

    out_view->steps = (nmo_behavior_trace_step_view_t *)calloc(
        chain.count, sizeof(*out_view->steps));
    if (out_view->steps == NULL) {
        nmo_array_dispose(&chain);
        return NMO_ERR_NOMEM;
    }
    out_view->step_count = chain.count;

    steps = (const nmo_behavior_trace_step_t *)chain.data;
    for (i = 0u; i < chain.count; ++i) {
        out_view->steps[i].id = steps[i].id;
        out_view->steps[i].step_kind = nmo_script_trace_map_step_kind(steps[i].type);
        out_view->steps[i].owner_id = steps[i].owner_id;
        out_view->steps[i].class_id = steps[i].class_id;
    }

    nmo_array_dispose(&chain);
    return NMO_OK;
}

NMO_API void nmo_behavior_trace_chain_view_destroy(
    nmo_behavior_trace_chain_view_t *view)
{
    nmo_script_trace_chain_view_clear(view);
}

NMO_API nmo_status_t nmo_behavior_trace_script_tree(
    nmo_workspace_t *workspace,
    nmo_object_id_t root_behavior_id,
    uint32_t max_depth,
    nmo_behavior_tree_view_t *out_view)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repository = NULL;
    nmo_script_tree_collect_ctx_t collect = {0};
    nmo_status_t status = NMO_OK;

    if (workspace == NULL || out_view == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(out_view, 0, sizeof(*out_view));
    if (root_behavior_id == 0u) {
        return NMO_ERR_NOT_FOUND;
    }

    session = nmo_session_from_workspace(workspace);
    ctx = session != NULL ? nmo_session_get_context(session) : NULL;
    if (ctx == NULL || session == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    repository = nmo_session_get_repository(session);
    if (repository == NULL ||
        nmo_object_repository_find_by_id(repository, root_behavior_id) == NULL) {
        return NMO_ERR_NOT_FOUND;
    }

    collect.session = session;
    collect.repository = repository;
    collect.max_depth = max_depth == 0u ? UINT32_MAX : max_depth;
    status = nmo_behavior_walk(ctx,
                                    session,
                                    root_behavior_id,
                                    nmo_script_trace_collect_tree_node,
                                    &collect);
    if (status != NMO_OK) {
        nmo_behavior_tree_view_t cleanup_view = {
            .nodes = collect.nodes,
            .node_count = collect.count
        };
        nmo_script_tree_view_clear(&cleanup_view);
        return status;
    }
    if (collect.alloc_failed) {
        nmo_behavior_tree_view_t cleanup_view = {
            .nodes = collect.nodes,
            .node_count = collect.count
        };
        nmo_script_tree_view_clear(&cleanup_view);
        return NMO_ERR_NOMEM;
    }
    if (collect.count == 0u) {
        free(collect.nodes);
        return NMO_ERR_NOT_FOUND;
    }

    out_view->nodes = collect.nodes;
    out_view->node_count = collect.count;
    return NMO_OK;
}

NMO_API void nmo_behavior_tree_view_destroy(
    nmo_behavior_tree_view_t *view)
{
    nmo_script_tree_view_clear(view);
}
