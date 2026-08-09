/**
 * @file ckparameterlocal_schemas.c
 * @brief CKParameterLocal schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterLocal.
 *
 * Based on official Virtools SDK (reference/src/CKParameterLocal.cpp:100-140).
 */

#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_serialize_context.h"
#include "object/builtin/nmo_object_schemas.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    parameterlocal,
    nmo_parameterlocal_state_t,
    do {
        NMO_RETURN_IF_ERROR(nmo_parameter_vtable.create(
            &state->base, NULL, context));
        state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    } while (0),
    nmo_parameter_vtable.destroy(&state->base, NULL, context))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameterlocal_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameterlocal_state_t, base),
                    sizeof(nmo_parameter_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_VALUE(nmo_parameterlocal_state_t, owner),
    NMO_FIELD(nmo_parameterlocal_state_t, is_myself, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterlocal_state_t, is_setting, CKPGUID_UINT8)
};

/* =============================================================================
 * CKParameterLocal DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterLocal state from chunk
 *
 * Reference: reference/src/CKParameterLocal.cpp:131-145
 */
static nmo_status_t nmo_parameterlocal_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_parameterlocal_state_t *out_state = (nmo_parameterlocal_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Read base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_parameter_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETEROUT_OWNER, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
        nmo_ref_t owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &owner));
        nmo_ref_check_class(
            &owner,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_BEHAVIOR);
        out_state->owner = owner;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Check if "myself" parameter */
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETEROUT_MYSELF, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords != 0u) return NMO_ERR_INVALID_FORMAT;
        out_state->is_myself = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Check if setting */
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords != 0u) return NMO_ERR_INVALID_FORMAT;
        out_state->is_setting = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterlocal_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_parameterlocal_state_t *out_state =
        (nmo_parameterlocal_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_parameterlocal_state_t decoded = {0};
    const nmo_allocator_t *allocator =
        out_state->base.buffer_data.allocator.alloc != NULL
            ? &out_state->base.buffer_data.allocator : NULL;
    nmo_status_t result = nmo_array_init(
        &decoded.base.buffer_data, sizeof(uint8_t), 0, allocator);
    if (result != NMO_OK) return result;
    result = nmo_parameterlocal_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded.base.buffer_data);
        return result;
    }
    nmo_array_dispose(&out_state->base.buffer_data);
    *out_state = decoded;
    return NMO_OK;
}

/**
 * @brief Serialize CKParameterLocal state to chunk
 *
 * Reference: reference/src/CKParameterLocal.cpp:119-130
 */
static nmo_status_t nmo_parameterlocal_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_parameterlocal_state_t *in_state = (const nmo_parameterlocal_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = is_file
        ? CK_STATESAVE_PARAMETEROUT_ALL
        : nmo_serialize_context_get_save_flags(context);
    const bool want_value = is_file || ((save_flags & CK_STATESAVE_PARAMETEROUT_VAL) != 0);

    /* Write base state (CKObject when "myself", otherwise CKParameter unless value is skipped) */
    if (in_state->is_myself || !want_value) {
        result = nmo_object_serialize(&in_state->base.base, out_chunk, NULL, context);
    } else {
        result = nmo_parameter_serialize(&in_state->base, out_chunk, NULL, context);
    }
    if (result != NMO_OK) return result;

    if (!is_file && save_flags == 0) {
        return NMO_OK;
    }

    if ((is_file ||
         (save_flags & CK_STATESAVE_PARAMETEROUT_OWNER) != 0) &&
        nmo_ref_serialized_id(&in_state->owner) != NMO_OBJECT_ID_NONE) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_OWNER);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->owner);
        if (result != NMO_OK) return result;
    }

    /* Write "myself" flag if needed */
    if (in_state->is_myself &&
        (is_file || ((save_flags & CK_STATESAVE_PARAMETEROUT_MYSELF) != 0))) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_MYSELF);
        if (result != NMO_OK) return result;
    }

    /* Write setting flag if needed */
    if (in_state->is_setting &&
        (is_file || ((save_flags & CK_STATESAVE_PARAMETEROUT_ISSETTING) != 0))) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterlocal_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_parameterlocal_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_parameterlocal_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterlocal_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterlocal_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterlocal_remap_dependencies");
    }

    nmo_parameterlocal_state_t *state = (nmo_parameterlocal_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base.base, NULL, context));
    NMO_RETURN_IF_ERROR(nmo_parameter_remap_dependencies(&state->base, NULL, context));

    /* Preserve owner and payload fields; normalization is explicit. */
    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_parameterlocal_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterlocal_pre_delete");
    }
    nmo_parameterlocal_state_t *state =
        (nmo_parameterlocal_state_t *)instance;
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

