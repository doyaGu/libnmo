/**
 * @file nmo_ckparameterout_schemas.h
 * @brief Public API for CKParameterOut schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKParameterOut.
 *
 * Based on official Virtools SDK:
 * - CKParameterOut (reference/src/CKParameterOut.cpp:120-160)
 */

#ifndef NMO_CKPARAMETEROUT_SCHEMAS_H
#define NMO_CKPARAMETEROUT_SCHEMAS_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/* =============================================================================
 * CKParameterOut STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKParameterOut state
 *
 * Output parameters own data and can have multiple destinations.
 * Inherits from CKParameter.
 *
 * Reference: reference/src/CKParameterOut.cpp:120-160
 */
typedef struct nmo_ckparameterout_state {
    /* Inherits CKParameter data (stored separately) */

    /* Destination parameters */
    nmo_object_id_t *destination_ids;  /**< Array of destination parameter IDs */
    uint32_t destination_count;        /**< Number of destinations */
} nmo_ckparameterout_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

typedef nmo_result_t (*nmo_ckparameterout_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckparameterout_state_t *out_state);

typedef nmo_result_t (*nmo_ckparameterout_serialize_fn)(
    const nmo_ckparameterout_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - Accessors
 * ============================================================================= */

NMO_API nmo_ckparameterout_deserialize_fn nmo_get_ckparameterout_deserialize(void);
NMO_API nmo_ckparameterout_serialize_fn nmo_get_ckparameterout_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETEROUT_SCHEMAS_H */
