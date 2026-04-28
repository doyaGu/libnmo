#include "behavior/nmo_semantic_validator.h"

#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_behavior_registry.h"
#include "behavior/nmo_edit_plan.h"
#include "core/nmo_error.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "../runtime/runtime_internal.h"
#include "runtime/nmo_context.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
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

static nmo_status_t semantic_add_class_ref_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id,
    nmo_class_id_t expected_class_id,
    const char *code,
    const char *message)
{
    if (object_id == 0u || repo == NULL) {
        return NMO_OK;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_OK;
    }
    if (nmo_object_get_class_id(object) == expected_class_id) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        code,
        message,
        object_id);
}

static nmo_status_t semantic_add_behavior_owner_ref_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id)
{
    return semantic_add_class_ref_risk(
        repo,
        risks,
        risk_count,
        object_id,
        NMO_CID_BEHAVIOR,
        "behavior_owner_type_mismatch",
        "Edit operation expects a behavior owner");
}

static nmo_status_t semantic_add_behavior_io_ref_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id)
{
    return semantic_add_class_ref_risk(
        repo,
        risks,
        risk_count,
        object_id,
        NMO_CID_BEHAVIORIO,
        "behavior_io_type_mismatch",
        "Edit operation expects a behavior IO");
}

static bool semantic_behavior_has_direct_child(
    nmo_object_repository_t *repo,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t child_behavior_id)
{
    nmo_object_t *parent_obj = repo != NULL
        ? nmo_object_repository_find_by_id(repo, parent_behavior_id)
        : NULL;
    const nmo_behavior_state_t *parent = parent_obj != NULL &&
            nmo_object_get_class_id(parent_obj) == NMO_CID_BEHAVIOR
        ? (const nmo_behavior_state_t *)nmo_object_get_state(parent_obj)
        : NULL;
    if (parent == NULL) {
        return false;
    }
    if (parent_behavior_id == child_behavior_id) {
        return true;
    }
    return nmo_array_find(&parent->sub_behaviors, &child_behavior_id, NULL) != 0;
}

static bool semantic_find_behavior_io_owner(
    nmo_object_repository_t *repo,
    nmo_object_id_t io_id,
    nmo_object_id_t *out_owner_id)
{
    size_t object_count = repo != NULL ? nmo_object_repository_get_count(repo) : 0u;
    if (out_owner_id != NULL) {
        *out_owner_id = 0u;
    }
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_behavior_state_t *state = object != NULL &&
                nmo_object_get_class_id(object) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(object)
            : NULL;
        if (state == NULL) {
            continue;
        }
        if (nmo_array_find(&state->inputs, &io_id, NULL) != 0 ||
            nmo_array_find(&state->outputs, &io_id, NULL) != 0) {
            if (out_owner_id != NULL) {
                *out_owner_id = nmo_object_get_id(object);
            }
            return true;
        }
    }
    return false;
}

static bool semantic_find_behavior_link_owner(
    nmo_object_repository_t *repo,
    nmo_object_id_t link_id,
    nmo_object_id_t *out_owner_id)
{
    size_t object_count = repo != NULL ? nmo_object_repository_get_count(repo) : 0u;
    if (out_owner_id != NULL) {
        *out_owner_id = 0u;
    }
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        const nmo_behavior_state_t *state = object != NULL &&
                nmo_object_get_class_id(object) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(object)
            : NULL;
        if (state == NULL) {
            continue;
        }
        if (nmo_array_find(&state->sub_behavior_links, &link_id, NULL) != 0) {
            if (out_owner_id != NULL) {
                *out_owner_id = nmo_object_get_id(object);
            }
            return true;
        }
    }
    return false;
}

static nmo_status_t semantic_add_control_endpoint_scope_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t io_id)
{
    if (repo == NULL || parent_behavior_id == 0u || io_id == 0u) {
        return NMO_OK;
    }
    nmo_object_t *io_obj = nmo_object_repository_find_by_id(repo, io_id);
    if (io_obj == NULL || nmo_object_get_class_id(io_obj) != NMO_CID_BEHAVIORIO) {
        return NMO_OK;
    }

    nmo_object_id_t owner_id = 0u;
    if (!semantic_find_behavior_io_owner(repo, io_id, &owner_id)) {
        return semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "dangling_control_link",
            "Control-flow link endpoint is not owned by any behavior",
            io_id);
    }
    if (semantic_behavior_has_direct_child(repo, parent_behavior_id, owner_id)) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "control_endpoint_scope_mismatch",
        "Control-flow link endpoint is outside the parent behavior graph",
        io_id);
}

static nmo_status_t semantic_add_behavior_node_ref_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id)
{
    return semantic_add_class_ref_risk(
        repo,
        risks,
        risk_count,
        object_id,
        NMO_CID_BEHAVIOR,
        "behavior_node_type_mismatch",
        "Edit operation expects a behavior node");
}

static nmo_status_t semantic_add_behavior_target_consistency_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t behavior_id)
{
    nmo_object_t *object = repo != NULL
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NMO_OK;
    }
    const nmo_behavior_state_t *state =
        (const nmo_behavior_state_t *)nmo_object_get_state(object);
    if (state == NULL || (state->flags & CKBEHAVIOR_TARGETABLE) == 0u) {
        return NMO_OK;
    }
    if (state->target_parameter_id == 0u) {
        return semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "target_parameter_missing",
            "Targetable behavior is missing its target parameter",
            behavior_id);
    }
    nmo_object_t *target = nmo_object_repository_find_by_id(
        repo, state->target_parameter_id);
    if (target == NULL) {
        return semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "target_parameter_dangling_reference",
            "Targetable behavior references a missing target parameter",
            state->target_parameter_id);
    }
    if (nmo_object_get_class_id(target) != NMO_CID_PARAMETERIN) {
        return semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "target_parameter_type_mismatch",
            "Targetable behavior target must be a behavior input parameter",
            state->target_parameter_id);
    }
    return NMO_OK;
}

