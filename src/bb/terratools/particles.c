/**
 * @file particles.c
 * @brief Ports of the TT particle system building blocks, without rendering.
 *
 * Each function does what the block's function in TT_ParticleSystems_RT.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ). The emitters keep their particles in the render state and
 * draw them as their render modes do; the interactors (objects with the
 * Particle System Interactors attributes) and the deflectors act on them as
 * the ParticleManager does, and each emitter has its own random
 * numbers so that particles do not change the gameplay's (the Disruption Box
 * and the deflectors' density included, which take the game's rand() in the
 * DLL).
 */

#include "../bb_sdk.h"
#include "terratools.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

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
enum { PL_RENDER = 4, PL_SRC_BLEND, PL_DEST_BLEND, PL_EVOLUTIONS = 8, PL_VARIANCES, PL_DEFLECTORS, PL_MESSAGE,
       PL_INTERACTORS };
/* The inputs the deflectors and interactors add (Particle.h BOUNCE..SURFACEVARIATION) */
enum { PI_BOUNCE = 17, PI_BOUNCE_VAR, PI_WEIGHT, PI_WEIGHT_VAR, PI_SURFACE, PI_SURFACE_VAR };
#define PD_PLANE 0x01
#define PD_INFINITEPLANE 0x02
#define PD_CYLINDER 0x04
#define PD_SPHERE 0x08
#define PD_BOX 0x10
#define PD_OBJECT 0x20
#define PD_ALL 0x3F
#define PD_IMPACTS 0x40
#define PD_DIEONIMPACT 0x80
#define PV_BOUNCE 0x40
#define PI_GRAVITY 0x001
#define PI_GLOBALWIND 0x002
#define PI_LOCALWIND 0x004
#define PI_MAGNET 0x008
#define PI_VORTEX 0x010
#define PI_DISRUPTIONBOX 0x020
#define PI_ATMOSPHERE 0x040
#define PI_MUTATIONBOX 0x080
#define PI_TUNNEL 0x100
#define PI_PROJECTOR 0x200
#define PI_SURFACES (PI_LOCALWIND | PI_GLOBALWIND | PI_ATMOSPHERE)
#define PV_WEIGHT 0x80
#define PV_SURFACE 0x100
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
#define GUID_LINEAR NMO_GUID(0x1f1e7188, 0x0ec26d1f)
#define GUID_CUBIC NMO_GUID(0x3d7453f9, 0x1dba691f)
#define GUID_DISC NMO_GUID(0x3fb65480, 0x6ca450e2)
#define GUID_OBJECT NMO_GUID(0x13172f30, 0x3a7876ed)
#define GUID_CURVE NMO_GUID(0x4a6524c4, 0x13b55824)
#define GUID_CYLINDRICAL NMO_GUID(0x67c90f47, 0x1480721e)
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
    int32_t interactors;          /* Manage Interactors (PI_*) */
    int32_t deflectors;           /* Manage Deflectors (PD_*) */
    int32_t message;              /* Message To Deflectors: the type, -1 for none */
    float bounce, bounce_var;     /* with deflectors */
    float weight, weight_var;     /* with gravity */
    float surface, surface_var;   /* with the winds or the atmosphere */
} emitter_inputs_t;

void eng_free_emitters(nmo_engine_t *engine)
{
    for (size_t i = 0; i < eng_tt(engine)->emitters.count; i++) {
        free(eng_tt(engine)->emitters.data[i].particles.data);
        free(eng_tt(engine)->emitters.data[i].impacts.data);
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

/* The emitter's own rand(), 0 to RAND_MAX */
static int32_t emitter_rand_u15(eng_emitter_t *e)
{
    e->random = e->random * 1103515245u + 12345u;
    return (int32_t)((e->random >> 16) & 0x7FFF);
}

/* rand() * INV_RAND (0.000030518499: just under 1 at RAND_MAX), the deflectors' density roll */
static float emitter_rand_inv(eng_emitter_t *e)
{
    e->random = e->random * 1103515245u + 12345u;
    return (float)((e->random >> 16) & 0x7FFF) * 0.000030518499f;
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
    /* the settings' defaults name every deflector and interactor */
    in->deflectors = get_local_int(b, PL_DEFLECTORS, PD_ALL);
    in->message = get_local_int(b, PL_MESSAGE, -1);
    if (in->deflectors) {
        in->bounce = get_in_float(b, PI_BOUNCE, 0.0f);
        if (in->variances & PV_BOUNCE) {
            in->bounce_var = get_in_float(b, PI_BOUNCE_VAR, 0.0f);
        }
    }
    in->interactors = get_local_int(b, PL_INTERACTORS, 0x3FF);
    if (in->interactors & PI_GRAVITY) {
        in->weight = get_in_float(b, PI_WEIGHT, 0.0f);
        if (in->variances & PV_WEIGHT) {
            in->weight_var = get_in_float(b, PI_WEIGHT_VAR, 0.0f);
        }
    }
    if (in->interactors & PI_SURFACES) {
        in->surface = get_in_float(b, PI_SURFACE, 0.0f);
        if (in->variances & PV_SURFACE) {
            in->surface_var = get_in_float(b, PI_SURFACE_VAR, 0.0f);
        }
    }
}

/* The emitter a prototype makes (ParticleManager::CreateNewEmitter) */
typedef enum emitter_shape {
    SHAPE_POINT, SHAPE_PLANAR, SHAPE_SPHERICAL, SHAPE_TIME_POINT, SHAPE_WAVE, SHAPE_LINEAR, SHAPE_CUBIC, SHAPE_DISC,
    SHAPE_OBJECT, SHAPE_CURVE, SHAPE_CYLINDRICAL
} emitter_shape_t;

static emitter_shape_t emitter_shape(blk_t *b)
{
    nmo_guid_t guid = block_node(b)->prototype;
    if (nmo_guid_equals(guid, GUID_SPHERICAL)) {
        return SHAPE_SPHERICAL;
    }
    if (nmo_guid_equals(guid, GUID_PLANAR)) {
        return SHAPE_PLANAR;
    }
    static const struct { nmo_guid_t guid; emitter_shape_t shape; } shapes[] = {
        {GUID_WAVE, SHAPE_WAVE},   {GUID_LINEAR, SHAPE_LINEAR}, {GUID_CUBIC, SHAPE_CUBIC},
        {GUID_DISC, SHAPE_DISC},   {GUID_OBJECT, SHAPE_OBJECT}, {GUID_CURVE, SHAPE_CURVE},
        {GUID_CYLINDRICAL, SHAPE_CYLINDRICAL}};
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
        if (nmo_guid_equals(guid, shapes[i].guid)) {
            return shapes[i].shape;
        }
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
    if (in->deflectors) {
        p->bounce = in->bounce + (in->variances & PV_BOUNCE ? in->bounce_var * emitter_rand(e) : 0.0f);
    }
    if (in->interactors & PI_GRAVITY) {
        p->weight = in->weight + (in->variances & PV_WEIGHT ? in->weight_var * emitter_rand(e) : 0.0f);
    }
    p->dt = 0.01f;
    if (in->interactors & PI_SURFACES) {
        p->surface = in->surface + (in->variances & PV_SURFACE ? in->surface_var * emitter_rand(e) : 0.0f);
    }
}

/* ObjectEmitter's shape: the entity's current mesh as it is now (its vertices, indices and face
   normals); false without one */
static bool object_shape(nmo_engine_t *engine, nmo_object_id_t id, const nmo_vertex_t **vertices, uint32_t *vertex_count,
                         const uint16_t **indices, const nmo_face_t **faces, uint32_t *face_count)
{
    nmo_object_t *object = eng_find_object(engine, id);
    const nmo_3dentity_state_t *state = object != NULL
        ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                          CKPGUID_3DENTITY)
        : NULL;
    nmo_object_id_t mesh = state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
    nmo_object_t *mesh_object = mesh != 0 ? eng_find_object(engine, mesh) : NULL;
    const nmo_mesh_state_t *mesh_state = mesh_object != NULL
        ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, mesh_object,
                                                                                     CKPGUID_MESH)
        : NULL;
    if (mesh_state == NULL || mesh_state->face_count == 0 || mesh_state->face_vertex_indices == NULL) {
        return false;
    }
    eng_mesh_copy_t *copy = eng_mesh_copy_find(engine, mesh);
    *vertices = copy != NULL && copy->vertices != NULL ? copy->vertices : mesh_state->vertices;
    *vertex_count = mesh_state->vertex_count;
    *indices = mesh_state->face_vertex_indices;
    *faces = copy != NULL ? eng_mesh_copy_faces(engine, copy) : mesh_state->faces;
    *face_count = mesh_state->face_count;
    return *vertices != NULL && *faces != NULL;
}

