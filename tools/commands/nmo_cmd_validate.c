/**
 * @file nmo_cmd_validate.c
 * @brief CLI validate command group implementation
 *
 * Phase 4 - Reference graph and validation rules
 */

#include "nmo_cmd_validate.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"

#include "nmo.h"
#include "app/nmo_inspector.h"
#include "core/nmo_arena.h"
#include "session/nmo_object_repository.h"
#include "session/nmo_ref_graph.h"

#include <stdio.h>
#include <string.h>

/**
 * Find file path
 */
static const char *find_file_arg(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            return argv[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * validate all
 * ============================================================================ */

int nmo_cmd_validate_all(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo validate all <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Basic structure validation - validate all chunks */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t error_count = 0;
    size_t warning_count = 0;
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format != NMO_CLI_FORMAT_JSON && global->format != NMO_CLI_FORMAT_JSON_PRETTY) {
        nmo_cli_print_heading(out, "Validation Results", colorize);
        nmo_cli_print_kv(out, "File", file_path, 12, colorize);
        fprintf(out, "\n");
    }

    /* Validate each object's chunk */
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);

        if (!chunk) {
            warning_count++;
            if (global->format != NMO_CLI_FORMAT_JSON && global->format != NMO_CLI_FORMAT_JSON_PRETTY) {
                if (global->verbosity > 0) {
                    fprintf(out, "Warning: Object %u has no chunk\n", nmo_object_get_id(obj));
                }
            }
            continue;
        }

        nmo_chunk_validation_t result;
        int rc = nmo_inspector_validate_chunk(chunk, &result);
        if (rc != 0 || !result.is_valid) {
            error_count++;
            if (global->format != NMO_CLI_FORMAT_JSON && global->format != NMO_CLI_FORMAT_JSON_PRETTY) {
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

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", file_path);
        yyjson_mut_obj_add_bool(doc, data, "valid", error_count == 0);
        yyjson_mut_obj_add_uint(doc, data, "error_count", (uint64_t)error_count);
        yyjson_mut_obj_add_uint(doc, data, "warning_count", (uint64_t)warning_count);
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)object_count);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "validate.all", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        fprintf(out, "\nSummary:\n");
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", object_count);
        nmo_cli_print_kv(out, "Objects", buf, 12, colorize);
        snprintf(buf, sizeof(buf), "%zu", error_count);
        nmo_cli_print_kv(out, "Errors", buf, 12, colorize);
        snprintf(buf, sizeof(buf), "%zu", warning_count);
        nmo_cli_print_kv(out, "Warnings", buf, 12, colorize);

        fprintf(out, "\nResult: %s\n", error_count == 0 ? "VALID" : "INVALID");
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return exit_code;
}

/* ============================================================================
 * validate structure
 * ============================================================================ */

