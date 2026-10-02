/**
 * @file nmo_cmd_behavior.c
 * @brief CLI behavior command group -- shared helpers, list, and stats
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"
#include "nmo_cmd_behavior_rewrite.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "object/nmo_context.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "extension/nmo_behavior_registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nmo_cmd_behavior_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: behavior list|stats|show|graph|dump|find|trace|interface show ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return nmo_cmd_object_list_class_in_session(ctx, argc, argv, "CKBehavior");
    }
    if (strcmp(argv[0], "stats") == 0 || strcmp(argv[0], "st") == 0) {
        nmo_object_query_t query = {0};
        nmo_core_query_set_class_id(&query, NMO_CID_BEHAVIOR, true);
        nmo_core_iter_result_t result = {0};
        int rc = nmo_core_object_query_run(ctx, &query, NULL, NULL, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        fprintf(ctx->out, "Behaviors: %zu\n", result.matched);
        return NMO_CLI_EXIT_SUCCESS;
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0 ||
        strcmp(argv[0], "dump") == 0 || strcmp(argv[0], "d") == 0 ||
        strcmp(argv[0], "trace") == 0 || strcmp(argv[0], "tr") == 0) {
        return nmo_cmd_object_show_class_in_session(
            ctx, argc, argv, NMO_CID_BEHAVIOR, "CKBehavior");
    }
    if (strcmp(argv[0], "find") == 0 || strcmp(argv[0], "f") == 0) {
        return nmo_cmd_object_find_class_in_session(ctx, argc, argv, "CKBehavior");
    }
    if (strcmp(argv[0], "graph") == 0 || strcmp(argv[0], "g") == 0) {
        return nmo_cmd_behavior_graph_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "graph-boundary") == 0) {
        return nmo_cmd_behavior_graph_boundary_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "fold-candidates") == 0) {
        return nmo_cmd_behavior_fold_candidates_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "interface") == 0 || strcmp(argv[0], "iface") == 0) {
        return nmo_cmd_behavior_interface_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported behavior read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

/* ============================================================================
 * Shared helpers (declared in nmo_cmd_behavior_internal.h)
 * ============================================================================ */

int is_behavior_class(const nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return 0;
    }
    return nmo_type_registry_is_class_derived_from(
        registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEHAVIOR) ? 1 : 0;
}

const char *resolve_name(nmo_object_repository_t *repo, nmo_object_id_t id) {
    if (id == 0) return "(none)";
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) return "(missing)";
    const char *n = nmo_object_get_name(obj);
    return (n && n[0]) ? n : "(unnamed)";
}

const char *resolve_type(const nmo_type_registry_t *reg, nmo_guid_t guid) {
    if (nmo_guid_is_null(guid)) return "?";
    const char *n = nmo_field_type_name(reg, guid);
    return n ? n : "?";
}

nmo_guid_t get_param_type_guid(nmo_object_t *obj) {
    if (!obj || !obj->state) return (nmo_guid_t){0, 0};
    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (cid == NMO_CID_PARAMETERIN) {
        const nmo_parameterin_state_t *s = (const nmo_parameterin_state_t *)obj->state;
        return s->type_guid;
    }
    if (cid == NMO_CID_PARAMETEROUT || cid == NMO_CID_PARAMETERLOCAL || cid == NMO_CID_PARAMETER) {
        const nmo_parameter_state_t *s = (const nmo_parameter_state_t *)obj->state;
        return s->type_guid;
    }
    return (nmo_guid_t){0, 0};
}

const nmo_interface_behavior_t *find_interface_sub(
    const nmo_interface_data_t *idata, nmo_object_id_t behavior_id)
{
    if (!idata) return NULL;
    for (size_t i = 0; i < idata->sub_count; i++) {
        if (idata->subs[i].behavior_id == behavior_id)
            return &idata->subs[i];
    }
    return NULL;
}

bool find_interface_position(const nmo_interface_data_t *idata,
                             nmo_object_id_t behavior_id,
                             float *out_x, float *out_y) {
    if (!idata) return false;
    if (idata->script.behavior_id == behavior_id) {
        *out_x = idata->script.h_pos;
        *out_y = idata->script.v_pos;
        return true;
    }
    for (size_t i = 0; i < idata->sub_count; i++) {
        if (idata->subs[i].behavior_id == behavior_id) {
            *out_x = idata->subs[i].h_pos;
            *out_y = idata->subs[i].v_pos;
            return true;
        }
    }
    return false;
}

bool find_operation_position(const nmo_interface_data_t *idata,
                             nmo_object_id_t op_id,
                             float *out_x, float *out_y) {
    if (!idata) return false;
    for (size_t i = 0; i < idata->script.body.operation_count; i++) {
        if (idata->script.body.operations[i].id == op_id) {
            *out_x = idata->script.body.operations[i].h_pos;
            *out_y = idata->script.body.operations[i].v_pos;
            return true;
        }
    }
    for (size_t s = 0; s < idata->sub_count; s++) {
        for (size_t i = 0; i < idata->subs[s].body.operation_count; i++) {
            if (idata->subs[s].body.operations[i].id == op_id) {
                *out_x = idata->subs[s].body.operations[i].h_pos;
                *out_y = idata->subs[s].body.operations[i].v_pos;
                return true;
            }
        }
    }
    return false;
}

