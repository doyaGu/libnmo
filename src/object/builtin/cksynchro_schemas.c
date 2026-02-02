/**
 * @file cksynchro_schemas.c
 * @brief CKSynchroObject/CKStateObject/CKCriticalSectionObject schemas
 */

#include "object/nmo_cksynchro_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
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
    nmo_ckobject_state_t *out_base,
    nmo_chunk_t *chunk,
    void *context)
{
    return nmo_ckobject_deserialize(out_base, chunk, NULL, context);
}

static nmo_result_t serialize_ckobject_base(
    const nmo_ckobject_state_t *base,
    nmo_chunk_t *chunk,
    void *context)
{
    return nmo_ckobject_serialize(base, chunk, NULL, context);
}

/* =============================================================================
 * CKSynchroObject
 * ============================================================================= */

nmo_result_t nmo_cksynchro_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cksynchro_state_t *out_state = (nmo_cksynchro_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksynchro_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    cksynchro,
    nmo_cksynchro_state_t,
    nmo_cksynchro_serialize,
    nmo_cksynchro_deserialize,
    NMO_GUID_CKSYNCHRO,
    "CKSynchroObject",
    NMO_CID_SYNCHRO,
    NMO_GUID_CKOBJECT
)

NMO_DEFINE_OBJECT_SCHEMA(
    ckstate,
    nmo_ckstate_state_t,
    nmo_ckstate_serialize,
    nmo_ckstate_deserialize,
    NMO_GUID_CKSTATE,
    "CKStateObject",
    NMO_CID_STATE,
    NMO_GUID_CKOBJECT
)

NMO_DEFINE_OBJECT_SCHEMA(
    ckcriticalsection,
    nmo_ckcriticalsection_state_t,
    nmo_ckcriticalsection_serialize,
    nmo_ckcriticalsection_deserialize,
    NMO_GUID_CKCRITICALSECTION,
    "CKCriticalSectionObject",
    NMO_CID_CRITICALSECTION,
    NMO_GUID_CKOBJECT
)

nmo_result_t nmo_cksynchro_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cksynchro_state_t *in_state = (const nmo_cksynchro_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksynchro_serialize"));
    }

    nmo_result_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
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
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckstate_state_t *out_state = (nmo_ckstate_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckstate_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_STATE_DATA).code == NMO_OK) {
        nmo_chunk_read_int(chunk, &out_state->event_flag);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckstate_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckstate_state_t *in_state = (const nmo_ckstate_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckstate_serialize"));
    }

    nmo_result_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_STATE_DATA);
    if (result.code != NMO_OK) return result;

    return nmo_chunk_write_int(out_chunk, in_state->event_flag);
}

/* =============================================================================
 * CKCriticalSectionObject
 * ============================================================================= */

nmo_result_t nmo_ckcriticalsection_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckcriticalsection_state_t *out_state = (nmo_ckcriticalsection_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcriticalsection_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CRIT_DATA).code == NMO_OK) {
        nmo_chunk_read_object_id(chunk, &out_state->object_in_section_id);
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckcriticalsection_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckcriticalsection_state_t *in_state = (const nmo_ckcriticalsection_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcriticalsection_serialize"));
    }

    nmo_result_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CRIT_DATA);
    if (result.code != NMO_OK) return result;

    return nmo_chunk_write_object_id(out_chunk, in_state->object_in_section_id);
}

