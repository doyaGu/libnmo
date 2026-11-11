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
#include "object/builtin/nmo_sprite_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"

#include <stdio.h>
#include <stdlib.h>
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
    memset(sound, 0, sizeof(*sound));
    sound->id = id;
    sound->length = state != NULL && state->has_duration ? (float)state->duration : 0.0f;
    /* CKWaveSound's settings as the file has them, else its defaults */
    bool settings = state != NULL && state->has_data2;
    sound->gain = settings ? state->gain : 1.0f;
    sound->pitch = settings && state->pitch > 0.0f ? state->pitch : 1.0f;
    sound->pan = settings ? state->pan : 0.0f;
    sound->type = settings && (state->state_flags & CK_WAVESOUND_ALLTYPE) != 0
                      ? state->state_flags & CK_WAVESOUND_ALLTYPE
                      : CK_WAVESOUND_BACKGROUND;
    sound->priority = settings ? state->priority : 0.5f;
    sound->cone[0] = settings ? state->cone_in_angle : 360.0f;
    sound->cone[1] = settings ? state->cone_out_angle : 360.0f;
    sound->cone[2] = settings ? state->cone_out_gain : 1.0f;
    sound->min_distance = settings ? state->min_distance : 1.0f;
    sound->max_distance = settings ? state->max_distance : 100.0f;
    sound->mute_after_max = settings && state->distance_behavior != 0;
    if (settings) {
        sound->attached = eng_object_ref(engine, id, &state->attached_object);
        memcpy(sound->place, &state->position, sizeof(sound->place));
        memcpy(sound->heading, &state->direction, sizeof(sound->heading));
    } else {
        sound->heading[2] = 1.0f;
    }
    sound->last_instance = -1.0;
    /* CKSound's file name, else the one older wave sounds kept */
    if (state != NULL && state->base.file_name != NULL && state->base.file_name[0] != '\0') {
        eng_sound_set_file(sound, state->base.file_name);
    } else if (state != NULL && state->has_wave_file_name && state->wave_file_name != NULL &&
               state->wave_file_name[0] != '\0') {
        eng_sound_set_file(sound, state->wave_file_name);
    }
    return sound;
}

void eng_sound_set_file(eng_sound_t *sound, const char *name)
{
    size_t length = strlen(name);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, name, length + 1);
        free(sound->file);
        sound->file = copy;
    }
}

void eng_free_sounds(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->media.sounds.count; i++) {
        free(engine->media.sounds.data[i].file);
    }
    free(engine->media.sounds.data);
    free(engine->media.instances.data);
    free(engine->media.voices.data);
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
    size_t kept = 0;
    for (size_t i = 0; i < engine->media.instances.count; i++) {
        eng_sound_instance_t *instance = &engine->media.instances.data[i];
        instance->position += engine->delta;
        if (instance->position < instance->length) {
            engine->media.instances.data[kept++] = *instance;
        }
    }
    engine->media.instances.count = kept;
}

void eng_sound_play_instance(nmo_engine_t *engine, eng_sound_t *sound, float min_delay, float gain)
{
    double now = engine->time;
    if (sound->last_instance >= 0.0 && now - sound->last_instance < (double)min_delay) {
        return;
    }
    eng_sound_instance_t *instance = NULL;
    if (sound->length <= 0.0f || !ENG_VEC_PUSH(engine->media.instances, instance)) {
        return;
    }
    sound->last_instance = now;
    instance->key = ++engine->media.instance_keys;
    instance->sound = sound->id;
    instance->position = 0.0f;
    instance->length = sound->length;
    instance->gain = gain;
}

/* The gain a sound plays at now: its own, through its fade in and out */
static float eng_sound_gain(const eng_sound_t *sound)
{
    float gain = sound->gain;
    if (sound->fade_in > 0.0f && sound->position < sound->fade_in) {
        gain *= sound->position / sound->fade_in;
    }
    if (sound->fade_left > 0.0f && sound->fade_out > 0.0f) {
        gain *= sound->fade_left / sound->fade_out;
    }
    return gain;
}

