/**
 * @file ckplace_schemas.c
 * @brief CKPlace schema implementation
 */

#include "object/builtin/nmo_place_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
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

NMO_DEFINE_OBJECT_LIFECYCLE(
    place,
    nmo_place_state_t,
    do {
        nmo_status_t result = nmo_array_init(&state->portals, sizeof(nmo_place_portal_entry_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->references, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) return result;
    } while (0),
    do {
        nmo_array_dispose(&state->portals);
        nmo_array_dispose(&state->references);
    } while (0))

static nmo_status_t nmo_place_deserialize_internal(
    nmo_place_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_place_deserialize");
    }

    nmo_status_t result = nmo_beobject_deserialize(&out_state->base.base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const int file_mode = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (!file_mode) {
        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACECAMERA) == NMO_OK) {
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
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACELEVEL) == NMO_OK) {
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
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEPORTALS) == NMO_OK) {
        int32_t count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &count));
        if (count < 0) return NMO_ERR_INVALID_FORMAT;
        const nmo_chunk_parser_state_t *parser =
            (const nmo_chunk_parser_state_t *)chunk->parser_state;
        if (parser == NULL || parser->current_pos > chunk->data.count ||
            (size_t)count > (chunk->data.count - parser->current_pos) / 2u) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        nmo_array_t portals = {0};
        result = nmo_array_init(
            &portals, sizeof(nmo_place_portal_entry_t), (size_t)count, NULL);
        if (result != NMO_OK) return result;
        nmo_place_portal_entry_t *entries = NULL;
        result = nmo_array_extend(&portals, (size_t)count, (void **)&entries);
        for (uint32_t i = 0; result == NMO_OK && i < (uint32_t)count; ++i) {
            result = nmo_chunk_read_object_id(chunk, &entries[i].place_id);
            if (result == NMO_OK) {
                result = nmo_chunk_read_object_id(chunk, &entries[i].portal_id);
            }
        }
        if (result != NMO_OK) {
            nmo_array_dispose(&portals);
            return result;
        }
        NMO_RETURN_IF_ERROR(nmo_array_swap(&out_state->portals, &portals));
        nmo_array_dispose(&portals);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEREFERENCES) == NMO_OK) {
        nmo_ref_t *refs = NULL;
        size_t count = 0;
        nmo_status_t result = nmo_ref_read_sequence(
            chunk, &refs, &count, arena);
        if (result != NMO_OK) return result;
        nmo_array_t references = {0};
        result = nmo_array_init(&references, sizeof(nmo_ref_t), count, NULL);
        if (result != NMO_OK) return result;
        nmo_ref_t *dest = NULL;
        result = nmo_array_extend(&references, count, (void **)&dest);
        if (result != NMO_OK) {
            nmo_array_dispose(&references);
            return result;
        }
        if (count > 0) memcpy(dest, refs, sizeof(nmo_ref_t) * count);
        NMO_RETURN_IF_ERROR(nmo_array_swap(&out_state->references, &references));
        nmo_array_dispose(&references);
    }

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_place_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_place_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_place_state_t, has_camera, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_place_state_t, camera),
    NMO_FIELD(nmo_place_state_t, has_level, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_place_state_t, level),
    NMO_FIELD_ARRAY(nmo_place_state_t, portals, NMO_GUID_STRUCT_CKPLACEPORTALENTRY),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_place_state_t, references)
};

static nmo_status_t nmo_place_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_place_state_t *s = src;
    nmo_place_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    if (d->portals.data == s->portals.data) {
        memset(&d->portals, 0, sizeof(d->portals));
    } else {
        nmo_array_dispose(&d->portals);
    }
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->portals, &d->portals, &s->portals.allocator));
    if (d->references.data == s->references.data) {
        memset(&d->references, 0, sizeof(d->references));
    } else {
        nmo_array_dispose(&d->references);
    }
    return nmo_array_clone(&s->references, &d->references, &s->references.allocator);
}

static bool nmo_place_array_equals(
    const nmo_array_t *lhs,
    const nmo_array_t *rhs)
{
    if (lhs->count != rhs->count || lhs->element_size != rhs->element_size) {
        return false;
    }
    if (lhs->count == 0) return true;
    if (lhs->data == NULL || rhs->data == NULL || lhs->element_size == 0 ||
        lhs->count > SIZE_MAX / lhs->element_size) {
        return false;
    }
    return memcmp(lhs->data, rhs->data,
                  lhs->count * lhs->element_size) == 0;
}