static bool semantic_is_parameter_object_class(nmo_class_id_t class_id)
{
    return class_id == NMO_CID_PARAMETER ||
           class_id == NMO_CID_PARAMETERIN ||
           class_id == NMO_CID_PARAMETEROUT ||
           class_id == NMO_CID_PARAMETERLOCAL ||
           class_id == NMO_CID_PARAMETEROPERATION;
}

static nmo_status_t semantic_add_parameter_object_ref_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id)
{
    if (object_id == 0u || repo == NULL) {
        return NMO_OK;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_OK;
    }
    if (semantic_is_parameter_object_class(nmo_object_get_class_id(object))) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "parameter_object_type_mismatch",
        "Edit operation expects a parameter object",
        object_id);
}

static nmo_status_t semantic_add_parameterin_ref_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id)
{
    if (object_id == 0u || repo == NULL) {
        return NMO_OK;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, object_id);
    if (object == NULL) {
        return NMO_OK;
    }
    if (nmo_object_get_class_id(object) == NMO_CID_PARAMETERIN) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "parameter_target_type_mismatch",
        "Parameter connection target must be a behavior input parameter",
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

static const char *semantic_static_result_handle_name(
    nmo_edit_op_kind_t kind)
{
    switch (kind) {
    case NMO_EDIT_OP_ADD_NODE:
        return "node";
    case NMO_EDIT_OP_ADD_IO:
        return "io";
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        return "link";
    case NMO_EDIT_OP_ADD_PARAMETER:
        return "parameter";
    case NMO_EDIT_OP_ADD_OPERATION:
        return "operation";
    default:
        return NULL;
    }
}

static bool semantic_named_handle_matches(
    const char *handle_name,
    const char *prefix,
    const char *name)
{
    if (handle_name == NULL || prefix == NULL) {
        return false;
    }
    if (name == NULL || name[0] == '\0') {
        return strcmp(handle_name, prefix) == 0;
    }
    char expected[160];
    snprintf(expected, sizeof(expected), "%s:%s", prefix, name);
    return strcmp(handle_name, expected) == 0;
}

static bool semantic_string_array_has_handle(
    const char *handle_name,
    const char *prefix,
    const char *const *names,
    uint32_t count)
{
    for (uint32_t i = 0u; i < count; ++i) {
        if (semantic_named_handle_matches(handle_name, prefix, names[i])) {
            return true;
        }
    }
    return false;
}

static bool semantic_param_array_has_handle(
    const char *handle_name,
    const char *prefix,
    const nmo_behavior_param_desc_t *params,
    uint32_t count)
{
    for (uint32_t i = 0u; i < count; ++i) {
        if (semantic_named_handle_matches(handle_name, prefix, params[i].name)) {
            return true;
        }
    }
    return false;
}

static bool semantic_param_array_handle_type_guid(
    const char *handle_name,
    const char *prefix,
    const nmo_behavior_param_desc_t *params,
    uint32_t count,
    nmo_guid_t *out_type_guid)
{
    if (out_type_guid == NULL) {
        return false;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        if (semantic_named_handle_matches(handle_name, prefix, params[i].name)) {
            *out_type_guid = params[i].type_guid;
            return true;
        }
    }
    return false;
}

static bool semantic_add_node_has_handle(
    nmo_context_t *ctx,
    const nmo_edit_op_t *op,
    const char *handle_name)
{
    if (op == NULL || handle_name == NULL) {
        return false;
    }
    if (strcmp(handle_name, "node") == 0) {
        return true;
    }
    const nmo_behavior_proto_t *proto =
        ctx != NULL
            ? nmo_behavior_registry_find(
                  nmo_context_get_bb_registry(ctx),
                  op->data.add_node.bb_guid)
            : NULL;
    if (proto == NULL) {
        return true;
    }
    if (strcmp(handle_name, "target") == 0) {
        return (proto->behavior_flags & CKBEHAVIOR_TARGETABLE) != 0u;
    }
    if (semantic_string_array_has_handle(
            handle_name, "input", proto->inputs, proto->input_count) ||
        semantic_string_array_has_handle(
            handle_name, "output", proto->outputs, proto->output_count) ||
        semantic_param_array_has_handle(
            handle_name, "input_param", proto->input_params,
            proto->input_param_count) ||
        semantic_param_array_has_handle(
            handle_name, "input_param_source", proto->input_params,
            proto->input_param_count) ||
        semantic_param_array_has_handle(
            handle_name, "input_param", proto->settings,
            proto->setting_count) ||
        semantic_param_array_has_handle(
            handle_name, "input_param_source", proto->settings,
            proto->setting_count) ||
        semantic_param_array_has_handle(
            handle_name, "output_param", proto->output_params,
            proto->output_param_count) ||
        semantic_param_array_has_handle(
            handle_name, "local_param", proto->local_params,
            proto->local_param_count)) {
        return true;
    }
    return false;
}

static bool semantic_handle_has_prefix(const char *handle_name,
                                       const char *prefix)
{
    if (handle_name == NULL || prefix == NULL) {
        return false;
    }
    size_t prefix_len = strlen(prefix);
    return strncmp(handle_name, prefix, prefix_len) == 0 &&
           handle_name[prefix_len] == ':';
}

static bool semantic_handle_ref_is_control_endpoint(
    const nmo_edit_plan_t *plan,
    size_t ref_index,
    const char *handle_name)
{
    const nmo_edit_op_t *ref_op = nmo_edit_plan_get(plan, ref_index);
    if (ref_op == NULL || handle_name == NULL) {
        return false;
    }
    if (ref_op->kind == NMO_EDIT_OP_ADD_IO) {
        return strcmp(handle_name, "io") == 0;
    }
    if (ref_op->kind == NMO_EDIT_OP_ADD_NODE) {
        return semantic_handle_has_prefix(handle_name, "input") ||
               semantic_handle_has_prefix(handle_name, "output");
    }
    return false;
}

static nmo_status_t semantic_add_control_handle_ref_risk(
    const nmo_edit_plan_t *plan,
    size_t ref_index,
    const char *handle_name,
    nmo_object_id_t object_id,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count)
{
    if (semantic_handle_ref_is_control_endpoint(
            plan, ref_index, handle_name)) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "control_endpoint_type_mismatch",
        "Control-flow link handle must resolve to a behavior IO",
        object_id);
}

