/**
 * @file nmo_cli_dispatch.c
 * @brief CLI group/action routing.
 */

#include "nmo_cli_dispatch.h"
#include "nmo_cli_common.h"
#include "nmo_tool_common.h"

#include "commands/nmo_cmd_repl.h"

#include <stdio.h>
#include <string.h>

void nmo_cli_print_usage(FILE *out) {
    size_t group_count = 0;
    const nmo_cli_group_t *groups = nmo_cli_get_groups(&group_count);

    fprintf(out, "Usage: nmo [global-options] <group> <action> [options] [file]\n\n");
    fprintf(out, "Global Options:\n");
    fprintf(out, "  -h, --help              Show help\n");
    fprintf(out, "  -V, --version           Show version\n");
    fprintf(out, "  -f, --format <fmt>      Output format: text, json, json-pretty\n");
    fprintf(out, "  --color <mode>          Color mode: auto, always, never\n");
    fprintf(out, "  -o, --output <path>     Write output to file\n");
    fprintf(out, "  -v, --verbose           Increase verbosity (can repeat)\n");
    fprintf(out, "  -q, --quiet             Suppress non-essential output\n");
    fprintf(out, "  --no-pager              Disable pager for long output\n");
    fprintf(out, "  --strict                Enable strict validation mode\n");
    fprintf(out, "  --fail-on-warning       Exit with code 4 on warnings\n");
    fprintf(out, "  --plugin <path>         Load extension plugin (repeatable)\n");
    fprintf(out, "  -F, --filter <pattern>  Filter objects by name pattern\n");
    fprintf(out, "  --batch                 Process multiple files\n");
    fprintf(out, "\n");
    fprintf(out, "Command Groups:\n");
    for (size_t i = 0; i < group_count; ++i) {
        const nmo_cli_group_t *g = &groups[i];
        if (g->alias) {
            fprintf(out, "  %-10s (%-4s)  %s\n", g->name, g->alias, g->brief);
        } else {
            fprintf(out, "  %-10s        %s\n", g->name, g->brief);
        }
    }
    fprintf(out, "\n");
    fprintf(out, "Examples:\n");
    fprintf(out, "  nmo file info example.nmo           Show file summary\n");
    fprintf(out, "  nmo object list example.nmo         List all objects\n");
    fprintf(out, "  nmo object show 42 example.nmo      Show object #42\n");
    fprintf(out, "  nmo --format json file stats x.nmo  Output as JSON\n");
    fprintf(out, "  nmo repl start example.nmo          Start interactive REPL\n");
    fprintf(out, "\n");
    fprintf(out, "Run 'nmo <group> --help' for group-specific help.\n");
}

void nmo_cli_print_group_help(const nmo_cli_group_t *group, FILE *out) {
    if (!group || !out) {
        return;
    }

    fprintf(out, "Usage: nmo [global-options] %s <action> [options] [file]\n\n", group->name);
    fprintf(out, "%s\n\n", group->brief);
    fprintf(out, "Actions:\n");
    for (size_t i = 0; i < group->action_count; ++i) {
        const nmo_cli_action_t *a = &group->actions[i];
        if (a->alias) {
            fprintf(out, "  %-12s (%-3s)  %s\n", a->name, a->alias, a->brief);
        } else {
            fprintf(out, "  %-12s        %s\n", a->name, a->brief);
        }
    }
    fprintf(out, "\n");
    fprintf(out, "Run 'nmo %s <action> --help' for action-specific help.\n", group->name);
}

static const nmo_cli_action_t *nmo_cli_find_sub_action(
    const nmo_cli_action_t *parent, const char *name)
{
    for (size_t i = 0; i < parent->sub_action_count; ++i) {
        const nmo_cli_action_t *a = &parent->sub_actions[i];
        if (nmo_tool_streq_ci(name, a->name)) return a;
        if (a->alias && nmo_tool_streq_ci(name, a->alias)) return a;
    }
    return NULL;
}

static void nmo_cli_print_sub_action_help(
    const nmo_cli_group_t *group,
    const nmo_cli_action_t *parent,
    FILE *out)
{
    fprintf(out, "Usage: nmo %s %s <command> [options] [file]\n\n",
            group->name, parent->name);
    fprintf(out, "%s\n\n", parent->brief);
    if (parent->print_usage) {
        parent->print_usage(out);
    }
    fprintf(out, "Commands:\n");
    for (size_t i = 0; i < parent->sub_action_count; ++i) {
        const nmo_cli_action_t *a = &parent->sub_actions[i];
        const char *suffix = "";
        if (parent->default_sub && strcmp(a->name, parent->default_sub) == 0)
            suffix = " (default)";
        if (a->alias)
            fprintf(out, "  %-16s (%-3s)  %s%s\n",
                    a->name, a->alias, a->brief, suffix);
        else
            fprintf(out, "  %-16s        %s%s\n",
                    a->name, a->brief, suffix);
    }
}

