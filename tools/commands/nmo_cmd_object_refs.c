/**
 * @file nmo_cmd_object_refs.c
 * @brief CLI object ref-graph commands: refs, impact, orphans, cycles, graph
 */

#include "nmo_cmd_object.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "runtime/nmo_context.h"
#include "core/nmo_arena.h"
#include "core/nmo_parse.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "export/nmo_export_dot.h"
#include "nmo_tool_common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * object refs - visitor callbacks for nmo_core_iter_refs
 * ============================================================================ */

/*
 * One reference edge. JSON: target_id or source_id, kind, field, index (when
 * non-zero), target_/source_class and _name for a resolved peer, "broken"
 * for an unresolved outgoing target. Text columns: peer id, Kind, Field
 * (with "[index]"), peer class, peer name.
 */
static bool cli_refs_build_record(const nmo_core_ref_info_t *info,
                                  nmo_cli_record_t *rec)
{
    const bool in = info->is_incoming;
    nmo_object_id_t peer_id = in ? info->edge->from : info->edge->to;
    const char *field_name = info->edge->field_path ? info->edge->field_path : "unknown";

    bool ok = nmo_cli_record_uint(rec, in ? "source_id" : "target_id",
                                  in ? "Source" : "Target", peer_id) &&
              nmo_cli_record_str(rec, "kind", "Kind", nmo_ref_kind_name(info->edge->kind)) &&
              nmo_cli_record_str(rec, "field", "Field", field_name);
    if (ok && info->edge->index > 0) {
        ok = nmo_cli_record_set_text_fmt(rec, "%s[%u]", field_name, info->edge->index) &&
             nmo_cli_record_uint(rec, "index", NULL, info->edge->index);
    }

    const char *class_label = in ? "Source Class" : "Target Class";
    const char *name_label = in ? "Source Name" : "Target Name";
    if (info->peer) {
        if (info->peer_class_name) {
            ok = ok && nmo_cli_record_str(rec, in ? "source_class" : "target_class",
                                          class_label, info->peer_class_name);
        } else {
            ok = ok && nmo_cli_record_text(rec, class_label, "-");
        }
        return ok && nmo_cli_record_str_opt(rec, in ? "source_name" : "target_name",
                                            name_label, info->peer_name, "-");
    }
    ok = ok && nmo_cli_record_text(rec, class_label, "-");
    if (in) {
        return ok && nmo_cli_record_text(rec, name_label, "-");
    }
    return ok && nmo_cli_record_text(rec, name_label, "(BROKEN)") &&
           nmo_cli_record_bool(rec, "broken", NULL, true);
}

/** Visitor data: the edge records of each direction, in visit order. */
typedef struct {
    nmo_cli_record_t **items[2]; /* [0] outgoing, [1] incoming */
    size_t counts[2];
    size_t caps[2];
} cli_refs_data_t;

static int cli_refs_visitor(const nmo_core_ref_info_t *info,
                            const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    cli_refs_data_t *d = (cli_refs_data_t *)user;
    const size_t dir = info->is_incoming ? 1u : 0u;

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !cli_refs_build_record(info, rec)) {
        nmo_cli_record_free(rec);
        return 0;
    }
    if (d->counts[dir] == d->caps[dir]) {
        size_t cap = d->caps[dir] ? d->caps[dir] * 2u : 16u;
        nmo_cli_record_t **grown = (nmo_cli_record_t **)realloc(
            d->items[dir], cap * sizeof(*grown));
        if (!grown) {
            nmo_cli_record_free(rec);
            return 0;
        }
        d->items[dir] = grown;
        d->caps[dir] = cap;
    }
    d->items[dir][d->counts[dir]++] = rec;
    return 0;
}

