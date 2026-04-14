/**
 * @file nmo_cmd_resource.h
 * @brief CLI resource command group implementation
 */

#ifndef NMO_CMD_RESOURCE_H
#define NMO_CMD_RESOURCE_H

#include "../nmo_cli_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_resource_list(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_resource_show(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_resource_extract(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_resource_import(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_resource_replace(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_resource_remove(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_resource_info(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_RESOURCE_H */
