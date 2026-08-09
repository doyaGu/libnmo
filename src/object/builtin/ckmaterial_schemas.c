/**
 * @file ckmaterial_schemas.c
 * @brief CKMaterial schema implementation
 * @author libnmo
 * @date 2025
 *
 * Implementation of CKMaterial (ClassID 30) deserialization, serialization,
 * and runtime dependency hooks.
 *
 * Reference: reference/include/CKMaterial.h
 */

#include "object/builtin/nmo_material_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "object/nmo_object_repository.h"
#include "core/nmo_color.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

static void nmo_material_set_defaults(nmo_material_state_t *state)
{
    if (state == NULL) return;

    state->diffuse_color = nmo_color_to_argb32(
        &(nmo_color_t){0.7f, 0.7f, 0.7f, 1.0f});
    state->ambient_color = nmo_color_to_argb32(
        &(nmo_color_t){0.3f, 0.3f, 0.3f, 1.0f});
    state->specular_color = 0xFF000000u;
    state->emissive_color = 0xFF000000u;
    state->specular_power = 0.0f;
    for (size_t i = 0; i < 4; ++i) {
        state->textures[i] = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    }
    state->packed_modes =
        ((uint32_t)VXTEXTUREBLEND_MODULATEALPHA) |
        ((uint32_t)VXTEXTUREFILTER_LINEAR << 4) |
        ((uint32_t)VXTEXTUREFILTER_LINEAR << 8) |
        ((uint32_t)VXBLEND_ONE << 12) |
        ((uint32_t)VXBLEND_ZERO << 16) |
        ((uint32_t)VXSHADE_GOURAUD << 20) |
        ((uint32_t)VXFILL_SOLID << 24) |
        ((uint32_t)VXTEXTURE_ADDRESSWRAP << 28);
    state->packed_flags =
        0x06u |
        ((uint32_t)VXCMP_LESSEQUAL << 8) |
        ((uint32_t)VXCMP_ALWAYS << 16);
    state->effect_parameter = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->has_material_data = 1;
}

static void nmo_material_dispose_base_arrays(nmo_material_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->base.scripts);
    nmo_array_dispose(&state->base.attributes);
    nmo_array_dispose(&state->base.legacy_attributes);
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    material,
    nmo_material_state_t,
    do {
        nmo_status_t result = nmo_beobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        nmo_material_set_defaults(state);
    } while (0),
    nmo_beobject_vtable.destroy(&state->base, NULL, context))

