/* Object animations keep their flags and target while sharing keyframe data. */
#ifndef NMO_ENGINE_ANIMATION_H
#define NMO_ENGINE_ANIMATION_H

#include "engine_base.h"
#include "object/builtin/nmo_animation_schemas.h"

typedef struct eng_range eng_range_t;

typedef struct eng_animation_member { nmo_object_id_t id, entity; } eng_animation_member_t;
typedef struct eng_animation_channel { nmo_object_id_t id; float contribution; } eng_animation_channel_t;

typedef struct eng_keyed_animation {
    nmo_object_id_t id, root_entity, root_animation;
    nmo_vector_t offset;
    bool ready;
    bool sorted;
    nmo_object_id_t sort_character, sort_root, sort_floor;
    ENG_VEC(eng_animation_member_t) members;
    ENG_VEC(eng_animation_channel_t) order;
} eng_keyed_animation_t;

typedef struct eng_animations {
    ENG_VEC(eng_keyed_animation_t) keyed;
    eng_map_t keyed_map;
    eng_map_t parents;   /* ObjectAnimation ID -> parent keyed index */
    eng_map_t consumed;  /* Saved root vectors transferred from these objects */
} eng_animations_t;

nmo_status_t eng_load_animations(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_animations(nmo_engine_t *engine);
void eng_copy_animation(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
                          const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count);
bool eng_keyed_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step);
/* Sample raw position keys at an absolute frame, without parent centering. */
bool eng_animation_position(nmo_engine_t *engine, nmo_object_id_t animation, float frame, nmo_vector_t *out);
bool eng_keyed_animation_center(nmo_engine_t *engine, nmo_object_id_t animation, float frame);
bool eng_animation_root_offset(const nmo_engine_t *engine, nmo_object_id_t animation, nmo_vector_t *out);
float eng_animation_contribution(nmo_engine_t *engine, nmo_object_id_t animation);
float eng_character_animation_lod(const nmo_engine_t *engine, nmo_object_id_t character);
bool eng_character_set_animation_lod(nmo_engine_t *engine, nmo_object_id_t character, float lod);

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