static void nmo_parameterlocal_post_delete(
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

static nmo_status_t nmo_parameterlocal_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t nmo_parameterlocal_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (src == dst) return NMO_OK;
    const nmo_parameterlocal_state_t *s =
        (const nmo_parameterlocal_state_t *)src;
    nmo_parameterlocal_state_t *d = (nmo_parameterlocal_state_t *)dst;
    NMO_RETURN_IF_ERROR(nmo_parameterlocal_validate(s, type, NULL));

    nmo_parameterlocal_state_t copied;
    nmo_status_t result = nmo_parameterlocal_create(
        &copied, NULL, NULL);
    if (result != NMO_OK) return result;
    result = nmo_parameter_vtable.copy(
        &s->base, &copied.base, NULL, arena);
    if (result != NMO_OK) goto fail;

    copied.owner = s->owner;
    copied.is_myself = s->is_myself;
    copied.is_setting = s->is_setting;

    if (s->base.buffer_data.data != NULL &&
        d->base.buffer_data.data == s->base.buffer_data.data) {
        memset(&d->base.buffer_data, 0, sizeof(d->base.buffer_data));
    }
    nmo_parameterlocal_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_parameterlocal_destroy(&copied, NULL, NULL);
    return result;
}

static nmo_status_t nmo_parameterlocal_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_parameterlocal_state_t *state =
        (const nmo_parameterlocal_state_t *)instance;
    return nmo_parameter_vtable.validate(&state->base, NULL, context);
}

static bool nmo_parameterlocal_base_equals(
    const nmo_parameter_state_t *lhs,
    const nmo_parameter_state_t *rhs)
{
    if (lhs->base.visibility_flags != rhs->base.visibility_flags ||
        !nmo_guid_equals(lhs->type_guid, rhs->type_guid) ||
        lhs->mode != rhs->mode ||
        lhs->has_state != rhs->has_state ||
        memcmp(&lhs->object_ref, &rhs->object_ref,
               sizeof(nmo_ref_t)) != 0 ||
        !nmo_guid_equals(lhs->manager_guid, rhs->manager_guid) ||
        lhs->manager_value != rhs->manager_value ||
        lhs->buffer_data.count != rhs->buffer_data.count ||
        (lhs->buffer_data.count > 0 &&
         (lhs->buffer_data.data == NULL || rhs->buffer_data.data == NULL ||
          memcmp(lhs->buffer_data.data, rhs->buffer_data.data,
                 lhs->buffer_data.count) != 0)) ||
        ((lhs->subchunk == NULL) != (rhs->subchunk == NULL))) {
        return false;
    }
    if (lhs->subchunk != NULL) {
        size_t lhs_size = 0;
        size_t rhs_size = 0;
        const void *lhs_data = nmo_chunk_get_data(lhs->subchunk, &lhs_size);
        const void *rhs_data = nmo_chunk_get_data(rhs->subchunk, &rhs_size);
        if (lhs_size != rhs_size ||
            (lhs_size > 0 &&
             (lhs_data == NULL || rhs_data == NULL ||
              memcmp(lhs_data, rhs_data, lhs_size) != 0))) {
            return false;
        }
    }
    return true;
}

