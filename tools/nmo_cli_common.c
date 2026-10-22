/**
 * @file nmo_cli_common.c
 * @brief CLI global options parsing and common utilities
 */

#include "nmo_cli_common.h"
#include "nmo_tool_common.h"
#include "nmo_opt.h"
#include "type/nmo_type_query.h"

#include <string.h>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#endif

static int nmo_cli_stream_isatty(FILE *stream) {
    if (stream == stdout) {
#ifdef _WIN32
        return isatty(_fileno(stdout));
#else
        return isatty(STDOUT_FILENO);
#endif
    }
    if (stream == stderr) {
#ifdef _WIN32
        return isatty(_fileno(stderr));
#else
        return isatty(STDERR_FILENO);
#endif
    }
    return 0;
}

void nmo_cli_global_opts_init(nmo_cli_global_opts_t *opts) {
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->struct_size = sizeof(*opts);
    opts->format = NMO_CLI_FORMAT_TEXT;
    opts->color_mode = NMO_CLI_COLOR_AUTO;
    opts->verbosity = 0;
}

/**
 * Parse format string to enum
 */
static bool parse_format(const char *str, nmo_cli_format_t *out) {
    if (!str || !out) {
        return false;
    }
    if (nmo_tool_streq_ci(str, "text")) {
        *out = NMO_CLI_FORMAT_TEXT;
        return true;
    }
    if (nmo_tool_streq_ci(str, "json")) {
        *out = NMO_CLI_FORMAT_JSON;
        return true;
    }
    if (nmo_tool_streq_ci(str, "json-pretty") || nmo_tool_streq_ci(str, "pretty")) {
        *out = NMO_CLI_FORMAT_JSON_PRETTY;
        return true;
    }
    return false;
}

/**
 * Parse color mode string to enum
 */
static bool parse_color_mode(const char *str, nmo_cli_color_mode_t *out) {
    if (!str || !out) {
        return false;
    }
    if (nmo_tool_streq_ci(str, "auto")) {
        *out = NMO_CLI_COLOR_AUTO;
        return true;
    }
    if (nmo_tool_streq_ci(str, "always") || nmo_tool_streq_ci(str, "yes")) {
        *out = NMO_CLI_COLOR_ALWAYS;
        return true;
    }
    if (nmo_tool_streq_ci(str, "never") || nmo_tool_streq_ci(str, "no")) {
        *out = NMO_CLI_COLOR_NEVER;
        return true;
    }
    return false;
}

static bool add_plugin_path(nmo_cli_global_opts_t *opts, const char *path) {
    if (opts->plugin_count >= NMO_CLI_MAX_PLUGINS) {
        fprintf(stderr, "Error: Too many --plugin options (at most %d)\n",
                NMO_CLI_MAX_PLUGINS);
        return false;
    }
    opts->plugin_paths[opts->plugin_count++] = path;
    return true;
}

/**
 * Match @p arg against a value option spelled @p long_name or @p short_name
 * ("--name value", "--name=value", "-s value").
 *
 * @return Tokens consumed (1 or 2), 0 when @p arg is a different option,
 *         or -1 when the value is missing (message printed).
 */
static int match_value_opt(int argc, char **argv, int i,
                           const char *long_name, const char *short_name,
                           const char **out_value) {
    const char *arg = argv[i];
    size_t long_len = strlen(long_name);
    if (strncmp(arg, long_name, long_len) == 0 && arg[long_len] == '=') {
        *out_value = arg + long_len + 1;
        return 1;
    }
    if (strcmp(arg, long_name) != 0 &&
        (short_name == NULL || strcmp(arg, short_name) != 0)) {
        return 0;
    }
    if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires a value\n", arg);
        return -1;
    }
    *out_value = argv[i + 1];
    return 2;
}

static bool is_flag(const char *arg, const char *long_name, const char *short_name) {
    return strcmp(arg, long_name) == 0 ||
           (short_name != NULL && strcmp(arg, short_name) == 0);
}

/**
 * Parse one global option at argv[i].
 *
 * After the action only output and loading options are accepted: -o,
 * --batch, -h and -V must come before the group.
 *
 * @return Tokens consumed, 0 when argv[i] is not a global option, -1 on error.
 */
