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
 * - Preserves legacy GUID/payload pairs when runtime manager conversion is unavailable
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
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
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
    NMO_FIELD_REF_VALUE(nmo_parameter_state_t, object_ref),
    NMO_FIELD(nmo_parameter_state_t, manager_guid, CKPGUID_GUID),
    NMO_FIELD(nmo_parameter_state_t, manager_value, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_parameter_state_t, subchunk, CKPGUID_STATECHUNK)
};

/* =============================================================================
 * CKParameter IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From CKParameter.cpp */
#define CK_PARAM_IDENTIFIER  0x00000040

nmo_guid_t nmo_parameter_effective_type_guid(
    const nmo_parameter_state_t *state)
{
    if (state == NULL) return NMO_GUID_NULL;
    const nmo_guid_t guid = state->type_guid;
    if (nmo_guid_equals(guid, CKPGUID_OLDMESSAGE)) return CKPGUID_MESSAGE;
    if (nmo_guid_equals(guid, CKPGUID_OLDATTRIBUTE)) return CKPGUID_ATTRIBUTE;
    if (nmo_guid_equals(guid, CKPGUID_ID)) {
        /* The engine maps the old id type to 30EC20AB-6DF6517D. */
        const nmo_guid_t mapped = NMO_GUID_INIT(0x30EC20ABu, 0x6DF6517Du);
        return mapped;
    }
    if (nmo_guid_equals(guid, CKPGUID_OLDTIME)) return CKPGUID_TIME;
    return guid;
}

static void nmo_parameter_check_object_ref(
    nmo_ref_t *ref,
    nmo_guid_t parameter_type_guid,
    void *context)
{
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    const nmo_type_descriptor_t *parameter_type =
        types != NULL
            ? nmo_type_registry_find_by_guid(types, parameter_type_guid)
            : NULL;
    if (parameter_type == NULL || parameter_type->class_id == 0) {
        return;
    }
    nmo_ref_check_class(
        ref,
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context),
        types,
        parameter_type->class_id);
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
static nmo_status_t nmo_parameter_deserialize_internal(
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
    out_state->object_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->manager_guid = NMO_GUID_NULL;
    out_state->manager_value = 0;
    out_state->subchunk = NULL;
    nmo_array_clear(&out_state->buffer_data);

    /* Seek parameter identifier - optional section */
    size_t section_dwords = 0u;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_PARAM_IDENTIFIER, &section_dwords);
    if (result != NMO_OK) {
        /* No parameter data - valid for reference-only objects */
        return result == NMO_ERR_NOT_FOUND ? NMO_OK : result;
    }
    const size_t section_end =
        nmo_chunk_get_position(chunk) + section_dwords;
    if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;

    /* Read parameter type GUID */
    result = nmo_chunk_read_guid(chunk, &out_state->type_guid);
    if (result != NMO_OK) {
        return result;
    }
    /* If no more data in this section after GUID, preserve header-only state. */
    if (nmo_chunk_get_position(chunk) == section_end) {
        out_state->has_state = false;
        NMO_RETURN_OK();
    }
    if (nmo_chunk_get_position(chunk) > section_end) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    /* Read parameter state */
    uint32_t param_state = 0;
    result = nmo_chunk_read_dword(chunk, &param_state);
    if (result != NMO_OK) {
        return result;
    }
    if (nmo_chunk_get_position(chunk) > section_end) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    out_state->has_state = true;

    if (param_state == 3) {
        if (nmo_chunk_get_position(chunk) < section_end) {
            return NMO_ERR_INVALID_FORMAT;
        }
        out_state->mode = CKPARAM_MODE_NONE;
        NMO_RETURN_OK();
    }

    if (param_state == 0) {
        if (nmo_chunk_get_position(chunk) >= section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        nmo_chunk_t *subchunk = NULL;
        result = nmo_chunk_read_sub_chunk(chunk, &subchunk);
        if (result != NMO_OK) {
            return result;
        }
        if (nmo_chunk_get_position(chunk) > section_end) {
            if (subchunk != NULL) nmo_chunk_destroy(subchunk);
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            if (subchunk != NULL) nmo_chunk_destroy(subchunk);
            return NMO_ERR_INVALID_FORMAT;
        }
        out_state->mode = CKPARAM_MODE_SUBCHUNK;
        out_state->subchunk = subchunk;
        return NMO_OK;
    }

    if (param_state == 2) {
        if (nmo_chunk_get_position(chunk) >= section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        nmo_ref_t object_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &object_ref);
        if (result != NMO_OK) return result;
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            return NMO_ERR_INVALID_FORMAT;
        }
        nmo_parameter_check_object_ref(
            &object_ref, nmo_parameter_effective_type_guid(out_state), context);
        out_state->mode = CKPARAM_MODE_OBJECT;
        out_state->object_ref = object_ref;
        NMO_RETURN_OK();
    }

    if (param_state == 1) {
        if (nmo_guid_equals(out_state->type_guid, CKPGUID_PARAMETERTYPE)) {
            if (section_end - nmo_chunk_get_position(chunk) < 2u) {
                return NMO_ERR_TRUNCATED_CHUNK;
            }
            nmo_guid_t type_guid = NMO_GUID_NULL;
            result = nmo_chunk_read_guid(chunk, &type_guid);
            if (result != NMO_OK) {
                return result;
            }
            if (nmo_chunk_get_position(chunk) < section_end) {
                return NMO_ERR_INVALID_FORMAT;
            }
            result = nmo_array_resize(
                &out_state->buffer_data, sizeof(nmo_guid_t));
            if (result != NMO_OK) {
                return result;
            }
            memcpy(out_state->buffer_data.data, &type_guid, sizeof(nmo_guid_t));
            out_state->mode = CKPARAM_MODE_BUFFER;
            return NMO_OK;
        }

        if (nmo_chunk_get_position(chunk) >= section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        void *buffer_ptr = NULL;
        size_t buffer_size = 0;
        result = nmo_chunk_read_buffer(chunk, &buffer_ptr, &buffer_size);
        if (result != NMO_OK) {
            return result;
        }
        if (nmo_chunk_get_position(chunk) > section_end) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        if (nmo_chunk_get_position(chunk) < section_end) {
            return NMO_ERR_INVALID_FORMAT;
        }
        if (buffer_size > 0) {
            result = nmo_array_resize(&out_state->buffer_data, buffer_size);
            if (result != NMO_OK) {
                return result;
            }
            memcpy(out_state->buffer_data.data, buffer_ptr, buffer_size);
        }
        out_state->mode = CKPARAM_MODE_BUFFER;
        return NMO_OK;
    }

    /* Manager-specific int mode: param_state is manager_guid.d1 */
    if (section_end - nmo_chunk_get_position(chunk) < 2u) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    uint32_t manager_guid_d2 = 0u;
    uint32_t manager_value = 0u;
    result = nmo_chunk_read_dword(chunk, &manager_guid_d2);
    if (result != NMO_OK) {
        return result;
    }
    result = nmo_chunk_read_dword(chunk, &manager_value);
    if (result != NMO_OK) {
        return result;
    }
    if (nmo_chunk_get_position(chunk) < section_end) {
        return NMO_ERR_INVALID_FORMAT;
    }
    out_state->mode = CKPARAM_MODE_MANAGER;
    out_state->manager_guid.d1 = param_state;
    out_state->manager_guid.d2 = manager_guid_d2;
    out_state->manager_value = manager_value;

    NMO_RETURN_OK();
}

