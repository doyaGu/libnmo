/**
 * @file engine_media.h
 * @brief Sounds and textures in time (the sound manager, bitmap slots).
 *        Nothing plays.
 */

#ifndef NMO_ENGINE_MEDIA_H
#define NMO_ENGINE_MEDIA_H

#include "engine_base.h"

struct nmo_bitmap_slots;

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
    /* CKWaveSound's 3D settings */
    uint32_t type;               /* CK_WAVESOUND_TYPE */
    float priority;
    float cone[3];               /* inside and outside angles (degrees), outside gain */
    float min_distance, max_distance;
    bool mute_after_max;
    nmo_object_id_t attached;    /* PositionSound's object; 0 for the world */
    float place[3], heading[3]; /* PositionSound's position and direction, in the attached object's space */
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
/* An image as CKBitmapData keeps it in system memory: RGBA, unkeyed */
typedef struct eng_surface {
    int32_t width, height;
    uint8_t *rgba;
} eng_surface_t;

typedef struct eng_texture {
    nmo_object_id_t id;
    int32_t slot_count;
    int32_t current_slot;
    bool movie;
    eng_movie_t *movie_reader; /* Movie Load's movie; NULL when it cannot be decoded */
    ENG_VEC(char *) files; /* the file each slot was loaded from at run time (LoadImage); NULL: the file's */
    int32_t mipmap_level;  /* CKTexture's: 0 none, -1 as many as the size gives, else that many */
    bool transparent;            /* CKBitmapData::IsTransparent */
    uint32_t transparent_color;  /* ARGB; its pixels are clear when transparent */
    /* Each slot's image once a block wrote or sized it (rgba NULL: the file's) */
    ENG_VEC(eng_surface_t) surfaces;
    /* The user mipmap levels blocks set (CKTexture::SetUserMipMapMode); the file's otherwise */
    bool user_levels;
    ENG_VEC(eng_surface_t) levels;
    bool sprite; /* a CKSprite's bitmap, not a CKTexture's */
} eng_texture_t;

/* The sound manager's settings (CKSoundManager::SetSoundSettings) and listener */
typedef struct eng_sound_manager {
    bool set;                    /* false: every factor 1 */
    float distance_factor, doppler_factor, global_gain;
    nmo_object_id_t listener;    /* 0: the render context's camera */
} eng_sound_manager_t;

typedef struct eng_media {
    eng_sound_manager_t manager;
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
/* The sound manager's settings, every factor 1 until a block sets one. */
eng_sound_manager_t *eng_sound_manager(nmo_engine_t *engine);
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
/* The samples a second the WAV file `name` names records (CKWaveFormat::nSamplesPerSec); 0 when none */
uint32_t eng_wave_sample_rate(nmo_engine_t *engine, const char *name);
/* The engine's state of the bitmap of texture or sprite `id`, made the first time; NULL
   when `id` is neither a CKTexture nor a CKSprite. */
eng_texture_t *eng_texture(nmo_engine_t *engine, nmo_object_id_t id);
/* The bitmap slots the file gives texture or sprite `id` (a sprite sharing another's: that
   one's), empty for a sprite without; false when `id` is neither. */
bool eng_bitmap_view(const nmo_engine_t *engine, nmo_object_id_t id, struct nmo_bitmap_slots *out);
/* CKBitmapData::SetCurrentSlot: false (nothing done) for a slot it does not have */
bool eng_texture_set_slot(eng_texture_t *texture, int32_t slot);
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
/* CKBitmapData::LockSurfacePtr: slot `slot`'s image (-1 the current one) to write, read
   from the file the first time; NULL when there is none (no image, a movie). Blocks call
   eng_texture_changed once they wrote it. */
eng_surface_t *eng_texture_surface(nmo_engine_t *engine, eng_texture_t *texture, int32_t slot);
/* CKBitmapData::GetWidth/GetHeight: the size of the first slot with an image; false for none */
bool eng_bitmap_size(nmo_engine_t *engine, eng_texture_t *texture, int32_t *w, int32_t *h);
/* CKTexture::Create(width, height, 32, slot): an opaque black image; false (nothing done)
   for a size of 0, or another size than the texture's when it has other slots. */
bool eng_texture_create(nmo_engine_t *engine, eng_texture_t *texture, int32_t width, int32_t height,
                        int32_t slot);
/* CKBitmapData::ResizeImages: every slot's image to that size (VxResizeImage32); false for
   a movie, a texture without an image, or a size of 0. */
bool eng_texture_resize(nmo_engine_t *engine, eng_texture_t *texture, int32_t width, int32_t height);
/* VxResizeImage32: `src` (w1 x h1 pixels of 32 bits) scaled to `dst` (w2 x h2), averaging
   the pixels a smaller image covers and mixing neighbours for a larger one. */
void eng_resize_image32(const uint32_t *src, int32_t w1, int32_t h1, uint32_t *dst, int32_t w2, int32_t h2);
/* CKTexture::SetUserMipMapMode(TRUE): levels down to 1x1, clear, for a single slot texture of
   power of two sides; the file's levels when it has some. False when it cannot. */
bool eng_texture_use_user_levels(nmo_engine_t *engine, eng_texture_t *texture);

/* Whether the file `name` names is there to read. */
bool eng_media_file_exists(nmo_engine_t *engine, const char *name);

#endif /* NMO_ENGINE_MEDIA_H */
