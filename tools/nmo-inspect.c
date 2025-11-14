/**
 * @file nmo-inspect.c
 * @brief Command-line tool for inspecting NMO files
 * 
 * Provides comprehensive inspection capabilities including:
 * - File statistics and metrics
 * - Object listing and filtering
 * - Chunk inspection and validation
 * - Reference analysis
 * - JSON export
 */

#include "nmo.h"
#include "app/nmo_stats.h"
#include "app/nmo_inspector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Command-line options */
typedef struct {
    const char *input_file;
    bool show_stats;
    bool show_objects;
    bool verify;
    bool dump_chunks;
    bool show_references;
    nmo_class_id_t class_filter;  /* -1 = no filter */
    bool export_json;
    const char *json_output;
    nmo_dump_level_t dump_level;
    bool colorize;
    bool verbose;
} inspect_options_t;

/* Print usage information */
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] <input.nmo>\n\n", program_name);
    printf("Inspect and analyze NMO files\n\n");
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -s, --stats             Display detailed file statistics\n");
    printf("  -o, --objects           List all objects\n");
    printf("  -v, --verify            Verify file integrity\n");
    printf("  -d, --dump-chunks       Dump all chunks (use with -dd/-ddd for more detail)\n");
    printf("  -r, --show-references   Show object references\n");
    printf("  -c, --class-filter CID  Filter objects by class ID\n");
    printf("  -j, --export-json FILE  Export analysis to JSON\n");
    printf("  --no-color              Disable colored output\n");
    printf("  --verbose               Enable verbose output\n");
    printf("\nExamples:\n");
    printf("  %s file.nmo                    # Basic info\n", program_name);
    printf("  %s -s file.nmo                 # Show statistics\n", program_name);
    printf("  %s -o -c 2 file.nmo            # List objects of class 2\n", program_name);
    printf("  %s -ddd file.nmo               # Detailed chunk dump\n", program_name);
    printf("  %s -j output.json file.nmo     # Export to JSON\n", program_name);
}

/* Parse command-line arguments */
static int parse_args(int argc, char **argv, inspect_options_t *opts) {
    /* Initialize defaults */
    memset(opts, 0, sizeof(*opts));
    opts->class_filter = -1;
    opts->dump_level = NMO_DUMP_NORMAL;
    opts->colorize = true;
    
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return -1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stats") == 0) {
            opts->show_stats = true;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--objects") == 0) {
            opts->show_objects = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verify") == 0) {
            opts->verify = true;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dump-chunks") == 0) {
            opts->dump_chunks = true;
            opts->dump_level = NMO_DUMP_NORMAL;
        } else if (strcmp(argv[i], "-dd") == 0) {
            opts->dump_chunks = true;
            opts->dump_level = NMO_DUMP_DETAILED;
        } else if (strcmp(argv[i], "-ddd") == 0) {
            opts->dump_chunks = true;
            opts->dump_level = NMO_DUMP_FULL;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--show-references") == 0) {
            opts->show_references = true;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--class-filter") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --class-filter requires an argument\n");
                return -1;
            }
            opts->class_filter = (nmo_class_id_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--export-json") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --export-json requires an argument\n");
                return -1;
            }
            opts->export_json = true;
            opts->json_output = argv[++i];
        } else if (strcmp(argv[i], "--no-color") == 0) {
            opts->colorize = false;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            opts->verbose = true;
        } else if (argv[i][0] != '-') {
            if (opts->input_file != NULL) {
                fprintf(stderr, "Error: Multiple input files specified\n");
                return -1;
            }
            opts->input_file = argv[i];
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }
    
    if (opts->input_file == NULL) {
        fprintf(stderr, "Error: No input file specified\n");
        print_usage(argv[0]);
        return -1;
    }
    
    /* If no specific action requested, show basic info */
    if (!opts->show_stats && !opts->show_objects && !opts->verify && 
        !opts->dump_chunks && !opts->show_references && !opts->export_json) {
        opts->show_stats = true;
        opts->show_objects = true;
    }
    
    return 0;
}

/* Display basic file info */
static void show_basic_info(nmo_session_t *session, const inspect_options_t *opts) {
    printf("\n=== File Information ===\n");
    printf("File: %s\n", opts->input_file);
    
    /* Get object count */
    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_session_get_objects(session, &objects, &object_count);
    printf("Objects: %zu\n", object_count);

    uint32_t included_count = 0;
    nmo_included_file_t *included = nmo_session_get_included_files(session, &included_count);
    (void)included;
    printf("Included files: %u\n", included_count);
}

/* Display file statistics */
static void show_statistics(nmo_session_t *session) {
    printf("\n=== File Statistics ===\n");
    
    nmo_file_stats_t stats;
    int result = nmo_stats_collect(session, &stats);
    
    if (result != 0) {
        fprintf(stderr, "Warning: Failed to collect statistics\n");
        return;
    }
    
    nmo_stats_print(&stats, stdout);

    uint32_t included_count = 0;
    nmo_included_file_t *included = nmo_session_get_included_files(session, &included_count);
    if (included_count > 0 && included != NULL) {
        printf("\n--- Included Files ---\n");
        for (uint32_t i = 0; i < included_count; i++) {
            printf("  [%u] %s (%u bytes)\n", i,
                   included[i].name ? included[i].name : "(unnamed)",
                   included[i].size);
        }
    }
}

