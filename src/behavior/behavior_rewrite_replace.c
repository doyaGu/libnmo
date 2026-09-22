/**
 * @file behavior_rewrite_replace.c
 * @brief Behavior graph replace-bb: signature checks and in-edit replacement.
 */

#include "behavior_rewrite_internal.h"

#include <stdint.h>
#include <string.h>

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
    for (size_t i = 0; i < a->count; ++i) {
        if (nmo_behavior_ref_array_get_id(a, i) !=
            nmo_behavior_ref_array_get_id(b, i)) {
            return false;
        }
    }
    return true;
}

static void rewrite_report_reject(nmo_behavior_replace_report_t *report,
                                  const char *code,
                                  const char *message) {
    if (!report) {
        return;
    }
    report->diagnostic_code = code;
    report->diagnostic_message = message;
    report->diagnostics_count = 1;
}

static nmo_status_t rewrite_replace_bb_in_edit(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_replace_report_t *report) {
    if (report) {
        memset(report, 0, sizeof(*report));
    }
    if (!ctx || !workspace || !desc || desc->behavior_id == 0 ||
        nmo_guid_is_null(desc->block_guid) || !edit) {
        rewrite_report_reject(report, "invalid_argument",
                              "Invalid behavior replace-bb arguments");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
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
    if (!rewrite_is_behavior_object(ctx, object)) {
        rewrite_report_reject(report, "not_behavior",
                              "Object is not a CKBehavior");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_state_t *state = rewrite_behavior_state(ctx, object);
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
    nmo_status_t rc = NMO_OK;

    if (!nmo_behavior_boundary_build(workspace, desc->behavior_id,
                                     UINT32_MAX, &before_boundary)) {
        rewrite_report_reject(report, "boundary_failed",
                              "Failed to build original behavior boundary");
        return NMO_ERR_INVALID_STATE;
    }

    if (report) {
        nmo_object_id_t node_id = desc->behavior_id;
        rc = nmo_behavior_edit_collect_semantic_risks(
            workspace, &before_boundary, &node_id, 1u,
            &report->semantic_risks, &report->semantic_risk_count);
        if (rc != NMO_OK) {
            rewrite_report_reject(report, "out_of_memory",
                                  "Failed to build replace semantic risks");
            goto cleanup;
        }
    }

    rc = nmo_workspace_edit_snapshot_behavior_state(edit, state);
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
        rc = nmo_object_edit_rename(
            edit, desc->behavior_id, desc->name);
        if (rc != NMO_OK) {
            rewrite_report_reject(report, "rename_failed",
                                  "Failed to rename behavior");
            goto cleanup;
        }
    }

    nmo_workspace_edit_mark(edit, NMO_WORKSPACE_EDIT_OBJECT_STATE);

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

    if (!nmo_behavior_boundary_build(workspace, desc->behavior_id,
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

cleanup:
    nmo_behavior_boundary_free(&before_boundary);
    nmo_behavior_boundary_free(&after_boundary);
    return rc;
}

/* Workspace-level replace-bb: one script edit transaction around the shared
 * in-edit implementation. */
static nmo_status_t rewrite_replace_bb_workspace(
    nmo_context_t *ctx,
    nmo_workspace_t *workspace,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_replace_report_t *report) {
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;
    if (!ctx || !workspace) {
        if (report) {
            memset(report, 0, sizeof(*report));
        }
        rewrite_report_reject(report, "invalid_argument",
                              "Invalid behavior replace-bb arguments");
        return NMO_ERR_INVALID_ARGUMENT;
    }

    rc = nmo_script_edit_begin(workspace, "behavior replace-bb", &tx);
    if (rc != NMO_OK) {
        if (report) {
            memset(report, 0, sizeof(*report));
        }
        rewrite_report_reject(report, "edit_begin_failed",
                              "Failed to begin behavior rewrite edit");
        return rc;
    }

    rc = rewrite_replace_bb_in_edit(ctx, nmo_script_edit_workspace(tx),
                                    nmo_script_edit_workspace_edit(tx), desc,
                                    report);
    if (rc != NMO_OK) {
        nmo_script_edit_rollback(tx);
        return rc;
    }
    rc = nmo_script_edit_commit(tx);
    if (rc != NMO_OK) {
        rewrite_report_reject(report, "commit_failed",
                              "Failed to commit behavior rewrite");
    }
    return rc;
}

NMO_API nmo_status_t nmo_behavior_edit_replace_bb(
    nmo_workspace_t *workspace,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_replace_report_t *report) {
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    if (!workspace || !ctx) {
        if (report) {
            memset(report, 0, sizeof(*report));
            rewrite_report_reject(report, "invalid_argument",
                                  "Invalid behavior replace-bb arguments");
        }
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return rewrite_replace_bb_workspace(ctx, workspace, desc, report);
}

NMO_API nmo_status_t nmo_behavior_edit_replace_bb_in_edit(
    nmo_workspace_t *workspace,
    nmo_workspace_edit_t *edit,
    const nmo_behavior_replace_bb_desc_t *desc,
    nmo_behavior_replace_report_t *report) {
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    if (!workspace || !ctx || !edit) {
        if (report) {
            memset(report, 0, sizeof(*report));
            rewrite_report_reject(report, "invalid_argument",
                                  "Invalid behavior replace-bb arguments");
        }
        return NMO_ERR_INVALID_ARGUMENT;
    }
    return rewrite_replace_bb_in_edit(ctx, workspace, edit, desc, report);
}
