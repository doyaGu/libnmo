/**
 * @file nmo_cmd_ctx.c
 * @brief Command context framework implementation
 */

#include "nmo_cmd_ctx.h"

#include "nmo_tool_common.h"

#include "session/nmo_context.h"

#include <string.h>

int nmo_cmd_ctx_init(nmo_cmd_ctx_t *c, int argc, char **argv,
                     const nmo_cli_global_opts_t *global)
{
    memset(c, 0, sizeof(*c));
    c->global = global;
    c->is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                  global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    /* Find file argument */
    c->file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!c->file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    char errbuf[256];
    if (!nmo_tool_open_session(c->file_path, &c->ctx, &c->session,
                               errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Cache type registry */
    c->registry = nmo_context_get_type_registry(c->ctx);

    /* Open output stream */
    char out_err[128];
    c->out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!c->out) {
        nmo_tool_close_session(c->ctx, c->session);
        c->ctx = NULL;
        c->session = NULL;
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

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

    c->registry = nmo_context_get_type_registry(c->ctx);

    char out_err[128];
    c->out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!c->out) {
        nmo_tool_close_session(c->ctx, c->session);
        c->ctx = NULL;
        c->session = NULL;
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    c->colorize = nmo_cli_should_colorize(global, c->out);
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
    c->registry = ctx ? nmo_context_get_type_registry(ctx) : NULL;
    c->out = stdout;
    c->colorize = colorize;
    c->is_json = false;  /* REPL is always text mode */
}

int nmo_cmd_ctx_done(nmo_cmd_ctx_t *c, int exit_code)
{
    /* Close output and session only if we own them (CLI init paths).
     * REPL contexts have global == NULL and don't own resources. */
    if (c->global) {
        if (c->out) {
            nmo_cli_close_output_stream(c->global, c->out);
            c->out = NULL;
        }
        if (c->session) {
            nmo_tool_close_session(c->ctx, c->session);
            c->ctx = NULL;
            c->session = NULL;
        }
    }
    return exit_code;
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
