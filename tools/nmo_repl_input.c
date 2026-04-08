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

/* Common CK class names for list/find completion */
static const char *ck_class_names[] = {
    "CK3dEntity", "CK3dObject", "CKCamera", "CKTargetCamera",
    "CKLight", "CKTargetLight", "CKCharacter", "CKGroup",
    "CKMesh", "CKMaterial", "CKTexture", "CKSprite", "CKSpriteText",
    "CKSound", "CKWaveSound", "CKMidiSound",
    "CKBehavior", "CKBehaviorIO", "CKBehaviorLink",
    "CKParameter", "CKParameterLocal", "CKParameterOut",
    "CKParameterOperation",
    "CKScene", "CKLevel", "CKPlace", "CKCurve",
    "CKRenderContext", "CKLayer", "CKGrid",
    "CKDataArray", "CKBodyPart",
    NULL
};

static const char *set_options[] = {
    "color", "level", "page", "regex-icase", NULL
};

static const char *set_color_values[] = { "on", "off", NULL };
static const char *set_level_values[] = { "0", "1", "2", "3", NULL };

/**
 * Parse prefix to extract command name and determine argument position.
 * Returns number of words found (0 = no command yet).
 */
static int parse_prefix_words(const char *prefix, char *cmd_buf, size_t cmd_size,
                              char *arg1_buf, size_t arg1_size) {
    const char *p = prefix;
    int word_count = 0;

    /* Skip leading whitespace */
    while (*p == ' ') p++;
    if (*p == '\0') return 0;

    /* Extract first word (command) */
    const char *word_start = p;
    while (*p && *p != ' ') p++;
    size_t len = (size_t)(p - word_start);
    if (len >= cmd_size) len = cmd_size - 1;
    memcpy(cmd_buf, word_start, len);
    cmd_buf[len] = '\0';
    word_count = 1;

    /* Skip whitespace after command */
    while (*p == ' ') p++;
    if (*p == '\0') {
        /* Cursor is after command + space: completing first argument */
        if (p > word_start + len) {
            arg1_buf[0] = '\0';
            return 2; /* signal: completing arg1 */
        }
        return 1;
    }

    /* Extract second word (first argument) */
    word_start = p;
    while (*p && *p != ' ') p++;
    len = (size_t)(p - word_start);
    if (len >= arg1_size) len = arg1_size - 1;
    memcpy(arg1_buf, word_start, len);
    arg1_buf[len] = '\0';
    word_count = 2;

    /* Check if there's more after arg1 */
    while (*p == ' ') p++;
    if (*p != '\0' || (p > word_start + len && *(p - 1) == ' ')) {
        return 3; /* completing arg2 or later */
    }

    return word_count;
}

static void complete_set_args(ic_completion_env_t *cenv, const char *word_prefix) {
    ic_add_completions(cenv, word_prefix, set_options);
}

static void complete_set_color(ic_completion_env_t *cenv, const char *word_prefix) {
    ic_add_completions(cenv, word_prefix, set_color_values);
}

static void complete_set_level(ic_completion_env_t *cenv, const char *word_prefix) {
    ic_add_completions(cenv, word_prefix, set_level_values);
}

static void complete_class_names(ic_completion_env_t *cenv, const char *word_prefix) {
    ic_add_completions(cenv, word_prefix, ck_class_names);
}

static void nmo_repl_completer(ic_completion_env_t *cenv, const char *prefix) {
    /* prefix = full input up to cursor position */
    const char *p = prefix;
    while (*p && *p == ' ') p++;

    /* Find end of first word */
    const char *word_end = p;
    while (*word_end && *word_end != ' ') word_end++;

    /* If still on first word, complete command names */
    if (*word_end == '\0') {
        ic_complete_word(cenv, prefix, &complete_command_names, NULL);
        return;
    }

    /* We have at least one complete word followed by space.
     * Parse the prefix to determine context. */
    char cmd[64] = {0};
    char arg1[256] = {0};
    int words = parse_prefix_words(prefix, cmd, sizeof(cmd), arg1, sizeof(arg1));

    if (words < 2) {
        return;
    }

    /* help <TAB> -> command names */
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
        if (words == 2) {
            ic_complete_word(cenv, prefix, &complete_command_names, NULL);
        }
        return;
    }

    /* set <TAB> -> option names */
    if (strcmp(cmd, "set") == 0) {
        if (words == 2) {
            ic_complete_word(cenv, prefix, &complete_set_args, NULL);
        } else if (words == 3) {
            /* set color <TAB> or set level <TAB> */
            if (strcmp(arg1, "color") == 0) {
                ic_complete_word(cenv, prefix, &complete_set_color, NULL);
            } else if (strcmp(arg1, "level") == 0) {
                ic_complete_word(cenv, prefix, &complete_set_level, NULL);
            } else if (strcmp(arg1, "regex-icase") == 0) {
                ic_complete_word(cenv, prefix, &complete_set_color, NULL);
            }
        }
        return;
    }

    /* open/save/export <TAB> -> filename completion */
    if (strcmp(cmd, "open") == 0 || strcmp(cmd, "o") == 0 ||
        strcmp(cmd, "save") == 0 ||
        strcmp(cmd, "export") == 0 || strcmp(cmd, "x") == 0) {
        if (words == 2) {
            ic_complete_filename(cenv, prefix, '/', NULL, ".nmo;.cmo;.vmo");
        }
        return;
    }

    /* list/ls <TAB> -> class names */
    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0) {
        if (words == 2) {
            ic_complete_word(cenv, prefix, &complete_class_names, NULL);
        }
        return;
    }

    /* find class <TAB> -> class names */
    if (strcmp(cmd, "find") == 0 || strcmp(cmd, "f") == 0) {
        if (words == 2 && strcmp(arg1, "class") == 0) {
            /* Will need arg2 completion */
        } else if (words == 3 && strcmp(arg1, "class") == 0) {
            ic_complete_word(cenv, prefix, &complete_class_names, NULL);
        }
        return;
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
