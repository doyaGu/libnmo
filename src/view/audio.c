/**
 * @file audio.c
 * @brief The engine's voices through SDL3 audio streams, one a voice.
 *
 * Each wave file is read through the engine's sound paths and converted
 * once to stereo floats; a voice's stream gets it a little ahead of time,
 * panned as it goes in, with the voice's gain and pitch as the stream's
 * gain and frequency ratio. The settings mean what Ballance's
 * DX7SoundManager makes of them for DirectSound: the gain is an amplitude
 * up to 1 (20 log10 of it in dB), a pan lowers the other side linearly, and
 * a pitch scales the file's rate, 0 being the file's own (DSBFREQUENCY_ORIGINAL).
 */

#include "view_audio.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

#define AUDIO_RATE 44100
#define AUDIO_AHEAD 0.15 /* seconds queued ahead of what plays */

typedef struct audio_wave {
    char *name;
    float *samples; /* stereo frames at AUDIO_RATE; NULL when the file could not be read */
    size_t frames;
} audio_wave_t;

typedef struct audio_voice {
    uint32_t key, start;
    SDL_AudioStream *stream;
    const audio_wave_t *wave;
    size_t cursor; /* the next frame to queue */
    bool paused, seen;
} audio_voice_t;

struct view_audio {
    SDL_AudioDeviceID device;
    audio_wave_t **waves; /* each apart: voices keep pointers to them */
    size_t wave_count;
    audio_voice_t *voices;
    size_t voice_count, voice_capacity;
};

view_audio_t *view_audio_open(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return NULL;
    }
    view_audio_t *audio = (view_audio_t *)calloc(1, sizeof(*audio));
    if (audio == NULL) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    audio->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (audio->device == 0) {
        free(audio);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    return audio;
}

