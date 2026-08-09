/**
 * @file cksynchro_schemas.c
 * @brief CKSynchroObject/CKStateObject/CKCriticalSectionObject schemas
 */

#include "object/builtin/nmo_synchro_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    synchro,
    nmo_synchro_state_t,
    do {
        nmo_status_t result = nmo_object_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        result = nmo_array_init(
            &state->arrived_ids, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_object_vtable.destroy(&state->base, NULL, context);
            return result;
        }
        result = nmo_array_init(&state->passed_ids, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_array_dispose(&state->arrived_ids);
            nmo_object_vtable.destroy(&state->base, NULL, context);
            return result;
        }
    } while (0),
    do {
        nmo_array_dispose(&state->arrived_ids);
        nmo_array_dispose(&state->passed_ids);
        nmo_object_vtable.destroy(&state->base, NULL, context);
    } while (0))
NMO_DEFINE_OBJECT_LIFECYCLE(
    state,
    nmo_state_state_t,
    do {
        nmo_status_t result = nmo_object_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
    } while (0),
    nmo_object_vtable.destroy(&state->base, NULL, context))
NMO_DEFINE_OBJECT_LIFECYCLE(
    criticalsection,
    nmo_criticalsection_state_t,
    do {
        nmo_status_t result = nmo_object_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
    } while (0),
    nmo_object_vtable.destroy(&state->base, NULL, context))

static void nmo_synchro_dispose_arrays(nmo_synchro_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->arrived_ids);
    nmo_array_dispose(&state->passed_ids);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_synchro_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_synchro_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_synchro_state_t, max_waiters, CKPGUID_INT),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_synchro_state_t, arrived_ids),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_synchro_state_t, passed_ids)
};

static const nmo_type_field_t nmo_state_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_state_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_state_state_t, event_flag, CKPGUID_INT)
};

static const nmo_type_field_t nmo_criticalsection_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_criticalsection_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_VALUE(nmo_criticalsection_state_t, object_in_section)
};

nmo_status_t nmo_state_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_state_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_criticalsection_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_criticalsection_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t deserialize_ckobject_base(
    nmo_object_state_t *out_base,
    nmo_chunk_t *chunk,
    void *context)
{
    return nmo_object_deserialize(out_base, chunk, NULL, context);
}

static nmo_status_t serialize_ckobject_base(
    const nmo_object_state_t *base,
    nmo_chunk_t *chunk,
    void *context)
{
    return nmo_object_serialize(base, chunk, NULL, context);
}

