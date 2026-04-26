#include "behavior/nmo_edit_plan_json.h"

#include "core/nmo_guid.h"
#include "yyjson.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static nmo_status_t edit_plan_json_write_root(
    const nmo_edit_plan_t *plan,
    const char *input_path,
    const char *output_path,
    bool include_paths,
    char **out_json)
{
    if (plan == NULL || out_json == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (include_paths &&
        (input_path == NULL || input_path[0] == '\0' ||
         output_path == NULL || output_path[0] == '\0')) {
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
    if (include_paths) {
        add_str_safe(doc, root, "input", input_path);
        add_str_safe(doc, root, "output", output_path);
    }

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

nmo_status_t nmo_edit_plan_json_write(
    const nmo_edit_plan_t *plan,
    char **out_json)
{
    return edit_plan_json_write_root(plan, NULL, NULL, false, out_json);
}

nmo_status_t nmo_edit_plan_manifest_json_write(
    const nmo_edit_plan_t *plan,
    const char *input_path,
    const char *output_path,
    char **out_json)
{
    return edit_plan_json_write_root(
        plan, input_path, output_path, true, out_json);
}

static char *dup_string(const char *value)
{
    if (value == NULL) {
        return NULL;
    }
    size_t len = strlen(value);
    char *copy = (char *)malloc(len + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, value, len + 1u);
    return copy;
}

static bool json_key_allowed(const char *key,
                             const char *const *allowed,
                             size_t allowed_count)
{
    if (key == NULL) {
        return false;
    }
    for (size_t i = 0; i < allowed_count; ++i) {
        if (strcmp(key, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static nmo_status_t reject_unknown_fields(yyjson_val *obj,
                                          const char *where,
                                          const char *const *allowed,
                                          size_t allowed_count)
{
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *key = NULL;
    yyjson_val *val = NULL;
    yyjson_obj_foreach(obj, idx, max, key, val) {
        (void)val;
        const char *name = yyjson_get_str(key);
        if (!json_key_allowed(name, allowed, allowed_count)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Unknown field '%s' in %s",
                             name != NULL ? name : "(null)", where);
        }
    }
    NMO_RETURN_OK();
}

#define RETURN_IF_UNKNOWN_FIELDS(obj, where, allowed) \
    NMO_RETURN_IF_ERROR(reject_unknown_fields( \
        (obj), (where), (allowed), sizeof(allowed) / sizeof((allowed)[0])))

static bool read_required_u32(yyjson_val *obj,
                              const char *key,
                              uint32_t *out_value,
                              bool allow_zero)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL || !yyjson_is_uint(value) ||
        yyjson_get_uint(value) > UINT32_MAX) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Missing or invalid %s", key);
        return false;
    }
    if (!allow_zero && yyjson_get_uint(value) == 0u) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Missing or invalid %s", key);
        return false;
    }
    *out_value = (uint32_t)yyjson_get_uint(value);
    return true;
}

static bool read_optional_u32(yyjson_val *obj,
                              const char *key,
                              uint32_t *out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL) {
        return true;
    }
    if (!yyjson_is_uint(value) || yyjson_get_uint(value) > UINT32_MAX) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid %s", key);
        return false;
    }
    *out_value = (uint32_t)yyjson_get_uint(value);
    return true;
}

static bool read_optional_bool(yyjson_val *obj,
                               const char *key,
                               bool default_value,
                               bool *out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL) {
        *out_value = default_value;
        return true;
    }
    if (!yyjson_is_bool(value)) {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Invalid %s", key);
        return false;
    }
    *out_value = yyjson_get_bool(value);
    return true;
}

static bool read_required_string(yyjson_val *obj,
                                 const char *key,
                                 const char **out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL || !yyjson_is_str(value) ||
        yyjson_get_str(value)[0] == '\0') {
        nmo_last_error_setf(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                            __FILE__, __LINE__,
                            "Missing or invalid %s", key);
        return false;
    }
    *out_value = yyjson_get_str(value);
    return true;
}

static bool parse_parameter_kind(const char *text,
                                 nmo_script_edit_parameter_kind_t *out_kind)
{
    if (text == NULL || out_kind == NULL) {
        return false;
    }
    if (strcmp(text, "in") == 0 || strcmp(text, "input") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_IN;
        return true;
    }
    if (strcmp(text, "out") == 0 || strcmp(text, "output") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_OUT;
        return true;
    }
    if (strcmp(text, "local") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_LOCAL;
        return true;
    }
    if (strcmp(text, "shared") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_SHARED;
        return true;
    }
    return false;
}

static bool parse_io_kind(const char *text, nmo_script_edit_io_kind_t *out_kind)
{
    if (text == NULL || out_kind == NULL) {
        return false;
    }
    if (strcmp(text, "input") == 0 || strcmp(text, "in") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_IO_INPUT;
        return true;
    }
    if (strcmp(text, "output") == 0 || strcmp(text, "out") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_IO_OUTPUT;
        return true;
    }
    return false;
}

static bool parse_script_interface_mode(
    const char *text,
    nmo_script_edit_interface_mode_t *out_mode)
{
    if (text == NULL || out_mode == NULL) {
        return false;
    }
    if (strcmp(text, "preserve") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
        return true;
    }
    if (strcmp(text, "canonicalize") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE;
        return true;
    }
    if (strcmp(text, "remove") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_REMOVE;
        return true;
    }
    return false;
}

