/**
 * @file nmo_cmd_behavior_run.c
 * @brief CLI behavior run: execute the behavior graphs of a file frame by frame
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_record.h"
#include "../nmo_opt.h"

#include "nmo_engine.h"
#include "behavior/nmo_script_model.h"

#include <stdlib.h>
#include <string.h>

typedef struct run_args {
    uint32_t frames;
    float delta_ms;
    nmo_engine_start_mode_t start;
    nmo_object_id_t script_id;
    nmo_engine_unknown_block_t unknown;
    uint32_t max_iterations;
    uint32_t seed;
    bool trace;
} run_args_t;

typedef struct run_unknown {
    nmo_guid_t prototype;
    nmo_object_id_t first_behavior;
    uint64_t count;
} run_unknown_t;

typedef struct run_state {
    const nmo_script_model_t *model;
    const nmo_engine_t *engine;
    const run_args_t *args;
    nmo_cli_record_array_t *events;
    run_unknown_t *unknown;
    size_t unknown_count;
    size_t unknown_capacity;
    uint64_t executions;
    nmo_object_id_t last_unknown;  /* the block an "unknown block" event named last */
    nmo_object_id_t op_behavior;   /* the Op block whose operation ran last */
    int32_t op_status;
    bool ok;
} run_state_t;

/* "Name#id" from the script model, sized for the label; NULL on OOM. */
static char *run_label_dup(const nmo_script_model_t *model, nmo_object_id_t id)
{
    size_t length = nmo_script_model_label(model, id, NULL, 0);
    char *label = (char *)malloc(length + 1u);
    if (label != NULL) {
        nmo_script_model_label(model, id, label, length + 1u);
    }
    return label;
}

static bool run_event_shown(const run_args_t *args, const nmo_engine_event_t *event,
                            const nmo_script_node_t *node)
{
    if (args->trace) {
        return true;
    }
    switch (event->kind) {
    case NMO_ENGINE_EVENT_ACTIVATE:
    case NMO_ENGINE_EVENT_INFINITE_LOOP:
    case NMO_ENGINE_EVENT_MESSAGE_SENT:
    case NMO_ENGINE_EVENT_MESSAGE_RECEIVED:
        return true;
    case NMO_ENGINE_EVENT_EXECUTE:
        return node != NULL && node->kind == NMO_SCRIPT_NODE_BUILDING_BLOCK;
    default:
        return false;
    }
}

static void run_count_unknown(run_state_t *state, const nmo_script_node_t *node)
{
    if (node == NULL) {
        return;
    }
    for (size_t i = 0; i < state->unknown_count; i++) {
        if (nmo_guid_equals(state->unknown[i].prototype, node->prototype_guid)) {
            state->unknown[i].count++;
            return;
        }
    }
    if (state->unknown_count == state->unknown_capacity) {
        size_t capacity = state->unknown_capacity ? state->unknown_capacity * 2u : 16u;
        run_unknown_t *grown =
            (run_unknown_t *)realloc(state->unknown, capacity * sizeof(*grown));
        if (grown == NULL) {
            state->ok = false;
            return;
        }
        state->unknown = grown;
        state->unknown_capacity = capacity;
    }
    state->unknown[state->unknown_count++] = (run_unknown_t){
        .prototype = node->prototype_guid,
        .first_behavior = node->id,
        .count = 1,
    };
}

