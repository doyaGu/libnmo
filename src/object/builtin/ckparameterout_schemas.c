/**
 * @file ckparameterout_schemas.c
 * @brief CKParameterOut schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameterOut.
 *
 * Based on official Virtools SDK (reference/src/CKParameterOut.cpp:120-160).
 */

#include "object/nmo_ckparameterout_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckparameterout, nmo_ckparameterout_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_ckparameterout_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckparameterout_state_t, base),
                    sizeof(nmo_ckparameter_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_ARRAY(nmo_ckparameterout_state_t, destination_ids),
    NMO_FIELD(nmo_ckparameterout_state_t, destination_count, CKPGUID_UINT32)
};

/* =============================================================================
 * CKParameterOut DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameterOut state from chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:145-160
 */
nmo_status_t nmo_ckparameterout_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckparameterout_state_t *out_state = (nmo_ckparameterout_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Read base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_ckparameter_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Read destinations if present */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS) == NMO_OK) {
        int32_t count;
        nmo_status_t result = nmo_chunk_read_int(chunk, &count);
        if (result == NMO_OK && count > 0) {
            out_state->destination_count = (uint32_t)count;
            out_state->destination_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, count * sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t));

            if (out_state->destination_ids) {
                for (int32_t i = 0; i < count; i++) {
                    nmo_chunk_read_object_id(chunk, &out_state->destination_ids[i]);
                }
            }
        }
    }

    NMO_RETURN_OK();
}

/**
 * @brief Serialize CKParameterOut state to chunk
 *
 * Reference: reference/src/CKParameterOut.cpp:130-142
 */
nmo_status_t nmo_ckparameterout_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckparameterout_state_t *in_state = (const nmo_ckparameterout_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    /* Write base CKParameter state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_ckparameter_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Write destinations if any */
    if (in_state->destination_count > 0 && in_state->destination_ids) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->destination_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->destination_count; i++) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->destination_ids[i]);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t ckparameterout_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_ckparameterout_state_t *s = src;
    nmo_ckparameterout_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.buffer_data,
                                              s->base.buffer_data, s->base.buffer_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &d->base.subchunk, s->base.subchunk));
    return nmo_object_copy_array(arena, (void **)&d->destination_ids,
                                 s->destination_ids, sizeof(nmo_object_id_t), s->destination_count);
}

static nmo_status_t ckparameterout_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_ckparameterout_state_t *s = instance;
    NMO_VALIDATE_BYTES(s->base.buffer_data, s->base.buffer_size, "buffer_data");
    NMO_VALIDATE_COUNT(s->destination_ids, s->destination_count, "destination_ids");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    ckparameterout,
    nmo_ckparameterout_state_t,
    nmo_ckparameterout_serialize,
    nmo_ckparameterout_deserialize,
    nmo_ckparameterout_fields,
    CKPGUID_PARAMETEROUT,
    "CKParameterOut",
    NMO_CID_PARAMETEROUT,
    CKPGUID_PARAMETER
)


