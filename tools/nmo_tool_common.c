#include "nmo_tool_common.h"
#include "nmo_cli_common.h"
#include "nmo_cli_json.h"
#include "nmo_cli_output.h"
#include "yyjson.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nmo_tool_stricmp(const char *a, const char *b) {
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (*a && *b) {
        int da = tolower((unsigned char)*a);
        int db = tolower((unsigned char)*b);
        if (da != db) {
            return da - db;
        }
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bool nmo_tool_streq_ci(const char *a, const char *b) {
    return nmo_tool_stricmp(a, b) == 0;
}

bool nmo_tool_match_wildcard_ci(const char *pattern, const char *value) {
    if (!pattern || !*pattern) {
        return true;
    }
    if (!value) {
        value = "";
    }

    char pc = *pattern;
    if (pc == '*') {
        pattern++;
        if (!*pattern) {
            return true;
        }
        while (*value) {
            if (nmo_tool_match_wildcard_ci(pattern, value)) {
                return true;
            }
            value++;
        }
        return nmo_tool_match_wildcard_ci(pattern, value);
    }
    if (pc == '?') {
        if (!*value) {
            return false;
        }
        return nmo_tool_match_wildcard_ci(pattern + 1, value + 1);
    }
    if (tolower((unsigned char)pc) != tolower((unsigned char)*value)) {
        return false;
    }
    return nmo_tool_match_wildcard_ci(pattern + 1, value + 1);
}

char *nmo_tool_strdup(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

bool nmo_tool_parse_u32_dec(const char *text, uint32_t *out) {
    if (!text || !out) {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xFFFFFFFFu) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool nmo_tool_parse_u32(const char *text, uint32_t *out) {
    if (!text || !out) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > 0xFFFFFFFFu) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool nmo_tool_parse_size_dec(const char *text, size_t *out) {
    if (!text || !out) {
        return false;
    }
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    *out = (size_t)value;
    return true;
}

bool nmo_tool_parse_size(const char *text, size_t *out) {
    if (!text || !out) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    if (value > (unsigned long long)SIZE_MAX) {
        return false;
    }
    *out = (size_t)value;
    return true;
}

/* ============================================================================
 * Argument Parsing Helpers
 * ============================================================================ */

const char *nmo_tool_find_file_arg(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            return argv[i];
        }
    }
    return NULL;
}

const char *nmo_tool_find_file_arg_last(int argc, char **argv) {
    const char *last = NULL;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            last = argv[i];
        }
    }
    return last;
}

size_t nmo_tool_find_file_args(int argc, char **argv,
                               const char **out_paths, size_t max_count) {
    size_t count = 0;
    for (int i = 1; i < argc && count < max_count; ++i) {
        if (argv[i][0] != '-') {
            out_paths[count++] = argv[i];
        }
    }
    return count;
}

const char *nmo_tool_find_opt_value(int argc, char **argv,
                                    const char *opt1, const char *opt2) {
    for (int i = 1; i < argc - 1; ++i) {
        if ((opt1 && strcmp(argv[i], opt1) == 0) ||
            (opt2 && strcmp(argv[i], opt2) == 0)) {
            return argv[i + 1];
        }
    }
    return NULL;
}

bool nmo_tool_has_flag(int argc, char **argv,
                       const char *flag1, const char *flag2) {
    for (int i = 1; i < argc; ++i) {
        if ((flag1 && strcmp(argv[i], flag1) == 0) ||
            (flag2 && strcmp(argv[i], flag2) == 0)) {
            return true;
        }
    }
    return false;
}

void nmo_tool_sanitize_filename(char *dst, size_t dst_size,
                                const char *name, uint32_t index) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!name || !*name) {
        snprintf(dst, dst_size, "resource_%u.bin", index);
        return;
    }

    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)name;
         *p && pos + 1 < dst_size; ++p) {
        unsigned char c = *p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            dst[pos++] = '_';
        } else if (c < 0x20) {
            dst[pos++] = '_';
        } else {
            dst[pos++] = (char)c;
        }
    }
    dst[pos] = '\0';

    if (dst[0] == '\0') {
        snprintf(dst, dst_size, "resource_%u.bin", index);
    }
}

