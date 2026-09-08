/**
 * @file edit_op_kind.c
 * @brief Canonical edit operation kind metadata Implementation.
 */

#include "behavior/nmo_edit_plan.h"

#include "edit_op_kind_internal.h"

#include <stddef.h>
#include <string.h>

typedef struct nmo_edit_op_kind_descriptor {
    nmo_edit_op_kind_t kind;
    const char *name;
    const char *result_handle;
} nmo_edit_op_kind_descriptor_t;

static const nmo_edit_op_kind_descriptor_t EDIT_OP_KINDS[] = {
    {NMO_EDIT_OP_SET_PARAMETER_VALUE, "set_parameter_value", NULL},
    {NMO_EDIT_OP_SET_PARAMETER_BYTES, "set_parameter_bytes", NULL},
    {NMO_EDIT_OP_ADD_NODE, "add_node", "node"},
    {NMO_EDIT_OP_REMOVE_NODE, "remove_node", NULL},
    {NMO_EDIT_OP_ADD_IO, "add_io", "io"},
    {NMO_EDIT_OP_RENAME_IO, "rename_io", NULL},
    {NMO_EDIT_OP_REMOVE_IO, "remove_io", NULL},
    {NMO_EDIT_OP_ADD_BEHAVIOR_LINK, "add_behavior_link", "link"},
    {NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK, "rewire_behavior_link", NULL},
    {NMO_EDIT_OP_SET_BEHAVIOR_LINK_DELAY, "set_behavior_link_delay", NULL},
    {NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK, "remove_behavior_link", NULL},
    {NMO_EDIT_OP_ADD_PARAMETER, "add_parameter", "parameter"},
    {NMO_EDIT_OP_CONNECT_PARAMETER, "connect_parameter", NULL},
    {NMO_EDIT_OP_DISCONNECT_PARAMETER, "disconnect_parameter", NULL},
    {NMO_EDIT_OP_REMOVE_PARAMETER, "remove_parameter", NULL},
    {NMO_EDIT_OP_ADD_OPERATION, "add_operation", "operation"},
    {NMO_EDIT_OP_REWIRE_OPERATION, "rewire_operation", NULL},
    {NMO_EDIT_OP_REMOVE_OPERATION, "remove_operation", NULL},
    {NMO_EDIT_OP_INTERFACE_POLICY, "interface_policy", NULL},
    {NMO_EDIT_OP_SET_DATA_CELL, "set_data_cell", NULL},
    {NMO_EDIT_OP_FOLD, "fold", NULL},
    {NMO_EDIT_OP_REPLACE_BB, "replace_bb", NULL},
};

static const nmo_edit_op_kind_descriptor_t *edit_op_kind_find(
    nmo_edit_op_kind_t kind)
{
    for (size_t i = 0u; i < sizeof(EDIT_OP_KINDS) / sizeof(EDIT_OP_KINDS[0]);
         ++i) {
        if (EDIT_OP_KINDS[i].kind == kind) {
            return &EDIT_OP_KINDS[i];
        }
    }
    return NULL;
}

const char *nmo_edit_op_kind_name(nmo_edit_op_kind_t kind)
{
    const nmo_edit_op_kind_descriptor_t *descriptor = edit_op_kind_find(kind);
    return descriptor != NULL ? descriptor->name : "unknown";
}

nmo_status_t nmo_edit_op_kind_parse(
    const char *name,
    nmo_edit_op_kind_t *out_kind)
{
    if (name == NULL || out_kind == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_kind = (nmo_edit_op_kind_t)0;
    for (size_t i = 0u; i < sizeof(EDIT_OP_KINDS) / sizeof(EDIT_OP_KINDS[0]);
         ++i) {
        if (strcmp(EDIT_OP_KINDS[i].name, name) == 0) {
            *out_kind = EDIT_OP_KINDS[i].kind;
            return NMO_OK;
        }
    }
    NMO_RETURN_ERROR(
        NMO_ERR_NOT_SUPPORTED,
        NMO_SEVERITY_ERROR,
        "Unsupported edit plan op '%s'",
        name);
}

const char *nmo_edit_op_kind_result_handle(nmo_edit_op_kind_t kind)
{
    const nmo_edit_op_kind_descriptor_t *descriptor = edit_op_kind_find(kind);
    return descriptor != NULL ? descriptor->result_handle : NULL;
}

bool nmo_edit_op_kind_creates_result(nmo_edit_op_kind_t kind)
{
    return nmo_edit_op_kind_result_handle(kind) != NULL;
}
