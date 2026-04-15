/**
 * @file nmo_cmd_convert.c
 * @brief CLI convert command group implementation
 */

#include "nmo_cmd_convert.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "session/nmo_session.h"
#include "app/nmo_save.h"
#include "session/nmo_context.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "format/nmo_object.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Helper functions
 * ============================================================================ */

/**
 * @brief Parse compression level from string
 * @return true on success, false on error
 */
static bool parse_compression_level(const char *str, int *out_level)
{
    if (!str || !out_level) {
        return false;
    }

    char *endptr = NULL;
    long val = strtol(str, &endptr, 10);
    if (*endptr != '\0' || val < 0 || val > 9) {
        return false;
    }

    *out_level = (int)val;
    return true;
}

/* ============================================================================
 * nmo convert copy - Round-trip copy with save options
 * ============================================================================ */

int nmo_cmd_convert_copy(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",          "-o", NMO_OPT_STRING, "Output file path"},
        {"--compress",        NULL, NMO_OPT_STRING, "Compression level (0-9)"},
        {"--sequential-ids",  NULL, NMO_OPT_FLAG,   "Renumber object IDs sequentially"},
        {"--no-managers",     NULL, NMO_OPT_FLAG,   "Strip manager data"},
        {"--strip-resources", NULL, NMO_OPT_FLAG,   "Strip embedded resources"},
        {"--validate",        NULL, NMO_OPT_FLAG,   "Validate after copy"},
    };
    enum { OPT_OUTPUT, OPT_COMPRESS, OPT_SEQIDS, OPT_NOMGR, OPT_STRIPRES, OPT_VALIDATE, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path   = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    const char *compress_str  = vals[OPT_COMPRESS].present ? vals[OPT_COMPRESS].val.str : NULL;
    bool sequential_ids       = vals[OPT_SEQIDS].present && vals[OPT_SEQIDS].val.flag;
    bool no_managers          = vals[OPT_NOMGR].present && vals[OPT_NOMGR].val.flag;
    bool strip_resources      = vals[OPT_STRIPRES].present && vals[OPT_STRIPRES].val.flag;
    bool validate             = vals[OPT_VALIDATE].present && vals[OPT_VALIDATE].val.flag;

    if (!output_path) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build save options */
    nmo_save_options_t save_opts = nmo_save_options_default();

    if (compress_str) {
        int level = 0;
        if (!parse_compression_level(compress_str, &level)) {
            fprintf(stderr, "Error: Invalid compression level '%s' (must be 0-9)\n", compress_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        save_opts.compression_level = level;
        save_opts.flags |= NMO_SAVE_COMPRESSED;
    }

    if (sequential_ids) {
        save_opts.flags |= NMO_SAVE_SEQUENTIAL_IDS;
    }

    if (!no_managers) {
        save_opts.flags |= NMO_SAVE_INCLUDE_MANAGERS;
    }

    if (strip_resources) {
        save_opts.flags |= NMO_SAVE_STRIP_INCLUDED_FILES;
    }

    if (validate) {
        save_opts.flags |= NMO_SAVE_VALIDATE_BEFORE;
    }

    /* Save file */
    int result = nmo_save_file(c.session, output_path, &save_opts);
    if (result != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(result));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, data, "input_file", c.file_path);
        nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);
        nmo_cli_json_add_uint_safe(doc, data, "flags", save_opts.flags);
        nmo_cli_json_add_int_safe(doc, data, "compression_level", save_opts.compression_level);

        nmo_cmd_ctx_json_end(&c, doc, data, "convert.copy");
    } else {
        fprintf(c.out, "Saved to %s\n", output_path);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * nmo convert version - Show/modify file version metadata
 * ============================================================================ */

int nmo_cmd_convert_version(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[0].present ? vals[0].val.str : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get file info */
    nmo_file_info_t info = nmo_session_get_file_info(c.session);

    /* If no output, just show version info */
    if (!output_path) {
        if (c.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
            if (!doc) {
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }

            yyjson_mut_val *data = yyjson_mut_obj(doc);
            nmo_cli_json_add_uint_safe(doc, data, "file_version", info.file_version);
            nmo_cli_json_add_uint_safe(doc, data, "file_version2", info.file_version2);
            nmo_cli_json_add_uint_safe(doc, data, "ck_version", info.ck_version);
            nmo_cli_json_add_uint_safe(doc, data, "product_version", info.product_version);
            nmo_cli_json_add_uint_safe(doc, data, "product_build", info.product_build);
            nmo_cli_json_add_uint_safe(doc, data, "object_count", (uint64_t)info.object_count);
            nmo_cli_json_add_uint_safe(doc, data, "manager_count", (uint64_t)info.manager_count);

            nmo_cmd_ctx_json_end(&c, doc, data, "convert.version");
        } else {
            fprintf(c.out, "File version:    %u\n", info.file_version);
            fprintf(c.out, "File version2:   %u\n", info.file_version2);
            fprintf(c.out, "CK version:      %u\n", info.ck_version);
            fprintf(c.out, "Product version: %u\n", info.product_version);
            fprintf(c.out, "Product build:   %u\n", info.product_build);
            fprintf(c.out, "Object count:    %u\n", info.object_count);
            fprintf(c.out, "Manager count:   %u\n", info.manager_count);
        }

        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* If output specified, save the file (equivalent to copy) */
    nmo_save_options_t save_opts = nmo_save_options_default();
    int result = nmo_save_file(c.session, output_path, &save_opts);
    if (result != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(result));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, data, "input_file", c.file_path);
        nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);

        nmo_cmd_ctx_json_end(&c, doc, data, "convert.version");
    } else {
        fprintf(c.out, "Saved to %s\n", output_path);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * nmo convert strip - Remove objects by class/name pattern
 * ============================================================================ */

int nmo_cmd_convert_strip(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o", NMO_OPT_STRING, "Output file path"},
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--name",    "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without modifying"},
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_DRYRUN, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    const char *class_name  = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;
    const char *name_pattern = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    bool dry_run            = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

    if (!output_path && !dry_run) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!class_name && !name_pattern) {
        fprintf(stderr, "Error: Must specify --class or --name filter\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get repository */
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    size_t total_count = 0;
    nmo_object_t **all_objects = nmo_object_repository_get_all(repo, &total_count);

    if (!all_objects && total_count > 0) {
        fprintf(stderr, "Error: Failed to get object list\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Collect matching object IDs and object pointers */
    nmo_object_id_t *ids_to_remove = NULL;
    nmo_object_t **matched_objects = NULL;
    size_t remove_count = 0;

    if (all_objects) {
        /* Allocate capacity for both arrays */
        ids_to_remove = (nmo_object_id_t *)malloc(total_count * sizeof(nmo_object_id_t));
        matched_objects = (nmo_object_t **)malloc(total_count * sizeof(nmo_object_t *));
        if (!ids_to_remove || !matched_objects) {
            fprintf(stderr, "Error: Failed to allocate removal list\n");
            free(ids_to_remove);
            free(matched_objects);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Filter by class or name */
        for (size_t i = 0; i < total_count; ++i) {
            nmo_object_t *obj = all_objects[i];
            bool matches = false;

            if (class_name) {
                /* Check class match (including derived classes) */
                nmo_class_id_t filter_class = nmo_cli_class_id_from_name(c.ctx, class_name);
                if (filter_class == 0) {
                    fprintf(stderr, "Warning: Unknown class '%s'\n", class_name);
                    free(ids_to_remove);
                    free(matched_objects);
                    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
                }

                nmo_class_id_t obj_class = nmo_object_get_class_id(obj);
                if (nmo_cli_class_is_derived_from(c.ctx, obj_class, filter_class) ||
                    obj_class == filter_class) {
                    matches = true;
                }
            }

            if (name_pattern) {
                /* Check name match */
                const char *obj_name = nmo_object_get_name(obj);
                if (obj_name && nmo_tool_match_wildcard_ci(name_pattern, obj_name)) {
                    matches = true;
                }
            }

            if (matches) {
                ids_to_remove[remove_count] = nmo_object_get_id(obj);
                matched_objects[remove_count] = obj;
                remove_count++;
            }
        }
    }

    /* Dry-run: output preview and exit */
    if (dry_run) {
        /* Compute cascade impact via preview API */
        nmo_arena_t *preview_arena = nmo_arena_create(NULL, 0);
        nmo_object_id_t *expanded_ids = NULL;
        size_t expanded_count = 0;
        bool have_cascade = false;

        if (preview_arena && remove_count > 0) {
            int prev_rc = nmo_session_preview_destroy(
                c.session, ids_to_remove, remove_count,
                NMO_RUNTIME_REQUEST_CASCADE, preview_arena,
                &expanded_ids, &expanded_count);
            have_cascade = (prev_rc == NMO_OK && expanded_ids != NULL);
            if (prev_rc != NMO_OK) {
                fprintf(stderr, "Warning: Could not compute cascade impact\n");
            }
        }

        /* Partition expanded set into cascade-only IDs */
        nmo_object_t **cascade_objects = NULL;
        size_t cascade_count = 0;

        if (have_cascade && expanded_count > remove_count) {
            cascade_objects = (nmo_object_t **)malloc(
                expanded_count * sizeof(nmo_object_t *));
            if (!cascade_objects) {
                fprintf(stderr, "Warning: Could not allocate cascade display buffer\n");
            }
            if (cascade_objects) {
                for (size_t ei = 0; ei < expanded_count; ++ei) {
                    nmo_object_id_t eid = expanded_ids[ei];
                    /* Check if this ID was directly matched */
                    bool is_direct = false;
                    for (size_t di = 0; di < remove_count; ++di) {
                        if (ids_to_remove[di] == eid) {
                            is_direct = true;
                            break;
                        }
                    }
                    if (!is_direct) {
                        nmo_object_t *cobj =
                            nmo_object_repository_find_by_id(repo, eid);
                        if (cobj) {
                            cascade_objects[cascade_count++] = cobj;
                        }
                    }
                }
            }
        }

        if (c.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
            if (!doc) {
                free(cascade_objects);
                nmo_arena_destroy(preview_arena);
                free(ids_to_remove);
                free(matched_objects);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }

            yyjson_mut_val *data = yyjson_mut_obj(doc);
            nmo_cli_json_add_bool_safe(doc, data, "dry_run", true);
            nmo_cli_json_add_uint_safe(doc, data, "match_count", (uint64_t)remove_count);

            yyjson_mut_val *matches = yyjson_mut_arr(doc);
            uint64_t total_match_size = 0;
            for (size_t i = 0; i < remove_count; ++i) {
                nmo_object_t *obj = matched_objects[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                nmo_cli_json_add_uint_safe(doc, item, "id",
                                           (uint64_t)nmo_object_get_id(obj));
                nmo_class_id_t cid = nmo_object_get_class_id(obj);
                nmo_cli_json_add_uint_safe(doc, item, "class_id", (uint64_t)cid);
                const char *cn = nmo_cli_class_name_from_id(c.ctx, cid);
                if (cn) {
                    nmo_cli_json_add_str_safe(doc, item, "class_name", cn);
                }
                nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
                uint64_t sz = chunk ? (uint64_t)nmo_chunk_get_data_size(chunk) : 0;
                nmo_cli_json_add_uint_safe(doc, item, "size", sz);
                total_match_size += sz;
                const char *name = nmo_object_get_name(obj);
                if (name && name[0]) {
                    nmo_cli_json_add_str_safe(doc, item, "name", name);
                }
                yyjson_mut_arr_add_val(matches, item);
            }
            yyjson_mut_obj_add_val(doc, data, "matches", matches);
            nmo_cli_json_add_uint_safe(doc, data, "total_match_size", total_match_size);

            /* Cascade objects */
            nmo_cli_json_add_uint_safe(doc, data, "cascade_count", (uint64_t)cascade_count);
            yyjson_mut_val *cascade_arr = yyjson_mut_arr(doc);
            uint64_t cascade_size = 0;
            for (size_t i = 0; i < cascade_count; ++i) {
                nmo_object_t *obj = cascade_objects[i];
                yyjson_mut_val *item = yyjson_mut_obj(doc);
                nmo_cli_json_add_uint_safe(doc, item, "id",
                                           (uint64_t)nmo_object_get_id(obj));
                nmo_class_id_t cid = nmo_object_get_class_id(obj);
                const char *cn = nmo_cli_class_name_from_id(c.ctx, cid);
                if (cn) {
                    nmo_cli_json_add_str_safe(doc, item, "class_name", cn);
                }
                nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
                uint64_t sz = chunk ? (uint64_t)nmo_chunk_get_data_size(chunk) : 0;
                nmo_cli_json_add_uint_safe(doc, item, "size", sz);
                cascade_size += sz;
                const char *name = nmo_object_get_name(obj);
                if (name && name[0]) {
                    nmo_cli_json_add_str_safe(doc, item, "name", name);
                }
                yyjson_mut_arr_add_val(cascade_arr, item);
            }
            yyjson_mut_obj_add_val(doc, data, "cascade_objects", cascade_arr);
            nmo_cli_json_add_uint_safe(doc, data, "cascade_size", cascade_size);
            nmo_cli_json_add_uint_safe(doc, data, "total_size", total_match_size + cascade_size);

            if (class_name) {
                nmo_cli_json_add_str_safe(doc, data, "filter_class", class_name);
            }
            if (name_pattern) {
                nmo_cli_json_add_str_safe(doc, data, "filter_name", name_pattern);
            }

            nmo_cmd_ctx_json_end(&c, doc, data, "convert.strip");
        } else {
            fprintf(c.out, "=== Dry Run: Strip Preview ===\n\n");
            if (remove_count == 0) {
                fprintf(c.out, "No objects matched the filter.\n");
            } else {
                static const nmo_cli_table_col_t columns[] = {
                    {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
                    {"CLASS", NMO_CLI_ALIGN_LEFT, 20, 30},
                    {"SIZE", NMO_CLI_ALIGN_RIGHT, 10, 0},
                    {"NAME", NMO_CLI_ALIGN_LEFT, 20, 50},
                };

                /* Matched objects table */
                nmo_cli_table_t table;
                nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

                uint64_t match_size = 0;
                for (size_t i = 0; i < remove_count; ++i) {
                    nmo_object_t *obj = matched_objects[i];
                    char id_buf[16];
                    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));
                    const char *cn = nmo_cli_class_name_from_id(c.ctx,
                                                                nmo_object_get_class_id(obj));
                    const char *name = nmo_object_get_name(obj);
                    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
                    uint64_t sz = chunk ? (uint64_t)nmo_chunk_get_data_size(chunk) : 0;
                    match_size += sz;
                    char size_buf[32];
                    snprintf(size_buf, sizeof(size_buf), "%" PRIu64, sz);
                    const char *cells[] = {
                        id_buf, cn ? cn : "-", size_buf,
                        (name && name[0]) ? name : "-"
                    };
                    nmo_cli_table_add_row(&table, cells, 4);
                }

                fprintf(c.out, "Matched %zu object(s):\n\n", remove_count);
                nmo_cli_table_print(&table, c.out, c.colorize);
                nmo_cli_table_free(&table);

                /* Cascade impact section */
                if (cascade_count > 0) {
                    fprintf(c.out, "\nCascade impact: %zu additional object(s):\n",
                            cascade_count);

                    nmo_cli_table_t ctable;
                    nmo_cli_table_init(&ctable, columns,
                                       sizeof(columns) / sizeof(columns[0]));

                    uint64_t casc_size = 0;
                    for (size_t i = 0; i < cascade_count; ++i) {
                        nmo_object_t *obj = cascade_objects[i];
                        char id_buf[16];
                        snprintf(id_buf, sizeof(id_buf), "%u",
                                 nmo_object_get_id(obj));
                        const char *cn = nmo_cli_class_name_from_id(
                            c.ctx, nmo_object_get_class_id(obj));
                        const char *name = nmo_object_get_name(obj);
                        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
                        uint64_t sz = chunk
                            ? (uint64_t)nmo_chunk_get_data_size(chunk) : 0;
                        casc_size += sz;
                        char size_buf[32];
                        snprintf(size_buf, sizeof(size_buf), "%" PRIu64, sz);
                        const char *cells[] = {
                            id_buf, cn ? cn : "-", size_buf,
                            (name && name[0]) ? name : "-"
                        };
                        nmo_cli_table_add_row(&ctable, cells, 4);
                    }

                    nmo_cli_table_print(&ctable, c.out, c.colorize);
                    nmo_cli_table_free(&ctable);

                    fprintf(c.out, "\nTotal: %zu objects, %" PRIu64 " bytes\n",
                            remove_count + cascade_count,
                            match_size + casc_size);
                } else {
                    fprintf(c.out, "\nTotal: %zu objects, %" PRIu64 " bytes\n",
                            remove_count, match_size);
                }
            }
        }

        free(cascade_objects);
        nmo_arena_destroy(preview_arena);
        free(ids_to_remove);
        free(matched_objects);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    free(matched_objects);

    /* Destroy matched objects */
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    if (remove_count > 0) {
        int result = nmo_session_destroy_objects(c.session, ids_to_remove, remove_count,
                                                 NMO_RUNTIME_REQUEST_CASCADE, &report);
        if (result != NMO_OK) {
            fprintf(stderr, "Error destroying objects: %s\n", nmo_error_string(result));
            free(ids_to_remove);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    free(ids_to_remove);

    /* Save file */
    nmo_save_options_t save_opts = nmo_save_options_default();
    int result = nmo_save_file(c.session, output_path, &save_opts);
    if (result != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(result));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, data, "input_file", c.file_path);
        nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);
        nmo_cli_json_add_uint_safe(doc, data, "objects_removed", (uint64_t)report.deleted_objects);
        if (class_name) {
            nmo_cli_json_add_str_safe(doc, data, "filter_class", class_name);
        }
        if (name_pattern) {
            nmo_cli_json_add_str_safe(doc, data, "filter_name", name_pattern);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "convert.strip");
    } else {
        fprintf(c.out, "Removed %zu object(s)\n", report.deleted_objects);
        fprintf(c.out, "Saved to %s\n", output_path);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * nmo convert merge - Merge objects from source into target
 * ============================================================================ */

int nmo_cmd_convert_merge(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Find input files (need two positional args) */
    const char *file_args[2] = {NULL, NULL};
    const char *const value_opts[] = {"-o", "--output"};
    size_t file_count = nmo_tool_find_file_args_ex(
        argc, argv, file_args, 2, value_opts, 2);

    if (file_count < 2) {
        fprintf(stderr, "Error: Need two input files (source and target)\n");
        fprintf(stderr, "Usage: nmo convert merge [options] -o <output> <source> <target>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *source_path = file_args[0];
    const char *target_path = file_args[1];

    /* Parse options */
    const char *output_path = nmo_tool_find_opt_value(argc, argv, "-o", "--output");
    if (!output_path) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Use init_no_file since we manage two sessions manually */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    /* Load source file */
    nmo_context_t *src_ctx = NULL;
    nmo_session_t *src_session = NULL;
    char errbuf[512];
    if (!nmo_tool_open_session(source_path, &src_ctx, &src_session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error loading source file: %s\n", errbuf);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Load target file */
    nmo_context_t *tgt_ctx = NULL;
    nmo_session_t *tgt_session = NULL;
    if (!nmo_tool_open_session(target_path, &tgt_ctx, &tgt_session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error loading target file: %s\n", errbuf);
        nmo_tool_close_session(src_ctx, src_session);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Get all objects from source */
    nmo_object_repository_t *src_repo = nmo_session_get_repository(src_session);
    size_t src_count = 0;
    nmo_object_t **src_objects = nmo_object_repository_get_all(src_repo, &src_count);

    if (!src_objects && src_count > 0) {
        fprintf(stderr, "Error: Failed to get source object list\n");
        nmo_tool_close_session(src_ctx, src_session);
        nmo_tool_close_session(tgt_ctx, tgt_session);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Collect source object IDs */
    nmo_object_id_t *src_ids = NULL;
    if (src_count > 0 && src_objects) {
        src_ids = (nmo_object_id_t *)malloc(src_count * sizeof(nmo_object_id_t));
        if (!src_ids) {
            fprintf(stderr, "Error: Failed to allocate ID list\n");
            nmo_tool_close_session(src_ctx, src_session);
            nmo_tool_close_session(tgt_ctx, tgt_session);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        for (size_t i = 0; i < src_count; ++i) {
            src_ids[i] = nmo_object_get_id(src_objects[i]);
        }
    }

    /* Copy objects from source to target */
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    if (src_count > 0) {
        int result = nmo_session_copy_objects(tgt_session, src_ids, src_count,
                                              NMO_RUNTIME_REQUEST_DEFAULT, &report);
        if (result != NMO_OK) {
            fprintf(stderr, "Error copying objects: %s\n", nmo_error_string(result));
            free(src_ids);
            nmo_tool_close_session(src_ctx, src_session);
            nmo_tool_close_session(tgt_ctx, tgt_session);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    free(src_ids);

    /* Save target to output */
    nmo_save_options_t save_opts = nmo_save_options_default();
    int result = nmo_save_file(tgt_session, output_path, &save_opts);
    if (result != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(result));
        nmo_tool_close_session(src_ctx, src_session);
        nmo_tool_close_session(tgt_ctx, tgt_session);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            nmo_tool_close_session(src_ctx, src_session);
            nmo_tool_close_session(tgt_ctx, tgt_session);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, data, "source_file", source_path);
        nmo_cli_json_add_str_safe(doc, data, "target_file", target_path);
        nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);
        nmo_cli_json_add_uint_safe(doc, data, "objects_copied", (uint64_t)report.copied_objects);

        /* Use manual write since c.file_path is NULL for no_file init */
        nmo_cli_json_write_enveloped_and_free(doc, data, "convert.merge", source_path,
                                              c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        fprintf(c.out, "Copied %zu object(s) from source to target\n", report.copied_objects);
        fprintf(c.out, "Saved to %s\n", output_path);
    }

    nmo_tool_close_session(src_ctx, src_session);
    nmo_tool_close_session(tgt_ctx, tgt_session);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * nmo convert export - Export selected objects to new NMO file
 * ============================================================================ */

/** Dynamic array for collecting objects (local to convert export) */
typedef struct {
    nmo_object_t **objects;
    size_t count;
    size_t capacity;
} convert_obj_collect_t;

static int convert_obj_collect_visitor(size_t index, nmo_object_t *obj,
                                       const nmo_cmd_ctx_t *c, void *user) {
    (void)index;
    (void)c;
    convert_obj_collect_t *col = (convert_obj_collect_t *)user;
    if (col->count >= col->capacity) {
        size_t new_cap = col->capacity ? col->capacity * 2 : 64;
        nmo_object_t **tmp = (nmo_object_t **)realloc(col->objects, new_cap * sizeof(*tmp));
        if (!tmp) return -1;
        col->objects = tmp;
        col->capacity = new_cap;
    }
    col->objects[col->count++] = obj;
    return 0;
}

static int id_cmp(const void *a, const void *b) {
    nmo_object_id_t ia = *(const nmo_object_id_t *)a;
    nmo_object_id_t ib = *(const nmo_object_id_t *)b;
    return (ia > ib) - (ia < ib);
}

static bool id_in_sorted(const nmo_object_id_t *arr, size_t count, nmo_object_id_t id) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < id) lo = mid + 1;
        else if (arr[mid] > id) hi = mid;
        else return true;
    }
    return false;
}

int nmo_cmd_convert_export(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",   "-o", NMO_OPT_STRING, "Output file path (required)"},
        {"--class",    "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--name",     "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--filter",   "-f", NMO_OPT_STRING, "Filter by DSL expression"},
        {"--deps",     NULL, NMO_OPT_FLAG,   "Include transitive dependencies"},
        {"--all",      NULL, NMO_OPT_FLAG,   "Export all objects (no filter required)"},
        {"--dry-run",  NULL, NMO_OPT_FLAG,   "Preview matching objects without writing"},
        {"--compress", NULL, NMO_OPT_STRING, "Compression level (0-9)"},
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_FILTER, OPT_DEPS,
           OPT_ALL, OPT_DRYRUN, OPT_COMPRESS, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path      = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    const char *class_filter_str = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;
    const char *name_pattern     = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    const char *filter_expr      = vals[OPT_FILTER].present ? vals[OPT_FILTER].val.str : NULL;
    bool include_deps            = vals[OPT_DEPS].present && vals[OPT_DEPS].val.flag;
    bool export_all              = vals[OPT_ALL].present && vals[OPT_ALL].val.flag;
    bool dry_run                 = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
    const char *compress_str     = vals[OPT_COMPRESS].present ? vals[OPT_COMPRESS].val.str : NULL;

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!export_all && !class_filter_str && !name_pattern && !filter_expr) {
        fprintf(stderr, "Error: At least one filter required (--class, --name, --filter, or --all)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Validate compression level early (before any destructive session ops) */
    int compress_level = 0;
    if (compress_str) {
        if (!parse_compression_level(compress_str, &compress_level)) {
            fprintf(stderr, "Error: Invalid compression level '%s' (must be 0-9)\n", compress_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_dsl_t query_dsl = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .name_wildcard = name_pattern,
        .filter_expr = filter_expr,
        .include_derived_classes = true,
        .print_dsl_context = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_dsl, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Collect matching objects */
    convert_obj_collect_t col = {0};
    nmo_core_iter_result_t iter_result;
    nmo_core_iter_objects(&c, &query, convert_obj_collect_visitor, &col, &iter_result);

    if (col.count == 0) {
        fprintf(stderr, "No objects matched the filter.\n");
        free(col.objects);
        nmo_core_query_dsl_destroy(&query_dsl);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    size_t seed_count = col.count;

    /* Resolve transitive dependencies if requested */
    nmo_object_t **final_objects = NULL;
    size_t final_count = 0;
    size_t dep_count = 0;

    if (include_deps) {
        nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

        /* Extract and sort seed IDs for membership testing */
        nmo_object_id_t *seed_ids = (nmo_object_id_t *)malloc(seed_count * sizeof(nmo_object_id_t));
        if (!seed_ids) {
            fprintf(stderr, "Error: Out of memory\n");
            free(col.objects);
            nmo_core_query_dsl_destroy(&query_dsl);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        for (size_t i = 0; i < seed_count; i++)
            seed_ids[i] = nmo_object_get_id(col.objects[i]);
        qsort(seed_ids, seed_count, sizeof(nmo_object_id_t), id_cmp);

        /* Get reference graph from session cache and compute transitive closure */
        nmo_ref_graph_t *graph = nmo_session_get_ref_graph(c.session);
        if (!graph) {
            fprintf(stderr, "Error: Failed to build reference graph\n");
            free(seed_ids);
            free(col.objects);
            nmo_core_query_dsl_destroy(&query_dsl);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Arena for mark_reachable results */
        nmo_arena_t *deps_arena = nmo_arena_create(NULL, 0);
        if (!deps_arena) {
            free(seed_ids);
            free(col.objects);
            nmo_core_query_dsl_destroy(&query_dsl);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        nmo_object_id_t *reachable_ids = NULL;
        size_t reachable_count = 0;
        nmo_status_t ms = nmo_ref_graph_mark_reachable(
            graph, seed_ids, seed_count, deps_arena,
            &reachable_ids, &reachable_count);

        if (ms != NMO_OK) {
            fprintf(stderr, "Error: Failed to resolve dependencies\n");
            nmo_arena_destroy(deps_arena);
            free(seed_ids);
            free(col.objects);
            nmo_core_query_dsl_destroy(&query_dsl);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Build final array: dependencies first, then seeds */
        final_objects = (nmo_object_t **)malloc(reachable_count * sizeof(nmo_object_t *));
        if (!final_objects) {
            nmo_arena_destroy(deps_arena);
            free(seed_ids);
            free(col.objects);
            nmo_core_query_dsl_destroy(&query_dsl);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Pass 1: dependency-only objects (reachable but not in seed set) */
        final_count = 0;
        for (size_t i = 0; i < reachable_count; i++) {
            if (!id_in_sorted(seed_ids, seed_count, reachable_ids[i])) {
                nmo_object_t *obj = nmo_object_repository_find_by_id(repo, reachable_ids[i]);
                if (obj)
                    final_objects[final_count++] = obj;
            }
        }
        dep_count = final_count;

        /* Pass 2: seed objects (preserving original match order) */
        for (size_t i = 0; i < seed_count; i++)
            final_objects[final_count++] = col.objects[i];

        nmo_arena_destroy(deps_arena);
        free(seed_ids);
    } else {
        final_objects = col.objects;
        final_count = col.count;
        col.objects = NULL; /* prevent double-free */
    }

    /* Dry-run: just list what would be exported */
    if (dry_run) {
        if (c.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
            if (doc) {
                yyjson_mut_val *data = yyjson_mut_obj(doc);
                nmo_cli_json_add_str_safe(doc, data, "input_file", c.file_path);
                nmo_cli_json_add_bool_safe(doc, data, "dry_run", true);
                nmo_cli_json_add_uint_safe(doc, data, "matched", (uint64_t)seed_count);
                if (include_deps) {
                    nmo_cli_json_add_bool_safe(doc, data, "deps", true);
                    nmo_cli_json_add_uint_safe(doc, data, "deps_resolved", (uint64_t)dep_count);
                }
                nmo_cli_json_add_uint_safe(doc, data, "total", (uint64_t)final_count);

                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                for (size_t i = 0; i < final_count; i++) {
                    yyjson_mut_val *entry = yyjson_mut_obj(doc);
                    nmo_cli_json_add_uint_safe(doc, entry, "id",
                        (uint64_t)nmo_object_get_id(final_objects[i]));
                    const char *cn = nmo_core_class_name(&c,
                        nmo_object_get_class_id(final_objects[i]));
                    if (cn) nmo_cli_json_add_str_safe(doc, entry, "class_name", cn);
                    const char *nm = nmo_object_get_name(final_objects[i]);
                    if (nm && nm[0]) nmo_cli_json_add_str_safe(doc, entry, "name", nm);
                    nmo_cli_json_add_bool_safe(doc, entry, "is_dep", i < dep_count);
                    yyjson_mut_arr_add_val(arr, entry);
                }
                yyjson_mut_obj_add_val(doc, data, "objects", arr);
                nmo_cmd_ctx_json_end(&c, doc, data, "convert.export");
            }
        } else {
            fprintf(c.out, "Dry run: would export %zu object(s)\n", final_count);
            if (include_deps && dep_count > 0)
                fprintf(c.out, "  Matched: %zu, Dependencies: %zu\n", seed_count, dep_count);
            fprintf(c.out, "\n");
            fprintf(c.out, "   %5s  %-20s  %10s  %-4s  %s\n",
                    "ID", "CLASS", "SIZE", "DEP", "NAME");
            fprintf(c.out, "   %5s  %-20s  %10s  %-4s  %s\n",
                    "-----", "--------------------", "----------", "----", "--------------------");
            for (size_t i = 0; i < final_count; i++) {
                nmo_object_t *obj = final_objects[i];
                nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
                size_t sz = chunk ? nmo_chunk_get_data_size(chunk) : 0;
                const char *cn = nmo_core_class_name(&c, nmo_object_get_class_id(obj));
                const char *nm = nmo_object_get_name(obj);
                fprintf(c.out, "   %5u  %-20s  %10zu  %-4s  %s\n",
                        nmo_object_get_id(obj),
                        cn ? cn : "?",
                        sz,
                        i < dep_count ? "yes" : "",
                        nm ? nm : "");
            }
        }
        free(final_objects);
        free(col.objects);
        nmo_core_query_dsl_destroy(&query_dsl);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* Build include list for saver filter (no repository mutation needed) */
    nmo_object_id_t *include_ids = (nmo_object_id_t *)malloc(final_count * sizeof(nmo_object_id_t));
    if (!include_ids) {
        free(final_objects);
        free(col.objects);
        nmo_core_query_dsl_destroy(&query_dsl);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    for (size_t i = 0; i < final_count; i++)
        include_ids[i] = nmo_object_get_id(final_objects[i]);

    /* Save via saver pipeline with object filter */
    nmo_save_options_t save_opts = nmo_save_options_default();
    save_opts.include_ids = include_ids;
    save_opts.include_count = final_count;
    if (compress_str) {
        save_opts.compression_level = compress_level;
        save_opts.flags |= NMO_SAVE_COMPRESSED;
    }

    int save_result = nmo_save_file(c.session, output_path, &save_opts);
    if (save_result != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_result));
        free(include_ids);
        free(final_objects);
        free(col.objects);
        nmo_core_query_dsl_destroy(&query_dsl);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (doc) {
            yyjson_mut_val *data = yyjson_mut_obj(doc);
            nmo_cli_json_add_str_safe(doc, data, "input_file", c.file_path);
            nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);
            nmo_cli_json_add_uint_safe(doc, data, "matched", (uint64_t)seed_count);
            if (include_deps) {
                nmo_cli_json_add_bool_safe(doc, data, "deps", true);
                nmo_cli_json_add_uint_safe(doc, data, "deps_resolved", (uint64_t)dep_count);
            }
            nmo_cli_json_add_uint_safe(doc, data, "exported", (uint64_t)final_count);
            if (class_filter_str) nmo_cli_json_add_str_safe(doc, data, "filter_class", class_filter_str);
            if (name_pattern) nmo_cli_json_add_str_safe(doc, data, "filter_name", name_pattern);
            if (filter_expr) nmo_cli_json_add_str_safe(doc, data, "filter_expr", filter_expr);
            nmo_cmd_ctx_json_end(&c, doc, data, "convert.export");
        }
    } else {
        if (include_deps && dep_count > 0) {
            fprintf(c.out, "Matched %zu, resolved %zu dep(s), exported %zu object(s) to %s\n",
                    seed_count, dep_count, final_count, output_path);
        } else {
            fprintf(c.out, "Exported %zu object(s) to %s\n", final_count, output_path);
        }
    }

    free(include_ids);
    free(final_objects);
    free(col.objects);
    nmo_core_query_dsl_destroy(&query_dsl);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
