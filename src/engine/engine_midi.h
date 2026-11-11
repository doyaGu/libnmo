/**
 * @file engine_midi.h
 * @brief The Midi Manager (MidiManager.dll): the MIDI input port's messages
 *        and the notes held, and the CKMidiSounds' playback. The engine
 *        opens no device and plays no MIDI file: a host says how many input
 *        devices there are, hands in what the open port receives, and plays
 *        the midi sounds nmo_engine_midi_voices() lists.
 */

#ifndef NMO_ENGINE_MIDI_H
#define NMO_ENGINE_MIDI_H

#include "engine_base.h"
#include "engine/nmo_engine.h"

/* A short MIDI message as MidiInProc splits it */
typedef struct eng_midi_message {
    uint8_t channel, command, note, attack;
    uint32_t time;
} eng_midi_message_t;

/* MidiSound's sequencer states (SEQ_S_*) the blocks can reach */
typedef enum eng_midi_state {
    ENG_MIDI_NOFILE = 0,
    ENG_MIDI_OPENED = 1,
    ENG_MIDI_PLAYING = 4,
    ENG_MIDI_PAUSED = 5
} eng_midi_state_t;

typedef struct eng_midi_sound {
    nmo_object_id_t id;
    char *file;
    eng_midi_state_t state;
    float position; /* ms played since it started */
    uint32_t starts;
} eng_midi_sound_t;

typedef struct eng_midi {
    uint8_t notes[256]; /* a bit a note: 16 channels of 128 */
    ENG_VEC(eng_midi_message_t) incoming; /* listFromCallBack */
    ENG_VEC(eng_midi_message_t) current;  /* listForBehaviors: this frame's */
    int32_t desired_port, current_port, device_count, block_refs;
    bool open;
    ENG_VEC(eng_midi_sound_t) sounds;
    ENG_VEC(nmo_engine_midi_voice_t) voices;
} eng_midi_t;

/* core/midi.c */
void eng_free_midi(nmo_engine_t *engine);
/* MidiManager::PreProcess: this frame's messages are those received since the last */
void eng_midi_pre_process(nmo_engine_t *engine);
/* MidiManager::PostProcess: the messages dropped; a port asked for opened in place of the open one */
void eng_midi_post_process(nmo_engine_t *engine);
/* MidiManager::OnCKReset (the port closed) and OnCKPlay (opened when a block uses it) */
void eng_midi_reset(nmo_engine_t *engine);
void eng_midi_play(nmo_engine_t *engine);
/* The midi sounds move on by the frame time */
void eng_midi_sounds_begin_frame(nmo_engine_t *engine);
/* MidiManager::AddMidiBBref: a block that uses the input port loaded */
void eng_midi_add_block_ref(nmo_engine_t *engine);
/* Set Midi Input Port: the port to open (at the next frame's end) */
void eng_midi_set_desired_port(nmo_engine_t *engine, int32_t port);
/* MidiManager::IsNoteActive: the note (clamped to 0-127) held on the channel (0-15) */
bool eng_midi_note_active(const nmo_engine_t *engine, int32_t note, int32_t channel);
/* This frame's messages */
const eng_midi_message_t *eng_midi_messages(const nmo_engine_t *engine, size_t *out_count);
/* The engine's state of midi sound `id`, made the first time; NULL when `id` is no CKMidiSound */
eng_midi_sound_t *eng_midi_sound(nmo_engine_t *engine, nmo_object_id_t id);
/* MidiSound::Start (from the start; nothing while playing or paused), Stop, Pause, Restart */
void eng_midi_sound_play(eng_midi_sound_t *sound);
void eng_midi_sound_stop(eng_midi_sound_t *sound);
void eng_midi_sound_pause(eng_midi_sound_t *sound);
void eng_midi_sound_restart(eng_midi_sound_t *sound);

#endif /* NMO_ENGINE_MIDI_H */
