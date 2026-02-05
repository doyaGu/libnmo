#include "nmo_repl_commands.h"

#include "nmo_repl_util.h"

#include "nmo_repl_session.h"

#include "app/nmo_inspector.h"
#include "app/nmo_stats.h"

#include "nmo_cli_hex.h"
#include "nmo_cli_json.h"
#include "nmo_cli_output.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define NMO_REPL_MAX_SUGGESTIONS 5
#define NMO_REPL_MAX_SUGGEST_LEN 32

static int cmd_help(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_info(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_list(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_select(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_show(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_dump(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_find(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_trace(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_verify(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_stats(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_meta(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_export(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_set(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_open(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_reload(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_clear(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_quit(nmo_repl_context_t *repl, int argc, char **argv);

static const nmo_repl_command_t commands[] = {
    {"help", "h", "Show help for commands", "help [command]", cmd_help},
    {"info", "i", "Show loaded file summary", "info", cmd_info},
    {"list", "ls", "List objects (optional class filter)", "list [class_id|class:<id>|class:<name>|<name>] [limit]", cmd_list},
    {"select", "sel", "Select current object", "select <index|id:<id>|name:<substr>|name:/regex/>", cmd_select},
    {"show", "s", "Show object details", "show [selector]", cmd_show},
    {"dump", "d", "Dump chunk", "dump [selector] [level]", cmd_dump},
    {"find", "f", "Find objects by name/class/id/regex", "find <substr>|/<regex>/ | class <id|name> | id <id>", cmd_find},
    {"trace", "t", "Trace references (placeholder)", "trace <index>", cmd_trace},
    {"verify", "v", "Verify chunks", "verify [all|selector]", cmd_verify},
    {"stats", "st", "Show file statistics", "stats", cmd_stats},
    {"meta", "m", "Show chunk metadata", "meta [selector]", cmd_meta},
    {"export", "x", "Export chunk(s) to JSON", "export <path> [selector|all] [data]", cmd_export},
    {"set", "", "Set options", "set color on|off | set level 0-3 | set page <n> | set regex-icase on|off", cmd_set},
    {"open", "o", "Open a different file (reloads session)", "open <path>", cmd_open},
    {"reload", "r", "Reload the current file", "reload", cmd_reload},
    {"clear", "cls", "Clear the screen", "clear", cmd_clear},
    {"quit", "q", "Exit REPL", "quit", cmd_quit},
    {"exit", "", "Exit REPL", "exit", cmd_quit},
    {NULL, NULL, NULL, NULL, NULL}};

static void suggest_commands(const char *name) {
    if (!name || !*name) {
        return;
    }

    /* Small, allocation-free edit-distance matcher to catch common typos. */

    size_t name_len = strlen(name);
    if (name_len > NMO_REPL_MAX_SUGGEST_LEN) {
        return;
    }

    typedef struct {
        const char *cmd;
        int score;
    } suggestion_t;

    suggestion_t best[NMO_REPL_MAX_SUGGESTIONS];
    size_t best_count = 0;

    int prev[NMO_REPL_MAX_SUGGEST_LEN + 1];
    int curr[NMO_REPL_MAX_SUGGEST_LEN + 1];

    for (int i = 0; commands[i].name != NULL; i++) {
        const char *cmd_name = commands[i].name;
        if (!cmd_name) {
            continue;
        }

        size_t cmd_len = strlen(cmd_name);
        if (cmd_len > NMO_REPL_MAX_SUGGEST_LEN) {
            continue;
        }

        /* Initialize DP row. */
        for (size_t x = 0; x <= cmd_len; ++x) {
            prev[x] = (int)x;
        }

        for (size_t y = 1; y <= name_len; ++y) {
            curr[0] = (int)y;
            for (size_t x = 1; x <= cmd_len; ++x) {
                int cost = (name[y - 1] == cmd_name[x - 1]) ? 0 : 1;
                int del = prev[x] + 1;
                int ins = curr[x - 1] + 1;
                int sub = prev[x - 1] + cost;
                int v = del;
                if (ins < v) {
                    v = ins;
                }
                if (sub < v) {
                    v = sub;
                }
                curr[x] = v;
            }
            for (size_t x = 0; x <= cmd_len; ++x) {
                prev[x] = curr[x];
            }
        }

        int dist = prev[cmd_len];
        /* Also accept prefix matches as perfect suggestions. */
        if (strncmp(cmd_name, name, name_len) == 0) {
            dist = 0;
        }

        if (dist > 2) {
            continue;
        }

        if (best_count < NMO_REPL_MAX_SUGGESTIONS) {
            best[best_count++] = (suggestion_t){cmd_name, dist};
        } else {
            /* Replace the worst entry if this one is better. */
            size_t worst = 0;
            for (size_t k = 1; k < best_count; ++k) {
                if (best[k].score > best[worst].score) {
                    worst = k;
                }
            }
            if (dist < best[worst].score) {
                best[worst] = (suggestion_t){cmd_name, dist};
            }
        }
    }

    if (best_count == 0) {
        return;
    }

    /* Sort by score then name. */
    for (size_t i = 0; i < best_count; ++i) {
        for (size_t j = i + 1; j < best_count; ++j) {
            if (best[j].score < best[i].score ||
                (best[j].score == best[i].score && strcmp(best[j].cmd, best[i].cmd) < 0)) {
                suggestion_t tmp = best[i];
                best[i] = best[j];
                best[j] = tmp;
            }
        }
    }

    fprintf(stderr, "Did you mean: ");
    for (size_t i = 0; i < best_count; ++i) {
        fprintf(stderr, "%s%s", best[i].cmd, (i + 1 < best_count) ? ", " : "");
    }
    fprintf(stderr, "?\n");
}

static const nmo_repl_command_t *find_command(const char *name) {
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(commands[i].name, name) == 0 ||
            (commands[i].alias && commands[i].alias[0] && strcmp(commands[i].alias, name) == 0)) {
            return &commands[i];
        }
    }
    return NULL;
}

void nmo_repl_print_banner(const nmo_repl_context_t *repl) {
    const char *path = (repl && repl->filename) ? repl->filename : NULL;
    const char *label = path && *path ? path : "(no file)";

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    if (repl && repl->session) {
        nmo_session_get_objects(repl->session, &objects, &object_count);
    }

    printf("\nNMO REPL (nmo repl)\n");
    printf("File: %s\n", label);
    if (object_count) {
        printf("Objects: %zu\n", object_count);
    }
    printf("Tip: 'help' lists commands. Try: list, select 0, show, dump.\n");
}

int nmo_repl_dispatch_command(nmo_repl_context_t *repl, int argc, char **argv) {
    if (!repl || argc <= 0 || !argv || !argv[0]) {
        return -1;
    }

    const nmo_repl_command_t *cmd = find_command(argv[0]);
    if (cmd == NULL) {
        fprintf(stderr, "Unknown command: %s\n", argv[0]);
        suggest_commands(argv[0]);
        fprintf(stderr, "Type 'help' to see available commands.\n");
        return -1;
    }

    return cmd->handler(repl, argc, argv);
}

static int cmd_help(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)repl;

    if (argc > 1) {
        const nmo_repl_command_t *cmd = find_command(argv[1]);
        if (!cmd) {
            fprintf(stderr, "Unknown command: %s\n", argv[1]);
            return -1;
        }
        printf("\n%s\n", cmd->name);
        printf("  %s\n", cmd->help);
        if (cmd->usage) {
            printf("  usage: %s\n", cmd->usage);
        }
        if (cmd->alias && cmd->alias[0]) {
            printf("  alias: %s\n", cmd->alias);
        }
        printf("\n");
        return 0;
    }

    printf("\nQuick start:\n");
    printf("  list                  List objects\n");
    printf("  select 0              Select object by index\n");
    printf("  show                  Show selected object details\n");
    printf("  dump                  Dump selected object's chunk\n");
    printf("  find camera           Find objects by name substring\n");
    printf("  find /cam.*/          Find objects by regex (see 'set regex-icase on')\n");
    printf("  open other.cmo         Load a different file\n");
    printf("\nAvailable commands:\n");
    for (int i = 0; commands[i].name != NULL; i++) {
        printf("  %-12s", commands[i].name);
        if (commands[i].alias && commands[i].alias[0]) {
            printf("(%s)  ", commands[i].alias);
        } else {
            printf("     ");
        }
        printf("%s\n", commands[i].help);
        if (commands[i].usage) {
            printf("      usage: %s\n", commands[i].usage);
        }
    }
    printf("\n");
    return 0;
}

static int cmd_info(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!repl || !repl->session) {
        fprintf(stderr, "No session loaded.\n");
        return -1;
    }

    nmo_file_info_t info = nmo_session_get_file_info(repl->session);
    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_session_get_objects(repl->session, &objects, &object_count);

    printf("\nSession:\n");
    printf("  File: %s\n", repl->filename ? repl->filename : "(unknown)");
    printf("  Objects: %zu\n", object_count);
    printf("  Managers: %u\n", info.manager_count);
    printf("  CK version: %u\n", info.ck_version);
    printf("  File version: %u\n", info.file_version);
    if (repl->has_selection && repl->selected_index < object_count) {
        printf("  Selected: idx=%zu id=%u\n", repl->selected_index, nmo_object_get_id(objects[repl->selected_index]));
    } else {
        printf("  Selected: (none)\n");
    }
    printf("\n");
    return 0;
}

static int cmd_list(nmo_repl_context_t *repl, int argc, char **argv) {
    int filter = -1;
    size_t limit = 0;

    if (argc > 1) {
        const char *token = argv[1];
        if (strncmp(token, "class:", 6) == 0) {
            token = token + 6;
        }

        if (token && token[0] != '\0') {
            if (isdigit((unsigned char)token[0]) || token[0] == '-') {
                filter = atoi(token);
            } else {
                nmo_class_id_t class_id = 0;
                if (!nmo_repl_class_id_from_name(repl, token, &class_id)) {
                    fprintf(stderr, "Unknown class: %s\n", token);
                    fprintf(stderr, "Tip: use a numeric class_id or a known type name (e.g. CKCamera).\n");
                    return -1;
                }
                filter = (int)class_id;
            }
        }
    }

    if (argc > 2) {
        (void)nmo_repl_parse_size(argv[2], &limit);
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    printf("\nObjects:\n");
    size_t displayed = 0;

    for (size_t i = 0; i < object_count; i++) {
        nmo_class_id_t class_id = nmo_object_get_class_id(objects[i]);

        if (filter != -1 && class_id != (nmo_class_id_t)filter) {
            continue;
        }

        bool selected = repl->has_selection && repl->selected_index == i;
        nmo_repl_print_object_summary_marked(repl, i, objects[i], selected);

        displayed++;
        if (!nmo_repl_paginate_if_needed(repl, displayed)) {
            break;
        }
        if (limit > 0 && displayed >= limit) {
            break;
        }
    }

    if (filter != -1) {
        char class_buf[64];
        const char *class_name = nmo_repl_class_name_from_id(repl, (nmo_class_id_t)filter, class_buf, sizeof(class_buf));
        printf("\n%zu/%zu objects shown (class %d, %s)\n", displayed, object_count, filter, class_name);
    } else {
        printf("\n%zu/%zu objects shown\n", displayed, object_count);
    }
    if (!repl->has_selection && displayed > 0) {
        printf("Tip: use 'select <index>' then 'show'/'dump'.\n");
    }

    return 0;
}

static int cmd_select(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: select <index|id:<id>|name:<substr>|name:/regex/>\n");
        return -1;
    }

    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argv[1], &index, false) != 0) {
        return -1;
    }

    repl->selected_index = index;
    repl->has_selection = true;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    nmo_repl_get_objects(repl, &objects, &object_count);
    if (index < object_count) {
        printf("Selected object:\n");
        nmo_repl_print_object_summary(repl, index, objects[index]);
    }

    return 0;
}

static int cmd_show(nmo_repl_context_t *repl, int argc, char **argv) {
    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    if (index >= object_count) {
        fprintf(stderr, "Error: Index %zu out of range (0-%zu)\n", index, object_count ? object_count - 1 : 0);
        return -1;
    }

    nmo_object_t *obj = objects[index];
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);

    char class_buf[64];
    const char *class_name = nmo_repl_class_name_from_id(repl, class_id, class_buf, sizeof(class_buf));

    printf("\nObject Details:\n");
    printf("  Index: %zu\n", index);
    printf("  Class: %d (%s)\n", class_id, class_name);
    printf("  ID/Name: %u %s\n", obj_id, (name && name[0]) ? name : "(unnamed)");
    if (chunk) {
        printf("  Chunk Size: %zu bytes\n", nmo_chunk_get_data_size(chunk));
        char opt_buf[128];
        const char *opt = nmo_cli_chunk_options_to_string(chunk->chunk_options, opt_buf, sizeof(opt_buf));
        printf("  Chunk Options: %s (0x%08X)\n", opt, (unsigned int)chunk->chunk_options);
        printf("  Chunk Class: %u\n", chunk->class_id);
    } else {
        printf("  Chunk: (none)\n");
    }

    printf("\n");
    return 0;
}

static int cmd_dump(nmo_repl_context_t *repl, int argc, char **argv) {
    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    int level = (int)repl->dump_level;
    if (argc > 2) {
        level = atoi(argv[2]);
        if (level < 0) {
            level = 0;
        }
        if (level > 3) {
            level = 3;
        }
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    if (index >= object_count) {
        fprintf(stderr, "Error: Index %zu out of range (0-%zu)\n", index, object_count ? object_count - 1 : 0);
        return -1;
    }

    nmo_object_t *obj = objects[index];
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);

    if (!chunk) {
        fprintf(stderr, "Error: Object has no chunk\n");
        return -1;
    }

    printf("\nChunk Dump (level %d):\n", level);
    nmo_inspector_options_t options;
    nmo_inspector_init_options(&options);
    options.level = (nmo_dump_level_t)level;
    options.show_hex = (level >= NMO_DUMP_FULL);
    options.show_sub_chunks = true;
    options.colorize = repl->colorize;
    nmo_inspector_dump_chunk(chunk, stdout, &options);
    printf("\n");
    return 0;
}

static int cmd_find(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: find <substr>|/<regex>/ | class <id|name> | id <id>\n");
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    const char *token = argv[1];
    bool use_regex = false;
    const char *pattern = NULL;
    const char *name_substr = NULL;
    int class_filter = -1;
    uint32_t id_filter = 0;
    bool use_id = false;

    if (strcmp(token, "class") == 0 && argc > 2) {
        const char *arg = argv[2];
        if (isdigit((unsigned char)arg[0]) || arg[0] == '-') {
            class_filter = atoi(arg);
        } else {
            nmo_class_id_t class_id = 0;
            if (!nmo_repl_class_id_from_name(repl, arg, &class_id)) {
                fprintf(stderr, "Unknown class: %s\n", arg);
                return -1;
            }
            class_filter = (int)class_id;
        }
    } else if (strcmp(token, "id") == 0 && argc > 2) {
        use_id = nmo_repl_parse_u32(argv[2], &id_filter);
    } else {
        size_t len = strlen(token);
        if (len >= 2 && token[0] == '/' && token[len - 1] == '/') {
            use_regex = true;
            pattern = token + 1;
            len -= 2;
            if (len >= 256) {
                len = 255;
            }
            static char buf[256];
            memcpy(buf, token + 1, len);
            buf[len] = '\0';
            pattern = buf;
        } else {
            name_substr = token;
        }
    }

    size_t found = 0;
    printf("\nMatches:\n");

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];

        if (class_filter != -1) {
            if ((int)nmo_object_get_class_id(obj) != class_filter) {
                continue;
            }
        }

        if (use_id) {
            if (nmo_object_get_id(obj) != id_filter) {
                continue;
            }
        }

        if (use_regex) {
            const char *name = nmo_object_get_name(obj);
            if (!name || !nmo_repl_regex_matches(name, pattern, repl->regex_icase)) {
                continue;
            }
        } else if (name_substr) {
            const char *name = nmo_object_get_name(obj);
            if (!name || !strstr(name, name_substr)) {
                continue;
            }
        }

        nmo_repl_print_object_summary(repl, i, obj);
        found++;
        if (!nmo_repl_paginate_if_needed(repl, found)) {
            break;
        }
    }

    if (!found) {
        printf("No matches.\n");
    }
    printf("\n");
    return 0;
}

static int cmd_trace(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)repl;
    (void)argc;
    (void)argv;
    fprintf(stderr, "Trace not implemented yet.\n");
    return -1;
}

static int cmd_verify(nmo_repl_context_t *repl, int argc, char **argv) {
    size_t index = 0;
    bool verify_all = false;

    if (argc > 1 && strcmp(argv[1], "all") == 0) {
        verify_all = true;
    }

    if (!verify_all) {
        if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
            return -1;
        }
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    size_t errors = 0;
    size_t checked = 0;
    printf("\nVerifying chunks...\n");

    for (size_t i = 0; i < object_count; ++i) {
        if (!verify_all && i != index) {
            continue;
        }

        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (!chunk) {
            continue;
        }

        nmo_chunk_validation_t result;
        if (nmo_inspector_validate_chunk(chunk, &result) != 0 || !result.is_valid) {
            errors++;
                 printf("  [%zu] ID=%u Class=%d: verify result %zu\n",
                   i,
                   nmo_object_get_id(obj),
                   nmo_object_get_class_id(obj),
                     result.error_count);
        }

        checked++;
    }

    if (errors == 0) {
        printf("All %zu chunk(s) verified successfully.\n", checked);
    } else {
        printf("%zu/%zu chunk(s) failed verification.\n", errors, checked);
    }
    printf("\n");
    return (errors == 0) ? 0 : -1;
}

static int cmd_stats(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!repl || !repl->session) {
        fprintf(stderr, "No session loaded.\n");
        return -1;
    }

    nmo_finish_loading_stats_t stats;
    if (nmo_session_get_finish_loading_stats(repl->session, &stats) != NMO_OK) {
        fprintf(stderr, "Finish loading stats unavailable.\n");
        return -1;
    }

    printf("\nLoad Phases:\n");
    printf("  Total Objects: %zu\n", stats.total_objects);
    printf("  References: total=%u resolved=%u unresolved=%u ambiguous=%u\n",
           stats.references.total,
           stats.references.resolved,
           stats.references.unresolved,
           stats.references.ambiguous);
    printf("  Indexes: classes=%zu names=%zu guids=%zu memory=%zu bytes\n",
           stats.indexes.class_entries,
           stats.indexes.name_entries,
           stats.indexes.guid_entries,
           stats.indexes.memory_usage);
    printf("  Manager Errors: %u\n", stats.manager_errors);
    printf("\n");
    return 0;
}

static int cmd_meta(nmo_repl_context_t *repl, int argc, char **argv) {
    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    if (index >= object_count) {
        fprintf(stderr, "Error: Index %zu out of range (0-%zu)\n", index, object_count ? object_count - 1 : 0);
        return -1;
    }

    nmo_object_t *obj = objects[index];
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) {
        fprintf(stderr, "Error: Object has no chunk\n");
        return -1;
    }

    printf("\nChunk Metadata:\n");
    printf("  Object ID: %u\n", nmo_object_get_id(obj));
    printf("  Class ID: %d\n", nmo_object_get_class_id(obj));
    printf("  Chunk Class: %u\n", chunk->class_id);
    printf("  Data Size: %zu bytes\n", nmo_chunk_get_data_size(chunk));
    printf("  Compressed: %s\n",
           ((chunk->chunk_options & NMO_CHUNK_OPTION_PACKED) || chunk->is_compressed) ? "yes" : "no");
    {
        char opt_buf[128];
        const char *opt = nmo_cli_chunk_options_to_string(chunk->chunk_options, opt_buf, sizeof(opt_buf));
        printf("  Options: %s (0x%08X)\n", opt, (unsigned int)chunk->chunk_options);
    }
    printf("\n");
    return 0;
}

static int cmd_export(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: export <path> [selector|all] [data]\n");
        return -1;
    }

    const char *path = argv[1];
    const char *selector = NULL;
    bool include_data = false;
    size_t max_hex_bytes = 256;

    if (argc > 2) {
        selector = argv[2];
    }
    if (argc > 3 && strcmp(argv[3], "data") == 0) {
        include_data = true;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    bool export_all = false;
    size_t single_index = 0;

    if (!selector || strcmp(selector, "all") == 0) {
        export_all = true;
    } else {
        if (nmo_repl_resolve_object_index(repl, selector, &single_index, true) != 0) {
            return -1;
        }
    }

    yyjson_mut_doc *doc = nmo_cli_json_create_doc();
    if (!doc) {
        fprintf(stderr, "Out of memory\n");
        return -1;
    }

    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "objects", arr);

    for (size_t i = 0; i < object_count; ++i) {
        if (!export_all && i != single_index) {
            continue;
        }

        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);

        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_arr_add_val(arr, entry);

        yyjson_mut_obj_add_uint(doc, entry, "index", (uint64_t)i);
        yyjson_mut_obj_add_uint(doc, entry, "id", (uint64_t)nmo_object_get_id(obj));
        yyjson_mut_obj_add_int(doc, entry, "class_id", (int64_t)nmo_object_get_class_id(obj));
        (void)nmo_cli_json_add_str_safe(doc, entry, "name", nmo_object_get_name(obj));
        yyjson_mut_obj_add_uint(doc, entry, "chunk_size",
                                (uint64_t)(chunk ? nmo_chunk_get_data_size(chunk) : 0u));

        if (include_data && chunk) {
            size_t data_size = 0;
            const void *data = nmo_chunk_get_data(chunk, &data_size);
            (void)nmo_cli_json_add_data_hex(doc, entry, data, data_size, max_hex_bytes, false);
        }
    }

    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "Failed to open '%s' for writing\n", path);
        nmo_cli_json_free_doc(doc);
        return -1;
    }

    (void)nmo_cli_json_write(doc, out, true);
    fclose(out);
    nmo_cli_json_free_doc(doc);
    printf("Exported %s\n", path);
    return 0;
}

