/**
 * @file nmo_ckbehaviorlink_schemas.h
 * @brief CKBehaviorLink schema definitions
 *
 * CKBehaviorLink represents connections between behavior I/O endpoints in a behavior graph.
 * It stores activation delays and references to input/output CKBehaviorIO objects.
 * 
 * Based on official Virtools SDK (reference/src/CKBehaviorLink.cpp).
 */

#ifndef NMO_CKBEHAVIORLINK_SCHEMAS_H
#define NMO_CKBEHAVIORLINK_SCHEMAS_H

#include "nmo_types.h"
#include "nmo_ckobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_result nmo_result_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/* =============================================================================
 * CKBehaviorLink STATE STRUCTURE
 * ============================================================================= */

/**
 * @brief CKBehaviorLink state structure
 * 
 * CKBehaviorLink connects behavior I/O endpoints in a behavior graph.
 * It stores timing information (delays) and references to connected I/Os.
 * 
 * The delays control when the link activates:
 * - activation_delay: Current activation delay (frames to wait)
 * - initial_activation_delay: Reset value for activation delay
 * 
 * Reference: reference/src/CKBehaviorLink.cpp:49-121
 */
typedef struct nmo_ckbehaviorlink_state_t {
    /**
     * @brief Base CKObject state
     */
    nmo_ckobject_state_t base;

    /**
     * @brief Current activation delay (in frames)
     * 
     * Number of frames to wait before activating the link.
     * Decrements each frame until reaching 0, then activates.
     */
    int16_t activation_delay;

    /**
     * @brief Initial activation delay (in frames)
     * 
     * Reset value for activation_delay after each activation.
     * Allows for periodic or delayed activation patterns.
     */
    int16_t initial_activation_delay;

    /**
     * @brief Input I/O object ID
     * 
     * Reference to the CKBehaviorIO that serves as the input endpoint.
     * ID = 0 means no input connected.
     */
    nmo_object_id_t in_io_id;

    /**
     * @brief Output I/O object ID
     * 
     * Reference to the CKBehaviorIO that serves as the output endpoint.
     * ID = 0 means no output connected.
     */
    nmo_object_id_t out_io_id;

    /**
     * @brief Whether the link format was detected during load
     */
    bool has_format;

    /**
     * @brief Use new format (NEWDATA identifier)
     */
    bool use_new_format;

    /**
     * @brief Presence of legacy identifiers
     */
    bool has_legacy_curdelay;
    bool has_legacy_ios;
    bool has_legacy_delay;
} nmo_ckbehaviorlink_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_result_t nmo_ckbehaviorlink_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckbehaviorlink_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckbehaviorlink_vtable, nmo_register_ckbehaviorlink_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEHAVIORLINK_SCHEMAS_H */
