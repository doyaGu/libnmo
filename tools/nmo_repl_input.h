#ifndef NMO_REPL_INPUT_H
#define NMO_REPL_INPUT_H

#include "nmo_repl_types.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void nmo_repl_input_init(nmo_repl_context_t *repl);
void nmo_repl_input_cleanup(void);

#ifdef NMO_HAVE_ISOCLINE
#include <isocline.h>
#define nmo_repl_readline(prompt)   ic_readline(prompt)
#define nmo_repl_free_line(line)    ic_free(line)
#else
char *nmo_repl_readline_basic(const char *prompt);
#define nmo_repl_readline(prompt)   nmo_repl_readline_basic(prompt)
#define nmo_repl_free_line(line)    free(line)
#endif

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPL_INPUT_H */
