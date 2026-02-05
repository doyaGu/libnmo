#include "nmo_repl_repl.h"

#include "nmo_repl_commands.h"
#include "nmo_repl_util.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

void nmo_repl_loop(nmo_repl_context_t *repl) {
    char line[NMO_REPL_MAX_CMD_LEN];
    char *argv[NMO_REPL_MAX_ARGS];

    nmo_repl_print_banner(repl);

    while (true) {
        nmo_repl_print_prompt(repl);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break; /* EOF */
        }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        const char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }

        if (*p == '\0') {
            continue;
        }

        if (*p == '#' || *p == ';') {
            continue;
        }

        int argc = nmo_repl_parse_command((char *)p, argv, NMO_REPL_MAX_ARGS);
        if (argc == 0) {
            continue;
        }

        int result = nmo_repl_dispatch_command(repl, argc, argv);
        if (result > 0) {
            break;
        }
    }

    printf("\nGoodbye!\n");
}
