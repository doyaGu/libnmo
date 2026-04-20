/**
 * @file nmo_cmd_behavior_rewrite.c
 * @brief Behavior graph rewrite CLI commands.
 */

#include "nmo_cmd_behavior_rewrite.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_json.h"
#include "../nmo_opt.h"

#include "behavior/nmo_behavior_boundary.h"
#include "core/nmo_error.h"
#include "object/nmo_class_ids.h"

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