static void run_trace(const nmo_engine_event_t *event, void *user_data)
{
    run_state_t *state = (run_state_t *)user_data;
    const nmo_script_node_t *node =
        event->behavior_id != 0 ? nmo_script_model_find_node(state->model, event->behavior_id)
                                : NULL;
    if (event->kind == NMO_ENGINE_EVENT_EXECUTE && node != NULL &&
        node->kind == NMO_SCRIPT_NODE_BUILDING_BLOCK) {
        state->executions++;
    }
    if (event->kind == NMO_ENGINE_EVENT_UNKNOWN_BLOCK) {
        run_count_unknown(state, node);
        state->last_unknown = event->behavior_id;
    }
    if (event->kind == NMO_ENGINE_EVENT_OPERATION && event->behavior_id != 0) {
        state->op_behavior = event->behavior_id;
        state->op_status = event->result;
    }
    /* What an execute line notes about the block, from the events just before it */
    const char *note = "";
    if (event->kind == NMO_ENGINE_EVENT_EXECUTE) {
        if (event->behavior_id == state->last_unknown) {
            note = " (no function)";
        } else if (event->behavior_id == state->op_behavior && state->op_status != 0) {
            note = " (operation not supported)";
        } else if (event->result == NMO_ENGINE_BR_ACTIVATENEXTFRAME) {
            note = " (stays active)";
        } else if (event->result != NMO_ENGINE_BR_OK) {
            note = " (error)";
        }
        state->last_unknown = 0;
        state->op_behavior = 0;
    }
    if (!state->ok || !run_event_shown(state->args, event, node)) {
        return;
    }

    const char *kind = nmo_engine_event_kind_name(event->kind);
    char *behavior = event->behavior_id != 0 ? run_label_dup(state->model, event->behavior_id)
                                             : NULL;
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL && (event->behavior_id == 0 || behavior != NULL);
    ok = ok && nmo_cli_record_uint(item, "frame", NULL, event->frame);
    ok = ok && nmo_cli_record_str(item, "kind", NULL, kind);
    if (event->behavior_id != 0) {
        ok = ok && nmo_cli_record_uint(item, "behavior_id", NULL, event->behavior_id);
        ok = ok && nmo_cli_record_str(item, "behavior", NULL, behavior);
    }
    if (event->object_id != 0) {
        ok = ok && nmo_cli_record_uint(item, "object_id", NULL, event->object_id);
    }
    ok = ok && nmo_cli_record_int(item, "result", NULL, event->result);
    ok = ok && nmo_cli_record_uint(item, "depth", NULL, event->depth);

    unsigned indent = 2u + 2u * event->depth;
    switch (event->kind) {
    case NMO_ENGINE_EVENT_FRAME_BEGIN:
        ok = ok && nmo_cli_record_set_summary_fmt(item, "frame %llu (t = %.3f ms)",
                                                  (unsigned long long)event->frame,
                                                  event->time);
        break;
    case NMO_ENGINE_EVENT_FRAME_END:
        nmo_cli_record_omit_json(item);
        ok = ok && nmo_cli_record_set_summary(item, "");
        break;
    case NMO_ENGINE_EVENT_ACTIVATE:
        ok = ok && nmo_cli_record_set_summary_fmt(item, "[%llu]%*s%s %s",
                                                  (unsigned long long)event->frame, indent, "",
                                                  event->result ? "activate" : "deactivate",
                                                  behavior);
        break;
    case NMO_ENGINE_EVENT_EXECUTE:
        ok = ok && nmo_cli_record_set_summary_fmt(item, "[%llu]%*s%s%s",
                                                  (unsigned long long)event->frame, indent, "",
                                                  behavior, note);
        break;
    case NMO_ENGINE_EVENT_OPERATION:
        ok = ok && nmo_cli_record_set_summary_fmt(item, "[%llu]%*soperation #%u%s%s%s",
                                                  (unsigned long long)event->frame, indent, "",
                                                  event->object_id,
                                                  behavior != NULL ? " in " : "",
                                                  behavior != NULL ? behavior : "",
                                                  event->result == 0 ? "" : " (not run)");
        break;
    case NMO_ENGINE_EVENT_UNKNOWN_BLOCK:
        ok = ok && nmo_cli_record_set_summary_fmt(item, "[%llu]%*s%s: no function",
                                                  (unsigned long long)event->frame, indent, "",
                                                  behavior);
        break;
    case NMO_ENGINE_EVENT_MESSAGE_SENT:
    case NMO_ENGINE_EVENT_MESSAGE_RECEIVED: {
        const char *message = nmo_engine_message_type_name(state->engine, event->result);
        char *object = event->object_id != 0 ? run_label_dup(state->model, event->object_id)
                                             : NULL;
        ok = ok && (event->object_id == 0 || object != NULL);
        ok = ok && nmo_cli_record_str(item, "message", NULL, message != NULL ? message : "");
        if (event->kind == NMO_ENGINE_EVENT_MESSAGE_SENT) {
            ok = ok && nmo_cli_record_set_summary_fmt(
                item, "[%llu]%*s%s sends \"%s\" to %s", (unsigned long long)event->frame,
                indent, "", behavior != NULL ? behavior : "host", message != NULL ? message : "?",
                object != NULL ? object : "a broadcast");
        } else {
            ok = ok && nmo_cli_record_set_summary_fmt(
                item, "[%llu]  \"%s\" reaches %s: %s stops waiting",
                (unsigned long long)event->frame, message != NULL ? message : "?",
                object != NULL ? object : "?", behavior);
        }
        free(object);
        break;
    }
    case NMO_ENGINE_EVENT_INFINITE_LOOP:
        ok = ok && nmo_cli_record_set_summary_fmt(item, "[%llu]%*s%s: infinite loop, stopped after %d iterations",
                                                  (unsigned long long)event->frame, indent, "",
                                                  behavior, event->result);
        break;
    default:
        ok = ok && nmo_cli_record_set_summary_fmt(item, "[%llu]%*s%s #%u in %s",
                                                  (unsigned long long)event->frame, indent, "",
                                                  kind, event->object_id,
                                                  behavior != NULL ? behavior : "?");
        break;
    }
    free(behavior);
    if (!ok || !nmo_cli_record_array_add(state->events, item)) {
        if (!ok) {
            nmo_cli_record_free(item);
        }
        state->ok = false;
    }
}

