/**
 * @file nmo_cmd_repl.h
 * @brief CLI REPL command group (wraps existing interactive debugger)
 */

#ifndef NMO_CMD_REPL_H
#define NMO_CMD_REPL_H

#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo repl start - Start interactive REPL
 */
int nmo_cmd_repl_start(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_REPL_H */
