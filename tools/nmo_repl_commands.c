#include "nmo_repl_commands.h"

#include "nmo_cmd_core.h"
#include "nmo_cmd_ctx.h"
#include "nmo_repl_input.h"
#include "nmo_repl_util.h"

#include "nmo_repl_session.h"

#include "app/nmo_inspector.h"
#include "app/nmo_save.h"
#include "app/nmo_stats.h"
#include "behavior/nmo_param_value.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "dsl/nmo_dsl.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session_edit.h"
#include "type/nmo_type_string.h"

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
static int cmd_param(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_refs(nmo_repl_context_t *repl, int argc, char **argv);
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
static int cmd_rename(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_delete(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_create(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_copy(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_set_param(nmo_repl_context_t *repl, int argc, char **argv);

static const nmo_repl_command_t commands[] = {
    {"help", "h", "Show help for commands", "help [command]", cmd_help},
    {"info", "i", "Show loaded file summary", "info", cmd_info},
    {"list", "ls", "List objects (optional class filter)", "list [class_id|class:<id>|class:<name>|<name>] [limit]", cmd_list},
    {"select", "sel", "Select current object", "select <index|id:<id>|name:<substr>|name:/regex/>", cmd_select},
    {"show", "s", "Show object details", "show [selector]", cmd_show},
    {"dump", "d", "Dump chunk", "dump [selector] [level]", cmd_dump},
    {"find", "f", "Find objects by name/class/id/regex", "find <substr>|/<regex>/ | class <id|name> | id <id>", cmd_find},
    {"trace", "t", "Trace object references", "trace [selector] [--incoming|--outgoing|--both]", cmd_trace},
    {"param", "p", "Show parameter details with decoded value", "param [selector]", cmd_param},
    {"refs", "", "Show references for object", "refs [selector]", cmd_refs},
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
    /* mutation commands */
    {"rename", "ren", "Rename an object", "rename <selector> <new_name>", cmd_rename},
    {"delete", "del", "Delete object(s)", "delete <selector> [--cascade]", cmd_delete},
    {"create", "", "Create new object", "create <class> [name]", cmd_create},
    {"copy", "cp", "Copy object(s)", "copy <selector> [--cascade]", cmd_copy},
    {"set-param", "sp", "Set parameter value", "set-param <selector> <value>", cmd_set_param},
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

const char **nmo_repl_get_command_names(void) {
    /* Count entries (names + non-empty aliases) */
    size_t count = 0;
    for (int i = 0; commands[i].name != NULL; i++) {
        count++;
        if (commands[i].alias && commands[i].alias[0]) {
            count++;
        }
    }

    static const char *names[128];
    size_t idx = 0;
    for (int i = 0; commands[i].name != NULL && idx < sizeof(names) / sizeof(names[0]) - 1; i++) {
        names[idx++] = commands[i].name;
        if (commands[i].alias && commands[i].alias[0] && idx < sizeof(names) / sizeof(names[0]) - 1) {
            names[idx++] = commands[i].alias;
        }
    }
    names[idx] = NULL;
    return names;
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
#ifdef NMO_HAVE_ISOCLINE
    printf("Tip: 'help' lists commands. Tab completes. Ctrl+R searches history.\n");
#else
    printf("Tip: 'help' lists commands. Try: list, select 0, show, dump.\n");
#endif
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
#ifdef NMO_HAVE_ISOCLINE
    printf("\nNavigation:\n");
    printf("  Tab             Complete command or argument\n");
    printf("  Up/Down         Navigate command history\n");
    printf("  Ctrl+R          Search command history\n");
    printf("  !!              Repeat last command\n");
#else
    printf("\nHistory:\n");
    printf("  !!              Repeat last command\n");
    printf("  !N              Recall command N from history\n");
#endif
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

/** Visitor for REPL list command */
typedef struct {
    nmo_repl_context_t *repl;
    size_t displayed;
    size_t limit;
} repl_list_data_t;

static int repl_list_visitor(size_t index, nmo_object_t *obj,
                             const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    repl_list_data_t *d = (repl_list_data_t *)user;
    bool selected = d->repl->has_selection && d->repl->selected_index == index;
    nmo_repl_print_object_summary_marked(d->repl, index, obj, selected);
    d->displayed++;
    if (!nmo_repl_paginate_if_needed(d->repl, d->displayed)) return 1;
    if (d->limit > 0 && d->displayed >= d->limit) return 1;
    return 0;
}

static int cmd_list(nmo_repl_context_t *repl, int argc, char **argv) {
    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, repl->colorize);

    nmo_object_query_t query = {0};
    size_t limit = 0;

    if (argc > 1) {
        const char *token = argv[1];
        if (strncmp(token, "class:", 6) == 0) {
            token = token + 6;
        }

        if (token && token[0] != '\0') {
            if (isdigit((unsigned char)token[0]) || token[0] == '-') {
                query.class_id = (nmo_class_id_t)atoi(token);
            } else {
                query.class_id = nmo_core_class_id(&c, token);
                if (query.class_id == 0) {
                    fprintf(stderr, "Unknown class: %s\n", token);
                    fprintf(stderr, "Tip: use a numeric class_id or a known type name (e.g. CKCamera).\n");
                    return -1;
                }
            }
            query.include_derived_classes = false; /* REPL uses exact class match */
        }
    }

    if (argc > 2) {
        (void)nmo_repl_parse_size(argv[2], &limit);
    }

    printf("\nObjects:\n");

    repl_list_data_t ld = { .repl = repl, .displayed = 0, .limit = limit };
    nmo_core_iter_result_t result;
    nmo_core_iter_objects(&c, query.class_id ? &query : NULL,
                          repl_list_visitor, &ld, &result);

    if (query.class_id) {
        char class_buf[64];
        const char *class_name = nmo_core_class_name_or(&c, query.class_id,
                                                         class_buf, sizeof(class_buf));
        printf("\n%zu/%zu objects shown (class %d, %s)\n",
               ld.displayed, result.total, (int)query.class_id, class_name);
    } else {
        printf("\n%zu/%zu objects shown\n", ld.displayed, result.total);
    }
    if (!repl->has_selection && ld.displayed > 0) {
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

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, false);
    char class_buf[64];
    const char *class_name = nmo_core_class_name_or(&c, class_id, class_buf, sizeof(class_buf));

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
    nmo_status_t rc = nmo_inspector_dump_chunk(chunk, stdout, &options);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to dump chunk\n");
        return -1;
    }
    printf("\n");
    return 0;
}

/** Visitor for REPL find command */
typedef struct {
    nmo_repl_context_t *repl;
    size_t found;
} repl_find_data_t;

static int repl_find_visitor(size_t index, nmo_object_t *obj,
                             const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    repl_find_data_t *d = (repl_find_data_t *)user;
    nmo_repl_print_object_summary(d->repl, index, obj);
    d->found++;
    if (!nmo_repl_paginate_if_needed(d->repl, d->found)) return 1;
    return 0;
}

static int cmd_find(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: find <substr>|/<regex>/ | class <id|name> | id <id>\n");
        return -1;
    }

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, repl->colorize);

    nmo_object_query_t query = {0};
    const char *token = argv[1];

    /* Buffer for regex pattern extraction (must outlive filter) */
    char regex_buf[256];

    if (strcmp(token, "class") == 0 && argc > 2) {
        const char *arg = argv[2];
        if (isdigit((unsigned char)arg[0]) || arg[0] == '-') {
            query.class_id = (nmo_class_id_t)atoi(arg);
        } else {
            query.class_id = nmo_core_class_id(&c, arg);
            if (query.class_id == 0) {
                fprintf(stderr, "Unknown class: %s\n", arg);
                return -1;
            }
        }
        query.include_derived_classes = false; /* REPL uses exact match */
    } else if (strcmp(token, "id") == 0 && argc > 2) {
        uint32_t id_val = 0;
        if (nmo_repl_parse_u32(argv[2], &id_val)) {
            query.object_id = (nmo_object_id_t)id_val;
        }
    } else {
        size_t len = strlen(token);
        if (len >= 2 && token[0] == '/' && token[len - 1] == '/') {
            /* Regex pattern */
            len -= 2;
            if (len >= sizeof(regex_buf)) {
                len = sizeof(regex_buf) - 1;
            }
            memcpy(regex_buf, token + 1, len);
            regex_buf[len] = '\0';
            query.name = regex_buf;
            query.name_mode = NMO_OBJECT_QUERY_NAME_REGEX;
            query.name_case_insensitive = repl->regex_icase;
        } else {
            query.name = token;
            query.name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING;
            query.name_case_insensitive = false;
        }
    }

    printf("\nMatches:\n");

    repl_find_data_t fd = { .repl = repl, .found = 0 };
    nmo_core_iter_objects(&c, &query, repl_find_visitor, &fd, NULL);

    if (!fd.found) {
        printf("No matches.\n");
    }
    printf("\n");
    return 0;
}

/** Shared REPL ref visitor for trace and refs commands */
static int repl_ref_visitor(const nmo_core_ref_info_t *info,
                            const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    (void)user;
    nmo_object_id_t peer_id = info->is_incoming ? info->edge->from : info->edge->to;
    const char *arrow = info->is_incoming ? "<-" : "->";
    const char *name = info->peer_name ? info->peer_name : "(unknown)";
    printf("    %s ID=%u %s", arrow, peer_id, name);
    if (info->edge->field_path && info->edge->field_path[0])
        printf(" (via %s)", info->edge->field_path);
    printf(" [%s]\n", nmo_ref_kind_name(info->edge->kind));
    return 0;
}

static int cmd_trace(nmo_repl_context_t *repl, int argc, char **argv) {
    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    /* Parse direction flag (default: show both) */
    unsigned dir = NMO_CORE_REFS_BOTH;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--incoming") == 0 || strcmp(argv[i], "-i") == 0) {
            dir = NMO_CORE_REFS_IN;
        } else if (strcmp(argv[i], "--outgoing") == 0 || strcmp(argv[i], "-o") == 0) {
            dir = NMO_CORE_REFS_OUT;
        } else if (strcmp(argv[i], "--both") == 0 || strcmp(argv[i], "-b") == 0) {
            dir = NMO_CORE_REFS_BOTH;
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

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, repl->colorize);

    printf("\nReferences for [%zu] ID=%u %s:\n", index, obj_id,
           (obj_name && obj_name[0]) ? obj_name : "(unnamed)");

    nmo_core_ref_result_t ref_result = {0};
    nmo_core_iter_refs(&c, obj_id, dir, repl_ref_visitor, NULL, &ref_result);

    size_t total = 0;
    if (dir & NMO_CORE_REFS_OUT) total += ref_result.outgoing;
    if (dir & NMO_CORE_REFS_IN) total += ref_result.incoming;

    if (total == 0) {
        printf("  (no references found)\n");
    }

    printf("\n");
    return 0;
}

static int cmd_param(nmo_repl_context_t *repl, int argc, char **argv) {
    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_repl_get_objects(repl, &objects, &object_count);

    if (index >= object_count) {
        fprintf(stderr, "Error: Index %zu out of range\n", index);
        return -1;
    }

    nmo_object_t *obj = objects[index];
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    nmo_type_registry_t *registry = nmo_context_get_type_registry(repl->ctx);

    if (!registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        return -1;
    }

    if (!nmo_type_registry_is_class_derived_from(registry, (uint32_t)class_id, (uint32_t)NMO_CID_PARAMETER)) {
        fprintf(stderr, "Error: Object is not a parameter (class %d)\n", class_id);
        return -1;
    }

    const void *state = nmo_object_get_state(obj);
    if (!state) {
        fprintf(stderr, "Error: No state available\n");
        return -1;
    }

    const nmo_parameter_state_t *pstate = NULL;

    if (class_id == NMO_CID_PARAMETER) {
        pstate = (const nmo_parameter_state_t *)state;
    } else if (class_id == NMO_CID_PARAMETERLOCAL) {
        const nmo_parameterlocal_state_t *plocal = (const nmo_parameterlocal_state_t *)state;
        pstate = &plocal->base;
    } else if (class_id == NMO_CID_PARAMETEROUT) {
        const nmo_parameterout_state_t *pout = (const nmo_parameterout_state_t *)state;
        pstate = &pout->base;
    } else {
        fprintf(stderr, "Error: Unsupported parameter class %d\n", class_id);
        return -1;
    }

    if (!pstate) {
        fprintf(stderr, "Error: No parameter state available\n");
        return -1;
    }

    const char *name = nmo_object_get_name(obj);
    nmo_object_id_t obj_id = nmo_object_get_id(obj);
    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, false);
    char class_buf[64];
    const char *class_name = nmo_core_class_name_or(&c, class_id, class_buf, sizeof(class_buf));

    printf("\nParameter Details:\n");
    printf("  Index: %zu\n", index);
    printf("  ID: %u\n", obj_id);
    printf("  Class: %s\n", class_name);
    if (name && name[0]) {
        printf("  Name: %s\n", name);
    }

    if (pstate->has_state) {
        const char *type_name = nmo_param_value_type_name(pstate, registry);
        if (type_name && type_name[0]) {
            printf("  Type: %s\n", type_name);
        } else {
            char guid_str[64];
            nmo_guid_format(pstate->type_guid, guid_str, sizeof(guid_str));
            printf("  Type: %s\n", guid_str);
        }

        printf("  Mode: %s\n", nmo_param_mode_to_string(pstate->mode));

        if (pstate->mode == CKPARAM_MODE_BUFFER && pstate->buffer_data.data && pstate->buffer_data.count > 0) {
            printf("  Buffer Size: %zu bytes\n", pstate->buffer_data.count);
            if (pstate->buffer_data.count < 64) {
                printf("  Hex: ");
                const uint8_t *bytes = (const uint8_t *)pstate->buffer_data.data;
                for (size_t i = 0; i < pstate->buffer_data.count; ++i) {
                    printf("%02x ", bytes[i]);
                }
                printf("\n");
            }
        } else if (pstate->mode == CKPARAM_MODE_OBJECT) {
            printf("  Referenced Object ID: %u\n", pstate->object_id);
        }
    } else {
        printf("  State: (no state saved)\n");
    }

    printf("\n");
    return 0;
}

static int cmd_refs(nmo_repl_context_t *repl, int argc, char **argv) {
    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
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
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, repl->colorize);
    char class_buf[64];
    const char *class_name = nmo_core_class_name_or(&c, class_id, class_buf, sizeof(class_buf));

    printf("\nReferences for [%zu] ID=%u %s (%s):\n", index, obj_id,
           (obj_name && obj_name[0]) ? obj_name : "(unnamed)", class_name);

    nmo_core_ref_result_t ref_result = {0};
    nmo_core_iter_refs(&c, obj_id, NMO_CORE_REFS_BOTH,
                       repl_ref_visitor, NULL, &ref_result);

    if (ref_result.outgoing == 0 && ref_result.incoming == 0) {
        printf("  (no references found)\n");
    }

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

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, repl->colorize);

    nmo_dsl_value_t result = {0};
    nmo_status_t st = nmo_core_dsl_eval(&c, obj, expr_buf, &result);

    if (st != NMO_OK) {
        fprintf(stderr, "Error: DSL evaluation failed: %s\n", nmo_error_string(st));
        return -1;
    }

    char buf[512];
    nmo_core_dsl_format(&result, buf, sizeof(buf));
    printf("=> %s\n", buf);

    nmo_dsl_value_destroy(&result);
    return 0;
}

