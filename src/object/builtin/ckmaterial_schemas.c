/**
 * @file ckmaterial_schemas.c
 * @brief CKMaterial schema implementation
 * @author libnmo
 * @date 2025
 *
 * Implementation of CKMaterial (ClassID 30) deserialization, serialization,
 * and finish loading handlers.
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

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(material, nmo_material_state_t)

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
    /* Textures (4 slots) */
    NMO_FIELD_FULL(nmo_material_state_t, texture_ids, CKPGUID_ID,
                   NMO_FIELD_REFERENCE | NMO_FIELD_REPEATED, 0),
    /* Render settings */
    NMO_FIELD(nmo_material_state_t, texture_border_color, CKPGUID_COLOR),
    NMO_FIELD(nmo_material_state_t, packed_modes, CKPGUID_UINT32),
    NMO_FIELD(nmo_material_state_t, packed_flags, CKPGUID_UINT32),
    /* Effect */
    NMO_FIELD(nmo_material_state_t, effect, CKPGUID_UINT32),
    NMO_FIELD_REF(nmo_material_state_t, effect_parameter_id),
    NMO_FIELD(nmo_material_state_t, has_effect, CKPGUID_UINT8),
    NMO_FIELD(nmo_material_state_t, has_effect_param, CKPGUID_UINT8),
    NMO_FIELD(nmo_material_state_t, has_additional_textures, CKPGUID_UINT8)
};

