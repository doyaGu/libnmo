/**
 * @file nmo_cmd_behavior.h
 * @brief CLI behavior command group implementation
 */

#ifndef NMO_CMD_BEHAVIOR_H
#define NMO_CMD_BEHAVIOR_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_behavior_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_stats(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_graph(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_BEHAVIOR_H */
