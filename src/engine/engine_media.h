/**
 * @file engine_media.h
 * @brief Sounds and textures in time (the sound manager, bitmap slots), and
 *        the render side the blocks ask about: the screen mode, video
 *        memory and fonts. Nothing plays or draws.
 */

#ifndef NMO_ENGINE_MEDIA_H
#define NMO_ENGINE_MEDIA_H

#include "engine_base.h"

/* A sound as the engine plays it: no audio, only its position in time. */
typedef struct eng_sound {
    nmo_object_id_t id;
    float length;    /* ms, as the file records it; 0 when it does not */
    float position;  /* ms since it started */
    float fade_left; /* ms of fade out before it stops; 0 when not stopping */
    bool playing;
    bool paused;
    bool loop;
} eng_sound_t;

/* A texture's bitmap slots as CKBitmapData has them: a loaded movie's frames, or its own. */
typedef struct eng_texture {
    nmo_object_id_t id;
    int32_t slot_count;
    int32_t current_slot;
    bool movie;
} eng_texture_t;

typedef struct eng_media {
    ENG_VEC(eng_sound_t) sounds;
    ENG_VEC(eng_texture_t) textures;
} eng_media_t;

/* A CKTextureFont: only what tells fonts apart, as nothing draws them. */
typedef struct eng_font {
    char *name;
    nmo_object_id_t texture; /* the texture it reads, 0 for a system font's own */
} eng_font_t;

/* A screen mode of the engine's one render driver (4:3, in 16 and 32 bits) */
typedef struct eng_screen_mode {
    int32_t width, height, bpp;
} eng_screen_mode_t;

#define ENG_SCREEN_MODE_COUNT 6
extern const eng_screen_mode_t eng_screen_modes[ENG_SCREEN_MODE_COUNT];

/* The render manager and context, and the font manager */
typedef struct eng_render {
    int32_t screen_mode;      /* the render context's mode, of the engine's driver's list */
    eng_map_t video_textures; /* the textures TT_PreloadTextures put in video memory */
    ENG_VEC(eng_font_t) fonts;
} eng_render_t;

/* core/media.c */
/* The sounds move on by the frame time (the sound manager's frame). */
void eng_sounds_begin_frame(nmo_engine_t *engine);
/* The engine's state of sound `id`, made the first time; NULL when `id` is no CKWaveSound. */
eng_sound_t *eng_sound(nmo_engine_t *engine, nmo_object_id_t id);
/* CKWaveSound::Play: from the start (a fade in changes only the volume). */
void eng_sound_play(nmo_engine_block_t *b, eng_sound_t *sound, bool loop);
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
/*
 * The frame count of the AVI movie `name` names (the AVI movie reader's
 * GetMovieFrameCount, from the main header); -1 when it is none.
 */
int32_t eng_movie_frame_count(nmo_engine_t *engine, const char *name);
/* Whether the file `name` names is there to read. */
bool eng_media_file_exists(nmo_engine_t *engine, const char *name);

/* blocks/interface.c (the font manager) */
void eng_fonts_clear(nmo_engine_t *engine);

#endif /* NMO_ENGINE_MEDIA_H */
