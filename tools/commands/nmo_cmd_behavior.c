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
#include "app/nmo_behavior_graph.h"
#include "app/nmo_script_walker.h"
#include "app/nmo_context.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Forward declarations */
static nmo_object_id_t find_io_owner(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent_bs,
    nmo_object_id_t io_id);

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
                                      size_t *out_max_edges)
{
    static const nmo_opt_def_t opts[] = {
        {"--dot",       NULL, NMO_OPT_FLAG, "Emit DOT graph output"},
        {"--max-nodes", NULL, NMO_OPT_UINT, "Max nodes to display"},
        {"--max-edges", NULL, NMO_OPT_UINT, "Max edges to display"},
    };
    nmo_opt_val_t vals[3];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return false;

    if (r.pos_count < 2) return false;

    const char *id_str = r.pos_args[0];
    const char *file_path = r.pos_args[1];

    uint32_t id = 0;
    if (!nmo_tool_parse_u32(id_str, &id) || id == 0) {
        return false;
    }

    if (out_id) *out_id = (nmo_object_id_t)id;
    if (out_file) *out_file = file_path;
    if (out_dot) *out_dot = vals[0].val.flag;
    if (out_max_nodes) *out_max_nodes = vals[1].present ? (size_t)vals[1].val.u : 0;
    if (out_max_edges) *out_max_edges = vals[2].present ? (size_t)vals[2].val.u : 0;
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

int nmo_cmd_behavior_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
                else if (bs->flags & 0x8000) type_str = "BB";
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
    if (cid == NMO_CID_PARAMETEROUT || cid == NMO_CID_PARAMETERLOCAL) {
        const nmo_parameter_state_t *s = (const nmo_parameter_state_t *)obj->state;
        return s->type_guid;
    }
    return (nmo_guid_t){0, 0};
}

