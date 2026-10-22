/**
 * @file nmo_cmd_repl.c
 * @brief CLI REPL command group implementation
 *
 * Wraps the REPL implementation from nmo_repl_repl.c
 */

#include "nmo_cmd_repl.h"

#include "../nmo_cli_common.h"
#include "../nmo_repl_types.h"
#include "../nmo_repl_repl.h"
#include "../nmo_repl_commands.h"
#include "../nmo_repl_session.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"

#include "nmo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nmo_cmd_repl_start(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cli_global_opts_t local_global = *global;
    const char *pos[4];
    nmo_opt_result_t r = NMO_OPT_RESULT(NULL, pos);
    if (nmo_cli_parse_args(argc, argv, NULL, 0, &r, &local_global) != NMO_CLI_EXIT_SUCCESS) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count == 0) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo repl start <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count > 1) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[1]);
        fprintf(stderr, "Usage: nmo repl start <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *file_path = r.pos_args[0];

    /* Open document */
    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    char *open_error = NULL;

    if (!nmo_tool_open_document(file_path, &ctx, &document, &workspace, &open_error)) {
        fprintf(stderr, "Error: %s\n", open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Initialize repl context */
    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    repl.ctx = ctx;
    repl.document = document;
    repl.workspace = workspace;
    repl.filename = file_path;
    repl.colorize = nmo_cli_should_colorize(&local_global, stdout);
    repl.dump_level = NMO_DUMP_NORMAL;
    repl.page_size = 20;
    repl.regex_icase = false;

    /* Enter REPL */
    nmo_repl_loop(&repl);

    /* Cleanup: the repl may have replaced the document via `open` */
    nmo_tool_close_document(repl.ctx, repl.document, repl.workspace);
    nmo_repl_session_cleanup(&repl);
    return NMO_CLI_EXIT_SUCCESS;
}
