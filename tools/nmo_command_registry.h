/**
 * @file nmo_command_registry.h
 * @brief Shared CLI/REPL command registry metadata.
 */

#ifndef NMO_COMMAND_REGISTRY_H
#define NMO_COMMAND_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_cli_global_opts nmo_cli_global_opts_t;
typedef struct nmo_cmd_ctx nmo_cmd_ctx_t;

typedef int (*nmo_cli_action_handler_t)(int argc, char **argv,
                                        const nmo_cli_global_opts_t *global);
typedef void (*nmo_cli_action_usage_t)(FILE *out);
typedef int (*nmo_cli_group_session_handler_t)(nmo_cmd_ctx_t *ctx,
                                               int argc,
                                               char **argv);

typedef enum nmo_repl_action_policy {
    NMO_REPL_ACTION_FORBIDDEN = 0,
    NMO_REPL_ACTION_READ_SESSION,
    NMO_REPL_ACTION_READ_NO_SESSION,
    NMO_REPL_ACTION_MUTATE_SESSION_SUPPORTED,
    NMO_REPL_ACTION_MUTATE_FILE_ONLY
} nmo_repl_action_policy_t;

typedef struct nmo_cli_action {
    const char *name;
    const char *alias;
    const char *brief;
    nmo_cli_action_handler_t handler;
    nmo_cli_action_usage_t print_usage;
    const struct nmo_cli_action *sub_actions;
    size_t sub_action_count;
    const char *default_sub;
    nmo_repl_action_policy_t repl_policy;
} nmo_cli_action_t;

typedef struct nmo_cli_group {
    const char *name;
    const char *alias;
    const char *brief;
    const nmo_cli_action_t *actions;
    size_t action_count;
    nmo_cli_group_session_handler_t repl_session_handler;
} nmo_cli_group_t;

const nmo_cli_group_t *nmo_command_registry_find_group(const char *name,
                                                       bool allow_alias);
const nmo_cli_action_t *nmo_command_registry_find_action(
    const nmo_cli_group_t *group,
    const char *name,
    bool allow_alias);
const nmo_cli_group_t *nmo_command_registry_get_groups(size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_COMMAND_REGISTRY_H */