int nmo_cmd_behavior_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--raw", NULL, NMO_OPT_FLAG, "Show raw reflection (like object show)"},
    };
    enum { OPT_RAW, OPT_COUNT };
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
        /* For JSON, delegate to object show for now */
        return nmo_cmd_object_show(argc, argv, global);
    }

    /* Text output: BB signature view */
    char heading[256];
    snprintf(heading, sizeof(heading), "Behavior #%u: %s",
             target_id, (name && name[0]) ? name : "(unnamed)");
    nmo_cli_print_heading(c.out, heading, c.colorize);

    /* Flags and identity */
    bool is_bb = (bs->flags & 0x8000) != 0; /* CKBEHAVIOR_BUILDINGBLOCK */
    bool is_script = (bs->flags & 0x2) != 0; /* CKBEHAVIOR_SCRIPT */
    fprintf(c.out, "  Type: %s\n", is_script ? "Script" : is_bb ? "Building Block" : "Graph");
    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
        fprintf(c.out, "  GUID: {%08X-%08X}  Version: %u\n",
                bs->block_guid.d1, bs->block_guid.d2, bs->block_version);
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
            fprintf(c.out, "  pIn  %zu: %-24s  [%s]\n", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
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
            fprintf(c.out, "  pOut %zu: %-24s  [%s]\n", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
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
            fprintf(c.out, "  local %zu: %-24s  [%s]\n", i,
                    (pname && pname[0]) ? pname : "(unnamed)", tname);
        }
    }

    /* Sub-behaviors */
    if (bs->sub_behaviors.count > 0) {
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Sub-Behaviors", c.colorize);
        const nmo_object_id_t *ids = (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, ids[i]);
            const char *sname = sub ? nmo_object_get_name(sub) : "?";
            fprintf(c.out, "  [%zu] #%u %s\n", i, ids[i],
                    (sname && sname[0]) ? sname : "(unnamed)");
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
            nmo_object_id_t src_owner = find_io_owner(repo, bs, ls->in_io_id);
            nmo_object_id_t tgt_owner = find_io_owner(repo, bs, ls->out_io_id);
            const char *so = (src_owner == 0) ? name : resolve_name(repo, src_owner);
            const char *to = (tgt_owner == 0) ? name : resolve_name(repo, tgt_owner);
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

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

typedef struct {
    nmo_class_id_t class_id;
    size_t count;
} nmo_cli_class_count_t;

static int class_count_cmp_desc(const void *a, const void *b) {
    const nmo_cli_class_count_t *aa = (const nmo_cli_class_count_t *)a;
    const nmo_cli_class_count_t *bb = (const nmo_cli_class_count_t *)b;
    if (aa->count != bb->count) {
        return (aa->count < bb->count) ? 1 : -1;
    }
    if (aa->class_id != bb->class_id) {
        return (aa->class_id < bb->class_id) ? -1 : 1;
    }
    return 0;
}

int nmo_cmd_behavior_stats(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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

    nmo_cli_class_count_t *by_class = NULL;
    size_t by_class_count = 0;
    size_t by_class_cap = 0;
    size_t total = 0;

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_class_id_t class_id = nmo_object_get_class_id(obj);
        if (!is_behavior_class(c.registry, class_id)) {
            continue;
        }
        total++;

        bool found = false;
        for (size_t j = 0; j < by_class_count; ++j) {
            if (by_class[j].class_id == class_id) {
                by_class[j].count++;
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }

        if (by_class_count == by_class_cap) {
            size_t new_cap = (by_class_cap == 0) ? 8 : (by_class_cap * 2);
            nmo_cli_class_count_t *new_arr = (nmo_cli_class_count_t *)realloc(by_class, new_cap * sizeof(*by_class));
            if (!new_arr) {
                free(by_class);
                fprintf(stderr, "Error: Out of memory\n");
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }
            by_class = new_arr;
            by_class_cap = new_cap;
        }
        by_class[by_class_count++] = (nmo_cli_class_count_t){.class_id = class_id, .count = 1};
    }

    qsort(by_class, by_class_count, sizeof(*by_class), class_count_cmp_desc);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total", (uint64_t)total);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < by_class_count; ++i) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "class_id", (uint64_t)by_class[i].class_id);
            yyjson_mut_obj_add_uint(doc, item, "count", (uint64_t)by_class[i].count);

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, by_class[i].class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "by_class", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.stats");
    } else {
        nmo_cli_print_heading(c.out, "Behavior Statistics", c.colorize);
        fprintf(c.out, "\n");

        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", total);
        nmo_cli_print_kv(c.out, "Total", buf, 16, c.colorize);
        fprintf(c.out, "\n");

        static const nmo_cli_table_col_t columns[] = {
            {"Class ID", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 32},
            {"Count", NMO_CLI_ALIGN_RIGHT, 5, 0},
        };
        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < by_class_count; ++i) {
            char class_id_buf[16];
            char count_buf[16];
            snprintf(class_id_buf, sizeof(class_id_buf), "%u", (unsigned)by_class[i].class_id);
            snprintf(count_buf, sizeof(count_buf), "%zu", by_class[i].count);

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, by_class[i].class_id);
            const char *cells[] = {
                class_id_buf,
                class_name ? class_name : "-",
                count_buf,
            };
            nmo_cli_table_add_row(&table, cells, 3);
        }

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    free(by_class);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_graph(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_object_id_t behavior_id = 0;
    const char *file_path = NULL;
    bool emit_dot = false;
    size_t max_nodes = 0;
    size_t max_edges = 0;
    int exit_code = NMO_CLI_EXIT_SUCCESS;

    nmo_behavior_graph_t graph = {0};

    nmo_object_id_t *emit_node_ids = NULL;
    size_t *emit_edge_indices = NULL;

    if (!parse_behavior_graph_args(argc, argv, &behavior_id, &file_path,
                                   &emit_dot, &max_nodes, &max_edges)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: nmo behavior graph <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    if (!nmo_behavior_graph_build(c.ctx, c.session, behavior_id, &graph)) {
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

        static const nmo_cli_table_col_t node_columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"Kind", NMO_CLI_ALIGN_LEFT, 12, 16},
            {"Name", NMO_CLI_ALIGN_LEFT, 22, 50},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
        };
        nmo_cli_table_t node_table;
        nmo_cli_table_init(&node_table, node_columns, sizeof(node_columns) / sizeof(node_columns[0]));

        for (size_t i = 0; i < emit_node_count; ++i) {
            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nodes[i].id);
            const char *cells[] = {
                id_buf,
                nodes[i].kind ? nodes[i].kind : "-",
                (nodes[i].name && nodes[i].name[0]) ? nodes[i].name : "-",
                (nodes[i].class_name && nodes[i].class_name[0]) ? nodes[i].class_name : "-",
            };
            nmo_cli_table_add_row(&node_table, cells, 4);
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
                snprintf(meta_buf, sizeof(meta_buf), "io %u->%u %d/%d",
                         edge_ref.in_io_id,
                         edge_ref.out_io_id,
                         edge_ref.activation_delay,
                         edge_ref.initial_activation_delay);
            } else if (edge_ref.kind && strcmp(edge_ref.kind, "io_link") == 0) {
                snprintf(meta_buf, sizeof(meta_buf), "io %u->%u",
                         edge_ref.in_io_id,
                         edge_ref.out_io_id);
            } else if (edge_ref.kind && strcmp(edge_ref.kind, "param_source") == 0 && edge_ref.is_shared) {
                snprintf(meta_buf, sizeof(meta_buf), "shared");
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
            fprintf(c.out, "digraph behavior_graph {\n");
            fprintf(c.out, "  node [shape=box, fontname=\"Courier\"];\n");
            for (size_t i = 0; i < emit_node_count; ++i) {
                const char *label = (nodes[i].name && nodes[i].name[0]) ? nodes[i].name :
                    (nodes[i].class_name && nodes[i].class_name[0]) ? nodes[i].class_name :
                    (nodes[i].kind ? nodes[i].kind : "node");
                fprintf(c.out, "  n%u [label=\"", nodes[i].id);
                dot_write_label(c.out, label);
                fprintf(c.out, "\"];\n");
            }
            for (size_t i = 0; i < emit_edge_count; ++i) {
                size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
                const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
                const char *edge_label = edge_ref.kind ? edge_ref.kind : "link";
                fprintf(c.out, "  n%u -> n%u [label=\"", edge_ref.from_id, edge_ref.to_id);
                dot_write_label(c.out, edge_label);
                fprintf(c.out, "\"];\n");
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
    bool is_bb = (bs->flags & 0x8000) != 0;
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

int nmo_cmd_behavior_dump(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--all", "-a", NMO_OPT_FLAG, "Dump all script behaviors as trees"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

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

    if (dump_all) {
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

int nmo_cmd_behavior_find(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--name",       "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--guid",       "-g", NMO_OPT_STRING, "Filter by BB GUID (substring)"},
        {"--param-type", "-t", NMO_OPT_STRING, "Filter by parameter type name"},
        {"--scripts",    NULL, NMO_OPT_FLAG,   "Show only scripts"},
        {"--bbs",        NULL, NMO_OPT_FLAG,   "Show only building blocks"},
    };
    enum { OPT_NAME, OPT_GUID, OPT_PTYPE, OPT_SCRIPTS, OPT_BBS, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *name_pat  = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    const char *guid_pat  = vals[OPT_GUID].present ? vals[OPT_GUID].val.str : NULL;
    const char *ptype_pat = vals[OPT_PTYPE].present ? vals[OPT_PTYPE].val.str : NULL;
    bool only_scripts     = vals[OPT_SCRIPTS].present && vals[OPT_SCRIPTS].val.flag;
    bool only_bbs         = vals[OPT_BBS].present && vals[OPT_BBS].val.flag;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    size_t total = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &total);

    size_t match_count = 0;

    static const nmo_cli_table_col_t columns[] = {
        {"ID",   NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"TYPE", NMO_CLI_ALIGN_LEFT,  6, 0},
        {"GUID", NMO_CLI_ALIGN_LEFT, 19, 0},
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
        bool is_bb = (bs->flags & 0x8000) != 0;

        if (only_scripts && !is_script) continue;
        if (only_bbs && !is_bb) continue;

        const char *name = nmo_object_get_name(all[i]);
        if (name_pat && (!name || !nmo_tool_match_wildcard_ci(name_pat, name))) continue;

        if (guid_pat) {
            if (nmo_guid_is_null(bs->block_guid) || !guid_str_match(bs->block_guid, guid_pat))
                continue;
        }

        if (ptype_pat && !behavior_has_param_type(repo, c.registry, bs, ptype_pat)) continue;

        if (!c.is_json) {
            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(all[i]));

            char guid_buf[24] = "-";
            if (!nmo_guid_is_null(bs->block_guid)) {
                snprintf(guid_buf, sizeof(guid_buf), "{%08X-%08X}",
                         bs->block_guid.d1, bs->block_guid.d2);
            }

            const char *cells[] = {
                id_buf,
                is_script ? "Script" : is_bb ? "BB" : "Graph",
                guid_buf,
                (name && name[0]) ? name : "-",
            };
            nmo_cli_table_add_row(&table, cells, 4);
        }
        match_count++;
    }

    if (!c.is_json) {
        fprintf(c.out, "Found: %zu behavior(s)\n\n", match_count);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * behavior trace — execution path tracing from IO
 * ============================================================================ */

static nmo_object_id_t find_io_owner(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *parent_bs,
    nmo_object_id_t io_id)
{
    if (parent_bs->inputs.data) {
        const nmo_object_id_t *ids = (const nmo_object_id_t *)parent_bs->inputs.data;
        for (size_t i = 0; i < parent_bs->inputs.count; i++)
            if (ids[i] == io_id) return 0;
    }
    if (parent_bs->outputs.data) {
        const nmo_object_id_t *ids = (const nmo_object_id_t *)parent_bs->outputs.data;
        for (size_t i = 0; i < parent_bs->outputs.count; i++)
            if (ids[i] == io_id) return 0;
    }
    if (parent_bs->sub_behaviors.data) {
        const nmo_object_id_t *subs = (const nmo_object_id_t *)parent_bs->sub_behaviors.data;
        for (size_t si = 0; si < parent_bs->sub_behaviors.count; si++) {
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, subs[si]);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sbs = (const nmo_behavior_state_t *)sub->state;
            if (sbs->inputs.data) {
                const nmo_object_id_t *ids = (const nmo_object_id_t *)sbs->inputs.data;
                for (size_t i = 0; i < sbs->inputs.count; i++)
                    if (ids[i] == io_id) return subs[si];
            }
            if (sbs->outputs.data) {
                const nmo_object_id_t *ids = (const nmo_object_id_t *)sbs->outputs.data;
                for (size_t i = 0; i < sbs->outputs.count; i++)
                    if (ids[i] == io_id) return subs[si];
            }
        }
    }
    return NMO_OBJECT_ID_NONE;
}

int nmo_cmd_behavior_trace(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--from", NULL, NMO_OPT_STRING, "Start IO name (default: first bIn)"},
    };
    enum { OPT_FROM, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *from_name = vals[OPT_FROM].present ? vals[OPT_FROM].val.str : NULL;

    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo behavior trace [--from <io>] <id> <file>\n");
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
    if (!bs->sub_behavior_links.data || bs->sub_behavior_links.count == 0) {
        fprintf(c.out, "No behavior links to trace.\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    const char *beh_name = nmo_object_get_name(beh);

    /* Build link table. IMPORTANT: Virtools SDK naming is backwards:
     * in_io_id = activation SOURCE (GetInBehaviorIO)
     * out_io_id = activation TARGET (GetOutBehaviorIO)
     * Direction: source_io → target_io */
    typedef struct {
        nmo_object_id_t source_io; /* in_io_id: where activation comes FROM */
        nmo_object_id_t target_io; /* out_io_id: where activation goes TO */
        int16_t delay;
    } trace_link_t;
    trace_link_t *links = (trace_link_t *)malloc(bs->sub_behavior_links.count * sizeof(trace_link_t));
    if (!links) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    size_t link_count = 0;

    const nmo_object_id_t *link_ids = (const nmo_object_id_t *)bs->sub_behavior_links.data;
    for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
        nmo_object_t *lo = nmo_object_repository_find_by_id(repo, link_ids[i]);
        if (!lo || !lo->state) continue;
        const nmo_behaviorlink_state_t *ls = (const nmo_behaviorlink_state_t *)lo->state;
        links[link_count].source_io = ls->in_io_id;  /* source */
        links[link_count].target_io = ls->out_io_id;  /* target */
        links[link_count].delay = ls->activation_delay;
        link_count++;
    }

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

    /* Display execution chains: for each link, show the complete path with
     * BB ownership context. Use BFS from all entry IOs (those that appear
     * as out_io but are not themselves targets of any link). */

    /* Entry points: the root behavior's bIn ports that appear as source_io
     * in any link. These are the activation entry points of the script. */
    nmo_object_id_t entry_ios[64];
    size_t entry_count = 0;

    if (bs->inputs.data) {
        const nmo_object_id_t *root_ins = (const nmo_object_id_t *)bs->inputs.data;
        for (size_t i = 0; i < bs->inputs.count && entry_count < 64; i++) {
            /* Check if this root bIn is a source in any link */
            for (size_t li = 0; li < link_count; li++) {
                if (links[li].source_io == root_ins[i]) {
                    entry_ios[entry_count++] = root_ins[i];
                    break;
                }
            }
        }
    }

    /* If --from specified, filter to matching entry by IO name */
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

    fprintf(c.out, "Execution Trace: %s [#%u]\n",
            (beh_name && beh_name[0]) ? beh_name : "(unnamed)", beh_id);
    fprintf(c.out, "Entry points: %zu, Links: %zu\n\n", entry_count, link_count);

    /* Iterative DFS with explicit stack — per-entry visited set for
     * independent chain tracing with full depth display. */
    typedef struct { nmo_object_id_t io; int depth; } stack_entry_t;
    stack_entry_t *stack = (stack_entry_t *)malloc(512 * sizeof(stack_entry_t));
    nmo_object_id_t *visited = (nmo_object_id_t *)malloc(512 * sizeof(nmo_object_id_t));
    if (!stack || !visited) {
        free(links); free(stack); free(visited);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    for (size_t ei = 0; ei < entry_count; ei++) {
        nmo_object_id_t entry = entry_ios[ei];
        nmo_object_id_t entry_owner = find_io_owner(repo, bs, entry);
        const char *eowner = (entry_owner == 0) ? beh_name : resolve_name(repo, entry_owner);

        fprintf(c.out, "%s.%s\n",
                (eowner && eowner[0]) ? eowner : "?",
                resolve_name(repo, entry));

        /* Per-entry visited set */
        size_t sp = 0, vis_count = 0;
        stack[sp].io = entry;
        stack[sp].depth = 0;
        sp++;
        visited[vis_count++] = entry;

        while (sp > 0) {
            stack_entry_t cur = stack[--sp];
            int d = cur.depth;
            if (d > 12) continue;

            for (size_t li = 0; li < link_count; li++) {
                if (links[li].source_io != cur.io) continue;

                nmo_object_id_t tgt = links[li].target_io;
                nmo_object_id_t tgt_owner = find_io_owner(repo, bs, tgt);
                const char *tname = (tgt_owner == 0) ? beh_name : resolve_name(repo, tgt_owner);
                const char *tio = resolve_name(repo, tgt);

                for (int dd = 0; dd <= d; dd++) fprintf(c.out, "  ");
                fprintf(c.out, "→ %s.%s",
                        (tname && tname[0]) ? tname : "?", tio);
                if (links[li].delay != 0)
                    fprintf(c.out, "  (delay: %d)", links[li].delay);
                fprintf(c.out, "\n");

                if (tgt_owner != 0 && tgt_owner != NMO_OBJECT_ID_NONE) {
                    nmo_object_t *to = nmo_object_repository_find_by_id(repo, tgt_owner);
                    if (to && to->state) {
                        const nmo_behavior_state_t *tbs = (const nmo_behavior_state_t *)to->state;
                        if (tbs->outputs.data) {
                            const nmo_object_id_t *oids = (const nmo_object_id_t *)tbs->outputs.data;
                            for (size_t oi = 0; oi < tbs->outputs.count && sp < 510; oi++) {
                                bool seen = false;
                                for (size_t v = 0; v < vis_count; v++) {
                                    if (visited[v] == oids[oi]) { seen = true; break; }
                                }
                                if (!seen) {
                                    stack[sp].io = oids[oi];
                                    stack[sp].depth = d + 1;
                                    sp++;
                                    if (vis_count < 512) visited[vis_count++] = oids[oi];
                                }
                            }
                        }
                    }
                }
            }
        }

        fprintf(c.out, "\n");
    }

    free(stack);
    free(visited);
    free(links);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

