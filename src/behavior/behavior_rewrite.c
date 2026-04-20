#include "behavior/nmo_behavior_rewrite.h"

#include "behavior/nmo_behavior_boundary.h"
#include "behavior/nmo_behavior_graph.h"
#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_statesave_ids.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool rewrite_is_behavior_class(nmo_context_t *ctx,
                                      nmo_class_id_t class_id) {
    const nmo_type_registry_t *registry =
        ctx ? nmo_context_get_type_registry(ctx) : NULL;
    if (!registry) {
        return class_id == NMO_CID_BEHAVIOR;
    }
    return nmo_type_registry_is_class_derived_from(
        registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEHAVIOR);
}

static bool rewrite_control_edges_equal(
    const nmo_behavior_boundary_control_edge_t *a,
    const nmo_behavior_boundary_control_edge_t *b) {
    return a->link_id == b->link_id &&
           a->source_owner_id == b->source_owner_id &&
           a->source_io_id == b->source_io_id &&
           a->target_owner_id == b->target_owner_id &&
           a->target_io_id == b->target_io_id &&
           a->activation_delay == b->activation_delay &&
           a->initial_activation_delay == b->initial_activation_delay;
}

static bool rewrite_parameter_edges_equal(
    const nmo_behavior_boundary_parameter_edge_t *a,
    const nmo_behavior_boundary_parameter_edge_t *b) {
    return a->source_parameter_id == b->source_parameter_id &&
           a->target_parameter_id == b->target_parameter_id &&
           a->source_owner_id == b->source_owner_id &&
           a->target_owner_id == b->target_owner_id &&
           nmo_guid_equals(a->type_guid, b->type_guid) &&
           a->shared == b->shared;
}

static bool rewrite_control_edge_sets_equal(
    const nmo_behavior_boundary_control_edge_t *a,
    size_t a_count,
    const nmo_behavior_boundary_control_edge_t *b,
    size_t b_count) {
    if (a_count != b_count) {
        return false;
    }
    for (size_t i = 0; i < a_count; ++i) {
        if (!rewrite_control_edges_equal(&a[i], &b[i])) {
            return false;
        }
    }
    return true;
}

static bool rewrite_parameter_edge_sets_equal(
    const nmo_behavior_boundary_parameter_edge_t *a,
    size_t a_count,
    const nmo_behavior_boundary_parameter_edge_t *b,
    size_t b_count) {
    if (a_count != b_count) {
        return false;
    }
    for (size_t i = 0; i < a_count; ++i) {
        if (!rewrite_parameter_edges_equal(&a[i], &b[i])) {
            return false;
        }
    }
    return true;
}

static bool rewrite_array_ids_equal(const nmo_array_t *a,
                                    const nmo_array_t *b) {
    if (!a || !b || a->count != b->count) {
        return false;
    }
    if (a->count == 0) {
        return true;
    }
    if (a->element_size != sizeof(nmo_object_id_t) ||
        b->element_size != sizeof(nmo_object_id_t)) {
        return false;
    }
    return a->data && b->data &&
           memcmp(a->data, b->data,
                  a->count * sizeof(nmo_object_id_t)) == 0;
}

