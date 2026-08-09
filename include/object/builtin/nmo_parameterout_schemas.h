/**
 * @file nmo_parameterout_schemas.h
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
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_ref.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

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
typedef struct nmo_parameterout_state {
    /* Base CKParameter state */
    nmo_parameter_state_t base;

    nmo_ref_t owner;                   /**< Runtime owner behavior or operation */

    /* Destination parameters */
    nmo_ref_t *destination_ids;        /**< Destination parameter references */
    uint32_t destination_count;        /**< Number of destinations */
    uint8_t has_destinations;          /**< Destinations section was present */
} nmo_parameterout_state_t;

static inline nmo_object_id_t nmo_parameterout_owner_id(
    const nmo_parameterout_state_t *state)
{
    return state != NULL
        ? nmo_ref_runtime_id(&state->owner)
        : NMO_OBJECT_ID_NONE;
}

static inline void nmo_parameterout_set_owner_id(
    nmo_parameterout_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) {
        state->owner = nmo_ref_from_id(id);
    }
}

static inline nmo_object_id_t nmo_parameterout_destination_id(
    const nmo_parameterout_state_t *state,
    uint32_t index)
{
    return state != NULL && state->destination_ids != NULL &&
            index < state->destination_count
        ? nmo_ref_runtime_id(&state->destination_ids[index])
        : NMO_OBJECT_ID_NONE;
}

static inline uint32_t nmo_parameterout_valid_destination_count(
    const nmo_parameterout_state_t *state)
{
    uint32_t count = 0;
    if (state == NULL || state->destination_ids == NULL) return 0;
    for (uint32_t i = 0; i < state->destination_count; ++i) {
        if (nmo_parameterout_destination_id(state, i) !=
            NMO_OBJECT_ID_NONE) {
            ++count;
        }
    }
    return count;
}

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_parameterout_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterout_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterout_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterout_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_parameterout_vtable, nmo_register_parameterout_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETEROUT_SCHEMAS_H */