const nmo_interface_link_t *find_interface_link(
    const nmo_interface_data_t *idata, nmo_object_id_t link_id)
{
    if (!idata || link_id == 0) return NULL;
    for (size_t i = 0; i < idata->script.body.link_count; i++) {
        if (idata->script.body.links[i].link_id == link_id)
            return &idata->script.body.links[i];
    }
    for (size_t s = 0; s < idata->sub_count; s++) {
        for (size_t i = 0; i < idata->subs[s].body.link_count; i++) {
            if (idata->subs[s].body.links[i].link_id == link_id)
                return &idata->subs[s].body.links[i];
        }
    }
    return NULL;
}

void nmo_cmd_behavior_add_interface_diagnostics_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    nmo_workspace_t *workspace)
{
    if (!doc || !data || !workspace) {
        return;
    }

    nmo_tool_behavior_interface_diagnostics_t diag;
    nmo_tool_owner_behavior_interface_diagnostics(workspace, &diag);

    yyjson_mut_obj_add_bool(doc, data, "interface_available",
                            diag.attempted ? diag.available : false);

    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, obj, "attempted", diag.attempted != 0);
    yyjson_mut_obj_add_bool(doc, obj, "available", diag.available != 0);
    yyjson_mut_obj_add_int(doc, obj, "status", diag.status);
    nmo_cli_json_add_str_safe(doc, obj, "status_name",
                              nmo_error_string(diag.status));
    yyjson_mut_obj_add_uint(doc, obj, "attempted_count",
                            (uint64_t)diag.attempted_count);
    yyjson_mut_obj_add_uint(doc, obj, "parsed_count",
                            (uint64_t)diag.parsed_count);
    yyjson_mut_obj_add_uint(doc, obj, "failed_count",
                            (uint64_t)diag.failed_count);
    yyjson_mut_obj_add_uint(doc, obj, "skipped_no_arena_count",
                            (uint64_t)diag.skipped_no_arena_count);
    yyjson_mut_obj_add_uint(doc, obj, "allocation_failure_count",
                            (uint64_t)diag.allocation_failure_count);
    if (diag.status != NMO_OK) {
        yyjson_mut_obj_add_uint(doc, obj, "first_error_object_id",
                                diag.first_error_object_id);
        yyjson_mut_obj_add_uint(doc, obj, "first_error_file_id",
                                diag.first_error_file_id);
        yyjson_mut_obj_add_uint(doc, obj, "first_error_chunk_version",
                                diag.first_error_chunk_version);
        yyjson_mut_obj_add_uint(doc, obj, "first_error_data_version",
                                diag.first_error_data_version);
        yyjson_mut_obj_add_uint(doc, obj, "first_error_reader_offset",
                                (uint64_t)diag.first_error_reader_offset);
        yyjson_mut_obj_add_uint(doc, obj, "first_error_chunk_dwords",
                                (uint64_t)diag.first_error_chunk_dwords);
    }
    yyjson_mut_obj_add_val(doc, data, "interface_parse", obj);
}

/* Text: the failed-parse summary line, when an interface parse failed. */
static bool behavior_add_interface_diagnostics_text(nmo_cli_record_t *rec,
                                                    nmo_workspace_t *workspace)
{
    nmo_tool_behavior_interface_diagnostics_t diag;
    nmo_tool_owner_behavior_interface_diagnostics(workspace, &diag);
    if (!diag.attempted || diag.status == NMO_OK) {
        return true;
    }

    bool ok = nmo_cli_record_raw_fmt(
        rec, "Interface parse diagnostics: status=%s(%d), parsed=%zu/%zu, failed=%zu",
        nmo_error_string(diag.status),
        diag.status,
        diag.parsed_count,
        diag.attempted_count,
        diag.failed_count);
    if (diag.first_error_object_id != 0 || diag.first_error_file_id != 0) {
        ok = ok && nmo_cli_record_raw_fmt(
            rec,
            ", first_error_object=%u, file_id=%u, chunk_version=%u, data_version=%u, offset=%zu/%zu dwords",
            diag.first_error_object_id,
            diag.first_error_file_id,
            diag.first_error_chunk_version,
            diag.first_error_data_version,
            diag.first_error_reader_offset,
            diag.first_error_chunk_dwords);
    }
    return ok && nmo_cli_record_raw(rec, "\n");
}

static bool behavior_interface_diagnostics_json(yyjson_mut_doc *doc,
                                                yyjson_mut_val *obj,
                                                const void *data)
{
    nmo_cmd_behavior_add_interface_diagnostics_json(doc, obj, (nmo_workspace_t *)data);
    return true;
}

bool nmo_cmd_behavior_add_interface_diagnostics(nmo_cli_record_t *rec,
                                                nmo_workspace_t *workspace,
                                                bool show_text)
{
    if (!rec || !workspace) {
        return rec != NULL;
    }
    bool ok = nmo_cli_record_json(rec, behavior_interface_diagnostics_json, workspace);
    return ok && (!show_text || behavior_add_interface_diagnostics_text(rec, workspace));
}

