/**
 * @file nmo_cmd_completion.c
 * @brief Shell completion output command
 */

#include "nmo_cmd_completion.h"

#include "../generated/nmo_completion_data.h"
#include "../nmo_cli_common.h"

#include <stdio.h>
#include <string.h>

int nmo_cmd_completion_print(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    (void)global;

    if (argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: nmo completion <bash|fish|zsh|powershell|ps1>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *shell = argv[0];
    for (size_t i = 0; i < nmo_completion_entry_count; ++i) {
        if (strcmp(shell, nmo_completion_entries[i].shell) == 0) {
            const char *const *chunk = nmo_completion_entries[i].chunks;
            while (chunk && *chunk) {
                fputs(*chunk, stdout);
                ++chunk;
            }
            return NMO_CLI_EXIT_SUCCESS;
        }
    }

    fprintf(stderr, "Unknown completion shell: %s\n", shell);
    fprintf(stderr, "Usage: nmo completion <bash|fish|zsh|powershell|ps1>\n");
    return NMO_CLI_EXIT_ARG_ERROR;
}
