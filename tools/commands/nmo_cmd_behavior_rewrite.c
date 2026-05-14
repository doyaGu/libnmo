/**
 * @file nmo_cmd_behavior_rewrite.c
 * @brief Behavior graph rewrite CLI commands.
 */

#include "nmo_cmd_behavior_rewrite.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_json.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_edit.h"
#include "behavior/nmo_edit_plan.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_parse.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rewrite_guid_to_string(nmo_guid_t guid, char *buf, size_t size) {
    if (!buf || size == 0) {
        return;
    }
    snprintf(buf, size, "%08X-%08X", guid.d1, guid.d2);
}

static bool parse_graph_boundary_args(int argc,
                                      char **argv,
                                      bool expect_file_operand,
                                      nmo_core_object_selector_t *out_selector,
                                      const char **out_file,
                                      uint32_t *out_depth) {
    static const nmo_opt_def_t opts[] = {
        {"--depth", "-d", NMO_OPT_UINT, "Recursion depth (default: unlimited)"},
        {"--json",  "-j", NMO_OPT_FLAG, "JSON output"},
        {"--id",    "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name",  "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_DEPTH, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 16,
    };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &result) < 0) {
        return false;
    }

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    const char *file_path = NULL;

    if (expect_file_operand) {
        if ((has_selector_opt && result.pos_count < 1) ||
            (!has_selector_opt && result.pos_count < 2)) {
            return false;
        }
        positional_id = has_selector_opt ? NULL : result.pos_args[0];
        file_path = result.pos_args[result.pos_count - 1];
    } else if (has_selector_opt) {
        if (result.pos_count != 0) {
            return false;
        }
    } else {
        if (result.pos_count != 1) {
            return false;
        }
        positional_id = result.pos_args[0];
    }

    if (out_selector) {
        *out_selector = (nmo_core_object_selector_t){
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_BEHAVIOR,
            .selector_label = "Behavior",
            .type_label = "CKBehavior",
        };
    }
    if (out_file) {
        *out_file = file_path;
    }
    if (out_depth) {
        *out_depth = vals[OPT_DEPTH].present ?
            vals[OPT_DEPTH].val.u : UINT32_MAX;
    }
    return true;
}

static void add_internal_nodes_json(yyjson_mut_doc *doc,
                                    yyjson_mut_val *data,
                                    const nmo_behavior_boundary_t *boundary) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < boundary->internal_node_count; ++i) {
        yyjson_mut_arr_add_uint(doc, arr, boundary->internal_nodes[i]);
    }
    yyjson_mut_obj_add_val(doc, data, "internal_nodes", arr);
}

static void add_control_edges_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    const char *key,
    const nmo_behavior_boundary_control_edge_t *edges,
    size_t count) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < count; ++i) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "link_id", edges[i].link_id);
        yyjson_mut_obj_add_uint(doc, item, "source_owner_id",
                                edges[i].source_owner_id);
        yyjson_mut_obj_add_uint(doc, item, "source_io_id",
                                edges[i].source_io_id);
        yyjson_mut_obj_add_uint(doc, item, "target_owner_id",
                                edges[i].target_owner_id);
        yyjson_mut_obj_add_uint(doc, item, "target_io_id",
                                edges[i].target_io_id);
        yyjson_mut_obj_add_int(doc, item, "activation_delay",
                               edges[i].activation_delay);
        yyjson_mut_obj_add_int(doc, item, "initial_activation_delay",
                               edges[i].initial_activation_delay);
        yyjson_mut_arr_add_val(arr, item);
    }
    yyjson_mut_obj_add_val(doc, data, key, arr);
}

static void add_parameter_edges_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    const char *key,
    const nmo_behavior_boundary_parameter_edge_t *edges,
    size_t count) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < count; ++i) {
        char guid_buf[24];
        rewrite_guid_to_string(edges[i].type_guid, guid_buf, sizeof(guid_buf));

        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "source_parameter_id",
                                edges[i].source_parameter_id);
        yyjson_mut_obj_add_uint(doc, item, "target_parameter_id",
                                edges[i].target_parameter_id);
        yyjson_mut_obj_add_uint(doc, item, "source_owner_id",
                                edges[i].source_owner_id);
        yyjson_mut_obj_add_uint(doc, item, "target_owner_id",
                                edges[i].target_owner_id);
        nmo_cli_json_add_str_safe(doc, item, "type_guid", guid_buf);
        yyjson_mut_obj_add_bool(doc, item, "shared", edges[i].shared);
        yyjson_mut_arr_add_val(arr, item);
    }
    yyjson_mut_obj_add_val(doc, data, key, arr);
}

static void add_edit_report_json(yyjson_mut_doc *doc,
                                 yyjson_mut_val *data,
                                 nmo_edit_report_t *report,
                                 const char *output_path) {
    if (report != NULL && output_path != NULL && report->output_path == NULL) {
        (void)nmo_edit_report_set_output_path(report, output_path);
    }
    nmo_cli_edit_report_add_schema_v2_json(
        doc, data, report, report != NULL && report->dry_run);
}

static int graph_boundary_emit(nmo_cmd_ctx_t *ctx,
                               const nmo_behavior_boundary_t *boundary) {
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        if (!doc) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "behavior_id",
                                boundary->behavior_id);
        add_internal_nodes_json(doc, data, boundary);
        add_control_edges_json(doc, data, "control_in",
                               boundary->control_in,
                               boundary->control_in_count);
        add_control_edges_json(doc, data, "control_out",
                               boundary->control_out,
                               boundary->control_out_count);
        add_parameter_edges_json(doc, data, "parameter_in",
                                 boundary->parameter_in,
                                 boundary->parameter_in_count);
        add_parameter_edges_json(doc, data, "parameter_out",
                                 boundary->parameter_out,
                                 boundary->parameter_out_count);
        yyjson_mut_obj_add_uint(doc, data, "broken_links",
                                (uint64_t)boundary->broken_links);
        yyjson_mut_obj_add_uint(doc, data, "missing_nodes",
                                (uint64_t)boundary->missing_nodes);
        return nmo_cmd_ctx_json_end(ctx, doc, data,
                                    "behavior.graph-boundary");
    }

    fprintf(ctx->out, "Behavior #%u boundary\n", boundary->behavior_id);
    fprintf(ctx->out, "Internal nodes: %zu\n",
            boundary->internal_node_count);
    fprintf(ctx->out, "Control in: %zu\n", boundary->control_in_count);
    fprintf(ctx->out, "Control out: %zu\n", boundary->control_out_count);
    fprintf(ctx->out, "Parameter in: %zu\n", boundary->parameter_in_count);
    fprintf(ctx->out, "Parameter out: %zu\n", boundary->parameter_out_count);
    return NMO_CLI_EXIT_SUCCESS;
}

static int graph_boundary_run(nmo_cmd_ctx_t *ctx,
                              const nmo_core_object_selector_t *selector,
                              uint32_t depth,
                              bool close_ctx,
                              const char *usage) {
    nmo_cmd_ctx_t c = *ctx;
    nmo_object_t *behavior = NULL;
    nmo_object_id_t behavior_id = 0;
    nmo_behavior_boundary_t boundary = {0};
    int exit_code = NMO_CLI_EXIT_SUCCESS;

    int rc = nmo_core_resolve_one_object(&c, selector,
                                         &behavior, &behavior_id);
    (void)behavior;
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        exit_code = rc;
        goto cleanup;
    }

    if (!nmo_behavior_boundary_build(c.workspace,
                                     behavior_id, depth, &boundary)) {
        char detail[256];
        size_t detail_len = nmo_last_error_message_copy(detail,
                                                        sizeof(detail));
        nmo_error_code_t code = nmo_last_error_code();
        if (detail_len > 0) {
            fprintf(stderr, "Error: %s\n", detail);
        } else {
            fprintf(stderr, "Error: Failed to build behavior boundary\n");
        }
        exit_code = (code == NMO_ERR_INVALID_ARGUMENT ||
                     code == NMO_ERR_NOT_FOUND)
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    exit_code = graph_boundary_emit(&c, &boundary);

cleanup:
    nmo_behavior_boundary_free(&boundary);
    return close_ctx ? nmo_cmd_ctx_done(&c, exit_code) : exit_code;
}