static nmo_status_t nmo_synchro_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static size_t nmo_synchro_identifier_remaining_dwords(
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

static nmo_status_t nmo_synchro_read_ref_array(
    nmo_chunk_t *chunk,
    nmo_array_t *out_refs,
    const nmo_allocator_t *allocator,
    void *context)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;
    if (count > INT32_MAX || count > SIZE_MAX / sizeof(nmo_ref_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (count > nmo_synchro_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    result = nmo_array_init(out_refs, sizeof(nmo_ref_t), count, allocator);
    if (result != NMO_OK) return result;
    nmo_ref_t *refs = NULL;
    result = nmo_array_extend(out_refs, count, (void **)&refs);
    for (size_t i = 0; result == NMO_OK && i < count; ++i) {
        result = nmo_ref_read(chunk, &refs[i]);
        if (result == NMO_OK) {
            nmo_ref_check_class(
                &refs[i],
                (const nmo_object_repository_t *)
                    nmo_deserialize_context_get_repository(context),
                nmo_deserialize_context_get_type_registry(context),
                NMO_CID_BEOBJECT);
        }
    }
    if (result != NMO_OK) {
        nmo_array_dispose(out_refs);
    }
    return result;
}

/* =============================================================================
 * CKSynchroObject
 * ============================================================================= */

static nmo_status_t nmo_synchro_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_synchro_state_t *out_state = (nmo_synchro_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_synchro_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SYNCHRODATA, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        int32_t max_waiters = 0;
        nmo_array_t arrived_ids = {0};
        nmo_array_t passed_ids = {0};
        result = nmo_chunk_read_int(chunk, &max_waiters);
        if (result != NMO_OK) return result;

        const nmo_allocator_t *arrived_allocator =
            out_state->arrived_ids.element_size != 0
                ? &out_state->arrived_ids.allocator : NULL;
        const nmo_allocator_t *passed_allocator =
            out_state->passed_ids.element_size != 0
                ? &out_state->passed_ids.allocator : NULL;
        result = nmo_synchro_read_ref_array(
            chunk, &arrived_ids, arrived_allocator, context);
        if (result != NMO_OK) return result;
        result = nmo_synchro_read_ref_array(
            chunk, &passed_ids, passed_allocator, context);
        if (result != NMO_OK) {
            nmo_array_dispose(&arrived_ids);
            return result;
        }
        if (nmo_chunk_get_position(chunk) > section_end) {
            nmo_array_dispose(&arrived_ids);
            nmo_array_dispose(&passed_ids);
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            nmo_array_dispose(&arrived_ids);
            nmo_array_dispose(&passed_ids);
            return NMO_ERR_INVALID_FORMAT;
        }

        nmo_array_dispose(&out_state->arrived_ids);
        nmo_array_dispose(&out_state->passed_ids);
        out_state->arrived_ids = arrived_ids;
        out_state->passed_ids = passed_ids;
        out_state->max_waiters = max_waiters;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_synchro_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_synchro_state_t *out_state = (nmo_synchro_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_synchro_state_t decoded = {0};
    const nmo_allocator_t *arrived_allocator =
        out_state->arrived_ids.allocator.alloc != NULL
            ? &out_state->arrived_ids.allocator : NULL;
    const nmo_allocator_t *passed_allocator =
        out_state->passed_ids.allocator.alloc != NULL
            ? &out_state->passed_ids.allocator : NULL;
    nmo_status_t result = nmo_array_init(
        &decoded.arrived_ids, sizeof(nmo_ref_t), 0, arrived_allocator);
    if (result != NMO_OK) return result;
    result = nmo_array_init(
        &decoded.passed_ids, sizeof(nmo_ref_t), 0, passed_allocator);
    if (result != NMO_OK) {
        nmo_synchro_dispose_arrays(&decoded);
        return result;
    }
    result = nmo_synchro_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_synchro_dispose_arrays(&decoded);
        return result;
    }
    nmo_synchro_dispose_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_synchro_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_synchro_state_t *in_state = (const nmo_synchro_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_synchro_serialize");
    }

    NMO_RETURN_IF_ERROR(nmo_synchro_validate(in_state, type, context));

    nmo_status_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->max_waiters);
    if (result != NMO_OK) return result;

    result = nmo_ref_write_sequence(
        out_chunk,
        NMO_ARRAY_DATA(nmo_ref_t, &in_state->arrived_ids),
        in_state->arrived_ids.count);
    if (result != NMO_OK) return result;

    result = nmo_ref_write_sequence(
        out_chunk,
        NMO_ARRAY_DATA(nmo_ref_t, &in_state->passed_ids),
        in_state->passed_ids.count);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_synchro_serialize(
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
    nmo_status_t result = nmo_synchro_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* =============================================================================
 * CKSynchroObject FINISH LOADING (PostLoad equivalent)
 * ============================================================================= */

nmo_status_t nmo_synchro_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_synchro_prepare_dependencies");
    }
    return nmo_synchro_validate(instance, type, context);
}

nmo_status_t nmo_synchro_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_synchro_remap_dependencies");
    }

    (void)context;
    return nmo_synchro_prepare_dependencies(instance, type, NULL);
}

/* =============================================================================
 * CKStateObject
 * ============================================================================= */

