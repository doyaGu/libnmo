/**
 * @file nmo_cmd_object_refs.c
 * @brief CLI object ref-graph commands: refs, impact, orphans, cycles, graph
 */

#include "nmo_cmd_object.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_runtime_delete.h"
#include "core/nmo_arena.h"
#include "core/nmo_parse.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "app/nmo_export_dot.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * object refs - visitor callbacks for nmo_core_iter_refs
 * ============================================================================ */

/** Visitor data for JSON ref output */
typedef struct {
    yyjson_mut_doc *doc;
    yyjson_mut_val *outgoing;
    yyjson_mut_val *incoming;
} cli_refs_json_data_t;

static int cli_refs_json_visitor(const nmo_core_ref_info_t *info,
                                 const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    cli_refs_json_data_t *d = (cli_refs_json_data_t *)user;
    yyjson_mut_doc *doc = d->doc;

    yyjson_mut_val *edge = yyjson_mut_obj(doc);

    if (info->is_incoming) {
        yyjson_mut_obj_add_uint(doc, edge, "source_id", info->edge->from);
    } else {
        yyjson_mut_obj_add_uint(doc, edge, "target_id", info->edge->to);
    }

    yyjson_mut_obj_add_str(doc, edge, "kind",
                           nmo_ref_kind_name(info->edge->kind));
    yyjson_mut_obj_add_str(doc, edge, "field",
                           info->edge->field_path ? info->edge->field_path : "unknown");
    if (info->edge->index > 0) {
        yyjson_mut_obj_add_uint(doc, edge, "index", info->edge->index);
    }

    if (info->peer) {
        if (info->peer_class_name) {
            yyjson_mut_obj_add_str(doc, edge,
                info->is_incoming ? "source_class" : "target_class",
                info->peer_class_name);
        }
        if (info->peer_name && info->peer_name[0]) {
            nmo_cli_json_add_str_safe(doc, edge,
                info->is_incoming ? "source_name" : "target_name",
                info->peer_name);
        }
    } else if (!info->is_incoming) {
        yyjson_mut_obj_add_bool(doc, edge, "broken", true);
    }

    yyjson_mut_arr_add_val(
        info->is_incoming ? d->incoming : d->outgoing, edge);
    return 0;
}

/** Visitor data for text ref output */
typedef struct {
    nmo_cli_table_t *out_table;
    nmo_cli_table_t *in_table;
} cli_refs_text_data_t;

