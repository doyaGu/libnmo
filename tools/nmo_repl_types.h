#ifndef NMO_REPL_TYPES_H
#define NMO_REPL_TYPES_H

#include "nmo.h"
#include "app/nmo_inspector.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NMO_REPL_MAX_CMD_LEN 256
#define NMO_REPL_MAX_ARGS 16

typedef struct {
    nmo_context_t *ctx;
    nmo_session_t *session;
    const char *filename;
    char filename_storage[512];
    bool colorize;
    nmo_dump_level_t dump_level;
    size_t selected_index;
    bool has_selection;
    size_t page_size;
    bool regex_icase;
} nmo_repl_context_t;

typedef int (*nmo_repl_command_handler_t)(nmo_repl_context_t *repl, int argc, char **argv);

typedef struct {
    const char *name;
    const char *alias;
    const char *help;
    const char *usage;
    nmo_repl_command_handler_t handler;
} nmo_repl_command_t;

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPL_TYPES_H */