static const char *eng_sound_file(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->media.sounds.count; i++) {
        if (engine->media.sounds.data[i].id == id) {
            return engine->media.sounds.data[i].file;
        }
    }
    return NULL;
}

eng_sound_manager_t *eng_sound_manager(nmo_engine_t *engine)
{
    eng_sound_manager_t *manager = &engine->media.manager;
    if (!manager->set) {
        manager->set = true;
        manager->distance_factor = manager->doppler_factor = manager->global_gain = 1.0f;
        manager->rolloff_factor = 1.0f;
    }
    return manager;
}

/* A voice's 3D settings: its sound's, its position and direction in the world */
static void eng_voice_place(nmo_engine_t *engine, const eng_sound_t *sound, nmo_engine_voice_t *voice)
{
    if (engine->media.manager.set) {
        voice->gain *= engine->media.manager.global_gain;
    }
    voice->type = sound->type;
    voice->priority = sound->priority;
    memcpy(voice->cone, sound->cone, sizeof(voice->cone));
    voice->min_distance = sound->min_distance;
    voice->max_distance = sound->max_distance;
    voice->mute_after_max = sound->mute_after_max;
    eng_vector_t position = {sound->place[0], sound->place[1], sound->place[2]};
    eng_vector_t direction = {sound->heading[0], sound->heading[1], sound->heading[2]};
    int32_t attached = sound->attached != 0 ? eng_entity_index(engine, sound->attached) : -1;
    if (attached >= 0) {
        eng_vector_t local = position;
        eng_entity_transform(engine, attached, &position, &local, -1);
        local = direction;
        eng_entity_transform_vector(engine, attached, &direction, &local, -1, false);
    }
    voice->world_position[0] = position.x, voice->world_position[1] = position.y;
    voice->world_position[2] = position.z;
    voice->world_direction[0] = direction.x, voice->world_direction[1] = direction.y;
    voice->world_direction[2] = direction.z;
}

/*
 * DX7SoundManager::PostProcess calls CKWaveSound::UpdatePosition with the
 * time manager's delta (ms) for each sound playing that is not a background
 * one; PositionSource sets the source's velocity to the world position's
 * change since the last update times that delta and 0.001 (a product, as
 * CK2.dll has it), and keeps the position for the next.
 */
void eng_sounds_end_frame(nmo_engine_t *engine)
{
    const double delta = engine->delta;
    for (size_t i = 0; i < engine->media.sounds.count; i++) {
        eng_sound_t *sound = &engine->media.sounds.data[i];
        if (!sound->playing || sound->paused || (sound->type & 1u) != 0) {
            continue;
        }
        eng_vector_t position = {sound->place[0], sound->place[1], sound->place[2]};
        int32_t attached = sound->attached != 0 ? eng_entity_index(engine, sound->attached) : -1;
        if (attached >= 0) {
            eng_vector_t local = position;
            eng_entity_transform(engine, attached, &position, &local, -1);
        }
        const float world[3] = {position.x, position.y, position.z};
        for (int c = 0; c < 3; c++) {
            sound->velocity[c] = (float)((world[c] - sound->old_place[c]) * delta * 0.001);
            sound->old_place[c] = world[c];
        }
    }
}

bool nmo_engine_sound_listener(const nmo_engine_t *engine, nmo_object_id_t *out_listener,
                               float *out_distance_factor, float *out_doppler_factor, float *out_global_gain)
{
    if (engine == NULL) {
        return false;
    }
    const eng_sound_manager_t *manager = &engine->media.manager;
    if (out_listener != NULL) {
        *out_listener = manager->listener != 0 ? manager->listener : engine->render.camera;
    }
    if (out_distance_factor != NULL) {
        *out_distance_factor = manager->set ? manager->distance_factor : 1.0f;
    }
    if (out_doppler_factor != NULL) {
        *out_doppler_factor = manager->set ? manager->doppler_factor : 1.0f;
    }
    if (out_global_gain != NULL) {
        *out_global_gain = manager->set ? manager->global_gain : 1.0f;
    }
    return true;
}

