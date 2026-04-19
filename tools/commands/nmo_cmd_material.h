/**
 * @file nmo_cmd_material.h
 * @brief CLI material command group
 */

#ifndef NMO_CMD_MATERIAL_H
#define NMO_CMD_MATERIAL_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_material_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_material_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_material_set(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_material_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_MATERIAL_H */
