/**
 * @file nmo_cmd_entity.h
 * @brief CLI 3D entity command group
 */

#ifndef NMO_CMD_ENTITY_H
#define NMO_CMD_ENTITY_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_entity_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_entity_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_entity_set_position(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_entity_set_parent(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_entity_set_camera(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_entity_set_light(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_entity_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_ENTITY_H */
