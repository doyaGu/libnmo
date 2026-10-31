/**
 * @file sounds.c
 * @brief Ports of the Sounds building blocks that play sounds in time.
 *
 * Each function does what the block's function in Sounds.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Wave Player is not an effect: it stays active and activates its
 * outputs when the sound ends; starting a sound reports an effect event.
 */

#include "../engine_block_util.h"

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
        eng_sound_play(b, sound, loop);
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
        eng_sound_play(b, sound, get_in_int(b, 3, 0) != 0);
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

static const nmo_engine_block_desc_t eng_sounds_blocks[] = {
    {NMO_GUID(0x5bde0e45, 0x2e2107d5), "Wave Player", bb_wave_player, cb_wave_player, NULL},
};

nmo_status_t eng_register_sounds(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_sounds_blocks);
}