/* List objects with optional filtering */
static void list_objects(nmo_session_t *session, nmo_class_id_t class_filter) {
    printf("\n=== Objects ===\n");
    
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(session, &objects, &object_count);
    
    size_t displayed = 0;
    for (size_t i = 0; i < object_count; i++) {
        nmo_class_id_t class_id = nmo_object_get_class_id(objects[i]);
        
        /* Apply filter if specified */
        if (class_filter != -1 && class_id != (nmo_class_id_t)class_filter) {
            continue;
        }
        
        nmo_object_id_t obj_id = nmo_object_get_id(objects[i]);
        const char *name = nmo_object_get_name(objects[i]);
        
        printf("  [%zu] ID=%u Class=%d Name=\"%s\"\n",
               i, obj_id, class_id, name ? name : "(unnamed)");
        
        displayed++;
    }
    
    if (class_filter != -1) {
        printf("\nDisplayed %zu/%zu objects (class filter: %d)\n", 
               displayed, object_count, class_filter);
    } else {
        printf("\nTotal: %zu objects\n", displayed);
    }
}

/* Verify file integrity */
static int verify_file(nmo_session_t *session) {
    printf("\n=== Verification ===\n");
    
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(session, &objects, &object_count);
    
    int errors = 0;
    
    /* Verify each object's chunk */
    for (size_t i = 0; i < object_count; i++) {
        nmo_chunk_t *chunk = nmo_object_get_chunk(objects[i]);
        if (chunk == NULL) {
            printf("  [%zu] ERROR: Object has no chunk\n", i);
            errors++;
            continue;
        }
        
        nmo_chunk_validation_t result;
        int ret = nmo_inspector_validate_chunk(chunk, &result);
        
        if (ret != 0 || !result.is_valid) {
            printf("  [%zu] ERROR: %s\n", i, 
                   result.error_message[0] ? result.error_message : "Validation failed");
            errors++;
        }
    }
    
    if (errors == 0) {
        printf("[OK] All chunks validated successfully\n");
    } else {
        printf("[ERROR] Found %d error(s)\n", errors);
    }
    
    return errors;
}

/* Dump all chunks */
static void dump_chunks(nmo_session_t *session, const inspect_options_t *opts) {
    printf("\n=== Chunk Dump ===\n");
    
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(session, &objects, &object_count);
    
    nmo_inspector_options_t inspector_opts;
    nmo_inspector_init_options(&inspector_opts);
    inspector_opts.level = opts->dump_level;
    inspector_opts.colorize = opts->colorize;
    inspector_opts.show_hex = (opts->dump_level >= NMO_DUMP_DETAILED);
    inspector_opts.show_sub_chunks = true;
    
    for (size_t i = 0; i < object_count; i++) {
        nmo_chunk_t *chunk = nmo_object_get_chunk(objects[i]);
        if (chunk == NULL) {
            continue;
        }
        
        printf("\n--- Object [%zu] ---\n", i);
        nmo_inspector_dump_chunk(chunk, stdout, &inspector_opts);
    }
}

/* Show object references */
static void show_references(nmo_session_t *session) {
    printf("\n=== Object References ===\n");
    
    size_t object_count;
    nmo_object_t **objects;
    nmo_session_get_objects(session, &objects, &object_count);
    
    /* This is a placeholder - full reference tracking would need */
    /* additional API support in the reference resolver */
    printf("(Reference tracking requires Phase 4 completion)\n");
    printf("Total objects: %zu\n", object_count);
}

/* Export analysis to JSON */
static int export_json(nmo_session_t *session, const char *output_file) {
    printf("\n=== Exporting to JSON ===\n");
    
    nmo_file_stats_t stats;
    int result = nmo_stats_collect(session, &stats);
    
    if (result != 0) {
        fprintf(stderr, "Error: Failed to collect statistics\n");
        return -1;
    }
    
    result = nmo_stats_export_json(&stats, output_file);
    
    if (result == 0) {
        printf("[OK] Exported to: %s\n", output_file);
    } else {
        fprintf(stderr, "[FAILED] Export failed\n");
    }
    
    return result;
}

/* Main entry point */
int main(int argc, char **argv) {
    inspect_options_t opts;
    
    /* Parse arguments */
    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }
    
    /* Create context */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (ctx == NULL) {
        fprintf(stderr, "Error: Failed to create context\n");
        return 1;
    }
    
    /* Load file */
    if (opts.verbose) {
        printf("Loading file: %s\n", opts.input_file);
    }
    
    nmo_session_t *session = nmo_session_load(ctx, opts.input_file);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to load file: %s\n", opts.input_file);
        nmo_context_destroy(ctx);
        return 1;
    }
    
    /* Show basic info */
    show_basic_info(session, &opts);
    
    /* Execute requested actions */
    if (opts.show_stats) {
        show_statistics(session);
    }
    
    if (opts.show_objects) {
        list_objects(session, opts.class_filter);
    }
    
    int exit_code = 0;
    
    if (opts.verify) {
        int errors = verify_file(session);
        if (errors > 0) {
            exit_code = 1;
        }
    }
    
    if (opts.dump_chunks) {
        dump_chunks(session, &opts);
    }
    
    if (opts.show_references) {
        show_references(session);
    }
    
    if (opts.export_json) {
        if (export_json(session, opts.json_output) != 0) {
            exit_code = 1;
        }
    }
    
    /* Cleanup */
    nmo_session_destroy(session);
    nmo_context_destroy(ctx);
    
    return exit_code;
}
