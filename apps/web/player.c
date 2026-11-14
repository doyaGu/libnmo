/**
 * @file player.c
 * @brief nmo's player: runs a Virtools composition (a .cmo or .vmo) in the
 *        engine as a game player does, in a browser (WebAssembly, the page
 *        in this folder), in a desktop window, or headless (Node.js or
 *        natively) saving frames as PNG files.
 *
 *     player [options] FILE
 *       --registry KEY\NAME=VALUE;...  registry values the game reads
 *       --path DIR       more files to load (besides the document's folder)
 *       --write DIR      where saved files go, read first (default: none; a
 *                        window run saves into a temporary folder)
 *       --physics none|ivp  simulate the physics blocks (default: ivp when built)
 *       --headless       draw only the --shot frames, in memory, without a window
 *       --renderer gl3|soft  what the window draws with (default: gl3, falling back to
 *                        soft where the context has no OpenGL 3.3/ES 3.0/WebGL 2)
 *       --size WxH       the render context's size, a screen mode of the engine's
 *                        driver (default: 640x480, as the game starts)
 *       --scale N        the window's size, N times the render context's
 *       -n N             frames to run (default: until the window closes)
 *       --delta MS       a fixed frame time (default: the time that passed;
 *                        16.667 headless or with --shot)
 *       --no-frame-limit step every turn of the loop, ignoring the game's frame rate limit
 *       --keys KEY@FRAME[-FRAME],...  keys held down
 *       --shot FILE.png@FRAME,...     frames saved
 *       --mute           play no sound
 *
 * In the browser the page starts a run with player_start() and ends it with
 * player_stop(), as often as it likes, and asks about the running game (its
 * files, objects, scripts and what the behavior engine did) through the
 * other player_* functions.
 */

#include "nmo_engine.h"
#include "bb/nmo_bb_physics.h"
#include "bb/nmo_bb_terratools.h"
#include "bb/nmo_bb_virtools.h"
#include "document/nmo_document_objects.h"
#include "engine/nmo_engine_render.h"
#include "render/nmo_render_soft.h"
#include "nmo_run_host.h"
#ifdef NMO_HAS_VIEW
#include "engine/nmo_view.h"
#endif
#ifdef NMO_HAS_PHYSICS_IVP
#include "engine/nmo_physics_ivp.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/heap.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the page puts libnmo's data files (the Virtools type and block tables) */
#ifndef PLAYER_DATA_DIR
#define PLAYER_DATA_DIR "/nmo/data"
#endif

/* The most behavior engine events one trace keeps */
#define PLAYER_TRACE_LIMIT 500000u

/* What the window draws with (--renderer); PLAYER_RENDERER_AUTO tries gl3, then soft */
typedef enum player_renderer {
    PLAYER_RENDERER_AUTO,
    PLAYER_RENDERER_GL3,
    PLAYER_RENDERER_SOFT,
} player_renderer_t;

typedef struct player_args {
    const char *file;
    const char *registry;
    const char *path;
    const char *write;
    const char *keys;
    const char *shots;
    bool physics;
    bool headless;
    bool mute;
    bool no_frame_limit;
    int scale;
    int32_t width, height; /* --size; 0 for the default screen mode */
    uint32_t frames; /* 0: until the window closes */
    float delta_ms;  /* 0: the time that passed */
    player_renderer_t renderer;
} player_args_t;

/* One event of a trace as the page reads it, straight from memory: six 32-bit words */
typedef struct player_trace_record {
    uint32_t kind; /* nmo_engine_event_kind_t */
    uint32_t frame;
    uint32_t behavior;
    uint32_t object;
    int32_t result;
    uint32_t depth;
} player_trace_record_t;

_Static_assert(sizeof(player_trace_record_t) == 24u, "the page reads a record as 6 words");

typedef struct player {
    player_args_t args;
    char *arg_text;       /* player_start's arguments, which `args` points into */
    char **argv;
    nmo_context_t *ctx;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_engine_t *engine;
#ifdef NMO_HAS_PHYSICS_IVP
    nmo_physics_ivp_t *physics;
#endif
#ifdef NMO_HAS_VIEW
    nmo_view_t *view;
#endif
    nmo_render_t *render; /* headless: the frames drawn in memory */
    nmo_run_files_t files;
    bool files_ready;
    nmo_run_key_t *keys;
    long key_count;
    uint32_t frame;       /* frames run */
    double last_step;     /* the window's clock when the engine last stepped */
    int exit_code;
    bool muted;           /* --mute, or the page's mute button (player_set_mute) */
    float volume;         /* the page's volume (player_set_volume) */
    bool frame_limit;     /* keep to the game's frame rate limit */
    double step_ms;       /* how long the last frame took the engine */
    double draw_ms;       /* and drawing it */
    char *label;          /* player_label's last answer */
    float matrix[16];     /* player_entity_matrix's last answer */
    const void *param;    /* player_param_value's last answer: the value's bytes */
    char param_type[NMO_GUID_STRING_SIZE]; /* and its type */
    /* player_trace: the events of the next frames */
    player_trace_record_t *trace;
    size_t trace_count, trace_capacity;
    size_t trace_dropped; /* events past PLAYER_TRACE_LIMIT, or that found no memory */
    int trace_frames;     /* frames still to record; 0 when not recording */
} player_t;

