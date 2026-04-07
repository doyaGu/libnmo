/**
 * @file nmo_cmd_parameter.h
 * @brief CLI parameter command group implementation
 */

#ifndef NMO_CMD_PARAMETER_H
#define NMO_CMD_PARAMETER_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_parameter_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_parameter_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_parameter_dump(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_PARAMETER_H */
