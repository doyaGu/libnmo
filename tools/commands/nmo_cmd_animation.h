/**
 * @file nmo_cmd_animation.h
 * @brief CLI animation command group
 */

#ifndef NMO_CMD_ANIMATION_H
#define NMO_CMD_ANIMATION_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_animation_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_animation_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_animation_keys(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_animation_export(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_animation_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_animation_import(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_animation_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_ANIMATION_H */
