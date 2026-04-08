/**
 * @file nmo_cmd_validate.c
 * @brief CLI validate command group implementation
 *
 * Phase 4 - Reference graph and validation rules
 */

#include "nmo_cmd_validate.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_inspector.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "session/nmo_ref_graph.h"

#include <stdio.h>
#include <string.h>

/**
 * Check if --fix or --suggest-fixes flag is present.
 */
static bool parse_fix_flag(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--fix") == 0 || strcmp(argv[i], "--suggest-fixes") == 0) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * validate all (single-file core + batch support)
 * ============================================================================ */

/**
 * Core validation logic for a single file.
 * When doc/data are non-NULL (JSON batch mode), populates data.
 * When NULL (text mode), prints directly to stdout.
 */
static int validate_all_single(const char *file_path,
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

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t error_count = 0;
    size_t warning_count = 0;
    FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
    bool colorize = (text_ctx != NULL) ? text_ctx->colorize : nmo_cli_should_colorize(global, out);

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);

        if (!chunk) {
            warning_count++;
            if (!doc && global->verbosity > 0) {
                fprintf(out, "Warning: Object %u has no chunk\n", nmo_object_get_id(obj));
            }
            continue;
        }

        nmo_chunk_validation_t result;
        nmo_status_t rc = nmo_inspector_validate_chunk(chunk, &result);
        if (rc != NMO_OK || !result.is_valid) {
            error_count++;
            if (!doc) {
                fprintf(out, "Error: Object %u chunk validation failed: %s\n",
                        nmo_object_get_id(obj),
                        result.error_message[0] ? result.error_message : "unknown");
            }
        }
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (error_count > 0) {
        exit_code = global->strict_mode ? NMO_CLI_EXIT_STRICT_FAILURE : NMO_CLI_EXIT_SUCCESS;
    }
    if (warning_count > 0 && global->fail_on_warning) {
        exit_code = NMO_CLI_EXIT_WARNING;
    }

    if (doc && data) {
        /* JSON batch mode: populate data object */
        yyjson_mut_obj_add_bool(doc, data, "valid", error_count == 0);
        yyjson_mut_obj_add_uint(doc, data, "error_count", (uint64_t)error_count);
        yyjson_mut_obj_add_uint(doc, data, "warning_count", (uint64_t)warning_count);
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)object_count);
    } else {
        /* Text mode: output summary */
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", object_count);
        nmo_cli_print_kv(out, "Objects", buf, 12, colorize);
        snprintf(buf, sizeof(buf), "%zu", error_count);
        nmo_cli_print_kv(out, "Errors", buf, 12, colorize);
        snprintf(buf, sizeof(buf), "%zu", warning_count);
        nmo_cli_print_kv(out, "Warnings", buf, 12, colorize);
        fprintf(out, "Result: %s\n", error_count == 0 ? "VALID" : "INVALID");
    }

    nmo_tool_close_session(ctx, session);
    return exit_code;
}

