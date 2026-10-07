/**
 * @file nmo_cmd_behavior_flow.c
 * @brief Script model text for behavior dump, show, and trace: labels, parameter sources, data flow
 */

#include "nmo_cmd_behavior_internal.h"

#include "../nmo_tool_common.h"
#include "../nmo_tool_owner.h"

#include "behavior/nmo_script_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Longest value text a flow line shows before "...". */
#define FLOW_VALUE_TEXT_MAX 48
/* Graph input hops followed to show where a graph pIn reads from. */
#define FLOW_UPSTREAM_DEPTH_MAX 8

bool nmo_cmd_behavior_flow_init(nmo_cmd_behavior_flow_ctx_t *f, const nmo_cmd_ctx_t *c)
{
    memset(f, 0, sizeof(*f));
    f->repo = nmo_tool_owner_repository(c->workspace);
    f->registry = c->registry;
    if (nmo_script_model_build(c->workspace, &f->model) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build the script model\n");
        return false;
    }
    return true;
}

void nmo_cmd_behavior_flow_dispose(nmo_cmd_behavior_flow_ctx_t *f)
{
    if (f) {
        nmo_script_model_destroy(f->model);
        f->model = NULL;
    }
}

nmo_object_id_t nmo_cmd_behavior_parent_id(const nmo_cmd_behavior_flow_ctx_t *f,
                                           nmo_object_id_t behavior_id)
{
    const nmo_script_node_t *node = nmo_script_model_find_node(f->model, behavior_id);
    return node ? node->parent_id : 0;
}

const char *nmo_cmd_behavior_op_block_name(const nmo_cmd_behavior_flow_ctx_t *f,
                                           nmo_object_id_t behavior_id)
{
    const nmo_script_node_t *node = nmo_script_model_find_node(f->model, behavior_id);
    return node ? node->operation_name : NULL;
}

char *nmo_cmd_behavior_node_label_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                      nmo_object_id_t id)
{
    size_t length = nmo_script_model_label(f->model, id, NULL, 0);
    char *label = (char *)malloc(length + 1u);
    if (label) {
        nmo_script_model_label(f->model, id, label, length + 1u);
    }
    return label;
}

char *nmo_cmd_behavior_io_label_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                    nmo_object_id_t io_id)
{
    const nmo_script_io_t *io = nmo_script_model_find_io(f->model, io_id);
    if (!io) {
        return nmo_tool_strdup_fmt("?.%s", resolve_name(f->repo, io_id));
    }
    char *owner = nmo_cmd_behavior_node_label_dup(f, io->node_id);
    char *label = owner ? nmo_tool_strdup_fmt("%s.%s", owner, io->name) : NULL;
    free(owner);
    return label;
}

void nmo_cmd_behavior_param_ref_dispose(nmo_cmd_behavior_param_ref_t *ref)
{
    if (ref) {
        free(ref->text);
        free(ref->value);
        memset(ref, 0, sizeof(*ref));
    }
}

static bool flow_reach_has_value(nmo_script_reach_t reach)
{
    return reach == NMO_SCRIPT_REACH_LOCAL || reach == NMO_SCRIPT_REACH_ANCESTOR_LOCAL ||
           reach == NMO_SCRIPT_REACH_FOREIGN_LOCAL || reach == NMO_SCRIPT_REACH_EXTERNAL;
}

/* "Owner#id.Param", or "Param#id" for a parameter no behavior or operation holds. */
static char *flow_param_name_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                 nmo_object_id_t owner_id,
                                 nmo_object_id_t param_id)
{
    if (owner_id == 0) {
        return nmo_tool_strdup_fmt("%s#%u", resolve_name(f->repo, param_id), (unsigned)param_id);
    }
    char *owner = nmo_cmd_behavior_node_label_dup(f, owner_id);
    char *text = owner ? nmo_tool_strdup_fmt("%s.%s", owner, resolve_name(f->repo, param_id))
                       : NULL;
    free(owner);
    return text;
}