/** Visitor for REPL query command - prints matching objects with pagination */
typedef struct {
    nmo_repl_context_t *repl;
    size_t found;
} repl_query_data_t;

static int repl_query_visitor(size_t index, nmo_object_t *obj,
                              const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    repl_query_data_t *d = (repl_query_data_t *)user;
    nmo_repl_print_object_summary(d->repl, index, obj);
    d->found++;
    if (!nmo_repl_paginate_if_needed(d->repl, d->found)) return 1;
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

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, repl->colorize);

    nmo_object_query_t query = {0};
    nmo_core_query_dsl_t query_dsl = {0};
    nmo_status_t st = nmo_core_query_add_dsl_filter(&c, &query, expr_buf, &query_dsl);
    if (st != NMO_OK) {
        nmo_core_dsl_print_error(stderr, expr_buf, "Error: Failed to compile expression");
        return -1;
    }

    printf("\nQuery matches:\n");

    repl_query_data_t qd = { .repl = repl, .found = 0 };
    nmo_core_iter_result_t result;
    nmo_core_iter_objects(&c, &query, repl_query_visitor, &qd, &result);

    nmo_core_query_dsl_destroy(&query_dsl);

    if (!qd.found) {
        printf("No matches.\n");
    } else {
        printf("\n%zu match(es)\n", qd.found);
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

    repl->dirty = false;
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
        if (nmo_inspector_validate_chunk(chunk, &result) != NMO_OK || !result.is_valid) {
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

    nmo_repl_input_invalidate_name_cache(repl);
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

    nmo_repl_input_invalidate_name_cache(repl);
    printf("Reloaded %s\n", repl->filename);
    return 0;
}

static int cmd_history(nmo_repl_context_t *repl, int argc, char **argv) {
    (void)argc;
    (void)argv;

#ifdef NMO_HAVE_ISOCLINE
    (void)repl;
    printf("\nHistory is managed by the line editor.\n");
    printf("  Use Up/Down arrows to navigate history.\n");
    printf("  Use Ctrl+R to search history.\n");
    printf("  Use !! to recall the last command.\n\n");
#else
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
#endif
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
    (void)argc;
    (void)argv;
    if (repl->dirty) {
        fprintf(stderr, "Warning: unsaved changes. Use 'save <path>' first, or 'quit' again to discard.\n");
        repl->dirty = false;  /* allow second quit to proceed */
        return 0;
    }
    return 1;
}

/* ============================================================================
 * Mutation commands
 * ============================================================================ */

static int cmd_rename(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: rename <selector> <new_name>\n");
        return -1;
    }
    if (!repl->session) { fprintf(stderr, "No session loaded.\n"); return -1; }

    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argv[1], &index, false) != 0) return -1;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    nmo_repl_get_objects(repl, &objects, &object_count);
    if (index >= object_count) { fprintf(stderr, "Error: Index out of range\n"); return -1; }

    nmo_object_t *obj = objects[index];
    nmo_object_id_t id = nmo_object_get_id(obj);
    const char *old_name = nmo_object_get_name(obj);
    printf("Renaming #%u '%s' -> '%s'\n", id, old_name ? old_name : "", argv[2]);

    nmo_object_repository_t *repo = nmo_session_get_repository(repl->session);
    int rc = nmo_object_repository_rename(repo, id, argv[2]);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(rc));
        return -1;
    }

    repl->dirty = true;
    nmo_repl_input_invalidate_name_cache(repl);
    return 0;
}