static int cmd_set(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: set color on|off | set level 0-3 | set page <n> | set regex-icase on|off\n");
        return -1;
    }

    if (strcmp(argv[1], "color") == 0) {
        if (strcmp(argv[2], "on") == 0) {
            repl->colorize = true;
        } else if (strcmp(argv[2], "off") == 0) {
            repl->colorize = false;
        } else {
            fprintf(stderr, "Usage: set color on|off\n");
            return -1;
        }
        printf("Colorization %s\n", repl->colorize ? "enabled" : "disabled");
        return 0;
    }

    if (strcmp(argv[1], "level") == 0) {
        int level = atoi(argv[2]);
        if (level < 0 || level > 3) {
            fprintf(stderr, "Usage: set level 0-3\n");
            return -1;
        }
        repl->dump_level = (nmo_dump_level_t)level;
        printf("Dump level set to %d\n", level);
        return 0;
    }

    if (strcmp(argv[1], "page") == 0) {
        size_t page = 0;
        if (!nmo_repl_parse_size(argv[2], &page) || page == 0) {
            fprintf(stderr, "Usage: set page <n>\n");
            return -1;
        }
        repl->page_size = page;
        printf("Page size set to %zu\n", page);
        return 0;
    }

    if (strcmp(argv[1], "regex-icase") == 0) {
        if (strcmp(argv[2], "on") == 0) {
            repl->regex_icase = true;
        } else if (strcmp(argv[2], "off") == 0) {
            repl->regex_icase = false;
        } else {
            fprintf(stderr, "Usage: set regex-icase on|off\n");
            return -1;
        }
        printf("Regex case-insensitive %s\n", repl->regex_icase ? "enabled" : "disabled");
        return 0;
    }

    fprintf(stderr, "Unknown option: %s\n", argv[1]);
    return -1;
}

static int cmd_open(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: open <path>\n");
        return -1;
    }

    char errbuf[128];
    if (!nmo_repl_load_file(repl, argv[1], errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return -1;
    }

    printf("Loaded %s\n", repl->filename);
    return 0;
}

static int cmd_reload(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!repl || !repl->filename) {
        fprintf(stderr, "No file loaded\n");
        return -1;
    }

    char errbuf[128];
    if (!nmo_repl_load_file(repl, repl->filename, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return -1;
    }

    printf("Reloaded %s\n", repl->filename);
    return 0;
}

static int cmd_clear(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)repl;
    (void)argc;
    (void)argv;
#if defined(_WIN32)
    system("cls");
#else
    system("clear");
#endif
    return 0;
}

static int cmd_quit(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)repl;
    (void)argc;
    (void)argv;
    return 1;
}
