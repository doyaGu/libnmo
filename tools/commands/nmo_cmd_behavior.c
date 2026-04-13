/**
 * @file nmo_cmd_behavior.c
 * @brief CLI behavior command group implementation
 */

#include "nmo_cmd_behavior.h"

#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "behavior/nmo_behavior_graph.h"
#include "behavior/nmo_behavior_index.h"
#include "behavior/nmo_script_walker.h"
#include "session/nmo_context.h"
#include "core/nmo_array.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_interface_edit.h"
#include "format/nmo_object.h"
#include "app/nmo_save.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "behavior/nmo_bb_registry.h"
#include "behavior/nmo_param_value.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Behavior flag constants */
#define CKBEHAVIOR_SCRIPT          0x00000002u
#define CKBEHAVIOR_BUILDINGBLOCK   0x00008000u

static int is_behavior_class(const nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return 0;
    }
    return nmo_type_registry_is_class_derived_from(
        registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEHAVIOR) ? 1 : 0;
}

typedef nmo_behavior_graph_node_t nmo_cli_graph_node_t;
typedef nmo_behavior_graph_edge_t nmo_cli_graph_edge_t;

static bool parse_behavior_graph_args(int argc, char **argv,
                                      nmo_object_id_t *out_id,
                                      const char **out_file,
                                      bool *out_dot,
                                      size_t *out_max_nodes,
                                      size_t *out_max_edges,
                                      uint32_t *out_depth)
{
    static const nmo_opt_def_t opts[] = {
        {"--dot",       NULL, NMO_OPT_FLAG, "Emit DOT graph output"},
        {"--max-nodes", NULL, NMO_OPT_UINT, "Max nodes to display"},
        {"--max-edges", NULL, NMO_OPT_UINT, "Max edges to display"},
        {"--depth",     "-d", NMO_OPT_UINT, "Recursion depth (default: unlimited)"},
        {"--json",      "-j", NMO_OPT_FLAG, "JSON output"},
    };
    enum { OPT_DOT, OPT_MAX_NODES, OPT_MAX_EDGES, OPT_DEPTH, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return false;

    if (r.pos_count < 2) return false;

    const char *id_str = r.pos_args[0];
    const char *file_path = r.pos_args[1];

    uint32_t id = 0;
    if (!nmo_tool_parse_u32(id_str, &id) || id == 0) {
        return false;
    }

    if (out_id) *out_id = (nmo_object_id_t)id;
    if (out_file) *out_file = file_path;
    if (out_dot) *out_dot = vals[OPT_DOT].val.flag;
    if (out_max_nodes) *out_max_nodes = vals[OPT_MAX_NODES].present ? (size_t)vals[OPT_MAX_NODES].val.u : 0;
    if (out_max_edges) *out_max_edges = vals[OPT_MAX_EDGES].present ? (size_t)vals[OPT_MAX_EDGES].val.u : 0;
    if (out_depth) *out_depth = vals[OPT_DEPTH].present ? vals[OPT_DEPTH].val.u : UINT32_MAX;
    return true;
}

static bool node_id_in_set(const nmo_object_id_t *ids, size_t count, nmo_object_id_t id) {
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

static void dot_write_label(FILE *out, const char *label) {
    if (!out) {
        return;
    }
    if (!label) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)label; *p; ++p) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            fputc('\\', out);
            fputc((char)c, out);
        } else if (c == '\n' || c == '\r') {
            fputs("\\n", out);
        } else if (c == '\t') {
            fputs("\\t", out);
        } else if (isprint(c)) {
            fputc((char)c, out);
        } else {
            fputc('?', out);
        }
    }
}

static const nmo_cli_graph_node_t *find_graph_node(
    const nmo_cli_graph_node_t *nodes,
    size_t count,
    nmo_object_id_t id)
{
    if (!nodes || id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (nodes[i].id == id) {
            return &nodes[i];
        }
    }
    return NULL;
}

/* ---- Interface data helpers ---- */

static const nmo_interface_behavior_t *find_interface_sub(
    const nmo_interface_data_t *idata, nmo_object_id_t behavior_id)
{
    if (!idata) return NULL;
    for (size_t i = 0; i < idata->sub_count; i++) {
        if (idata->subs[i].behavior_id == behavior_id)
            return &idata->subs[i];
    }
    return NULL;
}

static bool find_interface_position(const nmo_interface_data_t *idata,
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

static bool find_operation_position(const nmo_interface_data_t *idata,
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

static const nmo_interface_link_t *find_interface_link(
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

static const char *interface_color_to_hex(uint32_t color, char *buf, size_t size) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    snprintf(buf, size, "#%02X%02X%02X", r, g, b);
    return buf;
}

/* ---- behavior list: per-file handler for batch mode ---- */

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

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t behavior_count = 0;

    if (doc && data) {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);
            if (!is_behavior_class(registry, class_id)) continue;

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            if (class_name) nmo_cli_json_add_str_safe(doc, item, "class_name", class_name);

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) nmo_cli_json_add_str_safe(doc, item, "name", name);

            yyjson_mut_arr_add_val(arr, item);
            behavior_count++;
        }
        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)behavior_count);
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

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);
            if (!is_behavior_class(registry, class_id)) continue;

            const nmo_behavior_state_t *bs =
                (const nmo_behavior_state_t *)nmo_object_get_state(obj);

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *type_str = "Graph";
            if (bs) {
                if (bs->flags & 0x2) type_str = "Script";
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
            nmo_cli_table_add_row(&table, cells, 7);
            behavior_count++;
        }

        fprintf(out, "Behaviors: %zu\n\n", behavior_count);
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

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    size_t behavior_count = 0;

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            if (!is_behavior_class(c.registry, class_id)) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, item, "name", name);
            }

            yyjson_mut_arr_add_val(arr, item);
            behavior_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)behavior_count);
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

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);
            if (!is_behavior_class(c.registry, class_id)) {
                continue;
            }

            const nmo_behavior_state_t *bs =
                (const nmo_behavior_state_t *)nmo_object_get_state(obj);

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *type_str = "Graph";
            if (bs) {
                if (bs->flags & 0x2) type_str = "Script";
                else if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) type_str = "BB";
            }

            char io_buf[16], pin_buf[16], pout_buf[16], sub_buf[16];
            size_t n_io = bs ? (bs->inputs.count + bs->outputs.count) : 0;
            snprintf(io_buf, sizeof(io_buf), "%zu", n_io);
            snprintf(pin_buf, sizeof(pin_buf), "%zu",
                     bs ? bs->in_parameters.count : 0);
            snprintf(pout_buf, sizeof(pout_buf), "%zu",
                     bs ? bs->out_parameters.count : 0);
            snprintf(sub_buf, sizeof(sub_buf), "%zu",
                     bs ? bs->sub_behaviors.count : 0);

            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                id_buf, type_str, io_buf, pin_buf, pout_buf, sub_buf,
                (name && name[0]) ? name : "-",
            };
            nmo_cli_table_add_row(&table, cells, 7);
            behavior_count++;
        }

        fprintf(c.out, "Behaviors: %zu\n\n", behavior_count);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* Helper: resolve an object ID to name, return "(unnamed)" if NULL/empty */
static const char *resolve_name(nmo_object_repository_t *repo, nmo_object_id_t id) {
    if (id == 0) return "(none)";
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) return "(missing)";
    const char *n = nmo_object_get_name(obj);
    return (n && n[0]) ? n : "(unnamed)";
}

/* Helper: resolve parameter type GUID to name */
static const char *resolve_type(const nmo_type_registry_t *reg, nmo_guid_t guid) {
    if (nmo_guid_is_null(guid)) return "?";
    const char *n = nmo_field_type_name(reg, guid);
    return n ? n : "?";
}

