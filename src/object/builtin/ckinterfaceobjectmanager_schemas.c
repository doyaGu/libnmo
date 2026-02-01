/**
 * @file ckinterfaceobjectmanager_schemas.c
 * @brief CKInterfaceObjectManager schema implementation
 */

#include "object/nmo_ckinterfaceobjectmanager_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* Identifiers from CKInterfaceObjectManager.cpp */
#define CK_STATESAVE_IOM_CHUNKS 0x01234567u
#define CK_STATESAVE_IOM_GUID   0x87654321u

static nmo_result_t nmo_ckinterfaceobjectmanager_deserialize_internal(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckinterfaceobjectmanager_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckinterfaceobjectmanager_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ckobject_deserialize_fn parent_deserialize = nmo_get_ckobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_IOM_CHUNKS).code == NMO_OK) {
        int32_t count = 0;
        nmo_result_t result = nmo_chunk_read_int(chunk, &count);
        if (result.code == NMO_OK && count > 0) {
            out_state->chunk_count = count;
            out_state->chunks = (nmo_chunk_t **)nmo_arena_alloc(
                arena, sizeof(nmo_chunk_t *) * (size_t)count, _Alignof(nmo_chunk_t *));
            if (out_state->chunks) {
                for (int32_t i = 0; i < count; ++i) {
                    (void)nmo_chunk_read_sub_chunk(chunk, &out_state->chunks[i]);
                }
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_IOM_GUID).code == NMO_OK) {
        (void)nmo_chunk_read_guid(chunk, &out_state->guid);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckinterfaceobjectmanager_serialize_internal(
    const nmo_ckinterfaceobjectmanager_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckinterfaceobjectmanager_serialize"));
    }

    nmo_ckobject_serialize_fn parent_serialize = nmo_get_ckobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) return result;
    }

    nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_IOM_CHUNKS);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->chunk_count);
    if (result.code != NMO_OK) return result;

    for (int32_t i = 0; i < in_state->chunk_count; ++i) {
        nmo_chunk_t *sub = NULL;
        if (in_state->chunks && i >= 0) {
            sub = in_state->chunks[i];
        }
        if (!sub) {
            sub = nmo_chunk_create(arena);
        }
        result = nmo_chunk_write_sub_chunk(out_chunk, sub);
        if (result.code != NMO_OK) return result;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_IOM_GUID);
    if (result.code != NMO_OK) return result;

    return nmo_chunk_write_guid(out_chunk, in_state->guid);
}

static nmo_result_t vtable_read_ckinterfaceobjectmanager(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr)
{
    (void)type;
    return nmo_ckinterfaceobjectmanager_deserialize_internal(
        chunk, arena, (nmo_ckinterfaceobjectmanager_state_t *)out_ptr);
}

static nmo_result_t vtable_write_ckinterfaceobjectmanager(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckinterfaceobjectmanager_serialize_internal(
        (const nmo_ckinterfaceobjectmanager_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckinterfaceobjectmanager_vtable = {
    .read = vtable_read_ckinterfaceobjectmanager,
    .write = vtable_write_ckinterfaceobjectmanager,
    .validate = NULL
};

nmo_result_t nmo_register_ckinterfaceobjectmanager_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckinterfaceobjectmanager_schemas"));
    }

    nmo_schema_builder_t builder = nmo_builder_struct(
        arena,
        "CKInterfaceObjectManagerState",
        sizeof(nmo_ckinterfaceobjectmanager_state_t),
        alignof(nmo_ckinterfaceobjectmanager_state_t));

    nmo_builder_set_vtable(&builder, &nmo_ckinterfaceobjectmanager_vtable);

    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) return result;

    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(
        registry, "CKInterfaceObjectManagerState");
    if (type) {
        result = nmo_schema_registry_map_class_id(
            registry, NMO_CID_INTERFACEOBJECTMANAGER, type);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckinterfaceobjectmanager_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckinterfaceobjectmanager_state_t *out_state)
{
    return nmo_ckinterfaceobjectmanager_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckinterfaceobjectmanager_serialize(
    const nmo_ckinterfaceobjectmanager_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckinterfaceobjectmanager_serialize_internal(in_state, out_chunk, arena);
}
