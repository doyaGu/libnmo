/**
 * @file behavior_rewrite_apply.c
 * @brief Behavior graph fold: in-edit anchor transformation and boundary rewiring.
 */

#include "behavior_rewrite_internal.h"
#include "script_edit_internal.h"

#include <stdint.h>
#include <string.h>

nmo_status_t rewrite_fold_transform_anchor_in_tx(
    nmo_context_t *ctx,
    nmo_script_edit_tx_t *tx,
    const nmo_behavior_fold_desc_t *desc,
    nmo_behavior_fold_report_t *report,
    bool clear_graph_state) {
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    nmo_workspace_edit_t *edit = nmo_script_edit_workspace_edit(tx);
    if (!workspace || !edit) {
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

    nmo_status_t rc = script_edit_make_building_block(
        edit, state, desc->block_guid, desc->block_version, clear_graph_state);
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "snapshot_failed",
                                   "Failed to snapshot fold anchor");
        return rc;
    }

    if (desc->name && desc->name[0] != '\0') {
        rc = nmo_script_edit_rename_node(tx, report->anchor_id, desc->name);
        if (rc != NMO_OK) {
            rewrite_fold_report_reject(report, "rename_failed",
                                       "Failed to rename fold anchor");
            return rc;
        }
    }
    return NMO_OK;
}

static nmo_status_t rewrite_fold_rewire_control_link(
    nmo_script_edit_tx_t *tx,
    nmo_behavior_fold_report_t *report,
    nmo_object_id_t link_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id) {
    nmo_status_t rc =
        nmo_script_edit_rewire_behavior_link(tx, link_id, from_io_id, to_io_id);
    if (rc == NMO_ERR_NOT_FOUND) {
        rewrite_fold_report_reject(report, "control_link_missing",
                                   "Boundary control link was not found");
    } else if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "control_rewire_failed",
                                   "Failed to rewire boundary control link");
    }
    return rc;
}

nmo_status_t rewrite_fold_rewire_control_boundary_in_tx(
    nmo_script_edit_tx_t *tx,
    nmo_context_t *ctx,
    nmo_behavior_fold_report_t *report) {
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    if (!workspace || !report) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (report->boundary.control_in_count == 0 &&
        report->boundary.control_out_count == 0) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    if (!repo) {
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
        /* The anchor input becomes the link target. */
        rc = rewrite_fold_rewire_control_link(tx, report, edge->link_id,
                                              0, new_io_id);
        if (rc != NMO_OK) {
            return rc;
        }
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
        /* The anchor output becomes the link source. */
        rc = rewrite_fold_rewire_control_link(tx, report, edge->link_id,
                                              new_io_id, 0);
        if (rc != NMO_OK) {
            return rc;
        }
    }
    return NMO_OK;
}

static nmo_status_t rewrite_fold_connect_parameter(
    nmo_script_edit_tx_t *tx,
    nmo_behavior_fold_report_t *report,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id) {
    /* Keep an unchanged connection as is: files may connect parameters
     * of different types, which connect_parameter rejects. */
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    const nmo_parameterin_state_t *target =
        script_edit_find_parameterin_state_in_repo(
            nmo_workspace_internal_type_registry(workspace),
            nmo_workspace_internal_repository(workspace),
            target_parameter_id, NULL);
    if (target && nmo_parameterin_source_id(target) == source_parameter_id) {
        return NMO_OK;
    }
    nmo_status_t rc = nmo_script_edit_connect_parameter(
        tx, source_parameter_id, target_parameter_id);
    if (rc == NMO_ERR_NOT_FOUND) {
        rewrite_fold_report_reject(report, "parameter_target_missing",
                                   "Boundary parameter was not found");
    } else if (rc != NMO_OK) {
        rewrite_fold_report_reject(report, "parameter_rewire_failed",
                                   "Failed to rewire boundary parameter");
    }
    return rc;
}

static nmo_status_t rewrite_fold_check_destination(
    nmo_behavior_fold_report_t *report,
    nmo_status_t rc) {
    if (rc != NMO_OK) {
        rewrite_fold_report_reject(
            report, "parameter_destination_failed",
            "Failed to update fold parameter destinations");
    }
    return rc;
}

nmo_status_t rewrite_fold_rewire_parameter_boundary_in_tx(
    nmo_script_edit_tx_t *tx,
    nmo_context_t *ctx,
    nmo_behavior_fold_report_t *report) {
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    if (!workspace || !report) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (report->boundary.parameter_in_count == 0 &&
        report->boundary.parameter_out_count == 0) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    if (!repo) {
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

        /* The anchor input takes over the outside source. */
        rc = rewrite_fold_connect_parameter(
            tx, report, edge->source_parameter_id, new_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
        rc = rewrite_fold_check_destination(
            report, script_edit_add_parameter_destination(
                        tx, edge->source_parameter_id, new_parameter_id));
        if (rc != NMO_OK) {
            return rc;
        }
        rc = rewrite_fold_check_destination(
            report, script_edit_remove_parameter_destination(
                        tx, edge->source_parameter_id,
                        edge->target_parameter_id));
        if (rc != NMO_OK) {
            return rc;
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

        /* The outside target now reads from the anchor output. */
        rc = rewrite_fold_connect_parameter(
            tx, report, new_parameter_id, edge->target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
        rc = rewrite_fold_check_destination(
            report, script_edit_add_parameter_destination(
                        tx, new_parameter_id, edge->target_parameter_id));
        if (rc != NMO_OK) {
            return rc;
        }
        if (edge->source_parameter_id != new_parameter_id) {
            rc = rewrite_fold_check_destination(
                report, script_edit_remove_parameter_destination(
                            tx, edge->source_parameter_id,
                            edge->target_parameter_id));
            if (rc != NMO_OK) {
                return rc;
            }
        }
    }
    return NMO_OK;
}
