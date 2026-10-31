/**
 * @file media.c
 * @brief Sounds and textures in time, without audio or rendering.
 *
 * A sound advances by the frame time while it plays and ends after the length
 * its CKWaveSound records (the sound manager's frame). A texture is its
 * bitmap slots, a movie the frames its AVI file records.
 */

#include "../engine_block_util.h"

#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Sounds
 * ============================================================================ */

/* The engine's state of sound `id`, made the first time; NULL when `id` is no CKWaveSound. */
eng_sound_t *eng_sound(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->media.sounds.count; i++) {
        if (engine->media.sounds.data[i].id == id) {
            return &engine->media.sounds.data[i];
        }
    }
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_WAVESOUND) {
        return NULL;
    }
    const nmo_wavesound_state_t *state = (const nmo_wavesound_state_t *)nmo_object_get_state(object);
    eng_sound_t *sound = NULL;
    if (!ENG_VEC_PUSH(engine->media.sounds, sound)) {
        return NULL;
    }
    sound->id = id;
    sound->length = state != NULL && state->has_duration ? (float)state->duration : 0.0f;
    return sound;
}

void eng_sounds_begin_frame(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->media.sounds.count; i++) {
        eng_sound_t *sound = &engine->media.sounds.data[i];
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
    for (size_t i = 0; engine != NULL && i < engine->media.sounds.count; i++) {
        const eng_sound_t *sound = &engine->media.sounds.data[i];
        if (sound->id == sound_id) {
            return sound->playing && !sound->paused;
        }
    }
    return false;
}

/* CKWaveSound::Play: from the start (a fade in changes only the volume). */
void eng_sound_play(blk_t *b, eng_sound_t *sound, bool loop)
{
    sound->playing = true;
    sound->paused = false;
    sound->position = 0.0f;
    sound->fade_left = 0.0f;
    sound->loop = loop;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, sound->id, 0);
}

/* CKWaveSound::Stop: now, or once the fade out is over. */
void eng_sound_stop(eng_sound_t *sound, float fade_out)
{
    if (fade_out > 0.0f && sound->playing) {
        sound->fade_left = fade_out;
    } else {
        sound->playing = false;
    }
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
float eng_wave_length(nmo_engine_t *engine, const char *name)
{
    const char *path = engine->host.resolve_file != NULL
                           ? engine->host.resolve_file(engine->host.resolve_file_user_data, name)
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

/* ============================================================================
 * Textures and movies
 * ============================================================================ */

const eng_screen_mode_t eng_screen_modes[ENG_SCREEN_MODE_COUNT] = {
    {640, 480, 16}, {640, 480, 32}, {800, 600, 16}, {800, 600, 32}, {1024, 768, 16}, {1024, 768, 32}};

/* The engine's state of texture `id`, made the first time; NULL when `id` is no CKTexture. */
eng_texture_t *eng_texture(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->media.textures.count; i++) {
        if (engine->media.textures.data[i].id == id) {
            return &engine->media.textures.data[i];
        }
    }
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_TEXTURE) {
        return NULL;
    }
    const nmo_texture_state_t *state = (const nmo_texture_state_t *)nmo_object_get_state(object);
    eng_texture_t *texture = NULL;
    if (!ENG_VEC_PUSH(engine->media.textures, texture)) {
        return NULL;
    }
    texture->id = id;
    texture->slot_count = state != NULL ? (int32_t)state->slot_count : 0;
    texture->current_slot = state != NULL && state->has_current_slot ? state->current_slot : 0;
    return texture;
}

int32_t nmo_engine_texture_current_slot(const nmo_engine_t *engine, nmo_object_id_t texture_id)
{
    for (size_t i = 0; engine != NULL && i < engine->media.textures.count; i++) {
        if (engine->media.textures.data[i].id == texture_id) {
            return engine->media.textures.data[i].current_slot;
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
int32_t eng_movie_frame_count(nmo_engine_t *engine, const char *name)
{
    const char *path = engine->host.resolve_file != NULL
                           ? engine->host.resolve_file(engine->host.resolve_file_user_data, name)
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

/* Whether the file `name` names is there to read. */
bool eng_media_file_exists(nmo_engine_t *engine, const char *name)
{
    const char *path = engine->host.resolve_file != NULL
                           ? engine->host.resolve_file(engine->host.resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file != NULL) {
        fclose(file);
    }
    return file != NULL;
}