/* Helper: get parameter type GUID from any parameter object */
static nmo_guid_t get_param_type_guid(nmo_object_t *obj) {
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

int nmo_cmd_behavior_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--raw",  NULL, NMO_OPT_FLAG, "Show raw reflection (like object show)"},
        {"--json", "-j", NMO_OPT_FLAG, "JSON output"},
    };
    enum { OPT_RAW, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool raw_mode = vals[OPT_RAW].present && vals[OPT_RAW].val.flag;
    if (raw_mode) {
        return nmo_cmd_object_show(argc, argv, global);
    }

    const char *id_str = r.pos_count > 0 ? r.pos_args[0] : NULL;
    if (!id_str) {
        fprintf(stderr, "Usage: nmo behavior show <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t target_id;
    if (!nmo_tool_parse_u32(id_str, &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_object_t *beh = nmo_object_repository_find_by_id(repo, target_id);
    if (!beh) {
        fprintf(stderr, "Error: Object %u not found\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (!is_behavior_class(c.registry, nmo_object_get_class_id(beh))) {
        fprintf(stderr, "Error: Object %u is not a CKBehavior\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)nmo_object_get_state(beh);
    if (!bs) {
        fprintf(stderr, "Error: No state for behavior %u\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(beh);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", target_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");
        yyjson_mut_obj_add_uint(doc, data, "class_id",
                                nmo_object_get_class_id(beh));
        const char *cls_name = nmo_cli_class_name_from_id(
            c.ctx, nmo_object_get_class_id(beh));
        if (cls_name) {
            nmo_cli_json_add_str_safe(doc, data, "class_name", cls_name);
        }

        bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
        bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
        nmo_cli_json_add_str_safe(doc, data, "behavior_type",
                                  is_script ? "Script"
                                            : is_bb ? "BB" : "Graph");
        yyjson_mut_obj_add_uint(doc, data, "flags", bs->flags);
        yyjson_mut_obj_add_int(doc, data, "priority", bs->priority);
        yyjson_mut_obj_add_int(doc, data, "compatible_class_id",
                               bs->compatible_class_id);

        if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
            char guid_buf[24];
            snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                     bs->block_guid.d1, bs->block_guid.d2);
            nmo_cli_json_add_str_safe(doc, data, "bb_guid", guid_buf);
            yyjson_mut_obj_add_uint(doc, data, "bb_version",
                                    bs->block_version);
            const char *proto_name = nmo_bb_registry_get_name(
                nmo_context_get_bb_registry(c.ctx), bs->block_guid);
            if (proto_name) {
                nmo_cli_json_add_str_safe(doc, data, "bb_proto_name",
                                          proto_name);
            }
        }

        if (bs->target_parameter_id != 0) {
            yyjson_mut_obj_add_uint(doc, data, "target_parameter_id",
                                    bs->target_parameter_id);
        }

        /* IO Ports: inputs */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->inputs.data;
            for (size_t i = 0; i < bs->inputs.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_cli_json_add_str_safe(doc, item, "name",
                                          resolve_name(repo, ids[i]));
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "inputs", arr);
        }

        /* IO Ports: outputs */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->outputs.data;
            for (size_t i = 0; i < bs->outputs.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_cli_json_add_str_safe(doc, item, "name",
                                          resolve_name(repo, ids[i]));
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "outputs", arr);
        }

        /* Input parameters */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->in_parameters.data;
            for (size_t i = 0; i < bs->in_parameters.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_object_t *p =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                const char *pname = p ? nmo_object_get_name(p) : NULL;
                nmo_cli_json_add_str_safe(doc, item, "name",
                    (pname && pname[0]) ? pname : "");
                nmo_guid_t tg = get_param_type_guid(p);
                nmo_cli_json_add_str_safe(doc, item, "type",
                                          resolve_type(c.registry, tg));
                if (p && nmo_object_get_class_id(p) == NMO_CID_PARAMETERIN) {
                    const nmo_parameterin_state_t *pin =
                        (const nmo_parameterin_state_t *)
                            nmo_object_get_state(p);
                    if (pin && pin->source_id != 0) {
                        yyjson_mut_obj_add_uint(doc, item, "source_id",
                                                pin->source_id);
                        if (pin->is_shared) {
                            yyjson_mut_obj_add_bool(doc, item, "is_shared",
                                                    true);
                        }
                    }
                }
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "input_parameters", arr);
        }

        /* Output parameters */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->out_parameters.data;
            for (size_t i = 0; i < bs->out_parameters.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_object_t *p =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                const char *pname = p ? nmo_object_get_name(p) : NULL;
                nmo_cli_json_add_str_safe(doc, item, "name",
                    (pname && pname[0]) ? pname : "");
                nmo_guid_t tg = get_param_type_guid(p);
                nmo_cli_json_add_str_safe(doc, item, "type",
                                          resolve_type(c.registry, tg));
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "output_parameters", arr);
        }

        /* Local parameters */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->local_parameters.data;
            for (size_t i = 0; i < bs->local_parameters.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_object_t *p =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                const char *pname = p ? nmo_object_get_name(p) : NULL;
                nmo_cli_json_add_str_safe(doc, item, "name",
                    (pname && pname[0]) ? pname : "");
                nmo_guid_t tg = get_param_type_guid(p);
                nmo_cli_json_add_str_safe(doc, item, "type",
                                          resolve_type(c.registry, tg));
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "local_parameters", arr);
        }

        /* Operations */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *op_ids =
                (const nmo_object_id_t *)bs->operations.data;
            for (size_t i = 0; i < bs->operations.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", op_ids[i]);
                nmo_object_t *op_obj =
                    nmo_object_repository_find_by_id(repo, op_ids[i]);
                if (op_obj && op_obj->state) {
                    const nmo_parameteroperation_state_t *op_state =
                        (const nmo_parameteroperation_state_t *)op_obj->state;
                    const char *op_name = nmo_type_registry_guid_to_name(
                        c.registry, op_state->operation_guid);
                    if (op_name) {
                        nmo_cli_json_add_str_safe(doc, item, "operation",
                                                  op_name);
                    }
                    if (op_state->has_in1) {
                        yyjson_mut_obj_add_uint(doc, item, "in1_id",
                                                op_state->in1_id);
                    }
                    if (op_state->has_in2) {
                        yyjson_mut_obj_add_uint(doc, item, "in2_id",
                                                op_state->in2_id);
                    }
                    if (op_state->has_out) {
                        yyjson_mut_obj_add_uint(doc, item, "out_id",
                                                op_state->out_id);
                    }
                }
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "operations", arr);
        }

        /* Sub-behaviors */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->sub_behaviors.data;
            for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                nmo_object_t *sub =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                const char *sname = sub ? nmo_object_get_name(sub) : NULL;
                nmo_cli_json_add_str_safe(doc, item, "name",
                    (sname && sname[0]) ? sname : "");
                if (sub && sub->state) {
                    const nmo_behavior_state_t *sub_bs =
                        (const nmo_behavior_state_t *)sub->state;
                    bool sub_bb =
                        (sub_bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
                    bool sub_script = (sub_bs->flags & CKBEHAVIOR_SCRIPT) != 0;
                    nmo_cli_json_add_str_safe(doc, item, "type",
                        sub_script ? "Script" : sub_bb ? "BB" : "Graph");
                    if (sub_bb && !nmo_guid_is_null(sub_bs->block_guid)) {
                        const char *proto = nmo_bb_registry_get_name(
                            nmo_context_get_bb_registry(c.ctx),
                            sub_bs->block_guid);
                        if (proto) {
                            nmo_cli_json_add_str_safe(doc, item,
                                                      "proto_name", proto);
                        }
                    }
                }
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "sub_behaviors", arr);
        }

        /* Behavior links */
        {
            const nmo_behavior_index_t *bidx =
                nmo_session_get_behavior_index(c.session);
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            const nmo_object_id_t *ids =
                (const nmo_object_id_t *)bs->sub_behavior_links.data;
            for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
                nmo_object_t *link_obj =
                    nmo_object_repository_find_by_id(repo, ids[i]);
                if (!link_obj || !link_obj->state) continue;
                const nmo_behaviorlink_state_t *ls =
                    (const nmo_behaviorlink_state_t *)link_obj->state;
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "id", ids[i]);
                yyjson_mut_obj_add_uint(doc, item, "in_io_id", ls->in_io_id);
                yyjson_mut_obj_add_uint(doc, item, "out_io_id",
                                        ls->out_io_id);
                if (bidx) {
                    const nmo_port_owner_t *sp =
                        nmo_behavior_index_find(bidx, ls->in_io_id);
                    const nmo_port_owner_t *tp =
                        nmo_behavior_index_find(bidx, ls->out_io_id);
                    if (sp) {
                        yyjson_mut_obj_add_uint(doc, item, "source_owner_id",
                                                sp->owner_id);
                    }
                    if (tp) {
                        yyjson_mut_obj_add_uint(doc, item, "target_owner_id",
                                                tp->owner_id);
                    }
                }
                yyjson_mut_obj_add_int(doc, item, "activation_delay",
                                       ls->activation_delay);
                yyjson_mut_arr_add_val(arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "behavior_links", arr);
        }

        if (bs->interface_data) {
            const nmo_interface_body_t *ibody = &bs->interface_data->script.body;
            yyjson_mut_val *comments_arr = yyjson_mut_arr(doc);
            for (size_t ci = 0; ci < ibody->comment_count; ci++) {
                const nmo_interface_comment_t *cm = &ibody->comments[ci];
                yyjson_mut_val *cobj = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, cobj, "index", ci);
                if (cm->text) yyjson_mut_obj_add_str(doc, cobj, "text", cm->text);
                yyjson_mut_obj_add_real(doc, cobj, "left", (double)cm->left);
                yyjson_mut_obj_add_real(doc, cobj, "top", (double)cm->top);
                yyjson_mut_obj_add_real(doc, cobj, "right", (double)cm->right);
                yyjson_mut_obj_add_real(doc, cobj, "bottom", (double)cm->bottom);
                if (cm->style_flags)
                    yyjson_mut_obj_add_uint(doc, cobj, "style_flags", cm->style_flags);
                yyjson_mut_arr_add_val(comments_arr, cobj);
            }
            yyjson_mut_obj_add_val(doc, data, "comments", comments_arr);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.show");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* Text output: BB signature view */
    char heading[256];
    snprintf(heading, sizeof(heading), "Behavior #%u: %s",
             target_id, (name && name[0]) ? name : "(unnamed)");
    nmo_cli_print_heading(c.out, heading, c.colorize);

    /* Flags and identity */
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
    fprintf(c.out, "  Type: %s\n", is_script ? "Script" : is_bb ? "Building Block" : "Graph");
    if (bs->interface_data && (bs->interface_data->script.flags & NMO_INTERFACE_FLAG_FOLDED))
        fprintf(c.out, "  Layout: Folded\n");
    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {

        const char *proto_name = nmo_bb_registry_get_name(nmo_context_get_bb_registry(c.ctx),bs->block_guid);
        if (proto_name) {
            fprintf(c.out, "  Prototype: %s  {%08X-%08X}  v%u\n",
                    proto_name, bs->block_guid.d1, bs->block_guid.d2, bs->block_version);
        } else {
            fprintf(c.out, "  GUID: {%08X-%08X}  Version: %u\n",
                    bs->block_guid.d1, bs->block_guid.d2, bs->block_version);
        }
    }
    if (bs->compatible_class_id > 0) {
        const char *cls = nmo_core_class_name(&c, (nmo_class_id_t)bs->compatible_class_id);
        fprintf(c.out, "  Target Class: %s (#%d)\n",
                cls ? cls : "?", bs->compatible_class_id);
    }

    /* IO Ports */
    if (bs->inputs.count > 0 || bs->outputs.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "IO Ports", c.colorize);
        const nmo_object_id_t *in_ids = (const nmo_object_id_t *)bs->inputs.data;
        for (size_t i = 0; i < bs->inputs.count; i++) {
            fprintf(c.out, "  bIn  %zu: %s\n", i, resolve_name(repo, in_ids[i]));
        }
        const nmo_object_id_t *out_ids = (const nmo_object_id_t *)bs->outputs.data;
        for (size_t i = 0; i < bs->outputs.count; i++) {
            fprintf(c.out, "  bOut %zu: %s\n", i, resolve_name(repo, out_ids[i]));
        }
    }

    /* Input Parameters */
    if (bs->in_parameters.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Input Parameters", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->in_parameters.data;
        for (size_t i = 0; i < bs->in_parameters.count; i++) {
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *pname = p ? nmo_object_get_name(p) : "?";
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tname = resolve_type(c.registry, tg);
            fprintf(c.out, "  pIn  %zu: %-24s  [%s]", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
            /* Show source with shared chain tracing */
            if (p && nmo_object_get_class_id(p) == NMO_CID_PARAMETERIN) {
                const nmo_parameterin_state_t *pin =
                    (const nmo_parameterin_state_t *)nmo_object_get_state(p);
                if (pin && pin->source_id != 0) {
                    if (pin->is_shared) {
                        /* Trace shared chain to find direct source */
                        uint32_t shared_hops = 0;
                        nmo_object_id_t cur_id = pin->source_id;
                        while (shared_hops < 32) {
                            nmo_object_t *cur_obj = nmo_object_repository_find_by_id(repo, cur_id);
                            if (!cur_obj) break;
                            if (nmo_object_get_class_id(cur_obj) != NMO_CID_PARAMETERIN) break;
                            const nmo_parameterin_state_t *cur_pin =
                                (const nmo_parameterin_state_t *)nmo_object_get_state(cur_obj);
                            if (!cur_pin || cur_pin->source_id == 0) break;
                            if (!cur_pin->is_shared) {
                                /* Reached direct source */
                                cur_id = cur_pin->source_id;
                                shared_hops++;
                                break;
                            }
                            cur_id = cur_pin->source_id;
                            shared_hops++;
                        }
                        const char *final_name = resolve_name(repo, cur_id);
                        nmo_object_t *final_obj = nmo_object_repository_find_by_id(repo, cur_id);
                        nmo_guid_t final_tg = get_param_type_guid(final_obj);
                        const char *final_type = resolve_type(c.registry, final_tg);
                        fprintf(c.out, "  <- %s [%s] via %u shared link%s",
                                final_name ? final_name : "?",
                                final_type,
                                shared_hops,
                                shared_hops == 1 ? "" : "s");
                    } else {
                        const char *src = resolve_name(repo, pin->source_id);
                        fprintf(c.out, "  <- %s", src ? src : "?");
                    }
                }
            }
            fprintf(c.out, "\n");
        }
    }

    /* Output Parameters */
    if (bs->out_parameters.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Output Parameters", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->out_parameters.data;
        for (size_t i = 0; i < bs->out_parameters.count; i++) {
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *pname = p ? nmo_object_get_name(p) : "?";
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tname = resolve_type(c.registry, tg);
            fprintf(c.out, "  pOut %zu: %-24s  [%s]", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
            /* Decode value if available */
            if (p && (nmo_object_get_class_id(p) == NMO_CID_PARAMETEROUT ||
                      nmo_object_get_class_id(p) == NMO_CID_PARAMETER)) {
                const nmo_parameter_state_t *ps =
                    (const nmo_parameter_state_t *)nmo_object_get_state(p);
                if (ps && ps->has_state) {
                    char val_buf[256];
                    if (nmo_param_value_to_string(ps, c.registry, c.session,
                                                  val_buf, sizeof(val_buf)) == NMO_OK
                        && val_buf[0] != '\0') {
                        fprintf(c.out, " = %s", val_buf);
                    }
                }
            }
            fprintf(c.out, "\n");
        }
    }

    /* Local Parameters */
    if (bs->local_parameters.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Local Parameters", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->local_parameters.data;
        for (size_t i = 0; i < bs->local_parameters.count; i++) {
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *pname = p ? nmo_object_get_name(p) : "?";
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tname = resolve_type(c.registry, tg);
            fprintf(c.out, "  local %zu: %-24s  [%s]", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
            /* Decode value if available */
            if (p && (nmo_object_get_class_id(p) == NMO_CID_PARAMETERLOCAL ||
                      nmo_object_get_class_id(p) == NMO_CID_PARAMETER)) {
                const nmo_parameter_state_t *lps =
                    (const nmo_parameter_state_t *)nmo_object_get_state(p);
                if (lps && lps->has_state) {
                    char val_buf[256];
                    if (nmo_param_value_to_string(lps, c.registry, c.session,
                                                  val_buf, sizeof(val_buf)) == NMO_OK
                        && val_buf[0] != '\0') {
                        fprintf(c.out, " = %s", val_buf);
                    }
                }
            }
            if (bs->interface_data && bs->interface_data->script.body.has_params) {
                const nmo_interface_param_set_t *ips = &bs->interface_data->script.body.params;
                if (i < ips->local_count) {
                    const nmo_interface_param_t *ip = &ips->locals[i];
                    fprintf(c.out, "  grid=(%d,%d)", ip->h_pos, ip->v_pos);
                    if (ip->style & NMO_INTERFACE_PARAM_STYLE_COLLAPSED)
                        fprintf(c.out, " [collapsed]");
                    else if (ip->style & NMO_INTERFACE_PARAM_STYLE_NAMEVALUE)
                        fprintf(c.out, " [name+value]");
                    else if (ip->style & NMO_INTERFACE_PARAM_STYLE_VALUE)
                        fprintf(c.out, " [value]");
                    else if (ip->style & NMO_INTERFACE_PARAM_STYLE_NAME)
                        fprintf(c.out, " [name]");
                }
            }
            fprintf(c.out, "\n");
        }
    }

    /* Operations */
    if (bs->operations.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Operations", c.colorize);
        const nmo_object_id_t *op_ids = (const nmo_object_id_t *)bs->operations.data;
        for (size_t i = 0; i < bs->operations.count; i++) {
            nmo_object_t *op_obj = nmo_object_repository_find_by_id(repo, op_ids[i]);
            if (!op_obj || !op_obj->state) {
                fprintf(c.out, "  pOp %zu: #%u (missing)\n", i, op_ids[i]);
                continue;
            }
            const nmo_parameteroperation_state_t *op_state =
                (const nmo_parameteroperation_state_t *)op_obj->state;
            const char *op_name = nmo_type_registry_guid_to_name(
                c.registry, op_state->operation_guid);
            /* Resolve in1, in2, out names and types */
            const char *n1 = op_state->has_in1 ? resolve_name(repo, op_state->in1_id) : NULL;
            const char *n2 = op_state->has_in2 ? resolve_name(repo, op_state->in2_id) : NULL;
            const char *no = op_state->has_out ? resolve_name(repo, op_state->out_id) : NULL;
            /* Resolve result type from out parameter */
            const char *out_type = "?";
            if (op_state->has_out) {
                nmo_object_t *out_p = nmo_object_repository_find_by_id(repo, op_state->out_id);
                nmo_guid_t otg = get_param_type_guid(out_p);
                out_type = resolve_type(c.registry, otg);
            }
            /* Display as: [OP] in1 + in2 -> out [Type] */
            fprintf(c.out, "  [%s] ", op_name ? op_name : "?");
            if (n1) fprintf(c.out, "%s", n1);
            if (n1 && n2) fprintf(c.out, " + ");
            if (n2) fprintf(c.out, "%s", n2);
            if (no) fprintf(c.out, " -> %s", no);
            fprintf(c.out, "  [%s]\n", out_type);
        }
    }

    /* Sub-behaviors */
    if (bs->sub_behaviors.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Sub-Behaviors", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->sub_behaviors.data;

        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *sname = sub ? nmo_object_get_name(sub) : NULL;
            /* Resolve BB prototype name */
            const char *proto_name = NULL;
            if (sub && sub->state) {
                const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
                if ((sub_bs->flags & CKBEHAVIOR_BUILDINGBLOCK) && !nmo_guid_is_null(sub_bs->block_guid)) {
                    proto_name = nmo_bb_registry_get_name(nmo_context_get_bb_registry(c.ctx),sub_bs->block_guid);
                }
            }
            if (proto_name) {
                fprintf(c.out, "  [%zu] #%u %s", i, ids[i], proto_name);
                if (sname && sname[0] && strcmp(sname, proto_name) != 0) {
                    fprintf(c.out, " (%s)", sname);
                }
            } else {
                fprintf(c.out, "  [%zu] #%u %s", i, ids[i],
                        (sname && sname[0]) ? sname : "(unnamed)");
            }
            {
                const nmo_interface_behavior_t *isub = find_interface_sub(bs->interface_data, ids[i]);
                if (isub) {
                    if (isub->flags & NMO_INTERFACE_FLAG_FOLDED)
                        fprintf(c.out, " [folded]");
                    if (isub->flags & NMO_INTERFACE_FLAG_HEADER_ONLY)
                        fprintf(c.out, " [header-only]");
                }
            }
            fprintf(c.out, "\n");
        }
    }

    /* Behavior Links */
    if (bs->sub_behavior_links.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Execution Flow", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->sub_behavior_links.data;
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, ids[i]);
            if (!link_obj || !link_obj->state) continue;
            const nmo_behaviorlink_state_t *ls =
                (const nmo_behaviorlink_state_t *)link_obj->state;
            /* in_io_id = source (SDK naming is backwards), out_io_id = target */
            const nmo_behavior_index_t *bidx = nmo_session_get_behavior_index(c.session);
            nmo_object_id_t src_owner = 0, tgt_owner = 0;
            if (bidx) {
                const nmo_port_owner_t *sp = nmo_behavior_index_find(bidx, ls->in_io_id);
                const nmo_port_owner_t *tp = nmo_behavior_index_find(bidx, ls->out_io_id);
                if (sp) src_owner = sp->owner_id;
                if (tp) tgt_owner = tp->owner_id;
            }
            const char *so = (src_owner == 0 || src_owner == target_id) ? name : resolve_name(repo, src_owner);
            const char *to = (tgt_owner == 0 || tgt_owner == target_id) ? name : resolve_name(repo, tgt_owner);
            fprintf(c.out, "  %s.%s -> %s.%s",
                    (so && so[0]) ? so : "?", resolve_name(repo, ls->in_io_id),
                    (to && to[0]) ? to : "?", resolve_name(repo, ls->out_io_id));
            if (ls->activation_delay != 0) {
                fprintf(c.out, "  (delay: %d)", ls->activation_delay);
            }
            fprintf(c.out, "\n");
        }
    }

    /* Data Flow: trace pIn.source_id connections between sub-behaviors.
     * Build a map: param_id → owner_behavior_name, then for each sub-BB's
     * input parameters, show where the data comes from. */
    if (bs->sub_behaviors.count > 0) {
        /* Collect all sub-behavior pOut/pLocal → owner name mapping */
        typedef struct { nmo_object_id_t param_id; const char *owner; const char *param_name; } flow_src_t;
        flow_src_t sources[512];
        size_t src_count = 0;

        /* Add parent's local parameters as sources */
        if (bs->local_parameters.data) {
            const nmo_object_id_t *lids = (const nmo_object_id_t *)bs->local_parameters.data;
            for (size_t i = 0; i < bs->local_parameters.count && src_count < 512; i++) {
                nmo_object_t *p = nmo_object_repository_find_by_id(repo, lids[i]);
                sources[src_count].param_id = lids[i];
                sources[src_count].owner = (name && name[0]) ? name : "(root)";
                sources[src_count].param_name = p ? nmo_object_get_name(p) : "?";
                src_count++;
            }
        }

        /* Add each sub-behavior's pOut as sources */
        const nmo_object_id_t *sub_ids = (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, sub_ids[si]);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
            const char *sname = nmo_object_get_name(sub);
            if (!sname || !sname[0]) sname = "(unnamed)";

            if (sub_bs->out_parameters.data) {
                const nmo_object_id_t *pids = (const nmo_object_id_t *)sub_bs->out_parameters.data;
                for (size_t i = 0; i < sub_bs->out_parameters.count && src_count < 512; i++) {
                    nmo_object_t *p = nmo_object_repository_find_by_id(repo, pids[i]);
                    sources[src_count].param_id = pids[i];
                    sources[src_count].owner = sname;
                    sources[src_count].param_name = p ? nmo_object_get_name(p) : "?";
                    src_count++;
                }
            }
        }

        /* Now trace each sub-behavior's pIn.source_id to find data flow edges */
        size_t flow_count = 0;
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Data Flow", c.colorize);

        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, sub_ids[si]);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sub_bs = (const nmo_behavior_state_t *)sub->state;
            const char *sname = nmo_object_get_name(sub);
            if (!sname || !sname[0]) sname = "(unnamed)";

            if (!sub_bs->in_parameters.data) continue;
            const nmo_object_id_t *pids = (const nmo_object_id_t *)sub_bs->in_parameters.data;
            for (size_t pi = 0; pi < sub_bs->in_parameters.count; pi++) {
                nmo_object_t *pin = nmo_object_repository_find_by_id(repo, pids[pi]);
                if (!pin || !pin->state) continue;
                const nmo_parameterin_state_t *ps = (const nmo_parameterin_state_t *)pin->state;
                if (ps->source_id == 0) continue;

                /* Find source in our map */
                const char *src_owner = NULL;
                const char *src_name = NULL;
                for (size_t s = 0; s < src_count; s++) {
                    if (sources[s].param_id == ps->source_id) {
                        src_owner = sources[s].owner;
                        src_name = sources[s].param_name;
                        break;
                    }
                }

                if (!src_owner) {
                    /* Source not in local scope — might be external */
                    src_owner = "(external)";
                    nmo_object_t *src_obj = nmo_object_repository_find_by_id(repo, ps->source_id);
                    src_name = src_obj ? nmo_object_get_name(src_obj) : "?";
                }

                const char *pin_name = nmo_object_get_name(pin);
                nmo_guid_t tg = get_param_type_guid(pin);
                const char *tname = resolve_type(c.registry, tg);

                fprintf(c.out, "  %s.%s -> %s.%s  [%s]%s\n",
                        src_owner,
                        (src_name && src_name[0]) ? src_name : "?",
                        sname,
                        (pin_name && pin_name[0]) ? pin_name : "?",
                        tname,
                        ps->is_shared ? " (shared)" : "");
                flow_count++;
            }
        }

        if (flow_count == 0) {
            fprintf(c.out, "  (no parameter connections)\n");
        }
    }

    /* Interface: Comments (script body only -- sub-behavior comments belong
     * to the sub-behavior's own behavior show output) */
    if (bs->interface_data && bs->interface_data->script.body.comment_count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Comments", c.colorize);
        const nmo_interface_body_t *body = &bs->interface_data->script.body;
        for (size_t ci = 0; ci < body->comment_count; ci++) {
            const nmo_interface_comment_t *cm = &body->comments[ci];
            fprintf(c.out, "  [%zu] ", ci);
            if (cm->text && cm->text[0]) {
                size_t tlen = strlen(cm->text);
                if (tlen > 60)
                    fprintf(c.out, "\"%.57s...\"", cm->text);
                else
                    fprintf(c.out, "\"%s\"", cm->text);
            } else {
                fprintf(c.out, "(empty)");
            }
            fprintf(c.out, "  rect=(%.0f,%.0f,%.0f,%.0f)", cm->left, cm->top, cm->right, cm->bottom);
            if (cm->style_flags)
                fprintf(c.out, "  flags=0x%X", cm->style_flags);
            fprintf(c.out, "\n");
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

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

/* ---- behavior stats: per-file handler for batch mode ---- */

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

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    const nmo_bb_registry_t *bb_reg = nmo_context_get_bb_registry(ctx);

    size_t total_behaviors = 0, n_scripts = 0, n_graphs = 0, n_bbs = 0;
    size_t n_parameters = 0, n_links = 0, n_operations = 0;
    size_t n_with_interface = 0, n_total_comments = 0;
    size_t n_total_routing_points = 0, n_folded = 0, n_with_snapshot = 0;

    nmo_cli_bb_proto_count_t *protos = NULL;
    size_t proto_count = 0, proto_cap = 0;

    nmo_object_id_t *script_ids = NULL;
    size_t script_id_count = 0, script_id_cap = 0;

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_class_id_t cid = nmo_object_get_class_id(obj);

        if (cid == NMO_CID_PARAMETERIN || cid == NMO_CID_PARAMETEROUT ||
            cid == NMO_CID_PARAMETERLOCAL || cid == NMO_CID_PARAMETER) {
            n_parameters++; continue;
        }
        if (cid == NMO_CID_BEHAVIORLINK) { n_links++; continue; }
        if (cid == NMO_CID_PARAMETEROPERATION) { n_operations++; continue; }
        if (!is_behavior_class(registry, cid)) continue;

        total_behaviors++;
        const nmo_behavior_state_t *bs =
            (const nmo_behavior_state_t *)nmo_object_get_state(obj);
        if (!bs) continue;

        if (bs->interface_data) {
            n_with_interface++;
            const nmo_interface_data_t *id = bs->interface_data;
            n_total_comments += id->script.body.comment_count;
            if (id->script.has_snapshot) n_with_snapshot++;
            if (id->script.flags & NMO_INTERFACE_FLAG_FOLDED) n_folded++;
            for (size_t s = 0; s < id->sub_count; s++) {
                n_total_comments += id->subs[s].body.comment_count;
                if (id->subs[s].flags & NMO_INTERFACE_FLAG_FOLDED) n_folded++;
                for (size_t l = 0; l < id->subs[s].body.link_count; l++)
                    n_total_routing_points += id->subs[s].body.links[l].point_count;
            }
            for (size_t l = 0; l < id->script.body.link_count; l++)
                n_total_routing_points += id->script.body.links[l].point_count;
        }

        if (bs->flags & CKBEHAVIOR_SCRIPT) {
            n_scripts++;
            if (script_id_count == script_id_cap) {
                size_t new_cap = (script_id_cap == 0) ? 16 : (script_id_cap * 2);
                nmo_object_id_t *na = (nmo_object_id_t *)realloc(
                    script_ids, new_cap * sizeof(*script_ids));
                if (na) { script_ids = na; script_id_cap = new_cap; }
            }
            if (script_id_count < script_id_cap)
                script_ids[script_id_count++] = nmo_object_get_id(obj);
        } else if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) {
            n_bbs++;
            if (!nmo_guid_is_null(bs->block_guid)) {
                bool found = false;
                for (size_t j = 0; j < proto_count; j++) {
                    if (protos[j].guid.d1 == bs->block_guid.d1 &&
                        protos[j].guid.d2 == bs->block_guid.d2) {
                        protos[j].count++; found = true; break;
                    }
                }
                if (!found) {
                    if (proto_count == proto_cap) {
                        size_t new_cap = (proto_cap == 0) ? 64 : (proto_cap * 2);
                        nmo_cli_bb_proto_count_t *na =
                            (nmo_cli_bb_proto_count_t *)realloc(
                                protos, new_cap * sizeof(*protos));
                        if (na) { protos = na; proto_cap = new_cap; }
                    }
                    if (proto_count < proto_cap) {
                        protos[proto_count] = (nmo_cli_bb_proto_count_t){
                            .guid = bs->block_guid,
                            .name = nmo_bb_registry_get_name(bb_reg, bs->block_guid),
                            .count = 1,
                        };
                        proto_count++;
                    }
                }
            }
        } else {
            n_graphs++;
        }
    }

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

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    const nmo_bb_registry_t *bb_reg = nmo_context_get_bb_registry(c.ctx);

    /* Counters */
    size_t total_behaviors = 0;
    size_t n_scripts = 0;
    size_t n_graphs = 0;
    size_t n_bbs = 0;
    size_t n_parameters = 0;
    size_t n_links = 0;
    size_t n_operations = 0;
    size_t n_with_interface = 0, n_total_comments = 0;
    size_t n_total_routing_points = 0, n_folded = 0, n_with_snapshot = 0;

    /* BB prototype counting */
    nmo_cli_bb_proto_count_t *protos = NULL;
    size_t proto_count = 0;
    size_t proto_cap = 0;

    /* Script IDs for depth computation */
    nmo_object_id_t *script_ids = NULL;
    size_t script_id_count = 0;
    size_t script_id_cap = 0;

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_class_id_t cid = nmo_object_get_class_id(obj);

        /* Count parameters, links, operations by class */
        if (cid == NMO_CID_PARAMETERIN || cid == NMO_CID_PARAMETEROUT ||
            cid == NMO_CID_PARAMETERLOCAL || cid == NMO_CID_PARAMETER) {
            n_parameters++;
            continue;
        }
        if (cid == NMO_CID_BEHAVIORLINK) {
            n_links++;
            continue;
        }
        if (cid == NMO_CID_PARAMETEROPERATION) {
            n_operations++;
            continue;
        }

        if (!is_behavior_class(c.registry, cid))
            continue;

        total_behaviors++;
        const nmo_behavior_state_t *bs =
            (const nmo_behavior_state_t *)nmo_object_get_state(obj);
        if (!bs) continue;

        if (bs->interface_data) {
            n_with_interface++;
            const nmo_interface_data_t *id = bs->interface_data;
            n_total_comments += id->script.body.comment_count;
            if (id->script.has_snapshot) n_with_snapshot++;
            if (id->script.flags & NMO_INTERFACE_FLAG_FOLDED) n_folded++;
            for (size_t si = 0; si < id->sub_count; si++) {
                n_total_comments += id->subs[si].body.comment_count;
                if (id->subs[si].flags & NMO_INTERFACE_FLAG_FOLDED) n_folded++;
                for (size_t li = 0; li < id->subs[si].body.link_count; li++)
                    n_total_routing_points += id->subs[si].body.links[li].point_count;
            }
            for (size_t li = 0; li < id->script.body.link_count; li++)
                n_total_routing_points += id->script.body.links[li].point_count;
        }

        if (bs->flags & CKBEHAVIOR_SCRIPT) {
            n_scripts++;
            /* Track script ID for depth computation */
            if (script_id_count == script_id_cap) {
                size_t new_cap = (script_id_cap == 0) ? 16 : (script_id_cap * 2);
                nmo_object_id_t *na = (nmo_object_id_t *)realloc(
                    script_ids, new_cap * sizeof(*script_ids));
                if (na) {
                    script_ids = na;
                    script_id_cap = new_cap;
                }
            }
            if (script_id_count < script_id_cap) {
                script_ids[script_id_count++] = nmo_object_get_id(obj);
            }
        } else if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) {
            n_bbs++;
            /* Count BB prototype */
            if (!nmo_guid_is_null(bs->block_guid)) {
                bool found = false;
                for (size_t j = 0; j < proto_count; j++) {
                    if (protos[j].guid.d1 == bs->block_guid.d1 &&
                        protos[j].guid.d2 == bs->block_guid.d2) {
                        protos[j].count++;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if (proto_count == proto_cap) {
                        size_t new_cap = (proto_cap == 0) ? 64 : (proto_cap * 2);
                        nmo_cli_bb_proto_count_t *na =
                            (nmo_cli_bb_proto_count_t *)realloc(
                                protos, new_cap * sizeof(*protos));
                        if (na) {
                            protos = na;
                            proto_cap = new_cap;
                        }
                    }
                    if (proto_count < proto_cap) {
                        const char *pname = nmo_bb_registry_get_name(
                            bb_reg, bs->block_guid);
                        protos[proto_count] = (nmo_cli_bb_proto_count_t){
                            .guid = bs->block_guid,
                            .name = pname,
                            .count = 1,
                        };
                        proto_count++;
                    }
                }
            }
        } else {
            n_graphs++;
        }
    }

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

        /* Top BB prototypes (all, sorted) */
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

