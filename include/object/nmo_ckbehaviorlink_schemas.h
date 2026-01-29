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

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_result nmo_result_t;

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
} nmo_ckbehaviorlink_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief Function pointer for CKBehaviorLink deserialization
 * 
 * @param chunk Chunk to read from
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckbehaviorlink_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckbehaviorlink_state_t *out_state);

/**
 * @brief Function pointer for CKBehaviorLink serialization
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckbehaviorlink_serialize_fn)(
    const nmo_ckbehaviorlink_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

/**
 * @brief Register CKBehaviorLink schema types
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckbehaviorlink_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get the deserialize function for CKBehaviorLink
 * 
 * @return Deserialize function pointer
 */
nmo_ckbehaviorlink_deserialize_fn nmo_get_ckbehaviorlink_deserialize(void);

/**
 * @brief Get the serialize function for CKBehaviorLink
 * 
 * @return Serialize function pointer
 */
nmo_ckbehaviorlink_serialize_fn nmo_get_ckbehaviorlink_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEHAVIORLINK_SCHEMAS_H */
