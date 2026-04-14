#include "nmo_tool_common.h"
#include "nmo_cli_common.h"
#include "nmo_cli_json.h"
#include "nmo_cli_output.h"
#include "core/nmo_allocator.h"
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

/* ----------------------------------------------------------------------------
 * Wildcard capture (case-insensitive)
 * -------------------------------------------------------------------------- */

static bool wildcard_capture_impl(const char *pattern, const char *value,
                                  char captures[][256], size_t max_captures,
                                  size_t *count) {
    if (!*pattern) {
        return *value == '\0';
    }

    char pc = *pattern;
    if (pc == '*') {
        /* Allocate a capture slot for this star */
        size_t idx = *count;
        if (idx >= max_captures) {
            return false;
        }
        (*count)++;
        pattern++;

        /* Try matching star against 0..N characters */
        const char *start = value;
        const char *end = value;
        while (1) {
            /* Save captured text so far */
            size_t len = (size_t)(end - start);
            if (len >= 256) {
                (*count) = idx;
                return false;
            }
            memcpy(captures[idx], start, len);
            captures[idx][len] = '\0';

            size_t saved_count = *count;
            if (wildcard_capture_impl(pattern, end,
                                      captures, max_captures, count)) {
                return true;
            }
            /* Backtrack: restore capture count */
            *count = saved_count;

            if (*end == '\0') {
                break;
            }
            end++;
        }
        /* No match found; undo capture slot */
        *count = idx;
        return false;
    }

    if (pc == '?') {
        if (*value == '\0') {
            return false;
        }
        return wildcard_capture_impl(pattern + 1, value + 1,
                                     captures, max_captures, count);
    }

    if (tolower((unsigned char)pc) != tolower((unsigned char)*value)) {
        return false;
    }
    return wildcard_capture_impl(pattern + 1, value + 1,
                                 captures, max_captures, count);
}

bool nmo_tool_wildcard_capture_ci(const char *pattern, const char *value,
                                  char captures[][256], size_t max_captures,
                                  size_t *out_capture_count) {
    if (!out_capture_count) {
        return false;
    }
    *out_capture_count = 0;

    if (!pattern || !*pattern) {
        return true;
    }
    if (!value) {
        value = "";
    }

    size_t count = 0;
    bool ok = wildcard_capture_impl(pattern, value,
                                    captures, max_captures, &count);
    if (ok) {
        *out_capture_count = count;
    }
    return ok;
}

/* ----------------------------------------------------------------------------
 * Template substitution
 * -------------------------------------------------------------------------- */

int nmo_tool_apply_rename_template(const char *tmpl,
                                   const char *full_match,
                                   char captures[][256], size_t capture_count,
                                   char *out, size_t out_size) {
    if (!tmpl || !out || out_size == 0) {
        return -1;
    }

    size_t pos = 0;
    const char *p = tmpl;

    while (*p) {
        if (*p == '{') {
            if (*(p + 1) == '{') {
                /* Escaped literal brace: {{ -> { */
                if (pos + 1 >= out_size) {
                    return -1;
                }
                out[pos++] = '{';
                p += 2;
                continue;
            }
            /* Parse placeholder: {N} */
            p++;
            if (*p < '0' || *p > '9') {
                return -1;
            }
            size_t ref = 0;
            while (*p >= '0' && *p <= '9') {
                ref = ref * 10 + (size_t)(*p - '0');
                p++;
            }
            if (*p != '}') {
                return -1;
            }
            p++; /* skip closing brace */

            /* Resolve reference */
            const char *replacement;
            if (ref == 0) {
                replacement = full_match ? full_match : "";
            } else {
                if (ref > capture_count) {
                    return -1;
                }
                replacement = captures[ref - 1];
            }

            size_t rlen = strlen(replacement);
            if (pos + rlen >= out_size) {
                return -1;
            }
            memcpy(out + pos, replacement, rlen);
            pos += rlen;
        } else if (*p == '}') {
            if (*(p + 1) == '}') {
                /* Escaped literal brace: }} -> } */
                if (pos + 1 >= out_size) {
                    return -1;
                }
                out[pos++] = '}';
                p += 2;
                continue;
            }
            /* Stray closing brace */
            return -1;
        } else {
            if (pos + 1 >= out_size) {
                return -1;
            }
            out[pos++] = *p;
            p++;
        }
    }

    out[pos] = '\0';
    return 0;
}

char *nmo_tool_strdup(const char *src) {
    if (!src) {
        return NULL;
    }
    nmo_allocator_t alloc = nmo_allocator_default();
    return nmo_strdup(&alloc, src);
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
    static const char *const known_value_opts[] = {
        "-o", "--output", "--object"
    };
    return nmo_tool_find_file_args_ex(
        argc, argv, out_paths, max_count, known_value_opts,
        sizeof(known_value_opts) / sizeof(known_value_opts[0]));
}

static bool option_takes_value(const char *token,
                               const char *const *opts_with_values,
                               size_t opt_count)
{
    if (!token || !opts_with_values || opt_count == 0) {
        return false;
    }

    for (size_t i = 0; i < opt_count; ++i) {
        const char *opt = opts_with_values[i];
        if (!opt) {
            continue;
        }

        if (strcmp(token, opt) == 0) {
            return true;
        }
    }

    return false;
}

