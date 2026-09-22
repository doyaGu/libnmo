/**
 * @file behavior_rewrite.c
 * @brief Behavior graph fold: analysis, write-blocker checks, and the fold drivers.
 */

#include "behavior_rewrite_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool rewrite_is_behavior_object(
    nmo_context_t *ctx,
    const nmo_object_t *object) {
    const nmo_type_registry_t *registry =
        ctx ? nmo_context_get_type_registry(ctx) : NULL;
    return nmo_type_query_object_is_derived_from_class(
        registry, object, NMO_CID_BEHAVIOR);
}

nmo_behavior_state_t *rewrite_behavior_state(
    nmo_context_t *ctx,
    nmo_object_t *object) {
    const nmo_type_registry_t *registry =
        ctx ? nmo_context_get_type_registry(ctx) : NULL;
    return (nmo_behavior_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(
            registry, object, CKPGUID_BEHAVIOR);
}

static nmo_status_t rewrite_fold_add_semantic_risks(
    nmo_workspace_t *workspace,
    nmo_behavior_fold_report_t *report) {
    if (!report) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return nmo_behavior_edit_collect_semantic_risks(
        workspace, &report->boundary,
        report->selected_nodes, report->selected_node_count,
        &report->semantic_risks, &report->semantic_risk_count);
}

static bool rewrite_id_in_set(const nmo_object_id_t *ids,
                              size_t count,
                              nmo_object_id_t id) {
    if (!ids || id == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

static nmo_status_t rewrite_copy_node_ids(nmo_object_id_t **out_ids,
                                          size_t *out_count,
                                          const nmo_object_id_t *ids,
                                          size_t count) {
    if (!out_ids || !out_count || (!ids && count > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_ids = NULL;
    *out_count = 0;
    if (count == 0) {
        return NMO_OK;
    }

    nmo_object_id_t *copy =
        (nmo_object_id_t *)malloc(count * sizeof(*copy));
    if (!copy) {
        return NMO_ERR_NOMEM;
    }
    memcpy(copy, ids, count * sizeof(*copy));
    *out_ids = copy;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t rewrite_copy_fold_maps(
    nmo_behavior_fold_map_t **out_maps,
    size_t *out_count,
    const nmo_behavior_fold_map_t *maps,
    size_t count) {
    if (!out_maps || !out_count || (!maps && count > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_maps = NULL;
    *out_count = 0;
    if (count == 0) {
        return NMO_OK;
    }

    nmo_behavior_fold_map_t *copy =
        (nmo_behavior_fold_map_t *)malloc(count * sizeof(*copy));
    if (!copy) {
        return NMO_ERR_NOMEM;
    }
    memcpy(copy, maps, count * sizeof(*copy));
    *out_maps = copy;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t rewrite_add_unique_delete_id(nmo_object_id_t **ids,
                                                 size_t *count,
                                                 size_t *capacity,
                                                 nmo_object_id_t id) {
    if (!ids || !count || !capacity || id == 0) {
        return NMO_OK;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*ids)[i] == id) {
            return NMO_OK;
        }
    }
    if (*count == *capacity) {
        size_t next_capacity = *capacity ? *capacity * 2u : 16u;
        nmo_object_id_t *next =
            (nmo_object_id_t *)realloc(*ids,
                                       next_capacity * sizeof(**ids));
        if (!next) {
            return NMO_ERR_NOMEM;
        }
        *ids = next;
        *capacity = next_capacity;
    }
    (*ids)[(*count)++] = id;
    return NMO_OK;
}

static nmo_status_t rewrite_add_array_delete_ids(nmo_object_id_t **ids,
                                                 size_t *count,
                                                 size_t *capacity,
                                                 const nmo_array_t *array,
                                                 nmo_object_id_t keep_id) {
    if (!array || array->count == 0 || !array->data) {
        return NMO_OK;
    }
    for (size_t i = 0; i < array->count; ++i) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        if (id == 0 || id == keep_id) {
            continue;
        }
        nmo_status_t rc = rewrite_add_unique_delete_id(ids, count,
                                                       capacity,
                                                       id);
        if (rc != NMO_OK) {
            return rc;
        }
    }
    return NMO_OK;
}

static nmo_status_t rewrite_build_nodes_to_delete(
    nmo_behavior_fold_report_t *report,
    const nmo_behavior_fold_desc_t *desc) {
    if (!report || !desc || desc->node_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t delete_count = desc->node_count > 0 ? desc->node_count - 1 : 0;
    if (delete_count == 0) {
        return NMO_OK;
    }

    report->nodes_to_delete =
        (nmo_object_id_t *)malloc(delete_count *
                                  sizeof(*report->nodes_to_delete));
    if (!report->nodes_to_delete) {
        return NMO_ERR_NOMEM;
    }

    size_t out = 0;
    for (size_t i = 0; i < desc->node_count; ++i) {
        if (desc->node_ids[i] != report->representative_id) {
            report->nodes_to_delete[out++] = desc->node_ids[i];
        }
    }
    report->nodes_to_delete_count = out;
    return NMO_OK;
}

static nmo_status_t rewrite_add_delete_control_link(
    nmo_behavior_fold_report_t *report,
    const nmo_behavior_graph_edge_t *edge) {
    if (!report || !edge) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t new_count = report->control_links_to_delete_count + 1;
    nmo_behavior_boundary_control_edge_t *new_edges =
        (nmo_behavior_boundary_control_edge_t *)realloc(
            report->control_links_to_delete,
            new_count * sizeof(*new_edges));
    if (!new_edges) {
        return NMO_ERR_NOMEM;
    }

    new_edges[report->control_links_to_delete_count] =
        (nmo_behavior_boundary_control_edge_t){
            .link_id = edge->link_id,
            .source_owner_id = edge->from_id,
            .source_io_id = edge->in_io_id,
            .target_owner_id = edge->to_id,
            .target_io_id = edge->out_io_id,
            .activation_delay = edge->activation_delay,
            .initial_activation_delay = edge->initial_activation_delay,
        };
    report->control_links_to_delete = new_edges;
    report->control_links_to_delete_count = new_count;
    return NMO_OK;
}

static nmo_status_t rewrite_build_delete_control_links(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_behavior_graph_t graph = {0};
    nmo_status_t rc = NMO_OK;

    if (!ctx || !workspace || !desc || !report) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!nmo_behavior_graph_build(workspace, desc->parent_id,
                                  UINT32_MAX, &graph)) {
        return NMO_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < graph.edge_count; ++i) {
        const nmo_behavior_graph_edge_t *edge = &graph.edges[i];
        if (!edge->kind || strcmp(edge->kind, "behavior_link") != 0) {
            continue;
        }

        bool source_internal = rewrite_id_in_set(desc->node_ids,
                                                 desc->node_count,
                                                 edge->from_id);
        bool target_internal = rewrite_id_in_set(desc->node_ids,
                                                 desc->node_count,
                                                 edge->to_id);
        if (source_internal && target_internal) {
            rc = rewrite_add_delete_control_link(report, edge);
            if (rc != NMO_OK) {
                break;
            }
        }
    }

    nmo_behavior_graph_free(&graph);
    return rc;
}

static nmo_status_t rewrite_fold_validate_map_indices(
    nmo_behavior_fold_report_t *report,
    const nmo_behavior_fold_map_t *maps,
    size_t map_count,
    size_t boundary_count,
    const char *code,
    const char *message) {
    if (!report || !maps || map_count == 0) {
        return NMO_OK;
    }

    for (size_t i = 0; i < map_count; ++i) {
        if ((size_t)maps[i].old_index >= boundary_count) {
            rewrite_fold_report_reject(report, code, message);
            return NMO_ERR_INVALID_ARGUMENT;
        }
        for (size_t j = i + 1; j < map_count; ++j) {
            if (maps[i].old_index == maps[j].old_index) {
                rewrite_fold_report_reject(report, code, message);
                return NMO_ERR_INVALID_ARGUMENT;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t rewrite_fold_validate_map_targets(
    nmo_behavior_fold_report_t *report,
    const nmo_behavior_fold_map_t *maps,
    size_t map_count,
    uint32_t old_index_base,
    size_t boundary_count,
    const char *code,
    const char *message) {
    if (!report || !maps || map_count == 0 || boundary_count <= 1u) {
        return NMO_OK;
    }

    uint32_t old_index_limit = old_index_base + (uint32_t)boundary_count;
    for (size_t i = 0; i < map_count; ++i) {
        if (maps[i].old_index < old_index_base ||
            maps[i].old_index >= old_index_limit) {
            continue;
        }
        for (size_t j = i + 1; j < map_count; ++j) {
            if (maps[j].old_index < old_index_base ||
                maps[j].old_index >= old_index_limit) {
                continue;
            }
            if (maps[i].new_index == maps[j].new_index) {
                rewrite_fold_report_reject(report, code, message);
                return NMO_ERR_INVALID_ARGUMENT;
            }
        }
    }
    return NMO_OK;
}

static nmo_status_t rewrite_fold_validate_explicit_maps(
    nmo_behavior_fold_report_t *report) {
    if (!report || !report->preserve_boundary) {
        return NMO_OK;
    }

    if (report->boundary.control_in_count > 1 &&
        report->input_map_count < report->boundary.control_in_count) {
        rewrite_fold_report_reject(
            report, "input_map_required",
            "preserve-boundary has multiple boundary control inputs; "
            "provide --map-input for each input edge");
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t rc = rewrite_fold_validate_map_indices(
        report, report->input_maps, report->input_map_count,
        report->boundary.control_in_count,
        "input_map_invalid",
        "preserve-boundary input maps must reference existing boundary "
        "control input edges exactly once");
    if (rc != NMO_OK) {
        return rc;
    }
    rc = rewrite_fold_validate_map_targets(
        report, report->input_maps, report->input_map_count, 0u,
        report->boundary.control_in_count,
        "input_map_invalid",
        "preserve-boundary input maps must target distinct anchor "
        "control inputs");
    if (rc != NMO_OK) {
        return rc;
    }
    if (report->boundary.control_out_count > 1 &&
        report->output_map_count < report->boundary.control_out_count) {
        rewrite_fold_report_reject(
            report, "output_map_required",
            "preserve-boundary has multiple boundary control outputs; "
            "provide --map-output for each output edge");
        return NMO_ERR_INVALID_ARGUMENT;
    }
    rc = rewrite_fold_validate_map_indices(
        report, report->output_maps, report->output_map_count,
        report->boundary.control_out_count,
        "output_map_invalid",
        "preserve-boundary output maps must reference existing boundary "
        "control output edges exactly once");
    if (rc != NMO_OK) {
        return rc;
    }
    rc = rewrite_fold_validate_map_targets(
        report, report->output_maps, report->output_map_count, 0u,
        report->boundary.control_out_count,
        "output_map_invalid",
        "preserve-boundary output maps must target distinct anchor "
        "control outputs");
    if (rc != NMO_OK) {
        return rc;
    }

    size_t parameter_edge_count = report->boundary.parameter_in_count +
                                  report->boundary.parameter_out_count;
    if (parameter_edge_count > 1 &&
        report->parameter_map_count < parameter_edge_count) {
        rewrite_fold_report_reject(
            report, "parameter_map_required",
            "preserve-boundary has multiple boundary parameter edges; "
            "provide --map-param for each parameter edge");
        return NMO_ERR_INVALID_ARGUMENT;
    }
    rc = rewrite_fold_validate_map_indices(
        report, report->parameter_maps, report->parameter_map_count,
        parameter_edge_count,
        "parameter_map_invalid",
        "preserve-boundary parameter maps must reference existing boundary "
        "parameter edges exactly once");
    if (rc != NMO_OK) {
        return rc;
    }
    rc = rewrite_fold_validate_map_targets(
        report, report->parameter_maps, report->parameter_map_count, 0u,
        report->boundary.parameter_in_count,
        "parameter_map_invalid",
        "preserve-boundary parameter input maps must target distinct "
        "anchor input parameters");
    if (rc != NMO_OK) {
        return rc;
    }
    rc = rewrite_fold_validate_map_targets(
        report, report->parameter_maps, report->parameter_map_count,
        (uint32_t)report->boundary.parameter_in_count,
        report->boundary.parameter_out_count,
        "parameter_map_invalid",
        "preserve-boundary parameter output maps must target distinct "
        "anchor output parameters");
    if (rc != NMO_OK) {
        return rc;
    }
    return NMO_OK;
}

static bool rewrite_fold_report_is_single_anchor_only(
    const nmo_behavior_fold_report_t *report) {
    return report &&
           report->selected_node_count == 1 &&
           report->nodes_to_delete_count == 0 &&
           report->control_links_to_delete_count == 0 &&
           report->boundary.parameter_in_count == 0 &&
           report->boundary.parameter_out_count == 0;
}

static bool rewrite_fold_report_is_closed_graph_anchor(
    const nmo_behavior_fold_report_t *report) {
    return report &&
           report->selected_node_count > 1 &&
           report->nodes_to_delete_count > 0;
}

static bool rewrite_behavior_state_is_leaf_bb(
    const nmo_behavior_state_t *state) {
    return state &&
           (state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0u &&
           (state->flags & CKBEHAVIOR_SCRIPT) == 0u &&
           state->sub_behaviors.count == 0 &&
           state->sub_behavior_links.count == 0 &&
           state->operations.count == 0;
}

static void rewrite_fold_report_clear_write_blockers(
    nmo_behavior_fold_report_t *report) {
    if (!report) {
        return;
    }
    free(report->write_blockers);
    report->write_blockers = NULL;
    report->write_blocker_count = 0;
}

static bool rewrite_fold_boundary_targets_are_writable(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_fold_report_t *report);

static bool rewrite_fold_selection_has_unselected_child(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_fold_report_t *report,
    nmo_object_id_t *out_missing_id);

static bool rewrite_fold_report_supports_single_anchor_write(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_fold_report_t *report) {
    if (!rewrite_fold_report_is_single_anchor_only(report)) {
        return false;
    }
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, report->anchor_id)
             : NULL;
    if (!anchor || !rewrite_is_behavior_object(ctx, anchor)) {
        return false;
    }
    return rewrite_behavior_state_is_leaf_bb(
        (const nmo_behavior_state_t *)rewrite_behavior_state(ctx, anchor));
}

static bool rewrite_fold_report_supports_closed_graph_write(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_fold_report_t *report) {
    if (!rewrite_fold_report_is_closed_graph_anchor(report)) {
        return false;
    }
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, report->anchor_id)
             : NULL;
    if (!anchor || !rewrite_is_behavior_object(ctx, anchor)) {
        return false;
    }
    const nmo_behavior_state_t *state = rewrite_behavior_state(ctx, anchor);
    if (!state ||
        (state->sub_behaviors.count == 0 &&
         !rewrite_behavior_state_is_leaf_bb(state))) {
        return false;
    }
    for (size_t i = 0; i < report->boundary.parameter_in_count; ++i) {
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->parameter_maps, report->parameter_map_count, (uint32_t)i);
        if ((size_t)new_index >= state->in_parameters.count) {
            return false;
        }
    }
    for (size_t i = 0; i < report->boundary.parameter_out_count; ++i) {
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->parameter_maps, report->parameter_map_count, (uint32_t)i);
        if ((size_t)new_index >= state->out_parameters.count) {
            return false;
        }
    }
    if (!rewrite_fold_boundary_targets_are_writable(ctx, repo, report)) {
        return false;
    }
    nmo_object_id_t missing_child_id = 0;
    return !rewrite_fold_selection_has_unselected_child(ctx, repo, report,
                                                        &missing_child_id);
}

static bool rewrite_selected_behavior_has_unselected_child(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *state,
    const nmo_object_id_t *selected_ids,
    size_t selected_count,
    nmo_object_id_t *out_missing_id) {
    if (!state || state->sub_behaviors.count == 0) {
        return false;
    }

    for (size_t i = 0; i < state->sub_behaviors.count; ++i) {
        nmo_object_id_t child_id = nmo_behavior_ref_array_get_id(
            &state->sub_behaviors, i);
        if (child_id == 0) continue;
        if (!rewrite_id_in_set(selected_ids, selected_count, child_id)) {
            if (out_missing_id) {
                *out_missing_id = child_id;
            }
            return true;
        }

        nmo_object_t *child =
            repo ? nmo_object_repository_find_by_id(repo, child_id)
                 : NULL;
        if (!child || !rewrite_is_behavior_object(ctx, child)) {
            continue;
        }
        const nmo_behavior_state_t *child_state =
            rewrite_behavior_state(ctx, child);
        if (rewrite_selected_behavior_has_unselected_child(
                ctx, repo, child_state, selected_ids, selected_count,
                out_missing_id)) {
            return true;
        }
    }
    return false;
}

static bool rewrite_fold_selection_has_unselected_child(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_fold_report_t *report,
    nmo_object_id_t *out_missing_id) {
    if (!report || !report->selected_nodes) {
        return false;
    }
    for (size_t i = 0; i < report->selected_node_count; ++i) {
        nmo_object_t *object = repo
            ? nmo_object_repository_find_by_id(repo, report->selected_nodes[i])
            : NULL;
        if (!object || !rewrite_is_behavior_object(ctx, object)) {
            continue;
        }
        const nmo_behavior_state_t *state =
            rewrite_behavior_state(ctx, object);
        if (rewrite_selected_behavior_has_unselected_child(
                ctx, repo, state, report->selected_nodes,
                report->selected_node_count, out_missing_id)) {
            return true;
        }
    }
    return false;
}

static nmo_status_t rewrite_fold_collect_delete_ids(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_fold_report_t *report,
    nmo_object_id_t **out_ids,
    size_t *out_count) {
    if (!ctx || !repo || !report || !out_ids || !out_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_ids = NULL;
    *out_count = 0;

    nmo_object_id_t *ids = NULL;
    size_t count = 0;
    size_t capacity = 0;

    for (size_t i = 0; i < report->nodes_to_delete_count; ++i) {
        nmo_status_t rc = rewrite_add_unique_delete_id(
            &ids, &count, &capacity, report->nodes_to_delete[i]);
        if (rc != NMO_OK) {
            free(ids);
            return rc;
        }
    }
    for (size_t i = 0; i < report->control_links_to_delete_count; ++i) {
        nmo_status_t rc = rewrite_add_unique_delete_id(
            &ids, &count, &capacity,
            report->control_links_to_delete[i].link_id);
        if (rc != NMO_OK) {
            free(ids);
            return rc;
        }
    }

    for (size_t i = 0; i < report->selected_node_count; ++i) {
        nmo_object_id_t behavior_id = report->selected_nodes[i];
        nmo_object_t *object =
            nmo_object_repository_find_by_id(repo, behavior_id);
        if (!object || !rewrite_is_behavior_object(ctx, object)) {
            continue;
        }
        const nmo_behavior_state_t *state =
            rewrite_behavior_state(ctx, object);
        if (!state) {
            continue;
        }
        nmo_status_t rc = NMO_OK;
        if (behavior_id != report->anchor_id) {
            rc = rewrite_add_array_delete_ids(
                &ids, &count, &capacity, &state->inputs, report->anchor_id);
            if (rc == NMO_OK) {
                rc = rewrite_add_array_delete_ids(
                    &ids, &count, &capacity, &state->outputs,
                    report->anchor_id);
            }
            if (rc == NMO_OK) {
                rc = rewrite_add_array_delete_ids(
                    &ids, &count, &capacity, &state->in_parameters,
                    report->anchor_id);
            }
            if (rc == NMO_OK) {
                rc = rewrite_add_array_delete_ids(
                    &ids, &count, &capacity, &state->out_parameters,
                    report->anchor_id);
            }
        }
        if (rc == NMO_OK) {
            rc = rewrite_add_array_delete_ids(
                &ids, &count, &capacity, &state->local_parameters,
                report->anchor_id);
        }
        if (rc == NMO_OK) {
            rc = rewrite_add_array_delete_ids(
                &ids, &count, &capacity, &state->sub_behavior_links,
                report->anchor_id);
        }
        if (rc == NMO_OK) {
            rc = rewrite_add_array_delete_ids(
                &ids, &count, &capacity, &state->operations,
                report->anchor_id);
        }
        if (rc != NMO_OK) {
            free(ids);
            return rc;
        }
    }

    *out_ids = ids;
    *out_count = count;
    return NMO_OK;
}

uint32_t rewrite_fold_mapped_new_index(
    const nmo_behavior_fold_map_t *maps,
    size_t map_count,
    uint32_t old_index) {
    for (size_t i = 0; maps && i < map_count; ++i) {
        if (maps[i].old_index == old_index) {
            return maps[i].new_index;
        }
    }
    return old_index;
}

nmo_status_t rewrite_fold_anchor_io_at(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    nmo_object_id_t anchor_id,
    bool input,
    uint32_t index,
    nmo_object_id_t *out_io_id) {
    if (!repo || !out_io_id) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_io_id = 0;
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, anchor_id) : NULL;
    if (!anchor || !rewrite_is_behavior_object(ctx, anchor)) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_behavior_state_t *state = rewrite_behavior_state(ctx, anchor);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    const nmo_array_t *ios = input ? &state->inputs : &state->outputs;
    if (index >= ios->count || !ios->data) {
        return NMO_ERR_NOT_FOUND;
    }

    *out_io_id = nmo_behavior_ref_array_get_id(ios, index);
    return *out_io_id != 0 ? NMO_OK : NMO_ERR_NOT_FOUND;
}

nmo_status_t rewrite_fold_anchor_parameter_at(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    nmo_object_id_t anchor_id,
    bool input,
    uint32_t index,
    nmo_object_id_t *out_parameter_id) {
    if (!repo || !out_parameter_id) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_parameter_id = 0;
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, anchor_id) : NULL;
    if (!anchor || !rewrite_is_behavior_object(ctx, anchor)) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_behavior_state_t *state = rewrite_behavior_state(ctx, anchor);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    const nmo_array_t *parameters = input ? &state->in_parameters
                                          : &state->out_parameters;
    if (index >= parameters->count || !parameters->data) {
        return NMO_ERR_NOT_FOUND;
    }

    *out_parameter_id = nmo_behavior_ref_array_get_id(parameters, index);
    return *out_parameter_id != 0 ? NMO_OK : NMO_ERR_NOT_FOUND;
}

static bool rewrite_fold_boundary_targets_are_writable(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_behavior_fold_report_t *report) {
    if (!ctx || !repo || !report || !report->preserve_boundary) {
        return true;
    }

    for (size_t i = 0; i < report->boundary.control_in_count; ++i) {
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->input_maps, report->input_map_count, (uint32_t)i);
        nmo_object_id_t io_id = 0;
        if (rewrite_fold_anchor_io_at(
                ctx, repo, report->anchor_id, true, new_index, &io_id) !=
            NMO_OK) {
            return false;
        }
    }

    for (size_t i = 0; i < report->boundary.control_out_count; ++i) {
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->output_maps, report->output_map_count, (uint32_t)i);
        nmo_object_id_t io_id = 0;
        if (rewrite_fold_anchor_io_at(
                ctx, repo, report->anchor_id, false, new_index, &io_id) !=
            NMO_OK) {
            return false;
        }
    }

    for (size_t i = 0; i < report->boundary.parameter_in_count; ++i) {
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->parameter_maps, report->parameter_map_count,
            (uint32_t)i);
        nmo_object_id_t parameter_id = 0;
        if (rewrite_fold_anchor_parameter_at(
                ctx, repo, report->anchor_id, true, new_index,
                &parameter_id) != NMO_OK) {
            return false;
        }
    }

    for (size_t i = 0; i < report->boundary.parameter_out_count; ++i) {
        uint32_t old_index = (uint32_t)report->boundary.parameter_in_count +
                             (uint32_t)i;
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->parameter_maps, report->parameter_map_count, old_index);
        nmo_object_id_t parameter_id = 0;
        if (rewrite_fold_anchor_parameter_at(
                ctx, repo, report->anchor_id, false, new_index,
                &parameter_id) != NMO_OK) {
            return false;
        }
    }

    return true;
}

void rewrite_fold_report_reject(nmo_behavior_fold_report_t *report,
                                       const char *code,
                                       const char *message) {
    if (!report) {
        return;
    }
    report->rejected = true;
    report->diagnostic_code = code;
    report->diagnostic_message = message;
}

static nmo_status_t rewrite_fold_add_write_blocker(
    nmo_behavior_fold_report_t *report,
    const char *code,
    const char *message) {
    if (!report || !code || !message) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t new_count = report->write_blocker_count + 1;
    nmo_behavior_fold_write_blocker_t *new_blockers =
        (nmo_behavior_fold_write_blocker_t *)realloc(
            report->write_blockers, new_count * sizeof(*new_blockers));
    if (!new_blockers) {
        return NMO_ERR_NOMEM;
    }

    new_blockers[report->write_blocker_count] =
        (nmo_behavior_fold_write_blocker_t){
            .code = code,
            .message = message,
        };
    report->write_blockers = new_blockers;
    report->write_blocker_count = new_count;
    return NMO_OK;
}

static nmo_status_t rewrite_fold_analyze_workspace(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_object_repository_t *repo = NULL;
    if (report) {
        memset(report, 0, sizeof(*report));
    }
    if (!ctx || !workspace || !desc || !report || desc->parent_id == 0 ||
        !desc->node_ids || desc->node_count == 0 ||
        nmo_guid_is_null(desc->block_guid)) {
        rewrite_fold_report_reject(report, "invalid_argument",
                                   "Invalid behavior fold analysis arguments");
        return NMO_ERR_INVALID_ARGUMENT;
    }
    repo = nmo_workspace_internal_repository(workspace);
    if (!repo) {
        rewrite_fold_report_reject(report, "invalid_state",
                                   "Behavior fold repository is unavailable");
        return NMO_ERR_INVALID_STATE;
    }

    report->analysis_only = true;
    report->can_write = false;
    nmo_object_id_t anchor_id =
        desc->anchor_id != 0 ? desc->anchor_id : desc->node_ids[0];
    report->parent_id = desc->parent_id;
    report->anchor_id = anchor_id;
    report->representative_id = anchor_id;
    report->target_guid = desc->block_guid;
    report->target_name = desc->name;
    report->target_version =
        desc->block_version != 0 ? desc->block_version : 65536u;
    report->preserve_boundary = desc->preserve_boundary;
    report->preserve_links = desc->preserve_boundary || desc->preserve_links;
    report->preserve_params = desc->preserve_boundary || desc->preserve_params;
    report->interface_mode = desc->interface_mode;

    if (rewrite_id_in_set(desc->node_ids, desc->node_count,
                          desc->parent_id)) {
        rewrite_fold_report_reject(
            report, "parent_selected",
            "Selected fold nodes must not include the parent behavior");
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (desc->anchor_id != 0 &&
        !rewrite_id_in_set(desc->node_ids, desc->node_count,
                           desc->anchor_id)) {
        rewrite_fold_report_reject(
            report, "anchor_not_selected",
            "Fold anchor must be one of the selected nodes");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t rc = rewrite_copy_node_ids(&report->selected_nodes,
                                            &report->selected_node_count,
                                            desc->node_ids,
                                            desc->node_count);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "out_of_memory",
                                   "Failed to copy selected fold nodes");
        goto fail;
    }

    rc = rewrite_copy_fold_maps(&report->input_maps,
                                &report->input_map_count,
                                desc->input_maps,
                                desc->input_map_count);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "out_of_memory",
                                   "Failed to copy fold input maps");
        goto fail;
    }

    rc = rewrite_copy_fold_maps(&report->output_maps,
                                &report->output_map_count,
                                desc->output_maps,
                                desc->output_map_count);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "out_of_memory",
                                   "Failed to copy fold output maps");
        goto fail;
    }

    rc = rewrite_copy_fold_maps(&report->parameter_maps,
                                &report->parameter_map_count,
                                desc->parameter_maps,
                                desc->parameter_map_count);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "out_of_memory",
                                   "Failed to copy fold parameter maps");
        goto fail;
    }

    rc = rewrite_fold_add_write_blocker(
        report, "analysis_only",
        "Behavior fold write mode is not implemented yet");
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "out_of_memory",
                                   "Failed to record fold write blocker");
        goto fail;
    }

    rc = rewrite_build_nodes_to_delete(report, desc);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "out_of_memory",
                                   "Failed to build fold delete node plan");
        goto fail;
    }

    if (!nmo_behavior_boundary_build_for_nodes(workspace,
                                               desc->parent_id,
                                               desc->node_ids,
                                               desc->node_count,
                                               &report->boundary)) {
        nmo_error_code_t code = nmo_last_error_code();
        rewrite_fold_report_reject(report, "boundary_failed",
                                   "Failed to build selected fold boundary");
        rc = (code == NMO_ERR_INVALID_ARGUMENT || code == NMO_ERR_NOT_FOUND)
            ? code
            : NMO_ERR_INVALID_STATE;
        goto fail;
    }

    rc = rewrite_fold_add_semantic_risks(workspace, report);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "out_of_memory",
                                   "Failed to build fold semantic risks");
        goto fail;
    }

    rc = rewrite_fold_validate_explicit_maps(report);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = rewrite_build_delete_control_links(ctx, workspace, desc, report);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "control_plan_failed",
                                   "Failed to build fold control plan");
        goto fail;
    }

    if (rewrite_fold_report_supports_single_anchor_write(ctx, repo,
                                                         report)) {
        rewrite_fold_report_clear_write_blockers(report);
        report->analysis_only = false;
        report->can_write = true;
    } else if (rewrite_fold_report_supports_closed_graph_write(ctx, repo,
                                                               report)) {
        rewrite_fold_report_clear_write_blockers(report);
        report->analysis_only = false;
        report->can_write = true;
    }

    return NMO_OK;