int nmo_cmd_behavior_graph_boundary(int argc,
                                    char **argv,
                                    const nmo_cli_global_opts_t *global) {
    nmo_core_object_selector_t selector = {0};
    const char *file_path = NULL;
    uint32_t depth = UINT32_MAX;
    const char *usage =
        "nmo behavior graph-boundary [--depth N] [--id <id> | --name <name> | <id>] <file>";

    if (!parse_graph_boundary_args(argc, argv, true, &selector,
                                   &file_path, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    (void)file_path;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) {
        return rc;
    }

    return graph_boundary_run(&c, &selector, depth, true, usage);
}

int nmo_cmd_behavior_graph_boundary_in_session(nmo_cmd_ctx_t *ctx,
                                               int argc,
                                               char **argv) {
    nmo_core_object_selector_t selector = {0};
    const char *file_path = NULL;
    uint32_t depth = UINT32_MAX;
    const char *usage =
        "behavior graph-boundary [--depth N] [--id <id> | --name <name> | <id>]";

    if (!parse_graph_boundary_args(argc, argv, false, &selector,
                                   &file_path, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    (void)file_path;

    return graph_boundary_run(ctx, &selector, depth, false, usage);
}

typedef struct fold_candidates_args {
    nmo_object_id_t parent_id;
    uint32_t depth;
} fold_candidates_args_t;

typedef struct fold_candidate_group_desc {
    const char *kind;
    nmo_workspace_t *workspace;
    nmo_object_id_t root_id;
    const nmo_behavior_state_t *root_state;
    const nmo_object_id_t *roots;
    size_t root_count;
    const nmo_behavior_boundary_t *boundary;
} fold_candidate_group_desc_t;

typedef struct fold_candidate_child {
    nmo_object_id_t root_id;
    const nmo_behavior_state_t *root_state;
    nmo_behavior_boundary_t boundary;
} fold_candidate_child_t;

typedef struct fold_args {
    nmo_object_id_t parent_id;
    nmo_object_id_t nodes[256];
    size_t node_count;
    nmo_object_id_t anchor_id;
    nmo_guid_t block_guid;
    const char *name;
    uint32_t block_version;
    bool preserve_boundary;
    bool preserve_links;
    bool preserve_params;
    nmo_behavior_fold_map_t input_maps[16];
    size_t input_map_count;
    nmo_behavior_fold_map_t output_maps[16];
    size_t output_map_count;
    nmo_behavior_fold_map_t parameter_maps[16];
    size_t parameter_map_count;
    nmo_behavior_fold_interface_mode_t interface_mode;
    bool dry_run;
    const char *output_path;
} fold_args_t;

static const char *fold_behavior_type(const nmo_behavior_state_t *state) {
    if (!state) {
        return "Unknown";
    }
    if ((state->flags & CKBEHAVIOR_SCRIPT) != 0u) {
        return "Script";
    }
    if ((state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0u) {
        return "BB";
    }
    return "Graph";
}

static const nmo_behavior_state_t *fold_find_behavior_state(
    nmo_object_repository_t *repo,
    nmo_object_id_t behavior_id) {
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, behavior_id)
        : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)nmo_object_get_state(object);
}

static const char *fold_interface_action(
    const nmo_behavior_state_t *state) {
    if (!state || !state->has_interface) {
        return "none";
    }
    if (state->interface_data) {
        return "preserve";
    }
    if (state->interface_chunk) {
        return "preserve_raw";
    }
    return "preserve_marker";
}

static void add_fold_interface_json(yyjson_mut_doc *doc,
                                    yyjson_mut_val *group,
                                    const nmo_behavior_state_t *state) {
    yyjson_mut_val *interface_obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, interface_obj, "available",
                            state && state->has_interface);
    yyjson_mut_obj_add_bool(doc, interface_obj, "structured",
                            state && state->interface_data != NULL);
    yyjson_mut_obj_add_bool(doc, interface_obj, "raw",
                            state && state->interface_chunk != NULL);
    yyjson_mut_obj_add_bool(doc, interface_obj, "runtime_ids",
                            state && state->interface_ids_are_runtime);
    nmo_cli_json_add_str_safe(doc, interface_obj, "action",
                              fold_interface_action(state));
    yyjson_mut_obj_add_val(doc, group, "interface", interface_obj);
}

static void add_id_list_json(yyjson_mut_doc *doc,
                             yyjson_mut_val *obj,
                             const char *key,
                             const nmo_object_id_t *ids,
                             size_t count);

static void add_boundary_semantic_risks_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    nmo_workspace_t *workspace,
    const nmo_behavior_boundary_t *boundary) {
    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0;
    if (workspace && boundary) {
        (void)nmo_behavior_edit_collect_semantic_risks(
            workspace, boundary,
            boundary->internal_nodes, boundary->internal_node_count,
            &risks, &risk_count);
    }
    nmo_cli_edit_report_add_semantic_risk_array_json(
        doc, data, risks, risk_count);
    nmo_behavior_edit_semantic_risks_free(risks);
}

static void add_fold_candidate_group_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *groups,
    const fold_candidate_group_desc_t *desc) {
    if (!doc || !groups || !desc || !desc->boundary) {
        return;
    }

    yyjson_mut_val *group = yyjson_mut_obj(doc);
    nmo_cli_json_add_str_safe(doc, group, "kind", desc->kind);
    yyjson_mut_obj_add_uint(doc, group, "root_id", desc->root_id);
    nmo_cli_json_add_str_safe(doc, group, "root_behavior_type",
                              fold_behavior_type(desc->root_state));
    add_id_list_json(doc, group, "roots", desc->roots, desc->root_count);

    yyjson_mut_val *nodes = yyjson_mut_arr(doc);
    for (size_t i = 0; i < desc->boundary->internal_node_count; ++i) {
        yyjson_mut_arr_add_uint(doc, nodes, desc->boundary->internal_nodes[i]);
    }
    yyjson_mut_obj_add_val(doc, group, "nodes", nodes);
    add_internal_nodes_json(doc, group, desc->boundary);
    add_control_edges_json(doc, group, "control_in",
                           desc->boundary->control_in,
                           desc->boundary->control_in_count);
    add_control_edges_json(doc, group, "control_out",
                           desc->boundary->control_out,
                           desc->boundary->control_out_count);
    add_parameter_edges_json(doc, group, "parameter_in",
                             desc->boundary->parameter_in,
                             desc->boundary->parameter_in_count);
    add_parameter_edges_json(doc, group, "parameter_out",
                             desc->boundary->parameter_out,
                             desc->boundary->parameter_out_count);
    add_fold_interface_json(doc, group, desc->root_state);
    yyjson_mut_obj_add_uint(doc, group, "node_count",
                            (uint64_t)desc->boundary->internal_node_count);
    yyjson_mut_obj_add_uint(doc, group, "control_in_count",
                            (uint64_t)desc->boundary->control_in_count);
    yyjson_mut_obj_add_uint(doc, group, "control_out_count",
                            (uint64_t)desc->boundary->control_out_count);
    yyjson_mut_obj_add_uint(doc, group, "parameter_in_count",
                            (uint64_t)desc->boundary->parameter_in_count);
    yyjson_mut_obj_add_uint(doc, group, "parameter_out_count",
                            (uint64_t)desc->boundary->parameter_out_count);
    yyjson_mut_obj_add_uint(doc, group, "broken_links",
                            (uint64_t)desc->boundary->broken_links);
    yyjson_mut_obj_add_uint(doc, group, "missing_nodes",
                            (uint64_t)desc->boundary->missing_nodes);
    add_boundary_semantic_risks_json(
        doc, group, desc->workspace, desc->boundary);
    yyjson_mut_arr_add_val(groups, group);
}

static void add_id_list_json(yyjson_mut_doc *doc,
                             yyjson_mut_val *obj,
                             const char *key,
                             const nmo_object_id_t *ids,
                             size_t count) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; ids && i < count; ++i) {
        yyjson_mut_arr_add_uint(doc, arr, ids[i]);
    }
    yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool fold_candidate_contains_id(const nmo_behavior_boundary_t *boundary,
                                       nmo_object_id_t id) {
    if (!boundary || id == 0) {
        return false;
    }
    for (size_t i = 0; i < boundary->internal_node_count; ++i) {
        if (boundary->internal_nodes[i] == id) {
            return true;
        }
    }
    return false;
}

