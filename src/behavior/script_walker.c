/**
 * @file script_walker.c
 * @brief Scene-level script traversal implementation
 *
 * Provides discovery of scripts across all objects, recursive behavior tree
 * walking, parameter source tracing, and text dump output.
 */

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_view.h"
#include "../runtime/runtime_internal.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"
#include "behavior/nmo_behavior_registry.h"

#include <stdio.h>
#include <string.h>

/* Behavior flag constants (from ckbehavior_schemas.c) */
#define CKBEHAVIOR_SCRIPT          0x00000002u
#define CKBEHAVIOR_BUILDINGBLOCK   0x00008000u

/* ============================================================================
 * Behavior tree walking (recursive)
 * ============================================================================ */

static nmo_status_t walk_recursive(
    nmo_workspace_t *workspace,
    nmo_object_repository_t *repo,
    nmo_object_id_t behavior_id,
    uint32_t depth,
    nmo_behavior_walk_visitor_fn visitor,
    void *user_data,
    nmo_array_t *visited)
{
    if (depth > 256) {
        return NMO_OK; /* guard against infinite recursion */
    }

    const nmo_object_id_t *visited_ids = NMO_ARRAY_DATA(
        nmo_object_id_t, visited);
    for (size_t i = 0; visited_ids && i < visited->count; ++i) {
        if (visited_ids[i] == behavior_id) return NMO_OK;
    }
    nmo_object_id_t *visited_slot = NULL;
    NMO_RETURN_IF_ERROR(nmo_array_extend(visited, 1, (void **)&visited_slot));
    *visited_slot = behavior_id;

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, behavior_id);
    if (!obj) return NMO_OK;
    if (nmo_object_get_class_id(obj) != NMO_CID_BEHAVIOR) return NMO_OK;

    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);

    bool is_bb = state && (state->flags & CKBEHAVIOR_BUILDINGBLOCK);

    if (!visitor(behavior_id, state, depth, is_bb, user_data)) {
        return NMO_OK; /* visitor requested stop */
    }

    /* Recurse into sub-behaviors (only for graph behaviors) */
    if (state && !is_bb) {
        size_t sub_count = state->sub_behaviors.count;

        for (size_t i = 0; i < sub_count; ++i) {
            nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
                &state->sub_behaviors, i);
            if (sub_id == 0) continue;
            nmo_status_t st = walk_recursive(
                workspace, repo, sub_id,
                depth + 1, visitor, user_data, visited);
            if (st != NMO_OK) return st;
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_behavior_walk(
    nmo_workspace_t *workspace,
    nmo_object_id_t root_behavior_id,
    nmo_behavior_walk_visitor_fn visitor,
    void *user_data)
{
    if (!workspace || !visitor) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to nmo_behavior_walk");
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    if (!repo) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "No object repository in workspace");
    }

    nmo_array_t visited;
    NMO_RETURN_IF_ERROR(nmo_array_init(
        &visited, sizeof(nmo_object_id_t), 32, NULL));
    nmo_status_t status = walk_recursive(
        workspace, repo, root_behavior_id, 0, visitor, user_data, &visited);
    nmo_array_dispose(&visited);
    return status;
}

/* ============================================================================
 * Parameter source tracing
 * ============================================================================ */

