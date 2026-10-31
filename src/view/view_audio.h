/**
 * @file view_audio.h
 * @brief The engine's voices through SDL3 audio streams.
 */

#ifndef NMO_VIEW_AUDIO_H
#define NMO_VIEW_AUDIO_H

#include "engine/nmo_engine.h"

typedef struct view_audio view_audio_t;

/* The default playback device; NULL when there is no audio. */
view_audio_t *view_audio_open(void);
void view_audio_close(view_audio_t *audio);
/* Start, follow and stop a stream for each voice the engine has now. */
void view_audio_play(view_audio_t *audio, nmo_engine_t *engine);

#endif /* NMO_VIEW_AUDIO_H */
