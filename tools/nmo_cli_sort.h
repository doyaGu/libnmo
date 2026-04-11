/**
 * @file nmo_cli_sort.h
 * @brief Shared sort key parsing for CLI list commands
 */

#ifndef NMO_CLI_SORT_H
#define NMO_CLI_SORT_H

typedef enum nmo_cli_sort_key {
    NMO_CLI_SORT_NONE = 0,
    NMO_CLI_SORT_ID,
    NMO_CLI_SORT_NAME,
    NMO_CLI_SORT_CLASS,
    NMO_CLI_SORT_SIZE,
    NMO_CLI_SORT_COUNT,
} nmo_cli_sort_key_t;

/* Parse sort key from string. Returns NMO_CLI_SORT_NONE on unrecognized input. */
nmo_cli_sort_key_t nmo_cli_parse_sort_key(const char *str);

#endif /* NMO_CLI_SORT_H */