/* ============================================================================
 * Batch Processing Framework
 * ============================================================================ */

int nmo_tool_batch_run(
    const char **file_paths,
    size_t file_count,
    const nmo_cli_global_opts_t *global,
    const char *command,
    nmo_batch_handler_t handler,
    void *user_data)
{
    if (!file_paths || file_count == 0 || !global || !handler) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    bool is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                    global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    int worst_exit = NMO_CLI_EXIT_SUCCESS;
    size_t succeeded = 0;
    size_t failed = 0;

    /* JSON mode: build a single document with results array */
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *results_arr = NULL;

    if (is_json) {
        doc = nmo_cli_json_create_doc();
        if (!doc) {
            nmo_cli_close_output_stream(global, out);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        results_arr = yyjson_mut_arr(doc);
    }

    /* Process each file */
    for (size_t i = 0; i < file_count; ++i) {
        const char *path = file_paths[i];

        if (is_json) {
            /* Create per-file result object */
            yyjson_mut_val *result_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_str(doc, result_obj, "file", path);

            yyjson_mut_val *data_obj = yyjson_mut_obj(doc);
            int rc = handler(path, global, user_data, doc, data_obj);

            yyjson_mut_obj_add_bool(doc, result_obj, "success", rc == NMO_CLI_EXIT_SUCCESS);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                yyjson_mut_obj_add_int(doc, result_obj, "exit_code", rc);
                failed++;
            } else {
                succeeded++;
            }
            yyjson_mut_obj_add_val(doc, result_obj, "data", data_obj);
            yyjson_mut_arr_append(results_arr, result_obj);

            if (rc > worst_exit) {
                worst_exit = rc;
            }
        } else {
            /* Text mode: print per-file heading */
            bool colorize = nmo_cli_should_colorize(global, out);

            if (i > 0) {
                fprintf(out, "\n");
            }
            fprintf(out, "%s--- [%zu/%zu] %s ---%s\n",
                    colorize ? NMO_CLI_COLOR_CYAN : "",
                    i + 1, file_count, path,
                    colorize ? NMO_CLI_COLOR_RESET : "");

            int rc = handler(path, global, user_data, NULL, NULL);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                failed++;
            } else {
                succeeded++;
            }

            if (rc > worst_exit) {
                worst_exit = rc;
            }
        }
    }

    /* Output */
    if (is_json) {
        yyjson_mut_val *envelope = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, envelope, "schema_version", NMO_CLI_JSON_SCHEMA_VERSION);
        yyjson_mut_obj_add_str(doc, envelope, "tool", "nmo");
        yyjson_mut_obj_add_str(doc, envelope, "command", command);
        yyjson_mut_obj_add_bool(doc, envelope, "batch_mode", true);
        yyjson_mut_obj_add_val(doc, envelope, "results", results_arr);

        /* Summary */
        yyjson_mut_val *summary = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, summary, "total", (uint64_t)file_count);
        yyjson_mut_obj_add_uint(doc, summary, "succeeded", (uint64_t)succeeded);
        yyjson_mut_obj_add_uint(doc, summary, "failed", (uint64_t)failed);
        yyjson_mut_obj_add_val(doc, envelope, "summary", summary);

        yyjson_mut_doc_set_root(doc, envelope);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Text mode: print summary */
        bool colorize = nmo_cli_should_colorize(global, out);
        fprintf(out, "\n%s=== Batch Summary ===%s\n",
                colorize ? NMO_CLI_COLOR_BOLD : "",
                colorize ? NMO_CLI_COLOR_RESET : "");
        fprintf(out, "Total: %zu  Succeeded: %zu  Failed: %zu\n",
                file_count, succeeded, failed);
    }

    nmo_cli_close_output_stream(global, out);
    return worst_exit;
}
