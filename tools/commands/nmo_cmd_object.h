/**
 * @file nmo_cmd_object.h
 * @brief CLI object command group
 */

#ifndef NMO_CMD_OBJECT_H
#define NMO_CMD_OBJECT_H

#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo object list - List objects
 */
int nmo_cmd_object_list(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo object tree - Show object hierarchy
 */
int nmo_cmd_object_tree(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo object show - Show object details
 */
int nmo_cmd_object_show(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo object find - Find objects by query
 */
int nmo_cmd_object_find(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo object refs - Show object references
 */
int nmo_cmd_object_refs(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo object rename - Rename an object and save
 */
int nmo_cmd_object_rename(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_OBJECT_H */
