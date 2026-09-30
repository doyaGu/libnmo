/**
 * @file ckbehaviorio_schemas.c
 * @brief CKBehaviorIO schema implementation
 *
 * Implements schema-driven deserialization for CKBehaviorIO (behavior I/O endpoints).
 * CKBehaviorIO extends CKObject and is a simple class storing only I/O flags.
 * 
 * Based on official Virtools SDK (reference/src/CKBehaviorIO.cpp:19-48).
 */

#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

static void nmo_behaviorio_set_defaults(void *instance)
{
    nmo_behaviorio_state_t *state = instance;
    state->has_flags = true;
}

static const nmo_object_state_member_t nmo_behaviorio_members[] = {
    NMO_STATE_VALUE(nmo_behaviorio_state_t, old_flags),
    NMO_STATE_VALUE(nmo_behaviorio_state_t, has_flags)
};

static const nmo_object_state_layout_t nmo_behaviorio_layout = {
    .size = sizeof(nmo_behaviorio_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nmo_behaviorio_members,
    .member_count =
        sizeof(nmo_behaviorio_members) / sizeof(nmo_behaviorio_members[0]),
    .set_defaults = nmo_behaviorio_set_defaults,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(behaviorio, nmo_behaviorio_layout)

static nmo_status_t nmo_behaviorio_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_behaviorio_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_behaviorio_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_OBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_behaviorio_state_t, old_flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_behaviorio_state_t, has_flags, CKPGUID_BOOL)
};

/* =============================================================================
 * CKBehaviorIO DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBehaviorIO state from chunk
 * 
 * Implements the symmetric read operation for CKBehaviorIO::Load.
 * Reads I/O flags that determine the endpoint type and characteristics.
 * 
 * Reference: reference/src/CKBehaviorIO.cpp:39-48
 * 
 * @param chunk Chunk containing CKBehaviorIO data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_status_t nmo_behaviorio_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_behaviorio_state_t *out_state = (nmo_behaviorio_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_behaviorio_deserialize");
    }

    /* Read base CKObject state (merged into this chunk by AddChunkAndDelete) */
    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Read I/O flags.  Newly-created states persist this section, while a
     * loaded legacy chunk must retain its absence. */
    out_state->has_flags = false;
    size_t section_dwords = 0;
    result = nmo_chunk_seek_identifier_with_size(
        chunk, CK_STATESAVE_BEHAV_IOFLAGS, &section_dwords);
    if (result == NMO_OK) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        if (section_dwords > 1u) return NMO_ERR_INVALID_FORMAT;
        result = nmo_chunk_read_dword(chunk, &out_state->old_flags);
        if (result != NMO_OK) return result;
        out_state->has_flags = true;
    } else if (result != NMO_ERR_NOT_FOUND) return result;
    /* Note: If identifier not found, old_flags remains 0 (valid for older versions) */

    NMO_RETURN_OK();
}

nmo_status_t nmo_behaviorio_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_behaviorio_state_t *out_state = (nmo_behaviorio_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_behaviorio_state_t decoded;
    nmo_status_t result = nmo_behaviorio_create(&decoded, type, context);
    if (result != NMO_OK) return result;
    result = nmo_behaviorio_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_behaviorio_destroy(&decoded, type, context);
        return result;
    }

    nmo_behaviorio_destroy(out_state, type, context);
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKBehaviorIO SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBehaviorIO state to chunk
 * 
 * Implements the symmetric write operation for CKBehaviorIO::Save.
 * Writes I/O flags that determine the endpoint type.
 * 
 * Reference: reference/src/CKBehaviorIO.cpp:19-36
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_status_t nmo_behaviorio_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_behaviorio_state_t *in_state = (const nmo_behaviorio_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_behaviorio_serialize");
    }
    NMO_RETURN_IF_ERROR(nmo_behaviorio_validate(
        in_state, type, context));

    nmo_status_t result;

    /* Write base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);

    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_BEHAVIOONLY) == 0) {
            return NMO_OK;
        }
    }

    if (!in_state->has_flags) return NMO_OK;

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_IOFLAGS);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->old_flags);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_behaviorio)

nmo_status_t nmo_behaviorio_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_behaviorio_remap_dependencies");
    }

    nmo_behaviorio_state_t *state = (nmo_behaviorio_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));

    return NMO_OK;
}

NMO_DEFINE_OBJECT_PREPARE_CHECKED(nmo_behaviorio)

static nmo_status_t nmo_behaviorio_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_behaviorio_pre_delete");
    }

    nmo_behaviorio_state_t *state = (nmo_behaviorio_state_t *)instance;
    state->old_flags = 0;
    state->has_flags = false;
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static nmo_status_t nmo_behaviorio_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_behaviorio_state_t *state = instance;
    NMO_RETURN_IF_ERROR(nmo_object_vtable.validate(
        &state->base, NULL, context));
    if (!state->has_flags && state->old_flags != 0u) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "BehaviorIO flags are present without their section");
    }
    NMO_RETURN_OK();
}

nmo_type_vtable_t nmo_behaviorio_vtable = {
    .prepare_dependencies = nmo_behaviorio_prepare_dependencies,
    .remap_dependencies = nmo_behaviorio_remap_dependencies,
    .pre_delete = nmo_behaviorio_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_behaviorio_create,
        nmo_behaviorio_destroy,
        nmo_behaviorio_serialize,
        nmo_behaviorio_deserialize,
        nmo_behaviorio_copy,
        nmo_behaviorio_validate,
        nmo_behaviorio_equals,
        nmo_behaviorio_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_behaviorio_type,
    CKPGUID_BEHAVIORIO,
    "CKBehaviorIO",
    NMO_CID_BEHAVIORIO,
    CKPGUID_OBJECT,
    nmo_behaviorio_state_t,
    &nmo_behaviorio_vtable,
    nmo_behaviorio_fields)







