/**
 * @file nmo_cmd_behavior_rewrite.h
 * @brief Behavior graph rewrite CLI commands.
 */

#ifndef NMO_CMD_BEHAVIOR_REWRITE_H
#define NMO_CMD_BEHAVIOR_REWRITE_H

#include "../nmo_cli_dispatch.h"
#include "../nmo_cmd_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_behavior_graph_boundary(int argc,
                                    char **argv,
                                    const nmo_cli_global_opts_t *global);

int nmo_cmd_behavior_graph_boundary_in_session(nmo_cmd_ctx_t *ctx,
                                               int argc,
                                               char **argv);

int nmo_cmd_behavior_replace_bb(int argc,
                                char **argv,
                                const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_BEHAVIOR_REWRITE_H */
