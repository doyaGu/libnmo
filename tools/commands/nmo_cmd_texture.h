/**
 * @file nmo_cmd_texture.h
 * @brief CLI texture command group
 */

#ifndef NMO_CMD_TEXTURE_H
#define NMO_CMD_TEXTURE_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_texture_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_texture_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_texture_extract(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_texture_extract_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_texture_replace(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_texture_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_TEXTURE_H */