static int nmo_cli_dispatch_sub_action(
    const nmo_cli_group_t *group,
    const nmo_cli_action_t *parent,
    int argc, char **argv,
    const nmo_cli_global_opts_t *global)
{
    if (argc < 2) {
        if (parent->default_sub) {
            const nmo_cli_action_t *def =
                nmo_cli_find_sub_action(parent, parent->default_sub);
            if (def) return def->handler(argc, argv, global);
        }
        nmo_cli_print_sub_action_help(group, parent,
            global->show_help ? stdout : stderr);
        return global->show_help ? NMO_CLI_EXIT_SUCCESS : NMO_CLI_EXIT_ARG_ERROR;
    }

    if (nmo_tool_streq_ci(argv[1], "--help") ||
        nmo_tool_streq_ci(argv[1], "-h")) {
        nmo_cli_print_sub_action_help(group, parent, stdout);
        return NMO_CLI_EXIT_SUCCESS;
    }

    const char *sub_name = argv[1];
    const nmo_cli_action_t *sub = NULL;
    if (sub_name[0] != '-' &&
        !(sub_name[0] >= '0' && sub_name[0] <= '9')) {
        sub = nmo_cli_find_sub_action(parent, sub_name);
    }

    if (sub) {
        for (int i = 2; i < argc; ++i) {
            if (nmo_tool_streq_ci(argv[i], "--help") ||
                nmo_tool_streq_ci(argv[i], "-h")) {
                fprintf(stdout, "Usage: nmo %s %s %s [options] [file]\n\n",
                        group->name, parent->name, sub->name);
                fprintf(stdout, "%s\n\n", sub->brief);
                if (sub->print_usage) sub->print_usage(stdout);
                return NMO_CLI_EXIT_SUCCESS;
            }
        }
        return sub->handler(argc - 1, argv + 1, global);
    }

    if (parent->default_sub) {
        sub = nmo_cli_find_sub_action(parent, parent->default_sub);
        if (sub) return sub->handler(argc, argv, global);
    }

    fprintf(stderr, "Error: Unknown sub-command '%s' in '%s %s'\n\n",
            sub_name, group->name, parent->name);
    nmo_cli_print_sub_action_help(group, parent, stderr);
    return NMO_CLI_EXIT_ARG_ERROR;
}

static int nmo_cli_dispatch_repl(int argc, char **argv,
                                 const nmo_cli_global_opts_t *global) {
    const nmo_cli_group_t *group =
        nmo_command_registry_find_group("repl", false);
    if (!group) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (global->show_help || argc < 2 ||
        nmo_tool_streq_ci(argv[1], "--help") ||
        nmo_tool_streq_ci(argv[1], "-h")) {
        nmo_cli_print_group_help(group, stdout);
        return (argc < 2 && !global->show_help) ? NMO_CLI_EXIT_ARG_ERROR : NMO_CLI_EXIT_SUCCESS;
    }

    const nmo_cli_action_t *action =
        nmo_command_registry_find_action(group, argv[1], true);
    if (!action || !nmo_tool_streq_ci(action->name, "start")) {
        fprintf(stderr, "Error: Unknown action '%s' in group 'repl'\n\n", argv[1]);
        nmo_cli_print_group_help(group, stderr);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    for (int i = 2; i < argc; ++i) {
        if (nmo_tool_streq_ci(argv[i], "--help") ||
            nmo_tool_streq_ci(argv[i], "-h")) {
            fprintf(stdout, "Usage: nmo repl start [options] [file]\n\n");
            fprintf(stdout, "%s\n\n", action->brief);
            if (action->print_usage) {
                action->print_usage(stdout);
            }
            return NMO_CLI_EXIT_SUCCESS;
        }
    }
    return nmo_cmd_repl_start(argc - 1, argv + 1, global);
}

int nmo_cli_dispatch(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    if (!global) {
        fprintf(stderr, "Error: Invalid global options\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (argc < 1 || !argv[0]) {
        if (global->show_help) {
            nmo_cli_print_usage(stdout);
            return NMO_CLI_EXIT_SUCCESS;
        }
        fprintf(stderr, "Error: No command group specified\n\n");
        nmo_cli_print_usage(stderr);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *group_name = argv[0];
    if (nmo_tool_streq_ci(group_name, "repl")) {
        return nmo_cli_dispatch_repl(argc, argv, global);
    }

    if (nmo_tool_streq_ci(group_name, "help")) {
        if (argc >= 2) {
            const nmo_cli_group_t *g = nmo_cli_find_group(argv[1]);
            if (g) {
                nmo_cli_print_group_help(g, stdout);
                return NMO_CLI_EXIT_SUCCESS;
            }
            fprintf(stderr, "Error: Unknown group '%s'\n", argv[1]);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        nmo_cli_print_usage(stdout);
        return NMO_CLI_EXIT_SUCCESS;
    }

    const nmo_cli_group_t *group = nmo_cli_find_group(group_name);
    if (!group) {
        fprintf(stderr, "Error: Unknown group '%s'\n\n", group_name);
        nmo_cli_print_usage(stderr);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (global->show_help || argc < 2) {
        nmo_cli_print_group_help(group, stdout);
        return (argc < 2 && !global->show_help) ? NMO_CLI_EXIT_ARG_ERROR : NMO_CLI_EXIT_SUCCESS;
    }

    const char *action_name = argv[1];
    if (nmo_tool_streq_ci(action_name, "--help") ||
        nmo_tool_streq_ci(action_name, "-h")) {
        nmo_cli_print_group_help(group, stdout);
        return NMO_CLI_EXIT_SUCCESS;
    }

    const nmo_cli_action_t *action = nmo_cli_find_action(group, action_name);
    if (!action) {
        fprintf(stderr, "Error: Unknown action '%s' in group '%s'\n\n", action_name, group->name);
        nmo_cli_print_group_help(group, stderr);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (action->sub_actions && action->sub_action_count > 0) {
        return nmo_cli_dispatch_sub_action(group, action,
                                           argc - 1, argv + 1, global);
    }

    for (int i = 2; i < argc; ++i) {
        if (nmo_tool_streq_ci(argv[i], "--help") ||
            nmo_tool_streq_ci(argv[i], "-h")) {
            fprintf(stdout, "Usage: nmo %s %s [options] [file]\n\n", group->name, action->name);
            fprintf(stdout, "%s\n\n", action->brief);
            if (action->print_usage) {
                action->print_usage(stdout);
            }
            return NMO_CLI_EXIT_SUCCESS;
        }
    }

    return action->handler(argc - 1, argv + 1, global);
}
