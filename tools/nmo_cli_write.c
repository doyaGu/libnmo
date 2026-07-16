#include "nmo_cli_write.h"

#include "nmo_cmd_ctx.h"

#include "core/nmo_error.h"

#include <stdio.h>

int nmo_cli_write_init_ctx(
    nmo_cmd_ctx_t *ctx,
    const char *input_path,
    const nmo_cli_global_opts_t *global)
{
    if (ctx == NULL || input_path == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return nmo_cmd_ctx_init_with_file(ctx, input_path, global);
}

int nmo_cli_save_document(
    nmo_document_t *document,
    const char *output_path,
    const nmo_save_options_t *save_opts)
{
    if (document == NULL || output_path == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    int save_rc = nmo_document_save_file(document, output_path, save_opts);
    if (save_rc != NMO_OK) {
        char detail[1024];
        size_t detail_len = nmo_last_error_message_copy(detail, sizeof(detail));
        fprintf(stderr, "Error saving file: %s", nmo_error_string(save_rc));
        if (detail_len > 0u) {
            fprintf(stderr, ": %s", detail);
        }
        fputc('\n', stderr);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cli_run_write_command(
    const char *input_path,
    const char *output_path,
    bool dry_run,
    const nmo_cli_global_opts_t *global,
    const nmo_cli_write_spec_t *spec,
    nmo_cli_write_mutate_fn mutate,
    nmo_cli_write_report_fn report,
    void *user_data)
{
    if (input_path == NULL || spec == NULL || mutate == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (spec->output_required_unless_dry_run && !dry_run && output_path == NULL) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t ctx;
    int rc = nmo_cli_write_init_ctx(&ctx, input_path, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    rc = mutate(&ctx, dry_run, output_path, user_data);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&ctx, rc);
    }

    bool should_save = !dry_run && output_path != NULL;
    if (should_save && spec->should_save != NULL) {
        should_save = spec->should_save(dry_run, output_path, user_data);
    }

    if (should_save) {
        nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
        int save_rc = nmo_cli_save_document(ctx.document, output_path, &save_opts);
        if (save_rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&ctx, save_rc);
        }
    }

    if (report != NULL) {
        rc = report(&ctx, dry_run, output_path, user_data);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return nmo_cmd_ctx_done(&ctx, rc);
        }
    }

    return nmo_cmd_ctx_done(&ctx, NMO_CLI_EXIT_SUCCESS);
}
