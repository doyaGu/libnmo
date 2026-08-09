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
#include "type/nmo_type_query.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    parameterout,
    nmo_parameterout_state_t,
    do {
        NMO_RETURN_IF_ERROR(nmo_parameter_vtable.create(
            &state->base, NULL, context));
        state->owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    } while (0),
    nmo_parameter_vtable.destroy(&state->base, NULL, context))

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
    NMO_FIELD(nmo_parameterout_state_t, has_owner, CKPGUID_UINT8),
    NMO_FIELD(nmo_parameterout_state_t, has_destinations, CKPGUID_UINT8)
};

/* =============================================================================
 * CKParameterOut DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

static size_t nmo_parameterout_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

static void nmo_parameterout_check_owner(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository,
    const nmo_type_registry_t *types)
{
    if (ref == NULL || ref->state != NMO_REF_RESOLVED ||
        repository == NULL || types == NULL) {
        return;
    }
    const nmo_object_t *target =
        nmo_object_repository_find_by_id(repository, ref->id);
    if (target != NULL &&
        !nmo_type_query_object_is_derived_from_class(
            types, target, NMO_CID_BEHAVIOR) &&
        !nmo_type_query_object_is_derived_from_class(
            types, target, NMO_CID_PARAMETEROPERATION)) {
        ref->state = NMO_REF_CLASS_MISMATCH;
    }
}

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

    nmo_ref_t owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_ref_t *destination_ids = NULL;
    uint32_t destination_count = 0;
    uint8_t has_owner = 0;
    uint8_t has_destinations = 0;
    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETEROUT_OWNER, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &owner));
        nmo_parameterout_check_owner(&owner, repository, types);
        has_owner = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Read destinations if present */
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS, &section_dwords);
    if (result == NMO_OK) {
        has_destinations = 1;
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        int32_t count = 0;
        nmo_status_t result = nmo_chunk_read_int(chunk, &count);
        if (result != NMO_OK) return result;
        if (count < 0) return NMO_ERR_INVALID_FORMAT;
        if ((size_t)count >
            nmo_parameterout_identifier_remaining_dwords(chunk)) {
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

    out_state->owner = owner;
    out_state->destination_ids = destination_ids;
    out_state->destination_count = destination_count;
    out_state->has_owner = has_owner;
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

    const bool has_owner = in_state->has_owner ||
        nmo_ref_serialized_id(&in_state->owner) != NMO_OBJECT_ID_NONE;
    if (!is_file &&
        (save_flags & CK_STATESAVE_PARAMETEROUT_OWNER) != 0 &&
        has_owner) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_OWNER);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->owner);
        if (result != NMO_OK) return result;
    }

    /* Write destinations if any */
    if (want_destinations &&
        (in_state->has_destinations || in_state->destination_count > 0)) {
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

nmo_status_t nmo_parameterout_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_parameterout_validate(instance, type, context));
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_parameterout_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static nmo_status_t nmo_parameterout_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (src == dst) return NMO_OK;
    const nmo_parameterout_state_t *s = src;
    nmo_parameterout_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_parameterout_validate(s, type, NULL));

    nmo_parameterout_state_t copied;
    nmo_status_t result = nmo_parameterout_create(
        &copied, NULL, NULL);
    if (result != NMO_OK) return result;
    result = nmo_parameter_vtable.copy(
        &s->base, &copied.base, NULL, arena);
    if (result != NMO_OK) goto fail;

    copied.owner = s->owner;
    copied.destination_count = s->destination_count;
    copied.has_owner = s->has_owner;
    copied.has_destinations = s->has_destinations;
    result = nmo_object_copy_array(
        arena, (void **)&copied.destination_ids, s->destination_ids,
        sizeof(nmo_ref_t), s->destination_count);
    if (result != NMO_OK) goto fail;

    if (s->base.buffer_data.data != NULL &&
        d->base.buffer_data.data == s->base.buffer_data.data) {
        memset(&d->base.buffer_data, 0, sizeof(d->base.buffer_data));
    }
    nmo_parameterout_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_parameterout_destroy(&copied, NULL, NULL);
    return result;
}

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
    if (lhs->base.base.visibility_flags != rhs->base.base.visibility_flags ||
        !nmo_guid_equals(lhs->base.type_guid, rhs->base.type_guid) ||
        lhs->base.mode != rhs->base.mode ||
        lhs->base.has_state != rhs->base.has_state ||
        memcmp(&lhs->base.object_ref, &rhs->base.object_ref,
               sizeof(nmo_ref_t)) != 0 ||
        !nmo_guid_equals(lhs->base.manager_guid, rhs->base.manager_guid) ||
        lhs->base.manager_value != rhs->base.manager_value ||
        lhs->base.buffer_data.count != rhs->base.buffer_data.count ||
        (lhs->base.buffer_data.count > 0 &&
         (lhs->base.buffer_data.data == NULL ||
          rhs->base.buffer_data.data == NULL ||
          memcmp(lhs->base.buffer_data.data, rhs->base.buffer_data.data,
                 lhs->base.buffer_data.count) != 0)) ||
        memcmp(&lhs->owner, &rhs->owner, sizeof(nmo_ref_t)) != 0 ||
        lhs->destination_count != rhs->destination_count ||
        lhs->has_owner != rhs->has_owner ||
        lhs->has_destinations != rhs->has_destinations ||
        (lhs->destination_count > 0 &&
         (lhs->destination_ids == NULL || rhs->destination_ids == NULL))) {
        return false;
    }
    if ((lhs->base.subchunk == NULL) != (rhs->base.subchunk == NULL)) {
        return false;
    }
    if (lhs->base.subchunk != NULL) {
        size_t lhs_size = 0;
        size_t rhs_size = 0;
        const void *lhs_data = nmo_chunk_get_data(
            lhs->base.subchunk, &lhs_size);
        const void *rhs_data = nmo_chunk_get_data(
            rhs->base.subchunk, &rhs_size);
        if (lhs_size != rhs_size ||
            (lhs_size > 0 &&
             (lhs_data == NULL || rhs_data == NULL ||
              memcmp(lhs_data, rhs_data, lhs_size) != 0))) {
            return false;
        }
    }
    return lhs->destination_count == 0 || memcmp(
        lhs->destination_ids, rhs->destination_ids,
        (size_t)lhs->destination_count * sizeof(nmo_ref_t)) == 0;
}

