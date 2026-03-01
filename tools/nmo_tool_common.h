#ifndef NMO_TOOL_COMMON_H
#define NMO_TOOL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Case-insensitive string compare (ASCII, locale-independent). */
int nmo_tool_stricmp(const char *a, const char *b);

/** Case-insensitive equality check (ASCII, locale-independent). */
bool nmo_tool_streq_ci(const char *a, const char *b);

/** Simple wildcard matcher supporting '*' and '?' (case-insensitive ASCII). */
bool nmo_tool_match_wildcard_ci(const char *pattern, const char *value);

/** Heap-duplicate a string (malloc). Returns NULL on OOM or if src is NULL. */
char *nmo_tool_strdup(const char *src);

/** Parse an unsigned 32-bit decimal integer. Returns false on failure. */
bool nmo_tool_parse_u32_dec(const char *text, uint32_t *out);

/** Parse an unsigned 32-bit integer (base 0: supports 123, 0x7B). Returns false on failure. */
bool nmo_tool_parse_u32(const char *text, uint32_t *out);

/** Parse a decimal size_t. Returns false on failure. */
bool nmo_tool_parse_size_dec(const char *text, size_t *out);

/** Parse a size_t (base 0: supports 123, 0x7B). Returns false on failure. */
bool nmo_tool_parse_size(const char *text, size_t *out);

/* ============================================================================
 * Argument Parsing Helpers (centralized to avoid duplication)
 * ============================================================================ */

/**
 * Find the first positional (non-option) argument.
 * Use for commands with a single file argument: `nmo file info <file>`.
 */
const char *nmo_tool_find_file_arg(int argc, char **argv);

/**
 * Find the last positional (non-option) argument.
 * Use for commands where the file is the trailing arg:
 * `nmo object list --class Foo <file>`.
 */
const char *nmo_tool_find_file_arg_last(int argc, char **argv);

/**
 * Collect all positional (non-option) arguments into out_paths.
 * Returns the number of paths written (up to max_count).
 * Skips argv[0] (the action name).
 */
size_t nmo_tool_find_file_args(int argc, char **argv,
                               const char **out_paths, size_t max_count);

/**
 * Look up a named option value.
 * Searches for `opt1 <value>` or `opt2 <value>` in argv.
 * Either opt1 or opt2 may be NULL.
 * Returns the value string or NULL if not found.
 */
const char *nmo_tool_find_opt_value(int argc, char **argv,
                                    const char *opt1, const char *opt2);

/**
 * Check whether a boolean flag is present in argv.
 * Returns true if flag1 or flag2 is found.
 * Either flag1 or flag2 may be NULL.
 */
bool nmo_tool_has_flag(int argc, char **argv,
                       const char *flag1, const char *flag2);

/**
 * Sanitize a string for use as a file name.
 * Replaces path separators and control characters with '_'.
 * Falls back to "resource_<index>.bin" if the name is empty.
 */
void nmo_tool_sanitize_filename(char *dst, size_t dst_size,
                                const char *name, uint32_t index);

/* ============================================================================
 * Batch Processing Framework
 * ============================================================================ */

/* Forward declarations */
struct nmo_cli_global_opts;
struct yyjson_mut_doc;
struct yyjson_mut_val;

/**
 * Per-file handler for batch processing.
 * Called once per file. Should populate result_data with command-specific JSON.
 * Returns an NMO_CLI_EXIT_* code (0 = success).
 */
typedef int (*nmo_batch_handler_t)(
    const char *file_path,
    const struct nmo_cli_global_opts *global,
    void *user_data,
    struct yyjson_mut_doc *doc,
    struct yyjson_mut_val *result_data);

/**
 * Run a command handler over multiple files.
 *
 * In JSON mode, produces:
 *   { "schema_version": ..., "command": ..., "batch_mode": true,
 *     "results": [ { "file": ..., "success": bool, "data": {...} }, ... ],
 *     "summary": { "total": N, "succeeded": N, "failed": N } }
 *
 * In text mode, prints a per-file heading followed by handler output,
 * then a summary line.
 *
 * @param file_paths  Array of file path strings
 * @param file_count  Number of files
 * @param global      Global options
 * @param command     Command name for envelope (e.g. "validate.all")
 * @param handler     Per-file handler
 * @param user_data   Opaque pointer forwarded to handler
 * @return Worst exit code across all files
 */
int nmo_tool_batch_run(
    const char **file_paths,
    size_t file_count,
    const struct nmo_cli_global_opts *global,
    const char *command,
    nmo_batch_handler_t handler,
    void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TOOL_COMMON_H */
