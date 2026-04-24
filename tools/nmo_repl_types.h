#ifndef NMO_REPL_TYPES_H
#define NMO_REPL_TYPES_H

#include "nmo.h"
#include "chunk/nmo_chunk_inspect.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NMO_REPL_MAX_CMD_LEN 4096
#define NMO_REPL_MAX_ARGS 64
#define NMO_REPL_HISTORY_SIZE 256

typedef struct {
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    const char *filename;
    char filename_storage[512];
    bool colorize;
    nmo_dump_level_t dump_level;
    size_t selected_index;
    bool has_selection;
    size_t page_size;
    bool regex_icase;
    bool dirty;  /**< True if document modified since last save */

#ifdef NMO_HAVE_ISOCLINE
    struct nmo_arena *name_cache_arena;
    const char **name_cache;
    size_t name_cache_count;
    bool name_cache_dirty;
#endif

#ifndef NMO_HAVE_ISOCLINE
    /* Command history (ring buffer) */
    char *history[NMO_REPL_HISTORY_SIZE];
    size_t history_count;         /**< Total entries stored */
    size_t history_start;         /**< Oldest entry index */
#endif
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
