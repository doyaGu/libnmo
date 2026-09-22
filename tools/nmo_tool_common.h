#ifndef NMO_TOOL_COMMON_H
#define NMO_TOOL_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Case-insensitive string compare (ASCII, locale-independent). */
int nmo_tool_stricmp(const char *a, const char *b);

/** Case-insensitive equality check (ASCII, locale-independent). */
bool nmo_tool_streq_ci(const char *a, const char *b);

/** Simple wildcard matcher supporting '*' and '?' (case-insensitive ASCII). */
bool nmo_tool_match_wildcard_ci(const char *pattern, const char *value);

/**
 * Glob matching with capture groups (case-insensitive ASCII).
 *
 * Each '*' captures the text it matched; '?' matches one character without
 * capturing. On a match, *out_captures receives a malloc'd array holding one
 * malloc'd string per '*' (NULL when the pattern has no stars) and
 * *out_capture_count its length. Release it with nmo_tool_captures_free().
 * A NULL or empty pattern matches everything with zero captures.
 *
 * @return true if pattern matches value; false on mismatch or allocation failure
 */
bool nmo_tool_wildcard_capture_ci(const char *pattern, const char *value,
                                  char ***out_captures,
                                  size_t *out_capture_count);

/** Free an array returned by nmo_tool_wildcard_capture_ci (NULL is ignored). */
void nmo_tool_captures_free(char **captures, size_t count);

/**
 * Template substitution for batch rename.
 *
 * Placeholders: {0} = full_match, {1}-{N} = captures[0]-captures[N-1].
 * Literal braces: {{ produces {, }} produces }.
 *
 * @return A malloc'd result of exactly the needed size, or NULL when the
 *         template is NULL, malformed, references a missing capture, or
 *         memory is exhausted.
 */
char *nmo_tool_apply_rename_template(const char *tmpl,
                                     const char *full_match,
                                     const char *const *captures,
                                     size_t capture_count);

/** Heap-duplicate a string (malloc). Returns NULL on OOM or if src is NULL. */
char *nmo_tool_strdup(const char *src);

/** printf into a freshly malloc'd string of exactly the needed size. Returns NULL on OOM. */
char *nmo_tool_strdup_fmt(const char *format, ...);

/** va_list form of nmo_tool_strdup_fmt. */
char *nmo_tool_vstrdup_fmt(const char *format, va_list args);

/**
 * malloc'd copy of the calling thread's last libnmo error chain (an empty
 * string when none is recorded). Returns NULL on OOM; free with free().
 */
char *nmo_tool_last_error_chain_dup(void);

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
 * Collect positional arguments while skipping values for known value-taking options.
 *
 * `opts_with_values` should contain option names such as "--object" or "-o".
 * Tokens provided as values for those options are excluded from positional results.
 */
size_t nmo_tool_find_file_args_ex(int argc, char **argv,
                                  const char **out_paths, size_t max_count,
                                  const char *const *opts_with_values, size_t opt_count);

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

typedef struct nmo_tool_text_output_ctx {
    FILE *out;
    bool colorize;
    void *user_data;
} nmo_tool_text_output_ctx_t;

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

/**
 * Per-file handler for batch write processing.
 * Called once per file with computed output path.
 * Returns an NMO_CLI_EXIT_* code (0 = success).
 */
typedef int (*nmo_batch_write_handler_t)(
    const char *input_path,
    const char *output_path,
    const struct nmo_cli_global_opts *global,
    void *user_data,
    struct yyjson_mut_doc *doc,
    struct yyjson_mut_val *result_data);

/**
 * Expand output template: replace {} with input basename (no extension).
 * E.g. input="dir/file.cmo", template="{}.stripped.cmo" -> "file.stripped.cmo"
 *
 * @return 0 on success, -1 if no {} in template with multiple files
 */
int nmo_tool_expand_output_template(
    const char *input_path,
    const char *output_template,
    char *out_buf,
    size_t out_buf_size);

/**
 * Run a write command handler over multiple files with output template.
 * Similar to nmo_tool_batch_run but each file gets a computed output path.
 */
int nmo_tool_batch_write_run(
    const char **file_paths,
    size_t file_count,
    const char *output_template,
    const struct nmo_cli_global_opts *global,
    const char *command,
    nmo_batch_write_handler_t handler,
    void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* NMO_TOOL_COMMON_H */
