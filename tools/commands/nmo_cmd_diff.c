/**
 * @file nmo_cmd_diff.c
 * @brief CLI diff command group implementation
 */

#include "nmo_cmd_diff.h"
#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "app/nmo_session.h"
#include "app/nmo_comparison.h"
#include "app/nmo_context.h"
#include "app/nmo_inspector.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Open two sessions for comparison
 * @return 0 on success, NMO_CLI_EXIT_IO_ERROR on failure
 */
static int open_two_sessions(const char *path1, const char *path2,
                             nmo_context_t **ctx1, nmo_session_t **ses1,
                             nmo_context_t **ctx2, nmo_session_t **ses2)
{
    char errbuf[256];

    /* Open first file */
    if (!nmo_tool_open_session(path1, ctx1, ses1, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error opening '%s': %s\n", path1, errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Open second file */
    if (!nmo_tool_open_session(path2, ctx2, ses2, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error opening '%s': %s\n", path2, errbuf);
        nmo_tool_close_session(*ctx1, *ses1);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    return 0;
}

/**
 * @brief Get diff type name as string
 */
static const char *diff_type_name(nmo_diff_type_t type)
{
    switch (type) {
        case NMO_DIFF_NONE: return "none";
        case NMO_DIFF_OBJECT_COUNT: return "object_count";
        case NMO_DIFF_MANAGER_COUNT: return "manager_count";
        case NMO_DIFF_OBJECT_MISSING: return "object_missing";
        case NMO_DIFF_OBJECT_ORDER: return "object_order";
        case NMO_DIFF_OBJECT_ID: return "object_id";
        case NMO_DIFF_OBJECT_NAME: return "object_name";
        case NMO_DIFF_OBJECT_CLASS_ID: return "object_class_id";
        case NMO_DIFF_OBJECT_REFERENCE_FLAG: return "object_reference_flag";
        case NMO_DIFF_OBJECT_CHUNK_SIZE: return "object_chunk_size";
        case NMO_DIFF_OBJECT_CHUNK_DATA: return "object_chunk_data";
        case NMO_DIFF_MANAGER_MISSING: return "manager_missing";
        case NMO_DIFF_MANAGER_GUID: return "manager_guid";
        case NMO_DIFF_MANAGER_CHUNK_SIZE: return "manager_chunk_size";
        case NMO_DIFF_MANAGER_CHUNK_DATA: return "manager_chunk_data";
        case NMO_DIFF_FILE_VERSION: return "file_version";
        case NMO_DIFF_CK_VERSION: return "ck_version";
        case NMO_DIFF_SHADOW_DATA: return "shadow_data";
        default: return "unknown";
    }
}

/* ============================================================================
 * diff summary
 * ============================================================================ */

int nmo_cmd_diff_summary(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Parse file arguments */
    const char *paths[2];
    size_t path_count = nmo_tool_find_file_args(argc, argv, paths, 2);
    if (path_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff summary [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse flags */
    bool ignore_order = nmo_tool_has_flag(argc, argv, "--ignore-order", NULL);
    bool verbose = nmo_tool_has_flag(argc, argv, "--verbose", "-v");
    bool strict = nmo_tool_has_flag(argc, argv, "--strict", NULL);

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_session_t *ses1 = NULL, *ses2 = NULL;
    int open_result = open_two_sessions(paths[0], paths[1], &ctx1, &ses1, &ctx2, &ses2);
    if (open_result != 0) {
        return open_result;
    }

    /* Compare sessions */
    nmo_compare_flags_t flags = NMO_COMPARE_STRUCTURE | NMO_COMPARE_FILE_INFO;
    if (ignore_order) {
        flags |= NMO_COMPARE_IGNORE_ORDER;
    }
    if (verbose) {
        flags |= NMO_COMPARE_VERBOSE;
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    int cmp_result = nmo_session_compare(ses1, ses2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get file info from both sessions */
    nmo_file_info_t info1 = nmo_session_get_file_info(ses1);
    nmo_file_info_t info2 = nmo_session_get_file_info(ses2);

    /* Output */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        /* JSON output */
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file1", paths[0]);
        yyjson_mut_obj_add_str(doc, data, "file2", paths[1]);
        yyjson_mut_obj_add_bool(doc, data, "identical", result.match != 0);
        yyjson_mut_obj_add_int(doc, data, "diff_count", result.diff_count);
        yyjson_mut_obj_add_uint(doc, data, "objects_compared", result.objects_compared);
        yyjson_mut_obj_add_uint(doc, data, "objects_matched", result.objects_matched);

        /* File info */
        yyjson_mut_val *file1_info = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, file1_info, "object_count", info1.object_count);
        yyjson_mut_obj_add_uint(doc, file1_info, "manager_count", info1.manager_count);
        yyjson_mut_obj_add_uint(doc, file1_info, "file_version", info1.file_version);
        yyjson_mut_obj_add_uint(doc, file1_info, "ck_version", info1.ck_version);
        yyjson_mut_obj_add_val(doc, data, "file1_info", file1_info);

        yyjson_mut_val *file2_info = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, file2_info, "object_count", info2.object_count);
        yyjson_mut_obj_add_uint(doc, file2_info, "manager_count", info2.manager_count);
        yyjson_mut_obj_add_uint(doc, file2_info, "file_version", info2.file_version);
        yyjson_mut_obj_add_uint(doc, file2_info, "ck_version", info2.ck_version);
        yyjson_mut_obj_add_val(doc, data, "file2_info", file2_info);

        /* Differences */
        if (result.diff_count > 0) {
            yyjson_mut_val *diffs = yyjson_mut_arr(doc);
            for (int i = 0; i < result.diff_count; i++) {
                yyjson_mut_val *diff = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, diff, "type", diff_type_name(result.diffs[i].type));
                yyjson_mut_obj_add_uint(doc, diff, "object_id", result.diffs[i].object_id);
                nmo_cli_json_add_str_safe(doc, diff, "context", result.diffs[i].context);
                yyjson_mut_arr_append(diffs, diff);
            }
            yyjson_mut_obj_add_val(doc, data, "diffs", diffs);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "diff.summary", paths[0]);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Text output */
        nmo_cli_print_heading(out, "Diff Summary", colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s", paths[0]);
        nmo_cli_print_kv(out, "File 1", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%s", paths[1]);
        nmo_cli_print_kv(out, "File 2", buf, 18, colorize);

        if (result.match) {
            fprintf(out, "\n%sFiles are identical%s\n",
                    colorize ? NMO_CLI_COLOR_GREEN : "",
                    colorize ? NMO_CLI_COLOR_RESET : "");
        } else {
            fprintf(out, "\n%sDifferences found: %d%s\n",
                    colorize ? NMO_CLI_COLOR_YELLOW : "",
                    result.diff_count,
                    colorize ? NMO_CLI_COLOR_RESET : "");
        }

        fprintf(out, "\n");
        snprintf(buf, sizeof(buf), "%u / %u", info1.object_count, info2.object_count);
        nmo_cli_print_kv(out, "Object count", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%u / %u", info1.manager_count, info2.manager_count);
        nmo_cli_print_kv(out, "Manager count", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "0x%08X / 0x%08X", info1.ck_version, info2.ck_version);
        nmo_cli_print_kv(out, "CK version", buf, 18, colorize);

        if (verbose && result.diff_count > 0) {
            fprintf(out, "\n");
            nmo_cli_print_heading(out, "Differences", colorize);
            for (int i = 0; i < result.diff_count; i++) {
                fprintf(out, "  [%s] %s\n",
                        diff_type_name(result.diffs[i].type),
                        result.diffs[i].context);
            }
        }
    }

    nmo_tool_close_session(ctx1, ses1);
    nmo_tool_close_session(ctx2, ses2);
    nmo_cli_close_output_stream(global, out);

    /* Return strict failure if requested and diffs found */
    if (strict && !result.match) {
        return NMO_CLI_EXIT_STRICT_FAILURE;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * diff objects
 * ============================================================================ */

int nmo_cmd_diff_objects(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Parse file arguments */
    const char *paths[2];
    size_t path_count = nmo_tool_find_file_args(argc, argv, paths, 2);
    if (path_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff objects [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse flags */
    bool ignore_order = nmo_tool_has_flag(argc, argv, "--ignore-order", NULL);
    bool verbose = nmo_tool_has_flag(argc, argv, "--verbose", "-v");

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_session_t *ses1 = NULL, *ses2 = NULL;
    int open_result = open_two_sessions(paths[0], paths[1], &ctx1, &ses1, &ctx2, &ses2);
    if (open_result != 0) {
        return open_result;
    }

    /* Compare objects */
    nmo_compare_flags_t flags = NMO_COMPARE_IDS | NMO_COMPARE_NAMES |
                                NMO_COMPARE_CLASS_IDS | NMO_COMPARE_CHUNKS;
    if (ignore_order) {
        flags |= NMO_COMPARE_IGNORE_ORDER;
    }
    if (verbose) {
        flags |= NMO_COMPARE_VERBOSE;
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    int cmp_result = nmo_session_compare(ses1, ses2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        /* JSON output */
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file1", paths[0]);
        yyjson_mut_obj_add_str(doc, data, "file2", paths[1]);
        yyjson_mut_obj_add_bool(doc, data, "identical", result.match != 0);
        yyjson_mut_obj_add_int(doc, data, "diff_count", result.diff_count);
        yyjson_mut_obj_add_uint(doc, data, "objects_compared", result.objects_compared);
        yyjson_mut_obj_add_uint(doc, data, "objects_matched", result.objects_matched);

        /* Object differences */
        if (result.diff_count > 0) {
            yyjson_mut_val *diffs = yyjson_mut_arr(doc);
            for (int i = 0; i < result.diff_count; i++) {
                yyjson_mut_val *diff = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, diff, "type", diff_type_name(result.diffs[i].type));
                yyjson_mut_obj_add_uint(doc, diff, "object_id", result.diffs[i].object_id);
                nmo_cli_json_add_str_safe(doc, diff, "context", result.diffs[i].context);
                yyjson_mut_arr_append(diffs, diff);
            }
            yyjson_mut_obj_add_val(doc, data, "diffs", diffs);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "diff.objects", paths[0]);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Text output */
        nmo_cli_print_heading(out, "Object Comparison", colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s", paths[0]);
        nmo_cli_print_kv(out, "File 1", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%s", paths[1]);
        nmo_cli_print_kv(out, "File 2", buf, 18, colorize);

        fprintf(out, "\n");
        snprintf(buf, sizeof(buf), "%u", result.objects_compared);
        nmo_cli_print_kv(out, "Objects compared", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%u", result.objects_matched);
        nmo_cli_print_kv(out, "Objects matched", buf, 18, colorize);

        if (result.match) {
            fprintf(out, "\n%sAll objects match%s\n",
                    colorize ? NMO_CLI_COLOR_GREEN : "",
                    colorize ? NMO_CLI_COLOR_RESET : "");
        } else {
            fprintf(out, "\n%sObject differences: %d%s\n",
                    colorize ? NMO_CLI_COLOR_YELLOW : "",
                    result.diff_count,
                    colorize ? NMO_CLI_COLOR_RESET : "");

            if (result.diff_count > 0) {
                fprintf(out, "\n");
                nmo_cli_print_heading(out, "Differences", colorize);
                for (int i = 0; i < result.diff_count; i++) {
                    fprintf(out, "  [%s] %s\n",
                            diff_type_name(result.diffs[i].type),
                            result.diffs[i].context);
                }
            }
        }
    }

    nmo_tool_close_session(ctx1, ses1);
    nmo_tool_close_session(ctx2, ses2);
    nmo_cli_close_output_stream(global, out);

    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * diff chunks
 * ============================================================================ */

int nmo_cmd_diff_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Parse file arguments */
    const char *paths[2];
    size_t path_count = nmo_tool_find_file_args(argc, argv, paths, 2);
    if (path_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff chunks [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse options */
    const char *object_id_str = nmo_tool_find_opt_value(argc, argv, "--object", "-o");
    uint32_t object_id = 0;
    bool specific_object = false;

    if (object_id_str) {
        if (!nmo_tool_parse_u32(object_id_str, &object_id)) {
            fprintf(stderr, "Error: Invalid object ID: %s\n", object_id_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        specific_object = true;
    }

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_session_t *ses1 = NULL, *ses2 = NULL;
    int open_result = open_two_sessions(paths[0], paths[1], &ctx1, &ses1, &ctx2, &ses2);
    if (open_result != 0) {
        return open_result;
    }

    /* Compare chunks */
    nmo_compare_flags_t flags = NMO_COMPARE_CHUNKS | NMO_COMPARE_IDS;
    if (specific_object) {
        /* When comparing a specific object, we'll use the general comparison
         * but filter results in output */
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    int cmp_result = nmo_session_compare(ses1, ses2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        /* JSON output */
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file1", paths[0]);
        yyjson_mut_obj_add_str(doc, data, "file2", paths[1]);
        if (specific_object) {
            yyjson_mut_obj_add_uint(doc, data, "object_id", object_id);
        }
        yyjson_mut_obj_add_bool(doc, data, "identical", result.match != 0);
        yyjson_mut_obj_add_int(doc, data, "diff_count", result.diff_count);

        /* Chunk differences (filter by object_id if specified) */
        if (result.diff_count > 0) {
            yyjson_mut_val *diffs = yyjson_mut_arr(doc);
            for (int i = 0; i < result.diff_count; i++) {
                if (specific_object && result.diffs[i].object_id != object_id) {
                    continue;
                }

                /* Only include chunk-related diffs */
                nmo_diff_type_t type = result.diffs[i].type;
                if (type != NMO_DIFF_OBJECT_CHUNK_SIZE &&
                    type != NMO_DIFF_OBJECT_CHUNK_DATA) {
                    continue;
                }

                yyjson_mut_val *diff = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, diff, "type", diff_type_name(type));
                yyjson_mut_obj_add_uint(doc, diff, "object_id", result.diffs[i].object_id);
                nmo_cli_json_add_str_safe(doc, diff, "context", result.diffs[i].context);
                yyjson_mut_arr_append(diffs, diff);
            }
            yyjson_mut_obj_add_val(doc, data, "diffs", diffs);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "diff.chunks", paths[0]);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Text output */
        nmo_cli_print_heading(out, "Chunk Comparison", colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s", paths[0]);
        nmo_cli_print_kv(out, "File 1", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%s", paths[1]);
        nmo_cli_print_kv(out, "File 2", buf, 18, colorize);

        if (specific_object) {
            snprintf(buf, sizeof(buf), "%u", object_id);
            nmo_cli_print_kv(out, "Object ID", buf, 18, colorize);
        }

        /* Count chunk-specific diffs */
        int chunk_diff_count = 0;
        for (int i = 0; i < result.diff_count; i++) {
            if (specific_object && result.diffs[i].object_id != object_id) {
                continue;
            }
            nmo_diff_type_t type = result.diffs[i].type;
            if (type == NMO_DIFF_OBJECT_CHUNK_SIZE ||
                type == NMO_DIFF_OBJECT_CHUNK_DATA) {
                chunk_diff_count++;
            }
        }

        if (chunk_diff_count == 0) {
            fprintf(out, "\n%sChunks are identical%s\n",
                    colorize ? NMO_CLI_COLOR_GREEN : "",
                    colorize ? NMO_CLI_COLOR_RESET : "");
        } else {
            fprintf(out, "\n%sChunk differences: %d%s\n",
                    colorize ? NMO_CLI_COLOR_YELLOW : "",
                    chunk_diff_count,
                    colorize ? NMO_CLI_COLOR_RESET : "");

            fprintf(out, "\n");
            nmo_cli_print_heading(out, "Differences", colorize);
            for (int i = 0; i < result.diff_count; i++) {
                if (specific_object && result.diffs[i].object_id != object_id) {
                    continue;
                }
                nmo_diff_type_t type = result.diffs[i].type;
                if (type == NMO_DIFF_OBJECT_CHUNK_SIZE ||
                    type == NMO_DIFF_OBJECT_CHUNK_DATA) {
                    fprintf(out, "  [%s] %s\n",
                            diff_type_name(type),
                            result.diffs[i].context);
                }
            }
        }
    }

    nmo_tool_close_session(ctx1, ses1);
    nmo_tool_close_session(ctx2, ses2);
    nmo_cli_close_output_stream(global, out);

    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * diff full
 * ============================================================================ */

int nmo_cmd_diff_full(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Parse file arguments */
    const char *paths[2];
    size_t path_count = nmo_tool_find_file_args(argc, argv, paths, 2);
    if (path_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff full [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse flags */
    bool ignore_order = nmo_tool_has_flag(argc, argv, "--ignore-order", NULL);

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_session_t *ses1 = NULL, *ses2 = NULL;
    int open_result = open_two_sessions(paths[0], paths[1], &ctx1, &ses1, &ctx2, &ses2);
    if (open_result != 0) {
        return open_result;
    }

    /* Full comparison with all flags */
    nmo_compare_flags_t flags = NMO_COMPARE_STRUCTURE | NMO_COMPARE_IDS |
                                NMO_COMPARE_NAMES | NMO_COMPARE_CLASS_IDS |
                                NMO_COMPARE_CHUNKS | NMO_COMPARE_SHADOW |
                                NMO_COMPARE_MANAGERS | NMO_COMPARE_FILE_INFO |
                                NMO_COMPARE_VERBOSE;
    if (ignore_order) {
        flags |= NMO_COMPARE_IGNORE_ORDER;
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    int cmp_result = nmo_session_compare(ses1, ses2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Generate detailed report */
    nmo_comparison_result_format_report(&result);

    /* Output */
    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        /* JSON output */
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file1", paths[0]);
        yyjson_mut_obj_add_str(doc, data, "file2", paths[1]);
        yyjson_mut_obj_add_bool(doc, data, "identical", result.match != 0);
        yyjson_mut_obj_add_int(doc, data, "diff_count", result.diff_count);
        yyjson_mut_obj_add_bool(doc, data, "diff_overflow", result.diff_overflow != 0);
        yyjson_mut_obj_add_uint(doc, data, "objects_compared", result.objects_compared);
        yyjson_mut_obj_add_uint(doc, data, "objects_matched", result.objects_matched);
        yyjson_mut_obj_add_uint(doc, data, "managers_compared", result.managers_compared);
        yyjson_mut_obj_add_uint(doc, data, "managers_matched", result.managers_matched);

        /* Full report */
        if (result.report[0] != '\0') {
            nmo_cli_json_add_str_safe(doc, data, "report", result.report);
        }

        /* All differences */
        if (result.diff_count > 0) {
            yyjson_mut_val *diffs = yyjson_mut_arr(doc);
            for (int i = 0; i < result.diff_count; i++) {
                yyjson_mut_val *diff = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, diff, "type", diff_type_name(result.diffs[i].type));
                yyjson_mut_obj_add_uint(doc, diff, "object_id", result.diffs[i].object_id);
                nmo_cli_json_add_str_safe(doc, diff, "context", result.diffs[i].context);
                yyjson_mut_arr_append(diffs, diff);
            }
            yyjson_mut_obj_add_val(doc, data, "diffs", diffs);
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "diff.full", paths[0]);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Text output - print the formatted report */
        nmo_cli_print_heading(out, "Full Comparison Report", colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s", paths[0]);
        nmo_cli_print_kv(out, "File 1", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%s", paths[1]);
        nmo_cli_print_kv(out, "File 2", buf, 18, colorize);

        fprintf(out, "\n");
        snprintf(buf, sizeof(buf), "%u", result.objects_compared);
        nmo_cli_print_kv(out, "Objects compared", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%u", result.objects_matched);
        nmo_cli_print_kv(out, "Objects matched", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%u", result.managers_compared);
        nmo_cli_print_kv(out, "Managers compared", buf, 18, colorize);
        snprintf(buf, sizeof(buf), "%u", result.managers_matched);
        nmo_cli_print_kv(out, "Managers matched", buf, 18, colorize);

        if (result.match) {
            fprintf(out, "\n%sFiles are identical%s\n",
                    colorize ? NMO_CLI_COLOR_GREEN : "",
                    colorize ? NMO_CLI_COLOR_RESET : "");
        } else {
            fprintf(out, "\n%sDifferences found: %d%s",
                    colorize ? NMO_CLI_COLOR_YELLOW : "",
                    result.diff_count,
                    colorize ? NMO_CLI_COLOR_RESET : "");
            if (result.diff_overflow) {
                fprintf(out, " %s(overflow, only first %d shown)%s",
                        colorize ? NMO_CLI_COLOR_RED : "",
                        NMO_MAX_DIFFS,
                        colorize ? NMO_CLI_COLOR_RESET : "");
            }
            fprintf(out, "\n");
        }

        /* Print the detailed report */
        if (result.report[0] != '\0') {
            fprintf(out, "\n");
            nmo_cli_print_heading(out, "Detailed Report", colorize);
            fprintf(out, "%s", result.report);
        }
    }

    nmo_tool_close_session(ctx1, ses1);
    nmo_tool_close_session(ctx2, ses2);
    nmo_cli_close_output_stream(global, out);

    return NMO_CLI_EXIT_SUCCESS;
}