static bool nmo_parameterlocal_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_parameterlocal_state_t *lhs =
        (const nmo_parameterlocal_state_t *)a;
    const nmo_parameterlocal_state_t *rhs =
        (const nmo_parameterlocal_state_t *)b;
    return nmo_parameterlocal_base_equals(&lhs->base, &rhs->base) &&
        memcmp(&lhs->owner, &rhs->owner, sizeof(nmo_ref_t)) == 0 &&
        lhs->is_myself == rhs->is_myself &&
        lhs->is_setting == rhs->is_setting;
}

static uint32_t nmo_parameterlocal_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_parameterlocal_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_parameterlocal_state_t *state =
        (const nmo_parameterlocal_state_t *)instance;
    uint32_t hash = 2166136261u;
#define NMO_PARAMETERLOCAL_HASH_FIELD(field) \
    hash = nmo_parameterlocal_hash_bytes(hash, &(field), sizeof(field))
    NMO_PARAMETERLOCAL_HASH_FIELD(state->base.base.visibility_flags);
    NMO_PARAMETERLOCAL_HASH_FIELD(state->base.type_guid);
    NMO_PARAMETERLOCAL_HASH_FIELD(state->base.mode);
    NMO_PARAMETERLOCAL_HASH_FIELD(state->base.has_state);
    NMO_PARAMETERLOCAL_HASH_FIELD(state->base.object_ref);
    NMO_PARAMETERLOCAL_HASH_FIELD(state->base.manager_guid);
    NMO_PARAMETERLOCAL_HASH_FIELD(state->base.manager_value);
    NMO_PARAMETERLOCAL_HASH_FIELD(state->base.buffer_data.count);
#undef NMO_PARAMETERLOCAL_HASH_FIELD
    if (state->base.buffer_data.data != NULL &&
        state->base.buffer_data.count > 0) {
        hash = nmo_parameterlocal_hash_bytes(
            hash, state->base.buffer_data.data,
            state->base.buffer_data.count);
    }
    const uint8_t has_subchunk = state->base.subchunk != NULL;
    hash = nmo_parameterlocal_hash_bytes(
        hash, &has_subchunk, sizeof(has_subchunk));
    if (state->base.subchunk != NULL) {
        size_t chunk_size = 0;
        const void *chunk_data = nmo_chunk_get_data(
            state->base.subchunk, &chunk_size);
        hash = nmo_parameterlocal_hash_bytes(
            hash, &chunk_size, sizeof(chunk_size));
        if (chunk_data != NULL && chunk_size > 0) {
            hash = nmo_parameterlocal_hash_bytes(
                hash, chunk_data, chunk_size);
        }
    }
    hash = nmo_parameterlocal_hash_bytes(
        hash, &state->owner, sizeof(state->owner));
    hash = nmo_parameterlocal_hash_bytes(
        hash, &state->is_myself, sizeof(state->is_myself));
    return nmo_parameterlocal_hash_bytes(
        hash, &state->is_setting, sizeof(state->is_setting));
}

nmo_type_vtable_t nmo_parameterlocal_vtable = {
    .prepare_dependencies = nmo_parameterlocal_prepare_dependencies,
    .remap_dependencies = nmo_parameterlocal_remap_dependencies,
    .pre_delete = nmo_parameterlocal_pre_delete,
    .post_delete = nmo_parameterlocal_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_parameterlocal_create,
        nmo_parameterlocal_destroy,
        nmo_parameterlocal_serialize,
        nmo_parameterlocal_deserialize,
        nmo_parameterlocal_copy,
        nmo_parameterlocal_validate,
        nmo_parameterlocal_equals,
        nmo_parameterlocal_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_parameterlocal_type,
    CKPGUID_PARAMETERLOCAL,
    "CKParameterLocal",
    NMO_CID_PARAMETERLOCAL,
    CKPGUID_PARAMETER,
    nmo_parameterlocal_state_t,
    &nmo_parameterlocal_vtable,
    nmo_parameterlocal_fields)






