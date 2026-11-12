#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700 /* mkdtemp */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE /* where Darwin declares mkdtemp */
#endif

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
#include "bb/nmo_bb_physics.h"
#include "bb/nmo_bb_terratools.h"
#include "bb/nmo_bb_virtools.h"
#include "engine/nmo_engine_render.h"
#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_script_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#include <unistd.h>
#endif

#ifdef NMO_HAS_PHYSICS_IVP
#include "engine/nmo_physics_ivp.h"
#endif
#ifdef NMO_HAS_VIEW
#include "engine/nmo_view.h"
#endif

typedef struct run_args {
    uint32_t frames;
    float delta_ms;
    nmo_engine_start_mode_t start;
    nmo_object_id_t script_id;
    nmo_engine_unknown_block_t unknown;
    uint32_t max_iterations;
    uint32_t seed;
    bool trace;
    bool strict;
    const char *path; /* --path: where loaded files are found, besides the file's folder */
    const char *write; /* --write: where saved files go, and where loading looks first */
    const char *keys; /* "Space@30-35,Enter@60": keys and the frames they are down */
    const char *steer; /* "X,Z[,Y[,F]];...@FRAME[-FRAME]": points an object is steered through */
    const char *steer_objects; /* "NAME,NAME": the objects --steer may steer, first shown */
    const char *steer_log;     /* CSV file of the steered object's frames, or NULL */
    const char *physics_log;   /* CSV world matrices of movable physical bodies */
    const char *observe;       /* named entities whose final world poses are reported */
    const char *messages; /* "Level_Finish@4000:All_Gameplay,...": messages the host sends */
    const char *registry; /* "Software\Game\Settings\Language=1;...": registry values */
    bool physics_ivp;     /* --physics ivp: simulate the physics blocks with libivp */
    bool window;          /* --window: show the frames and take the keyboard and mouse */
    bool frames_given;    /* -n given: a window closes after that many frames */
    const char *shots;    /* "menu.png@700,...": frames a window saves */
    bool mute;            /* --mute: the window plays no sound */
    bool mipmaps;         /* --mipmaps: the window mipmaps every texture */
    bool quiet;           /* --quiet, or a window played until it closes: no events listed */
    bool control;         /* stdin step commands; the engine waits between commands */
    bool real_time;       /* a window with no --delta or --shot: frames take the time that passed */
} run_args_t;

/* A key held down from frame `first` to frame `last`. */
typedef struct run_key {
    uint32_t key;
    uint32_t first;
    uint32_t last;
} run_key_t;

/* A key: a name nmo_behavior_keyboard_key_name() spells, or a scan code. */
static uint32_t run_key_code(const char *name, size_t length)
{
    char *copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, name, length);
    copy[length] = '\0';
    uint32_t key = nmo_behavior_keyboard_key_from_name(copy);
    if (key == 0 && length > 0) {
        char *end = NULL;
        unsigned long code = strtoul(copy, &end, 0);
        key = (end != NULL && *end == '\0' && code > 0 && code < 256) ? (uint32_t)code : 0u;
    }
    free(copy);
    return key;
}

static bool run_frame_number(const char **text, uint32_t *out)
{
    const char *start = *text;
    char *end = NULL;
    unsigned long value = strtoul(start, &end, 10);
    if (end == start || value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    *text = end;
    return true;
}

/*
 * Parse "KEY@FRAME[-FRAME],..." into `out` (when not NULL); the count, or -1
 * when the text is malformed. A key name ends at its '@', so names with
 * commas or spaces ("Right Shift") need no quoting inside the list.
 */
static long run_parse_keys(const char *spec, run_key_t *out)
{
    long count = 0;
    const char *p = spec;
    while (p != NULL && *p != '\0') {
        const char *at = strchr(p, '@');
        if (at == NULL || at == p) {
            return -1;
        }
        run_key_t entry = {.key = run_key_code(p, (size_t)(at - p))};
        p = at + 1;
        if (entry.key == 0 || !run_frame_number(&p, &entry.first)) {
            return -1;
        }
        entry.last = entry.first;
        if (*p == '-') {
            p++;
            if (!run_frame_number(&p, &entry.last) || entry.last < entry.first) {
                return -1;
            }
        }
        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            return -1;
        }
        if (out != NULL) {
            out[count] = entry;
        }
        count++;
    }
    return count;
}

/*
 * --steer: from frame `first` (to `last`), the arrow keys roll an object through the
 * points in turn, as a player would, in the camera's frame: the first shown
 * of the objects --steer-object names (a game swaps the object a player
 * rolls). A point is passed once the object comes within STEER_REACH of it
 * (STEER_STOP_REACH of one it must stop at), and, if the point has a height,
 * is at least that high there (it waits to be lifted), and, if it has a frame
 * count, after that many more frames heading for it (it keeps pushing
 * there), and, if it names an object, once that object is within a reach of
 * a place (it waits for something moving to come by); after a jump (a
 * respawn), it heads for the point after the nearest one.  A point whose
 * frame count is "rush" is run at flat out, never braking, as a jump's
 * run-up is; "precision" follows narrow beams slowly with tighter tracking.
 * One whose count is "push:N" is headed for N frames,
 * reached or not, or until the object is as high as its height, and passed
 * then (a ball rocked where it may stick till something lifts it).  Between points it follows the path without
 * stopping, slowing down only as much as the bends ahead need
 * (run_steer_heading).  When the object has not moved at all for
 * STEER_NUDGE frames with keys held, the keys are let go of for a frame,
 * as a player's finger lifts: physics puts a body at rest to sleep, and
 * only a force set anew wakes it.
 */
typedef struct run_steer {
    float *points; /* x, z, y (-INFINITY: any height), frames to hold (-1: rush;
                      -1.25: precision;
                      -1 - N: push for N frames) */
    char **waits;  /* each point's object to wait for, or NULL */
    float *wait_at; /* where it is waited for: x, z, reach */
    long count;
    uint32_t first, last;
    long next;
    long held; /* frames spent at the next point since reaching it */
    long still; /* frames the object has not moved with keys held */
    bool placed;
    float x, z; /* the object last frame */
    const char *objects; /* "NAME,NAME" */
    FILE *log;           /* frame, position, speed, next point and keys, or NULL */
} run_steer_t;

static char *run_strndup(const char *text, size_t length);

#define STEER_REACH 3.5f
#define STEER_STOP_REACH 1.5f
#define STEER_JUMP 10.0f
#define STEER_LOOKAHEAD 20.0f /* frames of the object's speed to allow for */
#define STEER_DEAD 0.5f
#define STEER_CARROT 6.0f /* how far along the path ahead of the object to aim */
#define STEER_PLAN 40.0f  /* how far along the path to look for bends to slow down for */
#define STEER_FAST 0.45f  /* speeds, a frame: on straight road */
#define STEER_SLOW 0.1f   /* through a right-angled bend */
#define STEER_BRAKE 0.006f /* how much speed braking takes off a frame */
#define STEER_EASE 0.02f  /* how far off the speed wanted to push or brake */
#define STEER_TRACK 1.0f  /* how hard it is pulled back onto the path, a unit off */
#define STEER_NUDGE 30    /* frames held still before the keys are let go of */
#define STEER_PRECISION -1.25f /* point action: narrow single-beam tracking */

static bool run_steer_precise(const run_steer_t *steer, long i)
{
    return steer->points[i * 4 + 3] == STEER_PRECISION;
}

/* Whether the object must stop at point i: the last, or one with a height to
 * wait for, frames to hold or an object to wait for. */
static bool run_steer_stops(const run_steer_t *steer, long i)
{
    const float *point = &steer->points[i * 4];
    return i + 1 >= steer->count || isfinite(point[2]) || point[3] > 0.0f ||
           point[3] < -1.5f || steer->waits[i] != NULL;
}

/* The frames point i is pushed for, reached or not; 0 for another point. */
static long run_steer_push(const run_steer_t *steer, long i)
{
    float hold = steer->points[i * 4 + 3];
    return hold < -1.5f ? (long)(-hold - 1.0f) : 0;
}

/* The speed point i may be passed at: STEER_FAST where the path goes on
 * straight, less the sharper it bends there, down to STEER_SLOW at a right
 * angle; 0 where it must stop. */
static float run_steer_corner_speed(const run_steer_t *steer, long i, float x, float z)
{
    if (run_steer_stops(steer, i)) {
        return 0.0f;
    }
    const float *point = &steer->points[i * 4];
    float ax = i > 0 ? point[-4] : x, az = i > 0 ? point[-3] : z;
    float ux = point[0] - ax, uz = point[1] - az, wx = point[4] - point[0], wz = point[5] - point[1];
    float ul = hypotf(ux, uz), wl = hypotf(wx, wz);
    if (ul <= 0.0f || wl <= 0.0f) {
        return STEER_SLOW;
    }
    float bend = acosf(fmaxf(-1.0f, fminf(1.0f, (ux * wx + uz * wz) / (ul * wl))));
    float t = fminf(bend / 1.5707964f, 1.0f); /* of a right angle */
    float fast = run_steer_precise(steer, i) ? 0.22f : STEER_FAST;
    return fast + (STEER_SLOW - fast) * t;
}

/*
 * Where to push from (x, z), moving (vx, vz) a frame.  The object aims at the
 * point STEER_CARROT ahead along the path, so it rounds the path's bends
 * rather than stopping at each point, and never past a point it must stop at;
 * its drift across that aim is taken off as it would carry it over
 * STEER_LOOKAHEAD frames.  Along the aim, it speeds up, coasts or brakes
 * toward the speed it may have now: the least, over the points within
 * STEER_PLAN ahead, of what still lets it brake down to their speed by then.
 * At a point it must stop at, within reach, it heads for the point itself.
 */
