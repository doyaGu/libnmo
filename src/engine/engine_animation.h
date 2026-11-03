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

/* Bound cyclic/deep input graphs and repeated work in a merge DAG. */
#define ENG_ANIMATION_WORK_LIMIT 4096u
typedef struct eng_animation_path {
    nmo_object_id_t id;
    uint32_t depth;
    const struct eng_animation_path *parent;
} eng_animation_path_t;

bool eng_animation_visit(const eng_animation_path_t *path, nmo_object_id_t id, uint32_t *work);
bool eng_animation_map_time(const eng_animation_source_t *parent,
                              const eng_animation_source_t *child, float frame, float *out);

bool eng_animation_source(const nmo_engine_t *engine, nmo_object_id_t animation,
                            eng_animation_source_t *out);
void eng_animation_morph(nmo_engine_t *engine, nmo_object_id_t animation,
                          nmo_object_id_t entity, float frame);

#endif
