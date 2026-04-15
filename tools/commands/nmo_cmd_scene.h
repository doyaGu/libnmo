/**
 * @file nmo_cmd_scene.h
 * @brief CLI scene/level command group
 */

#ifndef NMO_CMD_SCENE_H
#define NMO_CMD_SCENE_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_scene_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_scene_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_scene_set(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_SCENE_H */
