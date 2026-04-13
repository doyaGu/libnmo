/**
 * @file nmo_cli_dispatch.h
 * @brief CLI command dispatch definitions
 *
 * Defines the group/action dispatch infrastructure for the new CLI architecture:
 *   nmo <group> <action> [options] <file>
 */

#ifndef NMO_CLI_DISPATCH_H
#define NMO_CLI_DISPATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct nmo_cli_global_opts nmo_cli_global_opts_t;

/**
 * @brief Action handler function signature
 * @param argc Argument count (including action name)
 * @param argv Argument vector
 * @param global Global options (already parsed)
 * @return Exit code (0 = success)
 */
typedef int (*nmo_cli_action_handler_t)(int argc, char **argv, const nmo_cli_global_opts_t *global);

/**
 * @brief Action usage printer function signature
 * @param out Output stream
 */
typedef void (*nmo_cli_action_usage_t)(FILE *out);

/**
 * @brief Single CLI action within a group
 */
typedef struct nmo_cli_action {
    const char *name;       /**< Primary name (e.g., "list") */
    const char *alias;      /**< Optional alias (e.g., "ls"), NULL if none */
    const char *brief;      /**< One-line description */
    nmo_cli_action_handler_t handler;
    nmo_cli_action_usage_t print_usage;
    /* Sub-action support (NULL = leaf action) */
    const struct nmo_cli_action *sub_actions; /**< Sub-action table, NULL for leaf */
    size_t sub_action_count;                  /**< Number of sub-actions */
    const char *default_sub;                  /**< Default sub-action name for fallback */
} nmo_cli_action_t;

/**
 * @brief Command group containing related actions
 */
typedef struct nmo_cli_group {
    const char *name;       /**< Primary name (e.g., "file") */
    const char *alias;      /**< Optional alias (e.g., "f"), NULL if none */
    const char *brief;      /**< One-line description */
    const nmo_cli_action_t *actions;
    size_t action_count;
} nmo_cli_group_t;

/**
 * @brief Find a command group by name or alias
 * @param name Group name to search for
 * @return Pointer to group, or NULL if not found
 */
const nmo_cli_group_t *nmo_cli_find_group(const char *name);

/**
 * @brief Find an action within a group by name or alias
 * @param group Group to search in
 * @param name Action name to search for
 * @return Pointer to action, or NULL if not found
 */
const nmo_cli_action_t *nmo_cli_find_action(const nmo_cli_group_t *group, const char *name);

/**
 * @brief Get all registered command groups
 * @param count Output: number of groups
 * @return Array of group pointers
 */
const nmo_cli_group_t *nmo_cli_get_groups(size_t *count);

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
