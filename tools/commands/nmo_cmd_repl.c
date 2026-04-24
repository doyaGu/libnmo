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
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"

#include "nmo.h"

#include <stdio.h>
#include <string.h>

int nmo_cmd_repl_start(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo repl start <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open document */
    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    char errbuf[256];

    if (!nmo_tool_open_document(file_path, &ctx, &document, &workspace,
                                errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Initialize repl context */
    nmo_repl_context_t repl;
    memset(&repl, 0, sizeof(repl));
    repl.ctx = ctx;
    repl.document = document;
    repl.workspace = workspace;
    repl.filename = file_path;
    repl.colorize = nmo_cli_should_colorize(global, stdout);
    repl.dump_level = NMO_DUMP_NORMAL;
    repl.page_size = 20;
    repl.regex_icase = false;

    /* Enter REPL */
    nmo_repl_loop(&repl);

    /* Cleanup */
    nmo_tool_close_document(ctx, document, workspace);
    return NMO_CLI_EXIT_SUCCESS;
}
