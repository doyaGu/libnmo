/* Object animations keep their flags and target while sharing keyframe data. */
#ifndef NMO_ENGINE_ANIMATION_H
#define NMO_ENGINE_ANIMATION_H

#include "engine_base.h"
#include "object/builtin/nmo_animation_schemas.h"

typedef struct eng_animation_source {
    nmo_object_id_t id;
    const nmo_objectanimation_state_t *state;
    const nmo_objectanimation_state_t *keys; /* NULL for absent/cyclic shared data */
    float length;
} eng_animation_source_t;

bool eng_animation_source(const nmo_engine_t *engine, nmo_object_id_t animation,
                            eng_animation_source_t *out);
void eng_animation_morph(nmo_engine_t *engine, nmo_object_id_t animation,
                          nmo_object_id_t entity, float frame);

#endif