static int player_usage(void)
{
    fprintf(stderr,
            "Usage: player [--registry SPEC] [--path DIR] [--write DIR] [--physics none|ivp]\n"
            "              [--headless] [--renderer gl3|soft] [--size WxH] [--scale N] [-n N]\n"
            "              [--delta MS] [--no-frame-limit] [--keys SPEC] [--shot FILE@FRAME,...]\n"
            "              [--mute] FILE\n");
    return 2;
}

static bool player_parse(int argc, char **argv, player_args_t *out)
{
    *out = (player_args_t){.physics = true, .scale = 1};
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        bool takes = true;
        if (strcmp(arg, "--registry") == 0 && value != NULL) {
            out->registry = value;
        } else if (strcmp(arg, "--path") == 0 && value != NULL) {
            out->path = value;
        } else if (strcmp(arg, "--write") == 0 && value != NULL) {
            out->write = value;
        } else if (strcmp(arg, "--keys") == 0 && value != NULL) {
            out->keys = value;
        } else if (strcmp(arg, "--shot") == 0 && value != NULL) {
            out->shots = value;
        } else if (strcmp(arg, "--physics") == 0 && value != NULL &&
                   (strcmp(value, "none") == 0 || strcmp(value, "ivp") == 0)) {
            out->physics = strcmp(value, "ivp") == 0;
        } else if (strcmp(arg, "--renderer") == 0 && value != NULL &&
                   (strcmp(value, "gl3") == 0 || strcmp(value, "soft") == 0)) {
            out->renderer = strcmp(value, "gl3") == 0 ? PLAYER_RENDERER_GL3 : PLAYER_RENDERER_SOFT;
        } else if (strcmp(arg, "--size") == 0 && value != NULL &&
                   sscanf(value, "%dx%d", &out->width, &out->height) == 2) {
            /* checked against the screen modes once there is an engine */
        } else if (strcmp(arg, "--scale") == 0 && value != NULL && atoi(value) >= 1) {
            out->scale = atoi(value);
        } else if (strcmp(arg, "-n") == 0 && value != NULL) {
            out->frames = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(arg, "--delta") == 0 && value != NULL && atof(value) > 0.0) {
            out->delta_ms = (float)atof(value);
        } else {
            takes = false;
            if (strcmp(arg, "--headless") == 0) {
                out->headless = true;
            } else if (strcmp(arg, "--mute") == 0) {
                out->mute = true;
            } else if (strcmp(arg, "--no-frame-limit") == 0) {
                out->no_frame_limit = true;
            } else if (arg[0] != '-' && out->file == NULL) {
                out->file = arg;
            } else {
                fprintf(stderr, "Error: Unknown or incomplete option %s\n", arg);
                return false;
            }
        }
        i += takes ? 1 : 0;
    }
    if (out->file == NULL) {
        return false;
    }
    if (!nmo_run_set_registry(NULL, out->registry)) {
        fprintf(stderr, "Error: --registry takes KEY\\NAME=VALUE entries separated by ';'\n");
        return false;
    }
    if (out->keys != NULL && nmo_run_parse_keys(out->keys, NULL) < 0) {
        fprintf(stderr, "Error: --keys takes KEY@FRAME[-FRAME],...\n");
        return false;
    }
    if (out->delta_ms == 0.0f && (out->headless || out->shots != NULL)) {
        out->delta_ms = 1000.0f / 60.0f; /* frame numbers stay the same from run to run */
    }
#ifndef NMO_HAS_VIEW
    if (!out->headless) {
        fprintf(stderr, "Error: This build has no window: pass --headless\n");
        return false;
    }
#endif
#ifndef NMO_HAS_PHYSICS_IVP
    out->physics = false;
#endif
    return true;
}

#ifdef __EMSCRIPTEN__
/* Keep the saves in the browser's storage: the page mounts --write as IDBFS */
static void player_persist(void)
{
    emscripten_run_script("if (typeof Module.persistSaves === 'function') Module.persistSaves();");
}

