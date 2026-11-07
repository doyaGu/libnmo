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
 * Volume input, unless one started less than Min Delay ago. The instance
 * is placed as its sound is: its own 2D, Object, Position and Direction
 * are not kept.
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

/* The target sound of the 3D setters, their out first */
static eng_sound_t *target_sound(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return eng_sound(b->engine, nmo_engine_block_target(b));
}

static int sound_changed(blk_t *b, const eng_sound_t *sound)
{
    if (sound != NULL) {
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, sound->id, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* Position Sound: CKWaveSound::PositionSound, the sound at Position and along Direction of Object */
static int bb_position_sound(blk_t *b)
{
    eng_sound_t *sound = target_sound(b);
    if (sound != NULL) {
        sound->attached = nmo_engine_block_get_input_object(b, 0);
        eng_vector_t position = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
        eng_vector_t direction = get_in_vector(b, 2, 0.0f, 0.0f, 1.0f);
        sound->place[0] = position.x, sound->place[1] = position.y, sound->place[2] = position.z;
        sound->heading[0] = direction.x, sound->heading[1] = direction.y, sound->heading[2] = direction.z;
    }
    return sound_changed(b, sound);
}

/*
 * Set Sound Cone Values: CKWaveSound::SetCone with the angles (radians made
 * degrees) and the outside volume; an input without a value keeps the
 * cone's own (degrees, which the block converts again).
 */
static int bb_set_sound_cone_values(blk_t *b)
{
    eng_sound_t *sound = target_sound(b);
    if (sound != NULL) {
        float inside = get_in_float(b, 0, sound->cone[0]);
        float outside = get_in_float(b, 1, sound->cone[1]);
        sound->cone[2] = get_in_float(b, 2, sound->cone[2]);
        sound->cone[0] = (float)((double)inside * 57.295776);
        sound->cone[1] = (float)((double)outside * 57.295776);
    }
    return sound_changed(b, sound);
}

/* Set Sound Range: CKWaveSound::SetMinMaxDistance, muted beyond the maximum */
static int bb_set_sound_range(blk_t *b)
{
    eng_sound_t *sound = target_sound(b);
    if (sound != NULL) {
        sound->min_distance = get_in_float(b, 0, 1.0f);
        sound->max_distance = get_in_float(b, 1, 100.0f);
        sound->mute_after_max = true;
    }
    return sound_changed(b, sound);
}

/* Set Priority (obsolete): CKWaveSound::SetPriority */
static int bb_set_priority(blk_t *b)
{
    eng_sound_t *sound = target_sound(b);
    if (sound != NULL) {
        sound->priority = get_in_float(b, 0, sound->priority);
    }
    return sound_changed(b, sound);
}

/* Set Sound Type: CKWaveSound::SetType (background, point or cone) */
static int bb_set_sound_type(blk_t *b)
{
    eng_sound_t *sound = target_sound(b);
    if (sound != NULL) {
        sound->type = (uint32_t)get_in_int(b, 0, (int32_t)sound->type) & CK_WAVESOUND_ALLTYPE;
    }
    return sound_changed(b, sound);
}

/*
 * Fade In and Fade Out: the sound manager's global gain from 0 to 1 (or 1
 * to 0) over Fade Delay ms, Elapsed Time the time so far; Out at the end.
 */
static int fade_global_gain(blk_t *b, bool in)
{
    eng_sound_manager_t *manager = eng_sound_manager(b->engine);
    float delay = (float)get_in_int(b, 0, 1000);
    float elapsed = 0.0f;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_out_float(b, 0, elapsed);
        manager->global_gain = in ? 0.0f : 1.0f;
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    elapsed = get_out_float(b, 0) + nmo_engine_block_delta_time(b);
    if (!(elapsed < delay)) {
        elapsed = delay;
    }
    set_out_float(b, 0, elapsed);
    manager->global_gain = in ? elapsed / delay : 1.0f - elapsed / delay;
    if (elapsed != delay) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_fade_in(blk_t *b) { return fade_global_gain(b, true); }

static int bb_fade_out(blk_t *b) { return fade_global_gain(b, false); }

/* Set Listener: the sound manager hears from the entity (the camera without one) */
static int bb_set_listener(blk_t *b)
{
    eng_sound_manager(b->engine)->listener = nmo_engine_block_get_input_object(b, 0);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * Sound Manager Setup: the distance factor, the global gain and the Doppler
 * factor, each when its input has a value (the DLL reads neither Rolloff
 * Factor nor Global Tone).
 */
static int bb_sound_manager_setup(blk_t *b)
{
    eng_sound_manager_t *manager = eng_sound_manager(b->engine);
    float value = 1.0f;
    if (nmo_engine_block_get_input_value(b, 0, &value, sizeof(value)) == NMO_OK) {
        manager->distance_factor = value;
    }
    if (nmo_engine_block_get_input_value(b, 3, &value, sizeof(value)) == NMO_OK) {
        manager->global_gain = value;
    }
    if (nmo_engine_block_get_input_value(b, 4, &value, sizeof(value)) == NMO_OK) {
        manager->doppler_factor = value;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_sounds_blocks[] = {
    {NMO_GUID_INIT(0x5bde0e45, 0x2e2107d5), "Wave Player", bb_wave_player, cb_wave_player, NULL},
    {NMO_GUID_INIT(0x33613f21, 0x776b185b), "Volume Control", bb_volume_control, cb_volume_control,
     NULL},
    {NMO_GUID_INIT(0x7b7c731e, 0x175b27a3), "Pitch Control", bb_pitch_control, cb_pitch_control, NULL},
    {NMO_GUID_INIT(0x6a5f17af, 0x63480864), "Panning Control", bb_panning_control, cb_panning_control,
     NULL},
    {NMO_GUID_INIT(0x00283a35, 0x38ef6b48), "Play Sound Instance", bb_play_sound_instance, NULL, NULL},
    {NMO_GUID_INIT(0x4ba42a5b, 0x3ca030dd), "Position Sound", bb_position_sound, NULL, NULL},
    {NMO_GUID_INIT(0xabf60a8c, 0x0ff91d0d), "Set Sound Cone Values", bb_set_sound_cone_values, NULL, NULL},
    {NMO_GUID_INIT(0x27f50a7c, 0x0ee91d0e), "Set Sound Range", bb_set_sound_range, NULL, NULL},
    {NMO_GUID_INIT(0x159630e5, 0x5a5a26c6), "Set Priority", bb_set_priority, NULL, NULL},
    {NMO_GUID_INIT(0x7734470d, 0x23b221f8), "Set Sound Type", bb_set_sound_type, NULL, NULL},
    {NMO_GUID_INIT(0x742215ea, 0x08a06c46), "Fade In", bb_fade_in, NULL, NULL},
    {NMO_GUID_INIT(0x1b82338f, 0x5a656975), "Fade Out", bb_fade_out, NULL, NULL},
    {NMO_GUID_INIT(0x28cd6341, 0x0cad5793), "Set Listener", bb_set_listener, NULL, NULL},
    {NMO_GUID_INIT(0x04c05c6a, 0x6454792b), "Sound Manager Setup", bb_sound_manager_setup, NULL, NULL},
};

nmo_status_t eng_register_sounds(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_sounds_blocks);
}