static bool fold_id_in_list(const nmo_object_id_t *ids,
                            size_t count,
                            nmo_object_id_t id) {
    if (!ids || id == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

static size_t fold_candidate_find_child_index(const fold_candidate_child_t *children,
                                              size_t child_count,
                                              nmo_object_id_t id) {
    for (size_t i = 0; children && i < child_count; ++i) {
        if (fold_candidate_contains_id(&children[i].boundary, id)) {
            return i;
        }
    }
    return SIZE_MAX;
}

static size_t fold_candidate_find_root_index(const fold_candidate_child_t *children,
                                             size_t child_count,
                                             nmo_object_id_t id) {
    for (size_t i = 0; children && i < child_count; ++i) {
        if (children[i].root_id == id) {
            return i;
        }
    }
    return SIZE_MAX;
}

static size_t fold_component_find(size_t *parents, size_t index) {
    while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

static void fold_component_union(size_t *parents, size_t a, size_t b) {
    size_t root_a = fold_component_find(parents, a);
    size_t root_b = fold_component_find(parents, b);
    if (root_a == root_b) {
        return;
    }
    if (root_a < root_b) {
        parents[root_b] = root_a;
    } else {
        parents[root_a] = root_b;
    }
}

static bool fold_component_append_unique_id(nmo_object_id_t **ids,
                                            size_t *count,
                                            nmo_object_id_t id) {
    if (!ids || !count || id == 0) {
        return false;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*ids)[i] == id) {
            return true;
        }
    }
    nmo_object_id_t *next = (nmo_object_id_t *)realloc(
        *ids, (*count + 1u) * sizeof(**ids));
    if (!next) {
        return false;
    }
    next[*count] = id;
    *ids = next;
    (*count)++;
    return true;
}

static bool fold_behavior_is_leaf_bb(const nmo_behavior_state_t *state) {
    return state &&
           (state->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0u &&
           state->sub_behaviors.count == 0u &&
           state->sub_behavior_links.count == 0u;
}

static bool fold_behavior_is_control_router_root(
    const nmo_behavior_state_t *state) {
    return fold_behavior_is_leaf_bb(state) &&
           state->outputs.count > 1u;
}

static bool fold_behavior_is_control_router_bridge(
    const nmo_behavior_state_t *state) {
    const uint32_t passthrough_flags =
        CKBEHAVIOR_VARIABLEINPUTS | CKBEHAVIOR_VARIABLEOUTPUTS;
    return fold_behavior_is_leaf_bb(state) &&
           state->inputs.count == 1u &&
           state->outputs.count == 1u &&
           state->in_parameters.count == 0u &&
           state->out_parameters.count == 0u &&
           state->local_parameters.count == 0u &&
           (state->flags & passthrough_flags) == passthrough_flags;
}

static void fold_candidates_union_connected_children(
    nmo_cmd_ctx_t *ctx,
    const nmo_behavior_state_t *parent,
    const fold_candidate_child_t *children,
    size_t child_count,
    size_t *parents) {
    if (!ctx || !parent || !children || child_count == 0 || !parents) {
        return;
    }

    const nmo_behavior_index_t *index =
        nmo_tool_owner_behavior_index(ctx->workspace);
    const nmo_object_id_t *link_ids = parent
        ? NMO_ARRAY_DATA(nmo_object_id_t, &parent->sub_behavior_links)
        : NULL;
    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    for (size_t i = 0; link_ids && i < parent->sub_behavior_links.count; ++i) {
        nmo_object_t *link_obj =
            repo ? nmo_object_repository_find_by_id(repo, link_ids[i]) : NULL;
        const nmo_behaviorlink_state_t *link_state =
            link_obj && nmo_object_get_class_id(link_obj) == NMO_CID_BEHAVIORLINK
                ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
                : NULL;
        if (!link_state || !index) {
            continue;
        }
        const nmo_port_owner_t *source_owner =
            nmo_behavior_index_find(index, link_state->in_io_id);
        const nmo_port_owner_t *target_owner =
            nmo_behavior_index_find(index, link_state->out_io_id);
        size_t from_index = source_owner
            ? fold_candidate_find_child_index(children, child_count,
                                              source_owner->owner_id)
            : SIZE_MAX;
        size_t to_index = target_owner
            ? fold_candidate_find_child_index(children, child_count,
                                              target_owner->owner_id)
            : SIZE_MAX;
        if (from_index == SIZE_MAX || to_index == SIZE_MAX ||
            from_index == to_index) {
            continue;
        }
        fold_component_union(parents, from_index, to_index);
    }
}

static int fold_candidates_emit_control_router_groups(
    nmo_cmd_ctx_t *ctx,
    nmo_object_id_t parent_id,
    yyjson_mut_doc *doc,
    yyjson_mut_val *groups,
    const nmo_behavior_state_t *parent,
    const fold_candidate_child_t *children,
    size_t child_count,
    size_t *inout_group_count) {
    if (!ctx || !doc || !groups || !parent || !children || child_count == 0) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    const nmo_behavior_index_t *index =
        nmo_tool_owner_behavior_index(ctx->workspace);
    const nmo_object_id_t *link_ids =
        NMO_ARRAY_DATA(nmo_object_id_t, &parent->sub_behavior_links);
    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);

    for (size_t i = 0; i < child_count; ++i) {
        if (!fold_behavior_is_control_router_root(children[i].root_state)) {
            continue;
        }

        nmo_object_id_t *router_ids = NULL;
        size_t router_count = 0;
        if (!fold_component_append_unique_id(&router_ids, &router_count,
                                             children[i].root_id)) {
            fprintf(stderr, "Error: Out of memory\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t link_idx = 0;
                 link_ids && link_idx < parent->sub_behavior_links.count;
                 ++link_idx) {
                nmo_object_t *link_obj = repo
                    ? nmo_object_repository_find_by_id(repo, link_ids[link_idx])
                    : NULL;
                const nmo_behaviorlink_state_t *link_state =
                    link_obj &&
                            nmo_object_get_class_id(link_obj) == NMO_CID_BEHAVIORLINK
                        ? (const nmo_behaviorlink_state_t *)
                              nmo_object_get_state(link_obj)
                        : NULL;
                if (!link_state || !index) {
                    continue;
                }

                const nmo_port_owner_t *source_owner =
                    nmo_behavior_index_find(index, link_state->in_io_id);
                const nmo_port_owner_t *target_owner =
                    nmo_behavior_index_find(index, link_state->out_io_id);
                size_t from_index = source_owner
                    ? fold_candidate_find_root_index(children, child_count,
                                                     source_owner->owner_id)
                    : SIZE_MAX;
                size_t to_index = target_owner
                    ? fold_candidate_find_root_index(children, child_count,
                                                     target_owner->owner_id)
                    : SIZE_MAX;
                if (from_index == SIZE_MAX || to_index == SIZE_MAX) {
                    continue;
                }

                bool from_selected = fold_id_in_list(router_ids, router_count,
                                                     children[from_index].root_id);
                bool to_selected = fold_id_in_list(router_ids, router_count,
                                                   children[to_index].root_id);
                if (from_selected == to_selected) {
                    continue;
                }

                size_t candidate_index = from_selected ? to_index : from_index;
                if (!fold_behavior_is_control_router_bridge(
                        children[candidate_index].root_state)) {
                    continue;
                }
                if (!fold_component_append_unique_id(
                        &router_ids, &router_count,
                        children[candidate_index].root_id)) {
                    free(router_ids);
                    fprintf(stderr, "Error: Out of memory\n");
                    return NMO_CLI_EXIT_INTERNAL_ERROR;
                }
                changed = true;
            }
        }

        if (router_count > 1u) {
            nmo_behavior_boundary_t router_boundary = {0};
            if (!nmo_behavior_boundary_build_for_nodes(
                    ctx->workspace, parent_id,
                    router_ids, router_count, &router_boundary)) {
                char detail[256];
                size_t detail_len = nmo_last_error_message_copy(
                    detail, sizeof(detail));
                free(router_ids);
                fprintf(stderr, "Error: %s\n",
                        detail_len > 0 ? detail
                                       : "Failed to build control router boundary");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }

            if (router_boundary.control_out_count > 1u) {
                fold_candidate_group_desc_t desc = {
                    .kind = "control_router",
                    .workspace = ctx->workspace,
                    .root_id = children[i].root_id,
                    .root_state = children[i].root_state,
                    .roots = router_ids,
                    .root_count = router_count,
                    .boundary = &router_boundary,
                };
                add_fold_candidate_group_json(doc, groups, &desc);
                (*inout_group_count)++;
            }
            nmo_behavior_boundary_free(&router_boundary);
        }

        free(router_ids);
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int fold_candidates_emit_connected_components(
    nmo_cmd_ctx_t *ctx,
    nmo_object_id_t parent_id,
    yyjson_mut_doc *doc,
    yyjson_mut_val *groups,
    const nmo_behavior_state_t *parent,
    const fold_candidate_child_t *children,
    size_t child_count,
    size_t *inout_group_count) {
    if (!ctx || !groups || !parent || !children || child_count == 0) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    size_t *parents = (size_t *)malloc(child_count * sizeof(*parents));
    if (!parents) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    for (size_t i = 0; i < child_count; ++i) {
        parents[i] = i;
    }

    fold_candidates_union_connected_children(ctx, parent, children,
                                             child_count, parents);

    bool saw_component = false;
    for (size_t i = 0; i < child_count; ++i) {
        if (fold_component_find(parents, i) != i) {
            continue;
        }

        nmo_object_id_t *component_roots = NULL;
        size_t component_root_count = 0;
        const nmo_behavior_state_t *root_state = children[i].root_state;
        nmo_object_id_t root_id = children[i].root_id;

        for (size_t j = 0; j < child_count; ++j) {
            if (fold_component_find(parents, j) != i) {
                continue;
            }
            if (!fold_component_append_unique_id(
                    &component_roots, &component_root_count,
                    children[j].root_id)) {
                free(component_roots);
                free(parents);
                fprintf(stderr, "Error: Out of memory\n");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
        }

        if (component_root_count > 1u) {
            nmo_behavior_boundary_t component_boundary = {0};
            if (!nmo_behavior_boundary_build_for_nodes(
                    ctx->workspace, parent_id,
                    component_roots, component_root_count,
                    &component_boundary)) {
                char detail[256];
                size_t detail_len = nmo_last_error_message_copy(
                    detail, sizeof(detail));
                free(component_roots);
                free(parents);
                fprintf(stderr, "Error: %s\n",
                        detail_len > 0 ? detail
                                       : "Failed to build component boundary");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }

            fold_candidate_group_desc_t desc = {
                .kind = "connected_component",
                .workspace = ctx->workspace,
                .root_id = root_id,
                .root_state = root_state,
                .roots = component_roots,
                .root_count = component_root_count,
                .boundary = &component_boundary,
            };
            add_fold_candidate_group_json(doc, groups, &desc);
            nmo_behavior_boundary_free(&component_boundary);
            (*inout_group_count)++;
            saw_component = true;
        }

        free(component_roots);
    }

    free(parents);
    return saw_component ? NMO_CLI_EXIT_SUCCESS : NMO_CLI_EXIT_SUCCESS;
}

static const char *fold_map_kind_string(nmo_behavior_fold_map_kind_t kind) {
    switch (kind) {
    case NMO_BEHAVIOR_FOLD_MAP_INPUT:
        return "input";
    case NMO_BEHAVIOR_FOLD_MAP_OUTPUT:
        return "output";
    case NMO_BEHAVIOR_FOLD_MAP_PARAMETER:
        return "parameter";
    }
    return "unknown";
}

static const char *fold_interface_mode_string(
    nmo_behavior_fold_interface_mode_t mode) {
    switch (mode) {
    case NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE:
        return "preserve";
    case NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE:
        return "canonicalize";
    case NMO_BEHAVIOR_FOLD_INTERFACE_REMOVE:
        return "remove";
    }
    return "unknown";
}

static void add_fold_maps_json(yyjson_mut_doc *doc,
                               yyjson_mut_val *obj,
                               const char *key,
                               const nmo_behavior_fold_map_t *maps,
                               size_t count) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < count; ++i) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, item, "kind",
                                  fold_map_kind_string(maps[i].kind));
        yyjson_mut_obj_add_uint(doc, item, "old_index",
                                (uint64_t)maps[i].old_index);
        yyjson_mut_obj_add_uint(doc, item, "new_index",
                                (uint64_t)maps[i].new_index);
        if (maps[i].old_id != 0) {
            yyjson_mut_obj_add_uint(doc, item, "old_id", maps[i].old_id);
        }
        if (maps[i].new_id != 0) {
            yyjson_mut_obj_add_uint(doc, item, "new_id", maps[i].new_id);
        }
        if (maps[i].label) {
            nmo_cli_json_add_str_safe(doc, item, "label", maps[i].label);
        }
        yyjson_mut_arr_add_val(arr, item);
    }
    yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static void add_fold_retarget_control_edges_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    const nmo_behavior_boundary_control_edge_t *edges,
    size_t count,
    nmo_object_id_t representative_id,
    bool incoming) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < count; ++i) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "link_id", edges[i].link_id);
        yyjson_mut_obj_add_int(doc, item, "activation_delay",
                               edges[i].activation_delay);
        yyjson_mut_obj_add_int(doc, item, "initial_activation_delay",
                               edges[i].initial_activation_delay);
        if (incoming) {
            yyjson_mut_obj_add_uint(doc, item, "source_owner_id",
                                    edges[i].source_owner_id);
            yyjson_mut_obj_add_uint(doc, item, "source_io_id",
                                    edges[i].source_io_id);
            yyjson_mut_obj_add_uint(doc, item, "old_target_owner_id",
                                    edges[i].target_owner_id);
            yyjson_mut_obj_add_uint(doc, item, "old_target_io_id",
                                    edges[i].target_io_id);
            yyjson_mut_obj_add_uint(doc, item, "new_target_owner_id",
                                    representative_id);
        } else {
            yyjson_mut_obj_add_uint(doc, item, "old_source_owner_id",
                                    edges[i].source_owner_id);
            yyjson_mut_obj_add_uint(doc, item, "old_source_io_id",
                                    edges[i].source_io_id);
            yyjson_mut_obj_add_uint(doc, item, "new_source_owner_id",
                                    representative_id);
            yyjson_mut_obj_add_uint(doc, item, "target_owner_id",
                                    edges[i].target_owner_id);
            yyjson_mut_obj_add_uint(doc, item, "target_io_id",
                                    edges[i].target_io_id);
        }
        yyjson_mut_arr_add_val(arr, item);
    }
    yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static void add_fold_retarget_parameter_edges_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    const nmo_behavior_boundary_parameter_edge_t *edges,
    size_t count,
    nmo_object_id_t representative_id,
    bool incoming) {
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < count; ++i) {
        char guid_buf[24];
        rewrite_guid_to_string(edges[i].type_guid, guid_buf,
                               sizeof(guid_buf));

        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "source_parameter_id",
                                edges[i].source_parameter_id);
        yyjson_mut_obj_add_uint(doc, item, "target_parameter_id",
                                edges[i].target_parameter_id);
        nmo_cli_json_add_str_safe(doc, item, "type_guid", guid_buf);
        yyjson_mut_obj_add_bool(doc, item, "shared", edges[i].shared);
        if (incoming) {
            yyjson_mut_obj_add_uint(doc, item, "source_owner_id",
                                    edges[i].source_owner_id);
            yyjson_mut_obj_add_uint(doc, item, "old_target_owner_id",
                                    edges[i].target_owner_id);
            yyjson_mut_obj_add_uint(doc, item, "new_target_owner_id",
                                    representative_id);
        } else {
            yyjson_mut_obj_add_uint(doc, item, "old_source_owner_id",
                                    edges[i].source_owner_id);
            yyjson_mut_obj_add_uint(doc, item, "new_source_owner_id",
                                    representative_id);
            yyjson_mut_obj_add_uint(doc, item, "target_owner_id",
                                    edges[i].target_owner_id);
        }
        yyjson_mut_arr_add_val(arr, item);
    }
    yyjson_mut_obj_add_val(doc, obj, key, arr);
}