/* The emitter's InitiateParticle, and with true its own InitiateDirection (Spherical, Cylindrical
   and Object leave the direction InitiateParticle gave, Wave none, Curve along the curve's Y);
   false: ParticleEmitter::InitiateDirection's, around the entity's Z */
static bool initiate_shape(nmo_engine_t *engine, eng_emitter_t *e, const emitter_inputs_t *in, int32_t entity,
                           emitter_shape_t shape, eng_vector_t *pos, eng_vector_t *dir)
{
    const eng_matrix_t *world = &engine->entities.data[entity].world;
    eng_vector_t r = {0.0f, 0.0f, 0.0f};
    switch (shape) {
    case SHAPE_SPHERICAL:
        /* SphericalEmitter: from a random point of the unit sphere, outward */
        r.x = emitter_rand(e), r.y = emitter_rand(e), r.z = emitter_rand(e);
        eng_vector_normalize(&r);
        eng_vector_transform(pos, world, &r);
        eng_vector_rotate(dir, world, &r);
        eng_vector_normalize(dir);
        return true;
    case SHAPE_CYLINDRICAL:
        /* CylindricalEmitter: a random point of the unit tube about Z (height -1..1), outward */
        r.y = emitter_rand(e);
        r.x = emitter_rand(e);
        eng_vector_normalize(&r);
        eng_vector_rotate(dir, world, &r);
        eng_vector_normalize(dir);
        r.z = emitter_rand(e);
        eng_vector_transform(pos, world, &r);
        return true;
    case SHAPE_PLANAR:
        /* PlanarEmitter: a random point of the entity's local XY square */
        r.y = emitter_rand(e);
        r.x = emitter_rand(e);
        eng_vector_transform(pos, world, &r);
        return false;
    case SHAPE_LINEAR:
        /* LineEmitter: a random point of the entity's local X from -1 to 1 */
        r.x = emitter_rand(e);
        eng_vector_transform(pos, world, &r);
        return false;
    case SHAPE_CUBIC:
        /* CubicEmitter: a random point of the entity's local -1..1 cube */
        r.z = emitter_rand(e);
        r.y = emitter_rand(e);
        r.x = emitter_rand(e);
        eng_vector_transform(pos, world, &r);
        return false;
    case SHAPE_DISC:
        /* DiscEmitter: a random point of the local XY square, halved outside the unit disc */
        r.y = emitter_rand(e);
        r.x = emitter_rand(e);
        if (r.x * r.x + r.y * r.y > 1.0f) {
            r.x *= 0.5f, r.y *= 0.5f;
        }
        eng_vector_transform(pos, world, &r);
        return false;
    case SHAPE_OBJECT: {
        /* ObjectEmitter: a random point of a random face of the entity's mesh, along the face's
           normal turned within the yaw and pitch variances */
        const nmo_vertex_t *vertices;
        const uint16_t *indices;
        const nmo_face_t *faces;
        uint32_t vertex_count, face_count;
        if (!object_shape(engine, e->entity, &vertices, &vertex_count, &indices, &faces, &face_count)) {
            break; /* the DLL reads through the missing mesh: the entity's position */
        }
        uint32_t face = (uint32_t)emitter_rand_u15(e) % face_count;
        const nmo_vector_t n = faces[face].normal;
        float w[3];
        for (int k = 0; k < 3; k++) {
            w[k] = fabsf(emitter_rand(e));
        }
        float inv = 1.0f / (w[2] + w[1] + w[0]);
        eng_vector_t local = {0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 3; k++) {
            uint16_t v = indices[3u * face + (uint32_t)k];
            const nmo_vector_t c = v < vertex_count ? vertices[v].position : (nmo_vector_t){0.0f, 0.0f, 0.0f};
            local.x += c.x * w[k], local.y += c.y * w[k], local.z += c.z * w[k];
        }
        local.x *= inv, local.y *= inv, local.z *= inv;
        eng_vector_transform(pos, world, &local);
        eng_vector_t tip_local = {n.x + local.x, n.y + local.y, n.z + local.z}, base, tip;
        eng_vector_transform(&base, world, &local);
        eng_vector_transform(&tip, world, &tip_local);
        eng_vector_t normal = {tip.x - base.x, tip.y - base.y, tip.z - base.z};
        float pitch = emitter_rand(e) * in->pitch;
        float yaw = emitter_rand(e) * in->yaw;
        eng_matrix_t m;
        eng_matrix_from_euler(&m, yaw, pitch, 0.0f);
        eng_vector_rotate(dir, &m, &normal);
        return true;
    }
    case SHAPE_CURVE: {
        /* CurveEmitter: a random point of the entity's curve, along the entity's Y turned within the
           variances */
        float step = (float)emitter_rand_u15(e) * 0.000030518499f;
        if (!eng_curve_position(engine, e->entity, step, pos)) {
            *pos = (eng_vector_t){world->m[3][0], world->m[3][1], world->m[3][2]};
        }
        eng_vector_t up;
        eng_entity_orientation(engine, entity, NULL, &up, NULL, -1);
        float pitch = emitter_rand(e) * in->pitch;
        float yaw = emitter_rand(e) * in->yaw;
        eng_matrix_t m;
        eng_matrix_from_euler(&m, yaw, 0.0f, pitch);
        eng_vector_rotate(dir, &m, &up);
        return true;
    }
    case SHAPE_WAVE:
        /* WaveEmitter: the entity's position, no direction */
        *pos = (eng_vector_t){world->m[3][0], world->m[3][1], world->m[3][2]};
        *dir = (eng_vector_t){0.0f, 0.0f, 0.0f};
        return true;
    default:
        break;
    }
    /* PointEmitter: the entity's position */
    *pos = (eng_vector_t){world->m[3][0], world->m[3][1], world->m[3][2]};
    return false;
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
        if (!initiate_shape(engine, e, in, entity, shape, &pos, &dir)) {
            initiate_direction(e, in, world, &dir);
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

/* ---- Interactors (ParticleManager::Manage*) ---- */

/* The attribute type of that name; -1 when the documents have none */
static int32_t particle_attribute(const nmo_engine_t *engine, const char *name)
{
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, name) == 0) {
            return (int32_t)t;
        }
    }
    return -1;
}

