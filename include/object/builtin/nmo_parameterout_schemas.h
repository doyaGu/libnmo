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

    /* Legacy owner reference (obsolete in CK2 but present in old files) */
    nmo_object_id_t owner_id;          /**< Owner object ID (legacy formats) */

    /* Destination parameters */
    nmo_object_id_t *destination_ids;  /**< Array of destination parameter IDs */
    uint32_t destination_count;        /**< Number of destinations */
} nmo_parameterout_state_t;

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

NMO_API nmo_status_t nmo_parameterout_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_DECLARE_OBJECT_SCHEMA(nmo_parameterout_vtable, nmo_register_parameterout_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETEROUT_SCHEMAS_H */
