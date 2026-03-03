#include "nmo_repl_commands.h"

#include "nmo_repl_util.h"

#include "nmo_repl_session.h"

#include "app/nmo_inspector.h"
#include "app/nmo_saver.h"
#include "app/nmo_stats.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "dsl/nmo_dsl.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_ref_graph.h"

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
static int cmd_eval(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_query(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_save(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_verify(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_stats(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_meta(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_export(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_set(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_open(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_reload(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_history(nmo_repl_context_t *repl, int argc, char **argv);
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
    {"trace", "t", "Trace object references", "trace [selector] [--incoming|--outgoing|--both]", cmd_trace},
    {"eval", "e", "Evaluate DSL expression on selected object", "eval <expression>", cmd_eval},
    {"query", "", "Filter objects by DSL predicate", "query <expression>", cmd_query},
    {"save", "", "Save session to file", "save <path> [--compress] [--sequential-ids]", cmd_save},
    {"verify", "v", "Verify chunks", "verify [all|selector]", cmd_verify},
    {"stats", "st", "Show file statistics", "stats", cmd_stats},
    {"meta", "m", "Show chunk metadata", "meta [selector]", cmd_meta},
    {"export", "x", "Export chunk(s) to JSON", "export <path> [selector|all] [data]", cmd_export},
    {"set", "", "Set options", "set color on|off | set level 0-3 | set page <n> | set regex-icase on|off", cmd_set},
    {"history", "", "Show command history (use !N to recall)", "history", cmd_history},
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

    printf("\nnmo interactive shell\n");
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
    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    /* Parse direction flag (default: show both) */
    bool show_outgoing = true;
    bool show_incoming = true;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--incoming") == 0 || strcmp(argv[i], "-i") == 0) {
            show_outgoing = false;
            show_incoming = true;
        } else if (strcmp(argv[i], "--outgoing") == 0 || strcmp(argv[i], "-o") == 0) {
            show_outgoing = true;
            show_incoming = false;
        } else if (strcmp(argv[i], "--both") == 0 || strcmp(argv[i], "-b") == 0) {
            show_outgoing = true;
            show_incoming = true;
        }
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    if (index >= object_count) {
        fprintf(stderr, "Error: Index %zu out of range\n", index);
        return -1;
    }

    nmo_object_t *obj = objects[index];
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    const char *obj_name = nmo_object_get_name(obj);

    /* Build reference graph */
    nmo_object_repository_t *repo = nmo_session_get_repository(repl->session);
    nmo_type_registry_t *type_reg = nmo_context_get_type_registry(repl->ctx);
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return -1;
    }

    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, type_reg, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return -1;
    }

    printf("\nReferences for [%zu] ID=%u %s:\n", index, obj_id,
           (obj_name && obj_name[0]) ? obj_name : "(unnamed)");

    size_t total_displayed = 0;

    /* Show outgoing references */
    if (show_outgoing) {
        nmo_ref_edge_t *out_edges = NULL;
        size_t out_count = 0;
        nmo_ref_graph_get_object_edges(graph, obj_id, NMO_REF_DIR_OUTGOING, &out_edges, &out_count);

        if (out_count > 0) {
            printf("\n  Outgoing (%zu):\n", out_count);
            for (size_t i = 0; i < out_count; ++i) {
                const nmo_ref_edge_t *e = &out_edges[i];
                /* Find peer name */
                const char *peer_name = "(unknown)";
                for (size_t j = 0; j < object_count; ++j) {
                    if (nmo_object_get_id(objects[j]) == e->to) {
                        const char *n = nmo_object_get_name(objects[j]);
                        if (n && n[0]) {
                            peer_name = n;
                        }
                        break;
                    }
                }
                printf("    -> ID=%u %s", e->to, peer_name);
                if (e->field_path && e->field_path[0]) {
                    printf(" (via %s)", e->field_path);
                }
                printf("\n");
                total_displayed++;
            }
        }
    }

    /* Show incoming references */
    if (show_incoming) {
        nmo_ref_edge_t *in_edges = NULL;
        size_t in_count = 0;
        nmo_ref_graph_get_object_edges(graph, obj_id, NMO_REF_DIR_INCOMING, &in_edges, &in_count);

        if (in_count > 0) {
            printf("\n  Incoming (%zu):\n", in_count);
            for (size_t i = 0; i < in_count; ++i) {
                const nmo_ref_edge_t *e = &in_edges[i];
                const char *peer_name = "(unknown)";
                for (size_t j = 0; j < object_count; ++j) {
                    if (nmo_object_get_id(objects[j]) == e->from) {
                        const char *n = nmo_object_get_name(objects[j]);
                        if (n && n[0]) {
                            peer_name = n;
                        }
                        break;
                    }
                }
                printf("    <- ID=%u %s", e->from, peer_name);
                if (e->field_path && e->field_path[0]) {
                    printf(" (via %s)", e->field_path);
                }
                printf("\n");
                total_displayed++;
            }
        }
    }

    if (total_displayed == 0) {
        printf("  (no references found)\n");
    }

    nmo_arena_destroy(arena);
    printf("\n");
    return 0;
}

static int cmd_eval(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: eval <expression>\n");
        fprintf(stderr, "Evaluates a DSL expression in the context of the selected object.\n");
        return -1;
    }

    if (!repl->has_selection) {
        fprintf(stderr, "No object selected. Use 'select <index>' first.\n");
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    if (repl->selected_index >= object_count) {
        fprintf(stderr, "Error: Selected index out of range\n");
        return -1;
    }

    /* Reconstruct expression from remaining args (handles spaces in quoted strings) */
    char expr_buf[NMO_REPL_MAX_CMD_LEN];
    size_t pos = 0;
    for (int i = 1; i < argc && pos < sizeof(expr_buf) - 1; ++i) {
        if (i > 1 && pos < sizeof(expr_buf) - 1) {
            expr_buf[pos++] = ' ';
        }
        size_t len = strlen(argv[i]);
        if (pos + len >= sizeof(expr_buf)) {
            len = sizeof(expr_buf) - pos - 1;
        }
        memcpy(expr_buf + pos, argv[i], len);
        pos += len;
    }
    expr_buf[pos] = '\0';

    nmo_object_t *obj = objects[repl->selected_index];
    nmo_type_registry_t *registry = nmo_context_get_type_registry(repl->ctx);

    /* Look up the type descriptor for the object's class */
    nmo_type_id_t type_id = nmo_type_registry_class_id_to_type_id(
        registry, (uint32_t)nmo_object_get_class_id(obj));
    const nmo_type_descriptor_t *type_desc = NULL;
    if (type_id != NMO_TYPE_ID_INVALID) {
        type_desc = nmo_type_registry_get_by_id(registry, type_id);
    }

    /* Set up DSL eval context */
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    const void *instance = NULL;
    size_t data_size = 0;
    if (chunk) {
        instance = nmo_chunk_get_data(chunk, &data_size);
    }

    nmo_dsl_eval_context_t eval_ctx;
    memset(&eval_ctx, 0, sizeof(eval_ctx));
    eval_ctx.registry = registry;
    eval_ctx.root_type = type_desc;
    eval_ctx.root_instance = (void *)instance;
    eval_ctx.current_type = type_desc;
    eval_ctx.current_instance = instance;

    nmo_dsl_value_t result = {0};
    nmo_status_t st = nmo_dsl_eval_one(registry, &eval_ctx, expr_buf, &result);

    if (st != NMO_OK) {
        fprintf(stderr, "Error: DSL evaluation failed: %s\n", nmo_error_string(st));
        return -1;
    }

    /* Format and display result */
    printf("=> ");
    switch (result.kind) {
        case NMO_DSL_VALUE_NULL:    printf("null\n"); break;
        case NMO_DSL_VALUE_BOOL:    printf("%s\n", result.as.b ? "true" : "false"); break;
        case NMO_DSL_VALUE_INT:     printf("%lld\n", (long long)result.as.i); break;
        case NMO_DSL_VALUE_UINT:    printf("%llu\n", (unsigned long long)result.as.u); break;
        case NMO_DSL_VALUE_REAL:    printf("%g\n", result.as.r); break;
        case NMO_DSL_VALUE_STRING:  printf("\"%s\"\n", result.as.s ? result.as.s : ""); break;
        default:                    printf("<value kind=%d>\n", result.kind); break;
    }

    nmo_dsl_value_destroy(&result);
    return 0;
}

static int cmd_query(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: query <expression>\n");
        fprintf(stderr, "Filters objects: evaluates expression for each, shows matches.\n");
        return -1;
    }

    /* Reconstruct expression */
    char expr_buf[NMO_REPL_MAX_CMD_LEN];
    size_t pos = 0;
    for (int i = 1; i < argc && pos < sizeof(expr_buf) - 1; ++i) {
        if (i > 1 && pos < sizeof(expr_buf) - 1) {
            expr_buf[pos++] = ' ';
        }
        size_t len = strlen(argv[i]);
        if (pos + len >= sizeof(expr_buf)) {
            len = sizeof(expr_buf) - pos - 1;
        }
        memcpy(expr_buf + pos, argv[i], len);
        pos += len;
    }
    expr_buf[pos] = '\0';

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    nmo_type_registry_t *registry = nmo_context_get_type_registry(repl->ctx);

    printf("\nQuery matches:\n");
    size_t found = 0;

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (!chunk) {
            continue;
        }

        /* Set up per-object DSL context */
        nmo_type_id_t type_id = nmo_type_registry_class_id_to_type_id(
            registry, (uint32_t)nmo_object_get_class_id(obj));
        const nmo_type_descriptor_t *type_desc = NULL;
        if (type_id != NMO_TYPE_ID_INVALID) {
            type_desc = nmo_type_registry_get_by_id(registry, type_id);
        }

        size_t data_size = 0;
        const void *instance = nmo_chunk_get_data(chunk, &data_size);

        nmo_dsl_eval_context_t eval_ctx;
        memset(&eval_ctx, 0, sizeof(eval_ctx));
        eval_ctx.registry = registry;
        eval_ctx.root_type = type_desc;
        eval_ctx.root_instance = (void *)instance;
        eval_ctx.current_type = type_desc;
        eval_ctx.current_instance = instance;

        nmo_dsl_value_t result = {0};
        nmo_status_t st = nmo_dsl_eval_one(registry, &eval_ctx, expr_buf, &result);

        /* Check if result is truthy */
        bool is_match = false;
        if (st == NMO_OK) {
            switch (result.kind) {
                case NMO_DSL_VALUE_BOOL: is_match = result.as.b; break;
                case NMO_DSL_VALUE_INT:  is_match = result.as.i != 0; break;
                case NMO_DSL_VALUE_UINT: is_match = result.as.u != 0; break;
                case NMO_DSL_VALUE_REAL: is_match = result.as.r != 0.0; break;
                case NMO_DSL_VALUE_STRING: is_match = (result.as.s != NULL && result.as.s[0] != '\0'); break;
                case NMO_DSL_VALUE_NULL: is_match = false; break;
                default: is_match = true; break;
            }
        }

        nmo_dsl_value_destroy(&result);

        if (is_match) {
            nmo_repl_print_object_summary(repl, i, obj);
            found++;
            if (!nmo_repl_paginate_if_needed(repl, found)) {
                break;
            }
        }
    }

    if (!found) {
        printf("No matches.\n");
    } else {
        printf("\n%zu match(es)\n", found);
    }
    printf("\n");
    return 0;
}

static int cmd_save(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: save <path> [--compress] [--sequential-ids]\n");
        return -1;
    }

    if (!repl->session) {
        fprintf(stderr, "No session loaded.\n");
        return -1;
    }

    const char *output_path = argv[1];
    nmo_save_options_t opts = nmo_save_options_default();

    /* Parse optional flags */
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--compress") == 0) {
            opts.flags |= NMO_SAVE_COMPRESSED;
        } else if (strcmp(argv[i], "--sequential-ids") == 0) {
            opts.flags |= NMO_SAVE_SEQUENTIAL_IDS;
        }
    }

    int rc = nmo_save_file(repl->session, output_path, &opts);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to save to '%s': %s\n",
                output_path, nmo_error_string(rc));
        return -1;
    }

    printf("Saved to %s\n", output_path);
    return 0;
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

    nmo_runtime_load_stats_t stats;
    if (nmo_session_get_runtime_load_stats(repl->session, &stats) != NMO_OK) {
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

static int cmd_history(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (repl->history_count == 0) {
        printf("No history.\n");
        return 0;
    }

    printf("\nCommand History:\n");
    for (size_t i = 0; i < repl->history_count; ++i) {
        size_t idx = (repl->history_start + i) % NMO_REPL_HISTORY_SIZE;
        printf("  %3zu  %s\n", i + 1, repl->history[idx] ? repl->history[idx] : "");
    }
    printf("\nTip: use !N to recall command N, !! to recall last.\n\n");
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
