#include "nmo_repl_input.h"

#include <stdio.h>
#include <string.h>

#ifdef NMO_HAVE_ISOCLINE

#include "nmo_repl_commands.h"

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

static void complete_command_names(ic_completion_env_t *cenv, const char *word_prefix) {
    const char **names = nmo_repl_get_command_names();
    ic_add_completions(cenv, word_prefix, names);
}

static void nmo_repl_completer(ic_completion_env_t *cenv, const char *prefix) {
    /* prefix = full input up to cursor position.
     * Check if we're still on the first word (command name). */
    const char *p = prefix;
    while (*p && *p == ' ') {
        p++;
    }

    /* Find end of first word */
    const char *word_end = p;
    while (*word_end && *word_end != ' ') {
        word_end++;
    }

    /* If there's no space after the first word, we're completing a command name */
    if (*word_end == '\0') {
        ic_complete_word(cenv, prefix, &complete_command_names, NULL);
    }
}

void nmo_repl_input_init(nmo_repl_context_t *repl) {
    const char *hist = nmo_repl_get_history_path();
    if (hist) {
        ic_set_history(hist, 500);
    }

    ic_enable_history_duplicates(false);
    ic_enable_multiline(false);
    ic_enable_brace_matching(true);
    ic_enable_brace_insertion(false);
    ic_set_prompt_marker("", NULL);
    ic_set_default_completer(&nmo_repl_completer, (void *)repl);
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