static nmo_status_t nmo_material_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_material_fields[] = {
    /* Base class */
    NMO_FIELD_NAMED("base", offsetof(nmo_material_state_t, base),
                    sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    /* Colors (packed ARGB) */
    NMO_FIELD_NAMED("diffuse_color", offsetof(nmo_material_state_t, diffuse_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("ambient_color", offsetof(nmo_material_state_t, ambient_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("specular_color", offsetof(nmo_material_state_t, specular_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("emissive_color", offsetof(nmo_material_state_t, emissive_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    /* Specular power */
    NMO_FIELD(nmo_material_state_t, specular_power, CKPGUID_FLOAT),
    /* Fixed texture slots are reflected separately so generic reference
     * traversal treats each nmo_ref_t atomically. */
    NMO_FIELD_NAMED("texture_0", offsetof(nmo_material_state_t, textures[0]),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD, 0),
    NMO_FIELD_NAMED("texture_1", offsetof(nmo_material_state_t, textures[1]),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD, 0),
    NMO_FIELD_NAMED("texture_2", offsetof(nmo_material_state_t, textures[2]),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD, 0),
    NMO_FIELD_NAMED("texture_3", offsetof(nmo_material_state_t, textures[3]),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD, 0),
    /* Render settings */
    NMO_FIELD(nmo_material_state_t, texture_border_color, CKPGUID_COLOR),
    NMO_FIELD(nmo_material_state_t, packed_modes, CKPGUID_UINT32),
    NMO_FIELD(nmo_material_state_t, packed_flags, CKPGUID_UINT32),
    /* Effect */
    NMO_FIELD(nmo_material_state_t, effect, CKPGUID_UINT32),
    NMO_FIELD_REF_VALUE(nmo_material_state_t, effect_parameter),
    NMO_FIELD(nmo_material_state_t, has_material_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_material_state_t, material_data_is_legacy, CKPGUID_UINT8),
    NMO_FIELD(nmo_material_state_t, has_effect, CKPGUID_UINT8),
    NMO_FIELD(nmo_material_state_t, has_effect_param, CKPGUID_UINT8),
    NMO_FIELD(nmo_material_state_t, has_additional_textures, CKPGUID_UINT8)
};

static uint32_t nmo_material_normalize_packed_flags(uint32_t packed_flags)
{
    return (packed_flags & 0xFFu) |
           (((packed_flags >> 8) & 0xFu) << 8) |
           (((packed_flags >> 16) & 0xFu) << 16) |
           (packed_flags & 0xFF000000u);
}

static nmo_status_t nmo_material_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_material_state_t *out_state = (nmo_material_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_material_deserialize");
    }

    {
        nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
            if (result != NMO_OK) return result;
    }

    nmo_material_state_t decoded = *out_state;
    for (size_t i = 0; i < 4; ++i) {
        decoded.textures[i] = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    }
    decoded.effect_parameter = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    decoded.has_material_data = 0;
    decoded.material_data_is_legacy = 0;
    decoded.has_effect = 0;
    decoded.has_effect_param = 0;
    decoded.has_additional_textures = 0;

    nmo_status_t seek_result = nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_MATDATA);
    if (seek_result == NMO_OK) {
        decoded.has_material_data = 1;
        uint32_t data_version = nmo_chunk_get_data_version(chunk);
        decoded.material_data_is_legacy = data_version < 5u;

        if (data_version < 5) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            float diffuse_a = 0.0f;
            nmo_color_t color;

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &r));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &g));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &b));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &a));
            diffuse_a = a;
            color.r = r;
            color.g = g;
            color.b = b;
            color.a = a;
            decoded.diffuse_color = nmo_color_to_argb32(&color);

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &r));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &g));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &b));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &a));
            color.r = r;
            color.g = g;
            color.b = b;
            color.a = a;
            decoded.ambient_color = nmo_color_to_argb32(&color);

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &r));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &g));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &b));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &a));
            color.r = r;
            color.g = g;
            color.b = b;
            color.a = a;
            decoded.specular_color = nmo_color_to_argb32(&color);

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &r));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &g));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &b));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &a));
            color.r = r;
            color.g = g;
            color.b = b;
            color.a = a;
            decoded.emissive_color = nmo_color_to_argb32(&color);

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(
                chunk, &decoded.specular_power));

            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.textures[0]));

            uint32_t low_flags = 0;
            uint32_t tex_blend = 0;
            uint32_t tex_min = 0;
            uint32_t tex_mag = 0;
            uint32_t src_blend = 0;
            uint32_t dst_blend = 0;
            uint32_t shade_mode = 0;
            uint32_t fill_mode = 0;
            uint32_t tex_address = 0;
            uint32_t zfunc = 0;

            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &low_flags));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &tex_blend));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &tex_min));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &tex_mag));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &src_blend));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &dst_blend));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &shade_mode));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &fill_mode));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &tex_address));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &decoded.texture_border_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &zfunc));

            if (zfunc == 0) {
                zfunc = (uint32_t)VXCMP_LESSEQUAL;
            }

            if (data_version < 4) {
                uint32_t low_byte = low_flags & 0xFFu;
                if (low_byte == 1u) {
                    low_flags = (low_flags & ~0xFFu) | 7u;
                } else if (low_byte == 0u) {
                    low_flags = (low_flags & ~0xFFu) | 6u;
                }

                if (diffuse_a < 1.0f &&
                    src_blend == (uint32_t)VXBLEND_ONE &&
                    dst_blend == (uint32_t)VXBLEND_ZERO) {
                    src_blend = (uint32_t)VXBLEND_SRCALPHA;
                    dst_blend = (uint32_t)VXBLEND_INVSRCALPHA;
                }

                if (dst_blend != (uint32_t)VXBLEND_ZERO) {
                    low_flags |= 8u;
                    low_flags &= ~2u;
                }
            }

            if (shade_mode > (uint32_t)VXSHADE_GOURAUD) {
                shade_mode = (uint32_t)VXSHADE_GOURAUD;
            }

            decoded.packed_modes =
                (tex_blend & 0xF) |
                ((tex_min & 0xF) << 4) |
                ((tex_mag & 0xF) << 8) |
                ((src_blend & 0xF) << 12) |
                ((dst_blend & 0xF) << 16) |
                ((shade_mode & 0xF) << 20) |
                ((fill_mode & 0xF) << 24) |
                ((tex_address & 0xF) << 28);

            decoded.packed_flags =
                (low_flags & 0xFF) |
                ((zfunc & 0xF) << 8) |
                (((uint32_t)VXCMP_ALWAYS & 0xF) << 16) |
                ((uint32_t)0 << 24);
        } else {
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &decoded.diffuse_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &decoded.ambient_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &decoded.specular_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &decoded.emissive_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(
                chunk, &decoded.specular_power));
            NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.textures[0]));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &decoded.texture_border_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &decoded.packed_modes));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &decoded.packed_flags));
            decoded.packed_flags =
                nmo_material_normalize_packed_flags(decoded.packed_flags);

            uint32_t alpha_func = (decoded.packed_flags >> 16) & 0xFu;
            if (alpha_func == 0) {
                decoded.packed_flags &= ~(0xFu << 16);
                decoded.packed_flags |=
                    ((uint32_t)VXCMP_ALWAYS & 0xFu) << 16;
            }

            uint32_t shade_mode = (decoded.packed_modes >> 20) & 0xFu;
            if (shade_mode > (uint32_t)VXSHADE_GOURAUD) {
                decoded.packed_modes &= ~(0xFu << 20);
                decoded.packed_modes |=
                    ((uint32_t)VXSHADE_GOURAUD & 0xFu) << 20;
            }
        }
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA2);
    if (seek_result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.textures[1]));
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.textures[2]));
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &decoded.textures[3]));
        decoded.has_additional_textures = 1;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA3);
    if (seek_result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &decoded.effect));
        decoded.has_effect = 1;
        decoded.has_effect_param = 0;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    seek_result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA5);
    if (seek_result == NMO_OK) {
        if (decoded.has_effect) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Material contains conflicting effect sections");
        }
        NMO_RETURN_IF_ERROR(nmo_ref_read(
            chunk, &decoded.effect_parameter));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &decoded.effect));
        decoded.has_effect = 1;
        decoded.has_effect_param = 1;
    } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;

    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    for (size_t i = 0; i < 4; ++i) {
        nmo_ref_check_class(
            &decoded.textures[i], repository, types, NMO_CID_TEXTURE);
    }
    nmo_ref_check_class(
        &decoded.effect_parameter, repository, types, NMO_CID_PARAMETER);
    *out_state = decoded;

    NMO_RETURN_OK();
}

