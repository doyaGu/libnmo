/**
 * @file nmo_cmd_behavior_rewrite.c
 * @brief Behavior graph rewrite CLI commands.
 */

#include "nmo_cmd_behavior_rewrite.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_record.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_opt.h"

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_analyze.h"
#include "edit/nmo_behavior_edit.h"
#include "edit/nmo_edit_plan.h"
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

static bool parse_graph_boundary_args(int argc,
                                      char **argv,
                                      bool expect_file_operand,
                                      nmo_core_object_selector_t *out_selector,
                                      const char **out_file,
                                      uint32_t *out_depth) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_DEPTH,
        NMO_OPT_DEF_JSON,
        {"--id",    "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name",  "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_DEPTH, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = NMO_OPT_RESULT(vals, pos);
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
            .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
            .positional_id = positional_id,
            .name = nmo_opt_str(&vals[OPT_NAME]),
            .required_base_class = NMO_CID_BEHAVIOR,
            .selector_label = "Behavior",
            .type_label = "CKBehavior",
        };
    }
    if (out_file) {
        *out_file = file_path;
    }
    if (out_depth) {
        *out_depth = nmo_opt_uint_or(&vals[OPT_DEPTH], UINT32_MAX);
    }
    return true;
}

/* Append a new item to `array`; the array owns it. NULL on allocation failure. */
static nmo_cli_record_t *array_add_item(nmo_cli_record_array_t *array) {
    nmo_cli_record_t *item = nmo_cli_record_new();
    return nmo_cli_record_array_add(array, item) ? item : NULL;
}

/* JSON string, or JSON null when `value` is NULL. */
static bool add_str_or_null(nmo_cli_record_t *rec,
                            const char *key,
                            const char *value) {
    return value ? nmo_cli_record_str(rec, key, NULL, value)
                 : nmo_cli_record_null(rec, key, NULL, NULL);
}

static bool add_id_list(nmo_cli_record_t *rec,
                        const char *key,
                        const nmo_object_id_t *ids,
                        size_t count) {
    if (!ids) {
        count = 0;
    }
    uint64_t *values = count > 0
        ? (uint64_t *)malloc(count * sizeof(*values))
        : NULL;
    if (count > 0 && !values) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        values[i] = ids[i];
    }
    bool ok = nmo_cli_record_uint_list(rec, key, NULL, values, count, NULL);
    free(values);
    return ok;
}

static bool add_control_edges(
    nmo_cli_record_t *rec,
    const char *key,
    const nmo_behavior_boundary_control_edge_t *edges,
    size_t count) {
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    bool ok = arr != NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        const nmo_behavior_boundary_control_edge_t *edge = &edges[i];
        nmo_cli_record_t *item = array_add_item(arr);
        ok = item != NULL;
        ok = ok && nmo_cli_record_uint(item, "link_id", NULL, edge->link_id);
        ok = ok && nmo_cli_record_uint(item, "source_owner_id", NULL,
                                       edge->source_owner_id);
        ok = ok && nmo_cli_record_uint(item, "source_io_id", NULL,
                                       edge->source_io_id);
        ok = ok && nmo_cli_record_uint(item, "target_owner_id", NULL,
                                       edge->target_owner_id);
        ok = ok && nmo_cli_record_uint(item, "target_io_id", NULL,
                                       edge->target_io_id);
        ok = ok && nmo_cli_record_int(item, "activation_delay", NULL,
                                      edge->activation_delay);
        ok = ok && nmo_cli_record_int(item, "initial_activation_delay", NULL,
                                      edge->initial_activation_delay);
    }
    return ok;
}

static bool add_parameter_edges(
    nmo_cli_record_t *rec,
    const char *key,
    const nmo_behavior_boundary_parameter_edge_t *edges,
    size_t count) {
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    bool ok = arr != NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        const nmo_behavior_boundary_parameter_edge_t *edge = &edges[i];
        nmo_cli_record_t *item = array_add_item(arr);
        ok = item != NULL;
        ok = ok && nmo_cli_record_uint(item, "source_parameter_id", NULL,
                                       edge->source_parameter_id);
        ok = ok && nmo_cli_record_uint(item, "target_parameter_id", NULL,
                                       edge->target_parameter_id);
        ok = ok && nmo_cli_record_uint(item, "source_owner_id", NULL,
                                       edge->source_owner_id);
        ok = ok && nmo_cli_record_uint(item, "target_owner_id", NULL,
                                       edge->target_owner_id);
        ok = ok && nmo_cli_record_str_fmt(item, "type_guid", NULL, "%08X-%08X",
                                          edge->type_guid.d1,
                                          edge->type_guid.d2);
        ok = ok && nmo_cli_record_bool(item, "shared", NULL, edge->shared);
    }
    return ok;
}

/* "internal_nodes" followed by the four boundary edge arrays. */
static bool add_boundary_edges(nmo_cli_record_t *rec,
                               const nmo_behavior_boundary_t *boundary) {
    return add_id_list(rec, "internal_nodes", boundary->internal_nodes,
                       boundary->internal_node_count) &&
           add_control_edges(rec, "control_in", boundary->control_in,
                             boundary->control_in_count) &&
           add_control_edges(rec, "control_out", boundary->control_out,
                             boundary->control_out_count) &&
           add_parameter_edges(rec, "parameter_in", boundary->parameter_in,
                               boundary->parameter_in_count) &&
           add_parameter_edges(rec, "parameter_out", boundary->parameter_out,
                               boundary->parameter_out_count);
}