static int parse_global_opt(int argc, char **argv, int i,
                            nmo_cli_global_opts_t *opts, bool after_action) {
    const char *arg = argv[i];
    const char *value = NULL;
    int n = 0;

    if (is_flag(arg, "--verbose", "-v")) {
        opts->verbosity++;
        return 1;
    }
    if (is_flag(arg, "--quiet", "-q")) {
        opts->quiet = true;
        opts->verbosity--;
        return 1;
    }
    if (is_flag(arg, "--fail-on-warning", NULL)) {
        opts->fail_on_warning = true;
        return 1;
    }
    if (is_flag(arg, "--strict", NULL)) {
        opts->strict_mode = true;
        return 1;
    }
    if ((n = match_value_opt(argc, argv, i, "--format", "-f", &value)) != 0) {
        if (n > 0 && !parse_format(value, &opts->format)) {
            fprintf(stderr, "Error: Invalid format '%s'\n", value);
            return -1;
        }
        return n;
    }
    if ((n = match_value_opt(argc, argv, i, "--color", NULL, &value)) != 0) {
        if (n > 0 && !parse_color_mode(value, &opts->color_mode)) {
            fprintf(stderr, "Error: Invalid color mode '%s'\n", value);
            return -1;
        }
        return n;
    }
    if ((n = match_value_opt(argc, argv, i, "--plugin", NULL, &value)) != 0) {
        if (n > 0 && !add_plugin_path(opts, value)) {
            return -1;
        }
        return n;
    }
    if (after_action) {
        return 0;
    }

    if (is_flag(arg, "--help", "-h")) {
        opts->show_help = true;
        return 1;
    }
    if (is_flag(arg, "--version", "-V")) {
        opts->show_version = true;
        return 1;
    }
    if (is_flag(arg, "--batch", NULL)) {
        opts->batch_mode = true;
        return 1;
    }
    if ((n = match_value_opt(argc, argv, i, "--output", "-o", &value)) != 0) {
        if (n > 0) {
            opts->output_path = value;
        }
        return n;
    }
    return 0;
}

int nmo_cli_parse_global_opts(int argc, char **argv, nmo_cli_global_opts_t *opts) {
    if (!opts) {
        return -1;
    }
    nmo_cli_global_opts_init(opts);

    int i = 1; /* Skip program name */
    while (i < argc) {
        /* "--" ends global options; a non-option is the command group */
        if (strcmp(argv[i], "--") == 0) {
            return i + 1;
        }
        if (argv[i][0] != '-') {
            return i;
        }

        int n = parse_global_opt(argc, argv, i, opts, false);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            /* Unknown option: the dispatcher reports it */
            return i;
        }
        i += n;
    }

    return i;
}

int nmo_cli_parse_trailing_global_opt(int argc, char **argv, int index,
                                      void *opts) {
    if (opts == NULL || index < 0 || index >= argc) {
        return 0;
    }
    return parse_global_opt(argc, argv, index,
                            (nmo_cli_global_opts_t *)opts, true);
}

int nmo_cli_parse_args(int argc, char **argv,
                       const nmo_opt_def_t *defs, size_t def_count,
                       nmo_opt_result_t *result,
                       nmo_cli_global_opts_t *global) {
    result->fallback = global ? nmo_cli_parse_trailing_global_opt : NULL;
    result->fallback_user = global;
    int rc = nmo_opt_parse(argc, argv, defs, def_count, result);
    result->fallback = NULL;
    result->fallback_user = NULL;
    return rc < 0 ? NMO_CLI_EXIT_ARG_ERROR : NMO_CLI_EXIT_SUCCESS;
}

void nmo_cli_print_missing_output(const nmo_cli_global_opts_t *opts,
                                  const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    if (opts && opts->output_path) {
        fprintf(stderr,
                "Note: '-o %s' before the command group only redirects the report; "
                "put -o after the action to name the saved file.\n",
                opts->output_path);
    }
}

bool nmo_cli_should_colorize(const nmo_cli_global_opts_t *opts, FILE *stream) {
    if (!opts || !stream) {
        return false;
    }
    switch (opts->color_mode) {
    case NMO_CLI_COLOR_ALWAYS:
        return true;
    case NMO_CLI_COLOR_NEVER:
        return false;
    case NMO_CLI_COLOR_AUTO:
    default:
        return nmo_cli_stream_isatty(stream) != 0;
    }
}

FILE *nmo_cli_get_output_stream(const nmo_cli_global_opts_t *opts, char **out_error) {
    if (!opts) {
        if (out_error) {
            *out_error = nmo_tool_strdup("Invalid options");
        }
        return NULL;
    }

    if (!opts->output_path) {
        return stdout;
    }

    FILE *fp = fopen(opts->output_path, "w");
    if (!fp) {
        if (out_error) {
            *out_error = nmo_tool_strdup_fmt("Cannot open '%s' for writing", opts->output_path);
        }
        return NULL;
    }
    return fp;
}

void nmo_cli_close_output_stream(const nmo_cli_global_opts_t *opts, FILE *stream) {
    if (opts && opts->output_path && stream && stream != stdout && stream != stderr) {
        fclose(stream);
    }
}

const char *nmo_cli_class_name_from_id(nmo_context_t *ctx, nmo_class_id_t class_id) {
    return nmo_type_query_class_name_from_id(nmo_context_get_type_registry(ctx), class_id);
}

nmo_class_id_t nmo_cli_class_id_from_name(nmo_context_t *ctx, const char *name) {
    return nmo_type_query_class_id_from_name(nmo_context_get_type_registry(ctx), name);
}

nmo_class_id_t nmo_cli_class_get_parent(nmo_context_t *ctx, nmo_class_id_t class_id) {
    return nmo_type_query_class_get_parent(nmo_context_get_type_registry(ctx), class_id);
}

bool nmo_cli_class_is_derived_from(nmo_context_t *ctx, nmo_class_id_t class_id, nmo_class_id_t base_id) {
    return nmo_type_query_class_is_derived_from(nmo_context_get_type_registry(ctx), class_id, base_id);
}
