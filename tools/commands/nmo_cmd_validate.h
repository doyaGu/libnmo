/**
 * @file nmo_cmd_validate.h
 * @brief CLI validate command group
 */

#ifndef NMO_CMD_VALIDATE_H
#define NMO_CMD_VALIDATE_H

#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo validate all - Run all validation checks
 */
int nmo_cmd_validate_all(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo validate structure - Validate file structure
 */
int nmo_cmd_validate_structure(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo validate references - Validate object references
 */
int nmo_cmd_validate_references(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo validate resources - Validate embedded resources
 */
int nmo_cmd_validate_resources(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief nmo validate orphans - Find unreferenced objects
 */
int nmo_cmd_validate_orphans(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_VALIDATE_H */