static double player_now(void)
{
    return emscripten_get_now();
}
#else
static void player_persist(void)
{
}

static double player_now(void)
{
    return 0.0; /* the timings are the page's alone */
}
#endif

static nmo_status_t player_write(void *user_data, const char *name, const void *data, size_t size)
{
    player_t *player = (player_t *)user_data;
    nmo_status_t status = nmo_run_files_write(&player->files, name, data, size);
    if (status == NMO_OK) {
        player_persist();
    }
    return status;
}

/* The volume the view plays at: none while muted */
static void player_apply_volume(player_t *player)
{
#ifdef NMO_HAS_VIEW
    nmo_view_set_volume(player->view, player->muted ? 0.0f : player->volume);
#else
    (void)player;
#endif
}

/* The render context's size: the first screen mode that size (the 16-bit one, as a game
   starts in); NMO_ERR_INVALID_ARGUMENT when the driver has none */
static nmo_status_t player_set_size(nmo_engine_t *engine, int32_t width, int32_t height)
{
    int32_t count = nmo_engine_screen_mode_count();
    for (int32_t mode = 0; mode < count; mode++) {
        int32_t mode_width = 0, mode_height = 0;
        nmo_engine_screen_mode_info(mode, &mode_width, &mode_height, NULL);
        if (mode_width == width && mode_height == height) {
            return nmo_engine_set_screen_mode(engine, mode);
        }
    }
    return NMO_ERR_INVALID_ARGUMENT;
}

/* The document, its engine with the block packs and physics, and the window or memory it
   draws in */
static nmo_status_t player_open(player_t *player)
{
    const player_args_t *args = &player->args;
    player->muted = args->mute;
    player->frame_limit = !args->no_frame_limit;
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.data_dir = PLAYER_DATA_DIR;
    player->ctx = nmo_context_create(&desc);
    if (player->ctx == NULL) {
        fprintf(stderr, "Error: Could not create the libnmo context\n");
        return NMO_ERR_NOMEM;
    }
    nmo_status_t status = nmo_document_load_file(player->ctx, args->file, NULL, &player->document);
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        fprintf(stderr, "Error: Could not load %s (%s)\n", args->file,
                message != NULL && message[0] != '\0' ? message : nmo_error_string(status));
        return status;
    }
    status = nmo_workspace_create(player->ctx, player->document, &player->workspace);
    nmo_engine_options_t options = nmo_engine_options_default();
    if (status == NMO_OK) {
        status = nmo_engine_create(player->workspace, &options, &player->engine);
    }
    /* The block packs, as a player loads the building block DLLs */
    if (status == NMO_OK) {
        status = nmo_bb_virtools_register(player->engine);
    }
    if (status == NMO_OK) {
        status = nmo_bb_physics_register(player->engine);
    }
    if (status == NMO_OK) {
        status = nmo_bb_terratools_register(player->engine);
    }
#ifdef NMO_HAS_PHYSICS_IVP
    if (status == NMO_OK && args->physics) {
        status = nmo_physics_ivp_attach(player->engine, &player->physics);
    }
#endif
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Could not create the engine (%s)\n", nmo_error_string(status));
        return status;
    }
    if (args->width != 0 || args->height != 0) {
        status = player_set_size(player->engine, args->width, args->height);
        if (status != NMO_OK) {
            fprintf(stderr, "Error: The engine has no %dx%d screen mode\n", args->width,
                    args->height);
            return status;
        }
    }

    if (args->headless) {
        status = nmo_render_create_soft(player->engine, &player->render);
    } else {
#ifdef NMO_HAS_VIEW
        bool soft = args->renderer == PLAYER_RENDERER_SOFT ||
                    (args->renderer == PLAYER_RENDERER_AUTO &&
                     !nmo_view_has_renderer(NMO_VIEW_RENDERER_GL3));
        nmo_view_renderer_t renderer = soft ? NMO_VIEW_RENDERER_SOFT : NMO_VIEW_RENDERER_GL3;
        status = nmo_view_open_with(player->engine, args->file, args->scale, renderer,
                                    &player->view);
        if (status != NMO_OK && !soft && args->renderer == PLAYER_RENDERER_AUTO) {
            /* The context came up without WebGL 2/GL ES 3.0/GL 3.3: the software renderer */
            fprintf(stderr, "GL3 is not available here; falling back to the software renderer\n");
            status = nmo_view_open_with(player->engine, args->file, args->scale,
                                        NMO_VIEW_RENDERER_SOFT, &player->view);
        }
        player_apply_volume(player);
#endif
    }
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Could not open what the frames are drawn in (%s)\n",
                nmo_error_string(status));
        return status;
    }

    player->files_ready = nmo_run_files_init(&player->files, args->file, args->path, args->write,
                                             !args->headless);
    if (!player->files_ready || !nmo_run_set_registry(player->engine, args->registry) ||
        !nmo_run_add_paths(player->engine)) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_ERR_NOMEM;
    }
    nmo_run_files_attach(&player->files, player->engine);
    nmo_engine_set_file_writer(player->engine, player_write, player);

    player->key_count = args->keys != NULL ? nmo_run_parse_keys(args->keys, NULL) : 0;
    if (player->key_count > 0) {
        player->keys = (nmo_run_key_t *)calloc((size_t)player->key_count, sizeof(*player->keys));
        if (player->keys == NULL) {
            return NMO_ERR_NOMEM;
        }
        nmo_run_parse_keys(args->keys, player->keys);
    }
    status = nmo_engine_start(player->engine, NMO_ENGINE_START_SCENE_DEFAULT);
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Could not start %s (%s)\n", args->file, nmo_error_string(status));
    }
    return status;
}

