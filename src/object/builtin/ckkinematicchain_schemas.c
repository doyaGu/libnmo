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
    out_state->start_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->end_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_KINEMATICCHAINALL);
    if (result == NMO_OK) {
        nmo_object_id_t placeholder = 0;
        result = nmo_chunk_read_object_id(chunk, &placeholder);
        if (result != NMO_OK) return result;
        nmo_ref_t start_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        nmo_ref_t end_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &start_effector);
        if (result != NMO_OK) return result;
        result = nmo_ref_read(chunk, &end_effector);
        if (result != NMO_OK) return result;
        out_state->has_chain_data = 1;
        out_state->start_effector = start_effector;
        out_state->end_effector = end_effector;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_kinematicchain_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_kinematicchain_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_kinematicchain_state_t, has_chain_data, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_kinematicchain_state_t, start_effector),
    NMO_FIELD_REF(nmo_kinematicchain_state_t, end_effector)
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

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    /* Preserve chain section presence and unresolved endpoints. */
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
    if (!in_state->has_chain_data ||
        (!is_file && (save_flags & CK_STATESAVE_KINEMATICCHAINALL) == 0)) {
        NMO_RETURN_OK();
    }

    {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KINEMATICCHAINALL);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, 0);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->start_effector);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->end_effector);
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
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_kinematicchain_state_t decoded = *out_state;
    nmo_status_t result = nmo_kinematicchain_deserialize_internal(
        &decoded, chunk, context);
    if (result != NMO_OK) return result;
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_kinematicchain_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    const nmo_kinematicchain_state_t *in_state = (const nmo_kinematicchain_state_t *)instance;
    if (in_state == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_kinematicchain_validate(
        in_state, type, context));
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_kinematicchain_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}
