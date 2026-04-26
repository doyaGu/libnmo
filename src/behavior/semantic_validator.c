#include "behavior/nmo_semantic_validator.h"

#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_behavior_registry.h"
#include "behavior/nmo_edit_plan.h"
#include "core/nmo_error.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "../runtime/runtime_internal.h"
#include "runtime/nmo_context.h"

#include <stdlib.h>
#include <string.h>

static nmo_status_t semantic_add_risk(
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_behavior_semantic_risk_severity_t severity,
    const char *code,
    const char *message,
    nmo_object_id_t object_id)
{
    if (risks == NULL || risk_count == NULL || code == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_semantic_risk_t *next =
        (nmo_behavior_semantic_risk_t *)realloc(
            *risks, (*risk_count + 1u) * sizeof(**risks));
    if (next == NULL) {
        return NMO_ERR_NOMEM;
    }

    next[*risk_count] = (nmo_behavior_semantic_risk_t){
        .severity = severity,
        .code = code,
        .message = message,
        .object_id = object_id,
    };
    *risks = next;
    ++(*risk_count);
    return NMO_OK;
}

static nmo_status_t semantic_add_boundary_delay_risks(
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    const nmo_behavior_boundary_control_edge_t *edges,
    size_t edge_count)
{
    for (size_t i = 0; i < edge_count; ++i) {
        if (edges[i].activation_delay == 0 &&
            edges[i].initial_activation_delay == 0) {
            continue;
        }
        NMO_RETURN_IF_ERROR(semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            "activation_delay",
            "Boundary control link preserves activation delay",
            edges[i].link_id));
    }
    return NMO_OK;
}

static nmo_status_t semantic_add_boundary_shared_parameter_risks(
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    const nmo_behavior_boundary_parameter_edge_t *edges,
    size_t edge_count)
{
    for (size_t i = 0; i < edge_count; ++i) {
        if (!edges[i].shared) {
            continue;
        }
        nmo_object_id_t object_id = edges[i].target_parameter_id != 0
            ? edges[i].target_parameter_id
            : edges[i].source_parameter_id;
        NMO_RETURN_IF_ERROR(semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            "shared_parameter",
            "Boundary parameter edge uses shared parameter semantics",
            object_id));
    }
    return NMO_OK;
}

static bool semantic_behavior_has_message_semantics(
    nmo_context_t *ctx,
    const nmo_behavior_state_t *state)
{
    if (state == NULL) {
        return false;
    }

    const uint32_t message_flags =
        CKBEHAVIOR_WAITSFORMESSAGE |
        CKBEHAVIOR_MESSAGESENDER |
        CKBEHAVIOR_MESSAGERECEIVER;
    if ((state->flags & message_flags) != 0u) {
        return true;
    }

    if ((state->flags & CKBEHAVIOR_BUILDINGBLOCK) == 0u || ctx == NULL) {
        return false;
    }

    const nmo_behavior_proto_t *proto = nmo_behavior_registry_find(
        nmo_context_get_bb_registry(ctx), state->block_guid);
    return proto != NULL && proto->category != NULL &&
           strcmp(proto->category, "Logics/Message") == 0;
}

static nmo_status_t semantic_add_message_flow_risks(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    const nmo_object_id_t *node_ids,
    size_t node_count)
{
    if (repo == NULL || node_ids == NULL) {
        return NMO_OK;
    }
    for (size_t i = 0; i < node_count; ++i) {
        nmo_object_t *object =
            nmo_object_repository_find_by_id(repo, node_ids[i]);
        const nmo_behavior_state_t *state =
            object != NULL
                ? (const nmo_behavior_state_t *)nmo_object_get_state(object)
                : NULL;
        if (!semantic_behavior_has_message_semantics(ctx, state)) {
            continue;
        }
        NMO_RETURN_IF_ERROR(semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            "message_flow",
            "Selected behavior participates in message send/wait flow",
            node_ids[i]));
    }
    return NMO_OK;
}

static bool semantic_object_exists(nmo_object_repository_t *repo,
                                   nmo_object_id_t object_id)
{
    return object_id == 0u ||
           (repo != NULL &&
            nmo_object_repository_find_by_id(repo, object_id) != NULL);
}

static nmo_status_t semantic_add_missing_ref_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id)
{
    if (semantic_object_exists(repo, object_id)) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "dangling_reference",
        "Edit operation references a missing object",
        object_id);
}

static nmo_status_t semantic_add_plan_activation_delay_risk(
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id,
    uint32_t activation_delay)
{
    if (activation_delay == 0u) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
        "activation_delay",
        "Edit operation creates or preserves activation delay",
        object_id);
}

