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
 * - Handles GUID migrations: OLDMESSAGE->MESSAGE, OLDATTRIBUTE->ATTRIBUTE, ID->OBJECT, OLDTIME->TIME
 * 
 * Key design decisions:
 * - Store raw buffer data for round-trip safety
 * - Preserve original GUID before migration
 * - Support all 5 storage modes from reference implementation
 */

#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "object/builtin/nmo_object_schemas.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_repository.h"
#include "nmo_types.h"

NMO_DEFINE_OBJECT_LIFECYCLE(
    parameter,
    nmo_parameter_state_t,
    do { \
        nmo_status_t result = nmo_array_init(&state->buffer_data, sizeof(uint8_t), 0, NULL); \
        if (result != NMO_OK) return result; \
        state->mode = CKPARAM_MODE_NONE; \
        state->has_state = false; \
    } while (0),
    ((void)0))
#include <stddef.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_parameter_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_parameter_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_parameter_state_t, type_guid, CKPGUID_GUID),
    NMO_FIELD(nmo_parameter_state_t, mode, NMO_GUID_ENUM_CK_PARAMETER_MODE),
    NMO_FIELD(nmo_parameter_state_t, has_state, CKPGUID_BOOL),
    NMO_FIELD_ARRAY(nmo_parameter_state_t, buffer_data, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_parameter_state_t, object_id),
    NMO_FIELD(nmo_parameter_state_t, manager_guid, CKPGUID_GUID),
    NMO_FIELD(nmo_parameter_state_t, manager_value, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_parameter_state_t, subchunk, CKPGUID_STATECHUNK)
};

/* =============================================================================
 * CKParameter IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From CKParameter.cpp */
#define CK_PARAM_IDENTIFIER  0x00000040

static void nmo_parameter_convert_legacy_guid(nmo_guid_t *guid)
{
    if (guid == NULL) {
        return;
    }

    if (nmo_guid_equals(*guid, CKPGUID_OLDMESSAGE)) {
        *guid = CKPGUID_MESSAGE;
    } else if (nmo_guid_equals(*guid, CKPGUID_OLDATTRIBUTE)) {
        *guid = CKPGUID_ATTRIBUTE;
    } else if (nmo_guid_equals(*guid, CKPGUID_ID)) {
        *guid = CKPGUID_OBJECT;
    } else if (nmo_guid_equals(*guid, CKPGUID_OLDTIME)) {
        *guid = CKPGUID_TIME;
    }
}

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
nmo_status_t nmo_parameter_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_parameter_state_t *out_state = (nmo_parameter_state_t *)instance;
    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_parameter_deserialize");
    }

    /* Read base CKObject state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Reset parameter payload state */
    out_state->mode = CKPARAM_MODE_NONE;
    out_state->has_state = false;
    out_state->object_id = 0;
    out_state->manager_guid = NMO_GUID_NULL;
    out_state->manager_value = 0;
    out_state->subchunk = NULL;
    nmo_array_clear(&out_state->buffer_data);

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
    nmo_parameter_convert_legacy_guid(&out_state->type_guid);

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
        out_state->mode = CKPARAM_MODE_NONE;
        NMO_RETURN_OK();
    }

    if (param_state == 0) {
        out_state->mode = CKPARAM_MODE_SUBCHUNK;
        result = nmo_chunk_read_sub_chunk(chunk, &out_state->subchunk);
        if (result != NMO_OK) {
            return result;
        }
        return NMO_OK;
    }

    if (param_state == 2) {
        out_state->mode = CKPARAM_MODE_OBJECT;
        result = nmo_chunk_read_object_id(chunk, &out_state->object_id);
        if (result != NMO_OK) {
            return result;
        }
        NMO_RETURN_OK();
    }

    if (param_state == 1) {
        out_state->mode = CKPARAM_MODE_BUFFER;
        if (nmo_guid_equals(out_state->type_guid, CKPGUID_PARAMETERTYPE)) {
            nmo_guid_t type_guid = NMO_GUID_NULL;
            result = nmo_chunk_read_guid(chunk, &type_guid);
            if (result != NMO_OK) {
                return result;
            }
            result = nmo_array_alloc(&out_state->buffer_data, sizeof(uint8_t), sizeof(nmo_guid_t), NULL);
            if (result != NMO_OK) {
                return result;
            }
            memcpy(out_state->buffer_data.data, &type_guid, sizeof(nmo_guid_t));
            return NMO_OK;
        }

        void *buffer_ptr = NULL;
        size_t buffer_size = 0;
        result = nmo_chunk_read_buffer(chunk, &buffer_ptr, &buffer_size);
        if (result != NMO_OK) {
            return result;
        }
        if (buffer_size > 0) {
            result = nmo_array_alloc(&out_state->buffer_data, sizeof(uint8_t), buffer_size, NULL);
            if (result != NMO_OK) {
                return result;
            }
            memcpy(out_state->buffer_data.data, buffer_ptr, buffer_size);
        }
        return NMO_OK;
    }

    /* Manager-specific int mode: param_state is manager_guid.d1 */
    out_state->mode = CKPARAM_MODE_MANAGER;
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
nmo_status_t nmo_parameter_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_parameter_state_t *in_state = (const nmo_parameter_state_t *)instance;
    nmo_status_t result;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_parameter_serialize");
    }

    /* Write base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
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
        if (in_state->mode != CKPARAM_MODE_NONE ||
            in_state->buffer_data.count > 0 ||
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
        case CKPARAM_MODE_NONE:
            result = nmo_chunk_write_dword(out_chunk, 3);
            if (result != NMO_OK) return result;
            break;

        case CKPARAM_MODE_SUBCHUNK:
            result = nmo_chunk_write_dword(out_chunk, 0);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(out_chunk, in_state->subchunk);
            if (result != NMO_OK) return result;
            break;

        case CKPARAM_MODE_OBJECT:
            result = nmo_chunk_write_dword(out_chunk, 2);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->object_id);
            if (result != NMO_OK) return result;
            break;

        case CKPARAM_MODE_MANAGER:
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_guid.d1);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_guid.d2);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_value);
            if (result != NMO_OK) return result;
            break;

        case CKPARAM_MODE_BUFFER:
        default:
            result = nmo_chunk_write_dword(out_chunk, 1);
            if (result != NMO_OK) return result;
            if (nmo_guid_equals(in_state->type_guid, CKPGUID_PARAMETERTYPE)) {
                if (in_state->buffer_data.count != 0 &&
                    in_state->buffer_data.count != sizeof(nmo_guid_t)) {
                    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                     "CKParameter: invalid PARAMETERTYPE payload size");
                }
                nmo_guid_t type_guid = NMO_GUID_NULL;
                if (in_state->buffer_data.count == sizeof(nmo_guid_t) && in_state->buffer_data.data) {
                    memcpy(&type_guid, in_state->buffer_data.data, sizeof(nmo_guid_t));
                }
                result = nmo_chunk_write_guid(out_chunk, type_guid);
                if (result != NMO_OK) return result;
            } else {
                result = nmo_chunk_write_buffer(out_chunk,
                    (in_state->buffer_data.data && in_state->buffer_data.count > 0)
                        ? in_state->buffer_data.data
                        : NULL,
                    in_state->buffer_data.count);
                if (result != NMO_OK) return result;
            }
            break;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_parameter_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_parameter_state_t *s = src;
    nmo_parameter_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->buffer_data, &d->buffer_data, &s->buffer_data.allocator));
    return nmo_object_copy_chunk(arena, &d->subchunk, s->subchunk);
}

static nmo_status_t nmo_parameter_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_parameter_state_t *s = instance;
    NMO_VALIDATE_BYTES(s->buffer_data.data, s->buffer_data.count, "buffer_data");
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameter_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameter_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_parameter_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameter_remap_dependencies");
    }

    nmo_parameter_state_t *state = (nmo_parameter_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    /* Preserve the selected payload lane and unresolved object ID. */
    return nmo_parameter_validate(state, NULL, NULL);
}