nmo_status_t nmo_material_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_material_state_t *out_state = (nmo_material_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_material_state_t decoded;
    nmo_status_t result = nmo_material_create(&decoded, type, context);
    if (result != NMO_OK) return result;
    if (out_state->base.scripts.allocator.alloc != NULL) {
        decoded.base.scripts.allocator = out_state->base.scripts.allocator;
    }
    if (out_state->base.attributes.allocator.alloc != NULL) {
        decoded.base.attributes.allocator = out_state->base.attributes.allocator;
    }
    if (out_state->base.legacy_attributes.allocator.alloc != NULL) {
        decoded.base.legacy_attributes.allocator =
            out_state->base.legacy_attributes.allocator;
    }

    result = nmo_material_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_material_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_material_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_material_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_material_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_material_remap_dependencies");
    }

    nmo_material_state_t *state = instance;
    NMO_RETURN_IF_ERROR(nmo_beobject_remap_dependencies(
        &state->base, NULL, context));
    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_material_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_material_pre_delete");
    }
    nmo_material_state_t *state = (nmo_material_state_t *)instance;
    for (size_t i = 0; i < 4; ++i) {
        state->textures[i] = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    }
    state->effect_parameter = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

static void nmo_material_post_delete(
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

static nmo_status_t nmo_material_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    const nmo_material_state_t *source = src;
    nmo_material_state_t *target = dst;
    nmo_type_descriptor_t base_type = {
        .size = sizeof(nmo_beobject_state_t),
    };
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.copy(
        &source->base, &target->base, &base_type, arena));
    target->diffuse_color = source->diffuse_color;
    target->ambient_color = source->ambient_color;
    target->specular_color = source->specular_color;
    target->emissive_color = source->emissive_color;
    target->specular_power = source->specular_power;
    memcpy(target->textures, source->textures, sizeof(target->textures));
    target->texture_border_color = source->texture_border_color;
    target->packed_modes = source->packed_modes;
    target->packed_flags = source->packed_flags;
    target->effect = source->effect;
    target->effect_parameter = source->effect_parameter;
    target->has_material_data = source->has_material_data;
    target->material_data_is_legacy = source->material_data_is_legacy;
    target->has_effect = source->has_effect;
    target->has_effect_param = source->has_effect_param;
    target->has_additional_textures = source->has_additional_textures;
    return NMO_OK;
}