int nmo_cmd_behavior_graph(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_object_id_t behavior_id = 0;
    const char *file_path = NULL;
    bool emit_dot = false;
    size_t max_nodes = 0;
    size_t max_edges = 0;
    uint32_t depth = UINT32_MAX;
    int exit_code = NMO_CLI_EXIT_SUCCESS;

    nmo_behavior_graph_t graph = {0};

    nmo_object_id_t *emit_node_ids = NULL;
    size_t *emit_edge_indices = NULL;

    if (!parse_behavior_graph_args(argc, argv, &behavior_id, &file_path,
                                   &emit_dot, &max_nodes, &max_edges, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: nmo behavior graph [--depth N] [--dot] <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    if (!nmo_behavior_graph_build(c.ctx, c.session, behavior_id, depth, &graph)) {
        char detail[256];
        size_t detail_len = nmo_last_error_message_copy(detail, sizeof(detail));
        nmo_error_code_t code = nmo_last_error_code();
        if (detail_len > 0) {
            fprintf(stderr, "Error: %s\n", detail);
        } else {
            fprintf(stderr, "Error: Failed to build behavior graph\n");
        }
        if (code == NMO_ERR_INVALID_ARGUMENT || code == NMO_ERR_NOT_FOUND) {
            exit_code = NMO_CLI_EXIT_ARG_ERROR;
        } else {
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        goto cleanup;
    }

    const nmo_cli_graph_node_t *nodes = graph.nodes;
    size_t node_count = graph.node_count;
    const nmo_cli_graph_edge_t *edges = graph.edges;
    size_t edge_count = graph.edge_count;
    size_t broken_links = graph.broken_links;
    size_t missing_nodes = graph.missing_nodes;

    size_t node_behavior = 0;
    size_t node_parameter = 0;
    size_t node_operation = 0;
    size_t node_io = 0;
    size_t node_unknown = 0;

    for (size_t i = 0; i < node_count; ++i) {
        if (!nodes[i].kind) {
            node_unknown++;
        } else if (strcmp(nodes[i].kind, "behavior") == 0) {
            node_behavior++;
        } else if (strcmp(nodes[i].kind, "parameter") == 0) {
            node_parameter++;
        } else if (strcmp(nodes[i].kind, "operation") == 0) {
            node_operation++;
        } else if (strcmp(nodes[i].kind, "io") == 0) {
            node_io++;
        } else {
            node_unknown++;
        }
    }

    size_t edge_behavior_link = 0;
    size_t edge_io_link = 0;
    size_t edge_param_in = 0;
    size_t edge_param_out = 0;
    size_t edge_param_local = 0;
    size_t edge_param_dest = 0;
    size_t edge_param_source = 0;
    size_t edge_op_in1 = 0;
    size_t edge_op_in2 = 0;
    size_t edge_op_out = 0;

    for (size_t i = 0; i < edge_count; ++i) {
        const char *kind = edges[i].kind ? edges[i].kind : "";
        if (strcmp(kind, "behavior_link") == 0) {
            edge_behavior_link++;
        } else if (strcmp(kind, "io_link") == 0) {
            edge_io_link++;
        } else if (strcmp(kind, "param_in") == 0) {
            edge_param_in++;
        } else if (strcmp(kind, "param_out") == 0) {
            edge_param_out++;
        } else if (strcmp(kind, "param_local") == 0) {
            edge_param_local++;
        } else if (strcmp(kind, "param_dest") == 0) {
            edge_param_dest++;
        } else if (strcmp(kind, "param_source") == 0) {
            edge_param_source++;
        } else if (strcmp(kind, "op_in1") == 0) {
            edge_op_in1++;
        } else if (strcmp(kind, "op_in2") == 0) {
            edge_op_in2++;
        } else if (strcmp(kind, "op_out") == 0) {
            edge_op_out++;
        }
    }

    size_t emit_node_count = node_count;
    bool nodes_truncated = false;
    if (max_nodes > 0 && node_count > max_nodes) {
        emit_node_count = max_nodes;
        nodes_truncated = true;
    }

    if (emit_node_count > 0) {
        emit_node_ids = (nmo_object_id_t *)malloc(emit_node_count * sizeof(*emit_node_ids));
        if (!emit_node_ids) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }
        for (size_t i = 0; i < emit_node_count; ++i) {
            emit_node_ids[i] = nodes[i].id;
        }
    }

    size_t emit_edge_count = 0;
    size_t emit_edge_cap = edge_count;
    if (max_edges > 0 && max_edges < emit_edge_cap) {
        emit_edge_cap = max_edges;
    }
    if (emit_edge_cap > 0) {
        emit_edge_indices = (size_t *)malloc(emit_edge_cap * sizeof(*emit_edge_indices));
        if (!emit_edge_indices) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }
    }

    bool edges_limited = false;
    for (size_t i = 0; i < edge_count; ++i) {
        if (!node_id_in_set(emit_node_ids, emit_node_count, edges[i].from_id) ||
            !node_id_in_set(emit_node_ids, emit_node_count, edges[i].to_id)) {
            continue;
        }
        if (max_edges > 0 && emit_edge_count >= max_edges) {
            edges_limited = true;
            break;
        }
        if (emit_edge_indices) {
            emit_edge_indices[emit_edge_count] = i;
        }
        emit_edge_count++;
    }

    bool edges_truncated = edges_limited || nodes_truncated;

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "behavior_id", behavior_id);

        const char *behavior_name = graph.behavior_name;
        if (behavior_name && behavior_name[0]) {
            nmo_cli_json_add_str_safe(doc, data, "behavior_name", behavior_name);
        }
        nmo_class_id_t behavior_class_id = graph.behavior_class_id;
        const char *behavior_class = graph.behavior_class_name;
        if (behavior_class_id != 0) {
            yyjson_mut_obj_add_uint(doc, data, "behavior_class_id", (uint64_t)behavior_class_id);
        }
        if (behavior_class) {
            yyjson_mut_obj_add_str(doc, data, "behavior_class", behavior_class);
        }

        yyjson_mut_val *counts = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, counts, "nodes_total", (uint64_t)node_count);
        yyjson_mut_obj_add_uint(doc, counts, "edges_total", (uint64_t)edge_count);
        yyjson_mut_obj_add_uint(doc, counts, "broken_links", (uint64_t)broken_links);
        yyjson_mut_obj_add_uint(doc, counts, "missing_nodes", (uint64_t)missing_nodes);

        yyjson_mut_val *nodes_by_kind = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "behavior", (uint64_t)node_behavior);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "parameter", (uint64_t)node_parameter);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "operation", (uint64_t)node_operation);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "io", (uint64_t)node_io);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "unknown", (uint64_t)node_unknown);
        yyjson_mut_obj_add_val(doc, counts, "nodes_by_kind", nodes_by_kind);

        yyjson_mut_val *edges_by_kind = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "behavior_link", (uint64_t)edge_behavior_link);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "io_link", (uint64_t)edge_io_link);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_in", (uint64_t)edge_param_in);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_out", (uint64_t)edge_param_out);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_local", (uint64_t)edge_param_local);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_dest", (uint64_t)edge_param_dest);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_source", (uint64_t)edge_param_source);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "op_in1", (uint64_t)edge_op_in1);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "op_in2", (uint64_t)edge_op_in2);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "op_out", (uint64_t)edge_op_out);
        yyjson_mut_obj_add_val(doc, counts, "edges_by_kind", edges_by_kind);

        yyjson_mut_obj_add_val(doc, data, "counts", counts);

        yyjson_mut_val *graph = yyjson_mut_obj(doc);
        yyjson_mut_val *nodes_arr = yyjson_mut_arr(doc);
        yyjson_mut_val *edges_arr = yyjson_mut_arr(doc);

        for (size_t i = 0; i < emit_node_count; ++i) {
            yyjson_mut_val *node = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, node, "id", nodes[i].id);
            if (nodes[i].kind) {
                yyjson_mut_obj_add_str(doc, node, "kind", nodes[i].kind);
            }
            if (nodes[i].name && nodes[i].name[0]) {
                nmo_cli_json_add_str_safe(doc, node, "name", nodes[i].name);
            }
            if (nodes[i].class_id != 0) {
                yyjson_mut_obj_add_uint(doc, node, "class_id", (uint64_t)nodes[i].class_id);
            }
            if (nodes[i].class_name && nodes[i].class_name[0]) {
                yyjson_mut_obj_add_str(doc, node, "class_name", nodes[i].class_name);
            }
            yyjson_mut_obj_add_uint(doc, node, "depth", (uint64_t)nodes[i].depth);
            if (nodes[i].parent_id != 0) {
                yyjson_mut_obj_add_uint(doc, node, "parent_id", (uint64_t)nodes[i].parent_id);
            }
            yyjson_mut_arr_add_val(nodes_arr, node);
        }

        for (size_t i = 0; i < emit_edge_count; ++i) {
            size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
            const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, edge, "from", edge_ref.from_id);
            yyjson_mut_obj_add_uint(doc, edge, "to", edge_ref.to_id);
            if (edge_ref.kind) {
                yyjson_mut_obj_add_str(doc, edge, "kind", edge_ref.kind);
            }
            if (edge_ref.field_path) {
                yyjson_mut_obj_add_str(doc, edge, "field_path", edge_ref.field_path);
            }
            if (edge_ref.link_id != 0) {
                yyjson_mut_obj_add_uint(doc, edge, "link_id", edge_ref.link_id);
            }
            if (edge_ref.in_io_id != 0) {
                yyjson_mut_obj_add_uint(doc, edge, "in_io_id", edge_ref.in_io_id);
            }
            if (edge_ref.out_io_id != 0) {
                yyjson_mut_obj_add_uint(doc, edge, "out_io_id", edge_ref.out_io_id);
            }
            if (edge_ref.kind && strcmp(edge_ref.kind, "behavior_link") == 0) {
                yyjson_mut_obj_add_int(doc, edge, "activation_delay", edge_ref.activation_delay);
                yyjson_mut_obj_add_int(doc, edge, "initial_activation_delay", edge_ref.initial_activation_delay);
            }
            if (edge_ref.is_shared) {
                yyjson_mut_obj_add_bool(doc, edge, "is_shared", true);
            }
            yyjson_mut_arr_add_val(edges_arr, edge);
        }

        yyjson_mut_obj_add_val(doc, graph, "nodes", nodes_arr);
        yyjson_mut_obj_add_val(doc, graph, "edges", edges_arr);
        if (nodes_truncated || edges_truncated) {
            yyjson_mut_val *truncated = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_bool(doc, truncated, "nodes", nodes_truncated);
            yyjson_mut_obj_add_bool(doc, truncated, "edges", edges_truncated);
            yyjson_mut_obj_add_val(doc, graph, "truncated", truncated);
        }
        yyjson_mut_obj_add_val(doc, data, "graph", graph);

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.graph");
    } else {
        nmo_cli_print_heading(c.out, "Behavior Graph", c.colorize);
        fprintf(c.out, "\n");
        const char *behavior_name = graph.behavior_name;
        const char *behavior_class = graph.behavior_class_name;
        fprintf(c.out, "Behavior %u: %s [%s]\n\n",
                behavior_id,
                (behavior_name && behavior_name[0]) ? behavior_name : "(unnamed)",
                behavior_class ? behavior_class : "?");

        fprintf(c.out, "Nodes: %zu (behavior %zu, parameter %zu, operation %zu, io %zu, unknown %zu)\n",
                node_count, node_behavior, node_parameter, node_operation, node_io, node_unknown);
        fprintf(c.out, "Edges: %zu (behavior links %zu, io links %zu, param %zu, op %zu)\n",
                edge_count,
                edge_behavior_link,
                edge_io_link,
                (edge_param_in + edge_param_out + edge_param_local + edge_param_dest + edge_param_source),
                (edge_op_in1 + edge_op_in2 + edge_op_out));
        if (nodes_truncated) {
            fprintf(c.out, "Note: Nodes truncated to %zu (use --max-nodes 0 to disable)\n", emit_node_count);
        }
        if (edges_truncated) {
            fprintf(c.out, "Note: Edges truncated to %zu (use --max-edges 0 to disable)\n", emit_edge_count);
        }
        if (broken_links > 0) {
            fprintf(c.out, "Broken links: %zu\n", broken_links);
        }
        if (missing_nodes > 0) {
            fprintf(c.out, "Missing objects: %zu\n", missing_nodes);
        }
        fprintf(c.out, "\n");

        nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

        static const nmo_cli_table_col_t node_columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"D", NMO_CLI_ALIGN_RIGHT, 2, 0},
            {"Kind", NMO_CLI_ALIGN_LEFT, 12, 16},
            {"Name", NMO_CLI_ALIGN_LEFT, 22, 50},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
        };
        nmo_cli_table_t node_table;
        nmo_cli_table_init(&node_table, node_columns, sizeof(node_columns) / sizeof(node_columns[0]));

        for (size_t i = 0; i < emit_node_count; ++i) {
            char id_buf[16];
            char depth_buf[8];
            snprintf(id_buf, sizeof(id_buf), "%u", nodes[i].id);
            snprintf(depth_buf, sizeof(depth_buf), "%u", nodes[i].depth);

            /* For operation nodes, resolve the operation name */
            char op_name_buf[80];
            const char *display_name = (nodes[i].name && nodes[i].name[0]) ? nodes[i].name : "-";
            if (nodes[i].kind && strcmp(nodes[i].kind, "operation") == 0) {
                nmo_object_t *op_obj = nmo_object_repository_find_by_id(repo, nodes[i].id);
                if (op_obj && op_obj->state) {
                    const nmo_parameteroperation_state_t *op_state =
                        (const nmo_parameteroperation_state_t *)op_obj->state;
                    const char *op_type = nmo_type_registry_guid_to_name(
                        c.registry, op_state->operation_guid);
                    if (op_type) {
                        if (display_name && strcmp(display_name, "-") != 0) {
                            snprintf(op_name_buf, sizeof(op_name_buf), "%s (%s)",
                                     display_name, op_type);
                        } else {
                            snprintf(op_name_buf, sizeof(op_name_buf), "%s", op_type);
                        }
                        display_name = op_name_buf;
                    }
                }
            }

            const char *cells[] = {
                id_buf,
                depth_buf,
                nodes[i].kind ? nodes[i].kind : "-",
                display_name,
                (nodes[i].class_name && nodes[i].class_name[0]) ? nodes[i].class_name : "-",
            };
            nmo_cli_table_add_row(&node_table, cells, 5);
        }

        nmo_cli_table_print(&node_table, c.out, c.colorize);
        nmo_cli_table_free(&node_table);
        fprintf(c.out, "\n");

        static const nmo_cli_table_col_t edge_columns[] = {
            {"From", NMO_CLI_ALIGN_LEFT, 18, 32},
            {"To", NMO_CLI_ALIGN_LEFT, 18, 32},
            {"Kind", NMO_CLI_ALIGN_LEFT, 14, 18},
            {"Field", NMO_CLI_ALIGN_LEFT, 18, 24},
            {"Link", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"Meta", NMO_CLI_ALIGN_LEFT, 16, 32},
        };
        nmo_cli_table_t edge_table;
        nmo_cli_table_init(&edge_table, edge_columns, sizeof(edge_columns) / sizeof(edge_columns[0]));

        for (size_t i = 0; i < emit_edge_count; ++i) {
            size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
            const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
            const nmo_cli_graph_node_t *from_node =
                find_graph_node(nodes, node_count, edge_ref.from_id);
            const nmo_cli_graph_node_t *to_node =
                find_graph_node(nodes, node_count, edge_ref.to_id);

            char from_buf[64];
            char to_buf[64];
            char link_buf[16];
            char meta_buf[64];

            if (from_node && from_node->name && from_node->name[0]) {
                snprintf(from_buf, sizeof(from_buf), "%u:%s", from_node->id, from_node->name);
            } else {
                snprintf(from_buf, sizeof(from_buf), "%u", edge_ref.from_id);
            }

            if (to_node && to_node->name && to_node->name[0]) {
                snprintf(to_buf, sizeof(to_buf), "%u:%s", to_node->id, to_node->name);
            } else {
                snprintf(to_buf, sizeof(to_buf), "%u", edge_ref.to_id);
            }

            if (edge_ref.link_id != 0) {
                snprintf(link_buf, sizeof(link_buf), "%u", edge_ref.link_id);
            } else {
                snprintf(link_buf, sizeof(link_buf), "-");
            }

            /* in_io = source, out_io = target (Virtools SDK naming) */
            if (edge_ref.kind && strcmp(edge_ref.kind, "behavior_link") == 0) {
                const char *src_io = resolve_name(repo, edge_ref.in_io_id);
                const char *tgt_io = resolve_name(repo, edge_ref.out_io_id);
                if (edge_ref.activation_delay != 0 || edge_ref.initial_activation_delay != 0) {
                    snprintf(meta_buf, sizeof(meta_buf), "%s->%s %d/%d",
                             src_io, tgt_io,
                             edge_ref.activation_delay,
                             edge_ref.initial_activation_delay);
                } else {
                    snprintf(meta_buf, sizeof(meta_buf), "%s->%s",
                             src_io, tgt_io);
                }
            } else if (edge_ref.kind && strcmp(edge_ref.kind, "io_link") == 0) {
                const char *src_io = resolve_name(repo, edge_ref.in_io_id);
                const char *tgt_io = resolve_name(repo, edge_ref.out_io_id);
                snprintf(meta_buf, sizeof(meta_buf), "%s->%s",
                         src_io, tgt_io);
            } else if (edge_ref.kind && (strcmp(edge_ref.kind, "param_local") == 0 ||
                        strcmp(edge_ref.kind, "param_in") == 0 ||
                        strcmp(edge_ref.kind, "param_out") == 0 ||
                        strcmp(edge_ref.kind, "param_source") == 0 ||
                        strcmp(edge_ref.kind, "param_dest") == 0 ||
                        strcmp(edge_ref.kind, "op_in1") == 0 ||
                        strcmp(edge_ref.kind, "op_in2") == 0 ||
                        strcmp(edge_ref.kind, "op_out") == 0)) {
                /* Look up the parameter node to get its type name */
                nmo_object_id_t param_id = edge_ref.from_id;
                if (strcmp(edge_ref.kind, "param_out") == 0 ||
                    strcmp(edge_ref.kind, "op_out") == 0) {
                    param_id = edge_ref.to_id;
                }
                nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
                nmo_guid_t tg = get_param_type_guid(param_obj);
                const char *tname = resolve_type(c.registry, tg);
                if (edge_ref.is_shared) {
                    snprintf(meta_buf, sizeof(meta_buf), "%s (shared)", tname);
                } else {
                    snprintf(meta_buf, sizeof(meta_buf), "%s", tname);
                }
            } else {
                snprintf(meta_buf, sizeof(meta_buf), "-");
            }

            const char *cells[] = {
                from_buf,
                to_buf,
                edge_ref.kind ? edge_ref.kind : "-",
                edge_ref.field_path ? edge_ref.field_path : "-",
                link_buf,
                meta_buf,
            };
            nmo_cli_table_add_row(&edge_table, cells, 6);
        }

        nmo_cli_table_print(&edge_table, c.out, c.colorize);
        nmo_cli_table_free(&edge_table);

        if (emit_dot) {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "DOT Graph", c.colorize);
            fprintf(c.out, "\n");
            /* Look up interface data for the root behavior */
            const nmo_interface_data_t *idata = NULL;
            {
                nmo_object_t *root_beh = nmo_object_repository_find_by_id(repo, behavior_id);
                if (root_beh) {
                    const nmo_behavior_state_t *root_bs =
                        (const nmo_behavior_state_t *)nmo_object_get_state(root_beh);
                    if (root_bs)
                        idata = root_bs->interface_data;
                }
            }

            fprintf(c.out, "digraph behavior_graph {\n");
            if (idata)
                fprintf(c.out, "  graph [layout=neato, overlap=false];\n");
            fprintf(c.out, "  node [shape=box, fontname=\"Courier\", style=filled];\n");
            for (size_t i = 0; i < emit_node_count; ++i) {
                const char *label = (nodes[i].name && nodes[i].name[0]) ? nodes[i].name :
                    (nodes[i].class_name && nodes[i].class_name[0]) ? nodes[i].class_name :
                    (nodes[i].kind ? nodes[i].kind : "node");

                /* Resolve operation type for operation nodes */
                char dot_op_buf[80];
                if (nodes[i].kind && strcmp(nodes[i].kind, "operation") == 0) {
                    nmo_object_t *op_obj = nmo_object_repository_find_by_id(repo, nodes[i].id);
                    if (op_obj && op_obj->state) {
                        const nmo_parameteroperation_state_t *op_state =
                            (const nmo_parameteroperation_state_t *)op_obj->state;
                        const char *op_type = nmo_type_registry_guid_to_name(
                            c.registry, op_state->operation_guid);
                        if (op_type) {
                            snprintf(dot_op_buf, sizeof(dot_op_buf), "%s", op_type);
                            label = dot_op_buf;
                        }
                    }
                }

                /* Color by kind */
                const char *fillcolor = "white";
                if (nodes[i].kind) {
                    if (strcmp(nodes[i].kind, "behavior") == 0) {
                        /* Sub-kind: check behavior flags */
                        nmo_object_t *bobj = nmo_object_repository_find_by_id(repo, nodes[i].id);
                        if (bobj && bobj->state) {
                            const nmo_behavior_state_t *bst =
                                (const nmo_behavior_state_t *)bobj->state;
                            if (bst->flags & CKBEHAVIOR_SCRIPT)
                                fillcolor = "lightgreen";
                            else if (bst->flags & CKBEHAVIOR_BUILDINGBLOCK)
                                fillcolor = "lightblue";
                            else
                                fillcolor = "lightyellow";
                        } else {
                            fillcolor = "lightyellow";
                        }
                    } else if (strcmp(nodes[i].kind, "parameter") == 0) {
                        fillcolor = "lemonchiffon";
                    } else if (strcmp(nodes[i].kind, "operation") == 0) {
                        fillcolor = "lightsalmon";
                    } else if (strcmp(nodes[i].kind, "io") == 0) {
                        fillcolor = "lightgray";
                    }
                }

                /* Override color for script root from interface data */
                char color_hex_buf[8];
                if (idata && idata->script.color != 0 && idata->script.behavior_id == nodes[i].id)
                    fillcolor = interface_color_to_hex(idata->script.color, color_hex_buf, sizeof(color_hex_buf));

                /* Position from interface data */
                float px, py;
                bool has_pos = false;
                if (nodes[i].kind && strcmp(nodes[i].kind, "operation") == 0)
                    has_pos = find_operation_position(idata, nodes[i].id, &px, &py);
                else
                    has_pos = find_interface_position(idata, nodes[i].id, &px, &py);

                /* Write node with optional position */
                fprintf(c.out, "  n%u [label=\"", nodes[i].id);
                dot_write_label(c.out, label);
                fprintf(c.out, "\", fillcolor=\"%s\"", fillcolor);
                if (has_pos)
                    fprintf(c.out, ", pos=\"%.0f,%.0f!\"", px, -py);
                fprintf(c.out, "];\n");
            }
            for (size_t i = 0; i < emit_edge_count; ++i) {
                size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
                const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
                char dot_edge_label[128];

                if (edge_ref.kind && strcmp(edge_ref.kind, "behavior_link") == 0) {
                    /* Show port names */
                    const char *src_io = resolve_name(repo, edge_ref.in_io_id);
                    const char *tgt_io = resolve_name(repo, edge_ref.out_io_id);
                    snprintf(dot_edge_label, sizeof(dot_edge_label), "%s->%s",
                             src_io, tgt_io);
                } else if (edge_ref.kind && (strcmp(edge_ref.kind, "param_local") == 0 ||
                            strcmp(edge_ref.kind, "param_in") == 0 ||
                            strcmp(edge_ref.kind, "param_out") == 0 ||
                            strcmp(edge_ref.kind, "param_source") == 0 ||
                            strcmp(edge_ref.kind, "param_dest") == 0 ||
                            strcmp(edge_ref.kind, "op_in1") == 0 ||
                            strcmp(edge_ref.kind, "op_in2") == 0 ||
                            strcmp(edge_ref.kind, "op_out") == 0)) {
                    /* Show parameter type name */
                    nmo_object_id_t pid = edge_ref.from_id;
                    if (strcmp(edge_ref.kind, "param_out") == 0 ||
                        strcmp(edge_ref.kind, "op_out") == 0) {
                        pid = edge_ref.to_id;
                    }
                    nmo_object_t *pobj = nmo_object_repository_find_by_id(repo, pid);
                    nmo_guid_t ptg = get_param_type_guid(pobj);
                    const char *ptn = resolve_type(c.registry, ptg);
                    snprintf(dot_edge_label, sizeof(dot_edge_label), "%s",
                             ptn);
                } else {
                    snprintf(dot_edge_label, sizeof(dot_edge_label), "%s",
                             edge_ref.kind ? edge_ref.kind : "link");
                }

                const nmo_interface_link_t *ilink = find_interface_link(idata, edge_ref.link_id);

                fprintf(c.out, "  n%u -> n%u [label=\"", edge_ref.from_id, edge_ref.to_id);
                dot_write_label(c.out, dot_edge_label);
                fprintf(c.out, "\"");
                if (ilink && ilink->highlight)
                    fprintf(c.out, ", style=bold, color=red");
                fprintf(c.out, "];\n");
            }
            fprintf(c.out, "}\n");
        }
    }

