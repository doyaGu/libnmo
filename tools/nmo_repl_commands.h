#ifndef NMO_REPL_COMMANDS_H
#define NMO_REPL_COMMANDS_H

#include "nmo_repl_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void nmo_repl_print_banner(const nmo_repl_context_t *repl);
int nmo_repl_dispatch_command(nmo_repl_context_t *repl, int argc, char **argv);
const char **nmo_repl_get_command_names(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPL_COMMANDS_H */
