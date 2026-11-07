/**
 * @file synchro.c
 * @brief Rendezvous and critical sections (CKSynchroObject,
 *        CKCriticalSectionObject), following CK2's CKSynchroObject.cpp.
 */

#include "../engine_internal.h"

#include "object/builtin/nmo_synchro_schemas.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <stdlib.h>
#include <string.h>

/* The saved references of `refs` (nmo_ref_t) as engine IDs, in the owner's module */
static bool synchro_load_refs(const nmo_engine_t *engine, nmo_object_id_t owner,
                              const nmo_array_t *refs, nmo_object_id_t **out, size_t *count,
                              size_t *capacity)
{
    const nmo_ref_t *saved = (const nmo_ref_t *)refs->data;
    for (size_t i = 0; i < refs->count; i++) {
        nmo_object_id_t id = eng_object_ref(engine, owner, &saved[i]);
        if (id == 0) {
            continue;
        }
        if (!eng_vec_reserve((void **)out, capacity, *count + 1u, sizeof(**out))) {
            return false;
        }
        (*out)[(*count)++] = id;
    }
    return true;
}

eng_synchro_t *eng_synchro(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = id != 0 ? eng_map_get(&engine->synchros.map, id) : -1;
    if (index >= 0) {
        return &engine->synchros.entries.data[index];
    }
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    if (object == NULL) {
        return NULL;
    }
    const nmo_synchro_state_t *rendezvous = (const nmo_synchro_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_SYNCHRO);
    const nmo_criticalsection_state_t *section = (const nmo_criticalsection_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object,
                                                         CKPGUID_CRITICALSECTION);
    const nmo_state_state_t *state = (const nmo_state_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_STATE);
    if (rendezvous == NULL && section == NULL && state == NULL) {
        return NULL;
    }
    eng_synchro_t synchro = {.id = id};
    if (state != NULL) {
        synchro.is_state = true;
        synchro.state_active = state->event_flag != 0;
    } else if (rendezvous != NULL) {
        synchro.max_waiters = rendezvous->max_waiters;
        if (!synchro_load_refs(engine, id, &rendezvous->arrived_ids, &synchro.arrived.data,
                               &synchro.arrived.count, &synchro.arrived.capacity) ||
            !synchro_load_refs(engine, id, &rendezvous->passed_ids, &synchro.passed.data,
                               &synchro.passed.count, &synchro.passed.capacity)) {
            free(synchro.arrived.data);
            free(synchro.passed.data);
            return NULL;
        }
    } else {
        synchro.in_section = eng_object_ref(engine, id, &section->object_in_section);
    }
    eng_synchro_t *slot = NULL;
    index = (int32_t)engine->synchros.entries.count;
    if (!ENG_VEC_PUSH(engine->synchros.entries, slot)) {
        free(synchro.arrived.data);
        free(synchro.passed.data);
        return NULL;
    }
    if (!eng_map_put(&engine->synchros.map, id, index)) {
        engine->synchros.entries.count--;
        free(synchro.arrived.data);
        free(synchro.passed.data);
        return NULL;
    }
    *slot = synchro;
    return slot;
}

static bool synchro_has(const nmo_object_id_t *ids, size_t count, nmo_object_id_t id)
{
    for (size_t i = 0; i < count; i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

bool eng_rendezvous_pass(nmo_engine_t *engine, eng_synchro_t *synchro, nmo_object_id_t asker)
{
    (void)engine;
    if (synchro == NULL || asker == 0) {
        return false;
    }
    bool arrived = synchro_has(synchro->arrived.data, synchro->arrived.count, asker);
    if ((int32_t)synchro->arrived.count == synchro->max_waiters) {
        if (!arrived) {
            return false;
        }
    } else {
        if (arrived) {
            return false;
        }
        nmo_object_id_t *slot = NULL;
        if (!ENG_VEC_PUSH(synchro->arrived, slot)) {
            return false;
        }
        *slot = asker;
        if ((int32_t)synchro->arrived.count != synchro->max_waiters) {
            return false;
        }
    }
    if (!synchro_has(synchro->passed.data, synchro->passed.count, asker)) {
        nmo_object_id_t *slot = NULL;
        if (!ENG_VEC_PUSH(synchro->passed, slot)) {
            return false;
        }
        *slot = asker;
    }
    if ((int32_t)synchro->passed.count == synchro->max_waiters) {
        synchro->passed.count = 0; /* Reset */
        synchro->arrived.count = 0;
    }
    return true;
}

bool eng_critical_enter(eng_synchro_t *section, nmo_object_id_t asker)
{
    if (section == NULL || asker == 0 || section->in_section != 0) {
        return false;
    }
    section->in_section = asker;
    return true;
}

bool eng_critical_leave(eng_synchro_t *section, nmo_object_id_t asker)
{
    if (section == NULL || asker == 0 || section->in_section != asker) {
        return false;
    }
    section->in_section = 0;
    return true;
}

void eng_free_synchros(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->synchros.entries.count; i++) {
        free(engine->synchros.entries.data[i].arrived.data);
        free(engine->synchros.entries.data[i].passed.data);
    }
    free(engine->synchros.entries.data);
    free(engine->synchros.map.slots);
}
