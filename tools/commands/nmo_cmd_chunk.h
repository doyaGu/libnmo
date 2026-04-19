/**
 * @file nmo_cmd_chunk.h
 * @brief CLI chunk command group
 */

#ifndef NMO_CMD_CHUNK_H
#define NMO_CMD_CHUNK_H

#include "../nmo_cli_common.h"
#include "../nmo_cmd_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo chunk list - List all chunks
 */
int nmo_cmd_chunk_list(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo chunk tree - Show chunk hierarchy
 */
int nmo_cmd_chunk_tree(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo chunk show - Show chunk details
 */
int nmo_cmd_chunk_show(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo chunk find - Find chunks by class/name
 */
int nmo_cmd_chunk_find(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief Run chunk read actions against an existing session.
 */
int nmo_cmd_chunk_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_CHUNK_H */
