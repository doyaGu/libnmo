#include "nmo_debug_commands.h"

#include "nmo_debug_util.h"

#include "nmo_debug_session.h"

#include "app/nmo_inspector.h"
#include "app/nmo_stats.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmd_help(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_info(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_list(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_select(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_show(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_dump(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_find(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_trace(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_verify(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_stats(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_meta(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_export(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_set(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_open(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_reload(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_clear(nmo_debug_context_t *dbg, int argc, char **argv);
static int cmd_quit(nmo_debug_context_t *dbg, int argc, char **argv);

static const nmo_debug_command_t commands[] = {
    {"help", "h", "Show help for commands", "help [command]", cmd_help},
    {"info", "i", "Show loaded file summary", "info", cmd_info},
    {"list", "ls", "List objects (optional class filter)", "list [class_id|class:<id>] [limit]", cmd_list},
    {"select", "sel", "Select current object", "select <index|id:<id>|name:<substr>|name:/regex/>", cmd_select},
    {"show", "s", "Show object details", "show [selector]", cmd_show},
    {"dump", "d", "Dump chunk", "dump [selector] [level]", cmd_dump},
    {"find", "f", "Find objects by name/class/id/regex", "find <substr>|/<regex>/ | class <id> | id <id>", cmd_find},
    {"trace", "t", "Trace references (placeholder)", "trace <index>", cmd_trace},
    {"verify", "v", "Verify chunks", "verify [all|selector]", cmd_verify},
    {"stats", "st", "Show file statistics", "stats", cmd_stats},
    {"meta", "m", "Show chunk metadata", "meta [selector]", cmd_meta},
    {"export", "x", "Export chunk(s) to JSON", "export <path> [selector|all] [data]", cmd_export},
    {"set", "", "Set options", "set color on|off | set level 0-3 | set page <n> | set regex-icase on|off", cmd_set},
    {"open", "o", "Open a different file (reloads session)", "open <path>", cmd_open},
    {"reload", "r", "Reload the current file", "reload", cmd_reload},
    {"clear", "cls", "Clear the screen", "clear", cmd_clear},
    {"quit", "q", "Exit debugger", "quit", cmd_quit},
    {"exit", "", "Exit debugger", "exit", cmd_quit},
    {NULL, NULL, NULL, NULL, NULL}};