static nmo_status_t nmo_state_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_state_state_t *out_state = (nmo_state_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_state_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SYNCHRODATA, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
        result = nmo_chunk_read_int(chunk, &out_state->event_flag);
        if (result != NMO_OK) {
            return result;
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_state_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_state_state_t *out_state = (nmo_state_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_state_state_t decoded = *out_state;
    nmo_status_t result = nmo_state_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) return result;
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_state_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_state_state_t *in_state = (const nmo_state_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_state_serialize");
    }

    nmo_status_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result != NMO_OK) return result;

    return nmo_chunk_write_int(out_chunk, in_state->event_flag);
}

nmo_status_t nmo_state_serialize(
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
    nmo_status_t result = nmo_state_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* =============================================================================
 * CKCriticalSectionObject
 * ============================================================================= */

static nmo_status_t nmo_criticalsection_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_criticalsection_state_t *out_state = (nmo_criticalsection_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_criticalsection_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    out_state->object_in_section = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_SYNCHRODATA, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
        nmo_ref_t object_in_section = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &object_in_section);
        if (result != NMO_OK) {
            return result;
        }
        nmo_ref_check_class(
            &object_in_section,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_BEOBJECT);
        out_state->object_in_section = object_in_section;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_criticalsection_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_criticalsection_state_t *out_state =
        (nmo_criticalsection_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_criticalsection_state_t decoded = *out_state;
    nmo_status_t result = nmo_criticalsection_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) return result;
    *out_state = decoded;
    return NMO_OK;
}

static nmo_status_t nmo_criticalsection_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_criticalsection_state_t *in_state = (const nmo_criticalsection_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_criticalsection_serialize");
    }

    nmo_status_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result != NMO_OK) return result;

    return nmo_ref_write(out_chunk, &in_state->object_in_section);
}

nmo_status_t nmo_criticalsection_serialize(
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
    nmo_status_t result = nmo_criticalsection_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

/* =============================================================================
 * CKStateObject FINISH LOADING
 * ============================================================================= */

nmo_status_t nmo_state_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_state_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_state_remap_dependencies");
    }

    nmo_state_state_t *state = (nmo_state_state_t *)instance;
    return nmo_object_default_validate(state, NULL, NULL);
}

/* =============================================================================
 * CKCriticalSectionObject FINISH LOADING
 * ============================================================================= */

nmo_status_t nmo_criticalsection_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_criticalsection_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_criticalsection_remap_dependencies");
    }

    (void)context;
    return nmo_object_default_validate(instance, NULL, NULL);
}

static nmo_status_t nmo_synchro_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_synchro_pre_delete");
    }
    nmo_synchro_state_t *state = (nmo_synchro_state_t *)instance;
    state->arrived_ids.count = 0;
    state->passed_ids.count = 0;
    NMO_RETURN_OK();
}

static void nmo_synchro_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_state_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_state_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_state_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_criticalsection_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_criticalsection_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_criticalsection_post_delete(
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

static nmo_status_t nmo_synchro_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    (void)arena;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (src == dst) return NMO_OK;
    const nmo_synchro_state_t *s = (const nmo_synchro_state_t *)src;
    nmo_synchro_state_t *d = (nmo_synchro_state_t *)dst;
    NMO_RETURN_IF_ERROR(nmo_synchro_validate(s, type, NULL));
    nmo_array_t arrived_ids = {0};
    nmo_array_t passed_ids = {0};
    nmo_status_t result = nmo_array_clone(
        &s->arrived_ids, &arrived_ids, &s->arrived_ids.allocator);
    if (result != NMO_OK) return result;
    result = nmo_array_clone(
        &s->passed_ids, &passed_ids, &s->passed_ids.allocator);
    if (result != NMO_OK) {
        nmo_array_dispose(&arrived_ids);
        return result;
    }
    nmo_synchro_state_t copied = {
        .base = s->base,
        .max_waiters = s->max_waiters,
        .arrived_ids = arrived_ids,
        .passed_ids = passed_ids,
    };
    if (s->arrived_ids.data != NULL &&
        d->arrived_ids.data == s->arrived_ids.data) {
        memset(&d->arrived_ids, 0, sizeof(d->arrived_ids));
    }
    if (s->passed_ids.data != NULL &&
        d->passed_ids.data == s->passed_ids.data) {
        memset(&d->passed_ids, 0, sizeof(d->passed_ids));
    }
    nmo_synchro_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;
}