static nmo_status_t nmo_material_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_material_state_t *state = instance;
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.validate(
        &state->base, NULL, context));
    if (((state->packed_modes >> 20) & 0xFu) >
            (uint32_t)VXSHADE_GOURAUD ||
        (state->has_effect_param && !state->has_effect)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Material state cannot be serialized losslessly");
    }
    NMO_RETURN_OK();
}

static bool nmo_material_ref_equals(
    const nmo_ref_t *lhs,
    const nmo_ref_t *rhs)
{
    return lhs->raw_id == rhs->raw_id &&
        lhs->id == rhs->id &&
        lhs->state == rhs->state;
}

static bool nmo_material_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_material_state_t *lhs = a;
    const nmo_material_state_t *rhs = b;
    if (!nmo_beobject_vtable.equals(&lhs->base, &rhs->base) ||
        lhs->diffuse_color != rhs->diffuse_color ||
        lhs->ambient_color != rhs->ambient_color ||
        lhs->specular_color != rhs->specular_color ||
        lhs->emissive_color != rhs->emissive_color ||
        memcmp(&lhs->specular_power, &rhs->specular_power,
               sizeof(lhs->specular_power)) != 0) {
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (!nmo_material_ref_equals(
                &lhs->textures[i], &rhs->textures[i])) {
            return false;
        }
    }
    return lhs->texture_border_color == rhs->texture_border_color &&
        lhs->packed_modes == rhs->packed_modes &&
        lhs->packed_flags == rhs->packed_flags &&
        lhs->effect == rhs->effect &&
        nmo_material_ref_equals(
            &lhs->effect_parameter, &rhs->effect_parameter) &&
        lhs->has_material_data == rhs->has_material_data &&
        lhs->material_data_is_legacy == rhs->material_data_is_legacy &&
        lhs->has_effect == rhs->has_effect &&
        lhs->has_effect_param == rhs->has_effect_param &&
        lhs->has_additional_textures == rhs->has_additional_textures;
}

