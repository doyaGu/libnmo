/**
 * @file nmo_cmd_parameter.h
 * @brief CLI parameter command group implementation
 */

#ifndef NMO_CMD_PARAMETER_H
#define NMO_CMD_PARAMETER_H

#include "../nmo_cli_dispatch.h"
#include "../nmo_cmd_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_parameter_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

int nmo_cmd_parameter_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_parameter_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_parameter_dump(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_parameter_set(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_parameter_set_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv,
                                     nmo_cmd_in_session_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_PARAMETER_H */
