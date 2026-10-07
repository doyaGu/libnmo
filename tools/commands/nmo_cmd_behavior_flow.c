/**
 * @file nmo_cmd_behavior_flow.c
 * @brief Node labels, parameter sources, and data flow shared by behavior dump and show
 */

#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_core.h"
#include "../nmo_tool_common.h"

#include "behavior/nmo_behavior_analyze.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_repository.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Longest value text a flow line shows before "...". */
#define FLOW_VALUE_TEXT_MAX 48
/* Graph input hops followed to show where a graph pIn reads from. */
#define FLOW_UPSTREAM_DEPTH_MAX 8

static const nmo_behavior_state_t *flow_behavior_state(const nmo_cmd_behavior_flow_ctx_t *f,
                                                       nmo_object_id_t id)
{
    nmo_object_t *obj = id != 0 ? nmo_object_repository_find_by_id(f->repo, id) : NULL;
    if (!obj || nmo_object_get_class_id(obj) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)nmo_object_get_state(obj);
}

nmo_object_id_t nmo_cmd_behavior_parent_id(const nmo_cmd_behavior_flow_ctx_t *f,
                                           nmo_object_id_t behavior_id)
{
    const nmo_port_owner_t *po = f->index ? nmo_behavior_index_find(f->index, behavior_id) : NULL;
    return (po && po->kind == NMO_PORT_SUB_BEHAVIOR) ? po->owner_id : 0;
}

static bool flow_is_ancestor(const nmo_cmd_behavior_flow_ctx_t *f,
                             nmo_object_id_t ancestor_id,
                             nmo_object_id_t behavior_id)
{
    nmo_object_id_t cur = nmo_cmd_behavior_parent_id(f, behavior_id);
    for (int hops = 0; cur != 0 && hops < 64; hops++) {
        if (cur == ancestor_id) {
            return true;
        }
        cur = nmo_cmd_behavior_parent_id(f, cur);
    }
    return false;
}

const char *nmo_cmd_behavior_op_block_name(const nmo_cmd_behavior_flow_ctx_t *f,
                                           nmo_object_id_t behavior_id)
{
    nmo_guid_t guid;
    if (!f->workspace ||
        nmo_behavior_op_block_operation_guid(f->workspace, behavior_id, &guid) != NMO_OK) {
        return NULL;
    }
    const char *name = nmo_type_registry_guid_to_name(f->registry, guid);
    return (name && name[0]) ? name : "?";
}

char *nmo_cmd_behavior_node_label_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                      nmo_object_id_t behavior_id)
{
    const char *name = resolve_name(f->repo, behavior_id);
    const char *op_name = nmo_cmd_behavior_op_block_name(f, behavior_id);
    if (op_name) {
        return nmo_tool_strdup_fmt("%s(%s)#%u", name, op_name, (unsigned)behavior_id);
    }
    return nmo_tool_strdup_fmt("%s#%u", name, (unsigned)behavior_id);
}

char *nmo_cmd_behavior_io_label_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                    nmo_object_id_t io_id)
{
    const nmo_port_owner_t *po = f->index ? nmo_behavior_index_find(f->index, io_id) : NULL;
    if (!po || po->owner_id == 0) {
        return nmo_tool_strdup_fmt("?.%s", resolve_name(f->repo, io_id));
    }
    char *owner = nmo_cmd_behavior_node_label_dup(f, po->owner_id);
    char *label = owner ? nmo_tool_strdup_fmt("%s.%s", owner, resolve_name(f->repo, io_id)) : NULL;
    free(owner);
    return label;
}

/* Decoded value of a parameter holding one (not a ParameterIn), or NULL. */
static char *flow_param_value_dup(const nmo_cmd_behavior_flow_ctx_t *f, nmo_object_t *obj)
{
    nmo_class_id_t cid = obj ? nmo_object_get_class_id(obj) : 0;
    if (cid != NMO_CID_PARAMETERLOCAL && cid != NMO_CID_PARAMETEROUT &&
        cid != NMO_CID_PARAMETER) {
        return NULL;
    }
    const nmo_parameter_state_t *ps = (const nmo_parameter_state_t *)nmo_object_get_state(obj);
    if (!ps || !ps->has_state) {
        return NULL;
    }
    char *value = nmo_core_param_value_dup(ps, f->registry, f->workspace);
    if (value && value[0] == '\0') {
        free(value);
        return NULL;
    }
    return value;
}

