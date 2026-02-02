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
#include <stddef.h>
#include <stdalign.h>

#define CK_STATESAVE_KINEMATICCHAINALL 0x000000FFu

static nmo_result_t nmo_ckkinematicchain_deserialize_internal(
    nmo_ckkinematicchain_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckkinematicchain_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KINEMATICCHAINALL).code == NMO_OK) {
        out_state->has_chain_data = 1;
        nmo_object_id_t placeholder = 0;
        (void)nmo_chunk_read_object_id(chunk, &placeholder);
        (void)nmo_chunk_read_object_id(chunk, &out_state->start_effector_id);
        (void)nmo_chunk_read_object_id(chunk, &out_state->end_effector_id);
    }

    return nmo_result_ok();
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

static nmo_result_t nmo_ckkinematicchain_serialize_internal(
    const nmo_ckkinematicchain_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckkinematicchain_serialize"));
    }

    nmo_result_t result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    if (in_state->has_chain_data) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KINEMATICCHAINALL);
        if (result.code != NMO_OK) return result;
        nmo_chunk_write_object_id(out_chunk, 0);
        nmo_chunk_write_object_id(out_chunk, in_state->start_effector_id);
        nmo_chunk_write_object_id(out_chunk, in_state->end_effector_id);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckkinematicchain_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckkinematicchain_state_t *out_state = (nmo_ckkinematicchain_state_t *)instance;
    return nmo_ckkinematicchain_deserialize_internal(out_state, chunk, context);
}

nmo_result_t nmo_ckkinematicchain_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckkinematicchain_state_t *in_state = (const nmo_ckkinematicchain_state_t *)instance;
    return nmo_ckkinematicchain_serialize_internal(in_state, out_chunk, context);
}