static bool run_add_unknown(nmo_cli_record_t *rec, const run_state_t *state)
{
    nmo_cli_record_array_t *list =
        nmo_cli_record_array(rec, "unknown_blocks", "Building blocks without a function");
    if (list == NULL) {
        return false;
    }
    nmo_cli_record_array_omit_empty(list);
    for (size_t i = 0; i < state->unknown_count; i++) {
        const run_unknown_t *u = &state->unknown[i];
        const nmo_script_node_t *node = nmo_script_model_find_node(state->model, u->first_behavior);
        const char *name = node != NULL && node->prototype_name != NULL ? node->prototype_name
                                                                         : "?";
        char guid[NMO_GUID_STRING_SIZE];
        nmo_guid_format(u->prototype, guid, sizeof(guid));
        nmo_cli_record_t *item = nmo_cli_record_new();
        bool ok = item != NULL && nmo_cli_record_str(item, "prototype", NULL, name) &&
                  nmo_cli_record_guid(item, "guid", NULL, u->prototype) &&
                  nmo_cli_record_uint(item, "executions", NULL, u->count) &&
                  nmo_cli_record_set_summary_fmt(item, "  %s %s: %llu execution%s", name, guid,
                                                 (unsigned long long)u->count,
                                                 u->count == 1 ? "" : "s");
        if (!ok || !nmo_cli_record_array_add(list, item)) {
            if (!ok) {
                nmo_cli_record_free(item);
            }
            return false;
        }
    }
    return true;
}

static bool run_add_active_scripts(nmo_cli_record_t *rec, const nmo_engine_t *engine,
                                   const nmo_script_model_t *model)
{
    nmo_cli_record_array_t *list =
        nmo_cli_record_array(rec, "active_scripts", "Active scripts");
    if (list == NULL) {
        return false;
    }
    size_t count = 0;
    const nmo_script_node_t *nodes = nmo_script_model_nodes(model, &count);
    for (size_t i = 0; i < count; i++) {
        if (nodes[i].kind != NMO_SCRIPT_NODE_SCRIPT ||
            !nmo_engine_behavior_is_active(engine, nodes[i].id)) {
            continue;
        }
        char *label = run_label_dup(model, nodes[i].id);
        nmo_cli_record_t *item = nmo_cli_record_new();
        bool ok = label != NULL && item != NULL &&
                  nmo_cli_record_uint(item, "id", NULL, nodes[i].id) &&
                  nmo_cli_record_str(item, "name", NULL, nodes[i].name) &&
                  nmo_cli_record_set_summary_fmt(item, "  %s", label);
        free(label);
        if (!ok || !nmo_cli_record_array_add(list, item)) {
            if (!ok) {
                nmo_cli_record_free(item);
            }
            return false;
        }
    }
    return true;
}

