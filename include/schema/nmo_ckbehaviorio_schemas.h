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

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_result nmo_result_t;

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
 * - CK_BEHAVIORIO_ACTIVE (0x04): Active I/O
 * 
 * Reference: reference/src/CKBehaviorIO.cpp:19-48
 */
typedef struct nmo_ckbehaviorio_state_t {
    /**
     * @brief I/O flags
     * 
     * Stored as "OldFlags" in SDK (compatibility naming).
     * Determines whether this is an input or output, and other characteristics.
     */
    uint32_t old_flags;
} nmo_ckbehaviorio_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief Function pointer for CKBehaviorIO deserialization
 * 
 * @param chunk Chunk to read from
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckbehaviorio_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckbehaviorio_state_t *out_state);

/**
 * @brief Function pointer for CKBehaviorIO serialization
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckbehaviorio_serialize_fn)(
    nmo_chunk_t *chunk,
    const nmo_ckbehaviorio_state_t *state);

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

/**
 * @brief Register CKBehaviorIO schema types
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckbehaviorio_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get the deserialize function for CKBehaviorIO
 * 
 * @return Deserialize function pointer
 */
nmo_ckbehaviorio_deserialize_fn nmo_get_ckbehaviorio_deserialize(void);

/**
 * @brief Get the serialize function for CKBehaviorIO
 * 
 * @return Serialize function pointer
 */
nmo_ckbehaviorio_serialize_fn nmo_get_ckbehaviorio_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEHAVIORIO_SCHEMAS_H */
