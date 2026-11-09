/**
 * @file particles.c
 * @brief Ports of the TT particle system building blocks, without rendering.
 *
 * Each function does what the block's function in TT_ParticleSystems_RT.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ). The emitters keep their particles in the render state and
 * draw them as their render modes do; the interactors and deflectors are
 * not ported, and each emitter has its own random numbers so that particles
 * do not change the gameplay's.
 */

#include "../bb_sdk.h"
#include "terratools.h"

#include <math.h>
#include <string.h>

/* ============================================================================
 * Particle systems
 * ============================================================================ */

/* GeneralParticleSystem's inputs and its Activity local */
#define PS_EMISSION_DELAY 0

#define PS_LIFE 8

#define PS_LIFE_VARIATION 9

#define PS_REAL_TIME_MODE 34

#define PS_DELTA_TIME 35

#define PS_ACTIVE 1

#define PS_FROZEN 2

/* The inputs ParticleEmitter::ReadInputs reads (Particle.h) */
enum {
    PI_YAW = 2, PI_PITCH, PI_SPEED, PI_SPEED_VAR, PI_ANG_SPEED, PI_ANG_SPEED_VAR,
    PI_NUMBER = 10, PI_EMISSION, PI_EMISSION_VAR, PI_START_SIZE, PI_START_SIZE_VAR, PI_END_SIZE,
    PI_END_SIZE_VAR, PI_START_COLOR = 23, PI_START_COLOR_VAR, PI_END_COLOR, PI_END_COLOR_VAR,
    PI_TEXTURE, PI_START_FRAME, PI_START_FRAME_VAR, PI_FRAME_SPEED, PI_FRAME_SPEED_VAR,
    PI_FRAME_COUNT, PI_FRAME_LOOP
};
/* Its settings, after the Emitter, Activity and EmissionTime locals */
enum { PL_RENDER = 4, PL_SRC_BLEND, PL_DEST_BLEND, PL_EVOLUTIONS = 8, PL_VARIANCES };
#define PE_SIZE 0x1
#define PE_COLOR 0x2
#define PE_TEXTURE 0x4
#define PV_SPEED 0x1
#define PV_ANGULARSPEED 0x2
#define PV_LIFESPAN 0x4
#define PV_EMISSION 0x8
#define PV_INITIALSIZE 0x10
#define PV_ENDINGSIZE 0x20
#define PV_INITIALCOLOR 0x200
#define PV_ENDINGCOLOR 0x400
#define PV_INITIALTEXTURE 0x800
#define PV_SPEEDTEXTURE 0x1000
#define PR_LINE 2
#define PR_SPRITE 3
#define PR_OSPRITE 5
#define PR_FSPRITE 7
#define PR_CSPRITE 8
#define PL_NOLOOP 0
#define PL_LOOP 1
#define PL_PINGPONG 2

#define GUID_SPHERICAL NMO_GUID(0x67c88f47, 0x8880721e)
#define GUID_PLANAR NMO_GUID(0x49957bfe, 0x0ff27ffc)
#define GUID_TIME_POINT NMO_GUID(0x569d2cc2, 0x3bcb01b9)
#define GUID_WAVE NMO_GUID(0x02da5a18, 0x52227285)
#define PR_OBJECT 4

/* The values ReadInputs keeps on the emitter */
typedef struct emitter_inputs {
    float yaw, pitch, speed, speed_var, ang_speed, ang_speed_var;
    float life, life_var;
    int32_t number, emission, emission_var;
    float start_size, start_size_var, end_size, end_size_var;
    float start_color[4], start_color_var[4], end_color[4], end_color_var[4];
    int32_t start_frame, start_frame_var, frame_speed, frame_speed_var, frame_count, frame_loop;
    int32_t evolutions, variances, render_mode;
} emitter_inputs_t;

void eng_free_emitters(nmo_engine_t *engine)
{
    for (size_t i = 0; i < eng_tt(engine)->emitters.count; i++) {
        free(eng_tt(engine)->emitters.data[i].particles.data);
    }
    free(eng_tt(engine)->emitters.data);
}

/* The block's emitter; NULL before it first ran */
static eng_emitter_t *emitter_find(blk_t *b)
{
    eng_tt_t *tt = eng_tt(b->engine);
    for (size_t i = 0; i < tt->emitters.count; i++) {
        if (tt->emitters.data[i].node == b->node) {
            return &tt->emitters.data[i];
        }
    }
    return NULL;
}

