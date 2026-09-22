/**
 * @file edit_plan.c
 * @brief Edit plan storage and the operation builder API.
 */

#include "edit_plan_internal.h"

#include <stdlib.h>
#include <string.h>

char *edit_plan_strdup(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    size_t len = strlen(text) + 1u;
    char *copy = (char *)malloc(len);
    if (copy != NULL) {
        memcpy(copy, text, len);
    }
    return copy;
}

nmo_status_t edit_plan_probe_analysis_copy(
    nmo_probe_selector_result_t *dst,
    const nmo_probe_selector_result_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_probe_analysis_dispose(dst);
    *dst = *src;
    dst->candidates = NULL;
    dst->candidate_count = 0u;
    dst->candidate_capacity = 0u;
    for (size_t i = 0; i < src->candidate_count; ++i) {
        nmo_status_t rc =
            nmo_probe_selector_result_add_candidate(dst, &src->candidates[i]);
        if (rc != NMO_OK) {
            nmo_probe_analysis_dispose(dst);
            return rc;
        }
    }
    return NMO_OK;
}

static nmo_status_t edit_plan_reserve(nmo_edit_plan_t *plan, size_t needed)
{
    if (plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (needed <= plan->capacity) {
        return NMO_OK;
    }
    size_t new_capacity = plan->capacity == 0 ? 8u : plan->capacity * 2u;
    while (new_capacity < needed) {
        new_capacity *= 2u;
    }
    nmo_edit_op_t *new_ops =
        (nmo_edit_op_t *)realloc(plan->ops, new_capacity * sizeof(*new_ops));
    if (new_ops == NULL) {
        return NMO_ERR_NOMEM;
    }
    plan->ops = new_ops;
    plan->capacity = new_capacity;
    return NMO_OK;
}

static edit_plan_handle_ref_t edit_plan_no_handle_ref(void)
{
    return (edit_plan_handle_ref_t){0};
}

static edit_plan_handle_ref_t edit_plan_ref_or_none(
    const nmo_edit_handle_ref_t *ref)
{
    return ref != NULL ? *ref : edit_plan_no_handle_ref();
}

static nmo_status_t edit_plan_append_op(
    nmo_edit_plan_t *plan,
    nmo_edit_op_kind_t kind,
    nmo_object_id_t primary_id,
    bool allow_zero_primary,
    nmo_edit_op_t **out_op)
{
    if (plan == NULL || out_op == NULL ||
        (!allow_zero_primary && primary_id == 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    op->primary_id = primary_id;
    *out_op = op;
    return NMO_OK;
}

static nmo_status_t edit_plan_append_blank(
    nmo_edit_plan_t *plan,
    nmo_edit_op_kind_t kind,
    nmo_object_id_t primary_id,
    nmo_edit_op_t **out_op)
{
    return edit_plan_append_op(plan, kind, primary_id, false, out_op);
}

nmo_status_t nmo_edit_plan_create(nmo_edit_plan_t **out_plan)
{
    if (out_plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_plan = (nmo_edit_plan_t *)calloc(1, sizeof(nmo_edit_plan_t));
    return *out_plan != NULL ? NMO_OK : NMO_ERR_NOMEM;
}

nmo_status_t nmo_edit_plan_clone(
    const nmo_edit_plan_t *plan,
    nmo_edit_plan_t **out_plan)
{
    if (plan == NULL || out_plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_plan = NULL;
    nmo_edit_plan_t *clone = NULL;
    NMO_RETURN_IF_ERROR(nmo_edit_plan_create(&clone));
    nmo_status_t rc = edit_plan_reserve(clone, plan->count);
    if (rc != NMO_OK) {
        nmo_edit_plan_destroy(clone);
        return rc;
    }
    for (size_t i = 0; i < plan->count; ++i) {
        rc = edit_op_copy(&clone->ops[i], &plan->ops[i]);
        if (rc != NMO_OK) {
            for (size_t j = 0; j < i; ++j) {
                edit_op_dispose(&clone->ops[j]);
            }
            nmo_edit_plan_destroy(clone);
            return rc;
        }
        clone->count++;
    }
    if (plan->has_probe_selector_analysis) {
        rc = edit_plan_probe_analysis_copy(&clone->probe_selector_analysis,
                                           &plan->probe_selector_analysis);
        if (rc != NMO_OK) {
            nmo_edit_plan_destroy(clone);
            return rc;
        }
        clone->has_probe_selector_analysis = true;
    }
    *out_plan = clone;
    return NMO_OK;
}

void nmo_edit_plan_destroy(nmo_edit_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    for (size_t i = 0; i < plan->count; i++) {
        edit_op_dispose(&plan->ops[i]);
    }
    nmo_probe_analysis_dispose(&plan->probe_selector_analysis);
    free(plan->ops);
    free(plan);
}

size_t nmo_edit_plan_count(const nmo_edit_plan_t *plan)
{
    return plan != NULL ? plan->count : 0u;
}

const nmo_edit_op_t *nmo_edit_plan_get(const nmo_edit_plan_t *plan, size_t index)
{
    if (plan == NULL || index >= plan->count) {
        return NULL;
    }
    return &plan->ops[index];
}

nmo_status_t nmo_edit_plan_set_probe_selector_analysis(
    nmo_edit_plan_t *plan,
    const nmo_probe_selector_result_t *analysis)
{
    if (plan == NULL || analysis == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t rc = edit_plan_probe_analysis_copy(
        &plan->probe_selector_analysis, analysis);
    if (rc != NMO_OK) {
        plan->has_probe_selector_analysis = false;
        return rc;
    }
    plan->has_probe_selector_analysis = true;
    return NMO_OK;
}

const nmo_probe_selector_result_t *
nmo_edit_plan_get_probe_selector_analysis(const nmo_edit_plan_t *plan)
{
    return plan != NULL && plan->has_probe_selector_analysis
        ? &plan->probe_selector_analysis
        : NULL;
}

static nmo_status_t edit_plan_add_set_parameter_value_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    edit_plan_handle_ref_t parameter_ref,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    nmo_edit_op_t *op = NULL;
    if (plan == NULL || value_str == NULL ||
        (parameter_id == 0u && !parameter_ref.has_ref)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_op(
        plan,
        NMO_EDIT_OP_SET_PARAMETER_VALUE,
        parameter_id,
        parameter_ref.has_ref,
        &op));
    op->data.set_value.value = edit_plan_strdup(value_str);
    if (op->data.set_value.value == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.set_value.parameter_ref, &parameter_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    st = edit_plan_copy_parameter_write_options(
        &op->data.set_value.options,
        &op->data.set_value.has_options,
        options);
    if (st != NMO_OK) {
        edit_op_dispose(op);
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_set_parameter_bytes_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    edit_plan_handle_ref_t parameter_ref,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    nmo_edit_op_t *op = NULL;
    if (plan == NULL || (bytes == NULL && byte_count > 0u) ||
        (parameter_id == 0u && !parameter_ref.has_ref)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_op(
        plan,
        NMO_EDIT_OP_SET_PARAMETER_BYTES,
        parameter_id,
        parameter_ref.has_ref,
        &op));
    nmo_status_t st = edit_plan_copy_bytes(
        bytes, byte_count, &op->data.set_bytes.bytes);
    if (st != NMO_OK) {
        edit_op_dispose(op);
        return st;
    }
    op->data.set_bytes.byte_count = byte_count;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.set_bytes.parameter_ref, &parameter_ref},
    };
    st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    st = edit_plan_copy_parameter_write_options(
        &op->data.set_bytes.options,
        &op->data.set_bytes.has_options,
        options);
    if (st != NMO_OK) {
        edit_op_dispose(op);
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_behavior_link_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    edit_plan_handle_ref_t from_ref,
    nmo_object_id_t to_io_id,
    edit_plan_handle_ref_t to_ref,
    uint32_t activation_delay)
{
    nmo_edit_op_t *op = NULL;
    if (parent_behavior_id == 0u ||
        (from_io_id == 0u && !from_ref.has_ref) ||
        (to_io_id == 0u && !to_ref.has_ref)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_BEHAVIOR_LINK, parent_behavior_id, &op));
    op->data.add_link.parent_behavior_id = parent_behavior_id;
    op->data.add_link.from_io_id = from_io_id;
    op->data.add_link.to_io_id = to_io_id;
    op->data.add_link.activation_delay = activation_delay;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.add_link.from_io_ref, &from_ref},
        {&op->data.add_link.to_io_ref, &to_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_connect_parameter_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id,
    edit_plan_handle_ref_t target_ref)
{
    nmo_edit_op_t *op = NULL;
    if (source_parameter_id == 0u ||
        (target_parameter_id == 0u && !target_ref.has_ref)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_id_t primary_id =
        target_ref.has_ref ? source_parameter_id : target_parameter_id;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_CONNECT_PARAMETER, primary_id, &op));
    op->data.connect_parameter.source_parameter_id = source_parameter_id;
    op->data.connect_parameter.target_parameter_id = target_parameter_id;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.connect_parameter.target_parameter_ref, &target_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_operation_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    edit_plan_handle_ref_t in1_ref,
    nmo_object_id_t in2_parameter_id,
    edit_plan_handle_ref_t in2_ref,
    nmo_object_id_t out_parameter_id,
    edit_plan_handle_ref_t out_ref)
{
    nmo_edit_op_t *op = NULL;
    if (nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_OPERATION, parent_behavior_id, &op));
    op->data.add_operation.parent_behavior_id = parent_behavior_id;
    op->data.add_operation.operation_guid = operation_guid;
    op->data.add_operation.in1_parameter_id = in1_parameter_id;
    op->data.add_operation.in2_parameter_id = in2_parameter_id;
    op->data.add_operation.out_parameter_id = out_parameter_id;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.add_operation.in1_parameter_ref, &in1_ref},
        {&op->data.add_operation.in2_parameter_ref, &in2_ref},
        {&op->data.add_operation.out_parameter_ref, &out_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    plan->count++;
    return NMO_OK;
}

static nmo_status_t edit_plan_add_rewire_operation_core(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    bool require_slot_flags,
    nmo_object_id_t in1_parameter_id,
    edit_plan_handle_ref_t in1_ref,
    nmo_object_id_t in2_parameter_id,
    edit_plan_handle_ref_t in2_ref,
    nmo_object_id_t out_parameter_id,
    edit_plan_handle_ref_t out_ref)
{
    nmo_edit_op_t *op = NULL;
    if (operation_id == 0u || (require_slot_flags && slot_flags == 0u)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REWIRE_OPERATION, operation_id, &op));
    op->data.rewire_operation.operation_id = operation_id;
    op->data.rewire_operation.slot_flags = slot_flags;
    op->data.rewire_operation.in1_parameter_id = in1_parameter_id;
    op->data.rewire_operation.in2_parameter_id = in2_parameter_id;
    op->data.rewire_operation.out_parameter_id = out_parameter_id;
    edit_plan_handle_ref_clone_slot_t refs[] = {
        {&op->data.rewire_operation.in1_parameter_ref, &in1_ref},
        {&op->data.rewire_operation.in2_parameter_ref, &in2_ref},
        {&op->data.rewire_operation.out_parameter_ref, &out_ref},
    };
    nmo_status_t st = edit_op_clone_handle_ref_slots_or_dispose(
        op, refs, sizeof(refs) / sizeof(refs[0]));
    if (st != NMO_OK) {
        return st;
    }
    if (in1_ref.has_ref) {
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN1;
    }
    if (in2_ref.has_ref) {
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN2;
    }
    if (out_ref.has_ref) {
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_OUT;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_set_parameter_value(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const nmo_edit_handle_ref_t *parameter_ref,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    return edit_plan_add_set_parameter_value_core(
        plan,
        parameter_id,
        edit_plan_ref_or_none(parameter_ref),
        value_str,
        options);
}

nmo_status_t nmo_edit_plan_add_set_parameter_bytes(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const nmo_edit_handle_ref_t *parameter_ref,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    return edit_plan_add_set_parameter_bytes_core(
        plan,
        parameter_id,
        edit_plan_ref_or_none(parameter_ref),
        bytes,
        byte_count,
        options);
}

nmo_status_t nmo_edit_plan_add_node(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name)
{
    return nmo_edit_plan_add_node_ex(
        plan, parent_behavior_id, bb_guid, name, NULL);
}

nmo_status_t nmo_edit_plan_add_node_ex(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name,
    const nmo_add_node_options_t *options)
{
    nmo_edit_op_t *op = NULL;
    if (plan == NULL || parent_behavior_id == 0 || nmo_guid_is_null(bb_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_NODE, parent_behavior_id, &op));
    op->data.add_node.parent_behavior_id = parent_behavior_id;
    op->data.add_node.bb_guid = bb_guid;
    if (name != NULL) {
        op->data.add_node.name = edit_plan_strdup(name);
        if (op->data.add_node.name == NULL) {
            return NMO_ERR_NOMEM;
        }
    }
    if (options != NULL) {
        nmo_status_t st = edit_plan_add_node_options_clone(
            &op->data.add_node.options, options);
        if (st != NMO_OK) {
            edit_op_dispose(op);
            return st;
        }
        op->data.add_node.has_options = true;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_remove_node(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t node_id,
    uint32_t delete_flags)
{
    nmo_edit_op_t *op = NULL;
    if (node_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_NODE, parent_behavior_id, &op));
    op->data.remove_node.parent_behavior_id = parent_behavior_id;
    op->data.remove_node.node_id = node_id;
    op->data.remove_node.delete_flags = delete_flags;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t behavior_id,
    nmo_script_edit_io_kind_t kind,
    const char *name)
{
    nmo_edit_op_t *op = NULL;
    if (name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_IO, behavior_id, &op));
    op->data.add_io.behavior_id = behavior_id;
    op->data.add_io.kind = kind;
    op->data.add_io.name = edit_plan_strdup(name);
    if (op->data.add_io.name == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_rename_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t io_id,
    const char *name)
{
    nmo_edit_op_t *op = NULL;
    if (name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_RENAME_IO, io_id, &op));
    op->data.rename_io.io_id = io_id;
    op->data.rename_io.name = edit_plan_strdup(name);
    if (op->data.rename_io.name == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_remove_io(
    nmo_edit_plan_t *plan,
    nmo_object_id_t io_id,
    bool detach_links)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_IO, io_id, &op));
    op->data.remove_io.io_id = io_id;
    op->data.remove_io.detach_links = detach_links;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    const nmo_edit_handle_ref_t *from_io_ref,
    nmo_object_id_t to_io_id,
    const nmo_edit_handle_ref_t *to_io_ref,
    uint32_t activation_delay)
{
    return edit_plan_add_behavior_link_core(
        plan,
        parent_behavior_id,
        from_io_id,
        edit_plan_ref_or_none(from_io_ref),
        to_io_id,
        edit_plan_ref_or_none(to_io_ref),
        activation_delay);
}

nmo_status_t nmo_edit_plan_add_rewire_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t link_id,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id)
{
    nmo_edit_op_t *op = NULL;
    if (link_id == 0 || (from_io_id == 0 && to_io_id == 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK, link_id, &op));
    op->data.rewire_link.link_id = link_id;
    op->data.rewire_link.from_io_id = from_io_id;
    op->data.rewire_link.to_io_id = to_io_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_set_behavior_link_delay(
    nmo_edit_plan_t *plan,
    nmo_object_id_t link_id,
    uint32_t activation_delay)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY, link_id, &op));
    op->data.set_link_delay.link_id = link_id;
    op->data.set_link_delay.activation_delay = activation_delay;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_remove_behavior_link(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t link_id)
{
    nmo_edit_op_t *op = NULL;
    if (link_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK, parent_behavior_id, &op));
    op->data.remove_link.parent_behavior_id = parent_behavior_id;
    op->data.remove_link.link_id = link_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t owner_behavior_id,
    nmo_script_edit_parameter_kind_t kind,
    nmo_guid_t type_guid,
    const char *name)
{
    nmo_edit_op_t *op = NULL;
    if (nmo_guid_is_null(type_guid) || name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_PARAMETER, owner_behavior_id, &op));
    op->data.add_parameter.owner_behavior_id = owner_behavior_id;
    op->data.add_parameter.kind = kind;
    op->data.add_parameter.type_guid = type_guid;
    op->data.add_parameter.name = edit_plan_strdup(name);
    if (op->data.add_parameter.name == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_connect_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t source_parameter_id,
    nmo_object_id_t target_parameter_id,
    const nmo_edit_handle_ref_t *target_parameter_ref)
{
    return edit_plan_add_connect_parameter_core(
        plan,
        source_parameter_id,
        target_parameter_id,
        edit_plan_ref_or_none(target_parameter_ref));
}

