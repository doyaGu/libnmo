#include "behavior/nmo_behavior_rewrite.h"

#include "behavior/nmo_behavior_boundary.h"
#include "core/nmo_error.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
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
