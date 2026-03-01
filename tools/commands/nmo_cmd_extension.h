/**
 * @file nmo_cmd_extension.h
 * @brief CLI extension command group
 */

#ifndef NMO_CMD_EXTENSION_H
#define NMO_CMD_EXTENSION_H

#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo extension list - List registered extensions
 */
int nmo_cmd_extension_list(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo extension load - Load an extension DLL
 */
int nmo_cmd_extension_load(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo extension info - Query extension metadata
 */
int nmo_cmd_extension_info(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo extension check - Check plugin dependencies for a file
 */
int nmo_cmd_extension_check(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_EXTENSION_H */