fail:
    nmo_behavior_edit_fold_report_free(report);
    return rc;
}

static nmo_status_t rewrite_fold_apply_script_tx(
    nmo_script_edit_tx_t *tx,
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report);

/* Workspace-level fold: one script edit transaction around the shared
 * transactional implementation, so every mutation path goes through
 * nmo_script_edit. */
static nmo_status_t rewrite_fold_apply_workspace(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = nmo_script_edit_begin(workspace, "behavior fold", &tx);
    if (rc != NMO_OK) {
        if (report) {
            memset(report, 0, sizeof(*report));
        }
        rewrite_fold_report_reject(report, "edit_begin_failed",
                                   "Failed to begin behavior fold edit");
        return rc;
    }
    rc = rewrite_fold_apply_script_tx(
        tx, ctx, nmo_script_edit_workspace(tx),
        nmo_script_edit_workspace_edit(tx), desc, report);
    if (rc != NMO_OK) {
        nmo_script_edit_rollback(tx);
        return rc;
    }
    rc = nmo_script_edit_commit(tx);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "commit_failed",
                                   "Failed to commit behavior fold");
    }
    return rc;
}

static nmo_status_t rewrite_fold_apply_script_tx(
    nmo_script_edit_tx_t *tx,
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_status_t rc = rewrite_fold_analyze_workspace(ctx, workspace, desc, report);
    if (rc != NMO_OK) {
        return rc;
    }
    if (!report->preserve_boundary) {
        rewrite_fold_report_reject(
            report, "preserve_boundary_required",
            "Behavior fold write requires preserve-boundary");
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_id_t missing_child_id = 0;
    if (rewrite_fold_selection_has_unselected_child(
            ctx, nmo_workspace_internal_repository(workspace), report,
            &missing_child_id)) {
        (void)missing_child_id;
        rewrite_fold_report_reject(
            report, "selection_not_closed",
            "Selected graph fold must include child behavior from every "
            "selected graph or script");
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (rewrite_fold_report_is_single_anchor_only(report)) {
        nmo_object_repository_t *repo =
            nmo_workspace_internal_repository(workspace);
        nmo_object_t *anchor =
            repo ? nmo_object_repository_find_by_id(repo, report->anchor_id)
                 : NULL;
        if (!anchor || !rewrite_is_behavior_object(ctx, anchor)) {
            rewrite_fold_report_reject(report, "anchor_not_found",
                                       "Fold anchor behavior was not found");
            return NMO_ERR_NOT_FOUND;
        }
        nmo_behavior_state_t *state = rewrite_behavior_state(ctx, anchor);
        if (!rewrite_behavior_state_is_leaf_bb(state)) {
            rewrite_fold_report_reject(
                report, "anchor_not_leaf",
                "Single-node fold requires a leaf BB anchor");
            return NMO_ERR_INVALID_ARGUMENT;
        }
        rc = rewrite_fold_transform_anchor_in_edit(
            ctx, workspace, edit, desc, report, false);
        if (rc != NMO_OK) {
            return rc;
        }
        rewrite_fold_report_clear_write_blockers(report);
        report->analysis_only = false;
        report->can_write = true;
        return NMO_OK;
    }
    if (rewrite_fold_report_is_closed_graph_anchor(report) &&
        report->can_write) {
        rc = rewrite_fold_rewire_control_boundary_in_edit(
            ctx, workspace, edit, report);
        if (rc != NMO_OK) {
            return rc;
        }
        rc = rewrite_fold_rewire_parameter_boundary_in_edit(
            ctx, workspace, edit, report);
        if (rc != NMO_OK) {
            return rc;
        }

        nmo_object_id_t *delete_ids = NULL;
        size_t delete_count = 0;
        rc = rewrite_fold_collect_delete_ids(
            ctx, nmo_workspace_internal_repository(workspace), report,
            &delete_ids, &delete_count);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(report, "delete_plan_failed",
                                       "Failed to build fold delete set");
            return rc;
        }
        rc = nmo_script_edit_defer_destroy_objects(
            tx, delete_ids, delete_count);
        free(delete_ids);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(report, "delete_failed",
                                       "Failed to defer folded graph objects");
            return rc;
        }

        rc = rewrite_fold_transform_anchor_in_edit(
            ctx, workspace, edit, desc, report, true);
        if (rc != NMO_OK) {
            return rc;
        }

        rewrite_fold_report_clear_write_blockers(report);
        report->analysis_only = false;
        report->can_write = true;
        return NMO_OK;
    }
    if (!report->can_write) {
        if (report->write_blocker_count > 0) {
            rewrite_fold_report_reject(report,
                                       report->write_blockers[0].code,
                                       report->write_blockers[0].message);
        } else {
            rewrite_fold_report_reject(
                report, "unsupported",
                "Behavior fold write mode is not supported");
        }
        return NMO_ERR_INVALID_STATE;
    }

    rewrite_fold_report_reject(report, "unsupported",
                               "Behavior fold write mode is not supported");
    return NMO_ERR_NOT_IMPLEMENTED;
}

