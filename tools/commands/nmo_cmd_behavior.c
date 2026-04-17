/**
 * @file nmo_cmd_behavior.c
 * @brief CLI behavior command group -- shared helpers, list, and stats
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "behavior/nmo_bb_registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

const char *interface_color_to_hex(uint32_t color, char *buf, size_t size) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    snprintf(buf, size, "#%02X%02X%02X", r, g, b);
    return buf;
}

void nmo_cmd_behavior_add_interface_diagnostics_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    nmo_session_t *session)
{
    if (!doc || !data || !session) {
        return;
    }

    nmo_session_behavior_interface_diagnostics_t diag;
    nmo_session_get_behavior_interface_diagnostics(session, &diag);

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

void nmo_cmd_behavior_print_interface_diagnostics(
    FILE *out,
    nmo_session_t *session)
{
    if (!out || !session) {
        return;
    }

    nmo_session_behavior_interface_diagnostics_t diag;
    nmo_session_get_behavior_interface_diagnostics(session, &diag);
    if (!diag.attempted || diag.status == NMO_OK) {
        return;
    }

    fprintf(out,
            "Interface parse diagnostics: status=%s(%d), parsed=%zu/%zu, failed=%zu",
            nmo_error_string(diag.status),
            diag.status,
            diag.parsed_count,
            diag.attempted_count,
            diag.failed_count);
    if (diag.first_error_object_id != 0 || diag.first_error_file_id != 0) {
        fprintf(out,
                ", first_error_object=%u, file_id=%u, chunk_version=%u, data_version=%u, offset=%zu/%zu dwords",
                diag.first_error_object_id,
                diag.first_error_file_id,
                diag.first_error_chunk_version,
                diag.first_error_data_version,
                diag.first_error_reader_offset,
                diag.first_error_chunk_dwords);
    }
    fputc('\n', out);
}

/* ============================================================================
 * behavior list
 * ============================================================================ */

typedef struct behavior_list_data {
    nmo_context_t *ctx;
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    nmo_cli_table_t *table;
    size_t count;
} behavior_list_data_t;

static void behavior_list_add_json(yyjson_mut_doc *doc,
                                   yyjson_mut_val *arr,
                                   nmo_context_t *ctx,
                                   nmo_object_t *obj)
{
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
    yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

    const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
    if (class_name) nmo_cli_json_add_str_safe(doc, item, "class_name", class_name);

    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) nmo_cli_json_add_str_safe(doc, item, "name", name);

    yyjson_mut_arr_add_val(arr, item);
}

static void behavior_list_add_table_row(nmo_cli_table_t *table, nmo_object_t *obj) {
    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);

    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

    const char *type_str = "Graph";
    if (bs) {
        if (bs->flags & CKBEHAVIOR_SCRIPT) type_str = "Script";
        else if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) type_str = "BB";
    }

    char io_buf[16], pin_buf[16], pout_buf[16], sub_buf[16];
    size_t n_io = bs ? (bs->inputs.count + bs->outputs.count) : 0;
    snprintf(io_buf, sizeof(io_buf), "%zu", n_io);
    snprintf(pin_buf, sizeof(pin_buf), "%zu", bs ? bs->in_parameters.count : 0);
    snprintf(pout_buf, sizeof(pout_buf), "%zu", bs ? bs->out_parameters.count : 0);
    snprintf(sub_buf, sizeof(sub_buf), "%zu", bs ? bs->sub_behaviors.count : 0);

    const char *name = nmo_object_get_name(obj);
    const char *cells[] = {
        id_buf, type_str, io_buf, pin_buf, pout_buf, sub_buf,
        (name && name[0]) ? name : "-",
    };
    nmo_cli_table_add_row(table, cells, 7);
}

