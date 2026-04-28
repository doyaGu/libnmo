/**
 * @file edit_plan.c
 * @brief Unified edit plan storage and transaction executor.
 */

#include "behavior/nmo_edit_plan.h"

#include "behavior/nmo_semantic_validator.h"
#include "behavior/nmo_script_edit.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_value_writer.h"

#include "../runtime/runtime_internal.h"

#include <stdio.h>
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
        free((void *)op->data.set_value.parameter_ref_handle);
    } else if (op->kind == NMO_EDIT_OP_SET_PARAMETER_BYTES) {
        free((void *)op->data.set_bytes.bytes);
        free((void *)op->data.set_bytes.parameter_ref_handle);
    } else if (op->kind == NMO_EDIT_OP_ADD_NODE) {
        free((void *)op->data.add_node.name);
    } else if (op->kind == NMO_EDIT_OP_ADD_IO) {
        free((void *)op->data.add_io.name);
    } else if (op->kind == NMO_EDIT_OP_RENAME_IO) {
        free((void *)op->data.rename_io.name);
    } else if (op->kind == NMO_EDIT_OP_ADD_BEHAVIOR_LINK) {
        free((void *)op->data.add_link.from_io_ref_handle);
        free((void *)op->data.add_link.to_io_ref_handle);
    } else if (op->kind == NMO_EDIT_OP_ADD_PARAMETER) {
        free((void *)op->data.add_parameter.name);
    } else if (op->kind == NMO_EDIT_OP_CONNECT_PARAMETER) {
        free((void *)op->data.connect_parameter.target_parameter_ref_handle);
    } else if (op->kind == NMO_EDIT_OP_ADD_OPERATION) {
        free((void *)op->data.add_operation.in1_parameter_ref_handle);
        free((void *)op->data.add_operation.in2_parameter_ref_handle);
        free((void *)op->data.add_operation.out_parameter_ref_handle);
    } else if (op->kind == NMO_EDIT_OP_REWIRE_OPERATION) {
        free((void *)op->data.rewire_operation.in1_parameter_ref_handle);
        free((void *)op->data.rewire_operation.in2_parameter_ref_handle);
        free((void *)op->data.rewire_operation.out_parameter_ref_handle);
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
        dst->data.set_value.parameter_ref_handle =
            edit_plan_strdup(src->data.set_value.parameter_ref_handle);
        if (src->data.set_value.parameter_ref_handle &&
            !dst->data.set_value.parameter_ref_handle) {
            edit_op_dispose(dst);
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
        dst->data.set_bytes.parameter_ref_handle =
            edit_plan_strdup(src->data.set_bytes.parameter_ref_handle);
        if (src->data.set_bytes.parameter_ref_handle &&
            !dst->data.set_bytes.parameter_ref_handle) {
            edit_op_dispose(dst);
            return NMO_ERR_NOMEM;
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
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        dst->data.add_link.from_io_ref_handle =
            edit_plan_strdup(src->data.add_link.from_io_ref_handle);
        if (src->data.add_link.from_io_ref_handle &&
            !dst->data.add_link.from_io_ref_handle) {
            edit_op_dispose(dst);
            return NMO_ERR_NOMEM;
        }
        dst->data.add_link.to_io_ref_handle =
            edit_plan_strdup(src->data.add_link.to_io_ref_handle);
        if (src->data.add_link.to_io_ref_handle &&
            !dst->data.add_link.to_io_ref_handle) {
            edit_op_dispose(dst);
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
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        dst->data.connect_parameter.target_parameter_ref_handle =
            edit_plan_strdup(
                src->data.connect_parameter.target_parameter_ref_handle);
        if (src->data.connect_parameter.target_parameter_ref_handle &&
            !dst->data.connect_parameter.target_parameter_ref_handle) {
            edit_op_dispose(dst);
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_ADD_OPERATION:
        dst->data.add_operation.in1_parameter_ref_handle =
            edit_plan_strdup(src->data.add_operation.in1_parameter_ref_handle);
        if (src->data.add_operation.in1_parameter_ref_handle &&
            !dst->data.add_operation.in1_parameter_ref_handle) {
            edit_op_dispose(dst);
            return NMO_ERR_NOMEM;
        }
        dst->data.add_operation.in2_parameter_ref_handle =
            edit_plan_strdup(src->data.add_operation.in2_parameter_ref_handle);
        if (src->data.add_operation.in2_parameter_ref_handle &&
            !dst->data.add_operation.in2_parameter_ref_handle) {
            edit_op_dispose(dst);
            return NMO_ERR_NOMEM;
        }
        dst->data.add_operation.out_parameter_ref_handle =
            edit_plan_strdup(src->data.add_operation.out_parameter_ref_handle);
        if (src->data.add_operation.out_parameter_ref_handle &&
            !dst->data.add_operation.out_parameter_ref_handle) {
            edit_op_dispose(dst);
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_REWIRE_OPERATION:
        dst->data.rewire_operation.in1_parameter_ref_handle =
            edit_plan_strdup(src->data.rewire_operation.in1_parameter_ref_handle);
        if (src->data.rewire_operation.in1_parameter_ref_handle &&
            !dst->data.rewire_operation.in1_parameter_ref_handle) {
            edit_op_dispose(dst);
            return NMO_ERR_NOMEM;
        }
        dst->data.rewire_operation.in2_parameter_ref_handle =
            edit_plan_strdup(src->data.rewire_operation.in2_parameter_ref_handle);
        if (src->data.rewire_operation.in2_parameter_ref_handle &&
            !dst->data.rewire_operation.in2_parameter_ref_handle) {
            edit_op_dispose(dst);
            return NMO_ERR_NOMEM;
        }
        dst->data.rewire_operation.out_parameter_ref_handle =
            edit_plan_strdup(src->data.rewire_operation.out_parameter_ref_handle);
        if (src->data.rewire_operation.out_parameter_ref_handle &&
            !dst->data.rewire_operation.out_parameter_ref_handle) {
            edit_op_dispose(dst);
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

nmo_status_t nmo_edit_plan_add_set_parameter_value_from_handle(
    nmo_edit_plan_t *plan,
    size_t operation_index,
    const char *handle_name,
    const char *value_str,
    const nmo_parameter_write_options_t *options)
{
    if (plan == NULL || handle_name == NULL || handle_name[0] == '\0' ||
        value_str == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = NMO_EDIT_OP_SET_PARAMETER_VALUE;
    op->primary_id = 0u;
    op->data.set_value.value = edit_plan_strdup(value_str);
    op->data.set_value.parameter_ref_handle = edit_plan_strdup(handle_name);
    if (op->data.set_value.value == NULL ||
        op->data.set_value.parameter_ref_handle == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    op->data.set_value.parameter_ref_operation_index = operation_index;
    op->data.set_value.has_parameter_ref = true;
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

nmo_status_t nmo_edit_plan_add_set_parameter_bytes_from_handle(
    nmo_edit_plan_t *plan,
    size_t operation_index,
    const char *handle_name,
    const uint8_t *bytes,
    size_t byte_count,
    const nmo_parameter_write_options_t *options)
{
    if (plan == NULL || handle_name == NULL || handle_name[0] == '\0' ||
        (bytes == NULL && byte_count > 0)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_reserve(plan, plan->count + 1u));
    nmo_edit_op_t *op = &plan->ops[plan->count];
    memset(op, 0, sizeof(*op));
    op->kind = NMO_EDIT_OP_SET_PARAMETER_BYTES;
    op->primary_id = 0u;
    if (byte_count > 0) {
        uint8_t *copy = (uint8_t *)malloc(byte_count);
        if (copy == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(copy, bytes, byte_count);
        op->data.set_bytes.bytes = copy;
    }
    op->data.set_bytes.byte_count = byte_count;
    op->data.set_bytes.parameter_ref_handle = edit_plan_strdup(handle_name);
    if (op->data.set_bytes.parameter_ref_handle == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    op->data.set_bytes.parameter_ref_operation_index = operation_index;
    op->data.set_bytes.has_parameter_ref = true;
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

nmo_status_t nmo_edit_plan_add_behavior_link_from_handles(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    size_t from_operation_index,
    const char *from_handle_name,
    size_t to_operation_index,
    const char *to_handle_name,
    uint32_t activation_delay)
{
    nmo_edit_op_t *op = NULL;
    if (parent_behavior_id == 0u || from_handle_name == NULL ||
        from_handle_name[0] == '\0' || to_handle_name == NULL ||
        to_handle_name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_BEHAVIOR_LINK, parent_behavior_id, &op));
    op->data.add_link.parent_behavior_id = parent_behavior_id;
    op->data.add_link.activation_delay = activation_delay;
    op->data.add_link.from_io_ref_operation_index = from_operation_index;
    op->data.add_link.from_io_ref_handle = edit_plan_strdup(from_handle_name);
    op->data.add_link.has_from_io_ref = true;
    op->data.add_link.to_io_ref_operation_index = to_operation_index;
    op->data.add_link.to_io_ref_handle = edit_plan_strdup(to_handle_name);
    op->data.add_link.has_to_io_ref = true;
    if (op->data.add_link.from_io_ref_handle == NULL ||
        op->data.add_link.to_io_ref_handle == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_behavior_link_to_handle(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t from_io_id,
    size_t to_operation_index,
    const char *to_handle_name,
    uint32_t activation_delay)
{
    nmo_edit_op_t *op = NULL;
    if (parent_behavior_id == 0u || from_io_id == 0u ||
        to_handle_name == NULL || to_handle_name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_BEHAVIOR_LINK, parent_behavior_id, &op));
    op->data.add_link.parent_behavior_id = parent_behavior_id;
    op->data.add_link.from_io_id = from_io_id;
    op->data.add_link.activation_delay = activation_delay;
    op->data.add_link.to_io_ref_operation_index = to_operation_index;
    op->data.add_link.to_io_ref_handle = edit_plan_strdup(to_handle_name);
    op->data.add_link.has_to_io_ref = true;
    if (op->data.add_link.to_io_ref_handle == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_behavior_link_from_handle(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    size_t from_operation_index,
    const char *from_handle_name,
    nmo_object_id_t to_io_id,
    uint32_t activation_delay)
{
    nmo_edit_op_t *op = NULL;
    if (parent_behavior_id == 0u || to_io_id == 0u ||
        from_handle_name == NULL || from_handle_name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_ADD_BEHAVIOR_LINK, parent_behavior_id, &op));
    op->data.add_link.parent_behavior_id = parent_behavior_id;
    op->data.add_link.to_io_id = to_io_id;
    op->data.add_link.activation_delay = activation_delay;
    op->data.add_link.from_io_ref_operation_index = from_operation_index;
    op->data.add_link.from_io_ref_handle = edit_plan_strdup(from_handle_name);
    op->data.add_link.has_from_io_ref = true;
    if (op->data.add_link.from_io_ref_handle == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
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
    if (source_parameter_id == 0 || target_parameter_id == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_CONNECT_PARAMETER, target_parameter_id, &op));
    op->data.connect_parameter.source_parameter_id = source_parameter_id;
    op->data.connect_parameter.target_parameter_id = target_parameter_id;
    plan->count++;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_add_connect_parameter_to_handle(
    nmo_edit_plan_t *plan,
    nmo_object_id_t source_parameter_id,
    size_t target_operation_index,
    const char *target_handle_name)
{
    nmo_edit_op_t *op = NULL;
    if (source_parameter_id == 0 || target_handle_name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_CONNECT_PARAMETER, source_parameter_id, &op));
    op->data.connect_parameter.source_parameter_id = source_parameter_id;
    op->data.connect_parameter.target_parameter_ref_operation_index =
        target_operation_index;
    op->data.connect_parameter.target_parameter_ref_handle =
        edit_plan_strdup(target_handle_name);
    if (op->data.connect_parameter.target_parameter_ref_handle == NULL) {
        edit_op_dispose(op);
        return NMO_ERR_NOMEM;
    }
    op->data.connect_parameter.has_target_parameter_ref = true;
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

nmo_status_t nmo_edit_plan_add_operation_with_refs(
    nmo_edit_plan_t *plan,
    nmo_object_id_t parent_behavior_id,
    nmo_guid_t operation_guid,
    nmo_object_id_t in1_parameter_id,
    size_t in1_operation_index,
    const char *in1_handle_name,
    nmo_object_id_t in2_parameter_id,
    size_t in2_operation_index,
    const char *in2_handle_name,
    nmo_object_id_t out_parameter_id,
    size_t out_operation_index,
    const char *out_handle_name)
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
    if (in1_handle_name != NULL) {
        op->data.add_operation.has_in1_parameter_ref = true;
        op->data.add_operation.in1_parameter_ref_operation_index =
            in1_operation_index;
        op->data.add_operation.in1_parameter_ref_handle =
            edit_plan_strdup(in1_handle_name);
        if (op->data.add_operation.in1_parameter_ref_handle == NULL) {
            edit_op_dispose(op);
            return NMO_ERR_NOMEM;
        }
    }
    if (in2_handle_name != NULL) {
        op->data.add_operation.has_in2_parameter_ref = true;
        op->data.add_operation.in2_parameter_ref_operation_index =
            in2_operation_index;
        op->data.add_operation.in2_parameter_ref_handle =
            edit_plan_strdup(in2_handle_name);
        if (op->data.add_operation.in2_parameter_ref_handle == NULL) {
            edit_op_dispose(op);
            return NMO_ERR_NOMEM;
        }
    }
    if (out_handle_name != NULL) {
        op->data.add_operation.has_out_parameter_ref = true;
        op->data.add_operation.out_parameter_ref_operation_index =
            out_operation_index;
        op->data.add_operation.out_parameter_ref_handle =
            edit_plan_strdup(out_handle_name);
        if (op->data.add_operation.out_parameter_ref_handle == NULL) {
            edit_op_dispose(op);
            return NMO_ERR_NOMEM;
        }
    }
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

nmo_status_t nmo_edit_plan_add_rewire_operation_with_refs(
    nmo_edit_plan_t *plan,
    nmo_object_id_t operation_id,
    uint32_t slot_flags,
    nmo_object_id_t in1_parameter_id,
    size_t in1_operation_index,
    const char *in1_handle_name,
    nmo_object_id_t in2_parameter_id,
    size_t in2_operation_index,
    const char *in2_handle_name,
    nmo_object_id_t out_parameter_id,
    size_t out_operation_index,
    const char *out_handle_name)
{
    nmo_edit_op_t *op = NULL;
    if (!plan || operation_id == 0u || slot_flags == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(edit_plan_append_blank(
        plan, NMO_EDIT_OP_REWIRE_OPERATION, operation_id, &op));
    op->data.rewire_operation.operation_id = operation_id;
    op->data.rewire_operation.slot_flags = slot_flags;
    op->data.rewire_operation.in1_parameter_id = in1_parameter_id;
    op->data.rewire_operation.in2_parameter_id = in2_parameter_id;
    op->data.rewire_operation.out_parameter_id = out_parameter_id;
    if (in1_handle_name != NULL) {
        op->data.rewire_operation.has_in1_parameter_ref = true;
        op->data.rewire_operation.in1_parameter_ref_operation_index =
            in1_operation_index;
        op->data.rewire_operation.in1_parameter_ref_handle =
            edit_plan_strdup(in1_handle_name);
        if (op->data.rewire_operation.in1_parameter_ref_handle == NULL) {
            edit_op_dispose(op);
            return NMO_ERR_NOMEM;
        }
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN1;
    }
    if (in2_handle_name != NULL) {
        op->data.rewire_operation.has_in2_parameter_ref = true;
        op->data.rewire_operation.in2_parameter_ref_operation_index =
            in2_operation_index;
        op->data.rewire_operation.in2_parameter_ref_handle =
            edit_plan_strdup(in2_handle_name);
        if (op->data.rewire_operation.in2_parameter_ref_handle == NULL) {
            edit_op_dispose(op);
            return NMO_ERR_NOMEM;
        }
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN2;
    }
    if (out_handle_name != NULL) {
        op->data.rewire_operation.has_out_parameter_ref = true;
        op->data.rewire_operation.out_parameter_ref_operation_index =
            out_operation_index;
        op->data.rewire_operation.out_parameter_ref_handle =
            edit_plan_strdup(out_handle_name);
        if (op->data.rewire_operation.out_parameter_ref_handle == NULL) {
            edit_op_dispose(op);
            return NMO_ERR_NOMEM;
        }
        op->data.rewire_operation.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_OUT;
    }
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
            free((void *)report->operations[i].diagnostic_code);
            free((void *)report->operations[i].diagnostic_message);
            free(report->operations[i].handles);
        }
    }
    free(report->operations);
    free(report->changed_objects);
    free(report->created_objects);
    free(report->deleted_objects);
    free(report->semantic_risks);
    free(report->output_path);
    memset(report, 0, sizeof(*report));
}

nmo_status_t nmo_edit_report_set_output_path(
    nmo_edit_report_t *report,
    const char *output_path)
{
    if (report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *copy = edit_plan_strdup(output_path);
    if (output_path != NULL && copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    free(report->output_path);
    report->output_path = copy;
    return NMO_OK;
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

static nmo_status_t edit_report_set_operation_diagnostic(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *code,
    const char *message)
{
    if (report == NULL || (code == NULL && message == NULL)) {
        return NMO_OK;
    }
    NMO_RETURN_IF_ERROR(edit_report_ensure_operations(
        report, operation_index + 1u));
    nmo_edit_operation_result_t *op = &report->operations[operation_index];
    char *code_copy = edit_plan_strdup(code);
    if (code && !code_copy) {
        return NMO_ERR_NOMEM;
    }
    char *message_copy = edit_plan_strdup(message);
    if (message && !message_copy) {
        free(code_copy);
        return NMO_ERR_NOMEM;
    }
    free((void *)op->diagnostic_code);
    free((void *)op->diagnostic_message);
    op->diagnostic_code = code_copy;
    op->diagnostic_message = message_copy;
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

static nmo_status_t edit_report_note_operation_slot_parameters(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t in1_parameter_id,
    nmo_object_id_t in2_parameter_id,
    nmo_object_id_t out_parameter_id)
{
    if (report == NULL) {
        return NMO_OK;
    }
    if (in1_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            in1_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    if (in2_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            in2_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    if (out_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            out_parameter_id,
            cause,
            "operation_slot_parameter"));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_note_control_link_endpoints(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t from_io_id,
    nmo_object_id_t to_io_id)
{
    if (report == NULL) {
        return NMO_OK;
    }
    if (from_io_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            from_io_id,
            cause,
            "control_link_endpoint"));
    }
    if (to_io_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            to_io_id,
            cause,
            "control_link_endpoint"));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_note_io_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t io_id)
{
    if (tx == NULL || report == NULL || io_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    if (repo == NULL) {
        return NMO_OK;
    }

    const size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0u; i < object_count; ++i) {
        nmo_object_t *behavior_obj = nmo_object_repository_get_by_index(repo, i);
        nmo_behavior_state_t *behavior_state = behavior_obj &&
                nmo_object_get_class_id(behavior_obj) == NMO_CID_BEHAVIOR
            ? (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj)
            : NULL;
        const nmo_object_id_t *link_ids = behavior_state &&
                behavior_state->sub_behavior_links.data
            ? (const nmo_object_id_t *)behavior_state->sub_behavior_links.data
            : NULL;
        for (size_t j = 0u; link_ids != NULL &&
                            j < behavior_state->sub_behavior_links.count; ++j) {
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_ids[j]);
            const nmo_behaviorlink_state_t *link_state = link_obj
                ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
                : NULL;
            if (link_state == NULL ||
                (link_state->in_io_id != io_id && link_state->out_io_id != io_id)) {
                continue;
            }
            NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
                report,
                link_ids[j],
                cause,
                "detached_control_link"));
            NMO_RETURN_IF_ERROR(edit_report_note_control_link_endpoints(
                report,
                cause,
                link_state->in_io_id,
                link_state->out_io_id));
        }
    }

    return NMO_OK;
}

static nmo_status_t edit_report_note_parameter_edge_source(
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t source_parameter_id)
{
    if (report == NULL || source_parameter_id == 0u) {
        return NMO_OK;
    }
    return nmo_edit_report_add_changed_object(
        report,
        source_parameter_id,
        cause,
        "parameter_edge_source");
}

static nmo_object_id_t edit_plan_get_parameterin_source(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t target_parameter_id)
{
    if (tx == NULL || target_parameter_id == 0u) {
        return 0u;
    }
    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, target_parameter_id)
        : NULL;
    nmo_parameterin_state_t *state = object
        ? (nmo_parameterin_state_t *)nmo_object_get_state(object)
        : NULL;
    return state ? state->source_id : 0u;
}

static void edit_plan_get_behavior_link_endpoints(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t link_id,
    nmo_object_id_t *out_from_io_id,
    nmo_object_id_t *out_to_io_id)
{
    if (out_from_io_id != NULL) {
        *out_from_io_id = 0u;
    }
    if (out_to_io_id != NULL) {
        *out_to_io_id = 0u;
    }
    if (tx == NULL || link_id == 0u) {
        return;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, link_id)
        : NULL;
    nmo_behaviorlink_state_t *state = object
        ? (nmo_behaviorlink_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return;
    }
    if (out_from_io_id != NULL) {
        *out_from_io_id = state->in_io_id;
    }
    if (out_to_io_id != NULL) {
        *out_to_io_id = state->out_io_id;
    }
}

static void edit_plan_get_parameter_operation_slots(
    nmo_script_edit_tx_t *tx,
    nmo_object_id_t operation_id,
    nmo_object_id_t *out_in1_parameter_id,
    nmo_object_id_t *out_in2_parameter_id,
    nmo_object_id_t *out_out_parameter_id)
{
    if (out_in1_parameter_id != NULL) {
        *out_in1_parameter_id = 0u;
    }
    if (out_in2_parameter_id != NULL) {
        *out_in2_parameter_id = 0u;
    }
    if (out_out_parameter_id != NULL) {
        *out_out_parameter_id = 0u;
    }
    if (tx == NULL || operation_id == 0u) {
        return;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, operation_id)
        : NULL;
    nmo_parameteroperation_state_t *state = object
        ? (nmo_parameteroperation_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return;
    }
    if (out_in1_parameter_id != NULL && state->has_in1) {
        *out_in1_parameter_id = state->in1_id;
    }
    if (out_in2_parameter_id != NULL && state->has_in2) {
        *out_in2_parameter_id = state->in2_id;
    }
    if (out_out_parameter_id != NULL && state->has_out) {
        *out_out_parameter_id = state->out_id;
    }
}

static nmo_status_t edit_report_note_parameter_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t parameter_id)
{
    if (tx == NULL || report == NULL || parameter_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    if (repo == NULL) {
        return NMO_OK;
    }

    const size_t object_count = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
        if (object == NULL) {
            continue;
        }

        if (nmo_object_get_class_id(object) == NMO_CID_PARAMETERIN) {
            const nmo_parameterin_state_t *state =
                (const nmo_parameterin_state_t *)nmo_object_get_state(object);
            if (state != NULL && state->source_id == parameter_id) {
                NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
                    report,
                    nmo_object_get_id(object),
                    cause,
                    "parameter_edge_target"));
            }
        } else if (nmo_object_get_class_id(object) == NMO_CID_PARAMETEROPERATION) {
            const nmo_parameteroperation_state_t *state =
                (const nmo_parameteroperation_state_t *)nmo_object_get_state(object);
            if (state == NULL) {
                continue;
            }

            if ((state->has_in1 && state->in1_id == parameter_id) ||
                (state->has_in2 && state->in2_id == parameter_id) ||
                (state->has_out && state->out_id == parameter_id)) {
                NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
                    report,
                    nmo_object_get_id(object),
                    cause,
                    "operation_slot_owner"));
            }
        }
    }

    return NMO_OK;
}

static nmo_status_t edit_report_note_behavior_owned_deleted_objects(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || report == NULL || behavior_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return NMO_OK;
    }

    if (state->target_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            state->target_parameter_id,
            cause,
            "target_parameter"));
    }

    const struct {
        const nmo_array_t *array;
        const char *role;
    } owned_arrays[] = {
        { &state->inputs, "owned_io" },
        { &state->outputs, "owned_io" },
        { &state->in_parameters, "owned_parameter" },
        { &state->out_parameters, "owned_parameter" },
        { &state->local_parameters, "owned_parameter" },
    };

    for (size_t i = 0u; i < sizeof(owned_arrays) / sizeof(owned_arrays[0]); ++i) {
        const nmo_array_t *array = owned_arrays[i].array;
        const nmo_object_id_t *ids = array && array->data
            ? (const nmo_object_id_t *)array->data
            : NULL;
        for (size_t j = 0u; ids != NULL && j < array->count; ++j) {
            NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
                report,
                ids[j],
                cause,
                owned_arrays[i].role));
        }
    }

    return NMO_OK;
}

static nmo_status_t edit_report_note_behavior_io_detach_impacts(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    nmo_edit_op_kind_t cause,
    nmo_object_id_t behavior_id)
{
    if (tx == NULL || report == NULL || behavior_id == 0u) {
        return NMO_OK;
    }

    nmo_object_repository_t *repo =
        nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    if (state == NULL) {
        return NMO_OK;
    }

    const nmo_array_t *io_arrays[] = {
        &state->inputs,
        &state->outputs,
    };
    for (size_t i = 0u; i < sizeof(io_arrays) / sizeof(io_arrays[0]); ++i) {
        const nmo_array_t *array = io_arrays[i];
        const nmo_object_id_t *ids = array && array->data
            ? (const nmo_object_id_t *)array->data
            : NULL;
        for (size_t j = 0u; ids != NULL && j < array->count; ++j) {
            NMO_RETURN_IF_ERROR(edit_report_note_io_detach_impacts(
                tx, report, cause, ids[j]));
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_edit_report_merge_semantic_risks(
    nmo_edit_report_t *report,
    const nmo_behavior_semantic_risk_t *risks,
    size_t risk_count)
{
    if (report == NULL || (risk_count > 0u && risks == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (risk_count == 0u) {
        return NMO_OK;
    }
    for (size_t i = 0; i < risk_count; ++i) {
        bool exists = false;
        for (size_t j = 0; j < report->semantic_risk_count; ++j) {
            const nmo_behavior_semantic_risk_t *existing =
                &report->semantic_risks[j];
            if (existing->severity == risks[i].severity &&
                existing->object_id == risks[i].object_id &&
                ((existing->code == NULL && risks[i].code == NULL) ||
                 (existing->code != NULL && risks[i].code != NULL &&
                  strcmp(existing->code, risks[i].code) == 0))) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        if (report->semantic_risk_count + 1u >
            report->semantic_risk_capacity) {
            size_t new_capacity = report->semantic_risk_capacity == 0u
                ? 8u
                : report->semantic_risk_capacity * 2u;
            nmo_behavior_semantic_risk_t *next =
                (nmo_behavior_semantic_risk_t *)realloc(
                    report->semantic_risks,
                    new_capacity * sizeof(*next));
            if (next == NULL) {
                return NMO_ERR_NOMEM;
            }
            report->semantic_risks = next;
            report->semantic_risk_capacity = new_capacity;
        }
        report->semantic_risks[report->semantic_risk_count++] = risks[i];
    }
    return NMO_OK;
}

static nmo_status_t edit_report_note_fold_impact(
    nmo_edit_report_t *report,
    const nmo_behavior_fold_report_t *fold_report,
    nmo_object_id_t parent_id)
{
    if (report == NULL || fold_report == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
        report, parent_id, NMO_EDIT_OP_FOLD, "parent"));
    NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
        report, fold_report->anchor_id, NMO_EDIT_OP_FOLD, "anchor"));

    for (size_t i = 0; i < fold_report->selected_node_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            fold_report->selected_nodes[i],
            NMO_EDIT_OP_FOLD,
            "selected_node"));
    }
    for (size_t i = 0; i < fold_report->nodes_to_delete_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            fold_report->nodes_to_delete[i],
            NMO_EDIT_OP_FOLD,
            "folded_node"));
    }
    for (size_t i = 0; i < fold_report->control_links_to_delete_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_deleted_object(
            report,
            fold_report->control_links_to_delete[i].link_id,
            NMO_EDIT_OP_FOLD,
            "folded_control_link"));
    }

    const nmo_behavior_boundary_t *boundary = &fold_report->boundary;
    for (size_t i = 0; i < boundary->control_in_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->control_in[i].link_id,
            NMO_EDIT_OP_FOLD,
            "boundary_control_link"));
    }
    for (size_t i = 0; i < boundary->control_out_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->control_out[i].link_id,
            NMO_EDIT_OP_FOLD,
            "boundary_control_link"));
    }
    for (size_t i = 0; i < boundary->parameter_in_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_in[i].source_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_source"));
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_in[i].target_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_target"));
    }
    for (size_t i = 0; i < boundary->parameter_out_count; ++i) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_out[i].source_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_source"));
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_changed_object(
            report,
            boundary->parameter_out[i].target_parameter_id,
            NMO_EDIT_OP_FOLD,
            "boundary_parameter_target"));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_resolve_operation_handle(
    const nmo_edit_report_t *report,
    size_t operation_index,
    const char *handle_name,
    nmo_object_id_t *out_id);

