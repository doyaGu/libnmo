/**
 * @file cklayer_schemas.c
 * @brief CKLayer schema implementation
 */

#include "object/nmo_cklayer_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_LAYERDATA 0x00000010u

nmo_result_t nmo_cklayer_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cklayer_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cklayer_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckobject_deserialize_fn base_deserialize = nmo_get_ckobject_deserialize();
    if (base_deserialize) {
        nmo_result_t result = base_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_LAYERDATA).code != NMO_OK) {
        return nmo_result_ok();
    }

    nmo_chunk_read_object_id(chunk, &out_state->grid_id);

    const int file_mode = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (file_mode) {
        int32_t format = 0;
        int32_t version = 0;
        if (nmo_chunk_read_int(chunk, &format).code == NMO_OK) {
            out_state->format = format;
        }
        if (nmo_chunk_read_int(chunk, &version).code == NMO_OK) {
            out_state->version = version;
            out_state->has_version = 1;
        }

        if (out_state->has_version && out_state->version >= 1) {
            uint32_t color = 0;
            if (nmo_chunk_read_dword(chunk, &color).code == NMO_OK) {
                out_state->color_rgba = color;
                out_state->has_color = 1;
            }
            if (out_state->version >= 3) {
                if (nmo_chunk_read_guid(chunk, &out_state->param_guid).code == NMO_OK) {
                    out_state->has_param_guid = 1;
                }
            }
            nmo_chunk_read_int(chunk, (int32_t *)&out_state->flags);
        }
    } else {
        int32_t type = 0;
        if (nmo_chunk_read_int(chunk, &type).code == NMO_OK) {
            out_state->type = type;
            out_state->has_type = 1;
        }
        nmo_chunk_read_int(chunk, &out_state->format);
        nmo_chunk_read_int(chunk, (int32_t *)&out_state->flags);
    }

    if (out_state->format == 0) {
        void *raw = NULL;
        size_t raw_size = 0;
        if (nmo_chunk_read_buffer(chunk, &raw, &raw_size).code == NMO_OK) {
            out_state->square_data = raw;
            out_state->square_data_size = raw_size;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_cklayer_serialize(
    const nmo_cklayer_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cklayer_serialize"));
    }

    nmo_ckobject_serialize_fn base_serialize = nmo_get_ckobject_serialize();
    if (base_serialize) {
        nmo_result_t result = base_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) return result;
    }

    nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_LAYERDATA);
    if (result.code != NMO_OK) return result;

    nmo_chunk_write_object_id(out_chunk, in_state->grid_id);

    const int file_mode = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (file_mode) {
        const int32_t version = in_state->has_version ? in_state->version : 3;
        nmo_chunk_write_int(out_chunk, in_state->format);
        nmo_chunk_write_int(out_chunk, version);
        nmo_chunk_write_dword(out_chunk, in_state->has_color ? in_state->color_rgba : 0);
        if (version >= 3) {
            nmo_chunk_write_guid(out_chunk,
                                 in_state->has_param_guid ? in_state->param_guid : (nmo_guid_t){0, 0});
        }
        nmo_chunk_write_int(out_chunk, (int32_t)in_state->flags);
    } else {
        nmo_chunk_write_int(out_chunk, in_state->has_type ? in_state->type : 0);
        nmo_chunk_write_int(out_chunk, in_state->format);
        nmo_chunk_write_int(out_chunk, (int32_t)in_state->flags);
    }

    if (in_state->format == 0 && in_state->square_data && in_state->square_data_size > 0) {
        return nmo_chunk_write_buffer(out_chunk, in_state->square_data, in_state->square_data_size);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_cklayer_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_cklayer_deserialize(chunk, arena, (nmo_cklayer_state_t *)out_ptr);
}

static nmo_result_t nmo_cklayer_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_cklayer_serialize((const nmo_cklayer_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_cklayer_vtable = {
    .read = nmo_cklayer_vtable_read,
    .write = nmo_cklayer_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_cklayer_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_cklayer_schemas"));
    }

    const nmo_schema_type_t *u32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *i32_type = nmo_schema_registry_find_by_name(registry, "i32");
    if (!u32_type || !i32_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required scalar types not found"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKLayerState",
                                                      sizeof(nmo_cklayer_state_t),
                                                      alignof(nmo_cklayer_state_t));
    nmo_builder_add_field_ex(&builder, "grid_id", u32_type,
                             offsetof(nmo_cklayer_state_t, grid_id), 0);
    nmo_builder_add_field_ex(&builder, "format", i32_type,
                             offsetof(nmo_cklayer_state_t, format), 0);
    nmo_builder_add_field_ex(&builder, "flags", u32_type,
                             offsetof(nmo_cklayer_state_t, flags), 0);

    nmo_builder_set_vtable(&builder, &nmo_cklayer_vtable);

    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) return result;

    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKLayerState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_LAYER, type);
    }

    return result.code == NMO_OK ? nmo_result_ok() : result;
}