static int cmd_delete(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: delete <selector> [--cascade]\n");
        return -1;
    }
    if (!repl->session) { fprintf(stderr, "No session loaded.\n"); return -1; }

    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argv[1], &index, false) != 0) return -1;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    nmo_repl_get_objects(repl, &objects, &object_count);
    if (index >= object_count) { fprintf(stderr, "Error: Index out of range\n"); return -1; }

    bool cascade = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--cascade") == 0) cascade = true;
    }

    nmo_object_t *obj = objects[index];
    nmo_object_id_t id = nmo_object_get_id(obj);
    const char *name = nmo_object_get_name(obj);

    uint32_t flags = cascade ? NMO_RUNTIME_REQUEST_CASCADE : NMO_RUNTIME_REQUEST_SAFE_DETACH;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    int rc = nmo_session_destroy_objects(repl->session, &id, 1, flags, &report);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(rc));
        return -1;
    }

    printf("Deleted #%u '%s' (%zu object(s) removed)\n",
           id, name ? name : "", report.deleted_objects);

    repl->dirty = true;
    repl->has_selection = false;
    nmo_repl_input_invalidate_name_cache(repl);
    return 0;
}

static int cmd_create(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: create <class_name> [object_name]\n");
        return -1;
    }
    if (!repl->session) { fprintf(stderr, "No session loaded.\n"); return -1; }

    const nmo_type_registry_t *registry = nmo_context_get_type_registry(repl->ctx);
    nmo_class_id_t class_id = 0;

    /* Try class name lookup */
    const nmo_type_descriptor_t *td = nmo_type_registry_find_by_name(registry, argv[1]);
    if (td) {
        class_id = (nmo_class_id_t)td->class_id;
    }
    if (!class_id) {
        fprintf(stderr, "Error: Unknown class '%s'\n", argv[1]);
        return -1;
    }

    const char *name = (argc >= 3) ? argv[2] : NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;

    nmo_object_id_t new_id = 0;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    int rc = nmo_session_create_object(repl->session, class_id, name, type_guid, &new_id, &report);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(rc));
        return -1;
    }

    printf("Created #%u (%s) %s\n", new_id, argv[1], name ? name : "");

    repl->dirty = true;
    nmo_repl_input_invalidate_name_cache(repl);
    return 0;
}

