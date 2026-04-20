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
int nmo_cmd_behavior_graph_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_behavior_dump(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_find(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_trace(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/* Link commands */
int nmo_cmd_behavior_add_link(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_remove_link(int argc, char **argv, const nmo_cli_global_opts_t *global);

/* Interface sub-action handlers */
int nmo_cmd_behavior_iface_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_interface_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);
int nmo_cmd_behavior_iface_set_pos(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_fold(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_unfold(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_set_color(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_canonicalize(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_add_comment(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_behavior_iface_remove_comment(int argc, char **argv, const nmo_cli_global_opts_t *global);

/* Interface sub-action table for dispatch */
#define NMO_BEHAVIOR_INTERFACE_SUB_ACTION_COUNT 24
extern const nmo_cli_action_t nmo_behavior_interface_sub_actions[];

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_BEHAVIOR_H */
