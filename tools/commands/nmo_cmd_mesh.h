/**
 * @file nmo_cmd_mesh.h
 * @brief CLI mesh command group
 */

#ifndef NMO_CMD_MESH_H
#define NMO_CMD_MESH_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_mesh_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_mesh_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_mesh_export(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_mesh_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_mesh_import(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_mesh_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_MESH_H */