nmo_status_t nmo_edit_plan_add_disconnect_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t target_parameter_id)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_DISCONNECT_PARAMETER, target_parameter_id, &op));
    op->data.disconnect_parameter.target_parameter_id = target_parameter_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_remove_parameter(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    bool detach)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_PARAMETER, parameter_id, &op));
    op->data.remove_parameter.parameter_id = parameter_id;
    op->data.remove_parameter.detach = detach;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    const nmo_edit_handle_ref_t *in1_parameter_ref,
    nmo_object_id_t in2_parameter_id,
    const nmo_edit_handle_ref_t *in2_parameter_ref,
    nmo_object_id_t out_parameter_id,
    const nmo_edit_handle_ref_t *out_parameter_ref)
{
    return edit_plan_add_operation_core(
        plan,
        parent_behavior_id,
        operation_guid,
        in1_parameter_id,
        edit_plan_ref_or_none(in1_parameter_ref),
        in2_parameter_id,
        edit_plan_ref_or_none(in2_parameter_ref),
        out_parameter_id,
        edit_plan_ref_or_none(out_parameter_ref));
}

nmo_status_t nmo_edit_plan_add_rewire_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    const nmo_edit_handle_ref_t *in1_parameter_ref,
    nmo_object_id_t in2_parameter_id,
    const nmo_edit_handle_ref_t *in2_parameter_ref,
    nmo_object_id_t out_parameter_id,
    const nmo_edit_handle_ref_t *out_parameter_ref)
{
    return edit_plan_add_rewire_operation_core(
        plan,
        operation_id,
        slot_flags,
        false,
        in1_parameter_id,
        edit_plan_ref_or_none(in1_parameter_ref),
        in2_parameter_id,
        edit_plan_ref_or_none(in2_parameter_ref),
        out_parameter_id,
        edit_plan_ref_or_none(out_parameter_ref));
}