static nmo_status_t nmo_synchro_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_synchro_state_t *state =
        (const nmo_synchro_state_t *)instance;
    if (state->arrived_ids.element_size != sizeof(nmo_ref_t) ||
        state->passed_ids.element_size != sizeof(nmo_ref_t) ||
        state->arrived_ids.count > INT32_MAX ||
        state->passed_ids.count > INT32_MAX ||
        (state->arrived_ids.count > 0 && state->arrived_ids.data == NULL) ||
        (state->passed_ids.count > 0 && state->passed_ids.data == NULL)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    return nmo_object_vtable.validate(&state->base, NULL, context);
}

static bool nmo_synchro_ref_arrays_equal(
    const nmo_array_t *a,
    const nmo_array_t *b)
{
    if (a->count != b->count) return false;
    const nmo_ref_t *refs_a = NMO_ARRAY_DATA(nmo_ref_t, a);
    const nmo_ref_t *refs_b = NMO_ARRAY_DATA(nmo_ref_t, b);
    for (size_t i = 0; i < a->count; ++i) {
        if (refs_a[i].raw_id != refs_b[i].raw_id ||
            refs_a[i].id != refs_b[i].id ||
            refs_a[i].state != refs_b[i].state) {
            return false;
        }
    }
    return true;
}

static bool nmo_synchro_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_synchro_state_t *sa = (const nmo_synchro_state_t *)a;
    const nmo_synchro_state_t *sb = (const nmo_synchro_state_t *)b;
    if (nmo_synchro_validate(sa, NULL, NULL) != NMO_OK ||
        nmo_synchro_validate(sb, NULL, NULL) != NMO_OK) {
        return false;
    }
    return nmo_object_vtable.equals(&sa->base, &sb->base) &&
        sa->max_waiters == sb->max_waiters &&
        nmo_synchro_ref_arrays_equal(&sa->arrived_ids, &sb->arrived_ids) &&
        nmo_synchro_ref_arrays_equal(&sa->passed_ids, &sb->passed_ids);
}

static uint32_t nmo_synchro_hash_u32(uint32_t hash, uint32_t value)
{
    for (size_t i = 0; i < sizeof(value); ++i) {
        hash ^= (uint8_t)(value >> (i * 8u));
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_synchro_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_synchro_state_t *state =
        (const nmo_synchro_state_t *)instance;
    if (nmo_synchro_validate(state, NULL, NULL) != NMO_OK) return 0;
    uint32_t hash = nmo_object_vtable.hash(&state->base);
    hash = nmo_synchro_hash_u32(hash, (uint32_t)state->max_waiters);
    const nmo_array_t *arrays[] = {
        &state->arrived_ids, &state->passed_ids
    };
    for (size_t array_index = 0; array_index < 2; ++array_index) {
        hash = nmo_synchro_hash_u32(
            hash, (uint32_t)arrays[array_index]->count);
        const nmo_ref_t *refs = NMO_ARRAY_DATA(
            nmo_ref_t, arrays[array_index]);
        for (size_t i = 0; i < arrays[array_index]->count; ++i) {
            hash = nmo_synchro_hash_u32(hash, refs[i].raw_id);
            hash = nmo_synchro_hash_u32(hash, refs[i].id);
            hash = nmo_synchro_hash_u32(hash, (uint32_t)refs[i].state);
        }
    }
    return hash;
}

static nmo_status_t nmo_state_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    (void)arena;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (src != dst) *(nmo_state_state_t *)dst =
        *(const nmo_state_state_t *)src;
    return NMO_OK;
}

static nmo_status_t nmo_state_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_state_state_t *state = instance;
    return nmo_object_vtable.validate(&state->base, NULL, context);
}

static bool nmo_state_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_state_state_t *lhs = a;
    const nmo_state_state_t *rhs = b;
    return nmo_object_vtable.equals(&lhs->base, &rhs->base) &&
        lhs->event_flag == rhs->event_flag;
}