static bool add_boundary_counts(nmo_cli_record_t *rec,
                                const nmo_behavior_boundary_t *boundary) {
    return nmo_cli_record_uint(rec, "node_count", NULL,
                               boundary->internal_node_count) &&
           nmo_cli_record_uint(rec, "control_in_count", NULL,
                               boundary->control_in_count) &&
           nmo_cli_record_uint(rec, "control_out_count", NULL,
                               boundary->control_out_count) &&
           nmo_cli_record_uint(rec, "parameter_in_count", NULL,
                               boundary->parameter_in_count) &&
           nmo_cli_record_uint(rec, "parameter_out_count", NULL,
                               boundary->parameter_out_count);
}

static bool edit_report_json(yyjson_mut_doc *doc,
                             yyjson_mut_val *obj,
                             const void *data) {
    const nmo_edit_report_t *report = (const nmo_edit_report_t *)data;
    nmo_cli_edit_report_add_schema_v2_json(
        doc, obj, report, report != NULL && report->dry_run);
    return true;
}

/* Splice the schema v2 edit report; `report` must outlive the record. */
static bool add_edit_report(nmo_cli_record_t *rec,
                            nmo_edit_report_t *report,
                            const char *output_path) {
    if (report != NULL && output_path != NULL && report->output_path == NULL) {
        (void)nmo_edit_report_set_output_path(report, output_path);
    }
    return nmo_cli_record_json(rec, edit_report_json, report);
}

/*
 * Emit a record built by this file. The text side is written with raw lines,
 * matching the plain fprintf output these commands have always produced. An
 * incomplete record (`ok` false) is an internal error.
 */
static int emit_record(nmo_cmd_ctx_t *ctx,
                       nmo_cli_record_t *rec,
                       bool ok,
                       const char *cmd_name) {
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    return nmo_cmd_ctx_emit_record(ctx, rec, cmd_name, 0, false);
}

static int graph_boundary_emit(nmo_cmd_ctx_t *ctx,
                               const nmo_behavior_boundary_t *boundary) {
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "behavior_id", NULL,
                                   boundary->behavior_id);
    ok = ok && add_boundary_edges(rec, boundary);
    ok = ok && nmo_cli_record_uint(rec, "broken_links", NULL,
                                   boundary->broken_links);
    ok = ok && nmo_cli_record_uint(rec, "missing_nodes", NULL,
                                   boundary->missing_nodes);
    ok = ok && nmo_cli_record_raw_fmt(
        rec,
        "Behavior #%u boundary\n"
        "Internal nodes: %zu\n"
        "Control in: %zu\n"
        "Control out: %zu\n"
        "Parameter in: %zu\n"
        "Parameter out: %zu\n",
        boundary->behavior_id,
        boundary->internal_node_count,
        boundary->control_in_count,
        boundary->control_out_count,
        boundary->parameter_in_count,
        boundary->parameter_out_count);
    return emit_record(ctx, rec, ok, "behavior.graph-boundary");
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
        const char *detail = nmo_last_error_message();
        nmo_error_code_t code = nmo_last_error_code();
        if (detail[0] != '\0') {
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

/*
 * One fold candidate group. Router and component groups own their roots and
 * boundary (owned_* set); the parent and direct-child groups borrow them.
 */
typedef struct fold_candidate_group {
    const char *kind;
    nmo_workspace_t *workspace;
    nmo_object_id_t root_id;
    const nmo_behavior_state_t *root_state;
    const nmo_object_id_t *roots;
    size_t root_count;
    const nmo_behavior_boundary_t *boundary;
    nmo_object_id_t *owned_roots;
    nmo_behavior_boundary_t *owned_boundary;
} fold_candidate_group_t;

typedef struct fold_candidate_group_list {
    fold_candidate_group_t *items;
    size_t count;
    size_t capacity;
} fold_candidate_group_list_t;

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

static bool add_fold_interface(nmo_cli_record_t *rec,
                               const nmo_behavior_state_t *state) {
    nmo_cli_record_t *obj = nmo_cli_record_object(rec, "interface");
    bool ok = obj != NULL;
    ok = ok && nmo_cli_record_bool(obj, "available", NULL,
                                   state && state->has_interface);
    ok = ok && nmo_cli_record_bool(obj, "structured", NULL,
                                   state && state->interface_data != NULL);
    ok = ok && nmo_cli_record_bool(obj, "raw", NULL,
                                   state && state->interface_chunk != NULL);
    ok = ok && nmo_cli_record_bool(obj, "runtime_ids", NULL,
                                   state && state->interface_ids_are_runtime);
    ok = ok && nmo_cli_record_str(obj, "action", NULL,
                                  fold_interface_action(state));
    return ok;
}

static bool fold_group_semantic_risks_json(yyjson_mut_doc *doc,
                                           yyjson_mut_val *obj,
                                           const void *data) {
    const fold_candidate_group_t *group = (const fold_candidate_group_t *)data;
    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0;
    if (group->workspace) {
        (void)nmo_behavior_edit_collect_semantic_risks(
            group->workspace, group->boundary,
            group->boundary->internal_nodes,
            group->boundary->internal_node_count,
            &risks, &risk_count);
    }
    nmo_cli_edit_report_add_semantic_risk_array_json(
        doc, obj, risks, risk_count);
    nmo_behavior_edit_semantic_risks_free(risks);
    return true;
}

static bool fold_group_set_summary(nmo_cli_record_t *item,
                                   const fold_candidate_group_t *group) {
    const nmo_behavior_boundary_t *b = group->boundary;
    if (strcmp(group->kind, "parent_recursive") == 0) {
        return nmo_cli_record_set_summary_fmt(
            item,
            "Candidate %s: nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu",
            group->kind, b->internal_node_count, b->control_in_count,
            b->control_out_count, b->parameter_in_count,
            b->parameter_out_count);
    }
    if (strcmp(group->kind, "direct_child") == 0) {
        return nmo_cli_record_set_summary_fmt(
            item,
            "Candidate %s #%u (%s): nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu interface=%s",
            group->kind, group->root_id, fold_behavior_type(group->root_state),
            b->internal_node_count, b->control_in_count,
            b->control_out_count, b->parameter_in_count,
            b->parameter_out_count, fold_interface_action(group->root_state));
    }
    return nmo_cli_record_set_summary_fmt(
        item,
        "Candidate %s #%u: roots=%zu nodes=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu interface=%s",
        group->kind, group->root_id, group->root_count,
        b->internal_node_count, b->control_in_count, b->control_out_count,
        b->parameter_in_count, b->parameter_out_count,
        fold_interface_action(group->root_state));
}

/* `group` must outlive the record: its semantic risks are read at render time. */
static bool add_fold_candidate_group(nmo_cli_record_array_t *groups,
                                     const fold_candidate_group_t *group) {
    const nmo_behavior_boundary_t *boundary = group->boundary;
    nmo_cli_record_t *item = array_add_item(groups);
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_str(item, "kind", NULL, group->kind);
    ok = ok && nmo_cli_record_uint(item, "root_id", NULL, group->root_id);
    ok = ok && nmo_cli_record_str(item, "root_behavior_type", NULL,
                                  fold_behavior_type(group->root_state));
    ok = ok && add_id_list(item, "roots", group->roots, group->root_count);
    ok = ok && add_id_list(item, "nodes", boundary->internal_nodes,
                           boundary->internal_node_count);
    ok = ok && add_boundary_edges(item, boundary);
    ok = ok && add_fold_interface(item, group->root_state);
    ok = ok && add_boundary_counts(item, boundary);
    ok = ok && nmo_cli_record_uint(item, "broken_links", NULL,
                                   boundary->broken_links);
    ok = ok && nmo_cli_record_uint(item, "missing_nodes", NULL,
                                   boundary->missing_nodes);
    ok = ok && nmo_cli_record_json(item, fold_group_semantic_risks_json, group);
    ok = ok && fold_group_set_summary(item, group);
    return ok;
}

static bool fold_group_list_push(fold_candidate_group_list_t *list,
                                 const fold_candidate_group_t *group) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2u : 8u;
        fold_candidate_group_t *items = (fold_candidate_group_t *)realloc(
            list->items, capacity * sizeof(*items));
        if (!items) {
            return false;
        }
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = *group;
    return true;
}

