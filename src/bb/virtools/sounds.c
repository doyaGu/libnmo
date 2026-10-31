/**
 * @file sounds.c
 * @brief Ports of the Sounds building blocks that play sounds in time.
 *
 * Each function does what the block's function in Sounds.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Wave Player is not an effect: it stays active and activates its
 * outputs when the sound ends; starting a sound reports an effect event.
 */

#include "virtools.h"

#include "type/nmo_object_guids.h"

/* Wave Player (WavePlay): Play, Stop, Pause/Resume on its target sound. */
static int bb_wave_player(blk_t *b)
{
    eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_target(b));
    if (sound == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        bool loop = get_in_int(b, 2, 0) != 0;
        eng_sound_play(b, sound, loop, get_in_float(b, 0, 0.0f));
        OUT_ON(b, 0);
        return loop ? NMO_ENGINE_BR_OK : NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (nmo_engine_block_input_count(b) > 2 && IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 2);
        if (sound->playing && !sound->paused) {
            sound->paused = true;
            return NMO_ENGINE_BR_OK;
        }
        if (sound->paused) {
            sound->paused = false;
            return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
        }
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        float fade_out = get_in_float(b, 1, 0.0f);
        eng_sound_stop(sound, fade_out);
        if (fade_out == 0.0f) {
            OUT_ON(b, 1);
            return NMO_ENGINE_BR_OK;
        }
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (!(sound->playing && !sound->paused)) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* OldWavePlay: the sound is the first input; out right away. */
static int bb_wave_player_old(blk_t *b)
{
    eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_get_input_object(b, 0));
    if (sound == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        eng_sound_play(b, sound, get_in_int(b, 3, 0) != 0, get_in_float(b, 1, 0.0f));
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        eng_sound_stop(sound, get_in_float(b, 2, 0.0f));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int cb_wave_player(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD &&
        nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_WAVESOUND)) {
        nmo_engine_block_set_function(b, bb_wave_player_old);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Volume Control, Pitch Control and Panning Control: CKWaveSound::SetGain,
 * SetPitch and SetPan on the target sound, playing or not; their old
 * versions take the sound as the first input. No sound: out all the same.
 */
enum { SOUND_GAIN, SOUND_PITCH, SOUND_PAN };

static int sound_control(blk_t *b, bool old, int which)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_sound_t *sound = eng_sound(b->engine, old ? nmo_engine_block_get_input_object(b, 0)
                                                  : nmo_engine_block_target(b));
    if (sound == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int pin = old ? 1 : 0;
    switch (which) {
    case SOUND_GAIN: sound->gain = get_in_float(b, pin, 1.0f); break;
    case SOUND_PITCH: sound->pitch = get_in_float(b, pin, sound->pitch); break;
    default: sound->pan = get_in_float(b, pin, 0.0f); break;
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, sound->id, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_volume_control(blk_t *b)
{
    return sound_control(b, false, SOUND_GAIN);
}

static int bb_volume_control_old(blk_t *b)
{
    return sound_control(b, true, SOUND_GAIN);
}

static int bb_pitch_control(blk_t *b)
{
    return sound_control(b, false, SOUND_PITCH);
}

static int bb_pitch_control_old(blk_t *b)
{
    return sound_control(b, true, SOUND_PITCH);
}

static int bb_panning_control(blk_t *b)
{
    return sound_control(b, false, SOUND_PAN);
}

static int bb_panning_control_old(blk_t *b)
{
    return sound_control(b, true, SOUND_PAN);
}

/* The callbacks: a version that takes the sound as its first input runs the old function */
static bool sound_input_first(blk_t *b, uint32_t message)
{
    return message == NMO_ENGINE_MSG_LOAD &&
           nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_WAVESOUND);
}

static int cb_volume_control(blk_t *b, uint32_t message)
{
    if (sound_input_first(b, message)) {
        nmo_engine_block_set_function(b, bb_volume_control_old);
    }
    return NMO_ENGINE_BR_OK;
}

static int cb_pitch_control(blk_t *b, uint32_t message)
{
    if (sound_input_first(b, message)) {
        nmo_engine_block_set_function(b, bb_pitch_control_old);
    }
    return NMO_ENGINE_BR_OK;
}

static int cb_panning_control(blk_t *b, uint32_t message)
{
    if (sound_input_first(b, message)) {
        nmo_engine_block_set_function(b, bb_panning_control_old);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Play Sound Instance: an instance of the target sound (PlayMinion) at the
 * Volume input, unless one started less than Min Delay ago. The engine
 * places no sound in 3D: 2D, Object, Position and Direction change nothing.
 */
static int bb_play_sound_instance(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_target(b));
    if (sound == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    eng_sound_play_instance(b->engine, sound, get_in_float(b, 4, 0.0f), get_in_float(b, 5, 1.0f));
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, sound->id, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_sounds_blocks[] = {
    {NMO_GUID(0x5bde0e45, 0x2e2107d5), "Wave Player", bb_wave_player, cb_wave_player, NULL},
    {NMO_GUID(0x33613f21, 0x776b185b), "Volume Control", bb_volume_control, cb_volume_control,
     NULL},
    {NMO_GUID(0x7b7c731e, 0x175b27a3), "Pitch Control", bb_pitch_control, cb_pitch_control, NULL},
    {NMO_GUID(0x6a5f17af, 0x63480864), "Panning Control", bb_panning_control, cb_panning_control,
     NULL},
    {NMO_GUID(0x00283a35, 0x38ef6b48), "Play Sound Instance", bb_play_sound_instance, NULL, NULL},
};

nmo_status_t eng_register_sounds(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_sounds_blocks);
}
