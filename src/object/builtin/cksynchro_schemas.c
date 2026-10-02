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

static const nmo_object_state_member_t nmo_synchro_members[] = {
    NMO_STATE_VALUE(nmo_synchro_state_t, max_waiters),
    NMO_STATE_ARRAY(nmo_synchro_state_t, arrived_ids, nmo_ref_t),
    NMO_STATE_ARRAY(nmo_synchro_state_t, passed_ids, nmo_ref_t)
};

static const nmo_object_state_layout_t nmo_synchro_layout = {
    .size = sizeof(nmo_synchro_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nmo_synchro_members,
    .member_count = sizeof(nmo_synchro_members) / sizeof(nmo_synchro_members[0]),
    .validate = nmo_synchro_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(synchro, nmo_synchro_layout)

static const nmo_object_state_member_t nmo_state_members[] = {
    NMO_STATE_VALUE(nmo_state_state_t, event_flag)
};

static const nmo_object_state_layout_t nmo_state_layout = {
    .size = sizeof(nmo_state_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nmo_state_members,
    .member_count = sizeof(nmo_state_members) / sizeof(nmo_state_members[0]),
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(state, nmo_state_layout)

static const nmo_object_state_member_t nmo_criticalsection_members[] = {
    NMO_STATE_VALUE(nmo_criticalsection_state_t, object_in_section)
};

static const nmo_object_state_layout_t nmo_criticalsection_layout = {
    .size = sizeof(nmo_criticalsection_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nmo_criticalsection_members,
    .member_count = sizeof(nmo_criticalsection_members) /
        sizeof(nmo_criticalsection_members[0]),
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(criticalsection, nmo_criticalsection_layout)

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
    if (count > nmo_chunk_identifier_remaining_dwords(chunk)) {
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

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_synchro)

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

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_state)

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

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_criticalsection)

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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

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

NMO_DEFINE_OBJECT_VALIDATE_BASE(nmo_state, nmo_state_state_t, base, nmo_object_vtable)

NMO_DEFINE_OBJECT_VALIDATE_BASE(nmo_criticalsection, nmo_criticalsection_state_t, base, nmo_object_vtable)

nmo_type_vtable_t nmo_synchro_vtable = {
    .prepare_dependencies = nmo_synchro_prepare_dependencies,
    .remap_dependencies = nmo_synchro_remap_dependencies,
    .pre_delete = nmo_synchro_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
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
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
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
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
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