const nmo_engine_voice_t *nmo_engine_voices(nmo_engine_t *engine, size_t *out_count)
{
    *out_count = 0;
    if (engine == NULL) {
        return NULL;
    }
    engine->media.voices.count = 0;
    for (size_t i = 0; i < engine->media.sounds.count; i++) {
        const eng_sound_t *sound = &engine->media.sounds.data[i];
        nmo_engine_voice_t *voice = NULL;
        if (!sound->playing || sound->file == NULL || !ENG_VEC_PUSH(engine->media.voices, voice)) {
            continue;
        }
        *voice = (nmo_engine_voice_t){.key = sound->id, .sound = sound->id, .file = sound->file,
                                      .start = sound->starts, .position = sound->position,
                                      .gain = eng_sound_gain(sound), .pitch = sound->pitch,
                                      .pan = sound->pan, .loop = sound->loop, .paused = sound->paused};
        eng_voice_place(engine, sound, voice);
    }
    for (size_t i = 0; i < engine->media.instances.count; i++) {
        const eng_sound_instance_t *instance = &engine->media.instances.data[i];
        const char *file = eng_sound_file(engine, instance->sound);
        eng_sound_t *sound = file != NULL ? eng_sound(engine, instance->sound) : NULL;
        nmo_engine_voice_t *voice = NULL;
        if (sound == NULL || !ENG_VEC_PUSH(engine->media.voices, voice)) {
            continue;
        }
        /* Instance keys count apart from object IDs: from the top of the range down */
        *voice = (nmo_engine_voice_t){.key = UINT32_MAX - instance->key, .sound = instance->sound,
                                      .file = file, .position = instance->position,
                                      .gain = instance->gain, .pitch = sound->pitch, .pan = sound->pan};
        eng_voice_place(engine, sound, voice);
    }
    *out_count = engine->media.voices.count;
    return engine->media.voices.data;
}

unsigned char *nmo_engine_read_sound_file(nmo_engine_t *engine, const char *name,
                                          size_t *out_size)
{
    return engine != NULL && name != NULL ? eng_read_path_file(engine, name, 2, out_size) : NULL;
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

/* CKWaveSound::Play: from the start, its gain rising from 0 over `fade_in` ms. */
void eng_sound_play(blk_t *b, eng_sound_t *sound, bool loop, float fade_in)
{
    sound->playing = true;
    sound->paused = false;
    sound->position = 0.0f;
    sound->fade_left = 0.0f;
    sound->fade_in = fade_in > 0.0f ? fade_in : 0.0f;
    sound->loop = loop;
    sound->starts++;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, sound->id, 0);
}

/* CKWaveSound::Stop: now, or once the fade out is over. */
void eng_sound_stop(eng_sound_t *sound, float fade_out)
{
    if (fade_out > 0.0f && sound->playing) {
        sound->fade_left = fade_out;
        sound->fade_out = fade_out;
    } else {
        sound->playing = false;
    }
}

static uint32_t read_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* The WAV file `name` names: its format's rate and bytes a second, and its data size */
static bool eng_wave_scan(nmo_engine_t *engine, const char *name, uint32_t *out_sample_rate,
                          uint32_t *out_bytes_per_second, uint32_t *out_data_size)
{
    const char *path = engine->host.resolve_file != NULL
                           ? engine->host.resolve_file(engine->host.resolve_file_user_data, name)
                           : NULL;
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        return false;
    }
    unsigned char header[12];
    uint32_t sample_rate = 0, bytes_per_second = 0, data_size = 0;
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
                sample_rate = read_le32(chunk + 4);
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
    if (!has_format || !has_data) {
        return false;
    }
    *out_sample_rate = sample_rate;
    *out_bytes_per_second = bytes_per_second;
    *out_data_size = data_size;
    return true;
}