static bool flow_describe(const nmo_cmd_behavior_flow_ctx_t *f,
                          nmo_object_id_t param_id,
                          nmo_object_id_t owner_id,
                          nmo_script_reach_t reach,
                          int depth,
                          nmo_cmd_behavior_param_ref_t *out);

/* flow_describe() of a parameter by the owner it names. */
static bool flow_resolve(const nmo_cmd_behavior_flow_ctx_t *f,
                         nmo_object_id_t graph_id,
                         nmo_object_id_t param_id,
                         int depth,
                         nmo_cmd_behavior_param_ref_t *out)
{
    const nmo_script_param_t *param = nmo_script_model_find_param(f->model, param_id);
    return flow_describe(f, param_id, param ? param->owner_id : 0,
                         nmo_script_model_reach(f->model, graph_id, param_id), depth, out);
}

/* " <- <upstream>" for a graph pIn, ", unconnected" without a source. Heap string. */
static char *flow_upstream_text_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                    const nmo_script_param_t *pin,
                                    int depth)
{
    if (pin->source_id == 0) {
        return nmo_tool_strdup_fmt(", unconnected");
    }
    if (depth >= FLOW_UPSTREAM_DEPTH_MAX) {
        return nmo_tool_strdup_fmt(" <- ...");
    }
    nmo_cmd_behavior_param_ref_t up = {0};
    if (!flow_resolve(f, nmo_cmd_behavior_parent_id(f, pin->owner_id), pin->source_id,
                      depth + 1, &up)) {
        return NULL;
    }
    char *text = nmo_tool_strdup_fmt(" <- %s", up.text);
    nmo_cmd_behavior_param_ref_dispose(&up);
    return text;
}

