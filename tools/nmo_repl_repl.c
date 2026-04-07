#include "nmo_repl_repl.h"

#include "nmo_repl_commands.h"
#include "nmo_repl_util.h"
#include "nmo_tool_common.h"
#include "core/nmo_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * Add a line to the history ring buffer.
 */
static void repl_history_add(nmo_repl_context_t *repl, const char *line) {
    if (!line || !*line) {
        return;
    }

    /* Skip if duplicate of most recent entry */
    if (repl->history_count > 0) {
        size_t last = (repl->history_start + repl->history_count - 1) % NMO_REPL_HISTORY_SIZE;
        if (repl->history[last] && strcmp(repl->history[last], line) == 0) {
            return;
        }
    }

    if (repl->history_count < NMO_REPL_HISTORY_SIZE) {
        size_t idx = (repl->history_start + repl->history_count) % NMO_REPL_HISTORY_SIZE;
        repl->history[idx] = nmo_tool_strdup(line);
        repl->history_count++;
    } else {
        /* Ring buffer full: overwrite oldest */
        free(repl->history[repl->history_start]);
        repl->history[repl->history_start] = nmo_tool_strdup(line);
        repl->history_start = (repl->history_start + 1) % NMO_REPL_HISTORY_SIZE;
    }
}

/**
 * Free all history entries.
 */
static void repl_history_free(nmo_repl_context_t *repl) {
    for (size_t i = 0; i < NMO_REPL_HISTORY_SIZE; ++i) {
        free(repl->history[i]);
        repl->history[i] = NULL;
    }
    repl->history_count = 0;
    repl->history_start = 0;
}

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

        /* Handle history recall: !<n> or !! */
        if (*p == '!') {
            if (p[1] == '!') {
                /* Recall most recent */
                if (repl->history_count > 0) {
                    size_t last = (repl->history_start + repl->history_count - 1) % NMO_REPL_HISTORY_SIZE;
                    strncpy(line, repl->history[last], sizeof(line) - 1);
                    line[sizeof(line) - 1] = '\0';
                    printf("%s\n", line);
                    p = line;
                } else {
                    fprintf(stderr, "No history.\n");
                    continue;
                }
            } else if (isdigit((unsigned char)p[1])) {
                /* Recall by number: !N */
                size_t n = (size_t)atoi(p + 1);
                if (n > 0 && n <= repl->history_count) {
                    size_t idx = (repl->history_start + n - 1) % NMO_REPL_HISTORY_SIZE;
                    strncpy(line, repl->history[idx], sizeof(line) - 1);
                    line[sizeof(line) - 1] = '\0';
                    printf("%s\n", line);
                    p = line;
                } else {
                    fprintf(stderr, "History index out of range (1-%zu).\n", repl->history_count);
                    continue;
                }
            }
        }

        /* Add to history before parsing (parse modifies buffer) */
        repl_history_add(repl, p);

        /* Make a mutable copy for parsing */
        char cmd_copy[NMO_REPL_MAX_CMD_LEN];
        strncpy(cmd_copy, p, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';

        int argc = nmo_repl_parse_command(cmd_copy, argv, NMO_REPL_MAX_ARGS);
        if (argc == 0) {
            continue;
        }

        int result = nmo_repl_dispatch_command(repl, argc, argv);
        if (result < 0) {
            /* Command failed - display error chain if available */
            char err_chain[1024];
            size_t err_len = nmo_last_error_chain_copy(err_chain, sizeof(err_chain));
            if (err_len > 0) {
                fprintf(stderr, "  Error: %s\n", err_chain);
            }
        }
        if (result > 0) {
            break;
        }
    }

    repl_history_free(repl);
    printf("\nGoodbye!\n");
}