static void rewrite_report_reject(nmo_behavior_rewrite_report_t *report,
                                  const char *code,
                                  const char *message) {
    if (!report) {
        return;
    }
    report->diagnostic_code = code;
    report->diagnostic_message = message;
    report->diagnostics_count = 1;
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
    const nmo_object_id_t *array_ids =
        NMO_ARRAY_DATA(nmo_object_id_t, array);
    for (size_t i = 0; i < array->count; ++i) {
        if (array_ids[i] == keep_id) {
            continue;
        }
        nmo_status_t rc = rewrite_add_unique_delete_id(ids, count,
                                                       capacity,
                                                       array_ids[i]);
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
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_behavior_graph_t graph = {0};
    if (!nmo_behavior_graph_build(ctx, session, desc->parent_id,
                                  UINT32_MAX, &graph)) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_status_t rc = NMO_OK;
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

static void rewrite_fold_report_reject(nmo_behavior_fold_report_t *report,
                                       const char *code,
                                       const char *message);

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

static uint32_t rewrite_fold_mapped_new_index(
    const nmo_behavior_fold_map_t *maps,
    size_t map_count,
    uint32_t old_index);

static bool rewrite_fold_selection_has_unselected_child(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_report_t *report,
    nmo_object_id_t *out_missing_id);

static bool rewrite_fold_report_supports_single_anchor_write(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_report_t *report) {
    if (!rewrite_fold_report_is_single_anchor_only(report)) {
        return false;
    }
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, report->anchor_id)
             : NULL;
    if (!anchor ||
        !rewrite_is_behavior_class(ctx, nmo_object_get_class_id(anchor))) {
        return false;
    }
    return rewrite_behavior_state_is_leaf_bb(
        (const nmo_behavior_state_t *)nmo_object_get_state(anchor));
}

static bool rewrite_fold_report_supports_closed_graph_write(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_report_t *report) {
    if (!rewrite_fold_report_is_closed_graph_anchor(report)) {
        return false;
    }
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, report->anchor_id)
             : NULL;
    if (!anchor ||
        !rewrite_is_behavior_class(ctx, nmo_object_get_class_id(anchor))) {
        return false;
    }
    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(anchor);
    if (!state || state->sub_behaviors.count == 0) {
        return false;
    }
    if (report->boundary.parameter_in_count > 0) {
        return false;
    }
    for (size_t i = 0; i < report->boundary.parameter_out_count; ++i) {
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->parameter_maps, report->parameter_map_count, (uint32_t)i);
        if ((size_t)new_index >= state->out_parameters.count) {
            return false;
        }
    }
    nmo_object_id_t missing_child_id = 0;
    return !rewrite_fold_selection_has_unselected_child(ctx, session, report,
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

    const nmo_object_id_t *child_ids =
        NMO_ARRAY_DATA(nmo_object_id_t, &state->sub_behaviors);
    for (size_t i = 0; i < state->sub_behaviors.count; ++i) {
        nmo_object_id_t child_id = child_ids[i];
        if (!rewrite_id_in_set(selected_ids, selected_count, child_id)) {
            if (out_missing_id) {
                *out_missing_id = child_id;
            }
            return true;
        }

        nmo_object_t *child =
            repo ? nmo_object_repository_find_by_id(repo, child_id)
                 : NULL;
        if (!child ||
            !rewrite_is_behavior_class(ctx, nmo_object_get_class_id(child))) {
            continue;
        }
        const nmo_behavior_state_t *child_state =
            (const nmo_behavior_state_t *)nmo_object_get_state(child);
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
    nmo_session_t *session,
    const nmo_behavior_fold_report_t *report,
    nmo_object_id_t *out_missing_id) {
    if (!report || !report->selected_nodes) {
        return false;
    }
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    for (size_t i = 0; i < report->selected_node_count; ++i) {
        nmo_object_t *object = repo
            ? nmo_object_repository_find_by_id(repo, report->selected_nodes[i])
            : NULL;
        if (!object ||
            !rewrite_is_behavior_class(ctx, nmo_object_get_class_id(object))) {
            continue;
        }
        const nmo_behavior_state_t *state =
            (const nmo_behavior_state_t *)nmo_object_get_state(object);
        if (rewrite_selected_behavior_has_unselected_child(
                ctx, repo, state, report->selected_nodes,
                report->selected_node_count, out_missing_id)) {
            return true;
        }
    }
    return false;
}

static nmo_status_t rewrite_fold_collect_delete_ids(
    nmo_session_t *session,
    const nmo_behavior_fold_report_t *report,
    nmo_object_id_t **out_ids,
    size_t *out_count) {
    if (!session || !report || !out_ids || !out_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_ids = NULL;
    *out_count = 0;

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        return NMO_ERR_INVALID_STATE;
    }

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
        if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
            continue;
        }
        const nmo_behavior_state_t *state =
            (const nmo_behavior_state_t *)nmo_object_get_state(object);
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

static nmo_status_t rewrite_fold_transform_anchor(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report,
    bool clear_graph_state) {
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, report->anchor_id)
             : NULL;
    if (!anchor ||
        !rewrite_is_behavior_class(ctx, nmo_object_get_class_id(anchor))) {
        rewrite_fold_report_reject(report, "anchor_not_found",
                                   "Fold anchor behavior was not found");
        return NMO_ERR_NOT_FOUND;
    }
    nmo_behavior_state_t *state =
        (nmo_behavior_state_t *)nmo_object_get_state(anchor);
    if (!state) {
        rewrite_fold_report_reject(report, "anchor_invalid",
                                   "Fold anchor behavior state is unavailable");
        return NMO_ERR_INVALID_STATE;
    }

    nmo_session_edit_t *edit = NULL;
    nmo_status_t rc =
        nmo_session_edit_begin(session, "behavior fold anchor", &edit);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "edit_begin_failed",
                                   "Failed to begin behavior fold edit");
        return rc;
    }
    rc = nmo_session_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "snapshot_failed",
                                   "Failed to snapshot fold anchor");
        nmo_session_edit_rollback(edit);
        return rc;
    }

    state->flags |= CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION;
    state->flags &= ~CKBEHAVIOR_SCRIPT;
    state->priority = 0;
    state->block_guid = desc->block_guid;
    state->block_version =
        desc->block_version != 0 ? desc->block_version : 65536u;

    if (clear_graph_state) {
        nmo_array_clear(&state->sub_behaviors);
        nmo_array_clear(&state->sub_behavior_chunks);
        nmo_array_clear(&state->sub_behavior_links);
        nmo_array_clear(&state->operations);
        nmo_array_clear(&state->local_parameters);
        nmo_array_clear(&state->local_parameter_chunks);
        state->save_flags &= ~(CK_STATESAVE_BEHAVIORSUBBEHAV |
                               CK_STATESAVE_BEHAVIORSUBLINKS |
                               CK_STATESAVE_BEHAVIOROPERATIONS |
                               CK_STATESAVE_BEHAVIORLOCALPARAMS);
        state->has_save_flags = true;
    }

    if (desc->name && desc->name[0] != '\0') {
        rc = nmo_session_edit_rename_object(edit, report->anchor_id,
                                            desc->name);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(report, "rename_failed",
                                       "Failed to rename fold anchor");
            nmo_session_edit_rollback(edit);
            return rc;
        }
    }

    nmo_session_edit_mark(
        edit, NMO_SESSION_EDIT_OBJECT_STATE |
              (clear_graph_state ? (NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
                                    NMO_SESSION_EDIT_REFERENCES)
                                 : 0u));
    rc = nmo_session_edit_commit(edit);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "commit_failed",
                                   "Failed to commit behavior fold");
        return rc;
    }
    return NMO_OK;
}

