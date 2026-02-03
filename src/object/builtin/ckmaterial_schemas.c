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
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

/* ========================================================================
 * Helper Functions
 * ======================================================================== */
/* ========================================================================
 * CKRenderEngine-aligned implementation
 * ======================================================================== */

#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_class_ids.h"

#define CK_STATESAVE_MATDATA 0x00001000u
#define CK_STATESAVE_MATDATA2 0x00002000u
#define CK_STATESAVE_MATDATA3 0x00004000u
#define CK_STATESAVE_MATDATA5 0x00010000u

static uint32_t nmo_pack_color(float r, float g, float b, float a) {
    if (r < 0.0f) r = 0.0f;
    if (g < 0.0f) g = 0.0f;
    if (b < 0.0f) b = 0.0f;
    if (a < 0.0f) a = 0.0f;
    if (r > 1.0f) r = 1.0f;
    if (g > 1.0f) g = 1.0f;
    if (b > 1.0f) b = 1.0f;
    if (a > 1.0f) a = 1.0f;

    const uint32_t rr = (uint32_t)(r * 255.0f + 0.5f);
    const uint32_t gg = (uint32_t)(g * 255.0f + 0.5f);
    const uint32_t bb = (uint32_t)(b * 255.0f + 0.5f);
    const uint32_t aa = (uint32_t)(a * 255.0f + 0.5f);

    return (aa << 24) | (rr << 16) | (gg << 8) | bb;
}

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

    memset(out_state, 0, sizeof(*out_state));

    {
        nmo_status_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
            if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MATDATA) == NMO_OK) {
        uint32_t data_version = nmo_chunk_get_data_version(chunk);

        if (data_version < 5) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            out_state->diffuse_color = nmo_pack_color(r, g, b, a);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            out_state->ambient_color = nmo_pack_color(r, g, b, a);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            out_state->specular_color = nmo_pack_color(r, g, b, a);

            nmo_chunk_read_float(chunk, &r);
            nmo_chunk_read_float(chunk, &g);
            nmo_chunk_read_float(chunk, &b);
            nmo_chunk_read_float(chunk, &a);
            out_state->emissive_color = nmo_pack_color(r, g, b, a);

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

NMO_DEFINE_OBJECT_SCHEMA(
    ckmaterial,
    nmo_ck_material_state_t,
    nmo_ckmaterial_serialize,
    nmo_ckmaterial_deserialize,
    NMO_GUID_CKMATERIAL,
    "CKMaterial",
    NMO_CID_MATERIAL,
    NMO_GUID_CKBEOBJECT
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

