/**
 * @file nmo_cmd_data.h
 * @brief CLI data array command group
 */

#ifndef NMO_CMD_DATA_H
#define NMO_CMD_DATA_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_data_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_data_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_data_dump(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_data_set_cell(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_data_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_DATA_H */
