/**
 * @file ckrendercontext_schemas.c
 * @brief CKRenderContext schema implementation
 */

#include "object/nmo_ckrendercontext_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

static nmo_result_t nmo_ckrendercontext_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckrendercontext_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrendercontext_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckobject_deserialize_fn parent_deserialize = nmo_get_ckobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckrendercontext_serialize_internal(
    const nmo_ckrendercontext_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrendercontext_serialize"));
    }

    nmo_ckobject_serialize_fn parent_serialize = nmo_get_ckobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckrendercontext_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckrendercontext_deserialize_internal(chunk, arena,
        (nmo_ckrendercontext_state_t *)out_ptr);
}

static nmo_result_t nmo_ckrendercontext_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckrendercontext_serialize_internal(
        (const nmo_ckrendercontext_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckrendercontext_vtable = {
    .read = nmo_ckrendercontext_vtable_read,
    .write = nmo_ckrendercontext_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckrendercontext_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckrendercontext_schemas"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(
        arena, "CKRenderContextState",
        sizeof(nmo_ckrendercontext_state_t),
        alignof(nmo_ckrendercontext_state_t));

    nmo_builder_set_vtable(&builder, &nmo_ckrendercontext_vtable);

    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) return result;

    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKRenderContextState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_RENDERCONTEXT, type);
    }

    return result.code == NMO_OK ? nmo_result_ok() : result;
}

nmo_result_t nmo_ckrendercontext_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckrendercontext_state_t *out_state)
{
    return nmo_ckrendercontext_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckrendercontext_serialize(
    const nmo_ckrendercontext_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckrendercontext_serialize_internal(in_state, out_chunk, arena);
}
