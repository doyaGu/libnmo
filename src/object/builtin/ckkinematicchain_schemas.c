/**
 * @file ckkinematicchain_schemas.c
 * @brief CKKinematicChain schema implementation
 */

#include "object/builtin/nmo_kinematicchain_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(kinematicchain, nmo_kinematicchain_state_t)
#include <stddef.h>
#include <stdalign.h>

static nmo_status_t nmo_kinematicchain_deserialize_internal(
    nmo_kinematicchain_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_kinematicchain_deserialize");
    }

    out_state->has_chain_data = 0;
    out_state->start_effector_id = 0;
    out_state->end_effector_id = 0;

    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KINEMATICCHAINALL) == NMO_OK) {
        out_state->has_chain_data = 1;
        nmo_object_id_t placeholder = 0;
        result = nmo_chunk_read_object_id(chunk, &placeholder);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_object_id(chunk, &out_state->start_effector_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_object_id(chunk, &out_state->end_effector_id);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_kinematicchain_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_kinematicchain_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_kinematicchain_state_t, has_chain_data, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_kinematicchain_state_t, start_effector_id),
    NMO_FIELD_REF(nmo_kinematicchain_state_t, end_effector_id)
};

nmo_status_t nmo_kinematicchain_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_kinematicchain_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_kinematicchain_remap_dependencies");
    }

    nmo_kinematicchain_state_t *state = (nmo_kinematicchain_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    state->has_chain_data = state->has_chain_data ? 1 : 0;

    if (!state->has_chain_data) {
        state->start_effector_id = 0;
        state->end_effector_id = 0;
        return nmo_object_default_validate(state, NULL, NULL);
    }

    if (state->start_effector_id != 0 && repo &&
        nmo_object_repository_find_by_id(repo, state->start_effector_id) == NULL) {
        state->start_effector_id = 0;
    }
    if (state->end_effector_id != 0 && repo &&
        nmo_object_repository_find_by_id(repo, state->end_effector_id) == NULL) {
        state->end_effector_id = 0;
    }

    if (state->start_effector_id == 0 && state->end_effector_id == 0) {
        state->has_chain_data = 0;
    }

    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_kinematicchain_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_kinematicchain_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_kinematicchain_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS(kinematicchain, nmo_kinematicchain_state_t)

nmo_type_vtable_t nmo_kinematicchain_vtable = {
    .prepare_dependencies = nmo_kinematicchain_prepare_dependencies,
    .remap_dependencies = nmo_kinematicchain_remap_dependencies,
    .pre_delete = nmo_kinematicchain_pre_delete,
    .post_delete = nmo_kinematicchain_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_kinematicchain_create,
        nmo_kinematicchain_destroy,
        nmo_kinematicchain_serialize,
        nmo_kinematicchain_deserialize,
        nmo_kinematicchain_copy,
        nmo_kinematicchain_validate,
        nmo_kinematicchain_equals,
        nmo_kinematicchain_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_kinematicchain_type,
    CKPGUID_KINEMATICCHAIN,
    "CKKinematicChain",
    NMO_CID_KINEMATICCHAIN,
    CKPGUID_OBJECT,
    nmo_kinematicchain_state_t,
    &nmo_kinematicchain_vtable,
    nmo_kinematicchain_fields)

static nmo_status_t nmo_kinematicchain_serialize_internal(
    const nmo_kinematicchain_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_kinematicchain_serialize");
    }

    nmo_status_t result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    if (!is_file && (save_flags & CK_STATESAVE_KINEMATICCHAINALL) == 0) {
        NMO_RETURN_OK();
    }

    {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KINEMATICCHAINALL);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, 0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->start_effector_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->end_effector_id);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_kinematicchain_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_kinematicchain_state_t *out_state = (nmo_kinematicchain_state_t *)instance;
    return nmo_kinematicchain_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_kinematicchain_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_kinematicchain_state_t *in_state = (const nmo_kinematicchain_state_t *)instance;
    return nmo_kinematicchain_serialize_internal(in_state, out_chunk, context);
}