static uint32_t nmo_parameterout_hash_bytes(
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

static uint32_t nmo_parameterout_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_parameterout_state_t *state =
        (const nmo_parameterout_state_t *)instance;
    uint32_t hash = 2166136261u;
    hash = nmo_parameterout_hash_bytes(
        hash, &state->base.base.visibility_flags,
        sizeof(state->base.base.visibility_flags));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->base.type_guid, sizeof(state->base.type_guid));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->base.mode, sizeof(state->base.mode));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->base.has_state, sizeof(state->base.has_state));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->base.object_ref, sizeof(state->base.object_ref));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->base.manager_guid, sizeof(state->base.manager_guid));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->base.manager_value, sizeof(state->base.manager_value));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->base.buffer_data.count,
        sizeof(state->base.buffer_data.count));
    if (state->base.buffer_data.data != NULL &&
        state->base.buffer_data.count > 0) {
        hash = nmo_parameterout_hash_bytes(
            hash, state->base.buffer_data.data,
            state->base.buffer_data.count);
    }
    if (state->base.subchunk != NULL) {
        size_t chunk_size = 0;
        const void *chunk_data = nmo_chunk_get_data(
            state->base.subchunk, &chunk_size);
        hash = nmo_parameterout_hash_bytes(
            hash, &chunk_size, sizeof(chunk_size));
        if (chunk_data != NULL && chunk_size > 0) {
            hash = nmo_parameterout_hash_bytes(
                hash, chunk_data, chunk_size);
        }
    }
    hash = nmo_parameterout_hash_bytes(
        hash, &state->owner, sizeof(state->owner));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->destination_count,
        sizeof(state->destination_count));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->has_owner, sizeof(state->has_owner));
    hash = nmo_parameterout_hash_bytes(
        hash, &state->has_destinations,
        sizeof(state->has_destinations));
    if (state->destination_ids != NULL && state->destination_count > 0) {
        hash = nmo_parameterout_hash_bytes(
            hash, state->destination_ids,
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




