#ifndef NMO_DEBUG_SESSION_H
#define NMO_DEBUG_SESSION_H

#include "nmo_debug_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load a file into the debugger context.
 *
 * On success, replaces any existing session/context, updates dbg->filename, and resets selection.
 * This function does not print; it returns false and optionally writes an error message.
 */
bool nmo_debug_load_file(nmo_debug_context_t *dbg, const char *path, char *errbuf, size_t errbuf_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DEBUG_SESSION_H */