static nmo_status_t edit_executor_apply_op(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_op_t *op,
    nmo_object_id_t *out_result_id,
    bool dry_run,
    nmo_edit_report_t *report,
    const char **out_diagnostic_code,
    const char **out_diagnostic_message)
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
    case NMO_EDIT_OP_SET_PARAMETER_VALUE: {
        nmo_object_id_t parameter_id = op->primary_id;
        if (op->data.set_value.has_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.set_value.parameter_ref_operation_index,
                op->data.set_value.parameter_ref_handle,
                &parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "handle_not_found";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message =
                        "Referenced edit operation handle was not found";
                }
                return ref_rc;
            }
        }
        {
            nmo_object_repository_t *repo =
                nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
            nmo_object_t *parameter_obj = repo != NULL
                ? nmo_object_repository_find_by_id(repo, parameter_id)
                : NULL;
            if (parameter_obj != NULL &&
                nmo_object_get_class_id(parameter_obj) == NMO_CID_PARAMETERIN) {
                nmo_status_t source_rc =
                    nmo_script_edit_ensure_input_parameter_source(
                        tx, parameter_id, &parameter_id);
                if (source_rc != NMO_OK) {
                    return source_rc;
                }
            }
        }
        if (out_result_id != NULL) {
            *out_result_id = parameter_id;
        }
        nmo_status_t write_rc = nmo_value_writer_set_parameter_value(
            edit,
            parameter_id,
            op->data.set_value.value,
            op->data.set_value.has_options ? &op->data.set_value.options : NULL);
        if (write_rc == NMO_OK) {
            nmo_script_edit_mark(tx, NMO_WORKSPACE_EDIT_OBJECT_STATE |
                                     NMO_WORKSPACE_EDIT_REFERENCES);
        }
        return write_rc;
    }
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
    {
        nmo_object_id_t parameter_id = op->primary_id;
        if (op->data.set_bytes.has_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.set_bytes.parameter_ref_operation_index,
                op->data.set_bytes.parameter_ref_handle,
                &parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "handle_not_found";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message =
                        "Referenced edit operation parameter handle was not found";
                }
                return ref_rc;
            }
            nmo_object_repository_t *repo =
                nmo_workspace_internal_repository(nmo_script_edit_workspace(tx));
            nmo_object_t *parameter_obj = repo != NULL
                ? nmo_object_repository_find_by_id(repo, parameter_id)
                : NULL;
            if (parameter_obj != NULL &&
                nmo_object_get_class_id(parameter_obj) == NMO_CID_PARAMETERIN) {
                nmo_status_t source_rc =
                    nmo_script_edit_ensure_input_parameter_source(
                        tx, parameter_id, &parameter_id);
                if (source_rc != NMO_OK) {
                    return source_rc;
                }
            }
        }
        if (out_result_id != NULL) {
            *out_result_id = parameter_id;
        }
        return nmo_value_writer_set_parameter_bytes(
            edit,
            parameter_id,
            op->data.set_bytes.bytes,
            op->data.set_bytes.byte_count,
            op->data.set_bytes.has_options ? &op->data.set_bytes.options : NULL);
    }
    case NMO_EDIT_OP_ADD_NODE:
        return nmo_script_edit_add_node(
            tx,
            op->data.add_node.parent_behavior_id,
            op->data.add_node.bb_guid,
            op->data.add_node.name,
            out_result_id);
    case NMO_EDIT_OP_REMOVE_NODE:
    {
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_owned_deleted_objects(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_NODE,
            op->data.remove_node.node_id));
        NMO_RETURN_IF_ERROR(edit_report_note_behavior_io_detach_impacts(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_NODE,
            op->data.remove_node.node_id));
        return nmo_script_edit_remove_node(
            tx,
            op->data.remove_node.parent_behavior_id,
            op->data.remove_node.node_id,
            op->data.remove_node.delete_flags);
    }
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
    {
        if (op->data.remove_io.detach_links) {
            NMO_RETURN_IF_ERROR(edit_report_note_io_detach_impacts(
                tx,
                report,
                NMO_EDIT_OP_REMOVE_IO,
                op->data.remove_io.io_id));
        }
        return nmo_script_edit_remove_io(
            tx,
            op->data.remove_io.io_id,
            op->data.remove_io.detach_links);
    }
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK: {
        nmo_object_id_t from_io_id = op->data.add_link.from_io_id;
        nmo_object_id_t to_io_id = op->data.add_link.to_io_id;
        if (op->data.add_link.has_from_io_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.add_link.from_io_ref_operation_index,
                op->data.add_link.from_io_ref_handle,
                &from_io_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "handle_not_found";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message =
                        "Referenced edit operation output IO handle was not found";
                }
                return ref_rc;
            }
        }
        if (op->data.add_link.has_to_io_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.add_link.to_io_ref_operation_index,
                op->data.add_link.to_io_ref_handle,
                &to_io_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "handle_not_found";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message =
                        "Referenced edit operation input IO handle was not found";
                }
                return ref_rc;
            }
        }
        nmo_status_t rc = nmo_script_edit_add_behavior_link(
            tx,
            op->data.add_link.parent_behavior_id,
            from_io_id,
            to_io_id,
            op->data.add_link.activation_delay,
            out_result_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_ADD_BEHAVIOR_LINK,
            from_io_id,
            to_io_id);
    }
    case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK: {
        nmo_status_t rc = nmo_script_edit_rewire_behavior_link(
            tx,
            op->data.rewire_link.link_id,
            op->data.rewire_link.from_io_id,
            op->data.rewire_link.to_io_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK,
            op->data.rewire_link.from_io_id,
            op->data.rewire_link.to_io_id);
    }
    case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY:
        return nmo_script_edit_set_behavior_link_delay(
            tx,
            op->data.set_link_delay.link_id,
            op->data.set_link_delay.activation_delay);
    case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
    {
        nmo_object_id_t from_io_id = 0u;
        nmo_object_id_t to_io_id = 0u;
        edit_plan_get_behavior_link_endpoints(
            tx,
            op->data.remove_link.link_id,
            &from_io_id,
            &to_io_id);
        nmo_status_t rc = nmo_script_edit_remove_behavior_link(
            tx,
            op->data.remove_link.parent_behavior_id,
            op->data.remove_link.link_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_control_link_endpoints(
            report,
            NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK,
            from_io_id,
            to_io_id);
    }
    case NMO_EDIT_OP_ADD_PARAMETER:
        return nmo_script_edit_add_parameter(
            tx,
            op->data.add_parameter.owner_behavior_id,
            op->data.add_parameter.kind,
            op->data.add_parameter.type_guid,
            op->data.add_parameter.name,
            out_result_id);
    case NMO_EDIT_OP_CONNECT_PARAMETER: {
        nmo_object_id_t target_parameter_id =
            op->data.connect_parameter.target_parameter_id;
        if (op->data.connect_parameter.has_target_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.connect_parameter.target_parameter_ref_operation_index,
                op->data.connect_parameter.target_parameter_ref_handle,
                &target_parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "handle_not_found";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message =
                        "Referenced edit operation parameter handle was not found";
                }
                return ref_rc;
            }
        }
        nmo_status_t rc = nmo_script_edit_connect_parameter(
            tx,
            op->data.connect_parameter.source_parameter_id,
            target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
        if (out_result_id != NULL) {
            *out_result_id = target_parameter_id;
        }
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_CONNECT_PARAMETER,
            op->data.connect_parameter.source_parameter_id);
    }
    case NMO_EDIT_OP_DISCONNECT_PARAMETER: {
        nmo_object_id_t old_source_parameter_id =
            edit_plan_get_parameterin_source(
                tx,
                op->data.disconnect_parameter.target_parameter_id);
        nmo_status_t rc = nmo_script_edit_disconnect_parameter(
            tx,
            op->data.disconnect_parameter.target_parameter_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_DISCONNECT_PARAMETER,
            old_source_parameter_id);
    }
    case NMO_EDIT_OP_REMOVE_PARAMETER:
    {
        nmo_object_id_t old_source_parameter_id =
            edit_plan_get_parameterin_source(
                tx,
                op->data.remove_parameter.parameter_id);
        NMO_RETURN_IF_ERROR(edit_report_note_parameter_detach_impacts(
            tx,
            report,
            NMO_EDIT_OP_REMOVE_PARAMETER,
            op->data.remove_parameter.parameter_id));
        nmo_status_t rc = nmo_script_edit_remove_parameter(
            tx,
            op->data.remove_parameter.parameter_id,
            op->data.remove_parameter.detach);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_parameter_edge_source(
            report,
            NMO_EDIT_OP_REMOVE_PARAMETER,
            old_source_parameter_id);
    }
    case NMO_EDIT_OP_ADD_OPERATION:
    {
        nmo_object_id_t in1_parameter_id =
            op->data.add_operation.in1_parameter_id;
        nmo_object_id_t in2_parameter_id =
            op->data.add_operation.in2_parameter_id;
        nmo_object_id_t out_parameter_id =
            op->data.add_operation.out_parameter_id;
        if (op->data.add_operation.has_in1_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.add_operation.in1_parameter_ref_operation_index,
                op->data.add_operation.in1_parameter_ref_handle,
                &in1_parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "handle_not_found";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message =
                        "Referenced edit operation input parameter handle was not found";
                }
                return ref_rc;
            }
        }
        if (op->data.add_operation.has_in2_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.add_operation.in2_parameter_ref_operation_index,
                op->data.add_operation.in2_parameter_ref_handle,
                &in2_parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "handle_not_found";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message =
                        "Referenced edit operation input parameter handle was not found";
                }
                return ref_rc;
            }
        }
        if (op->data.add_operation.has_out_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.add_operation.out_parameter_ref_operation_index,
                op->data.add_operation.out_parameter_ref_handle,
                &out_parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "handle_not_found";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message =
                        "Referenced edit operation output parameter handle was not found";
                }
                return ref_rc;
            }
        }
        nmo_status_t rc = nmo_script_edit_add_operation(
            tx,
            op->data.add_operation.parent_behavior_id,
            op->data.add_operation.operation_guid,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id,
            out_result_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_ADD_OPERATION,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
    }
    case NMO_EDIT_OP_REWIRE_OPERATION:
    {
        nmo_object_id_t in1_parameter_id =
            op->data.rewire_operation.in1_parameter_id;
        nmo_object_id_t in2_parameter_id =
            op->data.rewire_operation.in2_parameter_id;
        nmo_object_id_t out_parameter_id =
            op->data.rewire_operation.out_parameter_id;
        if (op->data.rewire_operation.has_in1_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.rewire_operation.in1_parameter_ref_operation_index,
                op->data.rewire_operation.in1_parameter_ref_handle,
                &in1_parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "missing_in1_handle";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message = "Failed to resolve in1 parameter handle";
                }
                return ref_rc;
            }
        }
        if (op->data.rewire_operation.has_in2_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.rewire_operation.in2_parameter_ref_operation_index,
                op->data.rewire_operation.in2_parameter_ref_handle,
                &in2_parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "missing_in2_handle";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message = "Failed to resolve in2 parameter handle";
                }
                return ref_rc;
            }
        }
        if (op->data.rewire_operation.has_out_parameter_ref) {
            nmo_status_t ref_rc = edit_report_resolve_operation_handle(
                report,
                op->data.rewire_operation.out_parameter_ref_operation_index,
                op->data.rewire_operation.out_parameter_ref_handle,
                &out_parameter_id);
            if (ref_rc != NMO_OK) {
                if (out_diagnostic_code != NULL) {
                    *out_diagnostic_code = "missing_out_handle";
                }
                if (out_diagnostic_message != NULL) {
                    *out_diagnostic_message = "Failed to resolve out parameter handle";
                }
                return ref_rc;
            }
        }
        nmo_status_t rc = nmo_script_edit_rewire_operation(
            tx,
            op->data.rewire_operation.operation_id,
            op->data.rewire_operation.slot_flags,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
        if (rc != NMO_OK || report == NULL) {
            return rc;
        }
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_REWIRE_OPERATION,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u ? in1_parameter_id : 0u,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u ? in2_parameter_id : 0u,
            (op->data.rewire_operation.slot_flags &
             NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u ? out_parameter_id : 0u);
    }
    case NMO_EDIT_OP_REMOVE_OPERATION: {
        nmo_object_id_t in1_parameter_id = 0u;
        nmo_object_id_t in2_parameter_id = 0u;
        nmo_object_id_t out_parameter_id = 0u;
        edit_plan_get_parameter_operation_slots(
            tx,
            op->data.remove_operation.operation_id,
            &in1_parameter_id,
            &in2_parameter_id,
            &out_parameter_id);
        nmo_status_t rc = nmo_script_edit_remove_operation(
            tx,
            op->data.remove_operation.operation_id);
        if (rc != NMO_OK) {
            return rc;
        }
        return edit_report_note_operation_slot_parameters(
            report,
            NMO_EDIT_OP_REMOVE_OPERATION,
            in1_parameter_id,
            in2_parameter_id,
            out_parameter_id);
    }
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
        if (out_diagnostic_code != NULL) {
            *out_diagnostic_code = replace_report.diagnostic_code;
        }
        if (out_diagnostic_message != NULL) {
            *out_diagnostic_message = replace_report.diagnostic_message;
        }
        if (rc == NMO_OK && out_result_id != NULL) {
            *out_result_id = op->data.replace_bb.desc.behavior_id;
        }
        if (rc == NMO_OK && report != NULL) {
            rc = nmo_edit_report_merge_semantic_risks(
                report,
                replace_report.semantic_risks,
                replace_report.semantic_risk_count);
        }
        free(replace_report.semantic_risks);
        return rc;
    }
    case NMO_EDIT_OP_FOLD: {
        nmo_behavior_fold_report_t fold_report = {0};
        nmo_status_t rc = dry_run
            ? nmo_behavior_edit_fold_analyze(
                  nmo_script_edit_workspace(tx),
                  &op->data.fold.desc,
                  &fold_report)
            : nmo_behavior_edit_fold_in_script_tx(
                  tx,
                  &op->data.fold.desc,
                  &fold_report);
        if (out_diagnostic_code != NULL) {
            *out_diagnostic_code = fold_report.diagnostic_code;
        }
        if (out_diagnostic_message != NULL) {
            *out_diagnostic_message = fold_report.diagnostic_message;
        }
        if (rc == NMO_OK && out_result_id != NULL) {
            *out_result_id = fold_report.anchor_id != 0u
                ? fold_report.anchor_id
                : op->data.fold.desc.anchor_id;
        }
        if (rc == NMO_OK && report != NULL) {
            rc = nmo_edit_report_merge_semantic_risks(
                report,
                fold_report.semantic_risks,
                fold_report.semantic_risk_count);
            if (rc == NMO_OK) {
                rc = edit_report_note_fold_impact(
                    report, &fold_report, op->data.fold.desc.parent_id);
            }
        }
        nmo_behavior_edit_fold_report_free(&fold_report);
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

static nmo_status_t edit_report_add_named_handle(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *prefix,
    nmo_object_repository_t *repo,
    nmo_object_id_t id)
{
    if (id == 0u) {
        return NMO_OK;
    }
    nmo_object_t *object = repo != NULL
        ? nmo_object_repository_find_by_id(repo, id)
        : NULL;
    const char *name = object != NULL ? nmo_object_get_name(object) : NULL;
    char handle_name[160];
    if (name != NULL && name[0] != '\0') {
        snprintf(handle_name, sizeof(handle_name), "%s:%s", prefix, name);
    } else {
        snprintf(handle_name, sizeof(handle_name), "%s", prefix);
    }
    return nmo_edit_report_add_operation_handle(
        report, operation_index, handle_name, id);
}

static nmo_status_t edit_report_add_array_handles(
    nmo_edit_report_t *report,
    size_t operation_index,
    const char *prefix,
    nmo_object_repository_t *repo,
    const nmo_array_t *array)
{
    if (array == NULL || array->count == 0u) {
        return NMO_OK;
    }
    if (array->element_size != sizeof(nmo_object_id_t) ||
        array->data == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    const nmo_object_id_t *ids = (const nmo_object_id_t *)array->data;
    for (size_t i = 0; i < array->count; ++i) {
        NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
            report, operation_index, prefix, repo, ids[i]));
    }
    return NMO_OK;
}