int nmo_cmd_validate_structure(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo validate structure <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
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
    size_t checked_count = 0;

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *data = NULL;
    yyjson_mut_val *issues = NULL;

    bool is_json = (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    if (is_json) {
        doc = nmo_cli_json_create_doc();
        data = yyjson_mut_obj(doc);
        issues = yyjson_mut_arr(doc);
    } else {
        nmo_cli_print_heading(out, "Structure Validation", colorize);
        nmo_cli_print_kv(out, "File", file_path, 14, colorize);
        fprintf(out, "\n");
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        nmo_object_id_t obj_id = nmo_object_get_id(obj);

        if (!chunk) {
            warning_count++;
            if (is_json) {
                yyjson_mut_val *issue = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, issue, "severity", "warning");
                yyjson_mut_obj_add_uint(doc, issue, "object_id", obj_id);
                yyjson_mut_obj_add_uint(doc, issue, "class_id", nmo_object_get_class_id(obj));
                const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
                if (class_name) {
                    yyjson_mut_obj_add_str(doc, issue, "class_name", class_name);
                }
                yyjson_mut_obj_add_str(doc, issue, "message", "missing chunk");
                yyjson_mut_arr_add_val(issues, issue);
            } else if (global->verbosity > 0) {
                fprintf(out, "Warning: Object %u has no chunk\n", obj_id);
            }
            continue;
        }

        checked_count++;
        nmo_chunk_validation_t result;
        int rc = nmo_inspector_validate_chunk(chunk, &result);
        if (rc != 0 || !result.is_valid) {
            error_count++;
            if (is_json) {
                yyjson_mut_val *issue = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, issue, "severity", "error");
                yyjson_mut_obj_add_uint(doc, issue, "object_id", obj_id);
                yyjson_mut_obj_add_uint(doc, issue, "class_id", nmo_object_get_class_id(obj));
                const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
                if (class_name) {
                    yyjson_mut_obj_add_str(doc, issue, "class_name", class_name);
                }
                yyjson_mut_obj_add_str(doc, issue, "message",
                                       result.error_message[0] ? result.error_message : "validation failed");
                yyjson_mut_arr_add_val(issues, issue);
            } else {
                fprintf(out, "Error: Object %u chunk invalid: %s\n",
                        obj_id,
                        result.error_message[0] ? result.error_message : "validation failed");
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

    if (is_json) {
        yyjson_mut_obj_add_str(doc, data, "file", file_path);
        yyjson_mut_obj_add_bool(doc, data, "valid", error_count == 0);
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "checked_chunks", (uint64_t)checked_count);
        yyjson_mut_obj_add_uint(doc, data, "error_count", (uint64_t)error_count);
        yyjson_mut_obj_add_uint(doc, data, "warning_count", (uint64_t)warning_count);
        yyjson_mut_obj_add_val(doc, data, "issues", issues);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "validate.structure", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        fprintf(out, "\nSummary:\n");
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", object_count);
        nmo_cli_print_kv(out, "Objects", buf, 14, colorize);
        snprintf(buf, sizeof(buf), "%zu", checked_count);
        nmo_cli_print_kv(out, "Chunks", buf, 14, colorize);
        snprintf(buf, sizeof(buf), "%zu", error_count);
        nmo_cli_print_kv(out, "Errors", buf, 14, colorize);
        snprintf(buf, sizeof(buf), "%zu", warning_count);
        nmo_cli_print_kv(out, "Warnings", buf, 14, colorize);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return exit_code;
}

/* ============================================================================
 * validate references
 * ============================================================================ */

int nmo_cmd_validate_references(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo validate references <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Create reference graph */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to create arena\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_ref_graph_t *graph = nmo_ref_graph_create(session, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Validate references */
    nmo_ref_edge_t *broken_edges = NULL;
    size_t broken_count = 0;
    nmo_status_t status = nmo_ref_graph_validate(graph, &broken_edges, &broken_count);

    /* Get stats */
    nmo_ref_graph_stats_t stats;
    nmo_ref_graph_get_stats(graph, &stats);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_arena_destroy(arena);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    if (status != NMO_OK) {
        exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
    }

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
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
                yyjson_mut_obj_add_str(doc, edge, "field", broken_edges[i].field_path);
                if (broken_edges[i].index > 0) {
                    yyjson_mut_obj_add_uint(doc, edge, "index", broken_edges[i].index);
                }

                /* Add source object info */
                nmo_object_t *source = nmo_object_repository_find_by_id(repo, broken_edges[i].from);
                if (source) {
                    const char *source_class = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(source));
                    if (source_class) {
                        yyjson_mut_obj_add_str(doc, edge, "source_class", source_class);
                    }
                    const char *source_name = nmo_object_get_name(source);
                    if (source_name && source_name[0]) {
                        nmo_cli_json_add_str_safe(doc, edge, "source_name", source_name);
                    }
                }

                yyjson_mut_arr_add_val(broken_arr, edge);
            }
            yyjson_mut_obj_add_val(doc, data, "broken_references", broken_arr);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "validate.references", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Text output */
        nmo_cli_print_heading(out, "Reference Validation", colorize);
        nmo_cli_print_kv(out, "File", file_path, 16, colorize);
        fprintf(out, "\n");

        /* Summary */
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", stats.total_edges);
        nmo_cli_print_kv(out, "Total references", buf, 16, colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.self_refs);
        nmo_cli_print_kv(out, "Self-references", buf, 16, colorize);
        snprintf(buf, sizeof(buf), "%zu", broken_count);
        nmo_cli_print_kv(out, "Broken references", buf, 16, colorize);
        fprintf(out, "\n");

        /* Status */
        if (status == NMO_OK) {
            fprintf(out, "All references valid\n");
        } else {
            fprintf(out, "Broken references found\n\n");

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
                if (broken_edges[i].index > 0) {
                    snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                             broken_edges[i].field_path, broken_edges[i].index);
                } else {
                    snprintf(field_buf, sizeof(field_buf), "%s", broken_edges[i].field_path);
                }

                nmo_object_t *source = nmo_object_repository_find_by_id(repo, broken_edges[i].from);
                const char *source_class = "-";
                const char *source_name = "-";

                if (source) {
                    const char *sc = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(source));
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

            nmo_cli_table_print(&table, out, colorize);
            nmo_cli_table_free(&table);
        }
    }

    nmo_arena_destroy(arena);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return exit_code;
}

/* ============================================================================
 * validate resources
 * ============================================================================ */

int nmo_cmd_validate_resources(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo validate resources <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(session);

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);
    bool is_json = (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY);

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

    if (is_json) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", file_path);
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

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "validate.resources", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Resource Validation", colorize);
        nmo_cli_print_kv(out, "File", file_path, 18, colorize);

        if (!diag) {
            fprintf(out, "\nPlugin diagnostics unavailable\n");
        } else {
            fprintf(out, "\n");
            nmo_cli_print_kv(out, "Registry", diag->extension_registry_available ? "available" : "unavailable", 18, colorize);
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", diag->entry_count);
            nmo_cli_print_kv(out, "Entries", buf, 18, colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->missing_count);
            nmo_cli_print_kv(out, "Missing", buf, 18, colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->outdated_count);
            nmo_cli_print_kv(out, "Outdated", buf, 18, colorize);

            if (diag->entries && diag->entry_count > 0 && global->verbosity > 0) {
                fprintf(out, "\nEntries:\n");
                for (size_t i = 0; i < diag->entry_count; ++i) {
                    const nmo_session_plugin_dependency_status_t *e = &diag->entries[i];
                    char guid_buf[64];
                    nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                    fprintf(out, "  %s", guid_buf);
                    if (e->resolved_name) {
                        fprintf(out, " (%s)", e->resolved_name);
                    }
                    if (e->status_flags) {
                        fprintf(out, " [flags=0x%X]", e->status_flags);
                    }
                    fprintf(out, "\n");
                }
            }
        }
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return exit_code;
}