/*
 * The length in ms of the WAV file `name` names (its data over the average
 * bytes a second its format records), as the wave reader gives it; -1 when
 * it is no WAV file.
 */
float eng_wave_length(nmo_engine_t *engine, const char *name)
{
    uint32_t sample_rate, bytes_per_second, data_size;
    if (!eng_wave_scan(engine, name, &sample_rate, &bytes_per_second, &data_size) ||
        bytes_per_second == 0) {
        return -1.0f;
    }
    return (float)((double)data_size * 1000.0 / (double)bytes_per_second);
}

uint32_t eng_wave_sample_rate(nmo_engine_t *engine, const char *name)
{
    uint32_t sample_rate, bytes_per_second, data_size;
    return name != NULL && eng_wave_scan(engine, name, &sample_rate, &bytes_per_second, &data_size)
               ? sample_rate
               : 0u;
}

/* ============================================================================
 * Textures and movies
 * ============================================================================ */

const eng_screen_mode_t eng_screen_modes[ENG_SCREEN_MODE_COUNT] = {
    {640, 480, 16}, {640, 480, 32}, {800, 600, 16}, {800, 600, 32}, {1024, 768, 16}, {1024, 768, 32}};

/* The engine's state of texture `id`, made the first time; NULL when `id` is no CKTexture. */
/* A sprite's own state, or that of the sprite it shares its bitmap with (CKSprite::Load:
   CopySpriteData); NULL for none */
static const nmo_sprite_state_t *eng_sprite_bitmap_state(const nmo_engine_t *engine, nmo_object_id_t id)
{
    for (int depth = 0; id != 0 && depth < 8; depth++) {
        nmo_object_t *object = eng_find_object(engine, id);
        if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_SPRITE) {
            return NULL;
        }
        const nmo_sprite_state_t *state = (const nmo_sprite_state_t *)nmo_object_get_state(object);
        if (state == NULL || !state->has_sprite_ref) {
            return state;
        }
        id = eng_object_ref(engine, id, &state->sprite_ref);
    }
    return NULL;
}

bool eng_bitmap_view(const nmo_engine_t *engine, nmo_object_id_t id, nmo_bitmap_slots_t *out)
{
    memset(out, 0, sizeof(*out));
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    uint32_t class_id = object != NULL ? nmo_object_get_class_id(object) : 0;
    if (class_id == NMO_CID_TEXTURE) {
        const nmo_texture_state_t *state = (const nmo_texture_state_t *)nmo_object_get_state(object);
        if (state == NULL) {
            return false;
        }
        out->kind = state->bitmap_kind;
        out->slot_count = state->slot_count;
        out->reader_slots = state->reader_slots;
        out->raw_slots = state->raw_slots;
        out->bitmap2_slots = state->bitmap2_slots;
        out->has_slot_filenames = state->has_slot_filenames;
        out->slot_filenames = state->slot_filenames;
        out->has_movie_filename = state->has_movie_filename;
        out->movie_filename = state->movie_filename;
        return true;
    }
    const nmo_sprite_state_t *sprite = class_id == NMO_CID_SPRITE ? eng_sprite_bitmap_state(engine, id) : NULL;
    if (sprite == NULL) {
        return false;
    }
    if (sprite->has_bitmap_data) {
        *out = sprite->bitmap;
    }
    return true;
}