static const nmo_cli_table_col_t object_refs_out_columns[] = {
    {"Target", NMO_CLI_ALIGN_RIGHT, 8, 0},
    {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
    {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
    {"Target Class", NMO_CLI_ALIGN_LEFT, 18, 0},
    {"Target Name", NMO_CLI_ALIGN_LEFT, 25, 0},
};
static const nmo_cli_table_col_t object_refs_in_columns[] = {
    {"Source", NMO_CLI_ALIGN_RIGHT, 8, 0},
    {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
    {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
    {"Source Class", NMO_CLI_ALIGN_LEFT, 18, 0},
    {"Source Name", NMO_CLI_ALIGN_LEFT, 25, 0},
};

/*
 * object refs report. JSON: id, class_name, name, then outgoing/incoming edge
 * arrays each followed by its count. Text: a title line, then one table per
 * direction, "  (none)" when it is empty. Takes ownership of the edge
 * records in `refs`.
 */
static nmo_cli_record_t *object_refs_record_new(const nmo_cmd_ctx_t *c,
                                                nmo_object_t *obj,
                                                nmo_object_id_t object_id,
                                                cli_refs_data_t *refs,
                                                const nmo_core_ref_result_t *result)
{
    static const char *const keys[2] = {"outgoing", "incoming"};
    static const char *const count_keys[2] = {"outgoing_count", "incoming_count"};
    static const char *const labels[2] = {"Outgoing references", "Incoming references"};
    static const nmo_cli_table_col_t *const columns[2] = {
        object_refs_out_columns, object_refs_in_columns,
    };
    const size_t totals[2] = {result->outgoing, result->incoming};

    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_core_class_name(c, nmo_object_get_class_id(obj));
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && nmo_cli_record_uint(rec, "id", NULL, object_id);
    if (ok && class_name) {
        ok = nmo_cli_record_str(rec, "class_name", NULL, class_name);
    }
    if (ok && name && name[0]) {
        ok = nmo_cli_record_str(rec, "name", NULL, name);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, "References for object #%u: %s [%s]\n",
                                      object_id,
                                      (name && name[0]) ? name : "(unnamed)",
                                      class_name ? class_name : "?");
    for (size_t dir = 0; dir < 2u; ++dir) {
        nmo_cli_record_array_t *arr =
            ok ? nmo_cli_record_array(rec, keys[dir], labels[dir]) : NULL;
        ok = arr != NULL &&
             nmo_cli_record_array_set_table(arr, columns[dir], 5) &&
             nmo_cli_record_array_set_empty_text(arr, "  (none)");
        for (size_t i = 0; i < refs->counts[dir]; ++i) {
            ok = nmo_cli_record_array_add(ok ? arr : NULL, refs->items[dir][i]) && ok;
        }
        free(refs->items[dir]);
        refs->items[dir] = NULL;
        refs->counts[dir] = 0;
        ok = ok && nmo_cli_record_uint(rec, count_keys[dir], NULL, (uint64_t)totals[dir]);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

typedef struct object_refs_args {
    bool has_id;
    uint32_t id;
    const char *positional_id;
    const char *name;
} object_refs_args_t;

static int object_refs_parse(int argc, char **argv, bool expect_file_operand,
                             object_refs_args_t *args, const char *usage)
{
    memset(args, 0, sizeof(*args));

    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    if (expect_file_operand) {
        args->positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;
        if (!has_selector_opt && args->positional_id == NULL) {
            fprintf(stderr, "Error: No object selector specified\n");
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else if (has_selector_opt) {
        if (r.pos_count != 0) {
            fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count != 1) {
            fprintf(stderr, "Error: No object selector specified\n");
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args->positional_id = r.pos_args[0];
    }

    args->has_id = vals[OPT_ID].present;
    args->id = nmo_opt_uint_or(&vals[OPT_ID], 0);
    args->name = nmo_opt_str(&vals[OPT_NAME]);
    return NMO_CLI_EXIT_SUCCESS;
}

static int object_refs_run(nmo_cmd_ctx_t *ctx, const object_refs_args_t *args,
                           bool close_ctx, const char *usage)
{
    nmo_cmd_ctx_t c = *ctx;

    nmo_core_object_selector_t selector = {
        .has_id = args->has_id,
        .id = args->id,
        .positional_id = args->positional_id,
        .name = args->name,
        .selector_label = "Object",
        .type_label = "object",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int rc = nmo_core_resolve_one_object(&c, &selector, &obj, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
    }

    cli_refs_data_t refs = {0};
    nmo_core_ref_result_t ref_result = {0};
    nmo_core_iter_refs(&c, object_id, NMO_CORE_REFS_BOTH,
                       cli_refs_visitor, &refs, &ref_result);
    rc = nmo_cmd_ctx_emit_record(&c, object_refs_record_new(&c, obj, object_id,
                                                            &refs, &ref_result),
                                 "object.refs", 0, c.colorize);
    return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
}

int nmo_cmd_object_refs(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    object_refs_args_t args;
    const char *usage = "nmo object refs [--id <id> | --name <name> | <id>] <file>";
    int rc = object_refs_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return object_refs_run(&c, &args, true, usage);
}

int nmo_cmd_object_refs_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    object_refs_args_t args;
    const char *usage = "object refs [--id <id> | --name <name> | <id>]";
    int rc = object_refs_parse(argc, argv, false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    return object_refs_run(ctx, &args, false, usage);
}

/* ============================================================================
 * object impact - Show deletion impact analysis
 * ============================================================================ */

/*
 * One dependent or cascade entry for object impact. JSON: id, then name and
 * class_name when the object resolves, then ref_kind when given. Text: ID,
 * Class, Name (, Kind).
 */
static bool object_impact_build_record(const nmo_cmd_ctx_t *c, nmo_object_id_t id,
                                       const char *ref_kind, nmo_cli_record_t *rec)
{
    nmo_object_t *peer = nmo_core_find_by_id(c, id);
    const char *name = peer ? nmo_object_get_name(peer) : NULL;
    nmo_class_id_t cid = peer ? nmo_object_get_class_id(peer) : 0;
    const char *cls = peer ? nmo_core_class_name(c, cid) : NULL;

    bool ok = nmo_cli_record_uint(rec, "id", "ID", id);
    if (ok && peer && name && name[0]) {
        ok = nmo_cli_record_str(rec, "name", NULL, name);
    }
    if (cls) {
        ok = ok && nmo_cli_record_str(rec, "class_name", "Class", cls);
    } else if (peer) {
        ok = ok && nmo_cli_record_str_fmt(rec, "class_name", "Class", "Class#%u", (unsigned)cid);
    } else {
        ok = ok && nmo_cli_record_text(rec, "Class", "-");
    }
    ok = ok && nmo_cli_record_text(rec, "Name", (name && name[0]) ? name : "-");
    if (ok && ref_kind) {
        ok = nmo_cli_record_str(rec, "ref_kind", "Kind", ref_kind);
    }
    return ok;
}

static const nmo_cli_table_col_t object_impact_dep_columns[] = {
    {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
    {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
    {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
    {"Kind",  NMO_CLI_ALIGN_LEFT, 15, 0},
};
static const nmo_cli_table_col_t object_impact_cascade_columns[] = {
    {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
    {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
    {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
};

/* Add one object_impact_build_record() item to `arr`. */
static bool object_impact_add(const nmo_cmd_ctx_t *c, nmo_cli_record_array_t *arr,
                              nmo_object_id_t id, const char *ref_kind)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    if (!item || !object_impact_build_record(c, id, ref_kind, item)) {
        nmo_cli_record_free(item);
        return true;
    }
    return nmo_cli_record_array_add(arr, item);
}

/*
 * object impact report. JSON: target {id, name, class_name},
 * direct_dependents, cascade_set, cascade_count. Text: a title line, then
 * the dependents and cascade tables, "  (none)" when empty.
 */
static nmo_cli_record_t *object_impact_record_new(const nmo_cmd_ctx_t *c,
                                                  nmo_object_id_t object_id,
                                                  const char *obj_name,
                                                  const char *obj_class,
                                                  const nmo_ref_edge_t *in_edges,
                                                  size_t in_count,
                                                  const nmo_object_id_t *cascade_ids,
                                                  size_t cascade_count)
{
    const bool named = obj_name && obj_name[0];
    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_t *target = rec ? nmo_cli_record_object(rec, "target") : NULL;
    bool ok = target != NULL && nmo_cli_record_uint(target, "id", NULL, object_id);
    if (ok && named) {
        ok = nmo_cli_record_str(target, "name", NULL, obj_name);
    }
    ok = ok && nmo_cli_record_str(target, "class_name", NULL, obj_class) &&
         nmo_cli_record_raw_fmt(rec, "Impact Analysis: Object #%u%s%s%s (%s)\n",
                                object_id, named ? " \"" : "",
                                named ? obj_name : "", named ? "\"" : "",
                                obj_class);

    nmo_cli_record_array_t *deps =
        ok ? nmo_cli_record_array(rec, "direct_dependents", "Direct dependents") : NULL;
    ok = deps != NULL &&
         nmo_cli_record_array_set_table(deps, object_impact_dep_columns,
                                        sizeof(object_impact_dep_columns) /
                                            sizeof(object_impact_dep_columns[0])) &&
         nmo_cli_record_array_set_empty_text(deps, "  (none)");
    for (size_t i = 0; ok && i < in_count; ++i) {
        ok = object_impact_add(c, deps, in_edges[i].from,
                               nmo_ref_kind_name(in_edges[i].kind));
    }

    nmo_cli_record_array_t *cas =
        ok ? nmo_cli_record_array(rec, "cascade_set", NULL) : NULL;
    char *heading = cas ? nmo_tool_strdup_fmt("Cascade deletion would remove %zu object(s):",
                                              cascade_count)
                        : NULL;
    ok = heading != NULL && nmo_cli_record_array_set_heading(cas, heading) &&
         nmo_cli_record_array_set_table(cas, object_impact_cascade_columns,
                                        sizeof(object_impact_cascade_columns) /
                                            sizeof(object_impact_cascade_columns[0])) &&
         nmo_cli_record_array_set_empty_text(cas, "  (none)");
    free(heading);
    for (size_t i = 0; ok && i < cascade_count; ++i) {
        ok = object_impact_add(c, cas, cascade_ids[i], NULL);
    }
    ok = ok && nmo_cli_record_uint(rec, "cascade_count", NULL, (uint64_t)cascade_count);
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static int object_impact_run(nmo_cmd_ctx_t *ctx, const object_refs_args_t *args,
                             bool close_ctx, const char *usage) {
    nmo_cmd_ctx_t c = *ctx;
    nmo_core_object_selector_t selector = {
        .has_id = args->has_id,
        .id = args->id,
        .positional_id = args->positional_id,
        .name = args->name,
        .selector_label = "Object",
        .type_label = "object",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int rc = nmo_core_resolve_one_object(&c, &selector, &obj, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
    }

    const char *obj_name = nmo_object_get_name(obj);
    nmo_class_id_t obj_cid = nmo_object_get_class_id(obj);
    const char *obj_class = nmo_core_class_name(&c, obj_cid);
    char *obj_class_owned = obj_class ? NULL : nmo_tool_strdup_fmt("Class#%u", (unsigned)obj_cid);
    if (!obj_class) {
        obj_class = obj_class_owned ? obj_class_owned : "?";
    }

    /* Get reference graph from session cache */
    nmo_ref_graph_t *graph = nmo_tool_owner_ref_graph(c.workspace);
    if (!graph) {
        fprintf(stderr, "Error: Failed to create reference graph\n");
        free(obj_class_owned);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Arena for cascade preview */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        free(obj_class_owned);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);

    /* Get direct dependents (incoming refs) */
    nmo_ref_edge_t *in_edges = NULL;
    size_t in_count = 0;
    nmo_ref_graph_get_object_edges(graph, object_id, NMO_REF_DIR_INCOMING,
                                   &in_edges, &in_count);

    /* Preview cascade deletion */
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(c.ctx);
    nmo_object_id_t *cascade_ids = NULL;
    size_t cascade_count = 0;
    int preview_rc = nmo_runtime_preview_delete(
        repo, type_rt, arena,
        &object_id, 1,
        NMO_RUNTIME_REQUEST_CASCADE,
        &cascade_ids, &cascade_count);

    if (preview_rc != NMO_OK) {
        /* Fallback: just the target itself */
        cascade_ids = NULL;
        cascade_count = 0;
    }

    rc = nmo_cmd_ctx_emit_record(&c, object_impact_record_new(&c, object_id, obj_name,
                                                              obj_class, in_edges, in_count,
                                                              cascade_ids, cascade_count),
                                 "object.impact", 0, c.colorize);

    nmo_arena_destroy(arena);
    free(obj_class_owned);
    return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
}

int nmo_cmd_object_impact(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    object_refs_args_t args;
    const char *usage = "nmo object impact [--id <id> | --name <name> | <id>] <file>";
    int rc = object_refs_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return object_impact_run(&c, &args, true, usage);
}

/* ============================================================================
 * object orphans - Find unreachable objects
 * ============================================================================ */

typedef struct object_orphans_args {
    const char *class_filter_str;
} object_orphans_args_t;

static int object_orphans_parse(int argc, char **argv, bool expect_file_operand,
                                object_orphans_args_t *args,
                                const char *usage) {
    memset(args, 0, sizeof(*args));

    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_CLASS_FILTER,
    };
    enum { OPT_CLASS };
    nmo_opt_val_t vals[1];
    const char *pos_arr[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->class_filter_str = nmo_opt_str(&vals[OPT_CLASS]);
    return NMO_CLI_EXIT_SUCCESS;
}

/* One unreachable object: JSON id/class_id/class_name/name, text ID/Class/Name. */
static bool object_orphan_build_record(const nmo_cmd_ctx_t *c, nmo_object_id_t id,
                                       nmo_object_t *o, nmo_cli_record_t *rec)
{
    nmo_class_id_t cid = nmo_object_get_class_id(o);
    const char *cname = nmo_core_class_name(c, cid);
    const char *name = nmo_object_get_name(o);
    return nmo_cli_record_uint(rec, "id", "ID", (uint64_t)id) &&
           nmo_cli_record_uint(rec, "class_id", NULL, (uint64_t)cid) &&
           (cname ? nmo_cli_record_str(rec, "class_name", "Class", cname)
                  : nmo_cli_record_str_fmt(rec, "class_name", "Class", "Class#%u", (unsigned)cid)) &&
           nmo_cli_record_str_opt(rec, "name", "Name", name, "-");
}

static int object_orphans_run(nmo_cmd_ctx_t *ctx, const object_orphans_args_t *args,
                              bool close_ctx) {
    nmo_cmd_ctx_t c = *ctx;
    nmo_object_query_t class_query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = args->class_filter_str,
        .include_derived_classes = true,
    };
    int rc = nmo_core_query_build(&c, &class_query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
    }
    const nmo_object_query_t *filter_query =
        args->class_filter_str != NULL ? &class_query : NULL;

    nmo_core_iter_result_t object_query_result = {0};
    rc = nmo_core_object_query_run(&c, NULL, NULL, NULL, &object_query_result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    size_t object_count = object_query_result.matched;

    /* Get reference graph from session cache */
    nmo_ref_graph_t *graph = nmo_tool_owner_ref_graph(c.workspace);
    if (!graph) {
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Use library API for orphan detection */
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    nmo_object_id_t *orphan_ids = NULL;
    size_t orphan_count = 0;
    nmo_status_t st = nmo_ref_graph_find_orphans(
        graph, repo, c.registry, arena, &orphan_ids, &orphan_count);
    if (st != NMO_OK) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Orphan detection failed\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Apply optional class filter to the orphan list */
    size_t filtered_count = 0;
    for (size_t i = 0; i < orphan_count; ++i) {
        if (filter_query != NULL) {
            nmo_object_t *o = nmo_core_find_by_id(&c, orphan_ids[i]);
            if (!o) continue;
            if (!nmo_core_query_matches_object(&c, filter_query, o)) {
                continue;
            }
        }
        orphan_ids[filtered_count++] = orphan_ids[i];
    }
    orphan_count = filtered_count;

    static const nmo_cli_table_col_t cols[] = {
        {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
        {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
        {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
    };

    /* Text: a title line, then the table only when something is unreachable. */
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "total_objects", NULL, (uint64_t)object_count) &&
              nmo_cli_record_uint(rec, "orphan_count", NULL, (uint64_t)orphan_count) &&
              nmo_cli_record_raw_fmt(rec, "Orphan Analysis: %zu unreachable object(s) "
                                          "(of %zu total)\n\n",
                                     orphan_count, object_count);
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "orphans", NULL) : NULL;
    ok = arr != NULL;
    if (ok && orphan_count > 0) {
        ok = nmo_cli_record_array_set_table(arr, cols, sizeof(cols) / sizeof(cols[0]));
    }
    for (size_t i = 0; ok && i < orphan_count; ++i) {
        nmo_object_t *o = nmo_core_find_by_id(&c, orphan_ids[i]);
        if (!o) continue;
        nmo_cli_record_t *item = nmo_cli_record_new();
        if (item && object_orphan_build_record(&c, orphan_ids[i], o, item)) {
            ok = nmo_cli_record_array_add(arr, item);
        } else {
            nmo_cli_record_free(item);
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "object.orphans", 0, c.colorize);

    nmo_arena_destroy(arena);
    return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
}

int nmo_cmd_object_orphans(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    object_orphans_args_t args;
    const char *usage = "nmo object orphans [--class <name>] <file>";
    int rc = object_orphans_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return object_orphans_run(&c, &args, true);
}

/* ============================================================================
 * object cycles - Detect circular references
 * ============================================================================ */

static int object_cycles_parse(int argc, char **argv, bool expect_file_operand,
                               const char *usage) {
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, NULL, 0, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

/*
 * One cycle. JSON: length, objects [{id, name, class_name}], ref_kinds.
 * Text: "Cycle N (M objects):", the id chain back to its first object, and
 * the chain of reference kinds.
 */
static bool object_cycle_build_record(const nmo_cmd_ctx_t *c, size_t index,
                                      const nmo_ref_cycle_t *cycle,
                                      nmo_cli_record_t *rec)
{
    bool ok = nmo_cli_record_uint(rec, "length", NULL, (uint64_t)cycle->count) &&
              nmo_cli_record_raw_fmt(rec, "\nCycle %zu (%zu object%s):\n  ",
                                     index + 1, cycle->count,
                                     cycle->count == 1 ? "" : "s");
    nmo_cli_record_array_t *objs = ok ? nmo_cli_record_array(rec, "objects", NULL) : NULL;
    ok = objs != NULL;
    for (size_t j = 0; ok && j < cycle->count; ++j) {
        nmo_object_t *o = nmo_core_find_by_id(c, cycle->ids[j]);
        const char *n = o ? nmo_object_get_name(o) : NULL;
        char *cls = o ? nmo_core_class_name_dup(c, nmo_object_get_class_id(o)) : NULL;
        nmo_cli_record_t *entry = nmo_cli_record_new();
        ok = entry != NULL && nmo_cli_record_uint(entry, "id", NULL, cycle->ids[j]);
        if (ok && o && n && n[0]) {
            ok = nmo_cli_record_str(entry, "name", NULL, n);
        }
        if (ok && o) {
            ok = nmo_cli_record_str(entry, "class_name", NULL, cls ? cls : "?");
        }
        ok = nmo_cli_record_array_add(ok ? objs : NULL, entry) && ok;
        ok = ok && nmo_cli_record_raw_fmt(rec, "%s#%u %s", j > 0 ? " -> " : "",
                                          cycle->ids[j], cls ? cls : "?");
        if (ok && n && n[0]) {
            ok = nmo_cli_record_raw_fmt(rec, " \"%s\"", n);
        }
        free(cls);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, " -> #%u\n  Reference kinds: ", cycle->ids[0]);

    const char **kinds = ok ? (const char **)calloc(cycle->count ? cycle->count : 1u,
                                                    sizeof(*kinds))
                            : NULL;
    ok = kinds != NULL;
    for (size_t j = 0; ok && j < cycle->count; ++j) {
        kinds[j] = nmo_ref_kind_name(cycle->kinds[j]);
        ok = nmo_cli_record_raw_fmt(rec, "%s%s", j > 0 ? " -> " : "", kinds[j]);
    }
    ok = ok && nmo_cli_record_str_list(rec, "ref_kinds", NULL, kinds, cycle->count, NULL) &&
         nmo_cli_record_raw(rec, "\n");
    free(kinds);
    return ok;
}

/*
 * object cycles report. JSON: cycle_count, cycles. Text: a summary line,
 * then each cycle.
 */
static nmo_cli_record_t *object_cycles_record_new(const nmo_cmd_ctx_t *c,
                                                  const nmo_ref_cycle_t *cycles,
                                                  size_t cycle_count)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "cycle_count", NULL, (uint64_t)cycle_count);
    if (ok && cycle_count == 0) {
        ok = nmo_cli_record_raw(rec, "No circular references detected.\n");
    } else if (ok) {
        ok = nmo_cli_record_raw_fmt(rec, "Cycle Detection: %zu cycle(s) found\n",
                                    cycle_count);
    }
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "cycles", NULL) : NULL;
    ok = arr != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(arr);
        nmo_cli_record_array_inline_items(arr);
    }
    for (size_t ci = 0; ok && ci < cycle_count; ++ci) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL && object_cycle_build_record(c, ci, &cycles[ci], item);
        ok = nmo_cli_record_array_add(ok ? arr : NULL, item) && ok;
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static int object_cycles_run(nmo_cmd_ctx_t *ctx, bool close_ctx) {
    nmo_cmd_ctx_t c = *ctx;
    /* Get reference graph from session cache */
    nmo_ref_graph_t *graph = nmo_tool_owner_ref_graph(c.workspace);
    if (!graph) {
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Use library API for cycle detection */
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    nmo_ref_cycle_t *cycles = NULL;
    size_t cycle_count = 0;
    nmo_status_t st = nmo_ref_graph_find_cycles(graph, repo, arena,
                                                 &cycles, &cycle_count);
    if (st != NMO_OK) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Cycle detection failed\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    int rc = nmo_cmd_ctx_emit_record(&c, object_cycles_record_new(&c, cycles, cycle_count),
                                     "object.cycles", 0, false);
    nmo_arena_destroy(arena);
    return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
}

int nmo_cmd_object_cycles(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *usage = "nmo object cycles <file>";
    int rc = object_cycles_parse(argc, argv, true, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return object_cycles_run(&c, true);
}

/* ============================================================================
 * object graph - Export full reference graph
 * ============================================================================ */

/** Collect unique node IDs from edges into a sorted array */
static size_t graph_collect_nodes(const nmo_ref_edge_t *edges, size_t edge_count,
                                  nmo_object_id_t *out, size_t cap) {
    size_t count = 0;
    for (size_t i = 0; i < edge_count; ++i) {
        nmo_object_id_t ids[2] = { edges[i].from, edges[i].to };
        for (int k = 0; k < 2; ++k) {
            /* Linear search for existing */
            bool found = false;
            for (size_t j = 0; j < count; ++j) {
                if (out[j] == ids[k]) { found = true; break; }
            }
            if (!found && count < cap) {
                out[count++] = ids[k];
            }
        }
    }
    /* Simple insertion sort */
    for (size_t i = 1; i < count; ++i) {
        nmo_object_id_t key = out[i];
        size_t j = i;
        while (j > 0 && out[j - 1] > key) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }
    return count;
}

typedef struct object_graph_args {
    bool dot_mode;
    const char *kind_str;
} object_graph_args_t;

static int object_graph_parse(int argc, char **argv, bool expect_file_operand,
                              object_graph_args_t *args,
                              const char *usage) {
    memset(args, 0, sizeof(*args));

    static const nmo_opt_def_t opts[] = {
        {"--dot",  NULL, NMO_OPT_FLAG,   "Output DOT digraph format"},
        {"--kind", NULL, NMO_OPT_STRING, "Filter edges by ref kind name"},
    };
    enum { OPT_DOT, OPT_KIND };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->dot_mode = nmo_opt_flag(&vals[OPT_DOT]);
    args->kind_str = nmo_opt_str(&vals[OPT_KIND]);
    return NMO_CLI_EXIT_SUCCESS;
}

/*
 * object graph report. JSON: node_count, edge_count, nodes, edges and a
 * per-kind edge count object. Text: the counts and the per-kind lines.
 */
static nmo_cli_record_t *object_graph_record_new(const nmo_cmd_ctx_t *c,
                                                 const nmo_object_id_t *node_ids,
                                                 size_t node_count,
                                                 const nmo_ref_edge_t *edges,
                                                 size_t edge_count,
                                                 const size_t *kind_counts)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "node_count", NULL, (uint64_t)node_count) &&
              nmo_cli_record_uint(rec, "edge_count", NULL, (uint64_t)edge_count) &&
              nmo_cli_record_raw_fmt(rec, "Reference Graph: %zu nodes, %zu edges\n\n"
                                          "Edges by kind:\n",
                                     node_count, edge_count);

    nmo_cli_record_array_t *nodes = ok ? nmo_cli_record_array(rec, "nodes", NULL) : NULL;
    ok = nodes != NULL;
    for (size_t i = 0; ok && i < node_count; ++i) {
        nmo_cli_record_t *node = nmo_cli_record_new();
        ok = node != NULL && nmo_cli_record_uint(node, "id", NULL, node_ids[i]);
        nmo_object_t *obj = ok ? nmo_core_find_by_id(c, node_ids[i]) : NULL;
        if (obj) {
            char *cls = nmo_core_class_name_dup(c, nmo_object_get_class_id(obj));
            ok = nmo_cli_record_str(node, "class_name", NULL, cls ? cls : "?");
            free(cls);
            const char *name = nmo_object_get_name(obj);
            if (ok && name && name[0]) {
                ok = nmo_cli_record_str(node, "name", NULL, name);
            }
        }
        ok = nmo_cli_record_array_add(ok ? nodes : NULL, node) && ok;
    }

    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "edges", NULL) : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < edge_count; ++i) {
        nmo_cli_record_t *edge = nmo_cli_record_new();
        ok = edge != NULL &&
             nmo_cli_record_uint(edge, "from", NULL, edges[i].from) &&
             nmo_cli_record_uint(edge, "to", NULL, edges[i].to) &&
             nmo_cli_record_str(edge, "kind", NULL, nmo_ref_kind_name(edges[i].kind)) &&
             nmo_cli_record_str(edge, "field", NULL,
                                edges[i].field_path ? edges[i].field_path : "unknown");
        ok = nmo_cli_record_array_add(ok ? arr : NULL, edge) && ok;
    }

    nmo_cli_record_t *summary = ok ? nmo_cli_record_object(rec, "kind_summary") : NULL;
    ok = summary != NULL;
    bool any_kind = false;
    for (int k = 0; ok && k < NMO_REF_KIND_MAX; ++k) {
        if (kind_counts[k] > 0) {
            const char *kind = nmo_ref_kind_name((nmo_ref_kind_t)k);
            ok = nmo_cli_record_uint(summary, kind, NULL, (uint64_t)kind_counts[k]) &&
                 nmo_cli_record_raw_fmt(summary, "  %-16s: %4zu\n", kind, kind_counts[k]);
            any_kind = true;
        }
    }
    if (ok && !any_kind) {
        ok = nmo_cli_record_raw(summary, "  (none)\n");
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static int object_graph_run(nmo_cmd_ctx_t *ctx, const object_graph_args_t *args,
                            bool close_ctx) {
    nmo_cmd_ctx_t c = *ctx;
    /* Get reference graph from session cache */
    nmo_ref_graph_t *graph = nmo_tool_owner_ref_graph(c.workspace);
    if (!graph) {
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get all edges */
    nmo_ref_edge_t *all_edges = NULL;
    size_t all_count = 0;
    if (nmo_ref_graph_get_edges(graph, &all_edges, &all_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get edges\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Optional kind filter: build a filtered edge list */
    nmo_ref_edge_t *edges = all_edges;
    size_t edge_count = all_count;
    nmo_ref_edge_t *filtered = NULL;

    if (args->kind_str) {
        filtered = (nmo_ref_edge_t *)malloc(all_count * sizeof(nmo_ref_edge_t));
        if (!filtered) {
            fprintf(stderr, "Error: Allocation failed\n");
            return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        size_t fc = 0;
        for (size_t i = 0; i < all_count; ++i) {
            if (nmo_tool_streq_ci(nmo_ref_kind_name(all_edges[i].kind),
                                  args->kind_str)) {
                filtered[fc++] = all_edges[i];
            }
        }
        edges = filtered;
        edge_count = fc;
    }

    /* Collect unique node IDs */
    size_t node_cap = edge_count * 2 + 1;
    nmo_object_id_t *node_ids = (nmo_object_id_t *)malloc(
        node_cap * sizeof(nmo_object_id_t));
    size_t node_count = 0;
    if (node_ids) {
        node_count = graph_collect_nodes(edges, edge_count, node_ids, node_cap);
    }

    /* Kind summary counts (always over the filtered set) */
    size_t kind_counts[NMO_REF_KIND_MAX];
    memset(kind_counts, 0, sizeof(kind_counts));
    for (size_t i = 0; i < edge_count; ++i) {
        if ((int)edges[i].kind >= 0 && edges[i].kind < NMO_REF_KIND_MAX)
            kind_counts[edges[i].kind]++;
    }

    int rc = NMO_CLI_EXIT_SUCCESS;
    if (!c.is_json && args->dot_mode) {
        /* ---- DOT output via library ---- */
        uint32_t kind_mask = 0;
        if (args->kind_str) {
            for (int k = 0; k < NMO_REF_KIND_MAX; ++k) {
                if (nmo_tool_streq_ci(nmo_ref_kind_name((nmo_ref_kind_t)k),
                                      args->kind_str)) {
                    kind_mask |= (1u << (unsigned)k);
                    break;
                }
            }
        }
        nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
        nmo_arena_t *dot_arena = nmo_arena_create(NULL, 0);
        nmo_ref_graph_to_dot(graph, repo, c.registry, kind_mask, dot_arena, c.out);
        nmo_arena_destroy(dot_arena);
    } else {
        rc = nmo_cmd_ctx_emit_record(&c, object_graph_record_new(&c, node_ids, node_count,
                                                                 edges, edge_count,
                                                                 kind_counts),
                                     "object.graph", 0, false);
    }

    free(node_ids);
    free(filtered);
    return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
}

int nmo_cmd_object_graph(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    object_graph_args_t args;
    const char *usage = "nmo object graph [--dot] [--kind <kind>] <file>";
    int rc = object_graph_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return object_graph_run(&c, &args, true);
}

int nmo_cmd_object_refgraph_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: object impact|orphans|cycles|graph ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "impact") == 0 || strcmp(argv[0], "imp") == 0) {
        object_refs_args_t args;
        const char *usage = "object impact [--id <id> | --name <name> | <id>]";
        int rc = object_refs_parse(argc, argv, false, &args, usage);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        return object_impact_run(ctx, &args, false, usage);
    }

    if (strcmp(argv[0], "orphans") == 0 || strcmp(argv[0], "orp") == 0) {
        object_orphans_args_t args;
        const char *usage = "object orphans [--class <name>]";
        int rc = object_orphans_parse(argc, argv, false, &args, usage);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        return object_orphans_run(ctx, &args, false);
    }

    if (strcmp(argv[0], "cycles") == 0 || strcmp(argv[0], "cyc") == 0) {
        const char *usage = "object cycles";
        int rc = object_cycles_parse(argc, argv, false, usage);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        return object_cycles_run(ctx, false);
    }

    if (strcmp(argv[0], "graph") == 0 || strcmp(argv[0], "gr") == 0) {
        object_graph_args_t args;
        const char *usage = "object graph [--dot] [--kind <kind>]";
        int rc = object_graph_parse(argc, argv, false, &args, usage);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
        return object_graph_run(ctx, &args, false);
    }

    fprintf(stderr, "Unsupported object reference graph action in session: %s\n",
            argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