static bool semantic_handle_ref_is_parameter(
    const nmo_edit_plan_t *plan,
    size_t ref_index,
    const char *handle_name)
{
    const nmo_edit_op_t *ref_op = nmo_edit_plan_get(plan, ref_index);
    if (ref_op == NULL || handle_name == NULL) {
        return false;
    }
    if (ref_op->kind == NMO_EDIT_OP_ADD_PARAMETER) {
        return strcmp(handle_name, "parameter") == 0;
    }
    if (ref_op->kind == NMO_EDIT_OP_ADD_NODE) {
        return strcmp(handle_name, "target") == 0 ||
               semantic_handle_has_prefix(handle_name, "input_param") ||
               semantic_handle_has_prefix(handle_name, "input_param_source") ||
               semantic_handle_has_prefix(handle_name, "output_param") ||
               semantic_handle_has_prefix(handle_name, "local_param");
    }
    return false;
}

static nmo_status_t semantic_add_parameter_handle_ref_risk(
    const nmo_edit_plan_t *plan,
    size_t ref_index,
    const char *handle_name,
    nmo_object_id_t object_id,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count)
{
    if (semantic_handle_ref_is_parameter(plan, ref_index, handle_name)) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "parameter_object_type_mismatch",
        "Parameter handle must resolve to a parameter object",
        object_id);
}

static nmo_status_t semantic_add_invalid_handle_ref_risk(
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id)
{
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "invalid_handle_reference",
        "Edit operation references an invalid operation handle",
        object_id);
}

static nmo_status_t semantic_validate_handle_ref(
    nmo_context_t *ctx,
    const nmo_edit_plan_t *plan,
    size_t current_index,
    size_t ref_index,
    const char *handle_name,
    nmo_object_id_t object_id,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count)
{
    if (handle_name == NULL || handle_name[0] == '\0' ||
        ref_index >= current_index) {
        return semantic_add_invalid_handle_ref_risk(
            risks, risk_count, object_id);
    }

    const nmo_edit_op_t *ref_op = nmo_edit_plan_get(plan, ref_index);
    const char *expected = ref_op != NULL
        ? semantic_static_result_handle_name(ref_op->kind)
        : NULL;
    if (expected == NULL) {
        return semantic_add_invalid_handle_ref_risk(
            risks, risk_count, object_id);
    }

    if (ref_op->kind == NMO_EDIT_OP_ADD_NODE) {
        if (semantic_add_node_has_handle(ctx, ref_op, handle_name)) {
            return NMO_OK;
        }
        return semantic_add_invalid_handle_ref_risk(
            risks, risk_count, object_id);
    }

    if (strcmp(handle_name, expected) != 0) {
        return semantic_add_invalid_handle_ref_risk(
            risks, risk_count, object_id);
    }
    return NMO_OK;
}

static bool semantic_parameter_type_guid(
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id,
    nmo_guid_t *out_guid)
{
    if (out_guid == NULL) {
        return false;
    }
    *out_guid = NMO_GUID_NULL;
    nmo_object_t *object = repo != NULL
        ? nmo_object_repository_find_by_id(repo, parameter_id)
        : NULL;
    if (object == NULL) {
        return false;
    }
    switch (nmo_object_get_class_id(object)) {
    case NMO_CID_PARAMETERIN: {
        const nmo_parameterin_state_t *state =
            (const nmo_parameterin_state_t *)nmo_object_get_state(object);
        if (state == NULL) {
            return false;
        }
        *out_guid = state->type_guid;
        return true;
    }
    case NMO_CID_PARAMETEROUT: {
        const nmo_parameterout_state_t *state =
            (const nmo_parameterout_state_t *)nmo_object_get_state(object);
        if (state == NULL) {
            return false;
        }
        *out_guid = state->base.type_guid;
        return true;
    }
    case NMO_CID_PARAMETERLOCAL: {
        const nmo_parameterlocal_state_t *state =
            (const nmo_parameterlocal_state_t *)nmo_object_get_state(object);
        if (state == NULL) {
            return false;
        }
        *out_guid = state->base.type_guid;
        return true;
    }
    case NMO_CID_PARAMETER: {
        const nmo_parameter_state_t *state =
            (const nmo_parameter_state_t *)nmo_object_get_state(object);
        if (state == NULL) {
            return false;
        }
        *out_guid = state->type_guid;
        return true;
    }
    default:
        return false;
    }
}

static nmo_status_t semantic_add_parameter_type_mismatch_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id)
{
    nmo_guid_t source_guid = NMO_GUID_NULL;
    nmo_guid_t target_guid = NMO_GUID_NULL;
    if (!semantic_parameter_type_guid(repo, source_parameter_id, &source_guid) ||
        !semantic_parameter_type_guid(repo, target_parameter_id, &target_guid)) {
        return NMO_OK;
    }
    if (nmo_guid_is_null(source_guid) || nmo_guid_is_null(target_guid) ||
        nmo_guid_equals(source_guid, target_guid)) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "parameter_type_mismatch",
        "Parameter connection links incompatible source and target types",
        target_parameter_id);
}

static nmo_status_t semantic_add_parameter_type_desc_mismatch_risk(
    const nmo_type_descriptor_t *source_type,
    const nmo_type_descriptor_t *target_type,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id)
{
    if (source_type == NULL || target_type == NULL ||
        nmo_guid_equals(source_type->guid, target_type->guid)) {
        return NMO_OK;
    }
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "parameter_type_mismatch",
        "Parameter connection links incompatible source and target types",
        object_id);
}

