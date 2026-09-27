/**
 * @file ckplace_schemas.c
 * @brief CKPlace schema implementation
 */

#include "object/builtin/nmo_place_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_deserialize_context.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include <stdint.h>
#include <string.h>

static nmo_status_t nmo_place_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static const nmo_object_state_member_t nmo_place_members[] = {
    NMO_STATE_VALUE(nmo_place_state_t, has_camera),
    NMO_STATE_VALUE(nmo_place_state_t, camera),
    NMO_STATE_VALUE(nmo_place_state_t, has_level),
    NMO_STATE_VALUE(nmo_place_state_t, level),
    NMO_STATE_VALUE(nmo_place_state_t, has_portals),
    NMO_STATE_ARRAY(nmo_place_state_t, portals, nmo_place_portal_entry_t),
    NMO_STATE_VALUE(nmo_place_state_t, has_references),
    NMO_STATE_ARRAY(nmo_place_state_t, references, nmo_ref_t)
};

static const size_t nmo_place_base_arrays[] = {
    NMO_BEOBJECT_STATE_ARRAY_OFFSETS
};

static const nmo_object_state_layout_t nmo_place_layout = {
    .size = sizeof(nmo_place_state_t),
    .base_vtable = &nmo_3dentity_vtable,
    .members = nmo_place_members,
    .member_count = sizeof(nmo_place_members) / sizeof(nmo_place_members[0]),
    .base_arrays = nmo_place_base_arrays,
    .base_array_count =
        sizeof(nmo_place_base_arrays) / sizeof(nmo_place_base_arrays[0]),
    .validate = nmo_place_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(place, nmo_place_layout)

static size_t nmo_place_identifier_remaining_dwords(
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

static nmo_status_t nmo_place_restore_read_position(
    nmo_chunk_t *chunk,
    size_t position,
    nmo_status_t error)
{
    nmo_status_t result = nmo_chunk_goto(chunk, position);
    return result == NMO_OK ? error : result;
}

static nmo_status_t nmo_place_deserialize_internal(
    nmo_place_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_place_deserialize");
    }

    nmo_status_t result = nmo_beobject_deserialize(&out_state->base.base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const nmo_deserialize_context_t *deser_ctx =
        nmo_deserialize_context_get(context);
    const int file_mode =
        ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (deser_ctx != NULL &&
         (deser_ctx->flags & NMO_DESER_FLAG_FILE_MODE) != 0);
    if (!file_mode) {
        NMO_RETURN_OK();
    }

    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PLACECAMERA, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
        nmo_ref_t camera = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &camera));
        nmo_ref_check_class(
            &camera,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_CAMERA);
        out_state->camera = camera;
        out_state->has_camera = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PLACELEVEL, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
        nmo_ref_t level = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &level));
        nmo_ref_check_class(
            &level,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_LEVEL);
        out_state->level = level;
        out_state->has_level = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PLACEPORTALS, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        if (count < 0) return NMO_ERR_INVALID_FORMAT;
        if ((size_t)count >
            nmo_place_identifier_remaining_dwords(chunk) / 2u) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        nmo_array_t portals = {0};
        const nmo_allocator_t *allocator =
            out_state->portals.allocator.alloc != NULL
                ? &out_state->portals.allocator : NULL;
        result = nmo_array_init(
            &portals, sizeof(nmo_place_portal_entry_t), (size_t)count, allocator);
        if (result != NMO_OK) return result;
        nmo_place_portal_entry_t *entries = NULL;
        result = nmo_array_extend(&portals, (size_t)count, (void **)&entries);
        const nmo_object_repository_t *repository =
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        for (uint32_t i = 0; result == NMO_OK && i < (uint32_t)count; ++i) {
            entries[i].place = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            entries[i].portal = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
            result = nmo_ref_read(chunk, &entries[i].place);
            if (result == NMO_OK) {
                result = nmo_ref_read(chunk, &entries[i].portal);
            }
            if (result == NMO_OK) {
                nmo_ref_check_class(
                    &entries[i].place, repository, types, NMO_CID_PLACE);
                nmo_ref_check_class(
                    &entries[i].portal, repository, types, NMO_CID_3DENTITY);
            }
        }
        if (result != NMO_OK) {
            nmo_array_dispose(&portals);
            return result;
        }
        if (nmo_chunk_get_position(chunk) != section_end) {
            nmo_array_dispose(&portals);
            return NMO_ERR_INVALID_FORMAT;
        }
        NMO_RETURN_IF_ERROR(nmo_array_swap(&out_state->portals, &portals));
        nmo_array_dispose(&portals);
        out_state->has_portals = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_PLACEREFERENCES, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        const size_t sequence_start = nmo_chunk_get_position(chunk);
        size_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_object_sequence_start(
            chunk, &count));
        if (count > (size_t)INT32_MAX ||
            count > SIZE_MAX / sizeof(nmo_ref_t)) {
            return nmo_place_restore_read_position(
                chunk, sequence_start, NMO_ERR_INVALID_FORMAT);
        }
        if (count > nmo_place_identifier_remaining_dwords(chunk)) {
            return nmo_place_restore_read_position(
                chunk, sequence_start, NMO_ERR_TRUNCATED_CHUNK);
        }
        nmo_array_t references = {0};
        const nmo_allocator_t *allocator =
            out_state->references.allocator.alloc != NULL
                ? &out_state->references.allocator : NULL;
        result = nmo_array_init(
            &references, sizeof(nmo_ref_t), count, allocator);
        if (result != NMO_OK) {
            return nmo_place_restore_read_position(
                chunk, sequence_start, result);
        }
        nmo_ref_t *dest = NULL;
        result = nmo_array_extend(&references, count, (void **)&dest);
        if (result != NMO_OK) {
            nmo_array_dispose(&references);
            return nmo_place_restore_read_position(
                chunk, sequence_start, result);
        }
        for (size_t i = 0; result == NMO_OK && i < count; ++i) {
            result = nmo_ref_read(chunk, &dest[i]);
        }
        if (result != NMO_OK) {
            nmo_array_dispose(&references);
            return nmo_place_restore_read_position(
                chunk, sequence_start, result);
        }
        const nmo_object_repository_t *repository =
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        for (size_t i = 0; i < count; ++i) {
            nmo_ref_check_class(
                &dest[i], repository, types, NMO_CID_3DENTITY);
        }
        if (nmo_chunk_get_position(chunk) != section_end) {
            nmo_array_dispose(&references);
            return NMO_ERR_INVALID_FORMAT;
        }
        NMO_RETURN_IF_ERROR(nmo_array_swap(&out_state->references, &references));
        nmo_array_dispose(&references);
        out_state->has_references = 1;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_place_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_place_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_place_state_t, has_camera, CKPGUID_UINT8),
    NMO_FIELD_REF_VALUE(nmo_place_state_t, camera),
    NMO_FIELD(nmo_place_state_t, has_level, CKPGUID_UINT8),
    NMO_FIELD_REF_VALUE(nmo_place_state_t, level),
    NMO_FIELD(nmo_place_state_t, has_portals, CKPGUID_UINT8),
    NMO_FIELD_ARRAY(nmo_place_state_t, portals, NMO_GUID_STRUCT_CKPLACEPORTALENTRY),
    NMO_FIELD(nmo_place_state_t, has_references, CKPGUID_UINT8),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_place_state_t, references)
};