/* GetAttributeParameter()->GetValue of a plain value: its first `size` bytes, zero when short */
static void attribute_value(const eng_attribute_t *attribute, void *out, size_t size)
{
    memset(out, 0, size);
    if (attribute->has_value && attribute->value.data != NULL) {
        memcpy(out, attribute->value.data, attribute->value.size < size ? attribute->value.size : size);
    }
}

/* A structure attribute's field (its sub-parameter's value) */
static void attribute_field(nmo_engine_t *engine, const eng_attribute_t *attribute, uint32_t field, void *out,
                            uint32_t size)
{
    memset(out, 0, size);
    if (attribute->has_value) {
        eng_struct_field_bytes(engine, attribute->object, attribute->value.data, attribute->value.size, field, out, size);
    }
}

/* CK3dEntity::GetBoundingBox(FALSE): a meshless entity's is its position */
static void world_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min, eng_vector_t *max)
{
    if (!eng_entity_world_box(engine, index, min, max)) {
        *min = *max = eng_entity_position(engine, index, -1);
    }
}

static bool boxes_meet(const eng_vector_t *amin, const eng_vector_t *amax, const eng_vector_t *bmin,
                       const eng_vector_t *bmax)
{
    return amin->x <= bmax->x && amax->x >= bmin->x && amin->y <= bmax->y && amax->y >= bmin->y &&
           amin->z <= bmax->z && amax->z >= bmin->z;
}

static bool inside_box(const float pos[3], const eng_vector_t *min, const eng_vector_t *max)
{
    return pos[0] >= min->x && pos[0] <= max->x && pos[1] >= min->y && pos[1] <= max->y && pos[2] >= min->z &&
           pos[2] <= max->z;
}

/* Each object with the interactor's attribute, in the attribute list's order, with its entity */
#define FOR_EACH_INTERACTOR(engine, type, attribute, at)                                                   \
    for (size_t a_ = 0; (type) >= 0 && a_ < (engine)->attributes.values.count; a_++)                       \
        for (const eng_attribute_t *attribute = &(engine)->attributes.values.data[a_]; attribute != NULL; \
             attribute = NULL)                                                                             \
            for (int32_t at = attribute->type == (type) ? eng_entity_index((engine), attribute->object) : -1; \
                 at >= 0; at = -1)

/* Gravity: down Y by the force times each particle's weight */
static void manage_gravity(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    int32_t type = particle_attribute(engine, "Particle Gravity");
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        float g;
        attribute_value(attribute, &g, sizeof(g));
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            p->dir[1] = g * p->weight * dt + p->dir[1];
        }
    }
}

/* Atmosphere: the motion slowed by the friction times each particle's surface */
static void manage_atmosphere(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    int32_t type = particle_attribute(engine, "Particle Atmosphere");
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        float f;
        attribute_value(attribute, &f, sizeof(f));
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            for (int c = 0; c < 3; c++) {
                p->dir[c] = p->dir[c] - f * p->dir[c] * dt * p->surface;
            }
        }
    }
}

/* Global Wind: along the object's Z by the force times each particle's surface */
static void manage_global_wind(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    int32_t type = particle_attribute(engine, "Particle Global Wind : Force");
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        float force;
        attribute_value(attribute, &force, sizeof(force));
        eng_vector_t dir;
        eng_entity_orientation(engine, at, &dir, NULL, NULL, -1);
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            float k = force * p->surface * dt;
            p->dir[0] += dir.x * k, p->dir[1] += dir.y * k, p->dir[2] += dir.z * k;
        }
    }
}

/* Local Wind: along the object's Z, in front of it within its radius, the force falling off with
   the distance by the decay */
static void manage_local_wind(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    int32_t type = particle_attribute(engine, "Particle Local Wind : Force/Decay");
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        float wind[2];
        attribute_value(attribute, wind, sizeof(wind));
        eng_vector_t dir;
        eng_entity_orientation(engine, at, &dir, NULL, NULL, -1);
        float radius = eng_entity_radius(engine, at);
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            eng_vector_t local, pos = {p->pos[0], p->pos[1], p->pos[2]};
            eng_entity_inverse_transform(engine, at, &local, &pos, -1);
            if (local.z > 0.0f && radius * radius > local.y * local.y + local.x * local.x) {
                float k = 1.0f / (local.z * wind[1] + 1.0f) * p->surface * wind[0] * dt;
                p->dir[0] += dir.x * k, p->dir[1] += dir.y * k, p->dir[2] += dir.z * k;
            }
        }
    }
}

/* Magnet: toward the object (or slowed inside its radius) */
static void manage_magnet(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    int32_t type = particle_attribute(engine, "Particle Magnet");
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        float radius = eng_entity_radius(engine, at);
        float force;
        attribute_value(attribute, &force, sizeof(force));
        eng_vector_t pos = eng_entity_position(engine, at, -1);
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            float w[3] = {pos.x - p->pos[0] + dt * p->dir[0], pos.y - p->pos[1] + dt * p->dir[1],
                          pos.z - p->pos[2] + dt * p->dir[2]};
            float distance = sqrtf(w[2] * w[2] + w[1] * w[1] + w[0] * w[0]);
            if (distance <= 0.0f) {
                continue;
            }
            if (distance <= radius) {
                float k = 1.0f / (distance + 1.0f);
                p->dir[0] *= k, p->dir[1] *= k, p->dir[2] *= k;
            } else {
                float inv = 1.0f / (distance * distance);
                for (int c = 0; c < 3; c++) {
                    p->dir[c] = inv * w[c] * force * dt * 0.001f + p->dir[c];
                }
            }
        }
    }
}