void view_audio_close(view_audio_t *audio)
{
    if (audio == NULL) {
        return;
    }
    for (size_t i = 0; i < audio->voice_count; i++) {
        SDL_DestroyAudioStream(audio->voices[i].stream);
    }
    free(audio->voices);
    for (size_t i = 0; i < audio->wave_count; i++) {
        free(audio->waves[i]->name);
        SDL_free(audio->waves[i]->samples);
        free(audio->waves[i]);
    }
    free(audio->waves);
    SDL_CloseAudioDevice(audio->device);
    free(audio);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

/* The wave file `name` as stereo floats, read the first time it is asked for */
static const audio_wave_t *audio_wave(view_audio_t *audio, nmo_engine_t *engine, const char *name)
{
    for (size_t i = 0; i < audio->wave_count; i++) {
        if (strcmp(audio->waves[i]->name, name) == 0) {
            return audio->waves[i];
        }
    }
    audio_wave_t **grown =
        (audio_wave_t **)realloc(audio->waves, (audio->wave_count + 1) * sizeof(*grown));
    if (grown == NULL) {
        return NULL;
    }
    audio->waves = grown;
    size_t length = strlen(name);
    audio_wave_t *wave = (audio_wave_t *)calloc(1, sizeof(*wave));
    char *copy = wave != NULL ? (char *)malloc(length + 1) : NULL;
    if (copy == NULL) {
        free(wave);
        return NULL;
    }
    memcpy(copy, name, length + 1);
    wave->name = copy;
    audio->waves[audio->wave_count++] = wave;

    size_t size = 0;
    unsigned char *data = nmo_engine_read_sound_file(engine, name, &size);
    SDL_IOStream *io = data != NULL ? SDL_IOFromConstMem(data, size) : NULL;
    SDL_AudioSpec spec;
    Uint8 *pcm = NULL;
    Uint32 pcm_size = 0;
    if (io != NULL && SDL_LoadWAV_IO(io, true, &spec, &pcm, &pcm_size)) {
        const SDL_AudioSpec stereo = {SDL_AUDIO_F32, 2, AUDIO_RATE};
        Uint8 *out = NULL;
        int out_size = 0;
        if (SDL_ConvertAudioSamples(&spec, pcm, (int)pcm_size, &stereo, &out, &out_size)) {
            wave->samples = (float *)out;
            wave->frames = (size_t)out_size / (2u * sizeof(float));
        }
        SDL_free(pcm);
    }
    free(data);
    return wave;
}

static audio_voice_t *audio_voice(view_audio_t *audio, uint32_t key)
{
    for (size_t i = 0; i < audio->voice_count; i++) {
        if (audio->voices[i].key == key) {
            return &audio->voices[i];
        }
    }
    return NULL;
}

/* A new voice's stream, bound to the device; NULL when it cannot be made */
static audio_voice_t *audio_add_voice(view_audio_t *audio, const nmo_engine_voice_t *voice,
                                      const audio_wave_t *wave)
{
    if (audio->voice_count == audio->voice_capacity) {
        size_t capacity = audio->voice_capacity != 0 ? audio->voice_capacity * 2 : 16;
        audio_voice_t *grown =
            (audio_voice_t *)realloc(audio->voices, capacity * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        audio->voices = grown;
        audio->voice_capacity = capacity;
    }
    const SDL_AudioSpec stereo = {SDL_AUDIO_F32, 2, AUDIO_RATE};
    SDL_AudioStream *stream = SDL_CreateAudioStream(&stereo, NULL);
    if (stream == NULL || !SDL_BindAudioStream(audio->device, stream)) {
        SDL_DestroyAudioStream(stream);
        return NULL;
    }
    audio_voice_t *added = &audio->voices[audio->voice_count++];
    memset(added, 0, sizeof(*added));
    added->key = voice->key;
    added->start = voice->start - 1u; /* so it starts below */
    added->stream = stream;
    added->wave = wave;
    return added;
}

/* The rate a voice plays at, of the file's */
static float audio_pitch(const nmo_engine_voice_t *source)
{
    if (source->pitch <= 0.0f) {
        return 1.0f;
    }
    return source->pitch < 0.01f ? 0.01f : source->pitch > 100.0f ? 100.0f : source->pitch;
}

/* Queue the voice's next frames, panned, until AUDIO_AHEAD of them wait */
static void audio_feed(audio_voice_t *voice, const nmo_engine_voice_t *source)
{
    const audio_wave_t *wave = voice->wave;
    double pitch = (double)audio_pitch(source);
    size_t wanted = (size_t)(AUDIO_AHEAD * AUDIO_RATE * pitch);
    size_t queued = (size_t)SDL_GetAudioStreamQueued(voice->stream) / (2u * sizeof(float));
    if (queued >= wanted) {
        return;
    }
    size_t count = wanted - queued;
    float *chunk = (float *)malloc(count * 2u * sizeof(float));
    if (chunk == NULL) {
        return;
    }
    float pan = source->pan < -1.0f ? -1.0f : source->pan > 1.0f ? 1.0f : source->pan;
    float left = pan > 0.0f ? 1.0f - pan : 1.0f, right = pan < 0.0f ? 1.0f + pan : 1.0f;
    size_t filled = 0;
    while (filled < count) {
        if (voice->cursor >= wave->frames) {
            if (!source->loop || wave->frames == 0) {
                break;
            }
            voice->cursor = 0;
        }
        const float *frame = &wave->samples[voice->cursor * 2u];
        chunk[filled * 2u] = frame[0] * left;
        chunk[filled * 2u + 1u] = frame[1] * right;
        voice->cursor++;
        filled++;
    }
    if (filled > 0) {
        SDL_PutAudioStreamData(voice->stream, chunk, (int)(filled * 2u * sizeof(float)));
    }
    free(chunk);
}

void view_audio_play(view_audio_t *audio, nmo_engine_t *engine)
{
    if (audio == NULL) {
        return;
    }
    for (size_t i = 0; i < audio->voice_count; i++) {
        audio->voices[i].seen = false;
    }
    size_t count = 0;
    const nmo_engine_voice_t *voices = nmo_engine_voices(engine, &count);
    for (size_t i = 0; i < count; i++) {
        const nmo_engine_voice_t *source = &voices[i];
        const audio_wave_t *wave = audio_wave(audio, engine, source->file);
        if (wave == NULL || wave->samples == NULL) {
            continue;
        }
        audio_voice_t *voice = audio_voice(audio, source->key);
        if (voice == NULL || voice->wave != wave) {
            if (voice != NULL) {
                voice->seen = false; /* another file: a new stream below, this one goes */
                voice->key = 0;
            }
            voice = audio_add_voice(audio, source, wave);
            if (voice == NULL) {
                continue;
            }
        }
        voice->seen = true;
        if (voice->start != source->start) {
            /* Started again: from where the engine has it */
            voice->start = source->start;
            SDL_ClearAudioStream(voice->stream);
            voice->cursor = (size_t)((double)source->position * AUDIO_RATE / 1000.0);
        }
        if (source->paused != voice->paused) {
            voice->paused = source->paused;
            if (voice->paused) {
                SDL_UnbindAudioStream(voice->stream);
            } else {
                SDL_BindAudioStream(audio->device, voice->stream);
            }
        }
        float gain = source->gain < 0.0f ? 0.0f : source->gain > 1.0f ? 1.0f : source->gain;
        SDL_SetAudioStreamGain(voice->stream, gain);
        SDL_SetAudioStreamFrequencyRatio(voice->stream, audio_pitch(source));
        if (!voice->paused) {
            audio_feed(voice, source);
        }
    }
    /* The voices the engine stopped: cut */
    size_t kept = 0;
    for (size_t i = 0; i < audio->voice_count; i++) {
        if (audio->voices[i].seen) {
            audio->voices[kept++] = audio->voices[i];
        } else {
            SDL_DestroyAudioStream(audio->voices[i].stream);
        }
    }
    audio->voice_count = kept;
}