static void run_steer_heading(const run_steer_t *steer, float x, float z, float vx, float vz,
                              float *dx, float *dz)
{
    long i = steer->next;
    const float *point = &steer->points[i * 4];
    bool precise = run_steer_precise(steer, i);
    float carrot = precise ? 2.0f : STEER_CARROT;
    float lookahead = precise ? 14.0f : STEER_LOOKAHEAD;
    float dead = precise ? 0.12f : STEER_DEAD;
    float left = hypotf(point[0] - x, point[1] - z);
    if (run_steer_stops(steer, i) && left < STEER_REACH) {
        *dx = point[0] - x - vx * STEER_LOOKAHEAD;
        *dz = point[1] - z - vz * STEER_LOOKAHEAD;
        return;
    }
    /* the aim: STEER_CARROT along the path, or the first point it must stop
     * at if that comes sooner; and the speed it may have now */
    float tx = point[0], tz = point[1], walked = left, wanted = precise ? 0.22f : STEER_FAST;
    bool aimed = left >= carrot;
    for (long k = i; k < steer->count; k++) {
        const float *pk = &steer->points[k * 4];
        if (k > i) {
            float seg = hypotf(pk[0] - pk[-4], pk[1] - pk[-3]);
            walked += seg;
            if (!aimed && walked >= carrot && seg > 0.0f) {
                float back = walked - carrot;
                tx = pk[0] - (pk[0] - pk[-4]) / seg * back;
                tz = pk[1] - (pk[1] - pk[-3]) / seg * back;
                aimed = true;
            }
        }
        float corner = run_steer_corner_speed(steer, k, x, z);
        wanted = fminf(wanted, sqrtf(corner * corner + 2.0f * STEER_BRAKE * walked));
        if (run_steer_stops(steer, k)) {
            if (!aimed) {
                tx = pk[0];
                tz = pk[1];
            }
            break;
        }
        if (walked > STEER_PLAN) {
            break;
        }
    }
    if (point[3] == -1.0f) {
        wanted = INFINITY; /* a run-up */
    }
    float cx = tx - x, cz = tz - z, cl = hypotf(cx, cz);
    if (cl <= 0.0f) {
        *dx = -vx * STEER_LOOKAHEAD;
        *dz = -vz * STEER_LOOKAHEAD;
        return;
    }
    float ux = cx / cl, uz = cz / cl;
    float along = vx * ux + vz * uz;
    float sx = vx - ux * along, sz = vz - uz * along; /* drift across the aim */
    float push = along < wanted - STEER_EASE ? 1.0f : along > wanted + STEER_EASE ? -1.0f : 0.0f;
    *dx = ux * push * dead * 4.0f - sx * lookahead;
    *dz = uz * push * dead * 4.0f - sz * lookahead;
    /* away from the next point, off the line to it from the point before,
     * it is pulled back across: the aim alone lets it settle as far off as
     * the keys' dead zone */
    if (i > 0 && (precise || left > STEER_CARROT)) {
        const float *from = &steer->points[(i - 1) * 4];
        float px = point[0] - from[0], pz = point[1] - from[1], pl = hypotf(px, pz);
        if (pl > 0.0f) {
            float off = ((x - from[0]) * pz - (z - from[1]) * px) / pl;
            *dx -= pz / pl * off * STEER_TRACK;
            *dz += px / pl * off * STEER_TRACK;
        }
    }
}

/* Parse "X,Z[,Y[,F]];...@FRAME[-FRAME]" into `out` (points left NULL when only checking);
 * Y may be left empty ("X,Z,,F"). */
/* A number that is all of text[0..length). */
static bool run_steer_number(const char *text, size_t length, float *out)
{
    char *end = NULL;
    float value = strtof(text, &end);
    if (length == 0 || end != text + length || !isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

static void run_free_steer(run_steer_t *steer)
{
    for (long i = 0; steer->waits != NULL && i < steer->count; i++) {
        free(steer->waits[i]);
    }
    free(steer->waits);
    free(steer->wait_at);
    free(steer->points);
    steer->waits = NULL;
    steer->wait_at = NULL;
    steer->points = NULL;
}

static bool run_parse_steer(const char *spec, run_steer_t *out)
{
    const char *at = strrchr(spec, '@');
    if (at == NULL) {
        return false;
    }
    const char *p = at + 1;
    uint32_t first = 0, last = UINT32_MAX;
    if (!run_frame_number(&p, &first)) {
        return false;
    }
    if (*p == '-' && (++p, !run_frame_number(&p, &last) || last < first)) {
        return false;
    }
    if (*p != '\0') {
        return false;
    }
    long count = 0;
    for (const char *q = spec; q < at; q++) {
        count += *q == ';';
    }
    count++;
    run_steer_t steer = {.count = count, .first = first, .last = last};
    if (out != NULL) {
        steer.points = (float *)calloc((size_t)count * 4u, sizeof(float));
        steer.waits = (char **)calloc((size_t)count, sizeof(char *));
        steer.wait_at = (float *)calloc((size_t)count * 3u, sizeof(float));
        if (steer.points == NULL || steer.waits == NULL || steer.wait_at == NULL) {
            run_free_steer(&steer);
            return false;
        }
    }
    const char *q = spec;
    for (long i = 0; i < count; i++) {
        /* X,Z[,Y[,F[,NAME,X,Z,REACH]]] */
        const char *stop = memchr(q, ';', (size_t)(at - q));
        if (stop == NULL) {
            stop = at;
        }
        const char *fields[8] = {NULL};
        size_t lengths[8] = {0};
        int n = 0;
        for (const char *f = q;;) {
            const char *comma = memchr(f, ',', (size_t)(stop - f));
            const char *end = comma != NULL ? comma : stop;
            if (n == 8) {
                run_free_steer(&steer);
                return false;
            }
            fields[n] = f;
            lengths[n] = (size_t)(end - f);
            n++;
            if (comma == NULL) {
                break;
            }
            f = comma + 1;
        }
        float x = 0.0f, z = 0.0f, y = -INFINITY, hold = 0.0f, wait[3] = {0.0f, 0.0f, 0.0f};
        bool rush = n >= 4 && lengths[3] == 4 && memcmp(fields[3], "rush", 4) == 0;
        bool precise = n == 4 && lengths[3] == 9 && memcmp(fields[3], "precision", 9) == 0;
        float push = 0.0f;
        bool pushed = n >= 4 && lengths[3] > 5 && memcmp(fields[3], "push:", 5) == 0 &&
                      run_steer_number(fields[3] + 5, lengths[3] - 5, &push) && push >= 1.0f;
        bool ok = (n == 2 || n == 3 || n == 4 || n == 8) &&
                  run_steer_number(fields[0], lengths[0], &x) &&
                  run_steer_number(fields[1], lengths[1], &z) &&
                  (n < 3 || lengths[2] == 0 || run_steer_number(fields[2], lengths[2], &y)) &&
                  (n < 4 || rush || pushed || precise || (n == 8 && lengths[3] == 0) ||
                   (run_steer_number(fields[3], lengths[3], &hold) && hold >= 0.0f));
        if (rush) {
            hold = -1.0f;
        } else if (pushed) {
            hold = -1.0f - floorf(push);
        } else if (precise) {
            hold = STEER_PRECISION;
        }
        for (int k = 0; ok && n == 8 && k < 3; k++) {
            ok = run_steer_number(fields[5 + k], lengths[5 + k], &wait[k]);
        }
        if (!ok || (n == 8 && (lengths[4] == 0 || wait[2] <= 0.0f))) {
            run_free_steer(&steer);
            return false;
        }
        if (out != NULL) {
            steer.points[i * 4] = x;
            steer.points[i * 4 + 1] = z;
            steer.points[i * 4 + 2] = y;
            steer.points[i * 4 + 3] = hold;
            if (n == 8) {
                steer.waits[i] = run_strndup(fields[4], lengths[4]);
                if (steer.waits[i] == NULL) {
                    run_free_steer(&steer);
                    return false;
                }
                memcpy(&steer.wait_at[i * 3], wait, sizeof(wait));
            }
        }
        q = stop + 1;
    }
    if (out != NULL) {
        *out = steer;
    }
    return true;
}

/* The object to steer: the first shown of those named. */
static nmo_object_id_t run_steer_object(const nmo_engine_t *engine, const char *names)
{
    for (const char *item = names; item != NULL && *item != '\0';) {
        const char *end = strchr(item, ',');
        size_t length = end != NULL ? (size_t)(end - item) : strlen(item);
        char *name = run_strndup(item, length);
        nmo_object_id_t id = name != NULL ? nmo_engine_object_named(engine, name, NMO_CID_3DENTITY) : 0;
        free(name);
        if (id != 0 && nmo_engine_object_is_visible(engine, id)) {
            return id;
        }
        item = end != NULL ? end + 1 : NULL;
    }
    return 0;
}

/* Whether name and wanted are copies of one object: the same name once the
 * digits the game numbers copies with are taken off both ends
 * (P_Modul_08_Schaukel, P_Modul_08_Schaukel001, ...). */
static bool run_steer_copy_of(const char *name, const char *wanted)
{
    size_t n = strlen(wanted);
    while (n > 0 && wanted[n - 1] >= '0' && wanted[n - 1] <= '9') {
        n--;
    }
    if (name == NULL || strncmp(name, wanted, n) != 0) {
        return false;
    }
    for (const char *c = name + n; *c != '\0'; c++) {
        if (*c < '0' || *c > '9') {
            return false;
        }
    }
    return true;
}

/* Whether point i's object to wait for, if it has one, is within its reach
 * of its place, on X and Z.  The game names the copies of a module's moving
 * part by the order it makes them in, which changes with the sectors live at
 * the time, so any moving copy of it within reach of the place will do: the
 * place itself tells the copies apart. */
static bool run_steer_waited(const nmo_engine_t *engine, const run_steer_t *steer, long i)
{
    if (steer->waits[i] == NULL) {
        return true;
    }
    float m[16];
    const float *at = &steer->wait_at[i * 3];
    for (int32_t k = 0; k < nmo_engine_physics_object_count(engine); k++) {
        nmo_engine_physics_object_t object;
        if (nmo_engine_physics_object_at(engine, k, &object) && !object.fixed &&
            run_steer_copy_of(nmo_engine_object_name(engine, object.entity), steer->waits[i]) &&
            nmo_engine_entity_world_matrix(engine, object.entity, m) &&
            hypotf(m[12] - at[0], m[14] - at[1]) <= at[2]) {
            return true;
        }
    }
    nmo_object_id_t id = nmo_engine_object_named(engine, steer->waits[i], NMO_CID_3DENTITY);
    return id != 0 && nmo_engine_entity_world_matrix(engine, id, m) &&
           hypotf(m[12] - at[0], m[14] - at[1]) <= at[2];
}

static void run_steer_frame(nmo_engine_t *engine, run_steer_t *steer, uint32_t frame)
{
    if (frame < steer->first || frame > steer->last) {
        return;
    }
    static const char *const arrows[] = {"Up", "Down", "Left", "Right"};
    bool down[4] = {false, false, false, false};
    nmo_object_id_t steered = frame >= steer->first && frame <= steer->last
                                  ? run_steer_object(engine, steer->objects)
                                  : 0;
    float m[16] = {0};
    float vx_last = 0.0f, vz_last = 0.0f;
    nmo_engine_frame_t view;
    if (steered != 0 && nmo_engine_entity_world_matrix(engine, steered, m) &&
        nmo_engine_render_frame(engine, &view) == NMO_OK) {
        float x = m[12], z = m[14];
        float vx = steer->placed ? x - steer->x : 0.0f, vz = steer->placed ? z - steer->z : 0.0f;
        vx_last = vx;
        vz_last = vz;
        if (!steer->placed || hypotf(vx, vz) > STEER_JUMP) {
            long nearest = 0;
            for (long i = 1; i < steer->count; i++) {
                if (hypotf(steer->points[i * 4] - x, steer->points[i * 4 + 1] - z) <
                    hypotf(steer->points[nearest * 4] - x, steer->points[nearest * 4 + 1] - z)) {
                    nearest = i;
                }
            }
            steer->next = nearest + 1 < steer->count ? nearest + 1 : nearest;
            steer->held = 0;
            vx = vz = 0.0f;
        }
        steer->placed = true;
        steer->x = x;
        steer->z = z;
        if (steer->next + 1 < steer->count && run_steer_push(steer, steer->next) > 0 &&
            (++steer->held >= run_steer_push(steer, steer->next) ||
             m[13] >= steer->points[steer->next * 4 + 2])) {
            steer->next++;
            steer->held = 0;
        }
        while (steer->next + 1 < steer->count && run_steer_push(steer, steer->next) == 0 &&
               hypotf(steer->points[steer->next * 4] - x,
                      steer->points[steer->next * 4 + 1] - z) <
                   (run_steer_stops(steer, steer->next) ? STEER_STOP_REACH :
                    run_steer_precise(steer, steer->next) ? 1.0f : STEER_REACH) &&
               m[13] >= steer->points[steer->next * 4 + 2] &&
               run_steer_waited(engine, steer, steer->next)) {
            if ((float)steer->held < steer->points[steer->next * 4 + 3]) {
                steer->held++;
                break;
            }
            steer->next++;
            steer->held = 0;
        }
        float dx, dz;
        run_steer_heading(steer, x, z, vx, vz, &dx, &dz);
        /* the camera's X and Z axes, flat */
        float rx = view.view[0], rz = view.view[2], fx = view.view[8], fz = view.view[10];
        float rl = hypotf(rx, rz), fl = hypotf(fx, fz);
        if (rl > 0.0f && fl > 0.0f) {
            float ahead = (dx * fx + dz * fz) / fl, right = (dx * rx + dz * rz) / rl;
            float dead = run_steer_precise(steer, steer->next) ? 0.12f : STEER_DEAD;
            down[0] = ahead > dead;
            down[1] = ahead < -dead;
            down[2] = right < -dead;
            down[3] = right > dead;
        }
        bool pushed = down[0] || down[1] || down[2] || down[3];
        steer->still = pushed && vx_last == 0.0f && vz_last == 0.0f ? steer->still + 1 : 0;
        if (steer->still >= STEER_NUDGE) {
            memset(down, 0, sizeof(down));
            steer->still = 0;
        }
    }
    if (frame >= steer->first) {
        for (int k = 0; k < 4; k++) {
            nmo_engine_set_key(engine, run_key_code(arrows[k], strlen(arrows[k])), down[k]);
        }
        if (steer->log != NULL && steered != 0 && steer->placed) {
            fprintf(steer->log, "%u,%.3f,%.3f,%.3f,%.4f,%ld,%d,%d,%d,%d,%u,\"", frame, m[12], m[13],
                    m[14], hypotf(vx_last, vz_last), steer->next, down[0], down[1], down[2],
                    down[3], steered);
            const char *name = nmo_engine_object_name(engine, steered);
            for (const char *p = name != NULL ? name : ""; *p; p++) {
                if (*p == '"') {
                    fputc('"', steer->log);
                }
                fputc(*p, steer->log);
            }
            fputs("\"\n", steer->log);
        }
    }
}

/* A message the host sends before frame `frame` steps, to the object named `recipient`. */
typedef struct run_message {
    char *name;
    uint32_t frame;
    char *recipient;
} run_message_t;

static char *run_strndup(const char *text, size_t length)
{
    char *copy = (char *)malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

/*
 * Parse "NAME@FRAME:OBJECT,..." into `out` (when not NULL; the names are
 * allocated); the count, or -1 when the text is malformed. A message name
 * ends at its '@' and an object name at the next ',', so both may hold
 * spaces.
 */
static long run_parse_messages(const char *spec, run_message_t *out)
{
    long count = 0;
    const char *p = spec;
    while (p != NULL && *p != '\0') {
        const char *at = strchr(p, '@');
        if (at == NULL || at == p) {
            return -1;
        }
        const char *name = p;
        size_t name_length = (size_t)(at - p);
        p = at + 1;
        uint32_t frame = 0;
        if (!run_frame_number(&p, &frame) || *p != ':') {
            return -1;
        }
        const char *recipient = ++p;
        const char *end = strchr(p, ',');
        size_t recipient_length = end != NULL ? (size_t)(end - p) : strlen(p);
        if (recipient_length == 0) {
            return -1;
        }
        if (out != NULL) {
            out[count].name = run_strndup(name, name_length);
            out[count].frame = frame;
            out[count].recipient = run_strndup(recipient, recipient_length);
            if (out[count].name == NULL || out[count].recipient == NULL) {
                return -1;
            }
        }
        count++;
        p = end != NULL ? end + 1 : NULL;
    }
    return count;
}

/* Send the messages due before frame `frame`; a warning for a type or object the run lacks */
static void run_send_messages(nmo_engine_t *engine, const run_message_t *messages, long count,
                              uint32_t frame)
{
    for (long i = 0; i < count; i++) {
        if (messages[i].frame != frame) {
            continue;
        }
        int32_t type = nmo_engine_message_type(engine, messages[i].name);
        nmo_object_id_t recipient = nmo_engine_object_named(engine, messages[i].recipient, 0);
        if (type < 0 || recipient == 0) {
            fprintf(stderr, "Warning: --message %s@%u:%s: no %s of that name at frame %u\n",
                    messages[i].name, frame, messages[i].recipient,
                    type < 0 ? "message type" : "object", frame);
            continue;
        }
        nmo_engine_send_message(engine, type, recipient, 0);
    }
}

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
    nmo_object_id_t effect_behavior; /* the block an effect event named last */
    nmo_object_id_t effect_target;
    int32_t op_status;
    uint64_t deaths;
    uint64_t death_frame;
    bool finished;
    bool ok;
} run_state_t;

/* "Name#id" from the script model of the file the engine runs it from; NULL on OOM. */
static char *run_label_dup(const nmo_engine_t *engine, nmo_object_id_t id)
{
    size_t length = nmo_engine_label(engine, id, NULL, 0);
    char *label = (char *)malloc(length + 1u);
    if (label != NULL) {
        nmo_engine_label(engine, id, label, length + 1u);
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
    case NMO_ENGINE_EVENT_FILE_LOADED:
        return true;
    case NMO_ENGINE_EVENT_EXECUTE:
        return node != NULL && node->kind == NMO_SCRIPT_NODE_BUILDING_BLOCK;
    default:
        return false;
    }
}

static void run_count_unknown(run_state_t *state, nmo_object_id_t id, const nmo_script_node_t *node)
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
        .first_behavior = id,
        .count = 1,
    };
}

