/**
 * @file engine_media.c
 * @brief Sounds, textures, movies, animations and particle systems, without
 *        audio or rendering.
 *
 * Wave Player and the particle systems are not effects: they stay active and
 * activate outputs later (when a sound ends, when the last particle dies).
 * The engine keeps that timing. A sound advances by the frame time while it
 * plays and ends after the length its CKWaveSound records; a particle system
 * counts its particles as alive until the longest lifespan its inputs allow
 * has passed since it last emitted. Starting a sound or an emission reports
 * an effect event. A texture is its slots, a movie the frames its AVI file
 * records; Movie Player moves through them as the movie plays. Play
 * Animation 3D Entity keeps its progression, the step the animation shows.
 */

#include "engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_guids.h"
#include "../runtime/runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
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
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
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

static uint32_t read_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * The length in ms of the WAV file `name` names (its data over the average
 * bytes a second its format records), as the wave reader gives it; -1 when
 * it is no WAV file.
 */
static float wave_length(nmo_engine_t *engine, const char *name)
{
    const char *path = engine->resolve_file != NULL
                           ? engine->resolve_file(engine->resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        return -1.0f;
    }
    unsigned char header[12];
    uint32_t bytes_per_second = 0, data_size = 0;
    bool has_format = false, has_data = false;
    if (fread(header, 1, sizeof(header), file) == sizeof(header) &&
        memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0) {
        unsigned char chunk[16];
        while (!(has_format && has_data) && fread(chunk, 1, 8, file) == 8) {
            uint32_t size = read_le32(chunk + 4);
            if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
                if (fread(chunk, 1, 16, file) != 16) {
                    break;
                }
                bytes_per_second = read_le32(chunk + 8);
                has_format = true;
                size -= 16;
            } else if (memcmp(chunk, "data", 4) == 0) {
                data_size = size;
                has_data = true;
            }
            /* chunks are word aligned */
            if (fseek(file, (long)size + (long)(size & 1u), SEEK_CUR) != 0) {
                break;
            }
        }
    }
    fclose(file);
    if (!has_format || !has_data || bytes_per_second == 0) {
        return -1.0f;
    }
    return (float)((double)data_size * 1000.0 / (double)bytes_per_second);
}

/* Sound Load's asynchronous load: the result it reports the frame after */
typedef struct sound_load_state {
    bool pending;
    bool loaded;
} sound_load_state_t;

/*
 * Sound Load (SoundLoader): the sound file into the target sound, which
 * stops (CKWaveSound::SetSoundFileName, then SetFileStreaming): Out, or
 * Failed when it is not a sound file. With "Use asynchonized loading" the
 * load thread's result comes on a later frame; the engine has it the next.
 */
