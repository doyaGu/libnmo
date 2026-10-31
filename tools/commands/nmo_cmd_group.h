/**
 * @file nmo_cmd_group.h
 * @brief CLI group command group: CKGroup membership
 */

#ifndef NMO_CMD_GROUP_H
#define NMO_CMD_GROUP_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_group_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_group_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_group_add(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_group_remove(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_group_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_GROUP_H */
