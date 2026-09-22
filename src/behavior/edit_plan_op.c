/**
 * @file edit_plan_op.c
 * @brief Ownership helpers for edit operations: option, handle, and payload copies.
 */

#include "edit_plan_internal.h"

#include <stdlib.h>
#include <string.h>

static void edit_plan_manager_entry_options_dispose(
    nmo_manager_entry_options_t *options)
{
    if (options == NULL) {
        return;
    }
    free((void *)options->key);
    free((void *)options->create.category);
    options->key = NULL;
    options->create.category = NULL;
}

static nmo_status_t edit_plan_manager_entry_options_clone(
    nmo_manager_entry_options_t *dst,
    const nmo_manager_entry_options_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *dst = *src;
    dst->key = NULL;
    dst->create.category = NULL;
    dst->key = edit_plan_strdup(src->key);
    if (src->key != NULL && dst->key == NULL) {
        return NMO_ERR_NOMEM;
    }
    dst->create.category = edit_plan_strdup(src->create.category);
    if (src->create.category != NULL && dst->create.category == NULL) {
        edit_plan_manager_entry_options_dispose(dst);
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

static nmo_status_t edit_plan_parameter_write_options_clone(
    nmo_parameter_write_options_t *dst,
    const nmo_parameter_write_options_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *dst = *src;
    return edit_plan_manager_entry_options_clone(
        &dst->manager_entry, &src->manager_entry);
}

static void edit_plan_parameter_write_options_dispose(
    nmo_parameter_write_options_t *options)
{
    if (options == NULL) {
        return;
    }
    edit_plan_manager_entry_options_dispose(&options->manager_entry);
}

nmo_status_t edit_plan_add_node_options_clone(
    nmo_add_node_options_t *dst,
    const nmo_add_node_options_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *dst = *src;
    return edit_plan_manager_entry_options_clone(
        &dst->manager_entry, &src->manager_entry);
}

static void edit_plan_add_node_options_dispose(
    nmo_add_node_options_t *options)
{
    if (options == NULL) {
        return;
    }
    edit_plan_manager_entry_options_dispose(&options->manager_entry);
}

nmo_status_t edit_plan_dup_object_ids(
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

nmo_status_t edit_plan_dup_fold_maps(
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

static void edit_plan_handle_ref_dispose(edit_plan_handle_ref_t *ref)
{
    if (ref == NULL) {
        return;
    }
    free((void *)ref->handle_name);
    memset(ref, 0, sizeof(*ref));
}

static nmo_status_t edit_plan_handle_ref_clone(
    edit_plan_handle_ref_t *dst,
    const edit_plan_handle_ref_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *dst = *src;
    dst->handle_name = NULL;
    if (!src->has_ref) {
        dst->operation_index = 0u;
        return NMO_OK;
    }
    if (src->handle_name == NULL || src->handle_name[0] == '\0') {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *handle_copy = edit_plan_strdup(src->handle_name);
    if (handle_copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    dst->handle_name = handle_copy;
    return NMO_OK;
}

static nmo_status_t edit_plan_handle_ref_clone_slots(
    const edit_plan_handle_ref_clone_slot_t *slots,
    size_t slot_count)
{
    if (slot_count == 0u) {
        return NMO_OK;
    }
    if (slots == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < slot_count; ++i) {
        if (slots[i].dst == NULL || slots[i].src == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        nmo_status_t st =
            edit_plan_handle_ref_clone(slots[i].dst, slots[i].src);
        if (st != NMO_OK) {
            return st;
        }
    }
    return NMO_OK;
}

nmo_status_t edit_plan_copy_parameter_write_options(
    nmo_parameter_write_options_t *out_options,
    bool *out_has_options,
    const nmo_parameter_write_options_t *options)
{
    if (out_options == NULL || out_has_options == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_has_options = false;
    if (options == NULL) {
        return NMO_OK;
    }
    NMO_RETURN_IF_ERROR(edit_plan_parameter_write_options_clone(
        out_options, options));
    *out_has_options = true;
    return NMO_OK;
}

nmo_status_t edit_plan_copy_bytes(
    const uint8_t *bytes,
    size_t byte_count,
    const uint8_t **out_bytes)
{
    if (out_bytes == NULL || (bytes == NULL && byte_count > 0u)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_bytes = NULL;
    if (byte_count == 0u) {
        return NMO_OK;
    }
    uint8_t *copy = (uint8_t *)malloc(byte_count);
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }
    memcpy(copy, bytes, byte_count);
    *out_bytes = copy;
    return NMO_OK;
}

void edit_op_dispose(nmo_edit_op_t *op)
{
    if (op == NULL) {
        return;
    }
    if (op->kind == NMO_EDIT_OP_SET_PARAMETER_VALUE) {
        free((void *)op->data.set_value.value);
        edit_plan_handle_ref_dispose(&op->data.set_value.parameter_ref);
        edit_plan_parameter_write_options_dispose(
            &op->data.set_value.options);
    } else if (op->kind == NMO_EDIT_OP_SET_PARAMETER_BYTES) {
        free((void *)op->data.set_bytes.bytes);
        edit_plan_handle_ref_dispose(&op->data.set_bytes.parameter_ref);
        edit_plan_parameter_write_options_dispose(
            &op->data.set_bytes.options);
    } else if (op->kind == NMO_EDIT_OP_ADD_NODE) {
        free((void *)op->data.add_node.name);
        edit_plan_add_node_options_dispose(&op->data.add_node.options);
    } else if (op->kind == NMO_EDIT_OP_ADD_IO) {
        free((void *)op->data.add_io.name);
    } else if (op->kind == NMO_EDIT_OP_RENAME_IO) {
        free((void *)op->data.rename_io.name);
    } else if (op->kind == NMO_EDIT_OP_ADD_BEHAVIOR_LINK) {
        edit_plan_handle_ref_dispose(&op->data.add_link.from_io_ref);
        edit_plan_handle_ref_dispose(&op->data.add_link.to_io_ref);
    } else if (op->kind == NMO_EDIT_OP_ADD_PARAMETER) {
        free((void *)op->data.add_parameter.name);
    } else if (op->kind == NMO_EDIT_OP_CONNECT_PARAMETER) {
        edit_plan_handle_ref_dispose(
            &op->data.connect_parameter.target_parameter_ref);
    } else if (op->kind == NMO_EDIT_OP_ADD_OPERATION) {
        edit_plan_handle_ref_dispose(
            &op->data.add_operation.in1_parameter_ref);
        edit_plan_handle_ref_dispose(
            &op->data.add_operation.in2_parameter_ref);
        edit_plan_handle_ref_dispose(
            &op->data.add_operation.out_parameter_ref);
    } else if (op->kind == NMO_EDIT_OP_REWIRE_OPERATION) {
        edit_plan_handle_ref_dispose(
            &op->data.rewire_operation.in1_parameter_ref);
        edit_plan_handle_ref_dispose(
            &op->data.rewire_operation.in2_parameter_ref);
        edit_plan_handle_ref_dispose(
            &op->data.rewire_operation.out_parameter_ref);
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

nmo_status_t edit_op_clone_handle_ref_slots_or_dispose(
    nmo_edit_op_t *op,
    const edit_plan_handle_ref_clone_slot_t *slots,
    size_t slot_count)
{
    nmo_status_t st = edit_plan_handle_ref_clone_slots(slots, slot_count);
    if (st != NMO_OK) {
        edit_op_dispose(op);
        return st;
    }
    return NMO_OK;
}

static void edit_op_clear_owned_pointers(nmo_edit_op_t *op)
{
    if (op == NULL) {
        return;
    }
    switch (op->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        op->data.set_value.value = NULL;
        op->data.set_value.parameter_ref.handle_name = NULL;
        op->data.set_value.options.manager_entry.key = NULL;
        op->data.set_value.options.manager_entry.create.category = NULL;
        break;
    case NMO_EDIT_OP_SET_PARAMETER_BYTES:
        op->data.set_bytes.bytes = NULL;
        op->data.set_bytes.parameter_ref.handle_name = NULL;
        op->data.set_bytes.options.manager_entry.key = NULL;
        op->data.set_bytes.options.manager_entry.create.category = NULL;
        break;
    case NMO_EDIT_OP_ADD_NODE:
        op->data.add_node.name = NULL;
        op->data.add_node.options.manager_entry.key = NULL;
        op->data.add_node.options.manager_entry.create.category = NULL;
        break;
    case NMO_EDIT_OP_ADD_IO:
        op->data.add_io.name = NULL;
        break;
    case NMO_EDIT_OP_RENAME_IO:
        op->data.rename_io.name = NULL;
        break;
    case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
        op->data.add_link.from_io_ref.handle_name = NULL;
        op->data.add_link.to_io_ref.handle_name = NULL;
        break;
    case NMO_EDIT_OP_ADD_PARAMETER:
        op->data.add_parameter.name = NULL;
        break;
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        op->data.connect_parameter.target_parameter_ref.handle_name = NULL;
        break;
    case NMO_EDIT_OP_ADD_OPERATION:
        op->data.add_operation.in1_parameter_ref.handle_name = NULL;
        op->data.add_operation.in2_parameter_ref.handle_name = NULL;
        op->data.add_operation.out_parameter_ref.handle_name = NULL;
        break;
    case NMO_EDIT_OP_REWIRE_OPERATION:
        op->data.rewire_operation.in1_parameter_ref.handle_name = NULL;
        op->data.rewire_operation.in2_parameter_ref.handle_name = NULL;
        op->data.rewire_operation.out_parameter_ref.handle_name = NULL;
        break;
    case NMO_EDIT_OP_SET_DATA_CELL:
        op->data.data_cell.value = NULL;
        break;
    case NMO_EDIT_OP_FOLD:
        op->data.fold.desc.name = NULL;
        op->data.fold.desc.node_ids = NULL;
        op->data.fold.desc.input_maps = NULL;
        op->data.fold.desc.output_maps = NULL;
        op->data.fold.desc.parameter_maps = NULL;
        op->data.fold.node_ids = NULL;
        op->data.fold.input_maps = NULL;
        op->data.fold.output_maps = NULL;
        op->data.fold.parameter_maps = NULL;
        break;
    case NMO_EDIT_OP_REPLACE_BB:
        op->data.replace_bb.desc.name = NULL;
        break;
    default:
        break;
    }
}

nmo_status_t edit_op_copy(
    nmo_edit_op_t *dst,
    const nmo_edit_op_t *src)
{
    if (dst == NULL || src == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    edit_op_clear_owned_pointers(dst);
    switch (src->kind) {
    case NMO_EDIT_OP_SET_PARAMETER_VALUE:
        dst->data.set_value.value =
            edit_plan_strdup(src->data.set_value.value);
        if (src->data.set_value.value && !dst->data.set_value.value) {
            return NMO_ERR_NOMEM;
        }
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.set_value.parameter_ref,
                    &src->data.set_value.parameter_ref,
                },
            },
            1u));
        if (src->data.set_value.has_options) {
            nmo_status_t st = edit_plan_parameter_write_options_clone(
                &dst->data.set_value.options,
                &src->data.set_value.options);
            if (st != NMO_OK) {
                edit_op_dispose(dst);
                return st;
            }
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
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.set_bytes.parameter_ref,
                    &src->data.set_bytes.parameter_ref,
                },
            },
            1u));
        if (src->data.set_bytes.has_options) {
            nmo_status_t st = edit_plan_parameter_write_options_clone(
                &dst->data.set_bytes.options,
                &src->data.set_bytes.options);
            if (st != NMO_OK) {
                edit_op_dispose(dst);
                return st;
            }
        }
        break;
    case NMO_EDIT_OP_ADD_NODE:
        dst->data.add_node.name = edit_plan_strdup(src->data.add_node.name);
        if (src->data.add_node.name && !dst->data.add_node.name) {
            return NMO_ERR_NOMEM;
        }
        if (src->data.add_node.has_options) {
            nmo_status_t st = edit_plan_add_node_options_clone(
                &dst->data.add_node.options,
                &src->data.add_node.options);
            if (st != NMO_OK) {
                edit_op_dispose(dst);
                return st;
            }
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
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.add_link.from_io_ref,
                    &src->data.add_link.from_io_ref,
                },
                {
                    &dst->data.add_link.to_io_ref,
                    &src->data.add_link.to_io_ref,
                },
            },
            2u));
        break;
    case NMO_EDIT_OP_ADD_PARAMETER:
        dst->data.add_parameter.name =
            edit_plan_strdup(src->data.add_parameter.name);
        if (src->data.add_parameter.name && !dst->data.add_parameter.name) {
            return NMO_ERR_NOMEM;
        }
        break;
    case NMO_EDIT_OP_CONNECT_PARAMETER:
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.connect_parameter.target_parameter_ref,
                    &src->data.connect_parameter.target_parameter_ref,
                },
            },
            1u));
        break;
    case NMO_EDIT_OP_ADD_OPERATION:
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.add_operation.in1_parameter_ref,
                    &src->data.add_operation.in1_parameter_ref,
                },
                {
                    &dst->data.add_operation.in2_parameter_ref,
                    &src->data.add_operation.in2_parameter_ref,
                },
                {
                    &dst->data.add_operation.out_parameter_ref,
                    &src->data.add_operation.out_parameter_ref,
                },
            },
            3u));
        break;
    case NMO_EDIT_OP_REWIRE_OPERATION:
        NMO_RETURN_IF_ERROR(edit_op_clone_handle_ref_slots_or_dispose(
            dst,
            (edit_plan_handle_ref_clone_slot_t[]){
                {
                    &dst->data.rewire_operation.in1_parameter_ref,
                    &src->data.rewire_operation.in1_parameter_ref,
                },
                {
                    &dst->data.rewire_operation.in2_parameter_ref,
                    &src->data.rewire_operation.in2_parameter_ref,
                },
                {
                    &dst->data.rewire_operation.out_parameter_ref,
                    &src->data.rewire_operation.out_parameter_ref,
                },
            },
            3u));
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