static void player_close(player_t *player)
{
#ifdef NMO_HAS_VIEW
    nmo_view_close(player->view);
    player->view = NULL;
#endif
    nmo_render_destroy(player->render);
    player->render = NULL;
#ifdef NMO_HAS_PHYSICS_IVP
    nmo_physics_ivp_detach(player->physics);
    player->physics = NULL;
#endif
    nmo_engine_destroy(player->engine);
    player->engine = NULL;
    if (player->files_ready) {
        nmo_run_files_release(&player->files);
        player->files_ready = false;
    }
    free(player->keys);
    player->keys = NULL;
    if (player->workspace != NULL) {
        nmo_workspace_destroy(player->workspace);
        player->workspace = NULL;
    }
    if (player->document != NULL) {
        nmo_document_destroy(player->document);
        player->document = NULL;
    }
    if (player->ctx != NULL) {
        nmo_context_release(player->ctx);
        player->ctx = NULL;
    }
    free(player->label);
    player->label = NULL;
    free(player->trace);
    player->trace = NULL;
    player->trace_count = player->trace_capacity = player->trace_dropped = 0u;
    player->trace_frames = 0;
}

/* Save the frame to `path`: the window's, or the one drawn in memory */
static nmo_status_t player_save(player_t *player, const char *path)
{
    nmo_status_t status = NMO_ERR_INVALID_STATE;
#ifdef NMO_HAS_VIEW
    if (player->view != NULL) {
        status = nmo_view_screenshot(player->view, path);
    }
#endif
    if (player->render != NULL) {
        /* At the render context's size, as its screen mode has it now */
        int32_t width = 640, height = 480;
        nmo_engine_screen_mode_info(nmo_engine_screen_mode(player->engine), &width, &height, NULL);
        status = nmo_render_screenshot(player->render, width, height, path);
    }
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Could not save %s\n", path);
    }
    return status;
}

/* Save the frame if the --shot list names frame `frame`: FILE@FRAME,... */
static void player_take_shots(player_t *player, uint64_t frame)
{
    for (const char *item = player->args.shots; item != NULL && *item != '\0';) {
        const char *end = strchr(item, ',');
        size_t length = end != NULL ? (size_t)(end - item) : strlen(item);
        const char *at = memchr(item, '@', length);
        if (at != NULL && strtoull(at + 1, NULL, 10) == frame) {
            size_t path_length = (size_t)(at - item);
            char *path = (char *)malloc(path_length + 1u);
            if (path != NULL) {
                memcpy(path, item, path_length);
                path[path_length] = '\0';
                player_save(player, path);
            }
            free(path);
        }
        item = end != NULL ? end + 1 : NULL;
    }
}

/* What the blocks wrote to the console this frame, on the player's output */
static void player_print_console(player_t *player)
{
    int32_t count = nmo_engine_console_line_count(player->engine);
    for (int32_t i = 0; i < count; i++) {
        nmo_engine_console_line_t line;
        if (nmo_engine_console_line(player->engine, i, &line) && line.text != NULL) {
            printf("[console] %s\n", line.text);
        }
    }
}

