/**
 * @file nmo_cli_dispatch.c
 * @brief CLI group/action registration and routing
 */

#include "nmo_cli_dispatch.h"
#include "nmo_cli_common.h"
#include "nmo_tool_common.h"

/* Command group headers */
#include "commands/nmo_cmd_file.h"
#include "commands/nmo_cmd_chunk.h"
#include "commands/nmo_cmd_object.h"
#include "commands/nmo_cmd_debug.h"
#include "commands/nmo_cmd_repl.h"
#include "commands/nmo_cmd_type.h"
#include "commands/nmo_cmd_validate.h"
#include "commands/nmo_cmd_resource.h"
#include "commands/nmo_cmd_behavior.h"
#include "commands/nmo_cmd_parameter.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Stub handler (returns "not implemented")
 * ============================================================================ */

static int stub_handler(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    (void)argc;
    (void)argv;
    (void)global;
    fprintf(stderr, "Error: This command is not yet implemented\n");
    return NMO_CLI_EXIT_INTERNAL_ERROR;
}

static void stub_usage(FILE *out) {
    fprintf(out, "  (No detailed usage available yet)\n");
}

/* Usage printers for implemented commands */
static void file_info_usage(FILE *out) {
    fprintf(out, "Usage: nmo file info <file>\n\n");
    fprintf(out, "Show basic file information including object count, manager count,\n");
    fprintf(out, "and CK version.\n");
}

static void file_header_usage(FILE *out) {
    fprintf(out, "Usage: nmo file header <file>\n\n");
    fprintf(out, "Show detailed file header fields including signature, version,\n");
    fprintf(out, "CRC, packed/unpacked sizes, etc.\n");
}

static void file_stats_usage(FILE *out) {
    fprintf(out, "Usage: nmo file stats <file>\n\n");
    fprintf(out, "Show file statistics including object counts by category,\n");
    fprintf(out, "chunk statistics, and unique class counts.\n");
}

static void file_plugins_usage(FILE *out) {
    fprintf(out, "Usage: nmo file plugins <file>\n\n");
    fprintf(out, "Show plugin dependencies and their status.\n");
}

static void chunk_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo chunk list <file>\n\n");
    fprintf(out, "List all chunks in the file (including sub-chunks) with a stable\n");
    fprintf(out, "flat index, parent index, owner object ID, class ID, and data size.\n");
}

static void chunk_tree_usage(FILE *out) {
    fprintf(out, "Usage: nmo chunk tree <file>\n\n");
    fprintf(out, "Show chunk sub-chunk hierarchy for each object.\n");
}

static void chunk_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo chunk show --index <n> <file>\n");
    fprintf(out, "       nmo chunk show <object-id> <file>\n\n");
    fprintf(out, "Show detailed information about a specific chunk.\n");
    fprintf(out, "--index selects from the flat list shown by 'nmo chunk list'.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  --hexdump           Include hexdump -C compatible output (text) or data_hex (JSON)\n");
    fprintf(out, "  --max-bytes, -m <n> Limit data bytes emitted for --hexdump (default 256; 0 = all)\n");
}

static void chunk_find_usage(FILE *out) {
    fprintf(out, "Usage: nmo chunk find --class <name> <file>\n\n");
    fprintf(out, "Find chunks by class (includes derived classes).\n");
}

static void object_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo object list [--class <name>] <file>\n\n");
    fprintf(out, "List all objects in the file.\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --class, -c <name>  Filter by class (includes derived classes)\n");
}

static void object_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo object show <id> <file>\n\n");
    fprintf(out, "Show detailed information about a specific object.\n");
}

static void object_tree_usage(FILE *out) {
    fprintf(out, "Usage: nmo object tree <file>\n\n");
    fprintf(out, "Show the object parent/child hierarchy as a tree.\n");
}

