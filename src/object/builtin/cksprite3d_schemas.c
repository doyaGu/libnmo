/**
 * @file cksprite3d_schemas.c
 * @brief CKSprite3D schema implementation
 */

#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include <string.h>

static void nmo_sprite3d_set_defaults(nmo_sprite3d_state_t *state) {
    if (state == NULL) {
        return;
    }

    /* Mirrors RCKSprite3D ctor defaults (see CKRenderEngine/src/CKSprite3d.cpp). */
    state->has_data = 1;
    state->mode = VXSPRITE3D_BILLBOARD;
    state->half_width = 1.0f;
    state->half_height = 1.0f;
    state->offset.x = 0.0f;
    state->offset.y = 0.0f;
    state->uv_rect.left = 0.0f;
    state->uv_rect.top = 0.0f;
    state->uv_rect.right = 1.0f;
    state->uv_rect.bottom = 1.0f;
    state->material = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    sprite3d,
    nmo_sprite3d_state_t,
    do { \
        nmo_sprite3d_set_defaults(state); \
    } while (0),
    ((void)0))

static void nmo_sprite3d_dispose_base_arrays(nmo_sprite3d_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *base = &state->base.base.base;
    nmo_array_dispose(&base->scripts);
    nmo_array_dispose(&base->attributes);
    nmo_array_dispose(&base->legacy_attributes);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_sprite3d_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_sprite3d_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_sprite3d_state_t, has_data, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite3d_state_t, mode, NMO_GUID_ENUM_VXSPRITE3D_TYPE),
    NMO_FIELD(nmo_sprite3d_state_t, half_width, CKPGUID_FLOAT),
    NMO_FIELD(nmo_sprite3d_state_t, half_height, CKPGUID_FLOAT),
    NMO_FIELD(nmo_sprite3d_state_t, offset, CKPGUID_2DVECTOR),
    NMO_FIELD(nmo_sprite3d_state_t, uv_rect, CKPGUID_RECT),
    NMO_FIELD_REF(nmo_sprite3d_state_t, material)
};

static nmo_status_t nmo_sprite3d_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_sprite3d_state_t *out_state)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sprite3d_deserialize");
    }

    nmo_status_t result = nmo_3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    nmo_sprite3d_set_defaults(out_state);
    out_state->has_data = 0;

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITE3DDATA);
    if (result == NMO_OK) {
        uint32_t mode = 0;
        float half_width = 0.0f;
        float half_height = 0.0f;
        nmo_vector2_t offset = {0.0f, 0.0f};
        nmo_rect_t uv_rect = {0.0f, 0.0f, 0.0f, 0.0f};
        nmo_ref_t material = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &mode));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &half_width));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &half_height));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &offset.x));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &offset.y));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &uv_rect.left));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &uv_rect.top));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &uv_rect.right));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &uv_rect.bottom));
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &material));
        nmo_ref_check_class(
            &material,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_MATERIAL);
        out_state->has_data = 1;
        out_state->mode = mode;
        out_state->half_width = half_width;
        out_state->half_height = half_height;
        out_state->offset = offset;
        out_state->uv_rect = uv_rect;
        out_state->material = material;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_sprite3d_serialize_internal(
    const nmo_sprite3d_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sprite3d_serialize");
    }

    nmo_status_t result = nmo_3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_SPRITE3DONLY) == 0) {
            return NMO_OK;
        }
    }

    if (in_state->has_data) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SPRITE3DDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->mode);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->half_width);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->half_height);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->offset.x);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->offset.y);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.left);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.top);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.right);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.bottom);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->material);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_sprite3d_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_sprite3d_state_t *out_state = (nmo_sprite3d_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_sprite3d_state_t decoded = {0};
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

    nmo_status_t result = nmo_sprite3d_deserialize_internal(
        chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_sprite3d_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_sprite3d_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_sprite3d_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_sprite3d_state_t *in_state = (const nmo_sprite3d_state_t *)instance;
    if (in_state == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
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

    nmo_status_t result = nmo_sprite3d_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_sprite3d_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_sprite3d_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sprite3d_remap_dependencies");
    }

    nmo_sprite3d_state_t *state = (nmo_sprite3d_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_3dentity_remap_dependencies(&state->base, NULL, context));

    /* Preserve unresolved material reference. */
    NMO_RETURN_OK();
}

static nmo_status_t nmo_sprite3d_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_sprite3d_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_sprite3d_post_delete(
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

NMO_DEFINE_OBJECT_STATE_OPS(sprite3d, nmo_sprite3d_state_t)

nmo_type_vtable_t nmo_sprite3d_vtable = {
    .prepare_dependencies = nmo_sprite3d_prepare_dependencies,
    .remap_dependencies = nmo_sprite3d_remap_dependencies,
    .pre_delete = nmo_sprite3d_pre_delete,
    .post_delete = nmo_sprite3d_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_sprite3d_create,
        nmo_sprite3d_destroy,
        nmo_sprite3d_serialize,
        nmo_sprite3d_deserialize,
        nmo_sprite3d_copy,
        nmo_sprite3d_validate,
        nmo_sprite3d_equals,
        nmo_sprite3d_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_sprite3d_type,
    CKPGUID_SPRITE3D,
    "CKSprite3D",
    NMO_CID_SPRITE3D,
    CKPGUID_3DENTITY,
    nmo_sprite3d_state_t,
    &nmo_sprite3d_vtable,
    nmo_sprite3d_fields)