static nmo_status_t nmo_place_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_place_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_3dentity_vtable.validate(
        &s->base, NULL, context));
    if (s->has_camera > 1u || s->has_level > 1u ||
        s->has_portals > 1u || s->has_references > 1u) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(s->portals.data, s->portals.count, "portals");
    NMO_VALIDATE_COUNT(s->references.data, s->references.count, "references");
    if (s->portals.element_size != sizeof(nmo_place_portal_entry_t) ||
        s->portals.count > INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->references.element_size != sizeof(nmo_ref_t) ||
        s->references.count > INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_place_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_place_remap_dependencies");
    }

    nmo_place_state_t *state = (nmo_place_state_t *)instance;
    nmo_status_t result = nmo_beobject_remap_dependencies(&state->base.base, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (state->portals.count > 0 && state->portals.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Place portals missing");
    }
    if (state->references.count > 0 && state->references.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Place references missing");
    }

    /* Reference validation is non-destructive; normalize is explicit. */
    return nmo_place_validate(state, NULL, NULL);
}

nmo_status_t nmo_place_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_place_validate(instance, type, context);
}

static nmo_status_t nmo_place_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_place_pre_delete");
    }
    nmo_place_state_t *state = (nmo_place_state_t *)instance;
    state->has_camera = 0;
    state->camera = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->has_level = 0;
    state->level = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->has_portals = 0;
    state->portals.count = 0;
    state->has_references = 0;
    state->references.count = 0;
    NMO_RETURN_OK();
}

