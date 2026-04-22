#include "nmo_repl_commands.h"

#include "nmo_cmd_core.h"
#include "nmo_cmd_ctx.h"
#include "nmo_command_registry.h"
#include "nmo_cli_common.h"
#include "nmo_cli_write.h"
#include "nmo_repl_input.h"
#include "nmo_repl_util.h"

#include "nmo_repl_session.h"

#include "commands/nmo_cmd_animation.h"
#include "commands/nmo_cmd_behavior.h"
#include "commands/nmo_cmd_chunk.h"
#include "commands/nmo_cmd_completion.h"
#include "commands/nmo_cmd_data.h"
#include "commands/nmo_cmd_debug.h"
#include "commands/nmo_cmd_diff.h"
#include "commands/nmo_cmd_entity.h"
#include "commands/nmo_cmd_extension.h"
#include "commands/nmo_cmd_file.h"
#include "commands/nmo_cmd_material.h"
#include "commands/nmo_cmd_mesh.h"
#include "commands/nmo_cmd_object.h"
#include "commands/nmo_cmd_parameter.h"
#include "commands/nmo_cmd_resource.h"
#include "commands/nmo_cmd_scene.h"
#include "commands/nmo_cmd_texture.h"
#include "commands/nmo_cmd_type.h"
#include "commands/nmo_cmd_validate.h"