/* The operation among `graph_id` and its ancestors whose output is `param_id`. */
static nmo_object_id_t flow_find_operation_output(const nmo_cmd_behavior_flow_ctx_t *f,
                                                  nmo_object_id_t graph_id,
                                                  nmo_object_id_t param_id)
{
    nmo_object_id_t cur = graph_id;
    for (int hops = 0; cur != 0 && hops < 64; hops++) {
        const nmo_behavior_state_t *bs = flow_behavior_state(f, cur);
        for (size_t i = 0; bs && i < bs->operations.count; i++) {
            nmo_object_id_t op_id = nmo_behavior_ref_array_get_id(&bs->operations, i);
            nmo_object_t *op = nmo_object_repository_find_by_id(f->repo, op_id);
            const nmo_parameteroperation_state_t *os =
                op ? (const nmo_parameteroperation_state_t *)nmo_object_get_state(op) : NULL;
            if (os && os->has_out && nmo_parameteroperation_out_id(os) == param_id) {
                return op_id;
            }
        }
        cur = nmo_cmd_behavior_parent_id(f, cur);
    }
    return 0;
}

static const char *flow_operation_name(const nmo_cmd_behavior_flow_ctx_t *f,
                                       nmo_object_id_t op_id)
{
    nmo_object_t *op = nmo_object_repository_find_by_id(f->repo, op_id);
    const nmo_parameteroperation_state_t *os =
        op ? (const nmo_parameteroperation_state_t *)nmo_object_get_state(op) : NULL;
    const char *name = os ? nmo_type_registry_guid_to_name(f->registry, os->operation_guid) : NULL;
    return (name && name[0]) ? name : resolve_name(f->repo, op_id);
}

void nmo_cmd_behavior_param_ref_dispose(nmo_cmd_behavior_param_ref_t *ref)
{
    if (ref) {
        free(ref->text);
        free(ref->value);
        memset(ref, 0, sizeof(*ref));
    }
}

static bool flow_resolve(const nmo_cmd_behavior_flow_ctx_t *f,
                         nmo_object_id_t graph_id,
                         nmo_object_id_t param_id,
                         int depth,
                         nmo_cmd_behavior_param_ref_t *out);

/* " <- <upstream>" for a graph pIn, "" when it has no source. Heap string. */
static char *flow_upstream_text_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                    nmo_object_id_t graph_id,
                                    nmo_object_t *pin_obj,
                                    int depth)
{
    const nmo_parameterin_state_t *pin =
        (const nmo_parameterin_state_t *)nmo_object_get_state(pin_obj);
    nmo_object_id_t source_id = pin ? nmo_parameterin_source_id(pin) : 0;
    if (source_id == 0) {
        return nmo_tool_strdup_fmt(", unconnected");
    }
    if (depth >= FLOW_UPSTREAM_DEPTH_MAX) {
        return nmo_tool_strdup_fmt(" <- ...");
    }
    nmo_cmd_behavior_param_ref_t up = {0};
    if (!flow_resolve(f, nmo_cmd_behavior_parent_id(f, graph_id), source_id, depth + 1, &up)) {
        return NULL;
    }
    char *text = nmo_tool_strdup_fmt(" <- %s", up.text);
    nmo_cmd_behavior_param_ref_dispose(&up);
    return text;
}

