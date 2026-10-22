/**
 * @file nmo_cmd_ctx.h
 * @brief Command context framework - eliminates per-command boilerplate
 *
 * Every CLI command follows the same lifecycle:
 *   1. Parse argv against the command's option table (the file is the
 *      last positional argument)
 *   2. Open document/workspace
 *   3. Open output stream
 *   4. Determine colorize / JSON mode
 *   5. ... command logic ...
 *   6. Close output stream
 *   7. Close document/workspace
 *
 * nmo_cmd_ctx_begin() + nmo_cmd_ctx_parse() cover step 1,
 * nmo_cmd_ctx_open() steps 2-4, and nmo_cmd_ctx_done() steps 6-7.
 */

#ifndef NMO_CMD_CTX_H
#define NMO_CMD_CTX_H

#include "nmo_cli_common.h"
#include "nmo_cli_json.h"
#include "nmo_cli_record.h"
#include "nmo_opt.h"
#include "nmo_tool_common.h"
#include "nmo_tool_owner.h"
#include "nmo_tool_session.h"

#include "nmo.h"

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_type_registry nmo_type_registry_t;

typedef enum nmo_cmd_ctx_state {
    NMO_CMD_CTX_READY = 0,        /**< Usable; nmo_cmd_ctx_open is a no-op */
    NMO_CMD_CTX_PENDING_FILE,     /**< Begun; open the last positional as the file */
    NMO_CMD_CTX_PENDING_NO_FILE   /**< Begun; open only the output stream */
} nmo_cmd_ctx_state_t;

typedef struct nmo_cmd_ctx {
    /* Input */
    const nmo_cli_global_opts_t *global;
    const char *file_path;

    /* CLI commands started with nmo_cmd_ctx_begin*() */
    nmo_cmd_ctx_state_t state;
    nmo_cli_global_opts_t global_opts; /**< Copy that trailing global options update */
    const nmo_load_options_t *load_options; /**< Used by nmo_cmd_ctx_open (may be NULL) */

    /* Document/workspace (opened by init, closed by done) */
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    bool owns_document;

    /* Output (opened by init, closed by done) */
    FILE *out;
    bool owns_output;
    bool colorize;
    bool is_json;

    /* Cached accessors (set by init after session open) */
    const nmo_type_registry_t *registry;
    nmo_load_diagnostics_t *load_diagnostics; /**< Borrowed from load options. */
} nmo_cmd_ctx_t;

typedef struct nmo_cmd_in_session_result {
    bool changed;
    bool dry_run;
} nmo_cmd_in_session_result_t;

typedef enum nmo_cmd_source_kind {
    NMO_CMD_SOURCE_EXPLICIT_FILE = 0,
    NMO_CMD_SOURCE_DOCUMENT,
    NMO_CMD_SOURCE_NO_DOCUMENT
} nmo_cmd_source_kind_t;

typedef struct nmo_cmd_source {
    nmo_cmd_source_kind_t kind;
    const char *file_path;
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    const char *source_label;
    const nmo_load_options_t *load_options;
} nmo_cmd_source_t;

/**
 * @brief Begin a CLI command whose last positional argument is the file.
 *
 * Nothing is opened yet. Call nmo_cmd_ctx_parse(), then nmo_cmd_ctx_open().
 * c->global points at a copy of @p global inside @p c, so do not move a
 * begun context. Set c->load_options before opening to change how the
 * file is loaded.
 *
 * @param c      Context to begin (caller-allocated)
 * @param global Global options parsed before the group (may be NULL)
 */
void nmo_cmd_ctx_begin(nmo_cmd_ctx_t *c, const nmo_cli_global_opts_t *global);

/**
 * @brief Begin a CLI command that opens no document (output stream only).
 */
void nmo_cmd_ctx_begin_no_file(nmo_cmd_ctx_t *c,
                               const nmo_cli_global_opts_t *global);

/**
 * @brief Parse a command's argv.
 *
 * For a begun context, also accepts global options after the action (an
 * option in @p defs wins over a global one of the same spelling) and, for
 * nmo_cmd_ctx_begin(), records the last positional as c->file_path. The
 * file stays in result->pos_args. For other contexts this is
 * nmo_opt_parse().
 *
 * @return NMO_CLI_EXIT_SUCCESS, or NMO_CLI_EXIT_ARG_ERROR (message printed)
 */
int nmo_cmd_ctx_parse(nmo_cmd_ctx_t *c, int argc, char **argv,
                      const nmo_opt_def_t *defs, size_t def_count,
                      nmo_opt_result_t *result);


/**
 * @brief Open what a begun context needs: the file (if any) and the output.
 *
 * A no-op for contexts that are already usable, such as REPL sessions.
 *
 * @return NMO_CLI_EXIT_SUCCESS or an NMO_CLI_EXIT_* code (message printed)
 */
int nmo_cmd_ctx_open(nmo_cmd_ctx_t *c);

/**
 * @brief nmo_cmd_ctx_parse() followed by nmo_cmd_ctx_open().
 */
int nmo_cmd_ctx_parse_and_open(nmo_cmd_ctx_t *c, int argc, char **argv,
                               const nmo_opt_def_t *defs, size_t def_count,
                               nmo_opt_result_t *result);