static int cmd_copy(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: copy <selector> [--cascade]\n");
        return -1;
    }
    if (!repl->session) { fprintf(stderr, "No session loaded.\n"); return -1; }

    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argv[1], &index, false) != 0) return -1;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    nmo_repl_get_objects(repl, &objects, &object_count);
    if (index >= object_count) { fprintf(stderr, "Error: Index out of range\n"); return -1; }

    bool cascade = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--cascade") == 0) cascade = true;
    }

    nmo_object_id_t id = nmo_object_get_id(objects[index]);
    uint32_t flags = cascade ? NMO_RUNTIME_REQUEST_CASCADE : 0;

    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));
    int rc = nmo_session_copy_objects(repl->session, &id, 1, flags, &report);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(rc));
        return -1;
    }

    printf("Copied %zu object(s)\n", report.copied_objects);

    repl->dirty = true;
    nmo_repl_input_invalidate_name_cache(repl);
    return 0;
}

static int cmd_set_param(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: set-param <selector> <value>\n");
        return -1;
    }
    if (!repl->session) { fprintf(stderr, "No session loaded.\n"); return -1; }

    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argv[1], &index, false) != 0) return -1;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    nmo_repl_get_objects(repl, &objects, &object_count);
    if (index >= object_count) { fprintf(stderr, "Error: Index out of range\n"); return -1; }

    nmo_object_t *obj = objects[index];
    /* Check that object has a settable parameter state */
    const nmo_parameter_state_t *pstate = nmo_parameter_get_state(obj);
    if (!pstate) {
        fprintf(stderr, "Error: Object is not a parameter (class %u)\n",
                nmo_object_get_class_id(obj));
        return -1;
    }

    if (pstate->mode != CKPARAM_MODE_BUFFER) {
        fprintf(stderr, "Error: Only buffer-mode parameters can be set (mode=%d)\n", pstate->mode);
        return -1;
    }

    nmo_session_edit_t *edit = NULL;
    nmo_status_t st = nmo_session_edit_begin(repl->session, "repl set-param", &edit);
    if (st == NMO_OK) {
        st = nmo_session_edit_set_parameter_value(
            edit, nmo_object_get_id(obj), argv[2]);
    }
    if (st == NMO_OK) {
        st = nmo_session_edit_commit(edit);
    } else if (edit != NULL) {
        nmo_session_edit_rollback(edit);
    }
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Cannot set value '%s': %s\n",
                argv[2], nmo_error_string(st));
        return -1;
    }

    const char *name = nmo_object_get_name(obj);
    printf("Set parameter #%u '%s' = %s\n",
           nmo_object_get_id(obj), name ? name : "", argv[2]);

    repl->dirty = true;
    return 0;
}
