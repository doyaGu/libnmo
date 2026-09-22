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
 * On success, replaces any existing document/context, updates repl->filename,
 * and resets selection. This function does not print; on failure it returns
 * false and, when `out_error` is non-NULL, stores a malloc'd message there
 * for the caller to free().
 */
bool nmo_repl_load_file(nmo_repl_context_t *repl, const char *path, char **out_error);

/** Release the strings owned by the repl context (filename, prompt). */
void nmo_repl_session_cleanup(nmo_repl_context_t *repl);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPL_SESSION_H */