static void nmo_place_post_delete(
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

nmo_type_vtable_t nmo_place_vtable = {
    .prepare_dependencies = nmo_place_prepare_dependencies,
    .remap_dependencies = nmo_place_remap_dependencies,
    .pre_delete = nmo_place_pre_delete,
    .post_delete = nmo_place_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_place_create,
        nmo_place_destroy,
        nmo_place_serialize,
        nmo_place_deserialize,
        nmo_place_copy,
        nmo_place_validate,
        nmo_place_equals,
        nmo_place_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_place_type,
    CKPGUID_PLACE,
    "CKPlace",
    NMO_CID_PLACE,
    CKPGUID_3DENTITY,
    nmo_place_state_t,
    &nmo_place_vtable,
    nmo_place_fields)

static nmo_status_t nmo_place_serialize_internal(
    const nmo_place_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_place_serialize");
    }

    nmo_status_t result = nmo_beobject_serialize(&in_state->base.base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);

    if (!is_file && save_flags == 0) {
        NMO_RETURN_OK();
    }

    const bool use_flags = (!is_file) || (save_flags != 0);
    const bool write_camera = use_flags ? ((save_flags & CK_STATESAVE_PLACECAMERA) != 0) : true;
    const bool write_level = use_flags ? ((save_flags & CK_STATESAVE_PLACELEVEL) != 0) : true;
    const bool write_portals = use_flags ? ((save_flags & CK_STATESAVE_PLACEPORTALS) != 0) : true;

    if (write_camera && (in_state->has_camera ||
                         in_state->camera.state != NMO_REF_NONE)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACECAMERA);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->camera);
        if (result != NMO_OK) return result;
    }

    if (is_file && write_level && (in_state->has_level ||
                                   in_state->level.state != NMO_REF_NONE)) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACELEVEL);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->level);
        if (result != NMO_OK) return result;
    }

    if (write_portals &&
        (in_state->has_portals || in_state->portals.count > 0)) {
        if ((in_state->portals.count > 0 &&
             in_state->portals.data == NULL) ||
            in_state->portals.element_size != sizeof(nmo_place_portal_entry_t) ||
            in_state->portals.count > INT32_MAX) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACEPORTALS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->portals.count);
        if (result != NMO_OK) return result;

        const nmo_place_portal_entry_t *portals = NMO_ARRAY_DATA(
            nmo_place_portal_entry_t, &in_state->portals);
        for (uint32_t i = 0; i < in_state->portals.count; ++i) {
            result = nmo_ref_write(out_chunk, &portals[i].place);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &portals[i].portal);
            if (result != NMO_OK) return result;
        }
    }

    const bool write_references = use_flags
        ? ((save_flags & CK_STATESAVE_PLACEREFERENCES) != 0) : true;
    if (write_references &&
        (in_state->has_references || in_state->references.count > 0)) {
        if ((in_state->references.count > 0 &&
             in_state->references.data == NULL) ||
            in_state->references.element_size != sizeof(nmo_ref_t) ||
            in_state->references.count > INT32_MAX) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        result = nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_PLACEREFERENCES);
        if (result != NMO_OK) return result;
        result = nmo_ref_write_sequence(
            out_chunk,
            NMO_ARRAY_DATA(nmo_ref_t, &in_state->references),
            in_state->references.count);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_place_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_place_state_t *out_state = (nmo_place_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_place_state_t decoded;
    nmo_status_t result = nmo_place_create(&decoded, NULL, context);
    if (result != NMO_OK) return result;
    nmo_beobject_state_t *old_base = &out_state->base.base.base;
    nmo_beobject_state_t *new_base = &decoded.base.base.base;
    if (old_base->scripts.allocator.alloc != NULL) {
        new_base->scripts.allocator = old_base->scripts.allocator;
    }
    if (old_base->attributes.allocator.alloc != NULL) {
        new_base->attributes.allocator = old_base->attributes.allocator;
    }
    if (old_base->legacy_attributes.allocator.alloc != NULL) {
        new_base->legacy_attributes.allocator =
            old_base->legacy_attributes.allocator;
    }
    const nmo_allocator_t *portal_allocator =
        out_state->portals.allocator.alloc != NULL
            ? &out_state->portals.allocator : NULL;
    const nmo_allocator_t *reference_allocator =
        out_state->references.allocator.alloc != NULL
            ? &out_state->references.allocator : NULL;
    decoded.portals.allocator = portal_allocator != NULL
        ? *portal_allocator : nmo_allocator_default();
    decoded.references.allocator = reference_allocator != NULL
        ? *reference_allocator : nmo_allocator_default();
    result = nmo_place_deserialize_internal(&decoded, chunk, context);
    if (result != NMO_OK) {
        nmo_place_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_place_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_place_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    const nmo_place_state_t *in_state = (const nmo_place_state_t *)instance;
    if (in_state == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_place_validate(in_state, type, context));
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_place_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}