void nmo_cmd_behavior_print_interface_diagnostics(
    FILE *out,
    nmo_workspace_t *workspace)
{
    if (!out || !workspace) {
        return;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (rec && behavior_add_interface_diagnostics_text(rec, workspace)) {
        nmo_cli_record_print_kv(rec, out, 0, false);
    }
    nmo_cli_record_free(rec);
}

/* ============================================================================
 * behavior list
 * ============================================================================ */

typedef struct behavior_list_data {
    nmo_object_t **items;
    size_t count;
    size_t capacity;
    bool oom;
} behavior_list_data_t;

/*
 * One behavior: JSON id/class_id/class_name/name; text columns ID, TYPE, IO,
 * PIN, POUT, SUB, NAME.
 */
static bool behavior_list_build_record(nmo_context_t *ctx, nmo_object_t *obj,
                                       nmo_cli_record_t *rec)
{
    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
    const char *name = nmo_object_get_name(obj);

    const char *type_str = "Graph";
    if (bs) {
        if (bs->flags & CKBEHAVIOR_SCRIPT) type_str = "Script";
        else if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) type_str = "BB";
    }

    size_t n_io = bs ? (bs->inputs.count + bs->outputs.count) : 0;

    bool ok = nmo_cli_record_uint(rec, "id", "ID", nmo_object_get_id(obj)) &&
              nmo_cli_record_text(rec, "TYPE", type_str) &&
              nmo_cli_record_text_fmt(rec, "IO", "%zu", n_io) &&
              nmo_cli_record_text_fmt(rec, "PIN", "%zu", bs ? bs->in_parameters.count : (size_t)0) &&
              nmo_cli_record_text_fmt(rec, "POUT", "%zu", bs ? bs->out_parameters.count : (size_t)0) &&
              nmo_cli_record_text_fmt(rec, "SUB", "%zu", bs ? bs->sub_behaviors.count : (size_t)0) &&
              nmo_cli_record_uint(rec, "class_id", NULL, class_id);
    if (ok && class_name) {
        ok = nmo_cli_record_str(rec, "class_name", NULL, class_name);
    }
    return ok && nmo_cli_record_str_opt(rec, "name", "NAME", name, "-");
}

static int behavior_list_core_visitor(size_t index,
                                      nmo_object_t *obj,
                                      const nmo_cmd_ctx_t *c,
                                      void *user)
{
    (void)index;
    (void)c;

    behavior_list_data_t *list = (behavior_list_data_t *)user;
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2u : 64u;
        nmo_object_t **new_items =
            (nmo_object_t **)realloc(list->items, new_capacity * sizeof(*new_items));
        if (!new_items) {
            list->oom = true;
            return 1;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count++] = obj;
    return 0;
}

/* "Behaviors: N" and a table of `objects`; JSON: count + objects. */
static bool behavior_list_build(nmo_context_t *ctx,
                                nmo_object_t *const *objects,
                                size_t count,
                                nmo_cli_record_t *rec)
{
    static const nmo_cli_table_col_t columns[] = {
        {"ID",   NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"TYPE", NMO_CLI_ALIGN_LEFT,  6, 0},
        {"IO",   NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"PIN",  NMO_CLI_ALIGN_RIGHT, 4, 0},
        {"POUT", NMO_CLI_ALIGN_RIGHT, 4, 0},
        {"SUB",  NMO_CLI_ALIGN_RIGHT, 4, 0},
        {"NAME", NMO_CLI_ALIGN_LEFT, 24, 50},
    };

    bool ok = nmo_cli_record_uint(rec, "count", NULL, (uint64_t)count) &&
              nmo_cli_record_raw_fmt(rec, "Behaviors: %zu\n\n", count);
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "objects", NULL) : NULL;
    ok = arr != NULL &&
         nmo_cli_record_array_set_table(arr, columns, sizeof(columns) / sizeof(columns[0]));
    for (size_t i = 0; ok && i < count; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL && behavior_list_build_record(ctx, objects[i], item);
        if (!ok) {
            nmo_cli_record_free(item);
            break;
        }
        ok = nmo_cli_record_array_add(arr, item);
    }
    return ok;
}

