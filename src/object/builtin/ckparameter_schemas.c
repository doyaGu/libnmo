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
        NMO_RETURN_IF_ERROR(nmo_object_vtable.create( \
            &state->base, NULL, context)); \
        nmo_status_t result = nmo_array_init(&state->buffer_data, sizeof(uint8_t), 0, NULL); \
        if (result != NMO_OK) { \
            nmo_object_vtable.destroy(&state->base, NULL, context); \
            return result; \
        } \
        state->mode = CKPARAM_MODE_NONE; \
        state->has_state = false; \
        state->object_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE); \
    } while (0),
    do { \
        nmo_array_dispose(&state->buffer_data); \
        nmo_object_vtable.destroy(&state->base, NULL, context); \
    } while (0))
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
    nmo_parameter_convert_legacy_guid(&out_state->type_guid);

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

static nmo_status_t nmo_parameter_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_parameter_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    const nmo_parameter_state_t *in_state =
        (const nmo_parameter_state_t *)instance;
    if (in_state == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_parameter_validate(in_state, type, context));
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_parameter_serialize_internal(
        in_state, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static nmo_status_t nmo_parameter_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (src == dst) return NMO_OK;
    const nmo_parameter_state_t *s = src;
    nmo_parameter_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_parameter_validate(s, type, NULL));

    nmo_parameter_state_t copied;
    nmo_status_t result = nmo_parameter_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;
    nmo_type_descriptor_t base_type = {
        .size = sizeof(nmo_object_state_t),
    };
    result = nmo_object_vtable.copy(
        &s->base, &copied.base, &base_type, arena);
    if (result != NMO_OK) goto fail;

    copied.type_guid = s->type_guid;
    copied.mode = s->mode;
    copied.has_state = s->has_state;
    copied.object_ref = s->object_ref;
    copied.manager_guid = s->manager_guid;
    copied.manager_value = s->manager_value;
    result = nmo_array_clone(
        &s->buffer_data, &copied.buffer_data,
        &s->buffer_data.allocator);
    if (result != NMO_OK) goto fail;
    result = nmo_object_copy_chunk(
        arena, &copied.subchunk, s->subchunk);
    if (result != NMO_OK) goto fail;

    if (s->buffer_data.data != NULL &&
        d->buffer_data.data == s->buffer_data.data) {
        memset(&d->buffer_data, 0, sizeof(d->buffer_data));
    }
    nmo_parameter_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_parameter_destroy(&copied, NULL, NULL);
    return result;
}

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

static bool nmo_parameter_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_parameter_state_t *lhs =
        (const nmo_parameter_state_t *)a;
    const nmo_parameter_state_t *rhs =
        (const nmo_parameter_state_t *)b;
    if (lhs->base.visibility_flags != rhs->base.visibility_flags ||
        !nmo_guid_equals(lhs->type_guid, rhs->type_guid) ||
        lhs->mode != rhs->mode ||
        lhs->has_state != rhs->has_state ||
        memcmp(&lhs->object_ref, &rhs->object_ref,
               sizeof(nmo_ref_t)) != 0 ||
        !nmo_guid_equals(lhs->manager_guid, rhs->manager_guid) ||
        lhs->manager_value != rhs->manager_value ||
        lhs->buffer_data.count != rhs->buffer_data.count ||
        (lhs->buffer_data.count > 0 &&
         (lhs->buffer_data.data == NULL || rhs->buffer_data.data == NULL ||
          memcmp(lhs->buffer_data.data, rhs->buffer_data.data,
                 lhs->buffer_data.count) != 0)) ||
        ((lhs->subchunk == NULL) != (rhs->subchunk == NULL))) {
        return false;
    }
    if (lhs->subchunk != NULL) {
        size_t lhs_size = 0;
        size_t rhs_size = 0;
        const void *lhs_data = nmo_chunk_get_data(lhs->subchunk, &lhs_size);
        const void *rhs_data = nmo_chunk_get_data(rhs->subchunk, &rhs_size);
        if (lhs_size != rhs_size ||
            (lhs_size > 0 &&
             (lhs_data == NULL || rhs_data == NULL ||
              memcmp(lhs_data, rhs_data, lhs_size) != 0))) {
            return false;
        }
    }
    return true;
}

static uint32_t nmo_parameter_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_parameter_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_parameter_state_t *state =
        (const nmo_parameter_state_t *)instance;
    uint32_t hash = 2166136261u;
#define NMO_PARAMETER_HASH_FIELD(field) \
    hash = nmo_parameter_hash_bytes(hash, &(field), sizeof(field))
    NMO_PARAMETER_HASH_FIELD(state->base.visibility_flags);
    NMO_PARAMETER_HASH_FIELD(state->type_guid);
    NMO_PARAMETER_HASH_FIELD(state->mode);
    NMO_PARAMETER_HASH_FIELD(state->has_state);
    NMO_PARAMETER_HASH_FIELD(state->object_ref);
    NMO_PARAMETER_HASH_FIELD(state->manager_guid);
    NMO_PARAMETER_HASH_FIELD(state->manager_value);
    NMO_PARAMETER_HASH_FIELD(state->buffer_data.count);
#undef NMO_PARAMETER_HASH_FIELD
    if (state->buffer_data.data != NULL && state->buffer_data.count > 0) {
        hash = nmo_parameter_hash_bytes(
            hash, state->buffer_data.data, state->buffer_data.count);
    }
    const uint8_t has_subchunk = state->subchunk != NULL;
    hash = nmo_parameter_hash_bytes(
        hash, &has_subchunk, sizeof(has_subchunk));
    if (state->subchunk != NULL) {
        size_t chunk_size = 0;
        const void *chunk_data = nmo_chunk_get_data(
            state->subchunk, &chunk_size);
        hash = nmo_parameter_hash_bytes(
            hash, &chunk_size, sizeof(chunk_size));
        if (chunk_data != NULL && chunk_size > 0) {
            hash = nmo_parameter_hash_bytes(
                hash, chunk_data, chunk_size);
        }
    }
    return hash;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

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