static nmo_status_t edit_report_add_input_parameter_handles(
    nmo_edit_report_t *report,
    size_t operation_index,
    nmo_object_repository_t *repo,
    const nmo_array_t *array)
{
    if (array == NULL || array->count == 0u) {
        return NMO_OK;
    }
    if (array->element_size != sizeof(nmo_object_id_t) ||
        array->data == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    const nmo_object_id_t *ids = (const nmo_object_id_t *)array->data;
    for (size_t i = 0; i < array->count; ++i) {
        NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
            report, operation_index, "input_param", repo, ids[i]));
        nmo_object_t *param_obj =
            repo != NULL ? nmo_object_repository_find_by_id(repo, ids[i]) : NULL;
        nmo_parameterin_state_t *param_state = param_obj != NULL
            ? (nmo_parameterin_state_t *)nmo_object_get_state(param_obj)
            : NULL;
        if (param_state != NULL && param_state->source_id != 0u) {
            NMO_RETURN_IF_ERROR(edit_report_add_named_handle(
                report,
                operation_index,
                "input_param_source",
                repo,
                param_state->source_id));
        }
    }
    return NMO_OK;
}

static nmo_status_t edit_report_add_node_child_handles(
    nmo_script_edit_tx_t *tx,
    nmo_edit_report_t *report,
    size_t operation_index,
    nmo_object_id_t node_id)
{
    if (tx == NULL || report == NULL || node_id == 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_workspace_t *workspace = nmo_script_edit_workspace(tx);
    nmo_object_repository_t *repo =
        workspace != NULL ? nmo_workspace_internal_repository(workspace) : NULL;
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }
    nmo_object_t *node_obj = nmo_object_repository_find_by_id(repo, node_id);
    nmo_behavior_state_t *state = node_obj != NULL
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    if (state == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    if (state->target_parameter_id != 0u) {
        NMO_RETURN_IF_ERROR(nmo_edit_report_add_operation_handle(
            report, operation_index, "target", state->target_parameter_id));
    }
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "input", repo, &state->inputs));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "output", repo, &state->outputs));
    NMO_RETURN_IF_ERROR(edit_report_add_input_parameter_handles(
        report, operation_index, repo, &state->in_parameters));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "output_param", repo, &state->out_parameters));
    NMO_RETURN_IF_ERROR(edit_report_add_array_handles(
        report, operation_index, "local_param", repo, &state->local_parameters));
    return NMO_OK;
}