static bool parse_fold_nodes(const char *text,
                             nmo_object_id_t *out_nodes,
                             size_t out_capacity,
                             size_t *out_count) {
    if (!text || !out_nodes || out_capacity == 0 || !out_count) {
        return false;
    }
    *out_count = 0;
    const char *p = text;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == ',') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        char token[32];
        size_t len = 0;
        while (*p != '\0' && *p != ',') {
            if (*p != ' ' && *p != '\t') {
                if (len + 1 >= sizeof(token)) {
                    return false;
                }
                token[len++] = *p;
            }
            ++p;
        }
        token[len] = '\0';
        if (len == 0 || *out_count >= out_capacity) {
            return false;
        }
        uint32_t id = 0;
        if (nmo_parse_u32_range(token, 1, UINT32_MAX, &id) != NMO_OK) {
            return false;
        }
        out_nodes[(*out_count)++] = id;
    }
    return *out_count > 0;
}

static bool parse_fold_index_map(const char *text,
                                 nmo_behavior_fold_map_kind_t kind,
                                 nmo_behavior_fold_map_t *out_map) {
    if (!text || !out_map) {
        return false;
    }
    const char *colon = strchr(text, ':');
    if (!colon || colon == text || colon[1] == '\0') {
        return false;
    }

    char left[32];
    char right[32];
    size_t left_len = (size_t)(colon - text);
    size_t right_len = strlen(colon + 1);
    if (left_len >= sizeof(left) || right_len >= sizeof(right)) {
        return false;
    }
    memcpy(left, text, left_len);
    left[left_len] = '\0';
    memcpy(right, colon + 1, right_len + 1);

    uint32_t old_index = 0;
    uint32_t new_index = 0;
    if (nmo_parse_u32_range(left, 0, UINT32_MAX, &old_index) != NMO_OK ||
        nmo_parse_u32_range(right, 0, UINT32_MAX, &new_index) != NMO_OK) {
        return false;
    }

    *out_map = (nmo_behavior_fold_map_t){
        .kind = kind,
        .old_index = old_index,
        .new_index = new_index,
    };
    return true;
}

static bool parse_fold_maps_from_argv(int argc,
                                      char **argv,
                                      const char *option,
                                      nmo_behavior_fold_map_kind_t kind,
                                      nmo_behavior_fold_map_t *out_maps,
                                      size_t out_capacity,
                                      size_t *out_count) {
    if (!argv || !option || !out_maps || !out_count) {
        return false;
    }
    *out_count = 0;

    size_t option_len = strlen(option);
    for (int i = 0; i < argc; ++i) {
        const char *value = NULL;
        if (strcmp(argv[i], option) == 0) {
            if (i + 1 >= argc) {
                return false;
            }
            value = argv[++i];
        } else if (strncmp(argv[i], option, option_len) == 0 &&
                   argv[i][option_len] == '=') {
            value = argv[i] + option_len + 1;
        } else {
            continue;
        }

        if (*out_count >= out_capacity ||
            !parse_fold_index_map(value, kind, &out_maps[*out_count])) {
            return false;
        }
        ++(*out_count);
    }
    return true;
}

static bool parse_fold_interface_mode(
    const char *text,
    nmo_behavior_fold_interface_mode_t *out_mode) {
    if (!text || !out_mode) {
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

static bool parse_fold_candidates_args(int argc,
                                       char **argv,
                                       bool expect_file_operand,
                                       fold_candidates_args_t *out_args,
                                       const char **out_file) {
    static const nmo_opt_def_t opts[] = {
        {"--parent", "-p", NMO_OPT_UINT, "Parent behavior ID"},
        {"--depth",  "-d", NMO_OPT_UINT, "Recursion depth (default: unlimited)"},
        {"--json",   "-j", NMO_OPT_FLAG, "JSON output"},
    };
    enum { OPT_PARENT, OPT_DEPTH, OPT_JSON, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 16,
    };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &result) < 0) {
        return false;
    }
    (void)vals[OPT_JSON];

    nmo_object_id_t parent_id = 0;
    const char *file_path = NULL;
    if (vals[OPT_PARENT].present) {
        parent_id = vals[OPT_PARENT].val.u;
        if (expect_file_operand) {
            if (result.pos_count != 1) {
                return false;
            }
            file_path = result.pos_args[0];
        } else if (result.pos_count != 0) {
            return false;
        }
    } else {
        if (expect_file_operand) {
            if (result.pos_count != 2) {
                return false;
            }
            if (nmo_parse_u32_range(result.pos_args[0], 1, UINT32_MAX,
                                    &parent_id) != NMO_OK) {
                return false;
            }
            file_path = result.pos_args[1];
        } else {
            if (result.pos_count != 1) {
                return false;
            }
            if (nmo_parse_u32_range(result.pos_args[0], 1, UINT32_MAX,
                                    &parent_id) != NMO_OK) {
                return false;
            }
        }
    }

    if (out_args) {
        out_args->parent_id = parent_id;
        out_args->depth = vals[OPT_DEPTH].present
            ? vals[OPT_DEPTH].val.u
            : UINT32_MAX;
    }
    if (out_file) {
        *out_file = file_path;
    }
    return parent_id != 0;
}