static eng_emitter_t *emitter_of(blk_t *b)
{
    eng_emitter_t *emitter = emitter_find(b);
    if (emitter != NULL) {
        return emitter;
    }
    eng_tt_t *tt = eng_tt(b->engine);
    if (!ENG_VEC_PUSH(tt->emitters, emitter)) {
        return NULL;
    }
    emitter->node = b->node;
    emitter->random = 0x9e3779b9u ^ (uint32_t)b->node;
    return emitter;
}

/* rand() of the emitter: RANDNUM in [-1, 1), RANDNUMP in [0, 1) */
static float emitter_rand01(eng_emitter_t *e)
{
    e->random = e->random * 1103515245u + 12345u;
    return (float)((e->random >> 16) & 0x7FFF) / 32767.0f;
}

static float emitter_rand(eng_emitter_t *e)
{
    return emitter_rand01(e) * 2.0f - 1.0f;
}

static void read_inputs(blk_t *b, emitter_inputs_t *in)
{
    memset(in, 0, sizeof(*in));
    in->evolutions = get_local_int(b, PL_EVOLUTIONS, PE_SIZE | PE_COLOR | PE_TEXTURE);
    in->variances = get_local_int(b, PL_VARIANCES, 0);
    in->render_mode = get_local_int(b, PL_RENDER, PR_SPRITE);
    in->yaw = get_in_float(b, PI_YAW, 0.0f);
    in->pitch = get_in_float(b, PI_PITCH, 0.0f);
    in->speed = get_in_float(b, PI_SPEED, 0.005f);
    in->speed_var = in->variances & PV_SPEED ? get_in_float(b, PI_SPEED_VAR, 0.0f) : 0.0f;
    in->ang_speed = get_in_float(b, PI_ANG_SPEED, 0.0f);
    in->ang_speed_var = in->variances & PV_ANGULARSPEED ? get_in_float(b, PI_ANG_SPEED_VAR, 0.0f) : 0.0f;
    in->start_size = get_in_float(b, PI_START_SIZE, 1.0f);
    in->start_size_var = in->variances & PV_INITIALSIZE ? get_in_float(b, PI_START_SIZE_VAR, 0.0f) : 0.0f;
    in->end_size = get_in_float(b, PI_END_SIZE, 0.1f);
    in->end_size_var = in->variances & PV_ENDINGSIZE ? get_in_float(b, PI_END_SIZE_VAR, 0.0f) : 0.0f;
    in->life = get_in_float(b, PS_LIFE, 1000.0f);
    in->life_var = in->variances & PV_LIFESPAN ? get_in_float(b, PS_LIFE_VARIATION, 0.0f) : 0.0f;
    in->number = get_in_int(b, PI_NUMBER, 100);
    in->emission = get_in_int(b, PI_EMISSION, 10);
    in->emission_var = in->variances & PV_EMISSION ? get_in_int(b, PI_EMISSION_VAR, 0) : 0;
    nmo_engine_block_get_input_value(b, PI_START_COLOR, in->start_color, sizeof(in->start_color));
    nmo_engine_block_get_input_value(b, PI_START_COLOR_VAR, in->start_color_var,
                                     sizeof(in->start_color_var));
    nmo_engine_block_get_input_value(b, PI_END_COLOR, in->end_color, sizeof(in->end_color));
    nmo_engine_block_get_input_value(b, PI_END_COLOR_VAR, in->end_color_var,
                                     sizeof(in->end_color_var));
    in->start_frame = get_in_int(b, PI_START_FRAME, 0);
    in->start_frame_var = get_in_int(b, PI_START_FRAME_VAR, 0);
    in->frame_speed = get_in_int(b, PI_FRAME_SPEED, 100);
    in->frame_speed_var = get_in_int(b, PI_FRAME_SPEED_VAR, 0);
    in->frame_count = get_in_int(b, PI_FRAME_COUNT, 1);
    in->frame_loop = get_in_int(b, PI_FRAME_LOOP, PL_NOLOOP);
}

/* The emitter a prototype makes; the other shapes emit as the point one does */
typedef enum emitter_shape { SHAPE_POINT, SHAPE_PLANAR, SHAPE_SPHERICAL, SHAPE_TIME_POINT, SHAPE_WAVE } emitter_shape_t;