static int cli_refs_text_visitor(const nmo_core_ref_info_t *info,
                                 const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    cli_refs_text_data_t *d = (cli_refs_text_data_t *)user;

    nmo_object_id_t peer_id = info->is_incoming ? info->edge->from : info->edge->to;
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", peer_id);

    char field_buf[32];
    const char *field_name = info->edge->field_path ? info->edge->field_path : "unknown";
    if (info->edge->index > 0) {
        snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                 field_name, info->edge->index);
    } else {
        snprintf(field_buf, sizeof(field_buf), "%s", field_name);
    }

    const char *peer_class = "-";
    const char *peer_name = "-";

    if (info->peer) {
        if (info->peer_class_name) peer_class = info->peer_class_name;
        if (info->peer_name && info->peer_name[0]) peer_name = info->peer_name;
    } else if (!info->is_incoming) {
        peer_name = "(BROKEN)";
    }

    const char *cells[] = {
        id_buf,
        nmo_ref_kind_name(info->edge->kind),
        field_buf,
        peer_class,
        peer_name
    };

    nmo_cli_table_t *table = info->is_incoming ? d->in_table : d->out_table;
    nmo_cli_table_add_row(table, cells, 5);
    return 0;
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
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
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
    args->id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0;
    args->name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
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

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Object info */
        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        const char *class_name = nmo_core_class_name(&c, nmo_object_get_class_id(obj));
        if (class_name) {
            yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        }
        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(doc, data, "name", name);
        }

        cli_refs_json_data_t jd = {
            .doc = doc,
            .outgoing = yyjson_mut_arr(doc),
            .incoming = yyjson_mut_arr(doc),
        };

        nmo_core_ref_result_t ref_result = {0};
        nmo_core_iter_refs(&c, object_id, NMO_CORE_REFS_BOTH,
                           cli_refs_json_visitor, &jd, &ref_result);

        yyjson_mut_obj_add_val(doc, data, "outgoing", jd.outgoing);
        yyjson_mut_obj_add_uint(doc, data, "outgoing_count",
                                (uint64_t)ref_result.outgoing);
        yyjson_mut_obj_add_val(doc, data, "incoming", jd.incoming);
        yyjson_mut_obj_add_uint(doc, data, "incoming_count",
                                (uint64_t)ref_result.incoming);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.refs");
    } else {
        /* Text output */
        const char *obj_name = nmo_object_get_name(obj);
        const char *obj_class = nmo_core_class_name(&c, nmo_object_get_class_id(obj));
        fprintf(c.out, "References for object #%u: %s [%s]\n\n",
                object_id,
                (obj_name && obj_name[0]) ? obj_name : "(unnamed)",
                obj_class ? obj_class : "?");

        static const nmo_cli_table_col_t out_cols[] = {
            {"Target", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
            {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
            {"Target Class", NMO_CLI_ALIGN_LEFT, 18, 0},
            {"Target Name", NMO_CLI_ALIGN_LEFT, 25, 0},
        };
        static const nmo_cli_table_col_t in_cols[] = {
            {"Source", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
            {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
            {"Source Class", NMO_CLI_ALIGN_LEFT, 18, 0},
            {"Source Name", NMO_CLI_ALIGN_LEFT, 25, 0},
        };

        nmo_cli_table_t out_table;
        nmo_cli_table_init(&out_table, out_cols,
                           sizeof(out_cols) / sizeof(out_cols[0]));
        nmo_cli_table_t in_table;
        nmo_cli_table_init(&in_table, in_cols,
                           sizeof(in_cols) / sizeof(in_cols[0]));

        cli_refs_text_data_t td = {
            .out_table = &out_table,
            .in_table = &in_table,
        };

        nmo_core_ref_result_t ref_result = {0};
        nmo_core_iter_refs(&c, object_id, NMO_CORE_REFS_BOTH,
                           cli_refs_text_visitor, &td, &ref_result);

        /* Outgoing references */
        fprintf(c.out, "Outgoing references (%zu):\n", ref_result.outgoing);
        if (ref_result.outgoing == 0) {
            fprintf(c.out, "  (none)\n");
        } else {
            nmo_cli_table_print(&out_table, c.out, c.colorize);
        }
        nmo_cli_table_free(&out_table);

        fprintf(c.out, "\n");

        /* Incoming references */
        fprintf(c.out, "Incoming references (%zu):\n", ref_result.incoming);
        if (ref_result.incoming == 0) {
            fprintf(c.out, "  (none)\n");
        } else {
            nmo_cli_table_print(&in_table, c.out, c.colorize);
        }
        nmo_cli_table_free(&in_table);
    }

    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS) : NMO_CLI_EXIT_SUCCESS;
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
    char obj_cbuf[32];
    const char *obj_class = nmo_core_class_name_or(&c, obj_cid, obj_cbuf, sizeof(obj_cbuf));

    /* Get reference graph from session cache */
    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(c.session);
    if (!graph) {
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Arena for cascade preview */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

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

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Target info */
        yyjson_mut_val *target = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, target, "id", object_id);
        if (obj_name && obj_name[0])
            nmo_cli_json_add_str_safe(doc, target, "name", obj_name);
        yyjson_mut_obj_add_str(doc, target, "class_name", obj_class);
        yyjson_mut_obj_add_val(doc, data, "target", target);

        /* Direct dependents */
        yyjson_mut_val *deps = yyjson_mut_arr(doc);
        for (size_t i = 0; i < in_count; ++i) {
            yyjson_mut_val *dep = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, dep, "id", in_edges[i].from);

            nmo_object_t *peer = nmo_core_find_by_id(&c, in_edges[i].from);
            if (peer) {
                const char *pname = nmo_object_get_name(peer);
                if (pname && pname[0])
                    nmo_cli_json_add_str_safe(doc, dep, "name", pname);
                char cbuf[32];
                const char *pcls = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(peer), cbuf, sizeof(cbuf));
                yyjson_mut_obj_add_str(doc, dep, "class_name", pcls);
            }
            yyjson_mut_obj_add_str(doc, dep, "ref_kind",
                                   nmo_ref_kind_name(in_edges[i].kind));
            yyjson_mut_arr_add_val(deps, dep);
        }
        yyjson_mut_obj_add_val(doc, data, "direct_dependents", deps);

        /* Cascade set */
        yyjson_mut_val *cas = yyjson_mut_arr(doc);
        for (size_t i = 0; i < cascade_count; ++i) {
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, entry, "id", cascade_ids[i]);

            nmo_object_t *cobj = nmo_core_find_by_id(&c, cascade_ids[i]);
            if (cobj) {
                const char *cname = nmo_object_get_name(cobj);
                if (cname && cname[0])
                    nmo_cli_json_add_str_safe(doc, entry, "name", cname);
                char cbuf[32];
                const char *ccls = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(cobj), cbuf, sizeof(cbuf));
                yyjson_mut_obj_add_str(doc, entry, "class_name", ccls);
            }
            yyjson_mut_arr_add_val(cas, entry);
        }
        yyjson_mut_obj_add_val(doc, data, "cascade_set", cas);
        yyjson_mut_obj_add_uint(doc, data, "cascade_count",
                                (uint64_t)cascade_count);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.impact");
    } else {
        /* Text output */
        fprintf(c.out, "Impact Analysis: Object #%u", object_id);
        if (obj_name && obj_name[0])
            fprintf(c.out, " \"%s\"", obj_name);
        fprintf(c.out, " (%s)\n\n", obj_class);

        /* Direct dependents */
        fprintf(c.out, "Direct dependents (%zu):\n", in_count);
        if (in_count == 0) {
            fprintf(c.out, "  (none)\n");
        } else {
            static const nmo_cli_table_col_t dep_cols[] = {
                {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
                {"Kind",  NMO_CLI_ALIGN_LEFT, 15, 0},
            };
            nmo_cli_table_t dep_table;
            nmo_cli_table_init(&dep_table, dep_cols,
                               sizeof(dep_cols) / sizeof(dep_cols[0]));

            for (size_t i = 0; i < in_count; ++i) {
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", in_edges[i].from);
                const char *pcls = "-";
                const char *pname = "-";
                nmo_object_t *peer = nmo_core_find_by_id(&c, in_edges[i].from);
                char cbuf[32];
                if (peer) {
                    pcls = nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(peer), cbuf, sizeof(cbuf));
                    const char *n = nmo_object_get_name(peer);
                    if (n && n[0]) pname = n;
                }
                const char *cells[] = {
                    id_buf, pcls, pname,
                    nmo_ref_kind_name(in_edges[i].kind)
                };
                nmo_cli_table_add_row(&dep_table, cells, 4);
            }
            nmo_cli_table_print(&dep_table, c.out, c.colorize);
            nmo_cli_table_free(&dep_table);
        }

        /* Cascade set */
        fprintf(c.out, "\nCascade deletion would remove %zu object(s):\n",
                cascade_count);
        if (cascade_count == 0) {
            fprintf(c.out, "  (none)\n");
        } else {
            static const nmo_cli_table_col_t cas_cols[] = {
                {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
            };
            nmo_cli_table_t cas_table;
            nmo_cli_table_init(&cas_table, cas_cols,
                               sizeof(cas_cols) / sizeof(cas_cols[0]));

            for (size_t i = 0; i < cascade_count; ++i) {
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", cascade_ids[i]);
                const char *ccls = "-";
                const char *cname_str = "-";
                nmo_object_t *cobj = nmo_core_find_by_id(&c, cascade_ids[i]);
                char cbuf[32];
                if (cobj) {
                    ccls = nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(cobj), cbuf, sizeof(cbuf));
                    const char *n = nmo_object_get_name(cobj);
                    if (n && n[0]) cname_str = n;
                }
                const char *cells[] = { id_buf, ccls, cname_str };
                nmo_cli_table_add_row(&cas_table, cells, 3);
            }
            nmo_cli_table_print(&cas_table, c.out, c.colorize);
            nmo_cli_table_free(&cas_table);
        }
    }

    nmo_arena_destroy(arena);
    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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
        {"--class", "-c", NMO_OPT_STRING, "Filter by class name"},
    };
    enum { OPT_CLASS };
    nmo_opt_val_t vals[1];
    const char *pos_arr[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->class_filter_str = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;
    return NMO_CLI_EXIT_SUCCESS;
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
    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(c.session);
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
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
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

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total_objects",
                                (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "orphan_count",
                                (uint64_t)orphan_count);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < orphan_count; ++i) {
            nmo_object_t *o = nmo_core_find_by_id(&c, orphan_ids[i]);
            if (!o) continue;
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, entry, "id",
                                    (uint64_t)orphan_ids[i]);
            nmo_class_id_t cid = nmo_object_get_class_id(o);
            yyjson_mut_obj_add_uint(doc, entry, "class_id", (uint64_t)cid);
            char cbuf[32];
            const char *cname = nmo_core_class_name_or(&c, cid, cbuf, sizeof(cbuf));
            yyjson_mut_obj_add_str(doc, entry, "class_name", cname);
            const char *name = nmo_object_get_name(o);
            if (name && name[0])
                nmo_cli_json_add_str_safe(doc, entry, "name", name);
            yyjson_mut_arr_add_val(arr, entry);
        }
        yyjson_mut_obj_add_val(doc, data, "orphans", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.orphans");
    } else {
        fprintf(c.out, "Orphan Analysis: %zu unreachable object(s) (of %zu total)\n\n",
                orphan_count, object_count);

        if (orphan_count > 0) {
            static const nmo_cli_table_col_t cols[] = {
                {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
            };
            nmo_cli_table_t table;
            nmo_cli_table_init(&table, cols, sizeof(cols) / sizeof(cols[0]));

            for (size_t i = 0; i < orphan_count; ++i) {
                nmo_object_t *o = nmo_core_find_by_id(&c, orphan_ids[i]);
                if (!o) continue;
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", orphan_ids[i]);
                char cbuf[32];
                const char *cname = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(o), cbuf, sizeof(cbuf));
                const char *name = nmo_object_get_name(o);
                const char *name_str = (name && name[0]) ? name : "-";
                const char *cells[] = { id_buf, cname, name_str };
                nmo_cli_table_add_row(&table, cells, 3);
            }
            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }
    }

    nmo_arena_destroy(arena);
    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, NULL, 0, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int object_cycles_run(nmo_cmd_ctx_t *ctx, bool close_ctx) {
    nmo_cmd_ctx_t c = *ctx;
    /* Get reference graph from session cache */
    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(c.session);
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
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
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

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "cycle_count",
                                (uint64_t)cycle_count);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t ci = 0; ci < cycle_count; ++ci) {
            nmo_ref_cycle_t *rec = &cycles[ci];
            yyjson_mut_val *cyc = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, cyc, "length", (uint64_t)rec->count);

            yyjson_mut_val *objs = yyjson_mut_arr(doc);
            for (size_t j = 0; j < rec->count; ++j) {
                yyjson_mut_val *entry = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, entry, "id", rec->ids[j]);
                nmo_object_t *o = nmo_core_find_by_id(&c, rec->ids[j]);
                if (o) {
                    const char *n = nmo_object_get_name(o);
                    if (n && n[0])
                        nmo_cli_json_add_str_safe(doc, entry, "name", n);
                    char cbuf[32];
                    const char *cls = nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(o), cbuf, sizeof(cbuf));
                    yyjson_mut_obj_add_str(doc, entry, "class_name", cls);
                }
                yyjson_mut_arr_add_val(objs, entry);
            }
            yyjson_mut_obj_add_val(doc, cyc, "objects", objs);

            yyjson_mut_val *kinds = yyjson_mut_arr(doc);
            for (size_t j = 0; j < rec->count; ++j) {
                yyjson_mut_arr_add_str(doc, kinds,
                                       nmo_ref_kind_name(rec->kinds[j]));
            }
            yyjson_mut_obj_add_val(doc, cyc, "ref_kinds", kinds);

            yyjson_mut_arr_add_val(arr, cyc);
        }
        yyjson_mut_obj_add_val(doc, data, "cycles", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.cycles");
    } else {
        if (cycle_count == 0) {
            fprintf(c.out, "No circular references detected.\n");
        } else {
            fprintf(c.out, "Cycle Detection: %zu cycle(s) found\n",
                    cycle_count);

            for (size_t ci = 0; ci < cycle_count; ++ci) {
                nmo_ref_cycle_t *rec = &cycles[ci];
                fprintf(c.out, "\nCycle %zu (%zu object%s):\n",
                        ci + 1, rec->count,
                        rec->count == 1 ? "" : "s");

                fprintf(c.out, "  ");
                for (size_t j = 0; j < rec->count; ++j) {
                    if (j > 0) fprintf(c.out, " -> ");
                    nmo_object_t *o = nmo_core_find_by_id(&c, rec->ids[j]);
                    const char *n = o ? nmo_object_get_name(o) : NULL;
                    char cbuf[32];
                    const char *cls = o ? nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(o), cbuf, sizeof(cbuf)) : "?";
                    fprintf(c.out, "#%u %s", rec->ids[j], cls);
                    if (n && n[0]) fprintf(c.out, " \"%s\"", n);
                }
                fprintf(c.out, " -> #%u\n", rec->ids[0]);

                fprintf(c.out, "  Reference kinds: ");
                for (size_t j = 0; j < rec->count; ++j) {
                    if (j > 0) fprintf(c.out, " -> ");
                    fprintf(c.out, "%s", nmo_ref_kind_name(rec->kinds[j]));
                }
                fprintf(c.out, "\n");
            }
        }
    }

    nmo_arena_destroy(arena);
    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (!expect_file_operand && r.pos_count != 0) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->dot_mode = vals[OPT_DOT].present && vals[OPT_DOT].val.flag;
    args->kind_str = vals[OPT_KIND].present ? vals[OPT_KIND].val.str : NULL;
    return NMO_CLI_EXIT_SUCCESS;
}

