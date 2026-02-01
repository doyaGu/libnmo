/**
 * @file ckkinematicchain_schemas.c
 * @brief CKKinematicChain schema implementation
 */

#include "object/nmo_ckkinematicchain_schemas.h"
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

#define CK_STATESAVE_KINEMATICCHAINALL 0x000000FFu

static nmo_result_t nmo_ckkinematicchain_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckkinematicchain_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckkinematicchain_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckobject_deserialize_fn parent_deserialize = nmo_get_ckobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KINEMATICCHAINALL).code == NMO_OK) {
        out_state->has_chain_data = 1;
        nmo_object_id_t placeholder = 0;
        (void)nmo_chunk_read_object_id(chunk, &placeholder);
        (void)nmo_chunk_read_object_id(chunk, &out_state->start_effector_id);
        (void)nmo_chunk_read_object_id(chunk, &out_state->end_effector_id);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckkinematicchain_serialize_internal(
    const nmo_ckkinematicchain_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckkinematicchain_serialize"));
    }

    nmo_ckobject_serialize_fn parent_serialize = nmo_get_ckobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_chain_data) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KINEMATICCHAINALL);
        if (result.code != NMO_OK) return result;
        nmo_chunk_write_object_id(out_chunk, 0);
        nmo_chunk_write_object_id(out_chunk, in_state->start_effector_id);
        nmo_chunk_write_object_id(out_chunk, in_state->end_effector_id);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckkinematicchain_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckkinematicchain_deserialize_internal(chunk, arena,
        (nmo_ckkinematicchain_state_t *)out_ptr);
}

static nmo_result_t nmo_ckkinematicchain_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckkinematicchain_serialize_internal(
        (const nmo_ckkinematicchain_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckkinematicchain_vtable = {
    .read = nmo_ckkinematicchain_vtable_read,
    .write = nmo_ckkinematicchain_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckkinematicchain_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckkinematicchain_schemas"));
    }

    const nmo_schema_type_t *u32_type = nmo_schema_registry_find_by_name(registry, "u32");
    if (!u32_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required scalar types not found"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(
        arena, "CKKinematicChainState",
        sizeof(nmo_ckkinematicchain_state_t),
        alignof(nmo_ckkinematicchain_state_t));

    nmo_builder_add_field_ex(&builder, "start_effector_id", u32_type,
                            offsetof(nmo_ckkinematicchain_state_t, start_effector_id), 0);
    nmo_builder_add_field_ex(&builder, "end_effector_id", u32_type,
                            offsetof(nmo_ckkinematicchain_state_t, end_effector_id), 0);

    nmo_builder_set_vtable(&builder, &nmo_ckkinematicchain_vtable);

    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) return result;

    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKKinematicChainState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_KINEMATICCHAIN, type);
    }

    return result.code == NMO_OK ? nmo_result_ok() : result;
}

nmo_result_t nmo_ckkinematicchain_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckkinematicchain_state_t *out_state)
{
    return nmo_ckkinematicchain_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckkinematicchain_serialize(
    const nmo_ckkinematicchain_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckkinematicchain_serialize_internal(in_state, out_chunk, arena);
}