static void object_find_usage(FILE *out) {
    fprintf(out, "Usage: nmo object find [--name <pattern>] [--class <name>] <file>\n\n");
    fprintf(out, "Find objects by name pattern and/or class (includes derived classes).\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --name <pattern>      Name filter (case-insensitive). Patterns: *, prefix*, *suffix, exact\n");
    fprintf(out, "  --class, -c <name>    Class filter (includes derived classes)\n\n");
    fprintf(out, "Note: in PowerShell, quote '*' like --name '*' to avoid wildcard expansion.\n");
}

static void object_refs_usage(FILE *out) {
    fprintf(out, "Usage: nmo object refs <id> <file>\n\n");
    fprintf(out, "Show incoming and outgoing references for a specific object.\n");
}

static void debug_load_phases_usage(FILE *out) {
    fprintf(out, "Usage: nmo debug load-phases <file>\n\n");
    fprintf(out, "Show load pipeline phase details including reference resolution\n");
    fprintf(out, "statistics and index memory usage.\n");
}

static void debug_chunks_usage(FILE *out) {
    fprintf(out, "Usage: nmo debug chunks <file>\n\n");
    fprintf(out, "Show detailed chunk parse information for debugging.\n");
}

static void debug_objects_usage(FILE *out) {
    fprintf(out, "Usage: nmo debug objects <file>\n\n");
    fprintf(out, "Show detailed object load information for debugging.\n");
}

static void repl_start_usage(FILE *out) {
    fprintf(out, "Usage: nmo repl start <file>\n\n");
    fprintf(out, "Start the interactive debugger REPL for exploring the file.\n");
}

static void type_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo type list\n\n");
    fprintf(out, "List all registered class types.\n");
}

static void type_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo type show <class-name-or-id>\n\n");
    fprintf(out, "Show details about a specific class type.\n");
}

static void validate_all_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate all <file>\n\n");
    fprintf(out, "Run all validation checks on the file.\n\n");
    fprintf(out, "Exit codes:\n");
    fprintf(out, "  0 - Success (valid file)\n");
    fprintf(out, "  3 - Strict mode failure (with --strict)\n");
    fprintf(out, "  4 - Warnings exist (with --fail-on-warning)\n");
}

static void validate_structure_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate structure <file>\n\n");
    fprintf(out, "Validate basic file/chunk structure and report issues.\n\n");
    fprintf(out, "Global options:\n");
    fprintf(out, "  --strict            Exit with code 3 if errors exist\n");
    fprintf(out, "  --fail-on-warning   Exit with code 4 if warnings exist\n");
}

static void validate_references_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate references <file>\n\n");
    fprintf(out, "Validate object reference graph (broken/self references, etc).\n\n");
    fprintf(out, "Global options:\n");
    fprintf(out, "  --strict            Exit with code 3 if errors exist\n");
    fprintf(out, "  --fail-on-warning   Exit with code 4 if warnings exist\n");
}

static void validate_resources_usage(FILE *out) {
    fprintf(out, "Usage: nmo validate resources <file>\n\n");
    fprintf(out, "Validate embedded resource/plugin entries against the registry.\n\n");
    fprintf(out, "Global options:\n");
    fprintf(out, "  --strict            Exit with code 3 if errors exist\n");
    fprintf(out, "  --fail-on-warning   Exit with code 4 if warnings exist\n");
}

static void debug_export_usage(FILE *out) {
    fprintf(out, "Usage: nmo debug export [--data] [--max-bytes <n>] <file>\n\n");
    fprintf(out, "Export a JSON snapshot for debugging (object list + chunk metadata).\n\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --data, --include-data     Include chunk raw bytes as data_hex\n");
    fprintf(out, "  --max-bytes <n>            Limit emitted chunk bytes when --data is set (default 4096)\n");
    fprintf(out, "\nNotes:\n");
    fprintf(out, "  Use --format json-pretty for human-readable JSON.\n");
    fprintf(out, "  Use -o/--output to write JSON to a file.\n");
}

static void resource_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource list <file>\n\n");
    fprintf(out, "List included files/resources embedded in the NMO.\n");
}

static void resource_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource show [--index <n> | --name <name>] <file>\n\n");
    fprintf(out, "Show details for a single included resource, including owner object IDs.\n");
}

