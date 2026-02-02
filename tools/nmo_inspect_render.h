#ifndef NMO_INSPECT_RENDER_H
#define NMO_INSPECT_RENDER_H

#include "nmo_inspect_types.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void nmo_inspect_render_report(FILE *out,
                              const inspect_state_t *state,
                              const inspect_options_t *opts,
                              const warning_list_t *warnings);

#ifdef __cplusplus
}
#endif

#endif /* NMO_INSPECT_RENDER_H */