cleanup:
    free(emit_edge_indices);
    free(emit_node_ids);
    nmo_behavior_graph_free(&graph);
    return nmo_cmd_ctx_done(&c, exit_code);
}

/* ============================================================================
 * behavior dump — dump behavior tree with decoded parameter values
 * ============================================================================ */

/* ============================================================================
 * behavior dump — hierarchical tree view
 * ============================================================================ */

static void dump_behavior_tree(
    FILE *out, nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    nmo_object_id_t beh_id, int depth, bool last_child, uint32_t branch_mask)
{
    if (depth > 16) return;

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, beh_id);
    if (!obj) return;

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) return;

    const char *name = nmo_object_get_name(obj);
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (bs->flags & 0x2) != 0;

    /* Draw tree connectors */
    for (int d = 0; d < depth; d++) {
        if (d == depth - 1) {
            fprintf(out, "%s", last_child ? "└── " : "├── ");
        } else {
            fprintf(out, "%s", (branch_mask & (1u << (unsigned)d)) ? "│   " : "    ");
        }
    }

    /* Node label */
    fprintf(out, "%s [#%u] (%s)",
            (name && name[0]) ? name : "(unnamed)",
            beh_id,
            is_script ? "Script" : is_bb ? "BB" : "Graph");

    /* Compact IO summary */
    if (bs->inputs.count > 0 || bs->outputs.count > 0) {
        fprintf(out, "  io:%zu/%zu", bs->inputs.count, bs->outputs.count);
    }
    if (bs->in_parameters.count > 0) {
        fprintf(out, "  pIn:%zu", bs->in_parameters.count);
    }
    if (bs->out_parameters.count > 0) {
        fprintf(out, "  pOut:%zu", bs->out_parameters.count);
    }
    fprintf(out, "\n");

    /* Show input parameter signatures for BBs (compact, one line) */
    if (is_bb && bs->in_parameters.count > 0 && depth < 8) {
        /* Print tree prefix for continuation line */
        for (int d = 0; d < depth; d++) {
            fprintf(out, "%s", (branch_mask & (1u << (unsigned)d)) ? "│   " : "    ");
        }
        fprintf(out, "%s", (depth > 0) ? (last_child ? "    " : "│   ") : "");
        fprintf(out, "  pIn: ");

        const nmo_object_id_t *pids = (const nmo_object_id_t *)bs->in_parameters.data;
        for (size_t i = 0; i < bs->in_parameters.count && i < 6; i++) {
            if (i > 0) fprintf(out, ", ");
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, pids[i]);
            const char *pn = p ? nmo_object_get_name(p) : "?";
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tn = resolve_type(reg, tg);
            fprintf(out, "%s[%s]", (pn && pn[0]) ? pn : "?", tn);
        }
        if (bs->in_parameters.count > 6) {
            fprintf(out, " ...(+%zu)", bs->in_parameters.count - 6);
        }
        fprintf(out, "\n");
    }

    /* Recurse into sub-behaviors */
    if (bs->sub_behaviors.count > 0) {
        const nmo_object_id_t *sub_ids = (const nmo_object_id_t *)bs->sub_behaviors.data;
        uint32_t next_mask = branch_mask;
        if (depth > 0 && !last_child) {
            next_mask |= (1u << (unsigned)depth);
        }
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            bool is_last = (i == bs->sub_behaviors.count - 1);
            dump_behavior_tree(out, repo, reg, sub_ids[i],
                               depth + 1, is_last, next_mask);
        }
    }
}