static int object_graph_run(nmo_cmd_ctx_t *ctx, const object_graph_args_t *args,
                            bool close_ctx) {
    nmo_cmd_ctx_t c = *ctx;
    /* Get reference graph from session cache */
    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(c.session);
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

    if (c.is_json) {
        /* ---- JSON output ---- */
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "node_count", (uint64_t)node_count);
        yyjson_mut_obj_add_uint(doc, data, "edge_count", (uint64_t)edge_count);

        /* Nodes */
        yyjson_mut_val *jarr_nodes = yyjson_mut_arr(doc);
        for (size_t i = 0; i < node_count; ++i) {
            yyjson_mut_val *jn = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, jn, "id", node_ids[i]);

            nmo_object_t *obj = nmo_core_find_by_id(&c, node_ids[i]);
            if (obj) {
                char cbuf[32];
                const char *cls = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(obj), cbuf, sizeof(cbuf));
                yyjson_mut_obj_add_str(doc, jn, "class_name", cls);
                const char *name = nmo_object_get_name(obj);
                if (name && name[0])
                    nmo_cli_json_add_str_safe(doc, jn, "name", name);
            }
            yyjson_mut_arr_add_val(jarr_nodes, jn);
        }
        yyjson_mut_obj_add_val(doc, data, "nodes", jarr_nodes);

        /* Edges */
        yyjson_mut_val *jarr_edges = yyjson_mut_arr(doc);
        for (size_t i = 0; i < edge_count; ++i) {
            yyjson_mut_val *je = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, je, "from", edges[i].from);
            yyjson_mut_obj_add_uint(doc, je, "to", edges[i].to);
            yyjson_mut_obj_add_str(doc, je, "kind",
                                   nmo_ref_kind_name(edges[i].kind));
            yyjson_mut_obj_add_str(doc, je, "field",
                                   edges[i].field_path ? edges[i].field_path : "unknown");
            yyjson_mut_arr_add_val(jarr_edges, je);
        }
        yyjson_mut_obj_add_val(doc, data, "edges", jarr_edges);

        /* Kind summary */
        yyjson_mut_val *jsummary = yyjson_mut_obj(doc);
        for (int k = 0; k < NMO_REF_KIND_MAX; ++k) {
            if (kind_counts[k] > 0) {
                yyjson_mut_obj_add_uint(doc, jsummary,
                    nmo_ref_kind_name((nmo_ref_kind_t)k),
                    (uint64_t)kind_counts[k]);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "kind_summary", jsummary);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.graph");
    } else if (args->dot_mode) {
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
        nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
        nmo_arena_t *dot_arena = nmo_arena_create(NULL, 0);
        nmo_ref_graph_to_dot(graph, repo, c.registry, kind_mask, dot_arena, c.out);
        nmo_arena_destroy(dot_arena);
    } else {
        /* ---- Text summary ---- */
        fprintf(c.out, "Reference Graph: %zu nodes, %zu edges\n\n",
                node_count, edge_count);

        fprintf(c.out, "Edges by kind:\n");
        bool any_kind = false;
        for (int k = 0; k < NMO_REF_KIND_MAX; ++k) {
            if (kind_counts[k] > 0) {
                fprintf(c.out, "  %-16s: %4zu\n",
                        nmo_ref_kind_name((nmo_ref_kind_t)k),
                        kind_counts[k]);
                any_kind = true;
            }
        }
        if (!any_kind)
            fprintf(c.out, "  (none)\n");
    }

    free(node_ids);
    free(filtered);
    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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
