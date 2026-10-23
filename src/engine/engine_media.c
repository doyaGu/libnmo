/**
 * @file engine_media.c
 * @brief Sounds and particle systems, without audio or rendering.
 *
 * Wave Player and the particle systems are not effects: they stay active and
 * activate outputs later (when a sound ends, when the last particle dies).
 * The engine keeps that timing. A sound advances by the frame time while it
 * plays and ends after the length its CKWaveSound records; a particle system
 * counts its particles as alive until the longest lifespan its inputs allow
 * has passed since it last emitted. Starting a sound or an emission reports
 * an effect event.
 */

#include "engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_object_guids.h"
#include "../runtime/runtime_internal.h"

#include <string.h>

typedef nmo_engine_block_t blk_t;

#define IN_ACTIVE(b, i) nmo_engine_block_is_input_active((b), (i))
#define IN_OFF(b, i) nmo_engine_block_activate_input((b), (i), false)
#define OUT_ON(b, i) nmo_engine_block_activate_output((b), (i), true)

static float get_in_float(blk_t *b, int pos, float fallback)
{
    float value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static int32_t get_in_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

/* ============================================================================
 * Sounds
 * ============================================================================ */

/* The engine's state of sound `id`, made the first time; NULL when `id` is no CKWaveSound. */
static eng_sound_t *eng_sound(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->sounds.count; i++) {
        if (engine->sounds.data[i].id == id) {
            return &engine->sounds.data[i];
        }
    }
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(engine->workspace);
    nmo_object_t *object = repo != NULL && id != 0 ? nmo_object_repository_find_by_id(repo, id)
                                                   : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_WAVESOUND) {
        return NULL;
    }
    const nmo_wavesound_state_t *state = (const nmo_wavesound_state_t *)nmo_object_get_state(object);
    eng_sound_t *sound = NULL;
    if (!ENG_VEC_PUSH(engine->sounds, sound)) {
        return NULL;
    }
    sound->id = id;
    sound->length = state != NULL && state->has_duration ? (float)state->duration : 0.0f;
    return sound;
}

void eng_sounds_begin_frame(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->sounds.count; i++) {
        eng_sound_t *sound = &engine->sounds.data[i];
        if (!sound->playing || sound->paused) {
            continue;
        }
        sound->position += engine->delta;
        if (sound->fade_left > 0.0f) {
            sound->fade_left -= engine->delta;
            if (sound->fade_left <= 0.0f) {
                sound->fade_left = 0.0f;
                sound->playing = false;
            }
        }
        if (!sound->loop && sound->position >= sound->length) {
            sound->playing = false;
        }
    }
}

bool nmo_engine_sound_is_playing(const nmo_engine_t *engine, nmo_object_id_t sound_id)
{
    for (size_t i = 0; engine != NULL && i < engine->sounds.count; i++) {
        const eng_sound_t *sound = &engine->sounds.data[i];
        if (sound->id == sound_id) {
            return sound->playing && !sound->paused;
        }
    }
    return false;
}

/* CKWaveSound::Play: from the start (a fade in changes only the volume). */
static void sound_play(blk_t *b, eng_sound_t *sound, bool loop)
{
    sound->playing = true;
    sound->paused = false;
    sound->position = 0.0f;
    sound->fade_left = 0.0f;
    sound->loop = loop;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, sound->id, 0);
}

/* CKWaveSound::Stop: now, or once the fade out is over. */
static void sound_stop(eng_sound_t *sound, float fade_out)
{
    if (fade_out > 0.0f && sound->playing) {
        sound->fade_left = fade_out;
    } else {
        sound->playing = false;
    }
}

/* Wave Player (WavePlay): Play, Stop, Pause/Resume on its target sound. */
static int bb_wave_player(blk_t *b)
{
    eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_target(b));
    if (sound == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        bool loop = get_in_int(b, 2, 0) != 0;
        sound_play(b, sound, loop);
        OUT_ON(b, 0);
        return loop ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (nmo_engine_block_input_count(b) > 2 && IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        if (sound->playing && !sound->paused) {
            sound->paused = true;
            return NMO_ENGINE_BR_OK;
        }
        if (sound->paused) {
            sound->paused = false;
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        float fade_out = get_in_float(b, 1, 0.0f);
        sound_stop(sound, fade_out);
        if (fade_out == 0.0f) {
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_OK;
        }
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (!(sound->playing && !sound->paused)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* OldWavePlay: the sound is the first input; out right away. */
static int bb_wave_player_old(blk_t *b)
{
    eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_get_input_object(b, 0));
    if (sound == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        sound_play(b, sound, get_in_int(b, 3, 0) != 0);
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        sound_stop(sound, get_in_float(b, 2, 0.0f));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int cb_wave_player(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD &&
        nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_WAVESOUND)) {
        nmo_engine_block_set_function(b, bb_wave_player_old);
    }
    return NMO_ENGINE_BR_OK;
}

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

static const nmo_engine_block_desc_t eng_media_blocks[] = {
    {NMO_GUID(0x5bde0e45, 0x2e2107d5), "Wave Player", bb_wave_player, cb_wave_player, NULL},
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

nmo_status_t eng_register_media(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_media_blocks) / sizeof(eng_media_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_media_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
