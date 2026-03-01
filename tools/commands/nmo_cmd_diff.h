/**
 * @file nmo_cmd_diff.h
 * @brief CLI diff command group
 */

#ifndef NMO_CMD_DIFF_H
#define NMO_CMD_DIFF_H

#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo diff summary - Show high-level diff between two files
 */
int nmo_cmd_diff_summary(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo diff objects - Compare objects between two files
 */
int nmo_cmd_diff_objects(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo diff chunks - Compare chunks between two files
 */
int nmo_cmd_diff_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo diff full - Comprehensive comparison
 */
int nmo_cmd_diff_full(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_DIFF_H */