#include "app/nmo_inspector.h"
#include "behavior/nmo_param_value.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_parse.h"
#include "document/nmo_document_save.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session.h"
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
static int cmd_cli_read(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_cli(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_object(nmo_repl_context_t *repl, int argc, char **argv);
static int cmd_parameter(nmo_repl_context_t *repl, int argc, char **argv);
static int repl_dispatch_cli_read_group(nmo_repl_context_t *repl, int argc, char **argv,
                                        const nmo_cli_global_opts_t *global);

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
    {"cli", "", "Run CLI-shaped read command with global options", "cli [global-options] <group> <action> ...", cmd_cli},
    {"file", "", "Run CLI file read commands", "file info|header|stats|classes|plugins|space ...", cmd_cli_read},
    {"chunk", "", "Run CLI chunk read commands", "chunk list|tree|show|find ...", cmd_cli_read},
    {"behavior", "", "Run CLI behavior read commands", "behavior list|stats|show|graph|dump|find|trace ...", cmd_cli_read},
    {"resource", "", "Run CLI resource read commands", "resource list|show|extract|info ...", cmd_cli_read},
    {"type", "", "Run CLI type read commands", "type list|show|class-tree ...", cmd_cli_read},
    {"validate", "", "Run CLI validation read commands", "validate all|structure|references|resources|orphans ...", cmd_cli_read},
    {"convert", "", "Reject CLI convert mutations in REPL", "convert ...", cmd_cli_read},
    {"diff", "", "Run CLI diff read commands", "diff summary|objects|chunks|full <other-file> ...", cmd_cli_read},
    {"extension", "", "Run CLI extension read commands", "extension list|info|check ...", cmd_cli_read},
    {"texture", "", "Run CLI texture read commands", "texture list|show|extract ...", cmd_cli_read},
    {"data", "", "Run CLI data read commands", "data list|show|dump ...", cmd_cli_read},
    {"scene", "", "Run CLI scene read commands", "scene list|show ...", cmd_cli_read},
    {"entity", "", "Run CLI entity read commands", "entity list|show ...", cmd_cli_read},
    {"material", "", "Run CLI material read commands", "material list|show ...", cmd_cli_read},
    {"mesh", "", "Run CLI mesh read commands", "mesh list|show|export ...", cmd_cli_read},
    {"animation", "", "Run CLI animation read commands", "animation list|show|keys|export ...", cmd_cli_read},
    {"debug", "", "Run CLI debug read commands", "debug load-phases|chunks|objects|export ...", cmd_cli_read},
    {"completion", "", "Run CLI completion printers", "completion bash|fish|zsh|powershell", cmd_cli_read},
    /* mutation commands */
    {"object", "", "Run grouped object commands", "object <cli-read-action>|show|refs|rename|delete|create|copy ...", cmd_object},
    {"parameter", "", "Run grouped parameter commands", "parameter list|show|dump|set ...", cmd_parameter},
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
    if (repl && repl->session) {
        nmo_cmd_ctx_t c;
        nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, false);
        nmo_core_iter_result_t result = {0};
        if (nmo_core_object_query_run(&c, NULL, NULL, NULL, &result) == NMO_CLI_EXIT_SUCCESS) {
            object_count = result.matched;
        }
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
    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, repl->colorize);
    nmo_core_object_count(&c, &object_count);

    printf("\nSession:\n");
    printf("  File: %s\n", repl->filename ? repl->filename : "(unknown)");
    printf("  Objects: %zu\n", object_count);
    printf("  Managers: %u\n", info.manager_count);
    printf("  CK version: %u\n", info.ck_version);
    printf("  File version: %u\n", info.file_version);
    if (repl->has_selection && repl->selected_index < object_count) {
        nmo_object_t *selected = nmo_repl_object_at(repl, repl->selected_index);
        printf("  Selected: idx=%zu id=%u\n",
               repl->selected_index,
               selected ? nmo_object_get_id(selected) : 0);
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

static int repl_query_set_exact_class(
    const nmo_cmd_ctx_t *c,
    nmo_object_query_t *query,
    const char *class_token,
    bool print_tip)
{
    if (class_token == NULL || class_token[0] == '\0') {
        return 0;
    }

    if (isdigit((unsigned char)class_token[0]) || class_token[0] == '-') {
        uint32_t class_id = 0;
        if (nmo_parse_u32_range(class_token, 0, UINT32_MAX, &class_id) != NMO_OK) {
            fprintf(stderr, "Invalid class ID: %s\n", class_token);
            return -1;
        }
        query->class_id = (nmo_class_id_t)class_id;
    } else if (nmo_core_query_set_class_name(
                   c, query, class_token, false) != NMO_OK) {
        fprintf(stderr, "Unknown class: %s\n", class_token);
        if (print_tip) {
            fprintf(stderr, "Tip: use a numeric class_id or a known type name (e.g. CKCamera).\n");
        }
        return -1;
    }

    query->include_derived_classes = false; /* REPL uses exact class match */
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

        if (repl_query_set_exact_class(&c, &query, token, true) != 0) {
            return -1;
        }
    }

    if (argc > 2) {
        (void)nmo_repl_parse_size(argv[2], &limit);
    }

    printf("\nObjects:\n");

    repl_list_data_t ld = { .repl = repl, .displayed = 0, .limit = limit };
    nmo_core_iter_result_t result = {0};
    nmo_core_object_query_run(&c, query.class_id ? &query : NULL,
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

    size_t object_count = nmo_repl_object_count(repl);
    nmo_object_t *obj = nmo_repl_object_at(repl, index);
    if (obj && index < object_count) {
        printf("Selected object:\n");
        nmo_repl_print_object_summary(repl, index, obj);
    }

    return 0;
}

static int cmd_show(nmo_repl_context_t *repl, int argc, char **argv) {
    size_t index = 0;
    if (nmo_repl_resolve_object_index(repl, argc > 1 ? argv[1] : NULL, &index, true) != 0) {
        return -1;
    }

    size_t object_count = nmo_repl_object_count(repl);
    nmo_object_t *obj = nmo_repl_object_at(repl, index);

    if (index >= object_count || !obj) {
        fprintf(stderr, "Error: Index %zu out of range (0-%zu)\n", index, object_count ? object_count - 1 : 0);
        return -1;
    }

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
        int32_t parsed = 0;
        if (nmo_parse_i32_range(argv[2], 0, 3, &parsed) != NMO_OK) {
            fprintf(stderr, "Usage: dump [index] [level 0-3]\n");
            return -1;
        }
        level = parsed;
    }

    size_t object_count = nmo_repl_object_count(repl);
    nmo_object_t *obj = nmo_repl_object_at(repl, index);

    if (index >= object_count || !obj) {
        fprintf(stderr, "Error: Index %zu out of range (0-%zu)\n", index, object_count ? object_count - 1 : 0);
        return -1;
    }

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

static int repl_query_build_find(
    nmo_repl_context_t *repl,
    const nmo_cmd_ctx_t *c,
    int argc,
    char **argv,
    nmo_object_query_t *query,
    char *regex_buf,
    size_t regex_buf_size)
{
    const char *token = argv[1];

    if (strcmp(token, "class") == 0 && argc > 2) {
        return repl_query_set_exact_class(c, query, argv[2], false);
    }

    if (strcmp(token, "id") == 0 && argc > 2) {
        uint32_t id_val = 0;
        if (nmo_repl_parse_u32(argv[2], &id_val)) {
            nmo_core_query_set_object_id(query, (nmo_object_id_t)id_val);
        }
        return 0;
    }

    size_t len = strlen(token);
    if (len >= 2 && token[0] == '/' && token[len - 1] == '/') {
        len -= 2;
        if (len >= regex_buf_size) {
            len = regex_buf_size - 1;
        }
        memcpy(regex_buf, token + 1, len);
        regex_buf[len] = '\0';
        query->name = regex_buf;
        query->name_mode = NMO_OBJECT_QUERY_NAME_REGEX;
        query->name_case_insensitive = repl->regex_icase;
    } else {
        query->name = token;
        query->name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING;
        query->name_case_insensitive = false;
    }

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
    char regex_buf[256];
    if (repl_query_build_find(
            repl, &c, argc, argv, &query, regex_buf, sizeof(regex_buf)) != 0) {
        return -1;
    }

    printf("\nMatches:\n");

    repl_find_data_t fd = { .repl = repl, .found = 0 };
    nmo_core_object_query_run(&c, &query, repl_find_visitor, &fd, NULL);

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

    size_t object_count = nmo_repl_object_count(repl);
    nmo_object_t *obj = nmo_repl_object_at(repl, index);

    if (index >= object_count || !obj) {
        fprintf(stderr, "Error: Index %zu out of range\n", index);
        return -1;
    }

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

    size_t object_count = nmo_repl_object_count(repl);
    nmo_object_t *obj = nmo_repl_object_at(repl, index);

    if (index >= object_count || !obj) {
        fprintf(stderr, "Error: Index %zu out of range\n", index);
        return -1;
    }

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

    size_t object_count = nmo_repl_object_count(repl);
    nmo_object_t *obj = nmo_repl_object_at(repl, index);

    if (index >= object_count || !obj) {
        fprintf(stderr, "Error: Index %zu out of range\n", index);
        return -1;
    }

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

    int rc = nmo_cli_save_session(repl->session, output_path, &opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
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

    size_t object_count = nmo_repl_object_count(repl);

    size_t errors = 0;
    size_t checked = 0;
    printf("\nVerifying chunks...\n");

    for (size_t i = 0; i < object_count; ++i) {
        if (!verify_all && i != index) {
            continue;
        }

        nmo_object_t *obj = nmo_repl_object_at(repl, i);
        if (!obj) {
            continue;
        }
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

    size_t object_count = nmo_repl_object_count(repl);
    nmo_object_t *obj = nmo_repl_object_at(repl, index);

    if (index >= object_count || !obj) {
        fprintf(stderr, "Error: Index %zu out of range (0-%zu)\n", index, object_count ? object_count - 1 : 0);
        return -1;
    }

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

    size_t object_count = nmo_repl_object_count(repl);

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

        nmo_object_t *obj = nmo_repl_object_at(repl, i);
        if (!obj) {
            continue;
        }
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
        int32_t level = 0;
        if (nmo_parse_i32_range(argv[2], 0, 3, &level) != NMO_OK) {
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
 * CLI read mirror and mutation commands
 * ============================================================================ */

static bool repl_streq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static bool repl_has_token(int argc, char **argv, const char *token) {
    for (int i = 0; i < argc; i++) {
        if (repl_streq(argv[i], token)) {
            return true;
        }
    }
    return false;
}

static bool repl_token_has_suffix_ci(const char *token, const char *suffix) {
    if (!token || !suffix) {
        return false;
    }
    size_t token_len = strlen(token);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > token_len) {
        return false;
    }

    const char *tail = token + token_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        unsigned char a = (unsigned char)tail[i];
        unsigned char b = (unsigned char)suffix[i];
        if ((char)tolower(a) != (char)tolower(b)) {
            return false;
        }
    }
    return true;
}

static bool repl_token_looks_like_session_file(const char *token) {
    return repl_token_has_suffix_ci(token, ".nmo") ||
           repl_token_has_suffix_ci(token, ".cmo") ||
           repl_token_has_suffix_ci(token, ".vmo");
}

static bool repl_cli_option_takes_value(const char *token) {
    static const char *value_options[] = {
        "--class", "-c",
        "--depth",
        "--format",
        "--from",
        "--id", "-i",
        "--index",
        "--kind",
        "--max-bytes", "-m",
        "--name", "-n",
        "--object",
        "--out-dir", "-d",
        "--owner",
        "--row",
        "--select", "-s",
        "--sort",
        "--top", "-t",
        "--type",
        "--value",
    };

    if (!token || token[0] != '-') {
        return false;
    }
    for (size_t i = 0; i < sizeof(value_options) / sizeof(value_options[0]); i++) {
        if (repl_streq(token, value_options[i])) {
            return true;
        }
    }
    return false;
}

static bool repl_has_explicit_session_file_operand(int argc, char **argv) {
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strchr(argv[i], '=') != NULL) {
                continue;
            }
            if (repl_cli_option_takes_value(argv[i]) && i + 1 < argc) {
                i++;
            }
            continue;
        }
        if (repl_token_looks_like_session_file(argv[i])) {
            return true;
        }
    }
    return false;
}

static const nmo_cli_group_t *repl_find_cli_group(const char *group)
{
    return nmo_command_registry_find_group(group, false);
}

static const nmo_cli_action_t *repl_find_cli_read_action(const char *group,
                                                         const char *action)
{
    const nmo_cli_group_t *entry_group = repl_find_cli_group(group);
    if (!entry_group) {
        return NULL;
    }
    const nmo_cli_action_t *entry =
        nmo_command_registry_find_action(entry_group, action, true);
    if (!entry) {
        return NULL;
    }
    return (entry->repl_policy == NMO_REPL_ACTION_READ_SESSION ||
            entry->repl_policy == NMO_REPL_ACTION_READ_NO_SESSION)
        ? entry
        : NULL;
}

static bool repl_resource_info_has_selector(int argc, char **argv) {
    for (int i = 2; i < argc; i++) {
        if (repl_streq(argv[i], "--index") || repl_streq(argv[i], "-i") ||
            repl_streq(argv[i], "--name") || repl_streq(argv[i], "-n") ||
            strncmp(argv[i], "--index=", 8) == 0 ||
            strncmp(argv[i], "--name=", 7) == 0) {
            return true;
        }
    }
    return false;
}

static const char *repl_cli_source_label(nmo_repl_context_t *repl) {
    if (!repl || repl->dirty || !repl->filename || !repl->filename[0]) {
        return "(current session)";
    }
    return repl->filename;
}

static int repl_dispatch_cli_read_group(nmo_repl_context_t *repl, int argc, char **argv,
                                        const nmo_cli_global_opts_t *global) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <cli-read-action> ...\n", argc > 0 ? argv[0] : "(command)");
        return -1;
    }

    const char *group = argv[0];
    const nmo_cli_group_t *entry_group = repl_find_cli_group(group);
    const nmo_cli_action_t *action = repl_find_cli_read_action(group, argv[1]);
    if (!action) {
        fprintf(stderr, "Unsupported or mutating CLI action in REPL read mirror: %s %s\n",
                group, argc > 1 ? argv[1] : "");
        return -1;
    }

    if (repl_streq(group, "validate") &&
        (repl_has_token(argc, argv, "--strip") || repl_has_token(argc, argv, "-o") ||
         repl_has_token(argc, argv, "--output"))) {
        fprintf(stderr, "Validation write/fix options are not supported in REPL read mirror.\n");
        return -1;
    }
    if (repl_streq(group, "resource") && repl_streq(action->name, "info") &&
        !repl_resource_info_has_selector(argc, argv)) {
        fprintf(stderr, "resource info in REPL requires --index or --name; use the CLI for external-file sniffing.\n");
        return -1;
    }
    if (repl_streq(group, "behavior") && repl_streq(action->name, "interface") &&
        argc >= 3 && !repl_streq(argv[2], "show") && !repl_streq(argv[2], "s") &&
        argv[2][0] != '-' && !(argv[2][0] >= '0' && argv[2][0] <= '9')) {
        fprintf(stderr, "Unsupported or mutating CLI action in REPL read mirror: behavior interface %s\n", argv[2]);
        return -1;
    }
    bool needs_session = action->repl_policy == NMO_REPL_ACTION_READ_SESSION;
    if (needs_session && (!repl || !repl->session)) {
        fprintf(stderr, "No session loaded.\n");
        return -1;
    }
    if (!repl_streq(group, "diff") && needs_session &&
        repl_has_explicit_session_file_operand(argc, argv)) {
        fprintf(stderr, "File operands are not accepted in REPL CLI read mirror; use the current session.\n");
        return -1;
    }

    nmo_cli_global_opts_t local_global;
    if (global) {
        local_global = *global;
    } else {
        nmo_cli_global_opts_init(&local_global);
        local_global.color_mode = repl && repl->colorize ? NMO_CLI_COLOR_ALWAYS : NMO_CLI_COLOR_NEVER;
    }

    const char *source_label = repl_cli_source_label(repl);

    if (needs_session) {
        nmo_cmd_ctx_t cmd;
        int init_rc = nmo_cmd_ctx_init_with_session(&cmd, repl->ctx,
                                                    repl->session,
                                                    source_label,
                                                    &local_global);
        if (init_rc != NMO_CLI_EXIT_SUCCESS) {
            return -1;
        }

        int rc = nmo_command_registry_dispatch_read_in_session(
            entry_group, action, &cmd, argc - 1, &argv[1]);
        rc = nmo_cmd_ctx_done(&cmd, rc);
        return rc == NMO_CLI_EXIT_SUCCESS ? 0 : -1;
    }

    int rc = action->handler(argc - 1, &argv[1], &local_global);

    return rc == NMO_CLI_EXIT_SUCCESS ? 0 : -1;
}

