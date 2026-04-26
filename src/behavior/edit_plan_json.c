#include "behavior/nmo_edit_plan_json.h"

#include "core/nmo_guid.h"
#include "yyjson.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static void add_str_safe(yyjson_mut_doc *doc,
                         yyjson_mut_val *obj,
                         const char *key,
                         const char *value)
{
    if (value != NULL) {
        yyjson_mut_obj_add_strcpy(doc, obj, key, value);
    }
}

static void add_guid_json(yyjson_mut_doc *doc,
                          yyjson_mut_val *obj,
                          const char *key,
                          nmo_guid_t guid)
{
    char guid_text[32];
    if (nmo_guid_format(guid, guid_text, sizeof(guid_text)) > 0) {
        yyjson_mut_obj_add_strcpy(doc, obj, key, guid_text);
    }
}

static void add_optional_id_json(yyjson_mut_doc *doc,
                                 yyjson_mut_val *obj,
                                 const char *key,
                                 nmo_object_id_t id)
{
    if (id != 0u) {
        yyjson_mut_obj_add_uint(doc, obj, key, (uint64_t)id);
    }
}

static const char *edit_op_kind_string(nmo_edit_op_kind_t kind)
{
    switch (kind) {
        case NMO_EDIT_OP_SET_PARAMETER_VALUE:
            return "set_parameter_value";
        case NMO_EDIT_OP_SET_PARAMETER_BYTES:
            return "set_parameter_bytes";
        case NMO_EDIT_OP_ADD_NODE:
            return "add_node";
        case NMO_EDIT_OP_REMOVE_NODE:
            return "remove_node";
        case NMO_EDIT_OP_ADD_IO:
            return "add_io";
        case NMO_EDIT_OP_RENAME_IO:
            return "rename_io";
        case NMO_EDIT_OP_REMOVE_IO:
            return "remove_io";
        case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
            return "add_behavior_link";
        case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK:
            return "rewire_behavior_link";
        case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY:
            return "set_behavior_link_delay";
        case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
            return "remove_behavior_link";
        case NMO_EDIT_OP_ADD_PARAMETER:
            return "add_parameter";
        case NMO_EDIT_OP_CONNECT_PARAMETER:
            return "connect_parameter";
        case NMO_EDIT_OP_DISCONNECT_PARAMETER:
            return "disconnect_parameter";
        case NMO_EDIT_OP_REMOVE_PARAMETER:
            return "remove_parameter";
        case NMO_EDIT_OP_ADD_OPERATION:
            return "add_operation";
        case NMO_EDIT_OP_REWIRE_OPERATION:
            return "rewire_operation";
        case NMO_EDIT_OP_REMOVE_OPERATION:
            return "remove_operation";
        case NMO_EDIT_OP_INTERFACE_POLICY:
            return "interface_policy";
        case NMO_EDIT_OP_SET_DATA_CELL:
            return "set_data_cell";
        case NMO_EDIT_OP_FOLD:
            return "fold";
        case NMO_EDIT_OP_REPLACE_BB:
            return "replace_bb";
        default:
            return "unknown";
    }
}

static const char *io_kind_string(nmo_script_edit_io_kind_t kind)
{
    return kind == NMO_SCRIPT_EDIT_IO_OUTPUT ? "output" : "input";
}

static const char *parameter_kind_string(nmo_script_edit_parameter_kind_t kind)
{
    switch (kind) {
        case NMO_SCRIPT_EDIT_PARAM_OUT:
            return "out";
        case NMO_SCRIPT_EDIT_PARAM_LOCAL:
            return "local";
        case NMO_SCRIPT_EDIT_PARAM_SHARED:
            return "shared";
        case NMO_SCRIPT_EDIT_PARAM_IN:
        default:
            return "in";
    }
}

static const char *script_interface_mode_string(
    nmo_script_edit_interface_mode_t mode)
{
    switch (mode) {
        case NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE:
            return "canonicalize";
        case NMO_SCRIPT_EDIT_INTERFACE_REMOVE:
            return "remove";
        case NMO_SCRIPT_EDIT_INTERFACE_PRESERVE:
        default:
            return "preserve";
    }
}

static const char *fold_interface_mode_string(
    nmo_behavior_fold_interface_mode_t mode)
{
    switch (mode) {
        case NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE:
            return "canonicalize";
        case NMO_BEHAVIOR_FOLD_INTERFACE_REMOVE:
            return "remove";
        case NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE:
        default:
            return "preserve";
    }
}