eng_texture_t *eng_texture(nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->media.textures.count; i++) {
        if (engine->media.textures.data[i].id == id) {
            return &engine->media.textures.data[i];
        }
    }
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    uint32_t class_id = object != NULL ? nmo_object_get_class_id(object) : 0;
    if (class_id == NMO_CID_SPRITE) {
        /* A sprite's bitmap, as its file or the sprite it shares it with left it */
        const nmo_sprite_state_t *sprite = eng_sprite_bitmap_state(engine, id);
        eng_texture_t *texture = NULL;
        if (sprite == NULL || !ENG_VEC_PUSH(engine->media.textures, texture)) {
            return NULL;
        }
        texture->id = id;
        texture->sprite = true;
        texture->slot_count = sprite->has_bitmap_data ? (int32_t)sprite->bitmap.slot_count : 0;
        texture->current_slot = sprite->has_slot ? (int32_t)sprite->current_slot : 0;
        texture->transparent = sprite->has_transparency && sprite->is_transparent;
        texture->transparent_color = sprite->has_transparency ? sprite->transparent_color : 0u;
        return texture;
    }
    if (class_id != NMO_CID_TEXTURE) {
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
    /* The file's level count (0xFF stands for every level), an old file's mipmap flag, or
       saved levels (CKTexture::Load: SetUserMipMapMode, which uses them) */
    texture->transparent = state != NULL && state->is_transparent;
    texture->transparent_color = state != NULL && state->has_transparent_color ? state->transparent_color : 0u;
    texture->mipmap_level = state == NULL ? 0 : state->mipmap_level == 0xFF ? -1 : state->mipmap_level;
    if (state != NULL && texture->mipmap_level == 0 &&
        ((state->has_legacy_video_format && state->legacy_use_mipmap != 0) ||
         (state->has_user_mipmaps && state->user_mipmap_count > 0))) {
        texture->mipmap_level = -1;
    }
    return texture;
}

bool eng_texture_set_slot(eng_texture_t *texture, int32_t slot)
{
    if (slot < 0 || slot >= texture->slot_count) {
        return false;
    }
    texture->current_slot = slot;
    return true;
}

void eng_texture_use_mipmap(nmo_engine_t *engine, nmo_object_id_t id, int32_t use)
{
    eng_texture_t *texture = eng_texture(engine, id);
    if (texture != NULL) {
        texture->mipmap_level = use == 1 || use < 0 ? -1 : use > 1 ? use : 0;
    }
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
bool eng_texture_load(nmo_engine_t *engine, eng_texture_t *texture, const char *name, int32_t slot)
{
    size_t size = 0;
    unsigned char *data = slot >= 0 ? eng_read_path_file(engine, name, 0, &size) : NULL;
    if (data == NULL) {
        return false;
    }
    free(data);
    size_t length = strlen(name);
    char *copy = (char *)malloc(length + 1);
    while (copy != NULL && texture->files.count <= (size_t)slot) {
        char **item = NULL;
        if (!ENG_VEC_PUSH(texture->files, item)) {
            free(copy);
            return false;
        }
    }
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, name, length + 1);
    free(texture->files.data[slot]);
    texture->files.data[slot] = copy;
    if ((size_t)slot < texture->surfaces.count) {
        free(texture->surfaces.data[slot].rgba); /* the slot is the file's again */
        texture->surfaces.data[slot] = (eng_surface_t){0, 0, NULL};
    }
    if (slot >= texture->slot_count) {
        texture->slot_count = slot + 1;
    }
    eng_texture_changed(engine, texture->id);
    return true;
}

void eng_free_textures(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->media.textures.count; i++) {
        eng_texture_t *texture = &engine->media.textures.data[i];
        for (size_t f = 0; f < texture->files.count; f++) {
            free(texture->files.data[f]);
        }
        free(texture->files.data);
        for (size_t s = 0; s < texture->surfaces.count; s++) {
            free(texture->surfaces.data[s].rgba);
        }
        free(texture->surfaces.data);
        for (size_t l = 0; l < texture->levels.count; l++) {
            free(texture->levels.data[l].rgba);
        }
        free(texture->levels.data);
        eng_movie_close(texture->movie_reader);
    }
    free(engine->media.textures.data);
}

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
