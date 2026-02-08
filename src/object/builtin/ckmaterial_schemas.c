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

#include "object/nmo_ckmaterial_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_color.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_class_ids.h"

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckmaterial, nmo_ck_material_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_ckmaterial_fields[] = {
    /* Base class */
    NMO_FIELD_NAMED("base", offsetof(nmo_ck_material_state_t, base),
                    sizeof(nmo_ckbeobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    /* Colors (packed ARGB) */
    NMO_FIELD_NAMED("diffuse_color", offsetof(nmo_ck_material_state_t, diffuse_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("ambient_color", offsetof(nmo_ck_material_state_t, ambient_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("specular_color", offsetof(nmo_ck_material_state_t, specular_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("emissive_color", offsetof(nmo_ck_material_state_t, emissive_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    /* Specular power */
    NMO_FIELD(nmo_ck_material_state_t, specular_power, CKPGUID_FLOAT),
    /* Textures (4 slots) */
    NMO_FIELD_FULL(nmo_ck_material_state_t, texture_ids, CKPGUID_ID,
                   NMO_FIELD_REFERENCE | NMO_FIELD_REPEATED, 0),
    /* Render settings */
    NMO_FIELD(nmo_ck_material_state_t, texture_border_color, CKPGUID_COLOR),
    NMO_FIELD(nmo_ck_material_state_t, packed_modes, CKPGUID_UINT32),
    NMO_FIELD(nmo_ck_material_state_t, packed_flags, CKPGUID_UINT32),
    /* Effect */
    NMO_FIELD(nmo_ck_material_state_t, effect, CKPGUID_UINT32),
    NMO_FIELD_REF(nmo_ck_material_state_t, effect_parameter_id),
    NMO_FIELD(nmo_ck_material_state_t, has_effect, CKPGUID_UINT8),
    NMO_FIELD(nmo_ck_material_state_t, has_effect_param, CKPGUID_UINT8),
    NMO_FIELD(nmo_ck_material_state_t, has_additional_textures, CKPGUID_UINT8)
};

nmo_status_t nmo_ckmaterial_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ck_material_state_t *out_state = (nmo_ck_material_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmaterial_deserialize");
    }

    {
        nmo_status_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
            if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA) == NMO_OK) {
        uint32_t data_version = nmo_chunk_get_data_version(chunk);

        if (data_version < 5) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            nmo_color_t color;

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
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
                ((8u & 0x1F) << 16) |
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
    ckmaterial,
    nmo_ck_material_state_t,
    nmo_ckmaterial_serialize,
    nmo_ckmaterial_deserialize,
    nmo_ckmaterial_finish_loading,
    nmo_ckmaterial_fields,
    CKPGUID_MATERIAL,
    "CKMaterial",
    NMO_CID_MATERIAL,
    CKPGUID_BEOBJECT
)

nmo_status_t nmo_ckmaterial_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    (void)instance;
    (void)arena;
    (void)repository;
    NMO_RETURN_OK();
}

nmo_status_t nmo_ckmaterial_serialize(
    const void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ck_material_state_t *state = (const nmo_ck_material_state_t *)instance;

    if (!state || !chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmaterial_serialize");
    }

    {
        nmo_status_t result = nmo_ckbeobject_serialize(&state->base, chunk, NULL, context);
            if (result != NMO_OK) return result;
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

    if (state->has_effect) {
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

    if (state->has_effect && state->has_additional_textures) {
        result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MATDATA2);
        if (result != NMO_OK) return result;
        nmo_chunk_write_object_id(chunk, state->texture_ids[1]);
        nmo_chunk_write_object_id(chunk, state->texture_ids[2]);
        nmo_chunk_write_object_id(chunk, state->texture_ids[3]);
    }

    NMO_RETURN_OK();
}


