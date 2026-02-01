/**
 * @file ckgrid_schemas.c
 * @brief CKGrid schema implementation
 */

#include "object/nmo_ckgrid_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_GRIDDATA 0x00400000u

static int nmo_chunk_is_file_mode(const nmo_chunk_t *chunk) {
    return chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE);
}

nmo_result_t nmo_ckgrid_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckgrid_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgrid_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ck3dentity_deserialize(chunk, arena, &out_state->base);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_GRIDDATA).code != NMO_OK) {
        return nmo_result_ok();
    }

    nmo_chunk_read_int(chunk, &out_state->width);
    nmo_chunk_read_int(chunk, &out_state->length);
    {
        int32_t reserved = 0;
        nmo_chunk_read_int(chunk, &reserved);
    }
    nmo_chunk_read_int(chunk, &out_state->priority);
    nmo_chunk_read_dword(chunk, &out_state->orientation_mode);

    if (nmo_chunk_is_file_mode(chunk)) {
        int32_t file_flag = 0;
        if (nmo_chunk_read_int(chunk, &file_flag).code == NMO_OK) {
            out_state->has_file_flag = 1;
            out_state->file_flag = file_flag;
        }
    }

    size_t count = 0;
    result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result.code == NMO_OK && count > 0) {
        out_state->layer_ids = (nmo_object_id_t *)nmo_arena_alloc(
            arena, count * sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t));
        if (out_state->layer_ids) {
            out_state->layer_count = (uint32_t)count;
            for (size_t i = 0; i < count; ++i) {
                nmo_chunk_read_object_sequence_item(chunk, &out_state->layer_ids[i]);
            }
        }
    }

    if (!nmo_chunk_is_file_mode(chunk) && out_state->layer_count > 0) {
        out_state->layer_chunk_count = out_state->layer_count;
        out_state->layer_chunks = (nmo_chunk_t **)nmo_arena_alloc(
            arena, out_state->layer_chunk_count * sizeof(nmo_chunk_t *),
            _Alignof(nmo_chunk_t *));
        if (out_state->layer_chunks) {
            for (uint32_t i = 0; i < out_state->layer_chunk_count; ++i) {
                (void)nmo_chunk_read_sub_chunk(chunk, &out_state->layer_chunks[i]);
            }
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckgrid_serialize(
    const nmo_ckgrid_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgrid_serialize"));
    }

    nmo_result_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, arena);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_GRIDDATA);
    if (result.code != NMO_OK) return result;

    nmo_chunk_write_int(out_chunk, in_state->width);
    nmo_chunk_write_int(out_chunk, in_state->length);
    nmo_chunk_write_int(out_chunk, 0);
    nmo_chunk_write_int(out_chunk, in_state->priority);
    nmo_chunk_write_dword(out_chunk, in_state->orientation_mode);

    if (nmo_chunk_is_file_mode(out_chunk)) {
        nmo_chunk_write_int(out_chunk, in_state->has_file_flag ? in_state->file_flag : 1);
    }

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->layer_count);
    if (result.code != NMO_OK) return result;

    for (uint32_t i = 0; i < in_state->layer_count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, in_state->layer_ids[i]);
    }

    if (!nmo_chunk_is_file_mode(out_chunk) && in_state->layer_count > 0) {
        for (uint32_t i = 0; i < in_state->layer_count; ++i) {
            nmo_chunk_t *sub = NULL;
            if (in_state->layer_chunks && i < in_state->layer_chunk_count) {
                sub = in_state->layer_chunks[i];
            }
            nmo_chunk_write_sub_chunk(out_chunk, sub);
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckgrid_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckgrid_deserialize(chunk, arena, (nmo_ckgrid_state_t *)out_ptr);
}

static nmo_result_t nmo_ckgrid_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckgrid_serialize((const nmo_ckgrid_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckgrid_vtable = {
    .read = nmo_ckgrid_vtable_read,
    .write = nmo_ckgrid_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckgrid_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckgrid_schemas"));
    }

    const nmo_schema_type_t *u32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *i32_type = nmo_schema_registry_find_by_name(registry, "i32");
    if (!u32_type || !i32_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required scalar types not found"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKGridState",
                                                      sizeof(nmo_ckgrid_state_t),
                                                      alignof(nmo_ckgrid_state_t));
    nmo_builder_add_field_ex(&builder, "width", i32_type,
                             offsetof(nmo_ckgrid_state_t, width), 0);
    nmo_builder_add_field_ex(&builder, "length", i32_type,
                             offsetof(nmo_ckgrid_state_t, length), 0);
    nmo_builder_add_field_ex(&builder, "priority", i32_type,
                             offsetof(nmo_ckgrid_state_t, priority), 0);
    nmo_builder_add_field_ex(&builder, "orientation_mode", u32_type,
                             offsetof(nmo_ckgrid_state_t, orientation_mode), 0);

    nmo_builder_set_vtable(&builder, &nmo_ckgrid_vtable);

    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) return result;

    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKGridState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_GRID, type);
    }

    return result.code == NMO_OK ? nmo_result_ok() : result;
}
