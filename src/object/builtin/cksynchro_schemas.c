/**
 * @file cksynchro_schemas.c
 * @brief CKSynchroObject/CKStateObject/CKCriticalSectionObject schemas
 */

#include "object/nmo_synchro_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    synchro,
    nmo_synchro_state_t,
    do {
        nmo_status_t result = nmo_array_init(&state->arrived_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->passed_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
    } while (0),
    ((void)0))
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(state, nmo_state_state_t)
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(criticalsection, nmo_criticalsection_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_synchro_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_synchro_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_synchro_state_t, max_waiters, CKPGUID_INT),
    NMO_FIELD_REF_ARRAY(nmo_synchro_state_t, arrived_ids),
    NMO_FIELD_REF_ARRAY(nmo_synchro_state_t, passed_ids)
};

static const nmo_type_field_t nmo_state_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_state_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_state_state_t, event_flag, CKPGUID_INT)
};

static const nmo_type_field_t nmo_criticalsection_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_criticalsection_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF(nmo_criticalsection_state_t, object_in_section_id)
};

static nmo_status_t deserialize_ckobject_base(
    nmo_object_state_t *out_base,
    nmo_chunk_t *chunk,
    void *context)
{
    return nmo_object_deserialize(out_base, chunk, NULL, context);
}

static nmo_status_t serialize_ckobject_base(
    const nmo_object_state_t *base,
    nmo_chunk_t *chunk,
    void *context)
{
    return nmo_object_serialize(base, chunk, NULL, context);
}

/* =============================================================================
 * CKSynchroObject
 * ============================================================================= */

nmo_status_t nmo_synchro_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_synchro_state_t *out_state = (nmo_synchro_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_synchro_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SYNCHRODATA) == NMO_OK) {
        nmo_chunk_read_int(chunk, &out_state->max_waiters);

        size_t count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &count);
        if (result == NMO_OK && count > 0) {
            nmo_array_clear(&out_state->arrived_ids);
            result = nmo_array_reserve(&out_state->arrived_ids, count);
            if (result != NMO_OK) return result;

            nmo_object_id_t *arrived_ids = NULL;
            result = nmo_array_extend(&out_state->arrived_ids, count, (void **)&arrived_ids);
            if (result != NMO_OK) return result;

            for (size_t i = 0; i < count; ++i) {
                nmo_chunk_read_object_sequence_item(chunk, &arrived_ids[i]);
            }
        }

        count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &count);
        if (result == NMO_OK && count > 0) {
            nmo_array_clear(&out_state->passed_ids);
            result = nmo_array_reserve(&out_state->passed_ids, count);
            if (result != NMO_OK) return result;

            nmo_object_id_t *passed_ids = NULL;
            result = nmo_array_extend(&out_state->passed_ids, count, (void **)&passed_ids);
            if (result != NMO_OK) return result;

            for (size_t i = 0; i < count; ++i) {
                nmo_chunk_read_object_sequence_item(chunk, &passed_ids[i]);
            }
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    synchro,
    nmo_synchro_state_t,
    nmo_synchro_serialize,
    nmo_synchro_deserialize,
    nmo_synchro_fields,
    CKPGUID_SYNCHRO,
    "CKSynchroObject",
    NMO_CID_SYNCHRO,
    CKPGUID_OBJECT
)

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    state,
    nmo_state_state_t,
    nmo_state_serialize,
    nmo_state_deserialize,
    nmo_state_fields,
    CKPGUID_STATE,
    "CKStateObject",
    NMO_CID_STATE,
    CKPGUID_OBJECT
)

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    criticalsection,
    nmo_criticalsection_state_t,
    nmo_criticalsection_serialize,
    nmo_criticalsection_deserialize,
    nmo_criticalsection_fields,
    CKPGUID_CRITICALSECTION,
    "CKCriticalSectionObject",
    NMO_CID_CRITICALSECTION,
    CKPGUID_OBJECT
)

nmo_status_t nmo_synchro_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_synchro_state_t *in_state = (const nmo_synchro_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_synchro_serialize");
    }

    nmo_status_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_int(out_chunk, in_state->max_waiters);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_object_sequence_start(out_chunk, (uint32_t)in_state->arrived_ids.count);
    if (result != NMO_OK) return result;
    const nmo_object_id_t *arrived_ids = NMO_ARRAY_DATA(nmo_object_id_t, &in_state->arrived_ids);
    for (uint32_t i = 0; i < in_state->arrived_ids.count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, arrived_ids[i]);
    }

    result = nmo_chunk_write_object_sequence_start(out_chunk, (uint32_t)in_state->passed_ids.count);
    if (result != NMO_OK) return result;
    const nmo_object_id_t *passed_ids = NMO_ARRAY_DATA(nmo_object_id_t, &in_state->passed_ids);
    for (uint32_t i = 0; i < in_state->passed_ids.count; ++i) {
        nmo_chunk_write_object_sequence_item(out_chunk, passed_ids[i]);
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKStateObject
 * ============================================================================= */

nmo_status_t nmo_state_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_state_state_t *out_state = (nmo_state_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_state_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SYNCHRODATA) == NMO_OK) {
        nmo_chunk_read_int(chunk, &out_state->event_flag);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_state_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_state_state_t *in_state = (const nmo_state_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_state_serialize");
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

nmo_status_t nmo_criticalsection_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_criticalsection_state_t *out_state = (nmo_criticalsection_state_t *)instance;

    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_criticalsection_deserialize");
    }

    nmo_status_t result = deserialize_ckobject_base(&out_state->base, chunk, context);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SYNCHRODATA) == NMO_OK) {
        nmo_chunk_read_object_id(chunk, &out_state->object_in_section_id);
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_criticalsection_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_criticalsection_state_t *in_state = (const nmo_criticalsection_state_t *)instance;

    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_criticalsection_serialize");
    }

    nmo_status_t result = serialize_ckobject_base(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SYNCHRODATA);
    if (result != NMO_OK) return result;

    return nmo_chunk_write_object_id(out_chunk, in_state->object_in_section_id);
}


