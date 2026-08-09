/**
 * @file nmo_parameteroperation_schemas.h
 * @brief CKParameterOperation schema definitions
 */

#ifndef NMO_CKPARAMETEROPERATION_SCHEMAS_H
#define NMO_CKPARAMETEROPERATION_SCHEMAS_H

#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ref.h"
#include "nmo_types.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/** Object reference and its optional non-file state chunk. */
typedef struct nmo_parameteroperation_ref {
    nmo_ref_t ref;
    nmo_chunk_t *chunk;
} nmo_parameteroperation_ref_t;

/**
 * @brief CKParameterOperation state
 */
typedef struct nmo_parameteroperation_state {
    nmo_object_state_t base;
    nmo_guid_t operation_guid;
    nmo_ref_t legacy_prefix_ref;
    nmo_ref_t owner;
    nmo_parameteroperation_ref_t in1;
    nmo_parameteroperation_ref_t in2;
    nmo_parameteroperation_ref_t out;
    uint8_t has_new_data;
    uint8_t has_operation;
    uint8_t has_owner;
    uint8_t has_in1;
    uint8_t has_in2;
    uint8_t has_out;
} nmo_parameteroperation_state_t;

static inline nmo_object_id_t nmo_parameteroperation_owner_id(
    const nmo_parameteroperation_state_t *state)
{
    return state != NULL ? nmo_ref_runtime_id(&state->owner) : NMO_OBJECT_ID_NONE;
}

static inline nmo_object_id_t nmo_parameteroperation_in1_id(
    const nmo_parameteroperation_state_t *state)
{
    return state != NULL ? nmo_ref_runtime_id(&state->in1.ref) : NMO_OBJECT_ID_NONE;
}

static inline nmo_object_id_t nmo_parameteroperation_in2_id(
    const nmo_parameteroperation_state_t *state)
{
    return state != NULL ? nmo_ref_runtime_id(&state->in2.ref) : NMO_OBJECT_ID_NONE;
}

static inline nmo_object_id_t nmo_parameteroperation_out_id(
    const nmo_parameteroperation_state_t *state)
{
    return state != NULL ? nmo_ref_runtime_id(&state->out.ref) : NMO_OBJECT_ID_NONE;
}

static inline void nmo_parameteroperation_set_owner_id(
    nmo_parameteroperation_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->owner = nmo_ref_from_id(id);
}

static inline void nmo_parameteroperation_set_in1_id(
    nmo_parameteroperation_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->in1.ref = nmo_ref_from_id(id);
}

static inline void nmo_parameteroperation_set_in2_id(
    nmo_parameteroperation_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->in2.ref = nmo_ref_from_id(id);
}

static inline void nmo_parameteroperation_set_out_id(
    nmo_parameteroperation_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->out.ref = nmo_ref_from_id(id);
}

NMO_API nmo_status_t nmo_parameteroperation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameteroperation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameteroperation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameteroperation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_parameteroperation_vtable, nmo_register_parameteroperation_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETEROPERATION_SCHEMAS_H */
