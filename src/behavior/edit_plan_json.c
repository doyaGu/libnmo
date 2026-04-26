#include "behavior/nmo_edit_plan_json.h"

#include "core/nmo_guid.h"
#include "yyjson.h"

#include <stddef.h>
#include <stdint.h>
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

static bool read_required_u32(yyjson_val *obj,
                              const char *key,
                              uint32_t *out_value,
                              bool allow_zero)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL || !yyjson_is_uint(value) ||
        yyjson_get_uint(value) > UINT32_MAX) {
        return false;
    }
    if (!allow_zero && yyjson_get_uint(value) == 0u) {
        return false;
    }
    *out_value = (uint32_t)yyjson_get_uint(value);
    return true;
}

static bool read_required_string(yyjson_val *obj,
                                 const char *key,
                                 const char **out_value)
{
    yyjson_val *value = yyjson_obj_get(obj, key);
    if (value == NULL || !yyjson_is_str(value) ||
        yyjson_get_str(value)[0] == '\0') {
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

static nmo_status_t parse_add_parameter(yyjson_val *op_obj,
                                        nmo_edit_plan_t *plan)
{
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

static nmo_status_t parse_set_parameter_value(yyjson_val *op_obj,
                                              nmo_edit_plan_t *plan)
{
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
            return NMO_ERR_INVALID_FORMAT;
        }
        return nmo_edit_plan_add_set_parameter_value(
            plan, (nmo_object_id_t)yyjson_get_uint(parameter_id_val),
            value, NULL);
    }

    if (operation_val == NULL || !yyjson_is_uint(operation_val) ||
        yyjson_get_uint(operation_val) == 0u ||
        handle_val == NULL || !yyjson_is_str(handle_val) ||
        yyjson_get_str(handle_val)[0] == '\0') {
        return NMO_ERR_INVALID_FORMAT;
    }
    return nmo_edit_plan_add_set_parameter_value_from_handle(
        plan,
        (size_t)(yyjson_get_uint(operation_val) - 1u),
        yyjson_get_str(handle_val),
        value,
        NULL);
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
        return NMO_ERR_INVALID_FORMAT;
    }
    if (has_id) {
        if (!yyjson_is_uint(id_val) || yyjson_get_uint(id_val) > UINT32_MAX) {
            return NMO_ERR_INVALID_FORMAT;
        }
        *out_id = (nmo_object_id_t)yyjson_get_uint(id_val);
        return NMO_OK;
    }
    if (operation_val == NULL || !yyjson_is_uint(operation_val) ||
        yyjson_get_uint(operation_val) == 0u ||
        handle_val == NULL || !yyjson_is_str(handle_val) ||
        yyjson_get_str(handle_val)[0] == '\0') {
        return NMO_ERR_INVALID_FORMAT;
    }
    *out_operation_index = (size_t)(yyjson_get_uint(operation_val) - 1u);
    *out_handle = yyjson_get_str(handle_val);
    *out_has_ref = true;
    return NMO_OK;
}

static nmo_status_t parse_add_operation(yyjson_val *op_obj,
                                        nmo_edit_plan_t *plan)
{
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
    yyjson_val *version = yyjson_obj_get(root, "version");
    yyjson_val *ops = yyjson_obj_get(root, "operations");
    if (root == NULL || !yyjson_is_obj(root) ||
        version == NULL || !yyjson_is_uint(version) ||
        yyjson_get_uint(version) != 2u ||
        ops == NULL || !yyjson_is_arr(ops)) {
        yyjson_doc_free(doc);
        return NMO_ERR_INVALID_FORMAT;
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t st = nmo_edit_plan_create(&plan);
    if (st != NMO_OK) {
        yyjson_doc_free(doc);
        return st;
    }

    yyjson_val *op_obj = NULL;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(ops, &iter);
    while ((op_obj = yyjson_arr_iter_next(&iter)) != NULL) {
        if (!yyjson_is_obj(op_obj)) {
            st = NMO_ERR_INVALID_FORMAT;
            break;
        }
        const char *op_name = NULL;
        if (!read_required_string(op_obj, "op", &op_name)) {
            st = NMO_ERR_INVALID_FORMAT;
            break;
        }
        if (strcmp(op_name, "add_parameter") == 0) {
            st = parse_add_parameter(op_obj, plan);
        } else if (strcmp(op_name, "set_parameter_value") == 0) {
            st = parse_set_parameter_value(op_obj, plan);
        } else if (strcmp(op_name, "add_operation") == 0) {
            st = parse_add_operation(op_obj, plan);
        } else {
            st = NMO_ERR_NOT_SUPPORTED;
        }
        if (st != NMO_OK) {
            break;
        }
    }

    if (st == NMO_OK) {
        yyjson_val *input = yyjson_obj_get(root, "input");
        yyjson_val *output = yyjson_obj_get(root, "output");
        out_manifest->input_path =
            input != NULL && yyjson_is_str(input)
                ? dup_string(yyjson_get_str(input))
                : NULL;
        out_manifest->output_path =
            output != NULL && yyjson_is_str(output)
                ? dup_string(yyjson_get_str(output))
                : NULL;
        if ((input != NULL && yyjson_is_str(input) &&
             out_manifest->input_path == NULL) ||
            (output != NULL && yyjson_is_str(output) &&
             out_manifest->output_path == NULL)) {
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