static uint32_t nmo_state_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_state_state_t *state = instance;
    return nmo_synchro_hash_u32(
        nmo_object_vtable.hash(&state->base), (uint32_t)state->event_flag);
}

static nmo_status_t nmo_criticalsection_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    (void)arena;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (src != dst) *(nmo_criticalsection_state_t *)dst =
        *(const nmo_criticalsection_state_t *)src;
    return NMO_OK;
}

static nmo_status_t nmo_criticalsection_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_criticalsection_state_t *state = instance;
    return nmo_object_vtable.validate(&state->base, NULL, context);
}

static bool nmo_criticalsection_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_criticalsection_state_t *lhs = a;
    const nmo_criticalsection_state_t *rhs = b;
    return nmo_object_vtable.equals(&lhs->base, &rhs->base) &&
        lhs->object_in_section.raw_id == rhs->object_in_section.raw_id &&
        lhs->object_in_section.id == rhs->object_in_section.id &&
        lhs->object_in_section.state == rhs->object_in_section.state;
}

static uint32_t nmo_criticalsection_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_criticalsection_state_t *state = instance;
    uint32_t hash = nmo_object_vtable.hash(&state->base);
    hash = nmo_synchro_hash_u32(hash, state->object_in_section.raw_id);
    hash = nmo_synchro_hash_u32(hash, state->object_in_section.id);
    return nmo_synchro_hash_u32(
        hash, (uint32_t)state->object_in_section.state);
}

nmo_type_vtable_t nmo_synchro_vtable = {
    .prepare_dependencies = nmo_synchro_prepare_dependencies,
    .remap_dependencies = nmo_synchro_remap_dependencies,
    .pre_delete = nmo_synchro_pre_delete,
    .post_delete = nmo_synchro_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_synchro_create,
        nmo_synchro_destroy,
        nmo_synchro_serialize,
        nmo_synchro_deserialize,
        nmo_synchro_copy,
        nmo_synchro_validate,
        nmo_synchro_equals,
        nmo_synchro_hash)
};

nmo_type_vtable_t nmo_state_vtable = {
    .prepare_dependencies = nmo_state_prepare_dependencies,
    .remap_dependencies = nmo_state_remap_dependencies,
    .pre_delete = nmo_state_pre_delete,
    .post_delete = nmo_state_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_state_create,
        nmo_state_destroy,
        nmo_state_serialize,
        nmo_state_deserialize,
        nmo_state_copy,
        nmo_state_validate,
        nmo_state_equals,
        nmo_state_hash)
};

nmo_type_vtable_t nmo_criticalsection_vtable = {
    .prepare_dependencies = nmo_criticalsection_prepare_dependencies,
    .remap_dependencies = nmo_criticalsection_remap_dependencies,
    .pre_delete = nmo_criticalsection_pre_delete,
    .post_delete = nmo_criticalsection_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_criticalsection_create,
        nmo_criticalsection_destroy,
        nmo_criticalsection_serialize,
        nmo_criticalsection_deserialize,
        nmo_criticalsection_copy,
        nmo_criticalsection_validate,
        nmo_criticalsection_equals,
        nmo_criticalsection_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_synchro_type,
    CKPGUID_SYNCHRO,
    "CKSynchroObject",
    NMO_CID_SYNCHRO,
    CKPGUID_OBJECT,
    nmo_synchro_state_t,
    &nmo_synchro_vtable,
    nmo_synchro_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_state_type,
    CKPGUID_STATE,
    "CKStateObject",
    NMO_CID_STATE,
    CKPGUID_OBJECT,
    nmo_state_state_t,
    &nmo_state_vtable,
    nmo_state_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_criticalsection_type,
    CKPGUID_CRITICALSECTION,
    "CKCriticalSectionObject",
    NMO_CID_CRITICALSECTION,
    CKPGUID_OBJECT,
    nmo_criticalsection_state_t,
    &nmo_criticalsection_vtable,
    nmo_criticalsection_fields)