static int behavior_list_single(const char *file_path,
                                const nmo_cli_global_opts_t *global,
                                void *user_data,
                                yyjson_mut_doc *doc,
                                yyjson_mut_val *data)
{
    const nmo_tool_text_output_ctx_t *text_ctx =
        (const nmo_tool_text_output_ctx_t *)user_data;

    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    char *open_error = NULL;

    if (!nmo_tool_open_document(file_path, &ctx, &document, &workspace, &open_error)) {
        fprintf(stderr, "Error: %s\n", open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        nmo_tool_close_document(ctx, document, workspace);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_query_t query = {
        .class_id = NMO_CID_BEHAVIOR,
        .include_derived_classes = true,
    };

    nmo_cmd_ctx_t cmd;
    nmo_cmd_ctx_init_from_repl_document(&cmd, ctx, document, workspace, false);

    behavior_list_data_t list = {0};
    if (nmo_core_object_query_run(&cmd, &query,
                                  behavior_list_core_visitor, &list,
                                  NULL) != NMO_CLI_EXIT_SUCCESS ||
        list.oom) {
        free(list.items);
        fprintf(stderr, "Error: Failed to query objects\n");
        nmo_tool_close_document(ctx, document, workspace);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && behavior_list_build(ctx, list.items, list.count, rec);
    free(list.items);
    if (ok && doc && data) {
        ok = nmo_cli_record_to_json(rec, doc, data);
    } else if (ok) {
        FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
        bool colorize = text_ctx ? text_ctx->colorize : false;
        nmo_cli_record_print_kv(rec, out, 0, colorize);
    }
    nmo_cli_record_free(rec);

    (void)global;
    nmo_tool_close_document(ctx, document, workspace);
    if (!ok) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_behavior_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Batch mode */
    if (global->batch_mode) {
        const char *paths[256];
        size_t count = nmo_tool_find_file_args(argc, argv, paths, 256);
        if (count == 0) {
            fprintf(stderr, "Error: No files specified\n");
            fprintf(stderr, "Usage: nmo --batch behavior list <file1> <file2> ...\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return nmo_tool_batch_run(paths, count, global, "behavior.list",
                                  behavior_list_single, NULL);
    }

    /* Single file mode */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    if (!c.registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_query_t query = {0};
    nmo_core_query_set_class_id(&query, NMO_CID_BEHAVIOR, true);

    behavior_list_data_t list = {0};
    rc = nmo_core_object_query_run(&c, &query,
                                   behavior_list_core_visitor, &list, NULL);
    if (rc == NMO_CLI_EXIT_SUCCESS && list.oom) {
        fprintf(stderr, "Error: Out of memory\n");
        rc = NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        free(list.items);
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && behavior_list_build(c.ctx, list.items, list.count, rec);
    free(list.items);
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "behavior.list", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * behavior stats
 * ============================================================================ */

/* BB prototype count entry for stats */
typedef struct {
    nmo_guid_t guid;
    const char *name; /* Borrowed from BB registry */
    size_t count;
} nmo_cli_bb_proto_count_t;

typedef struct {
    nmo_guid_t guid;
    const char *name; /* Borrowed from registry */
    size_t count;
} nmo_cli_guid_count_t;

typedef struct {
    size_t count;
    uint32_t max;
    uint32_t p95;
    double avg;
} nmo_cli_u32_distribution_t;

static int bb_proto_count_cmp_desc(const void *a, const void *b) {
    const nmo_cli_bb_proto_count_t *aa = (const nmo_cli_bb_proto_count_t *)a;
    const nmo_cli_bb_proto_count_t *bb = (const nmo_cli_bb_proto_count_t *)b;
    if (aa->count != bb->count) {
        return (aa->count < bb->count) ? 1 : -1;
    }
    return 0;
}

static int guid_count_cmp_desc(const void *a, const void *b) {
    const nmo_cli_guid_count_t *aa = (const nmo_cli_guid_count_t *)a;
    const nmo_cli_guid_count_t *bb = (const nmo_cli_guid_count_t *)b;
    if (aa->count != bb->count) {
        return (aa->count < bb->count) ? 1 : -1;
    }
    return 0;
}

static int uint32_cmp_asc(const void *a, const void *b) {
    uint32_t aa = *(const uint32_t *)a;
    uint32_t bb = *(const uint32_t *)b;
    if (aa < bb) return -1;
    if (aa > bb) return 1;
    return 0;
}

static nmo_cli_u32_distribution_t compute_u32_distribution(uint32_t *values,
                                                           size_t count)
{
    nmo_cli_u32_distribution_t dist = {0};
    dist.count = count;
    if (!values || count == 0) {
        return dist;
    }

    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += values[i];
    }
    qsort(values, count, sizeof(*values), uint32_cmp_asc);

    size_t p95_index = (count * 95 + 99) / 100;
    if (p95_index == 0) {
        p95_index = 1;
    }
    p95_index--;
    if (p95_index >= count) {
        p95_index = count - 1;
    }

    dist.max = values[count - 1];
    dist.p95 = values[p95_index];
    dist.avg = (double)sum / (double)count;
    return dist;
}

typedef struct behavior_stats_data {
    const nmo_type_registry_t *registry;
    const nmo_behavior_registry_t *bb_reg;
    nmo_object_repository_t *repo;
    size_t total_behaviors;
    size_t n_scripts;
    size_t n_graphs;
    size_t n_bbs;
    size_t n_parameters;
    size_t n_links;
    size_t n_operations;
    size_t n_with_interface;
    size_t n_total_comments;
    size_t n_total_routing_points;
    size_t n_folded;
    size_t n_with_snapshot;
    nmo_cli_bb_proto_count_t *protos;
    size_t proto_count;
    size_t proto_cap;
    nmo_cli_guid_count_t *parameter_types;
    size_t parameter_type_count;
    size_t parameter_type_cap;
    nmo_cli_guid_count_t *operation_types;
    size_t operation_type_count;
    size_t operation_type_cap;
    nmo_object_id_t *script_ids;
    size_t script_id_count;
    size_t script_id_cap;
    uint32_t *script_sub_counts;
    size_t script_sub_count;
    size_t script_sub_cap;
    size_t link_delay_zero;
    size_t link_delay_next_frame;
    size_t link_delay_multi_frame;
    size_t broken_behavior_links;
    size_t broken_sub_behaviors;
    bool oom;
} behavior_stats_data_t;

static void behavior_stats_add_script_id(behavior_stats_data_t *stats,
                                         nmo_object_id_t id)
{
    if (stats->script_id_count == stats->script_id_cap) {
        size_t new_cap = (stats->script_id_cap == 0) ? 16 : (stats->script_id_cap * 2);
        nmo_object_id_t *na = (nmo_object_id_t *)realloc(
            stats->script_ids, new_cap * sizeof(*stats->script_ids));
        if (!na) {
            stats->oom = true;
            return;
        }
        stats->script_ids = na;
        stats->script_id_cap = new_cap;
    }
    stats->script_ids[stats->script_id_count++] = id;
}

static void behavior_stats_add_script_sub_count(behavior_stats_data_t *stats,
                                                uint32_t value)
{
    if (stats->script_sub_count == stats->script_sub_cap) {
        size_t new_cap = (stats->script_sub_cap == 0) ? 16 : (stats->script_sub_cap * 2);
        uint32_t *na = (uint32_t *)realloc(
            stats->script_sub_counts, new_cap * sizeof(*stats->script_sub_counts));
        if (!na) {
            stats->oom = true;
            return;
        }
        stats->script_sub_counts = na;
        stats->script_sub_cap = new_cap;
    }
    stats->script_sub_counts[stats->script_sub_count++] = value;
}

static void behavior_stats_add_proto(behavior_stats_data_t *stats,
                                     nmo_guid_t guid)
{
    for (size_t j = 0; j < stats->proto_count; j++) {
        if (stats->protos[j].guid.d1 == guid.d1 &&
            stats->protos[j].guid.d2 == guid.d2) {
            stats->protos[j].count++;
            return;
        }
    }

    if (stats->proto_count == stats->proto_cap) {
        size_t new_cap = (stats->proto_cap == 0) ? 64 : (stats->proto_cap * 2);
        nmo_cli_bb_proto_count_t *na =
            (nmo_cli_bb_proto_count_t *)realloc(
                stats->protos, new_cap * sizeof(*stats->protos));
        if (!na) {
            stats->oom = true;
            return;
        }
        stats->protos = na;
        stats->proto_cap = new_cap;
    }

    stats->protos[stats->proto_count] = (nmo_cli_bb_proto_count_t){
        .guid = guid,
        .name = nmo_behavior_registry_get_name(stats->bb_reg, guid),
        .count = 1,
    };
    stats->proto_count++;
}

static void behavior_stats_add_guid_count(nmo_cli_guid_count_t **items,
                                          size_t *count,
                                          size_t *cap,
                                          nmo_guid_t guid,
                                          const char *name,
                                          bool *oom)
{
    if (nmo_guid_is_null(guid)) {
        return;
    }
    for (size_t i = 0; i < *count; i++) {
        if ((*items)[i].guid.d1 == guid.d1 &&
            (*items)[i].guid.d2 == guid.d2) {
            (*items)[i].count++;
            return;
        }
    }

    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
        nmo_cli_guid_count_t *na =
            (nmo_cli_guid_count_t *)realloc(
                *items, new_cap * sizeof(**items));
        if (!na) {
            *oom = true;
            return;
        }
        *items = na;
        *cap = new_cap;
    }

    (*items)[*count] = (nmo_cli_guid_count_t){
        .guid = guid,
        .name = name,
        .count = 1,
    };
    (*count)++;
}

static void behavior_stats_count_link_delay(behavior_stats_data_t *stats,
                                            const nmo_behaviorlink_state_t *link)
{
    int32_t delay = 0;
    if (link) {
        delay = link->initial_activation_delay != 0
            ? link->initial_activation_delay
            : link->activation_delay;
    }

    if (delay == 0) {
        stats->link_delay_zero++;
    } else if (delay == 1) {
        stats->link_delay_next_frame++;
    } else {
        stats->link_delay_multi_frame++;
    }
}

/*
 * The ten most frequent GUIDs. JSON: `array_key` items with name_key (the
 * name, or the bare GUID), guid_key and count. Text, when there are any: a
 * `heading` section with a two-column table.
 */
static bool behavior_stats_add_guid_counts(nmo_cli_record_t *rec,
                                           const char *array_key,
                                           const char *heading,
                                           const nmo_cli_table_col_t *columns,
                                           const char *name_key,
                                           const char *guid_key,
                                           const nmo_cli_guid_count_t *items,
                                           size_t count)
{
    bool ok = true;
    if (count > 0) {
        ok = nmo_cli_record_heading(rec, heading) &&
             nmo_cli_record_raw(rec, "\n");
    }
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, array_key, NULL) : NULL;
    ok = arr != NULL;
    if (ok && count > 0) {
        ok = nmo_cli_record_array_set_table(arr, columns, 2);
    }
    size_t top_n = count < 10 ? count : 10;
    for (size_t i = 0; ok && i < top_n; i++) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL;
        if (ok && items[i].name && items[i].name[0]) {
            ok = nmo_cli_record_str(item, name_key, columns[0].header, items[i].name);
        } else if (ok) {
            ok = nmo_cli_record_str_fmt(item, name_key, NULL, "%08X-%08X",
                                        items[i].guid.d1, items[i].guid.d2) &&
                 nmo_cli_record_text_fmt(item, columns[0].header, "{%08X-%08X}",
                                         items[i].guid.d1, items[i].guid.d2);
        }
        ok = ok &&
             nmo_cli_record_str_fmt(item, guid_key, NULL, "%08X-%08X",
                                    items[i].guid.d1, items[i].guid.d2) &&
             nmo_cli_record_uint(item, "count", "Count", (uint64_t)items[i].count);
        if (!ok) {
            nmo_cli_record_free(item);
            break;
        }
        ok = nmo_cli_record_array_add(arr, item);
    }
    return ok;
}

/* The ten most used BB prototypes, like behavior_stats_add_guid_counts. */
static bool behavior_stats_add_protos(nmo_cli_record_t *rec,
                                      const nmo_cli_bb_proto_count_t *protos,
                                      size_t count)
{
    static const nmo_cli_table_col_t columns[] = {
        {"Name", NMO_CLI_ALIGN_LEFT, 28, 50},
        {"Count", NMO_CLI_ALIGN_RIGHT, 6, 0},
    };

    bool ok = true;
    if (count > 0) {
        ok = nmo_cli_record_heading(rec, "Top BB Prototypes") &&
             nmo_cli_record_raw(rec, "\n");
    }
    nmo_cli_record_array_t *arr =
        ok ? nmo_cli_record_array(rec, "top_bb_prototypes", NULL) : NULL;
    ok = arr != NULL;
    if (ok && count > 0) {
        ok = nmo_cli_record_array_set_table(arr, columns,
                                            sizeof(columns) / sizeof(columns[0]));
    }
    size_t top_n = count < 10 ? count : 10;
    for (size_t i = 0; ok && i < top_n; i++) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL;
        if (ok && protos[i].name) {
            ok = nmo_cli_record_str(item, "name", "Name", protos[i].name);
        } else if (ok) {
            ok = nmo_cli_record_text_fmt(item, "Name", "{%08X-%08X}",
                                         protos[i].guid.d1, protos[i].guid.d2);
        }
        ok = ok &&
             nmo_cli_record_str_fmt(item, "guid", NULL, "%08X-%08X",
                                    protos[i].guid.d1, protos[i].guid.d2) &&
             nmo_cli_record_uint(item, "count", "Count", (uint64_t)protos[i].count);
        if (!ok) {
            nmo_cli_record_free(item);
            break;
        }
        ok = nmo_cli_record_array_add(arr, item);
    }
    return ok;
}

/* JSON-only {count, max, avg, p95} object. */
static bool behavior_stats_add_distribution(nmo_cli_record_t *rec,
                                            const char *key,
                                            nmo_cli_u32_distribution_t dist)
{
    nmo_cli_record_t *obj = nmo_cli_record_object(rec, key);
    return obj != NULL &&
           nmo_cli_record_uint(obj, "count", NULL, (uint64_t)dist.count) &&
           nmo_cli_record_uint(obj, "max", NULL, (uint64_t)dist.max) &&
           nmo_cli_record_real(obj, "avg", NULL, dist.avg, NULL) &&
           nmo_cli_record_uint(obj, "p95", NULL, (uint64_t)dist.p95);
}

static void behavior_stats_consume_object(behavior_stats_data_t *stats,
                                          nmo_object_t *obj)
{
    nmo_class_id_t cid = nmo_object_get_class_id(obj);

    if (cid == NMO_CID_PARAMETERIN || cid == NMO_CID_PARAMETEROUT ||
        cid == NMO_CID_PARAMETERLOCAL || cid == NMO_CID_PARAMETER) {
        stats->n_parameters++;
        nmo_guid_t type_guid = get_param_type_guid(obj);
        behavior_stats_add_guid_count(
            &stats->parameter_types,
            &stats->parameter_type_count,
            &stats->parameter_type_cap,
            type_guid,
            resolve_type(stats->registry, type_guid),
            &stats->oom);
        return;
    }
    if (cid == NMO_CID_BEHAVIORLINK) {
        stats->n_links++;
        const nmo_behaviorlink_state_t *link =
            (const nmo_behaviorlink_state_t *)nmo_object_get_state(obj);
        behavior_stats_count_link_delay(stats, link);
        return;
    }
    if (cid == NMO_CID_PARAMETEROPERATION) {
        stats->n_operations++;
        const nmo_parameteroperation_state_t *op =
            (const nmo_parameteroperation_state_t *)nmo_object_get_state(obj);
        if (op) {
            behavior_stats_add_guid_count(
                &stats->operation_types,
                &stats->operation_type_count,
                &stats->operation_type_cap,
                op->operation_guid,
                nmo_type_registry_guid_to_name(stats->registry, op->operation_guid),
                &stats->oom);
        }
        return;
    }
    if (!is_behavior_class(stats->registry, cid)) {
        return;
    }

    stats->total_behaviors++;
    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) return;

    if (bs->interface_data) {
        stats->n_with_interface++;
        const nmo_interface_data_t *id = bs->interface_data;
        stats->n_total_comments += id->script.body.comment_count;
        if (id->script.has_snapshot) stats->n_with_snapshot++;
        if (id->script.flags & NMO_INTERFACE_FLAG_FOLDED) stats->n_folded++;
        for (size_t s = 0; s < id->sub_count; s++) {
            stats->n_total_comments += id->subs[s].body.comment_count;
            if (id->subs[s].flags & NMO_INTERFACE_FLAG_FOLDED) stats->n_folded++;
            for (size_t l = 0; l < id->subs[s].body.link_count; l++)
                stats->n_total_routing_points += id->subs[s].body.links[l].point_count;
        }
        for (size_t l = 0; l < id->script.body.link_count; l++)
            stats->n_total_routing_points += id->script.body.links[l].point_count;
    }

    if (bs->flags & CKBEHAVIOR_SCRIPT) {
        stats->n_scripts++;
        behavior_stats_add_script_id(stats, nmo_object_get_id(obj));
        behavior_stats_add_script_sub_count(stats,
                                            (uint32_t)bs->sub_behaviors.count);
    } else if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) {
        stats->n_bbs++;
        if (!nmo_guid_is_null(bs->block_guid)) {
            behavior_stats_add_proto(stats, bs->block_guid);
        }
    } else {
        stats->n_graphs++;
    }

    if (stats->repo && bs->sub_behavior_links.data) {
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->sub_behavior_links, i);
            if (id == 0 ||
                nmo_object_repository_find_by_id(stats->repo, id) == NULL) {
                stats->broken_behavior_links++;
            }
        }
    }
    if (stats->repo && bs->sub_behaviors.data) {
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->sub_behaviors, i);
            if (id == 0 ||
                nmo_object_repository_find_by_id(stats->repo, id) == NULL) {
                stats->broken_sub_behaviors++;
            }
        }
    }
}

