/**
 * @file midi.c
 * @brief The Midi Manager of MidiManager.dll: the input port (MidiInProc,
 *        OpenMidiIn, CloseMidiIn and the manager's frame) and MidiSound's
 *        sequencer states, which the host follows to play the files.
 */

#include "../engine_internal.h"

#include "object/builtin/nmo_sound_schemas.h"
#include "object/nmo_class_ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void eng_free_midi(nmo_engine_t *engine)
{
    eng_midi_t *midi = &engine->midi;
    free(midi->incoming.data);
    free(midi->current.data);
    for (size_t i = 0; i < midi->sounds.count; i++) {
        free(midi->sounds.data[i].file);
    }
    free(midi->sounds.data);
    free(midi->voices.data);
}

/* ---- The input port ---- */

/* CloseMidiIn: nothing held, nothing received */
static void midi_close(eng_midi_t *midi)
{
    midi->open = false;
    midi->current_port = 0;
    midi->incoming.count = 0;
    midi->current.count = 0;
    memset(midi->notes, 0, sizeof(midi->notes));
}

/* OpenMidiIn: false when a port is open already or there is no such device */
static bool midi_open(eng_midi_t *midi, int32_t port)
{
    if (midi->open || port < 0 || port >= midi->device_count) {
        return false;
    }
    midi->open = true;
    midi->current_port = port;
    memset(midi->notes, 0, sizeof(midi->notes));
    midi->incoming.count = 0;
    midi->current.count = 0;
    return true;
}

static void midi_set_note(eng_midi_t *midi, int32_t note, int32_t channel, bool state)
{
    note = note < 0 ? 0 : note > 127 ? 127 : note;
    channel = channel < 0 ? 0 : channel > 15 ? 15 : channel;
    const int32_t byte = (channel << 4) + (note >> 3);
    const uint8_t bit = (uint8_t)(1u << (note & 7));
    midi->notes[byte] = state ? (uint8_t)(midi->notes[byte] | bit) : (uint8_t)(midi->notes[byte] & ~bit);
}

bool eng_midi_note_active(const nmo_engine_t *engine, int32_t note, int32_t channel)
{
    note = note < 0 ? 0 : note > 127 ? 127 : note;
    channel = channel < 0 ? 0 : channel > 15 ? 15 : channel;
    return (engine->midi.notes[(channel << 4) + (note >> 3)] & (1u << (note & 7))) != 0;
}

nmo_status_t nmo_engine_midi_input(nmo_engine_t *engine, uint32_t message, uint32_t time)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    eng_midi_t *midi = &engine->midi;
    if (!midi->open) {
        return NMO_ERR_INVALID_STATE;
    }
    eng_midi_message_t *slot = NULL;
    if (!ENG_VEC_PUSH(midi->incoming, slot)) {
        return NMO_ERR_NOMEM;
    }
    *slot = (eng_midi_message_t){(uint8_t)(message & 0x0Fu), (uint8_t)((message & 0xF0u) >> 4),
                                 (uint8_t)((message >> 8) & 0xFFu), (uint8_t)((message >> 16) & 0xFFu),
                                 time};
    /* Note On with a velocity holds the note; Note Off, or On without one, lets it go */
    if (slot->command == 9 || slot->command == 8) {
        midi_set_note(midi, slot->note, slot->channel, slot->command == 9 && slot->attack != 0);
    }
    return NMO_OK;
}

void nmo_engine_set_midi_input_devices(nmo_engine_t *engine, int32_t count)
{
    if (engine != NULL) {
        engine->midi.device_count = count > 0 ? count : 0;
    }
}

int32_t nmo_engine_midi_input_port(const nmo_engine_t *engine)
{
    return engine != NULL && engine->midi.open ? engine->midi.current_port : -1;
}

void eng_midi_add_block_ref(nmo_engine_t *engine)
{
    engine->midi.block_refs++;
}

void eng_midi_set_desired_port(nmo_engine_t *engine, int32_t port)
{
    engine->midi.desired_port = port;
}

const eng_midi_message_t *eng_midi_messages(const nmo_engine_t *engine, size_t *out_count)
{
    *out_count = engine->midi.current.count;
    return engine->midi.current.data;
}

void eng_midi_pre_process(nmo_engine_t *engine)
{
    eng_midi_t *midi = &engine->midi;
    /* listFromCallBack.Swap(listForBehaviors): the behaviors' list was emptied after the last frame */
    eng_midi_message_t *data = midi->current.data;
    size_t capacity = midi->current.capacity;
    midi->current.data = midi->incoming.data;
    midi->current.count = midi->incoming.count;
    midi->current.capacity = midi->incoming.capacity;
    midi->incoming.data = data;
    midi->incoming.count = 0;
    midi->incoming.capacity = capacity;
}

