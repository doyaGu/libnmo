/**
 * nmo inspect - Structured inspector for Virtools NMO/CMO/VMO files
 */

#include "nmo.h"

#include "nmo_inspect_cli.h"
#include "nmo_inspect_collect.h"
#include "nmo_inspect_render.h"
#include "nmo_inspect_util.h"

#include "nmo_tool_session.h"

#include <stdio.h>
#include <string.h>

int nmo_inspect_run(int argc, char **argv) {
    inspect_options_t opts;
    nmo_inspect_options_init(&opts);

    if (nmo_inspect_parse_args(argc, argv, &opts) != 0) {
        nmo_inspect_options_free(&opts);
        return 1;
    }

    if (opts.show_help) {
        nmo_inspect_print_usage();
        nmo_inspect_options_free(&opts);
        return 0;
    }
    if (opts.show_version) {
        printf("nmo inspect %d.%d.%d\n", NMO_VERSION_MAJOR, NMO_VERSION_MINOR, NMO_VERSION_PATCH);
        nmo_inspect_options_free(&opts);
        return 0;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char err[128];
    if (!nmo_tool_open_session(opts.input_path, &ctx, &session, err, sizeof(err))) {
        nmo_inspect_log(&opts, LOG_ERROR, "%s: %s", opts.input_path, err[0] ? err : "Failed to open session");
        nmo_inspect_options_free(&opts);
        return 2;
    }

    inspect_state_t state;
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;
    state.session = session;
    state.file_info = nmo_session_get_file_info(session);

    if (nmo_session_get_objects(session, &state.objects, &state.object_count) != 0) {
        nmo_inspect_log(&opts, LOG_ERROR, "Failed to query objects from session");
        nmo_tool_close_session(ctx, session);
        nmo_inspect_options_free(&opts);
        return 5;
    }

    if (!nmo_inspect_resolve_class_filter(&state, &opts)) {
        nmo_tool_close_session(ctx, session);
        nmo_inspect_options_free(&opts);
        return 1;
    }

    nmo_inspect_resolve_scene_root(&state, &opts);
    nmo_inspect_collect_stats(&state);

    warning_list_t warnings;
    nmo_inspect_warning_list_init(&warnings);

    bool strict_failure = false;
    nmo_inspect_collect_plugin_warnings(&state, &opts, &warnings);
    nmo_inspect_collect_chunk_warnings(&state, &opts, &warnings, &strict_failure);

    FILE *output = stdout;
    if (opts.output_path) {
        output = fopen(opts.output_path, "w");
        if (!output) {
            nmo_inspect_log(&opts, LOG_ERROR, "Failed to open %s", opts.output_path);
            nmo_inspect_warning_list_free(&warnings);
            nmo_tool_close_session(ctx, session);
            nmo_inspect_options_free(&opts);
            return 2;
        }
    }

    nmo_inspect_render_report(output, &state, &opts, &warnings);

    if (output != stdout) {
        fclose(output);
    }

    int exit_code = 0;
    if (strict_failure && opts.strict_mode) {
        exit_code = 3;
    }
    if (opts.fail_on_warning && warnings.count > 0) {
        exit_code = 4;
    }

    nmo_inspect_warning_list_free(&warnings);
    nmo_tool_close_session(ctx, session);
    nmo_inspect_options_free(&opts);
    return exit_code;
}