static int cmd_cli_read(nmo_repl_context_t *repl, int argc, char **argv) {
    return repl_dispatch_cli_read_group(repl, argc, argv, NULL);
}

static int cmd_cli(nmo_repl_context_t *repl, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: cli [global-options] <group> <action> ...\n");
        return -1;
    }

    nmo_cli_global_opts_t global;
    int first = nmo_cli_parse_global_opts(argc, argv, &global);
    if (first < 0) {
        return -1;
    }
    if (global.batch_mode) {
        fprintf(stderr, "Batch mode is not supported in REPL CLI read mirror; use the current session commands without --batch.\n");
        return -1;
    }
    if (global.color_mode == NMO_CLI_COLOR_AUTO) {
        global.color_mode = repl && repl->colorize ? NMO_CLI_COLOR_ALWAYS : NMO_CLI_COLOR_NEVER;
    }
    if (first >= argc) {
        fprintf(stderr, "Usage: cli [global-options] <group> <action> ...\n");
        return -1;
    }

    return repl_dispatch_cli_read_group(repl, argc - first, argv + first, &global);
}

typedef int (*repl_mutation_dispatcher_t)(nmo_cmd_ctx_t *ctx,
                                          const nmo_cli_action_t *action,
                                          int argc,
                                          char **argv,
                                          nmo_cmd_in_session_result_t *result,
                                          bool *clear_selection,
                                          bool *invalidate_name_cache);

