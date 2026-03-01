/**
 * @file nmo_cmd_convert.c
 * @brief CLI convert command group implementation
 */

#include "nmo_cmd_convert.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "app/nmo_session.h"
#include "app/nmo_saver.h"
#include "app/nmo_context.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
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
    /* Find input file (last positional arg) */
    const char *input_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!input_path) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Usage: nmo convert copy [options] -o <output> <input>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse options */
    const char *output_path = nmo_tool_find_opt_value(argc, argv, "-o", "--output");
    if (!output_path) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *compress_str = nmo_tool_find_opt_value(argc, argv, "--compress", NULL);
    bool sequential_ids = nmo_tool_has_flag(argc, argv, "--sequential-ids", NULL);
    bool no_managers = nmo_tool_has_flag(argc, argv, "--no-managers", NULL);
    bool strip_resources = nmo_tool_has_flag(argc, argv, "--strip-resources", NULL);
    bool validate = nmo_tool_has_flag(argc, argv, "--validate", NULL);

    /* Load file */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[512];
    if (!nmo_tool_open_session(input_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error loading file: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Build save options */
    nmo_save_options_t save_opts = nmo_save_options_default();

    if (compress_str) {
        int level = 0;
        if (!parse_compression_level(compress_str, &level)) {
            fprintf(stderr, "Error: Invalid compression level '%s' (must be 0-9)\n", compress_str);
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_ARG_ERROR;
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
    int result = nmo_save_file(session, output_path, &save_opts);
    if (result != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(result));
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Output results */
    bool is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                    global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    if (is_json) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        if (!doc) {
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, data, "input_file", input_path);
        nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);
        nmo_cli_json_add_uint_safe(doc, data, "flags", save_opts.flags);
        nmo_cli_json_add_int_safe(doc, data, "compression_level", save_opts.compression_level);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "convert.copy", input_path);
        yyjson_mut_doc_set_root(doc, root);

        FILE *out = stdout;
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        printf("Saved to %s\n", output_path);
    }

    nmo_tool_close_session(ctx, session);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * nmo convert version - Show/modify file version metadata
 * ============================================================================ */

int nmo_cmd_convert_version(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Find input file */
    const char *input_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!input_path) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Usage: nmo convert version [options] <input>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Check if output specified */
    const char *output_path = nmo_tool_find_opt_value(argc, argv, "-o", "--output");

    /* Load file */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[512];
    if (!nmo_tool_open_session(input_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error loading file: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get file info */
    nmo_file_info_t info = nmo_session_get_file_info(session);

    /* If no output, just show version info */
    if (!output_path) {
        bool is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                        global->format == NMO_CLI_FORMAT_JSON_PRETTY);

        if (is_json) {
            yyjson_mut_doc *doc = nmo_cli_json_create_doc();
            if (!doc) {
                nmo_tool_close_session(ctx, session);
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }

            yyjson_mut_val *data = yyjson_mut_obj(doc);
            nmo_cli_json_add_uint_safe(doc, data, "file_version", info.file_version);
            nmo_cli_json_add_uint_safe(doc, data, "file_version2", info.file_version2);
            nmo_cli_json_add_uint_safe(doc, data, "ck_version", info.ck_version);
            nmo_cli_json_add_uint_safe(doc, data, "product_version", info.product_version);
            nmo_cli_json_add_uint_safe(doc, data, "product_build", info.product_build);
            nmo_cli_json_add_uint_safe(doc, data, "object_count", (uint64_t)info.object_count);
            nmo_cli_json_add_uint_safe(doc, data, "manager_count", (uint64_t)info.manager_count);

            yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "convert.version", input_path);
            yyjson_mut_doc_set_root(doc, root);

            FILE *out = stdout;
            nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
            nmo_cli_json_free_doc(doc);
        } else {
            printf("File version:    %u\n", info.file_version);
            printf("File version2:   %u\n", info.file_version2);
            printf("CK version:      %u\n", info.ck_version);
            printf("Product version: %u\n", info.product_version);
            printf("Product build:   %u\n", info.product_build);
            printf("Object count:    %u\n", info.object_count);
            printf("Manager count:   %u\n", info.manager_count);
        }

        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_SUCCESS;
    }

    /* If output specified, save the file (equivalent to copy) */
    nmo_save_options_t save_opts = nmo_save_options_default();
    int result = nmo_save_file(session, output_path, &save_opts);
    if (result != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(result));
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    bool is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                    global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    if (is_json) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        if (!doc) {
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, data, "input_file", input_path);
        nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "convert.version", input_path);
        yyjson_mut_doc_set_root(doc, root);

        FILE *out = stdout;
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        printf("Saved to %s\n", output_path);
    }

    nmo_tool_close_session(ctx, session);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * nmo convert strip - Remove objects by class/name pattern
 * ============================================================================ */

