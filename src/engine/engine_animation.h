/* Object animations keep their flags and target while sharing keyframe data. */
#ifndef NMO_ENGINE_ANIMATION_H
#define NMO_ENGINE_ANIMATION_H

#include "engine_base.h"
#include "object/builtin/nmo_animation_schemas.h"

typedef struct eng_range eng_range_t;
struct eng_animation_channel;

enum { ENG_ANIM_POSITION=1, ENG_ANIM_ROTATION=2, ENG_ANIM_SCALE=4, ENG_ANIM_AXIS=8 };
typedef struct eng_animation_pose {
    uint32_t present;
    nmo_vector_t position, scale;
    nmo_vector4_t rotation, axis;
} eng_animation_pose_t;

/* Bound sampled velocity keys and best-frame search work. */
#define ENG_TRANSITION_MAX_SAMPLES 65536u
eng_animation_pose_t eng_animation_sample_pose(nmo_engine_t *engine, nmo_object_id_t animation, float frame);
nmo_status_t eng_object_animation_transition(nmo_engine_t *engine, nmo_object_id_t result,
    nmo_object_id_t from, float from_step, nmo_object_id_t to, float to_step, float length,
    bool velocity, bool dont_turn, const eng_animation_pose_t *starting);
nmo_status_t eng_object_animation_capture_transition(nmo_engine_t *engine, nmo_object_id_t result,
    nmo_object_id_t from, float from_step, nmo_object_id_t to, float to_step, float length,
    bool velocity, bool dont_turn, const eng_animation_pose_t *starting, nmo_objectanimation_state_t *out);
nmo_status_t eng_keyed_animation_transition(nmo_engine_t *engine, nmo_object_id_t result,
    nmo_object_id_t from, nmo_object_id_t to, uint32_t mode, float length, float frame_to, float *entry);
bool eng_keyed_animation_channels(nmo_engine_t *engine, nmo_object_id_t animation,
    const struct eng_animation_channel **out, size_t *count);
void eng_keyed_animation_refresh(nmo_engine_t *engine, nmo_object_id_t animation);
struct nmo_arena;
struct nmo_arena *eng_animation_edit_arena(nmo_engine_t *engine, nmo_object_id_t animation);
bool eng_animation_local_ref(const nmo_engine_t *engine, nmo_object_id_t owner, nmo_object_id_t target, nmo_ref_t *out);
nmo_status_t eng_animation_transition_morph(nmo_engine_t *engine, nmo_object_id_t from, float from_frame,
    nmo_object_id_t to, float to_frame, struct nmo_arena *arena,
    const nmo_objectanimation_state_t *previous, nmo_objectanimation_state_t *result);

typedef struct eng_animation_member { nmo_object_id_t id, entity; } eng_animation_member_t;
typedef struct eng_animation_channel { nmo_object_id_t id; float contribution; } eng_animation_channel_t;
typedef struct eng_animation_clock { nmo_object_id_t id; float step; } eng_animation_clock_t;

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
    ENG_VEC(eng_animation_clock_t) clocks;
    eng_map_t clock_map;
} eng_animations_t;

const nmo_animation_state_t *eng_animation_state(const nmo_engine_t *engine, nmo_object_id_t animation);
bool eng_animation_current_step(nmo_engine_t *engine, nmo_object_id_t animation, float *out);
bool eng_animation_set_current_step(nmo_engine_t *engine, nmo_object_id_t animation, float step);
bool eng_animation_frame(nmo_engine_t *engine, nmo_object_id_t animation, float *out);
bool eng_animation_set_frame(nmo_engine_t *engine, nmo_object_id_t animation, float frame);
bool eng_animation_next_frame(nmo_engine_t *engine, nmo_object_id_t animation, float delta, float *out);
float eng_animation_length(const nmo_animation_state_t *state);

nmo_status_t eng_load_animations(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_animations(nmo_engine_t *engine);
void eng_copy_animation(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
                          const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count);
bool eng_keyed_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step);
/* Sample raw position keys at an absolute frame, without parent centering. */
bool eng_animation_position(nmo_engine_t *engine, nmo_object_id_t animation, float frame, nmo_vector_t *out);
bool eng_keyed_animation_center(nmo_engine_t *engine, nmo_object_id_t animation, float frame);
bool eng_animation_root_offset(const nmo_engine_t *engine, nmo_object_id_t animation, nmo_vector_t *out);
nmo_object_id_t eng_keyed_animation_root(nmo_engine_t *engine, nmo_object_id_t animation);
nmo_object_id_t eng_keyed_animation_part(nmo_engine_t *engine, nmo_object_id_t animation, nmo_object_id_t entity);
struct eng_matrix;
void eng_animation_yaw(nmo_engine_t *engine, nmo_object_id_t animation, float frame, struct eng_matrix *out);
float eng_animation_contribution(nmo_engine_t *engine, nmo_object_id_t animation);
float eng_character_animation_lod(const nmo_engine_t *engine, nmo_object_id_t character);
bool eng_character_set_animation_lod(nmo_engine_t *engine, nmo_object_id_t character, float lod);
/* Consume the previous render traversal's character visibility after processing. */
void eng_animations_end_frame(nmo_engine_t *engine);

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
