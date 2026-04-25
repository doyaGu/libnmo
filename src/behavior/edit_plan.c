/**
 * @file edit_plan.c
 * @brief Unified edit plan storage and transaction executor.
 */

#include "behavior/nmo_edit_plan.h"

#include "behavior/nmo_script_edit.h"
#include "object/nmo_value_writer.h"

#include <stdlib.h>
#include <string.h>

struct nmo_edit_plan {
    nmo_edit_op_t *ops;
    size_t count;
    size_t capacity;
};

static char *edit_plan_strdup(const char *text)
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

static nmo_status_t edit_plan_dup_object_ids(
    const nmo_object_id_t *ids,
    size_t count,
    nmo_object_id_t **out_ids)
{
    *out_ids = NULL;
    if (count == 0) {
        return NMO_OK;
    }
    if (ids == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_object_id_t *copy =
        (nmo_object_id_t *)malloc(count * sizeof(*copy));
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    memcpy(copy, ids, count * sizeof(*copy));
    *out_ids = copy;
    return NMO_OK;
}

static nmo_status_t edit_plan_dup_fold_maps(
    const nmo_behavior_fold_map_t *maps,
    size_t count,
    nmo_behavior_fold_map_t **out_maps)
{
    *out_maps = NULL;
    if (count == 0) {
        return NMO_OK;
    }
    if (maps == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_behavior_fold_map_t *copy =
        (nmo_behavior_fold_map_t *)calloc(count, sizeof(*copy));
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    for (size_t i = 0; i < count; ++i) {
        copy[i] = maps[i];
        if (maps[i].label != NULL) {
            copy[i].label = edit_plan_strdup(maps[i].label);
            if (copy[i].label == NULL) {
                for (size_t j = 0; j < i; ++j) {
                    free((void *)copy[j].label);
                }
                free(copy);
                return NMO_ERR_NOMEM;
            }
        }
    }
    *out_maps = copy;
    return NMO_OK;
}

static void edit_plan_free_fold_maps(
    nmo_behavior_fold_map_t *maps,
    size_t count)
{
    if (maps == NULL) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free((void *)maps[i].label);
    }
    free(maps);
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

static nmo_status_t edit_plan_append_blank(
    nmo_edit_plan_t *plan,
    nmo_edit_op_kind_t kind,
    nmo_object_id_t primary_id,
    nmo_edit_op_t **out_op)
{
    if (plan == NULL || out_op == NULL || primary_id == 0) {
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

static void edit_op_dispose(nmo_edit_op_t *op)
{
    if (op == NULL) {
        return;
    }
    if (op->kind == NMO_EDIT_OP_SET_PARAMETER_VALUE) {
        free((void *)op->data.set_value.value);
    } else if (op->kind == NMO_EDIT_OP_SET_PARAMETER_BYTES) {
        free((void *)op->data.set_bytes.bytes);
    } else if (op->kind == NMO_EDIT_OP_ADD_NODE) {
        free((void *)op->data.add_node.name);
    } else if (op->kind == NMO_EDIT_OP_ADD_IO) {
        free((void *)op->data.add_io.name);
    } else if (op->kind == NMO_EDIT_OP_RENAME_IO) {
        free((void *)op->data.rename_io.name);
    } else if (op->kind == NMO_EDIT_OP_ADD_PARAMETER) {
        free((void *)op->data.add_parameter.name);
    } else if (op->kind == NMO_EDIT_OP_SET_DATA_CELL) {
        free((void *)op->data.data_cell.value);
    } else if (op->kind == NMO_EDIT_OP_FOLD) {
        free((void *)op->data.fold.desc.name);
        free(op->data.fold.node_ids);
        edit_plan_free_fold_maps(
            op->data.fold.input_maps,
            op->data.fold.desc.input_map_count);
        edit_plan_free_fold_maps(
            op->data.fold.output_maps,
            op->data.fold.desc.output_map_count);
        edit_plan_free_fold_maps(
            op->data.fold.parameter_maps,
            op->data.fold.desc.parameter_map_count);
    } else if (op->kind == NMO_EDIT_OP_REPLACE_BB) {
        free((void *)op->data.replace_bb.desc.name);
    }
    memset(op, 0, sizeof(*op));
}

static nmo_status_t edit_op_copy(
    nmo_edit_op_t *dst,
    const nmo_edit_op_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    switch (src->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        dst->data.set_value.value =
            edit_plan_strdup(src->data.set_value.value);
        if (src->data.set_value.value && !dst->data.set_value.value) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        dst->data.set_bytes.bytes = NULL;
        if (src->data.set_bytes.byte_count > 0) {
            uint8_t *copy =
                (uint8_t *)malloc(src->data.set_bytes.byte_count);
            if (copy == NULL) {
                return NMO_ERR_NOMEM;
            }
            memcpy(copy, src->data.set_bytes.bytes,
                   src->data.set_bytes.byte_count);
            dst->data.set_bytes.bytes = copy;
        }
        break;
    case NMO_EDIT_OP_ADD_NODE:
        dst->data.add_node.name = edit_plan_strdup(src->data.add_node.name);
        if (src->data.add_node.name && !dst->data.add_node.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_ADD_IO:
        dst->data.add_io.name = edit_plan_strdup(src->data.add_io.name);
        if (src->data.add_io.name && !dst->data.add_io.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_RENAME_IO:
        dst->data.rename_io.name = edit_plan_strdup(src->data.rename_io.name);
        if (src->data.rename_io.name && !dst->data.rename_io.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_ADD_PARAMETER:
        dst->data.add_parameter.name =
            edit_plan_strdup(src->data.add_parameter.name);
        if (src->data.add_parameter.name && !dst->data.add_parameter.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_SET_DATA_CELL:
        dst->data.data_cell.value =
            edit_plan_strdup(src->data.data_cell.value);
        if (src->data.data_cell.value && !dst->data.data_cell.value) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_FOLD:
        dst->data.fold.desc = src->data.fold.desc;
        dst->data.fold.desc.name =
            edit_plan_strdup(src->data.fold.desc.name);
        if (src->data.fold.desc.name && !dst->data.fold.desc.name) {
            return NMO_ERR_NOMEM;
        }
        dst->data.fold.node_ids = NULL;
        dst->data.fold.input_maps = NULL;
        dst->data.fold.output_maps = NULL;
        dst->data.fold.parameter_maps = NULL;
        NMO_RETURN_IF_ERROR(edit_plan_dup_object_ids(
            src->data.fold.node_ids,
            src->data.fold.desc.node_count,
            &dst->data.fold.node_ids));
        dst->data.fold.desc.node_ids = dst->data.fold.node_ids;
        NMO_RETURN_IF_ERROR(edit_plan_dup_fold_maps(
            src->data.fold.input_maps,
            src->data.fold.desc.input_map_count,
            &dst->data.fold.input_maps));
        dst->data.fold.desc.input_maps = dst->data.fold.input_maps;
        NMO_RETURN_IF_ERROR(edit_plan_dup_fold_maps(
            src->data.fold.output_maps,
            src->data.fold.desc.output_map_count,
            &dst->data.fold.output_maps));
        dst->data.fold.desc.output_maps = dst->data.fold.output_maps;
        NMO_RETURN_IF_ERROR(edit_plan_dup_fold_maps(
            src->data.fold.parameter_maps,
            src->data.fold.desc.parameter_map_count,
            &dst->data.fold.parameter_maps));
        dst->data.fold.desc.parameter_maps = dst->data.fold.parameter_maps;
        break;
    case NMO_EDIT_OP_REPLACE_BB:
        dst->data.replace_bb.desc = src->data.replace_bb.desc;
        dst->data.replace_bb.desc.name =
            edit_plan_strdup(src->data.replace_bb.desc.name);
        if (src->data.replace_bb.desc.name &&
            !dst->data.replace_bb.desc.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    default:
        break;
    }
    return NMO_OK;
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

nmo_status_t nmo_edit_plan_add_set_parameter_value(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    if (plan == NULL || parameter_id == 0 || value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = NMO_EDIT_OP_SET_PARAMETER_VALUE;
    op->primary_id = parameter_id;
    op->data.set_value.value = edit_plan_strdup(value_str);
    if (op->data.set_value.value == NULL) {
        return NMO_ERR_NOMEM;
    }
    if (options != NULL) {
        op->data.set_value.options = *options;
        op->data.set_value.has_options = true;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_set_parameter_bytes(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parameter_id,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    if (plan == NULL || parameter_id == 0 || (bytes == NULL && byte_count > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = NMO_EDIT_OP_SET_PARAMETER_BYTES;
    op->primary_id = parameter_id;
    if (byte_count > 0) {
        uint8_t *copy = (uint8_t *)malloc(byte_count);
        if (copy == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(copy, bytes, byte_count);
        op->data.set_bytes.bytes = copy;
    }
    op->data.set_bytes.byte_count = byte_count;
    if (options != NULL) {
        op->data.set_bytes.options = *options;
        op->data.set_bytes.has_options = true;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_node(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t bb_guid,
    const char *name)
{
    if (plan == NULL || parent_behavior_id == 0 || nmo_guid_is_null(bb_guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = NMO_EDIT_OP_ADD_NODE;
    op->primary_id = parent_behavior_id;
    op->data.add_node.parent_behavior_id = parent_behavior_id;
    op->data.add_node.bb_guid = bb_guid;
    if (name != NULL) {
        op->data.add_node.name = edit_plan_strdup(name);
        if (op->data.add_node.name == NULL) {
            return NMO_ERR_NOMEM;
        }
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
    nmo_object_id_t to_io_id,
    uint32_t activation_delay)
{
    nmo_edit_op_t *op = NULL;
    if (from_io_id == 0 || to_io_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_BEHAVIOR_LINK, parent_behavior_id, &op));
    op->data.add_link.parent_behavior_id = parent_behavior_id;
    op->data.add_link.from_io_id = from_io_id;
    op->data.add_link.to_io_id = to_io_id;
    op->data.add_link.activation_delay = activation_delay;
    plan->count++;
    return NMO_OK;
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
    nmo_object_id_t target_parameter_id)
{
    nmo_edit_op_t *op = NULL;
    if (target_parameter_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_CONNECT_PARAMETER, target_parameter_id, &op));
    op->data.connect_parameter.source_parameter_id = source_parameter_id;
    op->data.connect_parameter.target_parameter_id = target_parameter_id;
    plan->count++;
    return NMO_OK;
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
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
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
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_rewire_operation(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    nmo_edit_op_t *op = NULL;
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REWIRE_OPERATION, operation_id, &op));
    op->data.rewire_operation.operation_id = operation_id;
    op->data.rewire_operation.slot_flags = slot_flags;
    op->data.rewire_operation.in1_parameter_id = in1_parameter_id;
    op->data.rewire_operation.in2_parameter_id = in2_parameter_id;
    op->data.rewire_operation.out_parameter_id = out_parameter_id;
    plan->count++;
    return NMO_OK;
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

nmo_edit_executor_options_t nmo_edit_executor_options_default(void)
{
    nmo_edit_executor_options_t options = {0};
    options.validation_flags =
        NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY |
        NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
        NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX |
        NMO_SCRIPT_EDIT_VALIDATE_INTERFACE;
    return options;
}

nmo_status_t nmo_edit_report_init(nmo_edit_report_t *report)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    return NMO_OK;
}

void nmo_edit_report_dispose(nmo_edit_report_t *report)
{
    if (report == NULL) {
        return;
    }
    for (size_t i = 0; i < report->operation_count; ++i) {
        if (report->operations != NULL) {
            for (size_t j = 0; j < report->operations[i].handle_count; ++j) {
                free((void *)report->operations[i].handles[j].name);
            }
            free(report->operations[i].handles);
        }
    }
    free(report->operations);
    free(report->changed_objects);
    free(report->created_objects);
    free(report->deleted_objects);
    memset(report, 0, sizeof(*report));
}

static nmo_status_t edit_report_ensure_operations(
    nmo_edit_report_t *report,
    size_t needed)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (needed <= report->operation_count) {
        return NMO_OK;
    }
    nmo_edit_operation_result_t *ops =
        (nmo_edit_operation_result_t *)realloc(
            report->operations, needed * sizeof(*ops));
    if (ops == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(ops + report->operation_count, 0,
           (needed - report->operation_count) * sizeof(*ops));
    report->operations = ops;
    report->operation_count = needed;
    return NMO_OK;
}

static nmo_status_t edit_report_prepare(nmo_edit_report_t *report,
                                        const nmo_edit_plan_t *plan,
                                        bool dry_run)
{
    if (report == NULL || plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_edit_report_dispose(report);
    report->dry_run = dry_run;
    report->operation_count = plan->count;
    if (plan->count == 0) {
        return NMO_OK;
    }
    report->operations =
        (nmo_edit_operation_result_t *)calloc(plan->count, sizeof(*report->operations));
    if (report->operations == NULL) {
        nmo_edit_report_dispose(report);
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

static bool edit_report_has_changed_object(
    const nmo_edit_report_t *report,
    nmo_object_id_t id)
{
    for (size_t i = 0; i < report->changed_object_count; i++) {
        if (report->changed_objects[i].id == id) {
            return true;
        }
    }
    return false;
}

static nmo_status_t edit_report_add_impact(
    nmo_edit_object_impact_t **items,
    size_t *count,
    size_t *capacity,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (items == NULL || count == NULL || capacity == NULL || id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (*count == *capacity) {
        size_t next_capacity = *capacity == 0u ? 8u : *capacity * 2u;
        nmo_edit_object_impact_t *next =
            (nmo_edit_object_impact_t *)realloc(
                *items, next_capacity * sizeof(*next));
        if (next == NULL) {
            return NMO_ERR_NOMEM;
        }
        *items = next;
        *capacity = next_capacity;
    }
    (*items)[*count] = (nmo_edit_object_impact_t){
        .id = id,
        .cause = cause,
        .role = role,
    };
    ++(*count);
    return NMO_OK;
}

nmo_status_t nmo_edit_report_add_changed_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (report == NULL || id == 0 ||
        edit_report_has_changed_object(report, id)) {
        return report && id ? NMO_OK : NMO_ERR_INVALID_ARGUMENT;
    }
    size_t capacity = report->changed_object_count;
    if (report->changed_objects != NULL) {
        capacity = report->changed_object_count;
        while (capacity > 0 &&
               report->changed_objects[capacity - 1].id == 0) {
            capacity--;
        }
    }
    capacity = report->changed_object_count;
    return edit_report_add_impact(
        &report->changed_objects,
        &report->changed_object_count,
        &capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_created_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    return edit_report_add_impact(
        &report->created_objects,
        &report->created_object_count,
        &report->created_object_capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_deleted_object(
    nmo_edit_report_t *report,
    nmo_object_id_t id,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    return edit_report_add_impact(
        &report->deleted_objects,
        &report->deleted_object_count,
        &report->deleted_object_capacity,
        id,
        cause,
        role);
}

nmo_status_t nmo_edit_report_add_operation_handle(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *name,
    nmo_object_id_t id)
{
    if (report == NULL || id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_report_ensure_operations(
        report, operation_index + 1u));
    nmo_edit_operation_result_t *op = &report->operations[operation_index];
    nmo_edit_operation_handle_t *next =
        (nmo_edit_operation_handle_t *)realloc(
            op->handles,
            (op->handle_count + 1u) * sizeof(*next));
    if (next == NULL) {
        return NMO_ERR_NOMEM;
    }
    op->handles = next;
    const char *name_copy = edit_plan_strdup(name);
    if (name && !name_copy) {
        return NMO_ERR_NOMEM;
    }
    op->handles[op->handle_count++] =
        (nmo_edit_operation_handle_t){.name = name_copy, .id = id};
    return NMO_OK;
}

static nmo_status_t edit_report_note_created_objects(
    nmo_edit_report_t *report,
    const nmo_object_id_t *ids,
    size_t count,
    nmo_edit_op_kind_t cause,
    const char *role)
{
    if (report == NULL || ids == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_created_object(
            report, ids[i], cause, role));
    }
    return NMO_OK;
}

static nmo_status_t edit_executor_apply_op(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_op_t *op,
    nmo_object_id_t *out_result_id)
{
    nmo_workspace_edit_t *edit = NULL;
    if (tx == NULL || op == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (out_result_id != NULL) {
        *out_result_id = 0;
    }
    edit = nmo_script_edit_workspace_edit(tx);
    if (edit == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        return nmo_value_writer_set_parameter_value(
            edit,
            op->primary_id,
            op->data.set_value.value,
            op->data.set_value.has_options ? &op->data.set_value.options : NULL);
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        return nmo_value_writer_set_parameter_bytes(
            edit,
            op->primary_id,
            op->data.set_bytes.bytes,
            op->data.set_bytes.byte_count,
            op->data.set_bytes.has_options ? &op->data.set_bytes.options : NULL);
    case NMO_EDIT_OP_ADD_NODE:
        return nmo_script_edit_add_node(
            tx,
            op->data.add_node.parent_behavior_id,
            op->data.add_node.bb_guid,
            op->data.add_node.name,
            out_result_id);
    case NMO_EDIT_OP_REMOVE_NODE:
        return nmo_script_edit_remove_node(
            tx,
            op->data.remove_node.parent_behavior_id,
            op->data.remove_node.node_id,
            op->data.remove_node.delete_flags);
    case NMO_EDIT_OP_ADD_IO:
        return nmo_script_edit_add_io(
            tx,
            op->data.add_io.behavior_id,
            op->data.add_io.kind,
            op->data.add_io.name,
            out_result_id);
    case NMO_EDIT_OP_RENAME_IO:
        return nmo_script_edit_rename_io(
            tx,
            op->data.rename_io.io_id,
            op->data.rename_io.name);
    case NMO_EDIT_OP_REMOVE_IO:
        return nmo_script_edit_remove_io(
            tx,
            op->data.remove_io.io_id,
            op->data.remove_io.detach_links);
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        return nmo_script_edit_add_behavior_link(
            tx,
            op->data.add_link.parent_behavior_id,
            op->data.add_link.from_io_id,
            op->data.add_link.to_io_id,
            op->data.add_link.activation_delay,
            out_result_id);
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK:
        return nmo_script_edit_rewire_behavior_link(
            tx,
            op->data.rewire_link.link_id,
            op->data.rewire_link.from_io_id,
            op->data.rewire_link.to_io_id);
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY:
        return nmo_script_edit_set_behavior_link_delay(
            tx,
            op->data.set_link_delay.link_id,
            op->data.set_link_delay.activation_delay);
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return nmo_script_edit_remove_behavior_link(
            tx,
            op->data.remove_link.parent_behavior_id,
            op->data.remove_link.link_id);
    case NMO_EDIT_OP_ADD_PARAMETER:
        return nmo_script_edit_add_parameter(
            tx,
            op->data.add_parameter.owner_behavior_id,
            op->data.add_parameter.kind,
            op->data.add_parameter.type_guid,
            op->data.add_parameter.name,
            out_result_id);
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        return nmo_script_edit_connect_parameter(
            tx,
            op->data.connect_parameter.source_parameter_id,
            op->data.connect_parameter.target_parameter_id);
    case NMO_EDIT_OP_DISCONNECT_PARAMETER:
        return nmo_script_edit_disconnect_parameter(
            tx,
            op->data.disconnect_parameter.target_parameter_id);
    case NMO_EDIT_OP_REMOVE_PARAMETER:
        return nmo_script_edit_remove_parameter(
            tx,
            op->data.remove_parameter.parameter_id,
            op->data.remove_parameter.detach);
    case NMO_EDIT_OP_ADD_OPERATION:
        return nmo_script_edit_add_operation(
            tx,
            op->data.add_operation.parent_behavior_id,
            op->data.add_operation.operation_guid,
            op->data.add_operation.in1_parameter_id,
            op->data.add_operation.in2_parameter_id,
            op->data.add_operation.out_parameter_id,
            out_result_id);
    case NMO_EDIT_OP_REWIRE_OPERATION:
        return nmo_script_edit_rewire_operation(
            tx,
            op->data.rewire_operation.operation_id,
            op->data.rewire_operation.slot_flags,
            op->data.rewire_operation.in1_parameter_id,
            op->data.rewire_operation.in2_parameter_id,
            op->data.rewire_operation.out_parameter_id);
    case NMO_EDIT_OP_REMOVE_OPERATION:
        return nmo_script_edit_remove_operation(
            tx,
            op->data.remove_operation.operation_id);
    case NMO_EDIT_OP_INTERFACE_POLICY:
        return nmo_script_edit_apply_interface_policy(
            tx,
            op->data.interface_policy.behavior_id,
            op->data.interface_policy.mode);
    case NMO_EDIT_OP_SET_DATA_CELL:
        return nmo_object_edit_set_dataarray_cell(
            edit,
            op->data.data_cell.dataarray_id,
            op->data.data_cell.row,
            op->data.data_cell.col,
            op->data.data_cell.value);
    case NMO_EDIT_OP_REPLACE_BB: {
        nmo_behavior_replace_report_t replace_report = {0};
        nmo_status_t rc = nmo_behavior_edit_replace_bb_in_edit(
            nmo_script_edit_workspace(tx),
            edit,
            &op->data.replace_bb.desc,
            &replace_report);
        if (rc == NMO_OK && out_result_id != NULL) {
            *out_result_id = op->data.replace_bb.desc.behavior_id;
        }
        return rc;
    }
    default:
        return NMO_ERR_NOT_SUPPORTED;
    }
}

static const char *edit_op_result_handle_name(nmo_edit_op_kind_t kind)
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
        return "object";
    }
}

static bool edit_op_creates_result(nmo_edit_op_kind_t kind)
{
    return kind == NMO_EDIT_OP_ADD_NODE ||
           kind == NMO_EDIT_OP_ADD_IO ||
           kind == NMO_EDIT_OP_ADD_BEHAVIOR_LINK ||
           kind == NMO_EDIT_OP_ADD_PARAMETER ||
           kind == NMO_EDIT_OP_ADD_OPERATION;
}

static nmo_object_id_t edit_op_deleted_id(const nmo_edit_op_t *op)
{
    switch (op->kind) {
    case NMO_EDIT_OP_REMOVE_NODE:
        return op->data.remove_node.node_id;
    case NMO_EDIT_OP_REMOVE_IO:
        return op->data.remove_io.io_id;
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return op->data.remove_link.link_id;
    case NMO_EDIT_OP_REMOVE_PARAMETER:
        return op->data.remove_parameter.parameter_id;
    case NMO_EDIT_OP_REMOVE_OPERATION:
        return op->data.remove_operation.operation_id;
    default:
        return 0;
    }
}

static nmo_object_id_t edit_op_changed_id(const nmo_edit_op_t *op)
{
    switch (op->kind) {
    case NMO_EDIT_OP_ADD_NODE:
        return op->data.add_node.parent_behavior_id;
    case NMO_EDIT_OP_REMOVE_NODE:
        return op->data.remove_node.parent_behavior_id;
    case NMO_EDIT_OP_ADD_IO:
        return op->data.add_io.behavior_id;
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        return op->data.add_link.parent_behavior_id;
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
        return op->data.remove_link.parent_behavior_id;
    case NMO_EDIT_OP_ADD_PARAMETER:
        return op->data.add_parameter.owner_behavior_id;
    case NMO_EDIT_OP_ADD_OPERATION:
        return op->data.add_operation.parent_behavior_id;
    case NMO_EDIT_OP_INTERFACE_POLICY:
        return op->data.interface_policy.behavior_id;
    default:
        return op->primary_id;
    }
}

static nmo_status_t edit_executor_validate(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    uint32_t validation_flags)
{
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY) != 0u) {
        report->validation.roundtrip_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY);
        if (report->validation.roundtrip_status != NMO_OK) {
            report->validation.final_status = report->validation.roundtrip_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_REFERENCES) != 0u) {
        report->validation.reference_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES);
        if (report->validation.reference_status != NMO_OK) {
            report->validation.final_status = report->validation.reference_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX) != 0u) {
        report->validation.behavior_index_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX);
        if (report->validation.behavior_index_status != NMO_OK) {
            report->validation.final_status =
                report->validation.behavior_index_status;
            return report->validation.final_status;
        }
    }
    if ((validation_flags & NMO_SCRIPT_EDIT_VALIDATE_INTERFACE) != 0u) {
        report->validation.interface_status =
            nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_INTERFACE);
        if (report->validation.interface_status != NMO_OK) {
            report->validation.final_status = report->validation.interface_status;
            return report->validation.final_status;
        }
    }
    report->validation.final_status = NMO_OK;
    return NMO_OK;
}

nmo_status_t nmo_edit_executor_execute(
    nmo_workspace_t *workspace,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report)
{
    if (workspace == NULL || plan == NULL || report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = nmo_script_edit_begin(workspace, "edit plan", &tx);
    if (rc != NMO_OK) {
        report->status = rc;
        return rc;
    }

    nmo_edit_executor_options_t effective =
        options != NULL ? *options : nmo_edit_executor_options_default();
    rc = nmo_edit_executor_execute_transaction(tx, plan, &effective, report);
    if (rc != NMO_OK) {
        nmo_script_edit_rollback(tx);
        return rc;
    }

    if (effective.dry_run) {
        nmo_script_edit_rollback(tx);
        report->ok = true;
        report->status = NMO_OK;
        return NMO_OK;
    }

    rc = nmo_script_edit_commit(tx);
    report->ok = rc == NMO_OK;
    report->status = rc;
    return rc;
}

nmo_status_t nmo_edit_executor_execute_transaction(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_plan_t *plan,
    const nmo_edit_executor_options_t *options,
    nmo_edit_report_t *report)
{
    if (tx == NULL || plan == NULL || report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_edit_executor_options_t effective =
        options != NULL ? *options : nmo_edit_executor_options_default();
    nmo_status_t rc = NMO_OK;
    NMO_RETURN_IF_ERROR(edit_report_prepare(report, plan, effective.dry_run));

    for (size_t i = 0; i < plan->count; i++) {
        const nmo_edit_op_t *op = &plan->ops[i];
        const nmo_script_edit_report_t *tx_report_before =
            nmo_script_edit_report(tx);
        size_t created_start = tx_report_before
            ? tx_report_before->created_object_id_count
            : 0u;
        nmo_object_id_t result_id = 0;
        nmo_status_t op_rc = edit_executor_apply_op(tx, op, &result_id);
        const nmo_script_edit_report_t *tx_report_after =
            nmo_script_edit_report(tx);
        report->operations[i] = (nmo_edit_operation_result_t){
            .kind = op->kind,
            .primary_id = op->primary_id,
            .result_id = result_id,
            .status = op_rc,
        };
        if (op_rc != NMO_OK) {
            report->ok = false;
            report->status = op_rc;
            return op_rc;
        }
        if (result_id != 0u && edit_op_creates_result(op->kind)) {
            nmo_status_t handle_rc = nmo_edit_report_add_operation_handle(
                report,
                i,
                edit_op_result_handle_name(op->kind),
                result_id);
            if (handle_rc != NMO_OK) {
                report->ok = false;
                report->status = handle_rc;
                return handle_rc;
            }
        }
        if (edit_op_creates_result(op->kind)) {
            nmo_status_t report_rc = NMO_OK;
            if (tx_report_after &&
                tx_report_after->created_object_ids &&
                tx_report_after->created_object_id_count > created_start) {
                report_rc = edit_report_note_created_objects(
                    report,
                    tx_report_after->created_object_ids + created_start,
                    tx_report_after->created_object_id_count - created_start,
                    op->kind,
                    "created");
            } else {
                report_rc = nmo_edit_report_add_created_object(
                    report, result_id, op->kind, "created");
            }
            if (report_rc != NMO_OK) {
                report->ok = false;
                report->status = report_rc;
                return report_rc;
            }
        } else {
            nmo_object_id_t deleted_id = edit_op_deleted_id(op);
            if (deleted_id != 0u) {
                (void)nmo_edit_report_add_deleted_object(
                    report, deleted_id, op->kind, "primary");
            }
        }
        (void)nmo_edit_report_add_changed_object(
            report, edit_op_changed_id(op), op->kind, "primary");
    }

    rc = edit_executor_validate(tx, report, effective.validation_flags);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
    }

    report->ok = true;
    report->status = NMO_OK;
    return NMO_OK;
}
