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
size_t nmo_repl_cli_read_session_public_fallback_count(void);
size_t nmo_repl_cli_read_generic_session_count_for_group(const char *group);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPL_COMMANDS_H */
