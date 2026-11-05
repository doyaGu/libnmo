#ifndef NMO_ENGINE_PORTAL_H
#define NMO_ENGINE_PORTAL_H

#include "engine_base.h"
#include "core/nmo_math.h"

struct eng_matrix;

typedef struct eng_portal_link {
    nmo_object_id_t place, portal; /* engine IDs; portal 0 means unrestricted visibility */
} eng_portal_link_t;

typedef struct eng_portal_list {
    eng_portal_link_t *data;
    size_t count, capacity;
} eng_portal_list_t;

typedef struct eng_place_portals {
    nmo_object_id_t id;
    eng_portal_list_t links;
    nmo_rect_t viewport;
} eng_place_portals_t;

typedef struct eng_portals {
    ENG_VEC(eng_place_portals_t) places;
    eng_map_t map;
} eng_portals_t;

/* The view lasts until another place's list is first read or changed. */
const eng_portal_list_t *eng_place_portals(nmo_engine_t *engine, nmo_object_id_t place);
bool eng_place_add_portal(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to, nmo_object_id_t portal);
void eng_copy_place_portals(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count, bool include);
void eng_portals_forget_object(nmo_engine_t *engine, nmo_object_id_t id);
void eng_free_portals(nmo_engine_t *engine);
bool eng_place_best_fit(nmo_engine_t *engine, nmo_object_id_t first, nmo_object_id_t second, struct eng_matrix *out);
nmo_rect_t eng_place_viewport_clip(const nmo_engine_t *engine, nmo_object_id_t place);
void eng_place_set_viewport_clip(nmo_engine_t *engine, nmo_object_id_t place, nmo_rect_t rect);

#endif /* NMO_ENGINE_PORTAL_H */
