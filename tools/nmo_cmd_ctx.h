/**
 * @file nmo_cmd_ctx.h
 * @brief Command context framework - eliminates per-command boilerplate
 *
 * Every CLI command follows the same lifecycle:
 *   1. Find file argument
 *   2. Open session (context + session)
 *   3. Open output stream
 *   4. Determine colorize / JSON mode
 *   5. ... command logic ...
 *   6. Close output stream
 *   7. Close session
 *
 * nmo_cmd_ctx_t wraps steps 1-4 into nmo_cmd_ctx_init() and
 * steps 6-7 into nmo_cmd_ctx_done().
 */

#ifndef NMO_CMD_CTX_H
#define NMO_CMD_CTX_H

#include "nmo_cli_common.h"
#include "nmo_cli_json.h"
#include "nmo_tool_session.h"

#include "nmo.h"

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_type_registry nmo_type_registry_t;

typedef struct nmo_cmd_ctx {
    /* Input */
    const nmo_cli_global_opts_t *global;
    const char *file_path;

    /* Session (opened by init, closed by done) */
    nmo_context_t *ctx;
    nmo_session_t *session;
    bool owns_session;

    /* Output (opened by init, closed by done) */
    FILE *out;
    bool owns_output;
    bool colorize;
    bool is_json;

    /* Cached accessors (set by init after session open) */
    const nmo_type_registry_t *registry;
} nmo_cmd_ctx_t;

typedef struct nmo_cmd_in_session_result {
    bool changed;
    bool dry_run;
} nmo_cmd_in_session_result_t;

typedef int (*nmo_cmd_public_handler_t)(int argc, char **argv,
                                        const nmo_cli_global_opts_t *global);

typedef enum nmo_cmd_source_kind {
    NMO_CMD_SOURCE_FILE_OPERAND = 0,
    NMO_CMD_SOURCE_EXPLICIT_FILE,
    NMO_CMD_SOURCE_SESSION,
    NMO_CMD_SOURCE_NO_SESSION
} nmo_cmd_source_kind_t;

typedef struct nmo_cmd_source {
    nmo_cmd_source_kind_t kind;
    const char *file_path;
    nmo_context_t *ctx;
    nmo_session_t *session;
    const char *source_label;
    const nmo_load_options_t *load_options;
} nmo_cmd_source_t;

typedef struct nmo_cmd_invocation {
    nmo_cli_global_opts_t global;
    const nmo_cmd_source_t *source;
} nmo_cmd_invocation_t;

const nmo_cmd_source_t *nmo_cmd_global_source(
    const nmo_cli_global_opts_t *global);
bool nmo_cmd_global_uses_session_source(
    const nmo_cli_global_opts_t *global);

/**
 * @brief Initialize command context: find file arg, open session, open output.
 *
 * The file argument is found via nmo_tool_find_file_arg_last(argc, argv).
 * On failure, prints an error to stderr and returns an NMO_CLI_EXIT_* code.
 * On success, returns 0 and the caller can use c->session, c->out, etc.
 *
 * @param c         Context to initialize (caller-allocated, e.g. on stack)
 * @param argc      Argument count (command-local, after global option stripping)
 * @param argv      Argument vector
 * @param global    Parsed global options
 * @return 0 on success, NMO_CLI_EXIT_* on failure
 */
int nmo_cmd_ctx_init(nmo_cmd_ctx_t *c, int argc, char **argv,
                     const nmo_cli_global_opts_t *global);

/**
 * @brief Initialize command context from an explicit frontend source.
 */
int nmo_cmd_ctx_init_from_source(nmo_cmd_ctx_t *c,
                                 int argc,
                                 char **argv,
                                 const nmo_cli_global_opts_t *global,
                                 const nmo_cmd_source_t *source);

/**
 * @brief Initialize command context with explicit load options.
 */
int nmo_cmd_ctx_init_with_load_options(nmo_cmd_ctx_t *c, int argc, char **argv,
                                       const nmo_cli_global_opts_t *global,
                                       const nmo_load_options_t *options);

/**
 * @brief Initialize context with an explicit file path.
 *
 * For write commands where -o is a separate option and the input file
 * path is known before ctx_init. Avoids the auto-detect logic of
 * nmo_cmd_ctx_init that may pick up -o's value.
 */
int nmo_cmd_ctx_init_with_file(nmo_cmd_ctx_t *c, const char *file_path,
                               const nmo_cli_global_opts_t *global);

/**
 * @brief Initialize context from a borrowed, already-open session.
 *
 * Opens the output stream from global options, but does not own or close the
 * supplied session/context. source_label is used in reports and JSON envelopes.
 */
int nmo_cmd_ctx_init_with_session(nmo_cmd_ctx_t *c,
                                  nmo_context_t *ctx,
                                  nmo_session_t *session,
                                  const char *source_label,
                                  const nmo_cli_global_opts_t *global);

/**
 * @brief Initialize context for commands that don't need a file/session.
 *
 * Opens output stream only. c->ctx, c->session, c->registry are NULL.
 *
 * @param c         Context to initialize
 * @param global    Parsed global options
 * @return 0 on success, NMO_CLI_EXIT_* on failure
 */
int nmo_cmd_ctx_init_no_file(nmo_cmd_ctx_t *c,
                             const nmo_cli_global_opts_t *global);

/**
 * @brief Initialize context from an already-open session (for REPL use).
 *
 * Does NOT open or close the session. Output goes to stdout.
 * This allows REPL commands to build a nmo_cmd_ctx_t that can be passed
 * to nmo_cmd_core functions without duplicating logic.
 *
 * @param c         Context to initialize
 * @param ctx       Existing nmo_context_t (from REPL)
 * @param session   Existing nmo_session_t (from REPL)
 * @param colorize  Whether to use ANSI colors
 */
void nmo_cmd_ctx_init_from_repl(nmo_cmd_ctx_t *c,
                                nmo_context_t *ctx,
                                nmo_session_t *session,
                                bool colorize);

/**
 * @brief Finalize command context: close output stream, close session.
 *
 * Safe to call even if init failed (handles NULL session/output).
 * Does NOT close session/output for contexts created via init_from_repl.
 *
 * @param c           Context to finalize
 * @param exit_code   Exit code to pass through (returned as-is)
 * @return exit_code (pass-through for convenient `return nmo_cmd_ctx_done(&c, 0)`)
 */
int nmo_cmd_ctx_done(nmo_cmd_ctx_t *c, int exit_code);

/**
 * @brief Run a command family action against an existing session context.
 *
 * Family-level REPL dispatchers use this to reuse their public parse/report
 * core while nmo_cmd_ctx_init() resolves the session from ctx rather than
 * opening a file. The caller passes command-local argv without a file operand.
 */
int nmo_cmd_ctx_dispatch_from_source(nmo_cmd_ctx_t *ctx,
                                     int argc,
                                     char **argv,
                                     nmo_cmd_public_handler_t handler);

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

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_CTX_H */
