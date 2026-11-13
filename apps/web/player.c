/**
 * @file player.c
 * @brief nmo's player: runs a Virtools document (Ballance's base.cmo) in the
 *        engine as a game player does, in a browser (WebAssembly, the page
 *        in this folder), in a desktop window, or headless (Node.js or
 *        natively) saving frames as PNG files.
 *
 *     player [options] base.cmo
 *       --registry KEY\NAME=VALUE;...  registry values (default: Ballance's
 *                                      Software\Ballance\Settings\Language=1)
 *       --path DIR       more files to load (besides the document's folder)
 *       --write DIR      where saved files go, read first (default: none; a
 *                        window run saves into a temporary folder)
 *       --physics none|ivp  simulate the physics blocks (default: ivp when built)
 *       --headless       draw only the --shot frames, in memory, without a window
 *       --renderer gl3|soft  what the window draws with (default: gl3, falling back to
 *                        soft where the context has no OpenGL 3.3/ES 3.0/WebGL 2)
 *       --scale N        the window's size, N times the render context's
 *       -n N             frames to run (default: until the window closes)
 *       --delta MS       a fixed frame time (default: the time that passed;
 *                        16.667 headless or with --shot)
 *       --keys KEY@FRAME[-FRAME],...  keys held down
 *       --shot FILE.png@FRAME,...     frames saved
 *       --mute           play no sound
 */

#include "nmo_engine.h"
#include "bb/nmo_bb_physics.h"
#include "bb/nmo_bb_terratools.h"
#include "bb/nmo_bb_virtools.h"
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
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the page puts libnmo's data files (the Virtools type and block tables) */
#ifndef PLAYER_DATA_DIR
#define PLAYER_DATA_DIR "/nmo/data"
#endif

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
    int scale;
    uint32_t frames; /* 0: until the window closes */
    float delta_ms;  /* 0: the time that passed */
    player_renderer_t renderer;
} player_args_t;

typedef struct player {
    player_args_t args;
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
} player_t;

static int player_usage(void)
{
    fprintf(stderr,
            "Usage: player [--registry SPEC] [--path DIR] [--write DIR] [--physics none|ivp]\n"
            "              [--headless] [--renderer gl3|soft] [--scale N] [-n N] [--delta MS]\n"
            "              [--keys SPEC] [--shot FILE@FRAME,...] [--mute] FILE\n");
    return 2;
}

static bool player_parse(int argc, char **argv, player_args_t *out)
{
    *out = (player_args_t){.registry = "Software\\Ballance\\Settings\\Language=1",
                           .physics = true, .scale = 1};
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
#else
static void player_persist(void)
{
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

/* The document, its engine with the block packs and physics, and the window or memory it draws in */
static nmo_status_t player_open(player_t *player)
{
    const player_args_t *args = &player->args;
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

    if (args->headless) {
        status = nmo_render_create_soft(player->engine, &player->render);
    } else {
#ifdef NMO_HAS_VIEW
        nmo_view_renderer_t renderer =
            args->renderer == PLAYER_RENDERER_SOFT ||
                    (args->renderer == PLAYER_RENDERER_AUTO && !nmo_view_has_renderer(NMO_VIEW_RENDERER_GL3))
                ? NMO_VIEW_RENDERER_SOFT
                : NMO_VIEW_RENDERER_GL3;
        status = nmo_view_open_with(player->engine, args->file, args->scale, renderer, &player->view);
        if (status != NMO_OK && renderer == NMO_VIEW_RENDERER_GL3 && args->renderer == PLAYER_RENDERER_AUTO) {
            /* The context came up without WebGL 2/GL ES 3.0/GL 3.3: the software renderer instead */
            fprintf(stderr, "GL3 is not available here; falling back to the software renderer\n");
            status = nmo_view_open_with(player->engine, args->file, args->scale,
                                        NMO_VIEW_RENDERER_SOFT, &player->view);
        }
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
}

/* Save the frame to `path`: the window's, or the one drawn in memory */
static void player_save(player_t *player, const char *path)
{
    nmo_status_t status = NMO_ERR_INVALID_STATE;
#ifdef NMO_HAS_VIEW
    if (player->view != NULL) {
        status = nmo_view_screenshot(player->view, path);
    }
#endif
    if (player->render != NULL) {
        int width = 0, height = 0;
        nmo_render_frame_size(player->render, &width, &height);
        status = nmo_render_screenshot(player->render, width, height, path);
    }
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Could not save %s\n", path);
    }
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
            if ((settings.limit_options & 0x40u) != 0 && settings.frame_rate_limit > 0.0f &&
                now - player->last_step < 1000.0 / settings.frame_rate_limit - 4.0) {
                return PLAYER_TICK_WAITING;
            }
            delta = nmo_engine_frame_delta(engine, (float)(now - player->last_step));
            player->last_step = now;
        }
    }
#endif
    nmo_run_apply_keys(engine, player->keys, player->key_count, player->frame);
    nmo_status_t status = nmo_engine_step(engine, delta);
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Behavior engine failed (%s)\n", nmo_error_string(status));
        player->exit_code = 1;
        return PLAYER_TICK_DONE;
    }
#ifdef NMO_HAS_VIEW
    if (player->view != NULL) {
        status = nmo_view_draw(player->view);
        if (status != NMO_OK) {
            fprintf(stderr, "Error: Could not draw the frame (%s)\n", nmo_error_string(status));
            player->exit_code = 1;
            return PLAYER_TICK_DONE;
        }
        if (!args->mute) {
            nmo_view_play_sounds(player->view);
        }
    }
#endif
    player_take_shots(player, nmo_engine_frame(engine));
    player->frame++;
    if ((args->frames > 0 && player->frame >= args->frames) || nmo_engine_exit_requested(engine)) {
        return PLAYER_TICK_DONE;
    }
    return PLAYER_TICK_STEPPED;
}

static player_t g_player;

#ifdef __EMSCRIPTEN__
/* The browser calls this once an animation frame */
static void player_browser_frame(void *user_data)
{
    player_t *player = (player_t *)user_data;
    if (player_tick(player) == PLAYER_TICK_DONE) {
        emscripten_cancel_main_loop();
        player_close(player);
        emscripten_run_script("if (typeof Module.onPlayerExit === 'function') "
                              "Module.onPlayerExit(Module._player_exit_code());");
    }
}
#endif

/* Frames run so far (the page shows the rate) */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
uint32_t player_frames(void)
{
    return g_player.frame;
}

/* 0 when the run ended well, else 1 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int player_exit_code(void)
{
    return g_player.exit_code;
}

int main(int argc, char **argv)
{
    player_t *player = &g_player;
    memset(player, 0, sizeof(*player));
    if (!player_parse(argc, argv, &player->args)) {
        return player_usage();
    }
    if (player_open(player) != NMO_OK) {
        player_close(player);
        return 1;
    }
#ifdef __EMSCRIPTEN__
    if (!player->args.headless) {
        /* The browser's animation frames drive the loop; main returns at once */
        emscripten_set_main_loop_arg(player_browser_frame, player, 0, false);
        return 0;
    }
#endif
    for (;;) {
        player_tick_result_t result = player_tick(player);
        if (result == PLAYER_TICK_DONE) {
            break;
        }
#ifdef NMO_HAS_VIEW
        if (result == PLAYER_TICK_WAITING) {
            nmo_view_wait(player->view, 1.0);
        }
#endif
    }
    int exit_code = player->exit_code;
    player_close(player);
    return exit_code;
}