static int fold_candidates_emit(nmo_cmd_ctx_t *ctx,
                                const nmo_behavior_state_t *parent,
                                const nmo_behavior_boundary_t *boundary,
                                uint32_t depth) {
    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    const nmo_object_id_t *sub_ids = parent
        ? NMO_ARRAY_DATA(nmo_object_id_t, &parent->sub_behaviors)
        : NULL;
    size_t child_count = parent ? parent->sub_behaviors.count : 0u;
    fold_candidate_child_t *children = child_count > 0
        ? (fold_candidate_child_t *)calloc(child_count, sizeof(*children))
        : NULL;
    if (child_count > 0 && !children) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (size_t i = 0; i < child_count; ++i) {
        nmo_object_id_t child_id = sub_ids[i];
        const nmo_behavior_state_t *child_state =
            fold_find_behavior_state(repo, child_id);
        children[i].root_id = child_id;
        children[i].root_state = child_state;
        if (!nmo_behavior_boundary_build(ctx->workspace,
                                         child_id, depth,
                                         &children[i].boundary)) {
            char detail[256];
            size_t detail_len = nmo_last_error_message_copy(
                detail, sizeof(detail));
            fprintf(stderr, "Error: %s\n",
                    detail_len > 0 ? detail
                                   : "Failed to build child fold boundary");
            for (size_t j = 0; j <= i; ++j) {
                nmo_behavior_boundary_free(&children[j].boundary);
            }
            free(children);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        if (!doc) {
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "parent_id",
                                boundary->behavior_id);

        yyjson_mut_val *parent_obj = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, parent_obj, "behavior_type",
                                  fold_behavior_type(parent));
        yyjson_mut_obj_add_uint(doc, parent_obj, "flags",
                                parent ? (uint64_t)parent->flags : 0u);
        yyjson_mut_obj_add_bool(doc, parent_obj, "script",
                                parent &&
                                (parent->flags & CKBEHAVIOR_SCRIPT) != 0u);
        yyjson_mut_obj_add_bool(doc, parent_obj, "building_block",
                                parent &&
                                (parent->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0u);
        yyjson_mut_obj_add_uint(doc, parent_obj, "sub_behaviors",
                                parent ? (uint64_t)parent->sub_behaviors.count : 0u);
        yyjson_mut_obj_add_uint(doc, parent_obj, "sub_behavior_links",
                                parent ? (uint64_t)parent->sub_behavior_links.count : 0u);
        yyjson_mut_obj_add_uint(doc, parent_obj, "operations",
                                parent ? (uint64_t)parent->operations.count : 0u);
        yyjson_mut_obj_add_val(doc, data, "parent", parent_obj);

        yyjson_mut_val *groups = yyjson_mut_arr(doc);
        size_t group_count = 0;
        fold_candidate_group_desc_t parent_desc = {
            .kind = "parent_recursive",
            .workspace = ctx->workspace,
            .root_id = boundary->behavior_id,
            .root_state = parent,
            .roots = &boundary->behavior_id,
            .root_count = 1u,
            .boundary = boundary,
        };
        add_fold_candidate_group_json(doc, groups, &parent_desc);
        ++group_count;

        exit_code = fold_candidates_emit_control_router_groups(
            ctx, boundary->behavior_id, doc, groups, parent, children,
            child_count, &group_count);
        if (exit_code != NMO_CLI_EXIT_SUCCESS) {
            yyjson_mut_doc_free(doc);
            goto cleanup;
        }

        exit_code = fold_candidates_emit_connected_components(
            ctx, boundary->behavior_id, doc, groups, parent, children,
            child_count, &group_count);
        if (exit_code != NMO_CLI_EXIT_SUCCESS) {
            yyjson_mut_doc_free(doc);
            goto cleanup;
        }

        for (size_t i = 0; i < child_count; ++i) {
            fold_candidate_group_desc_t child_desc = {
                .kind = "direct_child",
                .workspace = ctx->workspace,
                .root_id = children[i].root_id,
                .root_state = children[i].root_state,
                .roots = &children[i].root_id,
                .root_count = 1u,
                .boundary = &children[i].boundary,
            };
            add_fold_candidate_group_json(doc, groups, &child_desc);
            ++group_count;
        }
        yyjson_mut_obj_add_uint(doc, data, "candidate_group_count",
                                (uint64_t)group_count);
        yyjson_mut_obj_add_val(doc, data, "candidate_groups", groups);

        exit_code = nmo_cmd_ctx_json_end(ctx, doc, data,
                                         "behavior.fold-candidates");
        goto cleanup;
    }

    fprintf(ctx->out, "Fold candidates for behavior #%u (%s)\n",
            boundary->behavior_id, fold_behavior_type(parent));
    fprintf(ctx->out, "Candidate parent_recursive: nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu\n",
            boundary->internal_node_count,
            boundary->control_in_count,
            boundary->control_out_count,
            boundary->parameter_in_count,
            boundary->parameter_out_count);

    size_t *parents = child_count > 0
        ? (size_t *)malloc(child_count * sizeof(*parents))
        : NULL;
    if (child_count > 0 && !parents) {
        fprintf(stderr, "Error: Out of memory\n");
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }
    for (size_t i = 0; i < child_count; ++i) {
        parents[i] = i;
    }
    if (child_count > 0) {
        for (size_t i = 0; i < child_count; ++i) {
            if (!fold_behavior_is_control_router_root(children[i].root_state)) {
                continue;
            }
            nmo_object_id_t *router_ids = NULL;
            size_t router_count = 0;
            if (!fold_component_append_unique_id(&router_ids, &router_count,
                                                 children[i].root_id)) {
                free(router_ids);
                free(parents);
                fprintf(stderr, "Error: Out of memory\n");
                exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }

            bool changed = true;
            while (changed) {
                changed = false;
                const nmo_behavior_index_t *index =
                    nmo_tool_owner_behavior_index(ctx->workspace);
                const nmo_object_id_t *link_ids =
                    NMO_ARRAY_DATA(nmo_object_id_t, &parent->sub_behavior_links);
                for (size_t link_idx = 0;
                     link_ids && link_idx < parent->sub_behavior_links.count;
                     ++link_idx) {
                    nmo_object_t *link_obj = repo
                        ? nmo_object_repository_find_by_id(repo, link_ids[link_idx])
                        : NULL;
                    const nmo_behaviorlink_state_t *link_state =
                        link_obj &&
                                nmo_object_get_class_id(link_obj) == NMO_CID_BEHAVIORLINK
                            ? (const nmo_behaviorlink_state_t *)
                                  nmo_object_get_state(link_obj)
                            : NULL;
                    if (!link_state || !index) {
                        continue;
                    }
                    const nmo_port_owner_t *source_owner =
                        nmo_behavior_index_find(index, link_state->in_io_id);
                    const nmo_port_owner_t *target_owner =
                        nmo_behavior_index_find(index, link_state->out_io_id);
                    size_t from_index = source_owner
                        ? fold_candidate_find_root_index(children, child_count,
                                                         source_owner->owner_id)
                        : SIZE_MAX;
                    size_t to_index = target_owner
                        ? fold_candidate_find_root_index(children, child_count,
                                                         target_owner->owner_id)
                        : SIZE_MAX;
                    if (from_index == SIZE_MAX || to_index == SIZE_MAX) {
                        continue;
                    }
                    bool from_selected =
                        fold_id_in_list(router_ids, router_count,
                                        children[from_index].root_id);
                    bool to_selected =
                        fold_id_in_list(router_ids, router_count,
                                        children[to_index].root_id);
                    if (from_selected == to_selected) {
                        continue;
                    }
                    size_t candidate_index =
                        from_selected ? to_index : from_index;
                    if (!fold_behavior_is_control_router_bridge(
                            children[candidate_index].root_state)) {
                        continue;
                    }
                    if (!fold_component_append_unique_id(
                            &router_ids, &router_count,
                            children[candidate_index].root_id)) {
                        free(router_ids);
                        free(parents);
                        fprintf(stderr, "Error: Out of memory\n");
                        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                        goto cleanup;
                    }
                    changed = true;
                }
            }
            if (router_count > 1u) {
                nmo_behavior_boundary_t router_boundary = {0};
                if (!nmo_behavior_boundary_build_for_nodes(
                        ctx->workspace, boundary->behavior_id,
                        router_ids, router_count, &router_boundary)) {
                    char detail[256];
                    size_t detail_len = nmo_last_error_message_copy(
                        detail, sizeof(detail));
                    free(router_ids);
                    free(parents);
                    fprintf(stderr, "Error: %s\n",
                            detail_len > 0 ? detail
                                           : "Failed to build control router boundary");
                    exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                    goto cleanup;
                }
                if (router_boundary.control_out_count > 1u) {
                    fprintf(ctx->out,
                            "Candidate control_router #%u: roots=%zu nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu interface=%s\n",
                            children[i].root_id, router_count,
                            router_boundary.internal_node_count,
                            router_boundary.control_in_count,
                            router_boundary.control_out_count,
                            router_boundary.parameter_in_count,
                            router_boundary.parameter_out_count,
                            fold_interface_action(children[i].root_state));
                }
                nmo_behavior_boundary_free(&router_boundary);
            }
            free(router_ids);
        }

        fold_candidates_union_connected_children(ctx, parent, children,
                                                 child_count, parents);
        for (size_t i = 0; i < child_count; ++i) {
            if (fold_component_find(parents, i) != i) {
                continue;
            }
            nmo_object_id_t *component_roots = NULL;
            size_t root_count = 0;
            for (size_t j = 0; j < child_count; ++j) {
                if (fold_component_find(parents, j) == i) {
                    if (!fold_component_append_unique_id(
                            &component_roots, &root_count,
                            children[j].root_id)) {
                        free(component_roots);
                        free(parents);
                        fprintf(stderr, "Error: Out of memory\n");
                        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                        goto cleanup;
                    }
                }
            }
            if (root_count > 1u) {
                nmo_behavior_boundary_t component_boundary = {0};
                if (!nmo_behavior_boundary_build_for_nodes(
                        ctx->workspace, boundary->behavior_id,
                        component_roots, root_count,
                        &component_boundary)) {
                    char detail[256];
                    size_t detail_len = nmo_last_error_message_copy(
                        detail, sizeof(detail));
                    free(component_roots);
                    free(parents);
                    fprintf(stderr, "Error: %s\n",
                            detail_len > 0 ? detail
                                           : "Failed to build component boundary");
                    exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                    goto cleanup;
                }
                fprintf(ctx->out,
                        "Candidate connected_component #%u: roots=%zu nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu interface=%s\n",
                        children[i].root_id, root_count,
                        component_boundary.internal_node_count,
                        component_boundary.control_in_count,
                        component_boundary.control_out_count,
                        component_boundary.parameter_in_count,
                        component_boundary.parameter_out_count,
                        fold_interface_action(children[i].root_state));
                nmo_behavior_boundary_free(&component_boundary);
            }
            free(component_roots);
        }
        free(parents);
    }

    for (size_t i = 0; i < child_count; ++i) {
        fprintf(ctx->out, "Candidate direct_child #%u (%s): nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu interface=%s\n",
                children[i].root_id,
                fold_behavior_type(children[i].root_state),
                children[i].boundary.internal_node_count,
                children[i].boundary.control_in_count,
                children[i].boundary.control_out_count,
                children[i].boundary.parameter_in_count,
                children[i].boundary.parameter_out_count,
                fold_interface_action(children[i].root_state));
    }

cleanup:
    for (size_t i = 0; i < child_count; ++i) {
        nmo_behavior_boundary_free(&children[i].boundary);
    }
    free(children);
    return exit_code;
}

static int fold_candidates_run(nmo_cmd_ctx_t *ctx,
                               const fold_candidates_args_t *args,
                               bool close_ctx,
                               const char *usage) {
    nmo_cmd_ctx_t c = *ctx;
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    nmo_behavior_boundary_t boundary = {0};

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    nmo_object_t *object = repo
        ? nmo_object_repository_find_by_id(repo, args->parent_id)
        : NULL;
    if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        fprintf(stderr, "Error: Parent behavior %u not found\n",
                args->parent_id);
        fprintf(stderr, "Usage: %s\n", usage);
        exit_code = NMO_CLI_EXIT_ARG_ERROR;
        goto cleanup;
    }

    const nmo_behavior_state_t *parent =
        (const nmo_behavior_state_t *)nmo_object_get_state(object);
    if (!parent) {
        fprintf(stderr, "Error: Parent behavior state unavailable\n");
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    if (!nmo_behavior_boundary_build(c.workspace,
                                     args->parent_id,
                                     args->depth,
                                     &boundary)) {
        char detail[256];
        size_t detail_len = nmo_last_error_message_copy(detail,
                                                        sizeof(detail));
        fprintf(stderr, "Error: %s\n",
                detail_len > 0 ? detail : "Failed to build fold boundary");
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    exit_code = fold_candidates_emit(&c, parent, &boundary, args->depth);

cleanup:
    nmo_behavior_boundary_free(&boundary);
    return close_ctx ? nmo_cmd_ctx_done(&c, exit_code) : exit_code;
}

int nmo_cmd_behavior_fold_candidates(int argc,
                                     char **argv,
                                     const nmo_cli_global_opts_t *global) {
    fold_candidates_args_t args = {0};
    const char *file_path = NULL;
    const char *usage =
        "nmo behavior fold-candidates --parent <id> <file>";

    if (!parse_fold_candidates_args(argc, argv, true, &args, &file_path)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) {
        return rc;
    }
    (void)file_path;
    return fold_candidates_run(&c, &args, true, usage);
}

int nmo_cmd_behavior_fold_candidates_in_session(nmo_cmd_ctx_t *ctx,
                                                int argc,
                                                char **argv) {
    fold_candidates_args_t args = {0};
    const char *file_path = NULL;
    const char *usage =
        "behavior fold-candidates --parent <id>";

    if (!parse_fold_candidates_args(argc, argv, false, &args, &file_path)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    (void)file_path;
    return fold_candidates_run(ctx, &args, false, usage);
}

static bool parse_fold_args(int argc,
                            char **argv,
                            fold_args_t *out_args,
                            const char **out_file) {
    static const nmo_opt_def_t opts[] = {
        {"--parent",          "-p", NMO_OPT_UINT,   "Parent behavior ID"},
        {"--nodes",           NULL, NMO_OPT_STRING, "Comma-separated node IDs"},
        {"--anchor",          NULL, NMO_OPT_UINT,   "Anchor behavior ID"},
        {"--bb-guid",         NULL, NMO_OPT_STRING, "Target BB GUID"},
        {"--name",            NULL, NMO_OPT_STRING, "Target BB name"},
        {"--version",         NULL, NMO_OPT_UINT,   "Target BB version"},
        {"--preserve-boundary", NULL, NMO_OPT_FLAG,
         "Require full behavior boundary preservation"},
        {"--preserve-links",  NULL, NMO_OPT_FLAG,   "Require control boundary preservation"},
        {"--preserve-params", NULL, NMO_OPT_FLAG,   "Require parameter boundary preservation"},
        {"--map-input",       NULL, NMO_OPT_STRING, "Map input old_index:new_index"},
        {"--map-output",      NULL, NMO_OPT_STRING, "Map output old_index:new_index"},
        {"--map-param",       NULL, NMO_OPT_STRING, "Map parameter old_index:new_index"},
        {"--interface",       NULL, NMO_OPT_STRING, "Interface mode: preserve|canonicalize|remove"},
        {"--output",          "-o", NMO_OPT_STRING, "Output file"},
        {"--dry-run",         NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum {
        OPT_PARENT,
        OPT_NODES,
        OPT_ANCHOR,
        OPT_GUID,
        OPT_NAME,
        OPT_VERSION,
        OPT_PRESERVE_BOUNDARY,
        OPT_PRESERVE_LINKS,
        OPT_PRESERVE_PARAMS,
        OPT_MAP_INPUT,
        OPT_MAP_OUTPUT,
        OPT_MAP_PARAM,
        OPT_INTERFACE,
        OPT_OUTPUT,
        OPT_DRY_RUN,
        OPT_COUNT
    };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 16,
    };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) {
        return false;
    }
    if (!vals[OPT_PARENT].present || !vals[OPT_NODES].present ||
        !vals[OPT_GUID].present || !vals[OPT_NAME].present ||
        r.pos_count != 1) {
        return false;
    }

    fold_args_t args = {0};
    args.parent_id = vals[OPT_PARENT].val.u;
    args.anchor_id = vals[OPT_ANCHOR].present ? vals[OPT_ANCHOR].val.u : 0;
    args.block_guid = nmo_guid_parse(vals[OPT_GUID].val.str);
    args.name = vals[OPT_NAME].val.str;
    args.block_version = vals[OPT_VERSION].present
        ? vals[OPT_VERSION].val.u
        : 65536u;
    args.interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE;
    args.preserve_boundary = vals[OPT_PRESERVE_BOUNDARY].present &&
                             vals[OPT_PRESERVE_BOUNDARY].val.flag;
    args.preserve_links = args.preserve_boundary ||
                          (vals[OPT_PRESERVE_LINKS].present &&
                           vals[OPT_PRESERVE_LINKS].val.flag);
    args.preserve_params = args.preserve_boundary ||
                           (vals[OPT_PRESERVE_PARAMS].present &&
                            vals[OPT_PRESERVE_PARAMS].val.flag);
    args.dry_run = vals[OPT_DRY_RUN].present &&
                   vals[OPT_DRY_RUN].val.flag;
    args.output_path = vals[OPT_OUTPUT].present
        ? vals[OPT_OUTPUT].val.str
        : NULL;
    if (!parse_fold_maps_from_argv(
            argc, argv, "--map-input", NMO_BEHAVIOR_FOLD_MAP_INPUT,
            args.input_maps,
            sizeof(args.input_maps) / sizeof(args.input_maps[0]),
            &args.input_map_count) ||
        !parse_fold_maps_from_argv(
            argc, argv, "--map-output", NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            args.output_maps,
            sizeof(args.output_maps) / sizeof(args.output_maps[0]),
            &args.output_map_count) ||
        !parse_fold_maps_from_argv(
            argc, argv, "--map-param", NMO_BEHAVIOR_FOLD_MAP_PARAMETER,
            args.parameter_maps,
            sizeof(args.parameter_maps) / sizeof(args.parameter_maps[0]),
            &args.parameter_map_count)) {
        return false;
    }
    if (vals[OPT_INTERFACE].present &&
        !parse_fold_interface_mode(vals[OPT_INTERFACE].val.str,
                                   &args.interface_mode)) {
        return false;
    }
    if (args.parent_id == 0 || nmo_guid_is_null(args.block_guid) ||
        !parse_fold_nodes(vals[OPT_NODES].val.str, args.nodes,
                          sizeof(args.nodes) / sizeof(args.nodes[0]),
                          &args.node_count)) {
        return false;
    }

    if (out_args) {
        *out_args = args;
    }
    if (out_file) {
        *out_file = r.pos_args[0];
    }
    return true;
}

static int fold_emit_dry_run(nmo_cmd_ctx_t *ctx,
                             const nmo_behavior_state_t *parent,
                             const nmo_behavior_state_t *representative,
                             const nmo_behavior_fold_report_t *report,
                             nmo_edit_report_t *edit_report) {
    const nmo_behavior_boundary_t *boundary = &report->boundary;
    nmo_object_id_t representative_id = report->representative_id;
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        if (!doc) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        if (edit_report != NULL) {
            add_edit_report_json(doc, data, edit_report, NULL);
        } else {
            nmo_edit_report_t analysis_report = {0};
            analysis_report.ok = true;
            analysis_report.dry_run = true;
            analysis_report.semantic_risks = report->semantic_risks;
            analysis_report.semantic_risk_count =
                report->semantic_risk_count;
            nmo_cli_edit_report_add_schema_v2_json(
                doc, data, &analysis_report, true);
        }
        yyjson_mut_obj_add_bool(doc, data, "can_write",
                                report->can_write);
        yyjson_mut_obj_add_bool(doc, data, "write_supported",
                                report->can_write);
        nmo_cli_json_add_str_safe(doc, data, "status",
                                  report->can_write ? "ready"
                                                    : "analysis_only");
        yyjson_mut_val *write_blockers = yyjson_mut_arr(doc);
        for (size_t i = 0; i < report->write_blocker_count; ++i) {
            yyjson_mut_val *blocker = yyjson_mut_obj(doc);
            nmo_cli_json_add_str_safe(doc, blocker, "code",
                                      report->write_blockers[i].code);
            nmo_cli_json_add_str_safe(doc, blocker, "message",
                                      report->write_blockers[i].message);
            yyjson_mut_arr_add_val(write_blockers, blocker);
        }
        yyjson_mut_obj_add_val(doc, data, "write_blockers",
                               write_blockers);
        yyjson_mut_obj_add_uint(doc, data, "parent_id", report->parent_id);
        yyjson_mut_obj_add_uint(doc, data, "anchor_id", report->anchor_id);
        nmo_cli_json_add_str_safe(doc, data, "parent_behavior_type",
                                  fold_behavior_type(parent));
        yyjson_mut_obj_add_uint(doc, data, "representative_id",
                                representative_id);
        nmo_cli_json_add_str_safe(doc, data,
                                  "representative_behavior_type",
                                  fold_behavior_type(representative));
        add_id_list_json(doc, data, "selected_nodes",
                         report->selected_nodes,
                         report->selected_node_count);
        yyjson_mut_obj_add_bool(doc, data, "preserve_boundary",
                                report->preserve_boundary);
        yyjson_mut_obj_add_bool(doc, data, "preserve_links",
                                report->preserve_links);
        yyjson_mut_obj_add_bool(doc, data, "preserve_params",
                                report->preserve_params);
        nmo_cli_json_add_str_safe(
            doc, data, "interface_mode",
            fold_interface_mode_string(report->interface_mode));
        yyjson_mut_val *maps = yyjson_mut_obj(doc);
        add_fold_maps_json(doc, maps, "inputs",
                           report->input_maps,
                           report->input_map_count);
        add_fold_maps_json(doc, maps, "outputs",
                           report->output_maps,
                           report->output_map_count);
        add_fold_maps_json(doc, maps, "parameters",
                           report->parameter_maps,
                           report->parameter_map_count);
        yyjson_mut_obj_add_val(doc, data, "maps", maps);

        char guid_buf[24];
        rewrite_guid_to_string(report->target_guid, guid_buf,
                               sizeof(guid_buf));
        yyjson_mut_val *target = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, target, "guid", guid_buf);
        nmo_cli_json_add_str_safe(doc, target, "name", report->target_name);
        yyjson_mut_obj_add_uint(doc, target, "version",
                                (uint64_t)report->target_version);
        yyjson_mut_obj_add_val(doc, data, "target", target);

        yyjson_mut_val *planned = yyjson_mut_obj(doc);
        add_internal_nodes_json(doc, planned, boundary);
        add_id_list_json(doc, planned, "nodes_to_delete",
                         report->nodes_to_delete,
                         report->nodes_to_delete_count);
        yyjson_mut_val *delete_links = yyjson_mut_obj(doc);
        add_control_edges_json(doc, delete_links, "control",
                               report->control_links_to_delete,
                               report->control_links_to_delete_count);
        yyjson_mut_obj_add_val(doc, planned, "links_to_delete",
                               delete_links);
        yyjson_mut_val *links = yyjson_mut_obj(doc);
        add_control_edges_json(doc, links, "control_in",
                               boundary->control_in,
                               boundary->control_in_count);
        add_control_edges_json(doc, links, "control_out",
                               boundary->control_out,
                               boundary->control_out_count);
        yyjson_mut_obj_add_val(doc, planned, "links_to_move", links);

        yyjson_mut_val *retarget = yyjson_mut_obj(doc);
        add_fold_retarget_control_edges_json(doc, retarget, "control_in",
                                             boundary->control_in,
                                             boundary->control_in_count,
                                             representative_id, true);
        add_fold_retarget_control_edges_json(doc, retarget, "control_out",
                                             boundary->control_out,
                                             boundary->control_out_count,
                                             representative_id, false);
        yyjson_mut_obj_add_val(doc, planned, "links_to_retarget",
                               retarget);

        yyjson_mut_val *params = yyjson_mut_obj(doc);
        add_parameter_edges_json(doc, params, "parameter_in",
                                 boundary->parameter_in,
                                 boundary->parameter_in_count);
        add_parameter_edges_json(doc, params, "parameter_out",
                                 boundary->parameter_out,
                                 boundary->parameter_out_count);
        yyjson_mut_obj_add_val(doc, planned,
                               "parameters_to_preserve", params);
        yyjson_mut_val *param_retarget = yyjson_mut_obj(doc);
        add_fold_retarget_parameter_edges_json(
            doc, param_retarget, "parameter_in",
            boundary->parameter_in, boundary->parameter_in_count,
            representative_id, true);
        add_fold_retarget_parameter_edges_json(
            doc, param_retarget, "parameter_out",
            boundary->parameter_out, boundary->parameter_out_count,
            representative_id, false);
        yyjson_mut_obj_add_val(doc, planned, "parameters_to_retarget",
                               param_retarget);
        add_fold_interface_json(doc, planned, representative);
        yyjson_mut_obj_add_uint(doc, planned, "node_count",
                                (uint64_t)boundary->internal_node_count);
        yyjson_mut_obj_add_uint(doc, planned, "control_in_count",
                                (uint64_t)boundary->control_in_count);
        yyjson_mut_obj_add_uint(doc, planned, "control_out_count",
                                (uint64_t)boundary->control_out_count);
        yyjson_mut_obj_add_uint(doc, planned, "parameter_in_count",
                                (uint64_t)boundary->parameter_in_count);
        yyjson_mut_obj_add_uint(doc, planned, "parameter_out_count",
                                (uint64_t)boundary->parameter_out_count);
        yyjson_mut_obj_add_uint(doc, planned, "delete_link_count",
                                (uint64_t)report->control_links_to_delete_count);
        yyjson_mut_obj_add_val(doc, data, "planned", planned);

        return nmo_cmd_ctx_json_end(ctx, doc, data, "behavior.fold");
    }

    fprintf(ctx->out, "[dry-run] Fold behavior #%u into BB %08X-%08X\n",
            representative_id, report->target_guid.d1,
            report->target_guid.d2);
    fprintf(ctx->out, "Parent #%u (%s), representative #%u (%s)\n",
            report->parent_id, fold_behavior_type(parent),
            representative_id, fold_behavior_type(representative));
    fprintf(ctx->out,
            "Planned: nodes=%zu delete=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu interface=%s\n",
            boundary->internal_node_count,
            boundary->internal_node_count > 0
                ? boundary->internal_node_count - 1
                : 0,
            boundary->control_in_count,
            boundary->control_out_count,
            boundary->parameter_in_count,
            boundary->parameter_out_count,
            fold_interface_action(representative));
    fprintf(ctx->out, "Delete links: %zu\n",
            report->control_links_to_delete_count);
    fprintf(ctx->out, "Can write: %s\n", report->can_write ? "yes" : "no");
    for (size_t i = 0; i < report->write_blocker_count; ++i) {
        fprintf(ctx->out, "Write blocker: %s",
                report->write_blockers[i].code);
        if (report->write_blockers[i].message) {
            fprintf(ctx->out, " - %s",
                    report->write_blockers[i].message);
        }
        fputc('\n', ctx->out);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int fold_emit_rejection(nmo_cmd_ctx_t *ctx,
                               const nmo_behavior_fold_report_t *report,
                               int exit_code) {
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        if (!doc) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "ok", false);
        yyjson_mut_obj_add_bool(doc, data, "dry_run",
                                report->analysis_only);
        yyjson_mut_obj_add_bool(doc, data, "rejected",
                                report->rejected);
        yyjson_mut_obj_add_bool(doc, data, "can_write",
                                report->can_write);
        yyjson_mut_obj_add_uint(doc, data, "parent_id",
                                report->parent_id);
        yyjson_mut_obj_add_uint(doc, data, "anchor_id",
                                report->anchor_id);
        add_id_list_json(doc, data, "selected_nodes",
                         report->selected_nodes,
                         report->selected_node_count);
        nmo_cli_edit_report_add_semantic_risk_array_json(
            doc, data, report->semantic_risks,
            report->semantic_risk_count);

        yyjson_mut_val *rejections = yyjson_mut_arr(doc);
        yyjson_mut_val *rejection = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, rejection, "code",
                                  report->diagnostic_code);
        nmo_cli_json_add_str_safe(doc, rejection, "message",
                                  report->diagnostic_message);
        yyjson_mut_arr_add_val(rejections, rejection);
        yyjson_mut_obj_add_val(doc, data, "rejections", rejections);

        yyjson_mut_val *maps = yyjson_mut_obj(doc);
        add_fold_maps_json(doc, maps, "inputs",
                           report->input_maps,
                           report->input_map_count);
        add_fold_maps_json(doc, maps, "outputs",
                           report->output_maps,
                           report->output_map_count);
        add_fold_maps_json(doc, maps, "parameters",
                           report->parameter_maps,
                           report->parameter_map_count);
        yyjson_mut_obj_add_val(doc, data, "maps", maps);

        int json_rc = nmo_cmd_ctx_json_end(ctx, doc, data,
                                           "behavior.fold");
        return json_rc == NMO_CLI_EXIT_SUCCESS ? exit_code : json_rc;
    }

    if (report->diagnostic_message) {
        fprintf(stderr, "Error: behavior fold rejected");
        if (report->diagnostic_code) {
            fprintf(stderr, " (%s)", report->diagnostic_code);
        }
        fprintf(stderr, ": %s\n", report->diagnostic_message);
    } else {
        fprintf(stderr, "Error: Failed to analyze behavior fold\n");
    }
    return exit_code;
}