nmo_status_t nmo_parameter_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_parameter_state_t *out_state = (nmo_parameter_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_parameter_state_t decoded = {0};
    const nmo_allocator_t *allocator =
        out_state->buffer_data.allocator.alloc != NULL
            ? &out_state->buffer_data.allocator : NULL;
    nmo_status_t result = nmo_array_init(
        &decoded.buffer_data, sizeof(uint8_t), 0, allocator);
    if (result != NMO_OK) return result;
    result = nmo_parameter_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded.buffer_data);
        return result;
    }
    nmo_array_dispose(&out_state->buffer_data);
    *out_state = decoded;
    return NMO_OK;
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
static nmo_status_t nmo_parameter_serialize_internal(
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
            nmo_ref_serialized_id(&in_state->object_ref) != NMO_OBJECT_ID_NONE ||
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
            result = nmo_ref_write(out_chunk, &in_state->object_ref);
            if (result != NMO_OK) return result;
            break;

        case CKPARAM_MODE_MANAGER:
            result = nmo_chunk_write_manager_int(
                out_chunk, in_state->manager_guid, in_state->manager_value);
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

static nmo_status_t nmo_parameter_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static void nmo_parameter_set_defaults(void *instance)
{
    nmo_parameter_state_t *state = instance;
    state->mode = CKPARAM_MODE_NONE;
    state->has_state = false;
    state->object_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
}

static const nmo_object_state_member_t nmo_parameter_members[] = {
    NMO_STATE_VALUE(nmo_parameter_state_t, type_guid),
    NMO_STATE_VALUE(nmo_parameter_state_t, mode),
    NMO_STATE_VALUE(nmo_parameter_state_t, has_state),
    NMO_STATE_ARRAY(nmo_parameter_state_t, buffer_data, uint8_t),
    NMO_STATE_VALUE(nmo_parameter_state_t, object_ref),
    NMO_STATE_VALUE(nmo_parameter_state_t, manager_guid),
    NMO_STATE_VALUE(nmo_parameter_state_t, manager_value),
    NMO_STATE_CHUNK(nmo_parameter_state_t, subchunk)
};

static const nmo_object_state_layout_t nmo_parameter_layout = {
    .size = sizeof(nmo_parameter_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nmo_parameter_members,
    .member_count = sizeof(nmo_parameter_members) / sizeof(nmo_parameter_members[0]),
    .set_defaults = nmo_parameter_set_defaults,
    .validate = nmo_parameter_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(parameter, nmo_parameter_layout)

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_VALIDATED(nmo_parameter)

static nmo_status_t nmo_parameter_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_parameter_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_VALIDATE_BYTES(s->buffer_data.data, s->buffer_data.count, "buffer_data");
    if ((s->buffer_data.element_size != 0 &&
         s->buffer_data.element_size != sizeof(uint8_t)) ||
        (s->buffer_data.count > 0 &&
         s->buffer_data.element_size != sizeof(uint8_t))) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->mode < CKPARAM_MODE_SUBCHUNK ||
        s->mode > CKPARAM_MODE_MANAGER) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->mode == CKPARAM_MODE_MANAGER &&
        s->manager_guid.d1 <= CKPARAM_MODE_NONE) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_PREPARE_CHECKED(nmo_parameter)

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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_type_vtable_t nmo_parameter_vtable = {
    .prepare_dependencies = nmo_parameter_prepare_dependencies,
    .remap_dependencies = nmo_parameter_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
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


