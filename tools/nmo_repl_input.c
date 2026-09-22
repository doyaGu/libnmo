#include "nmo_repl_input.h"

#include "nmo_tool_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

#ifdef NMO_HAVE_ISOCLINE

#include "nmo_cmd_core.h"
#include "nmo_cmd_ctx.h"
#include "nmo_repl_commands.h"
#include "core/nmo_arena.h"
#include "format/nmo_object.h"

static const char *nmo_repl_get_history_path(void) {
    /* Resolved once; isocline keeps referencing it for the whole session. */
    static char *path = NULL;
    if (path) {
        return path;
    }

#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home || !home[0]) {
        return NULL;
    }

    path = nmo_tool_strdup_fmt("%s/.nmo_history", home);
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

/* File-scoped pointer set before inner completion callback invocation */
static nmo_repl_context_t *s_repl_for_completion;

static const char *set_options[] = {
    "color", "level", "page", "regex-icase", NULL
};

static const char *set_color_values[] = { "on", "off", NULL };
static const char *set_level_values[] = { "0", "1", "2", "3", NULL };

/* malloc'd copy of `len` bytes starting at `start`. */
static char *dup_word(const char *start, size_t len) {
    char *word = (char *)malloc(len + 1u);
    if (word) {
        memcpy(word, start, len);
        word[len] = '\0';
    }
    return word;
}

/**
 * Parse prefix to extract command name and determine argument position.
 * Returns number of words found (0 = no command yet, -1 on allocation
 * failure). *cmd_out and *arg1_out receive malloc'd copies (NULL when the
 * word is absent) that the caller frees.
 */
static int parse_prefix_words(const char *prefix, char **cmd_out, char **arg1_out) {
    const char *p = prefix;
    int word_count = 0;

    *cmd_out = NULL;
    *arg1_out = NULL;

    /* Skip leading whitespace */
    while (*p == ' ') p++;
    if (*p == '\0') return 0;

    /* Extract first word (command) */
    const char *word_start = p;
    while (*p && *p != ' ') p++;
    size_t len = (size_t)(p - word_start);
    *cmd_out = dup_word(word_start, len);
    if (!*cmd_out) return -1;
    word_count = 1;

    /* Skip whitespace after command */
    while (*p == ' ') p++;
    if (*p == '\0') {
        /* Cursor is after command + space: completing first argument */
        if (p > word_start + len) {
            return 2; /* signal: completing arg1 */
        }
        return 1;
    }

    /* Extract second word (first argument) */
    word_start = p;
    while (*p && *p != ' ') p++;
    len = (size_t)(p - word_start);
    *arg1_out = dup_word(word_start, len);
    if (!*arg1_out) return -1;
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

typedef struct name_cache_build_ctx {
    nmo_arena_t *arena;
    const char **names;
    size_t count;
    size_t capacity;
} name_cache_build_ctx_t;

static int collect_object_name_for_completion(
    size_t index,
    nmo_object_t *obj,
    const nmo_cmd_ctx_t *c,
    void *user)
{
    (void)index;
    (void)c;
    name_cache_build_ctx_t *build = (name_cache_build_ctx_t *)user;
    const char *name = obj ? nmo_object_get_name(obj) : NULL;
    if (build == NULL || name == NULL || name[0] == '\0' ||
        build->count >= build->capacity) {
        return 0;
    }

    const char *dup = nmo_arena_strdup(build->arena, name);
    if (dup != NULL) {
        build->names[build->count++] = dup;
    }
    return 0;
}

/**
 * Build (or rebuild) the sorted, deduplicated name cache from the document.
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

    if (!repl->document || !repl->workspace) {
        return;
    }

    nmo_cmd_ctx_t cmd;
    nmo_cmd_ctx_init_from_repl_document(
        &cmd, repl->ctx, repl->document, repl->workspace, false);

    size_t obj_count = 0;
    if (nmo_core_object_count(&cmd, &obj_count) != NMO_CLI_EXIT_SUCCESS ||
        obj_count == 0) {
        return;
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        return;
    }

    const char **names = (const char **)nmo_arena_alloc(
        arena, obj_count * sizeof(const char *), sizeof(void *));
    if (!names) {
        nmo_arena_destroy(arena);
        return;
    }

    name_cache_build_ctx_t build = {
        .arena = arena,
        .names = names,
        .capacity = obj_count
    };
    if (nmo_core_object_query_run(
            &cmd, NULL, collect_object_name_for_completion, &build, NULL) !=
        NMO_CLI_EXIT_SUCCESS ||
        build.count == 0) {
        nmo_arena_destroy(arena);
        return;
    }

    /* Sort case-insensitively */
    size_t name_count = build.count;
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

/* Argument completion once the command word (`cmd`) is complete. */
static void nmo_repl_complete_arguments(ic_completion_env_t *cenv, const char *prefix,
                                        const char *cmd, const char *arg1, int words) {
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
    char *cmd = NULL;
    char *arg1 = NULL;
    int words = parse_prefix_words(prefix, &cmd, &arg1);
    if (words > 0) {
        nmo_repl_complete_arguments(cenv, prefix, cmd ? cmd : "", arg1 ? arg1 : "", words);
    }
    free(cmd);
    free(arg1);
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
    if (prompt && prompt[0]) {
        printf("%s", prompt);
        fflush(stdout);
    }

    /* Read up to the newline into a buffer that grows with the line. */
    size_t capacity = 256u;
    size_t length = 0u;
    char *line = (char *)malloc(capacity);
    if (!line) {
        return NULL;
    }
    for (;;) {
        int ch = fgetc(stdin);
        if (ch == EOF) {
            if (length == 0u) {
                free(line);
                return NULL; /* EOF with no input, like fgets */
            }
            break;
        }
        if (ch == '\n') {
            break;
        }
        if (length + 2u > capacity) {
            capacity *= 2u;
            char *grown = (char *)realloc(line, capacity);
            if (!grown) {
                free(line);
                return NULL;
            }
            line = grown;
        }
        line[length++] = (char)ch;
    }
    line[length] = '\0';
    return line;
}

#endif /* NMO_HAVE_ISOCLINE */