static nmo_status_t edit_report_resolve_operation_handle(
    const nmo_edit_report_t *report,
    size_t operation_index,
    const char *handle_name,
    nmo_object_id_t *out_id)
{
    if (report == NULL || handle_name == NULL || out_id == NULL ||
        operation_index >= report->operation_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const nmo_edit_operation_result_t *operation =
        &report->operations[operation_index];
    for (size_t i = 0; i < operation->handle_count; ++i) {
        if (operation->handles[i].name != NULL &&
            strcmp(operation->handles[i].name, handle_name) == 0) {
            *out_id = operation->handles[i].id;
            return NMO_OK;
        }
    }
    return NMO_ERR_NOT_FOUND;
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
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        return op->data.connect_parameter.target_parameter_id;
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

static nmo_status_t edit_executor_validate_semantics(
    nmo_script_edit_tx_t *tx,
    const nmo_edit_plan_t *plan,
    nmo_edit_report_t *report,
    bool allow_rewrite_analysis_failure)
{
    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    nmo_status_t rc = nmo_semantic_validate_edit_plan(
        nmo_script_edit_workspace(tx), plan, &risks, &risk_count);
    if (rc != NMO_OK) {
        nmo_semantic_risks_free(risks);
        if (allow_rewrite_analysis_failure &&
            (rc == NMO_ERR_INVALID_STATE || rc == NMO_ERR_NOT_FOUND ||
             rc == NMO_ERR_VALIDATION_FAILED)) {
            return NMO_OK;
        }
        return rc;
    }
    rc = nmo_edit_report_merge_semantic_risks(report, risks, risk_count);
    nmo_semantic_risks_free(risks);
    return rc;
}

static bool edit_plan_contains_rewrite_op(const nmo_edit_plan_t *plan)
{
    if (plan == NULL) {
        return false;
    }
    for (size_t i = 0; i < plan->count; ++i) {
        if (plan->ops[i].kind == NMO_EDIT_OP_FOLD ||
            plan->ops[i].kind == NMO_EDIT_OP_REPLACE_BB) {
            return true;
        }
    }
    return false;
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

    const bool has_rewrite_ops = edit_plan_contains_rewrite_op(plan);
    rc = edit_executor_validate_semantics(tx, plan, report, has_rewrite_ops);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
    }

    for (size_t i = 0; i < plan->count; i++) {
        const nmo_edit_op_t *op = &plan->ops[i];
        const nmo_script_edit_report_t *tx_report_before =
            nmo_script_edit_report(tx);
        size_t created_start = tx_report_before
            ? tx_report_before->created_object_id_count
            : 0u;
        nmo_object_id_t result_id = 0;
        const char *diagnostic_code = NULL;
        const char *diagnostic_message = NULL;
        nmo_status_t op_rc = edit_executor_apply_op(
            tx,
            op,
            &result_id,
            effective.dry_run,
            report,
            &diagnostic_code,
            &diagnostic_message);
        const nmo_script_edit_report_t *tx_report_after =
            nmo_script_edit_report(tx);
        report->operations[i] = (nmo_edit_operation_result_t){
            .kind = op->kind,
            .primary_id = op->primary_id,
            .result_id = result_id,
            .status = op_rc,
        };
        nmo_status_t diagnostic_rc = edit_report_set_operation_diagnostic(
            report, i, diagnostic_code, diagnostic_message);
        if (diagnostic_rc != NMO_OK) {
            report->ok = false;
            report->status = diagnostic_rc;
            return diagnostic_rc;
        }
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
            if (op->kind == NMO_EDIT_OP_ADD_NODE) {
                handle_rc = edit_report_add_node_child_handles(
                    tx, report, i, result_id);
                if (handle_rc != NMO_OK) {
                    report->ok = false;
                    report->status = handle_rc;
                    return handle_rc;
                }
            }
        }
        bool noted_created_objects = false;
        if (tx_report_after &&
            tx_report_after->created_object_ids &&
            tx_report_after->created_object_id_count > created_start) {
            nmo_status_t report_rc = edit_report_note_created_objects(
                report,
                tx_report_after->created_object_ids + created_start,
                tx_report_after->created_object_id_count - created_start,
                op->kind,
                "created");
            if (report_rc != NMO_OK) {
                report->ok = false;
                report->status = report_rc;
                return report_rc;
            }
            noted_created_objects = true;
        }
        if (edit_op_creates_result(op->kind) && !noted_created_objects) {
            nmo_status_t report_rc = nmo_edit_report_add_created_object(
                report, result_id, op->kind, "created");
            if (report_rc != NMO_OK) {
                report->ok = false;
                report->status = report_rc;
                return report_rc;
            }
        }
        nmo_object_id_t deleted_id = edit_op_deleted_id(op);
        if (deleted_id != 0u) {
            (void)nmo_edit_report_add_deleted_object(
                report, deleted_id, op->kind, "primary");
        }
        nmo_object_id_t changed_id = edit_op_changed_id(op);
        if (changed_id == 0u && result_id != 0u) {
            changed_id = result_id;
        }
        (void)nmo_edit_report_add_changed_object(
            report, changed_id, op->kind, "primary");
    }

    rc = edit_executor_validate_semantics(
        tx, plan, report, has_rewrite_ops);
    if (rc != NMO_OK) {
        report->ok = false;
        report->status = rc;
        return rc;
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