static bool flow_resolve(const nmo_cmd_behavior_flow_ctx_t *f,
                         nmo_object_id_t graph_id,
                         nmo_object_id_t param_id,
                         int depth,
                         nmo_cmd_behavior_param_ref_t *out)
{
    memset(out, 0, sizeof(*out));
    out->param_id = param_id;
    nmo_object_t *obj = nmo_object_repository_find_by_id(f->repo, param_id);
    const char *pname = resolve_name(f->repo, param_id);
    const nmo_port_owner_t *po = f->index ? nmo_behavior_index_find(f->index, param_id) : NULL;
    bool show_value = false;
    char *suffix = NULL;

    if (po && po->owner_id != 0) {
        out->owner_id = po->owner_id;
        switch (po->kind) {
        case NMO_PORT_PARAM_LOCAL:
            out->kind = po->owner_id == graph_id ? "local"
                      : flow_is_ancestor(f, po->owner_id, graph_id) ? "ancestor local"
                      : "foreign local";
            show_value = true;
            break;
        case NMO_PORT_PARAM_OUT:
            out->kind = po->owner_id == graph_id ? "graph pOut"
                      : nmo_cmd_behavior_parent_id(f, po->owner_id) == graph_id ? "pOut"
                      : "foreign pOut";
            break;
        case NMO_PORT_PARAM_IN:
            if (po->owner_id == graph_id || flow_is_ancestor(f, po->owner_id, graph_id)) {
                out->kind = "graph pIn";
                suffix = obj ? flow_upstream_text_dup(f, po->owner_id, obj, depth) : NULL;
            } else {
                out->kind = "foreign pIn";
            }
            break;
        case NMO_PORT_PARAM_TARGET:
            out->kind = "target";
            break;
        default:
            out->kind = "other";
            break;
        }
    } else if ((out->owner_id = flow_find_operation_output(f, graph_id, param_id)) != 0) {
        out->kind = "operation";
    } else {
        out->kind = "external";
        show_value = true;
    }

    if (show_value) {
        out->value = flow_param_value_dup(f, obj);
    }

    char *owner = NULL;
    if (out->owner_id == 0) {
        owner = nmo_tool_strdup_fmt("%s#%u", pname, (unsigned)param_id);
    } else if (strcmp(out->kind, "operation") == 0) {
        owner = nmo_tool_strdup_fmt("%s#%u.%s", flow_operation_name(f, out->owner_id),
                                    (unsigned)out->owner_id, pname);
    } else {
        char *node = nmo_cmd_behavior_node_label_dup(f, out->owner_id);
        owner = node ? nmo_tool_strdup_fmt("%s.%s", node, pname) : NULL;
        free(node);
    }

    bool trimmed = out->value && strlen(out->value) > FLOW_VALUE_TEXT_MAX;
    if (owner && strcmp(out->kind, "pOut") == 0) {
        out->text = nmo_tool_strdup_fmt("%s", owner);
    } else if (owner && out->value) {
        out->text = nmo_tool_strdup_fmt("%s (%s = %.*s%s)", owner, out->kind,
                                        FLOW_VALUE_TEXT_MAX, out->value,
                                        trimmed ? "..." : "");
    } else if (owner) {
        out->text = nmo_tool_strdup_fmt("%s (%s%s)", owner, out->kind, suffix ? suffix : "");
    }
    free(owner);
    free(suffix);
    return out->text != NULL;
}

bool nmo_cmd_behavior_resolve_param(const nmo_cmd_behavior_flow_ctx_t *f,
                                    nmo_object_id_t graph_id,
                                    nmo_object_id_t param_id,
                                    nmo_cmd_behavior_param_ref_t *out)
{
    return flow_resolve(f, graph_id, param_id, 0, out);
}