static bool parse_fold_interface_mode(
    const char *text,
    nmo_behavior_fold_interface_mode_t *out_mode)
{
    if (text == NULL || out_mode == NULL) {
        return false;
    }
    if (strcmp(text, "preserve") == 0) {
        *out_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE;
        return true;
    }
    if (strcmp(text, "canonicalize") == 0) {
        *out_mode = NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE;
        return true;
    }
    if (strcmp(text, "remove") == 0) {
        *out_mode = NMO_BEHAVIOR_FOLD_INTERFACE_REMOVE;
        return true;
    }
    return false;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static nmo_status_t parse_hex_bytes(const char *hex,
                                    uint8_t **out_bytes,
                                    size_t *out_count)
{
    if (hex == NULL || out_bytes == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    size_t len = strlen(hex);
    if ((len % 2u) != 0u) {
        return NMO_ERR_INVALID_FORMAT;
    }
    size_t count = len / 2u;
    uint8_t *bytes = NULL;
    if (count != 0u) {
        bytes = (uint8_t *)malloc(count);
        if (bytes == NULL) {
            return NMO_ERR_NOMEM;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        int hi = hex_nibble(hex[i * 2u]);
        int lo = hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            free(bytes);
            return NMO_ERR_INVALID_FORMAT;
        }
        bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_bytes = bytes;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t parse_id_array(yyjson_val *arr,
                                   nmo_object_id_t **out_ids,
                                   size_t *out_count)
{
    if (arr == NULL || !yyjson_is_arr(arr) || yyjson_arr_size(arr) == 0u ||
        out_ids == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_FORMAT;
    }
    size_t count = yyjson_arr_size(arr);
    nmo_object_id_t *ids = (nmo_object_id_t *)calloc(count, sizeof(*ids));
    if (ids == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *item = NULL;
    yyjson_arr_foreach(arr, idx, max, item) {
        if (!yyjson_is_uint(item) || yyjson_get_uint(item) == 0u ||
            yyjson_get_uint(item) > UINT32_MAX) {
            free(ids);
            return NMO_ERR_INVALID_FORMAT;
        }
        ids[idx] = (nmo_object_id_t)yyjson_get_uint(item);
    }
    *out_ids = ids;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t parse_add_parameter(yyjson_val *op_obj,
                                        nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "owner_id", "kind", "type_guid", "name",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "add_parameter operation", allowed);
    uint32_t owner_id = 0u;
    const char *kind_text = NULL;
    const char *type_guid_text = NULL;
    const char *name = NULL;
    if (!read_required_u32(op_obj, "owner_id", &owner_id, false) ||
        !read_required_string(op_obj, "kind", &kind_text) ||
        !read_required_string(op_obj, "type_guid", &type_guid_text) ||
        !read_required_string(op_obj, "name", &name)) {
        return NMO_ERR_INVALID_FORMAT;
    }

    nmo_script_edit_parameter_kind_t kind = NMO_SCRIPT_EDIT_PARAM_IN;
    if (!parse_parameter_kind(kind_text, &kind)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    nmo_guid_t type_guid = nmo_guid_parse(type_guid_text);
    if (nmo_guid_is_null(type_guid)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_parameter(
        plan, (nmo_object_id_t)owner_id, kind, type_guid, name);
}

static nmo_status_t parse_add_node(yyjson_val *op_obj,
                                   nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "behavior_id", "guid", "name",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "add_node operation", allowed);
    uint32_t behavior_id = 0u;
    const char *guid_text = NULL;
    const char *name = NULL;
    if (!read_required_u32(op_obj, "behavior_id", &behavior_id, false) ||
        !read_required_string(op_obj, "guid", &guid_text)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *name_val = yyjson_obj_get(op_obj, "name");
    if (name_val != NULL && (!yyjson_is_str(name_val) ||
                             yyjson_get_str(name_val)[0] == '\0')) {
        return NMO_ERR_INVALID_FORMAT;
    }
    name = name_val != NULL ? yyjson_get_str(name_val) : NULL;
    nmo_guid_t guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(guid)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_node(plan, behavior_id, guid, name);
}

static nmo_status_t parse_remove_node(yyjson_val *op_obj,
                                      nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "parent_id", "node_id", "delete_flags",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "remove_node operation", allowed);
    uint32_t parent_id = 0u;
    uint32_t node_id = 0u;
    uint32_t delete_flags = 0u;
    if (!read_required_u32(op_obj, "parent_id", &parent_id, false) ||
        !read_required_u32(op_obj, "node_id", &node_id, false) ||
        !read_optional_u32(op_obj, "delete_flags", &delete_flags)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_remove_node(plan, parent_id, node_id, delete_flags);
}

static nmo_status_t parse_add_io(yyjson_val *op_obj,
                                 nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "behavior_id", "kind", "name",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "add_io operation", allowed);
    uint32_t behavior_id = 0u;
    const char *kind_text = NULL;
    const char *name = NULL;
    nmo_script_edit_io_kind_t kind = NMO_SCRIPT_EDIT_IO_INPUT;
    if (!read_required_u32(op_obj, "behavior_id", &behavior_id, false) ||
        !read_required_string(op_obj, "kind", &kind_text) ||
        !read_required_string(op_obj, "name", &name) ||
        !parse_io_kind(kind_text, &kind)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_io(plan, behavior_id, kind, name);
}

static nmo_status_t parse_rename_io(yyjson_val *op_obj,
                                    nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "io_id", "name",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "rename_io operation", allowed);
    uint32_t io_id = 0u;
    const char *name = NULL;
    if (!read_required_u32(op_obj, "io_id", &io_id, false) ||
        !read_required_string(op_obj, "name", &name)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_rename_io(plan, io_id, name);
}

static nmo_status_t parse_remove_io(yyjson_val *op_obj,
                                    nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "io_id", "detach_links",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "remove_io operation", allowed);
    uint32_t io_id = 0u;
    bool detach_links = false;
    if (!read_required_u32(op_obj, "io_id", &io_id, false) ||
        !read_optional_bool(op_obj, "detach_links", false, &detach_links)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_remove_io(plan, io_id, detach_links);
}

static nmo_status_t parse_set_parameter_value(yyjson_val *op_obj,
                                              nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "parameter_id", "parameter_operation", "parameter_handle",
        "value",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "set_parameter_value operation", allowed);
    const char *value = NULL;
    if (!read_required_string(op_obj, "value", &value)) {
        return NMO_ERR_INVALID_FORMAT;
    }

    yyjson_val *parameter_id_val = yyjson_obj_get(op_obj, "parameter_id");
    yyjson_val *operation_val = yyjson_obj_get(op_obj, "parameter_operation");
    yyjson_val *handle_val = yyjson_obj_get(op_obj, "parameter_handle");
    bool has_id = parameter_id_val != NULL;
    bool has_ref = operation_val != NULL || handle_val != NULL;
    if (has_id == has_ref) {
        return NMO_ERR_INVALID_FORMAT;
    }

    if (has_id) {
        if (!yyjson_is_uint(parameter_id_val) ||
            yyjson_get_uint(parameter_id_val) == 0u ||
            yyjson_get_uint(parameter_id_val) > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Missing or invalid parameter_id");
        }
        return nmo_edit_plan_add_set_parameter_value(
            plan, (nmo_object_id_t)yyjson_get_uint(parameter_id_val),
            value, NULL);
    }

    if (operation_val == NULL || !yyjson_is_uint(operation_val) ||
        yyjson_get_uint(operation_val) == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Missing or invalid parameter_operation");
    }
    if (handle_val == NULL || !yyjson_is_str(handle_val) ||
        yyjson_get_str(handle_val)[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Missing or invalid parameter_handle");
    }
    return nmo_edit_plan_add_set_parameter_value_from_handle(
        plan,
        (size_t)(yyjson_get_uint(operation_val) - 1u),
        yyjson_get_str(handle_val),
        value,
        NULL);
}

static nmo_status_t parse_set_parameter_bytes(yyjson_val *op_obj,
                                              nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "parameter_id", "parameter_operation", "parameter_handle",
        "hex", "resize",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "set_parameter_bytes operation", allowed);
    const char *hex = NULL;
    uint8_t *bytes = NULL;
    size_t byte_count = 0u;
    bool resize = false;
    nmo_status_t st = NMO_OK;
    if (!read_required_string(op_obj, "hex", &hex) ||
        !read_optional_bool(op_obj, "resize", false, &resize)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    st = parse_hex_bytes(hex, &bytes, &byte_count);
    if (st != NMO_OK) {
        return st;
    }
    nmo_parameter_write_options_t options = {
        .resize = resize,
    };
    yyjson_val *parameter_id_val = yyjson_obj_get(op_obj, "parameter_id");
    yyjson_val *operation_val = yyjson_obj_get(op_obj, "parameter_operation");
    yyjson_val *handle_val = yyjson_obj_get(op_obj, "parameter_handle");
    bool has_id = parameter_id_val != NULL;
    bool has_ref = operation_val != NULL || handle_val != NULL;
    if (has_id == has_ref) {
        free(bytes);
        return NMO_ERR_INVALID_FORMAT;
    }
    if (has_id) {
        if (!yyjson_is_uint(parameter_id_val) ||
            yyjson_get_uint(parameter_id_val) == 0u ||
            yyjson_get_uint(parameter_id_val) > UINT32_MAX) {
            free(bytes);
            return NMO_ERR_INVALID_FORMAT;
        }
        st = nmo_edit_plan_add_set_parameter_bytes(
            plan, (nmo_object_id_t)yyjson_get_uint(parameter_id_val),
            bytes, byte_count, &options);
    } else {
        if (operation_val == NULL || !yyjson_is_uint(operation_val) ||
            yyjson_get_uint(operation_val) == 0u ||
            handle_val == NULL || !yyjson_is_str(handle_val) ||
            yyjson_get_str(handle_val)[0] == '\0') {
            free(bytes);
            return NMO_ERR_INVALID_FORMAT;
        }
        st = nmo_edit_plan_add_set_parameter_bytes_from_handle(
            plan, (size_t)(yyjson_get_uint(operation_val) - 1u),
            yyjson_get_str(handle_val), bytes, byte_count, &options);
    }
    free(bytes);
    return st;
}

static nmo_status_t parse_optional_parameter_ref(
    yyjson_val *op_obj,
    const char *id_key,
    const char *operation_key,
    const char *handle_key,
    nmo_object_id_t *out_id,
    size_t *out_operation_index,
    const char **out_handle,
    bool *out_has_ref)
{
    yyjson_val *id_val = yyjson_obj_get(op_obj, id_key);
    yyjson_val *operation_val = yyjson_obj_get(op_obj, operation_key);
    yyjson_val *handle_val = yyjson_obj_get(op_obj, handle_key);
    bool has_id = id_val != NULL;
    bool has_ref = operation_val != NULL || handle_val != NULL;
    *out_id = 0u;
    *out_operation_index = 0u;
    *out_handle = NULL;
    *out_has_ref = false;

    if (!has_id && !has_ref) {
        return NMO_OK;
    }
    if (has_id && has_ref) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "add_operation operation requires either %s or %s plus %s",
                         id_key, operation_key, handle_key);
    }
    if (has_id) {
        if (!yyjson_is_uint(id_val) || yyjson_get_uint(id_val) > UINT32_MAX) {
            return NMO_ERR_INVALID_FORMAT;
        }
        *out_id = (nmo_object_id_t)yyjson_get_uint(id_val);
        return NMO_OK;
    }
    if (operation_val == NULL || !yyjson_is_uint(operation_val) ||
        yyjson_get_uint(operation_val) == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Missing or invalid %s", operation_key);
    }
    if (handle_val == NULL || !yyjson_is_str(handle_val) ||
        yyjson_get_str(handle_val)[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Missing or invalid %s", handle_key);
    }
    *out_operation_index = (size_t)(yyjson_get_uint(operation_val) - 1u);
    *out_handle = yyjson_get_str(handle_val);
    *out_has_ref = true;
    return NMO_OK;
}

static nmo_status_t parse_add_behavior_link(yyjson_val *op_obj,
                                            nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "parent_id", "from_io_id", "from_operation", "from_handle",
        "to_io_id", "to_operation", "to_handle", "activation_delay",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "add_behavior_link operation", allowed);
    uint32_t parent_id = 0u;
    uint32_t activation_delay = 0u;
    if (!read_required_u32(op_obj, "parent_id", &parent_id, false) ||
        !read_optional_u32(op_obj, "activation_delay", &activation_delay)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *from_id_val = yyjson_obj_get(op_obj, "from_io_id");
    yyjson_val *from_operation_val = yyjson_obj_get(op_obj, "from_operation");
    yyjson_val *from_handle_val = yyjson_obj_get(op_obj, "from_handle");
    yyjson_val *to_id_val = yyjson_obj_get(op_obj, "to_io_id");
    yyjson_val *to_operation_val = yyjson_obj_get(op_obj, "to_operation");
    yyjson_val *to_handle_val = yyjson_obj_get(op_obj, "to_handle");
    bool has_from_id = from_id_val != NULL;
    bool has_from_ref = from_operation_val != NULL || from_handle_val != NULL;
    bool has_to_id = to_id_val != NULL;
    bool has_to_ref = to_operation_val != NULL || to_handle_val != NULL;
    if (has_from_id == has_from_ref) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "add_behavior_link requires either from_io_id or from_operation plus from_handle");
    }
    if (has_to_id == has_to_ref) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "add_behavior_link requires either to_io_id or to_operation plus to_handle");
    }
    if (has_from_ref) {
        if (from_operation_val == NULL || !yyjson_is_uint(from_operation_val) ||
            yyjson_get_uint(from_operation_val) == 0u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Missing or invalid from_operation");
        }
        if (from_handle_val == NULL || !yyjson_is_str(from_handle_val) ||
            yyjson_get_str(from_handle_val)[0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Missing or invalid from_handle");
        }
        if (has_to_ref) {
            if (to_operation_val == NULL || !yyjson_is_uint(to_operation_val) ||
                yyjson_get_uint(to_operation_val) == 0u) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Missing or invalid to_operation");
            }
            if (to_handle_val == NULL || !yyjson_is_str(to_handle_val) ||
                yyjson_get_str(to_handle_val)[0] == '\0') {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Missing or invalid to_handle");
            }
            return nmo_edit_plan_add_behavior_link_from_handles(
                plan, parent_id,
                (size_t)(yyjson_get_uint(from_operation_val) - 1u),
                yyjson_get_str(from_handle_val),
                (size_t)(yyjson_get_uint(to_operation_val) - 1u),
                yyjson_get_str(to_handle_val),
                activation_delay);
        }
        if (!yyjson_is_uint(to_id_val) || yyjson_get_uint(to_id_val) == 0u ||
            yyjson_get_uint(to_id_val) > UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Missing or invalid to_io_id");
        }
        return nmo_edit_plan_add_behavior_link_from_handle(
            plan, parent_id,
            (size_t)(yyjson_get_uint(from_operation_val) - 1u),
            yyjson_get_str(from_handle_val),
            (nmo_object_id_t)yyjson_get_uint(to_id_val),
            activation_delay);
    }
    if (!yyjson_is_uint(from_id_val) || yyjson_get_uint(from_id_val) == 0u ||
        yyjson_get_uint(from_id_val) > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Missing or invalid from_io_id");
    }
    if (has_to_ref) {
        if (to_operation_val == NULL || !yyjson_is_uint(to_operation_val) ||
            yyjson_get_uint(to_operation_val) == 0u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Missing or invalid to_operation");
        }
        if (to_handle_val == NULL || !yyjson_is_str(to_handle_val) ||
            yyjson_get_str(to_handle_val)[0] == '\0') {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Missing or invalid to_handle");
        }
        return nmo_edit_plan_add_behavior_link_to_handle(
            plan, parent_id,
            (nmo_object_id_t)yyjson_get_uint(from_id_val),
            (size_t)(yyjson_get_uint(to_operation_val) - 1u),
            yyjson_get_str(to_handle_val),
            activation_delay);
    }
    if (!yyjson_is_uint(to_id_val) || yyjson_get_uint(to_id_val) == 0u ||
        yyjson_get_uint(to_id_val) > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Missing or invalid to_io_id");
    }
    return nmo_edit_plan_add_behavior_link(
        plan, parent_id,
        (nmo_object_id_t)yyjson_get_uint(from_id_val),
        (nmo_object_id_t)yyjson_get_uint(to_id_val),
        activation_delay);
}

static nmo_status_t parse_rewire_behavior_link(yyjson_val *op_obj,
                                               nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "link_id", "from_io_id", "to_io_id",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "rewire_behavior_link operation", allowed);
    uint32_t link_id = 0u;
    uint32_t from_io_id = 0u;
    uint32_t to_io_id = 0u;
    if (!read_required_u32(op_obj, "link_id", &link_id, false) ||
        !read_required_u32(op_obj, "from_io_id", &from_io_id, true) ||
        !read_required_u32(op_obj, "to_io_id", &to_io_id, true)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_rewire_behavior_link(
        plan, link_id, from_io_id, to_io_id);
}

static nmo_status_t parse_set_behavior_link_delay(yyjson_val *op_obj,
                                                  nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "link_id", "activation_delay",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "set_behavior_link_delay operation", allowed);
    uint32_t link_id = 0u;
    uint32_t activation_delay = 0u;
    if (!read_required_u32(op_obj, "link_id", &link_id, false) ||
        !read_required_u32(op_obj, "activation_delay", &activation_delay, true)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_set_behavior_link_delay(
        plan, link_id, activation_delay);
}

static nmo_status_t parse_remove_behavior_link(yyjson_val *op_obj,
                                               nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "parent_id", "link_id",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "remove_behavior_link operation", allowed);
    uint32_t parent_id = 0u;
    uint32_t link_id = 0u;
    if (!read_required_u32(op_obj, "parent_id", &parent_id, false) ||
        !read_required_u32(op_obj, "link_id", &link_id, false)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_remove_behavior_link(plan, parent_id, link_id);
}

static nmo_status_t parse_connect_parameter(yyjson_val *op_obj,
                                            nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "source_id", "target_id", "target_operation", "target_handle",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "connect_parameter operation", allowed);
    uint32_t source_id = 0u;
    if (!read_required_u32(op_obj, "source_id", &source_id, false)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *target_id_val = yyjson_obj_get(op_obj, "target_id");
    yyjson_val *operation_val = yyjson_obj_get(op_obj, "target_operation");
    yyjson_val *handle_val = yyjson_obj_get(op_obj, "target_handle");
    bool has_id = target_id_val != NULL;
    bool has_ref = operation_val != NULL || handle_val != NULL;
    if (has_id == has_ref) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "connect_parameter requires either target_id or target_operation plus target_handle");
    }
    if (has_id) {
        if (!yyjson_is_uint(target_id_val) ||
            yyjson_get_uint(target_id_val) == 0u ||
            yyjson_get_uint(target_id_val) > UINT32_MAX) {
            return NMO_ERR_INVALID_FORMAT;
        }
        return nmo_edit_plan_add_connect_parameter(
            plan, source_id, (nmo_object_id_t)yyjson_get_uint(target_id_val));
    }
    if (operation_val == NULL || !yyjson_is_uint(operation_val) ||
        yyjson_get_uint(operation_val) == 0u ||
        handle_val == NULL || !yyjson_is_str(handle_val) ||
        yyjson_get_str(handle_val)[0] == '\0') {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_connect_parameter_to_handle(
        plan, source_id,
        (size_t)(yyjson_get_uint(operation_val) - 1u),
        yyjson_get_str(handle_val));
}

static nmo_status_t parse_disconnect_parameter(yyjson_val *op_obj,
                                               nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "target_id",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "disconnect_parameter operation", allowed);
    uint32_t target_id = 0u;
    if (!read_required_u32(op_obj, "target_id", &target_id, false)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_disconnect_parameter(plan, target_id);
}

static nmo_status_t parse_remove_parameter(yyjson_val *op_obj,
                                           nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "parameter_id", "detach",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "remove_parameter operation", allowed);
    uint32_t parameter_id = 0u;
    bool detach = false;
    if (!read_required_u32(op_obj, "parameter_id", &parameter_id, false) ||
        !read_optional_bool(op_obj, "detach", false, &detach)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_remove_parameter(plan, parameter_id, detach);
}

static nmo_status_t parse_add_operation(yyjson_val *op_obj,
                                        nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "parent_id", "operation_guid",
        "in1_id", "in1_operation", "in1_handle",
        "in2_id", "in2_operation", "in2_handle",
        "out_id", "out_operation", "out_handle",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "add_operation operation", allowed);
    uint32_t parent_id = 0u;
    const char *operation_guid_text = NULL;
    if (!read_required_u32(op_obj, "parent_id", &parent_id, false) ||
        !read_required_string(op_obj, "operation_guid",
                              &operation_guid_text)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    nmo_guid_t operation_guid = nmo_guid_parse(operation_guid_text);
    if (nmo_guid_is_null(operation_guid)) {
        return NMO_ERR_INVALID_FORMAT;
    }

    nmo_object_id_t in1_id = 0u;
    nmo_object_id_t in2_id = 0u;
    nmo_object_id_t out_id = 0u;
    size_t in1_operation_index = 0u;
    size_t in2_operation_index = 0u;
    size_t out_operation_index = 0u;
    const char *in1_handle = NULL;
    const char *in2_handle = NULL;
    const char *out_handle = NULL;
    bool has_in1_ref = false;
    bool has_in2_ref = false;
    bool has_out_ref = false;

    nmo_status_t st = parse_optional_parameter_ref(
        op_obj, "in1_id", "in1_operation", "in1_handle",
        &in1_id, &in1_operation_index, &in1_handle, &has_in1_ref);
    if (st != NMO_OK) {
        return st;
    }
    st = parse_optional_parameter_ref(
        op_obj, "in2_id", "in2_operation", "in2_handle",
        &in2_id, &in2_operation_index, &in2_handle, &has_in2_ref);
    if (st != NMO_OK) {
        return st;
    }
    st = parse_optional_parameter_ref(
        op_obj, "out_id", "out_operation", "out_handle",
        &out_id, &out_operation_index, &out_handle, &has_out_ref);
    if (st != NMO_OK) {
        return st;
    }

    if (has_in1_ref || has_in2_ref || has_out_ref) {
        return nmo_edit_plan_add_operation_with_refs(
            plan,
            (nmo_object_id_t)parent_id,
            operation_guid,
            in1_id,
            in1_operation_index,
            in1_handle,
            in2_id,
            in2_operation_index,
            in2_handle,
            out_id,
            out_operation_index,
            out_handle);
    }
    return nmo_edit_plan_add_operation(
        plan, (nmo_object_id_t)parent_id, operation_guid,
        in1_id, in2_id, out_id);
}

static nmo_status_t parse_rewire_operation(yyjson_val *op_obj,
                                           nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "operation_id", "in1_id", "in2_id", "out_id",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "rewire_operation operation", allowed);
    uint32_t operation_id = 0u;
    uint32_t in1_id = 0u;
    uint32_t in2_id = 0u;
    uint32_t out_id = 0u;
    uint32_t slot_flags = 0u;
    if (!read_required_u32(op_obj, "operation_id", &operation_id, false)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    if (yyjson_obj_get(op_obj, "in1_id") != NULL) {
        if (!read_required_u32(op_obj, "in1_id", &in1_id, true)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN1;
    }
    if (yyjson_obj_get(op_obj, "in2_id") != NULL) {
        if (!read_required_u32(op_obj, "in2_id", &in2_id, true)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN2;
    }
    if (yyjson_obj_get(op_obj, "out_id") != NULL) {
        if (!read_required_u32(op_obj, "out_id", &out_id, true)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_OUT;
    }
    if (slot_flags == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "rewire_operation requires in1_id, in2_id, or out_id");
    }
    return nmo_edit_plan_add_rewire_operation(
        plan, operation_id, slot_flags, in1_id, in2_id, out_id);
}

static nmo_status_t parse_remove_operation(yyjson_val *op_obj,
                                           nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "operation_id",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "remove_operation operation", allowed);
    uint32_t operation_id = 0u;
    if (!read_required_u32(op_obj, "operation_id", &operation_id, false)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_remove_operation(plan, operation_id);
}

static nmo_status_t parse_interface_policy(yyjson_val *op_obj,
                                           nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "behavior_id", "mode",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "interface_policy operation", allowed);
    uint32_t behavior_id = 0u;
    const char *mode_text = NULL;
    nmo_script_edit_interface_mode_t mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    if (!read_required_u32(op_obj, "behavior_id", &behavior_id, false) ||
        !read_required_string(op_obj, "mode", &mode_text) ||
        !parse_script_interface_mode(mode_text, &mode)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_interface_policy(plan, behavior_id, mode);
}

static nmo_status_t parse_set_data_cell(yyjson_val *op_obj,
                                        nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "dataarray_id", "row", "col", "value",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "set_data_cell operation", allowed);
    uint32_t dataarray_id = 0u;
    uint32_t row = 0u;
    uint32_t col = 0u;
    const char *value = NULL;
    if (!read_required_u32(op_obj, "dataarray_id", &dataarray_id, false) ||
        !read_required_u32(op_obj, "row", &row, true) ||
        !read_required_u32(op_obj, "col", &col, true) ||
        !read_required_string(op_obj, "value", &value)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_data_cell(plan, dataarray_id, row, col, value);
}

static nmo_status_t parse_fold_maps(yyjson_val *arr,
                                    nmo_behavior_fold_map_kind_t kind,
                                    nmo_behavior_fold_map_t **out_maps,
                                    size_t *out_count)
{
    *out_maps = NULL;
    *out_count = 0u;
    if (arr == NULL) {
        return NMO_OK;
    }
    if (!yyjson_is_arr(arr)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    size_t count = yyjson_arr_size(arr);
    if (count == 0u) {
        return NMO_OK;
    }
    nmo_behavior_fold_map_t *maps =
        (nmo_behavior_fold_map_t *)calloc(count, sizeof(*maps));
    if (maps == NULL) {
        return NMO_ERR_NOMEM;
    }
    size_t idx = 0u;
    size_t max = 0u;
    yyjson_val *item = NULL;
    yyjson_arr_foreach(arr, idx, max, item) {
        static const char *const allowed[] = {
            "old_index", "new_index", "old_id", "new_id",
            "old_io_id", "new_io_id",
            "old_parameter_id", "new_parameter_id", "label",
        };
        uint32_t old_index = 0u;
        uint32_t new_index = 0u;
        uint32_t old_id = 0u;
        uint32_t new_id = 0u;
        if (!yyjson_is_obj(item) ||
            reject_unknown_fields(
                item, "fold map", allowed,
                sizeof(allowed) / sizeof(allowed[0])) != NMO_OK ||
            !read_required_u32(item, "old_index", &old_index, true) ||
            !read_required_u32(item, "new_index", &new_index, true)) {
            free(maps);
            return NMO_ERR_INVALID_FORMAT;
        }
        const char *old_id_key =
            yyjson_obj_get(item, "old_id") != NULL ? "old_id" :
            (kind == NMO_BEHAVIOR_FOLD_MAP_PARAMETER
                 ? "old_parameter_id"
                 : "old_io_id");
        const char *new_id_key =
            yyjson_obj_get(item, "new_id") != NULL ? "new_id" :
            (kind == NMO_BEHAVIOR_FOLD_MAP_PARAMETER
                 ? "new_parameter_id"
                 : "new_io_id");
        if (!read_optional_u32(item, old_id_key, &old_id) ||
            !read_optional_u32(item, new_id_key, &new_id)) {
            free(maps);
            return NMO_ERR_INVALID_FORMAT;
        }
        maps[idx].kind = kind;
        maps[idx].old_index = old_index;
        maps[idx].new_index = new_index;
        maps[idx].old_id = old_id;
        maps[idx].new_id = new_id;
        yyjson_val *label_val = yyjson_obj_get(item, "label");
        maps[idx].label =
            label_val != NULL && yyjson_is_str(label_val)
                ? yyjson_get_str(label_val)
                : NULL;
    }
    *out_maps = maps;
    *out_count = count;
    return NMO_OK;
}

static nmo_status_t parse_fold(yyjson_val *op_obj,
                               nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "parent_id", "nodes", "anchor_id", "guid", "name",
        "version", "preserve_boundary", "preserve_links",
        "preserve_params", "interface", "inputs", "outputs",
        "parameters",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "fold operation", allowed);
    nmo_behavior_fold_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_object_id_t *node_ids = NULL;
    nmo_behavior_fold_map_t *input_maps = NULL;
    nmo_behavior_fold_map_t *output_maps = NULL;
    nmo_behavior_fold_map_t *parameter_maps = NULL;
    const char *guid_text = NULL;
    const char *mode_text = NULL;
    bool preserve_boundary = false;
    bool preserve_links = false;
    bool preserve_params = false;

    nmo_status_t st = parse_id_array(
        yyjson_obj_get(op_obj, "nodes"), &node_ids, &desc.node_count);
    if (st != NMO_OK) {
        return st;
    }
    desc.node_ids = node_ids;
    if (!read_required_u32(op_obj, "parent_id", &desc.parent_id, false) ||
        !read_required_u32(op_obj, "anchor_id", &desc.anchor_id, false) ||
        !read_required_string(op_obj, "guid", &guid_text) ||
        !read_required_string(op_obj, "name", &desc.name) ||
        !read_optional_u32(op_obj, "version", &desc.block_version) ||
        !read_optional_bool(op_obj, "preserve_boundary", false,
                            &preserve_boundary) ||
        !read_optional_bool(op_obj, "preserve_links", false,
                            &preserve_links) ||
        !read_optional_bool(op_obj, "preserve_params", false,
                            &preserve_params)) {
        free(node_ids);
        return NMO_ERR_INVALID_FORMAT;
    }
    desc.preserve_boundary = preserve_boundary;
    desc.preserve_links = preserve_links;
    desc.preserve_params = preserve_params;
    desc.block_guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(desc.block_guid)) {
        free(node_ids);
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *mode_val = yyjson_obj_get(op_obj, "interface");
    if (mode_val != NULL) {
        if (!yyjson_is_str(mode_val) ||
            !parse_fold_interface_mode(yyjson_get_str(mode_val),
                                       &desc.interface_mode)) {
            free(node_ids);
            return NMO_ERR_INVALID_FORMAT;
        }
    }
    st = parse_fold_maps(yyjson_obj_get(op_obj, "inputs"),
                         NMO_BEHAVIOR_FOLD_MAP_INPUT,
                         &input_maps, &desc.input_map_count);
    if (st == NMO_OK) {
        st = parse_fold_maps(yyjson_obj_get(op_obj, "outputs"),
                             NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
                             &output_maps, &desc.output_map_count);
    }
    if (st == NMO_OK) {
        st = parse_fold_maps(yyjson_obj_get(op_obj, "parameters"),
                             NMO_BEHAVIOR_FOLD_MAP_PARAMETER,
                             &parameter_maps, &desc.parameter_map_count);
    }
    if (st == NMO_OK) {
        desc.input_maps = input_maps;
        desc.output_maps = output_maps;
        desc.parameter_maps = parameter_maps;
        st = nmo_edit_plan_add_fold(plan, &desc);
    }
    (void)mode_text;
    free(parameter_maps);
    free(output_maps);
    free(input_maps);
    free(node_ids);
    return st;
}

static nmo_status_t parse_replace_bb(yyjson_val *op_obj,
                                     nmo_edit_plan_t *plan)
{
    static const char *const allowed[] = {
        "op", "behavior_id", "guid", "name", "version",
        "preserve_links", "preserve_params",
    };
    RETURN_IF_UNKNOWN_FIELDS(op_obj, "replace_bb operation", allowed);
    nmo_behavior_replace_bb_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    const char *guid_text = NULL;
    bool preserve_links = false;
    bool preserve_params = false;
    if (!read_required_u32(op_obj, "behavior_id", &desc.behavior_id, false) ||
        !read_required_string(op_obj, "guid", &guid_text) ||
        !read_required_string(op_obj, "name", &desc.name) ||
        !read_optional_u32(op_obj, "version", &desc.block_version) ||
        !read_optional_bool(op_obj, "preserve_links", false,
                            &preserve_links) ||
        !read_optional_bool(op_obj, "preserve_params", false,
                            &preserve_params)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    desc.preserve_links = preserve_links;
    desc.preserve_params = preserve_params;
    desc.block_guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(desc.block_guid)) {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_replace_bb(plan, &desc);
}

static nmo_status_t parse_operations_array(yyjson_val *ops,
                                           nmo_edit_plan_t *plan)
{
    yyjson_val *op_obj = NULL;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(ops, &iter);
    while ((op_obj = yyjson_arr_iter_next(&iter)) != NULL) {
        nmo_status_t st = NMO_OK;
        if (!yyjson_is_obj(op_obj)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        const char *op_name = NULL;
        if (!read_required_string(op_obj, "op", &op_name)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        if (strcmp(op_name, "set_parameter_value") == 0) {
            st = parse_set_parameter_value(op_obj, plan);
        } else if (strcmp(op_name, "set_parameter_bytes") == 0) {
            st = parse_set_parameter_bytes(op_obj, plan);
        } else if (strcmp(op_name, "add_node") == 0) {
            st = parse_add_node(op_obj, plan);
        } else if (strcmp(op_name, "remove_node") == 0) {
            st = parse_remove_node(op_obj, plan);
        } else if (strcmp(op_name, "add_io") == 0) {
            st = parse_add_io(op_obj, plan);
        } else if (strcmp(op_name, "rename_io") == 0) {
            st = parse_rename_io(op_obj, plan);
        } else if (strcmp(op_name, "remove_io") == 0) {
            st = parse_remove_io(op_obj, plan);
        } else if (strcmp(op_name, "add_behavior_link") == 0) {
            st = parse_add_behavior_link(op_obj, plan);
        } else if (strcmp(op_name, "rewire_behavior_link") == 0) {
            st = parse_rewire_behavior_link(op_obj, plan);
        } else if (strcmp(op_name, "set_behavior_link_delay") == 0) {
            st = parse_set_behavior_link_delay(op_obj, plan);
        } else if (strcmp(op_name, "remove_behavior_link") == 0) {
            st = parse_remove_behavior_link(op_obj, plan);
        } else if (strcmp(op_name, "add_parameter") == 0) {
            st = parse_add_parameter(op_obj, plan);
        } else if (strcmp(op_name, "connect_parameter") == 0) {
            st = parse_connect_parameter(op_obj, plan);
        } else if (strcmp(op_name, "disconnect_parameter") == 0) {
            st = parse_disconnect_parameter(op_obj, plan);
        } else if (strcmp(op_name, "remove_parameter") == 0) {
            st = parse_remove_parameter(op_obj, plan);
        } else if (strcmp(op_name, "add_operation") == 0) {
            st = parse_add_operation(op_obj, plan);
        } else if (strcmp(op_name, "rewire_operation") == 0) {
            st = parse_rewire_operation(op_obj, plan);
        } else if (strcmp(op_name, "remove_operation") == 0) {
            st = parse_remove_operation(op_obj, plan);
        } else if (strcmp(op_name, "interface_policy") == 0) {
            st = parse_interface_policy(op_obj, plan);
        } else if (strcmp(op_name, "set_data_cell") == 0) {
            st = parse_set_data_cell(op_obj, plan);
        } else if (strcmp(op_name, "fold") == 0) {
            st = parse_fold(op_obj, plan);
        } else if (strcmp(op_name, "replace_bb") == 0) {
            st = parse_replace_bb(op_obj, plan);
        } else {
            nmo_last_error_setf(NMO_ERR_NOT_SUPPORTED, NMO_SEVERITY_ERROR,
                                __FILE__, __LINE__,
                                "Unsupported edit plan op '%s'", op_name);
            st = NMO_ERR_NOT_SUPPORTED;
        }
        if (st != NMO_OK) {
            return st;
        }
    }
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_json_read(
    const char *json,
    size_t json_len,
    nmo_edit_plan_t **out_plan)
{
    if (json == NULL || out_plan == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_plan = NULL;

    yyjson_doc *doc = yyjson_read(json, json_len, 0);
    if (doc == NULL) {
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan root must be an object");
    }
    yyjson_val *version = yyjson_obj_get(root, "version");
    yyjson_val *ops = yyjson_obj_get(root, "operations");
    if (version == NULL || !yyjson_is_uint(version) ||
        yyjson_get_uint(version) != 2u) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan version 2 is required");
    }
    if (ops == NULL || !yyjson_is_arr(ops) ||
        yyjson_arr_size(ops) == 0u) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan operations must be a non-empty array");
    }
    static const char *const root_allowed[] = {
        "version", "operations",
    };
    nmo_status_t st = reject_unknown_fields(
        root, "edit plan root", root_allowed,
        sizeof(root_allowed) / sizeof(root_allowed[0]));
    if (st != NMO_OK) {
        yyjson_doc_free(doc);
        return st;
    }

    nmo_edit_plan_t *plan = NULL;
    st = nmo_edit_plan_create(&plan);
    if (st == NMO_OK) {
        st = parse_operations_array(ops, plan);
    }
    yyjson_doc_free(doc);
    if (st != NMO_OK) {
        nmo_edit_plan_destroy(plan);
        return st;
    }
    *out_plan = plan;
    return NMO_OK;
}

nmo_status_t nmo_edit_plan_manifest_json_read(
    const char *json,
    size_t json_len,
    nmo_edit_plan_manifest_t *out_manifest)
{
    if (json == NULL || out_manifest == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(out_manifest, 0, sizeof(*out_manifest));

    yyjson_doc *doc = yyjson_read(json, json_len, 0);
    if (doc == NULL) {
        return NMO_ERR_INVALID_FORMAT;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (root == NULL || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest root must be an object");
    }
    yyjson_val *version = yyjson_obj_get(root, "version");
    yyjson_val *input = yyjson_obj_get(root, "input");
    yyjson_val *output = yyjson_obj_get(root, "output");
    yyjson_val *ops = yyjson_obj_get(root, "operations");
    if (version == NULL || !yyjson_is_uint(version) ||
        yyjson_get_uint(version) != 2u) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest version 2 is required");
    }
    if (input == NULL || !yyjson_is_str(input) ||
        yyjson_get_str(input)[0] == '\0') {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest requires input");
    }
    if (output == NULL || !yyjson_is_str(output) ||
        yyjson_get_str(output)[0] == '\0') {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest requires output");
    }
    if (ops == NULL || !yyjson_is_arr(ops) ||
        yyjson_arr_size(ops) == 0u) {
        yyjson_doc_free(doc);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Edit plan manifest operations must be a non-empty array");
    }
    static const char *const root_allowed[] = {
        "version", "input", "output", "operations",
    };
    nmo_status_t st = reject_unknown_fields(
        root, "edit plan manifest root", root_allowed,
        sizeof(root_allowed) / sizeof(root_allowed[0]));
    if (st != NMO_OK) {
        yyjson_doc_free(doc);
        return st;
    }

    nmo_edit_plan_t *plan = NULL;
    st = nmo_edit_plan_create(&plan);
    if (st != NMO_OK) {
        yyjson_doc_free(doc);
        return st;
    }

    st = parse_operations_array(ops, plan);

    if (st == NMO_OK) {
        out_manifest->input_path = dup_string(yyjson_get_str(input));
        out_manifest->output_path = dup_string(yyjson_get_str(output));
        if (out_manifest->input_path == NULL ||
            out_manifest->output_path == NULL) {
            st = NMO_ERR_NOMEM;
        } else {
            out_manifest->plan = plan;
            plan = NULL;
        }
    }

    nmo_edit_plan_destroy(plan);
    yyjson_doc_free(doc);
    if (st != NMO_OK) {
        nmo_edit_plan_manifest_dispose(out_manifest);
    }
    return st;
}

nmo_status_t nmo_edit_plan_manifest_json_read_file(
    const char *path,
    nmo_edit_plan_manifest_t *out_manifest)
{
    FILE *fp = NULL;
    long size = 0;
    char *json = NULL;
    size_t bytes_read = 0u;
    nmo_status_t st = NMO_OK;

    if (path == NULL || out_manifest == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(out_manifest, 0, sizeof(*out_manifest));

    fp = fopen(path, "rb");
    if (fp == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_OPEN_FILE, NMO_SEVERITY_ERROR,
                         "Failed to open edit plan manifest file: %s", path);
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to seek edit plan manifest file: %s", path);
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to size edit plan manifest file: %s", path);
    }
    rewind(fp);

    json = (char *)malloc((size_t)size + 1u);
    if (json == NULL) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate edit plan manifest buffer");
    }
    bytes_read = fread(json, 1u, (size_t)size, fp);
    fclose(fp);
    if (bytes_read != (size_t)size) {
        free(json);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to read edit plan manifest file: %s", path);
    }
    json[bytes_read] = '\0';

    st = nmo_edit_plan_manifest_json_read(json, bytes_read, out_manifest);
    free(json);
    return st;
}

void nmo_edit_plan_manifest_dispose(nmo_edit_plan_manifest_t *manifest)
{
    if (manifest == NULL) {
        return;
    }
    free(manifest->input_path);
    free(manifest->output_path);
    nmo_edit_plan_destroy(manifest->plan);
    memset(manifest, 0, sizeof(*manifest));
}

void nmo_edit_plan_manifest_json_free(char *json)
{
    free(json);
}