int nmo_cmd_validate_all(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Batch mode: process multiple files */
    if (global->batch_mode) {
        const char *paths[64];
        size_t count = nmo_tool_find_file_args(argc, argv, paths, 64);
        if (count == 0) {
            fprintf(stderr, "Error: No files specified\n");
            fprintf(stderr, "Usage: nmo --batch validate all <file1> <file2> ...\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return nmo_tool_batch_run(paths, count, global, "validate.all",
                                   validate_all_single, NULL);
    }

    /* Single file mode - validate_all_single opens its own session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    const char *file_path = nmo_tool_find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo validate all <file>\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (c.is_json) {
        /* Single-file JSON: use the batch handler to populate, then wrap */
        yyjson_mut_doc *doc = NULL;
        yyjson_mut_val *data = NULL;
        if (!nmo_cli_json_create_data_doc(&doc, &data)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        rc = validate_all_single(file_path, global, NULL, doc, data);

        yyjson_mut_obj_add_str(doc, data, "file", file_path);
        nmo_cli_json_write_enveloped_and_free(doc, data, "validate.all", file_path,
                                              c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Single-file text mode */
    nmo_tool_text_output_ctx_t text_ctx = {
        .out = c.out,
        .colorize = c.colorize,
        .user_data = NULL
    };
    nmo_cli_print_heading(c.out, "Validation Results", c.colorize);
    nmo_cli_print_kv(c.out, "File", file_path, 12, c.colorize);
    fprintf(c.out, "\n");

    rc = validate_all_single(file_path, global, &text_ctx, NULL, NULL);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * validate structure
 * ============================================================================ */

int nmo_cmd_validate_structure(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    bool suggest_fixes = parse_fix_flag(argc, argv);

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    size_t error_count = 0;
    size_t warning_count = 0;
    size_t checked_count = 0;

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *data = NULL;
    yyjson_mut_val *issues = NULL;

    if (c.is_json) {
        doc = nmo_cmd_ctx_json_begin(&c);
        data = yyjson_mut_obj(doc);
        issues = yyjson_mut_arr(doc);
    } else {
        nmo_cli_print_heading(c.out, "Structure Validation", c.colorize);
        nmo_cli_print_kv(c.out, "File", c.file_path, 14, c.colorize);
        fprintf(c.out, "\n");
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        nmo_object_id_t obj_id = nmo_object_get_id(obj);

        if (!chunk) {
            warning_count++;
            if (c.is_json) {
                yyjson_mut_val *issue = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, issue, "severity", "warning");
                yyjson_mut_obj_add_uint(doc, issue, "object_id", obj_id);
                yyjson_mut_obj_add_uint(doc, issue, "class_id", nmo_object_get_class_id(obj));
                const char *class_name = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
                if (class_name) {
                    yyjson_mut_obj_add_str(doc, issue, "class_name", class_name);
                }
                yyjson_mut_obj_add_str(doc, issue, "message", "missing chunk");
                if (suggest_fixes) {
                    yyjson_mut_obj_add_str(doc, issue, "fix", "re-save file to regenerate chunks");
                }
                yyjson_mut_arr_add_val(issues, issue);
            } else if (global->verbosity > 0) {
                fprintf(c.out, "Warning: Object %u has no chunk\n", obj_id);
                if (suggest_fixes) {
                    fprintf(c.out, "  Fix: Re-save file to regenerate chunks\n");
                }
            }
            continue;
        }

        checked_count++;

        /* -vv: Show per-object chunk details */
        if (!c.is_json && global->verbosity >= 2) {
            size_t ds = 0;
            (void)nmo_chunk_get_data(chunk, &ds);
            fprintf(c.out, "  Object %u: chunk %zu bytes\n", obj_id, ds);
        }

        nmo_chunk_validation_t result;
        nmo_status_t vrc = nmo_inspector_validate_chunk(chunk, &result);
        if (vrc != NMO_OK || !result.is_valid) {
            error_count++;
            if (c.is_json) {
                yyjson_mut_val *issue = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, issue, "severity", "error");
                yyjson_mut_obj_add_uint(doc, issue, "object_id", obj_id);
                yyjson_mut_obj_add_uint(doc, issue, "class_id", nmo_object_get_class_id(obj));
                const char *class_name = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
                if (class_name) {
                    yyjson_mut_obj_add_str(doc, issue, "class_name", class_name);
                }
                yyjson_mut_obj_add_str(doc, issue, "message",
                                       result.error_message[0] ? result.error_message : "validation failed");
                if (suggest_fixes) {
                    yyjson_mut_obj_add_str(doc, issue, "fix",
                                           "re-save with nmo convert to regenerate chunk data");
                }
                yyjson_mut_arr_add_val(issues, issue);
            } else {
                fprintf(c.out, "Error: Object %u chunk invalid: %s\n",
                        obj_id,
                        result.error_message[0] ? result.error_message : "validation failed");
                if (suggest_fixes) {
                    fprintf(c.out, "  Fix: Re-save with 'nmo convert' to regenerate chunk data\n");
                }
            }
        }
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (error_count > 0 && global->strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }
    if (warning_count > 0 && global->fail_on_warning) {
        exit_code = NMO_CLI_EXIT_WARNING;
    }

    if (c.is_json) {
        yyjson_mut_obj_add_str(doc, data, "file", c.file_path);
        yyjson_mut_obj_add_bool(doc, data, "valid", error_count == 0);
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "checked_chunks", (uint64_t)checked_count);
        yyjson_mut_obj_add_uint(doc, data, "error_count", (uint64_t)error_count);
        yyjson_mut_obj_add_uint(doc, data, "warning_count", (uint64_t)warning_count);
        yyjson_mut_obj_add_val(doc, data, "issues", issues);

        nmo_cmd_ctx_json_end(&c, doc, data, "validate.structure");
    } else {
        fprintf(c.out, "\nSummary:\n");
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", object_count);
        nmo_cli_print_kv(c.out, "Objects", buf, 14, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", checked_count);
        nmo_cli_print_kv(c.out, "Chunks", buf, 14, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", error_count);
        nmo_cli_print_kv(c.out, "Errors", buf, 14, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", warning_count);
        nmo_cli_print_kv(c.out, "Warnings", buf, 14, c.colorize);
    }

    return nmo_cmd_ctx_done(&c, exit_code);
}

/* ============================================================================
 * validate references
 * ============================================================================ */

int nmo_cmd_validate_references(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    bool suggest_fixes = parse_fix_flag(argc, argv);

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Create reference graph */
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

    /* Validate references */
    nmo_ref_edge_t *broken_edges = NULL;
    size_t broken_count = 0;
    nmo_status_t status = nmo_ref_graph_validate(graph, &broken_edges, &broken_count);

    /* Get stats */
    nmo_ref_graph_stats_t stats;
    nmo_ref_graph_get_stats(graph, &stats);

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (status != NMO_OK && global->strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Summary stats */
        yyjson_mut_obj_add_uint(doc, data, "total_references", (uint64_t)stats.total_edges);
        yyjson_mut_obj_add_uint(doc, data, "broken_count", (uint64_t)broken_count);
        yyjson_mut_obj_add_uint(doc, data, "self_refs", (uint64_t)stats.self_refs);
        yyjson_mut_obj_add_bool(doc, data, "valid", status == NMO_OK);

        /* Stats by kind */
        yyjson_mut_val *by_kind = yyjson_mut_obj(doc);
        for (int i = 0; i < NMO_REF_MAX; ++i) {
            if (stats.edge_counts[i] > 0) {
                yyjson_mut_obj_add_uint(doc, by_kind, nmo_ref_kind_name((nmo_ref_kind_t)i),
                                        (uint64_t)stats.edge_counts[i]);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "by_kind", by_kind);

        /* Broken references list */
        if (broken_count > 0) {
            yyjson_mut_val *broken_arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < broken_count; ++i) {
                yyjson_mut_val *edge = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, edge, "source_id", broken_edges[i].from);
                yyjson_mut_obj_add_uint(doc, edge, "target_id", broken_edges[i].to);
                yyjson_mut_obj_add_str(doc, edge, "kind", nmo_ref_kind_name(broken_edges[i].kind));
                yyjson_mut_obj_add_str(doc, edge, "field",
                                       broken_edges[i].field_path ? broken_edges[i].field_path : "unknown");
                if (broken_edges[i].index > 0) {
                    yyjson_mut_obj_add_uint(doc, edge, "index", broken_edges[i].index);
                }

                /* Add source object info */
                nmo_object_t *source = nmo_object_repository_find_by_id(repo, broken_edges[i].from);
                if (source) {
                    const char *source_class = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(source));
                    if (source_class) {
                        yyjson_mut_obj_add_str(doc, edge, "source_class", source_class);
                    }
                    const char *source_name = nmo_object_get_name(source);
                    if (source_name && source_name[0]) {
                        nmo_cli_json_add_str_safe(doc, edge, "source_name", source_name);
                    }
                }

                if (suggest_fixes) {
                    yyjson_mut_obj_add_str(doc, edge, "fix",
                                           "null the reference field or re-save to strip dangling refs");
                }
                yyjson_mut_arr_add_val(broken_arr, edge);
            }
            yyjson_mut_obj_add_val(doc, data, "broken_references", broken_arr);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "validate.references");
    } else {
        /* Text output */
        nmo_cli_print_heading(c.out, "Reference Validation", c.colorize);
        nmo_cli_print_kv(c.out, "File", c.file_path, 16, c.colorize);
        fprintf(c.out, "\n");

        /* Summary */
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", stats.total_edges);
        nmo_cli_print_kv(c.out, "Total references", buf, 16, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.self_refs);
        nmo_cli_print_kv(c.out, "Self-references", buf, 16, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", broken_count);
        nmo_cli_print_kv(c.out, "Broken references", buf, 16, c.colorize);
        fprintf(c.out, "\n");

        /* Status */
        if (status == NMO_OK) {
            fprintf(c.out, "All references valid\n");
        } else {
            fprintf(c.out, "Broken references found\n\n");

            /* List broken references */
            static const nmo_cli_table_col_t cols[] = {
                {"Source", NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"Target", NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
                {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
                {"Source Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Source Name", NMO_CLI_ALIGN_LEFT, 20, 0},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, cols, sizeof(cols) / sizeof(cols[0]));

            for (size_t i = 0; i < broken_count; ++i) {
                char from_buf[16], to_buf[16];
                snprintf(from_buf, sizeof(from_buf), "%u", broken_edges[i].from);
                snprintf(to_buf, sizeof(to_buf), "%u", broken_edges[i].to);

                char field_buf[32];
                const char *field_name = broken_edges[i].field_path ? broken_edges[i].field_path : "unknown";
                if (broken_edges[i].index > 0) {
                    snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                             field_name, broken_edges[i].index);
                } else {
                    snprintf(field_buf, sizeof(field_buf), "%s", field_name);
                }

                nmo_object_t *source = nmo_object_repository_find_by_id(repo, broken_edges[i].from);
                const char *source_class = "-";
                const char *source_name = "-";

                if (source) {
                    const char *sc = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(source));
                    if (sc) source_class = sc;
                    const char *sn = nmo_object_get_name(source);
                    if (sn && sn[0]) source_name = sn;
                }

                const char *cells[] = {
                    from_buf,
                    to_buf,
                    nmo_ref_kind_name(broken_edges[i].kind),
                    field_buf,
                    source_class,
                    source_name
                };
                nmo_cli_table_add_row(&table, cells, 6);
            }

            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);

            if (suggest_fixes) {
                fprintf(c.out, "\nSuggested fixes:\n");
                fprintf(c.out, "  - Re-save file with 'nmo convert' to strip dangling references\n");
                fprintf(c.out, "  - Or null specific reference fields via DSL script mode\n");
            }
        }
    }

    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, exit_code);
}

/* ============================================================================
 * validate resources
 * ============================================================================ */

int nmo_cmd_validate_resources(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(c.session);

    size_t error_count = 0;
    size_t warning_count = 0;

    if (diag) {
        error_count = diag->missing_count;
        warning_count = diag->outdated_count;
        if (!diag->extension_registry_available) {
            warning_count += 1;
        }
    }

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (error_count > 0 && global->strict_mode) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }
    if (warning_count > 0 && global->fail_on_warning) {
        exit_code = NMO_CLI_EXIT_WARNING;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c.file_path);
        yyjson_mut_obj_add_bool(doc, data, "registry_available", diag ? diag->extension_registry_available : false);
        yyjson_mut_obj_add_uint(doc, data, "missing_count", (uint64_t)(diag ? diag->missing_count : 0));
        yyjson_mut_obj_add_uint(doc, data, "outdated_count", (uint64_t)(diag ? diag->outdated_count : 0));
        yyjson_mut_obj_add_uint(doc, data, "entry_count", (uint64_t)(diag ? diag->entry_count : 0));
        yyjson_mut_obj_add_uint(doc, data, "error_count", (uint64_t)error_count);
        yyjson_mut_obj_add_uint(doc, data, "warning_count", (uint64_t)warning_count);

        yyjson_mut_val *entries = yyjson_mut_arr(doc);
        if (diag && diag->entries) {
            for (size_t i = 0; i < diag->entry_count; ++i) {
                const nmo_session_plugin_dependency_status_t *e = &diag->entries[i];
                yyjson_mut_val *entry = yyjson_mut_obj(doc);

                char guid_buf[64];
                nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                yyjson_mut_obj_add_str(doc, entry, "guid", guid_buf);
                yyjson_mut_obj_add_uint(doc, entry, "category", (uint64_t)e->category);
                yyjson_mut_obj_add_uint(doc, entry, "required_version", e->required_version);
                yyjson_mut_obj_add_uint(doc, entry, "resolved_version", e->resolved_version);
                if (e->resolved_name) {
                    yyjson_mut_obj_add_str(doc, entry, "name", e->resolved_name);
                }
                yyjson_mut_obj_add_uint(doc, entry, "status_flags", e->status_flags);

                yyjson_mut_val *status = yyjson_mut_arr(doc);
                if (e->status_flags & NMO_SESSION_PLUGIN_DEP_STATUS_MISSING) {
                    yyjson_mut_arr_add_str(doc, status, "missing");
                }
                if (e->status_flags & NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) {
                    yyjson_mut_arr_add_str(doc, status, "outdated");
                }
                if (e->status_flags & NMO_SESSION_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE) {
                    yyjson_mut_arr_add_str(doc, status, "manager_unavailable");
                }
                yyjson_mut_obj_add_val(doc, entry, "status", status);

                yyjson_mut_arr_add_val(entries, entry);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "entries", entries);

        nmo_cmd_ctx_json_end(&c, doc, data, "validate.resources");
    } else {
        nmo_cli_print_heading(c.out, "Resource Validation", c.colorize);
        nmo_cli_print_kv(c.out, "File", c.file_path, 18, c.colorize);

        if (!diag) {
            fprintf(c.out, "\nPlugin diagnostics unavailable\n");
        } else {
            fprintf(c.out, "\n");
            nmo_cli_print_kv(c.out, "Registry", diag->extension_registry_available ? "available" : "unavailable", 18, c.colorize);
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", diag->entry_count);
            nmo_cli_print_kv(c.out, "Entries", buf, 18, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->missing_count);
            nmo_cli_print_kv(c.out, "Missing", buf, 18, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->outdated_count);
            nmo_cli_print_kv(c.out, "Outdated", buf, 18, c.colorize);

            if (diag->entries && diag->entry_count > 0 && global->verbosity > 0) {
                fprintf(c.out, "\nEntries:\n");
                for (size_t i = 0; i < diag->entry_count; ++i) {
                    const nmo_session_plugin_dependency_status_t *e = &diag->entries[i];
                    char guid_buf[64];
                    nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                    fprintf(c.out, "  %s", guid_buf);
                    if (e->resolved_name) {
                        fprintf(c.out, " (%s)", e->resolved_name);
                    }
                    if (e->status_flags) {
                        fprintf(c.out, " [flags=0x%X]", e->status_flags);
                    }
                    fprintf(c.out, "\n");
                }
            }
        }
    }

    return nmo_cmd_ctx_done(&c, exit_code);
}

/* ============================================================================
 * validate orphans
 * ============================================================================ */

int nmo_cmd_validate_orphans(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class",  "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--strict", NULL,  NMO_OPT_FLAG,   "Exit code 3 if orphans found"},
    };
    enum { OPT_CLASS, OPT_STRICT };
    nmo_opt_val_t vals[2];
    const char *pos_arr[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;
    bool strict = vals[OPT_STRICT].present || global->strict_mode;

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

    /* Scan for orphans */
    size_t zero_incoming = 0;
    size_t root_count = 0;
    size_t likely_orphans = 0;
    size_t likely_orphan_size = 0;
    size_t total_filtered = 0;

    /* Per-orphan info for output */
    typedef struct {
        nmo_object_t *obj;
        size_t outgoing;
        size_t data_size;
        bool is_root;
    } orphan_info_t;

    orphan_info_t *orphan_list = NULL;
    size_t orphan_cap = 0;
    if (object_count > 0) {
        orphan_list = (orphan_info_t *)nmo_arena_alloc(arena,
            object_count * sizeof(orphan_info_t),
            _Alignof(orphan_info_t));
        if (!orphan_list) {
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: Allocation failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        orphan_cap = object_count;
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_class_id_t cid = nmo_object_get_class_id(obj);

        /* Apply class filter */
        if (filter_cid != 0) {
            if (cid != filter_cid &&
                !nmo_type_registry_is_class_derived_from(c.registry, cid, filter_cid)) {
                continue;
            }
        }
        total_filtered++;

        nmo_object_id_t obj_id = nmo_object_get_id(obj);

        /* Check incoming edges */
        nmo_ref_edge_t *in_edges = NULL;
        size_t in_count = 0;
        nmo_ref_graph_get_object_edges(graph, obj_id, NMO_REF_DIR_INCOMING,
                                       &in_edges, &in_count);

        if (in_count > 0) {
            continue;
        }

        /* Zero incoming */
        zero_incoming++;

        /* Get outgoing count */
        nmo_ref_edge_t *out_edges = NULL;
        size_t out_count = 0;
        nmo_ref_graph_get_object_edges(graph, obj_id, NMO_REF_DIR_OUTGOING,
                                       &out_edges, &out_count);

        /* Get data size */
        size_t data_size = 0;
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (chunk) {
            data_size = nmo_chunk_get_data_size(chunk);
        }

        /* Check if root (CKLevel) */
        bool is_root = (cid == NMO_CID_LEVEL) ||
                       nmo_type_registry_is_class_derived_from(c.registry, cid,
                                                               NMO_CID_LEVEL);

        if (is_root) {
            root_count++;
        } else {
            likely_orphans++;
            likely_orphan_size += data_size;
        }

        /* Store for output */
        if (orphan_cap > 0) {
            size_t idx = zero_incoming - 1;
            if (idx < orphan_cap) {
                orphan_list[idx].obj = obj;
                orphan_list[idx].outgoing = out_count;
                orphan_list[idx].data_size = data_size;
                orphan_list[idx].is_root = is_root;
            }
        }
    }

    /* Determine exit code */
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (likely_orphans > 0 && strict) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }

    double orphan_pct = (total_filtered > 0)
        ? (100.0 * (double)likely_orphans / (double)total_filtered)
        : 0.0;

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c.file_path);
        yyjson_mut_obj_add_uint(doc, data, "total_objects",
                                (uint64_t)total_filtered);
        yyjson_mut_obj_add_uint(doc, data, "zero_incoming",
                                (uint64_t)zero_incoming);
        yyjson_mut_obj_add_uint(doc, data, "root_count",
                                (uint64_t)root_count);
        yyjson_mut_obj_add_uint(doc, data, "likely_orphans",
                                (uint64_t)likely_orphans);
        yyjson_mut_obj_add_uint(doc, data, "likely_orphan_size",
                                (uint64_t)likely_orphan_size);
        yyjson_mut_obj_add_real(doc, data, "orphan_percentage", orphan_pct);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < zero_incoming && i < orphan_cap; ++i) {
            nmo_object_t *obj = orphan_list[i].obj;
            yyjson_mut_val *entry = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_uint(doc, entry, "id",
                                    (uint64_t)nmo_object_get_id(obj));

            nmo_class_id_t cid = nmo_object_get_class_id(obj);
            const char *cname = nmo_core_class_name(&c, cid);
            char cbuf[32];
            if (!cname) {
                snprintf(cbuf, sizeof(cbuf), "Class#%u", cid);
                cname = cbuf;
            }
            yyjson_mut_obj_add_str(doc, entry, "class", cname);
            yyjson_mut_obj_add_uint(doc, entry, "size",
                                    (uint64_t)orphan_list[i].data_size);
            yyjson_mut_obj_add_uint(doc, entry, "outgoing",
                                    (uint64_t)orphan_list[i].outgoing);

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, entry, "name", name);
            }

            yyjson_mut_obj_add_bool(doc, entry, "root", orphan_list[i].is_root);
            yyjson_mut_arr_add_val(arr, entry);
        }
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "validate.orphans");
    } else {
        /* Text output */
        nmo_cli_print_heading(c.out, "Orphan Detection", c.colorize);
        nmo_cli_print_kv(c.out, "File", c.file_path, 18, c.colorize);
        fprintf(c.out, "\n");

        if (zero_incoming > 0) {
            static const nmo_cli_table_col_t cols[] = {
                {"ID",       NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"CLASS",    NMO_CLI_ALIGN_LEFT, 18, 0},
                {"SIZE",     NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"OUTGOING", NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"NAME",     NMO_CLI_ALIGN_LEFT, 24, 0},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, cols, sizeof(cols) / sizeof(cols[0]));

            for (size_t i = 0; i < zero_incoming && i < orphan_cap; ++i) {
                nmo_object_t *obj = orphan_list[i].obj;
                char id_buf[16], size_buf[16], out_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u",
                         nmo_object_get_id(obj));
                snprintf(size_buf, sizeof(size_buf), "%zu",
                         orphan_list[i].data_size);
                snprintf(out_buf, sizeof(out_buf), "%zu",
                         orphan_list[i].outgoing);

                nmo_class_id_t cid = nmo_object_get_class_id(obj);
                const char *cname = nmo_core_class_name(&c, cid);
                char cbuf[32];
                if (!cname) {
                    snprintf(cbuf, sizeof(cbuf), "Class#%u", cid);
                    cname = cbuf;
                }

                const char *name = nmo_object_get_name(obj);
                char name_buf[64];
                if (orphan_list[i].is_root) {
                    if (name && name[0]) {
                        snprintf(name_buf, sizeof(name_buf), "%s [root]",
                                 name);
                    } else {
                        snprintf(name_buf, sizeof(name_buf), "[root]");
                    }
                } else {
                    if (name && name[0]) {
                        snprintf(name_buf, sizeof(name_buf), "%s", name);
                    } else {
                        name_buf[0] = '-';
                        name_buf[1] = '\0';
                    }
                }

                const char *cells[] = {
                    id_buf, cname, size_buf, out_buf, name_buf
                };
                nmo_cli_table_add_row(&table, cells, 5);
            }

            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }

        /* Summary */
        fprintf(c.out, "\nSummary:\n");
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", total_filtered);
        nmo_cli_print_kv(c.out, "Total objects", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", zero_incoming);
        nmo_cli_print_kv(c.out, "Zero incoming", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", root_count);
        nmo_cli_print_kv(c.out, "Roots", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", likely_orphans);
        nmo_cli_print_kv(c.out, "Likely orphans", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", likely_orphan_size);
        nmo_cli_print_kv(c.out, "Orphan size", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%.1f%%", orphan_pct);
        nmo_cli_print_kv(c.out, "Orphan %", buf, 18, c.colorize);
    }

    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, exit_code);
}