#ifdef __EMSCRIPTEN__
/* player_trace's recorder: each event of the frames being traced, until PLAYER_TRACE_LIMIT */
static void player_on_trace(const nmo_engine_event_t *event, void *user_data)
{
    player_t *player = (player_t *)user_data;
    if (player->trace_count == player->trace_capacity) {
        size_t capacity = player->trace_capacity == 0u ? 4096u : player->trace_capacity * 2u;
        if (capacity > PLAYER_TRACE_LIMIT) {
            capacity = PLAYER_TRACE_LIMIT;
        }
        player_trace_record_t *grown =
            capacity > player->trace_capacity
                ? (player_trace_record_t *)realloc(player->trace, capacity * sizeof(*grown))
                : NULL;
        if (grown == NULL) {
            player->trace_dropped++;
            return;
        }
        player->trace = grown;
        player->trace_capacity = capacity;
    }
    player->trace[player->trace_count++] = (player_trace_record_t){
        .kind = (uint32_t)event->kind,
        .frame = (uint32_t)event->frame,
        .behavior = event->behavior_id,
        .object = event->object_id,
        .result = event->result,
        .depth = event->depth,
    };
}
#endif

typedef enum player_tick_result {
    PLAYER_TICK_STEPPED,
    PLAYER_TICK_WAITING, /* the game's frame rate limit has not passed yet */
    PLAYER_TICK_DONE,
} player_tick_result_t;

/*
 * One turn of the player's loop: input, then a frame of the engine, drawn
 * and heard. A window playing in real time steps by the time that passed
 * (CKTimeManager bounds and scales it) and, when the game limits its frame
 * rate, skips turns that come sooner than the limit allows; the caller's
 * loop (the browser's animation frames, or the desktop's vertical sync)
 * paces the turns, so the player never waits itself.
 */
static player_tick_result_t player_tick(player_t *player)
{
    const player_args_t *args = &player->args;
    nmo_engine_t *engine = player->engine;
    float delta = args->delta_ms;
#ifdef NMO_HAS_VIEW
    if (player->view != NULL) {
        if (!nmo_view_poll(player->view)) {
            return PLAYER_TICK_DONE;
        }
        if (delta == 0.0f) {
            double now = nmo_view_time(player->view);
            nmo_engine_time_settings_t settings;
            nmo_engine_time_settings(engine, &settings);
            /* 4 ms early still counts: display refreshes jitter around the limit */
            if (player->frame_limit && (settings.limit_options & 0x40u) != 0 &&
                settings.frame_rate_limit > 0.0f &&
                now - player->last_step < 1000.0 / settings.frame_rate_limit - 4.0) {
                return PLAYER_TICK_WAITING;
            }
            delta = nmo_engine_frame_delta(engine, (float)(now - player->last_step));
            player->last_step = now;
        }
    }
#endif
    nmo_run_apply_keys(engine, player->keys, player->key_count, player->frame);
    double started = player_now();
    nmo_status_t status = nmo_engine_step(engine, delta);
    double stepped = player_now();
    player->step_ms = stepped - started;
    if (player->trace_frames > 0 && --player->trace_frames == 0) {
        nmo_engine_set_trace(engine, NULL, NULL);
    }
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Behavior engine failed (%s)\n", nmo_error_string(status));
        player->exit_code = 1;
        return PLAYER_TICK_DONE;
    }
    player_print_console(player);
#ifdef NMO_HAS_VIEW
    if (player->view != NULL) {
        status = nmo_view_draw(player->view);
        if (status != NMO_OK) {
            fprintf(stderr, "Error: Could not draw the frame (%s)\n", nmo_error_string(status));
            player->exit_code = 1;
            return PLAYER_TICK_DONE;
        }
        nmo_view_play_sounds(player->view);
    }
#endif
    player->draw_ms = player_now() - stepped;
    player_take_shots(player, nmo_engine_frame(engine));
    player->frame++;
    if ((args->frames > 0 && player->frame >= args->frames) || nmo_engine_exit_requested(engine)) {
        return PLAYER_TICK_DONE;
    }
    return PLAYER_TICK_STEPPED;
}

/* Parse the arguments and open the game: 0 when it is ready to run, 2 on bad arguments */
static int player_begin(player_t *player, int argc, char **argv)
{
    player->volume = 1.0f;
    if (!player_parse(argc, argv, &player->args)) {
        return player_usage();
    }
    if (player_open(player) != NMO_OK) {
        player_close(player);
        return 1;
    }
    return 0;
}

static player_t g_player;

#ifdef __EMSCRIPTEN__
#define PLAYER_EXPORT EMSCRIPTEN_KEEPALIVE

static bool g_running;      /* a run's loop is going (or paused) */
static int g_last_exit_code; /* how the last run ended (player_exit_code once it has) */

