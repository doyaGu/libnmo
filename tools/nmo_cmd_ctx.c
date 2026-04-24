/**
 * @file nmo_cmd_ctx.c
 * @brief Command context framework implementation
 */

#include "nmo_cmd_ctx.h"

#include "nmo_tool_common.h"

#include "runtime/nmo_context.h"

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
    nmo_cmd_source_t source = {
        .kind = NMO_CMD_SOURCE_FILE_OPERAND,
        .load_options = options,
    };
    return nmo_cmd_ctx_init_from_source(c, argc, argv, global, &source);
}

static int init_output(nmo_cmd_ctx_t *c, const nmo_cli_global_opts_t *global) {
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

    if (source->kind == NMO_CMD_SOURCE_NO_DOCUMENT) {
        return init_output(c, global);
    }

    if (source->kind == NMO_CMD_SOURCE_DOCUMENT) {
        return nmo_cmd_ctx_init_with_document(
            c, source->ctx, source->document, source->workspace,
            source->source_label ? source->source_label : "(current document)",
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

    char errbuf[256];
    bool opened = (source->load_options != NULL)
        ? nmo_tool_open_document_opts(c->file_path, source->load_options,
                                      &c->ctx, &c->document, &c->workspace,
                                      errbuf, sizeof(errbuf))
        : nmo_tool_open_document(c->file_path,
                                 &c->ctx, &c->document, &c->workspace,
                                 errbuf, sizeof(errbuf));
    if (!opened) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    c->owns_document = true;

    c->registry = nmo_context_get_type_registry(c->ctx);

    int rc = init_output(c, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        nmo_tool_close_document(c->ctx, c->document, c->workspace);
        c->ctx = NULL;
        c->document = NULL;
        c->workspace = NULL;
        c->owns_document = false;
        return rc;
    }

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

int nmo_cmd_ctx_init_with_document(nmo_cmd_ctx_t *c,
                                   nmo_context_t *ctx,
                                   nmo_document_t *document,
                                   nmo_workspace_t *workspace,
                                   const char *source_label,
                                   const nmo_cli_global_opts_t *global)
{
    memset(c, 0, sizeof(*c));
    c->global = global;
    c->file_path = source_label ? source_label : "(current document)";
    c->ctx = ctx;
    c->document = document;
    c->workspace = workspace;
    c->owns_document = false;
    c->is_json = global && (global->format == NMO_CLI_FORMAT_JSON ||
                            global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    c->registry = ctx ? nmo_context_get_type_registry(ctx) : NULL;

    if (global) {
        return init_output(c, global);
    }

    c->out = stdout;
    c->owns_output = false;
    c->colorize = false;
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_ctx_init_no_file(nmo_cmd_ctx_t *c,
                             const nmo_cli_global_opts_t *global)
{
    nmo_cmd_source_t source = {
        .kind = NMO_CMD_SOURCE_NO_DOCUMENT,
    };
    return nmo_cmd_ctx_init_from_source(c, 0, NULL, global, &source);
}

void nmo_cmd_ctx_init_from_repl_document(nmo_cmd_ctx_t *c,
                                         nmo_context_t *ctx,
                                         nmo_document_t *document,
                                         nmo_workspace_t *workspace,
                                         bool colorize)
{
    memset(c, 0, sizeof(*c));
    c->global = NULL;
    c->file_path = NULL;
    c->ctx = ctx;
    c->document = document;
    c->workspace = workspace;
    c->owns_document = false;
    c->registry = ctx ? nmo_context_get_type_registry(ctx) : NULL;
    c->out = stdout;
    c->owns_output = false;
    c->colorize = colorize;
    c->is_json = false;
}

int nmo_cmd_ctx_done(nmo_cmd_ctx_t *c, int exit_code)
{
    if (c->owns_output && c->global && c->out) {
        nmo_cli_close_output_stream(c->global, c->out);
        c->out = NULL;
        c->owns_output = false;
    }
    if (c->owns_document) {
        nmo_tool_close_document(c->ctx, c->document, c->workspace);
        c->ctx = NULL;
        c->document = NULL;
        c->workspace = NULL;
        c->owns_document = false;
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