static uint32_t nmo_material_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_material_hash_ref(
    uint32_t hash,
    const nmo_ref_t *ref)
{
    hash = nmo_material_hash_bytes(hash, &ref->raw_id, sizeof(ref->raw_id));
    hash = nmo_material_hash_bytes(hash, &ref->id, sizeof(ref->id));
    return nmo_material_hash_bytes(hash, &ref->state, sizeof(ref->state));
}

static uint32_t nmo_material_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_material_state_t *state = instance;
    uint32_t hash = nmo_beobject_vtable.hash(&state->base);
#define NMO_MATERIAL_HASH_FIELD(field) \
    hash = nmo_material_hash_bytes(hash, &state->field, sizeof(state->field))
    NMO_MATERIAL_HASH_FIELD(diffuse_color);
    NMO_MATERIAL_HASH_FIELD(ambient_color);
    NMO_MATERIAL_HASH_FIELD(specular_color);
    NMO_MATERIAL_HASH_FIELD(emissive_color);
    NMO_MATERIAL_HASH_FIELD(specular_power);
    for (size_t i = 0; i < 4; ++i) {
        hash = nmo_material_hash_ref(hash, &state->textures[i]);
    }
    NMO_MATERIAL_HASH_FIELD(texture_border_color);
    NMO_MATERIAL_HASH_FIELD(packed_modes);
    NMO_MATERIAL_HASH_FIELD(packed_flags);
    NMO_MATERIAL_HASH_FIELD(effect);
    hash = nmo_material_hash_ref(hash, &state->effect_parameter);
    NMO_MATERIAL_HASH_FIELD(has_material_data);
    NMO_MATERIAL_HASH_FIELD(material_data_is_legacy);
    NMO_MATERIAL_HASH_FIELD(has_effect);
    NMO_MATERIAL_HASH_FIELD(has_effect_param);
    NMO_MATERIAL_HASH_FIELD(has_additional_textures);
#undef NMO_MATERIAL_HASH_FIELD
    return hash;
}

nmo_type_vtable_t nmo_material_vtable = {
    .prepare_dependencies = nmo_material_prepare_dependencies,
    .remap_dependencies = nmo_material_remap_dependencies,
    .pre_delete = nmo_material_pre_delete,
    .post_delete = nmo_material_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_material_create,
        nmo_material_destroy,
        nmo_material_serialize,
        nmo_material_deserialize,
        nmo_material_copy,
        nmo_material_validate,
        nmo_material_equals,
        nmo_material_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_material_type,
    CKPGUID_MATERIAL,
    "CKMaterial",
    NMO_CID_MATERIAL,
    CKPGUID_BEOBJECT,
    nmo_material_state_t,
    &nmo_material_vtable,
    nmo_material_fields)

