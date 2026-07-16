/**
 * @file nmo_cmd_behavior_search.c
 * @brief CLI behavior find and trace command implementations
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "behavior/nmo_behavior_analyze.h"
#include "runtime/nmo_context.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "behavior/nmo_behavior_registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * behavior find -- search behaviors by name/GUID/parameter type
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
        for (size_t i = 0; i < bs->in_parameters.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->in_parameters, i);
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, id);
            if (!p) continue;
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tn = resolve_type(reg, tg);
            if (tn && nmo_tool_match_wildcard_ci(type_pattern, tn)) return true;
        }
    }
    if (bs->out_parameters.data) {
        for (size_t i = 0; i < bs->out_parameters.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->out_parameters, i);
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, id);
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
    for (size_t i = 0; i < bs->operations.count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->operations, i);
        nmo_object_t *op = nmo_object_repository_find_by_id(repo, id);
        if (!op || !op->state) continue;
        const nmo_parameteroperation_state_t *os =
            (const nmo_parameteroperation_state_t *)op->state;
        const char *on = nmo_type_registry_guid_to_name(reg, os->operation_guid);
        if (on && nmo_tool_match_wildcard_ci(op_pattern, on)) return true;
    }
    return false;
}

typedef struct behavior_find_data {
    nmo_object_repository_t *repo;
    const char *name_pat;
    const char *guid_pat;
    const char *ptype_pat;
    const char *optype_pat;
    bool only_scripts;
    bool only_bbs;
    yyjson_mut_doc *doc;
    yyjson_mut_val *json_results;
    nmo_cli_table_t *table;
    size_t match_count;
} behavior_find_data_t;

static int behavior_find_object(size_t index, nmo_object_t *obj,
                                const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;

    behavior_find_data_t *data = (behavior_find_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_behavior_class(c->registry, cid)) {
        return 0;
    }

    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) {
        return 0;
    }

    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;

    if (data->only_scripts && !is_script) return 0;
    if (data->only_bbs && !is_bb) return 0;

    const char *name = nmo_object_get_name(obj);
    if (data->name_pat &&
        (!name || !nmo_tool_match_wildcard_ci(data->name_pat, name))) {
        return 0;
    }

    if (data->guid_pat) {
        if (nmo_guid_is_null(bs->block_guid) ||
            !guid_str_match(bs->block_guid, data->guid_pat)) {
            return 0;
        }
    }

    if (data->ptype_pat &&
        !behavior_has_param_type(data->repo, c->registry, bs, data->ptype_pat)) {
        return 0;
    }
    if (data->optype_pat &&
        !behavior_has_op_type(data->repo, c->registry, bs, data->optype_pat)) {
        return 0;
    }

    if (data->doc && data->json_results) {
        yyjson_mut_val *item = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, item, "id", nmo_object_get_id(obj));
        nmo_cli_json_add_str_safe(data->doc, item, "name",
            (name && name[0]) ? name : "");
        nmo_cli_json_add_str_safe(data->doc, item, "type",
            is_script ? "Script" : is_bb ? "BB" : "Graph");
        if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
            char guid_buf[24];
            snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                     bs->block_guid.d1, bs->block_guid.d2);
            nmo_cli_json_add_str_safe(data->doc, item, "bb_guid", guid_buf);
            const char *proto_name = nmo_behavior_registry_get_name(
                nmo_context_get_bb_registry(c->ctx), bs->block_guid);
            if (proto_name) {
                nmo_cli_json_add_str_safe(data->doc, item, "proto_name",
                                          proto_name);
            }
        }
        yyjson_mut_arr_add_val(data->json_results, item);
    } else if (data->table) {
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

        char proto_buf[64] = "-";
        if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
            const char *proto_name = nmo_behavior_registry_get_name(
                nmo_context_get_bb_registry(c->ctx), bs->block_guid);
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
        nmo_cli_table_add_row(data->table, cells, 4);
    }

    data->match_count++;
    return 0;
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

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);

    behavior_find_data_t find_data = {
        .repo = repo,
        .name_pat = name_pat,
        .guid_pat = guid_pat,
        .ptype_pat = ptype_pat,
        .optype_pat = optype_pat,
        .only_scripts = only_scripts,
        .only_bbs = only_bbs,
    };

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *json_data = NULL;
    yyjson_mut_val *json_results = NULL;
    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        json_data = yyjson_mut_obj(doc);
        json_results = yyjson_mut_arr(doc);
        find_data.doc = doc;
        find_data.json_results = json_results;
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
        find_data.table = &table;
    }

    rc = nmo_core_object_query_run(&c, NULL, behavior_find_object,
                                   &find_data, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        if (doc) yyjson_mut_doc_free(doc);
        if (!c.is_json) nmo_cli_table_free(&table);
        fprintf(stderr, "Error: Failed to query objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_obj_add_uint(doc, json_data, "match_count",
                                (uint64_t)find_data.match_count);
        yyjson_mut_obj_add_val(doc, json_data, "results", json_results);
        nmo_cmd_ctx_json_end(&c, doc, json_data, "behavior.find");
    } else {
        fprintf(c.out, "Found: %zu behavior(s)\n\n", find_data.match_count);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * behavior trace -- execution path tracing from IO (recursive into sub-graphs)
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
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->sub_behavior_links, i);
            nmo_object_t *lo = nmo_object_repository_find_by_id(repo, id);
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
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->sub_behaviors, i);
            if (id == 0) continue;
            nmo_object_t *sub = nmo_object_repository_find_by_id(repo, id);
            if (!sub || !sub->state) continue;
            const nmo_behavior_state_t *sbs = (const nmo_behavior_state_t *)sub->state;
            if (sbs->flags & CKBEHAVIOR_BUILDINGBLOCK) continue;
            if (!collect_links_recursive(repo, id, links, count, cap,
                                         depth + 1, max_depth))
                return false;
        }
    }
    return true;
}

static const char *trace_behavior_type_name(const nmo_behavior_state_t *bs) {
    if (!bs) {
        return "Unknown";
    }
    if (bs->flags & CKBEHAVIOR_SCRIPT) {
        return "Script";
    }
    if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) {
        return "BB";
    }
    return "Graph";
}

static const nmo_behavior_state_t *trace_owner_state(
    nmo_object_repository_t *repo,
    nmo_object_id_t owner_id)
{
    if (!repo || owner_id == 0) {
        return NULL;
    }
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, owner_id);
    return (obj && obj->state) ? (const nmo_behavior_state_t *)obj->state : NULL;
}

static const char *trace_bb_proto_name(
    nmo_context_t *ctx,
    const nmo_behavior_state_t *bs)
{
    if (!ctx || !bs ||
        !(bs->flags & CKBEHAVIOR_BUILDINGBLOCK) ||
        nmo_guid_is_null(bs->block_guid)) {
        return NULL;
    }
    return nmo_behavior_registry_get_name(nmo_context_get_bb_registry(ctx),
                                    bs->block_guid);
}

static const char *trace_transition_name(
    nmo_object_id_t root_behavior_id,
    nmo_object_id_t source_owner,
    nmo_object_id_t target_owner,
    const nmo_behavior_state_t *target_bs)
{
    if (source_owner == 0 || target_owner == 0) {
        return "unknown";
    }
    if (target_owner == root_behavior_id && source_owner != root_behavior_id) {
        return "exit_to_parent";
    }
    if (target_owner != source_owner &&
        target_bs &&
        !(target_bs->flags & CKBEHAVIOR_BUILDINGBLOCK)) {
        return "enter_subgraph";
    }
    return "same_graph";
}

int nmo_cmd_behavior_trace(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--from",  NULL, NMO_OPT_STRING, "Start IO name (default: first bIn)"},
        {"--depth", "-d", NMO_OPT_UINT,   "Max trace depth (default: unlimited)"},
        {"--json",  "-j", NMO_OPT_FLAG,   "JSON output"},
        {"--id",    "-i", NMO_OPT_UINT,   "Behavior object ID"},
        {"--name",  "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_FROM, OPT_DEPTH, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *from_name = vals[OPT_FROM].present ? vals[OPT_FROM].val.str : NULL;
    uint32_t max_trace_depth = vals[OPT_DEPTH].present ? vals[OPT_DEPTH].val.u : 64;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = has_selector_opt ? NULL : (r.pos_count >= 2 ? r.pos_args[0] : NULL);
    if ((has_selector_opt && r.pos_count < 1) || (!has_selector_opt && positional_id == NULL)) {
        fprintf(stderr, "Usage: nmo behavior trace [--from <io>] [--depth N] [--id <id> | --name <name> | <id>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
        .required_base_class = NMO_CID_BEHAVIOR,
        .selector_label = "Behavior",
        .type_label = "CKBehavior",
    };
    nmo_object_t *beh = NULL;
    nmo_object_id_t beh_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &beh, &beh_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo behavior trace [--from <io>] [--depth N] [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }
    if (!beh->state) {
        fprintf(stderr, "Error: Behavior %u has no state\n", beh_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)beh->state;
    const char *beh_name = nmo_object_get_name(beh);

    /* Build link table recursively */
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
    const nmo_behavior_index_t *beh_index = nmo_tool_owner_behavior_index(c.workspace);

    /* Find starting IO */
    nmo_object_id_t start_io = NMO_OBJECT_ID_NONE;
    if (from_name) {
        if (bs->inputs.data) {
            for (size_t i = 0; i < bs->inputs.count; i++) {
                nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->inputs, i);
                const char *ion = resolve_name(repo, id);
                if (ion && nmo_tool_match_wildcard_ci(from_name, ion)) {
                    start_io = id; break;
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
            start_io = nmo_behavior_ref_array_get_id(&bs->inputs, 0);
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
        for (size_t i = 0; i < bs->inputs.count && entry_count < 64; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->inputs, i);
            for (size_t li = 0; li < link_count; li++) {
                if (links[li].source_io == id) {
                    entry_ios[entry_count++] = id;
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
        fprintf(c.out, "Current graph: %s [#%u]\n",
                (beh_name && beh_name[0]) ? beh_name : "(unnamed)",
                beh_id);
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

                nmo_object_id_t src_owner = 0;
                const char *sname = "?";
                if (beh_index) {
                    const nmo_port_owner_t *spo =
                        nmo_behavior_index_find(beh_index, cur.io);
                    if (spo) {
                        src_owner = spo->owner_id;
                        const char *n = resolve_name(repo, src_owner);
                        if (n && n[0]) sname = n;
                    }
                }

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
                const nmo_behavior_state_t *tgt_bs =
                    trace_owner_state(repo, tgt_owner);
                const char *target_type =
                    trace_behavior_type_name(tgt_bs);
                const char *target_proto =
                    trace_bb_proto_name(c.ctx, tgt_bs);
                if (!target_proto &&
                    tgt_bs &&
                    (tgt_bs->flags & CKBEHAVIOR_BUILDINGBLOCK) &&
                    tname && tname[0] && strcmp(tname, "?") != 0) {
                    target_proto = tname;
                }
                const char *transition =
                    trace_transition_name(beh_id, src_owner, tgt_owner,
                                          tgt_bs);
                const char *truncated_reason = NULL;
                nmo_object_id_t loop_io = 0;
                bool loop_detected = false;
                bool has_unseen_continuation = false;
                if (tgt_owner != 0) {
                    nmo_object_t *to =
                        nmo_object_repository_find_by_id(repo, tgt_owner);
                    if (to && to->state) {
                        const nmo_behavior_state_t *tbs =
                            (const nmo_behavior_state_t *)to->state;
                        if (tbs->outputs.data) {
                            for (size_t oi = 0; oi < tbs->outputs.count; oi++) {
                                nmo_object_id_t output_id =
                                    nmo_behavior_ref_array_get_id(&tbs->outputs, oi);
                                bool seen = false;
                                for (size_t v = 0; v < vis_count; v++) {
                                    if (visited[v] == output_id) {
                                        seen = true;
                                        break;
                                    }
                                }
                                if (seen) {
                                    loop_detected = true;
                                    if (loop_io == 0) {
                                        loop_io = output_id;
                                    }
                                } else {
                                    has_unseen_continuation = true;
                                }
                            }
                        }
                    }
                }
                if (cur.depth >= max_trace_depth && has_unseen_continuation) {
                    truncated_reason = "max_depth";
                } else if (loop_detected && !has_unseen_continuation) {
                    truncated_reason = "loop";
                }

                if (c.is_json) {
                    yyjson_mut_val *step = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_uint(doc, step, "depth", cur.depth);
                    yyjson_mut_obj_add_uint(doc, step, "source_io_id",
                                            cur.io);
                    if (src_owner != 0) {
                        yyjson_mut_obj_add_uint(doc, step,
                                                "source_owner_id",
                                                src_owner);
                        nmo_cli_json_add_str_safe(doc, step,
                            "source_owner_name", sname);
                    }
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
                    nmo_cli_json_add_str_safe(doc, step,
                                              "target_behavior_type",
                                              target_type);
                    if (target_proto) {
                        nmo_cli_json_add_str_safe(doc, step,
                                                  "target_bb_proto_name",
                                                  target_proto);
                    }
                    nmo_cli_json_add_str_safe(doc, step, "transition",
                                              transition);
                    if (links[li].delay != 0) {
                        yyjson_mut_obj_add_int(doc, step, "delay",
                                               links[li].delay);
                    }
                    if (truncated_reason) {
                        nmo_cli_json_add_str_safe(doc, step,
                                                  "truncated_reason",
                                                  truncated_reason);
                    }
                    if (loop_detected) {
                        yyjson_mut_obj_add_bool(doc, step, "loop_detected",
                                                true);
                        yyjson_mut_val *loop_path = yyjson_mut_arr(doc);
                        yyjson_mut_arr_add_uint(doc, loop_path, cur.io);
                        yyjson_mut_arr_add_uint(doc, loop_path, tgt);
                        if (loop_io != 0) {
                            yyjson_mut_arr_add_uint(doc, loop_path, loop_io);
                        }
                        yyjson_mut_obj_add_val(doc, step,
                                               "loop_path_io_ids",
                                               loop_path);
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
                                    proto_name = nmo_behavior_registry_get_name(
                                        nmo_context_get_bb_registry(c.ctx),
                                        tgt_bs->block_guid);
                                }
                                if (!proto_name) {
                                    proto_name = target_proto;
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
                    fprintf(c.out, "%s", tname);
                    if (proto_name)
                        fprintf(c.out, " [%s]", proto_name);
                    fprintf(c.out, ".%s%s", tio ? tio : "?", type_label);
                    fprintf(c.out, "  (transition: %s)", transition);
                    if (links[li].delay != 0)
                        fprintf(c.out, "  (delay: %d)", links[li].delay);
                    if (truncated_reason)
                        fprintf(c.out, "  (truncated: %s)", truncated_reason);
                    if (loop_detected)
                        fprintf(c.out, "  (loop path: #%u -> #%u",
                                cur.io, tgt);
                    if (loop_detected && loop_io != 0)
                        fprintf(c.out, " -> #%u", loop_io);
                    if (loop_detected)
                        fprintf(c.out, ")");
                    fprintf(c.out, "\n");
                }

                /* Continue through: add target owner's outputs to stack */
                if (tgt_owner != 0 && cur.depth < max_trace_depth) {
                    nmo_object_t *to = nmo_object_repository_find_by_id(repo, tgt_owner);
                    if (to && to->state) {
                        const nmo_behavior_state_t *tbs = (const nmo_behavior_state_t *)to->state;
                        if (tbs->outputs.data) {
                            for (size_t oi = 0; oi < tbs->outputs.count && sp < stack_cap - 1; oi++) {
                                nmo_object_id_t output_id =
                                    nmo_behavior_ref_array_get_id(&tbs->outputs, oi);
                                bool seen = false;
                                for (size_t v = 0; v < vis_count; v++) {
                                    if (visited[v] == output_id) { seen = true; break; }
                                }
                                if (!seen) {
                                    stack[sp].io = output_id;
                                    stack[sp].depth = cur.depth + 1;
                                    sp++;
                                    if (vis_count < stack_cap) visited[vis_count++] = output_id;
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

