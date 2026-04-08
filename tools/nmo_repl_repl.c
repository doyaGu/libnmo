#include "nmo_repl_repl.h"

#include "nmo_repl_commands.h"
#include "nmo_repl_input.h"
#include "nmo_repl_util.h"
#include "nmo_tool_common.h"
#include "core/nmo_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef NMO_HAVE_ISOCLINE

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

#endif /* !NMO_HAVE_ISOCLINE */

void nmo_repl_loop(nmo_repl_context_t *repl) {
    char *argv[NMO_REPL_MAX_ARGS];

    nmo_repl_input_init(repl);
    nmo_repl_print_banner(repl);

#ifdef NMO_HAVE_ISOCLINE
    static char last_command[NMO_REPL_MAX_CMD_LEN];
    last_command[0] = '\0';
#endif

    while (true) {
        char *line = nmo_repl_readline(nmo_repl_format_prompt(repl));
        if (!line) {
            break; /* EOF or Ctrl+C */
        }

        /* Skip leading whitespace */
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            ++p;
        }

        if (*p == '\0' || *p == '#' || *p == ';') {
            nmo_repl_free_line(line);
            continue;
        }

        /* Handle history recall */
        if (*p == '!') {
#ifdef NMO_HAVE_ISOCLINE
            if (p[1] == '!') {
                if (last_command[0] != '\0') {
                    printf("%s\n", last_command);
                    ic_history_remove_last();
                    nmo_repl_free_line(line);
                    /* Use last_command as the input */
                    line = nmo_tool_strdup(last_command);
                    if (!line) {
                        break;
                    }
                    p = line;
                } else {
                    fprintf(stderr, "No history.\n");
                    nmo_repl_free_line(line);
                    continue;
                }
            } else if (p[1] == 'N' || isdigit((unsigned char)p[1])) {
                fprintf(stderr, "Use Ctrl+R to search history or Up/Down arrows to navigate.\n");
                ic_history_remove_last();
                nmo_repl_free_line(line);
                continue;
            }
#else
            if (p[1] == '!') {
                /* Recall most recent */
                if (repl->history_count > 0) {
                    size_t last = (repl->history_start + repl->history_count - 1) % NMO_REPL_HISTORY_SIZE;
                    nmo_repl_free_line(line);
                    line = nmo_tool_strdup(repl->history[last]);
                    if (!line) {
                        break;
                    }
                    printf("%s\n", line);
                    p = line;
                } else {
                    fprintf(stderr, "No history.\n");
                    nmo_repl_free_line(line);
                    continue;
                }
            } else if (isdigit((unsigned char)p[1])) {
                /* Recall by number: !N */
                size_t n = (size_t)atoi(p + 1);
                if (n > 0 && n <= repl->history_count) {
                    size_t idx = (repl->history_start + n - 1) % NMO_REPL_HISTORY_SIZE;
                    nmo_repl_free_line(line);
                    line = nmo_tool_strdup(repl->history[idx]);
                    if (!line) {
                        break;
                    }
                    printf("%s\n", line);
                    p = line;
                } else {
                    fprintf(stderr, "History index out of range (1-%zu).\n", repl->history_count);
                    nmo_repl_free_line(line);
                    continue;
                }
            }
#endif
        }

#ifdef NMO_HAVE_ISOCLINE
        /* Save last command for !! recall */
        strncpy(last_command, p, sizeof(last_command) - 1);
        last_command[sizeof(last_command) - 1] = '\0';
#else
        /* Add to ring buffer history before parsing (parse modifies buffer) */
        repl_history_add(repl, p);
#endif

        /* Make a mutable copy for parsing */
        char cmd_copy[NMO_REPL_MAX_CMD_LEN];
        strncpy(cmd_copy, p, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';

        nmo_repl_free_line(line);

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

#ifndef NMO_HAVE_ISOCLINE
    repl_history_free(repl);
#endif
    nmo_repl_input_cleanup();
    printf("\nGoodbye!\n");
}
