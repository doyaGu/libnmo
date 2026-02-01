/**
 * @file ckplace_schemas.c
 * @brief CKPlace schema implementation
 */

#include "object/nmo_ckplace_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_PLACEPORTALS    0x00001000u
#define CK_STATESAVE_PLACECAMERA     0x00002000u
#define CK_STATESAVE_PLACEREFERENCES 0x00004000u
#define CK_STATESAVE_PLACELEVEL      0x00008000u

static nmo_result_t nmo_ckplace_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckplace_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckplace_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckbeobject_deserialize_fn parent_deserialize = nmo_get_ckbeobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    const int file_mode = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (!file_mode) {
        return nmo_result_ok();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACECAMERA).code == NMO_OK) {
        out_state->has_camera = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->camera_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACELEVEL).code == NMO_OK) {
        out_state->has_level = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->level_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEPORTALS).code == NMO_OK) {
        int32_t count = 0;
        if (nmo_chunk_read_int(chunk, &count).code == NMO_OK && count > 0) {
            out_state->portal_count = (uint32_t)count;
            out_state->portals = (nmo_ckplace_portal_entry_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckplace_portal_entry_t) * out_state->portal_count,
                _Alignof(nmo_ckplace_portal_entry_t));
            if (!out_state->portals) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate portal array"));
            }

            for (uint32_t i = 0; i < out_state->portal_count; ++i) {
                (void)nmo_chunk_read_object_id(chunk, &out_state->portals[i].place_id);
                (void)nmo_chunk_read_object_id(chunk, &out_state->portals[i].portal_id);
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEREFERENCES).code == NMO_OK) {
        nmo_object_id_t *ids = NULL;
        size_t count = 0;
        nmo_result_t result = nmo_chunk_read_object_id_array(chunk, &ids, &count, arena);
        if (result.code == NMO_OK && count > 0) {
            out_state->reference_count = (uint32_t)count;
            out_state->reference_ids = ids;
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckplace_serialize_internal(
    const nmo_ckplace_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckplace_serialize"));
    }

    nmo_ckbeobject_serialize_fn parent_serialize = nmo_get_ckbeobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    if (in_state->has_camera) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACECAMERA);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->camera_id);
        if (result.code != NMO_OK) return result;
    }

    const int file_mode = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (file_mode && in_state->has_level) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACELEVEL);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->level_id);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->portal_count > 0 && in_state->portals) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACEPORTALS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->portal_count);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->portal_count; ++i) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->portals[i].place_id);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->portals[i].portal_id);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckplace_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckplace_deserialize_internal(chunk, arena, (nmo_ckplace_state_t *)out_ptr);
}

static nmo_result_t nmo_ckplace_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckplace_serialize_internal((const nmo_ckplace_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckplace_vtable = {
    .read = nmo_ckplace_vtable_read,
    .write = nmo_ckplace_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckplace_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckplace_schemas"));
    }

    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    if (!uint32_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKPlaceState",
                                                      sizeof(nmo_ckplace_state_t),
                                                      alignof(nmo_ckplace_state_t));

    nmo_builder_add_field_ex(&builder, "camera_id", uint32_type,
                            offsetof(nmo_ckplace_state_t, camera_id), 0);
    nmo_builder_add_field_ex(&builder, "level_id", uint32_type,
                            offsetof(nmo_ckplace_state_t, level_id), 0);

    nmo_builder_set_vtable(&builder, &nmo_ckplace_vtable);

    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckplace_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckplace_state_t *out_state)
{
    return nmo_ckplace_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckplace_serialize(
    const nmo_ckplace_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckplace_serialize_internal(in_state, out_chunk, arena);
}