nmo_status_t nmo_behavior_analyze_trace_param_chain(
    nmo_workspace_t *workspace,
    nmo_object_id_t param_in_id,
    nmo_array_t *out_chain,
    uint32_t max_depth)
{
    if (!workspace || !out_chain) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to nmo_behavior_analyze_trace_param_chain");
    }

    if (max_depth == 0) max_depth = 32;

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    if (!repo) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "No object repository in workspace");
    }

    const nmo_behavior_index_t *beh_index =
        nmo_workspace_internal_behavior_index(workspace);
    nmo_object_id_t current_id = param_in_id;

    for (uint32_t step = 0; step < max_depth; ++step) {
        if (current_id == 0) break;

        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, current_id);
        if (!obj) break;

        nmo_class_id_t cid = nmo_object_get_class_id(obj);

        nmo_object_id_t owner_id = 0;
        if (beh_index) {
            const nmo_port_owner_t *owner = nmo_behavior_index_find(beh_index, current_id);
            if (owner) owner_id = owner->owner_id;
        }

        if (cid == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *pin =
                (const nmo_parameterin_state_t *)nmo_object_get_state(obj);

            nmo_behavior_trace_step_type_t step_type;
            if (step == 0) {
                step_type = NMO_BEHAVIOR_TRACE_STEP_START;
            } else if (pin && pin->is_shared) {
                step_type = NMO_BEHAVIOR_TRACE_STEP_SHARED_SOURCE;
            } else {
                step_type = NMO_BEHAVIOR_TRACE_STEP_DIRECT_SOURCE;
            }

            nmo_behavior_trace_step_t chain_step = {
                current_id, step_type, owner_id, cid
            };
            if (nmo_array_append(out_chain, &chain_step) != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Out of memory in trace_param_chain");
            }

            if (!pin || pin->source_id == 0) break;
            current_id = pin->source_id;
        } else {
            /* Reached a non-ParameterIn (ParameterOut, ParameterLocal, etc.) */
            nmo_behavior_trace_step_t chain_step = {
                current_id, NMO_BEHAVIOR_TRACE_STEP_DIRECT_SOURCE, owner_id, cid
            };
            if (nmo_array_append(out_chain, &chain_step) != NMO_OK) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Out of memory in trace_param_chain");
            }
            break;
        }
    }

    return NMO_OK;
}

/* ============================================================================
 * Text dump
 * ============================================================================ */

typedef struct dump_ctx {
    nmo_workspace_t *workspace;
    nmo_context_t *ctx;
    nmo_type_registry_t *registry;
    nmo_object_repository_t *repo;
    FILE *out;
} dump_ctx_t;

static void print_indent(FILE *out, uint32_t depth)
{
    for (uint32_t i = 0; i < depth; ++i) {
        fprintf(out, "  ");
    }
}

static void dump_param_array(
    dump_ctx_t *dctx, const char *label,
    const nmo_array_t *array, uint32_t depth)
{
    if (array->count == 0) return;

    for (size_t i = 0; i < array->count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        if (id == 0) continue;
        nmo_object_t *pobj = nmo_object_repository_find_by_id(dctx->repo, id);
        if (!pobj) {
            print_indent(dctx->out, depth);
            fprintf(dctx->out, "%s[%zu]: #%u (missing)\n", label, i, id);
            continue;
        }

        const char *pname = nmo_object_get_name(pobj);
        nmo_class_id_t pcid = nmo_object_get_class_id(pobj);

        print_indent(dctx->out, depth);
        fprintf(dctx->out, "%s[%zu]: #%u", label, i, id);
        if (pname && pname[0]) fprintf(dctx->out, " \"%s\"", pname);

        /* Decode parameter value if it's a CKParameter-derived object */
        if (pcid == NMO_CID_PARAMETEROUT || pcid == NMO_CID_PARAMETERLOCAL) {
            /* ParameterOut/Local extend CKParameter; state starts with nmo_parameter_state_t */
            const nmo_parameter_state_t *pstate =
                (const nmo_parameter_state_t *)nmo_object_get_state(pobj);
            if (pstate && dctx->registry) {
                char val_buf[256];
                if (nmo_behavior_param_value_to_string(pstate, dctx->registry,
                        NULL, val_buf, sizeof(val_buf)) == NMO_OK) {
                    const char *tname = nmo_behavior_param_type_name(pstate, dctx->registry);
                    fprintf(dctx->out, " : %s = %s",
                            tname ? tname : "?", val_buf);
                }
            }
        } else if (pcid == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *pin =
                (const nmo_parameterin_state_t *)nmo_object_get_state(pobj);
            if (pin) {
                const char *tname = nmo_type_registry_guid_to_name(
                    dctx->registry, pin->type_guid);
                fprintf(dctx->out, " : %s", tname ? tname : "?");
                if (pin->source_id) {
                    fprintf(dctx->out, " <- #%u", pin->source_id);
                }
                if (pin->is_shared) {
                    fprintf(dctx->out, " (shared)");
                }
            }
        }

        fprintf(dctx->out, "\n");
    }
}

