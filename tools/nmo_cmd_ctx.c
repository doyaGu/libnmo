/**
 * @file nmo_cmd_ctx.c
 * @brief Command context framework implementation
 */

#include "nmo_cmd_ctx.h"

#include "nmo_tool_common.h"

#include "core/nmo_error.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_virtools_loader.h"
#include "object/nmo_context.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool str_contains_ci(const char *text, const char *needle)
{
    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }

    size_t needle_len = strlen(needle);
    for (const char *p = text; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < needle_len &&
               p[i] != '\0' &&
               tolower((unsigned char)p[i]) ==
                   tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == needle_len) {
            return true;
        }
    }
    return false;
}

static bool path_has_extension_ci(const char *path, const char *extension)
{
    if (path == NULL || extension == NULL) {
        return false;
    }
    size_t path_len = strlen(path);
    size_t ext_len = strlen(extension);
    if (path_len < ext_len) {
        return false;
    }
    const char *tail = path + path_len - ext_len;
    for (size_t i = 0; i < ext_len; ++i) {
        if (tolower((unsigned char)tail[i]) !=
            tolower((unsigned char)extension[i])) {
            return false;
        }
    }
    return true;
}

static nmo_status_t load_json_plugin_data(nmo_context_t *ctx, const char *path)
{
    if (str_contains_ci(path, "param")) {
        return nmo_virtools_load_param_types(
            nmo_context_get_type_registry(ctx), path);
    }
    if (str_contains_ci(path, "op")) {
        return nmo_virtools_load_operations(
            nmo_context_get_type_registry(ctx),
            nmo_context_get_operation_registry(ctx),
            path);
    }
    if (str_contains_ci(path, "plugin")) {
        return nmo_virtools_load_plugins(
            nmo_context_get_extension_registry(ctx), path);
    }

    return nmo_virtools_load_building_blocks(
        nmo_context_get_bb_registry(ctx), path);
}

