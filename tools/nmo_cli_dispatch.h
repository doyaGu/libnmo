/**
 * @file nmo_cli_dispatch.h
 * @brief CLI command dispatch definitions
 *
 * Defines the group/action dispatch infrastructure for the new CLI architecture:
 *   nmo <group> <action> [options] <file>
 */

#ifndef NMO_CLI_DISPATCH_H
#define NMO_CLI_DISPATCH_H

#include "nmo_command_registry.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Find a command group by name or alias
 * @param name Group name to search for
 * @return Pointer to group, or NULL if not found
 */
static inline const nmo_cli_group_t *nmo_cli_find_group(const char *name) {
    return nmo_command_registry_find_group(name, true);
}

/**
 * @brief Find an action within a group by name or alias
 * @param group Group to search in
 * @param name Action name to search for
 * @return Pointer to action, or NULL if not found
 */
static inline const nmo_cli_action_t *nmo_cli_find_action(
    const nmo_cli_group_t *group,
    const char *name) {
    return nmo_command_registry_find_action(group, name, true);
}

/**
 * @brief Get all registered command groups
 * @param count Output: number of groups
 * @return Array of group pointers
 */
static inline const nmo_cli_group_t *nmo_cli_get_groups(size_t *count) {
    return nmo_command_registry_get_groups(count);
}

/**
 * @brief Print main CLI usage
 * @param out Output stream
 */
void nmo_cli_print_usage(FILE *out);

/**
 * @brief Print group help (list of actions)
 * @param group Group to print help for
 * @param out Output stream
 */
void nmo_cli_print_group_help(const nmo_cli_group_t *group, FILE *out);

/**
 * @brief Dispatch to appropriate group/action handler
 * @param argc Argument count (after global options consumed)
 * @param argv Argument vector
 * @param global Parsed global options
 * @return Exit code
 */
int nmo_cli_dispatch(int argc, char **argv, const nmo_cli_global_opts_t *global);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CLI_DISPATCH_H */