int nmo_cmd_convert_strip(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Find input file */
    const char *input_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!input_path) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Usage: nmo convert strip [options] -o <output> <input>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse options */
    const char *output_path = nmo_tool_find_opt_value(argc, argv, "-o", "--output");
    if (!output_path) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *class_name = nmo_tool_find_opt_value(argc, argv, "--class", NULL);
    const char *name_pattern = nmo_tool_find_opt_value(argc, argv, "--name", NULL);

    if (!class_name && !name_pattern) {
        fprintf(stderr, "Error: Must specify --class or --name filter\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Load file */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[512];
    if (!nmo_tool_open_session(input_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error loading file: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get repository */
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    size_t total_count = 0;
    nmo_object_t **all_objects = nmo_object_repository_get_all(repo, &total_count);

    if (!all_objects && total_count > 0) {
        fprintf(stderr, "Error: Failed to get object list\n");
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Collect matching object IDs */
    nmo_object_id_t *ids_to_remove = NULL;
    size_t remove_count = 0;
    size_t remove_capacity = 0;

    if (all_objects) {
        /* Allocate initial capacity */
        remove_capacity = total_count;
        ids_to_remove = (nmo_object_id_t *)malloc(remove_capacity * sizeof(nmo_object_id_t));
        if (!ids_to_remove) {
            fprintf(stderr, "Error: Failed to allocate removal list\n");
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        /* Filter by class or name */
        for (size_t i = 0; i < total_count; ++i) {
            nmo_object_t *obj = all_objects[i];
            bool matches = false;

            if (class_name) {
                /* Check class match (including derived classes) */
                nmo_class_id_t filter_class = nmo_cli_class_id_from_name(ctx, class_name);
                if (filter_class == 0) {
                    fprintf(stderr, "Warning: Unknown class '%s'\n", class_name);
                    free(ids_to_remove);
                    nmo_tool_close_session(ctx, session);
                    return NMO_CLI_EXIT_ARG_ERROR;
                }

                nmo_class_id_t obj_class = nmo_object_get_class_id(obj);
                if (nmo_cli_class_is_derived_from(ctx, obj_class, filter_class) ||
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
                ids_to_remove[remove_count++] = nmo_object_get_id(obj);
            }
        }
    }

    /* Destroy matched objects */
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    if (remove_count > 0) {
        int result = nmo_session_destroy_objects(session, ids_to_remove, remove_count,
                                                 NMO_RUNTIME_REQUEST_CASCADE, &report);
        if (result != NMO_OK) {
            fprintf(stderr, "Error destroying objects: %s\n", nmo_error_string(result));
            free(ids_to_remove);
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_IO_ERROR;
        }
    }

    free(ids_to_remove);

    /* Save file */
    nmo_save_options_t save_opts = nmo_save_options_default();
    int result = nmo_save_file(session, output_path, &save_opts);
    if (result != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(result));
        nmo_tool_close_session(ctx, session);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Output results */
    bool is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                    global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    if (is_json) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        if (!doc) {
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, data, "input_file", input_path);
        nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);
        nmo_cli_json_add_uint_safe(doc, data, "objects_removed", (uint64_t)report.deleted_objects);
        if (class_name) {
            nmo_cli_json_add_str_safe(doc, data, "filter_class", class_name);
        }
        if (name_pattern) {
            nmo_cli_json_add_str_safe(doc, data, "filter_name", name_pattern);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "convert.strip", input_path);
        yyjson_mut_doc_set_root(doc, root);

        FILE *out = stdout;
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        printf("Removed %zu object(s)\n", report.deleted_objects);
        printf("Saved to %s\n", output_path);
    }

    nmo_tool_close_session(ctx, session);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * nmo convert merge - Merge objects from source into target
 * ============================================================================ */

int nmo_cmd_convert_merge(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Find input files (need two positional args) */
    const char *file_args[2] = {NULL, NULL};
    size_t file_count = nmo_tool_find_file_args(argc, argv, file_args, 2);

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

    /* Load source file */
    nmo_context_t *src_ctx = NULL;
    nmo_session_t *src_session = NULL;
    char errbuf[512];
    if (!nmo_tool_open_session(source_path, &src_ctx, &src_session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error loading source file: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Load target file */
    nmo_context_t *tgt_ctx = NULL;
    nmo_session_t *tgt_session = NULL;
    if (!nmo_tool_open_session(target_path, &tgt_ctx, &tgt_session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error loading target file: %s\n", errbuf);
        nmo_tool_close_session(src_ctx, src_session);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get all objects from source */
    nmo_object_repository_t *src_repo = nmo_session_get_repository(src_session);
    size_t src_count = 0;
    nmo_object_t **src_objects = nmo_object_repository_get_all(src_repo, &src_count);

    if (!src_objects && src_count > 0) {
        fprintf(stderr, "Error: Failed to get source object list\n");
        nmo_tool_close_session(src_ctx, src_session);
        nmo_tool_close_session(tgt_ctx, tgt_session);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Collect source object IDs */
    nmo_object_id_t *src_ids = NULL;
    if (src_count > 0 && src_objects) {
        src_ids = (nmo_object_id_t *)malloc(src_count * sizeof(nmo_object_id_t));
        if (!src_ids) {
            fprintf(stderr, "Error: Failed to allocate ID list\n");
            nmo_tool_close_session(src_ctx, src_session);
            nmo_tool_close_session(tgt_ctx, tgt_session);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
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
            return NMO_CLI_EXIT_IO_ERROR;
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
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Output results */
    bool is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                    global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    if (is_json) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        if (!doc) {
            nmo_tool_close_session(src_ctx, src_session);
            nmo_tool_close_session(tgt_ctx, tgt_session);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, data, "source_file", source_path);
        nmo_cli_json_add_str_safe(doc, data, "target_file", target_path);
        nmo_cli_json_add_str_safe(doc, data, "output_file", output_path);
        nmo_cli_json_add_uint_safe(doc, data, "objects_copied", (uint64_t)report.copied_objects);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "convert.merge", source_path);
        yyjson_mut_doc_set_root(doc, root);

        FILE *out = stdout;
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        printf("Copied %zu object(s) from source to target\n", report.copied_objects);
        printf("Saved to %s\n", output_path);
    }

    nmo_tool_close_session(src_ctx, src_session);
    nmo_tool_close_session(tgt_ctx, tgt_session);
    return NMO_CLI_EXIT_SUCCESS;
}
