/**
 * @file nmo_cmd_ctx.c
 * @brief Command context framework implementation
 */

#include "nmo_cmd_ctx.h"

#include "nmo_tool_common.h"

#include "session/nmo_context.h"

#include <stdlib.h>
#include <string.h>

const nmo_cmd_source_t *nmo_cmd_global_source(
    const nmo_cli_global_opts_t *global)
{
    if (!global || global->struct_size < sizeof(nmo_cmd_invocation_t)) {
        return NULL;
    }

    const nmo_cmd_invocation_t *invocation =
        (const nmo_cmd_invocation_t *)global;
    return invocation->source;
}

bool nmo_cmd_global_uses_session_source(
    const nmo_cli_global_opts_t *global)
{
    const nmo_cmd_source_t *source = nmo_cmd_global_source(global);
    return source && source->kind == NMO_CMD_SOURCE_SESSION;
}

int nmo_cmd_ctx_init(nmo_cmd_ctx_t *c, int argc, char **argv,
                     const nmo_cli_global_opts_t *global)
{
    return nmo_cmd_ctx_init_with_load_options(c, argc, argv, global, NULL);
}

int nmo_cmd_ctx_init_with_load_options(nmo_cmd_ctx_t *c, int argc, char **argv,
                                       const nmo_cli_global_opts_t *global,
                                       const nmo_load_options_t *options)
{
    const nmo_cmd_source_t *bound_source = nmo_cmd_global_source(global);
    if (bound_source) {
        return nmo_cmd_ctx_init_from_source(c, argc, argv, global,
                                            bound_source);
    }

    nmo_cmd_source_t source = {
        .kind = NMO_CMD_SOURCE_FILE_OPERAND,
        .load_options = options,
    };
    return nmo_cmd_ctx_init_from_source(c, argc, argv, global, &source);
}

int nmo_cmd_ctx_init_from_source(nmo_cmd_ctx_t *c,
                                 int argc,
                                 char **argv,
                                 const nmo_cli_global_opts_t *global,
                                 const nmo_cmd_source_t *source)
{
    if (!source) {
        fprintf(stderr, "Error: No command source specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    memset(c, 0, sizeof(*c));
    c->global = global;
    c->is_json = global && (global->format == NMO_CLI_FORMAT_JSON ||
                            global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    if (source->kind == NMO_CMD_SOURCE_NO_SESSION) {
        c->file_path = NULL;
        c->ctx = NULL;
        c->session = NULL;
        c->registry = NULL;
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

    if (source->kind == NMO_CMD_SOURCE_SESSION) {
        return nmo_cmd_ctx_init_with_session(
            c, source->ctx, source->session,
            source->source_label ? source->source_label : "(current session)",
            global);
    }

    if (source->kind == NMO_CMD_SOURCE_EXPLICIT_FILE) {
        c->file_path = source->file_path;
    } else {
        c->file_path = nmo_tool_find_file_arg_last(argc, argv);
    }
    if (!c->file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session. Command-level --strict is handled by validators/mutations,
     * not by rejecting files during load. */
    char errbuf[256];
    bool opened = (source->load_options != NULL)
        ? nmo_tool_open_session_opts(c->file_path, source->load_options,
                                     &c->ctx, &c->session, errbuf, sizeof(errbuf))
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
    nmo_cmd_source_t source = {
        .kind = NMO_CMD_SOURCE_EXPLICIT_FILE,
        .file_path = file_path,
    };
    return nmo_cmd_ctx_init_from_source(c, 0, NULL, global, &source);
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
    nmo_cmd_source_t source = {
        .kind = NMO_CMD_SOURCE_NO_SESSION,
    };
    return nmo_cmd_ctx_init_from_source(c, 0, NULL, global, &source);
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

int nmo_cmd_ctx_dispatch_from_source(nmo_cmd_ctx_t *ctx,
                                     int argc,
                                     char **argv,
                                     nmo_cmd_public_handler_t handler)
{
    if (!ctx || !ctx->session || !handler) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_invocation_t invocation;
    if (ctx->global) {
        invocation.global = *ctx->global;
    } else {
        nmo_cli_global_opts_init(&invocation.global);
    }
    nmo_cmd_source_t source = {
        .kind = NMO_CMD_SOURCE_SESSION,
        .ctx = ctx->ctx,
        .session = ctx->session,
        .source_label = ctx->file_path,
    };
    invocation.global.struct_size = sizeof(invocation);
    invocation.source = &source;

    return handler(argc, argv, &invocation.global);
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
