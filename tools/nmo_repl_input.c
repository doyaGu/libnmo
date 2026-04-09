#include "nmo_repl_input.h"

#include <stdio.h>
#include <string.h>

#ifdef NMO_HAVE_ISOCLINE

#include "nmo_repl_commands.h"
#include "app/nmo_session.h"
#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"

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

/* DSL keywords for eval/query completion */
static const char *dsl_keywords[] = {
    "true", "false", "null",
    "id", "name", "class", "size", "cid",
    "schema", "enum", "flags", "struct", "alias",
    NULL
};

/* File-scoped pointer set before inner completion callback invocation */
static nmo_repl_context_t *s_repl_for_completion;

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

static void complete_dsl_keywords(ic_completion_env_t *cenv, const char *prefix) {
    ic_add_completions(cenv, prefix, dsl_keywords);
}

/* ---- Object name completion cache ---- */

static int name_cmp_icase(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
#ifdef _WIN32
    return _stricmp(sa, sb);
#else
    return strcasecmp(sa, sb);
#endif
}

/**
 * Build (or rebuild) the sorted, deduplicated name cache from the session.
 */
static void rebuild_name_cache(nmo_repl_context_t *repl) {
    /* Destroy old cache */
    if (repl->name_cache_arena) {
        nmo_arena_destroy(repl->name_cache_arena);
        repl->name_cache_arena = NULL;
    }
    repl->name_cache = NULL;
    repl->name_cache_count = 0;
    repl->name_cache_dirty = false;

    if (!repl->session) {
        return;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(repl->session);
    if (!repo) {
        return;
    }

    size_t obj_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &obj_count);
    if (!objects || obj_count == 0) {
        return;
    }

    /* Create arena sized for names */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        return;
    }

    /* First pass: count non-empty names */
    size_t name_count = 0;
    for (size_t i = 0; i < obj_count; i++) {
        const char *name = nmo_object_get_name(objects[i]);
        if (name && name[0]) {
            name_count++;
        }
    }

    if (name_count == 0) {
        nmo_arena_destroy(arena);
        return;
    }

    /* Allocate pointer array in arena */
    const char **names = (const char **)nmo_arena_alloc(
        arena, name_count * sizeof(const char *), sizeof(void *));
    if (!names) {
        nmo_arena_destroy(arena);
        return;
    }

    /* Second pass: deep-copy names into arena */
    size_t idx = 0;
    for (size_t i = 0; i < obj_count && idx < name_count; i++) {
        const char *name = nmo_object_get_name(objects[i]);
        if (name && name[0]) {
            const char *dup = nmo_arena_strdup(arena, name);
            if (dup) {
                names[idx++] = dup;
            }
        }
    }
    name_count = idx;

    /* Sort case-insensitively */
    qsort(names, name_count, sizeof(const char *), name_cmp_icase);

    /* Deduplicate in-place */
    size_t unique = 0;
    for (size_t i = 0; i < name_count; i++) {
        if (unique == 0 || name_cmp_icase(&names[i], &names[unique - 1]) != 0) {
            names[unique++] = names[i];
        }
    }

    repl->name_cache_arena = arena;
    repl->name_cache = names;
    repl->name_cache_count = unique;
}

static void complete_object_names(ic_completion_env_t *cenv, const char *prefix) {
    nmo_repl_context_t *repl = s_repl_for_completion;
    if (!repl) {
        return;
    }

    /* Build cache on first use or after invalidation */
    if (!repl->name_cache || repl->name_cache_dirty) {
        rebuild_name_cache(repl);
    }

    if (!repl->name_cache || repl->name_cache_count == 0) {
        return;
    }

    size_t prefix_len = prefix ? strlen(prefix) : 0;
    size_t added = 0;
    static const size_t MAX_COMPLETIONS = 100;

    for (size_t i = 0; i < repl->name_cache_count && added < MAX_COMPLETIONS; i++) {
        const char *name = repl->name_cache[i];
        if (prefix_len == 0) {
            ic_add_completion(cenv, name);
            added++;
        } else {
#ifdef _WIN32
            if (_strnicmp(name, prefix, prefix_len) == 0) {
#else
            if (strncasecmp(name, prefix, prefix_len) == 0) {
#endif
                ic_add_completion(cenv, name);
                added++;
            }
        }
    }
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

    /* eval/query <TAB> -> DSL keywords */
    if (strcmp(cmd, "eval") == 0 || strcmp(cmd, "e") == 0 ||
        strcmp(cmd, "query") == 0) {
        ic_complete_word(cenv, prefix, &complete_dsl_keywords, NULL);
        return;
    }

    /* show/trace/refs/param/dump/select <TAB> -> object names */
    if (strcmp(cmd, "show") == 0 || strcmp(cmd, "s") == 0 ||
        strcmp(cmd, "trace") == 0 || strcmp(cmd, "t") == 0 ||
        strcmp(cmd, "refs") == 0 ||
        strcmp(cmd, "param") == 0 || strcmp(cmd, "p") == 0 ||
        strcmp(cmd, "dump") == 0 || strcmp(cmd, "d") == 0 ||
        strcmp(cmd, "select") == 0 || strcmp(cmd, "sel") == 0) {
        if (words == 2) {
            s_repl_for_completion = (nmo_repl_context_t *)ic_completion_arg(cenv);
            ic_complete_qword(cenv, prefix, &complete_object_names, NULL);
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

void nmo_repl_input_cleanup(nmo_repl_context_t *repl) {
    /* Destroy object name cache */
    if (repl && repl->name_cache_arena) {
        nmo_arena_destroy(repl->name_cache_arena);
        repl->name_cache_arena = NULL;
        repl->name_cache = NULL;
        repl->name_cache_count = 0;
    }
    s_repl_for_completion = NULL;
    /* isocline auto-saves history on exit */
}

void nmo_repl_input_invalidate_name_cache(nmo_repl_context_t *repl) {
    if (repl) {
        repl->name_cache_dirty = true;
    }
}

#else /* !NMO_HAVE_ISOCLINE */

void nmo_repl_input_init(nmo_repl_context_t *repl) {
    (void)repl;
}

void nmo_repl_input_cleanup(nmo_repl_context_t *repl) {
    (void)repl;
}

void nmo_repl_input_invalidate_name_cache(nmo_repl_context_t *repl) {
    (void)repl;
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
