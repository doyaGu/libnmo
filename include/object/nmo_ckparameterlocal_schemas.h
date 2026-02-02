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
#include "nmo_ckparameter_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

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
    /* Base CKParameter state */
    nmo_ckparameter_state_t base;

    uint8_t is_myself;                 /**< TRUE if "myself" parameter */
    uint8_t is_setting;                /**< TRUE if behavior setting */
} nmo_ckparameterlocal_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_result_t nmo_ckparameterlocal_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckparameterlocal_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckparameterlocal_vtable, nmo_register_ckparameterlocal_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETERLOCAL_SCHEMAS_H */
