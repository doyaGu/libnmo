/**
 * @file cktargetcamera_schemas.c
 * @brief CKTargetCamera schema implementation
 */

#include "object/nmo_cktargetcamera_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_TCAMERATARGET 0x10000000u

static nmo_result_t nmo_cktargetcamera_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cktargetcamera_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktargetcamera_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckcamera_deserialize_fn base_deserialize = nmo_get_ckcamera_deserialize();
    if (base_deserialize) {
        nmo_result_t result = base_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TCAMERATARGET).code == NMO_OK) {
        out_state->has_target = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->target_id);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_cktargetcamera_serialize_internal(
    const nmo_cktargetcamera_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cktargetcamera_serialize"));
    }

    nmo_ckcamera_serialize_fn base_serialize = nmo_get_ckcamera_serialize();
    if (base_serialize) {
        nmo_result_t result = base_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    if (in_state->has_target) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_TCAMERATARGET);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->target_id);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_cktargetcamera_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_cktargetcamera_deserialize_internal(chunk, arena, (nmo_cktargetcamera_state_t *)out_ptr);
}

static nmo_result_t nmo_cktargetcamera_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_cktargetcamera_serialize_internal((const nmo_cktargetcamera_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_cktargetcamera_vtable = {
    .read = nmo_cktargetcamera_vtable_read,
    .write = nmo_cktargetcamera_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_cktargetcamera_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_cktargetcamera_schemas"));
    }

    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    if (!uint32_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKTargetCameraState",
                                                      sizeof(nmo_cktargetcamera_state_t),
                                                      alignof(nmo_cktargetcamera_state_t));

    nmo_builder_add_field_ex(&builder, "target_id", uint32_type,
                            offsetof(nmo_cktargetcamera_state_t, target_id), 0);

    nmo_builder_set_vtable(&builder, &nmo_cktargetcamera_vtable);

    return nmo_builder_build(&builder, registry);
}

nmo_result_t nmo_cktargetcamera_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cktargetcamera_state_t *out_state)
{
    return nmo_cktargetcamera_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_cktargetcamera_serialize(
    const nmo_cktargetcamera_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_cktargetcamera_serialize_internal(in_state, out_chunk, arena);
}
