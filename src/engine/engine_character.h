#ifndef NMO_ENGINE_CHARACTER_H
#define NMO_ENGINE_CHARACTER_H

#include "engine_base.h"

typedef struct eng_range eng_range_t;
typedef struct eng_character {
    nmo_object_id_t id, root, active, next;
    float frame_dest;
    bool automatic;
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
nmo_status_t eng_character_process(nmo_engine_t *engine, nmo_object_id_t character, float delta);
bool eng_character_automatic(nmo_engine_t *engine, nmo_object_id_t character);
void eng_copy_character(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
                          const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count, bool copy_next);

#endif