static emitter_shape_t emitter_shape(blk_t *b)
{
    nmo_guid_t guid = block_node(b)->prototype;
    if (nmo_guid_equals(guid, GUID_SPHERICAL)) {
        return SHAPE_SPHERICAL;
    }
    if (nmo_guid_equals(guid, GUID_PLANAR)) {
        return SHAPE_PLANAR;
    }
    if (nmo_guid_equals(guid, GUID_WAVE)) {
        return SHAPE_WAVE;
    }
    return nmo_guid_equals(guid, GUID_TIME_POINT) ? SHAPE_TIME_POINT : SHAPE_POINT;
}

/* A particle from the pool, while fewer than Maximum Number live; NULL when there is none */
static eng_particle_t *new_particle(blk_t *b, eng_emitter_t *e, const emitter_inputs_t *in)
{
    int32_t limit = in->number;
    int32_t maximum = get_local_int(b, 3, 100);
    if (maximum > 0 && limit > maximum) {
        limit = maximum;
    }
    eng_particle_t *p = NULL;
    if ((int32_t)e->particles.count >= limit || !ENG_VEC_PUSH(e->particles, p)) {
        return NULL;
    }
    memset(p, 0, sizeof(*p));
    return p;
}

/* ParticleEmitter::InitiateDirection: within the yaw and pitch variances around the entity's Z */
static void initiate_direction(eng_emitter_t *e, const emitter_inputs_t *in, const eng_matrix_t *world,
                               eng_vector_t *dir)
{
    float z = cosf(in->pitch * emitter_rand(e));
    eng_vector_t local;
    local.x = z * sinf(in->yaw * emitter_rand(e));
    local.z = z * cosf(in->yaw * emitter_rand(e));
    local.y = sinf(in->pitch * emitter_rand(e));
    eng_vector_rotate(dir, world, &local);
}

/* The rest of AddParticles for a particle at `pos` going along `dir`: its speed, angle, life,
   colors, sizes and texture frame */
static void init_particle(eng_emitter_t *e, const emitter_inputs_t *in, eng_particle_t *p,
                          const eng_vector_t *pos, const eng_vector_t *dir)
{
    float speed = in->speed + (in->variances & PV_SPEED ? in->speed_var * emitter_rand(e) : 0.0f);
    p->pos[0] = pos->x, p->pos[1] = pos->y, p->pos[2] = pos->z;
    p->dir[0] = dir->x * speed, p->dir[1] = dir->y * speed, p->dir[2] = dir->z * speed;
    switch (in->render_mode) {
    case PR_LINE:
    case PR_OSPRITE:
    case PR_CSPRITE:
        /* the latency */
        p->angle = in->ang_speed;
        p->dangle = in->ang_speed_var;
        break;
    default:
        p->dangle = in->variances & PV_ANGULARSPEED
                        ? in->ang_speed + in->ang_speed_var * emitter_rand(e)
                        : 0.0f;
        break;
    }
    p->life = in->life + (in->variances & PV_LIFESPAN ? in->life_var * emitter_rand(e) : 0.0f);
    if (p->life <= 0.0f) {
        p->life = 0.001f;
    }
    float inv_life = 1.0f / p->life;
    for (int c = 0; c < 4; c++) {
        p->color[c] = in->start_color[c] +
                      (in->variances & PV_INITIALCOLOR ? in->start_color_var[c] * emitter_rand(e) : 0.0f);
    }
    for (int c = 0; c < 4; c++) {
        float end = in->end_color[c] +
                    (in->variances & PV_ENDINGCOLOR ? in->end_color_var[c] * emitter_rand(e) : 0.0f);
        p->dcolor[c] = in->evolutions & PE_COLOR ? (end - p->color[c]) * inv_life : 0.0f;
    }
    p->size = in->start_size +
              (in->variances & PV_INITIALSIZE ? in->start_size_var * emitter_rand(e) : 0.0f);
    float end_size = in->end_size +
                     (in->variances & PV_ENDINGSIZE ? in->end_size_var * emitter_rand(e) : 0.0f);
    p->dsize = in->evolutions & PE_SIZE ? (end_size - p->size) * inv_life : 0.0f;
    p->frame = in->start_frame + (in->variances & PV_INITIALTEXTURE
                                      ? (int)((float)in->start_frame_var * emitter_rand01(e))
                                      : 0);
    p->dframe_time = (in->evolutions & PE_TEXTURE) && in->frame_count > 1
                         ? (float)in->frame_speed + (in->variances & PV_SPEEDTEXTURE
                                                         ? (float)in->frame_speed_var * emitter_rand(e)
                                                         : 0.0f)
                         : 0.0f;
    p->dt = 0.01f;
}

