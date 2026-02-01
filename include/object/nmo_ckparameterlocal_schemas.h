/**
 * @file nmo_ckparameterlocal_schemas.h
 * @brief Public API for CKParameterLocal schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKParameterLocal.
 *
 * Based on official Virtools SDK:
 * - CKParameterLocal (reference/src/CKParameterLocal.cpp:100-140)
 */

#ifndef NMO_CKPARAMETERLOCAL_SCHEMAS_H
#define NMO_CKPARAMETERLOCAL_SCHEMAS_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/* =============================================================================
 * CKParameterLocal STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKParameterLocal state
 *
 * Local parameters are behavior-local storage.
 * Can be "myself" parameters that reference the owner object.
 * Inherits from CKParameter.
 *
 * Reference: reference/src/CKParameterLocal.cpp:100-140
 */
typedef struct nmo_ckparameterlocal_state {
    /* Inherits CKParameter data (stored separately) */

    uint8_t is_myself;                 /**< TRUE if "myself" parameter */
    uint8_t is_setting;                /**< TRUE if behavior setting */
} nmo_ckparameterlocal_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

typedef nmo_result_t (*nmo_ckparameterlocal_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckparameterlocal_state_t *out_state);

typedef nmo_result_t (*nmo_ckparameterlocal_serialize_fn)(
    const nmo_ckparameterlocal_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - Accessors
 * ============================================================================= */

NMO_API nmo_ckparameterlocal_deserialize_fn nmo_get_ckparameterlocal_deserialize(void);
NMO_API nmo_ckparameterlocal_serialize_fn nmo_get_ckparameterlocal_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETERLOCAL_SCHEMAS_H */