static int apply_global_plugins(nmo_cmd_ctx_t *c)
{
    if (c == NULL || c->ctx == NULL || c->global == NULL) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    for (size_t i = 0; i < c->global->plugin_count; ++i) {
        const char *path = c->global->plugin_paths[i];
        if (path == NULL || path[0] == '\0') {
            continue;
        }

        nmo_status_t status = NMO_OK;
        if (path_has_extension_ci(path, ".json")) {
            status = load_json_plugin_data(c->ctx, path);
        } else {
            status = nmo_extension_registry_load_library(
                nmo_context_get_extension_registry(c->ctx), path, NULL);
        }

        if (status != NMO_OK) {
            fprintf(stderr,
                    "Error: Failed to load --plugin %s: %s\n",
                    path,
                    nmo_error_string(status));
            return NMO_CLI_EXIT_IO_ERROR;
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int init_output(nmo_cmd_ctx_t *c, const nmo_cli_global_opts_t *global) {
    char *out_error = NULL;
    c->out = nmo_cli_get_output_stream(global, &out_error);
    if (!c->out) {
        fprintf(stderr, "Error: %s\n", out_error ? out_error : "Cannot open output");
        free(out_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    c->owns_output = true;
    c->colorize = nmo_cli_should_colorize(global, c->out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* Open c->file_path, apply --plugin options, then open the output stream. */
static int open_document(nmo_cmd_ctx_t *c, const nmo_load_options_t *load_options)
{
    const nmo_cli_global_opts_t *global = c->global;
    char *open_error = NULL;
    bool opened = (load_options != NULL)
        ? nmo_tool_open_document_opts(c->file_path, load_options,
                                      &c->ctx, &c->document, &c->workspace,
                                      &open_error)
        : nmo_tool_open_document(c->file_path,
                                 &c->ctx, &c->document, &c->workspace,
                                 &open_error);
    if (!opened) {
        fprintf(stderr, "Error: Cannot load '%s': %s\n", c->file_path,
                open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    c->owns_document = true;

    int plugin_rc = apply_global_plugins(c);
    if (plugin_rc != NMO_CLI_EXIT_SUCCESS) {
        nmo_tool_close_document(c->ctx, c->document, c->workspace);
        c->ctx = NULL;
        c->document = NULL;
        c->workspace = NULL;
        c->owns_document = false;
        return plugin_rc;
    }

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

int nmo_cmd_ctx_init_from_source(nmo_cmd_ctx_t *c,
                                 const nmo_cli_global_opts_t *global,
                                 const nmo_cmd_source_t *source)
{
    if (!source) {
        fprintf(stderr, "Error: No command source specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    memset(c, 0, sizeof(*c));
    c->global = global;
    c->load_diagnostics = source->load_options
        ? source->load_options->diagnostics : NULL;
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

    c->file_path = source->file_path;
    if (!c->file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return open_document(c, source->load_options);
}

static void begin_common(nmo_cmd_ctx_t *c, const nmo_cli_global_opts_t *global,
                         nmo_cmd_ctx_state_t state)
{
    memset(c, 0, sizeof(*c));
    if (global) {
        c->global_opts = *global;
    } else {
        nmo_cli_global_opts_init(&c->global_opts);
    }
    c->global = &c->global_opts;
    c->state = state;
    /* Usable for messages printed before nmo_cmd_ctx_open */
    c->out = stdout;
}

void nmo_cmd_ctx_begin(nmo_cmd_ctx_t *c, const nmo_cli_global_opts_t *global)
{
    begin_common(c, global, NMO_CMD_CTX_PENDING_FILE);
}

void nmo_cmd_ctx_begin_no_file(nmo_cmd_ctx_t *c,
                               const nmo_cli_global_opts_t *global)
{
    begin_common(c, global, NMO_CMD_CTX_PENDING_NO_FILE);
}

int nmo_cmd_ctx_parse(nmo_cmd_ctx_t *c, int argc, char **argv,
                      const nmo_opt_def_t *defs, size_t def_count,
                      nmo_opt_result_t *result)
{
    if (c->state == NMO_CMD_CTX_READY) {
        return nmo_opt_parse(argc, argv, defs, def_count, result) < 0
            ? NMO_CLI_EXIT_ARG_ERROR : NMO_CLI_EXIT_SUCCESS;
    }

    c->global = &c->global_opts; /* in case the context was copied */
    int rc = nmo_cli_parse_args(argc, argv, defs, def_count, result,
                                &c->global_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    if (c->state == NMO_CMD_CTX_PENDING_FILE) {
        c->file_path = result->pos_count > 0
            ? result->pos_args[result->pos_count - 1] : NULL;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_ctx_open(nmo_cmd_ctx_t *c)
{
    nmo_cmd_ctx_state_t state = c->state;
    if (state == NMO_CMD_CTX_READY) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    c->state = NMO_CMD_CTX_READY;
    c->global = &c->global_opts; /* in case the context was copied */
    c->out = NULL;
    c->is_json = c->global_opts.format == NMO_CLI_FORMAT_JSON ||
                 c->global_opts.format == NMO_CLI_FORMAT_JSON_PRETTY;
    c->load_diagnostics = c->load_options ? c->load_options->diagnostics : NULL;

    if (state == NMO_CMD_CTX_PENDING_NO_FILE) {
        return init_output(c, c->global);
    }
    if (!c->file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return open_document(c, c->load_options);
}

int nmo_cmd_ctx_parse_and_open(nmo_cmd_ctx_t *c, int argc, char **argv,
                               const nmo_opt_def_t *defs, size_t def_count,
                               nmo_opt_result_t *result)
{
    int rc = nmo_cmd_ctx_parse(c, argc, argv, defs, def_count, result);
    return rc != NMO_CLI_EXIT_SUCCESS ? rc : nmo_cmd_ctx_open(c);
}

int nmo_cmd_ctx_open_without_args(nmo_cmd_ctx_t *c, int argc, char **argv,
                                  const char *usage)
{
    const char *pos[4];
    nmo_opt_result_t r = NMO_OPT_RESULT(NULL, pos);
    int rc = nmo_cmd_ctx_parse(c, argc, argv, NULL, 0, &r);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    size_t expected = c->state == NMO_CMD_CTX_PENDING_FILE ? 1 : 0;
    if (r.pos_count > expected) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
        if (usage) {
            fprintf(stderr, "Usage: %s\n", usage);
        }
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return nmo_cmd_ctx_open(c);
}

int nmo_cmd_batch_run_files(int argc, char **argv,
                            const nmo_cli_global_opts_t *global,
                            const char *usage, const char *cmd_name,
                            nmo_batch_handler_t handler, void *user_data)
{
    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_begin_no_file(&c, global);
    const char **paths = (const char **)malloc((size_t)argc * sizeof(const char *));
    if (!paths) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_opt_result_t r = { .pos_args = paths, .pos_capacity = (size_t)argc };
    int rc = nmo_cmd_ctx_parse(&c, argc, argv, NULL, 0, &r);
    if (rc == NMO_CLI_EXIT_SUCCESS && r.pos_count == 0) {
        fprintf(stderr, "Error: No files specified\n");
        fprintf(stderr, "Usage: %s\n", usage);
        rc = NMO_CLI_EXIT_ARG_ERROR;
    }
    if (rc == NMO_CLI_EXIT_SUCCESS) {
        rc = nmo_tool_batch_run(paths, r.pos_count, c.global, cmd_name,
                                handler, user_data);
    }
    free(paths);
    return nmo_cmd_ctx_done(&c, rc);
}

int nmo_cmd_ctx_init_with_file(nmo_cmd_ctx_t *c, const char *file_path,
                               const nmo_cli_global_opts_t *global)
{
    nmo_cmd_source_t source = {
        .kind = NMO_CMD_SOURCE_EXPLICIT_FILE,
        .file_path = file_path,
    };
    return nmo_cmd_ctx_init_from_source(c, global, &source);
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
    int plugin_rc = apply_global_plugins(c);
    if (plugin_rc != NMO_CLI_EXIT_SUCCESS) {
        return plugin_rc;
    }
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
    return nmo_cmd_ctx_init_from_source(c, global, &source);
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

int nmo_cmd_ctx_emit_record(nmo_cmd_ctx_t *c, nmo_cli_record_t *rec,
                            const char *cmd_name, int key_width,
                            bool colorize)
{
    if (!rec) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    int rc = NMO_CLI_EXIT_SUCCESS;
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = doc ? yyjson_mut_obj(doc) : NULL;
        if (data && nmo_cli_record_to_json(rec, doc, data)) {
            rc = nmo_cmd_ctx_json_end(c, doc, data, cmd_name);
        } else {
            yyjson_mut_doc_free(doc);
            rc = NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    } else {
        nmo_cli_record_print_kv(rec, c->out, key_width, colorize);
    }
    nmo_cli_record_free(rec);
    return rc;
}