/**
 * @brief Open a command that takes no options or arguments of its own.
 *
 * A begun file command must get exactly the file; other contexts must get
 * no positional argument. Global options after the action are accepted.
 *
 * @param usage Printed after an unexpected argument (may be NULL)
 */
int nmo_cmd_ctx_open_without_args(nmo_cmd_ctx_t *c, int argc, char **argv,
                                  const char *usage);

/**
 * @brief Run a --batch command over its positional arguments.
 *
 * Every positional argument is a file; global options after the action are
 * accepted. Calls nmo_tool_batch_run() with the files.
 *
 * @param usage Printed when no file is given, e.g. "nmo --batch file info <file1> <file2> ..."
 */
int nmo_cmd_batch_run_files(int argc, char **argv,
                            const nmo_cli_global_opts_t *global,
                            const char *usage, const char *cmd_name,
                            nmo_batch_handler_t handler, void *user_data);

/**
 * @brief Initialize command context from an explicit frontend source.
 *
 * On failure, prints an error to stderr and returns an NMO_CLI_EXIT_* code.
 */
int nmo_cmd_ctx_init_from_source(nmo_cmd_ctx_t *c,
                                 const nmo_cli_global_opts_t *global,
                                 const nmo_cmd_source_t *source);

/**
 * @brief Initialize context with an explicit file path.
 *
 * For commands that parsed argv themselves, such as write commands that
 * take the input file from their positional arguments.
 */
int nmo_cmd_ctx_init_with_file(nmo_cmd_ctx_t *c, const char *file_path,
                               const nmo_cli_global_opts_t *global);

/**
 * @brief Initialize context from a borrowed, already-open document/workspace.
 *
 * Opens the output stream from global options, but does not own or close the
 * supplied owners. source_label is used in reports and JSON envelopes.
 */
int nmo_cmd_ctx_init_with_document(nmo_cmd_ctx_t *c,
                                   nmo_context_t *ctx,
                                   nmo_document_t *document,
                                   nmo_workspace_t *workspace,
                                   const char *source_label,
                                   const nmo_cli_global_opts_t *global);

/**
 * @brief Initialize context for commands that don't need a file/session.
 *
 * Opens output stream only. c->ctx, c->document, c->workspace, c->registry are NULL.
 *
 * @param c         Context to initialize
 * @param global    Parsed global options
 * @return 0 on success, NMO_CLI_EXIT_* on failure
 */
int nmo_cmd_ctx_init_no_file(nmo_cmd_ctx_t *c,
                             const nmo_cli_global_opts_t *global);

/**
 * @brief Initialize context from already-open document owners (for REPL use).
 *
 * Does NOT open or close the document/workspace. Output goes to stdout.
 * This allows REPL commands to build a nmo_cmd_ctx_t that can be passed
 * to nmo_cmd_core functions without duplicating logic.
 *
 * @param c         Context to initialize
 * @param ctx       Existing nmo_context_t (from REPL)
 * @param document  Existing document (from REPL)
 * @param workspace Existing workspace (from REPL)
 * @param colorize  Whether to use ANSI colors
 */
void nmo_cmd_ctx_init_from_repl_document(nmo_cmd_ctx_t *c,
                                         nmo_context_t *ctx,
                                         nmo_document_t *document,
                                         nmo_workspace_t *workspace,
                                         bool colorize);

/**
 * @brief Finalize command context: close output stream, close document/workspace.
 *
 * Safe to call even if init failed (handles NULL document/output).
 * Does NOT close document/output for contexts created from REPL owners.
 *
 * @param c           Context to finalize
 * @param exit_code   Exit code to pass through (returned as-is)
 * @return exit_code (pass-through for convenient `return nmo_cmd_ctx_done(&c, 0)`)
 */
int nmo_cmd_ctx_done(nmo_cmd_ctx_t *c, int exit_code);

/**
 * @brief Begin a JSON document for command output.
 * @param c Command context
 * @return New mutable JSON document, or NULL on error
 */
yyjson_mut_doc *nmo_cmd_ctx_json_begin(nmo_cmd_ctx_t *c);

/**
 * @brief Finalize JSON output: add envelope, write to c->out, free doc.
 *
 * @param c         Command context
 * @param doc       JSON document (freed by this call)
 * @param data      Top-level data object
 * @param cmd_name  Command name for envelope (e.g. "object.list")
 * @return 0 on success, NMO_CLI_EXIT_INTERNAL_ERROR on failure
 */
int nmo_cmd_ctx_json_end(nmo_cmd_ctx_t *c, yyjson_mut_doc *doc,
                         yyjson_mut_val *data, const char *cmd_name);

/**
 * @brief Write a record as the command output and free it.
 *
 * JSON mode: the record's JSON fields become the envelope data. Text mode:
 * nmo_cli_record_print_kv(rec, c->out, key_width, colorize).
 *
 * @param c          Command context
 * @param rec        Record (freed by this call; NULL reports an internal error)
 * @param cmd_name   Command name for the JSON envelope
 * @param key_width  Text label column width
 * @param colorize   Whether text labels are colored
 * @return 0 on success, NMO_CLI_EXIT_INTERNAL_ERROR on failure
 */
int nmo_cmd_ctx_emit_record(nmo_cmd_ctx_t *c, nmo_cli_record_t *rec,
                            const char *cmd_name, int key_width,
                            bool colorize);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_CTX_H */