/* ParticleEmitter::AddParticles: up to Emission (plus variance) new particles, within Maximum
   Number, placed and aimed by the emitter's shape */
static void add_particles(blk_t *b, eng_emitter_t *e, const emitter_inputs_t *in, emitter_shape_t shape)
{
    nmo_engine_t *engine = b->engine;
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return;
    }
    const eng_matrix_t *world = &engine->entities.data[entity].world;
    int emits = in->emission + (in->variances & PV_EMISSION ? (int)((float)in->emission_var * emitter_rand(e)) : 0);
    for (; emits > 0; emits--) {
        eng_particle_t *p = new_particle(b, e, in);
        if (p == NULL) {
            return;
        }
        eng_vector_t pos, dir;
        if (shape == SHAPE_SPHERICAL) {
            /* SphericalEmitter: from a random point of the unit sphere, outward */
            eng_vector_t r = {emitter_rand(e), emitter_rand(e), emitter_rand(e)};
            eng_vector_normalize(&r);
            eng_vector_transform(&pos, world, &r);
            eng_vector_rotate(&dir, world, &r);
            eng_vector_normalize(&dir);
        } else {
            if (shape == SHAPE_PLANAR) {
                /* PlanarEmitter: a random point of the entity's local XY square */
                eng_vector_t r = {emitter_rand(e), emitter_rand(e), 0.0f};
                eng_vector_transform(&pos, world, &r);
            } else {
                /* PointEmitter, WaveEmitter: the entity's position */
                pos = (eng_vector_t){world->m[3][0], world->m[3][1], world->m[3][2]};
            }
            if (shape == SHAPE_WAVE) {
                dir = (eng_vector_t){0.0f, 0.0f, 0.0f}; /* WaveEmitter::InitiateDirection */
            } else {
                initiate_direction(e, in, world, &dir);
            }
        }
        init_particle(e, in, p, &pos, &dir);
    }
}

/*
 * ParticleEmitter::AddParticles2 (TimePointEmitter): Emission Delay is a rate, particles a
 * second; once rate * time passes 0.9 that many are laid evenly from the entity back to where
 * it last emitted, so a moving emitter leaves a trail. The first call only marks the place.
 */
static void add_trail(blk_t *b, eng_emitter_t *e, const emitter_inputs_t *in, float delta)
{
    nmo_engine_t *engine = b->engine;
    int32_t entity = target_entity(b);
    if (entity < 0) {
        return;
    }
    const eng_matrix_t *world = &engine->entities.data[entity].world;
    eng_vector_t here = {world->m[3][0], world->m[3][1], world->m[3][2]};
    float time = (e->trail_time + delta) * 0.001f;
    float rate = get_in_float(b, PS_EMISSION_DELAY, 0.0f);
    if (in->variances & PV_EMISSION) {
        rate += (float)in->emission_var * emitter_rand(e);
    }
    if (rate * time <= 0.9f || !e->trail_started) {
        e->trail_time += delta;
        if (!e->trail_started) {
            e->trail_started = true;
            e->trail_pos[0] = here.x, e->trail_pos[1] = here.y, e->trail_pos[2] = here.z;
        }
        return;
    }
    int count = (int)(rate * time);
    e->trail_time = 0.0f;
    eng_vector_t step = {0.0f, 0.0f, 0.0f};
    if (count > 0) {
        float inv = 1.0f / (float)count;
        step = (eng_vector_t){(e->trail_pos[0] - here.x) * inv, (e->trail_pos[1] - here.y) * inv,
                              (e->trail_pos[2] - here.z) * inv};
    }
    eng_vector_t pos = here;
    do {
        eng_particle_t *p = new_particle(b, e, in);
        if (p != NULL) {
            eng_vector_t dir;
            initiate_direction(e, in, world, &dir);
            init_particle(e, in, p, &pos, &dir);
            pos.x += step.x, pos.y += step.y, pos.z += step.z;
        }
    } while (--count > 0);
    e->trail_pos[0] = here.x, e->trail_pos[1] = here.y, e->trail_pos[2] = here.z;
}

/* ParticleEmitter::UpdateParticles, without the interactors and deflectors: then the emitter's
   box (SetBoundingBox, in the world) is where its living particles were and went, which an emitter
   whose particles all died leaves inverted. A wave emitter's UpdateParticles2 neither turns nor
   moves them (the box is where they were) */
