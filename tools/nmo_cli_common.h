/**
 * @file nmo_cli_common.h
 * @brief CLI global options and common utilities
 */

#ifndef NMO_CLI_COMMON_H
#define NMO_CLI_COMMON_H

#include "nmo.h"
#include "nmo_opt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_type_registry nmo_type_registry_t;

/* Exit codes */
#define NMO_CLI_EXIT_SUCCESS         0  /**< Success */
#define NMO_CLI_EXIT_ARG_ERROR       1  /**< Argument/usage error */
#define NMO_CLI_EXIT_IO_ERROR        2  /**< I/O error */
#define NMO_CLI_EXIT_STRICT_FAILURE  3  /**< Strict mode failure */
#define NMO_CLI_EXIT_WARNING         4  /**< --fail-on-warning triggered */
#define NMO_CLI_EXIT_INTERNAL_ERROR  5  /**< Internal error */
#define NMO_CLI_EXIT_NOT_FOUND       6  /**< Object/resource not found */

/** Maximum number of --plugin options */
#define NMO_CLI_MAX_PLUGINS 16

/**
 * @brief Output format for CLI commands
 */
typedef enum {
    NMO_CLI_FORMAT_TEXT = 0,      /**< Human-readable text (default) */
    NMO_CLI_FORMAT_JSON,          /**< Compact JSON */
    NMO_CLI_FORMAT_JSON_PRETTY    /**< Pretty-printed JSON */
} nmo_cli_format_t;

/**
 * @brief Color output mode
 */
typedef enum {
    NMO_CLI_COLOR_AUTO = 0,   /**< Auto-detect TTY */
    NMO_CLI_COLOR_ALWAYS,     /**< Always use colors */
    NMO_CLI_COLOR_NEVER       /**< Never use colors */
} nmo_cli_color_mode_t;

/**
 * @brief Global CLI options (parsed before group/action dispatch)
 */
typedef struct nmo_cli_global_opts {
    size_t struct_size;               /**< Size of this options structure */
    nmo_cli_format_t format;          /**< Output format */
    nmo_cli_color_mode_t color_mode;  /**< Color mode */
    const char *output_path;          /**< Output file path (NULL = stdout) */
    int verbosity;                    /**< Verbosity level (0 = normal, >0 = verbose, <0 = quiet) */
    bool quiet;                       /**< Suppress non-essential output */
    bool fail_on_warning;             /**< validate: exit with code 4 on warnings */
    bool strict_mode;                 /**< Exit with code 3 on validation failures */
    bool show_help;                   /**< --help was specified */
    bool show_version;                /**< --version was specified */

    /* Extension loading */
    const char *plugin_paths[NMO_CLI_MAX_PLUGINS]; /**< --plugin paths */
    size_t plugin_count;              /**< Number of --plugin paths */

    /* Batch processing */
    bool batch_mode;                  /**< --batch: process multiple files */

} nmo_cli_global_opts_t;

/**
 * @brief Initialize global options to defaults
 * @param opts Options structure to initialize
 */
void nmo_cli_global_opts_init(nmo_cli_global_opts_t *opts);

/**
 * @brief Parse global options from command line
 *
 * Parses options like --format, --color, -o, -v, -q, etc.
 * Stops at first non-option argument or "--".
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @param opts Output: parsed options
 * @return Index of first non-global argument, or -1 on error
 */
int nmo_cli_parse_global_opts(int argc, char **argv, nmo_cli_global_opts_t *opts);

/**
 * @brief Parse one global option that follows the action.
 *
 * Accepts -f/--format, --color, -v/--verbose, -q/--quiet, --strict,
 * --fail-on-warning and --plugin. -o/--output, --batch, -h and -V are only
 * global before the command group. Matches nmo_opt_fallback_fn.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 * @param index Index of the option token
 * @param opts  nmo_cli_global_opts_t to update
 * @return Tokens consumed, 0 if argv[index] is not such an option, -1 on error
 */
int nmo_cli_parse_trailing_global_opt(int argc, char **argv, int index,
                                      void *opts);

/**
 * @brief Parse command-local argv, also accepting trailing global options.
 *
 * Options in @p defs take precedence over global options of the same
 * spelling. Global options found in argv update @p global.
 *
 * @param global Global options to update, or NULL to accept only @p defs
 * @return NMO_CLI_EXIT_SUCCESS, or NMO_CLI_EXIT_ARG_ERROR (message printed)
 */
int nmo_cli_parse_args(int argc, char **argv,
                       const nmo_opt_def_t *defs, size_t def_count,
                       nmo_opt_result_t *result,
                       nmo_cli_global_opts_t *global);

/**
 * @brief Check if color output should be enabled
 * @param opts Global options
 * @param stream Output stream (for TTY detection)
 * @return true if colors should be used
 */
bool nmo_cli_should_colorize(const nmo_cli_global_opts_t *opts, FILE *stream);

/**
 * @brief Get output stream based on global options
 *
 * Opens output file if specified, otherwise returns stdout.
 * Caller must close returned stream if output_path was set.
 *
 * @param opts Global options
 * @param out_error Receives a malloc'd message on failure (may be NULL); free with free()
 * @return Output stream, or NULL on error
 */
FILE *nmo_cli_get_output_stream(const nmo_cli_global_opts_t *opts, char **out_error);

/**
 * @brief Close output stream if it was opened by nmo_cli_get_output_stream
 * @param opts Global options
 * @param stream Stream to potentially close
 */
void nmo_cli_close_output_stream(const nmo_cli_global_opts_t *opts, FILE *stream);

/**
 * @brief Report that a command needs its own -o/--output.
 *
 * Prints "Error: <message>". When a global -o was given before the command
 * group, also explains that it only redirects the command's report.
 *
 * @param opts    Global options (may be NULL)
 * @param message Error text without the "Error: " prefix
 */
void nmo_cli_print_missing_output(const nmo_cli_global_opts_t *opts,
                                  const char *message);

/* ============================================================================
 * Type system helpers
 * ============================================================================ */

/**
 * @brief Get class name from class ID using context's type registry
 * @param ctx Context with type registry
 * @param class_id Class ID
 * @return Class name (type registry string) or NULL if not found
 * @note Falls back to inherited class with a registered type if needed.
 */
const char *nmo_cli_class_name_from_id(nmo_context_t *ctx, nmo_class_id_t class_id);

/**
 * @brief Get class ID from class name using context's type registry
 * @param ctx Context with type registry
 * @param name Class name
 * @return Class ID or 0 if not found
 */
nmo_class_id_t nmo_cli_class_id_from_name(nmo_context_t *ctx, const char *name);

/**
 * @brief Get parent class ID
 * @param ctx Context with type registry
 * @param class_id Class ID
 * @return Parent class ID or 0 if root/not found
 * @note Uses the type registry base_type chain (no class hierarchy tables).
 */
nmo_class_id_t nmo_cli_class_get_parent(nmo_context_t *ctx, nmo_class_id_t class_id);

/**
 * @brief Check if class is derived from another
 * @param ctx Context with type registry
 * @param class_id Class ID to check
 * @param base_id Base class ID
 * @return true if class_id derives from base_id
 */
bool nmo_cli_class_is_derived_from(nmo_context_t *ctx, nmo_class_id_t class_id, nmo_class_id_t base_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CLI_COMMON_H */
