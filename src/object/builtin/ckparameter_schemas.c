/**
 * @file ckparameter_schemas.c
 * @brief CKParameter schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameter (parameter values).
 * CKParameter extends CKObject and stores typed data in a buffer.
 * 
 * Based on official Virtools SDK (reference/src/CKParameter.cpp:245-450):
 * - CKParameter::Save writes: identifier(0x40), GUID, mode, data
 * - CKParameter::Load reads: GUID (with migration), mode, data
 * - Supports 5 storage modes: buffer, object reference, manager int, sub-chunk, none
 * - Handles GUID migrations: OLDMESSAGE��MESSAGE, OLDATTRIBUTE��ATTRIBUTE, ID��OBJECT, OLDTIME��TIME
 * 
 * Key design decisions:
 * - Store raw buffer data for round-trip safety
 * - Preserve original GUID before migration
 * - Support all 5 storage modes from reference implementation
 */

#include "object/nmo_ckparameter_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"

NMO_DEFINE_OBJECT_LIFECYCLE(
    ckparameter,
    nmo_ckparameter_state_t,
    do { \
        state->mode = NMO_CKPARAM_MODE_NONE; \
        state->has_state = false; \
    } while (0),
    ((void)0))
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_ckparameter_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckparameter_state_t, base),
                    sizeof(nmo_ckobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_ckparameter_state_t, type_guid, CKPGUID_GUID),
    NMO_FIELD(nmo_ckparameter_state_t, mode, CKPGUID_UINT32),
    NMO_FIELD(nmo_ckparameter_state_t, has_state, CKPGUID_BOOL),
    NMO_FIELD_ARRAY(nmo_ckparameter_state_t, buffer_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_ckparameter_state_t, buffer_size, CKPGUID_UINT64),
    NMO_FIELD_REF(nmo_ckparameter_state_t, object_id),
    NMO_FIELD(nmo_ckparameter_state_t, manager_guid, CKPGUID_GUID),
    NMO_FIELD(nmo_ckparameter_state_t, manager_value, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_ckparameter_state_t, subchunk, CKPGUID_STATECHUNK)
};

/* =============================================================================
 * CKParameter IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From CKParameter.cpp */
#define CK_PARAM_IDENTIFIER  0x00000040