void nmo_behavior_edit_fold_report_free(nmo_behavior_fold_report_t *report) {
    if (!report) {
        return;
    }
    free(report->selected_nodes);
    free(report->nodes_to_delete);
    free(report->input_maps);
    free(report->output_maps);
    free(report->parameter_maps);
    free(report->control_links_to_delete);
    free(report->write_blockers);
    free(report->semantic_risks);
    nmo_behavior_boundary_free(&report->boundary);
    memset(report, 0, sizeof(*report));
}

NMO_API nmo_status_t nmo_behavior_edit_fold_analyze(
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    if (!workspace || !ctx) {
        if (report) {
            memset(report, 0, sizeof(*report));
            rewrite_fold_report_reject(report, "invalid_argument",
                                       "Invalid behavior fold analysis arguments");
        }
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return rewrite_fold_analyze_workspace(ctx, workspace, desc, report);
}

NMO_API nmo_status_t nmo_behavior_edit_fold_apply(
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    if (!workspace || !ctx) {
        if (report) {
            memset(report, 0, sizeof(*report));
            rewrite_fold_report_reject(report, "invalid_argument",
                                       "Invalid behavior fold analysis arguments");
        }
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return rewrite_fold_apply_workspace(ctx, workspace, desc, report);
}

NMO_API nmo_status_t nmo_behavior_edit_fold(
    nmo_workspace_t *workspace,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    if (!workspace || !ctx) {
        if (report) {
            memset(report, 0, sizeof(*report));
            rewrite_fold_report_reject(report, "invalid_argument",
                                       "Invalid behavior fold analysis arguments");
        }
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return rewrite_fold_apply_workspace(ctx, workspace, desc, report);
}

NMO_API nmo_status_t nmo_behavior_edit_fold_in_script_tx(
    nmo_script_edit_tx_t *tx,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    nmo_workspace_edit_t *edit = nmo_script_edit_workspace_edit(tx);
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    if (!tx || !workspace || !edit || !ctx) {
        if (report) {
            memset(report, 0, sizeof(*report));
            rewrite_fold_report_reject(
                report, "invalid_argument",
                "Invalid behavior fold transaction arguments");
        }
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return rewrite_fold_apply_script_tx(tx, ctx, workspace, edit, desc, report);
}