static void add_ref_json(yyjson_mut_doc *doc,
                         yyjson_mut_val *obj,
                         const char *operation_key,
                         const char *handle_key,
                         size_t operation_index,
                         const char *handle_name)
{
    yyjson_mut_obj_add_uint(doc, obj, operation_key,
                            (uint64_t)(operation_index + 1u));
    add_str_safe(doc, obj, handle_key, handle_name);
}

static char *bytes_to_hex(const uint8_t *bytes, size_t byte_count)
{
    static const char hex[] = "0123456789ABCDEF";
    if (byte_count == 0u) {
        char *empty = (char *)malloc(1u);
        if (empty != NULL) {
            empty[0] = '\0';
        }
        return empty;
    }
    if (bytes == NULL || byte_count > (SIZE_MAX - 1u) / 2u) {
        return NULL;
    }
    char *out = (char *)malloc(byte_count * 2u + 1u);
    if (out == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < byte_count; ++i) {
        out[i * 2u] = hex[(bytes[i] >> 4) & 0x0Fu];
        out[i * 2u + 1u] = hex[bytes[i] & 0x0Fu];
    }
    out[byte_count * 2u] = '\0';
    return out;
}

static yyjson_mut_val *fold_maps_to_json(
    yyjson_mut_doc *doc,
    const nmo_behavior_fold_map_t *maps,
    size_t count)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (arr == NULL) {
        return NULL;
    }
    for (size_t i = 0; maps != NULL && i < count; ++i) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        if (item == NULL) {
            return arr;
        }
        yyjson_mut_obj_add_uint(doc, item, "old_index",
                                (uint64_t)maps[i].old_index);
        yyjson_mut_obj_add_uint(doc, item, "new_index",
                                (uint64_t)maps[i].new_index);
        add_optional_id_json(doc, item, "old_id", maps[i].old_id);
        add_optional_id_json(doc, item, "new_id", maps[i].new_id);
        add_str_safe(doc, item, "label", maps[i].label);
        yyjson_mut_arr_add_val(arr, item);
    }
    return arr;
}

