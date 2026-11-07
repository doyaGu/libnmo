/**
 * @file engine_synchro.h
 * @brief CKSynchroObject and CKCriticalSectionObject as they are at run time:
 *        who arrived at a rendezvous and who passed it, who is in a critical
 *        section. Made from the saved state the first time a block asks.
 */

#ifndef NMO_ENGINE_SYNCHRO_H
#define NMO_ENGINE_SYNCHRO_H

#include "engine_base.h"

typedef struct eng_synchro {
    nmo_object_id_t id;
    int32_t max_waiters;                  /* CKSynchroObject::m_MaxWaiters */
    ENG_VEC(nmo_object_id_t) arrived;     /* m_Arrived, in arrival order */
    ENG_VEC(nmo_object_id_t) passed;      /* m_Passed */
    nmo_object_id_t in_section;           /* CKCriticalSectionObject::m_ObjectInSection */
} eng_synchro_t;

typedef struct eng_synchros {
    ENG_VEC(eng_synchro_t) entries;
    eng_map_t map;
} eng_synchros_t;

/* core/synchro.c */
/* The synchro or critical section object `id` now; NULL when it is neither. */
eng_synchro_t *eng_synchro(nmo_engine_t *engine, nmo_object_id_t id);
/* CKSynchroObject::CanIPassRendezVous: `asker` arrives; true once all the waiters have. */
bool eng_rendezvous_pass(nmo_engine_t *engine, eng_synchro_t *synchro, nmo_object_id_t asker);
/* CKCriticalSectionObject::EnterCriticalSection and LeaveCriticalSection */
bool eng_critical_enter(eng_synchro_t *section, nmo_object_id_t asker);
bool eng_critical_leave(eng_synchro_t *section, nmo_object_id_t asker);
void eng_free_synchros(nmo_engine_t *engine);

#endif /* NMO_ENGINE_SYNCHRO_H */
