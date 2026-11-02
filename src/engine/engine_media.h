/**
 * @file engine_media.h
 * @brief Sounds and textures in time (the sound manager, bitmap slots).
 *        Nothing plays.
 */

#ifndef NMO_ENGINE_MEDIA_H
#define NMO_ENGINE_MEDIA_H

#include "engine_base.h"

/* A sound as the engine plays it: no audio, its position in time and its settings. */
typedef struct eng_sound {
    nmo_object_id_t id;
    char *file;       /* the wave file it names (Sound Load's, else the file's); NULL for none */
    float length;     /* ms, as the file records it; 0 when it does not */
    float position;   /* ms since it started */
    float fade_left;  /* ms of fade out before it stops; 0 when not stopping */
    float fade_out;   /* the fade out's whole length */
    float fade_in;    /* ms of the fade in, from the start */
    float gain, pitch, pan;
    double last_instance; /* the engine time an instance last started; < 0 for never */
    uint32_t starts;
    bool playing;
    bool paused;
    bool loop;
} eng_sound_t;

/* An instance of a sound (CKWaveSound::PlayMinion): it plays once, then goes */
typedef struct eng_sound_instance {
    uint32_t key;
    nmo_object_id_t sound;
    float position, length;
    float gain;
} eng_sound_instance_t;

/* An AVI movie's frames, decoded as they are asked for */
typedef struct eng_movie eng_movie_t;

/* A texture's bitmap slots as CKBitmapData has them: a loaded movie's frames, or its own. */
typedef struct eng_texture {
    nmo_object_id_t id;
    int32_t slot_count;
    int32_t current_slot;
    bool movie;
    eng_movie_t *movie_reader; /* Movie Load's movie; NULL when it cannot be decoded */
    ENG_VEC(char *) files; /* the file each slot was loaded from at run time (LoadImage); NULL: the file's */
    int32_t mipmap_level;  /* CKTexture's: 0 none, -1 as many as the size gives, else that many */
} eng_texture_t;

typedef struct eng_media {
    ENG_VEC(eng_sound_t) sounds;
    ENG_VEC(eng_sound_instance_t) instances;
    uint32_t instance_keys;
    ENG_VEC(nmo_engine_voice_t) voices;
    ENG_VEC(eng_texture_t) textures;
} eng_media_t;

/* core/media.c */
/* The sounds move on by the frame time (the sound manager's frame). */
void eng_sounds_begin_frame(nmo_engine_t *engine);
void eng_free_sounds(nmo_engine_t *engine);
/* The engine's state of sound `id`, made the first time; NULL when `id` is no CKWaveSound. */
eng_sound_t *eng_sound(nmo_engine_t *engine, nmo_object_id_t id);
/* CKWaveSound::Play: from the start, its gain rising from 0 over `fade_in` ms. */
void eng_sound_play(nmo_engine_block_t *b, eng_sound_t *sound, bool loop, float fade_in);
/* CKWaveSound::PlayMinion: an instance at `gain`, unless one started less than `min_delay` ms ago. */
void eng_sound_play_instance(nmo_engine_t *engine, eng_sound_t *sound, float min_delay, float gain);
/* Sound Load: the sound plays `name` from now on. */
void eng_sound_set_file(eng_sound_t *sound, const char *name);
/* CKWaveSound::Stop: now, or once the fade out is over. */
void eng_sound_stop(eng_sound_t *sound, float fade_out);
/*
 * The length in ms of the WAV file `name` names (its data over the average
 * bytes a second its format records), as the wave reader gives it; -1 when
 * it is no WAV file.
 */
float eng_wave_length(nmo_engine_t *engine, const char *name);
/* The engine's state of texture `id`, made the first time; NULL when `id` is no CKTexture. */
eng_texture_t *eng_texture(nmo_engine_t *engine, nmo_object_id_t id);
/* CKTexture::UseMipmap: 1 or less than 0 for all the levels, more than 1 that many, 0 none */
void eng_texture_use_mipmap(nmo_engine_t *engine, nmo_object_id_t id, int32_t use);
/*
 * The frame count of the AVI movie `name` names (the AVI movie reader's
 * GetMovieFrameCount, from the main header); -1 when it is none.
 */
int32_t eng_movie_frame_count(nmo_engine_t *engine, const char *name);
/* core/movie.c */
/* The movie file `name` names, read whole; NULL when it is no AVI of MS Video 1 at 16 bits. */
eng_movie_t *eng_movie_open(nmo_engine_t *engine, const char *name);
void eng_movie_close(eng_movie_t *movie);
/* Frame `frame` as RGBA, top row first, owned by the movie until the next call */
bool eng_movie_frame(eng_movie_t *movie, int32_t frame, const uint8_t **out_rgba,
                     int32_t *out_width, int32_t *out_height);
/* CKTexture::LoadImage: slot `slot` shows file `name` from now on; false when it cannot be read. */
bool eng_texture_load(nmo_engine_t *engine, eng_texture_t *texture, const char *name, int32_t slot);
void eng_free_textures(nmo_engine_t *engine);
/* Whether the file `name` names is there to read. */
bool eng_media_file_exists(nmo_engine_t *engine, const char *name);

#endif /* NMO_ENGINE_MEDIA_H */