static bool dump_visitor(
    nmo_object_id_t behavior_id,
    const nmo_behavior_state_t *state,
    uint32_t depth,
    bool is_building_block,
    void *user_data)
{
    dump_ctx_t *dctx = (dump_ctx_t *)user_data;
    nmo_object_t *obj = nmo_object_repository_find_by_id(dctx->repo, behavior_id);
    const char *name = obj ? nmo_object_get_name(obj) : NULL;

    print_indent(dctx->out, depth);

    if (is_building_block && state) {
        const char *proto_name = NULL;
        nmo_behavior_registry_t *bb_reg = nmo_context_get_bb_registry(dctx->ctx);
        if (bb_reg) {
            proto_name = nmo_behavior_registry_get_name(bb_reg, state->block_guid);
        }
        if (proto_name) {
            fprintf(dctx->out, "[BB] #%u %s", behavior_id, proto_name);
            if (name && name[0] && strcmp(name, proto_name) != 0)
                fprintf(dctx->out, " (%s)", name);
        } else {
            char guid_buf[24];
            nmo_guid_format(state->block_guid, guid_buf, sizeof(guid_buf));
            fprintf(dctx->out, "[BB] #%u", behavior_id);
            if (name && name[0]) fprintf(dctx->out, " \"%s\"", name);
            fprintf(dctx->out, " {%s}", guid_buf);
        }
    } else {
        bool is_script = state && (state->flags & CKBEHAVIOR_SCRIPT);
        fprintf(dctx->out, "[%s] #%u",
                is_script ? "Script" : "Graph", behavior_id);
        if (name && name[0]) fprintf(dctx->out, " \"%s\"", name);
    }
    fprintf(dctx->out, "\n");

    if (!state) return true;

    /* Print parameters */
    dump_param_array(dctx, "  in", &state->in_parameters, depth);
    dump_param_array(dctx, "  out", &state->out_parameters, depth);
    dump_param_array(dctx, "  local", &state->local_parameters, depth);

    /* Print links */
    size_t link_count = state->sub_behavior_links.count;

    for (size_t i = 0; i < link_count; ++i) {
        nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
            &state->sub_behavior_links, i);
        if (link_id == 0) continue;
        nmo_object_t *lobj = nmo_object_repository_find_by_id(dctx->repo, link_id);
        if (!lobj) continue;

        const nmo_behaviorlink_state_t *link =
            (const nmo_behaviorlink_state_t *)nmo_object_get_state(lobj);
        if (!link) continue;

        print_indent(dctx->out, depth);
        fprintf(dctx->out, "  link: #%u -> #%u",
                link->in_io_id, link->out_io_id);
        if (link->activation_delay != 0) {
            fprintf(dctx->out, " (delay=%d)", (int)link->activation_delay);
        }
        fprintf(dctx->out, "\n");
    }

    return true;
}

nmo_status_t nmo_behavior_analyze_dump_text(
    nmo_workspace_t *workspace,
    nmo_object_id_t root_behavior_id,
    FILE *out)
{
    if (!workspace || !out) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to nmo_behavior_analyze_dump_text");
    }

    dump_ctx_t dctx;
    dctx.workspace = workspace;
    dctx.ctx = nmo_workspace_internal_context(workspace);
    dctx.registry = (nmo_type_registry_t *)nmo_workspace_internal_type_registry(workspace);
    dctx.repo = nmo_workspace_internal_repository(workspace);
    dctx.out = out;

    return nmo_behavior_walk(workspace, root_behavior_id,
                                  dump_visitor, &dctx);
}