static uint32_t rewrite_fold_mapped_new_index(
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

static nmo_status_t rewrite_fold_anchor_io_at(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t anchor_id,
    bool input,
    uint32_t index,
    nmo_object_id_t *out_io_id) {
    if (!session || !out_io_id) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_io_id = 0;

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, anchor_id) : NULL;
    if (!anchor ||
        !rewrite_is_behavior_class(ctx, nmo_object_get_class_id(anchor))) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(anchor);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    const nmo_array_t *ios = input ? &state->inputs : &state->outputs;
    if (index >= ios->count || !ios->data) {
        return NMO_ERR_NOT_FOUND;
    }

    const nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, ios);
    *out_io_id = ids[index];
    return *out_io_id != 0 ? NMO_OK : NMO_ERR_NOT_FOUND;
}

static nmo_status_t rewrite_fold_anchor_parameter_at(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_object_id_t anchor_id,
    bool input,
    uint32_t index,
    nmo_object_id_t *out_parameter_id) {
    if (!session || !out_parameter_id) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_parameter_id = 0;

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, anchor_id) : NULL;
    if (!anchor ||
        !rewrite_is_behavior_class(ctx, nmo_object_get_class_id(anchor))) {
        return NMO_ERR_NOT_FOUND;
    }
    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(anchor);
    if (!state) {
        return NMO_ERR_INVALID_STATE;
    }
    const nmo_array_t *parameters = input ? &state->in_parameters
                                          : &state->out_parameters;
    if (index >= parameters->count || !parameters->data) {
        return NMO_ERR_NOT_FOUND;
    }

    const nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, parameters);
    *out_parameter_id = ids[index];
    return *out_parameter_id != 0 ? NMO_OK : NMO_ERR_NOT_FOUND;
}

static bool rewrite_parameterout_has_destination(
    const nmo_parameterout_state_t *state,
    nmo_object_id_t target_id) {
    if (!state || !state->destination_ids || target_id == 0) {
        return false;
    }
    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (state->destination_ids[i] == target_id) {
            return true;
        }
    }
    return false;
}