static void update_particles(nmo_engine_t *engine, eng_emitter_t *e, const emitter_inputs_t *in, float delta,
                             bool wave)
{
    if (e->particles.count == 0) {
        return; /* no particles yet: nothing to do */
    }
    eng_vector_t low = {100000.0f, 100000.0f, 100000.0f}, high = {-100000.0f, -100000.0f, -100000.0f};
    size_t kept = 0;
    for (size_t i = 0; i < e->particles.count; i++) {
        eng_particle_t *p = &e->particles.data[i];
        if (p->life <= 0.0f) {
            continue; /* back to the pool */
        }
        float dt = p->life < delta ? p->life : delta;
        if (!wave) {
            p->angle += p->dangle * dt;
        }
        for (int moved = 0; moved < (wave ? 1 : 2); moved++) {
            low = (eng_vector_t){p->pos[0] < low.x ? p->pos[0] : low.x, p->pos[1] < low.y ? p->pos[1] : low.y,
                                 p->pos[2] < low.z ? p->pos[2] : low.z};
            high = (eng_vector_t){high.x < p->pos[0] ? p->pos[0] : high.x, high.y < p->pos[1] ? p->pos[1] : high.y,
                                  high.z < p->pos[2] ? p->pos[2] : high.z};
            for (int c = 0; moved == 0 && c < 3; c++) {
                p->pos[c] += p->dir[c] * dt;
            }
        }
        if (in->evolutions & PE_COLOR) {
            for (int c = 0; c < 4; c++) {
                p->color[c] += p->dcolor[c] * dt;
                p->color[c] = p->color[c] < 0.0f ? 0.0f : p->color[c] > 1.0f ? 1.0f : p->color[c];
            }
        }
        if (in->evolutions & PE_SIZE) {
            p->size += p->dsize * dt;
        }
        if (p->dframe_time != 0.0f && (in->evolutions & PE_TEXTURE) && in->render_mode != PR_OBJECT) {
            p->frame_time += dt;
            float step = fabsf(p->dframe_time);
            int dir = p->dframe_time < 0.0f ? -1 : 1;
            while (p->frame_time > step) {
                p->frame += dir;
                if (p->frame >= in->frame_count) {
                    p->frame = in->frame_loop == PL_LOOP ? 0 : in->frame_count - (in->frame_loop == PL_PINGPONG ? 2 : 1);
                    if (in->frame_loop == PL_PINGPONG) {
                        p->dframe_time = -p->dframe_time;
                    }
                } else if (p->frame < 0) {
                    p->frame = in->frame_loop == PL_LOOP ? in->frame_count - 1 : in->frame_loop == PL_PINGPONG ? 1 : 0;
                    if (in->frame_loop == PL_PINGPONG) {
                        p->dframe_time = -p->dframe_time;
                    }
                }
                p->frame_time -= step;
            }
        }
        p->life -= dt;
        p->dt = dt;
        e->particles.data[kept++] = *p;
    }
    e->particles.count = kept;
    eng_entity_set_user_box(engine, eng_entity_index(engine, e->entity), &low, &high, false);
}

/*
 * GeneralParticleSystem: On starts emitting (Exit On), Off stops, Freeze
 * toggles; while not frozen, the inputs are read, particles emitted each
 * Emission Delay and moved by the frame time; Exit Off once no particle is
 * left after Off. Point, planar, spherical, time dependent point and wave
 * emitters place their particles as the DLL does (a wave emitter's stay where
 * they were emitted, unturned: WaveEmitter, UpdateParticles2); the other
 * shapes emit from the entity's position.
 */
