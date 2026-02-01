/**
 * @file cksprite3d_schemas.c
 * @brief CKSprite3D schema implementation
 */

#include "object/nmo_cksprite3d_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_SPRITE3DDATA 0x00400000u

static nmo_result_t nmo_cksprite3d_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksprite3d_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksprite3d_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ck3dentity_deserialize(chunk, arena, &out_state->base);
    if (result.code != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITE3DDATA).code == NMO_OK) {
        out_state->has_data = 1;
        (void)nmo_chunk_read_dword(chunk, &out_state->mode);
        (void)nmo_chunk_read_float(chunk, &out_state->half_width);
        (void)nmo_chunk_read_float(chunk, &out_state->half_height);
        (void)nmo_chunk_read_float(chunk, &out_state->offset.x);
        (void)nmo_chunk_read_float(chunk, &out_state->offset.y);
        (void)nmo_chunk_read_float(chunk, &out_state->uv_rect.left);
        (void)nmo_chunk_read_float(chunk, &out_state->uv_rect.top);
        (void)nmo_chunk_read_float(chunk, &out_state->uv_rect.right);
        (void)nmo_chunk_read_float(chunk, &out_state->uv_rect.bottom);
        (void)nmo_chunk_read_object_id(chunk, &out_state->material_id);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_cksprite3d_serialize_internal(
    const nmo_cksprite3d_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksprite3d_serialize"));
    }

    nmo_result_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    if (in_state->has_data) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SPRITE3DDATA);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->mode);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->half_width);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->half_height);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->offset.x);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->offset.y);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.left);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.top);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.right);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.bottom);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->material_id);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_cksprite3d_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_cksprite3d_deserialize_internal(chunk, arena, (nmo_cksprite3d_state_t *)out_ptr);
}

static nmo_result_t nmo_cksprite3d_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_cksprite3d_serialize_internal((const nmo_cksprite3d_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_cksprite3d_vtable = {
    .read = nmo_cksprite3d_vtable_read,
    .write = nmo_cksprite3d_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_cksprite3d_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_cksprite3d_schemas"));
    }

    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(registry, "f32");
    if (!uint32_type || !float_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKSprite3DState",
                                                      sizeof(nmo_cksprite3d_state_t),
                                                      alignof(nmo_cksprite3d_state_t));

    nmo_builder_add_field_ex(&builder, "mode", uint32_type,
                            offsetof(nmo_cksprite3d_state_t, mode), 0);
    nmo_builder_add_field_ex(&builder, "half_width", float_type,
                            offsetof(nmo_cksprite3d_state_t, half_width), 0);
    nmo_builder_add_field_ex(&builder, "half_height", float_type,
                            offsetof(nmo_cksprite3d_state_t, half_height), 0);

    nmo_builder_set_vtable(&builder, &nmo_cksprite3d_vtable);

    return nmo_builder_build(&builder, registry);
}

nmo_result_t nmo_cksprite3d_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksprite3d_state_t *out_state)
{
    return nmo_cksprite3d_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_cksprite3d_serialize(
    const nmo_cksprite3d_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_cksprite3d_serialize_internal(in_state, out_chunk, arena);
}