static void run_trace(const nmo_engine_event_t *event, void *user_data)
{
    run_state_t *state = (run_state_t *)user_data;
    if (state->args->control && event->kind == NMO_ENGINE_EVENT_MESSAGE_SENT) {
        const char *message = nmo_engine_message_type_name(state->engine, event->result);
        if (message != NULL && strcmp(message, "Ball Off") == 0) {
            state->deaths++;
            state->death_frame = event->frame;
        }
        if (message != NULL && strcmp(message, "Level_Finish") == 0) state->finished = true;
    }
    const nmo_script_node_t *node =
        event->behavior_id != 0 ? nmo_engine_script_node(state->engine, event->behavior_id)
                                : NULL;
    if (event->kind == NMO_ENGINE_EVENT_EXECUTE && node != NULL &&
        node->kind == NMO_SCRIPT_NODE_BUILDING_BLOCK) {
        state->executions++;
    }
    if (event->kind == NMO_ENGINE_EVENT_UNKNOWN_BLOCK) {
        run_count_unknown(state, event->behavior_id, node);
        state->last_unknown = event->behavior_id;
    }
    if (event->kind == NMO_ENGINE_EVENT_EFFECT) {
        state->effect_behavior = event->behavior_id;
        state->effect_target = event->object_id;
    }
    if (event->kind == NMO_ENGINE_EVENT_OPERATION && event->behavior_id != 0) {
        state->op_behavior = event->behavior_id;
        state->op_status = event->result;
    }
    /* What an execute line notes about the block, from the events just before it */
    const char *note = "";
    char *effect_note = NULL;
    if (event->kind == NMO_ENGINE_EVENT_EXECUTE && event->behavior_id == state->effect_behavior) {
        char *target = state->effect_target != 0
                           ? run_label_dup(state->engine, state->effect_target)
                           : NULL;
        const char *shown = target != NULL ? target : "the scene";
        size_t length = strlen(" (effect on )") + strlen(shown) + 1u;
        effect_note = (char *)malloc(length);
        if (effect_note != NULL) {
            snprintf(effect_note, length, " (effect on %s)", shown);
            note = effect_note;
        }
        free(target);
        state->effect_behavior = 0;
    } else if (event->kind == NMO_ENGINE_EVENT_EXECUTE) {
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
    if (!state->ok || state->args->quiet || !run_event_shown(state->args, event, node)) {
        free(effect_note);
        return;
    }

    const char *kind = nmo_engine_event_kind_name(event->kind);
    char *behavior = event->behavior_id != 0 ? run_label_dup(state->engine, event->behavior_id)
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
        char *object = event->object_id != 0 ? run_label_dup(state->engine, event->object_id)
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
    case NMO_ENGINE_EVENT_FILE_LOADED: {
        const char *file = nmo_engine_loaded_file_name(state->engine, event->result);
        ok = ok && nmo_cli_record_str(item, "file", NULL, file != NULL ? file : "");
        ok = ok && (event->result >= 0
                        ? nmo_cli_record_set_summary_fmt(item, "[%llu]  load \"%s\"",
                                                         (unsigned long long)event->frame,
                                                         file != NULL ? file : "?")
                        : nmo_cli_record_set_summary_fmt(item, "[%llu]  load failed",
                                                         (unsigned long long)event->frame));
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
    free(effect_note);
    if (!ok || !nmo_cli_record_array_add(state->events, item)) {
        if (!ok) {
            nmo_cli_record_free(item);
        }
        state->ok = false;
    }
}

/* With --physics: where each movable physical object ended */
static bool run_log_physics(FILE *log, nmo_engine_t *engine)
{
    for (int32_t i = 0; i < nmo_engine_physics_object_count(engine); i++) {
        nmo_engine_physics_object_t object;
        float m[16];
        if (!nmo_engine_physics_object_at(engine, i, &object) || object.fixed ||
            !nmo_engine_entity_world_matrix(engine, object.entity, m)) {
            continue;
        }
        fprintf(log, "%llu,%u,\"", (unsigned long long)nmo_engine_frame(engine), object.entity);
        const char *name = nmo_engine_object_name(engine, object.entity);
        for (const char *p = name != NULL ? name : ""; *p; p++) {
            if (*p == '"') {
                fputc('"', log);
            }
            fputc(*p, log);
        }
        fputc('"', log);
        for (int k = 0; k < 16; k++) {
            fprintf(log, ",%.9g", (double)m[k]);
        }
        fputc('\n', log);
    }
    return !ferror(log);
}

static bool run_add_physics(nmo_cli_record_t *rec, const run_state_t *state)
{
    if (!state->args->physics_ivp) {
        return true;
    }
    nmo_cli_record_array_t *list =
        nmo_cli_record_array(rec, "physics_objects", "Movable physical objects");
    if (list == NULL) {
        return false;
    }
    for (int32_t i = 0; i < nmo_engine_physics_object_count(state->engine); i++) {
        nmo_engine_physics_object_t object;
        float m[16];
        if (!nmo_engine_physics_object_at(state->engine, i, &object) || object.fixed ||
            !nmo_engine_entity_world_matrix(state->engine, object.entity, m)) {
            continue;
        }
        const char *name = nmo_engine_object_name(state->engine, object.entity);
        nmo_cli_record_t *item = nmo_cli_record_new();
        bool ok = item != NULL &&
                  nmo_cli_record_uint(item, "id", NULL, object.entity) &&
                  nmo_cli_record_str(item, "name", NULL, name != NULL ? name : "") &&
                  nmo_cli_record_set_summary_fmt(item, "  %s#%u at (%.3f, %.3f, %.3f)",
                                                 name != NULL ? name : "", object.entity,
                                                 (double)m[12], (double)m[13], (double)m[14]);
        if (!ok || !nmo_cli_record_array_add(list, item)) {
            if (!ok) {
                nmo_cli_record_free(item);
            }
            return false;
        }
    }
    return true;
}

static bool run_add_observed(nmo_cli_record_t *rec, const run_state_t *state)
{
    if (state->args->observe == NULL) {
        return true;
    }
    nmo_cli_record_array_t *list = nmo_cli_record_array(rec, "observed_entities", "Observed entities");
    if (list == NULL) {
        return false;
    }
    const char *cursor = state->args->observe;
    while (*cursor) {
        const char *end = strchr(cursor, ',');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        char *name = malloc(length + 1);
        if (name == NULL) {
            return false;
        }
        memcpy(name, cursor, length);
        name[length] = '\0';
        nmo_object_id_t id = nmo_engine_object_named(state->engine, name, NMO_CID_3DENTITY);
        float matrix[16];
        bool found = id != 0 && nmo_engine_entity_world_matrix(state->engine, id, matrix);
        nmo_cli_record_t *item = nmo_cli_record_new();
        bool ok = item != NULL && nmo_cli_record_str(item, "name", NULL, name) &&
                  nmo_cli_record_uint(item, "id", NULL, id) &&
                  nmo_cli_record_bool(item, "found", NULL, found);
        for (int k = 0; ok && found && k < 16; k++) {
            char key[8];
            snprintf(key, sizeof(key), "m%d", k);
            ok = nmo_cli_record_real(item, key, NULL, matrix[k], "%.9g");
        }
        free(name);
        if (!ok) {
            nmo_cli_record_free(item);
            return false;
        }
        if (!nmo_cli_record_array_add(list, item)) {
            return false;
        }
        cursor += length;
        if (*cursor) {
            cursor++;
        }
    }
    return true;
}

static bool run_observe_matches(const char *patterns, const char *name)
{
    if (patterns == NULL || name == NULL) {
        return false;
    }
    for (const char *pattern = patterns; *pattern;) {
        const char *end = strchr(pattern, ',');
        size_t length = end != NULL ? (size_t)(end - pattern) : strlen(pattern);
        bool prefix = length > 0 && pattern[length - 1] == '*';
        size_t match_length = length - (prefix ? 1u : 0u);
        bool length_matches = prefix ? strlen(name) >= match_length : strlen(name) == match_length;
        if (length_matches && memcmp(name, pattern, match_length) == 0) {
            return true;
        }
        if (end == NULL) {
            break;
        }
        pattern = end + 1;
    }
    return false;
}

/* Controlled runs emit one JSON line at each pause, then wait without stepping.
 * "step COUNT MASK" holds Up/Down/Left/Right bits 1/2/4/8 for COUNT frames.
 * "step 0 0" inspects the same state; EOF or "quit" releases the engine. */
static bool run_control_snapshot(nmo_engine_t *engine, const run_args_t *args, const run_state_t *state)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (doc == NULL) {
        return false;
    }
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    bool ok = root != NULL && yyjson_mut_obj_add_uint(doc, root, "frame", nmo_engine_frame(engine)) &&
              yyjson_mut_obj_add_real(doc, root, "time_ms", nmo_engine_time(engine));
    yyjson_mut_val *gameplay = yyjson_mut_obj_add_obj(doc, root, "gameplay");
    ok = ok && gameplay != NULL && yyjson_mut_obj_add_uint(doc, gameplay, "deaths", state->deaths) &&
         yyjson_mut_obj_add_uint(doc, gameplay, "death_frame", state->death_frame) &&
         yyjson_mut_obj_add_bool(doc, gameplay, "finished", state->finished);
    yyjson_mut_val *entities = yyjson_mut_obj_add_arr(doc, root, "entities");
    ok = ok && entities != NULL;
    for (size_t i = 0; ok && i < nmo_engine_entity_count(engine); i++) {
        nmo_object_id_t id = nmo_engine_entity_id(engine, i);
        const char *name = nmo_engine_object_name(engine, id);
        float matrix[16];
        if (!run_observe_matches(args->observe, name) ||
            !nmo_engine_entity_world_matrix(engine, id, matrix)) {
            continue;
        }
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, entities);
        ok = item != NULL && yyjson_mut_obj_add_uint(doc, item, "id", id) &&
             yyjson_mut_obj_add_strcpy(doc, item, "name", name) &&
             yyjson_mut_obj_add_bool(doc, item, "visible", nmo_engine_object_is_visible(engine, id));
        yyjson_mut_val *pose = yyjson_mut_obj_add_arr(doc, item, "matrix");
        ok = ok && pose != NULL;
        for (int k = 0; ok && k < 16; k++) {
            ok = yyjson_mut_arr_add_real(doc, pose, matrix[k]);
        }
    }
    char *json = ok ? yyjson_mut_write(doc, 0, NULL) : NULL;
    ok = json != NULL && puts(json) >= 0 && fflush(stdout) == 0;
    free(json);
    yyjson_mut_doc_free(doc);
    return ok;
}