static void dump_behavior_tree_json(
    yyjson_mut_doc *doc, yyjson_mut_val *arr,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_bb_registry_t *bb_reg,
    nmo_object_id_t beh_id, int depth)
{
    if (depth > 16) return;

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, beh_id);
    if (!obj) return;

    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) return;

    const char *name = nmo_object_get_name(obj);
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;

    yyjson_mut_val *node = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, node, "id", beh_id);
    yyjson_mut_obj_add_int(doc, node, "depth", depth);
    nmo_cli_json_add_str_safe(doc, node, "name",
        (name && name[0]) ? name : "");
    nmo_cli_json_add_str_safe(doc, node, "type",
        is_script ? "Script" : is_bb ? "BB" : "Graph");
    yyjson_mut_obj_add_uint(doc, node, "input_count",
                            (uint64_t)bs->inputs.count);
    yyjson_mut_obj_add_uint(doc, node, "output_count",
                            (uint64_t)bs->outputs.count);
    yyjson_mut_obj_add_uint(doc, node, "in_param_count",
                            (uint64_t)bs->in_parameters.count);
    yyjson_mut_obj_add_uint(doc, node, "out_param_count",
                            (uint64_t)bs->out_parameters.count);
    yyjson_mut_obj_add_uint(doc, node, "sub_count",
                            (uint64_t)bs->sub_behaviors.count);

    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
        char guid_buf[24];
        snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                 bs->block_guid.d1, bs->block_guid.d2);
        nmo_cli_json_add_str_safe(doc, node, "bb_guid", guid_buf);
        const char *proto = nmo_bb_registry_get_name(bb_reg, bs->block_guid);
        if (proto) {
            nmo_cli_json_add_str_safe(doc, node, "proto_name", proto);
        }
    }

    yyjson_mut_arr_add_val(arr, node);

    /* Recurse into sub-behaviors */
    if (bs->sub_behaviors.count > 0) {
        const nmo_object_id_t *sub_ids =
            (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            dump_behavior_tree_json(doc, arr, repo, reg, bb_reg,
                                    sub_ids[i], depth + 1);
        }
    }
}

int nmo_cmd_behavior_dump(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--all",  "-a", NMO_OPT_FLAG, "Dump all script behaviors as trees"},
        {"--json", "-j", NMO_OPT_FLAG, "JSON output"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool dump_all = vals[0].present && vals[0].val.flag;

    const char *id_str = NULL;
    if (!dump_all) {
        if (r.pos_count < 2) {
            fprintf(stderr, "Usage: nmo behavior dump [--all] [<id>] <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        id_str = r.pos_args[0];
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *tree = yyjson_mut_arr(doc);
        const nmo_bb_registry_t *bb_reg =
            nmo_context_get_bb_registry(c.ctx);

        if (dump_all) {
            size_t total = 0;
            nmo_object_t **all = nmo_object_repository_get_all(repo, &total);
            for (size_t i = 0; i < total; i++) {
                nmo_class_id_t cid = nmo_object_get_class_id(all[i]);
                if (!is_behavior_class(c.registry, cid)) continue;
                const nmo_behavior_state_t *bs =
                    (const nmo_behavior_state_t *)nmo_object_get_state(all[i]);
                if (!bs || !(bs->flags & 0x2)) continue;
                dump_behavior_tree_json(doc, tree, repo, c.registry, bb_reg,
                                        nmo_object_get_id(all[i]), 0);
            }
        } else {
            uint32_t object_id;
            if (!nmo_tool_parse_u32(id_str, &object_id)) {
                fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
                yyjson_mut_doc_free(doc);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
            }
            dump_behavior_tree_json(doc, tree, repo, c.registry, bb_reg,
                                    object_id, 0);
        }

        yyjson_mut_obj_add_val(doc, data, "tree", tree);
        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.dump");
    } else if (dump_all) {
        /* Find scripts (behaviors with CKBEHAVIOR_SCRIPT flag) */
        size_t total = 0;
        nmo_object_t **all = nmo_object_repository_get_all(repo, &total);
        size_t printed = 0;
        for (size_t i = 0; i < total; i++) {
            nmo_class_id_t cid = nmo_object_get_class_id(all[i]);
            if (!is_behavior_class(c.registry, cid)) continue;
            const nmo_behavior_state_t *bs =
                (const nmo_behavior_state_t *)nmo_object_get_state(all[i]);
            if (!bs || !(bs->flags & 0x2)) continue;

            if (printed > 0) fprintf(c.out, "\n");
            dump_behavior_tree(c.out, repo, c.registry,
                               nmo_object_get_id(all[i]), 0, true, 0);
            printed++;
        }
        if (printed == 0) {
            fprintf(c.out, "No script behaviors found.\n");
        }
    } else {
        uint32_t object_id;
        if (!nmo_tool_parse_u32(id_str, &object_id)) {
            fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        dump_behavior_tree(c.out, repo, c.registry, object_id, 0, true, 0);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * behavior find — search behaviors by name/GUID/parameter type
 * ============================================================================ */

static bool guid_str_match(nmo_guid_t guid, const char *pattern) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%08X-%08X", guid.d1, guid.d2);
    for (const char *p = buf; *p; p++) {
        const char *a = p, *b = pattern;
        while (*a && *b) {
            char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
            char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
            if (ca != cb) break;
            a++; b++;
        }
        if (*b == '\0') return true;
    }
    return false;
}

static bool behavior_has_param_type(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_state_t *bs,
    const char *type_pattern)
{
    if (bs->in_parameters.data) {
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->in_parameters.data;
        for (size_t i = 0; i < bs->in_parameters.count; i++) {
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, ids[i]);
            if (!p) continue;
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tn = resolve_type(reg, tg);
            if (tn && nmo_tool_match_wildcard_ci(type_pattern, tn)) return true;
        }
    }
    if (bs->out_parameters.data) {
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->out_parameters.data;
        for (size_t i = 0; i < bs->out_parameters.count; i++) {
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, ids[i]);
            if (!p) continue;
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tn = resolve_type(reg, tg);
            if (tn && nmo_tool_match_wildcard_ci(type_pattern, tn)) return true;
        }
    }
    return false;
}

static bool behavior_has_op_type(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_state_t *bs,
    const char *op_pattern)
{
    if (!bs->operations.data) return false;
    const nmo_object_id_t *ops = (const nmo_object_id_t *)bs->operations.data;
    for (size_t i = 0; i < bs->operations.count; i++) {
        nmo_object_t *op = nmo_object_repository_find_by_id(repo, ops[i]);
        if (!op || !op->state) continue;
        const nmo_parameteroperation_state_t *os =
            (const nmo_parameteroperation_state_t *)op->state;
        const char *on = nmo_type_registry_guid_to_name(reg, os->operation_guid);
        if (on && nmo_tool_match_wildcard_ci(op_pattern, on)) return true;
    }
    return false;
}

int nmo_cmd_behavior_find(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--name",       "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--guid",       "-g", NMO_OPT_STRING, "Filter by BB GUID (substring)"},
        {"--param-type", "-t", NMO_OPT_STRING, "Filter by parameter type name"},
        {"--op-type",    "-o", NMO_OPT_STRING, "Filter by operation type name"},
        {"--scripts",    NULL, NMO_OPT_FLAG,   "Show only scripts"},
        {"--bbs",        NULL, NMO_OPT_FLAG,   "Show only building blocks"},
        {"--json",       "-j", NMO_OPT_FLAG,   "JSON output"},
    };
    enum { OPT_NAME, OPT_GUID, OPT_PTYPE, OPT_OPTYPE, OPT_SCRIPTS, OPT_BBS, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *name_pat  = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    const char *guid_pat  = vals[OPT_GUID].present ? vals[OPT_GUID].val.str : NULL;
    const char *ptype_pat = vals[OPT_PTYPE].present ? vals[OPT_PTYPE].val.str : NULL;
    const char *optype_pat = vals[OPT_OPTYPE].present ? vals[OPT_OPTYPE].val.str : NULL;
    bool only_scripts     = vals[OPT_SCRIPTS].present && vals[OPT_SCRIPTS].val.flag;
    bool only_bbs         = vals[OPT_BBS].present && vals[OPT_BBS].val.flag;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    size_t total = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &total);

    size_t match_count = 0;

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *json_data = NULL;
    yyjson_mut_val *json_results = NULL;
    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        json_data = yyjson_mut_obj(doc);
        json_results = yyjson_mut_arr(doc);
    }

    static const nmo_cli_table_col_t columns[] = {
        {"ID",   NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"TYPE", NMO_CLI_ALIGN_LEFT,  6, 0},
        {"PROTOTYPE", NMO_CLI_ALIGN_LEFT, 28, 0},
        {"NAME", NMO_CLI_ALIGN_LEFT, 28, 50},
    };
    nmo_cli_table_t table;
    if (!c.is_json) {
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
    }

    for (size_t i = 0; i < total; i++) {
        nmo_class_id_t cid = nmo_object_get_class_id(all[i]);
        if (!is_behavior_class(c.registry, cid)) continue;

        const nmo_behavior_state_t *bs =
            (const nmo_behavior_state_t *)nmo_object_get_state(all[i]);
        if (!bs) continue;

        bool is_script = (bs->flags & 0x2) != 0;
        bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;

        if (only_scripts && !is_script) continue;
        if (only_bbs && !is_bb) continue;

        const char *name = nmo_object_get_name(all[i]);
        if (name_pat && (!name || !nmo_tool_match_wildcard_ci(name_pat, name))) continue;

        if (guid_pat) {
            if (nmo_guid_is_null(bs->block_guid) || !guid_str_match(bs->block_guid, guid_pat))
                continue;
        }

        if (ptype_pat && !behavior_has_param_type(repo, c.registry, bs, ptype_pat)) continue;
        if (optype_pat && !behavior_has_op_type(repo, c.registry, bs, optype_pat)) continue;

        if (c.is_json) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id",
                                    nmo_object_get_id(all[i]));
            nmo_cli_json_add_str_safe(doc, item, "name",
                (name && name[0]) ? name : "");
            nmo_cli_json_add_str_safe(doc, item, "type",
                is_script ? "Script" : is_bb ? "BB" : "Graph");
            if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
                char guid_buf[24];
                snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                         bs->block_guid.d1, bs->block_guid.d2);
                nmo_cli_json_add_str_safe(doc, item, "bb_guid", guid_buf);
                const char *proto_name = nmo_bb_registry_get_name(
                    nmo_context_get_bb_registry(c.ctx), bs->block_guid);
                if (proto_name) {
                    nmo_cli_json_add_str_safe(doc, item, "proto_name",
                                              proto_name);
                }
            }
            yyjson_mut_arr_add_val(json_results, item);
        } else {
            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(all[i]));

            /* Resolve BB prototype name */
            char proto_buf[64] = "-";
            if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
                const char *proto_name = nmo_bb_registry_get_name(
                    nmo_context_get_bb_registry(c.ctx), bs->block_guid);
                if (proto_name) {
                    snprintf(proto_buf, sizeof(proto_buf), "%s", proto_name);
                } else {
                    snprintf(proto_buf, sizeof(proto_buf), "{%08X-%08X}",
                             bs->block_guid.d1, bs->block_guid.d2);
                }
            }

            const char *cells[] = {
                id_buf,
                is_script ? "Script" : is_bb ? "BB" : "Graph",
                proto_buf,
                (name && name[0]) ? name : "-",
            };
            nmo_cli_table_add_row(&table, cells, 4);
        }
        match_count++;
    }

    if (c.is_json) {
        yyjson_mut_obj_add_uint(doc, json_data, "match_count",
                                (uint64_t)match_count);
        yyjson_mut_obj_add_val(doc, json_data, "results", json_results);
        nmo_cmd_ctx_json_end(&c, doc, json_data, "behavior.find");
    } else {
        fprintf(c.out, "Found: %zu behavior(s)\n\n", match_count);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * behavior trace — execution path tracing from IO (recursive into sub-graphs)
 * ============================================================================ */

typedef struct {
    nmo_object_id_t source_io; /* in_io_id: where activation comes FROM */
    nmo_object_id_t target_io; /* out_io_id: where activation goes TO */
    int16_t delay;
} trace_link_t;

/* Recursively collect all behavior links from a behavior tree */
static bool collect_links_recursive(
    nmo_object_repository_t *repo,
    nmo_object_id_t beh_id,
    trace_link_t **links, size_t *count, size_t *cap,
    uint32_t depth, uint32_t max_depth)
{
    if (depth > 256) return true;
    nmo_object_t *beh = nmo_object_repository_find_by_id(repo, beh_id);
    if (!beh || !beh->state) return true;
    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)beh->state;

    /* Collect links from this behavior */
    if (bs->sub_behavior_links.data) {
        const nmo_object_id_t *link_ids = (const nmo_object_id_t *)bs->sub_behavior_links.data;
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_t *lo = nmo_object_repository_find_by_id(repo, link_ids[i]);
            if (!lo || !lo->state) continue;
            const nmo_behaviorlink_state_t *ls = (const nmo_behaviorlink_state_t *)lo->state;
            if (*count == *cap) {
                size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
                trace_link_t *nl = (trace_link_t *)realloc(*links, new_cap * sizeof(trace_link_t));
                if (!nl) return false;
                *links = nl;
                *cap = new_cap;
            }
            (*links)[*count] = (trace_link_t){
                .source_io = ls->in_io_id,
                .target_io = ls->out_io_id,
                .delay = ls->activation_delay,
            };
            (*count)++;
        }
    }

    /* Recurse into graph-type sub-behaviors */
    if (depth < max_depth && bs->sub_behaviors.data) {
        const nmo_object_id_t *subs = (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            if (subs[i] == 0) continue;
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, subs[i]);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sbs = (const nmo_behavior_state_t *)sub->state;
            if (sbs->flags & CKBEHAVIOR_BUILDINGBLOCK) continue;
            if (!collect_links_recursive(repo, subs[i], links, count, cap,
                                         depth + 1, max_depth))
                return false;
        }
    }
    return true;
}