static void suggest_commands(const char *name) {
    if (!name || !*name) {
        return;
    }

    /* Small, allocation-free edit-distance matcher to catch common typos. */
    const size_t max_suggestions = 5;
    const size_t max_len = 32;

    size_t name_len = strlen(name);
    if (name_len > max_len) {
        return;
    }

    typedef struct {
        const char *cmd;
        int score;
    } suggestion_t;

    suggestion_t best[max_suggestions];
    size_t best_count = 0;

    int prev[max_len + 1];
    int curr[max_len + 1];

    for (int i = 0; commands[i].name != NULL; i++) {
        const char *cmd_name = commands[i].name;
        if (!cmd_name) {
            continue;
        }

        size_t cmd_len = strlen(cmd_name);
        if (cmd_len > max_len) {
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

        if (best_count < max_suggestions) {
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

static const nmo_debug_command_t *find_command(const char *name) {
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(commands[i].name, name) == 0 ||
            (commands[i].alias && commands[i].alias[0] && strcmp(commands[i].alias, name) == 0)) {
            return &commands[i];
        }
    }
    return NULL;
}

void nmo_debug_print_banner(const nmo_debug_context_t *dbg) {
    const char *path = (dbg && dbg->filename) ? dbg->filename : NULL;
    const char *label = path && *path ? path : "(no file)";

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    if (dbg && dbg->session) {
        nmo_session_get_objects(dbg->session, &objects, &object_count);
    }

    printf("\nNMO Debugger (nmo debug)\n");
    printf("File: %s\n", label);
    if (object_count) {
        printf("Objects: %zu\n", object_count);
    }
    printf("Tip: 'help' lists commands. Try: list, select 0, show, dump.\n");
}

int nmo_debug_dispatch_command(nmo_debug_context_t *dbg, int argc, char **argv) {
    if (!dbg || argc <= 0 || !argv || !argv[0]) {
        return -1;
    }

    const nmo_debug_command_t *cmd = find_command(argv[0]);
    if (cmd == NULL) {
        fprintf(stderr, "Unknown command: %s\n", argv[0]);
        suggest_commands(argv[0]);
        fprintf(stderr, "Type 'help' to see available commands.\n");
        return -1;
    }

    return cmd->handler(dbg, argc, argv);
}

static int cmd_help(nmo_debug_context_t *dbg, int argc, char **argv) {
    (void)dbg;

    if (argc > 1) {
        const nmo_debug_command_t *cmd = find_command(argv[1]);
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

static int cmd_info(nmo_debug_context_t *dbg, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!dbg || !dbg->session) {
        fprintf(stderr, "No session loaded.\n");
        return -1;
    }

    nmo_file_info_t info = nmo_session_get_file_info(dbg->session);
    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_session_get_objects(dbg->session, &objects, &object_count);

    printf("\nSession:\n");
    printf("  File: %s\n", dbg->filename ? dbg->filename : "(unknown)");
    printf("  Objects: %zu\n", object_count);
    printf("  Managers: %u\n", info.manager_count);
    printf("  CK version: %u\n", info.ck_version);
    printf("  File version: %u\n", info.file_version);
    if (dbg->has_selection && dbg->selected_index < object_count) {
        printf("  Selected: idx=%zu id=%u\n", dbg->selected_index, nmo_object_get_id(objects[dbg->selected_index]));
    } else {
        printf("  Selected: (none)\n");
    }
    printf("\n");
    return 0;
}

static int cmd_list(nmo_debug_context_t *dbg, int argc, char **argv) {
    int filter = -1;
    size_t limit = 0;

    if (argc > 1) {
        if (strncmp(argv[1], "class:", 6) == 0) {
            filter = atoi(argv[1] + 6);
        } else if (isdigit((unsigned char)argv[1][0]) || argv[1][0] == '-') {
            filter = atoi(argv[1]);
        }
    }

    if (argc > 2) {
        (void)nmo_debug_parse_size(argv[2], &limit);
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_debug_get_objects(dbg, &objects, &object_count);

    printf("\nObjects:\n");
    size_t displayed = 0;

    for (size_t i = 0; i < object_count; i++) {
        nmo_class_id_t class_id = nmo_object_get_class_id(objects[i]);

        if (filter != -1 && class_id != (nmo_class_id_t)filter) {
            continue;
        }

        bool selected = dbg->has_selection && dbg->selected_index == i;
        nmo_debug_print_object_summary_marked(i, objects[i], selected);

        displayed++;
        if (!nmo_debug_paginate_if_needed(dbg, displayed)) {
            break;
        }
        if (limit > 0 && displayed >= limit) {
            break;
        }
    }

    if (filter != -1) {
        printf("\n%zu/%zu objects shown (class %d)\n", displayed, object_count, filter);
    } else {
        printf("\n%zu/%zu objects shown\n", displayed, object_count);
    }
    if (!dbg->has_selection && displayed > 0) {
        printf("Tip: use 'select <index>' then 'show'/'dump'.\n");
    }

    return 0;
}

static int cmd_select(nmo_debug_context_t *dbg, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: select <index|id:<id>|name:<substr>|name:/regex/>\n");
        return -1;
    }

    size_t index = 0;
    if (nmo_debug_resolve_object_index(dbg, argv[1], &index, false) != 0) {
        return -1;
    }

    dbg->selected_index = index;
    dbg->has_selection = true;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    nmo_debug_get_objects(dbg, &objects, &object_count);
    if (index < object_count) {
        printf("Selected object:\n");
        nmo_debug_print_object_summary(index, objects[index]);
    }

    return 0;
}

static int cmd_show(nmo_debug_context_t *dbg, int argc, char **argv) {
    size_t index = 0;
    if (nmo_debug_resolve_object_index(dbg, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_debug_get_objects(dbg, &objects, &object_count);

    if (index >= object_count) {
        fprintf(stderr, "Error: Index %zu out of range (0-%zu)\n", index, object_count ? object_count - 1 : 0);
        return -1;
    }

    nmo_object_t *obj = objects[index];
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *name = nmo_object_get_name(obj);
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);

    printf("\nObject [%zu]:\n", index);
    printf("  ID: %u\n", obj_id);
    printf("  Class: %d\n", class_id);
    printf("  Name: %s\n", name ? name : "(unnamed)");

    if (chunk) {
        size_t chunk_size = 0;
        nmo_chunk_get_data(chunk, &chunk_size);
        printf("  Chunk size: %zu bytes\n", chunk_size);
        printf("  Compressed: %s\n", nmo_chunk_is_compressed(chunk) ? "yes" : "no");
        printf("  ID count: %zu\n", nmo_chunk_get_id_count(chunk));
        printf("  Sub-chunks: %u\n", nmo_chunk_get_sub_chunk_count(chunk));
    } else {
        printf("  (No chunk data)\n");
    }

    return 0;
}

static int cmd_dump(nmo_debug_context_t *dbg, int argc, char **argv) {
    size_t index = 0;
    nmo_dump_level_t level = dbg->dump_level;

    const char *selector = argc > 1 ? argv[1] : NULL;
    const char *level_arg = argc > 2 ? argv[2] : NULL;
    if (selector && (selector[0] >= '0' && selector[0] <= '9') && argc > 2) {
        level_arg = argv[2];
    }

    if (nmo_debug_resolve_object_index(dbg, selector, &index, true) != 0) {
        return -1;
    }

    if (level_arg) {
        level = (nmo_dump_level_t)atoi(level_arg);
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_debug_get_objects(dbg, &objects, &object_count);

    if (index >= object_count) {
        fprintf(stderr, "Error: Index %zu out of range\n", index);
        return -1;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(objects[index]);
    if (chunk == NULL) {
        fprintf(stderr, "Error: Object has no chunk\n");
        return -1;
    }

    printf("\nChunk dump for object [%zu]:\n", index);

    nmo_inspector_options_t opts;
    nmo_inspector_init_options(&opts);
    opts.level = level;
    opts.colorize = dbg->colorize;
    opts.show_hex = (level >= NMO_DUMP_DETAILED);
    opts.show_sub_chunks = true;

    nmo_inspector_dump_chunk(chunk, stdout, &opts);

    return 0;
}

static int cmd_find(nmo_debug_context_t *dbg, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: find <substr>|/<regex>/ | class <id> | id <id>\n");
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_debug_get_objects(dbg, &objects, &object_count);

    if (strcmp(argv[1], "by-name") == 0 || strcmp(argv[1], "name") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: find name <substr>\n");
            return -1;
        }
        const char *search = argv[2];
        printf("\nSearching for name containing '%s':\n", search);

        size_t found = 0;
        for (size_t i = 0; i < object_count; i++) {
            const char *name = nmo_object_get_name(objects[i]);
            if (name && strstr(name, search)) {
                nmo_debug_print_object_summary(i, objects[i]);
                found++;
                if (!nmo_debug_paginate_if_needed(dbg, found)) {
                    break;
                }
            }
        }
        printf("\nFound %zu match(es)\n", found);
    } else if (strcmp(argv[1], "regex") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: find regex <pattern>\n");
            return -1;
        }
        const char *pattern = argv[2];
        printf("\nSearching for name matching /%s/\n", pattern);

        size_t found = 0;
        for (size_t i = 0; i < object_count; i++) {
            const char *name = nmo_object_get_name(objects[i]);
            if (!name) {
                continue;
            }
            if (nmo_debug_regex_matches(name, pattern, dbg->regex_icase)) {
                nmo_debug_print_object_summary(i, objects[i]);
                found++;
                if (!nmo_debug_paginate_if_needed(dbg, found)) {
                    break;
                }
            }
        }
        printf("\nFound %zu match(es)\n", found);

    } else if (strcmp(argv[1], "by-class") == 0 || strcmp(argv[1], "class") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: find class <class_id>\n");
            return -1;
        }
        char *list_args[3] = {(char *)"list", argv[2], NULL};
        return cmd_list(dbg, 2, list_args);
    } else if (strcmp(argv[1], "id") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: find id <object_id>\n");
            return -1;
        }
        size_t index = 0;
        char id_selector[64];
        snprintf(id_selector, sizeof(id_selector), "id:%s", argv[2]);
        if (nmo_debug_resolve_object_index(dbg, id_selector, &index, false) != 0) {
            return -1;
        }
        printf("\nFound object:\n");
        nmo_debug_print_object_summary(index, objects[index]);
        return 0;

    } else {
        /* Friendly shorthand:
         *   find <substr>
         *   find /regex/
         */
        const char *token = argv[1];
        size_t token_len = strlen(token);
        if (token_len >= 2 && token[0] == '/' && token[token_len - 1] == '/') {
            char pattern[256];
            size_t copy_len = token_len - 2;
            if (copy_len >= sizeof(pattern)) {
                copy_len = sizeof(pattern) - 1;
            }
            memcpy(pattern, token + 1, copy_len);
            pattern[copy_len] = '\0';
            char *args[3] = {(char *)"find", (char *)"regex", pattern};
            return cmd_find(dbg, 3, args);
        }

        char *args[4] = {(char *)"find", (char *)"name", (char *)token, NULL};
        return cmd_find(dbg, 3, args);
    }

    return 0;
}

static int cmd_open(nmo_debug_context_t *dbg, int argc, char **argv) {
    if (!dbg) {
        return -1;
    }
    if (argc < 2) {
        fprintf(stderr, "Usage: open <path>\n");
        return -1;
    }

    const char *path = argv[1];
    char err[128];
    if (!nmo_debug_load_file(dbg, path, err, sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err[0] ? err : "Failed to open file");
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_session_get_objects(dbg->session, &objects, &object_count);
    printf("Opened: %s (%zu objects)\n", dbg->filename, object_count);
    return 0;
}

static int cmd_reload(nmo_debug_context_t *dbg, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!dbg || !dbg->filename || !dbg->filename[0]) {
        fprintf(stderr, "No file to reload. Use 'open <path>' first.\n");
        return -1;
    }

    char *args[3] = {(char *)"open", (char *)dbg->filename, NULL};
    return cmd_open(dbg, 2, args);
}

static int cmd_clear(nmo_debug_context_t *dbg, int argc, char **argv) {
    (void)dbg;
    (void)argc;
    (void)argv;

#ifdef _WIN32
    (void)system("cls");
#else
    (void)system("clear");
#endif
    return 0;
}

static int cmd_trace(nmo_debug_context_t *dbg, int argc, char **argv) {
    (void)dbg;
    (void)argv;

    if (argc < 2) {
        fprintf(stderr, "Usage: trace <index>\n");
        return -1;
    }

    printf("(Reference tracing requires Phase 4 completion)\n");
    return 0;
}

static int cmd_verify(nmo_debug_context_t *dbg, int argc, char **argv) {
    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_debug_get_objects(dbg, &objects, &object_count);

    if (argc > 1 && strcmp(argv[1], "all") != 0) {
        size_t index = 0;
        if (nmo_debug_resolve_object_index(dbg, argv[1], &index, true) != 0) {
            return -1;
        }

        if (index >= object_count) {
            fprintf(stderr, "Error: Index out of range\n");
            return -1;
        }

        nmo_chunk_t *chunk = nmo_object_get_chunk(objects[index]);
        if (chunk == NULL) {
            fprintf(stderr, "Object has no chunk\n");
            return -1;
        }

        nmo_chunk_validation_t result;
        nmo_inspector_validate_chunk(chunk, &result);

        if (result.is_valid) {
            printf("[OK] Chunk [%zu] is valid\n", index);
        } else {
            printf("[ERROR] Chunk [%zu] is invalid: %s\n", index, result.error_message);
        }

    } else {
        printf("\nVerifying %zu chunks...\n", object_count);

        int errors = 0;
        for (size_t i = 0; i < object_count; i++) {
            nmo_chunk_t *chunk = nmo_object_get_chunk(objects[i]);
            if (chunk == NULL) {
                continue;
            }

            nmo_chunk_validation_t result;
            nmo_inspector_validate_chunk(chunk, &result);

            if (!result.is_valid) {
                printf("  [%3zu] ERROR: %s\n", i, result.error_message);
                errors++;
            }
        }

        if (errors == 0) {
            printf("[OK] All chunks valid\n");
        } else {
            printf("[ERROR] %d error(s) found\n", errors);
        }
    }

    return 0;
}

static int cmd_stats(nmo_debug_context_t *dbg, int argc, char **argv) {
    (void)argc;
    (void)argv;

    nmo_file_stats_t stats;
    if (nmo_stats_collect(dbg->session, &stats) != 0) {
        fprintf(stderr, "Error: Failed to collect statistics\n");
        return -1;
    }

    printf("\n");
    nmo_stats_print(&stats, stdout);

    return 0;
}

static int cmd_meta(nmo_debug_context_t *dbg, int argc, char **argv) {
    size_t index = 0;
    if (nmo_debug_resolve_object_index(dbg, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_debug_get_objects(dbg, &objects, &object_count);

    if (index >= object_count) {
        fprintf(stderr, "Error: Index out of range\n");
        return -1;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(objects[index]);
    if (!chunk) {
        fprintf(stderr, "Object has no chunk\n");
        return -1;
    }

    size_t data_size = 0;
    nmo_chunk_get_data(chunk, &data_size);
    printf("\nChunk metadata for object [%zu]:\n", index);
    printf("  Data version: %u\n", nmo_chunk_get_data_version(chunk));
    printf("  Chunk options: 0x%08X\n", chunk->chunk_options);
    printf("  Data size: %zu bytes\n", data_size);
    printf("  ID count: %zu\n", nmo_chunk_get_id_count(chunk));
    printf("  Sub-chunks: %u\n", nmo_chunk_get_sub_chunk_count(chunk));
    printf("  Compressed: %s\n", nmo_chunk_is_compressed(chunk) ? "yes" : "no");
    return 0;
}

static int cmd_export(nmo_debug_context_t *dbg, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: export <path> [selector|all] [data]\n");
        return -1;
    }

    const char *path = argv[1];
    const char *selector = NULL;
    bool export_all = false;
    bool include_data = false;

    if (argc > 2) {
        if (strcmp(argv[2], "all") == 0) {
            export_all = true;
        } else {
            selector = argv[2];
        }
    }

    if (argc > 3) {
        include_data = (strcmp(argv[3], "data") == 0 || strcmp(argv[3], "include-data") == 0);
    }

    FILE *out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "Failed to open output file: %s\n", path);
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_debug_get_objects(dbg, &objects, &object_count);

    if (export_all) {
        fprintf(out, "{\n  \"objects\": [\n");
        size_t written = 0;
        for (size_t i = 0; i < object_count; ++i) {
            nmo_chunk_t *chunk = nmo_object_get_chunk(objects[i]);
            if (!chunk) {
                continue;
            }

            if (written > 0) {
                fprintf(out, ",\n");
            }

            fprintf(out,
                    "    {\n      \"index\": %zu,\n      \"id\": %u,\n      \"class_id\": %u,\n      \"name\": ",
                    i,
                    nmo_object_get_id(objects[i]),
                    nmo_object_get_class_id(objects[i]));
            nmo_debug_json_write_string(out, nmo_object_get_name(objects[i]));
            fprintf(out, ",\n      \"chunk\": ");
            nmo_inspector_export_json(chunk, out, include_data);
            fprintf(out, "\n    }");
            written++;
        }
        fprintf(out, "\n  ]\n}\n");
    } else {
        size_t index = 0;
        if (nmo_debug_resolve_object_index(dbg, selector, &index, true) != 0) {
            fclose(out);
            return -1;
        }

        if (index >= object_count) {
            fprintf(stderr, "Error: Index out of range\n");
            fclose(out);
            return -1;
        }

        nmo_chunk_t *chunk = nmo_object_get_chunk(objects[index]);
        if (!chunk) {
            fprintf(stderr, "Object has no chunk\n");
            fclose(out);
            return -1;
        }

        fprintf(out,
                "{\n  \"index\": %zu,\n  \"id\": %u,\n  \"class_id\": %u,\n  \"name\": ",
                index,
                nmo_object_get_id(objects[index]),
                nmo_object_get_class_id(objects[index]));
        nmo_debug_json_write_string(out, nmo_object_get_name(objects[index]));
        fprintf(out, ",\n  \"chunk\": ");
        nmo_inspector_export_json(chunk, out, include_data);
        fprintf(out, "\n}\n");
    }

    fclose(out);
    printf("Exported JSON to %s\n", path);
    return 0;
}

static int cmd_set(nmo_debug_context_t *dbg, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: set <option> <value>\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  color on|off      - Enable/disable ANSI colors\n");
        fprintf(stderr, "  level 0-3         - Set dump detail level\n");
        fprintf(stderr, "  page <n>          - Set page size (0 disables paging)\n");
        fprintf(stderr, "  regex-icase on|off - Case-insensitive regex matching\n");
        return -1;
    }

    if (strcmp(argv[1], "color") == 0) {
        if (strcmp(argv[2], "on") == 0) {
            dbg->colorize = true;
            printf("Colors enabled\n");
        } else if (strcmp(argv[2], "off") == 0) {
            dbg->colorize = false;
            printf("Colors disabled\n");
        } else {
            fprintf(stderr, "Invalid value (use 'on' or 'off')\n");
            return -1;
        }

    } else if (strcmp(argv[1], "level") == 0) {
        int level = atoi(argv[2]);
        if (level < 0 || level > 3) {
            fprintf(stderr, "Invalid level (use 0-3)\n");
            return -1;
        }
        dbg->dump_level = (nmo_dump_level_t)level;
        printf("Dump level set to %d\n", level);
    } else if (strcmp(argv[1], "page") == 0) {
        size_t page_size = 0;
        if (!nmo_debug_parse_size(argv[2], &page_size)) {
            fprintf(stderr, "Invalid page size\n");
            return -1;
        }
        dbg->page_size = page_size;
        printf("Page size set to %zu\n", page_size);
    } else if (strcmp(argv[1], "regex-icase") == 0) {
        if (strcmp(argv[2], "on") == 0) {
            dbg->regex_icase = true;
            printf("Regex case-insensitive enabled\n");
        } else if (strcmp(argv[2], "off") == 0) {
            dbg->regex_icase = false;
            printf("Regex case-insensitive disabled\n");
        } else {
            fprintf(stderr, "Invalid value (use 'on' or 'off')\n");
            return -1;
        }

    } else {
        fprintf(stderr, "Unknown option: %s\n", argv[1]);
        return -1;
    }

    return 0;
}

static int cmd_quit(nmo_debug_context_t *dbg, int argc, char **argv) {
    (void)dbg;
    (void)argc;
    (void)argv;
    return 1;
}