static int behavior_stats_core_visitor(size_t index,
                                       nmo_object_t *obj,
                                       const nmo_cmd_ctx_t *c,
                                       void *user)
{
    (void)index;
    (void)c;

    behavior_stats_data_t *stats = (behavior_stats_data_t *)user;
    behavior_stats_consume_object(stats, obj);
    return stats->oom ? 1 : 0;
}

/* Compute max depth of behavior tree rooted at beh_id */
static uint32_t compute_tree_depth(nmo_object_repository_t *repo,
                                   const nmo_type_registry_t *registry,
                                   nmo_object_id_t beh_id,
                                   uint32_t cur_depth) {
    if (cur_depth > 256) return cur_depth;
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, beh_id);
    if (!obj) return cur_depth;
    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!registry || !nmo_type_registry_is_class_derived_from(
            registry, (uint32_t)cid, (uint32_t)NMO_CID_BEHAVIOR))
        return cur_depth;
    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) return cur_depth;
    if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) return cur_depth;

    uint32_t max_d = cur_depth;
    if (bs->sub_behaviors.data) {
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->sub_behaviors, i);
            if (id == 0) continue;
            uint32_t d = compute_tree_depth(repo, registry, id,
                                            cur_depth + 1);
            if (d > max_d) max_d = d;
        }
    }
    return max_d;
}

