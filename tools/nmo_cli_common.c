/**
 * @file nmo_cli_common.c
 * @brief CLI global options parsing and common utilities
 */

#include "nmo_cli_common.h"
#include "nmo_tool_common.h"
#include "app/nmo_type_query.h"

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
    if (nmo_tool_streq_ci(str, "yaml")) {
        *out = NMO_CLI_FORMAT_YAML;
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

/**
 * Check if argument is a long option with value (--opt=value)
 */
static const char *get_long_opt_value(const char *arg, const char *opt) {
    size_t opt_len = strlen(opt);
    if (strncmp(arg, opt, opt_len) == 0 && arg[opt_len] == '=') {
        return arg + opt_len + 1;
    }
    return NULL;
}

int nmo_cli_parse_global_opts(int argc, char **argv, nmo_cli_global_opts_t *opts) {
    if (!opts) {
        return -1;
    }
    nmo_cli_global_opts_init(opts);

    int i = 1; /* Skip program name */
    while (i < argc) {
        const char *arg = argv[i];

        /* End of options */
        if (strcmp(arg, "--") == 0) {
            return i + 1;
        }

        /* Not an option */
        if (arg[0] != '-') {
            return i;
        }

        /* Help */
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            opts->show_help = true;
            i++;
            continue;
        }

        /* Version */
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            opts->show_version = true;
            i++;
            continue;
        }

        /* Verbosity */
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opts->verbosity++;
            i++;
            continue;
        }

        /* Quiet */
        if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            opts->quiet = true;
            opts->verbosity--;
            i++;
            continue;
        }

        /* No pager */
        if (strcmp(arg, "--no-pager") == 0) {
            opts->no_pager = true;
            i++;
            continue;
        }

        /* Fail on warning */
        if (strcmp(arg, "--fail-on-warning") == 0) {
            opts->fail_on_warning = true;
            i++;
            continue;
        }

        /* Strict mode */
        if (strcmp(arg, "--strict") == 0) {
            opts->strict_mode = true;
            i++;
            continue;
        }

        /* Format: --format=value or --format value */
        const char *format_val = get_long_opt_value(arg, "--format");
        if (format_val) {
            if (!parse_format(format_val, &opts->format)) {
                fprintf(stderr, "Error: Invalid format '%s'\n", format_val);
                return -1;
            }
            i++;
            continue;
        }
        if (strcmp(arg, "--format") == 0 || strcmp(arg, "-f") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a value\n", arg);
                return -1;
            }
            if (!parse_format(argv[i + 1], &opts->format)) {
                fprintf(stderr, "Error: Invalid format '%s'\n", argv[i + 1]);
                return -1;
            }
            i += 2;
            continue;
        }

        /* Color: --color=value or --color value */
        const char *color_val = get_long_opt_value(arg, "--color");
        if (color_val) {
            if (!parse_color_mode(color_val, &opts->color_mode)) {
                fprintf(stderr, "Error: Invalid color mode '%s'\n", color_val);
                return -1;
            }
            i++;
            continue;
        }
        if (strcmp(arg, "--color") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --color requires a value\n");
                return -1;
            }
            if (!parse_color_mode(argv[i + 1], &opts->color_mode)) {
                fprintf(stderr, "Error: Invalid color mode '%s'\n", argv[i + 1]);
                return -1;
            }
            i += 2;
            continue;
        }

        /* Output file: -o path or --output=path */
        const char *output_val = get_long_opt_value(arg, "--output");
        if (output_val) {
            opts->output_path = output_val;
            i++;
            continue;
        }
        if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a path\n", arg);
                return -1;
            }
            opts->output_path = argv[i + 1];
            i += 2;
            continue;
        }

        /* Locale: --locale=value */
        const char *locale_val = get_long_opt_value(arg, "--locale");
        if (locale_val) {
            opts->locale = locale_val;
            i++;
            continue;
        }
        if (strcmp(arg, "--locale") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --locale requires a value\n");
                return -1;
            }
            opts->locale = argv[i + 1];
            i += 2;
            continue;
        }

        /* Encoding: --encoding=value */
        const char *encoding_val = get_long_opt_value(arg, "--encoding");
        if (encoding_val) {
            opts->encoding = encoding_val;
            i++;
            continue;
        }
        if (strcmp(arg, "--encoding") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --encoding requires a value\n");
                return -1;
            }
            opts->encoding = argv[i + 1];
            i += 2;
            continue;
        }

        /* Plugin: --plugin path (repeatable) */
        const char *plugin_val = get_long_opt_value(arg, "--plugin");
        if (plugin_val) {
            if (opts->plugin_count < 16) {
                opts->plugin_paths[opts->plugin_count++] = plugin_val;
            }
            i++;
            continue;
        }
        if (strcmp(arg, "--plugin") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --plugin requires a path\n");
                return -1;
            }
            if (opts->plugin_count < 16) {
                opts->plugin_paths[opts->plugin_count++] = argv[i + 1];
            }
            i += 2;
            continue;
        }

        /* Batch mode */
        if (strcmp(arg, "--batch") == 0) {
            opts->batch_mode = true;
            i++;
            continue;
        }

        /* Filter: --filter/-F pattern */
        const char *filter_val = get_long_opt_value(arg, "--filter");
        if (filter_val) {
            opts->filter_pattern = filter_val;
            i++;
            continue;
        }
        if (strcmp(arg, "--filter") == 0 || strcmp(arg, "-F") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires a pattern\n", arg);
                return -1;
            }
            opts->filter_pattern = argv[i + 1];
            i += 2;
            continue;
        }

        /* Unknown option - stop parsing, let subcommand handle it */
        return i;
    }

    return i;
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

FILE *nmo_cli_get_output_stream(const nmo_cli_global_opts_t *opts, char *errbuf, size_t errbuf_size) {
    if (!opts) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Invalid options");
        }
        return NULL;
    }

    if (!opts->output_path) {
        return stdout;
    }

    FILE *fp = fopen(opts->output_path, "w");
    if (!fp) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "Cannot open '%s' for writing", opts->output_path);
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
    return nmo_type_query_class_name_from_id(ctx, class_id);
}

nmo_class_id_t nmo_cli_class_id_from_name(nmo_context_t *ctx, const char *name) {
    return nmo_type_query_class_id_from_name(ctx, name);
}

nmo_class_id_t nmo_cli_class_get_parent(nmo_context_t *ctx, nmo_class_id_t class_id) {
    return nmo_type_query_class_get_parent(ctx, class_id);
}

bool nmo_cli_class_is_derived_from(nmo_context_t *ctx, nmo_class_id_t class_id, nmo_class_id_t base_id) {
    return nmo_type_query_class_is_derived_from(ctx, class_id, base_id);
}
