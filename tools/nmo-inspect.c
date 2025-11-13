/**
 * @file nmo-inspect.c
 * @brief CLI tool to inspect NMO file contents
 *
 * Usage: nmo-inspect [options] <file.nmo>
 */

#include "nmo.h"
#include "app/nmo_stats.h"
#include "app/nmo_inspector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    const char *filename;
    bool show_stats;
    bool verify;
    bool dump_chunks;
    bool show_references;
    bool export_json;
    const char *json_output;
    nmo_class_id_t class_filter;
    nmo_dump_level_t dump_level;
} inspect_options_t;

static void print_usage(const char *prog_name) {
    printf("Usage: %s [options] <file.nmo>\n", prog_name);
    printf("\nOptions:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -s, --stats             Show file statistics\n");
    printf("  -v, --verify            Verify chunk integrity\n");
    printf("  -d, --dump-chunks       Dump chunk structures\n");
    printf("  -r, --show-references   Show object references\n");
    printf("  -j, --json <file>       Export to JSON file\n");
    printf("  -c, --class <id>        Filter by class ID\n");
    printf("  -l, --level <0-3>       Dump detail level (0=brief, 3=full)\n");
    printf("\n");
}

static bool parse_options(int argc, char *argv[], inspect_options_t *opts) {
    memset(opts, 0, sizeof(inspect_options_t));
    opts->class_filter = NMO_CLASS_ID_INVALID;
    opts->dump_level = NMO_DUMP_NORMAL;
    
    int i = 1;
    while (i < argc) {
        const char *arg = argv[i];
        
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            return false;
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--stats") == 0) {
            opts->show_stats = true;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verify") == 0) {
            opts->verify = true;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--dump-chunks") == 0) {
            opts->dump_chunks = true;
        } else if (strcmp(arg, "-r") == 0 || strcmp(arg, "--show-references") == 0) {
            opts->show_references = true;
        } else if (strcmp(arg, "-j") == 0 || strcmp(arg, "--json") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --json requires an argument\n");
                return false;
            }
            opts->export_json = true;
            opts->json_output = argv[++i];
        } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "--class") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --class requires an argument\n");
                return false;
            }
            opts->class_filter = (nmo_class_id_t)atoi(argv[++i]);
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--level") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --level requires an argument\n");
                return false;
            }
            int level = atoi(argv[++i]);
            if (level < 0 || level > 3) {
                fprintf(stderr, "Error: level must be 0-3\n");
                return false;
            }
            opts->dump_level = (nmo_dump_level_t)level;
        } else if (arg[0] != '-') {
            opts->filename = arg;
            break;
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", arg);
            return false;
        }
        i++;
    }
    
    if (opts->filename == NULL) {
        fprintf(stderr, "Error: No input file specified\n");
        return false;
    }
    
    return true;
}