size_t nmo_tool_find_file_args_ex(int argc, char **argv,
                                  const char **out_paths, size_t max_count,
                                  const char *const *opts_with_values, size_t opt_count) {
    size_t count = 0;
    bool skip_next = false;

    for (int i = 1; i < argc && count < max_count; ++i) {
        const char *token = argv[i];
        if (!token) {
            continue;
        }

        if (skip_next) {
            skip_next = false;
            continue;
        }

        if (token[0] == '-') {
            if (option_takes_value(token, opts_with_values, opt_count)) {
                skip_next = true;
            }
            continue;
        }

        out_paths[count++] = token;
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
            nmo_tool_text_output_ctx_t text_ctx = {
                .out = out,
                .colorize = colorize,
                .user_data = user_data
            };

            if (i > 0) {
                fprintf(out, "\n");
            }
            fprintf(out, "%s--- [%zu/%zu] %s ---%s\n",
                    colorize ? NMO_CLI_COLOR_CYAN : "",
                    i + 1, file_count, path,
                    colorize ? NMO_CLI_COLOR_RESET : "");

            int rc = handler(path, global, &text_ctx, NULL, NULL);
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

/* ============================================================================
 * Batch write support
 * ============================================================================ */

int nmo_tool_expand_output_template(
    const char *input_path,
    const char *output_template,
    char *out_buf,
    size_t out_buf_size)
{
    if (!input_path || !output_template || !out_buf || out_buf_size == 0) {
        return -1;
    }

    /* Extract basename without extension */
    const char *slash = strrchr(input_path, '/');
    const char *bslash = strrchr(input_path, '\\');
    const char *base = input_path;
    if (slash && (!bslash || slash > bslash)) base = slash + 1;
    else if (bslash) base = bslash + 1;

    char basename[256];
    snprintf(basename, sizeof(basename), "%s", base);
    char *dot = strrchr(basename, '.');
    if (dot) *dot = '\0';

    /* Find {} in template and replace */
    const char *placeholder = strstr(output_template, "{}");
    if (!placeholder) {
        snprintf(out_buf, out_buf_size, "%s", output_template);
        return 0;
    }

    size_t prefix_len = (size_t)(placeholder - output_template);
    snprintf(out_buf, out_buf_size, "%.*s%s%s",
             (int)prefix_len, output_template,
             basename,
             placeholder + 2);
    return 0;
}

int nmo_tool_batch_write_run(
    const char **file_paths,
    size_t file_count,
    const char *output_template,
    const nmo_cli_global_opts_t *global,
    const char *command,
    nmo_batch_write_handler_t handler,
    void *user_data)
{
    if (!file_paths || file_count == 0 || !global || !handler || !output_template) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Check: if multiple files and no {} in template, error */
    if (file_count > 1 && !strstr(output_template, "{}")) {
        fprintf(stderr, "Error: Output template must contain {} when processing multiple files\n");
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

    for (size_t i = 0; i < file_count; ++i) {
        const char *path = file_paths[i];

        /* Expand output path from template */
        char output_path[1024];
        nmo_tool_expand_output_template(path, output_template, output_path, sizeof(output_path));

        if (is_json) {
            yyjson_mut_val *result_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_str(doc, result_obj, "file", path);
            yyjson_mut_obj_add_str(doc, result_obj, "output", output_path);

            yyjson_mut_val *data_obj = yyjson_mut_obj(doc);
            int rc = handler(path, output_path, global, user_data, doc, data_obj);

            yyjson_mut_obj_add_bool(doc, result_obj, "success", rc == NMO_CLI_EXIT_SUCCESS);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                yyjson_mut_obj_add_int(doc, result_obj, "exit_code", rc);
                failed++;
            } else {
                succeeded++;
            }
            yyjson_mut_obj_add_val(doc, result_obj, "data", data_obj);
            yyjson_mut_arr_append(results_arr, result_obj);

            if (rc > worst_exit) worst_exit = rc;
        } else {
            bool colorize = nmo_cli_should_colorize(global, out);

            if (i > 0) fprintf(out, "\n");
            fprintf(out, "%s--- [%zu/%zu] %s -> %s ---%s\n",
                    colorize ? NMO_CLI_COLOR_CYAN : "",
                    i + 1, file_count, path, output_path,
                    colorize ? NMO_CLI_COLOR_RESET : "");

            nmo_tool_text_output_ctx_t text_ctx = {
                .out = out,
                .colorize = colorize,
                .user_data = user_data
            };

            int rc = handler(path, output_path, global, &text_ctx, NULL, NULL);
            if (rc != NMO_CLI_EXIT_SUCCESS) failed++;
            else succeeded++;

            if (rc > worst_exit) worst_exit = rc;
        }
    }

    if (is_json) {
        yyjson_mut_val *envelope = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, envelope, "schema_version", NMO_CLI_JSON_SCHEMA_VERSION);
        yyjson_mut_obj_add_str(doc, envelope, "tool", "nmo");
        yyjson_mut_obj_add_str(doc, envelope, "command", command);
        yyjson_mut_obj_add_bool(doc, envelope, "batch_mode", true);
        yyjson_mut_obj_add_val(doc, envelope, "results", results_arr);

        yyjson_mut_val *summary = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, summary, "total", (uint64_t)file_count);
        yyjson_mut_obj_add_uint(doc, summary, "succeeded", (uint64_t)succeeded);
        yyjson_mut_obj_add_uint(doc, summary, "failed", (uint64_t)failed);
        yyjson_mut_obj_add_val(doc, envelope, "summary", summary);

        yyjson_mut_doc_set_root(doc, envelope);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        bool colorize = nmo_cli_should_colorize(global, out);
        fprintf(out, "\n%s=== Batch Write Summary ===%s\n",
                colorize ? NMO_CLI_COLOR_BOLD : "",
                colorize ? NMO_CLI_COLOR_RESET : "");
        fprintf(out, "Total: %zu  Succeeded: %zu  Failed: %zu\n",
                file_count, succeeded, failed);
    }

    nmo_cli_close_output_stream(global, out);
    return worst_exit;
}