static const nmo_type_descriptor_t *semantic_parameter_type_desc(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    nmo_object_id_t parameter_id)
{
    nmo_guid_t type_guid = NMO_GUID_NULL;
    if (ctx == NULL ||
        !semantic_parameter_type_guid(repo, parameter_id, &type_guid) ||
        nmo_guid_is_null(type_guid)) {
        return NULL;
    }

    nmo_type_registry_t *type_registry = nmo_context_get_type_registry(ctx);
    return type_registry != NULL
        ? nmo_type_registry_find_by_guid(type_registry, type_guid)
        : NULL;
}

static const nmo_type_descriptor_t *semantic_type_desc_from_guid(
    nmo_context_t *ctx,
    nmo_guid_t type_guid)
{
    if (ctx == NULL || nmo_guid_is_null(type_guid)) {
        return NULL;
    }
    nmo_type_registry_t *type_registry = nmo_context_get_type_registry(ctx);
    return type_registry != NULL
        ? nmo_type_registry_find_by_guid(type_registry, type_guid)
        : NULL;
}

static const nmo_type_descriptor_t *semantic_parameter_handle_type_desc(
    nmo_context_t *ctx,
    const nmo_edit_plan_t *plan,
    size_t ref_index,
    const char *handle_name)
{
    const nmo_edit_op_t *ref_op = nmo_edit_plan_get(plan, ref_index);
    if (ref_op == NULL || handle_name == NULL) {
        return NULL;
    }
    if (ref_op->kind == NMO_EDIT_OP_ADD_PARAMETER &&
        strcmp(handle_name, "parameter") == 0) {
        return semantic_type_desc_from_guid(
            ctx, ref_op->data.add_parameter.type_guid);
    }
    if (ref_op->kind == NMO_EDIT_OP_ADD_NODE) {
        const nmo_behavior_proto_t *proto =
            ctx != NULL
                ? nmo_behavior_registry_find(
                      nmo_context_get_bb_registry(ctx),
                      ref_op->data.add_node.bb_guid)
                : NULL;
        nmo_guid_t type_guid = NMO_GUID_NULL;
        if (proto == NULL) {
            return NULL;
        }
        if (semantic_param_array_handle_type_guid(
                handle_name, "input_param", proto->input_params,
                proto->input_param_count, &type_guid) ||
            semantic_param_array_handle_type_guid(
                handle_name, "input_param_source", proto->input_params,
                proto->input_param_count, &type_guid) ||
            semantic_param_array_handle_type_guid(
                handle_name, "input_param", proto->settings,
                proto->setting_count, &type_guid) ||
            semantic_param_array_handle_type_guid(
                handle_name, "input_param_source", proto->settings,
                proto->setting_count, &type_guid) ||
            semantic_param_array_handle_type_guid(
                handle_name, "output_param", proto->output_params,
                proto->output_param_count, &type_guid) ||
            semantic_param_array_handle_type_guid(
                handle_name, "local_param", proto->local_params,
                proto->local_param_count, &type_guid)) {
            return semantic_type_desc_from_guid(ctx, type_guid);
        }
    }
    return NULL;
}

static nmo_status_t semantic_add_operation_type_risk(
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    const char *code,
    const char *message,
    nmo_object_id_t object_id)
{
    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        code,
        message,
        object_id);
}

static nmo_status_t semantic_add_operation_signature_type_risk(
    nmo_context_t *ctx,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t object_id,
    nmo_guid_t operation_guid,
    const nmo_type_descriptor_t *in1_type,
    bool has_in1,
    const nmo_type_descriptor_t *in2_type,
    bool has_in2,
    const nmo_type_descriptor_t *out_type,
    bool has_out)
{
    if (nmo_guid_is_null(operation_guid) || (!has_in1 && !has_in2 && !has_out)) {
        return NMO_OK;
    }

    nmo_operation_registry_t *operation_registry =
        ctx != NULL ? nmo_context_get_operation_registry(ctx) : NULL;
    nmo_type_registry_t *type_registry =
        ctx != NULL ? nmo_context_get_type_registry(ctx) : NULL;
    if (operation_registry == NULL || type_registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    if ((has_in1 && in1_type == NULL) ||
        (has_in2 && in2_type == NULL) ||
        (has_out && out_type == NULL)) {
        return NMO_OK;
    }

    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t rc = out_type != NULL
        ? nmo_operation_registry_find_typed(
              operation_registry,
              &operation_guid,
              in1_type,
              in2_type,
              out_type,
              type_registry,
              &cell)
        : nmo_operation_registry_find(
              operation_registry,
              &operation_guid,
              in1_type,
              in2_type,
              type_registry,
              &cell);
    if (rc == NMO_OK) {
        return NMO_OK;
    }
    if (rc == NMO_ERR_NOT_FOUND || rc == NMO_ERR_VALIDATION_FAILED) {
        return semantic_add_operation_type_risk(
            risks,
            risk_count,
            "operation_type_mismatch",
            "Parameter operation slots are incompatible with the operation signature",
            object_id);
    }
    return rc;
}

static const nmo_parameteroperation_state_t *semantic_parameteroperation_state(
    nmo_object_repository_t *repo,
    nmo_object_id_t operation_id)
{
    nmo_object_t *object = repo != NULL
        ? nmo_object_repository_find_by_id(repo, operation_id)
        : NULL;
    if (object == NULL ||
        nmo_object_get_class_id(object) != NMO_CID_PARAMETEROPERATION) {
        return NULL;
    }
    return (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
}

static nmo_status_t semantic_add_operation_slot_ref_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t parameter_id)
{
    if (parameter_id == 0u || repo == NULL) {
        return NMO_OK;
    }
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, parameter_id);
    if (object == NULL) {
        return semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "operation_slot_dangling_reference",
            "Parameter operation slot references a missing parameter",
            parameter_id);
    }
    if (!semantic_is_parameter_object_class(nmo_object_get_class_id(object))) {
        return semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "operation_slot_type_mismatch",
            "Parameter operation slot must reference a parameter object",
            parameter_id);
    }
    return NMO_OK;
}

