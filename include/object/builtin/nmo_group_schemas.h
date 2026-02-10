/**
 * @file nmo_group_schemas.h
 * @brief Public API for CKGroup schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKGroup.
 * CKGroup is a container for grouping CKBeObject instances.
 * 
 * Based on official Virtools SDK (reference/src/CKGroup.cpp:185-220):
 * - CKGroup stores an array of object IDs
 * - Simple identifier-based serialization
 * - PostLoad ensures group membership consistency
 */

#ifndef NMO_CKGROUP_SCHEMAS_H
#define NMO_CKGROUP_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_array.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * CKGroup STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKGroup state
 * 
 * CKGroup contains an array of CKBeObject references.
 * The group maintains bidirectional relationships - objects know which groups they're in.
 * 
 * Reference: reference/src/CKGroup.cpp:185-220
 */
typedef struct nmo_group_state {
    /* Base class state */
    nmo_beobject_state_t base;      /**< CKBeObject base state */
    
    /* Object array */
    nmo_array_t object_ids;           /**< Grouped object IDs (nmo_object_id_t) */
} nmo_group_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_group_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_group_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_group_vtable, nmo_register_group_type)

NMO_API nmo_status_t nmo_group_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKGROUP_SCHEMAS_H */
