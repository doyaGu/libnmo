/**
 * @file nmo_parameterin_schemas.h
 * @brief Public API for CKParameterIn schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKParameterIn.
 *
 * Based on official Virtools SDK:
 * - CKParameterIn (reference/src/CKParameterIn.cpp:140-250)
 */

#ifndef NMO_CKPARAMETERIN_SCHEMAS_H
#define NMO_CKPARAMETERIN_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_object_schemas.h"
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
 * CKParameterIn STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKParameterIn state
 *
 * Input parameters get data from a source (direct source or shared input).
 * They don't own data - they reference another parameter.
 *
 * Reference: reference/src/CKParameterIn.cpp:170-250
 */
typedef struct nmo_parameterin_state {
    /* Base CKObject state */
    nmo_object_state_t base;

    nmo_guid_t type_guid;              /**< Parameter type GUID */
    nmo_ref_t source;                  /**< Source parameter (direct or shared) */
    nmo_ref_t owner;                   /**< Owner behavior (legacy/default data) */
    uint8_t is_shared;                 /**< TRUE if shared input, FALSE if direct source */
    uint8_t is_disabled;               /**< TRUE if parameter is disabled */
} nmo_parameterin_state_t;

static inline nmo_object_id_t nmo_parameterin_source_id(
    const nmo_parameterin_state_t *state)
{
    return state != NULL
        ? nmo_ref_runtime_id(&state->source)
        : NMO_OBJECT_ID_NONE;
}

static inline nmo_object_id_t nmo_parameterin_owner_id(
    const nmo_parameterin_state_t *state)
{
    return state != NULL
        ? nmo_ref_runtime_id(&state->owner)
        : NMO_OBJECT_ID_NONE;
}

static inline void nmo_parameterin_set_source_id(
    nmo_parameterin_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->source = nmo_ref_from_id(id);
}

static inline void nmo_parameterin_set_owner_id(
    nmo_parameterin_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->owner = nmo_ref_from_id(id);
}

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_parameterin_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterin_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterin_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameterin_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_parameterin_vtable, nmo_register_parameterin_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETERIN_SCHEMAS_H */