int nmo_cmd_behavior_trace(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--from",  NULL, NMO_OPT_STRING, "Start IO name (default: first bIn)"},
        {"--depth", "-d", NMO_OPT_UINT,   "Max trace depth (default: unlimited)"},
        {"--json",  "-j", NMO_OPT_FLAG,   "JSON output"},
    };
    enum { OPT_FROM, OPT_DEPTH, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *from_name = vals[OPT_FROM].present ? vals[OPT_FROM].val.str : NULL;
    uint32_t max_trace_depth = vals[OPT_DEPTH].present ? vals[OPT_DEPTH].val.u : 64;

    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo behavior trace [--from <io>] [--depth N] <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &beh_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_object_t *beh = nmo_object_repository_find_by_id(repo, beh_id);
    if (!beh || !beh->state) {
        fprintf(stderr, "Error: Behavior %u not found\n", beh_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)beh->state;
    const char *beh_name = nmo_object_get_name(beh);

    /* Build link table recursively. IMPORTANT: Virtools SDK naming is backwards:
     * in_io_id = activation SOURCE (GetInBehaviorIO)
     * out_io_id = activation TARGET (GetOutBehaviorIO) */
    trace_link_t *links = NULL;
    size_t link_count = 0, link_cap = 0;
    if (!collect_links_recursive(repo, beh_id, &links, &link_count, &link_cap,
                                 0, UINT32_MAX)) {
        free(links);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (link_count == 0) {
        if (c.is_json) {
            yyjson_mut_doc *doc0 = nmo_cmd_ctx_json_begin(&c);
            yyjson_mut_val *d0 = yyjson_mut_obj(doc0);
            yyjson_mut_obj_add_uint(doc0, d0, "behavior_id", beh_id);
            nmo_cli_json_add_str_safe(doc0, d0, "behavior_name",
                (beh_name && beh_name[0]) ? beh_name : "");
            yyjson_mut_obj_add_uint(doc0, d0, "entry_count", 0);
            yyjson_mut_obj_add_uint(doc0, d0, "link_count", 0);
            yyjson_mut_obj_add_val(doc0, d0, "entries",
                                   yyjson_mut_arr(doc0));
            nmo_cmd_ctx_json_end(&c, doc0, d0, "behavior.trace");
        } else {
            fprintf(c.out, "No behavior links to trace.\n");
        }
        free(links);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* Use behavior_index for O(1) IO owner lookups */
    const nmo_behavior_index_t *beh_index = nmo_session_get_behavior_index(c.session);

    /* Find starting IO */
    nmo_object_id_t start_io = NMO_OBJECT_ID_NONE;
    if (from_name) {
        if (bs->inputs.data) {
            const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->inputs.data;
            for (size_t i = 0; i < bs->inputs.count; i++) {
                const char *ion = resolve_name(repo, ids[i]);
                if (ion && nmo_tool_match_wildcard_ci(from_name, ion)) {
                    start_io = ids[i]; break;
                }
            }
        }
        if (start_io == NMO_OBJECT_ID_NONE) {
            fprintf(stderr, "Error: IO '%s' not found\n", from_name);
            free(links);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    } else {
        if (bs->inputs.data && bs->inputs.count > 0) {
            start_io = ((const nmo_object_id_t *)bs->inputs.data)[0];
        } else {
            fprintf(stderr, "Error: Behavior has no input IOs\n");
            free(links);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    /* Entry points: root behavior's bIn ports that appear as source in any link */
    nmo_object_id_t entry_ios[64];
    size_t entry_count = 0;

    if (bs->inputs.data) {
        const nmo_object_id_t *root_ins = (const nmo_object_id_t *)bs->inputs.data;
        for (size_t i = 0; i < bs->inputs.count && entry_count < 64; i++) {
            for (size_t li = 0; li < link_count; li++) {
                if (links[li].source_io == root_ins[i]) {
                    entry_ios[entry_count++] = root_ins[i];
                    break;
                }
            }
        }
    }

    if (from_name) {
        size_t matched = 0;
        for (size_t e = 0; e < entry_count; e++) {
            const char *ion = resolve_name(repo, entry_ios[e]);
            if (ion && nmo_tool_match_wildcard_ci(from_name, ion)) {
                entry_ios[matched++] = entry_ios[e];
            }
        }
        entry_count = matched;
        if (entry_count == 0) {
            fprintf(stderr, "Error: No entry IO matching '%s'\n", from_name);
            free(links);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    /* DFS with explicit stack, per-entry visited set */
    typedef struct { nmo_object_id_t io; uint32_t depth; } stack_entry_t;
    size_t stack_cap = (link_count > 512) ? link_count * 2 : 1024;
    stack_entry_t *stack = (stack_entry_t *)malloc(stack_cap * sizeof(stack_entry_t));
    nmo_object_id_t *visited = (nmo_object_id_t *)malloc(stack_cap * sizeof(nmo_object_id_t));
    if (!stack || !visited) {
        free(links); free(stack); free(visited);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *json_data = NULL;
    yyjson_mut_val *json_entries = NULL;
    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        json_data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, json_data, "behavior_id", beh_id);
        nmo_cli_json_add_str_safe(doc, json_data, "behavior_name",
            (beh_name && beh_name[0]) ? beh_name : "");
        yyjson_mut_obj_add_uint(doc, json_data, "entry_count",
                                (uint64_t)entry_count);
        yyjson_mut_obj_add_uint(doc, json_data, "link_count",
                                (uint64_t)link_count);
        json_entries = yyjson_mut_arr(doc);
    } else {
        /* Type label for root behavior */
        const char *root_label = "[Graph]";
        if (bs->flags & CKBEHAVIOR_SCRIPT) root_label = "[Script]";
        else if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) root_label = "[BB]";
        fprintf(c.out, "Execution Trace: %s %s [#%u]\n",
                (beh_name && beh_name[0]) ? beh_name : "(unnamed)",
                root_label, beh_id);
        fprintf(c.out, "Entry points: %zu, Links: %zu\n\n",
                entry_count, link_count);
    }

    for (size_t ei = 0; ei < entry_count; ei++) {
        nmo_object_id_t entry = entry_ios[ei];

        /* Resolve entry owner via behavior_index */
        const char *eowner = beh_name;
        nmo_object_id_t entry_owner_id = beh_id;
        if (beh_index) {
            const nmo_port_owner_t *po = nmo_behavior_index_find(beh_index, entry);
            if (po && po->owner_id != beh_id) {
                entry_owner_id = po->owner_id;
                const char *n = resolve_name(repo, po->owner_id);
                if (n && n[0]) eowner = n;
            }
        }

        yyjson_mut_val *json_entry = NULL;
        yyjson_mut_val *json_steps = NULL;
        if (c.is_json) {
            json_entry = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, json_entry, "entry_io_id", entry);
            nmo_cli_json_add_str_safe(doc, json_entry, "entry_io_name",
                                      resolve_name(repo, entry));
            yyjson_mut_obj_add_uint(doc, json_entry, "entry_owner_id",
                                    entry_owner_id);
            nmo_cli_json_add_str_safe(doc, json_entry, "entry_owner_name",
                (eowner && eowner[0]) ? eowner : "");
            json_steps = yyjson_mut_arr(doc);
        } else {
            fprintf(c.out, "%s.%s\n",
                    (eowner && eowner[0]) ? eowner : "?",
                    resolve_name(repo, entry));
        }

        size_t sp = 0, vis_count = 0;
        stack[sp].io = entry;
        stack[sp].depth = 0;
        sp++;
        visited[vis_count++] = entry;

        while (sp > 0) {
            stack_entry_t cur = stack[--sp];
            if (cur.depth > max_trace_depth) continue;

            for (size_t li = 0; li < link_count; li++) {
                if (links[li].source_io != cur.io) continue;

                nmo_object_id_t tgt = links[li].target_io;

                /* Resolve target owner via behavior_index */
                const char *tname = "?";
                nmo_object_id_t tgt_owner = 0;
                if (beh_index) {
                    const nmo_port_owner_t *po = nmo_behavior_index_find(beh_index, tgt);
                    if (po) {
                        tgt_owner = po->owner_id;
                        const char *n = resolve_name(repo, tgt_owner);
                        if (n && n[0]) tname = n;
                    }
                }
                const char *tio = resolve_name(repo, tgt);

                if (c.is_json) {
                    yyjson_mut_val *step = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_uint(doc, step, "depth", cur.depth);
                    yyjson_mut_obj_add_uint(doc, step, "source_io_id",
                                            cur.io);
                    yyjson_mut_obj_add_uint(doc, step, "target_io_id", tgt);
                    nmo_cli_json_add_str_safe(doc, step, "target_io_name",
                        tio ? tio : "");
                    if (tgt_owner != 0) {
                        yyjson_mut_obj_add_uint(doc, step,
                                                "target_owner_id",
                                                tgt_owner);
                        nmo_cli_json_add_str_safe(doc, step,
                            "target_owner_name", tname);
                    }
                    if (links[li].delay != 0) {
                        yyjson_mut_obj_add_int(doc, step, "delay",
                                               links[li].delay);
                    }
                    yyjson_mut_arr_add_val(json_steps, step);
                } else {
                    for (uint32_t dd = 0; dd <= cur.depth; dd++)
                        fprintf(c.out, "  ");
                    /* Resolve type label and prototype name */
                    const char *type_label = "";
                    const char *proto_name = NULL;
                    bool entering_subgraph = false;
                    if (tgt_owner != 0) {
                        nmo_object_t *tgt_obj = nmo_object_repository_find_by_id(repo, tgt_owner);
                        if (tgt_obj && tgt_obj->state) {
                            const nmo_behavior_state_t *tgt_bs =
                                (const nmo_behavior_state_t *)tgt_obj->state;
                            if (tgt_bs->flags & CKBEHAVIOR_SCRIPT)
                                type_label = " [Script]";
                            else if (tgt_bs->flags & CKBEHAVIOR_BUILDINGBLOCK) {
                                type_label = " [BB]";
                                if (!nmo_guid_is_null(tgt_bs->block_guid)) {
                                    proto_name = nmo_bb_registry_get_name(
                                        nmo_context_get_bb_registry(c.ctx),
                                        tgt_bs->block_guid);
                                }
                            } else {
                                type_label = " [Graph]";
                                entering_subgraph = true;
                            }
                        }
                    }
                    if (entering_subgraph)
                        fprintf(c.out, "\xe2\x96\xb6 ");
                    else
                        fprintf(c.out, "\xe2\x86\x92 ");
                    /* Show "name" [Proto] or just "name" */
                    fprintf(c.out, "%s", tname);
                    if (proto_name)
                        fprintf(c.out, " [%s]", proto_name);
                    fprintf(c.out, ".%s%s", tio ? tio : "?", type_label);
                    if (links[li].delay != 0)
                        fprintf(c.out, "  (delay: %d)", links[li].delay);
                    fprintf(c.out, "\n");
                }

                /* Continue through: add target owner's outputs to stack */
                if (tgt_owner != 0) {
                    nmo_object_t *to = nmo_object_repository_find_by_id(repo, tgt_owner);
                    if (to && to->state) {
                        const nmo_behavior_state_t *tbs = (const nmo_behavior_state_t *)to->state;
                        if (tbs->outputs.data) {
                            const nmo_object_id_t *oids = (const nmo_object_id_t *)tbs->outputs.data;
                            for (size_t oi = 0; oi < tbs->outputs.count && sp < stack_cap - 1; oi++) {
                                bool seen = false;
                                for (size_t v = 0; v < vis_count; v++) {
                                    if (visited[v] == oids[oi]) { seen = true; break; }
                                }
                                if (!seen) {
                                    stack[sp].io = oids[oi];
                                    stack[sp].depth = cur.depth + 1;
                                    sp++;
                                    if (vis_count < stack_cap) visited[vis_count++] = oids[oi];
                                }
                            }
                        }
                    }
                }
            }
        }

        if (c.is_json) {
            yyjson_mut_obj_add_val(doc, json_entry, "steps", json_steps);
            yyjson_mut_arr_add_val(json_entries, json_entry);
        } else {
            fprintf(c.out, "\n");
        }
    }

    if (c.is_json) {
        yyjson_mut_obj_add_val(doc, json_data, "entries", json_entries);
        nmo_cmd_ctx_json_end(&c, doc, json_data, "behavior.trace");
    }

    free(stack);
    free(visited);
    free(links);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * behavior interface — Show interface layout data
 * ============================================================================ */

static const char *iface_endpoint_type_name(uint32_t type) {
    switch (type) {
    case NMO_INTERFACE_ENDPOINT_POUT_SHORTCUT: return "pout_shortcut";
    case NMO_INTERFACE_ENDPOINT_PIN:           return "pin";
    case NMO_INTERFACE_ENDPOINT_POUT:          return "pout";
    case NMO_INTERFACE_ENDPOINT_PLOCAL:        return "plocal";
    case NMO_INTERFACE_ENDPOINT_TARGET_PIN:    return "target_pin";
    case NMO_INTERFACE_ENDPOINT_BIN:           return "bin";
    case NMO_INTERFACE_ENDPOINT_BOUT:          return "bout";
    case NMO_INTERFACE_ENDPOINT_START_BIN:     return "start_bin";
    default:                                    return "?";
    }
}

static void iface_print_body_links(FILE *out, const nmo_interface_body_t *body) {
    for (size_t li = 0; li < body->link_count; li++) {
        const nmo_interface_link_t *lk = &body->links[li];
        fprintf(out, "  #%u %s%s  %u:%d:%s -> %u:%d:%s",
                lk->link_id,
                lk->type == 1 ? "behavior" : lk->type == 2 ? "param" : "?",
                lk->highlight ? " hl" : "",
                lk->start.id, lk->start.index, iface_endpoint_type_name(lk->start.type),
                lk->end.id, lk->end.index, iface_endpoint_type_name(lk->end.type));
        if (lk->point_count > 0) {
            fprintf(out, "  pts=%zu [", lk->point_count);
            for (size_t pi = 0; pi < lk->point_count && pi < 4; pi++) {
                if (pi > 0) fprintf(out, ", ");
                fprintf(out, "(%.0f,%.0f)", lk->points[pi * 2], lk->points[pi * 2 + 1]);
            }
            if (lk->point_count > 4) fprintf(out, ", ...");
            fprintf(out, "]");
        }
        fprintf(out, "\n");
    }
}

static void iface_print_body_operations(FILE *out, const nmo_interface_body_t *body) {
    for (size_t oi = 0; oi < body->operation_count; oi++) {
        const nmo_interface_operation_t *op = &body->operations[oi];
        fprintf(out, "  id=%u pos=(%.1f, %.1f)\n", op->id, op->h_pos, op->v_pos);
    }
}

static void iface_print_body_comments(FILE *out, const nmo_interface_body_t *body) {
    for (size_t ci = 0; ci < body->comment_count; ci++) {
        const nmo_interface_comment_t *cm = &body->comments[ci];
        fprintf(out, "  rect=(%.0f,%.0f,%.0f,%.0f)", cm->left, cm->top, cm->right, cm->bottom);
        if (cm->style_flags) fprintf(out, " flags=0x%X", cm->style_flags);
        fprintf(out, "\n    \"%s\"\n", cm->text ? cm->text : "");
    }
}

static const char *iface_param_style_name(uint32_t style) {
    if (style & NMO_INTERFACE_PARAM_STYLE_COLLAPSED) return " [collapsed]";
    if (style & NMO_INTERFACE_PARAM_STYLE_NAMEVALUE) return " [name+value]";
    if (style & NMO_INTERFACE_PARAM_STYLE_VALUE)     return " [value]";
    if (style & NMO_INTERFACE_PARAM_STYLE_NAME)      return " [name]";
    return "";
}

static void iface_print_body_params(FILE *out, const nmo_interface_body_t *body) {
    if (!body->has_params) {
        fprintf(out, "  (not parsed)\n");
        return;
    }
    const nmo_interface_param_set_t *ps = &body->params;
    fprintf(out, "  Local (%zu):", ps->local_count);
    for (size_t pi = 0; pi < ps->local_count; pi++)
        fprintf(out, " (%d,%d)%s", ps->locals[pi].h_pos, ps->locals[pi].v_pos,
                iface_param_style_name(ps->locals[pi].style));
    fprintf(out, "\n  Shared (%zu):", ps->shared_count);
    for (size_t pi = 0; pi < ps->shared_count; pi++) {
        fprintf(out, " (%d,%d)%s", ps->shared[pi].h_pos, ps->shared[pi].v_pos,
                iface_param_style_name(ps->shared[pi].style));
        if (ps->shared[pi].source_id)
            fprintf(out, "->%u", ps->shared[pi].source_id);
    }
    fprintf(out, "\n");
}

static void iface_print_body_graph_io(FILE *out, const nmo_interface_graph_io_t *gio) {
    fprintf(out, "  inward_inputs (%zu):", gio->inward_input_count);
    for (size_t i = 0; i < gio->inward_input_count; i++)
        fprintf(out, " %d", gio->inward_inputs[i]);
    fprintf(out, "\n  outward_inputs (%zu):", gio->outward_input_count);
    for (size_t i = 0; i < gio->outward_input_count; i++)
        fprintf(out, " %d", gio->outward_inputs[i]);
    fprintf(out, "\n  inward_outputs (%zu):", gio->inward_output_count);
    for (size_t i = 0; i < gio->inward_output_count; i++)
        fprintf(out, " %d", gio->inward_outputs[i]);
    fprintf(out, "\n  outward_outputs (%zu):", gio->outward_output_count);
    for (size_t i = 0; i < gio->outward_output_count; i++)
        fprintf(out, " %d", gio->outward_outputs[i]);
    fprintf(out, "\n");
}

static void iface_json_add_endpoint(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key, const nmo_interface_endpoint_t *ep) {
    yyjson_mut_val *eo = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, eo, "id", ep->id);
    yyjson_mut_obj_add_int(doc, eo, "index", ep->index);
    yyjson_mut_obj_add_uint(doc, eo, "type", ep->type);
    nmo_cli_json_add_str_safe(doc, eo, "type_name", iface_endpoint_type_name(ep->type));
    yyjson_mut_obj_add_val(doc, obj, key, eo);
}

static yyjson_mut_val *iface_json_body(yyjson_mut_doc *doc, const nmo_interface_body_t *body) {
    yyjson_mut_val *bo = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, bo, "has_body", body->has_body);
    if (!body->has_body) return bo;

    /* Links */
    {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t li = 0; li < body->link_count; li++) {
            const nmo_interface_link_t *lk = &body->links[li];
            yyjson_mut_val *lo = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, lo, "type", lk->type);
            yyjson_mut_obj_add_bool(doc, lo, "highlight", lk->highlight);
            yyjson_mut_obj_add_uint(doc, lo, "link_id", lk->link_id);
            iface_json_add_endpoint(doc, lo, "start", &lk->start);
            iface_json_add_endpoint(doc, lo, "end", &lk->end);
            yyjson_mut_obj_add_uint(doc, lo, "point_count", (uint64_t)lk->point_count);
            if (lk->point_count > 0) {
                yyjson_mut_val *pts = yyjson_mut_arr(doc);
                for (size_t pi = 0; pi < lk->point_count; pi++) {
                    yyjson_mut_val *pt = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_real(doc, pt, "h", (double)lk->points[pi * 2]);
                    yyjson_mut_obj_add_real(doc, pt, "v", (double)lk->points[pi * 2 + 1]);
                    yyjson_mut_arr_add_val(pts, pt);
                }
                yyjson_mut_obj_add_val(doc, lo, "points", pts);
            }
            yyjson_mut_arr_add_val(arr, lo);
        }
        yyjson_mut_obj_add_val(doc, bo, "links", arr);
    }

    /* Operations */
    {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t oi = 0; oi < body->operation_count; oi++) {
            const nmo_interface_operation_t *op = &body->operations[oi];
            yyjson_mut_val *oo = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, oo, "id", op->id);
            yyjson_mut_obj_add_real(doc, oo, "h_pos", (double)op->h_pos);
            yyjson_mut_obj_add_real(doc, oo, "v_pos", (double)op->v_pos);
            yyjson_mut_arr_add_val(arr, oo);
        }
        yyjson_mut_obj_add_val(doc, bo, "operations", arr);
    }

    /* Comments */
    {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t ci = 0; ci < body->comment_count; ci++) {
            const nmo_interface_comment_t *cm = &body->comments[ci];
            yyjson_mut_val *co = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, co, "left", (double)cm->left);
            yyjson_mut_obj_add_real(doc, co, "top", (double)cm->top);
            yyjson_mut_obj_add_real(doc, co, "right", (double)cm->right);
            yyjson_mut_obj_add_real(doc, co, "bottom", (double)cm->bottom);
            if (cm->text)
                yyjson_mut_obj_add_str(doc, co, "text", cm->text);
            else
                yyjson_mut_obj_add_null(doc, co, "text");
            yyjson_mut_obj_add_uint(doc, co, "style_flags", cm->style_flags);
            yyjson_mut_arr_add_val(arr, co);
        }
        yyjson_mut_obj_add_val(doc, bo, "comments", arr);
    }

    /* Params */
    {
        yyjson_mut_val *po = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, po, "has_params", body->has_params);
        if (body->has_params) {
            const nmo_interface_param_set_t *ps = &body->params;
            yyjson_mut_obj_add_uint(doc, po, "local_count", (uint64_t)ps->local_count);
            yyjson_mut_obj_add_uint(doc, po, "shared_count", (uint64_t)ps->shared_count);
            {
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                for (size_t pi = 0; pi < ps->local_count; pi++) {
                    yyjson_mut_val *p = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_int(doc, p, "h_pos", ps->locals[pi].h_pos);
                    yyjson_mut_obj_add_int(doc, p, "v_pos", ps->locals[pi].v_pos);
                    yyjson_mut_obj_add_uint(doc, p, "style", ps->locals[pi].style);
                    yyjson_mut_arr_add_val(arr, p);
                }
                yyjson_mut_obj_add_val(doc, po, "locals", arr);
            }
            {
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                for (size_t pi = 0; pi < ps->shared_count; pi++) {
                    yyjson_mut_val *p = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_int(doc, p, "h_pos", ps->shared[pi].h_pos);
                    yyjson_mut_obj_add_int(doc, p, "v_pos", ps->shared[pi].v_pos);
                    yyjson_mut_obj_add_uint(doc, p, "style", ps->shared[pi].style);
                    yyjson_mut_obj_add_uint(doc, p, "source_id", ps->shared[pi].source_id);
                    yyjson_mut_arr_add_val(arr, p);
                }
                yyjson_mut_obj_add_val(doc, po, "shared", arr);
            }
        }
        yyjson_mut_obj_add_val(doc, bo, "params", po);
    }

    /* Graph IO */
    if (body->has_graph_io && body->graph_io) {
        const nmo_interface_graph_io_t *gio = body->graph_io;
        yyjson_mut_val *go = yyjson_mut_obj(doc);
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < gio->inward_input_count; i++)
                yyjson_mut_arr_add_int(doc, arr, gio->inward_inputs[i]);
            yyjson_mut_obj_add_val(doc, go, "inward_inputs", arr);
        }
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < gio->outward_input_count; i++)
                yyjson_mut_arr_add_int(doc, arr, gio->outward_inputs[i]);
            yyjson_mut_obj_add_val(doc, go, "outward_inputs", arr);
        }
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < gio->inward_output_count; i++)
                yyjson_mut_arr_add_int(doc, arr, gio->inward_outputs[i]);
            yyjson_mut_obj_add_val(doc, go, "inward_outputs", arr);
        }
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < gio->outward_output_count; i++)
                yyjson_mut_arr_add_int(doc, arr, gio->outward_outputs[i]);
            yyjson_mut_obj_add_val(doc, go, "outward_outputs", arr);
        }
        yyjson_mut_obj_add_val(doc, bo, "graph_io", go);
    }

    /* Section presence flags */
    yyjson_mut_obj_add_bool(doc, bo, "has_links_section", body->has_links_section);
    yyjson_mut_obj_add_bool(doc, bo, "has_operations_section", body->has_operations_section);
    yyjson_mut_obj_add_bool(doc, bo, "has_comments_section", body->has_comments_section);
    yyjson_mut_obj_add_bool(doc, bo, "has_unknown_flag_section", body->has_unknown_flag_section);
    if (body->has_unknown_flag_section)
        yyjson_mut_obj_add_int(doc, bo, "unknown_flag", body->unknown_flag);

    return bo;
}

