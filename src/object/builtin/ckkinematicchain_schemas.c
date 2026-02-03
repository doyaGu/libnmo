/**
 * @file ckkinematicchain_schemas.c
 * @brief CKKinematicChain schema implementation
 */

#include "object/nmo_ckkinematicchain_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckkinematicchain, nmo_ckkinematicchain_state_t)
#include <stddef.h>
#include <stdalign.h>

static nmo_status_t nmo_ckkinematicchain_deserialize_internal(
    nmo_ckkinematicchain_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckkinematicchain_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_ckkinematicchain_create(out_state, NULL, context));

    nmo_status_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KINEMATICCHAINALL) == NMO_OK) {
        out_state->has_chain_data = 1;
        nmo_object_id_t placeholder = 0;
        (void)nmo_chunk_read_object_id(chunk, &placeholder);
        (void)nmo_chunk_read_object_id(chunk, &out_state->start_effector_id);
        (void)nmo_chunk_read_object_id(chunk, &out_state->end_effector_id);
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckkinematicchain,
    nmo_ckkinematicchain_state_t,
    nmo_ckkinematicchain_serialize,
    nmo_ckkinematicchain_deserialize,
    NMO_GUID_CKKINEMATICCHAIN,
    "CKKinematicChain",
    NMO_CID_KINEMATICCHAIN,
    NMO_GUID_CKOBJECT
)

static nmo_status_t nmo_ckkinematicchain_serialize_internal(
    const nmo_ckkinematicchain_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckkinematicchain_serialize");
    }

    nmo_status_t result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (in_state->has_chain_data) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KINEMATICCHAINALL);
        if (result != NMO_OK) return result;
        nmo_chunk_write_object_id(out_chunk, 0);
        nmo_chunk_write_object_id(out_chunk, in_state->start_effector_id);
        nmo_chunk_write_object_id(out_chunk, in_state->end_effector_id);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_ckkinematicchain_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckkinematicchain_state_t *out_state = (nmo_ckkinematicchain_state_t *)instance;
    return nmo_ckkinematicchain_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_ckkinematicchain_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckkinematicchain_state_t *in_state = (const nmo_ckkinematicchain_state_t *)instance;
    return nmo_ckkinematicchain_serialize_internal(in_state, out_chunk, context);
}