static nmo_status_t semantic_add_data_cell_risk(
    nmo_object_repository_t *repo,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col)
{
    NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
        repo, risks, risk_count, dataarray_id));

    nmo_object_t *object =
        repo != NULL ? nmo_object_repository_find_by_id(repo, dataarray_id)
                     : NULL;
    if (object == NULL) {
        return NMO_OK;
    }
    if (nmo_object_get_class_id(object) != NMO_CID_DATAARRAY) {
        return semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "dataarray_type_mismatch",
            "Data cell edit target is not a CKDataArray",
            dataarray_id);
    }

    const nmo_dataarray_state_t *state =
        (const nmo_dataarray_state_t *)nmo_object_get_state(object);
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    if (row >= state->row_count || col >= state->column_count ||
        state->rows == NULL || state->column_formats == NULL ||
        col >= state->rows[row].column_count) {
        return semantic_add_risk(
            risks,
            risk_count,
            NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
            "data_cell_bounds",
            "Data cell edit is outside the data array shape",
            dataarray_id);
    }

    return NMO_OK;
}

static nmo_status_t semantic_add_building_block_risk(
    nmo_context_t *ctx,
    nmo_behavior_semantic_risk_t **risks,
    size_t *risk_count,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid)
{
    if (nmo_guid_is_null(bb_guid)) {
        return NMO_OK;
    }

    const nmo_behavior_proto_t *proto =
        ctx != NULL
            ? nmo_behavior_registry_find(
                  nmo_context_get_bb_registry(ctx), bb_guid)
            : NULL;
    if (proto != NULL) {
        return NMO_OK;
    }

    return semantic_add_risk(
        risks,
        risk_count,
        NMO_BEHAVIOR_SEMANTIC_RISK_REJECT,
        "unknown_bb_signature",
        "Edit operation references an unknown building-block signature",
        parent_behavior_id);
}

