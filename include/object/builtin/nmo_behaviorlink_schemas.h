/**
 * @file nmo_behaviorlink_schemas.h
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
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

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
typedef struct nmo_behaviorlink_state_t {
    /**
     * @brief Base CKObject state
     */
    nmo_object_state_t base;

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

    /** Reference to the CKBehaviorIO that serves as the input endpoint. */
    nmo_ref_t in_io;

    /** Reference to the CKBehaviorIO that serves as the output endpoint. */
    nmo_ref_t out_io;

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
} nmo_behaviorlink_state_t;

static inline nmo_object_id_t nmo_behaviorlink_in_io_id(
    const nmo_behaviorlink_state_t *state)
{
    return state != NULL ? nmo_ref_runtime_id(&state->in_io)
                         : NMO_OBJECT_ID_NONE;
}

static inline nmo_object_id_t nmo_behaviorlink_out_io_id(
    const nmo_behaviorlink_state_t *state)
{
    return state != NULL ? nmo_ref_runtime_id(&state->out_io)
                         : NMO_OBJECT_ID_NONE;
}

static inline void nmo_behaviorlink_set_in_io_id(
    nmo_behaviorlink_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->in_io = nmo_ref_from_id(id);
}

static inline void nmo_behaviorlink_set_out_io_id(
    nmo_behaviorlink_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->out_io = nmo_ref_from_id(id);
}

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_behaviorlink_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_behaviorlink_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_behaviorlink_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_behaviorlink_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_behaviorlink_vtable, nmo_register_behaviorlink_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEHAVIORLINK_SCHEMAS_H */