static int repl_dispatch_registry_grouped_command(nmo_repl_context_t *repl,
                                                  const char *group_name,
                                                  const char *usage,
                                                  int argc,
                                                  char **argv,
                                                  repl_mutation_dispatcher_t mutate)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s\n", usage);
        return -1;
    }

    const nmo_cli_group_t *group = repl_find_cli_group(group_name);
    const nmo_cli_action_t *action =
        group ? nmo_command_registry_find_action(group, argv[1], true) : NULL;
    if (!action) {
        fprintf(stderr, "Unknown %s command: %s\n", group_name, argv[1]);
        fprintf(stderr, "Usage: %s\n", usage);
        return -1;
    }

    if (action->repl_policy == NMO_REPL_ACTION_READ_SESSION ||
        action->repl_policy == NMO_REPL_ACTION_READ_NO_SESSION) {
        return repl_dispatch_cli_read_group(repl, argc, argv, NULL);
    }

    if (action->repl_policy != NMO_REPL_ACTION_MUTATE_SESSION_SUPPORTED || !mutate) {
        fprintf(stderr, "Unsupported or mutating CLI action in REPL read mirror: %s %s\n",
                group_name, argv[1]);
        return -1;
    }

    if (!repl->session) {
        fprintf(stderr, "No session loaded.\n");
        return -1;
    }

    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl(&c, repl->ctx, repl->session, repl->colorize);

    nmo_cmd_in_session_result_t result = {0};
    bool clear_selection = false;
    bool invalidate_name_cache = false;
    int rc = mutate(&c, action, argc - 1, &argv[1], &result,
                    &clear_selection, &invalidate_name_cache);
    rc = nmo_cmd_ctx_done(&c, rc);

    if (rc == NMO_CLI_EXIT_SUCCESS && result.changed) {
        repl->dirty = true;
        if (clear_selection) {
            repl->has_selection = false;
        }
        if (invalidate_name_cache) {
            nmo_repl_input_invalidate_name_cache(repl);
        }
    }

    return rc == NMO_CLI_EXIT_SUCCESS ? 0 : -1;
}

