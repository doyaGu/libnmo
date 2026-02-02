#include "nmo_inspect_render.h"

void nmo_inspect_render_text(FILE *out,
                            const inspect_state_t *state,
                            const inspect_options_t *opts,
                            const warning_list_t *warnings);

void nmo_inspect_render_machine(FILE *out,
                               const inspect_state_t *state,
                               const inspect_options_t *opts,
                               const warning_list_t *warnings);

void nmo_inspect_render_report(FILE *out,
                              const inspect_state_t *state,
                              const inspect_options_t *opts,
                              const warning_list_t *warnings) {
    if (!out || !state || !opts || !warnings) {
        return;
    }

    if (opts->format == INSPECT_FORMAT_TEXT) {
        nmo_inspect_render_text(out, state, opts, warnings);
    } else {
        nmo_inspect_render_machine(out, state, opts, warnings);
    }
}
