#include "nmo_debug_repl.h"

#include "nmo_debug_commands.h"
#include "nmo_debug_util.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

void nmo_debug_repl_loop(nmo_debug_context_t *dbg) {
    char line[NMO_DEBUG_MAX_CMD_LEN];
    char *argv[NMO_DEBUG_MAX_ARGS];

    nmo_debug_print_banner(dbg);

    while (true) {
        nmo_debug_print_prompt(dbg);

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

        int argc = nmo_debug_parse_command((char *)p, argv, NMO_DEBUG_MAX_ARGS);
        if (argc == 0) {
            continue;
        }

        int result = nmo_debug_dispatch_command(dbg, argc, argv);
        if (result > 0) {
            break;
        }
    }

    printf("\nGoodbye!\n");
}
