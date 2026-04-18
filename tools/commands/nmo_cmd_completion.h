/**
 * @file nmo_cmd_completion.h
 * @brief Shell completion output command
 */

#ifndef NMO_CMD_COMPLETION_H
#define NMO_CMD_COMPLETION_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_completion_print(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_COMPLETION_H */