/* Vortex: each particle turned about the object by the angles, less with the distance */
static void manage_vortex(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    (void)dt;
    int32_t type = particle_attribute(engine, "Particle Vortex");
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        float angles[3];
        attribute_value(attribute, angles, sizeof(angles));
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            eng_vector_t local, pos = {p->pos[0], p->pos[1], p->pos[2]}, turned;
            eng_entity_inverse_transform(engine, at, &local, &pos, -1);
            float k = 1.0f / (local.z * local.z + local.y * local.y + local.x * local.x + 1.0f);
            eng_matrix_t m;
            eng_matrix_from_euler(&m, angles[0] * k, angles[1] * k, angles[2] * k);
            eng_vector_rotate(&turned, &m, &local);
            eng_entity_transform(engine, at, &pos, &turned, -1);
            p->pos[0] = pos.x, p->pos[1] = pos.y, p->pos[2] = pos.z;
        }
    }
}

/* Disruption Box: inside the object's box (when it meets the emitter's), toward a random point of
   it by the strengths */
static void manage_disruption_box(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    int32_t type = particle_attribute(engine, "Particle Disruption Box");
    int32_t emitter = eng_entity_index(engine, e->entity);
    if (emitter < 0) {
        return;
    }
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        float strength[3];
        attribute_value(attribute, strength, sizeof(strength));
        eng_vector_t min, max, emin, emax;
        world_box(engine, at, &min, &max);
        world_box(engine, emitter, &emin, &emax);
        if (!boxes_meet(&emin, &emax, &min, &max)) {
            continue;
        }
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            if (!inside_box(p->pos, &min, &max)) {
                continue;
            }
            float x = emitter_rand01(e) * (max.x - min.x) + min.x;
            float y = emitter_rand01(e) * (max.y - min.y) + min.y;
            float z = emitter_rand01(e) * (max.z - min.z) + min.z;
            p->dir[0] = strength[0] * (x - p->pos[0]) * dt + p->dir[0];
            p->dir[1] = strength[1] * (y - p->pos[1]) * dt + p->dir[1];
            p->dir[2] = (z - p->pos[2]) * strength[2] * dt + p->dir[2];
        }
    }
}

/* Mutation Box: inside the object's box (when it meets the emitter's), the size and the color
   brought toward the attribute's at their speeds, their own evolutions stopped */
static void manage_mutation_box(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    int32_t type = particle_attribute(engine, "Particle Mutation Box");
    int32_t emitter = eng_entity_index(engine, e->entity);
    if (emitter < 0) {
        return;
    }
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        eng_vector_t min, max, emin, emax;
        world_box(engine, at, &min, &max);
        world_box(engine, emitter, &emin, &emax);
        if (!boxes_meet(&emin, &emax, &min, &max)) {
            continue;
        }
        float size = 0.0f, size_speed = 0.0f, color[4], color_speed = 0.0f;
        attribute_field(engine, attribute, 0, &size, sizeof(size));
        attribute_field(engine, attribute, 1, &size_speed, sizeof(size_speed));
        attribute_field(engine, attribute, 2, color, sizeof(color));
        attribute_field(engine, attribute, 3, &color_speed, sizeof(color_speed));
        size_speed *= dt;
        color_speed *= dt;
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            if (!inside_box(p->pos, &min, &max)) {
                continue;
            }
            if (size_speed != 0.0f) {
                p->dsize = 0.0f;
                p->size = (size - p->size) * size_speed + p->size;
            }
            if (color_speed != 0.0f) {
                for (int c = 0; c < 4; c++) {
                    p->color[c] = (color[c] - p->color[c]) * color_speed + p->color[c];
                    p->dcolor[c] = 0.0f;
                }
            }
        }
    }
}

/* Tunnel: along the object's Z within its unit square in front of it; behind it, within the
   radius, drawn into the funnel the shape makes, the speed and the direction pulled by the
   attractions times each particle's weight */
static void manage_tunnel(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    int32_t type = particle_attribute(engine, "Particle Tunnel");
    float step = dt * 0.001f;
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        float speed, radius, shape, speed_pull, dir_pull;
        attribute_field(engine, attribute, 0, &speed, sizeof(speed));
        attribute_field(engine, attribute, 1, &radius, sizeof(radius));
        attribute_field(engine, attribute, 2, &shape, sizeof(shape));
        attribute_field(engine, attribute, 3, &speed_pull, sizeof(speed_pull));
        attribute_field(engine, attribute, 4, &dir_pull, sizeof(dir_pull));
        const eng_matrix_t *m = &engine->entities.data[at].world;
        float z_length = sqrtf(m->m[2][0] * m->m[2][0] + m->m[2][1] * m->m[2][1] + m->m[2][2] * m->m[2][2]);
        float x_scale = 1.0f / sqrtf(m->m[0][0] * m->m[0][0] + m->m[0][1] * m->m[0][1] + m->m[0][2] * m->m[0][2]);
        const float axis[3] = {m->m[2][0] / z_length, m->m[2][1] / z_length, m->m[2][2] / z_length};
        eng_vector_t center = eng_entity_position(engine, at, -1);
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            eng_vector_t local, pos = {p->pos[0], p->pos[1], p->pos[2]};
            eng_entity_inverse_transform(engine, at, &local, &pos, -1);
            if (local.z >= 1.0f) {
                continue;
            }
            if (local.z >= 0.0f) {
                if (fabsf(local.x) < 1.0f && fabsf(local.y) < 1.0f) {
                    for (int c = 0; c < 3; c++) {
                        p->dir[c] = axis[c] * speed;
                    }
                }
                continue;
            }
            float d[3] = {p->pos[0] - center.x, p->pos[1] - center.y, p->pos[2] - center.z};
            if (sqrtf(d[2] * d[2] + d[1] * d[1] + d[0] * d[0]) >= radius) {
                continue;
            }
            float k = x_scale * z_length * shape;
            float kz = k * local.z;
            float q = kz * kz + 1.0f;
            if (!(fabsf(local.x) < q && fabsf(local.y) < q)) {
                continue;
            }
            float inv_q = 1.0f / q;
            eng_vector_t toward = {inv_q * local.x * kz + inv_q * local.x * kz, inv_q * local.y * kz + inv_q * local.y * kz,
                                   1.0f / k},
                         w;
            eng_entity_transform_vector(engine, at, &w, &toward, -1, false);
            eng_vector_normalize(&w);
            float s = sqrtf(p->dir[0] * p->dir[0] + p->dir[1] * p->dir[1] + p->dir[2] * p->dir[2]);
            float new_speed = (speed - s) * p->weight * speed_pull * step + s;
            float n[3] = {1.0f / s * p->dir[0], 1.0f / s * p->dir[1], 1.0f / s * p->dir[2]};
            float pull = dir_pull * p->weight * step;
            const float wv[3] = {w.x, w.y, w.z};
            for (int c = 0; c < 3; c++) {
                p->dir[c] = ((wv[c] - n[c]) * pull + n[c]) * new_speed;
            }
        }
    }
}

/* Projector: each particle in the 3D sprite's projection takes its texture's texel times its
   material's diffuse, fading with the depth; out of it, transparent black */