static void resource_extract_usage(FILE *out) {
    fprintf(out, "Usage: nmo resource extract --out-dir <dir> [--index <n> | --name <name>] [--overwrite] <file>\n\n");
    fprintf(out, "Extract included resources to a directory.\n");
    fprintf(out, "Entries marked metadata-only or with no payload are skipped.\n");
}

static void behavior_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior list <file>\n\n");
    fprintf(out, "List behavior objects in the file (CKBehavior-derived).\n");
}

static void behavior_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior show <id> <file>\n\n");
    fprintf(out, "Show a behavior object (same output as 'nmo object show').\n");
}

static void behavior_stats_usage(FILE *out) {
    fprintf(out, "Usage: nmo behavior stats <file>\n\n");
    fprintf(out, "Show behavior counts and distribution by class.\n");
}

static void parameter_list_usage(FILE *out) {
    fprintf(out, "Usage: nmo parameter list <file>\n\n");
    fprintf(out, "List parameter objects in the file (CKParameter* family).\n");
}

static void parameter_show_usage(FILE *out) {
    fprintf(out, "Usage: nmo parameter show <id> <file>\n\n");
    fprintf(out, "Show a parameter object (same output as 'nmo object show').\n");
}

static void type_class_tree_usage(FILE *out) {
    fprintf(out, "Usage: nmo type class-tree\n\n");
    fprintf(out, "Show the registered class hierarchy as a tree.\n");
}

/* ============================================================================
 * Action definitions for each group
 * ============================================================================ */

/* file group actions */
static const nmo_cli_action_t file_actions[] = {
    {"info", "i", "Show file summary", nmo_cmd_file_info, file_info_usage},
    {"header", "hdr", "Show file header fields", nmo_cmd_file_header, file_header_usage},
    {"stats", "st", "Show file statistics", nmo_cmd_file_stats, file_stats_usage},
    {"plugins", "pl", "Show plugin dependencies", nmo_cmd_file_plugins, file_plugins_usage},
};

/* chunk group actions */
static const nmo_cli_action_t chunk_actions[] = {
    {"list", "ls", "List all chunks", nmo_cmd_chunk_list, chunk_list_usage},
    {"tree", "t", "Show chunk hierarchy", nmo_cmd_chunk_tree, chunk_tree_usage},
    {"show", "s", "Show chunk details", nmo_cmd_chunk_show, chunk_show_usage},
    {"find", "f", "Find chunks by class/name", nmo_cmd_chunk_find, chunk_find_usage},
};

/* object group actions */
static const nmo_cli_action_t object_actions[] = {
    {"list", "ls", "List objects", nmo_cmd_object_list, object_list_usage},
    {"tree", "t", "Show object hierarchy", nmo_cmd_object_tree, object_tree_usage},
    {"show", "s", "Show object details", nmo_cmd_object_show, object_show_usage},
    {"find", "f", "Find objects by query", nmo_cmd_object_find, object_find_usage},
    {"refs", "r", "Show object references", nmo_cmd_object_refs, object_refs_usage},
};

/* behavior group actions */
static const nmo_cli_action_t behavior_actions[] = {
    {"list", "ls", "List behaviors", nmo_cmd_behavior_list, behavior_list_usage},
    {"stats", "st", "Show behavior statistics", nmo_cmd_behavior_stats, behavior_stats_usage},
    {"show", "s", "Show behavior object", nmo_cmd_behavior_show, behavior_show_usage},
    {"graph", "g", "Export behavior graph", stub_handler, stub_usage},
    {"links", "l", "Show behavior links", stub_handler, stub_usage},
};

/* parameter group actions */
static const nmo_cli_action_t parameter_actions[] = {
    {"list", "ls", "List parameters", nmo_cmd_parameter_list, parameter_list_usage},
    {"show", "s", "Show parameter object", nmo_cmd_parameter_show, parameter_show_usage},
    {"graph", "g", "Export parameter network", stub_handler, stub_usage},
};

