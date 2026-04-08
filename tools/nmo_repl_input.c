#include "nmo_repl_input.h"

#include <stdio.h>
#include <string.h>

static const char *nmo_repl_get_history_path(void) {
    static char path[512];

#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home || !home[0]) {
        return NULL;
    }

    snprintf(path, sizeof(path), "%s/.nmo_history", home);
    return path;
}

#ifdef NMO_HAVE_ISOCLINE

void nmo_repl_input_init(nmo_repl_context_t *repl) {
    (void)repl;

    const char *hist = nmo_repl_get_history_path();
    if (hist) {
        ic_set_history(hist, 500);
    }

    ic_enable_history_duplicates(false);
    ic_enable_multiline(false);
    ic_enable_brace_matching(true);
    ic_enable_brace_insertion(false);
    ic_set_prompt_marker("", NULL);
}

void nmo_repl_input_cleanup(void) {
    /* isocline auto-saves history on exit */
}

#else /* !NMO_HAVE_ISOCLINE */

void nmo_repl_input_init(nmo_repl_context_t *repl) {
    (void)repl;
}

void nmo_repl_input_cleanup(void) {
}

char *nmo_repl_readline_basic(const char *prompt) {
    static char buf[4096];

    if (prompt && prompt[0]) {
        printf("%s", prompt);
        fflush(stdout);
    }

    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return NULL;
    }

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        len--;
    }

    char *result = (char *)malloc(len + 1);
    if (result) {
        memcpy(result, buf, len + 1);
    }
    return result;
}

#endif /* NMO_HAVE_ISOCLINE */
