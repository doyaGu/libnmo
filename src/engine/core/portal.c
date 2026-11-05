#include "../engine_internal.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

static const nmo_place_state_t *place_state(const nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = eng_entity_index(engine,id) >= 0 ? eng_find_object(engine,id) : NULL;
    return object != NULL ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types,object,CKPGUID_PLACE) : NULL;
}

const eng_portal_list_t *eng_place_portals(nmo_engine_t *engine, nmo_object_id_t place)
{
    const nmo_place_state_t *state = place_state(engine,place);
    if (state == NULL) return NULL;
    int32_t index = eng_map_get(&engine->portals.map,place);
    if (index >= 0) return &engine->portals.places.data[index].links;
    eng_portal_list_t links = {0};
    if (!eng_vec_reserve((void **)&links.data,&links.capacity,state->portals.count,sizeof(*links.data))) return NULL;
    const eng_module_t *module = eng_module_of(engine,place,NULL);
    const nmo_place_portal_entry_t *saved = state->portals.data;
    for (size_t i = 0; i < state->portals.count; i++) {
        eng_portal_link_t link = {eng_module_id(module,nmo_ref_runtime_id(&saved[i].place)),eng_module_id(module,nmo_ref_runtime_id(&saved[i].portal))};
        if (place_state(engine,link.place) != NULL && (link.portal == 0 || eng_entity_index(engine,link.portal) >= 0)) links.data[links.count++] = link;
    }
    eng_place_portals_t *entry = NULL; index = (int32_t)engine->portals.places.count;
    if (!ENG_VEC_PUSH(engine->portals.places,entry)) { free(links.data); return NULL; }
    if (!eng_map_put(&engine->portals.map,place,index)) { engine->portals.places.count--; free(links.data); return NULL; }
    *entry = (eng_place_portals_t){place,links}; return &entry->links;
}

bool eng_place_add_portal(nmo_engine_t *engine, nmo_object_id_t from, nmo_object_id_t to, nmo_object_id_t portal)
{
    if (from == to || (portal != 0 && (eng_entity_index(engine,portal) < 0 || (engine->entities.data[eng_entity_index(engine,portal)].entity_flags & CK_3DENTITY_PORTAL) == 0))) return false;
    if (eng_place_portals(engine,from) == NULL || eng_place_portals(engine,to) == NULL) return false;
    eng_portal_list_t *a = &engine->portals.places.data[eng_map_get(&engine->portals.map,from)].links, *b = &engine->portals.places.data[eng_map_get(&engine->portals.map,to)].links;
    for (size_t i = 0; i < a->count; i++) if (a->data[i].place == to && (a->data[i].portal == 0 || a->data[i].portal == portal)) return true;
    if (!eng_vec_reserve((void **)&a->data,&a->capacity,a->count+1,sizeof(*a->data)) || !eng_vec_reserve((void **)&b->data,&b->capacity,b->count+1,sizeof(*b->data))) return false;
    a->data[a->count++] = (eng_portal_link_t){to,portal};
    b->data[b->count++] = (eng_portal_link_t){from,portal}; /* Native code appends even to an asymmetric loaded list. */
    return true;
}

void eng_copy_place_portals(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count, bool include)
{
    const eng_portal_list_t *links = eng_place_portals(engine,source);
    if (links == NULL) return;
    eng_portal_list_t saved = {0};
    if (include) {
        if (!eng_vec_reserve((void **)&saved.data,&saved.capacity,links->count,sizeof(*saved.data))) return;
        for (size_t i = 0; i < links->count; i++) {
            eng_portal_link_t link = links->data[i];
            for (size_t j = 0; j < count; j++) { if (link.place == originals[j]) link.place = copies[j]; if (link.portal == originals[j]) link.portal = copies[j]; }
            saved.data[saved.count++] = link;
        }
    }
    if (eng_place_portals(engine,target) == NULL) { free(saved.data); return; }
    eng_portal_list_t *copy = &engine->portals.places.data[eng_map_get(&engine->portals.map,target)].links;
    free(copy->data); *copy = saved;
}

void eng_portals_forget_object(nmo_engine_t *engine, nmo_object_id_t id)
{
    if (id == 0) return;
    for (size_t i = 0; i < engine->portals.places.count; i++) {
        eng_place_portals_t *place = &engine->portals.places.data[i];
        if (place->id == id) { free(place->links.data); place->links = (eng_portal_list_t){0}; }
        else {
            size_t kept = 0;
            for (size_t j = 0; j < place->links.count; j++) if (place->links.data[j].place != id && place->links.data[j].portal != id) place->links.data[kept++] = place->links.data[j];
            place->links.count = kept;
        }
    }
}

void eng_free_portals(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->portals.places.count; i++) free(engine->portals.places.data[i].links.data);
    free(engine->portals.places.data); free(engine->portals.map.slots);
}