int main(int argc, char *argv[]) {
    inspect_options_t opts;
    
    if (!parse_options(argc, argv, &opts)) {
        print_usage(argv[0]);
        return 1;
    }

    const char *filename = opts.filename;

    // Create context
    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        fprintf(stderr, "Error: Failed to create context\n");
        return 1;
    }

    // Register built-in schemas
    nmo_schema_registry_t *registry = nmo_context_get_schema_registry(ctx);
    if (registry == NULL) {
        fprintf(stderr, "Error: Failed to get schema registry\n");
        nmo_context_release(ctx);
        return 1;
    }

    // Create session
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }

    // Load file
    printf("Inspecting file: %s\n", filename);
    nmo_result_t result = nmo_load_file(session, filename, NMO_LOAD_DEFAULT);

    if (result.code != NMO_OK) {
        if (result.error != NULL) {
            fprintf(stderr, "Error: %s\n", nmo_error_message(result.error));
            nmo_error_destroy(result.error);
        } else {
            fprintf(stderr, "Error: Failed to load file (error code %d)\n",
                    result.code);
        }
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }

    printf("File loaded successfully\n\n");

    // Get object repository
    nmo_object_repository_t *repo = nmo_session_get_object_repository(session);
    if (repo == NULL) {
        fprintf(stderr, "Error: Failed to get object repository\n");
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }
    
    size_t object_count = 0;
    const nmo_object_t *const *objects = nmo_object_repository_get_all(repo, &object_count);
    
    printf("=== Basic Information ===\n");
    printf("Total Objects: %zu\n\n", object_count);
    
    // Show statistics
    if (opts.show_stats) {
        printf("=== File Statistics ===\n");
        nmo_file_stats_t stats;
        if (nmo_stats_collect(session, &stats) == 0) {
            nmo_stats_print(&stats, stdout);
            printf("\n");
        } else {
            fprintf(stderr, "Warning: Failed to collect statistics\n");
        }
    }
    
    // List objects with optional class filter
    printf("=== Objects ===\n");
    for (size_t i = 0; i < object_count; i++) {
        nmo_class_id_t class_id = objects[i]->class_id;
        
        // Apply class filter
        if (opts.class_filter != NMO_CLASS_ID_INVALID && 
            class_id != opts.class_filter) {
            continue;
        }
        
        const char *name = nmo_object_get_name(objects[i]);
        nmo_object_id_t obj_id = nmo_object_get_id(objects[i]);
        
        printf("  [%zu] ID=%d Class=%d Name=\"%s\"\n", 
               i, obj_id, class_id, name ? name : "(unnamed)");
        
        // Show references if requested
        if (opts.show_references) {
            /* TODO: Implement reference display when API available */
        }
    }
    printf("\n");
    
    // Dump chunks
    if (opts.dump_chunks) {
        printf("=== Chunk Structures ===\n");
        
        nmo_inspector_options_t inspector_opts;
        nmo_inspector_init_options(&inspector_opts);
        inspector_opts.level = opts.dump_level;
        inspector_opts.show_sub_chunks = true;
        inspector_opts.colorize = true;  /* Enable colors for terminal */
        
        for (size_t i = 0; i < object_count; i++) {
            // Apply class filter
            nmo_class_id_t class_id = objects[i]->class_id;
            if (opts.class_filter != NMO_CLASS_ID_INVALID && 
                class_id != opts.class_filter) {
                continue;
            }
            
            const char *name = nmo_object_get_name(objects[i]);
            printf("\nObject: %s (Class=%d)\n", 
                   name ? name : "(unnamed)", class_id);
            
            const nmo_chunk_t *chunk = nmo_object_get_chunk(objects[i]);
            if (chunk) {
                nmo_inspector_dump_chunk(chunk, stdout, &inspector_opts);
            } else {
                printf("  (no chunk data)\n");
            }
        }
        printf("\n");
    }
    
    // Verify chunks
    if (opts.verify) {
        printf("=== Chunk Verification ===\n");
        size_t error_count = 0;
        size_t warning_count = 0;
        
        for (size_t i = 0; i < object_count; i++) {
            // Apply class filter
            nmo_class_id_t class_id = objects[i]->class_id;
            if (opts.class_filter != NMO_CLASS_ID_INVALID && 
                class_id != opts.class_filter) {
                continue;
            }
            
            const nmo_chunk_t *chunk = nmo_object_get_chunk(objects[i]);
            if (chunk) {
                nmo_chunk_validation_t validation;
                if (nmo_inspector_validate_chunk(chunk, &validation) == 0) {
                    if (!validation.is_valid) {
                        const char *name = nmo_object_get_name(objects[i]);
                        printf("  [%zu] %s: FAILED - %s\n", 
                               i, name ? name : "(unnamed)", 
                               validation.error_message);
                        error_count += validation.error_count;
                        warning_count += validation.warning_count;
                    }
                }
            }
        }
        
        if (error_count == 0 && warning_count == 0) {
            printf("  All chunks valid!\n");
        } else {
            printf("  Total: %zu errors, %zu warnings\n", error_count, warning_count);
        }
        printf("\n");
    }
    
    // Export to JSON
    if (opts.export_json) {
        printf("=== Exporting to JSON ===\n");
        
        nmo_file_stats_t stats;
        if (nmo_stats_collect(session, &stats) == 0) {
            if (nmo_stats_export_json(&stats, opts.json_output) == 0) {
                printf("  Exported to: %s\n", opts.json_output);
            } else {
                fprintf(stderr, "  Error: Failed to export JSON\n");
            }
        }
        printf("\n");
    }

    // Clean up
    nmo_session_destroy(session);
    nmo_context_release(ctx);

    printf("Inspection complete.\n");
    return 0;
}