static void behavior_stats_data_free(behavior_stats_data_t *stats)
{
    free(stats->protos);
    free(stats->parameter_types);
    free(stats->operation_types);
    free(stats->script_ids);
    free(stats->script_sub_counts);
}

static bool behavior_stats_build(nmo_cli_record_t *rec,
                                 const behavior_stats_data_t *stats,
                                 nmo_cli_u32_distribution_t tree_depth_dist,
                                 nmo_cli_u32_distribution_t script_sub_dist,
                                 nmo_workspace_t *workspace)
{
    static const nmo_cli_table_col_t type_columns[] = {
        {"Type", NMO_CLI_ALIGN_LEFT, 28, 50},
        {"Count", NMO_CLI_ALIGN_RIGHT, 6, 0},
    };
    static const nmo_cli_table_col_t operation_columns[] = {
        {"Operation", NMO_CLI_ALIGN_LEFT, 28, 50},
        {"Count", NMO_CLI_ALIGN_RIGHT, 6, 0},
    };

    bool ok = nmo_cli_record_title(rec, "Behavior Statistics") &&
              nmo_cli_record_raw(rec, "\n") &&
              nmo_cli_record_uint(rec, "total", "Total behaviors",
                                  (uint64_t)stats->total_behaviors) &&
              nmo_cli_record_uint(rec, "scripts", "Scripts", (uint64_t)stats->n_scripts) &&
              nmo_cli_record_uint(rec, "graphs", "Graphs", (uint64_t)stats->n_graphs) &&
              nmo_cli_record_uint(rec, "building_blocks", "Building Blocks",
                                  (uint64_t)stats->n_bbs) &&
              behavior_stats_add_protos(rec, stats->protos, stats->proto_count) &&
              behavior_stats_add_guid_counts(rec, "parameter_types_top",
                                             "Top Parameter Types", type_columns,
                                             "type_name", "type_guid",
                                             stats->parameter_types,
                                             stats->parameter_type_count) &&
              behavior_stats_add_guid_counts(rec, "operation_types_top",
                                             "Top Operation Types", operation_columns,
                                             "operation_name", "operation_guid",
                                             stats->operation_types,
                                             stats->operation_type_count) &&
              nmo_cli_record_raw(rec, "\n") &&
              nmo_cli_record_uint(rec, "total_parameters", "Parameters",
                                  (uint64_t)stats->n_parameters) &&
              nmo_cli_record_uint(rec, "total_links", "Links", (uint64_t)stats->n_links) &&
              nmo_cli_record_uint(rec, "total_operations", "Operations",
                                  (uint64_t)stats->n_operations) &&
              nmo_cli_record_uint(rec, "max_tree_depth", "Max tree depth",
                                  (uint64_t)tree_depth_dist.max) &&
              nmo_cli_record_text_fmt(rec, "Tree depth avg/p95", "%.2f / %u",
                                      tree_depth_dist.avg, tree_depth_dist.p95) &&
              nmo_cli_record_text_fmt(rec, "Script sub avg/p95", "%.2f / %u",
                                      script_sub_dist.avg, script_sub_dist.p95) &&
              behavior_stats_add_distribution(rec, "tree_depth", tree_depth_dist) &&
              behavior_stats_add_distribution(rec, "script_sub_behavior_counts",
                                              script_sub_dist) &&
              nmo_cli_record_heading(rec, "Link Delay Distribution") &&
              nmo_cli_record_raw(rec, "\n");

    nmo_cli_record_t *link_delays =
        ok ? nmo_cli_record_object(rec, "link_delay_distribution") : NULL;
    ok = link_delays != NULL &&
         nmo_cli_record_uint(link_delays, "zero_delay", "Zero delay",
                             (uint64_t)stats->link_delay_zero) &&
         nmo_cli_record_uint(link_delays, "next_frame", "Next frame",
                             (uint64_t)stats->link_delay_next_frame) &&
         nmo_cli_record_uint(link_delays, "multi_frame", "Multi frame",
                             (uint64_t)stats->link_delay_multi_frame) &&
         nmo_cli_record_heading(rec, "Broken References") &&
         nmo_cli_record_raw(rec, "\n");

    nmo_cli_record_t *broken =
        ok ? nmo_cli_record_object(rec, "broken_references") : NULL;
    ok = broken != NULL &&
         nmo_cli_record_uint(broken, "behavior_links", "Behavior links",
                             (uint64_t)stats->broken_behavior_links) &&
         nmo_cli_record_uint(broken, "sub_behaviors", "Sub behaviors",
                             (uint64_t)stats->broken_sub_behaviors) &&
         nmo_cmd_behavior_add_interface_diagnostics(rec, workspace, false);

    if (ok && stats->n_with_interface > 0) {
        ok = nmo_cli_record_heading(rec, "Interface Layout") &&
             nmo_cli_record_raw(rec, "\n");
        nmo_cli_record_t *iface =
            ok ? nmo_cli_record_object(rec, "interface_layout") : NULL;
        ok = iface != NULL &&
             nmo_cli_record_uint(iface, "with_interface_data", "With interface data",
                                 (uint64_t)stats->n_with_interface) &&
             nmo_cli_record_set_text_fmt(iface, "%zu / %zu", stats->n_with_interface,
                                         stats->total_behaviors) &&
             nmo_cli_record_uint(iface, "comments", "Comments",
                                 (uint64_t)stats->n_total_comments) &&
             nmo_cli_record_uint(iface, "folded", "Folded behaviors",
                                 (uint64_t)stats->n_folded) &&
             nmo_cli_record_uint(iface, "routing_points", "Link routing points",
                                 (uint64_t)stats->n_total_routing_points) &&
             nmo_cli_record_uint(iface, "with_snapshot", "With snapshot",
                                 (uint64_t)stats->n_with_snapshot);
    }
    return ok;
}

