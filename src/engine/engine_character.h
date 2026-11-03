#ifndef NMO_ENGINE_CHARACTER_H
#define NMO_ENGINE_CHARACTER_H

#include "engine_base.h"

typedef struct eng_range eng_range_t;
typedef enum eng_secondary_phase {
    ENG_SECONDARY_PLAYING, ENG_SECONDARY_ENTERING, ENG_SECONDARY_LEAVING
} eng_secondary_phase_t;
typedef struct eng_secondary_gate {
    nmo_object_id_t animation;
    ENG_VEC(nmo_object_id_t) bodies;
} eng_secondary_gate_t;
typedef struct eng_secondary_animation {
    nmo_object_id_t id, warper, track;
    uint32_t flags, loops;
    float starting_frame, warp_length;
    eng_secondary_phase_t phase;
    eng_secondary_gate_t gate, warp_gate;
} eng_secondary_animation_t;

typedef struct eng_character {
    nmo_object_id_t id, root, active, next;
    nmo_object_id_t warper, source;
    float frame_dest;
    float frame_src;
    uint32_t transition_mode;
    bool automatic;
    ENG_VEC(eng_secondary_animation_t) secondary;
    ENG_VEC(nmo_object_id_t) secondary_warpers;
} eng_character_t;

typedef struct eng_characters {
    ENG_VEC(eng_character_t) entries;
    eng_map_t map;
} eng_characters_t;

nmo_status_t eng_load_characters(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_characters(nmo_engine_t *engine);
nmo_object_id_t eng_character_active_animation(nmo_engine_t *engine, nmo_object_id_t character);
nmo_object_id_t eng_character_next_animation(nmo_engine_t *engine, nmo_object_id_t character);
nmo_status_t eng_character_set_active_animation(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation);
nmo_status_t eng_character_set_next_animation(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation, uint32_t mode);
nmo_status_t eng_character_set_next_animation_warp(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation, uint32_t mode, float length);
nmo_object_id_t eng_character_warper(nmo_engine_t *engine, nmo_object_id_t character);
bool eng_character_warper_parameters(nmo_engine_t *engine, nmo_object_id_t character,
    uint32_t *mode, nmo_object_id_t *source, float *from, nmo_object_id_t *target, float *to);
nmo_status_t eng_character_process(nmo_engine_t *engine, nmo_object_id_t character, float delta);
bool eng_character_automatic(nmo_engine_t *engine, nmo_object_id_t character);
nmo_status_t eng_character_play_secondary(nmo_engine_t *engine, nmo_object_id_t character,
                                          nmo_object_id_t animation, float frame, uint32_t flags, uint32_t loops);
nmo_status_t eng_character_play_secondary_warp(nmo_engine_t *engine, nmo_object_id_t character,
    nmo_object_id_t animation, float frame, uint32_t flags, float length, uint32_t loops);
nmo_status_t eng_character_stop_secondary(nmo_engine_t *engine, nmo_object_id_t character, nmo_object_id_t animation);
void eng_character_flush_secondary(nmo_engine_t *engine, nmo_object_id_t character);
size_t eng_character_secondary_count(nmo_engine_t *engine, nmo_object_id_t character);
nmo_object_id_t eng_character_secondary_animation(nmo_engine_t *engine, nmo_object_id_t character, size_t index);
nmo_object_id_t eng_character_secondary_warper(nmo_engine_t *engine, nmo_object_id_t character, size_t index);
void eng_copy_character(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
                          const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count, bool copy_next);

#endif