int nmo_cmd_behavior_fold(int argc,
                          char **argv,
                          const nmo_cli_global_opts_t *global) {
    fold_args_t args = {0};
    const char *file_path = NULL;
    const char *usage =
        "nmo behavior fold --parent <id> --nodes <id,...> "
        "--bb-guid <guid> --name <name> [--dry-run] <file> -o <output>";

    if (!parse_fold_args(argc, argv, &args, &file_path)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!args.dry_run && (!args.output_path || args.output_path[0] == '\0')) {
        fprintf(stderr, "Error: behavior fold write requires -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_behavior_fold_report_t report = {0};
    nmo_edit_plan_t *edit_plan = NULL;
    nmo_edit_report_t edit_report;
    bool edit_report_ready = false;
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    const nmo_behavior_state_t *parent =
        fold_find_behavior_state(repo, args.parent_id);
    const nmo_behavior_state_t *representative =
        fold_find_behavior_state(repo, args.nodes[0]);
    if (!parent || !representative) {
        fprintf(stderr, "Error: Parent or representative behavior not found\n");
        rc = NMO_CLI_EXIT_ARG_ERROR;
        goto cleanup;
    }

    nmo_behavior_fold_desc_t desc = {
        .parent_id = args.parent_id,
        .node_ids = args.nodes,
        .node_count = args.node_count,
        .anchor_id = args.anchor_id,
        .block_guid = args.block_guid,
        .name = args.name,
        .block_version = args.block_version,
        .preserve_boundary = args.preserve_boundary,
        .preserve_links = args.preserve_links,
        .preserve_params = args.preserve_params,
        .input_maps = args.input_maps,
        .input_map_count = args.input_map_count,
        .output_maps = args.output_maps,
        .output_map_count = args.output_map_count,
        .parameter_maps = args.parameter_maps,
        .parameter_map_count = args.parameter_map_count,
        .interface_mode = args.interface_mode,
    };
    nmo_workspace_t *workspace = c.workspace;
    nmo_status_t fold_rc = NMO_OK;
    if (args.dry_run) {
        fold_rc = nmo_behavior_edit_fold_analyze(workspace, &desc, &report);
        if (fold_rc == NMO_OK) {
            fold_rc = nmo_edit_report_init(&edit_report);
            if (fold_rc == NMO_OK) {
                edit_report_ready = true;
                fold_rc = nmo_edit_plan_create(&edit_plan);
            }
        }
        if (fold_rc == NMO_OK) {
            fold_rc = nmo_edit_plan_add_fold(edit_plan, &desc);
        }
        if (fold_rc == NMO_OK) {
            nmo_edit_executor_options_t options =
                nmo_edit_executor_options_default();
            options.dry_run = true;
            fold_rc = nmo_edit_executor_execute(
                workspace, edit_plan, &options, &edit_report);
        }
    } else {
        fold_rc = nmo_edit_report_init(&edit_report);
        if (fold_rc == NMO_OK) {
            edit_report_ready = true;
            fold_rc = nmo_edit_plan_create(&edit_plan);
        }
        if (fold_rc == NMO_OK) {
            fold_rc = nmo_edit_plan_add_fold(edit_plan, &desc);
        }
        if (fold_rc == NMO_OK) {
            nmo_edit_executor_options_t options =
                nmo_edit_executor_options_default();
            options.dry_run = false;
            fold_rc = nmo_edit_executor_execute(
                workspace, edit_plan, &options, &edit_report);
        }
    }
    if (fold_rc != NMO_OK) {
        rc = (fold_rc == NMO_ERR_INVALID_ARGUMENT ||
              fold_rc == NMO_ERR_NOT_FOUND)
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
        if (args.dry_run) {
            rc = fold_emit_rejection(&c, &report, rc);
        } else if (c.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
            if (!doc) {
                rc = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }
            yyjson_mut_val *data = yyjson_mut_obj(doc);
            add_edit_report_json(
                doc, data, edit_report_ready ? &edit_report : NULL,
                args.output_path);
            yyjson_mut_obj_add_uint(doc, data, "parent_id",
                                    (uint64_t)args.parent_id);
            yyjson_mut_obj_add_uint(doc, data, "anchor_id",
                                    (uint64_t)args.anchor_id);
            rc = nmo_cmd_ctx_json_end(&c, doc, data, "behavior.fold");
        } else {
            const nmo_edit_operation_result_t *failed_op =
                edit_report_ready && edit_report.operation_count > 0u
                    ? &edit_report.operations[0]
                    : NULL;
            fprintf(stderr, "Error: behavior fold rejected: %s",
                    nmo_error_string(fold_rc));
            if (failed_op && failed_op->diagnostic_code) {
                fprintf(stderr, " (%s)", failed_op->diagnostic_code);
            }
            if (failed_op && failed_op->diagnostic_message) {
                fprintf(stderr, ": %s", failed_op->diagnostic_message);
            }
            fputc('\n', stderr);
        }
        goto cleanup;
    }

    if (args.dry_run) {
        rc = fold_emit_dry_run(&c, parent, representative, &report,
                               edit_report_ready ? &edit_report : NULL);
    } else {
        nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
        rc = nmo_cli_save_document(c.document, args.output_path, &save_opts);
        if (rc == NMO_CLI_EXIT_SUCCESS) {
            if (c.is_json) {
                yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
                if (!doc) {
                    rc = NMO_CLI_EXIT_INTERNAL_ERROR;
                    goto cleanup;
                }
                yyjson_mut_val *data = yyjson_mut_obj(doc);
                add_edit_report_json(doc, data, &edit_report,
                                     args.output_path);
                yyjson_mut_obj_add_bool(doc, data, "can_write",
                                        true);
                yyjson_mut_obj_add_uint(doc, data, "parent_id",
                                        (uint64_t)args.parent_id);
                yyjson_mut_obj_add_uint(doc, data, "anchor_id",
                                        edit_report.operation_count > 0u
                                            ? (uint64_t)edit_report
                                                  .operations[0]
                                                  .result_id
                                            : (uint64_t)args.anchor_id);
                nmo_cli_json_add_str_safe(doc, data, "output",
                                          args.output_path);
                rc = nmo_cmd_ctx_json_end(&c, doc, data,
                                          "behavior.fold");
            } else {
                fprintf(c.out, "Saved to: %s\n", args.output_path);
            }
        }
    }

cleanup:
    if (edit_report_ready) {
        nmo_edit_report_dispose(&edit_report);
    }
    nmo_edit_plan_destroy(edit_plan);
    nmo_behavior_edit_fold_report_free(&report);
    return nmo_cmd_ctx_done(&c, rc);
}

typedef struct replace_bb_args {
    nmo_behavior_replace_bb_desc_t desc;
    nmo_edit_plan_t *edit_plan;
    nmo_edit_report_t edit_report;
    bool edit_report_ready;
} replace_bb_args_t;

static int replace_bb_mutate(nmo_cmd_ctx_t *c,
                             bool dry_run,
                             const char *output_path,
                             void *user_data) {
    (void)output_path;
    replace_bb_args_t *args = (replace_bb_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_workspace_t *workspace = c->workspace;
    nmo_status_t rc = nmo_edit_report_init(&args->edit_report);
    if (rc == NMO_OK) {
        args->edit_report_ready = true;
        rc = nmo_edit_plan_create(&args->edit_plan);
    }
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_replace_bb(args->edit_plan, &args->desc);
    }
    if (rc == NMO_OK) {
        nmo_edit_executor_options_t options =
            nmo_edit_executor_options_default();
        options.dry_run = dry_run;
        rc = nmo_edit_executor_execute(
            workspace, args->edit_plan, &options, &args->edit_report);
    }
    if (rc != NMO_OK) {
        const nmo_edit_operation_result_t *failed_op =
            args->edit_report_ready && args->edit_report.operation_count > 0u
                ? &args->edit_report.operations[0]
                : NULL;
        fprintf(stderr, "Error: behavior replace-bb failed: %s",
                nmo_error_string(rc));
        if (failed_op && failed_op->diagnostic_code) {
            fprintf(stderr, " (%s)", failed_op->diagnostic_code);
        }
        if (failed_op && failed_op->diagnostic_message) {
            fprintf(stderr, ": %s", failed_op->diagnostic_message);
        }
        fputc('\n', stderr);
        if (args->edit_report_ready) {
            nmo_edit_report_dispose(&args->edit_report);
            nmo_edit_plan_destroy(args->edit_plan);
            args->edit_plan = NULL;
            args->edit_report_ready = false;
        }
        return (rc == NMO_ERR_INVALID_ARGUMENT || rc == NMO_ERR_NOT_FOUND)
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int replace_bb_report(nmo_cmd_ctx_t *c,
                             bool dry_run,
                             const char *output_path,
                             void *user_data) {
    replace_bb_args_t *args = (replace_bb_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (!doc) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        add_edit_report_json(doc, data, &args->edit_report, output_path);
        if (output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        int json_rc = nmo_cmd_ctx_json_end(c, doc, data,
                                           "behavior.replace-bb");
        if (args->edit_report_ready) {
            nmo_edit_report_dispose(&args->edit_report);
            nmo_edit_plan_destroy(args->edit_plan);
            args->edit_plan = NULL;
            args->edit_report_ready = false;
        }
        return json_rc;
    }

    if (dry_run) {
        fprintf(c->out, "[dry-run] ");
    }
    fprintf(c->out, "Replaced leaf BB #%u\n",
            args->desc.behavior_id);
    if (!dry_run && output_path) {
        fprintf(c->out, "Saved to: %s\n", output_path);
    }
    if (args->edit_report_ready) {
        nmo_edit_report_dispose(&args->edit_report);
        nmo_edit_plan_destroy(args->edit_plan);
        args->edit_plan = NULL;
        args->edit_report_ready = false;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_behavior_replace_bb(int argc,
                                char **argv,
                                const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--bb-guid",         NULL, NMO_OPT_STRING, "Replacement BB GUID"},
        {"--name",            NULL, NMO_OPT_STRING, "Replacement BB name"},
        {"--version",         NULL, NMO_OPT_UINT,   "Replacement BB version"},
        {"--preserve-links",  NULL, NMO_OPT_FLAG,   "Require unchanged control boundary"},
        {"--preserve-params", NULL, NMO_OPT_FLAG,   "Require unchanged parameter boundary"},
        {"--output",          "-o", NMO_OPT_STRING, "Output file"},
        {"--dry-run",         NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum {
        OPT_GUID,
        OPT_NAME,
        OPT_VERSION,
        OPT_PRESERVE_LINKS,
        OPT_PRESERVE_PARAMS,
        OPT_OUTPUT,
        OPT_DRY_RUN,
        OPT_COUNT
    };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 16,
    };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr,
                "Error: Usage: nmo behavior replace-bb <behavior-id> "
                "--bb-guid <guid> --name <name> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_GUID].present) {
        fprintf(stderr, "Error: --bb-guid is required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_guid_t guid = nmo_guid_parse(vals[OPT_GUID].val.str);
    if (nmo_guid_is_null(guid)) {
        fprintf(stderr, "Error: Invalid GUID '%s'\n",
                vals[OPT_GUID].val.str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t behavior_id = 0;
    if (nmo_parse_u32_range(r.pos_args[0], 1, UINT32_MAX,
                            &behavior_id) != NMO_OK) {
        fprintf(stderr, "Error: Invalid behavior id '%s'\n",
                r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];
    const char *output_path = vals[OPT_OUTPUT].present
        ? vals[OPT_OUTPUT].val.str
        : NULL;
    bool dry_run = vals[OPT_DRY_RUN].present &&
                   vals[OPT_DRY_RUN].val.flag;

    replace_bb_args_t args = {
        .desc = {
            .behavior_id = behavior_id,
            .block_guid = guid,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .block_version = vals[OPT_VERSION].present
                ? vals[OPT_VERSION].val.u
                : 65536u,
            .preserve_links = vals[OPT_PRESERVE_LINKS].present &&
                              vals[OPT_PRESERVE_LINKS].val.flag,
            .preserve_params = vals[OPT_PRESERVE_PARAMS].present &&
                               vals[OPT_PRESERVE_PARAMS].val.flag,
        },
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.replace-bb",
        .output_required_unless_dry_run = true,
    };

    return nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        replace_bb_mutate,
        replace_bb_report,
        &args);
}
