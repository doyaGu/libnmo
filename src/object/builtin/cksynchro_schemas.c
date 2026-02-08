/**
 * @file cksynchro_schemas.c
 * @brief CKSynchroObject/CKStateObject/CKCriticalSectionObject schemas
 */

#include "object/nmo_cksynchro_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cksynchro, nmo_cksynchro_state_t)
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckstate, nmo_ckstate_state_t)
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckcriticalsection, nmo_ckcriticalsection_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_cksynchro_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_cksynchro_state_t, base),
                    sizeof(nmo_ckobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_cksynchro_state_t, max_waiters, CKPGUID_INT),
    NMO_FIELD_REF_ARRAY(nmo_cksynchro_state_t, arrived_ids),
    NMO_FIELD(nmo_cksynchro_state_t, arrived_count, CKPGUID_UINT32),
    NMO_FIELD_REF_ARRAY(nmo_cksynchro_state_t, passed_ids),
    NMO_FIELD(nmo_cksynchro_state_t, passed_count, CKPGUID_UINT32)
};

static const nmo_type_field_t nmo_ckstate_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckstate_state_t, base),
                    sizeof(nmo_ckobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_ckstate_state_t, event_flag, CKPGUID_INT)
};

static const nmo_type_field_t nmo_ckcriticalsection_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckcriticalsection_state_t, base),
                    sizeof(nmo_ckobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF(nmo_ckcriticalsection_state_t, object_in_section_id)
};

static nmo_status_t deserialize_ckobject_base(
    nmo_ckobject_state_t *out_base,
    nmo_chunk_t *chunk,
    void *context)
{
    return nmo_ckobject_deserialize(out_base, chunk, NULL, context);
}

static nmo_status_t serialize_ckobject_base(
    const nmo_ckobject_state_t *base,
    nmo_chunk_t *chunk,
    void *context)
{
    return nmo_ckobject_serialize(base, chunk, NULL, context);
}

/* =============================================================================
 * CKSynchroObject
 * ============================================================================= */

nmo_status_t nmo_cksynchro_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cksynchro_state_t *out_state = (nmo_cksynchro_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksynchro_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SYNCHRODATA) == NMO_OK) {
        nmo_chunk_read_int(chunk, &out_state->max_waiters);

        size_t count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &count);
        if (result == NMO_OK && count > 0) {
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
        if (result == NMO_OK && count > 0) {
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

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    cksynchro,
    nmo_cksynchro_state_t,
    nmo_cksynchro_serialize,
    nmo_cksynchro_deserialize,
    nmo_cksynchro_fields,
    CKPGUID_SYNCHRO,
    "CKSynchroObject",
    NMO_CID_SYNCHRO,
    CKPGUID_OBJECT
)

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    ckstate,
    nmo_ckstate_state_t,
    nmo_ckstate_serialize,
    nmo_ckstate_deserialize,
    nmo_ckstate_fields,
    CKPGUID_STATE,
    "CKStateObject",
    NMO_CID_STATE,
    CKPGUID_OBJECT
)

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    ckcriticalsection,
    nmo_ckcriticalsection_state_t,
    nmo_ckcriticalsection_serialize,
    nmo_ckcriticalsection_deserialize,
    nmo_ckcriticalsection_fields,
    CKPGUID_CRITICALSECTION,
    "CKCriticalSectionObject",
    NMO_CID_CRITICALSECTION,
    CKPGUID_OBJECT
)

nmo_status_t nmo_cksynchro_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cksynchro_state_t *in_state = (const nmo_cksynchro_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksynchro_serialize");
    }

    nmo_status_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->max_waiters);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->arrived_count);
    if (result != NMO_OK) return result;
    for (uint32_t i = 0; i < in_state->arrived_count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, in_state->arrived_ids[i]);
    }

    result = nmo_chunk_write_object_sequence_start(out_chunk, in_state->passed_count);
    if (result != NMO_OK) return result;
    for (uint32_t i = 0; i < in_state->passed_count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, in_state->passed_ids[i]);
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKStateObject
 * ============================================================================= */

nmo_status_t nmo_ckstate_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckstate_state_t *out_state = (nmo_ckstate_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckstate_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SYNCHRODATA) == NMO_OK) {
        nmo_chunk_read_int(chunk, &out_state->event_flag);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_ckstate_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckstate_state_t *in_state = (const nmo_ckstate_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckstate_serialize");
    }

    nmo_status_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result != NMO_OK) return result;

    return nmo_chunk_write_int(out_chunk, in_state->event_flag);
}

/* =============================================================================
 * CKCriticalSectionObject
 * ============================================================================= */

nmo_status_t nmo_ckcriticalsection_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckcriticalsection_state_t *out_state = (nmo_ckcriticalsection_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcriticalsection_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SYNCHRODATA) == NMO_OK) {
        nmo_chunk_read_object_id(chunk, &out_state->object_in_section_id);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_ckcriticalsection_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckcriticalsection_state_t *in_state = (const nmo_ckcriticalsection_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcriticalsection_serialize");
    }

    nmo_status_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result != NMO_OK) return result;

    return nmo_chunk_write_object_id(out_chunk, in_state->object_in_section_id);
}


