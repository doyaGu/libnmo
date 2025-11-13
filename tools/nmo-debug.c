/**
 * @file nmo-debug.c
 * @brief Interactive debugging tool for NMO files
 * 
 * Provides a REPL interface for exploring and debugging NMO files:
 * - Interactive object browsing
 * - Chunk inspection
 * - Object search and filtering
 * - Reference tracing
 * - Real-time validation
 */

#include "nmo.h"
#include "app/nmo_stats.h"
#include "app/nmo_inspector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_CMD_LEN 256
#define MAX_ARGS 16

/* Global context for REPL */
typedef struct {
    nmo_context_t *ctx;
    nmo_session_t *session;
    const char *filename;
    bool colorize;
    nmo_dump_level_t dump_level;
} debug_context_t;

/* Command handler function pointer */
typedef int (*command_handler_t)(debug_context_t *dbg, int argc, char **argv);

/* Command definition */
typedef struct {
    const char *name;
    const char *alias;
    const char *help;
    command_handler_t handler;
} command_t;

/* Forward declarations */
static int cmd_help(debug_context_t *dbg, int argc, char **argv);
static int cmd_list(debug_context_t *dbg, int argc, char **argv);
static int cmd_show(debug_context_t *dbg, int argc, char **argv);
static int cmd_dump(debug_context_t *dbg, int argc, char **argv);
static int cmd_find(debug_context_t *dbg, int argc, char **argv);
static int cmd_trace(debug_context_t *dbg, int argc, char **argv);
static int cmd_verify(debug_context_t *dbg, int argc, char **argv);
static int cmd_stats(debug_context_t *dbg, int argc, char **argv);
static int cmd_set(debug_context_t *dbg, int argc, char **argv);
static int cmd_quit(debug_context_t *dbg, int argc, char **argv);

/* Command table */
static const command_t commands[] = {
    { "help", "h", "Show this help message", cmd_help },
    { "list", "ls", "List objects [class_id]", cmd_list },
    { "show", "s", "Show object details <index>", cmd_show },
    { "dump", "d", "Dump chunk <index> [level]", cmd_dump },
    { "find", "f", "Find objects by-name <name> | by-class <id>", cmd_find },
    { "trace", "t", "Trace references <index>", cmd_trace },
    { "verify", "v", "Verify all chunks or chunk <index>", cmd_verify },
    { "stats", "st", "Show file statistics", cmd_stats },
    { "set", "", "Set option: color on|off, level 0-3", cmd_set },
    { "quit", "q", "Exit debugger", cmd_quit },
    { NULL, NULL, NULL, NULL }
};

/* Print prompt */
static void print_prompt(void) {
    printf("nmo-debug> ");
    fflush(stdout);
}

/* Parse command line into argc/argv */
static int parse_command(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    bool in_quotes = false;
    char *arg_start = NULL;
    
    while (*p && argc < max_args) {
        if (isspace(*p) && !in_quotes) {
            if (arg_start) {
                *p = '\0';
                argv[argc++] = arg_start;
                arg_start = NULL;
            }
        } else if (*p == '"') {
            if (!in_quotes) {
                in_quotes = true;
                arg_start = p + 1;
            } else {
                *p = '\0';
                argv[argc++] = arg_start;
                arg_start = NULL;
                in_quotes = false;
            }
        } else if (!arg_start) {
            arg_start = p;
        }
        p++;
    }
    
    if (arg_start && argc < max_args) {
        argv[argc++] = arg_start;
    }
    
    return argc;
}

/* Find command by name or alias */
static const command_t *find_command(const char *name) {
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(commands[i].name, name) == 0 ||
            (commands[i].alias[0] && strcmp(commands[i].alias, name) == 0)) {
            return &commands[i];
        }
    }
    return NULL;
}

/* Command implementations */

static int cmd_help(debug_context_t *dbg, int argc, char **argv) {
    (void)dbg; (void)argc; (void)argv;
    
    printf("\nAvailable commands:\n");
    for (int i = 0; commands[i].name != NULL; i++) {
        printf("  %-12s", commands[i].name);
        if (commands[i].alias[0]) {
            printf("(%s)  ", commands[i].alias);
        } else {
            printf("     ");
        }
        printf("%s\n", commands[i].help);
    }
    printf("\n");
    return 0;
}