/* resource group actions */
static const nmo_cli_action_t resource_actions[] = {
    {"list", "ls", "List resources", nmo_cmd_resource_list, resource_list_usage},
    {"show", "s", "Show resource details", nmo_cmd_resource_show, resource_show_usage},
    {"extract", "x", "Extract embedded resources", nmo_cmd_resource_extract, resource_extract_usage},
};

/* type group actions */
static const nmo_cli_action_t type_actions[] = {
    {"list", "ls", "List registered types", nmo_cmd_type_list, type_list_usage},
    {"show", "s", "Show type details", nmo_cmd_type_show, type_show_usage},
    {"class-tree", "ct", "Show class hierarchy tree", nmo_cmd_type_class_tree, type_class_tree_usage},
};

/* validate group actions */
static const nmo_cli_action_t validate_actions[] = {
    {"all", "a", "Run all validation checks", nmo_cmd_validate_all, validate_all_usage},
    {"structure", "st", "Validate file structure", nmo_cmd_validate_structure, validate_structure_usage},
    {"references", "ref", "Validate object references", nmo_cmd_validate_references, validate_references_usage},
    {"resources", "res", "Validate embedded resources", nmo_cmd_validate_resources, validate_resources_usage},
};

/* convert group actions */
static const nmo_cli_action_t convert_actions[] = {
    {"version", "v", "Convert to different file version", stub_handler, stub_usage},
    {"format", "f", "Convert to different format", stub_handler, stub_usage},
};

/* diff group actions */
static const nmo_cli_action_t diff_actions[] = {
    {"summary", "s", "Show diff summary", stub_handler, stub_usage},
    {"objects", "obj", "Diff objects between files", stub_handler, stub_usage},
    {"chunks", "ch", "Diff chunks between files", stub_handler, stub_usage},
};

/* debug group actions */
static const nmo_cli_action_t debug_actions[] = {
    {"load-phases", "lp", "Show load pipeline phases", nmo_cmd_debug_load_phases, debug_load_phases_usage},
    {"chunks", "ch", "Show chunk parse details", nmo_cmd_debug_chunks, debug_chunks_usage},
    {"objects", "obj", "Show object load details", nmo_cmd_debug_objects, debug_objects_usage},
    {"export", "x", "Export JSON snapshot for debugging", nmo_cmd_debug_export, debug_export_usage},
};

/* repl group - single action */
static const nmo_cli_action_t repl_actions[] = {
    {"start", NULL, "Start interactive REPL", nmo_cmd_repl_start, repl_start_usage},
};

/* ============================================================================
 * Group definitions
 * ============================================================================ */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const nmo_cli_group_t groups[] = {
    {"file", "f", "File information and statistics", file_actions, ARRAY_SIZE(file_actions)},
    {"chunk", "ch", "Chunk inspection", chunk_actions, ARRAY_SIZE(chunk_actions)},
    {"object", "obj", "Object inspection", object_actions, ARRAY_SIZE(object_actions)},
    {"behavior", "beh", "Behavior inspection", behavior_actions, ARRAY_SIZE(behavior_actions)},
    {"parameter", "param", "Parameter inspection", parameter_actions, ARRAY_SIZE(parameter_actions)},
    {"resource", "res", "Resource management", resource_actions, ARRAY_SIZE(resource_actions)},
    {"type", "t", "Type system information", type_actions, ARRAY_SIZE(type_actions)},
    {"validate", "val", "File validation", validate_actions, ARRAY_SIZE(validate_actions)},
    {"convert", "conv", "Format conversion", convert_actions, ARRAY_SIZE(convert_actions)},
    {"diff", "d", "File comparison", diff_actions, ARRAY_SIZE(diff_actions)},
    {"debug", "dbg", "Debugging tools", debug_actions, ARRAY_SIZE(debug_actions)},
    {"repl", NULL, "Interactive debugger", repl_actions, ARRAY_SIZE(repl_actions)},
};

static const size_t group_count = ARRAY_SIZE(groups);

/* ============================================================================
 * Lookup functions
 * ============================================================================ */

