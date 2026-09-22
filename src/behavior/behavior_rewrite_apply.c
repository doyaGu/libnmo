/**
 * @file behavior_rewrite_apply.c
 * @brief Behavior graph fold: in-edit anchor transformation and boundary rewiring.
 */

#include "behavior_rewrite_internal.h"

#include <stdint.h>
#include <string.h>

nmo_status_t rewrite_fold_transform_anchor_in_edit(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report,
    bool clear_graph_state) {
    if (!edit) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    nmo_object_t *anchor =
        repo ? nmo_object_repository_find_by_id(repo, report->anchor_id)
             : NULL;
    if (!anchor || !rewrite_is_behavior_object(ctx, anchor)) {
        rewrite_fold_report_reject(report, "anchor_not_found",
                                   "Fold anchor behavior was not found");
        return NMO_ERR_NOT_FOUND;
    }
    nmo_behavior_state_t *state = rewrite_behavior_state(ctx, anchor);
    if (!state) {
        rewrite_fold_report_reject(report, "anchor_invalid",
                                   "Fold anchor behavior state is unavailable");
        return NMO_ERR_INVALID_STATE;
    }

    nmo_status_t rc = NMO_OK;
    rc = nmo_workspace_edit_snapshot_behavior_state(edit, state);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "snapshot_failed",
                                   "Failed to snapshot fold anchor");
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
        nmo_array_clear(&state->sub_behavior_links);
        nmo_array_clear(&state->operations);
        nmo_array_clear(&state->local_parameters);
        state->save_flags &= ~(CK_STATESAVE_BEHAVIORSUBBEHAV |
                               CK_STATESAVE_BEHAVIORSUBLINKS |
                               CK_STATESAVE_BEHAVIOROPERATIONS |
                               CK_STATESAVE_BEHAVIORLOCALPARAMS);
        state->has_save_flags = true;
    }

    if (desc->name && desc->name[0] != '\0') {
        rc = nmo_object_edit_rename(edit, report->anchor_id, desc->name);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(report, "rename_failed",
                                       "Failed to rename fold anchor");
            return rc;
        }
    }

    nmo_workspace_edit_mark(
        edit, NMO_WORKSPACE_EDIT_OBJECT_STATE |
              (clear_graph_state ? (NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
                                    NMO_WORKSPACE_EDIT_REFERENCES)
                                 : 0u));
    return NMO_OK;
}

static bool rewrite_parameterout_has_destination(
    const nmo_parameterout_state_t *state,
    nmo_object_id_t target_id) {
    if (!state || !state->destination_ids || target_id == 0) {
        return false;
    }
    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (nmo_parameterout_destination_id(state, i) == target_id) {
            return true;
        }
    }
    return false;
}

static nmo_status_t rewrite_parameterout_add_destination(
    nmo_parameterout_state_t *state,
    nmo_arena_t *arena,
    nmo_object_id_t target_id) {
    if (!state || !arena || target_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (rewrite_parameterout_has_destination(state, target_id)) {
        return NMO_OK;
    }
    if (state->destination_count == UINT32_MAX) {
        return NMO_ERR_INVALID_FORMAT;
    }
    nmo_ref_t *next = (nmo_ref_t *)nmo_arena_alloc(
        arena,
        (size_t)(state->destination_count + 1u) * sizeof(*next),
        _Alignof(nmo_ref_t));
    if (!next) {
        return NMO_ERR_NOMEM;
    }
    if (state->destination_count > 0 && state->destination_ids != NULL) {
        memcpy(next, state->destination_ids,
               (size_t)state->destination_count * sizeof(*next));
    }
    next[state->destination_count] = nmo_ref_from_id(target_id);
    state->destination_ids = next;
    state->destination_count++;
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
        if (nmo_parameterout_destination_id(state, i) != target_id) {
            state->destination_ids[kept++] = state->destination_ids[i];
        }
    }
    state->destination_count = kept;
    if (kept == 0) {
        state->destination_ids = NULL;
    }
}