static void iface_print_body_text(FILE *out, const nmo_interface_body_t *body,
                                  const char *label, bool colorize) {
    if (!body->has_body) {
        fprintf(out, "  (header only)\n");
        return;
    }
    if (body->link_count > 0) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Links (%zu)", label, body->link_count);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_links(out, body);
    }
    if (body->operation_count > 0) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Operations (%zu)", label, body->operation_count);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_operations(out, body);
    }
    if (body->comment_count > 0) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Comments (%zu)", label, body->comment_count);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_comments(out, body);
    }
    if (body->has_params) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Parameters", label);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_params(out, body);
    }
    if (body->has_graph_io && body->graph_io) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Graph IO", label);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_graph_io(out, body->graph_io);
    }
    if (body->has_unknown_flag_section) {
        fprintf(out, "  unknown_flag: %d\n", body->unknown_flag);
    }
}

/* ================================================================
 * Interface edit: shared helpers
 * ================================================================ */

static int iface_edit_open_ctx(nmo_cmd_ctx_t *c, const char *file_path,
                               const nmo_cli_global_opts_t *global)
{
    memset(c, 0, sizeof(*c));
    c->global = global;
    c->is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                  global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    c->file_path = file_path;

    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &c->ctx, &c->session,
                               errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    c->registry = nmo_context_get_type_registry(c->ctx);

    char out_err[128];
    c->out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!c->out) {
        nmo_tool_close_session(c->ctx, c->session);
        c->ctx = NULL;
        c->session = NULL;
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    c->colorize = nmo_cli_should_colorize(global, c->out);
    return 0;
}

static nmo_interface_data_t *iface_edit_get_data(
    nmo_cmd_ctx_t *c, uint32_t target_id,
    nmo_object_t **out_obj)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);
    nmo_object_t *beh = nmo_object_repository_find_by_id(repo, target_id);
    if (!beh) {
        fprintf(stderr, "Error: Object %u not found\n", target_id);
        return NULL;
    }
    if (!is_behavior_class(c->registry, nmo_object_get_class_id(beh))) {
        fprintf(stderr, "Error: Object %u is not a CKBehavior\n", target_id);
        return NULL;
    }
    nmo_behavior_state_t *bs = (nmo_behavior_state_t *)nmo_object_get_state(beh);
    if (!bs || !bs->interface_data) {
        fprintf(stderr, "Error: Behavior %u has no interface data\n", target_id);
        return NULL;
    }
    if (out_obj) *out_obj = beh;
    return bs->interface_data;
}

