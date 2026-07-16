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

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(behaviorio, nmo_behaviorio_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_behaviorio_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_behaviorio_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
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
nmo_status_t nmo_behaviorio_deserialize(
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

    /* Read I/O flags */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_IOFLAGS);
    if (result == NMO_OK) {
        result = nmo_chunk_read_dword(chunk, &out_state->old_flags);
        if (result != NMO_OK) return result;
        out_state->has_flags = true;
    }
    /* Note: If identifier not found, old_flags remains 0 (valid for older versions) */

    NMO_RETURN_OK();
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
nmo_status_t nmo_behaviorio_serialize(
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

    /* CKBehaviorIO::Save always writes IOFLAGS in file context */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_IOFLAGS);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->old_flags);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

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

nmo_status_t nmo_behaviorio_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_behaviorio_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

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

static void nmo_behaviorio_post_delete(
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

NMO_DEFINE_OBJECT_STATE_OPS(behaviorio, nmo_behaviorio_state_t)

nmo_type_vtable_t nmo_behaviorio_vtable = {
    .prepare_dependencies = nmo_behaviorio_prepare_dependencies,
    .remap_dependencies = nmo_behaviorio_remap_dependencies,
    .pre_delete = nmo_behaviorio_pre_delete,
    .post_delete = nmo_behaviorio_post_delete,
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