static int bb_sound_load(blk_t *b)
{
    eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_target(b));
    if (sound == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    sound_load_state_t *state = (sound_load_state_t *)nmo_engine_block_state(b, sizeof(*state));
    if (state == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    int32_t async = 0;
    nmo_engine_block_get_local_value(b, 2, &async, sizeof(async));
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        size_t size = 0;
        const char *name = (const char *)nmo_engine_block_input_data(b, 0, &size);
        float length = name != NULL && size > 0 && memchr(name, '\0', size) != NULL
                           ? wave_length(b->engine, name)
                           : -1.0f;
        sound_stop(sound, 0.0f);
        if (length >= 0.0f) {
            sound->length = length;
        }
        if (async == 0) {
            OUT_ON(b, length >= 0.0f ? 0 : 1);
            return NMO_ENGINE_BR_OK;
        }
        state->pending = true;
        state->loaded = length >= 0.0f;
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (nmo_engine_block_input_count(b) > 1 && IN_ACTIVE(b, 1)) {
        state->pending = false; /* the thread stopped */
        return NMO_ENGINE_BR_OK;
    }
    if (async != 0 && state->pending) {
        state->pending = false;
        OUT_ON(b, state->loaded ? 0 : 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* TT_GetSoundProperties: the gain, pitch and pan of the target sound. */
static int bb_get_sound_properties(blk_t *b)
{
    nmo_object_t *object = eng_find_object(b->engine, nmo_engine_block_target(b));
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_WAVESOUND) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    const nmo_wavesound_state_t *state = (const nmo_wavesound_state_t *)nmo_object_get_state(object);
    float gain = state != NULL ? state->gain : 1.0f;
    float pitch = state != NULL ? state->pitch : 1.0f;
    float pan = state != NULL ? state->pan : 0.0f;
    nmo_engine_block_set_output_value(b, 0, &gain, sizeof(gain));
    nmo_engine_block_set_output_value(b, 1, &pitch, sizeof(pitch));
    nmo_engine_block_set_output_value(b, 2, &pan, sizeof(pan));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Textures and movies
 * ============================================================================ */

/* The engine's state of texture `id`, made the first time; NULL when `id` is no CKTexture. */
static eng_texture_t *eng_texture(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->textures.count; i++) {
        if (engine->textures.data[i].id == id) {
            return &engine->textures.data[i];
        }
    }
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_TEXTURE) {
        return NULL;
    }
    const nmo_texture_state_t *state = (const nmo_texture_state_t *)nmo_object_get_state(object);
    eng_texture_t *texture = NULL;
    if (!ENG_VEC_PUSH(engine->textures, texture)) {
        return NULL;
    }
    texture->id = id;
    texture->slot_count = state != NULL ? (int32_t)state->slot_count : 0;
    texture->current_slot = state != NULL && state->has_current_slot ? state->current_slot : 0;
    return texture;
}

int32_t nmo_engine_texture_current_slot(const nmo_engine_t *engine, nmo_object_id_t texture_id)
{
    for (size_t i = 0; engine != NULL && i < engine->textures.count; i++) {
        if (engine->textures.data[i].id == texture_id) {
            return engine->textures.data[i].current_slot;
        }
    }
    nmo_object_t *object = engine != NULL ? eng_find_object(engine, texture_id) : NULL;
    const nmo_texture_state_t *state =
        object != NULL && nmo_object_get_class_id(object) == NMO_CID_TEXTURE
            ? (const nmo_texture_state_t *)nmo_object_get_state(object)
            : NULL;
    return state != NULL && state->has_current_slot ? state->current_slot : 0;
}

/*
 * The frame count of the AVI movie `name` names (the AVI movie reader's
 * GetMovieFrameCount, from the main header); -1 when it is none.
 */
static int32_t movie_frame_count(nmo_engine_t *engine, const char *name)
{
    const char *path = engine->resolve_file != NULL
                           ? engine->resolve_file(engine->resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        return -1;
    }
    /* RIFF <size> AVI  LIST <size> hdrl avih <size> MainAVIHeader */
    unsigned char header[52];
    size_t read = fread(header, 1, sizeof(header), file);
    fclose(file);
    if (read != sizeof(header) || memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "AVI ", 4) != 0 || memcmp(header + 12, "LIST", 4) != 0 ||
        memcmp(header + 20, "hdrl", 4) != 0 || memcmp(header + 24, "avih", 4) != 0) {
        return -1;
    }
    int32_t frames = 0;
    memcpy(&frames, header + 32 + 16, sizeof(frames)); /* dwTotalFrames */
    return frames > 0 ? frames : -1;
}

/*
 * Change Texture Video Format: the texture's format in video memory. The
 * engine's render driver has every format, so Success for one of the 14.
 */
static int bb_change_texture_video_format(blk_t *b)
{
    if (!IN_ACTIVE(b, 0)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    int32_t format = get_in_int(b, 0, 0);
    if (format < 0 || format >= 14) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t texture = nmo_engine_block_target(b);
    if (texture == 0) {
        return NMO_ENGINE_BR_BEHAVIORERROR;
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, texture, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Whether the file `name` names is there to read. */
static bool media_file_exists(nmo_engine_t *engine, const char *name)
{
    const char *path = engine->resolve_file != NULL
                           ? engine->resolve_file(engine->resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file != NULL) {
        fclose(file);
    }
    return file != NULL;
}

/*
 * Texture Load (TextureLoader): the image file into a slot of the target
 * texture, which gets that many slots (CKBitmapData::LoadSlotImage); Error
 * when the file is not there or the texture plays a movie.
 */
static int bb_texture_load(blk_t *b)
{
    eng_texture_t *texture = eng_texture(b->engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (!IN_ACTIVE(b, 0)) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    size_t size = 0;
    const char *name = (const char *)nmo_engine_block_input_data(b, 0, &size);
    int32_t slot = get_in_int(b, 1, 0);
    bool loaded = name != NULL && size > 1 && memchr(name, '\0', size) != NULL && name[0] != '\0' &&
                  slot >= 0 && !texture->movie && media_file_exists(b->engine, name);
    if (!loaded) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (slot >= texture->slot_count) {
        texture->slot_count = slot + 1;
    }
    if (nmo_engine_block_output_parameter_count(b) > 0) {
        nmo_object_id_t id = texture->id;
        nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Movie Load (MovieLoader): the target texture plays the movie file. */
static int bb_movie_load(blk_t *b)
{
    eng_texture_t *texture = eng_texture(b->engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    IN_OFF(b, 0);
    size_t size = 0;
    const char *name = (const char *)nmo_engine_block_input_data(b, 0, &size);
    int32_t frames = name != NULL && size > 0 && memchr(name, '\0', size) != NULL
                         ? movie_frame_count(b->engine, name)
                         : -1;
    if (frames < 0) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    texture->movie = true;
    texture->slot_count = frames;
    texture->current_slot = 0;
    if (nmo_engine_block_output_parameter_count(b) > 0) {
        nmo_object_id_t id = texture->id;
        nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Movie Player (MoviePlayer): the target texture's slots from Starting Slot
 * to Ending Slot (all of them when both are 0) over Duration, Synchro Out at
 * each new slot, One Loop Played at the end of each pass.
 */
static int bb_movie_player(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_texture_t *texture = target != 0 ? eng_texture(b->engine, target) : NULL;
    if (texture == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float duration = 5000.0f;
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_INT)) {
        duration = (float)get_in_int(b, 0, 5000); /* back compatibility */
    } else {
        duration = get_in_float(b, 0, 5000.0f);
    }
    if (duration == 0.0f) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t capacity = texture->slot_count;
    if (capacity == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t start = get_in_int(b, 2, 0);
    start = start < 0 ? 0 : start > capacity - 1 ? capacity : start;
    int32_t end = get_in_int(b, 3, 0);
    end = end < 0 ? 0 : end > capacity - 1 ? capacity : end;
    int32_t slots = start == 0 && end == 0 ? capacity : abs(end - start);
    float rate = (float)slots / duration;

    float time = 0.0f;
    int32_t old_slot = -1;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    } else {
        nmo_engine_block_get_local_value(b, 0, &time, sizeof(time));
        old_slot = (int32_t)(time * rate);
        time += nmo_engine_block_delta_time(b);
    }
    if (time >= duration) {
        OUT_ON(b, 1);
        if (get_in_int(b, 1, 1) == 0) {
            return NMO_ENGINE_BR_OK;
        }
        do {
            time -= duration;
        } while (time >= duration);
    }
    nmo_engine_block_set_local_value(b, 0, &time, sizeof(time));
    int32_t new_slot = (int32_t)(time * rate);
    if (new_slot != old_slot) {
        int32_t slot = start <= end ? start + new_slot : start - 1 - new_slot;
        texture->current_slot = slot;
        nmo_engine_block_set_output_value(b, 0, &slot, sizeof(slot));
        OUT_ON(b, 0);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * Animations
 * ============================================================================ */

/*
 * Play Animation 3D Entity (PlayAnimEntity): the animation's progression
 * from 0 to 1 over Duration, One Loop Played each time it passes 1; without
 * Loop it stays at 1 and stops. The animation takes that step through the
 * progression curve (an effect).
 */
static int bb_play_animation(blk_t *b)
{
    if (nmo_engine_block_get_input_object(b, 0) == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    float duration = 5000.0f;
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 1), CKPGUID_INT)) {
        duration = (float)get_in_int(b, 1, 5000); /* back compatibility */
    } else {
        duration = get_in_float(b, 1, 5000.0f);
    }
    if (duration < 0.001f) {
        return NMO_ENGINE_BR_OK;
    }
    bool loop = get_in_int(b, 3, 1) != 0;
    float step = 0.0f;
    nmo_engine_block_get_local_value(b, 0, &step, sizeof(step));
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    step += nmo_engine_block_delta_time(b) / duration;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        step = 0.0f;
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_owner(b), 0);
    }
    int result = NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    if (step > 1.0f) {
        OUT_ON(b, 0);
        if (loop) {
            step -= (float)(int)step;
        } else {
            step = 1.0f;
            result = NMO_ENGINE_BR_OK;
        }
    }
    nmo_engine_block_set_local_value(b, 0, &step, sizeof(step));
    nmo_engine_block_set_output_value(b, 0, &step, sizeof(step));
    return result;
}

static const nmo_engine_block_desc_t eng_media_blocks[] = {
    {NMO_GUID(0x5bde0e45, 0x2e2107d5), "Wave Player", bb_wave_player, cb_wave_player, NULL},
    {NMO_GUID(0x02bc537c, 0x147c22e0), "Sound Load", bb_sound_load, NULL, NULL},
    {NMO_GUID(0x30fa6a70, 0x3b3f728a), "TT_GetSoundProperties", bb_get_sound_properties, NULL,
     NULL},
    {NMO_GUID(0x00e85ab1, 0x312a731c), "Texture Load", bb_texture_load, NULL, NULL},
    {NMO_GUID(0xf56d92ef, 0x41eb20c1), "Change Texture Video Format",
     bb_change_texture_video_format, NULL, NULL},
    {NMO_GUID(0x64221225, 0x769a143f), "Play Animation 3D Entity", bb_play_animation, NULL, NULL},
    {NMO_GUID(0x798e26a9, 0x34e4169e), "Movie Load", bb_movie_load, NULL, NULL},
    {NMO_GUID(0x778d16d4, 0x1dd60060), "Movie Player", bb_movie_player, NULL, NULL},
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
