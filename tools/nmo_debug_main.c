/**
 * nmo debug - Interactive debugging tool for NMO files
 */

#include "nmo_debug_repl.h"

#include "nmo.h"

#include "nmo_tool_session.h"

#include "nmo_debug_session.h"

#include <stdio.h>
#include <string.h>

int nmo_debug_run(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: nmo debug [options] <file.nmo>\n");
        fprintf(stderr, "\nInteractive debugger for NMO files\n");
        fprintf(stderr, "\nOptions:\n");
        fprintf(stderr, "  -h, --help     Show this help\n");
        fprintf(stderr, "  -v, --verbose  Show startup info (loading stats)\n");
        return 0;
    }

    bool verbose = false;
    int argi = 1;
    for (; argi < argc; ++argi) {
        const char *arg = argv[argi];
        if (!arg) {
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            argi++;
            break;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            fprintf(stderr, "Usage: nmo debug [options] <file.nmo>\n");
            fprintf(stderr, "\nInteractive debugger for NMO files\n");
            fprintf(stderr, "\nOptions:\n");
            fprintf(stderr, "  -h, --help     Show this help\n");
            fprintf(stderr, "  -v, --verbose  Show startup info (loading stats)\n");
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            verbose = true;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "Error: Unknown option: %s\n", arg);
            return 1;
        }
        break;
    }

    if (argi >= argc) {
        fprintf(stderr, "Error: Missing input file\n");
        return 1;
    }

    const char *filename = argv[argi];

    nmo_debug_context_t dbg;
    memset(&dbg, 0, sizeof(dbg));
    dbg.filename_storage[0] = '\0';
    dbg.filename = dbg.filename_storage;
    dbg.colorize = true;
    dbg.dump_level = NMO_DUMP_NORMAL;
    dbg.page_size = 20;
    dbg.regex_icase = false;

    if (verbose) {
        printf("Loading: %s\n", filename);
    }

    char err[128];
    if (!nmo_debug_load_file(&dbg, filename, err, sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err[0] ? err : "Failed to open session");
        return 1;
    }

    size_t object_count = 0;
    nmo_object_t **objects = NULL;
    nmo_session_get_objects(dbg.session, &objects, &object_count);
    if (verbose) {
        printf("Loaded %zu objects\n", object_count);
    }

    nmo_debug_repl_loop(&dbg);

    nmo_tool_close_session(dbg.ctx, dbg.session);
    return 0;
}
