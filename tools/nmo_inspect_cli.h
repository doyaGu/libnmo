#ifndef NMO_INSPECT_CLI_H
#define NMO_INSPECT_CLI_H

#include "nmo_inspect_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void nmo_inspect_filters_init(inspect_filters_t *filters);
void nmo_inspect_filters_free(inspect_filters_t *filters);

void nmo_inspect_options_init(inspect_options_t *opts);
void nmo_inspect_options_free(inspect_options_t *opts);

void nmo_inspect_print_usage(void);

int nmo_inspect_parse_args(int argc, char **argv, inspect_options_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INSPECT_CLI_H */
