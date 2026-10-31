/**
 * @file particles.c
 * @brief Ports of the TT particle system building blocks, without rendering.
 *
 * Each function does what the block's function in TT_ParticleSystems_RT.dll
 * does (the reference is the Ballanced source of the blocks, or the DLL where
 * they differ). A particle system stays active and counts its particles as
 * alive until the longest lifespan its inputs allow has passed since it last
 * emitted; starting an emission reports an effect event.
 */

#include "../../engine_block_util.h"

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

typedef struct particle_state {
    float clock;          /* particle time, advanced by the system's delta time */
    float alive_until;    /* when the particles emitted last will all have died */
    uint64_t ticked_frame; /* the engine frame + 1 the clock last advanced in */
} particle_state_t;

/*
 * GeneralParticleSystem: On starts emitting (Exit On), Off stops, Freeze
 * toggles; Exit Off once no particle is left after Off.
 */
static int bb_particle_system(blk_t *b)
{
    particle_state_t *ps = (particle_state_t *)nmo_engine_block_state(b, sizeof(*ps));
    if (ps == NULL) {
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
            eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_target(b), 0);
        } else if (IN_ACTIVE(b, 1)) {
            IN_OFF(b, 1);
            activity = 0;
        }
        nmo_engine_block_set_local_value(b, 1, &activity, sizeof(activity));
        if (activity & PS_FROZEN) {
            return NMO_ENGINE_BR_OK;
        }
        /* The particle manager updates a system once a frame, however often it runs */
        if (ps->ticked_frame != b->engine->frame + 1) {
            ps->ticked_frame = b->engine->frame + 1;
            ps->clock += delta;
        }
        if (activity) {
            float life = get_in_float(b, PS_LIFE, 0.0f);
            float variation = get_in_float(b, PS_LIFE_VARIATION, 0.0f);
            ps->alive_until = ps->clock + life + (variation > 0.0f ? variation : 0.0f);
        }
    }
    if (!activity && ps->clock >= ps->alive_until) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static const nmo_engine_block_desc_t eng_tt_particles_blocks[] = {
    {NMO_GUID(0x506b40f7, 0x30852e46), "Point Particle System", bb_particle_system, NULL, NULL},
    {NMO_GUID(0x1f1e7188, 0x0ec26d1f), "LinearParticleSystem", bb_particle_system, NULL, NULL},
    {NMO_GUID(0x49957bfe, 0x0ff27ffc), "PlanarParticleSystem", bb_particle_system, NULL, NULL},
    {NMO_GUID(0x3d7453f9, 0x1dba691f), "CubicParticleSystem", bb_particle_system, NULL, NULL},
    {NMO_GUID(0x3fb65480, 0x6ca450e2), "DiscParticleSystem", bb_particle_system, NULL, NULL},
    {NMO_GUID(0x13172f30, 0x3a7876ed), "ObjectParticleSystem", bb_particle_system, NULL, NULL},
    {NMO_GUID(0x4a6524c4, 0x13b55824), "CurveParticleSystem", bb_particle_system, NULL, NULL},
    {NMO_GUID(0x67c90f47, 0x1480721e), "CylindricalParticleSystem", bb_particle_system, NULL,
     NULL},
    {NMO_GUID(0x67c88f47, 0x8880721e), "SphericalParticleSystem", bb_particle_system, NULL, NULL},
    {NMO_GUID(0x569d2cc2, 0x3bcb01b9), "TT_TimedependentPointParticlesystem", bb_particle_system,
     NULL, NULL},
    {NMO_GUID(0x02da5a18, 0x52227285), "TT WaveParticleSystem", bb_particle_system, NULL, NULL},
};

nmo_status_t eng_register_tt_particles(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_tt_particles_blocks);
}