nmo_status_t rewrite_fold_rewire_control_boundary_in_edit(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    nmo_behavior_fold_report_t *report) {
    if (!workspace || !edit || !report) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (report->boundary.control_in_count == 0 &&
        report->boundary.control_out_count == 0) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    const nmo_type_registry_t *registry =
        nmo_workspace_internal_type_registry(workspace);
    if (!repo || !registry) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_status_t rc = NMO_OK;
    for (size_t i = 0; i < report->boundary.control_in_count; ++i) {
        const nmo_behavior_boundary_control_edge_t *edge =
            &report->boundary.control_in[i];
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->input_maps, report->input_map_count, (uint32_t)i);
        nmo_object_id_t new_io_id = 0;
        rc = rewrite_fold_anchor_io_at(ctx, repo, report->anchor_id,
                                       true, new_index, &new_io_id);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "input_map_target_missing",
                "Fold input map does not resolve to an anchor input");
            return rc;
        }

        nmo_object_t *link_obj =
            nmo_object_repository_find_by_id(repo, edge->link_id);
        nmo_behaviorlink_state_t *link_state = link_obj
            ? (nmo_behaviorlink_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, link_obj, CKPGUID_BEHAVIORLINK)
            : NULL;
        if (!link_state) {
            rewrite_fold_report_reject(
                report, "control_link_missing",
                "Boundary control link was not found");
            return NMO_ERR_NOT_FOUND;
        }
        rc = nmo_workspace_edit_snapshot_bytes(edit, link_state,
                                               sizeof(*link_state));
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "snapshot_failed",
                "Failed to snapshot boundary control link");
            return rc;
        }

        /* CK2/SDK naming is counterintuitive: link in_io_id is the source IO,
         * and link out_io_id is the target IO. Keep graph edge direction
         * source owner -> target owner. */
        nmo_behaviorlink_set_out_io_id(link_state, new_io_id);
    }

    for (size_t i = 0; i < report->boundary.control_out_count; ++i) {
        const nmo_behavior_boundary_control_edge_t *edge =
            &report->boundary.control_out[i];
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->output_maps, report->output_map_count, (uint32_t)i);
        nmo_object_id_t new_io_id = 0;
        rc = rewrite_fold_anchor_io_at(ctx, repo, report->anchor_id,
                                       false, new_index, &new_io_id);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "output_map_target_missing",
                "Fold output map does not resolve to an anchor output");
            return rc;
        }

        nmo_object_t *link_obj =
            nmo_object_repository_find_by_id(repo, edge->link_id);
        nmo_behaviorlink_state_t *link_state = link_obj
            ? (nmo_behaviorlink_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, link_obj, CKPGUID_BEHAVIORLINK)
            : NULL;
        if (!link_state) {
            rewrite_fold_report_reject(
                report, "control_link_missing",
                "Boundary control link was not found");
            return NMO_ERR_NOT_FOUND;
        }
        rc = nmo_workspace_edit_snapshot_bytes(edit, link_state,
                                               sizeof(*link_state));
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "snapshot_failed",
                "Failed to snapshot boundary control link");
            return rc;
        }

        /* CK2/SDK naming is counterintuitive: link in_io_id is the source IO,
         * and link out_io_id is the target IO. Keep graph edge direction
         * source owner -> target owner. */
        nmo_behaviorlink_set_in_io_id(link_state, new_io_id);
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH |
                                  NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}