static int iface_edit_save(nmo_cmd_ctx_t *c, const char *output_path)
{
    nmo_save_options_t save_opts = nmo_save_options_default();
    int save_rc = nmo_save_file(c->session, output_path, &save_opts);
    if (save_rc != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
        return NMO_CLI_EXIT_IO_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

/* ================================================================
 * Interface edit: verb handlers
 * ================================================================ */

static int iface_cmd_set_pos(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 5) {
        fprintf(stderr, "Usage: nmo behavior interface set-pos <id> <beh_id> <h> <v> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id, beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!nmo_tool_parse_u32(r.pos_args[1], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float h = strtof(r.pos_args[2], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = strtof(r.pos_args[3], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[3]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = iface_edit_open_ctx(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    if (idata->script.behavior_id == beh_id) {
        idata->script.h_pos = h;
        idata->script.v_pos = v;
    } else {
        nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, beh_id);
        if (!sub) {
            fprintf(stderr, "Error: Behavior %u not found in interface data\n", beh_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        sub->h_pos = h;
        sub->v_pos = v;
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Moved behavior %u to (%.1f, %.1f)\n", beh_id, (double)h, (double)v);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

static int iface_cmd_fold(int argc, char **argv, const nmo_cli_global_opts_t *global, bool fold) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface %s <id> <beh_id> <file> -o <out>\n",
                fold ? "fold" : "unfold");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id, beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!nmo_tool_parse_u32(r.pos_args[1], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = iface_edit_open_ctx(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t *flags = NULL;
    if (idata->script.behavior_id == beh_id) {
        flags = &idata->script.flags;
    } else {
        nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, beh_id);
        if (!sub) {
            fprintf(stderr, "Error: Behavior %u not found in interface data\n", beh_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        flags = &sub->flags;
    }

    if (fold) {
        *flags |= NMO_INTERFACE_FLAG_FOLDED;
    } else {
        *flags &= ~NMO_INTERFACE_FLAG_FOLDED;
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "%s behavior %u\n", fold ? "Folded" : "Unfolded", beh_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

static int iface_cmd_set_color(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface set-color <id> <color> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *color_str = r.pos_args[1];
    char *endp;
    unsigned long color_val = strtoul(color_str, &endp, 16);
    if (*endp != '\0' || color_val > 0xFFFFFF) {
        fprintf(stderr, "Error: Invalid color '%s' (expected RRGGBB or 0xRRGGBB)\n", color_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = iface_edit_open_ctx(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    idata->script.color = (uint32_t)color_val;

    if (idata->version < 0x14 || idata->sectioned_layout) {
        fprintf(stderr, "Warning: color will not be written "
                "(version 0x%02X%s)\n",
                idata->version,
                idata->sectioned_layout ? ", sectioned layout" : " < 0x14");
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set script color to #%06X\n", (unsigned)color_val);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

static int iface_cmd_add_comment(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",    NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--text",   "-t",    NMO_OPT_STRING, "Comment text"},
        {"--rect",   "-r",    NMO_OPT_STRING, "Rectangle L,T,R,B"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_TEXT, OPT_RECT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_TEXT].present) {
        fprintf(stderr, "Error: --text required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_RECT].present) {
        fprintf(stderr, "Error: --rect required (L,T,R,B)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo behavior interface add-comment <id> [--body <beh_id>] "
                "--text \"...\" --rect L,T,R,B <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float left, top, right, bottom;
    if (sscanf(vals[OPT_RECT].val.str, "%f,%f,%f,%f", &left, &top, &right, &bottom) != 4) {
        fprintf(stderr, "Error: Invalid --rect format '%s', expected L,T,R,B\n",
                vals[OPT_RECT].val.str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = iface_edit_open_ctx(&c, file_path, global);
    if (rc) return rc;

    nmo_object_t *beh_obj = NULL;
    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, &beh_obj);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_arena_t *arena = nmo_object_get_storage_arena(beh_obj);
    size_t idx = 0;
    nmo_status_t st = nmo_interface_body_add_comment(
        body, arena, vals[OPT_TEXT].val.str, left, top, right, bottom, 0, &idx);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Added comment at index %zu to behavior %u\n", idx, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

static int iface_cmd_remove_comment(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",    NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface remove-comment <id> <index> "
                "[--body <beh_id>] <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[1], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = iface_edit_open_ctx(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if ((size_t)index_val >= body->comment_count) {
        fprintf(stderr, "Error: Comment index %u out of range (count=%zu)\n",
                index_val, body->comment_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_status_t st = nmo_interface_body_remove_comment(body, (size_t)index_val);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Removed comment %u from behavior %u\n", index_val, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Interface read-only command (existing) with verb dispatch
 * ================================================================ */

int nmo_cmd_behavior_interface(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Check for edit subcommand verb.
     * The dispatch system passes (argc-1, argv+1) from the group level,
     * so argv[0] = "interface" (the action name) and the user's first
     * positional arg (verb or <id>) is in argv[1].
     * Shift by 1 so the verb name lands in argv[0] — nmo_opt_parse
     * skips argv[0] (the "command name") and starts at argv[1]. */
    if (argc >= 2 && argv[1][0] != '-') {
        const char *verb = argv[1];
        if (strcmp(verb, "set-pos") == 0)
            return iface_cmd_set_pos(argc - 1, argv + 1, global);
        if (strcmp(verb, "fold") == 0)
            return iface_cmd_fold(argc - 1, argv + 1, global, true);
        if (strcmp(verb, "unfold") == 0)
            return iface_cmd_fold(argc - 1, argv + 1, global, false);
        if (strcmp(verb, "set-color") == 0)
            return iface_cmd_set_color(argc - 1, argv + 1, global);
        if (strcmp(verb, "add-comment") == 0)
            return iface_cmd_add_comment(argc - 1, argv + 1, global);
        if (strcmp(verb, "remove-comment") == 0)
            return iface_cmd_remove_comment(argc - 1, argv + 1, global);
    }
    /* Fall through to existing read-only path */

    static const nmo_opt_def_t opts[] = {
        {"--brief", "-b", NMO_OPT_FLAG, "Brief summary output"},
        {"--json",  "-j", NMO_OPT_FLAG, "JSON output"},
    };
    enum { OPT_BRIEF, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool brief = vals[OPT_BRIEF].present && vals[OPT_BRIEF].val.flag;

    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo behavior interface [--brief] [--json] <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_object_t *beh = nmo_object_repository_find_by_id(repo, target_id);
    if (!beh) {
        fprintf(stderr, "Error: Object %u not found\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (!is_behavior_class(c.registry, nmo_object_get_class_id(beh))) {
        fprintf(stderr, "Error: Object %u is not a CKBehavior\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)nmo_object_get_state(beh);
    if (!bs) {
        fprintf(stderr, "Error: No state for behavior %u\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const nmo_interface_data_t *idata = bs->interface_data;
    if (!idata) {
        fprintf(stderr, "Error: Behavior %u has no interface data\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *name = nmo_object_get_name(beh);

    /* --- JSON output --- */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "behavior_id", target_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");
        yyjson_mut_obj_add_uint(doc, data, "version", idata->version);
        yyjson_mut_obj_add_bool(doc, data, "sectioned_layout", idata->sectioned_layout);
        yyjson_mut_obj_add_uint(doc, data, "sub_count", (uint64_t)idata->sub_count);

        /* Script header */
        {
            const nmo_interface_script_header_t *sh = &idata->script;
            yyjson_mut_val *so = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, so, "behavior_id", sh->behavior_id);
            yyjson_mut_obj_add_uint(doc, so, "flags", sh->flags);
            yyjson_mut_obj_add_uint(doc, so, "script_index", sh->script_index);
            yyjson_mut_obj_add_real(doc, so, "h_pos", (double)sh->h_pos);
            yyjson_mut_obj_add_real(doc, so, "v_pos", (double)sh->v_pos);
            yyjson_mut_obj_add_real(doc, so, "h_start_pos", (double)sh->h_start_pos);
            yyjson_mut_obj_add_real(doc, so, "v_start_pos", (double)sh->v_start_pos);
            yyjson_mut_obj_add_real(doc, so, "v_size", (double)sh->v_size);
            yyjson_mut_obj_add_uint(doc, so, "color", sh->color);
            yyjson_mut_obj_add_bool(doc, so, "has_snapshot", sh->has_snapshot);
            if (sh->has_snapshot) {
                yyjson_mut_val *snap = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, snap, "width", sh->snapshot_desc.width);
                yyjson_mut_obj_add_uint(doc, snap, "height", sh->snapshot_desc.height);
                yyjson_mut_obj_add_uint(doc, snap, "size", (uint64_t)sh->snapshot_size);
                yyjson_mut_obj_add_val(doc, so, "snapshot", snap);
            }
            yyjson_mut_obj_add_val(doc, so, "body", iface_json_body(doc, &sh->body));
            yyjson_mut_obj_add_val(doc, data, "script", so);
        }

        /* Sub-behaviors */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t si = 0; si < idata->sub_count; si++) {
                const nmo_interface_behavior_t *sb = &idata->subs[si];
                yyjson_mut_val *so = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, so, "behavior_id", sb->behavior_id);
                yyjson_mut_obj_add_uint(doc, so, "flags", sb->flags);
                yyjson_mut_obj_add_uint(doc, so, "depth", sb->depth);
                yyjson_mut_obj_add_real(doc, so, "h_pos", (double)sb->h_pos);
                yyjson_mut_obj_add_real(doc, so, "v_pos", (double)sb->v_pos);
                yyjson_mut_obj_add_real(doc, so, "h_size", (double)sb->h_size);
                yyjson_mut_obj_add_real(doc, so, "v_size", (double)sb->v_size);
                yyjson_mut_obj_add_real(doc, so, "h_expand_size", (double)sb->h_expand_size);
                yyjson_mut_obj_add_real(doc, so, "v_expand_size", (double)sb->v_expand_size);
                yyjson_mut_obj_add_val(doc, so, "body", iface_json_body(doc, &sb->body));
                yyjson_mut_arr_add_val(arr, so);
            }
            yyjson_mut_obj_add_val(doc, data, "subs", arr);
        }

        /* Extra data */
        {
            const nmo_interface_extra_t *ex = &idata->extra;
            yyjson_mut_val *eo = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_bool(doc, eo, "present", ex->present);
            if (ex->present) {
                yyjson_mut_obj_add_uint(doc, eo, "version", ex->version);
                yyjson_mut_obj_add_uint(doc, eo, "entry_count", (uint64_t)ex->entry_count);
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                for (size_t ei = 0; ei < ex->entry_count; ei++) {
                    const nmo_interface_extra_entry_t *ee = &ex->entries[ei];
                    yyjson_mut_val *eobj = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_uint(doc, eobj, "type", ee->type);
                    yyjson_mut_obj_add_uint(doc, eobj, "id1", ee->id1);
                    if (ee->type == 3)
                        yyjson_mut_obj_add_uint(doc, eobj, "id2", ee->id2);
                    if (ee->type == 4)
                        yyjson_mut_obj_add_int(doc, eobj, "value", ee->value);
                    if (ee->sub_count > 0) {
                        yyjson_mut_val *sub_arr = yyjson_mut_arr(doc);
                        for (size_t si = 0; si < ee->sub_count; si++) {
                            const nmo_interface_extra_sub_t *se = &ee->sub_entries[si];
                            yyjson_mut_val *sobj = yyjson_mut_obj(doc);
                            yyjson_mut_obj_add_int(doc, sobj, "value1", se->value1);
                            yyjson_mut_obj_add_int(doc, sobj, "value2", se->value2);
                            yyjson_mut_obj_add_uint(doc, sobj, "id1", se->id1);
                            yyjson_mut_obj_add_uint(doc, sobj, "id2", se->id2);
                            if (se->data_size > 0)
                                yyjson_mut_obj_add_uint(doc, sobj, "data_size", (uint64_t)se->data_size);
                            yyjson_mut_arr_add_val(sub_arr, sobj);
                        }
                        yyjson_mut_obj_add_val(doc, eobj, "sub_entries", sub_arr);
                    }
                    yyjson_mut_arr_add_val(arr, eobj);
                }
                yyjson_mut_obj_add_val(doc, eo, "entries", arr);
            }
            yyjson_mut_obj_add_val(doc, data, "extra", eo);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.interface");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* --- Brief output --- */
    if (brief) {
        char buf[128];
        char heading[256];
        snprintf(heading, sizeof(heading), "Interface: Behavior #%u %s",
                 target_id, (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_heading(c.out, heading, c.colorize);

        snprintf(buf, sizeof(buf), "0x%02X", idata->version);
        nmo_cli_print_kv(c.out, "Version", buf, 22, c.colorize);

        nmo_cli_print_kv(c.out, "Layout",
                         idata->sectioned_layout ? "sectioned" : "inline",
                         22, c.colorize);

        if (idata->script.color) {
            snprintf(buf, sizeof(buf), "0x%08X", idata->script.color);
            nmo_cli_print_kv(c.out, "Color", buf, 22, c.colorize);
        }

        snprintf(buf, sizeof(buf), "%zu", idata->sub_count);
        nmo_cli_print_kv(c.out, "Sub-behaviors", buf, 22, c.colorize);

        /* Count totals across all bodies */
        size_t total_links = idata->script.body.link_count;
        size_t total_routing = 0;
        size_t total_ops = idata->script.body.operation_count;
        size_t total_comments = idata->script.body.comment_count;
        size_t total_local = 0, total_shared = 0;
        size_t folded_count = 0;

        for (size_t li = 0; li < idata->script.body.link_count; li++)
            total_routing += idata->script.body.links[li].point_count;
        if (idata->script.body.has_params) {
            total_local += idata->script.body.params.local_count;
            total_shared += idata->script.body.params.shared_count;
        }
        if (idata->script.flags & NMO_INTERFACE_FLAG_FOLDED)
            folded_count++;

        for (size_t si = 0; si < idata->sub_count; si++) {
            const nmo_interface_behavior_t *sb = &idata->subs[si];
            total_links += sb->body.link_count;
            total_ops += sb->body.operation_count;
            total_comments += sb->body.comment_count;
            for (size_t li = 0; li < sb->body.link_count; li++)
                total_routing += sb->body.links[li].point_count;
            if (sb->body.has_params) {
                total_local += sb->body.params.local_count;
                total_shared += sb->body.params.shared_count;
            }
            if (sb->flags & NMO_INTERFACE_FLAG_FOLDED)
                folded_count++;
        }

        snprintf(buf, sizeof(buf), "%zu", total_links);
        nmo_cli_print_kv(c.out, "Total links", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", total_routing);
        nmo_cli_print_kv(c.out, "Routing points", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", total_ops);
        nmo_cli_print_kv(c.out, "Operations", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", total_comments);
        nmo_cli_print_kv(c.out, "Comments", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu local + %zu shared", total_local, total_shared);
        nmo_cli_print_kv(c.out, "Params", buf, 22, c.colorize);

        if (idata->script.has_snapshot) {
            snprintf(buf, sizeof(buf), "%ux%u (%zu bytes)",
                     idata->script.snapshot_desc.width,
                     idata->script.snapshot_desc.height,
                     idata->script.snapshot_size);
            nmo_cli_print_kv(c.out, "Snapshot", buf, 22, c.colorize);
        } else {
            nmo_cli_print_kv(c.out, "Snapshot", "(none)", 22, c.colorize);
        }

        if (idata->extra.present) {
            snprintf(buf, sizeof(buf), "v%u, %zu entries",
                     idata->extra.version, idata->extra.entry_count);
        } else {
            snprintf(buf, sizeof(buf), "(none)");
        }
        nmo_cli_print_kv(c.out, "Extra data", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", folded_count);
        nmo_cli_print_kv(c.out, "Folded", buf, 22, c.colorize);

        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* --- Full text output --- */
    {
        char heading[256];
        snprintf(heading, sizeof(heading), "Interface: Behavior #%u %s",
                 target_id, (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_heading(c.out, heading, c.colorize);
    }

    /* Header */
    fprintf(c.out, "  version: 0x%02X  layout: %s\n",
            idata->version,
            idata->sectioned_layout ? "sectioned" : "inline");

    /* Script header */
    {
        const nmo_interface_script_header_t *sh = &idata->script;
        nmo_cli_print_heading(c.out, "Script Header", c.colorize);
        fprintf(c.out, "  behavior_id: %u  flags: 0x%X", sh->behavior_id, sh->flags);
        if (sh->flags & NMO_INTERFACE_FLAG_FOLDED) fprintf(c.out, " [folded]");
        if (sh->flags & NMO_INTERFACE_FLAG_HEADER_ONLY) fprintf(c.out, " [header-only]");
        fprintf(c.out, "\n");
        fprintf(c.out, "  pos: (%.1f, %.1f)  start: (%.1f, %.1f)  v_size: %.1f\n",
                sh->h_pos, sh->v_pos, sh->h_start_pos, sh->v_start_pos, sh->v_size);
        fprintf(c.out, "  script_index: %u\n", sh->script_index);
        if (sh->has_snapshot) {
            fprintf(c.out, "  snapshot: %ux%u (%zu bytes)\n",
                    sh->snapshot_desc.width, sh->snapshot_desc.height, sh->snapshot_size);
        }
        if (sh->color) {
            fprintf(c.out, "  color: 0x%08X\n", sh->color);
        }

        iface_print_body_text(c.out, &sh->body, "Script", c.colorize);
    }

    /* Sub-behaviors */
    if (idata->sub_count > 0) {
        char heading[128];
        snprintf(heading, sizeof(heading), "Sub-behaviors (%zu)", idata->sub_count);
        nmo_cli_print_heading(c.out, heading, c.colorize);
        for (size_t si = 0; si < idata->sub_count; si++) {
            const nmo_interface_behavior_t *sb = &idata->subs[si];
            fprintf(c.out, "  [%zu] id=%u depth=%u flags=0x%X",
                    si, sb->behavior_id, sb->depth, sb->flags);
            if (sb->flags & NMO_INTERFACE_FLAG_FOLDED) fprintf(c.out, " [folded]");
            if (sb->flags & NMO_INTERFACE_FLAG_HEADER_ONLY) fprintf(c.out, " [header-only]");
            fprintf(c.out, "\n");
            fprintf(c.out, "      pos=(%.1f,%.1f) size=(%.1f,%.1f) expand=(%.1f,%.1f)\n",
                    sb->h_pos, sb->v_pos,
                    sb->h_size, sb->v_size,
                    sb->h_expand_size, sb->v_expand_size);

            char label[64];
            snprintf(label, sizeof(label), "Sub[%zu]", si);
            iface_print_body_text(c.out, &sb->body, label, c.colorize);
        }
    }

    /* Extra data */
    if (idata->extra.present) {
        char heading[128];
        snprintf(heading, sizeof(heading), "Extra Data (v%u, %zu entries)",
                 idata->extra.version, idata->extra.entry_count);
        nmo_cli_print_heading(c.out, heading, c.colorize);
        for (size_t ei = 0; ei < idata->extra.entry_count; ei++) {
            const nmo_interface_extra_entry_t *ee = &idata->extra.entries[ei];
            fprintf(c.out, "  [%zu] type=%u id1=%u", ei, ee->type, ee->id1);
            if (ee->type == 3) fprintf(c.out, " id2=%u", ee->id2);
            if (ee->type == 4) fprintf(c.out, " value=%d", ee->value);
            if (ee->sub_count > 0) fprintf(c.out, " sub_entries=%zu", ee->sub_count);
            fprintf(c.out, "\n");
            for (size_t si = 0; si < ee->sub_count; si++) {
                const nmo_interface_extra_sub_t *se = &ee->sub_entries[si];
                fprintf(c.out, "    val1=%d val2=%d id1=%u id2=%u",
                        se->value1, se->value2, se->id1, se->id2);
                if (se->data_size > 0)
                    fprintf(c.out, " data=%zu bytes", se->data_size);
                fprintf(c.out, "\n");
            }
        }
    }

    /* Section presence (only relevant for sectioned layout) */
    if (idata->sectioned_layout) {
        nmo_cli_print_heading(c.out, "Section Presence", c.colorize);
        const nmo_interface_body_t *sb = &idata->script.body;
        fprintf(c.out, "  Script: links=%s ops=%s comments=%s unknown_flag=%s\n",
                sb->has_links_section ? "yes" : "no",
                sb->has_operations_section ? "yes" : "no",
                sb->has_comments_section ? "yes" : "no",
                sb->has_unknown_flag_section ? "yes" : "no");
        for (size_t si = 0; si < idata->sub_count; si++) {
            const nmo_interface_body_t *b = &idata->subs[si].body;
            fprintf(c.out, "  Sub[%zu]: links=%s ops=%s comments=%s unknown_flag=%s\n",
                    si,
                    b->has_links_section ? "yes" : "no",
                    b->has_operations_section ? "yes" : "no",
                    b->has_comments_section ? "yes" : "no",
                    b->has_unknown_flag_section ? "yes" : "no");
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