const nmo_cli_group_t *nmo_cli_find_group(const char *name) {
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < group_count; ++i) {
        const nmo_cli_group_t *g = &groups[i];
        if (nmo_tool_streq_ci(name, g->name)) {
            return g;
        }
        if (g->alias && nmo_tool_streq_ci(name, g->alias)) {
            return g;
        }
    }
    return NULL;
}

const nmo_cli_action_t *nmo_cli_find_action(const nmo_cli_group_t *group, const char *name) {
    if (!group || !name) {
        return NULL;
    }
    for (size_t i = 0; i < group->action_count; ++i) {
        const nmo_cli_action_t *a = &group->actions[i];
        if (nmo_tool_streq_ci(name, a->name)) {
            return a;
        }
        if (a->alias && nmo_tool_streq_ci(name, a->alias)) {
            return a;
        }
    }
    return NULL;
}

const nmo_cli_group_t *nmo_cli_get_groups(size_t *count) {
    if (count) {
        *count = group_count;
    }
    return groups;
}

/* ============================================================================
 * Help/usage printing
 * ============================================================================ */

void nmo_cli_print_usage(FILE *out) {
    fprintf(out, "Usage: nmo [global-options] <group> <action> [options] [file]\n\n");
    fprintf(out, "Global Options:\n");
    fprintf(out, "  -h, --help              Show help\n");
    fprintf(out, "  -V, --version           Show version\n");
    fprintf(out, "  -f, --format <fmt>      Output format: text, json, json-pretty, yaml\n");
    fprintf(out, "  --color <mode>          Color mode: auto, always, never\n");
    fprintf(out, "  -o, --output <path>     Write output to file\n");
    fprintf(out, "  -v, --verbose           Increase verbosity (can repeat)\n");
    fprintf(out, "  -q, --quiet             Suppress non-essential output\n");
    fprintf(out, "  --no-pager              Disable pager for long output\n");
    fprintf(out, "  --strict                Enable strict validation mode\n");
    fprintf(out, "  --fail-on-warning       Exit with code 4 on warnings\n");
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

/* ============================================================================
 * Dispatch
 * ============================================================================ */

int nmo_cli_dispatch(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    if (!global) {
        fprintf(stderr, "Error: Invalid global options\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* No group specified */
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

    /* Help for main command */
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

    /* Find group */
    const nmo_cli_group_t *group = nmo_cli_find_group(group_name);
    if (!group) {
        fprintf(stderr, "Error: Unknown group '%s'\n\n", group_name);
        nmo_cli_print_usage(stderr);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Group help if --help or no action */
    if (global->show_help || argc < 2) {
        nmo_cli_print_group_help(group, stdout);
        return (argc < 2 && !global->show_help) ? NMO_CLI_EXIT_ARG_ERROR : NMO_CLI_EXIT_SUCCESS;
    }

    const char *action_name = argv[1];

    /* Check for action-level help flag */
    if (nmo_tool_streq_ci(action_name, "--help") || nmo_tool_streq_ci(action_name, "-h")) {
        nmo_cli_print_group_help(group, stdout);
        return NMO_CLI_EXIT_SUCCESS;
    }

    /* Find action */
    const nmo_cli_action_t *action = nmo_cli_find_action(group, action_name);
    if (!action) {
        fprintf(stderr, "Error: Unknown action '%s' in group '%s'\n\n", action_name, group->name);
        nmo_cli_print_group_help(group, stderr);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Check for help in remaining args */
    for (int i = 2; i < argc; ++i) {
        if (nmo_tool_streq_ci(argv[i], "--help") || nmo_tool_streq_ci(argv[i], "-h")) {
            fprintf(stdout, "Usage: nmo %s %s [options] [file]\n\n", group->name, action->name);
            fprintf(stdout, "%s\n\n", action->brief);
            if (action->print_usage) {
                action->print_usage(stdout);
            }
            return NMO_CLI_EXIT_SUCCESS;
        }
    }

    /* Dispatch to action handler */
    return action->handler(argc - 1, argv + 1, global);
}