static nmo_status_t semantic_validate_basic_edit_op(
    nmo_object_repository_t *repo,
    const nmo_edit_op_t *op,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count)
{
    if (op == NULL) {
        return NMO_OK;
    }

    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        if (op->kind == NMO_EDIT_OP_SET_PARAMETER_VALUE &&
            op->data.set_value.has_parameter_ref) {
            return NMO_OK;
        }
        if (op->kind == NMO_EDIT_OP_SET_PARAMETER_BYTES &&
            op->data.set_bytes.has_parameter_ref) {
            return NMO_OK;
        }
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->primary_id);
    case NMO_EDIT_OP_ADD_NODE:
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.add_node.parent_behavior_id);
    case NMO_EDIT_OP_REMOVE_NODE:
        return NMO_OK;
    case NMO_EDIT_OP_ADD_IO:
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.add_io.behavior_id);
    case NMO_EDIT_OP_RENAME_IO:
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.rename_io.io_id);
    case NMO_EDIT_OP_REMOVE_IO:
        return NMO_OK;
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.add_link.parent_behavior_id));
        if (!op->data.add_link.has_from_io_ref) {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count, op->data.add_link.from_io_id));
        }
        if (!op->data.add_link.has_to_io_ref) {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count, op->data.add_link.to_io_id));
        }
        return semantic_add_plan_activation_delay_risk(
            risks,
            risk_count,
            op->data.add_link.parent_behavior_id,
            op->data.add_link.activation_delay);
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.rewire_link.link_id));
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.rewire_link.from_io_id));
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.rewire_link.to_io_id);
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.set_link_delay.link_id));
        return semantic_add_plan_activation_delay_risk(
            risks,
            risk_count,
            op->data.set_link_delay.link_id,
            op->data.set_link_delay.activation_delay);
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return NMO_OK;
    case NMO_EDIT_OP_ADD_PARAMETER:
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.add_parameter.owner_behavior_id);
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.connect_parameter.source_parameter_id));
        if (op->data.connect_parameter.has_target_parameter_ref) {
            return NMO_OK;
        }
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.connect_parameter.target_parameter_id);
    case NMO_EDIT_OP_DISCONNECT_PARAMETER:
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.disconnect_parameter.target_parameter_id);
    case NMO_EDIT_OP_REMOVE_PARAMETER:
        return NMO_OK;
    case NMO_EDIT_OP_ADD_OPERATION:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.add_operation.parent_behavior_id));
        if (!op->data.add_operation.has_in1_parameter_ref) {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.in1_parameter_id));
        }
        if (!op->data.add_operation.has_in2_parameter_ref) {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.in2_parameter_id));
        }
        if (!op->data.add_operation.has_out_parameter_ref) {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.out_parameter_id));
        }
        return NMO_OK;
    case NMO_EDIT_OP_REWIRE_OPERATION:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.rewire_operation.operation_id));
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.rewire_operation.in1_parameter_id));
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.rewire_operation.in2_parameter_id));
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.rewire_operation.out_parameter_id);
    case NMO_EDIT_OP_REMOVE_OPERATION:
        return NMO_OK;
    case NMO_EDIT_OP_INTERFACE_POLICY:
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.interface_policy.behavior_id);
    case NMO_EDIT_OP_SET_DATA_CELL:
        return semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.data_cell.dataarray_id);
    case NMO_EDIT_OP_FOLD:
    case NMO_EDIT_OP_REPLACE_BB:
        return NMO_OK;
    default:
        return NMO_OK;
    }
}

