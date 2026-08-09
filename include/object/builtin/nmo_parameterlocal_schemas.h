/**
 * @file nmo_parameterlocal_schemas.h
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
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

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
typedef struct nmo_parameterlocal_state {
    /* Base CKParameter state */
    nmo_parameter_state_t base;

    nmo_ref_t owner;                   /**< Runtime owner behavior */

    uint8_t is_myself;                 /**< TRUE if "myself" parameter */
    uint8_t is_setting;                /**< TRUE if behavior setting */
} nmo_parameterlocal_state_t;

static inline nmo_object_id_t nmo_parameterlocal_owner_id(
    const nmo_parameterlocal_state_t *state)
{
    return state != NULL
        ? nmo_ref_runtime_id(&state->owner)
        : NMO_OBJECT_ID_NONE;
}

static inline void nmo_parameterlocal_set_owner_id(
    nmo_parameterlocal_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) {
        state->owner = nmo_ref_from_id(id);
    }
}

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_parameterlocal_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterlocal_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterlocal_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterlocal_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_parameterlocal_vtable, nmo_register_parameterlocal_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETERLOCAL_SCHEMAS_H */
