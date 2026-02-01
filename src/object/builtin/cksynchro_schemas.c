/**
 * @file cksynchro_schemas.c
 * @brief CKSynchroObject/CKStateObject/CKCriticalSectionObject schemas
 */

#include "object/nmo_cksynchro_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

/* CKDefines2.h identifiers */
#define CK_STATESAVE_SYNCHRODATA 0x00000010u
#define CK_STATESAVE_STATE_DATA  0x00000010u
#define CK_STATESAVE_CRIT_DATA   0x00000010u

static nmo_result_t deserialize_ckobject_base(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckobject_state_t *out_base)
{
    nmo_ckobject_deserialize_fn parent_deserialize = nmo_get_ckobject_deserialize();
    if (!parent_deserialize) {
        return nmo_result_ok();
    }
    return parent_deserialize(chunk, arena, out_base);
}

static nmo_result_t serialize_ckobject_base(
    const nmo_ckobject_state_t *base,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena)
{
    nmo_ckobject_serialize_fn parent_serialize = nmo_get_ckobject_serialize();
    if (!parent_serialize) {
        return nmo_result_ok();
    }
    return parent_serialize(base, chunk, arena);
}

/* =============================================================================
 * CKSynchroObject
 * ============================================================================= */

nmo_result_t nmo_cksynchro_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksynchro_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksynchro_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = deserialize_ckobject_base(chunk, arena, &out_state->base);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SYNCHRODATA).code == NMO_OK) {
        nmo_chunk_read_int(chunk, &out_state->max_waiters);

        size_t count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &count);
        if (result.code == NMO_OK && count > 0) {
            out_state->arrived_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, count * sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t));
            if (out_state->arrived_ids) {
                out_state->arrived_count = (uint32_t)count;
                for (size_t i = 0; i < count; ++i) {
                    nmo_chunk_read_object_sequence_item(chunk, &out_state->arrived_ids[i]);
                }
            }
        }

        count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &count);
        if (result.code == NMO_OK && count > 0) {
            out_state->passed_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, count * sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t));
            if (out_state->passed_ids) {
                out_state->passed_count = (uint32_t)count;
                for (size_t i = 0; i < count; ++i) {
                    nmo_chunk_read_object_sequence_item(chunk, &out_state->passed_ids[i]);
                }
            }
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_cksynchro_serialize(
    const nmo_cksynchro_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksynchro_serialize"));
    }

    nmo_result_t result = serialize_ckobject_base(&in_state->base, out_chunk, arena);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->max_waiters);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->arrived_count);
    if (result.code != NMO_OK) return result;
    for (uint32_t i = 0; i < in_state->arrived_count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, in_state->arrived_ids[i]);
    }

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->passed_count);
    if (result.code != NMO_OK) return result;
    for (uint32_t i = 0; i < in_state->passed_count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, in_state->passed_ids[i]);
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKStateObject
 * ============================================================================= */

nmo_result_t nmo_ckstate_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckstate_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckstate_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = deserialize_ckobject_base(chunk, arena, &out_state->base);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_STATE_DATA).code == NMO_OK) {
        nmo_chunk_read_int(chunk, &out_state->event_flag);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckstate_serialize(
    const nmo_ckstate_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckstate_serialize"));
    }

    nmo_result_t result = serialize_ckobject_base(&in_state->base, out_chunk, arena);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_STATE_DATA);
    if (result.code != NMO_OK) return result;

    return nmo_chunk_write_int(out_chunk, in_state->event_flag);
}

/* =============================================================================
 * CKCriticalSectionObject
 * ============================================================================= */

nmo_result_t nmo_ckcriticalsection_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcriticalsection_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcriticalsection_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = deserialize_ckobject_base(chunk, arena, &out_state->base);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CRIT_DATA).code == NMO_OK) {
        nmo_chunk_read_object_id(chunk, &out_state->object_in_section_id);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckcriticalsection_serialize(
    const nmo_ckcriticalsection_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcriticalsection_serialize"));
    }

    nmo_result_t result = serialize_ckobject_base(&in_state->base, out_chunk, arena);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CRIT_DATA);
    if (result.code != NMO_OK) return result;

    return nmo_chunk_write_object_id(out_chunk, in_state->object_in_section_id);
}

/* =============================================================================
 * Schema registration
 * ============================================================================= */

static nmo_result_t vtable_read_cksynchro(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr)
{
    (void)type;
    return nmo_cksynchro_deserialize(chunk, arena, (nmo_cksynchro_state_t *)out_ptr);
}

static nmo_result_t vtable_write_cksynchro(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena)
{
    (void)type;
    return nmo_cksynchro_serialize((const nmo_cksynchro_state_t *)in_ptr, chunk, arena);
}

static nmo_result_t vtable_read_ckstate(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr)
{
    (void)type;
    return nmo_ckstate_deserialize(chunk, arena, (nmo_ckstate_state_t *)out_ptr);
}

static nmo_result_t vtable_write_ckstate(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckstate_serialize((const nmo_ckstate_state_t *)in_ptr, chunk, arena);
}

static nmo_result_t vtable_read_ckcriticalsection(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr)
{
    (void)type;
    return nmo_ckcriticalsection_deserialize(
        chunk, arena, (nmo_ckcriticalsection_state_t *)out_ptr);
}

static nmo_result_t vtable_write_ckcriticalsection(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckcriticalsection_serialize(
        (const nmo_ckcriticalsection_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_cksynchro_vtable = {
    .read = vtable_read_cksynchro,
    .write = vtable_write_cksynchro,
    .validate = NULL
};

static const nmo_schema_vtable_t nmo_ckstate_vtable = {
    .read = vtable_read_ckstate,
    .write = vtable_write_ckstate,
    .validate = NULL
};

static const nmo_schema_vtable_t nmo_ckcriticalsection_vtable = {
    .read = vtable_read_ckcriticalsection,
    .write = vtable_write_ckcriticalsection,
    .validate = NULL
};

nmo_result_t nmo_register_cksynchro_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_cksynchro_schemas"));
    }

    nmo_schema_builder_t synchro_builder = nmo_builder_struct(
        arena, "CKSynchroState",
        sizeof(nmo_cksynchro_state_t), alignof(nmo_cksynchro_state_t));
    nmo_builder_set_vtable(&synchro_builder, &nmo_cksynchro_vtable);
    nmo_result_t result = nmo_builder_build(&synchro_builder, registry);
    if (result.code != NMO_OK) return result;

    nmo_schema_builder_t state_builder = nmo_builder_struct(
        arena, "CKStateObjectState",
        sizeof(nmo_ckstate_state_t), alignof(nmo_ckstate_state_t));
    nmo_builder_set_vtable(&state_builder, &nmo_ckstate_vtable);
    result = nmo_builder_build(&state_builder, registry);
    if (result.code != NMO_OK) return result;

    nmo_schema_builder_t crit_builder = nmo_builder_struct(
        arena, "CKCriticalSectionState",
        sizeof(nmo_ckcriticalsection_state_t), alignof(nmo_ckcriticalsection_state_t));
    nmo_builder_set_vtable(&crit_builder, &nmo_ckcriticalsection_vtable);
    result = nmo_builder_build(&crit_builder, registry);
    if (result.code != NMO_OK) return result;

    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKSynchroState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_SYNCHRO, type);
        if (result.code != NMO_OK) return result;
    }

    type = nmo_schema_registry_find_by_name(registry, "CKStateObjectState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_STATE, type);
        if (result.code != NMO_OK) return result;
    }

    type = nmo_schema_registry_find_by_name(registry, "CKCriticalSectionState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_CRITICALSECTION, type);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}
