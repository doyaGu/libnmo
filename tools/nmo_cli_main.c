/**
 * @file nmo_cli_main.c
 * @brief CLI entry point
 *
 * Unified CLI entry point with group/action dispatch:
 *   nmo [global-options] <group> <action> [options] [file]
 */

#include "nmo.h"
#include "nmo_cli_dispatch.h"
#include "nmo_cli_common.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    /* Handle no arguments */
    if (argc < 2) {
        nmo_cli_print_usage(stderr);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse global options */
    nmo_cli_global_opts_t global;
    int first_non_global = nmo_cli_parse_global_opts(argc, argv, &global);
    if (first_non_global < 0) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Handle version */
    if (global.show_version) {
        printf("nmo %d.%d.%d\n", NMO_VERSION_MAJOR, NMO_VERSION_MINOR, NMO_VERSION_PATCH);
        return NMO_CLI_EXIT_SUCCESS;
    }

    /* Handle help with no group */
    if (global.show_help && first_non_global >= argc) {
        nmo_cli_print_usage(stdout);
        return NMO_CLI_EXIT_SUCCESS;
    }

    /* Dispatch to group/action */
    int remaining_argc = argc - first_non_global;
    char **remaining_argv = argv + first_non_global;

    return nmo_cli_dispatch(remaining_argc, remaining_argv, &global);
}
