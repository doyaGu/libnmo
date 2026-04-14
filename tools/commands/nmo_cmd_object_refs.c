/**
 * @file nmo_cmd_object_refs.c
 * @brief CLI object ref-graph commands: refs, impact, orphans, cycles
 */

#include "nmo_cmd_object.h"

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
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"

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

int nmo_cmd_object_refs(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_opt_val_t vals[1]; /* no named options currently */
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    /* Parse with empty option table to collect positional args */
    if (nmo_opt_parse(argc, argv, NULL, 0, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    /* Find the object ID among positional args (first numeric value) */
    nmo_object_id_t object_id = 0;
    for (size_t i = 0; i < r.pos_count; ++i) {
        char *endptr = NULL;
        unsigned long id = strtoul(r.pos_args[i], &endptr, 10);
        if (endptr && *endptr == '\0' && id > 0 && id <= UINT32_MAX) {
            object_id = (nmo_object_id_t)id;
            break;
        }
    }
    if (object_id == 0) {
        fprintf(stderr, "Error: No valid object ID specified\n");
        fprintf(stderr, "Usage: nmo object refs <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Find the object */
    nmo_object_t *obj = nmo_core_find_by_id(&c, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
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
        fprintf(c.out, "References for object %u: %s [%s]\n\n",
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

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object impact - Show deletion impact analysis
 * ============================================================================ */

int nmo_cmd_object_impact(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, NULL, 0, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    /* Find the object ID among positional args */
    nmo_object_id_t object_id = 0;
    for (size_t i = 0; i < r.pos_count; ++i) {
        char *endptr = NULL;
        unsigned long id = strtoul(r.pos_args[i], &endptr, 10);
        if (endptr && *endptr == '\0' && id > 0 && id <= UINT32_MAX) {
            object_id = (nmo_object_id_t)id;
            break;
        }
    }
    if (object_id == 0) {
        fprintf(stderr, "Error: No valid object ID specified\n");
        fprintf(stderr, "Usage: nmo object impact <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Find the target object */
    nmo_object_t *obj = nmo_core_find_by_id(&c, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *obj_name = nmo_object_get_name(obj);
    nmo_class_id_t obj_cid = nmo_object_get_class_id(obj);
    char obj_cbuf[32];
    const char *obj_class = nmo_core_class_name_or(&c, obj_cid, obj_cbuf, sizeof(obj_cbuf));

    /* Build ref graph for direct dependents */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, c.registry, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

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

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object orphans - Find unreachable objects
 * ============================================================================ */

/** Binary search in sorted ID array */
static bool orphan_id_in_set(const nmo_object_id_t *arr, size_t count,
                             nmo_object_id_t id) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < id) lo = mid + 1;
        else if (arr[mid] > id) hi = mid;
        else return true;
    }
    return false;
}

int nmo_cmd_object_orphans(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class", "-c", NMO_OPT_STRING, "Filter by class name"},
    };
    enum { OPT_CLASS };
    nmo_opt_val_t vals[1];
    const char *pos_arr[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Resolve optional class filter */
    nmo_class_id_t filter_cid = 0;
    if (class_filter_str) {
        filter_cid = nmo_core_class_id(&c, class_filter_str);
        if (!filter_cid) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    /* Get objects */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Build reference graph */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, c.registry, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Collect root IDs using tiered strategy (same as validate orphans) */
    nmo_object_id_t *root_ids = NULL;
    size_t root_count = 0;
    if (object_count > 0) {
        root_ids = (nmo_object_id_t *)nmo_arena_alloc(arena,
            object_count * sizeof(nmo_object_id_t),
            _Alignof(nmo_object_id_t));
        if (!root_ids) {
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: Allocation failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Tier 1: CKLevel / CKScene */
        for (size_t i = 0; i < object_count; ++i) {
            nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
            if (cid == NMO_CID_LEVEL || cid == NMO_CID_SCENE ||
                nmo_type_registry_is_class_derived_from(c.registry, cid, NMO_CID_LEVEL) ||
                nmo_type_registry_is_class_derived_from(c.registry, cid, NMO_CID_SCENE)) {
                root_ids[root_count++] = nmo_object_get_id(objects[i]);
            }
        }

        /* Tier 2: CKGroup */
        if (root_count == 0) {
            for (size_t i = 0; i < object_count; ++i) {
                nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
                if (cid == NMO_CID_GROUP ||
                    nmo_type_registry_is_class_derived_from(c.registry, cid, NMO_CID_GROUP)) {
                    root_ids[root_count++] = nmo_object_get_id(objects[i]);
                }
            }
        }

        /* Tier 3: CK3dEntity / CK3dObject */
        if (root_count == 0) {
            for (size_t i = 0; i < object_count; ++i) {
                nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
                if (cid == NMO_CID_3DENTITY || cid == NMO_CID_3DOBJECT ||
                    nmo_type_registry_is_class_derived_from(c.registry, cid, NMO_CID_3DENTITY)) {
                    root_ids[root_count++] = nmo_object_get_id(objects[i]);
                }
            }
        }

        /* Tier 4: all objects with zero incoming references */
        if (root_count == 0) {
            for (size_t i = 0; i < object_count; ++i) {
                nmo_object_id_t oid = nmo_object_get_id(objects[i]);
                nmo_ref_edge_t *edges = NULL;
                size_t ecount = 0;
                nmo_ref_graph_get_object_edges(graph, oid, NMO_REF_DIR_INCOMING,
                                               &edges, &ecount);
                if (ecount == 0) {
                    root_ids[root_count++] = oid;
                }
            }
        }
    }

    /* Mark reachable set */
    nmo_object_id_t *reachable_ids = NULL;
    size_t reachable_count = 0;
    {
        nmo_status_t ms = nmo_ref_graph_mark_reachable(
            graph, root_ids, root_count, arena,
            &reachable_ids, &reachable_count);
        if (ms != NMO_OK) {
            nmo_ref_graph_destroy(graph);
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: mark_reachable failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
    }

    /* Collect orphans */
    typedef struct {
        nmo_object_t *obj;
    } orphan_entry_t;

    orphan_entry_t *orphan_list = NULL;
    size_t orphan_count = 0;
    if (object_count > 0) {
        orphan_list = (orphan_entry_t *)nmo_arena_alloc(arena,
            object_count * sizeof(orphan_entry_t),
            _Alignof(orphan_entry_t));
        if (!orphan_list) {
            nmo_ref_graph_destroy(graph);
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: Allocation failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *o = objects[i];
        nmo_object_id_t oid = nmo_object_get_id(o);

        /* Skip reachable */
        if (orphan_id_in_set(reachable_ids, reachable_count, oid))
            continue;

        /* Apply class filter */
        if (filter_cid != 0) {
            nmo_class_id_t cid = nmo_object_get_class_id(o);
            if (cid != filter_cid &&
                !nmo_type_registry_is_class_derived_from(c.registry, cid, filter_cid))
                continue;
        }

        orphan_list[orphan_count].obj = o;
        orphan_count++;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total_objects",
                                (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "root_count",
                                (uint64_t)root_count);

        /* Roots */
        yyjson_mut_val *roots = yyjson_mut_arr(doc);
        for (size_t i = 0; i < root_count; ++i) {
            yyjson_mut_val *rentry = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, rentry, "id", root_ids[i]);
            nmo_object_t *robj = nmo_core_find_by_id(&c, root_ids[i]);
            if (robj) {
                char cbuf[32];
                const char *rcls = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(robj), cbuf, sizeof(cbuf));
                yyjson_mut_obj_add_str(doc, rentry, "class_name", rcls);
            }
            yyjson_mut_arr_add_val(roots, rentry);
        }
        yyjson_mut_obj_add_val(doc, data, "roots", roots);

        yyjson_mut_obj_add_uint(doc, data, "orphan_count",
                                (uint64_t)orphan_count);

        /* Orphan list */
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < orphan_count; ++i) {
            nmo_object_t *o = orphan_list[i].obj;
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, entry, "id",
                                    (uint64_t)nmo_object_get_id(o));
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
        /* Text output */
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
                nmo_object_t *o = orphan_list[i].obj;
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(o));
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

        /* Root summary */
        fprintf(c.out, "\nRoot objects used: %zu", root_count);
        if (root_count > 0 && root_count <= 10) {
            fprintf(c.out, " (");
            for (size_t i = 0; i < root_count; ++i) {
                if (i > 0) fprintf(c.out, ", ");
                nmo_object_t *robj = nmo_core_find_by_id(&c, root_ids[i]);
                char cbuf[32];
                const char *rcls = robj ? nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(robj), cbuf, sizeof(cbuf)) : "?";
                fprintf(c.out, "%s #%u", rcls, root_ids[i]);
            }
            fprintf(c.out, ")");
        }
        fprintf(c.out, "\n");
    }

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object cycles - Detect circular references
 * ============================================================================ */

/** Cycle record: array of object IDs forming the cycle */
typedef struct {
    nmo_object_id_t *ids;
    nmo_ref_kind_t *kinds;    /**< ref kinds along edges (length == count) */
    size_t count;
} cycle_record_t;

/** DFS state for cycle detection */
typedef struct {
    nmo_cmd_ctx_t *c;
    nmo_ref_graph_t *graph;
    uint8_t *color;           /**< 0=WHITE, 1=GRAY, 2=BLACK */
    nmo_object_id_t *stack;   /**< DFS path stack */
    nmo_ref_kind_t *stack_kinds; /**< ref kind for each stack entry */
    size_t stack_size;

    cycle_record_t *cycles;
    size_t cycle_count;
    size_t cycle_cap;

    nmo_arena_t *arena;
    nmo_object_id_t max_id;
} cycle_dfs_state_t;

static void cycle_dfs_record(cycle_dfs_state_t *st, nmo_object_id_t back_target,
                             nmo_ref_kind_t back_kind) {
    /* Find back_target in the stack to extract the cycle */
    size_t start = 0;
    bool found = false;
    for (size_t i = 0; i < st->stack_size; ++i) {
        if (st->stack[i] == back_target) {
            start = i;
            found = true;
            break;
        }
    }
    if (!found) return;

    size_t len = st->stack_size - start;
    /* Normalize: rotate so minimum ID is first (for dedup across rotations) */
    size_t min_pos = 0;
    for (size_t j = 1; j < len; ++j) {
        if (st->stack[start + j] < st->stack[start + min_pos])
            min_pos = j;
    }

    /* Deduplicate: check if we already have this cycle (rotation-normalized) */
    for (size_t ci = 0; ci < st->cycle_count; ++ci) {
        if (st->cycles[ci].count == len) {
            bool same = true;
            for (size_t j = 0; j < len; ++j) {
                if (st->cycles[ci].ids[j] != st->stack[start + ((min_pos + j) % len)]) {
                    same = false;
                    break;
                }
            }
            if (same) return; /* already recorded */
        }
    }

    /* Grow cycle array if needed */
    if (st->cycle_count >= st->cycle_cap) {
        size_t new_cap = st->cycle_cap ? st->cycle_cap * 2 : 16;
        cycle_record_t *tmp = (cycle_record_t *)realloc(
            st->cycles, new_cap * sizeof(cycle_record_t));
        if (!tmp) return;
        st->cycles = tmp;
        st->cycle_cap = new_cap;
    }

    nmo_object_id_t *ids = (nmo_object_id_t *)nmo_arena_alloc(
        st->arena, len * sizeof(nmo_object_id_t),
        _Alignof(nmo_object_id_t));
    nmo_ref_kind_t *kinds = (nmo_ref_kind_t *)nmo_arena_alloc(
        st->arena, len * sizeof(nmo_ref_kind_t),
        _Alignof(nmo_ref_kind_t));
    if (!ids || !kinds) return;

    for (size_t j = 0; j < len; ++j) {
        size_t src_j = (min_pos + j) % len;
        size_t next_j = (min_pos + j + 1) % len;
        ids[j] = st->stack[start + src_j];
        kinds[j] = (j + 1 < len) ? st->stack_kinds[start + next_j] : back_kind;
    }

    cycle_record_t *rec = &st->cycles[st->cycle_count++];
    rec->ids = ids;
    rec->kinds = kinds;
    rec->count = len;
}

/* Iterative DFS frame — avoids C stack overflow on deep graphs */
typedef struct {
    nmo_object_id_t id;
    nmo_ref_kind_t entry_kind;
    nmo_ref_edge_t *edges;
    size_t ecount;
    size_t edge_idx;  /* next edge to process */
} dfs_frame_t;

static void cycle_dfs_visit(cycle_dfs_state_t *st, nmo_object_id_t start_id,
                            nmo_ref_kind_t start_kind) {
    size_t frame_cap = st->max_id < 4096 ? 4096 : (size_t)(st->max_id + 1);
    dfs_frame_t *frames = (dfs_frame_t *)malloc(frame_cap * sizeof(dfs_frame_t));
    if (!frames) return;
    size_t frame_top = 0;

    /* Push initial frame */
    if (start_id > st->max_id) { free(frames); return; }
    st->color[start_id] = 1; /* GRAY */
    st->stack[st->stack_size] = start_id;
    st->stack_kinds[st->stack_size] = start_kind;
    st->stack_size++;

    nmo_ref_edge_t *edges = NULL;
    size_t ecount = 0;
    nmo_ref_graph_get_object_edges(st->graph, start_id, NMO_REF_DIR_OUTGOING,
                                   &edges, &ecount);
    frames[frame_top].id = start_id;
    frames[frame_top].entry_kind = start_kind;
    frames[frame_top].edges = edges;
    frames[frame_top].ecount = ecount;
    frames[frame_top].edge_idx = 0;
    frame_top++;

    while (frame_top > 0) {
        dfs_frame_t *f = &frames[frame_top - 1];

        if (f->edge_idx >= f->ecount) {
            /* All edges processed — pop frame, mark BLACK */
            st->stack_size--;
            st->color[f->id] = 2; /* BLACK */
            frame_top--;
            continue;
        }

        nmo_ref_edge_t *edge = &f->edges[f->edge_idx++];
        nmo_object_id_t target = edge->to;
        if (target > st->max_id) continue;

        if (st->color[target] == 1) {
            /* Back edge → cycle */
            cycle_dfs_record(st, target, edge->kind);
        } else if (st->color[target] == 0) {
            /* Unvisited → push new frame */
            if (frame_top >= frame_cap || st->stack_size >= frame_cap) continue;

            st->color[target] = 1; /* GRAY */
            st->stack[st->stack_size] = target;
            st->stack_kinds[st->stack_size] = edge->kind;
            st->stack_size++;

            nmo_ref_edge_t *tedges = NULL;
            size_t tecount = 0;
            nmo_ref_graph_get_object_edges(st->graph, target, NMO_REF_DIR_OUTGOING,
                                           &tedges, &tecount);
            frames[frame_top].id = target;
            frames[frame_top].entry_kind = edge->kind;
            frames[frame_top].edges = tedges;
            frames[frame_top].ecount = tecount;
            frames[frame_top].edge_idx = 0;
            frame_top++;
        }
    }

    free(frames);
}

int nmo_cmd_object_cycles(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, NULL, 0, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get objects to find max_id */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_id_t max_id = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t oid = nmo_object_get_id(objects[i]);
        if (oid > max_id) max_id = oid;
    }

    /* Build reference graph */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, c.registry, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Allocate DFS state */
    size_t color_size = (size_t)(max_id + 1);
    uint8_t *color = (uint8_t *)calloc(color_size, sizeof(uint8_t));
    nmo_object_id_t *stack = (nmo_object_id_t *)malloc(
        object_count * sizeof(nmo_object_id_t));
    nmo_ref_kind_t *stack_kinds = (nmo_ref_kind_t *)malloc(
        object_count * sizeof(nmo_ref_kind_t));

    if (!color || !stack || !stack_kinds) {
        free(color);
        free(stack);
        free(stack_kinds);
        nmo_ref_graph_destroy(graph);
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Allocation failed\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    cycle_dfs_state_t st;
    memset(&st, 0, sizeof(st));
    st.c = &c;
    st.graph = graph;
    st.color = color;
    st.stack = stack;
    st.stack_kinds = stack_kinds;
    st.stack_size = 0;
    st.cycles = NULL;
    st.cycle_count = 0;
    st.cycle_cap = 0;
    st.arena = arena;
    st.max_id = max_id;

    /* Run DFS from each unvisited object */
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t oid = nmo_object_get_id(objects[i]);
        if (oid <= max_id && color[oid] == 0) {
            cycle_dfs_visit(&st, oid, NMO_REF_KIND_UNKNOWN);
        }
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "cycle_count",
                                (uint64_t)st.cycle_count);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t ci = 0; ci < st.cycle_count; ++ci) {
            cycle_record_t *rec = &st.cycles[ci];
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
        if (st.cycle_count == 0) {
            fprintf(c.out, "No circular references detected.\n");
        } else {
            fprintf(c.out, "Cycle Detection: %zu cycle(s) found\n",
                    st.cycle_count);

            for (size_t ci = 0; ci < st.cycle_count; ++ci) {
                cycle_record_t *rec = &st.cycles[ci];
                fprintf(c.out, "\nCycle %zu (%zu object%s):\n",
                        ci + 1, rec->count,
                        rec->count == 1 ? "" : "s");

                /* Print chain: #A -> #B -> ... -> #A */
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

                /* Print reference kinds */
                fprintf(c.out, "  Reference kinds: ");
                for (size_t j = 0; j < rec->count; ++j) {
                    if (j > 0) fprintf(c.out, " -> ");
                    fprintf(c.out, "%s", nmo_ref_kind_name(rec->kinds[j]));
                }
                fprintf(c.out, "\n");
            }
        }
    }

    free(color);
    free(stack);
    free(stack_kinds);
    free(st.cycles);
    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
