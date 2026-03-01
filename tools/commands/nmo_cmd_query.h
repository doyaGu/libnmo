/**
 * @file nmo_cmd_query.h
 * @brief CLI query command group (DSL integration)
 */

#ifndef NMO_CMD_QUERY_H
#define NMO_CMD_QUERY_H

#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo query eval - Evaluate a single DSL expression
 */
int nmo_cmd_query_eval(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo query script - Execute a DSL script file
 */
int nmo_cmd_query_script(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo query schema - Apply a DSL schema definition
 */
int nmo_cmd_query_schema(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo query module - Run a complete DSL module
 */
int nmo_cmd_query_module(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_QUERY_H */
