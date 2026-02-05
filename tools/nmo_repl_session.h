#ifndef NMO_REPL_SESSION_H
#define NMO_REPL_SESSION_H

#include "nmo_repl_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load a file into the repl context.
 *
 * On success, replaces any existing session/context, updates repl->filename, and resets selection.
 * This function does not print; it returns false and optionally writes an error message.
 */
bool nmo_repl_load_file(nmo_repl_context_t *repl, const char *path, char *errbuf, size_t errbuf_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPL_SESSION_H */
