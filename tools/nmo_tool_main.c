/**
 * nmo - Unified CLI tool (inspect + debug)
 */

#include "nmo.h"

#include <stdio.h>
#include <string.h>

int nmo_inspect_run(int argc, char **argv);
int nmo_debug_run(int argc, char **argv);

static void print_usage(void) {
    printf("Usage: nmo <command> [options]\n\n");
    printf("Commands:\n");
    printf("  inspect    Inspect Virtools file contents (default)\n");
    printf("  debug      Interactive debugger (REPL)\n\n");
    printf("  help       Show help (optionally for a subcommand)\n\n");
    printf("Examples:\n");
    printf("  nmo inspect --help\n");
    printf("  nmo debug <file>\n");
    printf("  nmo help inspect\n");
}

static bool is_help_flag(const char *arg) {
    return arg && (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0);
}

static bool is_version_flag(const char *arg) {
    return arg && (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0);
}

static int run_subcommand_help(const char *command) {
    if (!command || command[0] == '\0') {
        print_usage();
        return 0;
    }

    if (strcmp(command, "inspect") == 0) {
        char *argv_help[] = { (char *)"inspect", (char *)"--help", NULL };
        return nmo_inspect_run(2, argv_help);
    }
    if (strcmp(command, "debug") == 0) {
        char *argv_help[] = { (char *)"debug", (char *)"--help", NULL };
        return nmo_debug_run(2, argv_help);
    }

    fprintf(stderr, "Error: Unknown command for help: %s\n\n", command);
    print_usage();
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *command = argv[1];
    if (is_help_flag(command)) {
        if (argc >= 3) {
            return run_subcommand_help(argv[2]);
        }
        print_usage();
        return 0;
    }
    if (is_version_flag(command)) {
        printf("nmo %d.%d.%d\n", NMO_VERSION_MAJOR, NMO_VERSION_MINOR, NMO_VERSION_PATCH);
        return 0;
    }

    if (strcmp(command, "help") == 0) {
        if (argc < 3) {
            print_usage();
            return 0;
        }
        return run_subcommand_help(argv[2]);
    }

    if (strcmp(command, "inspect") == 0) {
        return nmo_inspect_run(argc - 1, argv + 1);
    }
    if (strcmp(command, "debug") == 0) {
        return nmo_debug_run(argc - 1, argv + 1);
    }

    return nmo_inspect_run(argc, argv);
}