static void fold_group_dispose(fold_candidate_group_t *group) {
    free(group->owned_roots);
    if (group->owned_boundary) {
        nmo_behavior_boundary_free(group->owned_boundary);
        free(group->owned_boundary);
    }
}

static void fold_group_list_free(fold_candidate_group_list_t *list) {
    for (size_t i = 0; i < list->count; ++i) {
        fold_group_dispose(&list->items[i]);
    }
    free(list->items);
}

/*
 * Push a group that owns `roots` and `boundary`. Ownership passes to the list
 * even on failure.
 */
static bool fold_group_list_push_owned(fold_candidate_group_list_t *list,
                                       nmo_cmd_ctx_t *ctx,
                                       const char *kind,
                                       const fold_candidate_child_t *root,
                                       nmo_object_id_t *roots,
                                       size_t root_count,
                                       nmo_behavior_boundary_t *boundary) {
    fold_candidate_group_t group = {
        .kind = kind,
        .workspace = ctx->workspace,
        .root_id = root->root_id,
        .root_state = root->root_state,
        .roots = roots,
        .root_count = root_count,
        .boundary = boundary,
        .owned_roots = roots,
        .owned_boundary = boundary,
    };
    if (!fold_group_list_push(list, &group)) {
        fold_group_dispose(&group);
        return false;
    }
    return true;
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
    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    for (size_t i = 0; parent && i < parent->sub_behavior_links.count; ++i) {
        nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
            &parent->sub_behavior_links, i);
        if (link_id == 0) continue;
        nmo_object_t *link_obj =
            repo ? nmo_object_repository_find_by_id(repo, link_id) : NULL;
        const nmo_behaviorlink_state_t *link_state =
            link_obj && nmo_object_get_class_id(link_obj) == NMO_CID_BEHAVIORLINK
                ? (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj)
                : NULL;
        if (!link_state || !index) {
            continue;
        }
        const nmo_port_owner_t *source_owner =
            nmo_behavior_index_find(
                index, nmo_behaviorlink_in_io_id(link_state));
        const nmo_port_owner_t *target_owner =
            nmo_behavior_index_find(
                index, nmo_behaviorlink_out_io_id(link_state));
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

/* Heap boundary for `nodes` under `parent_id`; NULL with the error reported. */
static nmo_behavior_boundary_t *fold_build_group_boundary(
    nmo_cmd_ctx_t *ctx,
    nmo_object_id_t parent_id,
    const nmo_object_id_t *nodes,
    size_t node_count,
    const char *failure) {
    nmo_behavior_boundary_t *boundary =
        (nmo_behavior_boundary_t *)calloc(1u, sizeof(*boundary));
    if (!boundary) {
        fprintf(stderr, "Error: Out of memory\n");
        return NULL;
    }
    if (!nmo_behavior_boundary_build_for_nodes(
            ctx->workspace, parent_id, nodes, node_count, boundary)) {
        const char *detail = nmo_last_error_message();
        fprintf(stderr, "Error: %s\n", detail[0] != '\0' ? detail : failure);
        free(boundary);
        return NULL;
    }
    return boundary;
}

static int fold_candidates_collect_control_router_groups(
    nmo_cmd_ctx_t *ctx,
    nmo_object_id_t parent_id,
    const nmo_behavior_state_t *parent,
    const fold_candidate_child_t *children,
    size_t child_count,
    fold_candidate_group_list_t *groups) {
    if (!parent || !children || child_count == 0) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    const nmo_behavior_index_t *index =
        nmo_tool_owner_behavior_index(ctx->workspace);
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
                 link_idx < parent->sub_behavior_links.count;
                 ++link_idx) {
                nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
                    &parent->sub_behavior_links, link_idx);
                if (link_id == 0) continue;
                nmo_object_t *link_obj = repo
                    ? nmo_object_repository_find_by_id(repo, link_id)
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
                    nmo_behavior_index_find(
                        index, nmo_behaviorlink_in_io_id(link_state));
                const nmo_port_owner_t *target_owner =
                    nmo_behavior_index_find(
                        index, nmo_behaviorlink_out_io_id(link_state));
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

        if (router_count <= 1u) {
            free(router_ids);
            continue;
        }
        nmo_behavior_boundary_t *router_boundary = fold_build_group_boundary(
            ctx, parent_id, router_ids, router_count,
            "Failed to build control router boundary");
        if (!router_boundary) {
            free(router_ids);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        if (router_boundary->control_out_count <= 1u) {
            nmo_behavior_boundary_free(router_boundary);
            free(router_boundary);
            free(router_ids);
            continue;
        }
        if (!fold_group_list_push_owned(groups, ctx, "control_router",
                                        &children[i], router_ids,
                                        router_count, router_boundary)) {
            fprintf(stderr, "Error: Out of memory\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int fold_candidates_collect_connected_components(
    nmo_cmd_ctx_t *ctx,
    nmo_object_id_t parent_id,
    const nmo_behavior_state_t *parent,
    const fold_candidate_child_t *children,
    size_t child_count,
    fold_candidate_group_list_t *groups) {
    if (!parent || !children || child_count == 0) {
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

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    for (size_t i = 0; i < child_count; ++i) {
        if (fold_component_find(parents, i) != i) {
            continue;
        }

        nmo_object_id_t *component_roots = NULL;
        size_t component_root_count = 0;
        bool ok = true;
        for (size_t j = 0; ok && j < child_count; ++j) {
            if (fold_component_find(parents, j) == i) {
                ok = fold_component_append_unique_id(
                    &component_roots, &component_root_count,
                    children[j].root_id);
            }
        }
        if (!ok) {
            free(component_roots);
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            break;
        }
        if (component_root_count <= 1u) {
            free(component_roots);
            continue;
        }

        nmo_behavior_boundary_t *component_boundary = fold_build_group_boundary(
            ctx, parent_id, component_roots, component_root_count,
            "Failed to build component boundary");
        if (!component_boundary) {
            free(component_roots);
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            break;
        }
        if (!fold_group_list_push_owned(groups, ctx, "connected_component",
                                        &children[i], component_roots,
                                        component_root_count,
                                        component_boundary)) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            break;
        }
    }

    free(parents);
    return exit_code;
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

static bool add_fold_maps(nmo_cli_record_t *rec,
                          const char *key,
                          const nmo_behavior_fold_map_t *maps,
                          size_t count) {
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    bool ok = arr != NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        const nmo_behavior_fold_map_t *map = &maps[i];
        nmo_cli_record_t *item = array_add_item(arr);
        ok = item != NULL;
        ok = ok && nmo_cli_record_str(item, "kind", NULL,
                                      fold_map_kind_string(map->kind));
        ok = ok && nmo_cli_record_uint(item, "old_index", NULL, map->old_index);
        ok = ok && nmo_cli_record_uint(item, "new_index", NULL, map->new_index);
        if (map->old_id != 0) {
            ok = ok && nmo_cli_record_uint(item, "old_id", NULL, map->old_id);
        }
        if (map->new_id != 0) {
            ok = ok && nmo_cli_record_uint(item, "new_id", NULL, map->new_id);
        }
        if (map->label) {
            ok = ok && nmo_cli_record_str(item, "label", NULL, map->label);
        }
    }
    return ok;
}

static bool add_fold_all_maps(nmo_cli_record_t *rec,
                              const nmo_behavior_fold_report_t *report) {
    nmo_cli_record_t *maps = nmo_cli_record_object(rec, "maps");
    return maps != NULL &&
           add_fold_maps(maps, "inputs", report->input_maps,
                         report->input_map_count) &&
           add_fold_maps(maps, "outputs", report->output_maps,
                         report->output_map_count) &&
           add_fold_maps(maps, "parameters", report->parameter_maps,
                         report->parameter_map_count);
}

static bool add_fold_retarget_control_edges(
    nmo_cli_record_t *rec,
    const char *key,
    const nmo_behavior_boundary_control_edge_t *edges,
    size_t count,
    nmo_object_id_t representative_id,
    bool incoming) {
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    bool ok = arr != NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        const nmo_behavior_boundary_control_edge_t *edge = &edges[i];
        nmo_cli_record_t *item = array_add_item(arr);
        ok = item != NULL;
        ok = ok && nmo_cli_record_uint(item, "link_id", NULL, edge->link_id);
        ok = ok && nmo_cli_record_int(item, "activation_delay", NULL,
                                      edge->activation_delay);
        ok = ok && nmo_cli_record_int(item, "initial_activation_delay", NULL,
                                      edge->initial_activation_delay);
        if (incoming) {
            ok = ok && nmo_cli_record_uint(item, "source_owner_id", NULL,
                                           edge->source_owner_id);
            ok = ok && nmo_cli_record_uint(item, "source_io_id", NULL,
                                           edge->source_io_id);
            ok = ok && nmo_cli_record_uint(item, "old_target_owner_id", NULL,
                                           edge->target_owner_id);
            ok = ok && nmo_cli_record_uint(item, "old_target_io_id", NULL,
                                           edge->target_io_id);
            ok = ok && nmo_cli_record_uint(item, "new_target_owner_id", NULL,
                                           representative_id);
        } else {
            ok = ok && nmo_cli_record_uint(item, "old_source_owner_id", NULL,
                                           edge->source_owner_id);
            ok = ok && nmo_cli_record_uint(item, "old_source_io_id", NULL,
                                           edge->source_io_id);
            ok = ok && nmo_cli_record_uint(item, "new_source_owner_id", NULL,
                                           representative_id);
            ok = ok && nmo_cli_record_uint(item, "target_owner_id", NULL,
                                           edge->target_owner_id);
            ok = ok && nmo_cli_record_uint(item, "target_io_id", NULL,
                                           edge->target_io_id);
        }
    }
    return ok;
}

static bool add_fold_retarget_parameter_edges(
    nmo_cli_record_t *rec,
    const char *key,
    const nmo_behavior_boundary_parameter_edge_t *edges,
    size_t count,
    nmo_object_id_t representative_id,
    bool incoming) {
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    bool ok = arr != NULL;
    for (size_t i = 0; ok && i < count; ++i) {
        const nmo_behavior_boundary_parameter_edge_t *edge = &edges[i];
        nmo_cli_record_t *item = array_add_item(arr);
        ok = item != NULL;
        ok = ok && nmo_cli_record_uint(item, "source_parameter_id", NULL,
                                       edge->source_parameter_id);
        ok = ok && nmo_cli_record_uint(item, "target_parameter_id", NULL,
                                       edge->target_parameter_id);
        ok = ok && nmo_cli_record_str_fmt(item, "type_guid", NULL, "%08X-%08X",
                                          edge->type_guid.d1,
                                          edge->type_guid.d2);
        ok = ok && nmo_cli_record_bool(item, "shared", NULL, edge->shared);
        if (incoming) {
            ok = ok && nmo_cli_record_uint(item, "source_owner_id", NULL,
                                           edge->source_owner_id);
            ok = ok && nmo_cli_record_uint(item, "old_target_owner_id", NULL,
                                           edge->target_owner_id);
            ok = ok && nmo_cli_record_uint(item, "new_target_owner_id", NULL,
                                           representative_id);
        } else {
            ok = ok && nmo_cli_record_uint(item, "old_source_owner_id", NULL,
                                           edge->source_owner_id);
            ok = ok && nmo_cli_record_uint(item, "new_source_owner_id", NULL,
                                           representative_id);
            ok = ok && nmo_cli_record_uint(item, "target_owner_id", NULL,
                                           edge->target_owner_id);
        }
    }
    return ok;
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
        const char *field = p;
        while (*p != '\0' && *p != ',') {
            ++p;
        }
        /* Copy the field without its interior blanks, sized for the field. */
        char *token = (char *)malloc((size_t)(p - field) + 1u);
        if (!token) {
            return false;
        }
        size_t len = 0;
        for (const char *q = field; q != p; ++q) {
            if (*q != ' ' && *q != '\t') {
                token[len++] = *q;
            }
        }
        token[len] = '\0';
        uint32_t id = 0;
        bool ok = len > 0 && *out_count < out_capacity &&
                  nmo_parse_u32_range(token, 1, UINT32_MAX, &id) == NMO_OK;
        free(token);
        if (!ok) {
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

    size_t left_len = (size_t)(colon - text);
    char *left = (char *)malloc(left_len + 1u);
    if (!left) {
        return false;
    }
    memcpy(left, text, left_len);
    left[left_len] = '\0';

    uint32_t old_index = 0;
    uint32_t new_index = 0;
    bool ok = nmo_parse_u32_range(left, 0, UINT32_MAX, &old_index) == NMO_OK &&
              nmo_parse_u32_range(colon + 1, 0, UINT32_MAX, &new_index) == NMO_OK;
    free(left);
    if (!ok) {
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
        NMO_OPT_DEF_DEPTH,
        NMO_OPT_DEF_JSON,
    };
    enum { OPT_PARENT, OPT_DEPTH, OPT_JSON, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = NMO_OPT_RESULT(vals, pos);
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
        out_args->depth = nmo_opt_uint_or(&vals[OPT_DEPTH], UINT32_MAX);
    }
    if (out_file) {
        *out_file = file_path;
    }
    return parent_id != 0;
}

/*
 * Candidate groups in output order: the parent itself, control routers,
 * connected components, then each direct child.
 */
static int fold_candidates_collect_groups(
    nmo_cmd_ctx_t *ctx,
    const nmo_behavior_state_t *parent,
    const nmo_behavior_boundary_t *boundary,
    const fold_candidate_child_t *children,
    size_t child_count,
    fold_candidate_group_list_t *groups) {
    fold_candidate_group_t parent_group = {
        .kind = "parent_recursive",
        .workspace = ctx->workspace,
        .root_id = boundary->behavior_id,
        .root_state = parent,
        .roots = &boundary->behavior_id,
        .root_count = 1u,
        .boundary = boundary,
    };
    if (!fold_group_list_push(groups, &parent_group)) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    int exit_code = fold_candidates_collect_control_router_groups(
        ctx, boundary->behavior_id, parent, children, child_count, groups);
    if (exit_code == NMO_CLI_EXIT_SUCCESS) {
        exit_code = fold_candidates_collect_connected_components(
            ctx, boundary->behavior_id, parent, children, child_count, groups);
    }
    for (size_t i = 0; exit_code == NMO_CLI_EXIT_SUCCESS && i < child_count;
         ++i) {
        fold_candidate_group_t child_group = {
            .kind = "direct_child",
            .workspace = ctx->workspace,
            .root_id = children[i].root_id,
            .root_state = children[i].root_state,
            .roots = &children[i].root_id,
            .root_count = 1u,
            .boundary = &children[i].boundary,
        };
        if (!fold_group_list_push(groups, &child_group)) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }
    return exit_code;
}

static int fold_candidates_emit(nmo_cmd_ctx_t *ctx,
                                const nmo_behavior_state_t *parent,
                                const nmo_behavior_boundary_t *boundary,
                                uint32_t depth) {
    nmo_object_repository_t *repo = nmo_tool_owner_repository(ctx->workspace);
    size_t child_count = parent ? parent->sub_behaviors.count : 0u;
    fold_candidate_child_t *children = child_count > 0
        ? (fold_candidate_child_t *)calloc(child_count, sizeof(*children))
        : NULL;
    if (child_count > 0 && !children) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (size_t i = 0; i < child_count; ++i) {
        nmo_object_id_t child_id = nmo_behavior_ref_array_get_id(
            &parent->sub_behaviors, i);
        const nmo_behavior_state_t *child_state =
            fold_find_behavior_state(repo, child_id);
        children[i].root_id = child_id;
        children[i].root_state = child_state;
        if (!nmo_behavior_boundary_build(ctx->workspace,
                                         child_id, depth,
                                         &children[i].boundary)) {
            const char *detail = nmo_last_error_message();
            fprintf(stderr, "Error: %s\n",
                    detail[0] != '\0' ? detail
                                   : "Failed to build child fold boundary");
            for (size_t j = 0; j <= i; ++j) {
                nmo_behavior_boundary_free(&children[j].boundary);
            }
            free(children);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    fold_candidate_group_list_t groups = {0};
    int exit_code = fold_candidates_collect_groups(
        ctx, parent, boundary, children, child_count, &groups);
    if (exit_code == NMO_CLI_EXIT_SUCCESS) {
        nmo_cli_record_t *rec = nmo_cli_record_new();
        bool ok = rec != NULL;
        ok = ok && nmo_cli_record_raw_fmt(
            rec, "Fold candidates for behavior #%u (%s)\n",
            boundary->behavior_id, fold_behavior_type(parent));
        ok = ok && nmo_cli_record_uint(rec, "parent_id", NULL,
                                       boundary->behavior_id);

        nmo_cli_record_t *parent_obj =
            ok ? nmo_cli_record_object(rec, "parent") : NULL;
        ok = parent_obj != NULL;
        ok = ok && nmo_cli_record_str(parent_obj, "behavior_type", NULL,
                                      fold_behavior_type(parent));
        ok = ok && nmo_cli_record_uint(parent_obj, "flags", NULL,
                                       parent ? parent->flags : 0u);
        ok = ok && nmo_cli_record_bool(
            parent_obj, "script", NULL,
            parent && (parent->flags & CKBEHAVIOR_SCRIPT) != 0u);
        ok = ok && nmo_cli_record_bool(
            parent_obj, "building_block", NULL,
            parent && (parent->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0u);
        ok = ok && nmo_cli_record_uint(
            parent_obj, "sub_behaviors", NULL,
            parent ? parent->sub_behaviors.count : 0u);
        ok = ok && nmo_cli_record_uint(
            parent_obj, "sub_behavior_links", NULL,
            parent ? parent->sub_behavior_links.count : 0u);
        ok = ok && nmo_cli_record_uint(
            parent_obj, "operations", NULL,
            parent ? parent->operations.count : 0u);

        ok = ok && nmo_cli_record_uint(rec, "candidate_group_count", NULL,
                                       groups.count);
        nmo_cli_record_array_t *group_arr =
            ok ? nmo_cli_record_array(rec, "candidate_groups", NULL) : NULL;
        ok = group_arr != NULL;
        nmo_cli_record_array_omit_heading(group_arr);
        for (size_t i = 0; ok && i < groups.count; ++i) {
            ok = add_fold_candidate_group(group_arr, &groups.items[i]);
        }
        exit_code = emit_record(ctx, rec, ok, "behavior.fold-candidates");
    }

    fold_group_list_free(&groups);
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
        const char *detail = nmo_last_error_message();
        fprintf(stderr, "Error: %s\n",
                detail[0] != '\0' ? detail : "Failed to build fold boundary");
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
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
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
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
    args.anchor_id = nmo_opt_uint_or(&vals[OPT_ANCHOR], 0);
    args.block_guid = nmo_guid_parse(vals[OPT_GUID].val.str);
    args.name = vals[OPT_NAME].val.str;
    args.block_version = nmo_opt_uint_or(&vals[OPT_VERSION], 65536u);
    args.interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE;
    args.preserve_boundary = nmo_opt_flag(&vals[OPT_PRESERVE_BOUNDARY]);
    args.preserve_links = args.preserve_boundary ||
                          nmo_opt_flag(&vals[OPT_PRESERVE_LINKS]);
    args.preserve_params = args.preserve_boundary ||
                           nmo_opt_flag(&vals[OPT_PRESERVE_PARAMS]);
    args.dry_run = nmo_opt_flag(&vals[OPT_DRY_RUN]);
    args.output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
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

static bool add_fold_planned(nmo_cli_record_t *rec,
                             const nmo_behavior_state_t *representative,
                             const nmo_behavior_fold_report_t *report) {
    const nmo_behavior_boundary_t *boundary = &report->boundary;
    nmo_object_id_t representative_id = report->representative_id;
    nmo_cli_record_t *planned = nmo_cli_record_object(rec, "planned");
    nmo_cli_record_t *group = NULL;
    bool ok = planned != NULL;
    ok = ok && add_id_list(planned, "internal_nodes", boundary->internal_nodes,
                           boundary->internal_node_count);
    ok = ok && add_id_list(planned, "nodes_to_delete", report->nodes_to_delete,
                           report->nodes_to_delete_count);

    group = ok ? nmo_cli_record_object(planned, "links_to_delete") : NULL;
    ok = group != NULL &&
         add_control_edges(group, "control",
                           report->control_links_to_delete,
                           report->control_links_to_delete_count);

    group = ok ? nmo_cli_record_object(planned, "links_to_move") : NULL;
    ok = group != NULL &&
         add_control_edges(group, "control_in", boundary->control_in,
                           boundary->control_in_count) &&
         add_control_edges(group, "control_out", boundary->control_out,
                           boundary->control_out_count);

    group = ok ? nmo_cli_record_object(planned, "links_to_retarget") : NULL;
    ok = group != NULL &&
         add_fold_retarget_control_edges(group, "control_in",
                                         boundary->control_in,
                                         boundary->control_in_count,
                                         representative_id, true) &&
         add_fold_retarget_control_edges(group, "control_out",
                                         boundary->control_out,
                                         boundary->control_out_count,
                                         representative_id, false);

    group = ok ? nmo_cli_record_object(planned, "parameters_to_preserve")
               : NULL;
    ok = group != NULL &&
         add_parameter_edges(group, "parameter_in", boundary->parameter_in,
                             boundary->parameter_in_count) &&
         add_parameter_edges(group, "parameter_out", boundary->parameter_out,
                             boundary->parameter_out_count);

    group = ok ? nmo_cli_record_object(planned, "parameters_to_retarget")
               : NULL;
    ok = group != NULL &&
         add_fold_retarget_parameter_edges(group, "parameter_in",
                                           boundary->parameter_in,
                                           boundary->parameter_in_count,
                                           representative_id, true) &&
         add_fold_retarget_parameter_edges(group, "parameter_out",
                                           boundary->parameter_out,
                                           boundary->parameter_out_count,
                                           representative_id, false);

    ok = ok && add_fold_interface(planned, representative);
    ok = ok && add_boundary_counts(planned, boundary);
    ok = ok && nmo_cli_record_uint(planned, "delete_link_count", NULL,
                                   report->control_links_to_delete_count);
    ok = ok && nmo_cli_record_raw_fmt(
        planned,
        "Planned: nodes=%zu delete=%zu control_in=%zu control_out=%zu parameter_in=%zu parameter_out=%zu interface=%s\n"
        "Delete links: %zu\n",
        boundary->internal_node_count,
        boundary->internal_node_count > 0
            ? boundary->internal_node_count - 1
            : 0,
        boundary->control_in_count,
        boundary->control_out_count,
        boundary->parameter_in_count,
        boundary->parameter_out_count,
        fold_interface_action(representative),
        report->control_links_to_delete_count);
    return ok;
}

static int fold_emit_dry_run(nmo_cmd_ctx_t *ctx,
                             const nmo_behavior_state_t *parent,
                             const nmo_behavior_state_t *representative,
                             const nmo_behavior_fold_report_t *report,
                             nmo_edit_report_t *edit_report) {
    nmo_object_id_t representative_id = report->representative_id;
    nmo_edit_report_t analysis_report = {0};
    if (edit_report == NULL) {
        analysis_report.ok = true;
        analysis_report.dry_run = true;
        analysis_report.semantic_risks = report->semantic_risks;
        analysis_report.semantic_risk_count = report->semantic_risk_count;
        edit_report = &analysis_report;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_raw_fmt(
        rec,
        "[dry-run] Fold behavior #%u into BB %08X-%08X\n"
        "Parent #%u (%s), representative #%u (%s)\n",
        representative_id, report->target_guid.d1, report->target_guid.d2,
        report->parent_id, fold_behavior_type(parent),
        representative_id, fold_behavior_type(representative));
    ok = ok && add_edit_report(rec, edit_report, NULL);
    ok = ok && nmo_cli_record_bool(rec, "can_write", NULL, report->can_write);
    ok = ok && nmo_cli_record_bool(rec, "write_supported", NULL,
                                   report->can_write);
    ok = ok && nmo_cli_record_str(rec, "status", NULL,
                                  report->can_write ? "ready"
                                                    : "analysis_only");
    nmo_cli_record_array_t *blockers =
        ok ? nmo_cli_record_array(rec, "write_blockers", NULL) : NULL;
    ok = blockers != NULL;
    for (size_t i = 0; ok && i < report->write_blocker_count; ++i) {
        nmo_cli_record_t *item = array_add_item(blockers);
        ok = item != NULL;
        ok = ok && add_str_or_null(item, "code",
                                   report->write_blockers[i].code);
        ok = ok && add_str_or_null(item, "message",
                                   report->write_blockers[i].message);
    }
    ok = ok && nmo_cli_record_uint(rec, "parent_id", NULL, report->parent_id);
    ok = ok && nmo_cli_record_uint(rec, "anchor_id", NULL, report->anchor_id);
    ok = ok && nmo_cli_record_str(rec, "parent_behavior_type", NULL,
                                  fold_behavior_type(parent));
    ok = ok && nmo_cli_record_uint(rec, "representative_id", NULL,
                                   representative_id);
    ok = ok && nmo_cli_record_str(rec, "representative_behavior_type", NULL,
                                  fold_behavior_type(representative));
    ok = ok && add_id_list(rec, "selected_nodes", report->selected_nodes,
                           report->selected_node_count);
    ok = ok && nmo_cli_record_bool(rec, "preserve_boundary", NULL,
                                   report->preserve_boundary);
    ok = ok && nmo_cli_record_bool(rec, "preserve_links", NULL,
                                   report->preserve_links);
    ok = ok && nmo_cli_record_bool(rec, "preserve_params", NULL,
                                   report->preserve_params);
    ok = ok && nmo_cli_record_str(
        rec, "interface_mode", NULL,
        fold_interface_mode_string(report->interface_mode));
    ok = ok && add_fold_all_maps(rec, report);

    nmo_cli_record_t *target = ok ? nmo_cli_record_object(rec, "target") : NULL;
    ok = target != NULL;
    ok = ok && nmo_cli_record_str_fmt(target, "guid", NULL, "%08X-%08X",
                                      report->target_guid.d1,
                                      report->target_guid.d2);
    ok = ok && add_str_or_null(target, "name", report->target_name);
    ok = ok && nmo_cli_record_uint(target, "version", NULL,
                                   report->target_version);

    ok = ok && add_fold_planned(rec, representative, report);
    ok = ok && nmo_cli_record_raw_fmt(rec, "Can write: %s\n",
                                      report->can_write ? "yes" : "no");
    for (size_t i = 0; ok && i < report->write_blocker_count; ++i) {
        const char *message = report->write_blockers[i].message;
        ok = nmo_cli_record_raw_fmt(rec, "Write blocker: %s%s%s\n",
                                    report->write_blockers[i].code,
                                    message ? " - " : "",
                                    message ? message : "");
    }
    return emit_record(ctx, rec, ok, "behavior.fold");
}

static bool fold_report_semantic_risks_json(yyjson_mut_doc *doc,
                                            yyjson_mut_val *obj,
                                            const void *data) {
    const nmo_behavior_fold_report_t *report =
        (const nmo_behavior_fold_report_t *)data;
    nmo_cli_edit_report_add_semantic_risk_array_json(
        doc, obj, report->semantic_risks, report->semantic_risk_count);
    return true;
}

static int fold_emit_rejection(nmo_cmd_ctx_t *ctx,
                               const nmo_behavior_fold_report_t *report,
                               int exit_code) {
    if (!ctx->is_json) {
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

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_bool(rec, "ok", NULL, false);
    ok = ok && nmo_cli_record_bool(rec, "dry_run", NULL,
                                   report->analysis_only);
    ok = ok && nmo_cli_record_bool(rec, "rejected", NULL, report->rejected);
    ok = ok && nmo_cli_record_bool(rec, "can_write", NULL, report->can_write);
    ok = ok && nmo_cli_record_uint(rec, "parent_id", NULL, report->parent_id);
    ok = ok && nmo_cli_record_uint(rec, "anchor_id", NULL, report->anchor_id);
    ok = ok && add_id_list(rec, "selected_nodes", report->selected_nodes,
                           report->selected_node_count);
    ok = ok && nmo_cli_record_json(rec, fold_report_semantic_risks_json,
                                   report);
    nmo_cli_record_array_t *rejections =
        ok ? nmo_cli_record_array(rec, "rejections", NULL) : NULL;
    nmo_cli_record_t *rejection = rejections ? array_add_item(rejections)
                                             : NULL;
    ok = rejection != NULL;
    ok = ok && add_str_or_null(rejection, "code", report->diagnostic_code);
    ok = ok && add_str_or_null(rejection, "message",
                               report->diagnostic_message);
    ok = ok && add_fold_all_maps(rec, report);

    int json_rc = emit_record(ctx, rec, ok, "behavior.fold");
    return json_rc == NMO_CLI_EXIT_SUCCESS ? exit_code : json_rc;
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
    nmo_edit_report_t edit_report = {0};
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
            nmo_cli_record_t *rec = nmo_cli_record_new();
            bool ok = rec != NULL;
            ok = ok && add_edit_report(
                rec, edit_report_ready ? &edit_report : NULL,
                args.output_path);
            ok = ok && nmo_cli_record_uint(rec, "parent_id", NULL,
                                           args.parent_id);
            ok = ok && nmo_cli_record_uint(rec, "anchor_id", NULL,
                                           args.anchor_id);
            rc = emit_record(&c, rec, ok, "behavior.fold");
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
            nmo_object_id_t anchor_id = edit_report.operation_count > 0u
                ? edit_report.operations[0].result_id
                : args.anchor_id;
            nmo_cli_record_t *rec = nmo_cli_record_new();
            bool ok = rec != NULL;
            ok = ok && add_edit_report(rec, &edit_report, args.output_path);
            ok = ok && nmo_cli_record_bool(rec, "can_write", NULL, true);
            ok = ok && nmo_cli_record_uint(rec, "parent_id", NULL,
                                           args.parent_id);
            ok = ok && nmo_cli_record_uint(rec, "anchor_id", NULL, anchor_id);
            ok = ok && nmo_cli_record_str(rec, "output", NULL,
                                          args.output_path);
            ok = ok && nmo_cli_record_raw_fmt(rec, "Saved to: %s\n",
                                              args.output_path);
            rc = emit_record(&c, rec, ok, "behavior.fold");
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

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && add_edit_report(rec, &args->edit_report, output_path);
    if (output_path) {
        ok = ok && nmo_cli_record_str(rec, "output", NULL, output_path);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, "%sReplaced leaf BB #%u\n",
                                      dry_run ? "[dry-run] " : "",
                                      args->desc.behavior_id);
    if (!dry_run && output_path) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
    }
    int rc = emit_record(c, rec, ok, "behavior.replace-bb");
    if (args->edit_report_ready) {
        nmo_edit_report_dispose(&args->edit_report);
        nmo_edit_plan_destroy(args->edit_plan);
        args->edit_plan = NULL;
        args->edit_report_ready = false;
    }
    return rc;
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
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
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
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRY_RUN]);

    replace_bb_args_t args = {
        .desc = {
            .behavior_id = behavior_id,
            .block_guid = guid,
            .name = nmo_opt_str(&vals[OPT_NAME]),
            .block_version = nmo_opt_uint_or(&vals[OPT_VERSION], 65536u),
            .preserve_links = nmo_opt_flag(&vals[OPT_PRESERVE_LINKS]),
            .preserve_params = nmo_opt_flag(&vals[OPT_PRESERVE_PARAMS]),
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