/* One data flow item: `source` feeds `target` (both seen from `graph_id`). */
static bool flow_add_item(nmo_cli_record_array_t *arr,
                          const nmo_cmd_behavior_flow_ctx_t *f,
                          const nmo_cmd_behavior_param_ref_t *source,
                          const nmo_cmd_behavior_param_ref_t *target,
                          nmo_object_t *typed_param,
                          bool is_shared,
                          bool with_text)
{
    nmo_object_t *src_obj = nmo_object_repository_find_by_id(f->repo, source->param_id);
    nmo_guid_t type_guid = get_param_type_guid(typed_param);
    const char *type_name = resolve_type(f->registry, type_guid);
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL &&
        nmo_cli_record_uint(item, "source_id", NULL, source->param_id) &&
        nmo_cli_record_str(item, "source_name", NULL, resolve_name(f->repo, source->param_id)) &&
        nmo_cli_record_uint(item, "source_owner_id", NULL, source->owner_id) &&
        nmo_cli_record_str(item, "source_owner_name", NULL,
                           source->owner_id ? resolve_name(f->repo, source->owner_id)
                                            : "(external)") &&
        nmo_cli_record_str(item, "source_kind", NULL, source->kind) &&
        nmo_cli_record_str_opt(item, "source_value", NULL, source->value, NULL) &&
        nmo_cli_record_uint(item, "target_id", NULL, target->param_id) &&
        nmo_cli_record_str(item, "target_name", NULL, resolve_name(f->repo, target->param_id)) &&
        nmo_cli_record_uint(item, "target_owner_id", NULL, target->owner_id) &&
        nmo_cli_record_str(item, "target_owner_name", NULL,
                           target->owner_id ? resolve_name(f->repo, target->owner_id)
                                            : "(external)") &&
        nmo_cli_record_str(item, "target_kind", NULL, target->kind) &&
        nmo_cli_record_str_fmt(item, "type_guid", NULL, "%08X-%08X",
                               type_guid.d1, type_guid.d2) &&
        nmo_cli_record_str(item, "type_name", NULL, type_name) &&
        nmo_cli_record_bool(item, "is_shared", NULL, is_shared);
    if (ok && src_obj && nmo_object_get_class_id(src_obj) == NMO_CID_PARAMETERIN) {
        ok = nmo_cli_record_bool(item, "source_is_parameter_in", NULL, true);
    }
    if (ok && with_text) {
        ok = nmo_cli_record_set_summary_fmt(item, "  %s -> %s  [%s]%s",
                                            source->text, target->text, type_name,
                                            is_shared ? " (shared)" : "");
    }
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(arr, item);
}

/* The read of ParameterIn `pin_id`, owned by `owner_id` inside `graph_id`. */
static bool flow_add_read(nmo_cli_record_array_t *arr,
                          const nmo_cmd_behavior_flow_ctx_t *f,
                          nmo_object_id_t graph_id,
                          nmo_object_id_t owner_id,
                          nmo_object_id_t pin_id,
                          bool with_text)
{
    nmo_object_t *pin_obj = pin_id ? nmo_object_repository_find_by_id(f->repo, pin_id) : NULL;
    if (!pin_obj || nmo_object_get_class_id(pin_obj) != NMO_CID_PARAMETERIN) {
        return true;
    }
    const nmo_parameterin_state_t *pin =
        (const nmo_parameterin_state_t *)nmo_object_get_state(pin_obj);
    nmo_object_id_t source_id = pin ? nmo_parameterin_source_id(pin) : 0;
    if (source_id == 0) {
        return true;
    }

    nmo_cmd_behavior_param_ref_t source = {0};
    nmo_cmd_behavior_param_ref_t target = {0};
    bool ok = nmo_cmd_behavior_resolve_param(f, graph_id, source_id, &source);
    if (ok) {
        target.param_id = pin_id;
        target.owner_id = owner_id;
        target.kind = "pIn";
        nmo_object_t *owner_obj = nmo_object_repository_find_by_id(f->repo, owner_id);
        if (owner_obj && nmo_object_get_class_id(owner_obj) == NMO_CID_PARAMETEROPERATION) {
            target.kind = "operation pIn";
            target.text = nmo_tool_strdup_fmt("%s#%u.%s", flow_operation_name(f, owner_id),
                                              (unsigned)owner_id,
                                              resolve_name(f->repo, pin_id));
        } else {
            char *node = nmo_cmd_behavior_node_label_dup(f, owner_id);
            target.text = node ? nmo_tool_strdup_fmt("%s.%s", node,
                                                     resolve_name(f->repo, pin_id)) : NULL;
            free(node);
        }
        ok = target.text != NULL &&
             flow_add_item(arr, f, &source, &target, pin_obj, pin->is_shared != 0, with_text);
    }
    nmo_cmd_behavior_param_ref_dispose(&source);
    free(target.text);
    return ok;
}