static int behavior_list_core_visitor(size_t index,
                                      nmo_object_t *obj,
                                      const nmo_cmd_ctx_t *c,
                                      void *user)
{
    (void)index;

    behavior_list_data_t *list = (behavior_list_data_t *)user;
    if (list->arr) {
        behavior_list_add_json(list->doc, list->arr, c->ctx, obj);
    } else if (list->table) {
        behavior_list_add_table_row(list->table, obj);
    }
    list->count++;
    return 0;
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
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_query_t query = {
        .class_id = NMO_CID_BEHAVIOR,
        .include_derived_classes = true,
    };

    nmo_cmd_ctx_t cmd;
    nmo_cmd_ctx_init_from_repl(&cmd, ctx, session, false);

    if (doc && data) {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        behavior_list_data_t ld = { .ctx = ctx, .doc = doc, .arr = arr };
        if (nmo_core_object_query_run(&cmd, &query,
                                      behavior_list_core_visitor, &ld,
                                      NULL) != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Error: Failed to query objects\n");
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)ld.count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);
    } else {
        FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
        bool colorize = text_ctx ? text_ctx->colorize : false;

        static const nmo_cli_table_col_t columns[] = {
            {"ID",   NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"TYPE", NMO_CLI_ALIGN_LEFT,  6, 0},
            {"IO",   NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"PIN",  NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"POUT", NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"SUB",  NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"NAME", NMO_CLI_ALIGN_LEFT, 24, 50},
        };
        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        behavior_list_data_t ld = { .ctx = ctx, .table = &table };
        if (nmo_core_object_query_run(&cmd, &query,
                                      behavior_list_core_visitor, &ld,
                                      NULL) != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Error: Failed to query objects\n");
            nmo_cli_table_free(&table);
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        fprintf(out, "Behaviors: %zu\n\n", ld.count);
        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    (void)global;
    nmo_tool_close_session(ctx, session);
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

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        behavior_list_data_t ld = { .ctx = c.ctx, .doc = doc, .arr = arr };
        rc = nmo_core_object_query_run(&c, &query,
                                       behavior_list_core_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&c, rc);
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)ld.count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID",   NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"TYPE", NMO_CLI_ALIGN_LEFT,  6, 0},
            {"IO",   NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"PIN",  NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"POUT", NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"SUB",  NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"NAME", NMO_CLI_ALIGN_LEFT, 24, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        behavior_list_data_t ld = { .table = &table };
        rc = nmo_core_object_query_run(&c, &query,
                                       behavior_list_core_visitor, &ld, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            return nmo_cmd_ctx_done(&c, rc);
        }

        fprintf(c.out, "Behaviors: %zu\n\n", ld.count);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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

static int bb_proto_count_cmp_desc(const void *a, const void *b) {
    const nmo_cli_bb_proto_count_t *aa = (const nmo_cli_bb_proto_count_t *)a;
    const nmo_cli_bb_proto_count_t *bb = (const nmo_cli_bb_proto_count_t *)b;
    if (aa->count != bb->count) {
        return (aa->count < bb->count) ? 1 : -1;
    }
    return 0;
}

typedef struct behavior_stats_data {
    const nmo_type_registry_t *registry;
    const nmo_bb_registry_t *bb_reg;
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
    nmo_object_id_t *script_ids;
    size_t script_id_count;
    size_t script_id_cap;
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
        .name = nmo_bb_registry_get_name(stats->bb_reg, guid),
        .count = 1,
    };
    stats->proto_count++;
}

static void behavior_stats_consume_object(behavior_stats_data_t *stats,
                                          nmo_object_t *obj)
{
    nmo_class_id_t cid = nmo_object_get_class_id(obj);

    if (cid == NMO_CID_PARAMETERIN || cid == NMO_CID_PARAMETEROUT ||
        cid == NMO_CID_PARAMETERLOCAL || cid == NMO_CID_PARAMETER) {
        stats->n_parameters++;
        return;
    }
    if (cid == NMO_CID_BEHAVIORLINK) {
        stats->n_links++;
        return;
    }
    if (cid == NMO_CID_PARAMETEROPERATION) {
        stats->n_operations++;
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
    } else if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) {
        stats->n_bbs++;
        if (!nmo_guid_is_null(bs->block_guid)) {
            behavior_stats_add_proto(stats, bs->block_guid);
        }
    } else {
        stats->n_graphs++;
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
        const nmo_object_id_t *subs =
            (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            uint32_t d = compute_tree_depth(repo, registry, subs[i],
                                            cur_depth + 1);
            if (d > max_d) max_d = d;
        }
    }
    return max_d;
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
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        fprintf(stderr, "Error: Failed to get object repository\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (nmo_session_ensure_behavior_acceleration(session) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    const nmo_bb_registry_t *bb_reg = nmo_context_get_bb_registry(ctx);

    behavior_stats_data_t stats = {
        .registry = registry,
        .bb_reg = bb_reg,
    };
    nmo_cmd_ctx_t cmd;
    nmo_cmd_ctx_init_from_repl(&cmd, ctx, session, false);
    if (nmo_core_object_query_run(&cmd, NULL,
                                  behavior_stats_core_visitor, &stats,
                                  NULL) != NMO_CLI_EXIT_SUCCESS ||
        stats.oom) {
        fprintf(stderr, "Error: Failed to query objects\n");
        free(stats.protos);
        free(stats.script_ids);
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t total_behaviors = stats.total_behaviors;
    size_t n_scripts = stats.n_scripts;
    size_t n_graphs = stats.n_graphs;
    size_t n_bbs = stats.n_bbs;
    size_t n_parameters = stats.n_parameters;
    size_t n_links = stats.n_links;
    size_t n_operations = stats.n_operations;
    size_t n_with_interface = stats.n_with_interface;
    size_t n_total_comments = stats.n_total_comments;
    size_t n_total_routing_points = stats.n_total_routing_points;
    size_t n_folded = stats.n_folded;
    size_t n_with_snapshot = stats.n_with_snapshot;
    nmo_cli_bb_proto_count_t *protos = stats.protos;
    size_t proto_count = stats.proto_count;
    nmo_object_id_t *script_ids = stats.script_ids;
    size_t script_id_count = stats.script_id_count;

    if (proto_count > 1)
        qsort(protos, proto_count, sizeof(*protos), bb_proto_count_cmp_desc);

    uint32_t max_depth = 0;
    for (size_t i = 0; i < script_id_count; i++) {
        uint32_t d = compute_tree_depth(repo, registry, script_ids[i], 0);
        if (d > max_depth) max_depth = d;
    }

    if (doc && data) {
        yyjson_mut_obj_add_uint(doc, data, "total", (uint64_t)total_behaviors);
        yyjson_mut_obj_add_uint(doc, data, "scripts", (uint64_t)n_scripts);
        yyjson_mut_obj_add_uint(doc, data, "graphs", (uint64_t)n_graphs);
        yyjson_mut_obj_add_uint(doc, data, "building_blocks", (uint64_t)n_bbs);

        yyjson_mut_val *proto_arr = yyjson_mut_arr(doc);
        size_t top_n = proto_count < 10 ? proto_count : 10;
        for (size_t i = 0; i < top_n; i++) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            if (protos[i].name)
                nmo_cli_json_add_str_safe(doc, item, "name", protos[i].name);
            char guid_buf[24];
            snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                     protos[i].guid.d1, protos[i].guid.d2);
            nmo_cli_json_add_str_safe(doc, item, "guid", guid_buf);
            yyjson_mut_obj_add_uint(doc, item, "count", (uint64_t)protos[i].count);
            yyjson_mut_arr_add_val(proto_arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "top_bb_prototypes", proto_arr);

        yyjson_mut_obj_add_uint(doc, data, "total_parameters", (uint64_t)n_parameters);
        yyjson_mut_obj_add_uint(doc, data, "total_links", (uint64_t)n_links);
        yyjson_mut_obj_add_uint(doc, data, "total_operations", (uint64_t)n_operations);
        yyjson_mut_obj_add_uint(doc, data, "max_tree_depth", (uint64_t)max_depth);
        nmo_cmd_behavior_add_interface_diagnostics_json(doc, data, session);

        if (n_with_interface > 0) {
            yyjson_mut_val *iface = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, iface, "with_interface_data", (uint64_t)n_with_interface);
            yyjson_mut_obj_add_uint(doc, iface, "comments", (uint64_t)n_total_comments);
            yyjson_mut_obj_add_uint(doc, iface, "folded", (uint64_t)n_folded);
            yyjson_mut_obj_add_uint(doc, iface, "routing_points", (uint64_t)n_total_routing_points);
            yyjson_mut_obj_add_uint(doc, iface, "with_snapshot", (uint64_t)n_with_snapshot);
            yyjson_mut_obj_add_val(doc, data, "interface_layout", iface);
        }
    } else {
        FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
        bool colorize = text_ctx ? text_ctx->colorize : false;

        nmo_cli_print_heading(out, "Behavior Statistics", colorize);
        fprintf(out, "\n");

        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", total_behaviors);
        nmo_cli_print_kv(out, "Total behaviors", buf, 22, colorize);
        snprintf(buf, sizeof(buf), "%zu", n_scripts);
        nmo_cli_print_kv(out, "Scripts", buf, 22, colorize);
        snprintf(buf, sizeof(buf), "%zu", n_graphs);
        nmo_cli_print_kv(out, "Graphs", buf, 22, colorize);
        snprintf(buf, sizeof(buf), "%zu", n_bbs);
        nmo_cli_print_kv(out, "Building Blocks", buf, 22, colorize);

        if (proto_count > 0) {
            fprintf(out, "\n");
            nmo_cli_print_heading(out, "Top BB Prototypes", colorize);
            fprintf(out, "\n");

            static const nmo_cli_table_col_t proto_columns[] = {
                {"Name", NMO_CLI_ALIGN_LEFT, 28, 50},
                {"Count", NMO_CLI_ALIGN_RIGHT, 6, 0},
            };
            nmo_cli_table_t table;
            nmo_cli_table_init(&table, proto_columns,
                               sizeof(proto_columns) / sizeof(proto_columns[0]));

            size_t top_n = proto_count < 10 ? proto_count : 10;
            for (size_t i = 0; i < top_n; i++) {
                char count_buf[16];
                snprintf(count_buf, sizeof(count_buf), "%zu", protos[i].count);
                char name_buf[80];
                if (protos[i].name)
                    snprintf(name_buf, sizeof(name_buf), "%s", protos[i].name);
                else
                    snprintf(name_buf, sizeof(name_buf), "{%08X-%08X}",
                             protos[i].guid.d1, protos[i].guid.d2);
                const char *cells[] = { name_buf, count_buf };
                nmo_cli_table_add_row(&table, cells, 2);
            }
            nmo_cli_table_print(&table, out, colorize);
            nmo_cli_table_free(&table);
        }

        fprintf(out, "\n");
        snprintf(buf, sizeof(buf), "%zu", n_parameters);
        nmo_cli_print_kv(out, "Parameters", buf, 22, colorize);
        snprintf(buf, sizeof(buf), "%zu", n_links);
        nmo_cli_print_kv(out, "Links", buf, 22, colorize);
        snprintf(buf, sizeof(buf), "%zu", n_operations);
        nmo_cli_print_kv(out, "Operations", buf, 22, colorize);
        snprintf(buf, sizeof(buf), "%u", max_depth);
        nmo_cli_print_kv(out, "Max tree depth", buf, 22, colorize);

        if (n_with_interface > 0) {
            fprintf(out, "\n");
            nmo_cli_print_heading(out, "Interface Layout", colorize);
            fprintf(out, "\n");
            snprintf(buf, sizeof(buf), "%zu / %zu", n_with_interface, total_behaviors);
            nmo_cli_print_kv(out, "With interface data", buf, 22, colorize);
            snprintf(buf, sizeof(buf), "%zu", n_total_comments);
            nmo_cli_print_kv(out, "Comments", buf, 22, colorize);
            snprintf(buf, sizeof(buf), "%zu", n_folded);
            nmo_cli_print_kv(out, "Folded behaviors", buf, 22, colorize);
            snprintf(buf, sizeof(buf), "%zu", n_total_routing_points);
            nmo_cli_print_kv(out, "Link routing points", buf, 22, colorize);
            snprintf(buf, sizeof(buf), "%zu", n_with_snapshot);
            nmo_cli_print_kv(out, "With snapshot", buf, 22, colorize);
        }
    }

    (void)global;
    free(protos);
    free(script_ids);
    nmo_tool_close_session(ctx, session);
    return NMO_CLI_EXIT_SUCCESS;
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

    if (!c.registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    if (!repo) {
        fprintf(stderr, "Error: Failed to get object repository\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    if (nmo_session_ensure_behavior_acceleration(c.session) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    const nmo_bb_registry_t *bb_reg = nmo_context_get_bb_registry(c.ctx);

    behavior_stats_data_t stats = {
        .registry = c.registry,
        .bb_reg = bb_reg,
    };
    rc = nmo_core_object_query_run(&c, NULL,
                                   behavior_stats_core_visitor, &stats, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS || stats.oom) {
        free(stats.protos);
        free(stats.script_ids);
        fprintf(stderr, "Error: Failed to query objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    size_t total_behaviors = stats.total_behaviors;
    size_t n_scripts = stats.n_scripts;
    size_t n_graphs = stats.n_graphs;
    size_t n_bbs = stats.n_bbs;
    size_t n_parameters = stats.n_parameters;
    size_t n_links = stats.n_links;
    size_t n_operations = stats.n_operations;
    size_t n_with_interface = stats.n_with_interface;
    size_t n_total_comments = stats.n_total_comments;
    size_t n_total_routing_points = stats.n_total_routing_points;
    size_t n_folded = stats.n_folded;
    size_t n_with_snapshot = stats.n_with_snapshot;
    nmo_cli_bb_proto_count_t *protos = stats.protos;
    size_t proto_count = stats.proto_count;
    nmo_object_id_t *script_ids = stats.script_ids;
    size_t script_id_count = stats.script_id_count;

    /* Sort prototypes by count descending */
    if (proto_count > 1) {
        qsort(protos, proto_count, sizeof(*protos), bb_proto_count_cmp_desc);
    }

    /* Compute max tree depth across all scripts */
    uint32_t max_depth = 0;
    for (size_t i = 0; i < script_id_count; i++) {
        uint32_t d = compute_tree_depth(repo, c.registry, script_ids[i], 0);
        if (d > max_depth) max_depth = d;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total", (uint64_t)total_behaviors);
        yyjson_mut_obj_add_uint(doc, data, "scripts", (uint64_t)n_scripts);
        yyjson_mut_obj_add_uint(doc, data, "graphs", (uint64_t)n_graphs);
        yyjson_mut_obj_add_uint(doc, data, "building_blocks", (uint64_t)n_bbs);

        yyjson_mut_val *proto_arr = yyjson_mut_arr(doc);
        size_t top_n = proto_count < 10 ? proto_count : 10;
        for (size_t i = 0; i < top_n; i++) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            if (protos[i].name) {
                nmo_cli_json_add_str_safe(doc, item, "name", protos[i].name);
            }
            char guid_buf[24];
            snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                     protos[i].guid.d1, protos[i].guid.d2);
            nmo_cli_json_add_str_safe(doc, item, "guid", guid_buf);
            yyjson_mut_obj_add_uint(doc, item, "count",
                                    (uint64_t)protos[i].count);
            yyjson_mut_arr_add_val(proto_arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "top_bb_prototypes", proto_arr);

        yyjson_mut_obj_add_uint(doc, data, "total_parameters",
                                (uint64_t)n_parameters);
        yyjson_mut_obj_add_uint(doc, data, "total_links", (uint64_t)n_links);
        yyjson_mut_obj_add_uint(doc, data, "total_operations",
                                (uint64_t)n_operations);
        yyjson_mut_obj_add_uint(doc, data, "max_tree_depth",
                                (uint64_t)max_depth);
        nmo_cmd_behavior_add_interface_diagnostics_json(doc, data, c.session);

        if (n_with_interface > 0) {
            yyjson_mut_val *iface = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, iface, "with_interface_data", (uint64_t)n_with_interface);
            yyjson_mut_obj_add_uint(doc, iface, "comments", (uint64_t)n_total_comments);
            yyjson_mut_obj_add_uint(doc, iface, "folded", (uint64_t)n_folded);
            yyjson_mut_obj_add_uint(doc, iface, "routing_points", (uint64_t)n_total_routing_points);
            yyjson_mut_obj_add_uint(doc, iface, "with_snapshot", (uint64_t)n_with_snapshot);
            yyjson_mut_obj_add_val(doc, data, "interface_layout", iface);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.stats");
    } else {
        nmo_cli_print_heading(c.out, "Behavior Statistics", c.colorize);
        fprintf(c.out, "\n");

        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", total_behaviors);
        nmo_cli_print_kv(c.out, "Total behaviors", buf, 22, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", n_scripts);
        nmo_cli_print_kv(c.out, "Scripts", buf, 22, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", n_graphs);
        nmo_cli_print_kv(c.out, "Graphs", buf, 22, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", n_bbs);
        nmo_cli_print_kv(c.out, "Building Blocks", buf, 22, c.colorize);

        /* Top BB prototypes */
        if (proto_count > 0) {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Top BB Prototypes", c.colorize);
            fprintf(c.out, "\n");

            static const nmo_cli_table_col_t proto_columns[] = {
                {"Name", NMO_CLI_ALIGN_LEFT, 28, 50},
                {"Count", NMO_CLI_ALIGN_RIGHT, 6, 0},
            };
            nmo_cli_table_t table;
            nmo_cli_table_init(&table, proto_columns,
                               sizeof(proto_columns) / sizeof(proto_columns[0]));

            size_t top_n = proto_count < 10 ? proto_count : 10;
            for (size_t i = 0; i < top_n; i++) {
                char count_buf[16];
                snprintf(count_buf, sizeof(count_buf), "%zu", protos[i].count);
                char name_buf[80];
                if (protos[i].name) {
                    snprintf(name_buf, sizeof(name_buf), "%s", protos[i].name);
                } else {
                    snprintf(name_buf, sizeof(name_buf), "{%08X-%08X}",
                             protos[i].guid.d1, protos[i].guid.d2);
                }
                const char *cells[] = { name_buf, count_buf };
                nmo_cli_table_add_row(&table, cells, 2);
            }

            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }

        fprintf(c.out, "\n");
        snprintf(buf, sizeof(buf), "%zu", n_parameters);
        nmo_cli_print_kv(c.out, "Parameters", buf, 22, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", n_links);
        nmo_cli_print_kv(c.out, "Links", buf, 22, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", n_operations);
        nmo_cli_print_kv(c.out, "Operations", buf, 22, c.colorize);
        snprintf(buf, sizeof(buf), "%u", max_depth);
        nmo_cli_print_kv(c.out, "Max tree depth", buf, 22, c.colorize);

        if (n_with_interface > 0) {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Interface Layout", c.colorize);
            fprintf(c.out, "\n");
            snprintf(buf, sizeof(buf), "%zu / %zu", n_with_interface, total_behaviors);
            nmo_cli_print_kv(c.out, "With interface data", buf, 22, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", n_total_comments);
            nmo_cli_print_kv(c.out, "Comments", buf, 22, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", n_folded);
            nmo_cli_print_kv(c.out, "Folded behaviors", buf, 22, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", n_total_routing_points);
            nmo_cli_print_kv(c.out, "Link routing points", buf, 22, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", n_with_snapshot);
            nmo_cli_print_kv(c.out, "With snapshot", buf, 22, c.colorize);
        }
    }

    free(protos);
    free(script_ids);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
