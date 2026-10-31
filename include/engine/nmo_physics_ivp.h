/**
 * @file nmo_physics_ivp.h
 * @brief A physics provider on libivp, the C17 port of Ipion's IVP that
 *        physics_RT simulates with.
 *
 * Built only when libnmo is configured with NMO_IVP_SOURCE_DIR (a libivp
 * source tree); the library then defines NMO_HAS_PHYSICS_IVP. Attaching it
 * gives the physics pack's blocks an IVP environment as CKIpionManager
 * keeps one: objects, constraints, springs, forces and pushes, stepped by
 * the engine each frame, the moving entities' matrices written back.
 */
#ifndef NMO_PHYSICS_IVP_H
#define NMO_PHYSICS_IVP_H

#include "engine/nmo_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_physics_ivp nmo_physics_ivp_t;

/** Make the provider and set it on `engine` (nmo_engine_set_physics_provider). */
nmo_status_t nmo_physics_ivp_attach(nmo_engine_t *engine, nmo_physics_ivp_t **out_physics);
/** Take the provider off its engine and free it, with its environment. */
void nmo_physics_ivp_detach(nmo_physics_ivp_t *physics);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PHYSICS_IVP_H */
