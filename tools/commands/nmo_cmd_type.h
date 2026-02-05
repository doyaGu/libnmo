/**
 * @file nmo_cmd_type.h
 * @brief CLI type command group
 */

#ifndef NMO_CMD_TYPE_H
#define NMO_CMD_TYPE_H

#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo type list - List registered types
 */
int nmo_cmd_type_list(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo type show - Show type details
 */
int nmo_cmd_type_show(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo type class-tree - Show class hierarchy tree
 */
int nmo_cmd_type_class_tree(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_TYPE_H */