static nmo_status_t semantic_validate_basic_edit_op(
    nmo_context_t *ctx,
    nmo_object_repository_t *repo,
    const nmo_edit_plan_t *plan,
    size_t op_index,
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
            NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                ctx,
                plan,
                op_index,
                op->data.set_value.parameter_ref_operation_index,
                op->data.set_value.parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            return semantic_add_parameter_handle_ref_risk(
                plan,
                op->data.set_value.parameter_ref_operation_index,
                op->data.set_value.parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count);
        }
        if (op->kind == NMO_EDIT_OP_SET_PARAMETER_BYTES &&
            op->data.set_bytes.has_parameter_ref) {
            NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                ctx,
                plan,
                op_index,
                op->data.set_bytes.parameter_ref_operation_index,
                op->data.set_bytes.parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            return semantic_add_parameter_handle_ref_risk(
                plan,
                op->data.set_bytes.parameter_ref_operation_index,
                op->data.set_bytes.parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count);
        }
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->primary_id));
        return semantic_add_parameter_object_ref_risk(
            repo, risks, risk_count, op->primary_id);
    case NMO_EDIT_OP_ADD_NODE:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.add_node.parent_behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_behavior_owner_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.add_node.parent_behavior_id));
        return semantic_add_building_block_risk(
            ctx,
            risks,
            risk_count,
            op->data.add_node.parent_behavior_id,
            op->data.add_node.bb_guid);
    case NMO_EDIT_OP_REMOVE_NODE:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.remove_node.parent_behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_behavior_owner_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.remove_node.parent_behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.remove_node.node_id));
        return semantic_add_behavior_node_ref_risk(
            repo, risks, risk_count, op->data.remove_node.node_id);
    case NMO_EDIT_OP_ADD_IO:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.add_io.behavior_id));
        return semantic_add_behavior_owner_ref_risk(
            repo, risks, risk_count, op->data.add_io.behavior_id);
    case NMO_EDIT_OP_RENAME_IO:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.rename_io.io_id));
        return semantic_add_behavior_io_ref_risk(
            repo, risks, risk_count, op->data.rename_io.io_id);
    case NMO_EDIT_OP_REMOVE_IO:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.remove_io.io_id));
        return semantic_add_behavior_io_ref_risk(
            repo, risks, risk_count, op->data.remove_io.io_id);
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.add_link.parent_behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_behavior_owner_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.add_link.parent_behavior_id));
        if (!op->data.add_link.has_from_io_ref) {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count, op->data.add_link.from_io_id));
            NMO_RETURN_IF_ERROR(semantic_add_class_ref_risk(
                repo,
                risks,
                risk_count,
                op->data.add_link.from_io_id,
                NMO_CID_BEHAVIORIO,
                "control_endpoint_type_mismatch",
                "Control-flow link endpoint must be a behavior IO"));
            NMO_RETURN_IF_ERROR(semantic_add_control_endpoint_scope_risk(
                repo,
                risks,
                risk_count,
                op->data.add_link.parent_behavior_id,
                op->data.add_link.from_io_id));
        } else {
            NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                ctx,
                plan,
                op_index,
                op->data.add_link.from_io_ref_operation_index,
                op->data.add_link.from_io_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            NMO_RETURN_IF_ERROR(semantic_add_control_handle_ref_risk(
                plan,
                op->data.add_link.from_io_ref_operation_index,
                op->data.add_link.from_io_ref_handle,
                op->primary_id,
                risks,
                risk_count));
        }
        if (!op->data.add_link.has_to_io_ref) {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count, op->data.add_link.to_io_id));
            NMO_RETURN_IF_ERROR(semantic_add_class_ref_risk(
                repo,
                risks,
                risk_count,
                op->data.add_link.to_io_id,
                NMO_CID_BEHAVIORIO,
                "control_endpoint_type_mismatch",
                "Control-flow link endpoint must be a behavior IO"));
            NMO_RETURN_IF_ERROR(semantic_add_control_endpoint_scope_risk(
                repo,
                risks,
                risk_count,
                op->data.add_link.parent_behavior_id,
                op->data.add_link.to_io_id));
        } else {
            NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                ctx,
                plan,
                op_index,
                op->data.add_link.to_io_ref_operation_index,
                op->data.add_link.to_io_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            NMO_RETURN_IF_ERROR(semantic_add_control_handle_ref_risk(
                plan,
                op->data.add_link.to_io_ref_operation_index,
                op->data.add_link.to_io_ref_handle,
                op->primary_id,
                risks,
                risk_count));
        }
        return semantic_add_plan_activation_delay_risk(
            risks,
            risk_count,
            op->data.add_link.parent_behavior_id,
            op->data.add_link.activation_delay);
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK:
    {
        nmo_object_id_t link_owner_id = 0u;
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.rewire_link.link_id));
        NMO_RETURN_IF_ERROR(semantic_add_class_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.rewire_link.link_id,
            NMO_CID_BEHAVIORLINK,
            "control_link_type_mismatch",
            "Control-flow link operation expects a behavior link"));
        (void)semantic_find_behavior_link_owner(
            repo, op->data.rewire_link.link_id, &link_owner_id);
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.rewire_link.from_io_id));
        NMO_RETURN_IF_ERROR(semantic_add_class_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.rewire_link.from_io_id,
            NMO_CID_BEHAVIORIO,
            "control_endpoint_type_mismatch",
            "Control-flow link endpoint must be a behavior IO"));
        NMO_RETURN_IF_ERROR(semantic_add_control_endpoint_scope_risk(
            repo,
            risks,
            risk_count,
            link_owner_id,
            op->data.rewire_link.from_io_id));
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.rewire_link.to_io_id));
        NMO_RETURN_IF_ERROR(semantic_add_class_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.rewire_link.to_io_id,
            NMO_CID_BEHAVIORIO,
            "control_endpoint_type_mismatch",
            "Control-flow link endpoint must be a behavior IO"));
        return semantic_add_control_endpoint_scope_risk(
            repo,
            risks,
            risk_count,
            link_owner_id,
            op->data.rewire_link.to_io_id);
    }
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.set_link_delay.link_id));
        NMO_RETURN_IF_ERROR(semantic_add_class_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.set_link_delay.link_id,
            NMO_CID_BEHAVIORLINK,
            "control_link_type_mismatch",
            "Control-flow link operation expects a behavior link"));
        return semantic_add_plan_activation_delay_risk(
            risks,
            risk_count,
            op->data.set_link_delay.link_id,
            op->data.set_link_delay.activation_delay);
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.remove_link.parent_behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_behavior_owner_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.remove_link.parent_behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.remove_link.link_id));
        return semantic_add_class_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.remove_link.link_id,
            NMO_CID_BEHAVIORLINK,
            "control_link_type_mismatch",
            "Control-flow link operation expects a behavior link");
    case NMO_EDIT_OP_ADD_PARAMETER:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.add_parameter.owner_behavior_id));
        return semantic_add_behavior_owner_ref_risk(
            repo, risks, risk_count,
            op->data.add_parameter.owner_behavior_id);
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.connect_parameter.source_parameter_id));
        NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
            repo, risks, risk_count,
            op->data.connect_parameter.source_parameter_id));
        if (op->data.connect_parameter.has_target_parameter_ref) {
            NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                ctx,
                plan,
                op_index,
                op->data.connect_parameter.target_parameter_ref_operation_index,
                op->data.connect_parameter.target_parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            NMO_RETURN_IF_ERROR(semantic_add_parameter_handle_ref_risk(
                plan,
                op->data.connect_parameter.target_parameter_ref_operation_index,
                op->data.connect_parameter.target_parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            return semantic_add_parameter_type_desc_mismatch_risk(
                semantic_parameter_type_desc(
                    ctx,
                    repo,
                    op->data.connect_parameter.source_parameter_id),
                semantic_parameter_handle_type_desc(
                    ctx,
                    plan,
                    op->data.connect_parameter.target_parameter_ref_operation_index,
                    op->data.connect_parameter.target_parameter_ref_handle),
                risks,
                risk_count,
                op->primary_id);
        }
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.connect_parameter.target_parameter_id));
        NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
            repo, risks, risk_count,
            op->data.connect_parameter.target_parameter_id));
        NMO_RETURN_IF_ERROR(semantic_add_parameterin_ref_risk(
            repo, risks, risk_count,
            op->data.connect_parameter.target_parameter_id));
        return semantic_add_parameter_type_mismatch_risk(
            repo,
            risks,
            risk_count,
            op->data.connect_parameter.source_parameter_id,
            op->data.connect_parameter.target_parameter_id);
    case NMO_EDIT_OP_DISCONNECT_PARAMETER:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.disconnect_parameter.target_parameter_id));
        NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
            repo, risks, risk_count,
            op->data.disconnect_parameter.target_parameter_id));
        return semantic_add_parameterin_ref_risk(
            repo, risks, risk_count,
            op->data.disconnect_parameter.target_parameter_id);
    case NMO_EDIT_OP_REMOVE_PARAMETER:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.remove_parameter.parameter_id));
        return semantic_add_parameter_object_ref_risk(
            repo, risks, risk_count,
            op->data.remove_parameter.parameter_id);
    case NMO_EDIT_OP_ADD_OPERATION: {
        const nmo_type_descriptor_t *in1_type = NULL;
        const nmo_type_descriptor_t *in2_type = NULL;
        const nmo_type_descriptor_t *out_type = NULL;
        bool has_in1 = false;
        bool has_in2 = false;
        bool has_out = false;

        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.add_operation.parent_behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_behavior_owner_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.add_operation.parent_behavior_id));
        if (op->data.add_operation.has_in1_parameter_ref) {
            NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                ctx,
                plan,
                op_index,
                op->data.add_operation.in1_parameter_ref_operation_index,
                op->data.add_operation.in1_parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            NMO_RETURN_IF_ERROR(semantic_add_parameter_handle_ref_risk(
                plan,
                op->data.add_operation.in1_parameter_ref_operation_index,
                op->data.add_operation.in1_parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            in1_type = semantic_parameter_handle_type_desc(
                ctx,
                plan,
                op->data.add_operation.in1_parameter_ref_operation_index,
                op->data.add_operation.in1_parameter_ref_handle);
            has_in1 = true;
        } else {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.in1_parameter_id));
            NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.in1_parameter_id));
            in1_type = semantic_parameter_type_desc(
                ctx, repo, op->data.add_operation.in1_parameter_id);
            has_in1 = op->data.add_operation.in1_parameter_id != 0u;
        }
        if (op->data.add_operation.has_in2_parameter_ref) {
            NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                ctx,
                plan,
                op_index,
                op->data.add_operation.in2_parameter_ref_operation_index,
                op->data.add_operation.in2_parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            NMO_RETURN_IF_ERROR(semantic_add_parameter_handle_ref_risk(
                plan,
                op->data.add_operation.in2_parameter_ref_operation_index,
                op->data.add_operation.in2_parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            in2_type = semantic_parameter_handle_type_desc(
                ctx,
                plan,
                op->data.add_operation.in2_parameter_ref_operation_index,
                op->data.add_operation.in2_parameter_ref_handle);
            has_in2 = true;
        } else {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.in2_parameter_id));
            NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.in2_parameter_id));
            in2_type = semantic_parameter_type_desc(
                ctx, repo, op->data.add_operation.in2_parameter_id);
            has_in2 = op->data.add_operation.in2_parameter_id != 0u;
        }
        if (op->data.add_operation.has_out_parameter_ref) {
            NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                ctx,
                plan,
                op_index,
                op->data.add_operation.out_parameter_ref_operation_index,
                op->data.add_operation.out_parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            NMO_RETURN_IF_ERROR(semantic_add_parameter_handle_ref_risk(
                plan,
                op->data.add_operation.out_parameter_ref_operation_index,
                op->data.add_operation.out_parameter_ref_handle,
                op->primary_id,
                risks,
                risk_count));
            out_type = semantic_parameter_handle_type_desc(
                ctx,
                plan,
                op->data.add_operation.out_parameter_ref_operation_index,
                op->data.add_operation.out_parameter_ref_handle);
            has_out = true;
        } else {
            NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.out_parameter_id));
            NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
                repo, risks, risk_count,
                op->data.add_operation.out_parameter_id));
            out_type = semantic_parameter_type_desc(
                ctx, repo, op->data.add_operation.out_parameter_id);
            has_out = op->data.add_operation.out_parameter_id != 0u;
        }
        return semantic_add_operation_signature_type_risk(
            ctx,
            risks,
            risk_count,
            op->primary_id,
            op->data.add_operation.operation_guid,
            in1_type,
            has_in1,
            in2_type,
            has_in2,
            out_type,
            has_out);
    }
    case NMO_EDIT_OP_REWIRE_OPERATION: {
        const nmo_parameteroperation_state_t *state = NULL;
        const nmo_type_descriptor_t *in1_type = NULL;
        const nmo_type_descriptor_t *in2_type = NULL;
        const nmo_type_descriptor_t *out_type = NULL;
        bool has_in1 = false;
        bool has_in2 = false;
        bool has_out = false;

        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.rewire_operation.operation_id));
        NMO_RETURN_IF_ERROR(semantic_add_class_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.rewire_operation.operation_id,
            NMO_CID_PARAMETEROPERATION,
            "operation_object_type_mismatch",
            "Edit operation expects a parameter operation"));
        state = semantic_parameteroperation_state(
            repo, op->data.rewire_operation.operation_id);
        if (state == NULL) {
            return NMO_OK;
        }

        if ((op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u) {
            if (op->data.rewire_operation.has_in1_parameter_ref) {
                NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                    ctx,
                    plan,
                    op_index,
                    op->data.rewire_operation.in1_parameter_ref_operation_index,
                    op->data.rewire_operation.in1_parameter_ref_handle,
                    op->primary_id,
                    risks,
                    risk_count));
                NMO_RETURN_IF_ERROR(semantic_add_parameter_handle_ref_risk(
                    plan,
                    op->data.rewire_operation.in1_parameter_ref_operation_index,
                    op->data.rewire_operation.in1_parameter_ref_handle,
                    op->primary_id,
                    risks,
                    risk_count));
                in1_type = semantic_parameter_handle_type_desc(
                    ctx,
                    plan,
                    op->data.rewire_operation.in1_parameter_ref_operation_index,
                    op->data.rewire_operation.in1_parameter_ref_handle);
                has_in1 = true;
            } else {
                NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                    repo, risks, risk_count,
                    op->data.rewire_operation.in1_parameter_id));
                NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
                    repo, risks, risk_count,
                    op->data.rewire_operation.in1_parameter_id));
                in1_type = semantic_parameter_type_desc(
                    ctx, repo, op->data.rewire_operation.in1_parameter_id);
                has_in1 = op->data.rewire_operation.in1_parameter_id != 0u;
            }
        } else {
            NMO_RETURN_IF_ERROR(semantic_add_operation_slot_ref_risk(
                repo,
                risks,
                risk_count,
                state->has_in1 ? state->in1_id : 0u));
            in1_type = semantic_parameter_type_desc(
                ctx, repo, state->has_in1 ? state->in1_id : 0u);
            has_in1 = state->has_in1 != 0u;
        }

        if ((op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u) {
            if (op->data.rewire_operation.has_in2_parameter_ref) {
                NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                    ctx,
                    plan,
                    op_index,
                    op->data.rewire_operation.in2_parameter_ref_operation_index,
                    op->data.rewire_operation.in2_parameter_ref_handle,
                    op->primary_id,
                    risks,
                    risk_count));
                NMO_RETURN_IF_ERROR(semantic_add_parameter_handle_ref_risk(
                    plan,
                    op->data.rewire_operation.in2_parameter_ref_operation_index,
                    op->data.rewire_operation.in2_parameter_ref_handle,
                    op->primary_id,
                    risks,
                    risk_count));
                in2_type = semantic_parameter_handle_type_desc(
                    ctx,
                    plan,
                    op->data.rewire_operation.in2_parameter_ref_operation_index,
                    op->data.rewire_operation.in2_parameter_ref_handle);
                has_in2 = true;
            } else {
                NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                    repo, risks, risk_count,
                    op->data.rewire_operation.in2_parameter_id));
                NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
                    repo, risks, risk_count,
                    op->data.rewire_operation.in2_parameter_id));
                in2_type = semantic_parameter_type_desc(
                    ctx, repo, op->data.rewire_operation.in2_parameter_id);
                has_in2 = op->data.rewire_operation.in2_parameter_id != 0u;
            }
        } else {
            NMO_RETURN_IF_ERROR(semantic_add_operation_slot_ref_risk(
                repo,
                risks,
                risk_count,
                state->has_in2 ? state->in2_id : 0u));
            in2_type = semantic_parameter_type_desc(
                ctx, repo, state->has_in2 ? state->in2_id : 0u);
            has_in2 = state->has_in2 != 0u;
        }

        if ((op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u) {
            if (op->data.rewire_operation.has_out_parameter_ref) {
                NMO_RETURN_IF_ERROR(semantic_validate_handle_ref(
                    ctx,
                    plan,
                    op_index,
                    op->data.rewire_operation.out_parameter_ref_operation_index,
                    op->data.rewire_operation.out_parameter_ref_handle,
                    op->primary_id,
                    risks,
                    risk_count));
                NMO_RETURN_IF_ERROR(semantic_add_parameter_handle_ref_risk(
                    plan,
                    op->data.rewire_operation.out_parameter_ref_operation_index,
                    op->data.rewire_operation.out_parameter_ref_handle,
                    op->primary_id,
                    risks,
                    risk_count));
                out_type = semantic_parameter_handle_type_desc(
                    ctx,
                    plan,
                    op->data.rewire_operation.out_parameter_ref_operation_index,
                    op->data.rewire_operation.out_parameter_ref_handle);
                has_out = true;
            } else {
                NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
                    repo, risks, risk_count,
                    op->data.rewire_operation.out_parameter_id));
                NMO_RETURN_IF_ERROR(semantic_add_parameter_object_ref_risk(
                    repo, risks, risk_count,
                    op->data.rewire_operation.out_parameter_id));
                out_type = semantic_parameter_type_desc(
                    ctx, repo, op->data.rewire_operation.out_parameter_id);
                has_out = op->data.rewire_operation.out_parameter_id != 0u;
            }
        } else {
            NMO_RETURN_IF_ERROR(semantic_add_operation_slot_ref_risk(
                repo,
                risks,
                risk_count,
                state->has_out ? state->out_id : 0u));
            out_type = semantic_parameter_type_desc(
                ctx, repo, state->has_out ? state->out_id : 0u);
            has_out = state->has_out != 0u;
        }

        return semantic_add_operation_signature_type_risk(
            ctx,
            risks,
            risk_count,
            op->data.rewire_operation.operation_id,
            state->operation_guid,
            in1_type,
            has_in1,
            in2_type,
            has_in2,
            out_type,
            has_out);
    }
    case NMO_EDIT_OP_REMOVE_OPERATION:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.remove_operation.operation_id));
        return semantic_add_class_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.remove_operation.operation_id,
            NMO_CID_PARAMETEROPERATION,
            "operation_object_type_mismatch",
            "Edit operation expects a parameter operation");
    case NMO_EDIT_OP_INTERFACE_POLICY:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count, op->data.interface_policy.behavior_id));
        return semantic_add_behavior_owner_ref_risk(
            repo, risks, risk_count, op->data.interface_policy.behavior_id);
    case NMO_EDIT_OP_SET_DATA_CELL:
        return semantic_add_data_cell_risk(
            repo,
            risks,
            risk_count,
            op->data.data_cell.dataarray_id,
            op->data.data_cell.row,
            op->data.data_cell.col);
    case NMO_EDIT_OP_FOLD:
        return NMO_OK;
    case NMO_EDIT_OP_REPLACE_BB:
        NMO_RETURN_IF_ERROR(semantic_add_missing_ref_risk(
            repo, risks, risk_count,
            op->data.replace_bb.desc.behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_behavior_owner_ref_risk(
            repo,
            risks,
            risk_count,
            op->data.replace_bb.desc.behavior_id));
        NMO_RETURN_IF_ERROR(semantic_add_behavior_target_consistency_risk(
            repo,
            risks,
            risk_count,
            op->data.replace_bb.desc.behavior_id));
        return semantic_add_building_block_risk(
            ctx,
            risks,
            risk_count,
            op->data.replace_bb.desc.behavior_id,
            op->data.replace_bb.desc.block_guid);
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
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
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
            ctx, repo, plan, i, op, &risks, &risk_count);
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
            nmo_object_t *target = nmo_object_repository_find_by_id(repo, node_id);
            if (target != NULL &&
                nmo_object_get_class_id(target) != NMO_CID_BEHAVIOR) {
                continue;
            }
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