static int cmd_list(debug_context_t *dbg, int argc, char **argv) {
    nmo_class_id_t filter = -1;
    
    if (argc > 1) {
        filter = (nmo_class_id_t)atoi(argv[1]);
    }
    
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(dbg->session, &objects, &object_count);
    
    printf("\nObjects:\n");
    size_t displayed = 0;
    
    for (size_t i = 0; i < object_count; i++) {
        nmo_class_id_t class_id = nmo_object_get_class_id(objects[i]);
        
        if (filter != -1 && class_id != (nmo_class_id_t)filter) {
            continue;
        }
        
        nmo_object_id_t obj_id = nmo_object_get_id(objects[i]);
        const char *name = nmo_object_get_name(objects[i]);
        
        printf("  [%3zu] ID=%-5u Class=%-3d %s\n",
               i, obj_id, class_id, name ? name : "(unnamed)");
        
        displayed++;
    }
    
    if (filter != -1) {
        printf("\n%zu/%zu objects (class %d)\n", displayed, object_count, filter);
    } else {
        printf("\n%zu objects total\n", displayed);
    }
    
    return 0;
}

static int cmd_show(debug_context_t *dbg, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: show <index>\n");
        return -1;
    }
    
    size_t index = (size_t)atoi(argv[1]);
    
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(dbg->session, &objects, &object_count);
    
    if (index >= object_count) {
        fprintf(stderr, "Error: Index %zu out of range (0-%zu)\n", 
                index, object_count - 1);
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
        printf("  Compressed: %s\n", 
               nmo_chunk_is_compressed(chunk) ? "yes" : "no");
        printf("  ID count: %zu\n", nmo_chunk_get_id_count(chunk));
        printf("  Sub-chunks: %u\n", nmo_chunk_get_sub_chunk_count(chunk));
    } else {
        printf("  (No chunk data)\n");
    }
    
    return 0;
}

static int cmd_dump(debug_context_t *dbg, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: dump <index> [level]\n");
        fprintf(stderr, "  level: 0=minimal, 1=normal, 2=detailed, 3=debug\n");
        return -1;
    }
    
    size_t index = (size_t)atoi(argv[1]);
    nmo_dump_level_t level = dbg->dump_level;
    
    if (argc > 2) {
        level = (nmo_dump_level_t)atoi(argv[2]);
    }
    
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(dbg->session, &objects, &object_count);
    
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

static int cmd_find(debug_context_t *dbg, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: find by-name <name> | by-class <class_id>\n");
        return -1;
    }
    
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(dbg->session, &objects, &object_count);
    
    if (strcmp(argv[1], "by-name") == 0) {
        const char *search = argv[2];
        printf("\nSearching for name containing '%s':\n", search);
        
        size_t found = 0;
        for (size_t i = 0; i < object_count; i++) {
            const char *name = nmo_object_get_name(objects[i]);
            if (name && strstr(name, search)) {
                printf("  [%3zu] %s\n", i, name);
                found++;
            }
        }
        printf("\nFound %zu match(es)\n", found);
        
    } else if (strcmp(argv[1], "by-class") == 0) {
        /* Note: class_id parsed but not used directly, passed to cmd_list */
        (void)atoi(argv[2]);  /* Parse to validate format */
        return cmd_list(dbg, 2, argv + 1);  /* Reuse list with filter */
        
    } else {
        fprintf(stderr, "Unknown find mode: %s\n", argv[1]);
        return -1;
    }
    
    return 0;
}

static int cmd_trace(debug_context_t *dbg, int argc, char **argv) {
    (void)dbg;
    (void)argv;  /* Suppress unused warning */
    
    if (argc < 2) {
        fprintf(stderr, "Usage: trace <index>\n");
        return -1;
    }
    
    printf("(Reference tracing requires Phase 4 completion)\n");
    return 0;
}