nmo_status_t nmo_edit_plan_add_remove_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REMOVE_OPERATION, operation_id, &op));
    op->data.remove_operation.operation_id = operation_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_interface_policy(
    nmo_edit_plan_t *plan,
    nmo_object_id_t behavior_id,
    nmo_script_edit_interface_mode_t mode)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_INTERFACE_POLICY, behavior_id, &op));
    op->data.interface_policy.behavior_id = behavior_id;
    op->data.interface_policy.mode = mode;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_data_cell(
    nmo_edit_plan_t *plan,
    nmo_object_id_t dataarray_id,
    uint32_t row,
    uint32_t col,
    const char *value)
{
    nmo_edit_op_t *op = NULL;
    if (value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_SET_DATA_CELL, dataarray_id, &op));
    op->data.data_cell.dataarray_id = dataarray_id;
    op->data.data_cell.row = row;
    op->data.data_cell.col = col;
    op->data.data_cell.value = edit_plan_strdup(value);
    if (op->data.data_cell.value == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_fold(
    nmo_edit_plan_t *plan,
    const nmo_behavior_fold_desc_t *desc)
{
    nmo_edit_op_t *op = NULL;
    if (desc == NULL || desc->parent_id == 0 || desc->node_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_FOLD, desc->parent_id, &op));
    op->data.fold.desc = *desc;
    op->data.fold.desc.name = edit_plan_strdup(desc->name);
    if (desc->name && !op->data.fold.desc.name) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    nmo_status_t rc = edit_plan_dup_object_ids(
        desc->node_ids, desc->node_count, &op->data.fold.node_ids);
    if (rc != NMO_OK) {
        edit_op_dispose(op);
        return rc;
    }
    op->data.fold.desc.node_ids = op->data.fold.node_ids;
    rc = edit_plan_dup_fold_maps(
        desc->input_maps, desc->input_map_count, &op->data.fold.input_maps);
    if (rc != NMO_OK) {
        edit_op_dispose(op);
        return rc;
    }
    op->data.fold.desc.input_maps = op->data.fold.input_maps;
    rc = edit_plan_dup_fold_maps(
        desc->output_maps, desc->output_map_count, &op->data.fold.output_maps);
    if (rc != NMO_OK) {
        edit_op_dispose(op);
        return rc;
    }
    op->data.fold.desc.output_maps = op->data.fold.output_maps;
    rc = edit_plan_dup_fold_maps(
        desc->parameter_maps,
        desc->parameter_map_count,
        &op->data.fold.parameter_maps);
    if (rc != NMO_OK) {
        edit_op_dispose(op);
        return rc;
    }
    op->data.fold.desc.parameter_maps = op->data.fold.parameter_maps;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_replace_bb(
    nmo_edit_plan_t *plan,
    const nmo_behavior_replace_bb_desc_t *desc)
{
    nmo_edit_op_t *op = NULL;
    if (desc == NULL || desc->behavior_id == 0 ||
        nmo_guid_is_null(desc->block_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REPLACE_BB, desc->behavior_id, &op));
    op->data.replace_bb.desc = *desc;
    op->data.replace_bb.desc.name = edit_plan_strdup(desc->name);
    if (desc->name && !op->data.replace_bb.desc.name) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}