/* `param_id` held by `owner_id`, reached as `reach`: its text and value. */
static bool flow_describe(const nmo_cmd_behavior_flow_ctx_t *f,
                          nmo_object_id_t param_id,
                          nmo_object_id_t owner_id,
                          nmo_script_reach_t reach,
                          int depth,
                          nmo_cmd_behavior_param_ref_t *out)
{
    memset(out, 0, sizeof(*out));
    const nmo_script_param_t *param = nmo_script_model_find_param(f->model, param_id);
    out->param_id = param_id;
    out->owner_id = owner_id;
    out->reach = reach;
    out->kind = nmo_script_reach_name(reach);

    if (flow_reach_has_value(reach)) {
        char value[512];
        if (nmo_script_model_param_value(f->model, param_id, value, sizeof(value)) == NMO_OK) {
            out->value = nmo_tool_strdup_fmt("%s", value);
        }
    }
    char *suffix = (reach == NMO_SCRIPT_REACH_GRAPH_INPUT && param)
        ? flow_upstream_text_dup(f, param, depth) : NULL;
    char *name = flow_param_name_dup(f, owner_id, param_id);

    bool trimmed = out->value && strlen(out->value) > FLOW_VALUE_TEXT_MAX;
    if (name && reach == NMO_SCRIPT_REACH_CHILD_OUTPUT) {
        out->text = nmo_tool_strdup_fmt("%s", name);
    } else if (name && out->value) {
        out->text = nmo_tool_strdup_fmt("%s (%s = %.*s%s)", name, out->kind,
                                        FLOW_VALUE_TEXT_MAX, out->value,
                                        trimmed ? "..." : "");
    } else if (name) {
        out->text = nmo_tool_strdup_fmt("%s (%s%s)", name, out->kind, suffix ? suffix : "");
    }
    free(name);
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

/* The text of an edge's target: the input it reads into, or the parameter it writes. */
static char *flow_target_text_dup(const nmo_cmd_behavior_flow_ctx_t *f,
                                  const nmo_script_data_edge_t *edge)
{
    char *name = flow_param_name_dup(f, edge->target_owner_id, edge->target_id);
    if (!name || edge->kind == NMO_SCRIPT_DATA_READ) {
        return name;
    }
    char *text = nmo_tool_strdup_fmt("%s (%s)", name, nmo_script_reach_name(edge->target_reach));
    free(name);
    return text;
}

static const char *flow_owner_name(const nmo_cmd_behavior_flow_ctx_t *f, nmo_object_id_t owner_id)
{
    if (owner_id == 0) {
        return "(external)";
    }
    const nmo_script_operation_t *op = nmo_script_model_find_operation(f->model, owner_id);
    return op ? op->operation_name : resolve_name(f->repo, owner_id);
}

static bool flow_add_edge(nmo_cli_record_array_t *arr,
                          const nmo_cmd_behavior_flow_ctx_t *f,
                          const nmo_script_data_edge_t *edge,
                          bool with_text)
{
    const nmo_script_param_t *source = nmo_script_model_find_param(f->model, edge->source_id);
    nmo_cmd_behavior_param_ref_t src = {0};
    char *target_text = NULL;
    const char *type_name = resolve_type(f->registry, edge->type_guid);
    nmo_cli_record_t *item = NULL;

    bool ok = flow_describe(f, edge->source_id, edge->source_owner_id, edge->source_reach, 0,
                            &src) &&
              (target_text = flow_target_text_dup(f, edge)) != NULL &&
              (item = nmo_cli_record_new()) != NULL &&
        nmo_cli_record_str(item, "kind", NULL,
                           edge->kind == NMO_SCRIPT_DATA_READ ? "read" : "write") &&
        nmo_cli_record_uint(item, "source_id", NULL, edge->source_id) &&
        nmo_cli_record_str(item, "source_name", NULL, resolve_name(f->repo, edge->source_id)) &&
        nmo_cli_record_uint(item, "source_owner_id", NULL, edge->source_owner_id) &&
        nmo_cli_record_str(item, "source_owner_name", NULL,
                           flow_owner_name(f, edge->source_owner_id)) &&
        nmo_cli_record_str(item, "source_kind", NULL, nmo_script_reach_name(edge->source_reach)) &&
        nmo_cli_record_str_opt(item, "source_value", NULL, src.value, NULL) &&
        nmo_cli_record_uint(item, "target_id", NULL, edge->target_id) &&
        nmo_cli_record_str(item, "target_name", NULL, resolve_name(f->repo, edge->target_id)) &&
        nmo_cli_record_uint(item, "target_owner_id", NULL, edge->target_owner_id) &&
        nmo_cli_record_str(item, "target_owner_name", NULL,
                           flow_owner_name(f, edge->target_owner_id)) &&
        nmo_cli_record_str(item, "target_kind", NULL, nmo_script_reach_name(edge->target_reach)) &&
        nmo_cli_record_str_fmt(item, "type_guid", NULL, "%08X-%08X",
                               edge->type_guid.d1, edge->type_guid.d2) &&
        nmo_cli_record_str(item, "type_name", NULL, type_name) &&
        nmo_cli_record_bool(item, "is_shared", NULL, edge->is_shared);
    if (ok && source && source->class_id == NMO_CID_PARAMETERIN) {
        ok = nmo_cli_record_bool(item, "source_is_parameter_in", NULL, true);
    }
    if (ok && with_text) {
        ok = nmo_cli_record_set_summary_fmt(item, "  %s -> %s  [%s]%s", src.text, target_text,
                                            type_name, edge->is_shared ? " (shared)" : "");
    }
    nmo_cmd_behavior_param_ref_dispose(&src);
    free(target_text);
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(arr, item);
}

bool nmo_cmd_behavior_add_data_flow_items(nmo_cli_record_array_t *arr,
                                          const nmo_cmd_behavior_flow_ctx_t *f,
                                          nmo_object_id_t graph_id,
                                          bool with_text)
{
    const nmo_script_node_t *node = nmo_script_model_find_node(f->model, graph_id);
    size_t count = 0;
    const nmo_script_data_edge_t *edges = nmo_script_model_data_edges(f->model, &count);
    bool ok = true;
    for (size_t i = 0; ok && node && i < node->data_edge_count; i++) {
        ok = flow_add_edge(arr, f, &edges[node->first_data_edge + i], with_text);
    }
    return ok;
}
