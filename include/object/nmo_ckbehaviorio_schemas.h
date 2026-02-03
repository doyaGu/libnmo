/**
 * @file nmo_ckbehaviorio_schemas.h
 * @brief CKBehaviorIO schema definitions
 *
 * CKBehaviorIO represents input/output endpoints for behavior graphs.
 * It's a simple class that extends CKObject and stores I/O flags.
 * 
 * Based on official Virtools SDK (reference/src/CKBehaviorIO.cpp).
 */

#ifndef NMO_CKBEHAVIORIO_SCHEMAS_H
#define NMO_CKBEHAVIORIO_SCHEMAS_H

#include "nmo_types.h"
#include "nmo_ckobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * CKBehaviorIO STATE STRUCTURE
 * ============================================================================= */

/**
 * @brief CKBehaviorIO state structure
 * 
 * CKBehaviorIO is a simple class representing behavior I/O endpoints.
 * It only stores old_flags which indicate the I/O type and characteristics.
 * 
 * Common flags (from CK_BEHAVIORIO_FLAGS):
 * - CK_BEHAVIORIO_IN (0x01): Input endpoint
 * - CK_BEHAVIORIO_OUT (0x02): Output endpoint
 * - CK_BEHAVIORIO_ACTIVE (0x100): Active I/O
 * 
 * Reference: reference/src/CKBehaviorIO.cpp:19-48
 */
typedef struct nmo_ckbehaviorio_state_t {
    /**
     * @brief Base CKObject state
     */
    nmo_ckobject_state_t base;

    /**
     * @brief I/O flags
     * 
     * Stored as "OldFlags" in SDK (compatibility naming).
     * Determines whether this is an input or output, and other characteristics.
     */
    uint32_t old_flags;

    /**
     * @brief Whether the flags identifier was present in the source chunk
     * 
     * Preserves legacy files that omit CK_STATESAVE_BEHAV_IOFLAGS.
     */
    bool has_flags;
} nmo_ckbehaviorio_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_ckbehaviorio_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckbehaviorio_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckbehaviorio_vtable, nmo_register_ckbehaviorio_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEHAVIORIO_SCHEMAS_H */
