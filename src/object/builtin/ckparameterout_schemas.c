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

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameterout_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameterout_state_t, base),
                    sizeof(nmo_parameter_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_VALUE(nmo_parameterout_state_t, owner),
    NMO_FIELD(nmo_parameterout_state_t, destination_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(
        nmo_parameterout_state_t, destination_ids, destination_count),
    NMO_FIELD(nmo_parameterout_state_t, has_destinations, CKPGUID_UINT8)
};

/* =============================================================================
 * CKParameterOut DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterOut state from chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:145-160
 */
static nmo_status_t nmo_parameterout_deserialize_internal(
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
    if (arena == NULL) arena = chunk->arena;

    /* Read base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_parameter_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    nmo_ref_t *destination_ids = NULL;
    uint32_t destination_count = 0;
    uint8_t has_destinations = 0;
    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);

    size_t section_dwords = 0;
    /* Read destinations if present */
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS, &section_dwords);
    if (result == NMO_OK) {
        has_destinations = 1;
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        int32_t count = 0;
        result = nmo_chunk_read_int(chunk, &count);
        if (result != NMO_OK) return result;
        if (count < 0) return NMO_ERR_INVALID_FORMAT;
        if ((size_t)count >
            nmo_chunk_identifier_remaining_dwords(chunk)) {
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
                    NMO_CID_PARAMETER);
            }
        }
        destination_count = (uint32_t)count;
        if (nmo_chunk_get_position(chunk) != section_end) {
            return NMO_ERR_INVALID_FORMAT;
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    out_state->destination_ids = destination_ids;
    out_state->destination_count = destination_count;
    out_state->has_destinations = has_destinations;

    NMO_RETURN_OK();
}

nmo_status_t nmo_parameterout_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_parameterout_state_t *out_state =
        (nmo_parameterout_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_parameterout_state_t decoded = {0};
    const nmo_allocator_t *allocator =
        out_state->base.buffer_data.allocator.alloc != NULL
            ? &out_state->base.buffer_data.allocator : NULL;
    nmo_status_t result = nmo_array_init(
        &decoded.base.buffer_data, sizeof(uint8_t), 0, allocator);
    if (result != NMO_OK) return result;
    result = nmo_parameterout_deserialize_internal(
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
 * @brief Serialize CKParameterOut state to chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:130-142
 */
static nmo_status_t nmo_parameterout_serialize_internal(
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

    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
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

static nmo_status_t nmo_parameterout_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static void nmo_parameterout_set_defaults(void *instance)
{
    nmo_parameterout_state_t *state = instance;
    state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
}

static const nmo_object_state_member_t nmo_parameterout_members[] = {
    NMO_STATE_VALUE(nmo_parameterout_state_t, owner),
    NMO_STATE_VALUE(nmo_parameterout_state_t, destination_count),
    NMO_STATE_COUNTED(nmo_parameterout_state_t, destination_ids,
                      destination_count, nmo_ref_t),
    NMO_STATE_VALUE(nmo_parameterout_state_t, has_destinations)
};

static const nmo_object_state_layout_t nmo_parameterout_layout = {
    .size = sizeof(nmo_parameterout_state_t),
    .base_vtable = &nmo_parameter_vtable,
    .base_size = sizeof(nmo_parameter_state_t),
    .members = nmo_parameterout_members,
    .member_count = sizeof(nmo_parameterout_members) /
        sizeof(nmo_parameterout_members[0]),
    .set_defaults = nmo_parameterout_set_defaults,
    .validate = nmo_parameterout_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(parameterout, nmo_parameterout_layout)

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_VALIDATED(nmo_parameterout)

static nmo_status_t nmo_parameterout_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_parameterout_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_parameter_vtable.validate(
        &s->base, NULL, context));
    if (s->destination_count > INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(s->destination_ids, s->destination_count, "destination_ids");
    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_PREPARE_CHECKED(nmo_parameterout)

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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_type_vtable_t nmo_parameterout_vtable = {
    .prepare_dependencies = nmo_parameterout_prepare_dependencies,
    .remap_dependencies = nmo_parameterout_remap_dependencies,
    .pre_delete = nmo_parameterout_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
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