static nmo_status_t nmo_parameter_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_parameter_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_parameter_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(parameter, nmo_parameter_state_t)

nmo_type_vtable_t nmo_parameter_vtable = {
    .prepare_dependencies = nmo_parameter_prepare_dependencies,
    .remap_dependencies = nmo_parameter_remap_dependencies,
    .pre_delete = nmo_parameter_pre_delete,
    .post_delete = nmo_parameter_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_parameter_create,
        nmo_parameter_destroy,
        nmo_parameter_serialize,
        nmo_parameter_deserialize,
        nmo_parameter_copy,
        nmo_parameter_validate,
        nmo_parameter_equals,
        nmo_parameter_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_parameter_type,
    CKPGUID_PARAMETER,
    "CKParameter",
    NMO_CID_PARAMETER,
    CKPGUID_OBJECT,
    nmo_parameter_state_t,
    &nmo_parameter_vtable,
    nmo_parameter_fields)

/* =============================================================================
 * PARAMETER STATE HELPERS
 * ============================================================================= */

nmo_parameter_state_t *nmo_parameter_get_mutable_state(nmo_object_t *obj)
{
    if (!obj) return NULL;
    void *raw = nmo_object_get_state(obj);
    if (!raw) return NULL;

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (cid == NMO_CID_PARAMETER)
        return (nmo_parameter_state_t *)raw;
    if (cid == NMO_CID_PARAMETEROUT)
        return &((nmo_parameterout_state_t *)raw)->base;
    if (cid == NMO_CID_PARAMETERLOCAL)
        return &((nmo_parameterlocal_state_t *)raw)->base;
    return NULL;
}

const nmo_parameter_state_t *nmo_parameter_get_state(const nmo_object_t *obj)
{
    return nmo_parameter_get_mutable_state((nmo_object_t *)obj);
}

nmo_status_t nmo_parameter_get_value(const nmo_object_t *obj,
                                     const nmo_type_registry_t *registry,
                                     char *out_buf,
                                     size_t buf_size)
{
    const nmo_parameter_state_t *pstate = nmo_parameter_get_state(obj);
    if (!pstate || !registry || !out_buf || buf_size == 0)
        return NMO_ERR_INVALID_ARGUMENT;
    if (!pstate->buffer_data.data || pstate->buffer_data.count == 0)
        return NMO_ERR_INVALID_STATE;

    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_guid(registry, pstate->type_guid);
    if (!type) return NMO_ERR_NOT_FOUND;

    return nmo_type_value_to_string(pstate->buffer_data.data,
                                    type, registry, out_buf, buf_size);
}