static nmo_status_t nmo_material_serialize_internal(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_material_state_t *state = (const nmo_material_state_t *)instance;
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file =
        (chunk != NULL &&
         (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL &&
         (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = ser_ctx ? ser_ctx->save_flags : 0;

    if (!state || !chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_material_serialize");
    }

    const bool write_material = is_file ||
        (save_flags & CK_STATESAVE_MATERIALONLY) != 0;
    const uint32_t data_version = nmo_chunk_get_data_version(chunk);
    const bool write_legacy = is_file && data_version < 5u &&
        (data_version != 0u || state->material_data_is_legacy);
    if (data_version == 0u && !write_legacy) {
        chunk->data_version = 7u;
    }
    if (write_material) {
        NMO_RETURN_IF_ERROR(nmo_material_validate(state, type, context));
        if (write_legacy &&
            (((state->packed_flags >> 16) & 0xFu) !=
                 (uint32_t)VXCMP_ALWAYS ||
             (state->packed_flags & 0xFF000000u) != 0u)) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Legacy material layout cannot store alpha settings");
        }
    }

    {
        nmo_status_t result = nmo_beobject_serialize(&state->base, chunk, NULL, context);
            if (result != NMO_OK) return result;
    }

    if (!write_material) NMO_RETURN_OK();

    nmo_material_state_t defaults = {0};
    nmo_material_set_defaults(&defaults);
    const bool has_default_data =
        state->diffuse_color == defaults.diffuse_color &&
        state->ambient_color == defaults.ambient_color &&
        state->specular_color == defaults.specular_color &&
        state->emissive_color == defaults.emissive_color &&
        state->specular_power == defaults.specular_power &&
        nmo_ref_serialized_id(&state->textures[0]) == NMO_OBJECT_ID_NONE &&
        state->texture_border_color == defaults.texture_border_color &&
        state->packed_modes == defaults.packed_modes &&
        nmo_material_normalize_packed_flags(state->packed_flags) ==
            defaults.packed_flags;
    const bool write_data = !is_file || state->has_material_data ||
        !has_default_data;

    nmo_status_t result;
    if (write_data) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
            chunk, CK_STATESAVE_MATDATA));
        if (write_legacy) {
            const uint32_t colors[] = {
                state->diffuse_color,
                state->ambient_color,
                state->specular_color,
                state->emissive_color,
            };
            for (size_t i = 0; i < 4; ++i) {
                nmo_color_t color;
                nmo_color_from_argb32(colors[i], &color);
                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(chunk, color.r));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(chunk, color.g));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(chunk, color.b));
                NMO_RETURN_IF_ERROR(nmo_chunk_write_float(chunk, color.a));
            }
            NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                chunk, state->specular_power));
            NMO_RETURN_IF_ERROR(nmo_ref_write(chunk, &state->textures[0]));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->packed_flags & 0xFFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->packed_modes & 0xFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, (state->packed_modes >> 4) & 0xFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, (state->packed_modes >> 8) & 0xFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, (state->packed_modes >> 12) & 0xFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, (state->packed_modes >> 16) & 0xFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, (state->packed_modes >> 20) & 0xFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, (state->packed_modes >> 24) & 0xFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, (state->packed_modes >> 28) & 0xFu));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->texture_border_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, (state->packed_flags >> 8) & 0xFu));
        } else {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->diffuse_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->ambient_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->specular_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->emissive_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
                chunk, state->specular_power));
            NMO_RETURN_IF_ERROR(nmo_ref_write(chunk, &state->textures[0]));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->texture_border_color));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, state->packed_modes));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
                chunk, nmo_material_normalize_packed_flags(
                    state->packed_flags)));
        }
    }

    const bool has_effect_parameter =
        nmo_ref_serialized_id(&state->effect_parameter) !=
        NMO_OBJECT_ID_NONE;
    const bool write_effect_param = state->has_effect_param ||
        has_effect_parameter;
    const bool write_effect = state->has_effect || write_effect_param ||
        state->effect != 0u;
    if (write_effect) {
        if (write_effect_param) {
            result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA5);
            if (result != NMO_OK) return result;
            NMO_RETURN_IF_ERROR(nmo_ref_write(
                chunk, &state->effect_parameter));
        } else {
            result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA3);
            if (result != NMO_OK) return result;
        }
        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, state->effect));
    }

    if (state->has_additional_textures ||
        nmo_ref_serialized_id(&state->textures[1]) != NMO_OBJECT_ID_NONE ||
        nmo_ref_serialized_id(&state->textures[2]) != NMO_OBJECT_ID_NONE ||
        nmo_ref_serialized_id(&state->textures[3]) != NMO_OBJECT_ID_NONE) {
        result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA2);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_ref_write(chunk, &state->textures[1]));
        NMO_RETURN_IF_ERROR(nmo_ref_write(chunk, &state->textures[2]));
        NMO_RETURN_IF_ERROR(nmo_ref_write(chunk, &state->textures[3]));
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_material_serialize(
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

    nmo_status_t result = nmo_material_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}