nmo_status_t nmo_semantic_validate_boundary(
    nmo_workspace_t *workspace,
    const nmo_behavior_boundary_t *boundary,
    const nmo_object_id_t *node_ids,
    size_t node_count,
    nmo_behavior_semantic_risk_t **out_risks,
    size_t *out_risk_count)
{
    if (workspace == NULL || boundary == NULL ||
        out_risks == NULL || out_risk_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    nmo_status_t rc = NMO_OK;

    if (boundary->broken_links > 0u || boundary->missing_nodes > 0u) {
        rc = semantic_add_risk(
            &risks,
            &risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "dangling_boundary",
            "Boundary contains broken links or missing nodes",
            boundary->behavior_id);
        if (rc != NMO_OK) {
            goto fail;
        }
    }

    rc = semantic_add_boundary_delay_risks(
        &risks, &risk_count,
        boundary->control_in, boundary->control_in_count);
    if (rc != NMO_OK) {
        goto fail;
    }
    rc = semantic_add_boundary_delay_risks(
        &risks, &risk_count,
        boundary->control_out, boundary->control_out_count);
    if (rc != NMO_OK) {
        goto fail;
    }
    rc = semantic_add_boundary_shared_parameter_risks(
        &risks, &risk_count,
        boundary->parameter_in, boundary->parameter_in_count);
    if (rc != NMO_OK) {
        goto fail;
    }
    rc = semantic_add_boundary_shared_parameter_risks(
        &risks, &risk_count,
        boundary->parameter_out, boundary->parameter_out_count);
    if (rc != NMO_OK) {
        goto fail;
    }
    rc = semantic_add_message_flow_risks(
        ctx, repo, &risks, &risk_count, node_ids, node_count);
    if (rc != NMO_OK) {
        goto fail;
    }

    *out_risks = risks;
    *out_risk_count = risk_count;
    return NMO_OK;

fail:
    free(risks);
    return rc;
}

nmo_status_t nmo_semantic_validate_edit_plan(
    nmo_workspace_t *workspace,
    const nmo_edit_plan_t *plan,
    nmo_behavior_semantic_risk_t **out_risks,
    size_t *out_risk_count)
{
    if (workspace == NULL || plan == NULL ||
        out_risks == NULL || out_risk_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    nmo_status_t rc = NMO_OK;
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(workspace);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < nmo_edit_plan_count(plan); ++i) {
        const nmo_edit_op_t *op = nmo_edit_plan_get(plan, i);
        if (op == NULL) {
            continue;
        }
        rc = semantic_validate_basic_edit_op(
            repo, op, &risks, &risk_count);
        if (rc != NMO_OK) {
            goto fail;
        }
        if (op->kind == NMO_EDIT_OP_FOLD) {
            nmo_behavior_fold_report_t fold_report = {0};
            rc = nmo_behavior_edit_fold_analyze(
                workspace, &op->data.fold.desc, &fold_report);
            if (rc == NMO_OK && fold_report.semantic_risk_count > 0u) {
                size_t old_count = risk_count;
                nmo_behavior_semantic_risk_t *next =
                    (nmo_behavior_semantic_risk_t *)realloc(
                        risks,
                        (risk_count + fold_report.semantic_risk_count) *
                            sizeof(*risks));
                if (next == NULL) {
                    nmo_behavior_edit_fold_report_free(&fold_report);
                    rc = NMO_ERR_NOMEM;
                    goto fail;
                }
                risks = next;
                memcpy(risks + old_count,
                       fold_report.semantic_risks,
                       fold_report.semantic_risk_count *
                           sizeof(*fold_report.semantic_risks));
                risk_count += fold_report.semantic_risk_count;
            }
            nmo_behavior_edit_fold_report_free(&fold_report);
            if (rc != NMO_OK) {
                goto fail;
            }
        } else if (op->kind == NMO_EDIT_OP_REPLACE_BB) {
            nmo_behavior_boundary_t boundary = {0};
            nmo_object_id_t node_id = op->data.replace_bb.desc.behavior_id;
            if (!nmo_behavior_boundary_build(
                    workspace, node_id, UINT32_MAX, &boundary)) {
                rc = NMO_ERR_INVALID_STATE;
                goto fail;
            }
            nmo_behavior_semantic_risk_t *op_risks = NULL;
            size_t op_risk_count = 0u;
            rc = nmo_semantic_validate_boundary(
                workspace, &boundary, &node_id, 1u,
                &op_risks, &op_risk_count);
            nmo_behavior_boundary_free(&boundary);
            if (rc != NMO_OK) {
                free(op_risks);
                goto fail;
            }
            if (op_risk_count > 0u) {
                size_t old_count = risk_count;
                nmo_behavior_semantic_risk_t *next =
                    (nmo_behavior_semantic_risk_t *)realloc(
                        risks,
                        (risk_count + op_risk_count) * sizeof(*risks));
                if (next == NULL) {
                    free(op_risks);
                    rc = NMO_ERR_NOMEM;
                    goto fail;
                }
                risks = next;
                memcpy(risks + old_count,
                       op_risks,
                       op_risk_count * sizeof(*op_risks));
                risk_count += op_risk_count;
            }
            free(op_risks);
        }
    }

    *out_risks = risks;
    *out_risk_count = risk_count;
    return NMO_OK;

fail:
    free(risks);
    return rc;
}

void nmo_semantic_risks_free(nmo_behavior_semantic_risk_t *risks)
{
    free(risks);
}

nmo_status_t nmo_behavior_edit_collect_semantic_risks(
    nmo_workspace_t *workspace,
    const nmo_behavior_boundary_t *boundary,
    const nmo_object_id_t *node_ids,
    size_t node_count,
    nmo_behavior_semantic_risk_t **out_risks,
    size_t *out_risk_count)
{
    return nmo_semantic_validate_boundary(
        workspace, boundary, node_ids, node_count,
        out_risks, out_risk_count);
}

void nmo_behavior_edit_semantic_risks_free(
    nmo_behavior_semantic_risk_t *risks)
{
    nmo_semantic_risks_free(risks);
}