void eng_midi_post_process(nmo_engine_t *engine)
{
    eng_midi_t *midi = &engine->midi;
    midi->current.count = 0;
    if (midi->open && midi->current_port != midi->desired_port) {
        midi_close(midi);
        midi_open(midi, midi->desired_port);
    }
}

void eng_midi_reset(nmo_engine_t *engine)
{
    midi_close(&engine->midi);
}

void eng_midi_play(nmo_engine_t *engine)
{
    eng_midi_t *midi = &engine->midi;
    if (midi->block_refs > 0 && !midi_open(midi, midi->desired_port) && midi->desired_port != 0) {
        midi_open(midi, 0);
    }
}

/* ---- Midi sounds ---- */

/* MidiSound::OpenFile: a standard MIDI file ("MThd") through the sound paths opens */
static bool midi_file_opens(nmo_engine_t *engine, const char *name)
{
    size_t size = 0;
    unsigned char *bytes = name != NULL && name[0] != '\0' ? nmo_engine_read_sound_file(engine, name, &size)
                                                         : NULL;
    bool ok = bytes != NULL && size >= 4 && memcmp(bytes, "MThd", 4) == 0;
    free(bytes);
    return ok;
}

eng_midi_sound_t *eng_midi_sound(nmo_engine_t *engine, nmo_object_id_t id)
{
    eng_midi_t *midi = &engine->midi;
    for (size_t i = 0; i < midi->sounds.count; i++) {
        if (midi->sounds.data[i].id == id) {
            return &midi->sounds.data[i];
        }
    }
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_MIDISOUND) {
        return NULL;
    }
    const nmo_midisound_state_t *state = (const nmo_midisound_state_t *)nmo_object_get_state(object);
    /* CKMidiSound::Load: the file the chunk names, else the sound's own */
    const char *name = state == NULL ? NULL
                       : state->has_midi_file_name && state->midi_file_name != NULL ? state->midi_file_name
                                                                                    : state->base.file_name;
    eng_midi_sound_t *sound = NULL;
    if (!ENG_VEC_PUSH(midi->sounds, sound)) {
        return NULL;
    }
    memset(sound, 0, sizeof(*sound));
    sound->id = id;
    sound->file = name != NULL ? eng_text_printf("%s", name) : NULL;
    sound->state = midi_file_opens(engine, name) ? ENG_MIDI_OPENED : ENG_MIDI_NOFILE;
    return sound;
}

void eng_midi_sound_play(eng_midi_sound_t *sound)
{
    if (sound->state == ENG_MIDI_OPENED) {
        sound->state = ENG_MIDI_PLAYING;
        sound->position = 0.0f;
        sound->starts++;
    }
}

void eng_midi_sound_stop(eng_midi_sound_t *sound)
{
    if (sound->state == ENG_MIDI_PLAYING || sound->state == ENG_MIDI_PAUSED) {
        sound->state = ENG_MIDI_OPENED;
        sound->position = 0.0f;
    }
}

void eng_midi_sound_pause(eng_midi_sound_t *sound)
{
    if (sound->state == ENG_MIDI_PLAYING) {
        sound->state = ENG_MIDI_PAUSED;
    }
}

void eng_midi_sound_restart(eng_midi_sound_t *sound)
{
    if (sound->state == ENG_MIDI_PAUSED) {
        sound->state = ENG_MIDI_PLAYING;
    } else if (sound->state == ENG_MIDI_OPENED) {
        eng_midi_sound_play(sound);
    }
}

void eng_midi_sounds_begin_frame(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->midi.sounds.count; i++) {
        eng_midi_sound_t *sound = &engine->midi.sounds.data[i];
        if (sound->state == ENG_MIDI_PLAYING) {
            sound->position += engine->delta;
        }
    }
}

const nmo_engine_midi_voice_t *nmo_engine_midi_voices(nmo_engine_t *engine, size_t *out_count)
{
    eng_midi_t *midi = &engine->midi;
    midi->voices.count = 0;
    for (size_t i = 0; i < midi->sounds.count; i++) {
        const eng_midi_sound_t *sound = &midi->sounds.data[i];
        nmo_engine_midi_voice_t *voice = NULL;
        if ((sound->state != ENG_MIDI_PLAYING && sound->state != ENG_MIDI_PAUSED) ||
            !ENG_VEC_PUSH(midi->voices, voice)) {
            continue;
        }
        *voice = (nmo_engine_midi_voice_t){sound->id, sound->file, sound->starts, sound->position,
                                           sound->state == ENG_MIDI_PAUSED};
    }
    if (out_count != NULL) {
        *out_count = midi->voices.count;
    }
    return midi->voices.data;
}