nmo_status_t rewrite_fold_rewire_parameter_boundary_in_edit(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    nmo_behavior_fold_report_t *report) {
    if (!workspace || !edit || !report) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (report->boundary.parameter_in_count == 0 &&
        report->boundary.parameter_out_count == 0) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    const nmo_type_registry_t *registry =
        nmo_workspace_internal_type_registry(workspace);
    if (!repo || !registry) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_status_t rc = NMO_OK;
    for (size_t i = 0; i < report->boundary.parameter_in_count; ++i) {
        const nmo_behavior_boundary_parameter_edge_t *edge =
            &report->boundary.parameter_in[i];
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->parameter_maps, report->parameter_map_count, (uint32_t)i);
        nmo_object_id_t new_parameter_id = 0;
        rc = rewrite_fold_anchor_parameter_at(
            ctx, repo, report->anchor_id, true, new_index,
            &new_parameter_id);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "parameter_map_target_missing",
                "Fold parameter map does not resolve to an anchor input "
                "parameter");
            return rc;
        }

        nmo_object_t *new_target_obj =
            nmo_object_repository_find_by_id(repo, new_parameter_id);
        nmo_parameterin_state_t *new_target_in = new_target_obj
            ? (nmo_parameterin_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, new_target_obj, CKPGUID_PARAMETERIN)
            : NULL;
        if (!new_target_in) {
            rewrite_fold_report_reject(
                report, "parameter_target_missing",
                "Fold anchor input parameter was not found");
            return NMO_ERR_NOT_FOUND;
        }
        rc = nmo_workspace_edit_snapshot_bytes(edit, new_target_in,
                                               sizeof(*new_target_in));
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "snapshot_failed",
                "Failed to snapshot fold anchor input parameter");
            return rc;
        }
        nmo_parameterin_set_source_id(new_target_in,
                                      edge->source_parameter_id);
        new_target_in->is_shared = edge->shared ? 1u : 0u;

        nmo_object_t *source_obj =
            nmo_object_repository_find_by_id(repo, edge->source_parameter_id);
        nmo_parameterout_state_t *source_out = source_obj
            ? (nmo_parameterout_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, source_obj, CKPGUID_PARAMETEROUT)
            : NULL;
        if (source_out) {
            rc = nmo_workspace_edit_snapshot_bytes(edit, source_out,
                                                   sizeof(*source_out));
            if (rc != NMO_OK) {
                rewrite_fold_report_reject(
                    report, "snapshot_failed",
                    "Failed to snapshot fold parameter source output");
                return rc;
            }
            rc = rewrite_parameterout_add_destination(
                source_out,
                nmo_workspace_internal_document_arena(workspace),
                new_parameter_id);
            if (rc != NMO_OK) {
                rewrite_fold_report_reject(
                    report, "out_of_memory",
                    "Failed to update fold source parameter destinations");
                return rc;
            }
            rewrite_parameterout_remove_destination(source_out,
                                                    edge->target_parameter_id);
        }
    }

    for (size_t i = 0; i < report->boundary.parameter_out_count; ++i) {
        const nmo_behavior_boundary_parameter_edge_t *edge =
            &report->boundary.parameter_out[i];
        uint32_t new_index = rewrite_fold_mapped_new_index(
            report->parameter_maps, report->parameter_map_count, (uint32_t)i);
        nmo_object_id_t new_parameter_id = 0;
        rc = rewrite_fold_anchor_parameter_at(
            ctx, repo, report->anchor_id, false, new_index,
            &new_parameter_id);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "parameter_map_target_missing",
                "Fold parameter map does not resolve to an anchor output "
                "parameter");
            return rc;
        }

        nmo_object_t *target_obj =
            nmo_object_repository_find_by_id(repo, edge->target_parameter_id);
        nmo_parameterin_state_t *target_in = target_obj
            ? (nmo_parameterin_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, target_obj, CKPGUID_PARAMETERIN)
            : NULL;
        if (!target_in) {
            rewrite_fold_report_reject(
                report, "parameter_target_missing",
                "Fold parameter target input was not found");
            return NMO_ERR_NOT_FOUND;
        }
        rc = nmo_workspace_edit_snapshot_bytes(edit, target_in,
                                               sizeof(*target_in));
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(
                report, "snapshot_failed",
                "Failed to snapshot fold parameter input");
            return rc;
        }
        nmo_parameterin_set_source_id(target_in, new_parameter_id);
        target_in->is_shared = edge->shared ? 1u : 0u;

        nmo_object_t *new_source_obj =
            nmo_object_repository_find_by_id(repo, new_parameter_id);
        nmo_parameterout_state_t *new_source_out = new_source_obj
            ? (nmo_parameterout_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, new_source_obj, CKPGUID_PARAMETEROUT)
            : NULL;
        if (new_source_out) {
            rc = nmo_workspace_edit_snapshot_bytes(edit, new_source_out,
                                                   sizeof(*new_source_out));
            if (rc != NMO_OK) {
                rewrite_fold_report_reject(
                    report, "snapshot_failed",
                    "Failed to snapshot fold parameter output");
                return rc;
            }
            rc = rewrite_parameterout_add_destination(
                new_source_out,
                nmo_workspace_internal_document_arena(workspace),
                edge->target_parameter_id);
            if (rc != NMO_OK) {
                rewrite_fold_report_reject(
                    report, "out_of_memory",
                    "Failed to update fold parameter destinations");
                return rc;
            }
        }

        nmo_object_t *old_source_obj =
            nmo_object_repository_find_by_id(repo, edge->source_parameter_id);
        nmo_parameterout_state_t *old_source_out = old_source_obj
            ? (nmo_parameterout_state_t *)
                nmo_type_query_object_get_ancestor_state_by_guid(
                    registry, old_source_obj, CKPGUID_PARAMETEROUT)
            : NULL;
        if (old_source_out && edge->source_parameter_id != new_parameter_id) {
            rc = nmo_workspace_edit_snapshot_bytes(edit, old_source_out,
                                                   sizeof(*old_source_out));
            if (rc != NMO_OK) {
                rewrite_fold_report_reject(
                    report, "snapshot_failed",
                    "Failed to snapshot old fold parameter output");
                return rc;
            }
            rewrite_parameterout_remove_destination(
                old_source_out, edge->target_parameter_id);
        }
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_REFERENCES);
    return NMO_OK;
}