static nmo_status_t rewrite_parameterout_add_destination(
    nmo_parameterout_state_t *state,
    nmo_object_id_t target_id) {
    if (!state || target_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (rewrite_parameterout_has_destination(state, target_id)) {
        return NMO_OK;
    }
    nmo_object_id_t *next = (nmo_object_id_t *)realloc(
        state->destination_ids,
        (size_t)(state->destination_count + 1u) * sizeof(*next));
    if (!next) {
        return NMO_ERR_NOMEM;
    }
    state->destination_ids = next;
    state->destination_ids[state->destination_count++] = target_id;
    return NMO_OK;
}

static void rewrite_parameterout_remove_destination(
    nmo_parameterout_state_t *state,
    nmo_object_id_t target_id) {
    if (!state || !state->destination_ids || target_id == 0) {
        return;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (state->destination_ids[i] != target_id) {
            state->destination_ids[kept++] = state->destination_ids[i];
        }
    }
    state->destination_count = kept;
    if (kept == 0) {
        free(state->destination_ids);
        state->destination_ids = NULL;
    }
}

static nmo_status_t rewrite_fold_rewire_control_boundary(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_behavior_fold_report_t *report) {
    if (!session || !report) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (report->boundary.control_in_count == 0 &&
        report->boundary.control_out_count == 0) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_session_edit_t *edit = NULL;
    nmo_status_t rc =
        nmo_session_edit_begin(session, "behavior fold boundary", &edit);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "edit_begin_failed",
                                   "Failed to begin fold boundary edit");
        return rc;
    }

    for (size_t i = 0; i < report->boundary.control_in_count; ++i) {
        const nmo_behavior_boundary_control_edge_t *edge =
            &report->boundary.control_in[i];
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->input_maps, report->input_map_count, (uint32_t)i);
        nmo_object_id_t new_io_id = 0;
        rc = rewrite_fold_anchor_io_at(ctx, session, report->anchor_id,
                                       true, new_index, &new_io_id);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "input_map_target_missing",
                "Fold input map does not resolve to an anchor input");
            nmo_session_edit_rollback(edit);
            return rc;
        }

        nmo_object_t *link_obj =
            nmo_object_repository_find_by_id(repo, edge->link_id);
        nmo_behaviorlink_state_t *link_state = link_obj
            ? (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
            : NULL;
        if (!link_state) {
            rewrite_fold_report_reject(
                report, "control_link_missing",
                "Boundary control link was not found");
            nmo_session_edit_rollback(edit);
            return NMO_ERR_NOT_FOUND;
        }
        rc = nmo_session_edit_snapshot_bytes(edit, link_state,
                                             sizeof(*link_state));
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "snapshot_failed",
                "Failed to snapshot boundary control link");
            nmo_session_edit_rollback(edit);
            return rc;
        }

        /* CK2/SDK naming is counterintuitive: link in_io_id is the source IO,
         * and link out_io_id is the target IO. Keep graph edge direction
         * source owner -> target owner. */
        link_state->out_io_id = new_io_id;
    }

    for (size_t i = 0; i < report->boundary.control_out_count; ++i) {
        const nmo_behavior_boundary_control_edge_t *edge =
            &report->boundary.control_out[i];
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->output_maps, report->output_map_count, (uint32_t)i);
        nmo_object_id_t new_io_id = 0;
        rc = rewrite_fold_anchor_io_at(ctx, session, report->anchor_id,
                                       false, new_index, &new_io_id);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "output_map_target_missing",
                "Fold output map does not resolve to an anchor output");
            nmo_session_edit_rollback(edit);
            return rc;
        }

        nmo_object_t *link_obj =
            nmo_object_repository_find_by_id(repo, edge->link_id);
        nmo_behaviorlink_state_t *link_state = link_obj
            ? (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
            : NULL;
        if (!link_state) {
            rewrite_fold_report_reject(
                report, "control_link_missing",
                "Boundary control link was not found");
            nmo_session_edit_rollback(edit);
            return NMO_ERR_NOT_FOUND;
        }
        rc = nmo_session_edit_snapshot_bytes(edit, link_state,
                                             sizeof(*link_state));
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "snapshot_failed",
                "Failed to snapshot boundary control link");
            nmo_session_edit_rollback(edit);
            return rc;
        }

        /* CK2/SDK naming is counterintuitive: link in_io_id is the source IO,
         * and link out_io_id is the target IO. Keep graph edge direction
         * source owner -> target owner. */
        link_state->in_io_id = new_io_id;
    }

    nmo_session_edit_mark(edit, NMO_SESSION_EDIT_BEHAVIOR_GRAPH |
                                NMO_SESSION_EDIT_REFERENCES);
    rc = nmo_session_edit_commit(edit);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "commit_failed",
                                   "Failed to commit fold boundary edit");
        return rc;
    }
    return NMO_OK;
}