static void manage_projector(nmo_engine_t *engine, eng_emitter_t *e, float dt)
{
    (void)dt;
    int32_t type = particle_attribute(engine, "Particle Projector");
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        const eng_sprite3d_t *sprite = eng_sprite3d(engine, attribute->object);
        eng_material_t *material = sprite != NULL && sprite->material != 0 ? eng_material(engine, sprite->material) : NULL;
        eng_texture_t *texture = material != NULL ? eng_texture(engine, material->desc.texture) : NULL;
        if (texture == NULL) {
            continue; /* the DLL reads through the missing material or texture */
        }
        eng_surface_t *surface = eng_texture_surface(engine, texture, -1);
        float size[2] = {sprite->max.x - sprite->min.x, sprite->max.y - sprite->min.y};
        float lens[3];
        attribute_value(attribute, lens, sizeof(lens));
        const float *diffuse = material->desc.diffuse;
        float half = lens[0] * 0.5f;
        float near_plane = 0.5f / tanf(half);
        float far_plane = lens[2] + near_plane;
        eng_matrix_t projection, frame, inverse, total;
        memset(&projection, 0, sizeof(projection));
        projection.m[0][0] = cosf(half) / sinf(half);
        projection.m[1][1] = lens[1] * projection.m[0][0];
        projection.m[2][2] = far_plane / (far_plane - near_plane);
        projection.m[3][2] = -(projection.m[2][2] * near_plane);
        projection.m[2][3] = 1.0f;
        frame = engine->entities.data[at].world;
        for (int c = 0; c < 3; c++) {
            frame.m[0][c] *= -size[0];
            frame.m[1][c] *= size[1];
            frame.m[2][c] *= -1.0f;
        }
        float inv = 1.0f / sqrtf(frame.m[2][2] * frame.m[2][2] + frame.m[2][1] * frame.m[2][1] + frame.m[2][0] * frame.m[2][0]);
        for (int c = 0; c < 3; c++) {
            frame.m[3][c] -= frame.m[2][c] * inv * near_plane;
        }
        eng_matrix_inverse(&inverse, &frame);
        eng_matrix_multiply4(&total, &projection, &inverse);
        for (size_t i = 0; i < e->particles.count; i++) {
            eng_particle_t *p = &e->particles.data[i];
            float h[4];
            for (int c = 0; c < 4; c++) {
                h[c] = p->pos[0] * total.m[0][c] + p->pos[1] * total.m[1][c] + p->pos[2] * total.m[2][c] + total.m[3][c];
            }
            if (h[2] <= 0.0f || h[2] >= h[3] || h[0] <= -h[3] || h[0] >= h[3] || h[1] <= -h[3] || h[1] >= h[3]) {
                p->color[0] = p->color[1] = p->color[2] = p->color[3] = 0.0f;
                continue;
            }
            float iw = 1.0f / h[3];
            float u = (1.0f - iw * h[0]) * 0.5f, v = (1.0f - iw * h[1]) * 0.5f, depth = 1.0f - iw * h[2];
            /* CKBitmapData::GetPixel(x, y, -1): no bounds test, 0 without a picture */
            uint8_t texel[4] = {0, 0, 0, 0};
            if (surface != NULL && surface->rgba != NULL) {
                int64_t x = (int64_t)(u * (float)surface->width), y = (int64_t)(v * (float)surface->height);
                int64_t at_texel = x + y * surface->width;
                if (at_texel >= 0 && at_texel < (int64_t)surface->width * surface->height) {
                    memcpy(texel, &surface->rgba[4 * at_texel], 4);
                }
            }
            for (int c = 0; c < 4; c++) {
                p->color[c] = (float)texel[c] * 0.0039215689f * diffuse[c];
            }
            p->color[3] = depth * p->color[3];
        }
    }
}

/* The interactors the emitter manages, in UpdateParticles' order */
static void manage_interactors(nmo_engine_t *engine, eng_emitter_t *e, int32_t flags, float dt)
{
    if (flags & PI_GRAVITY) manage_gravity(engine, e, dt);
    if (flags & PI_ATMOSPHERE) manage_atmosphere(engine, e, dt);
    if (flags & PI_GLOBALWIND) manage_global_wind(engine, e, dt);
    if (flags & PI_LOCALWIND) manage_local_wind(engine, e, dt);
    if (flags & PI_MAGNET) manage_magnet(engine, e, dt);
    if (flags & PI_VORTEX) manage_vortex(engine, e, dt);
    if (flags & PI_DISRUPTIONBOX) manage_disruption_box(engine, e, dt);
    if (flags & PI_MUTATIONBOX) manage_mutation_box(engine, e, dt);
    if (flags & PI_TUNNEL) manage_tunnel(engine, e, dt);
    if (flags & PI_PROJECTOR) manage_projector(engine, e, dt);
}

/* ---- Deflectors (ParticleManager::Manage*Deflectors) ---- */