/* =============================================================================
 * CKParameter DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameter state from chunk
 * 
 * Implements the symmetric read operation for CKParameter::Load.
 * Reads parameter GUID, storage mode, and data.
 * 
 * Reference: reference/src/CKParameter.cpp:300-450
 * 
 * @param chunk Chunk containing CKParameter data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckparameter_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckparameter_state_t *out_state = (nmo_ckparameter_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckparameter_deserialize");
    }

    /* Read base CKObject state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Seek parameter identifier - optional section */
    result = nmo_chunk_seek_identifier(chunk, CK_PARAM_IDENTIFIER);
    if (result != NMO_OK) {
        /* No parameter data - valid for reference-only objects */
        NMO_RETURN_OK();
    }

    /* Read parameter type GUID */
    result = nmo_chunk_read_guid(chunk, &out_state->type_guid);
    if (result != NMO_OK) {
        return result;
    }

    /* If no more data after GUID, preserve header-only state */
    {
        size_t data_size = nmo_chunk_get_data_size(chunk);
        size_t pos_dwords = nmo_chunk_get_position(chunk);
        size_t pos_bytes = pos_dwords * sizeof(uint32_t);
        if (pos_bytes >= data_size) {
            out_state->has_state = false;
            NMO_RETURN_OK();
        }
    }

    /* Read parameter state */
    uint32_t param_state = 0;
    result = nmo_chunk_read_dword(chunk, &param_state);
    if (result != NMO_OK) {
        return result;
    }
    out_state->has_state = true;

    if (param_state == 3) {
        out_state->mode = NMO_CKPARAM_MODE_NONE;
        NMO_RETURN_OK();
    }

    if (param_state == 0) {
        out_state->mode = NMO_CKPARAM_MODE_SUBCHUNK;
        result = nmo_chunk_read_sub_chunk(chunk, &out_state->subchunk);
        if (result != NMO_OK) {
            out_state->subchunk = NULL;
        }
        NMO_RETURN_OK();
    }

    if (param_state == 2) {
        out_state->mode = NMO_CKPARAM_MODE_OBJECT;
        result = nmo_chunk_read_object_id(chunk, &out_state->object_id);
        if (result != NMO_OK) {
            return result;
        }
        NMO_RETURN_OK();
    }

    if (param_state == 1) {
        out_state->mode = NMO_CKPARAM_MODE_BUFFER;
        void *buffer_ptr = NULL;
        size_t buffer_size = 0;
        result = nmo_chunk_read_buffer(chunk, &buffer_ptr, &buffer_size);
        if (result == NMO_OK && buffer_size > 0) {
            out_state->buffer_data = (uint8_t *)nmo_arena_alloc(arena, buffer_size, 1);
            if (out_state->buffer_data) {
                memcpy(out_state->buffer_data, buffer_ptr, buffer_size);
                out_state->buffer_size = buffer_size;
            }
        }
        NMO_RETURN_OK();
    }

    /* Manager-specific int mode: param_state is manager_guid.d1 */
    out_state->mode = NMO_CKPARAM_MODE_MANAGER;
    out_state->manager_guid.d1 = param_state;
    result = nmo_chunk_read_dword(chunk, &out_state->manager_guid.d2);
    if (result != NMO_OK) {
        return result;
    }
    result = nmo_chunk_read_dword(chunk, &out_state->manager_value);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKParameter SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKParameter state to chunk
 * 
 * Implements the symmetric write operation for CKParameter::Save.
 * Writes parameter GUID, storage mode, and data.
 * 
 * Reference: reference/src/CKParameter.cpp:245-298
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckparameter_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckparameter_state_t *in_state = (const nmo_ckparameter_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckparameter_serialize");
    }

    /* Write base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Write parameter identifier */
    result = nmo_chunk_write_identifier(out_chunk, CK_PARAM_IDENTIFIER);
    if (result != NMO_OK) return result;

    /* Write parameter type GUID */
    result = nmo_chunk_write_guid(out_chunk, in_state->type_guid);
    if (result != NMO_OK) return result;

    /* Write parameter state and payload if present */
    if (!in_state->has_state) {
        bool inferred_has_state = false;
        if (in_state->mode != NMO_CKPARAM_MODE_NONE ||
            in_state->buffer_size > 0 ||
            in_state->object_id != 0 ||
            in_state->subchunk != NULL ||
            in_state->manager_guid.d1 != 0 ||
            in_state->manager_guid.d2 != 0 ||
            in_state->manager_value != 0) {
            inferred_has_state = true;
        }
        if (!inferred_has_state) {
            NMO_RETURN_OK();
        }
    }

    /* Write parameter state and payload */
    switch (in_state->mode) {
        case NMO_CKPARAM_MODE_NONE:
            result = nmo_chunk_write_dword(out_chunk, 3);
            if (result != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_SUBCHUNK:
            result = nmo_chunk_write_dword(out_chunk, 0);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(out_chunk, in_state->subchunk);
            if (result != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_OBJECT:
            result = nmo_chunk_write_dword(out_chunk, 2);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->object_id);
            if (result != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_MANAGER:
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_guid.d1);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_guid.d2);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_value);
            if (result != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_BUFFER:
        default:
            result = nmo_chunk_write_dword(out_chunk, 1);
            if (result != NMO_OK) return result;
            if (in_state->buffer_data && in_state->buffer_size > 0) {
                result = nmo_chunk_write_buffer(out_chunk, in_state->buffer_data,
                    in_state->buffer_size);
                if (result != NMO_OK) return result;
            }
            break;
    }

    NMO_RETURN_OK();
}

static nmo_status_t ckparameter_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_ckparameter_state_t *s = src;
    nmo_ckparameter_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->buffer_data,
                                              s->buffer_data, s->buffer_size));
    return nmo_object_copy_chunk(arena, &d->subchunk, s->subchunk);
}

static nmo_status_t ckparameter_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_ckparameter_state_t *s = instance;
    NMO_VALIDATE_BYTES(s->buffer_data, s->buffer_size, "buffer_data");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    ckparameter,
    nmo_ckparameter_state_t,
    nmo_ckparameter_serialize,
    nmo_ckparameter_deserialize,
    nmo_ckparameter_fields,
    CKPGUID_PARAMETER,
    "CKParameter",
    NMO_CID_PARAMETER,
    CKPGUID_OBJECT
)