static nmo_status_t rewrite_fold_rewire_parameter_boundary(
    nmo_context_t *ctx,
    nmo_session_t *session,
    nmo_behavior_fold_report_t *report) {
    if (!session || !report) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (report->boundary.parameter_in_count > 0) {
        rewrite_fold_report_reject(
            report, "parameter_input_unsupported",
            "Fold parameter input rewrites are not implemented yet");
        return NMO_ERR_INVALID_STATE;
    }
    if (report->boundary.parameter_out_count == 0) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_session_edit_t *edit = NULL;
    nmo_status_t rc =
        nmo_session_edit_begin(session, "behavior fold parameters", &edit);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "edit_begin_failed",
                                   "Failed to begin fold parameter edit");
        return rc;
    }

    for (size_t i = 0; i < report->boundary.parameter_out_count; ++i) {
        const nmo_behavior_boundary_parameter_edge_t *edge =
            &report->boundary.parameter_out[i];
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->parameter_maps, report->parameter_map_count, (uint32_t)i);
        nmo_object_id_t new_parameter_id = 0;
        rc = rewrite_fold_anchor_parameter_at(
            ctx, session, report->anchor_id, false, new_index,
            &new_parameter_id);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "parameter_map_target_missing",
                "Fold parameter map does not resolve to an anchor output "
                "parameter");
            nmo_session_edit_rollback(edit);
            return rc;
        }

        nmo_object_t *target_obj =
            nmo_object_repository_find_by_id(repo, edge->target_parameter_id);
        nmo_parameterin_state_t *target_in = target_obj &&
            nmo_object_get_class_id(target_obj) == NMO_CID_PARAMETERIN
            ? (nmo_parameterin_state_t *)nmo_object_get_state(target_obj)
            : NULL;
        if (!target_in) {
            rewrite_fold_report_reject(
                report, "parameter_target_missing",
                "Fold parameter target input was not found");
            nmo_session_edit_rollback(edit);
            return NMO_ERR_NOT_FOUND;
        }
        rc = nmo_session_edit_snapshot_bytes(edit, target_in,
                                             sizeof(*target_in));
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "snapshot_failed",
                "Failed to snapshot fold parameter input");
            nmo_session_edit_rollback(edit);
            return rc;
        }
        target_in->source_id = new_parameter_id;
        target_in->is_shared = edge->shared ? 1u : 0u;

        nmo_object_t *new_source_obj =
            nmo_object_repository_find_by_id(repo, new_parameter_id);
        nmo_parameterout_state_t *new_source_out = new_source_obj &&
            nmo_object_get_class_id(new_source_obj) == NMO_CID_PARAMETEROUT
            ? (nmo_parameterout_state_t *)nmo_object_get_state(new_source_obj)
            : NULL;
        if (new_source_out) {
            rc = nmo_session_edit_snapshot_bytes(edit, new_source_out,
                                                 sizeof(*new_source_out));
            if (rc != NMO_OK) {
                rewrite_fold_report_reject(
                    report, "snapshot_failed",
                    "Failed to snapshot fold parameter output");
                nmo_session_edit_rollback(edit);
                return rc;
            }
            rc = rewrite_parameterout_add_destination(
                new_source_out, edge->target_parameter_id);
            if (rc != NMO_OK) {
                rewrite_fold_report_reject(
                    report, "out_of_memory",
                    "Failed to update fold parameter destinations");
                nmo_session_edit_rollback(edit);
                return rc;
            }
        }

        nmo_object_t *old_source_obj =
            nmo_object_repository_find_by_id(repo, edge->source_parameter_id);
        nmo_parameterout_state_t *old_source_out = old_source_obj &&
            nmo_object_get_class_id(old_source_obj) == NMO_CID_PARAMETEROUT
            ? (nmo_parameterout_state_t *)nmo_object_get_state(old_source_obj)
            : NULL;
        if (old_source_out && edge->source_parameter_id != new_parameter_id) {
            rc = nmo_session_edit_snapshot_bytes(edit, old_source_out,
                                                 sizeof(*old_source_out));
            if (rc != NMO_OK) {
                rewrite_fold_report_reject(
                    report, "snapshot_failed",
                    "Failed to snapshot old fold parameter output");
                nmo_session_edit_rollback(edit);
                return rc;
            }
            rewrite_parameterout_remove_destination(
                old_source_out, edge->target_parameter_id);
        }
    }

    nmo_session_edit_mark(edit, NMO_SESSION_EDIT_REFERENCES);
    rc = nmo_session_edit_commit(edit);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "commit_failed",
                                   "Failed to commit fold parameter edit");
        return rc;
    }
    return NMO_OK;
}