/* Gather the statistics of c's document into a new record in *out_rec. */
static int behavior_stats_run(nmo_cmd_ctx_t *c, nmo_cli_record_t **out_rec)
{
    *out_rec = NULL;
    if (!c->registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    if (!repo) {
        fprintf(stderr, "Error: Failed to get object repository\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (nmo_tool_owner_ensure_behavior_acceleration(c->workspace) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    behavior_stats_data_t stats = {
        .registry = c->registry,
        .bb_reg = nmo_context_get_bb_registry(c->ctx),
        .repo = repo,
    };
    int rc = nmo_core_object_query_run(c, NULL,
                                       behavior_stats_core_visitor, &stats, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS || stats.oom) {
        behavior_stats_data_free(&stats);
        fprintf(stderr, "Error: Failed to query objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Sort prototypes by count descending */
    if (stats.proto_count > 1) {
        qsort(stats.protos, stats.proto_count, sizeof(*stats.protos),
              bb_proto_count_cmp_desc);
    }
    if (stats.parameter_type_count > 1) {
        qsort(stats.parameter_types, stats.parameter_type_count,
              sizeof(*stats.parameter_types), guid_count_cmp_desc);
    }
    if (stats.operation_type_count > 1) {
        qsort(stats.operation_types, stats.operation_type_count,
              sizeof(*stats.operation_types), guid_count_cmp_desc);
    }

    /* Compute max tree depth across all scripts */
    uint32_t *tree_depths = NULL;
    if (stats.script_id_count > 0) {
        tree_depths = (uint32_t *)malloc(stats.script_id_count * sizeof(*tree_depths));
        if (!tree_depths) {
            behavior_stats_data_free(&stats);
            fprintf(stderr, "Error: Out of memory\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }
    for (size_t i = 0; i < stats.script_id_count; i++) {
        tree_depths[i] = compute_tree_depth(repo, c->registry, stats.script_ids[i], 0);
    }
    nmo_cli_u32_distribution_t tree_depth_dist =
        compute_u32_distribution(tree_depths, stats.script_id_count);
    nmo_cli_u32_distribution_t script_sub_dist =
        compute_u32_distribution(stats.script_sub_counts, stats.script_sub_count);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              behavior_stats_build(rec, &stats, tree_depth_dist, script_sub_dist,
                                   c->workspace);
    free(tree_depths);
    behavior_stats_data_free(&stats);
    if (!ok) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    *out_rec = rec;
    return NMO_CLI_EXIT_SUCCESS;
}

static int behavior_stats_single(const char *file_path,
                                 const nmo_cli_global_opts_t *global,
                                 void *user_data,
                                 yyjson_mut_doc *doc,
                                 yyjson_mut_val *data)
{
    const nmo_tool_text_output_ctx_t *text_ctx =
        (const nmo_tool_text_output_ctx_t *)user_data;

    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    char *open_error = NULL;

    if (!nmo_tool_open_document(file_path, &ctx, &document, &workspace, &open_error)) {
        fprintf(stderr, "Error: %s\n", open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_cmd_ctx_t cmd;
    nmo_cmd_ctx_init_from_repl_document(&cmd, ctx, document, workspace, false);

    nmo_cli_record_t *rec = NULL;
    int rc = behavior_stats_run(&cmd, &rec);
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        if (doc && data) {
            if (!nmo_cli_record_to_json(rec, doc, data)) {
                fprintf(stderr, "Error: Out of memory\n");
                rc = NMO_CLI_EXIT_INTERNAL_ERROR;
            }
        } else {
            FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
            bool colorize = text_ctx ? text_ctx->colorize : false;
            nmo_cli_record_print_kv(rec, out, 22, colorize);
        }
    }
    nmo_cli_record_free(rec);

    (void)global;
    nmo_tool_close_document(ctx, document, workspace);
    return rc;
}

int nmo_cmd_behavior_stats(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Batch mode */
    if (global->batch_mode) {
        const char *paths[256];
        size_t count = nmo_tool_find_file_args(argc, argv, paths, 256);
        if (count == 0) {
            fprintf(stderr, "Error: No files specified\n");
            fprintf(stderr, "Usage: nmo --batch behavior stats <file1> <file2> ...\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return nmo_tool_batch_run(paths, count, global, "behavior.stats",
                                  behavior_stats_single, NULL);
    }

    /* Single file mode */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_cli_record_t *rec = NULL;
    rc = behavior_stats_run(&c, &rec);
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        rc = nmo_cmd_ctx_emit_record(&c, rec, "behavior.stats", 22, c.colorize);
    }
    return nmo_cmd_ctx_done(&c, rc);
}