static yyjson_mut_val *edit_op_to_json(yyjson_mut_doc *doc,
                                       const nmo_edit_op_t *op)
{
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    if (obj == NULL || op == NULL) {
        return obj;
    }

    yyjson_mut_obj_add_str(doc, obj, "op", edit_op_kind_string(op->kind));

    switch (op->kind) {
        case NMO_EDIT_OP_SET_PARAMETER_VALUE:
            if (op->data.set_value.has_parameter_ref) {
                add_ref_json(doc, obj, "parameter_operation",
                             "parameter_handle",
                             op->data.set_value.parameter_ref_operation_index,
                             op->data.set_value.parameter_ref_handle);
            } else {
                yyjson_mut_obj_add_uint(doc, obj, "parameter_id",
                                        (uint64_t)op->primary_id);
            }
            add_str_safe(doc, obj, "value", op->data.set_value.value);
            break;
        case NMO_EDIT_OP_SET_PARAMETER_BYTES: {
            if (op->data.set_bytes.has_parameter_ref) {
                add_ref_json(doc, obj, "parameter_operation",
                             "parameter_handle",
                             op->data.set_bytes.parameter_ref_operation_index,
                             op->data.set_bytes.parameter_ref_handle);
            } else {
                yyjson_mut_obj_add_uint(doc, obj, "parameter_id",
                                        (uint64_t)op->primary_id);
            }
            char *hex = bytes_to_hex(op->data.set_bytes.bytes,
                                     op->data.set_bytes.byte_count);
            if (hex != NULL) {
                yyjson_mut_obj_add_strcpy(doc, obj, "hex", hex);
                free(hex);
            }
            yyjson_mut_obj_add_bool(
                doc, obj, "resize",
                op->data.set_bytes.has_options &&
                    op->data.set_bytes.options.resize);
            break;
        }
        case NMO_EDIT_OP_ADD_NODE:
            yyjson_mut_obj_add_uint(
                doc, obj, "behavior_id",
                (uint64_t)op->data.add_node.parent_behavior_id);
            add_guid_json(doc, obj, "guid", op->data.add_node.bb_guid);
            add_str_safe(doc, obj, "name", op->data.add_node.name);
            break;
        case NMO_EDIT_OP_REMOVE_NODE:
            yyjson_mut_obj_add_uint(
                doc, obj, "parent_id",
                (uint64_t)op->data.remove_node.parent_behavior_id);
            yyjson_mut_obj_add_uint(doc, obj, "node_id",
                                    (uint64_t)op->data.remove_node.node_id);
            yyjson_mut_obj_add_uint(
                doc, obj, "delete_flags",
                (uint64_t)op->data.remove_node.delete_flags);
            break;
        case NMO_EDIT_OP_ADD_IO:
            yyjson_mut_obj_add_uint(doc, obj, "behavior_id",
                                    (uint64_t)op->data.add_io.behavior_id);
            yyjson_mut_obj_add_str(doc, obj, "kind",
                                   io_kind_string(op->data.add_io.kind));
            add_str_safe(doc, obj, "name", op->data.add_io.name);
            break;
        case NMO_EDIT_OP_RENAME_IO:
            yyjson_mut_obj_add_uint(doc, obj, "io_id",
                                    (uint64_t)op->data.rename_io.io_id);
            add_str_safe(doc, obj, "name", op->data.rename_io.name);
            break;
        case NMO_EDIT_OP_REMOVE_IO:
            yyjson_mut_obj_add_uint(doc, obj, "io_id",
                                    (uint64_t)op->data.remove_io.io_id);
            yyjson_mut_obj_add_bool(doc, obj, "detach_links",
                                    op->data.remove_io.detach_links);
            break;
        case NMO_EDIT_OP_ADD_BEHAVIOR_LINK:
            yyjson_mut_obj_add_uint(
                doc, obj, "parent_id",
                (uint64_t)op->data.add_link.parent_behavior_id);
            if (op->data.add_link.has_from_io_ref) {
                add_ref_json(doc, obj, "from_operation", "from_handle",
                             op->data.add_link.from_io_ref_operation_index,
                             op->data.add_link.from_io_ref_handle);
            } else {
                yyjson_mut_obj_add_uint(
                    doc, obj, "from_io_id",
                    (uint64_t)op->data.add_link.from_io_id);
            }
            if (op->data.add_link.has_to_io_ref) {
                add_ref_json(doc, obj, "to_operation", "to_handle",
                             op->data.add_link.to_io_ref_operation_index,
                             op->data.add_link.to_io_ref_handle);
            } else {
                yyjson_mut_obj_add_uint(
                    doc, obj, "to_io_id",
                    (uint64_t)op->data.add_link.to_io_id);
            }
            yyjson_mut_obj_add_uint(
                doc, obj, "activation_delay",
                (uint64_t)op->data.add_link.activation_delay);
            break;
        case NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK:
            yyjson_mut_obj_add_uint(doc, obj, "link_id",
                                    (uint64_t)op->data.rewire_link.link_id);
            yyjson_mut_obj_add_uint(
                doc, obj, "from_io_id",
                (uint64_t)op->data.rewire_link.from_io_id);
            yyjson_mut_obj_add_uint(
                doc, obj, "to_io_id",
                (uint64_t)op->data.rewire_link.to_io_id);
            break;
        case NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY:
            yyjson_mut_obj_add_uint(
                doc, obj, "link_id",
                (uint64_t)op->data.set_link_delay.link_id);
            yyjson_mut_obj_add_uint(
                doc, obj, "activation_delay",
                (uint64_t)op->data.set_link_delay.activation_delay);
            break;
        case NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK:
            yyjson_mut_obj_add_uint(
                doc, obj, "parent_id",
                (uint64_t)op->data.remove_link.parent_behavior_id);
            yyjson_mut_obj_add_uint(
                doc, obj, "link_id",
                (uint64_t)op->data.remove_link.link_id);
            break;
        case NMO_EDIT_OP_ADD_PARAMETER:
            yyjson_mut_obj_add_uint(
                doc, obj, "owner_id",
                (uint64_t)op->data.add_parameter.owner_behavior_id);
            yyjson_mut_obj_add_str(
                doc, obj, "kind",
                parameter_kind_string(op->data.add_parameter.kind));
            add_guid_json(doc, obj, "type_guid",
                          op->data.add_parameter.type_guid);
            add_str_safe(doc, obj, "name", op->data.add_parameter.name);
            break;
        case NMO_EDIT_OP_CONNECT_PARAMETER:
            yyjson_mut_obj_add_uint(
                doc, obj, "source_id",
                (uint64_t)op->data.connect_parameter.source_parameter_id);
            if (op->data.connect_parameter.has_target_parameter_ref) {
                add_ref_json(
                    doc, obj, "target_operation", "target_handle",
                    op->data.connect_parameter
                        .target_parameter_ref_operation_index,
                    op->data.connect_parameter.target_parameter_ref_handle);
            } else {
                yyjson_mut_obj_add_uint(
                    doc, obj, "target_id",
                    (uint64_t)op->data.connect_parameter.target_parameter_id);
            }
            break;
        case NMO_EDIT_OP_DISCONNECT_PARAMETER:
            yyjson_mut_obj_add_uint(
                doc, obj, "target_id",
                (uint64_t)op->data.disconnect_parameter.target_parameter_id);
            break;
        case NMO_EDIT_OP_REMOVE_PARAMETER:
            yyjson_mut_obj_add_uint(
                doc, obj, "parameter_id",
                (uint64_t)op->data.remove_parameter.parameter_id);
            yyjson_mut_obj_add_bool(doc, obj, "detach",
                                    op->data.remove_parameter.detach);
            break;
        case NMO_EDIT_OP_ADD_OPERATION:
            yyjson_mut_obj_add_uint(
                doc, obj, "parent_id",
                (uint64_t)op->data.add_operation.parent_behavior_id);
            add_guid_json(doc, obj, "operation_guid",
                          op->data.add_operation.operation_guid);
            if (op->data.add_operation.has_in1_parameter_ref) {
                add_ref_json(
                    doc, obj, "in1_operation", "in1_handle",
                    op->data.add_operation.in1_parameter_ref_operation_index,
                    op->data.add_operation.in1_parameter_ref_handle);
            } else {
                add_optional_id_json(
                    doc, obj, "in1_id",
                    op->data.add_operation.in1_parameter_id);
            }
            if (op->data.add_operation.has_in2_parameter_ref) {
                add_ref_json(
                    doc, obj, "in2_operation", "in2_handle",
                    op->data.add_operation.in2_parameter_ref_operation_index,
                    op->data.add_operation.in2_parameter_ref_handle);
            } else {
                add_optional_id_json(
                    doc, obj, "in2_id",
                    op->data.add_operation.in2_parameter_id);
            }
            if (op->data.add_operation.has_out_parameter_ref) {
                add_ref_json(
                    doc, obj, "out_operation", "out_handle",
                    op->data.add_operation.out_parameter_ref_operation_index,
                    op->data.add_operation.out_parameter_ref_handle);
            } else {
                add_optional_id_json(
                    doc, obj, "out_id",
                    op->data.add_operation.out_parameter_id);
            }
            break;
        case NMO_EDIT_OP_REWIRE_OPERATION:
            yyjson_mut_obj_add_uint(
                doc, obj, "operation_id",
                (uint64_t)op->data.rewire_operation.operation_id);
            if ((op->data.rewire_operation.slot_flags &
                 NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u) {
                add_optional_id_json(
                    doc, obj, "in1_id",
                    op->data.rewire_operation.in1_parameter_id);
            }
            if ((op->data.rewire_operation.slot_flags &
                 NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u) {
                add_optional_id_json(
                    doc, obj, "in2_id",
                    op->data.rewire_operation.in2_parameter_id);
            }
            if ((op->data.rewire_operation.slot_flags &
                 NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u) {
                add_optional_id_json(
                    doc, obj, "out_id",
                    op->data.rewire_operation.out_parameter_id);
            }
            break;
        case NMO_EDIT_OP_REMOVE_OPERATION:
            yyjson_mut_obj_add_uint(
                doc, obj, "operation_id",
                (uint64_t)op->data.remove_operation.operation_id);
            break;
        case NMO_EDIT_OP_INTERFACE_POLICY:
            yyjson_mut_obj_add_uint(
                doc, obj, "behavior_id",
                (uint64_t)op->data.interface_policy.behavior_id);
            yyjson_mut_obj_add_str(
                doc, obj, "mode",
                script_interface_mode_string(
                    op->data.interface_policy.mode));
            break;
        case NMO_EDIT_OP_SET_DATA_CELL:
            yyjson_mut_obj_add_uint(doc, obj, "dataarray_id",
                                    (uint64_t)op->data.data_cell.dataarray_id);
            yyjson_mut_obj_add_uint(doc, obj, "row",
                                    (uint64_t)op->data.data_cell.row);
            yyjson_mut_obj_add_uint(doc, obj, "col",
                                    (uint64_t)op->data.data_cell.col);
            add_str_safe(doc, obj, "value", op->data.data_cell.value);
            break;
        case NMO_EDIT_OP_FOLD: {
            const nmo_behavior_fold_desc_t *desc = &op->data.fold.desc;
            yyjson_mut_obj_add_uint(doc, obj, "parent_id",
                                    (uint64_t)desc->parent_id);
            yyjson_mut_val *nodes = yyjson_mut_arr(doc);
            if (nodes != NULL) {
                for (size_t i = 0; i < desc->node_count; ++i) {
                    yyjson_mut_arr_add_uint(
                        doc, nodes, (uint64_t)desc->node_ids[i]);
                }
                yyjson_mut_obj_add_val(doc, obj, "nodes", nodes);
            }
            yyjson_mut_obj_add_uint(doc, obj, "anchor_id",
                                    (uint64_t)desc->anchor_id);
            add_guid_json(doc, obj, "guid", desc->block_guid);
            add_str_safe(doc, obj, "name", desc->name);
            yyjson_mut_obj_add_uint(doc, obj, "version",
                                    (uint64_t)desc->block_version);
            yyjson_mut_obj_add_bool(doc, obj, "preserve_boundary",
                                    desc->preserve_boundary);
            yyjson_mut_obj_add_bool(doc, obj, "preserve_links",
                                    desc->preserve_links);
            yyjson_mut_obj_add_bool(doc, obj, "preserve_params",
                                    desc->preserve_params);
            yyjson_mut_obj_add_str(
                doc, obj, "interface",
                fold_interface_mode_string(desc->interface_mode));
            yyjson_mut_obj_add_val(
                doc, obj, "inputs",
                fold_maps_to_json(doc, desc->input_maps,
                                  desc->input_map_count));
            yyjson_mut_obj_add_val(
                doc, obj, "outputs",
                fold_maps_to_json(doc, desc->output_maps,
                                  desc->output_map_count));
            yyjson_mut_obj_add_val(
                doc, obj, "parameters",
                fold_maps_to_json(doc, desc->parameter_maps,
                                  desc->parameter_map_count));
            break;
        }
        case NMO_EDIT_OP_REPLACE_BB:
            yyjson_mut_obj_add_uint(
                doc, obj, "behavior_id",
                (uint64_t)op->data.replace_bb.desc.behavior_id);
            add_str_safe(doc, obj, "name",
                         op->data.replace_bb.desc.name);
            add_guid_json(doc, obj, "guid",
                          op->data.replace_bb.desc.block_guid);
            yyjson_mut_obj_add_uint(
                doc, obj, "version",
                (uint64_t)op->data.replace_bb.desc.block_version);
            yyjson_mut_obj_add_bool(
                doc, obj, "preserve_links",
                op->data.replace_bb.desc.preserve_links);
            yyjson_mut_obj_add_bool(
                doc, obj, "preserve_params",
                op->data.replace_bb.desc.preserve_params);
            break;
    }

    return obj;
}

nmo_status_t nmo_edit_plan_manifest_json_write(
    const nmo_edit_plan_t *plan,
    const char *input_path,
    const char *output_path,
    char **out_json)
{
    if (plan == NULL || out_json == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_json = NULL;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return NMO_ERR_NOMEM;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_val *ops = yyjson_mut_arr(doc);
    if (root == NULL || ops == NULL) {
        yyjson_mut_doc_free(doc);
        return NMO_ERR_NOMEM;
    }
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_uint(doc, root, "version", 2u);
    add_str_safe(doc, root, "input", input_path);
    add_str_safe(doc, root, "output", output_path);

    size_t count = nmo_edit_plan_count(plan);
    for (size_t i = 0; i < count; ++i) {
        const nmo_edit_op_t *op = nmo_edit_plan_get(plan, i);
        yyjson_mut_val *op_obj = edit_op_to_json(doc, op);
        if (op_obj == NULL) {
            yyjson_mut_doc_free(doc);
            return NMO_ERR_NOMEM;
        }
        yyjson_mut_arr_add_val(ops, op_obj);
    }
    yyjson_mut_obj_add_val(doc, root, "operations", ops);

    size_t json_len = 0u;
    char *json = yyjson_mut_write(doc, 0, &json_len);
    yyjson_mut_doc_free(doc);
    if (json == NULL) {
        return NMO_ERR_NOMEM;
    }

    *out_json = json;
    return NMO_OK;
}

void nmo_edit_plan_manifest_json_free(char *json)
{
    free(json);
}