static const char *run_start_name(nmo_engine_start_mode_t mode)
{
    switch (mode) {
    case NMO_ENGINE_START_ALL: return "all";
    case NMO_ENGINE_START_NONE: return "none";
    default: return "scene";
    }
}

static int run_execute(nmo_cmd_ctx_t *ctx, const run_args_t *args)
{
    nmo_engine_options_t options = nmo_engine_options_default();
    options.unknown_block = args->unknown;
    options.max_iterations = (int32_t)args->max_iterations;
    options.random_seed = args->seed;

    nmo_engine_t *engine = NULL;
    nmo_status_t status = nmo_engine_create(ctx->workspace, &options, &engine);
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Failed to create the behavior engine (%s)\n",
                nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    const nmo_script_model_t *model = nmo_engine_script_model(engine);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    run_state_t state = {.model = model, .engine = engine, .args = args, .ok = rec != NULL};
    if (state.ok) {
        state.events = nmo_cli_record_array(rec, "events", NULL);
        state.ok = state.events != NULL;
    }
    if (state.ok) {
        nmo_cli_record_array_omit_heading(state.events);
    }
    nmo_engine_set_trace(engine, run_trace, &state);

    int exit_code = NMO_CLI_EXIT_SUCCESS;
    status = nmo_engine_start(engine, args->start);
    if (status == NMO_OK && args->script_id != 0) {
        status = nmo_engine_activate_script(engine, args->script_id, true, true);
        if (status == NMO_ERR_NOT_FOUND) {
            fprintf(stderr, "Error: #%u is not a script\n", args->script_id);
            exit_code = NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    for (uint32_t f = 0; status == NMO_OK && f < args->frames; f++) {
        status = nmo_engine_step(engine, args->delta_ms);
    }
    if (status != NMO_OK && exit_code == NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Error: Behavior engine failed (%s)\n", nmo_error_string(status));
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_engine_set_trace(engine, NULL, NULL);

    bool ok = state.ok && exit_code == NMO_CLI_EXIT_SUCCESS;
    ok = ok && nmo_cli_record_uint(rec, "frames", "Frames", nmo_engine_frame(engine));
    ok = ok && nmo_cli_record_real(rec, "delta_ms", "Frame time (ms)", args->delta_ms, "%.3f");
    ok = ok && nmo_cli_record_real(rec, "time_ms", "Time (ms)", nmo_engine_time(engine), "%.3f");
    ok = ok && nmo_cli_record_str(rec, "start", "Start", args->script_id != 0
                                                          ? "script"
                                                          : run_start_name(args->start));
    ok = ok && nmo_cli_record_uint(rec, "block_executions", "Building block executions",
                                   state.executions);
    ok = ok && run_add_unknown(rec, &state);
    ok = ok && run_add_active_scripts(rec, engine, model);

    if (exit_code == NMO_CLI_EXIT_SUCCESS) {
        if (!ok) {
            nmo_cli_record_free(rec);
            rec = NULL;
        }
        exit_code = nmo_cmd_ctx_emit_record(ctx, rec, "behavior.run", 0, false);
    } else {
        nmo_cli_record_free(rec);
    }
    free(state.unknown);
    nmo_engine_destroy(engine);
    return exit_code;
}

static bool run_parse_args(nmo_cmd_ctx_t *c, int argc, char **argv, bool expect_file,
                           run_args_t *out)
{
    static const nmo_opt_def_t opts[] = {
        {"--frames", "-n", NMO_OPT_UINT, "Frames to run (default: 1)"},
        {"--delta", NULL, NMO_OPT_FLOAT, "Frame time in ms (default: 16.667)"},
        {"--start", NULL, NMO_OPT_STRING, "Scripts to start: scene, all or none (default: scene)"},
        {"--script", "-s", NMO_OPT_UINT, "Start only this script (reset)"},
        {"--unknown", NULL, NMO_OPT_STRING,
         "Building blocks without a function: stop or pass (default: stop)"},
        {"--max-iterations", NULL, NMO_OPT_UINT, "Iterations per graph and frame (default: 8000)"},
        {"--seed", NULL, NMO_OPT_UINT, "Random seed (default: 1)"},
        {"--trace", "-t", NMO_OPT_FLAG, "Show links, operations and graphs too"},
    };
    enum { OPT_FRAMES, OPT_DELTA, OPT_START, OPT_SCRIPT, OPT_UNKNOWN, OPT_MAX_ITER, OPT_SEED,
           OPT_TRACE, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t result = NMO_OPT_RESULT(vals, pos);
    if (nmo_cmd_ctx_parse(c, argc, argv, opts, OPT_COUNT, &result) != NMO_CLI_EXIT_SUCCESS) {
        return false;
    }
    if (result.pos_count != (expect_file ? 1u : 0u)) {
        return false;
    }

    out->frames = nmo_opt_uint_or(&vals[OPT_FRAMES], 1u);
    out->delta_ms = nmo_opt_float_or(&vals[OPT_DELTA], 1000.0f / 60.0f);
    out->script_id = nmo_opt_uint_or(&vals[OPT_SCRIPT], 0u);
    out->max_iterations = nmo_opt_uint_or(&vals[OPT_MAX_ITER], NMO_ENGINE_DEFAULT_MAX_ITERATIONS);
    out->seed = nmo_opt_uint_or(&vals[OPT_SEED], 1u);
    out->trace = nmo_opt_flag(&vals[OPT_TRACE]);
    if (out->delta_ms < 0.0f || out->max_iterations > (uint32_t)INT32_MAX) {
        return false;
    }

    const char *start = nmo_opt_str(&vals[OPT_START]);
    if (start == NULL) {
        out->start = out->script_id != 0 ? NMO_ENGINE_START_NONE
                                         : NMO_ENGINE_START_SCENE_DEFAULT;
    } else if (strcmp(start, "scene") == 0) {
        out->start = NMO_ENGINE_START_SCENE_DEFAULT;
    } else if (strcmp(start, "all") == 0) {
        out->start = NMO_ENGINE_START_ALL;
    } else if (strcmp(start, "none") == 0) {
        out->start = NMO_ENGINE_START_NONE;
    } else {
        return false;
    }

    const char *unknown = nmo_opt_str(&vals[OPT_UNKNOWN]);
    if (unknown == NULL || strcmp(unknown, "stop") == 0) {
        out->unknown = NMO_ENGINE_UNKNOWN_BLOCK_STOP;
    } else if (strcmp(unknown, "pass") == 0) {
        out->unknown = NMO_ENGINE_UNKNOWN_BLOCK_PASS;
    } else {
        return false;
    }
    return true;
}

int nmo_cmd_behavior_run(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    const char *usage = "nmo behavior run [options] <file>";
    run_args_t args;
    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_begin(&c, global);
    if (!run_parse_args(&c, argc, argv, true, &args)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    int rc = nmo_cmd_ctx_open(&c);
    if (rc) {
        return nmo_cmd_ctx_done(&c, rc);
    }
    return nmo_cmd_ctx_done(&c, run_execute(&c, &args));
}

int nmo_cmd_behavior_run_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    run_args_t args;
    if (!run_parse_args(ctx, argc, argv, false, &args)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: behavior run [options]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return run_execute(ctx, &args);
}