static float dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* The segment a -> b against the plane through `point` along `normal` (sub_25091A40) */
static bool segment_plane(const float normal[3], const float point[3], const float a[3], const float b[3], float out[3])
{
    const float v[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float denominator = v[0] * normal[0] + v[2] * normal[2] + v[1] * normal[1];
    if (denominator == 0.0f) {
        return false;
    }
    float t = -(((a[0] - point[0]) * normal[0] + (a[2] - point[2]) * normal[2] + (a[1] - point[1]) * normal[1]) /
                denominator);
    if (t < 0.0f || t > 1.0f) {
        return false;
    }
    for (int c = 0; c < 3; c++) {
        out[c] = v[c] * t + a[c];
    }
    return true;
}

/* RayBoxIntersection (sub_25091120) and RayInteriorBoxIntersection (sub_25091350): the segment
   p0 -> p1 against the box's faces, the normal the face's; a start inside the box gives it back,
   -1 (or 1 for the interior test) */
static int segment_box(const float min[3], const float max[3], const float p0[3], const float p1[3], float res[3],
                       float norm[3], bool interior)
{
    bool inside = true;
    int quadrant[3];
    float candidate[3];
    for (int i = 0; i < 3; i++) {
        if (p0[i] < min[i]) {
            quadrant[i] = 1, candidate[i] = min[i], norm[i] = -1.0f, inside = false;
        } else if (p0[i] > max[i]) {
            quadrant[i] = 0, candidate[i] = max[i], norm[i] = 1.0f, inside = false;
        } else {
            norm[i] = 0.0f, candidate[i] = 0.0f, quadrant[i] = 2;
        }
    }
    if (inside) {
        memcpy(res, p0, 3 * sizeof(float));
        return interior ? 1 : -1;
    }
    const float dir[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float max_t[3];
    for (int i = 0; i < 3; i++) {
        max_t[i] = quadrant[i] != 2 && dir[i] != 0.0f ? (candidate[i] - p0[i]) / dir[i] : -1.0f;
    }
    int plane = 0;
    for (int i = 1; i < 3; i++) {
        if (max_t[plane] < max_t[i]) {
            plane = i;
        }
    }
    if (max_t[plane] < 0.0f || max_t[plane] > 1.0f) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        if (i == plane) {
            res[i] = candidate[i];
            continue;
        }
        norm[i] = 0.0f;
        res[i] = max_t[plane] * dir[i] + p0[i];
        if (res[i] < min[i] || res[i] > max[i]) {
            return 0;
        }
    }
    return 1;
}

/* RaySphereIntersection (sub_25091580): the segment crossing the unit sphere */
static bool segment_sphere(const float p0[3], const float p1[3], float res[3], float norm[3])
{
    float d0 = dot3(p0, p0), d1 = dot3(p1, p1);
    if (!((d0 < 1.0f && d1 > 1.0f) || (d0 > 1.0f && d1 < 1.0f))) {
        return false;
    }
    float v[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float inv = 1.0f / sqrtf(v[2] * v[2] + v[0] * v[0] + v[1] * v[1]);
    v[0] *= inv, v[1] *= inv, v[2] *= inv;
    float along = d0 >= 1.0f ? (-p0[2] * v[2] + -p0[1] * v[1]) + -p0[0] * v[0] : (v[0] * p0[0] + v[2] * p0[2]) + v[1] * p0[1];
    float t = along - sqrtf(1.0f - (dot3(p0, p0) - along * along));
    for (int c = 0; c < 3; c++) {
        res[c] = v[c] * t + p0[c];
        norm[c] = res[c];
    }
    return true;
}

/* RayTubeIntersection (sub_25091750): the segment crossing the unit tube about Z between -1 and 1,
   with the DLL's own roots (and t = b without one) */
static bool segment_tube(const float p0[3], const float p1[3], float res[3], float norm[3])
{
    float d0 = p0[0] * p0[0] + p0[1] * p0[1], d1 = p1[0] * p1[0] + p1[1] * p1[1];
    if (!((d0 < 1.0f && d1 > 1.0f) || (d0 > 1.0f && d1 < 1.0f)) || (p1[2] >= 1.0f && p0[2] >= 1.0f) ||
        (p1[2] <= -1.0f && p0[2] <= -1.0f)) {
        return false;
    }
    const float ray[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float a = ray[1] * ray[1] + ray[0] * ray[0];
    float r = p0[0] * p0[0] + p0[1] * p0[1];
    float b = r + r;
    float delta = b * b - (r - 2.0f) * a * 4.0f;
    float t;
    if (delta < 0.0f) {
        t = b;
    } else {
        float root = sqrtf(delta);
        t = (-b - root) / (a + a);
        if (t < 0.0f) {
            t = (root - b) / (a + a);
            if (t < 0.0f) {
                return false;
            }
        }
    }
    for (int c = 0; c < 3; c++) {
        res[c] = ray[c] * t + p0[c];
        norm[c] = res[c];
    }
    norm[2] = 0.0f;
    return true;
}

/* A deflector's response, friction and density (and an object deflector's smoothing): the first
   fields of its structure */
typedef struct deflector_fields {
    float response, friction, density;
    int32_t smooth;
} deflector_fields_t;

/* The impact (with PD_IMPACTS) and the bounce: the move kept along the surface times the friction,
   sent back along the normal by the response times the particle's bounce, from the impact point */
static void deflect(eng_emitter_t *e, const emitter_inputs_t *in, eng_particle_t *p, const float olddir[3],
                    const float point[3], const float normal[3], const deflector_fields_t *fields, float inv_dt,
                    nmo_object_id_t object, const float uv[2])
{
    float along = dot3(olddir, normal);
    for (int c = 0; c < 3; c++) {
        float d = (olddir[c] - normal[c] * along) * fields->friction - normal[c] * (along * fields->response * p->bounce);
        p->pos[c] = point[c] + d;
        p->dir[c] = d * inv_dt;
    }
    if (in->deflectors & PD_IMPACTS) {
        eng_impact_t *impact = NULL;
        if (ENG_VEC_PUSH(e->impacts, impact)) {
            memcpy(impact->position, point, sizeof(impact->position));
            memcpy(impact->direction, normal, sizeof(impact->direction));
            impact->object = object;
            impact->uv[0] = uv[0], impact->uv[1] = uv[1];
        }
    }
}

/* What every deflector shares: each object with the attribute, its fields; per particle its last
   move and the density's chance; `hit` places the impact. The message goes once per deflector, a
   particle dies on impact with PD_DIEONIMPACT */
typedef bool (*deflector_hit_fn)(nmo_engine_t *engine, int32_t at, const float prev[3], const float pos[3],
                                 const deflector_fields_t *fields, float point[3], float normal[3], float uv[2],
                                 void *data);

static void manage_deflectors(nmo_engine_t *engine, eng_emitter_t *e, const emitter_inputs_t *in, float dt,
                              const char *name, uint32_t field_count, bool box_test, float y_margin,
                              deflector_hit_fn hit, void *data)
{
    int32_t type = particle_attribute(engine, name);
    int32_t emitter = eng_entity_index(engine, e->entity);
    if (emitter < 0) {
        return;
    }
    FOR_EACH_INTERACTOR(engine, type, attribute, at) {
        if (box_test) {
            eng_vector_t min, max, emin, emax;
            world_box(engine, at, &min, &max);
            min.y -= y_margin, max.y += y_margin;
            world_box(engine, emitter, &emin, &emax);
            if (!boxes_meet(&min, &max, &emin, &emax)) {
                continue;
            }
        }
        if (!attribute->has_value) {
            continue;
        }
        deflector_fields_t fields = {0.0f, 0.0f, 0.0f, 0};
        attribute_field(engine, attribute, 0, &fields.response, sizeof(float));
        attribute_field(engine, attribute, 1, &fields.friction, sizeof(float));
        attribute_field(engine, attribute, 2, &fields.density, sizeof(float));
        if (field_count > 3) {
            attribute_field(engine, attribute, 3, &fields.smooth, sizeof(int32_t));
        }
        float inv_dt = 1.0f / dt;
        bool sent = false;
        for (size_t i = 0; i < e->particles.count;) {
            eng_particle_t *p = &e->particles.data[i];
            const float olddir[3] = {p->dt * p->dir[0], p->dt * p->dir[1], p->dt * p->dir[2]};
            const float prev[3] = {p->pos[0] - olddir[0], p->pos[1] - olddir[1], p->pos[2] - olddir[2]};
            float point[3], normal[3], uv[2] = {0.0f, 0.0f};
            if (!(emitter_rand_inv(e) < fields.density) ||
                !hit(engine, at, prev, p->pos, &fields, point, normal, uv, data)) {
                i++;
                continue;
            }
            deflect(e, in, p, olddir, point, normal, &fields, inv_dt, attribute->object, uv);
            if (!sent) {
                if (in->message >= 0) {
                    nmo_engine_send_message(engine, in->message, attribute->object, e->entity);
                }
                sent = true;
            }
            if (in->deflectors & PD_DIEONIMPACT) {
                memmove(&e->particles.data[i], &e->particles.data[i + 1],
                        (e->particles.count - i - 1) * sizeof(eng_particle_t));
                e->particles.count--;
                continue;
            }
            i++;
        }
    }
}

/* The deflector's local Y as a unit world normal, and its position */
static void plane_of(nmo_engine_t *engine, int32_t at, float normal[3], float point[3])
{
    const eng_vector_t y = {0.0f, 1.0f, 0.0f};
    eng_vector_t n;
    eng_entity_transform_vector(engine, at, &n, &y, -1, false);
    eng_vector_normalize(&n);
    eng_vector_t o = eng_entity_position(engine, at, -1);
    normal[0] = n.x, normal[1] = n.y, normal[2] = n.z;
    point[0] = o.x, point[1] = o.y, point[2] = o.z;
}

/* Infinite Plane: the move crossing the plane of the deflector's XZ */
static bool hit_infinite_plane(nmo_engine_t *engine, int32_t at, const float prev[3], const float pos[3],
                               const deflector_fields_t *fields, float point[3], float normal[3], float uv[2], void *data)
{
    (void)fields, (void)uv, (void)data;
    float o[3];
    plane_of(engine, at, normal, o);
    return segment_plane(normal, o, prev, pos, point);
}

/* Plane: the same within the deflector's local -1..1 square, the texture coordinates across it */
static bool hit_plane(nmo_engine_t *engine, int32_t at, const float prev[3], const float pos[3],
                      const deflector_fields_t *fields, float point[3], float normal[3], float uv[2], void *data)
{
    (void)fields, (void)data;
    float o[3];
    plane_of(engine, at, normal, o);
    if (!segment_plane(normal, o, prev, pos, point)) {
        return false;
    }
    eng_vector_t local, world = {point[0], point[1], point[2]};
    eng_entity_inverse_transform(engine, at, &local, &world, -1);
    if (local.x <= -1.0f || local.x >= 1.0f || local.z <= -1.0f || local.z >= 1.0f) {
        return false;
    }
    uv[0] = (local.x + 1.0f) * 0.5f;
    uv[1] = (1.0f - local.z) * 0.5f;
    return true;
}

/* The move in the deflector's space, `local` hitting a shape there, back in the world */
typedef bool (*local_shape_fn)(nmo_engine_t *engine, int32_t at, const float p0[3], const float p1[3], float res[3],
                               float norm[3]);

static bool hit_local(nmo_engine_t *engine, int32_t at, const float prev[3], const float pos[3], float point[3],
                      float normal[3], local_shape_fn shape, bool point_first)
{
    eng_vector_t a = {prev[0], prev[1], prev[2]}, b = {pos[0], pos[1], pos[2]}, la, lb;
    eng_entity_inverse_transform(engine, at, &la, &a, -1);
    eng_entity_inverse_transform(engine, at, &lb, &b, -1);
    const float p0[3] = {la.x, la.y, la.z}, p1[3] = {lb.x, lb.y, lb.z};
    float res[3], norm[3];
    if (!shape(engine, at, p0, p1, res, norm)) {
        return false;
    }
    eng_vector_t lr = {res[0], res[1], res[2]}, ln = {norm[0], norm[1], norm[2]}, wr, wn;
    if (point_first) {
        eng_entity_transform(engine, at, &wr, &lr, -1);
    }
    eng_entity_transform_vector(engine, at, &wn, &ln, -1, false);
    eng_vector_normalize(&wn);
    if (!point_first) {
        eng_entity_transform(engine, at, &wr, &lr, -1);
    }
    point[0] = wr.x, point[1] = wr.y, point[2] = wr.z;
    normal[0] = wn.x, normal[1] = wn.y, normal[2] = wn.z;
    return true;
}

static bool tube_shape(nmo_engine_t *engine, int32_t at, const float p0[3], const float p1[3], float res[3], float norm[3])
{
    (void)engine, (void)at;
    return segment_tube(p0, p1, res, norm);
}

static bool sphere_shape(nmo_engine_t *engine, int32_t at, const float p0[3], const float p1[3], float res[3],
                         float norm[3])
{
    (void)engine, (void)at;
    return segment_sphere(p0, p1, res, norm);
}

/* Box: the deflector's own box, the move tried backwards when it starts inside */
static bool box_shape(nmo_engine_t *engine, int32_t at, const float p0[3], const float p1[3], float res[3], float norm[3])
{
    eng_vector_t low = {0, 0, 0}, high = {0, 0, 0};
    eng_entity_local_box(engine, at, &low, &high);
    const float min[3] = {low.x, low.y, low.z}, max[3] = {high.x, high.y, high.z};
    int found = segment_box(min, max, p0, p1, res, norm, false);
    if (found == -1) {
        found = segment_box(min, max, p1, p0, res, norm, false);
    }
    return found > 0;
}

static bool hit_cylinder(nmo_engine_t *engine, int32_t at, const float prev[3], const float pos[3],
                         const deflector_fields_t *fields, float point[3], float normal[3], float uv[2], void *data)
{
    (void)fields, (void)uv, (void)data;
    return hit_local(engine, at, prev, pos, point, normal, tube_shape, false);
}

static bool hit_sphere(nmo_engine_t *engine, int32_t at, const float prev[3], const float pos[3],
                       const deflector_fields_t *fields, float point[3], float normal[3], float uv[2], void *data)
{
    (void)fields, (void)uv, (void)data;
    return hit_local(engine, at, prev, pos, point, normal, sphere_shape, true);
}

static bool hit_box(nmo_engine_t *engine, int32_t at, const float prev[3], const float pos[3],
                    const deflector_fields_t *fields, float point[3], float normal[3], float uv[2], void *data)
{
    (void)fields, (void)uv, (void)data;
    return hit_local(engine, at, prev, pos, point, normal, box_shape, false);
}

/* Object: the move through the object's world box, then against its mesh (RayIntersection, as a
   segment): the interpolated normal when smooth, the face's otherwise, and the hit's texture
   coordinates */
static bool hit_object(nmo_engine_t *engine, int32_t at, const float prev[3], const float pos[3],
                       const deflector_fields_t *fields, float point[3], float normal[3], float uv[2], void *data)
{
    (void)data;
    eng_vector_t low, high;
    world_box(engine, at, &low, &high);
    const float min[3] = {low.x, low.y, low.z}, max[3] = {high.x, high.y, high.z};
    float res[3], norm[3];
    if (segment_box(min, max, prev, pos, res, norm, true) == 0) {
        return false;
    }
    eng_vector_t a = {prev[0], prev[1], prev[2]}, b = {pos[0], pos[1], pos[2]};
    eng_ray_hit_t ray;
    if (eng_entity_ray_intersection(engine, at, &a, &b, &ray) <= 0 || ray.distance > 1.0f) {
        return false;
    }
    eng_vector_t local_normal = ray.normal;
    if (!fields->smooth) {
        nmo_object_id_t id = engine->entities.data[at].id;
        nmo_object_t *object = eng_find_object(engine, id);
        const nmo_3dentity_state_t *state = object != NULL
            ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                                                              CKPGUID_3DENTITY)
            : NULL;
        nmo_object_id_t mesh = state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
        eng_mesh_copy_t *copy = mesh != 0 ? eng_mesh_copy_find(engine, mesh) : NULL;
        nmo_object_t *mesh_object = mesh != 0 && copy == NULL ? eng_find_object(engine, mesh) : NULL;
        const nmo_mesh_state_t *mesh_state = mesh_object != NULL
            ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, mesh_object,
                                                                                         CKPGUID_MESH)
            : NULL;
        const nmo_face_t *faces = copy != NULL ? eng_mesh_copy_faces(engine, copy)
                                               : mesh_state != NULL ? mesh_state->faces : NULL;
        if (faces != NULL) {
            local_normal = (eng_vector_t){faces[ray.face].normal.x, faces[ray.face].normal.y, faces[ray.face].normal.z};
        }
    }
    eng_vector_t wn, wp;
    eng_entity_transform_vector(engine, at, &wn, &local_normal, -1, false);
    eng_vector_normalize(&wn);
    eng_entity_transform(engine, at, &wp, &ray.point, -1);
    point[0] = wp.x, point[1] = wp.y, point[2] = wp.z;
    normal[0] = wn.x, normal[1] = wn.y, normal[2] = wn.z;
    uv[0] = ray.u, uv[1] = ray.v;
    return true;
}

/* The deflectors the emitter manages, in UpdateParticles' order */
static void manage_all_deflectors(nmo_engine_t *engine, eng_emitter_t *e, const emitter_inputs_t *in, float dt)
{
    if (in->deflectors & PD_PLANE)
        manage_deflectors(engine, e, in, dt, "Particle Plane Deflector", 3, true, 0.1f, hit_plane, NULL);
    if (in->deflectors & PD_INFINITEPLANE)
        manage_deflectors(engine, e, in, dt, "Particle Infinite Plane Deflector", 3, false, 0.0f, hit_infinite_plane, NULL);
    if (in->deflectors & PD_CYLINDER)
        manage_deflectors(engine, e, in, dt, "Particle Cylinder Deflector", 3, true, 0.0f, hit_cylinder, NULL);
    if (in->deflectors & PD_SPHERE)
        manage_deflectors(engine, e, in, dt, "Particle Sphere Deflector", 3, true, 0.0f, hit_sphere, NULL);
    if (in->deflectors & PD_BOX)
        manage_deflectors(engine, e, in, dt, "Particle Box Deflector", 3, true, 0.0f, hit_box, NULL);
    if (in->deflectors & PD_OBJECT)
        manage_deflectors(engine, e, in, dt, "Particle Object Deflector", 4, true, 0.0f, hit_object, NULL);
}

/* ParticleEmitter::UpdateParticles, the interactors first, the deflectors last: then the emitter's
   box (SetBoundingBox, in the world) is where its living particles were and went, which an emitter
   whose particles all died leaves inverted. A wave emitter's UpdateParticles2 neither turns nor
   moves them (the box is where they were) */
static void update_particles(nmo_engine_t *engine, eng_emitter_t *e, const emitter_inputs_t *in, float delta,
                             bool wave)
{
    e->impacts.count = 0; /* m_Impacts.Clear() */
    e->current_impact = 0;
    if (e->particles.count == 0) {
        return; /* no particles yet: nothing to do */
    }
    manage_interactors(engine, e, in->interactors, delta);
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
    if (in->deflectors) {
        manage_all_deflectors(engine, e, in, delta);
    }
}

/*
 * GeneralParticleSystem: On starts emitting (Exit On), Off stops, Freeze
 * toggles; while not frozen, the inputs are read, particles emitted each
 * Emission Delay and moved by the frame time; Exit Off once no particle is
 * left after Off. Every emitter shape places its particles as the DLL does
 * (a wave emitter's stay where they were emitted, unturned: WaveEmitter,
 * UpdateParticles2).
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
    /* Deflector impacts: the next one on the outputs, through Impacts Loop Out */
    if (e->current_impact < e->impacts.count && nmo_engine_block_output_count(b) > 3 &&
        nmo_engine_block_output_parameter_count(b) >= 4) {
        const eng_impact_t *impact = &e->impacts.data[e->current_impact++];
        nmo_engine_block_set_output_value(b, 0, impact->position, sizeof(impact->position));
        nmo_engine_block_set_output_value(b, 1, impact->direction, sizeof(impact->direction));
        nmo_engine_block_set_output_value(b, 2, &impact->object, sizeof(impact->object));
        nmo_engine_block_set_output_value(b, 3, impact->uv, sizeof(impact->uv));
        OUT_ON(b, 3);
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
    if (message == NMO_ENGINE_MSG_NEWSCENE) {
        /* the owner renders last, its particles among the transparent objects */
        int32_t owner = eng_entity_index(b->engine, nmo_engine_block_owner(b));
        if (owner >= 0) {
            b->engine->entities.data[owner].moveable_flags |= VX_MOVEABLE_RENDERLAST;
        }
    }
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
/* RenderParticles_*: the emitter's particles as its render mode draws them (its entity's post render
   callback, so they draw where the entity renders, a render-last one among the transparent objects) */
static void draw_emitter(nmo_engine_t *engine, const eng_emitter_t *e, const float view[16])
{
    eng_render_t *render = &engine->render;
    const float right[3] = {view[0], view[1], view[2]}, up[3] = {view[4], view[5], view[6]};
    const float eye_z[3] = {view[8], view[9], view[10]};
    int32_t entity = e->entity != 0 ? eng_entity_index(engine, e->entity) : -1;
    if (!e->shown || e->particles.count == 0 || entity < 0 || !eng_in_scene(engine, e->entity) ||
        !nmo_engine_object_is_visible(engine, e->entity)) {
        return;
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

/* The emitter entity's post render callback: that block's emitter's particles */
static void render_emitter(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    (void)entity, (void)transparent;
    const eng_tt_t *tt = eng_tt(engine);
    for (size_t i = 0; engine->render.scene_view != NULL && i < tt->emitters.count; i++) {
        if (tt->emitters.data[i].node == node) {
            draw_emitter(engine, &tt->emitters.data[i], engine->render.scene_view);
            return;
        }
    }
}

/* ShowParticles: a shown emitter draws as its entity's post render callback, put there for the
   frame (and its passes) as it is made */
static void particle_manager_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                                    const eng_render_args_t *args)
{
    (void)state, (void)args;
    if (stage != ENG_RENDER_PREPARE) {
        return;
    }
    const eng_tt_t *tt = eng_tt(engine);
    for (size_t i = 0; i < tt->emitters.count; i++) {
        const eng_emitter_t *e = &tt->emitters.data[i];
        if (e->shown && e->entity != 0) {
            eng_entity_callback_t callback = {e->entity, ENG_ENTITY_POST_RENDER, render_emitter, e->node, true};
            eng_add_entity_callback(engine, &callback);
        }
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
