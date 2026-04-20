/**
 * @file nmo_cmd_behavior_rewrite.c
 * @brief Behavior graph rewrite CLI commands.
 */

#include "nmo_cmd_behavior_rewrite.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"

#include "behavior/nmo_behavior_boundary.h"
#include "behavior/nmo_behavior_rewrite.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_parse.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"

#include <stdint.h>
#include <stdio.h>
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

    if (!nmo_behavior_boundary_build(c.ctx, c.session,
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

static void add_fold_candidate_group_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *groups,
    const char *kind,
    nmo_object_id_t root_id,
    const nmo_behavior_state_t *root_state,
    const nmo_behavior_boundary_t *boundary) {
    yyjson_mut_val *group = yyjson_mut_obj(doc);
    nmo_cli_json_add_str_safe(doc, group, "kind", kind);
    yyjson_mut_obj_add_uint(doc, group, "root_id", root_id);
    nmo_cli_json_add_str_safe(doc, group, "root_behavior_type",
                              fold_behavior_type(root_state));

    yyjson_mut_val *nodes = yyjson_mut_arr(doc);
    for (size_t i = 0; i < boundary->internal_node_count; ++i) {
        yyjson_mut_arr_add_uint(doc, nodes, boundary->internal_nodes[i]);
    }
    yyjson_mut_obj_add_val(doc, group, "nodes", nodes);
    add_internal_nodes_json(doc, group, boundary);
    add_control_edges_json(doc, group, "control_in",
                           boundary->control_in,
                           boundary->control_in_count);
    add_control_edges_json(doc, group, "control_out",
                           boundary->control_out,
                           boundary->control_out_count);
    add_parameter_edges_json(doc, group, "parameter_in",
                             boundary->parameter_in,
                             boundary->parameter_in_count);
    add_parameter_edges_json(doc, group, "parameter_out",
                             boundary->parameter_out,
                             boundary->parameter_out_count);
    add_fold_interface_json(doc, group, root_state);
    yyjson_mut_obj_add_uint(doc, group, "node_count",
                            (uint64_t)boundary->internal_node_count);
    yyjson_mut_obj_add_uint(doc, group, "control_in_count",
                            (uint64_t)boundary->control_in_count);
    yyjson_mut_obj_add_uint(doc, group, "control_out_count",
                            (uint64_t)boundary->control_out_count);
    yyjson_mut_obj_add_uint(doc, group, "parameter_in_count",
                            (uint64_t)boundary->parameter_in_count);
    yyjson_mut_obj_add_uint(doc, group, "parameter_out_count",
                            (uint64_t)boundary->parameter_out_count);
    yyjson_mut_obj_add_uint(doc, group, "broken_links",
                            (uint64_t)boundary->broken_links);
    yyjson_mut_obj_add_uint(doc, group, "missing_nodes",
                            (uint64_t)boundary->missing_nodes);
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
    nmo_object_repository_t *repo = nmo_session_get_repository(ctx->session);
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        if (!doc) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
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
        add_fold_candidate_group_json(doc, groups, "parent_recursive",
                                      boundary->behavior_id, parent,
                                      boundary);
        ++group_count;

        const nmo_object_id_t *sub_ids = parent
            ? NMO_ARRAY_DATA(nmo_object_id_t, &parent->sub_behaviors)
            : NULL;
        for (size_t i = 0; sub_ids && i < parent->sub_behaviors.count; ++i) {
            nmo_behavior_boundary_t child_boundary = {0};
            nmo_object_id_t child_id = sub_ids[i];
            const nmo_behavior_state_t *child_state =
                fold_find_behavior_state(repo, child_id);
            if (!nmo_behavior_boundary_build(ctx->ctx, ctx->session,
                                             child_id, depth,
                                             &child_boundary)) {
                char detail[256];
                size_t detail_len = nmo_last_error_message_copy(
                    detail, sizeof(detail));
                fprintf(stderr, "Error: %s\n",
                        detail_len > 0 ? detail
                                       : "Failed to build child fold boundary");
                yyjson_mut_doc_free(doc);
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
            add_fold_candidate_group_json(doc, groups, "direct_child",
                                          child_id, child_state,
                                          &child_boundary);
            nmo_behavior_boundary_free(&child_boundary);
            ++group_count;
        }
        yyjson_mut_obj_add_uint(doc, data, "candidate_group_count",
                                (uint64_t)group_count);
        yyjson_mut_obj_add_val(doc, data, "candidate_groups", groups);

        return nmo_cmd_ctx_json_end(ctx, doc, data,
                                    "behavior.fold-candidates");
    }

    fprintf(ctx->out, "Fold candidates for behavior #%u (%s)\n",
            boundary->behavior_id, fold_behavior_type(parent));
    fprintf(ctx->out, "Candidate parent_recursive: nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu\n",
            boundary->internal_node_count,
            boundary->control_in_count,
            boundary->control_out_count,
            boundary->parameter_in_count,
            boundary->parameter_out_count);
    const nmo_object_id_t *sub_ids = parent
        ? NMO_ARRAY_DATA(nmo_object_id_t, &parent->sub_behaviors)
        : NULL;
    for (size_t i = 0; sub_ids && i < parent->sub_behaviors.count; ++i) {
        nmo_behavior_boundary_t child_boundary = {0};
        nmo_object_id_t child_id = sub_ids[i];
        const nmo_behavior_state_t *child_state =
            fold_find_behavior_state(repo, child_id);
        if (!nmo_behavior_boundary_build(ctx->ctx, ctx->session,
                                         child_id, depth,
                                         &child_boundary)) {
            char detail[256];
            size_t detail_len = nmo_last_error_message_copy(
                detail, sizeof(detail));
            fprintf(stderr, "Error: %s\n",
                    detail_len > 0 ? detail
                                   : "Failed to build child fold boundary");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        fprintf(ctx->out, "Candidate direct_child #%u (%s): nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu interface=%s\n",
                child_id,
                fold_behavior_type(child_state),
                child_boundary.internal_node_count,
                child_boundary.control_in_count,
                child_boundary.control_out_count,
                child_boundary.parameter_in_count,
                child_boundary.parameter_out_count,
                fold_interface_action(child_state));
        nmo_behavior_boundary_free(&child_boundary);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int fold_candidates_run(nmo_cmd_ctx_t *ctx,
                               const fold_candidates_args_t *args,
                               bool close_ctx,
                               const char *usage) {
    nmo_cmd_ctx_t c = *ctx;
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    nmo_behavior_boundary_t boundary = {0};

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
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

    if (!nmo_behavior_boundary_build(c.ctx, c.session,
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
        {"--guid",            NULL, NMO_OPT_STRING, "Target BB GUID"},
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
                             const nmo_behavior_fold_report_t *report) {
    const nmo_behavior_boundary_t *boundary = &report->boundary;
    nmo_object_id_t representative_id = report->representative_id;
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        if (!doc) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", true);
        yyjson_mut_obj_add_bool(doc, data, "can_write",
                                report->can_write);
        yyjson_mut_obj_add_bool(doc, data, "write_supported",
                                report->can_write);
        nmo_cli_json_add_str_safe(doc, data, "status", "analysis_only");
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

int nmo_cmd_behavior_fold(int argc,
                          char **argv,
                          const nmo_cli_global_opts_t *global) {
    fold_args_t args = {0};
    const char *file_path = NULL;
    const char *usage =
        "nmo behavior fold --parent <id> --nodes <id,...> "
        "--guid <guid> --name <name> [--dry-run] <file> -o <output>";

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
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
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
    nmo_status_t fold_rc = args.dry_run
        ? nmo_behavior_fold_analyze(c.ctx, c.session, &desc, &report)
        : nmo_behavior_fold(c.ctx, c.session, &desc, &report);
    if (fold_rc != NMO_OK) {
        if (report.diagnostic_message) {
            fprintf(stderr, "Error: behavior fold rejected");
            if (report.diagnostic_code) {
                fprintf(stderr, " (%s)", report.diagnostic_code);
            }
            fprintf(stderr, ": %s\n", report.diagnostic_message);
        } else {
            fprintf(stderr, "Error: Failed to analyze behavior fold\n");
        }
        rc = (fold_rc == NMO_ERR_INVALID_ARGUMENT ||
              fold_rc == NMO_ERR_NOT_FOUND)
            ? NMO_CLI_EXIT_ARG_ERROR
            : NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    rc = args.dry_run
        ? fold_emit_dry_run(&c, parent, representative, &report)
        : NMO_CLI_EXIT_SUCCESS;

cleanup:
    nmo_behavior_fold_report_free(&report);
    return nmo_cmd_ctx_done(&c, rc);
}

typedef struct replace_bb_args {
    nmo_behavior_replace_bb_desc_t desc;
    nmo_behavior_rewrite_report_t report;
} replace_bb_args_t;

static void replace_bb_add_report_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    const nmo_behavior_rewrite_report_t *report) {
    char before_guid[24];
    char after_guid[24];
    rewrite_guid_to_string(report->before_guid, before_guid,
                           sizeof(before_guid));
    rewrite_guid_to_string(report->after_guid, after_guid,
                           sizeof(after_guid));

    yyjson_mut_obj_add_uint(doc, data, "behavior_id",
                            (uint64_t)report->behavior_id);
    yyjson_mut_obj_add_bool(doc, data, "changed", report->changed);

    yyjson_mut_val *before = yyjson_mut_obj(doc);
    nmo_cli_json_add_str_safe(doc, before, "guid", before_guid);
    yyjson_mut_obj_add_uint(doc, before, "flags",
                            (uint64_t)report->before_flags);
    yyjson_mut_obj_add_val(doc, data, "before", before);

    yyjson_mut_val *after = yyjson_mut_obj(doc);
    nmo_cli_json_add_str_safe(doc, after, "guid", after_guid);
    yyjson_mut_obj_add_uint(doc, after, "flags",
                            (uint64_t)report->after_flags);
    yyjson_mut_obj_add_val(doc, data, "after", after);

    yyjson_mut_val *eligibility = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, eligibility, "leaf",
                            report->eligible_leaf);
    yyjson_mut_obj_add_uint(doc, eligibility, "sub_behaviors",
                            (uint64_t)report->sub_behavior_count);
    yyjson_mut_obj_add_uint(doc, eligibility, "sub_behavior_links",
                            (uint64_t)report->sub_behavior_link_count);
    yyjson_mut_obj_add_uint(doc, eligibility, "operations",
                            (uint64_t)report->operation_count);
    yyjson_mut_obj_add_val(doc, data, "eligibility", eligibility);

    yyjson_mut_val *preserved = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, preserved, "inputs",
                            (uint64_t)report->preserved_inputs);
    yyjson_mut_obj_add_uint(doc, preserved, "outputs",
                            (uint64_t)report->preserved_outputs);
    yyjson_mut_obj_add_uint(doc, preserved, "in_parameters",
                            (uint64_t)report->preserved_in_parameters);
    yyjson_mut_obj_add_uint(doc, preserved, "out_parameters",
                            (uint64_t)report->preserved_out_parameters);
    yyjson_mut_obj_add_uint(doc, preserved, "local_parameters",
                            (uint64_t)report->preserved_local_parameters);
    yyjson_mut_obj_add_uint(doc, preserved, "control_in",
                            (uint64_t)report->preserved_control_in);
    yyjson_mut_obj_add_uint(doc, preserved, "control_out",
                            (uint64_t)report->preserved_control_out);
    yyjson_mut_obj_add_uint(doc, preserved, "parameter_in",
                            (uint64_t)report->preserved_parameter_in);
    yyjson_mut_obj_add_uint(doc, preserved, "parameter_out",
                            (uint64_t)report->preserved_parameter_out);
    yyjson_mut_obj_add_val(doc, data, "preserved", preserved);

    if (report->diagnostic_code || report->diagnostic_message) {
        yyjson_mut_val *diagnostic = yyjson_mut_obj(doc);
        if (report->diagnostic_code) {
            nmo_cli_json_add_str_safe(doc, diagnostic, "code",
                                      report->diagnostic_code);
        }
        if (report->diagnostic_message) {
            nmo_cli_json_add_str_safe(doc, diagnostic, "message",
                                      report->diagnostic_message);
        }
        yyjson_mut_obj_add_val(doc, data, "diagnostic", diagnostic);
    }
}

static int replace_bb_mutate(nmo_cmd_ctx_t *c,
                             bool dry_run,
                             const char *output_path,
                             void *user_data) {
    (void)dry_run;
    (void)output_path;
    replace_bb_args_t *args = (replace_bb_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_status_t rc = nmo_behavior_replace_bb(
        c->ctx, c->session, &args->desc, &args->report);
    if (rc != NMO_OK) {
        fprintf(stderr,
                "Error: behavior %u is not leaf-replaceable "
                "(sub_behaviors=%zu, sub_behavior_links=%zu, operations=%zu)",
                args->desc.behavior_id,
                args->report.sub_behavior_count,
                args->report.sub_behavior_link_count,
                args->report.operation_count);
        if (args->report.diagnostic_message) {
            fprintf(stderr, ": %s", args->report.diagnostic_message);
        }
        fputc('\n', stderr);
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
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        replace_bb_add_report_json(doc, data, &args->report);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(c, doc, data,
                                    "behavior.replace-bb");
    }

    if (dry_run) {
        fprintf(c->out, "[dry-run] ");
    }
    char before_guid[24];
    char after_guid[24];
    rewrite_guid_to_string(args->report.before_guid, before_guid,
                           sizeof(before_guid));
    rewrite_guid_to_string(args->report.after_guid, after_guid,
                           sizeof(after_guid));
    fprintf(c->out,
            "Replaced leaf BB #%u: %s -> %s\n",
            args->report.behavior_id,
            before_guid,
            after_guid);
    fprintf(c->out,
            "Preserved: inputs=%zu outputs=%zu in_params=%zu "
            "out_params=%zu local_params=%zu control_in=%zu "
            "control_out=%zu parameter_in=%zu parameter_out=%zu\n",
            args->report.preserved_inputs,
            args->report.preserved_outputs,
            args->report.preserved_in_parameters,
            args->report.preserved_out_parameters,
            args->report.preserved_local_parameters,
            args->report.preserved_control_in,
            args->report.preserved_control_out,
            args->report.preserved_parameter_in,
            args->report.preserved_parameter_out);
    if (!dry_run && output_path) {
        fprintf(c->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_behavior_replace_bb(int argc,
                                char **argv,
                                const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--guid",            NULL, NMO_OPT_STRING, "Replacement BB GUID"},
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
                "--guid <guid> --name <name> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_GUID].present) {
        fprintf(stderr, "Error: --guid is required\n");
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