static int run_control_request(nmo_engine_t *engine, const run_args_t *args, const run_state_t *state)
{
    for (;;) {
        if (!run_control_snapshot(engine, args, state)) {
            return -1;
        }
        char line[128], extra;
        unsigned count = 0, mask = 0;
        if (fgets(line, sizeof(line), stdin) == NULL || strcmp(line, "quit\n") == 0) {
            return 0;
        }
        if (sscanf(line, "step %u %u %c", &count, &mask, &extra) != 2 || count > 10000 || mask > 15) {
            fprintf(stderr, "Error: control expects step COUNT(0..10000) MASK(0..15), or quit\n");
            return -1;
        }
        const char *arrows[] = {"Up", "Down", "Left", "Right"};
        for (int k = 0; k < 4; k++) {
            uint32_t key = run_key_code(arrows[k], strlen(arrows[k]));
            nmo_engine_set_key(engine, key, (mask & (1u << k)) != 0);
        }
        if (count != 0) {
            return (int)count;
        }
    }
}

static bool run_add_failure(nmo_cli_record_t *rec, const nmo_engine_t *engine)
{
    nmo_engine_event_t event;
    bool failed = nmo_engine_failure(engine, &event);
    if (!nmo_cli_record_bool(rec, "failed", "Failed", failed)) {
        return false;
    }
    if (!failed) {
        return true;
    }
    nmo_cli_record_t *failure = nmo_cli_record_object(rec, "failure");
    char *behavior = event.behavior_id != 0 ? run_label_dup(engine, event.behavior_id) : NULL;
    char *object = event.object_id != 0 ? run_label_dup(engine, event.object_id) : NULL;
    bool ok = failure != NULL && (event.behavior_id == 0 || behavior != NULL) &&
              (event.object_id == 0 || object != NULL) &&
              nmo_cli_record_str(failure, "kind", "Failure kind", nmo_engine_event_kind_name(event.kind)) &&
              nmo_cli_record_uint(failure, "frame", "Failure frame", event.frame) &&
              nmo_cli_record_real(failure, "time_ms", NULL, event.time, "%.3f") &&
              nmo_cli_record_uint(failure, "behavior_id", NULL, event.behavior_id) &&
              nmo_cli_record_uint(failure, "object_id", NULL, event.object_id) &&
              nmo_cli_record_int(failure, "result", "Failure result", event.result);
    if (ok && behavior != NULL) {
        ok = nmo_cli_record_str(failure, "behavior", "Failure behavior", behavior);
    }
    if (ok && object != NULL) {
        ok = nmo_cli_record_str(failure, "object", "Failure object", object);
    }
    free(behavior);
    free(object);
    return ok;
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
        const nmo_script_node_t *node = nmo_engine_script_node(state->engine, u->first_behavior);
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

/* The operations asked for with no function for their types: their outputs never changed */
static bool run_add_missing_operations(nmo_cli_record_t *rec, const nmo_engine_t *engine)
{
    nmo_cli_record_array_t *list =
        nmo_cli_record_array(rec, "missing_operations", "Operations without a function");
    if (list == NULL) {
        return false;
    }
    nmo_cli_record_array_omit_empty(list);
    size_t count = 0;
    const nmo_engine_missing_operation_t *missing = nmo_engine_missing_operations(engine, &count);
    for (size_t i = 0; i < count; i++) {
        const nmo_engine_missing_operation_t *m = &missing[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        bool ok = item != NULL && nmo_cli_record_str(item, "operation", NULL, m->operation_name) &&
                  nmo_cli_record_guid(item, "guid", NULL, m->operation) &&
                  nmo_cli_record_str(item, "result", NULL, m->result_name) &&
                  (m->input1_name == NULL || nmo_cli_record_str(item, "input1", NULL, m->input1_name)) &&
                  (m->input2_name == NULL || nmo_cli_record_str(item, "input2", NULL, m->input2_name)) &&
                  nmo_cli_record_uint(item, "runs", NULL, m->count) &&
                  nmo_cli_record_set_summary_fmt(item, "  %s: %s <- %s, %s: %u run%s", m->operation_name,
                                                 m->result_name, m->input1_name != NULL ? m->input1_name : "None",
                                                 m->input2_name != NULL ? m->input2_name : "None", m->count,
                                                 m->count == 1u ? "" : "s");
        if (!ok || !nmo_cli_record_array_add(list, item)) {
            if (!ok) {
                nmo_cli_record_free(item);
            }
            return false;
        }
    }
    return true;
}

static bool run_add_active_scripts(nmo_cli_record_t *rec, const nmo_engine_t *engine)
{
    nmo_cli_record_array_t *list =
        nmo_cli_record_array(rec, "active_scripts", "Active scripts");
    if (list == NULL) {
        return false;
    }
    /* The scripts of every file the engine runs, loaded ones too */
    int32_t count = nmo_engine_script_count(engine);
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t id = nmo_engine_script_id(engine, i);
        if (!nmo_engine_behavior_is_active(engine, id)) {
            continue;
        }
        const char *name = nmo_engine_object_name(engine, id);
        char *label = run_label_dup(engine, id);
        nmo_cli_record_t *item = nmo_cli_record_new();
        bool ok = label != NULL && item != NULL &&
                  nmo_cli_record_uint(item, "id", NULL, id) &&
                  nmo_cli_record_str(item, "name", NULL, name != NULL ? name : "") &&
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

/*
 * Where the files a document loads are found: --write, --path, then the run
 * file's folder; and where the files it saves go: --write, or nowhere.
 */
typedef struct run_files {
    const char *write;
    const char *path;
    char *folder;
    char *found; /* the path last given to the engine */
    /* A window run without --write saves into a folder of its own, removed at the end */
    char *temporary;
    char **written; /* the files saved there */
    size_t written_count;
    char **directories; /* the folders the file blocks made there */
    size_t directory_count;
    nmo_engine_t *engine;
} run_files_t;

static char *run_join(const char *folder, const char *name, size_t name_length)
{
    size_t folder_length = folder != NULL ? strlen(folder) : 0u;
    char *joined = (char *)malloc(folder_length + name_length + 2u);
    if (joined == NULL) {
        return NULL;
    }
    size_t n = 0;
    if (folder_length > 0) {
        memcpy(joined, folder, folder_length);
        n = folder_length;
        if (joined[n - 1] != '/') {
            joined[n++] = '/';
        }
    }
    for (size_t i = 0; i < name_length; i++) {
        joined[n++] = name[i] == '\\' ? '/' : name[i];
    }
    joined[n] = '\0';
    return joined;
}

static bool run_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

/*
 * The engine's file resolver: a name relative to the run file's folder (or
 * --path), with Windows separators; its last part alone as well, for files
 * copied into one folder. An absolute name (the machine the file was made
 * on: "E:\\Game\\Database.tdb") or one leaving the folder is found by its
 * last part only, so nothing outside the folders is read; one that leaves it
 * only by leading ".." parts (Ballance's "..\\Sounds\\", from its Bin folder)
 * is also found by the rest of it inside the folders.
 */
static const char *run_resolve_file(void *user_data, const char *name)
{
    run_files_t *files = (run_files_t *)user_data;
    free(files->found);
    files->found = NULL;
    size_t length = strlen(name);
    if (length == 0) {
        return NULL;
    }
    bool outside = name[0] == '/' || name[0] == '\\' || (length > 1 && name[1] == ':');
    for (size_t i = 0; i + 1 < length && !outside; i++) {
        bool starts = i == 0 || name[i - 1] == '/' || name[i - 1] == '\\';
        outside = starts && name[i] == '.' && name[i + 1] == '.';
    }
    const char *base = name;
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (base[0] == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return NULL;
    }
    /* The name past its leading ".." parts, when no other part leaves the folder */
    const char *inner = name;
    while (inner[0] == '.' && inner[1] == '.' && (inner[2] == '\\' || inner[2] == '/')) {
        inner += 3;
    }
    bool inner_inside = inner != name && inner[0] != '/' && inner[0] != '\\' &&
                        (inner[0] == '\0' || inner[1] != ':');
    for (size_t i = 0; inner_inside && inner[i] != '\0' && inner[i + 1] != '\0'; i++) {
        bool starts = i == 0 || inner[i - 1] == '/' || inner[i - 1] == '\\';
        inner_inside = !(starts && inner[i] == '.' && inner[i + 1] == '.');
    }
    const char *folders[3] = {files->write, files->path, files->folder};
    for (int f = 0; f < 3; f++) {
        if (f < 2 && folders[f] == NULL) {
            continue;
        }
        for (int whole = outside ? (inner_inside ? 2 : 0) : 1; whole >= 0; whole--) {
            const char *part = whole == 2 ? inner : whole ? name : base;
            if (whole == 1 && outside) {
                continue;
            }
            char *candidate = run_join(folders[f], part, strlen(part));
            if (candidate != NULL && run_file_exists(candidate)) {
                files->found = candidate;
                return candidate;
            }
            free(candidate);
        }
    }
    return NULL;
}

/*
 * The engine's file writer: a saved file goes into the --write folder under
 * its name's last part (where the resolver finds it first), never next to the
 * run file; without --write nothing is written (a window run has a temporary
 * folder instead, whose files it remembers to remove).
 */
static nmo_status_t run_write_file(void *user_data, const char *name, const void *data,
                                   size_t size)
{
    run_files_t *files = (run_files_t *)user_data;
    if (files->write == NULL) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    const char *base = name;
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (base[0] == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    char *path = run_join(files->write, base, strlen(base));
    FILE *file = path != NULL ? fopen(path, "wb") : NULL;
    if (file == NULL) {
        free(path);
        return NMO_ERR_CANT_OPEN_FILE;
    }
    bool written = size == 0 || fwrite(data, 1, size, file) == size;
    bool closed = fclose(file) == 0;
    bool listed = false;
    for (size_t i = 0; files->temporary != NULL && i < files->written_count && !listed; i++) {
        listed = strcmp(files->written[i], path) == 0;
    }
    char **grown = files->temporary != NULL && !listed
                       ? (char **)realloc(files->written,
                                          (files->written_count + 1u) * sizeof(*grown))
                       : NULL;
    if (grown != NULL) {
        files->written = grown;
        files->written[files->written_count++] = path;
        path = NULL;
    }
    free(path);
    return written && closed ? NMO_OK : NMO_ERR_CANT_WRITE_FILE;
}

/* ---- The file blocks' file system (TT_CopyFile, TT_ListDir, ...) ----
 *
 * The blocks read where the resolver reads (--write, --path, the run file's
 * folder) and change files only inside --write: a name that stays inside it
 * keeps its path there, any other goes there by its last part, as the writer
 * puts saved files. Without --write they change nothing. A window run's
 * temporary folder loses what they made there at the end.
 */

/* An absolute name, or one with a ".." part */
static bool run_name_leaves(const char *name)
{
    size_t length = strlen(name);
    if (length > 0 && (name[0] == '/' || name[0] == '\\' || (length > 1 && name[1] == ':'))) {
        return true;
    }
    for (size_t i = 0; i + 1 < length; i++) {
        bool starts = i == 0 || name[i - 1] == '/' || name[i - 1] == '\\';
        if (starts && name[i] == '.' && name[i + 1] == '.' &&
            (name[i + 2] == '\0' || name[i + 2] == '/' || name[i + 2] == '\\')) {
            return true;
        }
    }
    return false;
}

static const char *run_last_part(const char *name)
{
    const char *base = name;
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

/* `name` in `folder`: at its path when it stays inside, else by its last part; NULL for none */
static char *run_place(const char *folder, const char *name)
{
    const char *part = run_name_leaves(name) ? run_last_part(name) : name;
    if (part[0] == '\0' || strcmp(part, ".") == 0 || strcmp(part, "..") == 0) {
        return NULL;
    }
    size_t length = strlen(part);
    while (length > 0 && (part[length - 1] == '/' || part[length - 1] == '\\')) {
        length--; /* "Save\\" names the folder Save */
    }
    return length > 0 ? run_join(folder, part, length) : NULL;
}

static bool run_is_directory(const char *path)
{
    struct stat info;
    return path != NULL && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* The folder a name means, in the first of the folders the resolver reads that has it */
static char *run_find_directory(const run_files_t *files, const char *name)
{
    const char *folders[3] = {files->write, files->path, files->folder};
    for (int f = 0; f < 3; f++) {
        if (folders[f] == NULL) {
            continue;
        }
        char *path = name[0] != '\0' ? run_place(folders[f], name) : run_join(folders[f], "", 0);
        if (run_is_directory(path)) {
            return path;
        }
        free(path);
    }
    return NULL;
}

/* A path inside --write to remember: a window run's temporary folder removes it at the end */
static void run_remember(char ***list, size_t *count, const run_files_t *files, const char *path)
{
    if (files->temporary == NULL) {
        return;
    }
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*list)[i], path) == 0) {
            return;
        }
    }
    size_t length = strlen(path);
    char *copy = (char *)malloc(length + 1u);
    char **grown = copy != NULL ? (char **)realloc(*list, (*count + 1u) * sizeof(*grown)) : NULL;
    if (grown == NULL) {
        free(copy);
        return;
    }
    memcpy(copy, path, length + 1u);
    *list = grown;
    (*list)[(*count)++] = copy;
}

static bool run_fs_exists(void *user_data, const char *name)
{
    run_files_t *files = (run_files_t *)user_data;
    if (run_resolve_file(files, name) != NULL) {
        return true;
    }
    char *directory = run_find_directory(files, name);
    free(directory);
    return directory != NULL;
}

static bool run_fs_copy(void *user_data, const char *source, const char *destination, bool overwrite)
{
    run_files_t *files = (run_files_t *)user_data;
    const char *found = run_resolve_file(files, source);
    char *from_path = found != NULL ? run_join(NULL, found, strlen(found)) : NULL;
    char *to_path = files->write != NULL ? run_place(files->write, destination) : NULL;
    bool done = false;
    if (from_path != NULL && to_path != NULL && strcmp(from_path, to_path) != 0 &&
        (overwrite || !run_file_exists(to_path)) && !run_is_directory(to_path)) {
        FILE *in = fopen(from_path, "rb");
        FILE *out = in != NULL ? fopen(to_path, "wb") : NULL;
        done = out != NULL;
        char buffer[4096];
        size_t n;
        while (done && in != NULL && (n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
            done = fwrite(buffer, 1, n, out) == n;
        }
        done = done && !ferror(in);
        if (out != NULL) {
            done = fclose(out) == 0 && done;
            run_remember(&files->written, &files->written_count, files, to_path);
        }
        if (in != NULL) {
            fclose(in);
        }
    }
    free(from_path);
    free(to_path);
    return done;
}

static bool run_fs_remove_file(void *user_data, const char *name)
{
    run_files_t *files = (run_files_t *)user_data;
    char *path = files->write != NULL ? run_place(files->write, name) : NULL;
#ifdef _WIN32
    bool done = path != NULL && _unlink(path) == 0;
#else
    bool done = path != NULL && unlink(path) == 0;
#endif
    free(path);
    return done;
}

static bool run_fs_create_directory(void *user_data, const char *name)
{
    run_files_t *files = (run_files_t *)user_data;
    char *path = files->write != NULL ? run_place(files->write, name) : NULL;
#ifdef _WIN32
    bool done = path != NULL && _mkdir(path) == 0;
#else
    bool done = path != NULL && mkdir(path, 0777) == 0;
#endif
    if (done) {
        run_remember(&files->directories, &files->directory_count, files, path);
    }
    free(path);
    return done;
}

static bool run_fs_remove_directory(void *user_data, const char *name)
{
    run_files_t *files = (run_files_t *)user_data;
    char *path = files->write != NULL ? run_place(files->write, name) : NULL;
#ifdef _WIN32
    bool done = path != NULL && _rmdir(path) == 0;
#else
    bool done = path != NULL && rmdir(path) == 0;
#endif
    free(path);
    return done;
}

/* A folder the resolver can read becomes the current directory, reported by the name given */
static bool run_fs_set_current_directory(void *user_data, const char *name)
{
    run_files_t *files = (run_files_t *)user_data;
    char *directory = run_find_directory(files, name);
    free(directory);
    return directory != NULL && nmo_engine_set_current_directory(files->engine, name) == NMO_OK;
}

static int run_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* A Windows file mask: * and ?, without case; "*.*" is everything */
static bool run_mask_matches(const char *mask, const char *name)
{
    if (strcmp(mask, "*.*") == 0) {
        return true;
    }
    if (*mask == '\0') {
        return *name == '\0';
    }
    if (*mask == '*') {
        for (const char *rest = name;; rest++) {
            if (run_mask_matches(mask + 1, rest)) {
                return true;
            }
            if (*rest == '\0') {
                return false;
            }
        }
    }
    if (*name == '\0') {
        return false;
    }
    if (*mask == '?' || run_lower((unsigned char)*mask) == run_lower((unsigned char)*name)) {
        return run_mask_matches(mask + 1, name + 1);
    }
    return false;
}

static int run_compare_names(const void *a, const void *b)
{
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    for (; *x != '\0' && run_lower((unsigned char)*x) == run_lower((unsigned char)*y); x++, y++) {
    }
    return run_lower((unsigned char)*x) - run_lower((unsigned char)*y);
}

/* FindFirstFile/FindNextFile over the folder of the pattern, in name order as NTFS gives them */
static bool run_fs_list(void *user_data, const char *pattern, void (*found)(void *, const char *),
                        void *context)
{
    run_files_t *files = (run_files_t *)user_data;
    const char *mask = run_last_part(pattern);
    size_t folder_length = (size_t)(mask - pattern);
    char *folder_name = (char *)malloc(folder_length + 1u);
    if (folder_name == NULL) {
        return false;
    }
    memcpy(folder_name, pattern, folder_length);
    folder_name[folder_length] = '\0';
    char *folder = run_find_directory(files, folder_name);
    free(folder_name);
    if (folder == NULL || mask[0] == '\0') {
        free(folder);
        return false;
    }
    char **names = NULL;
    size_t count = 0;
    bool ok = true;
#ifdef _WIN32
    char *search = run_join(folder, "*", 1);
    struct _finddata_t entry;
    intptr_t handle = search != NULL ? _findfirst(search, &entry) : -1;
    free(search);
    for (int more = handle != -1 ? 0 : -1; more == 0 && ok; more = _findnext(handle, &entry)) {
        const char *entry_name = entry.name;
#else
    DIR *dir = opendir(folder);
    struct dirent *entry;
    while (dir != NULL && ok && (entry = readdir(dir)) != NULL) {
        const char *entry_name = entry->d_name;
#endif
        if (!run_mask_matches(mask, entry_name)) {
            continue;
        }
        size_t length = strlen(entry_name);
        char *copy = (char *)malloc(length + 1u);
        char **grown = copy != NULL ? (char **)realloc(names, (count + 1u) * sizeof(*grown)) : NULL;
        ok = grown != NULL;
        if (!ok) {
            free(copy);
            break;
        }
        memcpy(copy, entry_name, length + 1u);
        names = grown;
        names[count++] = copy;
    }
#ifdef _WIN32
    if (handle != -1) {
        _findclose(handle);
    }
#else
    if (dir != NULL) {
        closedir(dir);
    }
#endif
    free(folder);
    if (count > 1) {
        qsort(names, count, sizeof(*names), run_compare_names);
    }
    for (size_t i = 0; i < count; i++) {
        if (ok) {
            found(context, names[i]);
        }
        free(names[i]);
    }
    free(names);
    return ok;
}

/* A new empty folder for a window run's saves; NULL when none could be made */
static char *run_make_temporary_folder(void)
{
#ifdef _WIN32
    char *name = _tempnam(NULL, "nmo-run-");
    if (name != NULL && _mkdir(name) != 0) {
        free(name);
        name = NULL;
    }
    return name;
#else
    const char *base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') {
        base = "/tmp";
    }
    char *name = run_join(base, "nmo-run-XXXXXX", strlen("nmo-run-XXXXXX"));
    if (name != NULL && mkdtemp(name) == NULL) {
        free(name);
        name = NULL;
    }
    return name;
#endif
}

/* What run_files_t holds, with the temporary folder and the files saved in it removed */
static void run_files_release(run_files_t *files)
{
    for (size_t i = 0; i < files->written_count; i++) {
        remove(files->written[i]);
        free(files->written[i]);
    }
    free(files->written);
    for (size_t i = files->directory_count; i > 0; i--) {
#ifdef _WIN32
        _rmdir(files->directories[i - 1]);
#else
        rmdir(files->directories[i - 1]);
#endif
        free(files->directories[i - 1]);
    }
    free(files->directories);
    if (files->temporary != NULL) {
#ifdef _WIN32
        _rmdir(files->temporary);
#else
        rmdir(files->temporary);
#endif
        free(files->temporary);
    }
    free(files->folder);
    free(files->found);
}

/* The folder of `file`, "" for the current one. */
static char *run_folder_of(const char *file)
{
    const char *slash = file != NULL ? strrchr(file, '/') : NULL;
    size_t length = slash != NULL ? (size_t)(slash - file) : 0u;
    char *folder = (char *)malloc(length + 1u);
    if (folder != NULL) {
        memcpy(folder, file, length);
        folder[length] = '\0';
    }
    return folder;
}

/*
 * The path manager's paths a game player adds (GamePlayer.cpp): the
 * document's folder for data, sounds and bitmaps, and its Sounds and
 * Textures folders.
 */
static bool run_add_paths(nmo_engine_t *engine)
{
    static const struct {
        int32_t category; /* 0 bitmaps, 1 data, 2 sounds */
        const char *path;
    } paths[] = {{1, "."}, {2, "."}, {2, "Sounds"}, {0, "."}, {0, "Textures"}};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (nmo_engine_add_path(engine, paths[i].category, paths[i].path) != NMO_OK) {
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

/*
 * The registry values of --registry: KEY\NAME=VALUE entries separated by
 * ';', set in `engine` when given. False when an entry is not one.
 */
static bool run_set_registry(nmo_engine_t *engine, const char *text)
{
    while (text != NULL && *text != '\0') {
        const char *end = strchr(text, ';');
        size_t length = end != NULL ? (size_t)(end - text) : strlen(text);
        const char *equals = memchr(text, '=', length);
        const char *slash = NULL;
        for (const char *p = text; equals != NULL && p < equals; p++) {
            if (*p == '\\') {
                slash = p;
            }
        }
        if (slash == NULL || slash == text || slash + 1 == equals) {
            return false;
        }
        if (engine != NULL) {
            char *entry = (char *)malloc(length + 1u);
            if (entry == NULL) {
                return false;
            }
            memcpy(entry, text, length);
            entry[length] = '\0';
            entry[slash - text] = '\0';
            entry[equals - text] = '\0';
            nmo_status_t status = nmo_engine_set_registry_value(
                engine, entry, entry + (slash - text) + 1, entry + (equals - text) + 1);
            free(entry);
            if (status != NMO_OK) {
                return false;
            }
        }
        text = end != NULL ? end + 1 : NULL;
    }
    return true;
}

#ifdef NMO_HAS_VIEW
/* Save the window if the --shot list names frame `frame`: FILE@FRAME,... */
/*
 * The player's loop (CGamePlayer, CKTimeManager): wait out the frame rate
 * limit when the game set one (vertical sync paces the others), then step
 * by the time that passed, as the time manager bounds and scales it.
 */
static float run_real_time_delta(nmo_engine_t *engine, nmo_view_t *view, double *last_frame)
{
    nmo_engine_time_settings_t settings;
    nmo_engine_time_settings(engine, &settings);
    double now = nmo_view_time(view);
    if ((settings.limit_options & 0x40u) != 0 && settings.frame_rate_limit > 0.0f) {
        double wait = 1000.0 / settings.frame_rate_limit - (now - *last_frame);
        if (wait > 0.0) {
            nmo_view_wait(view, wait);
            now = nmo_view_time(view);
        }
    }
    float delta = nmo_engine_frame_delta(engine, (float)(now - *last_frame));
    *last_frame = now;
    return delta;
}

/*
 * A window stepping a fixed time a frame (--delta, --shot) shows each frame
 * for that long, so that it plays at the game's own speed rather than at
 * the display's refresh rate, evenly.
 */
static void run_pace(nmo_view_t *view, float delta, double *last_frame)
{
    double now = nmo_view_time(view);
    double wait = (double)delta - (now - *last_frame);
    if (wait > 0.0) {
        nmo_view_wait(view, wait);
        *last_frame += (double)delta;
    } else {
        *last_frame = now; /* behind: start afresh rather than rush to catch up */
    }
}

static void run_take_shots(nmo_view_t *view, const char *shots, uint64_t frame)
{
    for (const char *item = shots; item != NULL && *item != '\0';) {
        const char *end = strchr(item, ',');
        size_t length = end != NULL ? (size_t)(end - item) : strlen(item);
        const char *at = memchr(item, '@', length);
        if (at != NULL && strtoull(at + 1, NULL, 10) == frame) {
            char *path = run_strndup(item, (size_t)(at - item));
            if (path != NULL && nmo_view_screenshot(view, path) != NMO_OK) {
                fprintf(stderr, "Error: Could not save %s\n", path);
            }
            free(path);
        }
        item = end != NULL ? end + 1 : NULL;
    }
}
#endif

static int run_execute(nmo_cmd_ctx_t *ctx, const run_args_t *args)
{
    nmo_engine_options_t options = nmo_engine_options_default();
    options.unknown_block = args->unknown;
    options.max_iterations = (int32_t)args->max_iterations;
    options.random_seed = args->seed;
    options.fail_on = args->strict ? NMO_ENGINE_FAIL_ALL : 0;

    nmo_engine_t *engine = NULL;
    nmo_status_t status = nmo_engine_create(ctx->workspace, &options, &engine);
    /* The block packs, as a player loads the building block DLLs */
    if (status == NMO_OK) {
        status = nmo_bb_virtools_register(engine);
    }
    if (status == NMO_OK) {
        status = nmo_bb_physics_register(engine);
    }
    if (status == NMO_OK) {
        status = nmo_bb_terratools_register(engine);
    }
    if (status != NMO_OK) {
        fprintf(stderr, "Error: Failed to create the behavior engine (%s)\n",
                nmo_error_string(status));
        nmo_engine_destroy(engine);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
#ifdef NMO_HAS_PHYSICS_IVP
    nmo_physics_ivp_t *physics = NULL;
    if (args->physics_ivp && nmo_physics_ivp_attach(engine, &physics) != NMO_OK) {
        fprintf(stderr, "Error: Out of memory\n");
        nmo_engine_destroy(engine);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
#endif
#ifdef NMO_HAS_VIEW
    nmo_view_t *view = NULL;
    if (args->window && nmo_view_open(engine, ctx->file_path, 2, &view) != NMO_OK) {
#ifdef NMO_HAS_PHYSICS_IVP
        nmo_physics_ivp_detach(physics);
#endif
        nmo_engine_destroy(engine);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_view_set_mipmaps(view, args->mipmaps);
#endif
    const nmo_script_model_t *model = nmo_engine_script_model(engine);
    run_files_t files = {.write = args->write, .path = args->path,
                         .folder = run_folder_of(ctx->file_path)};
    if (files.write == NULL && args->window) {
        files.temporary = run_make_temporary_folder();
        files.write = files.temporary;
    }
    nmo_engine_set_file_resolver(engine, run_resolve_file, &files);
    nmo_engine_set_file_writer(engine, run_write_file, &files);
    files.engine = engine;
    const nmo_engine_file_system_t file_system = {
        &files, run_fs_exists, run_fs_copy, run_fs_remove_file, run_fs_create_directory,
        run_fs_remove_directory, run_fs_set_current_directory, run_fs_list};
    nmo_engine_set_file_system(engine, &file_system);
    if (!run_set_registry(engine, args->registry) || !run_add_paths(engine)) {
        fprintf(stderr, "Error: Out of memory\n");
#ifdef NMO_HAS_PHYSICS_IVP
        nmo_physics_ivp_detach(physics);
#endif
        nmo_engine_destroy(engine);
        run_files_release(&files);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

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
    long key_count = args->keys != NULL ? run_parse_keys(args->keys, NULL) : 0;
    run_key_t *keys = key_count > 0 ? (run_key_t *)calloc((size_t)key_count, sizeof(run_key_t))
                                    : NULL;
    if (key_count > 0 && keys == NULL) {
        status = NMO_ERR_NOMEM;
    } else if (keys != NULL) {
        run_parse_keys(args->keys, keys);
    }
    long message_count = args->messages != NULL ? run_parse_messages(args->messages, NULL) : 0;
    run_message_t *messages = message_count > 0
                                  ? (run_message_t *)calloc((size_t)message_count, sizeof(run_message_t))
                                  : NULL;
    if ((message_count > 0 && messages == NULL) ||
        (messages != NULL && run_parse_messages(args->messages, messages) != message_count)) {
        status = NMO_ERR_NOMEM;
    }
    run_steer_t steer = {0};
    if (args->steer != NULL && !run_parse_steer(args->steer, &steer)) {
        status = NMO_ERR_NOMEM;
    }
    steer.objects = args->steer_objects;
    FILE *physics_log = NULL;
    if (status == NMO_OK && args->physics_log != NULL) {
        physics_log = fopen(args->physics_log, "w");
        if (physics_log == NULL) {
            fprintf(stderr, "Error: cannot write %s\n", args->physics_log);
            status = NMO_ERR_INVALID_ARGUMENT;
        } else {
            fputs("frame,id,name,m0,m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15\n", physics_log);
        }
    }
    if (status == NMO_OK && args->steer_log != NULL) {
        steer.log = fopen(args->steer_log, "w");
        if (steer.log == NULL) {
            fprintf(stderr, "Error: cannot write %s\n", args->steer_log);
            status = NMO_ERR_INVALID_ARGUMENT;
        } else {
            fputs("frame,x,y,z,speed,next,up,down,left,right,object_id,object_name\n", steer.log);
        }
    }
#ifdef NMO_HAS_VIEW
    double last_frame = view != NULL ? nmo_view_time(view) : 0.0;
#endif
    int control_remaining = 0;
    for (uint32_t f = 0;
         status == NMO_OK &&
         (args->control || f < args->frames || (args->window && !args->frames_given));
         f++) {
        if (args->control && control_remaining == 0) {
            control_remaining = run_control_request(engine, args, &state);
            if (control_remaining <= 0) {
                if (control_remaining < 0) {
                    status = NMO_ERR_INVALID_ARGUMENT;
                }
                break;
            }
        }
        /* Release steering once, before collecting this frame's player
           input. Later frames must leave the player's arrow keys alone. */
        if (steer.points != NULL && f > steer.last && f - steer.last == 1) {
            const char *const arrows[] = {"Up", "Down", "Left", "Right"};
            for (int k = 0; k < 4; k++) {
                nmo_engine_set_key(engine, run_key_code(arrows[k], strlen(arrows[k])), false);
            }
        }
#ifdef NMO_HAS_VIEW
        if (view != NULL && !nmo_view_poll(view)) {
            break;
        }
#endif
        run_send_messages(engine, messages, message_count, f);
        /* A key is down in a frame one of its ranges covers */
        for (long k = 0; k < key_count; k++) {
            bool down = false;
            for (long j = 0; j < key_count; j++) {
                down = down || (keys[j].key == keys[k].key && f >= keys[j].first &&
                                f <= keys[j].last);
            }
            nmo_engine_set_key(engine, keys[k].key, down);
        }
        if (steer.points != NULL) {
            run_steer_frame(engine, &steer, f);
        }
        float delta = args->delta_ms;
#ifdef NMO_HAS_VIEW
        if (view != NULL && args->real_time) {
            delta = run_real_time_delta(engine, view, &last_frame);
        } else if (view != NULL) {
            run_pace(view, delta, &last_frame);
        }
#endif
        uint64_t deaths_before = state.deaths;
        status = nmo_engine_step(engine, delta);
        if (args->control) {
            control_remaining--;
        }
        /* Stop on the native terminal event, before automatic respawn can hide
         * the failed attempt from an interactive authoring client. */
        if (args->control && (state.deaths != deaths_before || state.finished)) {
            control_remaining = 0;
        }
        if (status == NMO_OK && physics_log != NULL && !run_log_physics(physics_log, engine)) {
            status = NMO_ERR_INVALID_ARGUMENT;
        }
#ifdef NMO_HAS_VIEW
        if (status == NMO_OK && view != NULL) {
            status = nmo_view_draw(view);
            if (!args->mute) {
                nmo_view_play_sounds(view);
            }
            run_take_shots(view, args->shots, nmo_engine_frame(engine));
        }
#endif
        if (status == NMO_OK && nmo_engine_exit_requested(engine)) {
            break; /* the player quits after the frame */
        }
    }
    free(keys);
    run_free_steer(&steer);
    if (steer.log != NULL) {
        bool failed = ferror(steer.log) != 0;
        if (fclose(steer.log) != 0 || failed) {
            fprintf(stderr, "Error: cannot finish writing %s\n", args->steer_log);
            status = NMO_ERR_INVALID_ARGUMENT;
        }
    }
    if (physics_log != NULL && fclose(physics_log) != 0) {
        fprintf(stderr, "Error: cannot finish writing %s\n", args->physics_log);
        status = NMO_ERR_INVALID_ARGUMENT;
    }
    for (long i = 0; messages != NULL && i < message_count; i++) {
        free(messages[i].name);
        free(messages[i].recipient);
    }
    free(messages);
    if (status != NMO_OK && exit_code == NMO_CLI_EXIT_SUCCESS) {
        if (status == NMO_ERR_VALIDATION_FAILED && nmo_engine_failure(engine, NULL)) {
            exit_code = NMO_CLI_EXIT_STRICT_FAILURE;
        } else {
            fprintf(stderr, "Error: Behavior engine failed (%s)\n", nmo_error_string(status));
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }
    nmo_engine_set_trace(engine, NULL, NULL);

    bool report = exit_code == NMO_CLI_EXIT_SUCCESS || exit_code == NMO_CLI_EXIT_STRICT_FAILURE;
    bool ok = state.ok && report;
    ok = ok && nmo_cli_record_uint(rec, "frames", "Frames", nmo_engine_frame(engine));
    ok = ok && nmo_cli_record_real(rec, "delta_ms", "Frame time (ms)", args->delta_ms, "%.3f");
    ok = ok && nmo_cli_record_real(rec, "time_ms", "Time (ms)", nmo_engine_time(engine), "%.3f");
    ok = ok && nmo_cli_record_str(rec, "start", "Start", args->script_id != 0
                                                          ? "script"
                                                          : run_start_name(args->start));
    ok = ok && nmo_cli_record_uint(rec, "block_executions", "Building block executions",
                                   state.executions);
    ok = ok && nmo_cli_record_bool(rec, "exited", "Exited to system",
                                   nmo_engine_exit_requested(engine));
    ok = ok && run_add_failure(rec, engine);
    ok = ok && run_add_unknown(rec, &state);
    ok = ok && run_add_missing_operations(rec, engine);
    ok = ok && run_add_physics(rec, &state);
    ok = ok && run_add_observed(rec, &state);
    ok = ok && run_add_active_scripts(rec, engine);

    if (report) {
        if (!ok) {
            nmo_cli_record_free(rec);
            rec = NULL;
        }
        int output_status = nmo_cmd_ctx_emit_record(ctx, rec, "behavior.run", 0, false);
        if (output_status != NMO_CLI_EXIT_SUCCESS) {
            exit_code = output_status;
        }
    } else {
        nmo_cli_record_free(rec);
    }
    free(state.unknown);
#ifdef NMO_HAS_VIEW
    nmo_view_close(view);
#endif
#ifdef NMO_HAS_PHYSICS_IVP
    nmo_physics_ivp_detach(physics);
#endif
    nmo_engine_destroy(engine);
    run_files_release(&files);
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
        {"--strict", NULL, NMO_OPT_FLAG,
         "Exit with code 3 on an unknown block, execution error or iteration limit"},
        {"--max-iterations", NULL, NMO_OPT_UINT, "Iterations per graph and frame (default: 8000)"},
        {"--seed", NULL, NMO_OPT_UINT, "Random seed (default: 1)"},
        {"--trace", "-t", NMO_OPT_FLAG, "Show links, operations and graphs too"},
        {"--keys", "-k", NMO_OPT_STRING,
         "Keys held down: KEY@FRAME[-FRAME],... (names like Space, or scan codes)"},
        {"--steer", NULL, NMO_OPT_STRING,
         "From FRAME (to FRAME), roll --steer-object through the points with the arrow keys, "
         "waiting at a point with a Y until it is that high, with an F for F more frames, and "
         "with a NAME until that object is within REACH of WX,WZ, run at flat out where F "
         "is rush, track narrow beams slowly where F is precision, and head for it N frames, "
         "reached or not (or till Y), where F is push:N: "
         "X,Z[,Y[,F[,NAME,WX,WZ,REACH]]];...@FRAME[-FRAME] (empty Y or F: none)"},
        {"--steer-object", NULL, NMO_OPT_STRING,
         "The objects --steer rolls: NAME,NAME,... (the first one shown)"},
        {"--steer-log", NULL, NMO_OPT_STRING,
         "Write the steered object's frames to a CSV file: frame, x, y, z, speed, the point "
         "it heads for, and the arrow keys held"},
        {"--physics-log", NULL, NMO_OPT_STRING,
         "Write movable physical bodies after every step: CSV frame, id, name, and 16 world matrix values"},
        {"--control", NULL, NMO_OPT_FLAG, "Pause for stdin step COUNT MASK commands; emit JSON pose snapshots"},
        {"--observe", NULL, NMO_OPT_STRING, "Report final world matrices of named entities: NAME,NAME,..."},
        {"--path", NULL, NMO_OPT_STRING,
         "Folder of the files the document loads, before its own folder"},
        {"--write", NULL, NMO_OPT_STRING,
         "Folder the files the document saves go to, read before the others (default: none; "
         "a window run saves into a temporary folder removed at the end); the only folder the "
         "file blocks change"},
        {"--message", NULL, NMO_OPT_STRING,
         "Messages the host sends: NAME@FRAME:OBJECT,... (before that frame steps)"},
        {"--registry", NULL, NMO_OPT_STRING,
         "Registry values the TT blocks read: KEY\\NAME=VALUE;..."},
        {"--physics", NULL, NMO_OPT_STRING,
         "Simulate the physics blocks: none or ivp (default: none; ivp when built with libivp)"},
        {"--window", "-w", NMO_OPT_FLAG,
         "Show the frames in a window and play with its keyboard and mouse, until it closes"},
        {"--shot", NULL, NMO_OPT_STRING, "Save the window at frames: FILE.png@FRAME,..."},
        {"--mute", NULL, NMO_OPT_FLAG, "Play no sound in the window"},
        {"--mipmaps", NULL, NMO_OPT_FLAG,
         "Mipmap every texture in the window but cutouts (default: only those CK mipmaps)"},
        {"--quiet", "-q", NMO_OPT_FLAG,
         "List no events, only the summary (the default for a window played until it closes)"},
    };
    enum { OPT_FRAMES, OPT_DELTA, OPT_START, OPT_SCRIPT, OPT_UNKNOWN, OPT_STRICT, OPT_MAX_ITER, OPT_SEED,
           OPT_TRACE, OPT_KEYS, OPT_STEER, OPT_STEER_OBJECT, OPT_STEER_LOG, OPT_PHYSICS_LOG, OPT_CONTROL, OPT_OBSERVE, OPT_PATH, OPT_WRITE, OPT_MESSAGE, OPT_REGISTRY, OPT_PHYSICS,
           OPT_WINDOW, OPT_SHOT, OPT_MUTE, OPT_MIPMAPS, OPT_QUIET, OPT_COUNT };

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
    out->frames_given = vals[OPT_FRAMES].present;
    out->window = nmo_opt_flag(&vals[OPT_WINDOW]);
    out->shots = nmo_opt_str(&vals[OPT_SHOT]);
    out->mute = nmo_opt_flag(&vals[OPT_MUTE]);
    out->mipmaps = nmo_opt_flag(&vals[OPT_MIPMAPS]);
    if (out->shots != NULL) {
        out->window = true;
    }
    /* A played window runs for minutes: listing its events would grow without end */
    out->quiet = nmo_opt_flag(&vals[OPT_QUIET]) || (out->window && !out->frames_given);
#ifndef NMO_HAS_VIEW
    if (out->window) {
        fprintf(stderr, "Error: --window and --shot need a build with the view (NMO_VIEWER)\n");
        return false;
    }
#endif
    out->delta_ms = nmo_opt_float_or(&vals[OPT_DELTA], 1000.0f / 60.0f);
    out->real_time = out->window && out->shots == NULL && !vals[OPT_DELTA].present;
    out->script_id = nmo_opt_uint_or(&vals[OPT_SCRIPT], 0u);
    out->max_iterations = nmo_opt_uint_or(&vals[OPT_MAX_ITER], NMO_ENGINE_DEFAULT_MAX_ITERATIONS);
    out->seed = nmo_opt_uint_or(&vals[OPT_SEED], 1u);
    out->trace = nmo_opt_flag(&vals[OPT_TRACE]);
    out->strict = nmo_opt_flag(&vals[OPT_STRICT]) || (c->global != NULL && c->global->strict_mode);
    out->keys = nmo_opt_str(&vals[OPT_KEYS]);
    out->steer = nmo_opt_str(&vals[OPT_STEER]);
    out->steer_objects = nmo_opt_str(&vals[OPT_STEER_OBJECT]);
    out->steer_log = nmo_opt_str(&vals[OPT_STEER_LOG]);
    out->physics_log = nmo_opt_str(&vals[OPT_PHYSICS_LOG]);
    out->observe = nmo_opt_str(&vals[OPT_OBSERVE]);
    out->control = nmo_opt_flag(&vals[OPT_CONTROL]);
    if (out->steer_log != NULL && out->steer == NULL) {
        fprintf(stderr, "Error: --steer-log needs --steer\n");
        return false;
    }
    if (out->steer != NULL && !run_parse_steer(out->steer, NULL)) {
        fprintf(stderr, "Error: Invalid --steer: %s\n", out->steer);
        return false;
    }
    if (out->steer != NULL && out->steer_objects == NULL) {
        fprintf(stderr, "Error: --steer needs --steer-object\n");
        return false;
    }
    out->path = nmo_opt_str(&vals[OPT_PATH]);
    out->write = nmo_opt_str(&vals[OPT_WRITE]);
    out->messages = nmo_opt_str(&vals[OPT_MESSAGE]);
    if (out->messages != NULL && run_parse_messages(out->messages, NULL) < 0) {
        fprintf(stderr, "Error: Invalid --message: %s\n", out->messages);
        return false;
    }
    out->registry = nmo_opt_str(&vals[OPT_REGISTRY]);
    if (!run_set_registry(NULL, out->registry)) {
        fprintf(stderr, "Error: Invalid --registry: %s\n", out->registry);
        return false;
    }
    if (out->keys != NULL && run_parse_keys(out->keys, NULL) < 0) {
        fprintf(stderr, "Error: Invalid --keys: %s\n", out->keys);
        return false;
    }
    if (out->delta_ms < 0.0f || out->max_iterations > (uint32_t)INT32_MAX) {
        return false;
    }

    if (out->control) {
        if (out->window || out->steer != NULL || out->keys != NULL || out->observe == NULL || !expect_file) {
            fprintf(stderr, "Error: --control needs --observe and a file, without --window, --steer or --keys\n");
            return false;
        }
        out->quiet = true;
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

    const char *physics = nmo_opt_str(&vals[OPT_PHYSICS]);
    out->physics_ivp = physics != NULL && strcmp(physics, "ivp") == 0;
    if (physics != NULL && strcmp(physics, "none") != 0 && !out->physics_ivp) {
        return false;
    }
#ifndef NMO_HAS_PHYSICS_IVP
    if (out->physics_ivp) {
        fprintf(stderr, "Error: --physics ivp needs a build with libivp (NMO_IVP_SOURCE_DIR)\n");
        return false;
    }
#endif

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
