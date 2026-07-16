/**
 * @file ckparameterout_schemas.c
 * @brief CKParameterOut schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterOut.
 *
 * Based on official Virtools SDK (reference/src/CKParameterOut.cpp:120-160).
 */

#include "object/builtin/nmo_parameterout_schemas.h"
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
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(parameterout, nmo_parameterout_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameterout_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameterout_state_t, base),
                    sizeof(nmo_parameter_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF(nmo_parameterout_state_t, owner),
    NMO_FIELD(nmo_parameterout_state_t, destination_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(
        nmo_parameterout_state_t, destination_ids, destination_count)
};

/* =============================================================================
 * CKParameterOut DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterOut state from chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:145-160
 */
nmo_status_t nmo_parameterout_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_parameterout_state_t *out_state = (nmo_parameterout_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Read base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_parameter_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    nmo_ref_t owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_ref_t *destination_ids = NULL;
    uint32_t destination_count = 0;
    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);

    if (nmo_chunk_seek_identifier(
            chunk, CK_STATESAVE_PARAMETEROUT_OWNER) == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &owner));
        nmo_ref_check_class(&owner, repository, types, NMO_CID_BEHAVIOR);
    }

    /* Read destinations if present */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS) == NMO_OK) {
        int32_t count = 0;
        nmo_status_t result = nmo_chunk_read_int(chunk, &count);
        if (result != NMO_OK) return result;
        if (count < 0) return NMO_ERR_INVALID_FORMAT;
        if (!nmo_chunk_has_read_capacity(chunk, (size_t)count)) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if ((size_t)count > SIZE_MAX / sizeof(nmo_ref_t)) {
            return NMO_ERR_INVALID_FORMAT;
        }
        if (count > 0) {
            destination_ids = (nmo_ref_t *)nmo_arena_alloc(
                arena, (size_t)count * sizeof(nmo_ref_t),
                _Alignof(nmo_ref_t));
            if (destination_ids == NULL) return NMO_ERR_NOMEM;

            for (int32_t i = 0; i < count; i++) {
                NMO_RETURN_IF_ERROR(nmo_ref_read(
                    chunk, &destination_ids[i]));
                nmo_ref_check_class(
                    &destination_ids[i], repository, types,
                    NMO_CID_PARAMETERIN);
            }
        }
        destination_count = (uint32_t)count;
    }

    out_state->owner = owner;
    out_state->destination_ids = destination_ids;
    out_state->destination_count = destination_count;

    NMO_RETURN_OK();
}

/**
 * @brief Serialize CKParameterOut state to chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:130-142
 */
nmo_status_t nmo_parameterout_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_parameterout_state_t *in_state = (const nmo_parameterout_state_t *)instance;
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
    const bool want_destinations = is_file ||
        ((save_flags & CK_STATESAVE_PARAMETEROUT_DESTINATIONS) != 0);
    if (want_destinations && in_state->destination_count > 0 &&
        in_state->destination_ids == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (want_destinations && in_state->destination_count > INT32_MAX) {
        return NMO_ERR_INVALID_FORMAT;
    }

    /* Write base state (CKParameter when saving value, otherwise CKObject) */
    if (want_value) {
        result = nmo_parameter_serialize(&in_state->base, out_chunk, NULL, context);
    } else {
        result = nmo_object_serialize(&in_state->base.base, out_chunk, NULL, context);
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

    /* Write destinations if any */
    if (want_destinations && in_state->destination_count > 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->destination_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->destination_count; i++) {
            result = nmo_ref_write(
                out_chunk, &in_state->destination_ids[i]);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_parameterout_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_parameterout_state_t *s = src;
    nmo_parameterout_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.buffer_data, &d->base.buffer_data,
                                        &s->base.buffer_data.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &d->base.subchunk, s->base.subchunk));
    return nmo_object_copy_array(arena, (void **)&d->destination_ids,
                                 s->destination_ids, sizeof(nmo_ref_t),
                                 s->destination_count);
}

static nmo_status_t nmo_parameterout_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_parameterout_state_t *s = instance;
    size_t buffer_bytes = nmo_array_size(&s->base.buffer_data) *
                          nmo_array_element_size(&s->base.buffer_data);
    NMO_VALIDATE_BYTES(nmo_array_data(&s->base.buffer_data), buffer_bytes, "buffer_data");
    NMO_VALIDATE_COUNT(s->destination_ids, s->destination_count, "destination_ids");
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterout_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterout_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterout_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterout_remap_dependencies");
    }

    nmo_parameterout_state_t *state = (nmo_parameterout_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base.base, NULL, context));
    NMO_RETURN_IF_ERROR(nmo_parameter_remap_dependencies(&state->base, NULL, context));

    if (state->destination_count > 0 && state->destination_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "ParameterOut destination_ids missing");
    }

    /* Preserve unresolved destinations, duplicates, and owner IDs. */
    return nmo_parameterout_validate(state, NULL, NULL);
}

static nmo_status_t nmo_parameterout_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameterout_pre_delete");
    }
    nmo_parameterout_state_t *state =
        (nmo_parameterout_state_t *)instance;
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->destination_ids = NULL;
    state->destination_count = 0;
    NMO_RETURN_OK();
}

static void nmo_parameterout_post_delete(
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

static bool nmo_parameterout_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_parameterout_state_t *lhs =
        (const nmo_parameterout_state_t *)a;
    const nmo_parameterout_state_t *rhs =
        (const nmo_parameterout_state_t *)b;
    if (lhs->destination_count != rhs->destination_count ||
        (lhs->destination_count > 0 &&
         (lhs->destination_ids == NULL || rhs->destination_ids == NULL))) {
        return false;
    }
    nmo_parameterout_state_t lhs_value = *lhs;
    nmo_parameterout_state_t rhs_value = *rhs;
    lhs_value.destination_ids = NULL;
    rhs_value.destination_ids = NULL;
    if (memcmp(&lhs_value, &rhs_value, sizeof(lhs_value)) != 0) {
        return false;
    }
    return lhs->destination_count == 0 || memcmp(
        lhs->destination_ids, rhs->destination_ids,
        (size_t)lhs->destination_count * sizeof(nmo_ref_t)) == 0;
}

static uint32_t nmo_parameterout_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_parameterout_state_t *state =
        (const nmo_parameterout_state_t *)instance;
    nmo_parameterout_state_t value = *state;
    value.destination_ids = NULL;
    uint32_t hash = (uint32_t)nmo_hash_fnv1a(&value, sizeof(value));
    if (state->destination_ids != NULL && state->destination_count > 0) {
        hash ^= (uint32_t)nmo_hash_fnv1a(
            state->destination_ids,
            (size_t)state->destination_count * sizeof(nmo_ref_t));
    }
    return hash;
}

nmo_type_vtable_t nmo_parameterout_vtable = {
    .prepare_dependencies = nmo_parameterout_prepare_dependencies,
    .remap_dependencies = nmo_parameterout_remap_dependencies,
    .pre_delete = nmo_parameterout_pre_delete,
    .post_delete = nmo_parameterout_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_parameterout_create,
        nmo_parameterout_destroy,
        nmo_parameterout_serialize,
        nmo_parameterout_deserialize,
        nmo_parameterout_copy,
        nmo_parameterout_validate,
        nmo_parameterout_equals,
        nmo_parameterout_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_parameterout_type,
    CKPGUID_PARAMETEROUT,
    "CKParameterOut",
    NMO_CID_PARAMETEROUT,
    CKPGUID_PARAMETER,
    nmo_parameterout_state_t,
    &nmo_parameterout_vtable,
    nmo_parameterout_fields)




