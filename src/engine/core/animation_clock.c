/* CKAnimation's clock is runtime state; SetCurrentStep does not apply a pose. */
#include "../engine_internal.h"
#include "object/nmo_class_ids.h"

#include <math.h>

const nmo_animation_state_t *eng_animation_state(const nmo_engine_t *engine, nmo_object_id_t animation)
{
    nmo_object_t *object = eng_find_object(engine, animation);
    uint32_t type = object != NULL ? nmo_object_get_class_id(object) : 0;
    return type == NMO_CID_ANIMATION || type == NMO_CID_KEYEDANIMATION ? nmo_object_get_state(object) : NULL;
}

float eng_animation_length(const nmo_animation_state_t *state)
{
    return state->has_length ? state->length : 100;
}

static eng_animation_clock_t *animation_clock(nmo_engine_t *engine, nmo_object_id_t animation)
{
    const nmo_animation_state_t *state = eng_animation_state(engine, animation);
    if (state == NULL) return NULL;
    int32_t index = eng_map_get(&engine->animations.clock_map, animation);
    if (index >= 0) return &engine->animations.clocks.data[index];
    eng_animation_clock_t *clock = NULL;
    index = (int32_t)engine->animations.clocks.count;
    if (!ENG_VEC_PUSH(engine->animations.clocks, clock)) return NULL;
    clock->id = animation;
    clock->step = state->has_current_step ? state->current_step : 0;
    if (!eng_map_put(&engine->animations.clock_map, animation, index)) return NULL;
    return clock;
}

bool eng_animation_current_step(nmo_engine_t *engine, nmo_object_id_t animation, float *out)
{
    eng_animation_clock_t *clock = animation_clock(engine, animation);
    if (clock == NULL) return false;
    *out = clock->step;
    return true;
}

bool eng_animation_set_current_step(nmo_engine_t *engine, nmo_object_id_t animation, float step)
{
    if (!isfinite(step)) return false;
    eng_animation_clock_t *clock = animation_clock(engine, animation);
    if (clock == NULL) return false;
    clock->step = step;
    return true;
}

bool eng_animation_frame(nmo_engine_t *engine, nmo_object_id_t animation, float *out)
{
    float step;
    const nmo_animation_state_t *state = eng_animation_state(engine, animation);
    if (state == NULL || !eng_animation_current_step(engine, animation, &step)) return false;
    *out = step * eng_animation_length(state);
    return true;
}

bool eng_animation_set_frame(nmo_engine_t *engine, nmo_object_id_t animation, float frame)
{
    const nmo_animation_state_t *state = eng_animation_state(engine, animation);
    if (state == NULL || !isfinite(frame)) return false;
    float length = eng_animation_length(state);
    if (!isfinite(length)) return false;
    return eng_animation_set_step(engine, animation, length != 0 ? frame / length : 0);
}

bool eng_animation_next_frame(nmo_engine_t *engine, nmo_object_id_t animation, float delta, float *out)
{
    const nmo_animation_state_t *state = eng_animation_state(engine, animation);
    float current, step;
    if (state == NULL || !isfinite(delta) || delta < 0 || !eng_animation_frame(engine, animation, &current)) return false;
    if (state->flags & CKANIMATION_LINKTOFRAMERATE) {
        if (!isfinite(state->frame_rate) || !eng_animation_current_step(engine, animation, &step)) return false;
        double frame = isfinite(current) ? current : (double)step * eng_animation_length(state);
        *out = (float)(frame + (double)delta * state->frame_rate * 0.001);
    } else *out = current + 1;
    return isfinite(*out);
}
