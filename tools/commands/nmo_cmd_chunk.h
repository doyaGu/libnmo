/**
 * @file nmo_cmd_chunk.h
 * @brief CLI chunk command group
 */

#ifndef NMO_CMD_CHUNK_H
#define NMO_CMD_CHUNK_H

#include "../nmo_cli_common.h"

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

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_CHUNK_H */