static int repl_dispatch_object_mutation(nmo_cmd_ctx_t *ctx,
                                         const nmo_cli_action_t *action,
                                         int argc,
                                         char **argv,
                                         nmo_cmd_in_session_result_t *result,
                                         bool *clear_selection,
                                         bool *invalidate_name_cache)
{
    if (invalidate_name_cache) {
        *invalidate_name_cache = true;
    }
    if (strcmp(action->name, "rename") == 0) {
        return nmo_cmd_object_rename_in_session(ctx, argc, argv, result);
    }
    if (strcmp(action->name, "delete") == 0) {
        if (clear_selection) {
            *clear_selection = true;
        }
        return nmo_cmd_object_delete_in_session(ctx, argc, argv, result);
    }
    if (strcmp(action->name, "create") == 0) {
        return nmo_cmd_object_create_in_session(ctx, argc, argv, result);
    }
    if (strcmp(action->name, "copy") == 0) {
        return nmo_cmd_object_copy_in_session(ctx, argc, argv, result);
    }

    fprintf(stderr, "Unsupported object mutation in REPL: %s\n", action->name);
    return NMO_CLI_EXIT_ARG_ERROR;
}

static int repl_dispatch_parameter_mutation(nmo_cmd_ctx_t *ctx,
                                            const nmo_cli_action_t *action,
                                            int argc,
                                            char **argv,
                                            nmo_cmd_in_session_result_t *result,
                                            bool *clear_selection,
                                            bool *invalidate_name_cache)
{
    (void)clear_selection;
    (void)invalidate_name_cache;
    if (strcmp(action->name, "set") == 0) {
        return nmo_cmd_parameter_set_in_session(ctx, argc, argv, result);
    }

    fprintf(stderr, "Unsupported parameter mutation in REPL: %s\n", action->name);
    return NMO_CLI_EXIT_ARG_ERROR;
}

static int cmd_object(nmo_repl_context_t *repl, int argc, char **argv) {
    return repl_dispatch_registry_grouped_command(
        repl, "object", "object <cli-read-action>|rename|delete|create|copy ...",
        argc, argv, repl_dispatch_object_mutation);
}

static int cmd_parameter(nmo_repl_context_t *repl, int argc, char **argv) {
    return repl_dispatch_registry_grouped_command(
        repl, "parameter", "parameter list|show|dump|set ...",
        argc, argv, repl_dispatch_parameter_mutation);
}
