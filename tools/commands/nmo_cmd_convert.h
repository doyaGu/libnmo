/**
 * @file nmo_cmd_convert.h
 * @brief CLI convert command group
 */

#ifndef NMO_CMD_CONVERT_H
#define NMO_CMD_CONVERT_H

#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo convert copy - Round-trip copy with save options
 */
int nmo_cmd_convert_copy(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo convert version - Show/modify file version metadata
 */
int nmo_cmd_convert_version(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo convert strip - Remove objects by class/name pattern
 */
int nmo_cmd_convert_strip(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo convert merge - Merge objects from source into target
 */
int nmo_cmd_convert_merge(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_CONVERT_H */
