/**
 * @file nmo_cmd_ctx.c
 * @brief Command context framework implementation
 */

#include "nmo_cmd_ctx.h"

#include "nmo_tool_common.h"

#include "session/nmo_context.h"

#include <stdlib.h>
#include <string.h>

int nmo_cmd_ctx_init(nmo_cmd_ctx_t *c, int argc, char **argv,
                     const nmo_cli_global_opts_t *global)
{
    return nmo_cmd_ctx_init_with_load_options(c, argc, argv, global, NULL);
}

int nmo_cmd_ctx_init_with_load_options(nmo_cmd_ctx_t *c, int argc, char **argv,
                                       const nmo_cli_global_opts_t *global,
                                       const nmo_load_options_t *options)
{
    memset(c, 0, sizeof(*c));
    c->global = global;
    c->is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                  global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    if (global->borrowed_session) {
        const char *label = global->borrowed_source_label;
        if (!label) {
            label = nmo_tool_find_file_arg_last(argc, argv);
        }
        return nmo_cmd_ctx_init_with_session(c, global->borrowed_ctx,
                                             global->borrowed_session,
                                             label, global);
    }

    /* Find file argument */
    c->file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!c->file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session. Command-level --strict is handled by validators/mutations,
     * not by rejecting files during load. */
    char errbuf[256];
    bool opened = (options != NULL)
        ? nmo_tool_open_session_opts(c->file_path, options, &c->ctx, &c->session,
                                     errbuf, sizeof(errbuf))
        : nmo_tool_open_session(c->file_path, &c->ctx, &c->session,
                                errbuf, sizeof(errbuf));
    if (!opened) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    c->owns_session = true;

    /* Cache type registry */
    c->registry = nmo_context_get_type_registry(c->ctx);

    /* Open output stream */
    char out_err[128];
    c->out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!c->out) {
        nmo_tool_close_session(c->ctx, c->session);
        c->ctx = NULL;
        c->session = NULL;
        c->owns_session = false;
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    c->owns_output = true;

    c->colorize = nmo_cli_should_colorize(global, c->out);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_ctx_init_with_file(nmo_cmd_ctx_t *c, const char *file_path,
                               const nmo_cli_global_opts_t *global)
{
    memset(c, 0, sizeof(*c));
    c->global = global;
    c->is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                  global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    if (global->borrowed_session) {
        const char *label = global->borrowed_source_label
            ? global->borrowed_source_label
            : file_path;
        return nmo_cmd_ctx_init_with_session(c, global->borrowed_ctx,
                                             global->borrowed_session,
                                             label, global);
    }

    c->file_path = file_path;
    if (!c->file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char errbuf[256];
    if (!nmo_tool_open_session(c->file_path, &c->ctx, &c->session,
                               errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    c->owns_session = true;

    c->registry = nmo_context_get_type_registry(c->ctx);

    char out_err[128];
    c->out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!c->out) {
        nmo_tool_close_session(c->ctx, c->session);
        c->ctx = NULL;
        c->session = NULL;
        c->owns_session = false;
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    c->owns_output = true;

    c->colorize = nmo_cli_should_colorize(global, c->out);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_ctx_init_with_session(nmo_cmd_ctx_t *c,
                                  nmo_context_t *ctx,
                                  nmo_session_t *session,
                                  const char *source_label,
                                  const nmo_cli_global_opts_t *global)
{
    memset(c, 0, sizeof(*c));
    c->global = global;
    c->file_path = source_label ? source_label : "(current session)";
    c->ctx = ctx;
    c->session = session;
    c->owns_session = false;
    c->is_json = global && (global->format == NMO_CLI_FORMAT_JSON ||
                            global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    c->registry = ctx ? nmo_context_get_type_registry(ctx) : NULL;

    if (global) {
        char out_err[128];
        c->out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
        if (!c->out) {
            fprintf(stderr, "Error: %s\n", out_err);
            return NMO_CLI_EXIT_IO_ERROR;
        }
        c->owns_output = true;
        c->colorize = nmo_cli_should_colorize(global, c->out);
    } else {
        c->out = stdout;
        c->owns_output = false;
        c->colorize = false;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_ctx_init_no_file(nmo_cmd_ctx_t *c,
                             const nmo_cli_global_opts_t *global)
{
    memset(c, 0, sizeof(*c));
    c->global = global;
    c->is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                  global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    /* No session */
    c->file_path = NULL;
    c->ctx = NULL;
    c->session = NULL;
    c->registry = NULL;

    /* Open output stream */
    char out_err[128];
    c->out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!c->out) {
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    c->owns_output = true;

    c->colorize = nmo_cli_should_colorize(global, c->out);
    return NMO_CLI_EXIT_SUCCESS;
}

void nmo_cmd_ctx_init_from_repl(nmo_cmd_ctx_t *c,
                                nmo_context_t *ctx,
                                nmo_session_t *session,
                                bool colorize)
{
    memset(c, 0, sizeof(*c));
    c->global = NULL;  /* REPL doesn't use global opts */
    c->file_path = NULL;
    c->ctx = ctx;
    c->session = session;
    c->owns_session = false;
    c->registry = ctx ? nmo_context_get_type_registry(ctx) : NULL;
    c->out = stdout;
    c->owns_output = false;
    c->colorize = colorize;
    c->is_json = false;  /* REPL is always text mode */
}

int nmo_cmd_ctx_done(nmo_cmd_ctx_t *c, int exit_code)
{
    if (c->owns_output && c->global && c->out) {
        nmo_cli_close_output_stream(c->global, c->out);
        c->out = NULL;
        c->owns_output = false;
    }
    if (c->owns_session && c->session) {
        nmo_tool_close_session(c->ctx, c->session);
        c->ctx = NULL;
        c->session = NULL;
        c->owns_session = false;
    }
    return exit_code;
}

static bool nmo_cmd_token_has_suffix_ci(const char *token, const char *suffix)
{
    if (!token || !suffix) {
        return false;
    }
    size_t token_len = strlen(token);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > token_len) {
        return false;
    }

    const char *tail = token + token_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        char a = tail[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static bool nmo_cmd_token_looks_like_session_file(const char *token)
{
    return nmo_cmd_token_has_suffix_ci(token, ".nmo") ||
           nmo_cmd_token_has_suffix_ci(token, ".cmo") ||
           nmo_cmd_token_has_suffix_ci(token, ".vmo");
}

int nmo_cmd_in_session_dispatch_with_source(nmo_cmd_ctx_t *ctx,
                                            int argc,
                                            char **argv,
                                            nmo_cmd_public_handler_t handler)
{
    if (!ctx || !ctx->session || !handler) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    for (int i = 1; i < argc; i++) {
        if (nmo_cmd_token_looks_like_session_file(argv[i])) {
            fprintf(stderr, "Error: File operands are not accepted in in-session read mode\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    char **cmd_argv = (char **)calloc((size_t)argc + 2u, sizeof(char *));
    if (!cmd_argv) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (int i = 0; i < argc; i++) {
        cmd_argv[i] = argv[i];
    }
    cmd_argv[argc] = (char *)ctx->file_path;

    nmo_cli_global_opts_t global;
    if (ctx->global) {
        global = *ctx->global;
    } else {
        nmo_cli_global_opts_init(&global);
    }
    global.borrowed_ctx = ctx->ctx;
    global.borrowed_session = ctx->session;
    global.borrowed_source_label = ctx->file_path;

    int rc = handler(argc + 1, cmd_argv, &global);
    free(cmd_argv);
    return rc;
}

yyjson_mut_doc *nmo_cmd_ctx_json_begin(nmo_cmd_ctx_t *c)
{
    (void)c;
    return nmo_cli_json_create_doc();
}

int nmo_cmd_ctx_json_end(nmo_cmd_ctx_t *c, yyjson_mut_doc *doc,
                         yyjson_mut_val *data, const char *cmd_name)
{
    bool pretty = c->global &&
                  c->global->format == NMO_CLI_FORMAT_JSON_PRETTY;
    bool ok = nmo_cli_json_write_enveloped_and_free(
        doc, data, cmd_name, c->file_path, c->out, pretty);
    return ok ? NMO_CLI_EXIT_SUCCESS : NMO_CLI_EXIT_INTERNAL_ERROR;
}
