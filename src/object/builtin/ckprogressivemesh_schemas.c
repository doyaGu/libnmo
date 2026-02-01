/**
 * @file ckprogressivemesh_schemas.c
 * @brief CKProgressiveMesh schema implementation
 */

#include "object/nmo_ckprogressivemesh_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "format/nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <stdalign.h>
#include <string.h>

static nmo_result_t nmo_ckprogressivemesh_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckprogressivemesh_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckprogressivemesh_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckmesh_deserialize_fn base_deserialize = nmo_get_ckmesh_deserialize();
    if (base_deserialize) {
        return base_deserialize(chunk, arena, &out_state->base);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckprogressivemesh_serialize_internal(
    const nmo_ckprogressivemesh_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckprogressivemesh_serialize"));
    }

    nmo_ckmesh_serialize_fn base_serialize = nmo_get_ckmesh_serialize();
    if (base_serialize) {
        return base_serialize(&in_state->base, out_chunk, arena);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckprogressivemesh_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckprogressivemesh_deserialize_internal(chunk, arena, (nmo_ckprogressivemesh_state_t *)out_ptr);
}

static nmo_result_t nmo_ckprogressivemesh_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckprogressivemesh_serialize_internal((const nmo_ckprogressivemesh_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckprogressivemesh_vtable = {
    .read = nmo_ckprogressivemesh_vtable_read,
    .write = nmo_ckprogressivemesh_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckprogressivemesh_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckprogressivemesh_schemas"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKProgressiveMeshState",
                                                      sizeof(nmo_ckprogressivemesh_state_t),
                                                      alignof(nmo_ckprogressivemesh_state_t));
    nmo_builder_set_vtable(&builder, &nmo_ckprogressivemesh_vtable);

    return nmo_builder_build(&builder, registry);
}

nmo_result_t nmo_ckprogressivemesh_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckprogressivemesh_state_t *out_state)
{
    return nmo_ckprogressivemesh_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckprogressivemesh_serialize(
    const nmo_ckprogressivemesh_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckprogressivemesh_serialize_internal(in_state, out_chunk, arena);
}