static int cmd_verify(debug_context_t *dbg, int argc, char **argv) {
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(dbg->session, &objects, &object_count);
    
    if (argc > 1 && strcmp(argv[1], "all") != 0) {
        /* Verify single chunk */
        size_t index = (size_t)atoi(argv[1]);
        
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
            printf("[ERROR] Chunk [%zu] is invalid: %s\n", 
                   index, result.error_message);
        }
        
    } else {
        /* Verify all chunks */
        printf("\nVerifying %zu chunks...\n", object_count);
        
        int errors = 0;
        for (size_t i = 0; i < object_count; i++) {
            nmo_chunk_t *chunk = nmo_object_get_chunk(objects[i]);
            if (chunk == NULL) continue;
            
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

static int cmd_stats(debug_context_t *dbg, int argc, char **argv) {
    (void)argc; (void)argv;
    
    nmo_file_stats_t stats;
    if (nmo_stats_collect(dbg->session, &stats) != 0) {
        fprintf(stderr, "Error: Failed to collect statistics\n");
        return -1;
    }
    
    printf("\n");
    nmo_stats_print(&stats, stdout);
    
    return 0;
}

static int cmd_set(debug_context_t *dbg, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: set <option> <value>\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  color on|off      - Enable/disable ANSI colors\n");
        fprintf(stderr, "  level 0-3         - Set dump detail level\n");
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
        
    } else {
        fprintf(stderr, "Unknown option: %s\n", argv[1]);
        return -1;
    }
    
    return 0;
}

static int cmd_quit(debug_context_t *dbg, int argc, char **argv) {
    (void)dbg; (void)argc; (void)argv;
    return 1;  /* Signal to exit REPL */
}

/* REPL main loop */
static void repl_loop(debug_context_t *dbg) {
    char line[MAX_CMD_LEN];
    char *argv[MAX_ARGS];
    
    printf("\nNMO Interactive Debugger\n");
    printf("Type 'help' for available commands, 'quit' to exit\n");
    
    while (true) {
        print_prompt();
        
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;  /* EOF */
        }
        
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        /* Skip empty lines */
        if (line[0] == '\0') {
            continue;
        }
        
        /* Parse command */
        int argc = parse_command(line, argv, MAX_ARGS);
        if (argc == 0) {
            continue;
        }
        
        /* Find and execute command */
        const command_t *cmd = find_command(argv[0]);
        if (cmd == NULL) {
            fprintf(stderr, "Unknown command: %s (type 'help' for list)\n", 
                    argv[0]);
            continue;
        }
        
        int result = cmd->handler(dbg, argc, argv);
        if (result > 0) {
            break;  /* Quit requested */
        }
    }
    
    printf("\nGoodbye!\n");
}

/* Main entry point */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.nmo>\n", argv[0]);
        fprintf(stderr, "\nInteractive debugger for NMO files\n");
        return 1;
    }
    
    const char *filename = argv[1];
    
    /* Initialize debug context */
    debug_context_t dbg;
    memset(&dbg, 0, sizeof(dbg));
    dbg.filename = filename;
    dbg.colorize = true;
    dbg.dump_level = NMO_DUMP_NORMAL;
    
    /* Create context */
    dbg.ctx = nmo_context_create(NULL);
    if (dbg.ctx == NULL) {
        fprintf(stderr, "Error: Failed to create context\n");
        return 1;
    }
    
    /* Load file */
    printf("Loading: %s\n", filename);
    dbg.session = nmo_session_load(dbg.ctx, filename);
    
    if (dbg.session == NULL) {
        fprintf(stderr, "Error: Failed to load file\n");
        nmo_context_destroy(dbg.ctx);
        return 1;
    }
    
    /* Show basic info */
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(dbg.session, &objects, &object_count);
    
    printf("Loaded %zu objects\n", object_count);
    
    /* Enter REPL */
    repl_loop(&dbg);
    
    /* Cleanup */
    nmo_session_destroy(dbg.session);
    nmo_context_destroy(dbg.ctx);
    
    return 0;
}
