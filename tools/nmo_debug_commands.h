#ifndef NMO_DEBUG_COMMANDS_H
#define NMO_DEBUG_COMMANDS_H

#include "nmo_debug_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void nmo_debug_print_banner(const nmo_debug_context_t *dbg);
int nmo_debug_dispatch_command(nmo_debug_context_t *dbg, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DEBUG_COMMANDS_H */
