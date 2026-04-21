/**
 * @file nmo_cmd_script.h
 * @brief CLI script command group.
 */

#ifndef NMO_CMD_SCRIPT_H
#define NMO_CMD_SCRIPT_H

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_script_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_script_graph(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_script_node(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_script_io(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_script_link(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_SCRIPT_H */