nmo_status_t nmo_material_deserialize(
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

    memset(out_state->texture_ids, 0, sizeof(out_state->texture_ids));
    out_state->has_effect = 0;
    out_state->has_effect_param = 0;
    out_state->has_additional_textures = 0;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA) == NMO_OK) {
        uint32_t data_version = nmo_chunk_get_data_version(chunk);

        if (data_version < 5) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            float diffuse_a = 0.0f;
            nmo_color_t color;

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            diffuse_a = a;
            color.r = r;
            color.g = g;
            color.b = b;
            color.a = a;
            out_state->diffuse_color = nmo_color_to_argb32(&color);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            color.r = r;
            color.g = g;
            color.b = b;
            color.a = a;
            out_state->ambient_color = nmo_color_to_argb32(&color);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            color.r = r;
            color.g = g;
            color.b = b;
            color.a = a;
            out_state->specular_color = nmo_color_to_argb32(&color);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            color.r = r;
            color.g = g;
            color.b = b;
            color.a = a;
            out_state->emissive_color = nmo_color_to_argb32(&color);

            nmo_chunk_read_float(chunk, &out_state->specular_power);

            nmo_chunk_read_object_id(chunk, &out_state->texture_ids[0]);

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

            nmo_chunk_read_dword(chunk, &low_flags);
            nmo_chunk_read_dword(chunk, &tex_blend);
            nmo_chunk_read_dword(chunk, &tex_min);
            nmo_chunk_read_dword(chunk, &tex_mag);
            nmo_chunk_read_dword(chunk, &src_blend);
            nmo_chunk_read_dword(chunk, &dst_blend);
            nmo_chunk_read_dword(chunk, &shade_mode);
            nmo_chunk_read_dword(chunk, &fill_mode);
            nmo_chunk_read_dword(chunk, &tex_address);
            nmo_chunk_read_dword(chunk, &out_state->texture_border_color);
            nmo_chunk_read_dword(chunk, &zfunc);

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

            out_state->packed_modes =
                (tex_blend & 0xF) |
                ((tex_min & 0xF) << 4) |
                ((tex_mag & 0xF) << 8) |
                ((src_blend & 0xF) << 12) |
                ((dst_blend & 0xF) << 16) |
                ((shade_mode & 0xF) << 20) |
                ((fill_mode & 0xF) << 24) |
                ((tex_address & 0xF) << 28);

            out_state->packed_flags =
                (low_flags & 0xFF) |
                ((zfunc & 0x1F) << 8) |
                (((uint32_t)VXCMP_ALWAYS & 0x1F) << 16) |
                ((uint32_t)0 << 24);
        } else {
            nmo_chunk_read_dword(chunk, &out_state->diffuse_color);
            nmo_chunk_read_dword(chunk, &out_state->ambient_color);
            nmo_chunk_read_dword(chunk, &out_state->specular_color);
            nmo_chunk_read_dword(chunk, &out_state->emissive_color);
            nmo_chunk_read_float(chunk, &out_state->specular_power);
            nmo_chunk_read_object_id(chunk, &out_state->texture_ids[0]);
            nmo_chunk_read_dword(chunk, &out_state->texture_border_color);
            nmo_chunk_read_dword(chunk, &out_state->packed_modes);
            nmo_chunk_read_dword(chunk, &out_state->packed_flags);

            uint32_t alpha_func = (out_state->packed_flags >> 16) & 0x1Fu;
            if (alpha_func == 0) {
                out_state->packed_flags &= ~(0x1Fu << 16);
                out_state->packed_flags |= ((uint32_t)VXCMP_ALWAYS & 0x1Fu) << 16;
            }

            uint32_t shade_mode = (out_state->packed_modes >> 20) & 0xFu;
            if (shade_mode > (uint32_t)VXSHADE_GOURAUD) {
                out_state->packed_modes &= ~(0xFu << 20);
                out_state->packed_modes |= ((uint32_t)VXSHADE_GOURAUD & 0xFu) << 20;
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA2) == NMO_OK) {
        nmo_chunk_read_object_id(chunk, &out_state->texture_ids[1]);
        nmo_chunk_read_object_id(chunk, &out_state->texture_ids[2]);
        nmo_chunk_read_object_id(chunk, &out_state->texture_ids[3]);
        out_state->has_additional_textures = 1;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA3) == NMO_OK) {
        nmo_chunk_read_dword(chunk, &out_state->effect);
        out_state->has_effect = 1;
        out_state->has_effect_param = 0;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA5) == NMO_OK) {
        nmo_chunk_read_object_id(chunk, &out_state->effect_parameter_id);
        nmo_chunk_read_dword(chunk, &out_state->effect);
        out_state->has_effect = 1;
        out_state->has_effect_param = 1;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS(
    material,
    nmo_material_state_t,
    nmo_material_serialize,
    nmo_material_deserialize,
    nmo_material_finish_loading,
    nmo_material_fields,
    CKPGUID_MATERIAL,
    "CKMaterial",
    NMO_CID_MATERIAL,
    CKPGUID_BEOBJECT
)

nmo_status_t nmo_material_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    (void)arena;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_material_finish_loading");
    }

    nmo_material_state_t *state = (nmo_material_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;

    if (repo) {
        for (uint32_t i = 0; i < 4; ++i) {
            nmo_object_id_t id = state->texture_ids[i];
            if (id == NMO_OBJECT_ID_NONE) {
                continue;
            }
            if (nmo_object_repository_find_by_id(repo, id) == NULL) {
                state->texture_ids[i] = NMO_OBJECT_ID_NONE;
            }
        }

        if (state->has_effect_param && state->effect_parameter_id != NMO_OBJECT_ID_NONE) {
            if (nmo_object_repository_find_by_id(repo, state->effect_parameter_id) == NULL) {
                state->effect_parameter_id = NMO_OBJECT_ID_NONE;
                state->has_effect_param = 0;
            }
        }
    }

    if (state->effect == 0) {
        state->has_effect = 0;
        state->has_effect_param = 0;
        state->effect_parameter_id = NMO_OBJECT_ID_NONE;
    } else if (state->has_effect_param && state->effect_parameter_id == NMO_OBJECT_ID_NONE) {
        state->has_effect_param = 0;
    }

    if (state->texture_ids[1] == NMO_OBJECT_ID_NONE &&
        state->texture_ids[2] == NMO_OBJECT_ID_NONE &&
        state->texture_ids[3] == NMO_OBJECT_ID_NONE) {
        state->has_additional_textures = 0;
    } else {
        state->has_additional_textures = 1;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_material_serialize(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_material_state_t *state = (const nmo_material_state_t *)instance;
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags = ser_ctx ? ser_ctx->save_flags : 0;

    if (!state || !chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_material_serialize");
    }

    {
        nmo_status_t result = nmo_beobject_serialize(&state->base, chunk, NULL, context);
            if (result != NMO_OK) return result;
    }

    if (!is_file && (save_flags & CK_STATESAVE_MATERIALONLY) == 0) {
        NMO_RETURN_OK();
    }

    nmo_status_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA);
    if (result != NMO_OK) return result;

    nmo_chunk_write_dword(chunk, state->diffuse_color);
    nmo_chunk_write_dword(chunk, state->ambient_color);
    nmo_chunk_write_dword(chunk, state->specular_color);
    nmo_chunk_write_dword(chunk, state->emissive_color);
    nmo_chunk_write_float(chunk, state->specular_power);
    nmo_chunk_write_object_id(chunk, state->texture_ids[0]);
    nmo_chunk_write_dword(chunk, state->texture_border_color);
    nmo_chunk_write_dword(chunk, state->packed_modes);
    nmo_chunk_write_dword(chunk, state->packed_flags);

    if (state->has_effect && state->effect != 0) {
        if (state->has_effect_param) {
            result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA5);
            if (result != NMO_OK) return result;
            nmo_chunk_write_object_id(chunk, state->effect_parameter_id);
        } else {
            result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA3);
            if (result != NMO_OK) return result;
        }
        nmo_chunk_write_dword(chunk, state->effect);
    }

    if (state->has_effect && state->effect != 0 &&
        (state->texture_ids[1] != NMO_OBJECT_ID_NONE ||
         state->texture_ids[2] != NMO_OBJECT_ID_NONE ||
         state->texture_ids[3] != NMO_OBJECT_ID_NONE)) {
        result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA2);
        if (result != NMO_OK) return result;
        nmo_chunk_write_object_id(chunk, state->texture_ids[1]);
        nmo_chunk_write_object_id(chunk, state->texture_ids[2]);
        nmo_chunk_write_object_id(chunk, state->texture_ids[3]);
    }

    NMO_RETURN_OK();
}


