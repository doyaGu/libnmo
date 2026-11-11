/**
 * @file midi.c
 * @brief MidiManager.dll's blocks: Read Midi, Midi Event, Switch On Midi
 *        and Set Midi Input Port on the Midi Manager's input port, and Midi
 *        Player on CKMidiSounds. The manager itself is the engine's
 *        (engine_midi.h); a block that uses the port counts as one at load.
 */

#include "virtools.h"

#include <stdbool.h>
#include <stdint.h>

/* MidiManager::AddMidiBBref at CKM_BEHAVIORLOAD (the blocks that read the input port) */
static int cb_midi_input(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        eng_midi_add_block_ref(b->engine);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Read Midi: the first message this frame on Channel. Without Ask For All
 * Commands (local 0), a Note On with a velocity gives Note and Attack and
 * activates Activated; another Note On or a Note Off activates Deactivated.
 * With it, any message gives Note, Attack and Command through the one
 * output. Active until Off.
 */
static int bb_read_midi(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    }
    const int32_t channel = get_in_int(b, 0, 0);
    const bool all = get_local_int(b, 0, 0) != 0;
    size_t count = 0;
    const eng_midi_message_t *messages = eng_midi_messages(b->engine, &count);
    for (size_t i = 0; i < count; i++) {
        const eng_midi_message_t *m = &messages[i];
        if (m->channel != channel) {
            continue;
        }
        if (all) {
            set_out_int(b, 0, m->note);
            set_out_int(b, 1, m->attack);
            set_out_int(b, 2, m->command);
            OUT_ON(b, 0);
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
        if (m->command == 9 && m->attack != 0) {
            set_out_int(b, 0, m->note);
            set_out_int(b, 1, m->attack);
            OUT_ON(b, 0);
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
        if (m->command == 9 || m->command == 8) {
            set_out_int(b, 0, m->note);
            set_out_int(b, 1, m->attack);
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * Midi Event: Activated once every note (the inputs after Channel) is held
 * on Channel, Deactivated once one is let go; local 0 says whether they all
 * were (On clears it). Active until Off.
 */
static int bb_midi_event(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    bool held = false;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_local_int(b, 0, 0);
    } else {
        held = get_local_int(b, 0, 0) != 0;
    }
    const int32_t channel = get_in_int(b, 0, 0);
    const int count = nmo_engine_block_input_parameter_count(b);
    int a = 1;
    for (; a < count; a++) {
        if (!eng_midi_note_active(b->engine, get_in_int(b, a, 0), channel)) {
            break;
        }
    }
    if (a == count) {
        if (!held) {
            OUT_ON(b, 0);
            set_local_int(b, 0, 1);
        }
    } else if (held) {
        OUT_ON(b, 1);
        set_local_int(b, 0, 0);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * Switch On Midi: output a - 1 for each held note of input a (after
 * Channel); a Note On or Off of that note this frame sets output parameter
 * a (Attack a) to its velocity. Active until Off.
 */
static int bb_switch_on_midi(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
    }
    const int32_t channel = get_in_int(b, 0, 0);
    size_t message_count = 0;
    const eng_midi_message_t *messages = eng_midi_messages(b->engine, &message_count);
    const int count = nmo_engine_block_input_parameter_count(b);
    for (int a = 1; a < count; a++) {
        const int32_t note = get_in_int(b, a, 0);
        if (eng_midi_note_active(b->engine, note, channel)) {
            OUT_ON(b, a - 1);
        }
        for (size_t i = 0; i < message_count; i++) {
            const eng_midi_message_t *m = &messages[i];
            if (m->channel == channel && (m->command == 9 || m->command == 8) && note == m->note) {
                set_out_int(b, a, m->attack);
            }
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Set Midi Input Port: the port the manager opens in place of the open one at the frame's end */
static int bb_set_midi_input_port(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_midi_set_desired_port(b->engine, get_in_int(b, 0, 0));
    return NMO_ENGINE_BR_OK;
}

/* The block's midi sound: its first input parameter in old files, else its target */
static eng_midi_sound_t *player_sound(blk_t *b)
{
    nmo_object_id_t id = nmo_engine_block_input_parameter_count(b) > 0
                             ? nmo_engine_block_get_input_object(b, 0)
                             : nmo_engine_block_target(b);
    return id != 0 ? eng_midi_sound(b->engine, id) : NULL;
}

/*
 * Midi Player: Play starts the sound from its start (not while it plays or
 * is paused), Stop stops it, Pause/Resume pauses it while it plays and
 * otherwise resumes it (or starts it); each input's output follows. Without
 * a sound, the inputs stay active.
 */
static int bb_midi_player(blk_t *b)
{
    for (int i = 0; i < 3; i++) {
        if (!IN_ACTIVE(b, i)) {
            continue;
        }
        eng_midi_sound_t *sound = player_sound(b);
        if (sound == NULL) {
            continue;
        }
        if (i == 0) {
            eng_midi_sound_play(sound);
        } else if (i == 1) {
            eng_midi_sound_stop(sound);
        } else if (sound->state == ENG_MIDI_PLAYING) {
            eng_midi_sound_pause(sound);
        } else {
            eng_midi_sound_restart(sound);
        }
        IN_OFF(b, i);
        OUT_ON(b, i);
    }
    return NMO_ENGINE_BR_OK;
}

/* MidiPlayerCB: a reset stops the sound, a pause pauses it, a resume resumes it when paused */
static int cb_midi_player(blk_t *b, uint32_t message)
{
    eng_midi_sound_t *sound = player_sound(b);
    if (sound == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (message == NMO_ENGINE_MSG_RESET) {
        eng_midi_sound_stop(sound);
    } else if (message == NMO_ENGINE_MSG_PAUSE) {
        eng_midi_sound_pause(sound);
    } else if (message == NMO_ENGINE_MSG_RESUME && sound->state == ENG_MIDI_PAUSED) {
        eng_midi_sound_restart(sound);
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_midi_blocks[] = {
    {NMO_GUID_INIT(0x408c50ed, 0x68ba5988), "Read Midi", bb_read_midi, cb_midi_input, NULL},
    {NMO_GUID_INIT(0x7c652f90, 0x64404377), "Midi Event", bb_midi_event, cb_midi_input, NULL},
    {NMO_GUID_INIT(0x624b1bec, 0x509400a8), "Switch On Midi", bb_switch_on_midi, cb_midi_input, NULL},
    {NMO_GUID_INIT(0x42d96c11, 0x64242546), "Set Midi Input Port", bb_set_midi_input_port,
     cb_midi_input, NULL},
    {NMO_GUID_INIT(0x843cb43a, 0xa12dac48), "Midi Player", bb_midi_player, cb_midi_player, NULL},
};

nmo_status_t eng_register_midi(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_midi_blocks);
}