static void nmo_place_normalize_array(nmo_array_t *array)
{
    const size_t count = array->count;
    const size_t element_size = array->element_size;
    memset(array, 0, sizeof(*array));
    array->count = count;
    array->element_size = element_size;
}

static bool nmo_place_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_place_state_t *lhs = (const nmo_place_state_t *)a;
    const nmo_place_state_t *rhs = (const nmo_place_state_t *)b;
    if (!nmo_place_array_equals(&lhs->portals, &rhs->portals) ||
        !nmo_place_array_equals(&lhs->references, &rhs->references)) {
        return false;
    }
    nmo_place_state_t lhs_value = *lhs;
    nmo_place_state_t rhs_value = *rhs;
    nmo_place_normalize_array(&lhs_value.portals);
    nmo_place_normalize_array(&rhs_value.portals);
    nmo_place_normalize_array(&lhs_value.references);
    nmo_place_normalize_array(&rhs_value.references);
    return memcmp(&lhs_value, &rhs_value, sizeof(lhs_value)) == 0;
}

static uint32_t nmo_place_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_place_state_t *state = (const nmo_place_state_t *)instance;
    nmo_place_state_t value = *state;
    nmo_place_normalize_array(&value.portals);
    nmo_place_normalize_array(&value.references);
    uint32_t hash = (uint32_t)nmo_hash_fnv1a(&value, sizeof(value));
    if (state->portals.data != NULL && state->portals.count > 0 &&
        state->portals.element_size > 0 &&
        state->portals.count <= SIZE_MAX / state->portals.element_size) {
        hash ^= (uint32_t)nmo_hash_fnv1a(
            state->portals.data,
            state->portals.count * state->portals.element_size);
    }
    if (state->references.data != NULL && state->references.count > 0 &&
        state->references.element_size > 0 &&
        state->references.count <= SIZE_MAX / state->references.element_size) {
        hash ^= (uint32_t)nmo_hash_fnv1a(
            state->references.data,
            state->references.count * state->references.element_size);
    }
    return hash;
}

static nmo_status_t nmo_place_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_place_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->portals.data, s->portals.count, "portals");
    NMO_VALIDATE_COUNT(s->references.data, s->references.count, "references");
    if (s->portals.element_size != sizeof(nmo_place_portal_entry_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->references.element_size != sizeof(nmo_ref_t)) {
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

    const uint32_t visibility_flags = state->base.base.base.base.base.visibility_flags;
    if ((visibility_flags & NMO_CKOBJECT_VISIBLE) != 0) {
        state->base.moveable_flags |= VX_MOVEABLE_VISIBLE;
    } else {
        state->base.moveable_flags &= ~VX_MOVEABLE_VISIBLE;
    }

    if ((visibility_flags & NMO_CKOBJECT_HIERARCHICAL) != 0) {
        state->base.moveable_flags |= VX_MOVEABLE_HIERARCHICALHIDE;
    } else {
        state->base.moveable_flags &= ~VX_MOVEABLE_HIERARCHICALHIDE;
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
    state->portals.count = 0;
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
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACECAMERA);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->camera);
        if (result != NMO_OK) return result;
    }

    if (is_file && write_level && (in_state->has_level ||
                                   in_state->level.state != NMO_REF_NONE)) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACELEVEL);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->level);
        if (result != NMO_OK) return result;
    }

    if (write_portals && in_state->portals.count > 0) {
        if (in_state->portals.data == NULL ||
            in_state->portals.element_size != sizeof(nmo_place_portal_entry_t) ||
            in_state->portals.count > INT32_MAX) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACEPORTALS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->portals.count);
        if (result != NMO_OK) return result;

        const nmo_place_portal_entry_t *portals = NMO_ARRAY_DATA(
            nmo_place_portal_entry_t, &in_state->portals);
        for (uint32_t i = 0; i < in_state->portals.count; ++i) {
            result = nmo_chunk_write_object_id(out_chunk, portals[i].place_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, portals[i].portal_id);
            if (result != NMO_OK) return result;
        }
    }

    const bool write_references = use_flags
        ? ((save_flags & CK_STATESAVE_PLACEREFERENCES) != 0) : true;
    if (write_references && in_state->references.count > 0) {
        if (in_state->references.data == NULL ||
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
    return nmo_place_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_place_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_place_state_t *in_state = (const nmo_place_state_t *)instance;
    return nmo_place_serialize_internal(in_state, out_chunk, context);
}





