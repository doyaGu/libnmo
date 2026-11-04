#ifndef NMO_ENGINE_KINEMATIC_H
#define NMO_ENGINE_KINEMATIC_H

#include "engine_transform.h"

typedef struct eng_kinematic_chain {
    nmo_object_id_t id, start, end;
} eng_kinematic_chain_t;

typedef struct eng_kinematics {
    ENG_VEC(eng_kinematic_chain_t) chains;
    eng_map_t map;
} eng_kinematics_t;

bool eng_kinematic_set_effectors(nmo_engine_t *engine, nmo_object_id_t chain,
    nmo_object_id_t start, nmo_object_id_t end);
bool eng_kinematic_effectors(const nmo_engine_t *engine, nmo_object_id_t chain,
    nmo_object_id_t *start, nmo_object_id_t *end);
void eng_copy_kinematic_chain(nmo_engine_t *engine, nmo_object_id_t source,
    nmo_object_id_t target, const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count);
void eng_free_kinematics(nmo_engine_t *engine);

/* CKKinematicChain::IKSetEffectorPos in world space. Only the connected
 * BodyPart hierarchy from start to end participates; the pivot and local
 * translations remain fixed. Failed constraint steps restore the pose. */
bool eng_kinematic_set_position(nmo_engine_t *engine, nmo_object_id_t start,
    nmo_object_id_t end, const eng_vector_t *position);

#endif