/* The writes of ParameterOut `pout_id` into parameters other than ParameterIns. */
static bool flow_add_writes(nmo_cli_record_array_t *arr,
                            const nmo_cmd_behavior_flow_ctx_t *f,
                            nmo_object_id_t graph_id,
                            nmo_object_id_t pout_id,
                            bool with_text)
{
    nmo_object_t *pout_obj = pout_id ? nmo_object_repository_find_by_id(f->repo, pout_id) : NULL;
    if (!pout_obj || nmo_object_get_class_id(pout_obj) != NMO_CID_PARAMETEROUT) {
        return true;
    }
    const nmo_parameterout_state_t *pout =
        (const nmo_parameterout_state_t *)nmo_object_get_state(pout_obj);
    bool ok = true;
    for (uint32_t i = 0; ok && pout && i < pout->destination_count; i++) {
        nmo_object_id_t dest_id = nmo_parameterout_destination_id(pout, i);
        nmo_object_t *dest = dest_id ? nmo_object_repository_find_by_id(f->repo, dest_id) : NULL;
        if (!dest || nmo_object_get_class_id(dest) == NMO_CID_PARAMETERIN) {
            continue; /* a ParameterIn reading this output is listed as its read */
        }
        nmo_cmd_behavior_param_ref_t source = {0};
        nmo_cmd_behavior_param_ref_t target = {0};
        ok = nmo_cmd_behavior_resolve_param(f, graph_id, pout_id, &source) &&
             nmo_cmd_behavior_resolve_param(f, graph_id, dest_id, &target);
        if (ok) {
            /* The destination takes the output's value; its saved value is stale */
            free(target.value);
            target.value = NULL;
            free(target.text);
            target.text = NULL;
            char *node = target.owner_id ? nmo_cmd_behavior_node_label_dup(f, target.owner_id)
                                         : NULL;
            target.text = target.owner_id
                ? (node ? nmo_tool_strdup_fmt("%s.%s (%s)", node, resolve_name(f->repo, dest_id),
                                              target.kind) : NULL)
                : nmo_tool_strdup_fmt("%s#%u (%s)", resolve_name(f->repo, dest_id),
                                      (unsigned)dest_id, target.kind);
            free(node);
            ok = target.text != NULL &&
                 flow_add_item(arr, f, &source, &target, pout_obj, false, with_text);
        }
        nmo_cmd_behavior_param_ref_dispose(&source);
        nmo_cmd_behavior_param_ref_dispose(&target);
    }
    return ok;
}

bool nmo_cmd_behavior_add_data_flow_items(nmo_cli_record_array_t *arr,
                                          const nmo_cmd_behavior_flow_ctx_t *f,
                                          nmo_object_id_t graph_id,
                                          bool with_text)
{
    const nmo_behavior_state_t *bs = flow_behavior_state(f, graph_id);
    bool ok = true;
    for (size_t si = 0; ok && bs && si < bs->sub_behaviors.count; si++) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(&bs->sub_behaviors, si);
        const nmo_behavior_state_t *sub = flow_behavior_state(f, sub_id);
        if (!sub) {
            continue;
        }
        nmo_object_id_t target_param = nmo_behavior_target_parameter_id(sub);
        if (target_param != 0) {
            ok = flow_add_read(arr, f, graph_id, sub_id, target_param, with_text);
        }
        for (size_t pi = 0; ok && pi < sub->in_parameters.count; pi++) {
            ok = flow_add_read(arr, f, graph_id, sub_id,
                               nmo_behavior_ref_array_get_id(&sub->in_parameters, pi),
                               with_text);
        }
        for (size_t pi = 0; ok && pi < sub->out_parameters.count; pi++) {
            ok = flow_add_writes(arr, f, graph_id,
                                 nmo_behavior_ref_array_get_id(&sub->out_parameters, pi),
                                 with_text);
        }
    }
    for (size_t oi = 0; ok && bs && oi < bs->operations.count; oi++) {
        nmo_object_id_t op_id = nmo_behavior_ref_array_get_id(&bs->operations, oi);
        nmo_object_t *op = nmo_object_repository_find_by_id(f->repo, op_id);
        const nmo_parameteroperation_state_t *os =
            op ? (const nmo_parameteroperation_state_t *)nmo_object_get_state(op) : NULL;
        if (!os) {
            continue;
        }
        if (os->has_in1) {
            ok = flow_add_read(arr, f, graph_id, op_id, nmo_parameteroperation_in1_id(os),
                               with_text);
        }
        if (ok && os->has_in2) {
            ok = flow_add_read(arr, f, graph_id, op_id, nmo_parameteroperation_in2_id(os),
                               with_text);
        }
        if (ok && os->has_out) {
            ok = flow_add_writes(arr, f, graph_id, nmo_parameteroperation_out_id(os), with_text);
        }
    }
    return ok;
}
