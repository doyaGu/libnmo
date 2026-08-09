/**
 * @file ckkinematicchain_schemas.c
 * @brief CKKinematicChain schema implementation
 */

#include "object/builtin/nmo_kinematicchain_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_deserialize_context.h"
#include "type/nmo_reflection.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    kinematicchain,
    nmo_kinematicchain_state_t,
    do {
        nmo_status_t result = nmo_object_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
    } while (0),
    nmo_object_vtable.destroy(&state->base, NULL, context))
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
    out_state->reserved_object_id = 0;
    out_state->start_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->end_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_KINEMATICCHAINALL, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 3u) return NMO_ERR_INVALID_FORMAT;
        uint32_t reserved_object_id = 0;
        result = nmo_chunk_read_dword(chunk, &reserved_object_id);
        if (result != NMO_OK) return result;
        nmo_ref_t start_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        nmo_ref_t end_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &start_effector);
        if (result != NMO_OK) return result;
        result = nmo_ref_read(chunk, &end_effector);
        if (result != NMO_OK) return result;
        const nmo_object_repository_t *repository =
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        nmo_ref_check_class(
            &start_effector, repository, types, NMO_CID_BODYPART);
        nmo_ref_check_class(
            &end_effector, repository, types, NMO_CID_BODYPART);
        out_state->has_chain_data = 1;
        out_state->reserved_object_id = reserved_object_id;
        out_state->start_effector = start_effector;
        out_state->end_effector = end_effector;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_kinematicchain_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_kinematicchain_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_kinematicchain_state_t, has_chain_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_kinematicchain_state_t, reserved_object_id, CKPGUID_UINT32),
    NMO_FIELD_REF_VALUE(nmo_kinematicchain_state_t, start_effector),
    NMO_FIELD_REF_VALUE(nmo_kinematicchain_state_t, end_effector)
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

    /* Preserve chain section presence and unresolved serialized values. */
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
    nmo_kinematicchain_state_t *state = instance;
    state->start_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->end_effector = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
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

static nmo_status_t nmo_kinematicchain_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    (void)arena;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (src != dst) *(nmo_kinematicchain_state_t *)dst =
        *(const nmo_kinematicchain_state_t *)src;
    return NMO_OK;
}

static nmo_status_t nmo_kinematicchain_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_kinematicchain_state_t *state = instance;
    return nmo_object_vtable.validate(&state->base, NULL, context);
}

static bool nmo_kinematicchain_ref_equals(
    const nmo_ref_t *lhs,
    const nmo_ref_t *rhs)
{
    return lhs->raw_id == rhs->raw_id &&
        lhs->id == rhs->id &&
        lhs->state == rhs->state;
}

static bool nmo_kinematicchain_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_kinematicchain_state_t *lhs = a;
    const nmo_kinematicchain_state_t *rhs = b;
    return nmo_object_vtable.equals(&lhs->base, &rhs->base) &&
        lhs->has_chain_data == rhs->has_chain_data &&
        lhs->reserved_object_id == rhs->reserved_object_id &&
        nmo_kinematicchain_ref_equals(
            &lhs->start_effector, &rhs->start_effector) &&
        nmo_kinematicchain_ref_equals(
            &lhs->end_effector, &rhs->end_effector);
}

static uint32_t nmo_kinematicchain_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_kinematicchain_state_t *state = instance;
    uint32_t hash = nmo_object_vtable.hash(&state->base);
#define NMO_KINEMATICCHAIN_HASH_FIELD(field) \
    do { \
        hash ^= (uint32_t)nmo_hash_fnv1a( \
            &state->field, sizeof(state->field)); \
        hash *= 16777619u; \
    } while (0)
    NMO_KINEMATICCHAIN_HASH_FIELD(has_chain_data);
    NMO_KINEMATICCHAIN_HASH_FIELD(reserved_object_id);
    NMO_KINEMATICCHAIN_HASH_FIELD(start_effector.raw_id);
    NMO_KINEMATICCHAIN_HASH_FIELD(start_effector.id);
    NMO_KINEMATICCHAIN_HASH_FIELD(start_effector.state);
    NMO_KINEMATICCHAIN_HASH_FIELD(end_effector.raw_id);
    NMO_KINEMATICCHAIN_HASH_FIELD(end_effector.id);
    NMO_KINEMATICCHAIN_HASH_FIELD(end_effector.state);
#undef NMO_KINEMATICCHAIN_HASH_FIELD
    return hash;
}

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
        result = nmo_chunk_write_dword(out_chunk, in_state->reserved_object_id);
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
