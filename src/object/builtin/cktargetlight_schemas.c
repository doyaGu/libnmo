/**
 * @file cktargetlight_schemas.c
 * @brief CKTargetLight schema implementation
 */

#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_deserialize_context.h"
#include "type/nmo_reflection.h"
#include <string.h>

static void nmo_targetlight_set_defaults(void *instance)
{
    nmo_targetlight_state_t *state = instance;
    state->target = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
}

static const nmo_object_state_member_t nmo_targetlight_members[] = {
    NMO_STATE_VALUE(nmo_targetlight_state_t, has_target),
    NMO_STATE_VALUE(nmo_targetlight_state_t, target)
};

static const nmo_object_state_layout_t nmo_targetlight_layout = {
    .size = sizeof(nmo_targetlight_state_t),
    .base_vtable = &nmo_light_vtable,
    .base_size = sizeof(nmo_light_state_t),
    .members = nmo_targetlight_members,
    .member_count =
        sizeof(nmo_targetlight_members) / sizeof(nmo_targetlight_members[0]),
    .set_defaults = nmo_targetlight_set_defaults,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(targetlight, nmo_targetlight_layout)

static void nmo_targetlight_dispose_base_arrays(nmo_targetlight_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *beobject = &state->base.entity.base.base;
    nmo_array_dispose(&beobject->scripts);
    nmo_array_dispose(&beobject->attributes);
    nmo_array_dispose(&beobject->legacy_attributes);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_targetlight_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_targetlight_state_t, base),
                    sizeof(nmo_light_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_targetlight_state_t, has_target, CKPGUID_UINT8),
    NMO_FIELD_REF_VALUE(nmo_targetlight_state_t, target)
};

static nmo_status_t nmo_targetlight_deserialize_internal(
    nmo_targetlight_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_targetlight_deserialize");
    }

    nmo_status_t result = nmo_light_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->has_target = 0;
    out_state->target = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_TLIGHTTARGET, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
        nmo_ref_t target = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &target);
        if (result != NMO_OK) {
            return result;
        }
        nmo_ref_check_class(
            &target,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_3DENTITY);
        out_state->target = target;
        out_state->has_target = target.state != NMO_REF_NONE;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_PREPARE_DEFAULT(nmo_targetlight)

nmo_status_t nmo_targetlight_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_targetlight_remap_dependencies");
    }

    nmo_targetlight_state_t *state = (nmo_targetlight_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_light_remap_dependencies(&state->base, NULL, context));

    /* Preserve target section presence and unresolved ID. */
    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_targetlight_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_targetlight_state_t *state = instance;
    return nmo_light_vtable.validate(&state->base, NULL, context);
}

nmo_type_vtable_t nmo_targetlight_vtable = {
    .prepare_dependencies = nmo_targetlight_prepare_dependencies,
    .remap_dependencies = nmo_targetlight_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_targetlight_create,
        nmo_targetlight_destroy,
        nmo_targetlight_serialize,
        nmo_targetlight_deserialize,
        nmo_targetlight_copy,
        nmo_targetlight_validate,
        nmo_targetlight_equals,
        nmo_targetlight_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_targetlight_type,
    CKPGUID_TARGETLIGHT,
    "CKTargetLight",
    NMO_CID_TARGETLIGHT,
    CKPGUID_LIGHT,
    nmo_targetlight_state_t,
    &nmo_targetlight_vtable,
    nmo_targetlight_fields)

static nmo_status_t nmo_targetlight_serialize_internal(
    const nmo_targetlight_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_targetlight_serialize");
    }

    nmo_status_t result = nmo_light_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_TLIGHTONLY) == 0) {
            return NMO_OK;
        }
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_TLIGHTTARGET);
    if (result != NMO_OK) return result;
    result = nmo_ref_write(out_chunk, &in_state->target);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_targetlight_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_targetlight_state_t *out_state = (nmo_targetlight_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_targetlight_state_t decoded;
    nmo_status_t result = nmo_targetlight_create(&decoded, type, context);
    if (result != NMO_OK) return result;

    result = nmo_targetlight_deserialize_internal(&decoded, chunk, context);
    if (result != NMO_OK) {
        nmo_targetlight_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_targetlight_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_targetlight_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
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

    nmo_status_t result = nmo_targetlight_serialize_internal(
        (const nmo_targetlight_state_t *)instance, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}