static int bb_particle_system(blk_t *b)
{
    eng_emitter_t *e = emitter_of(b);
    if (e == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* no emitter, as the original returns */
    }
    int32_t activity = 0;
    nmo_engine_block_get_local_value(b, 1, &activity, sizeof(activity));
    float delta = get_in_int(b, PS_REAL_TIME_MODE, 1) ? nmo_engine_block_delta_time(b)
                                                      : get_in_float(b, PS_DELTA_TIME, 20.0f);
    if (nmo_engine_block_input_count(b) > 3 && IN_ACTIVE(b, 3)) {
        IN_OFF(b, 3);
    } else {
        if (IN_ACTIVE(b, 2)) {
            IN_OFF(b, 2);
            OUT_ON(b, 2);
            activity ^= PS_FROZEN;
        } else if (IN_ACTIVE(b, 0)) {
            IN_OFF(b, 0);
            OUT_ON(b, 0);
            float emission_time = get_in_float(b, PS_EMISSION_DELAY, 0.0f);
            nmo_engine_block_set_local_value(b, 2, &emission_time, sizeof(emission_time));
            activity |= PS_ACTIVE;
            e->shown = true;
            eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_target(b), 0);
        } else if (IN_ACTIVE(b, 1)) {
            IN_OFF(b, 1);
            activity = 0;
        }
        nmo_engine_block_set_local_value(b, 1, &activity, sizeof(activity));
        if (activity & PS_FROZEN) {
            return NMO_ENGINE_BR_OK;
        }
        emitter_inputs_t in;
        read_inputs(b, &in);
        e->entity = nmo_engine_block_target(b);
        e->texture = nmo_engine_block_get_input_object(b, PI_TEXTURE);
        e->render_mode = in.render_mode;
        e->source_blend = get_local_int(b, PL_SRC_BLEND, 5);
        e->dest_blend = get_local_int(b, PL_DEST_BLEND, 2);
        e->frame_count = in.frame_count;
        float emission_time = 0.0f;
        nmo_engine_block_get_local_value(b, 2, &emission_time, sizeof(emission_time));
        emission_time += delta;
        /* The particle manager updates a system once a frame, however often it runs */
        if (e->updated != b->engine->frame + 1) {
            e->updated = b->engine->frame + 1;
            emitter_shape_t shape = emitter_shape(b);
            if (activity && shape == SHAPE_TIME_POINT) {
                add_trail(b, e, &in, delta);
            } else if (activity) {
                float emission_delay = get_in_float(b, PS_EMISSION_DELAY, 0.0f) +
                                       get_in_float(b, 1, 0.0f) * emitter_rand(e);
                if (emission_delay > 0.0f) {
                    while (emission_time >= emission_delay) {
                        emission_time -= emission_delay;
                        add_particles(b, e, &in, shape);
                    }
                } else {
                    add_particles(b, e, &in, shape);
                }
            }
            update_particles(b->engine, e, &in, delta, shape == SHAPE_WAVE);
            nmo_engine_block_set_local_value(b, 2, &emission_time, sizeof(emission_time));
        }
    }
    if (!activity && e->particles.count == 0) {
        e->shown = false;
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * GeneralParticleSystemCallback: a reset empties the emitter; a pause or
 * the script's deactivation stops drawing its particles, which stay where
 * they are; the script's activation or a resume draws them again.
 */
static int cb_particle_system(blk_t *b, uint32_t message)
{
    eng_emitter_t *e = emitter_find(b);
    if (e == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (message == NMO_ENGINE_MSG_RESET) {
        e->particles.count = 0; /* InitParticleSystem */
    } else if (message == NMO_ENGINE_MSG_PAUSE || message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        e->shown = false;
    } else if ((message == NMO_ENGINE_MSG_ACTIVATESCRIPT || message == NMO_ENGINE_MSG_RESUME) &&
               nmo_engine_parent_script_is_active(b->engine, block_node(b)->id)) {
        e->shown = true;
    }
    return NMO_ENGINE_BR_OK;
}

static uint32_t particle_color(const float c[4])
{
    uint32_t argb = 0;
    for (int i = 0; i < 4; i++) {
        float v = c[i] < 0.0f ? 0.0f : c[i] > 1.0f ? 1.0f : c[i];
        argb |= (uint32_t)(v * 255.0f + 0.5f) << (i == 3 ? 24 : 16 - 8 * i);
    }
    return argb;
}

static bool push_vertex(nmo_engine_t *engine, const float pos[3], float u, float v, uint32_t color)
{
    nmo_engine_vertex_t *vertex = NULL;
    if (!ENG_VEC_PUSH(engine->render.vertices, vertex)) {
        return false;
    }
    *vertex = (nmo_engine_vertex_t){pos[0], pos[1], pos[2], u, v, color, 0.0f, 0.0f, 0.0f};
    return true;
}

/* A particle's color one move ago */
static uint32_t particle_old_color(const eng_particle_t *p)
{
    float old[4];
    for (int c = 0; c < 4; c++) {
        old[c] = p->color[c] - p->dcolor[c] * p->dt;
    }
    return particle_color(old);
}

/* RenderParticles_L: a line from where the particle was, in its color then, to ahead of it by its
   latency */
static void draw_line(nmo_engine_t *engine, const eng_particle_t *p)
{
    float from[3], to[3];
    for (int c = 0; c < 3; c++) {
        from[c] = p->pos[c] - p->dir[c] * p->dt;
        to[c] = p->pos[c] + p->dir[c] * p->angle;
    }
    push_vertex(engine, from, 0.0f, 0.0f, particle_old_color(p));
    push_vertex(engine, to, 0.0f, 0.0f, particle_color(p->color));
}

/* RenderParticles_OS: a quad stretched from where the particle was (at its size then) to ahead
   of it by its latency, its width across the view */
static void draw_oriented_sprite(nmo_engine_t *engine, const eng_particle_t *p, const float eye_z[3],
                                 float u0, float v0, float step)
{
    eng_vector_t old = {p->pos[0] - p->dir[0] * p->dt, p->pos[1] - p->dir[1] * p->dt,
                        p->pos[2] - p->dir[2] * p->dt};
    float old_size = p->size - p->dsize * p->dt;
    eng_vector_t pos = {p->pos[0] + p->dir[0] * p->angle, p->pos[1] + p->dir[1] * p->angle,
                        p->pos[2] + p->dir[2] * p->angle};
    eng_vector_t vv = {pos.x - old.x, pos.y - old.y, pos.z - old.z};
    eng_vector_normalize(&vv);
    eng_vector_t ww = {eye_z[1] * vv.z - eye_z[2] * vv.y, eye_z[2] * vv.x - eye_z[0] * vv.z,
                       eye_z[0] * vv.y - eye_z[1] * vv.x};
    eng_vector_normalize(&ww);
    float q[4][3];
    const float along[4] = {p->size, p->size, -old_size, -old_size};
    const float across[4] = {p->size, -p->size, -old_size, old_size};
    for (int k = 0; k < 4; k++) {
        const eng_vector_t *at = k < 2 ? &pos : &old;
        q[k][0] = at->x + vv.x * along[k] + ww.x * across[k];
        q[k][1] = at->y + vv.y * along[k] + ww.y * across[k];
        q[k][2] = at->z + vv.z * along[k] + ww.z * across[k];
    }
    const float uv[4][2] = {{u0, v0}, {u0 + step, v0}, {u0 + step, v0 + step}, {u0, v0 + step}};
    uint32_t color = particle_color(p->color), old_color = particle_old_color(p);
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) {
        int i = order[k];
        push_vertex(engine, q[i], uv[i][0], uv[i][1], i < 2 ? color : old_color);
    }
}

/*
 * The render callbacks of the shown emitters: Line a line a particle,
 * Orientable Sprite a quad along its motion, Fast Sprite a triangle and
 * Sprite (and the other sprite modes) a quad, each facing the eye and turned
 * by its angle; frames of a texture grid as its animation.
 */
void eng_draw_particles(nmo_engine_t *engine, const float view[16])
{
    eng_render_t *render = &engine->render;
    const float right[3] = {view[0], view[1], view[2]}, up[3] = {view[4], view[5], view[6]};
    const float eye_z[3] = {view[8], view[9], view[10]};
    const eng_tt_t *tt = eng_tt(engine);
    for (size_t i = 0; i < tt->emitters.count; i++) {
        const eng_emitter_t *e = &tt->emitters.data[i];
        int32_t entity = e->entity != 0 ? eng_entity_index(engine, e->entity) : -1;
        if (!e->shown || e->particles.count == 0 || entity < 0 || !eng_in_scene(engine, e->entity) ||
            !nmo_engine_object_is_visible(engine, e->entity)) {
            continue;
        }
        bool line = e->render_mode == PR_LINE;
        bool fast = e->render_mode == PR_FSPRITE;
        float step = 1.0f;
        int grid = 0;
        if (e->frame_count > 1) {
            grid = 1 + (int)sqrtf((float)(e->frame_count - 1));
            step = 1.0f / (float)grid;
        }
        int32_t first = (int32_t)render->vertices.count;
        for (size_t k = 0; k < e->particles.count; k++) {
            const eng_particle_t *p = &e->particles.data[k];
            if (line) {
                draw_line(engine, p);
                continue;
            }
            float u0 = 0.0f, v0 = 0.0f;
            if (grid > 0) {
                u0 = (float)(p->frame % grid) * step;
                v0 = (float)(p->frame / grid) * step;
            }
            if (e->render_mode == PR_OSPRITE) {
                draw_oriented_sprite(engine, p, eye_z, u0, v0, step);
                continue;
            }
            float cr[3], cu[3];
            float ca = cosf(p->angle), sa = sinf(p->angle);
            for (int c = 0; c < 3; c++) {
                float r = right[c] * p->size * 0.5f, u = up[c] * p->size * 0.5f;
                cr[c] = p->angle != 0.0f ? r * ca - u * sa : r;
                cu[c] = p->angle != 0.0f ? r * sa + u * ca : u;
            }
            uint32_t color = particle_color(p->color);
            float a[3], bb[3], c3[3];
            for (int c = 0; c < 3; c++) {
                a[c] = p->pos[c] + cu[c];
                bb[c] = p->pos[c] + cr[c] - cu[c];
                c3[c] = p->pos[c] - cr[c] - cu[c];
            }
            if (fast) {
                push_vertex(engine, a, u0 + step * 0.5f, v0, color);
                push_vertex(engine, bb, u0 + step, v0 + step, color);
                push_vertex(engine, c3, u0, v0 + step, color);
                continue;
            }
            float tl[3], tr[3], br[3], bl[3];
            for (int c = 0; c < 3; c++) {
                tl[c] = p->pos[c] - cr[c] + cu[c];
                tr[c] = p->pos[c] + cr[c] + cu[c];
                br[c] = p->pos[c] + cr[c] - cu[c];
                bl[c] = p->pos[c] - cr[c] - cu[c];
            }
            push_vertex(engine, tl, u0, v0, color);
            push_vertex(engine, tr, u0 + step, v0, color);
            push_vertex(engine, br, u0 + step, v0 + step, color);
            push_vertex(engine, tl, u0, v0, color);
            push_vertex(engine, br, u0 + step, v0 + step, color);
            push_vertex(engine, bl, u0, v0 + step, color);
        }
        nmo_engine_draw_t *draw = eng_render_push(engine);
        if (draw == NULL) {
            return;
        }
        draw->kind = line ? NMO_ENGINE_DRAW_LINES : NMO_ENGINE_DRAW_TRIANGLES;
        draw->object = e->entity;
        draw->first_vertex = first;
        draw->vertex_count = (int32_t)render->vertices.count - first;
        draw->source_blend = e->source_blend;
        draw->dest_blend = e->dest_blend;
        if (line) {
            /* no texture; blending unless the destination factor is zero */
            draw->blend = e->dest_blend != VXBLEND_ZERO;
        } else {
            draw->texture = e->texture;
            draw->alpha_test = eng_texture_keyed(engine, e->texture);
            /* SetState: no blending for an opaque destination factor */
            draw->blend = !(e->dest_blend == VXBLEND_ZERO &&
                            (e->source_blend == VXBLEND_SRCALPHA || e->source_blend == VXBLEND_SRCCOLOR ||
                             e->source_blend == VXBLEND_ONE));
        }
        draw->transparent = true;
        const float *center4 = engine->entities.data[entity].world.m[3];
        eng_vector_t center = {center4[0], center4[1], center4[2]};
        draw->world[0] = draw->world[5] = draw->world[10] = draw->world[15] = 1.0f;
        draw->world[12] = center.x, draw->world[13] = center.y, draw->world[14] = center.z;
    }
}

static const nmo_engine_block_desc_t eng_tt_particles_blocks[] = {
    {NMO_GUID_INIT(0x506b40f7, 0x30852e46), "Point Particle System", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x1f1e7188, 0x0ec26d1f), "LinearParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x49957bfe, 0x0ff27ffc), "PlanarParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x3d7453f9, 0x1dba691f), "CubicParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x3fb65480, 0x6ca450e2), "DiscParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x13172f30, 0x3a7876ed), "ObjectParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x4a6524c4, 0x13b55824), "CurveParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x67c90f47, 0x1480721e), "CylindricalParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x67c88f47, 0x8880721e), "SphericalParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x569d2cc2, 0x3bcb01b9), "TT_TimedependentPointParticlesystem", bb_particle_system,
     cb_particle_system, NULL},
    {NMO_GUID_INIT(0x02da5a18, 0x52227285), "TT WaveParticleSystem", bb_particle_system,
     cb_particle_system, NULL},
};

static void particle_manager_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                                    const eng_render_args_t *args)
{
    (void)state;
    if (stage == ENG_RENDER_SCENE) {
        eng_draw_particles(engine, args->view);
    }
}

/* ParticleManager */
static const eng_manager_desc_t particle_manager = {
    .name = "Particle Manager",
    .render = particle_manager_render,
};

nmo_status_t eng_register_tt_particles(nmo_engine_t *engine)
{
    nmo_status_t status = eng_tt_add(engine);
    if (status == NMO_OK) {
        status = eng_add_manager(engine, &particle_manager, NULL);
    }
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_tt_particles_blocks) : status;
}
