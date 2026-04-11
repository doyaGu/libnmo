/**
 * @file nmo_cli_sort.c
 * @brief Shared sort key parsing for CLI list commands
 */

#include "nmo_cli_sort.h"

#include <string.h>

nmo_cli_sort_key_t nmo_cli_parse_sort_key(const char *str) {
    if (!str) return NMO_CLI_SORT_NONE;
    if (strcmp(str, "id") == 0)    return NMO_CLI_SORT_ID;
    if (strcmp(str, "name") == 0)  return NMO_CLI_SORT_NAME;
    if (strcmp(str, "class") == 0) return NMO_CLI_SORT_CLASS;
    if (strcmp(str, "size") == 0)  return NMO_CLI_SORT_SIZE;
    if (strcmp(str, "count") == 0) return NMO_CLI_SORT_COUNT;
    return NMO_CLI_SORT_NONE;
}