static void player_end(player_t *player)
{
    player_close(player);
    free(player->argv);
    free(player->arg_text);
    float volume = player->volume;
    memset(player, 0, sizeof(*player));
    player->volume = volume;
    g_running = false;
}

/* The browser calls this once an animation frame */
static void player_browser_frame(void *user_data)
{
    player_t *player = (player_t *)user_data;
    if (player_tick(player) == PLAYER_TICK_DONE) {
        g_last_exit_code = player->exit_code;
        emscripten_cancel_main_loop();
        player_end(player);
        emscripten_run_script("if (typeof Module.onPlayerExit === 'function') "
                              "Module.onPlayerExit(Module._player_exit_code());");
    }
}

/* End the run going on, if any; the page's Stop button and a new player_start */
PLAYER_EXPORT void player_stop(void)
{
    if (g_running) {
        emscripten_cancel_main_loop();
        player_end(&g_player);
    }
}

/*
 * Start a run, as main does with the arguments in `text` (one to a line):
 * 0 when it runs (on the browser's animation frames, until the game quits
 * or the page stops it), 1 when the game could not start, 2 on bad
 * arguments. A run going on ends first.
 */
PLAYER_EXPORT int player_start(const char *text)
{
    player_stop();
    player_t *player = &g_player;
    size_t length = strlen(text);
    int argc = 2; /* the program's name, and the last line */
    for (size_t i = 0; i < length; i++) {
        argc += text[i] == '\n';
    }
    player->arg_text = (char *)malloc(length + 1u);
    player->argv = (char **)calloc((size_t)argc + 1u, sizeof(char *));
    if (player->arg_text == NULL || player->argv == NULL) {
        player_end(player);
        return 1;
    }
    memcpy(player->arg_text, text, length + 1u);
    static char program[] = "player";
    player->argv[0] = program;
    argc = 1;
    for (char *line = player->arg_text; line != NULL;) {
        char *end = strchr(line, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        if (*line != '\0') {
            player->argv[argc++] = line;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    int result = player_begin(player, argc, player->argv);
    if (result != 0) {
        player_end(player);
        return result;
    }
    if (player->args.headless) {
        fprintf(stderr, "Error: The page's runs draw in its window: --headless is for main\n");
        player_end(player);
        return 2;
    }
    g_running = true;
    emscripten_set_main_loop_arg(player_browser_frame, player, 0, false);
    return 0;
}

/* Whether a run is going on */
PLAYER_EXPORT int player_running(void)
{
    return g_running;
}

/* The page's play/pause, mute and volume controls */
PLAYER_EXPORT void player_pause(void)
{
    emscripten_pause_main_loop();
}

PLAYER_EXPORT void player_resume(void)
{
    emscripten_resume_main_loop();
}

PLAYER_EXPORT void player_set_mute(int muted)
{
    g_player.muted = muted != 0;
    player_apply_volume(&g_player);
}

PLAYER_EXPORT void player_set_volume(float volume)
{
    g_player.volume = volume < 0.0f ? 0.0f : volume;
    player_apply_volume(&g_player);
}

/* Whether to keep to the game's frame rate limit (--no-frame-limit is the opposite) */
PLAYER_EXPORT void player_set_frame_limit(int limit)
{
    g_player.frame_limit = limit != 0;
}

/* Save the window's frame as a PNG file: 0 when saved */
PLAYER_EXPORT int player_screenshot(const char *path)
{
    return g_running && player_save(&g_player, path) == NMO_OK ? 0 : 1;
}

/* The state of a gamepad, as joystick `slot` (0-3): axes in -1..1, POV in
   hundredths of a degree (negative when centered), bit 0 for button 1 */
PLAYER_EXPORT void player_set_joystick(int slot, int attached, float x, float y, float z,
                                       float rx, float ry, float rz, float pov, uint32_t buttons)
{
    if (!g_running) {
        return;
    }
    nmo_engine_joystick_state_t state = {
        .attached = attached != 0,
        .position = {x, y, z},
        .rotation = {rx, ry, rz},
        .point_of_view = pov,
        .buttons = buttons,
    };
    nmo_engine_set_joystick(g_player.engine, slot, &state);
}

/* ---- What the page's developer tools show ---- */

/* How long the last frame took the engine, and drawing it, in ms */
PLAYER_EXPORT double player_step_ms(void)
{
    return g_player.step_ms;
}

PLAYER_EXPORT double player_draw_ms(void)
{
    return g_player.draw_ms;
}

/* The WebAssembly memory the player has taken, in bytes */
PLAYER_EXPORT double player_memory(void)
{
    return (double)emscripten_get_heap_size();
}

/* The engine's clock, in ms */
PLAYER_EXPORT double player_time(void)
{
    return g_running ? nmo_engine_time(g_player.engine) : 0.0;
}

/* The files the engine runs: the composition (0), then those loaded since */
PLAYER_EXPORT int player_file_count(void)
{
    int count = g_running ? 1 : 0;
    while (g_running && nmo_engine_loaded_file_name(g_player.engine, count) != NULL) {
        count++;
    }
    return count;
}

PLAYER_EXPORT const char *player_file_name(int file)
{
    if (!g_running) {
        return NULL;
    }
    return file == 0 ? g_player.args.file : nmo_engine_loaded_file_name(g_player.engine, file);
}

PLAYER_EXPORT int player_object_count(int file)
{
    return g_running ? nmo_engine_loaded_object_count(g_player.engine, file) : 0;
}

PLAYER_EXPORT uint32_t player_object(int file, int index)
{
    return g_running ? nmo_engine_loaded_object(g_player.engine, file, index) : 0u;
}

PLAYER_EXPORT const char *player_object_name(uint32_t id)
{
    return g_running ? nmo_engine_object_name(g_player.engine, id) : NULL;
}

PLAYER_EXPORT int player_object_class(uint32_t id)
{
    return g_running ? nmo_engine_object_class(g_player.engine, id) : 0;
}

PLAYER_EXPORT const char *player_class_name(int class_id)
{
    return g_running ? nmo_document_class_name(g_player.document, (nmo_class_id_t)class_id) : NULL;
}

PLAYER_EXPORT int player_object_active(uint32_t id)
{
    return g_running && nmo_engine_object_is_active(g_player.engine, id);
}

PLAYER_EXPORT int player_object_visible(uint32_t id)
{
    return g_running && nmo_engine_object_is_visible(g_player.engine, id);
}

PLAYER_EXPORT int player_script_count(void)
{
    return g_running ? nmo_engine_script_count(g_player.engine) : 0;
}

PLAYER_EXPORT uint32_t player_script_id(int index)
{
    return g_running ? nmo_engine_script_id(g_player.engine, index) : 0u;
}

/* A behavior's (or any object's) "Name#id" label; valid until the next call */
PLAYER_EXPORT const char *player_label(uint32_t id)
{
    if (!g_running) {
        return NULL;
    }
    size_t length = nmo_engine_label(g_player.engine, id, NULL, 0u);
    char *label = (char *)malloc(length + 1u);
    if (label == NULL) {
        return NULL;
    }
    nmo_engine_label(g_player.engine, id, label, length + 1u);
    free(g_player.label);
    g_player.label = label;
    return label;
}

PLAYER_EXPORT int player_behavior_active(uint32_t id)
{
    return g_running && nmo_engine_behavior_is_active(g_player.engine, id);
}

PLAYER_EXPORT double player_behavior_executions(uint32_t id)
{
    return g_running ? (double)nmo_engine_behavior_execution_count(g_player.engine, id) : 0.0;
}

/* CKBEHAVIOR_* flags of a behavior now; 0 for anything else */
PLAYER_EXPORT uint32_t player_behavior_flags(uint32_t id)
{
    return g_running ? nmo_engine_behavior_flags(g_player.engine, id) : 0u;
}

/* A 3D entity's world (or, with `world` 0, local) matrix, 16 floats by rows,
   valid until the next call; NULL for anything else */
PLAYER_EXPORT const float *player_entity_matrix(uint32_t id, int world)
{
    if (!g_running) {
        return NULL;
    }
    bool found = world ? nmo_engine_entity_world_matrix(g_player.engine, id, g_player.matrix)
                       : nmo_engine_entity_local_matrix(g_player.engine, id, g_player.matrix);
    return found ? g_player.matrix : NULL;
}

PLAYER_EXPORT uint32_t player_entity_parent(uint32_t id)
{
    return g_running ? nmo_engine_entity_parent(g_player.engine, id) : 0u;
}

/* The value a parameter holds now: its size in bytes (then its bytes from
   player_param_bytes and its type's GUID from player_param_type, valid until
   the next call into the engine), or -1 when `id` holds none */
PLAYER_EXPORT int player_param_value(uint32_t id)
{
    const void *data = NULL;
    size_t size = 0u;
    nmo_guid_t type = {0u, 0u};
    if (!g_running ||
        nmo_engine_param_value(g_player.engine, id, &data, &size, &type) != NMO_OK ||
        size > (size_t)INT32_MAX) {
        g_player.param = NULL;
        g_player.param_type[0] = '\0';
        return -1;
    }
    g_player.param = data;
    nmo_guid_format(type, g_player.param_type, sizeof(g_player.param_type));
    return (int)size;
}

PLAYER_EXPORT const void *player_param_bytes(void)
{
    return g_player.param;
}

PLAYER_EXPORT const char *player_param_type(void)
{
    return g_player.param_type;
}

PLAYER_EXPORT const char *player_message_name(int type)
{
    return g_running ? nmo_engine_message_type_name(g_player.engine, type) : NULL;
}

/* The screen modes a game can be started in (--size), from the engine's driver */
PLAYER_EXPORT int player_screen_mode_count(void)
{
    return nmo_engine_screen_mode_count();
}

PLAYER_EXPORT int player_screen_mode_width(int mode)
{
    int32_t width = 0;
    nmo_engine_screen_mode_info(mode, &width, NULL, NULL);
    return width;
}

PLAYER_EXPORT int player_screen_mode_height(int mode)
{
    int32_t height = 0;
    nmo_engine_screen_mode_info(mode, NULL, &height, NULL);
    return height;
}

/* Record what the behavior engine does in the next `frames` frames; read it
   with player_trace_records once player_tracing says it is done */
PLAYER_EXPORT void player_trace(int frames)
{
    if (!g_running || frames <= 0) {
        return;
    }
    g_player.trace_count = 0u;
    g_player.trace_dropped = 0u;
    g_player.trace_frames = frames;
    nmo_engine_set_trace(g_player.engine, player_on_trace, &g_player);
}

PLAYER_EXPORT int player_tracing(void)
{
    return g_player.trace_frames > 0;
}

/* The trace: player_trace_count records of 6 words (player_trace_record_t) */
PLAYER_EXPORT const player_trace_record_t *player_trace_records(void)
{
    return g_player.trace;
}

PLAYER_EXPORT int player_trace_count(void)
{
    return (int)g_player.trace_count;
}

/* Events the trace has no room for */
PLAYER_EXPORT double player_trace_dropped(void)
{
    return (double)g_player.trace_dropped;
}

/* What a record's kind means, in a few words */
PLAYER_EXPORT const char *player_trace_kind_name(int kind)
{
    switch ((nmo_engine_event_kind_t)kind) {
    case NMO_ENGINE_EVENT_FRAME_BEGIN: return "frame begin";
    case NMO_ENGINE_EVENT_FRAME_END: return "frame end";
    case NMO_ENGINE_EVENT_ACTIVATE: return "activate";
    case NMO_ENGINE_EVENT_EXECUTE: return "execute";
    case NMO_ENGINE_EVENT_LINK: return "link";
    case NMO_ENGINE_EVENT_LINK_DELAYED: return "link delayed";
    case NMO_ENGINE_EVENT_LINK_ARRIVED: return "link arrived";
    case NMO_ENGINE_EVENT_OPERATION: return "operation";
    case NMO_ENGINE_EVENT_UNKNOWN_BLOCK: return "unknown block";
    case NMO_ENGINE_EVENT_INFINITE_LOOP: return "infinite loop";
    case NMO_ENGINE_EVENT_MESSAGE_SENT: return "message sent";
    case NMO_ENGINE_EVENT_MESSAGE_RECEIVED: return "message received";
    case NMO_ENGINE_EVENT_EFFECT: return "effect";
    case NMO_ENGINE_EVENT_FILE_LOADED: return "file loaded";
    }
    return "";
}

/* Frames run so far (the page shows the rate) */
PLAYER_EXPORT uint32_t player_frames(void)
{
    return g_player.frame;
}

/* 0 when the run (going on, or the last) ended well, else 1 */
PLAYER_EXPORT int player_exit_code(void)
{
    return g_running ? g_player.exit_code : g_last_exit_code;
}
#endif /* __EMSCRIPTEN__ */

int main(int argc, char **argv)
{
    player_t *player = &g_player;
    memset(player, 0, sizeof(*player));
    int result = player_begin(player, argc, argv);
    if (result != 0) {
        return result;
    }
#ifdef __EMSCRIPTEN__
    if (!player->args.headless) {
        /* The browser's animation frames drive the loop; main returns at once */
        g_running = true;
        emscripten_set_main_loop_arg(player_browser_frame, player, 0, false);
        return 0;
    }
#endif
    for (;;) {
        player_tick_result_t step = player_tick(player);
        if (step == PLAYER_TICK_DONE) {
            break;
        }
#ifdef NMO_HAS_VIEW
        if (step == PLAYER_TICK_WAITING) {
            nmo_view_wait(player->view, 1.0);
        }
#endif
    }
    int exit_code = player->exit_code;
    player_close(player);
    return exit_code;
}