static void rewrite_fold_report_reject(nmo_behavior_fold_report_t *report,
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

nmo_status_t nmo_behavior_fold_analyze(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    if (report) {
        memset(report, 0, sizeof(*report));
    }
    if (!ctx || !session || !desc || !report || desc->parent_id == 0 ||
        !desc->node_ids || desc->node_count == 0 ||
        nmo_guid_is_null(desc->block_guid)) {
        rewrite_fold_report_reject(report, "invalid_argument",
                                   "Invalid behavior fold analysis arguments");
        return NMO_ERR_INVALID_ARGUMENT;
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

    if (!nmo_behavior_boundary_build_for_nodes(ctx, session,
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

    rc = rewrite_fold_validate_explicit_maps(report);
    if (rc != NMO_OK) {
        return rc;
    }

    rc = rewrite_build_delete_control_links(ctx, session, desc, report);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "control_plan_failed",
                                   "Failed to build fold control plan");
        goto fail;
    }

    if (rewrite_fold_report_supports_single_anchor_write(ctx, session,
                                                         report)) {
        rewrite_fold_report_clear_write_blockers(report);
        report->analysis_only = false;
        report->can_write = true;
    } else if (rewrite_fold_report_supports_closed_graph_write(ctx, session,
                                                               report)) {
        rewrite_fold_report_clear_write_blockers(report);
        report->analysis_only = false;
        report->can_write = true;
    }

    return NMO_OK;

fail:
    nmo_behavior_fold_report_free(report);
    return rc;
}

nmo_status_t nmo_behavior_fold_apply(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    nmo_status_t rc = nmo_behavior_fold_analyze(ctx, session, desc, report);
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
    if (rewrite_fold_selection_has_unselected_child(ctx, session, report,
                                                    &missing_child_id)) {
        (void)missing_child_id;
        rewrite_fold_report_reject(
            report, "selection_not_closed",
            "Selected graph fold must include child behavior from every "
            "selected graph or script");
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (rewrite_fold_report_is_single_anchor_only(report)) {
        nmo_object_repository_t *repo = nmo_session_get_repository(session);
        nmo_object_t *anchor =
            repo ? nmo_object_repository_find_by_id(repo, report->anchor_id)
                 : NULL;
        if (!anchor ||
            !rewrite_is_behavior_class(ctx, nmo_object_get_class_id(anchor))) {
            rewrite_fold_report_reject(report, "anchor_not_found",
                                       "Fold anchor behavior was not found");
            return NMO_ERR_NOT_FOUND;
        }
        nmo_behavior_state_t *state =
            (nmo_behavior_state_t *)nmo_object_get_state(anchor);
        if (!rewrite_behavior_state_is_leaf_bb(state)) {
            rewrite_fold_report_reject(
                report, "anchor_not_leaf",
                "Single-node fold requires a leaf BB anchor");
            return NMO_ERR_INVALID_ARGUMENT;
        }

        nmo_session_edit_t *edit = NULL;
        nmo_status_t edit_rc =
            nmo_session_edit_begin(session, "behavior fold anchor", &edit);
        if (edit_rc != NMO_OK) {
            rewrite_fold_report_reject(report, "edit_begin_failed",
                                       "Failed to begin behavior fold edit");
            return edit_rc;
        }
        edit_rc = nmo_session_edit_snapshot_bytes(edit, state,
                                                  sizeof(*state));
        if (edit_rc != NMO_OK) {
            rewrite_fold_report_reject(report, "snapshot_failed",
                                       "Failed to snapshot fold anchor");
            nmo_session_edit_rollback(edit);
            return edit_rc;
        }

        state->flags |= CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION;
        state->flags &= ~CKBEHAVIOR_SCRIPT;
        state->priority = 0;
        state->block_guid = desc->block_guid;
        state->block_version =
            desc->block_version != 0 ? desc->block_version : 65536u;
        if (desc->name && desc->name[0] != '\0') {
            edit_rc = nmo_session_edit_rename_object(
                edit, report->anchor_id, desc->name);
            if (edit_rc != NMO_OK) {
                rewrite_fold_report_reject(report, "rename_failed",
                                           "Failed to rename fold anchor");
                nmo_session_edit_rollback(edit);
                return edit_rc;
            }
        }
        nmo_session_edit_mark(edit, NMO_SESSION_EDIT_OBJECT_STATE);
        edit_rc = nmo_session_edit_commit(edit);
        if (edit_rc != NMO_OK) {
            rewrite_fold_report_reject(report, "commit_failed",
                                       "Failed to commit behavior fold");
            return edit_rc;
        }

        rewrite_fold_report_clear_write_blockers(report);
        report->analysis_only = false;
        report->can_write = true;
        return NMO_OK;
    }
    if (rewrite_fold_report_is_closed_graph_anchor(report) &&
        report->can_write) {
        nmo_status_t rewire_rc = rewrite_fold_rewire_control_boundary(
            ctx, session, report);
        if (rewire_rc != NMO_OK) {
            return rewire_rc;
        }
        rewire_rc = rewrite_fold_rewire_parameter_boundary(
            ctx, session, report);
        if (rewire_rc != NMO_OK) {
            return rewire_rc;
        }

        nmo_object_id_t *delete_ids = NULL;
        size_t delete_count = 0;
        nmo_status_t delete_rc = rewrite_fold_collect_delete_ids(
            session, report, &delete_ids, &delete_count);
        if (delete_rc != NMO_OK) {
            rewrite_fold_report_reject(report, "delete_plan_failed",
                                       "Failed to build fold delete set");
            return delete_rc;
        }

        nmo_runtime_report_t runtime_report = {0};
        delete_rc = nmo_session_destroy_objects(
            session, delete_ids, delete_count,
            NMO_RUNTIME_REQUEST_STRICT | NMO_RUNTIME_REQUEST_SAFE_DETACH,
            &runtime_report);
        free(delete_ids);
        if (delete_rc != NMO_OK) {
            rewrite_fold_report_reject(report, "delete_failed",
                                       "Failed to delete folded graph objects");
            return delete_rc;
        }

        nmo_status_t transform_rc = rewrite_fold_transform_anchor(
            ctx, session, desc, report, true);
        if (transform_rc != NMO_OK) {
            return transform_rc;
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

nmo_status_t nmo_behavior_fold(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report) {
    return nmo_behavior_fold_apply(ctx, session, desc, report);
}

void nmo_behavior_fold_report_free(nmo_behavior_fold_report_t *report) {
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
    nmo_behavior_boundary_free(&report->boundary);
    memset(report, 0, sizeof(*report));
}

nmo_status_t nmo_behavior_replace_bb(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_rewrite_report_t *report) {
    if (report) {
        memset(report, 0, sizeof(*report));
    }
    if (!ctx || !session || !desc || desc->behavior_id == 0 ||
        nmo_guid_is_null(desc->block_guid)) {
        rewrite_report_reject(report, "invalid_argument",
                              "Invalid behavior replace-bb arguments");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        rewrite_report_reject(report, "invalid_state",
                              "Object repository is unavailable");
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_t *object =
        nmo_object_repository_find_by_id(repo, desc->behavior_id);
    if (!object) {
        rewrite_report_reject(report, "not_found",
                              "Behavior object was not found");
        return NMO_ERR_NOT_FOUND;
    }
    if (!rewrite_is_behavior_class(ctx, nmo_object_get_class_id(object))) {
        rewrite_report_reject(report, "not_behavior",
                              "Object is not a CKBehavior");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_state_t *state =
        (nmo_behavior_state_t *)nmo_object_get_state(object);
    if (!state) {
        rewrite_report_reject(report, "invalid_state",
                              "Behavior state is unavailable");
        return NMO_ERR_INVALID_STATE;
    }

    if (report) {
        report->behavior_id = desc->behavior_id;
        report->before_flags = state->flags;
        report->after_flags = state->flags;
        report->before_guid = state->block_guid;
        report->after_guid = desc->block_guid;
        report->sub_behavior_count = state->sub_behaviors.count;
        report->sub_behavior_link_count = state->sub_behavior_links.count;
        report->operation_count = state->operations.count;
        report->preserved_inputs = state->inputs.count;
        report->preserved_outputs = state->outputs.count;
        report->preserved_in_parameters = state->in_parameters.count;
        report->preserved_out_parameters = state->out_parameters.count;
        report->preserved_local_parameters = state->local_parameters.count;
    }

    bool is_bb = (state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (state->flags & CKBEHAVIOR_SCRIPT) != 0;
    bool is_leaf = is_bb && !is_script &&
                   state->sub_behaviors.count == 0 &&
                   state->sub_behavior_links.count == 0 &&
                   state->operations.count == 0;
    if (report) {
        report->eligible_leaf = is_leaf;
    }
    if (!is_leaf) {
        rewrite_report_reject(report, "not_leaf_replaceable",
                              "Behavior is not leaf-replaceable");
        return NMO_ERR_INVALID_STATE;
    }

    nmo_behavior_boundary_t before_boundary = {0};
    nmo_behavior_boundary_t after_boundary = {0};
    nmo_behavior_state_t before_state = *state;
    nmo_session_edit_t *edit = NULL;
    nmo_status_t rc = NMO_OK;

    if (!nmo_behavior_boundary_build(ctx, session, desc->behavior_id,
                                     UINT32_MAX, &before_boundary)) {
        rewrite_report_reject(report, "boundary_failed",
                              "Failed to build original behavior boundary");
        return NMO_ERR_INVALID_STATE;
    }

    rc = nmo_session_edit_begin(session, "behavior replace-bb", &edit);
    if (rc != NMO_OK) {
        rewrite_report_reject(report, "edit_begin_failed",
                              "Failed to begin behavior rewrite edit");
        goto cleanup;
    }

    rc = nmo_session_edit_snapshot_bytes(edit, state, sizeof(*state));
    if (rc != NMO_OK) {
        rewrite_report_reject(report, "snapshot_failed",
                              "Failed to snapshot behavior state");
        goto cleanup;
    }

    state->flags |= CKBEHAVIOR_BUILDINGBLOCK | CKBEHAVIOR_USEFUNCTION;
    state->flags &= ~CKBEHAVIOR_SCRIPT;
    state->priority = 0;
    state->block_guid = desc->block_guid;
    state->block_version =
        desc->block_version != 0 ? desc->block_version : 65536u;

    if (desc->name && desc->name[0] != '\0') {
        rc = nmo_session_edit_rename_object(
            edit, desc->behavior_id, desc->name);
        if (rc != NMO_OK) {
            rewrite_report_reject(report, "rename_failed",
                                  "Failed to rename behavior");
            goto cleanup;
        }
    }

    nmo_session_edit_mark(edit, NMO_SESSION_EDIT_OBJECT_STATE);

    if (!rewrite_array_ids_equal(&before_state.inputs, &state->inputs) ||
        !rewrite_array_ids_equal(&before_state.outputs, &state->outputs) ||
        !rewrite_array_ids_equal(&before_state.in_parameters,
                                 &state->in_parameters) ||
        !rewrite_array_ids_equal(&before_state.out_parameters,
                                 &state->out_parameters) ||
        !rewrite_array_ids_equal(&before_state.local_parameters,
                                 &state->local_parameters)) {
        rewrite_report_reject(report, "ports_changed",
                              "Behavior ports or parameters changed");
        rc = NMO_ERR_INVALID_STATE;
        goto cleanup;
    }

    if (!nmo_behavior_boundary_build(ctx, session, desc->behavior_id,
                                     UINT32_MAX, &after_boundary)) {
        rewrite_report_reject(report, "boundary_failed",
                              "Failed to build rewritten behavior boundary");
        rc = NMO_ERR_INVALID_STATE;
        goto cleanup;
    }

    if (desc->preserve_links &&
        (!rewrite_control_edge_sets_equal(before_boundary.control_in,
                                          before_boundary.control_in_count,
                                          after_boundary.control_in,
                                          after_boundary.control_in_count) ||
         !rewrite_control_edge_sets_equal(before_boundary.control_out,
                                          before_boundary.control_out_count,
                                          after_boundary.control_out,
                                          after_boundary.control_out_count))) {
        rewrite_report_reject(report, "control_boundary_changed",
                              "Control boundary edges changed");
        rc = NMO_ERR_INVALID_STATE;
        goto cleanup;
    }

    if (desc->preserve_params &&
        (!rewrite_parameter_edge_sets_equal(before_boundary.parameter_in,
                                            before_boundary.parameter_in_count,
                                            after_boundary.parameter_in,
                                            after_boundary.parameter_in_count) ||
         !rewrite_parameter_edge_sets_equal(before_boundary.parameter_out,
                                            before_boundary.parameter_out_count,
                                            after_boundary.parameter_out,
                                            after_boundary.parameter_out_count))) {
        rewrite_report_reject(report, "parameter_boundary_changed",
                              "Parameter boundary edges changed");
        rc = NMO_ERR_INVALID_STATE;
        goto cleanup;
    }

    if (report) {
        report->changed =
            !nmo_guid_equals(report->before_guid, desc->block_guid) ||
            report->before_flags != state->flags ||
            (desc->name && desc->name[0] != '\0');
        report->after_flags = state->flags;
        report->after_guid = state->block_guid;
        report->preserved_control_in = after_boundary.control_in_count;
        report->preserved_control_out = after_boundary.control_out_count;
        report->preserved_parameter_in = after_boundary.parameter_in_count;
        report->preserved_parameter_out = after_boundary.parameter_out_count;
    }

    rc = nmo_session_edit_commit(edit);
    edit = NULL;
    if (rc != NMO_OK) {
        rewrite_report_reject(report, "commit_failed",
                              "Failed to commit behavior rewrite");
        goto cleanup;
    }

cleanup:
    if (edit) {
        nmo_session_edit_rollback(edit);
    }
    nmo_behavior_boundary_free(&before_boundary);
    nmo_behavior_boundary_free(&after_boundary);
    return rc;
}
