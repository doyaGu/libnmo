/**
 * @file ckbehaviorlink_schemas.c
 * @brief CKBehaviorLink schema implementation
 *
 * Implements schema-driven deserialization for CKBehaviorLink (behavior graph connections).
 * CKBehaviorLink extends CKObject and stores timing delays plus I/O endpoint references.
 * 
 * Based on official Virtools SDK (reference/src/CKBehaviorLink.cpp:49-121).
 */

#include "object/builtin/nmo_behaviorlink_schemas.h"
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

NMO_DEFINE_OBJECT_LIFECYCLE(
    behaviorlink,
    nmo_behaviorlink_state_t,
    do { \
        state->activation_delay = 1; \
        state->initial_activation_delay = 1; \
    } while (0),
    ((void)0))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_behaviorlink_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_behaviorlink_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_behaviorlink_state_t, activation_delay, CKPGUID_INT16),
    NMO_FIELD(nmo_behaviorlink_state_t, initial_activation_delay, CKPGUID_INT16),
    NMO_FIELD_REF(nmo_behaviorlink_state_t, in_io_id),
    NMO_FIELD_REF(nmo_behaviorlink_state_t, out_io_id),
    NMO_FIELD(nmo_behaviorlink_state_t, has_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_behaviorlink_state_t, use_new_format, CKPGUID_BOOL),
    NMO_FIELD(nmo_behaviorlink_state_t, has_legacy_curdelay, CKPGUID_BOOL),
    NMO_FIELD(nmo_behaviorlink_state_t, has_legacy_ios, CKPGUID_BOOL),
    NMO_FIELD(nmo_behaviorlink_state_t, has_legacy_delay, CKPGUID_BOOL)
};

/* =============================================================================
 * CKBehaviorLink DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBehaviorLink state from chunk
 * 
 * Implements the symmetric read operation for CKBehaviorLink::Load.
 * Reads activation delays and I/O endpoint references.
 * Supports both new format (NEWDATA) and legacy format (CURDELAY + IOS).
 * 
 * Reference: reference/src/CKBehaviorLink.cpp:73-121
 * 
 * @param chunk Chunk containing CKBehaviorLink data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_behaviorlink_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_behaviorlink_state_t *out_state = (nmo_behaviorlink_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_behaviorlink_deserialize");
    }

    nmo_status_t result;

    /* Read base CKObject state (merged into this chunk by AddChunkAndDelete) */
    result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Try new format first (preferred) */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_LINK_NEWDATA);
    if (result == NMO_OK) {
        out_state->has_format = true;
        out_state->use_new_format = true;
        /* New format: packed delays (lower 16 bits = activation, upper 16 bits = initial) */
        uint32_t delays;
        result = nmo_chunk_read_dword(chunk, &delays);
        if (result != NMO_OK) return result;

        out_state->activation_delay = (int16_t)(delays & 0xFFFF);
        out_state->initial_activation_delay = (int16_t)((delays >> 16) & 0xFFFF);

        /* Read I/O object references */
        result = nmo_chunk_read_object_id(chunk, &out_state->in_io_id);
        if (result != NMO_OK) return result;

        result = nmo_chunk_read_object_id(chunk, &out_state->out_io_id);
        if (result != NMO_OK) return result;
    } else {
        out_state->has_format = true;
        out_state->use_new_format = false;
        /* Legacy format support */
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_LINK_CURDELAY);
        if (result == NMO_OK) {
            int32_t delay;
            result = nmo_chunk_read_int(chunk, &delay);
            if (result != NMO_OK) return result;
            out_state->activation_delay = (int16_t)delay;
            out_state->has_legacy_curdelay = true;
        }

        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_LINK_IOS);
        if (result == NMO_OK) {
            result = nmo_chunk_read_object_id(chunk, &out_state->in_io_id);
            if (result != NMO_OK) return result;

            result = nmo_chunk_read_object_id(chunk, &out_state->out_io_id);
            if (result != NMO_OK) return result;
            out_state->has_legacy_ios = true;
        }

        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_LINK_DELAY);
        if (result == NMO_OK) {
            int32_t delay;
            result = nmo_chunk_read_int(chunk, &delay);
            if (result != NMO_OK) return result;
            out_state->initial_activation_delay = (int16_t)delay;
            out_state->has_legacy_delay = true;
        }
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKBehaviorLink SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBehaviorLink state to chunk
 * 
 * Implements the symmetric write operation for CKBehaviorLink::Save.
 * Writes activation delays and I/O endpoint references in new format.
 * 
 * Reference: reference/src/CKBehaviorLink.cpp:49-71
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_status_t nmo_behaviorlink_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_behaviorlink_state_t *in_state = (const nmo_behaviorlink_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_behaviorlink_serialize");
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
        if ((save_flags & CK_STATESAVE_BEHAV_LINKONLY) == 0) {
            return NMO_OK;
        }
    }

    const bool use_new_format = true;

    if (use_new_format) {
        /* Write new format identifier */
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_LINK_NEWDATA);
        if (result != NMO_OK) return result;

        /* Pack delays into single DWORD (lower 16 bits = activation, upper 16 bits = initial) */
        uint32_t delays = ((uint32_t)in_state->activation_delay & 0xFFFF) |
                          (((uint32_t)in_state->initial_activation_delay & 0xFFFF) << 16);
        result = nmo_chunk_write_dword(out_chunk, delays);
        if (result != NMO_OK) return result;

        /* Write I/O object references */
        result = nmo_chunk_write_object_id(out_chunk, in_state->in_io_id);
        if (result != NMO_OK) return result;

        result = nmo_chunk_write_object_id(out_chunk, in_state->out_io_id);
        if (result != NMO_OK) return result;
    } else {
        /* Legacy format: emit only identifiers present in the source */
        if (in_state->has_legacy_curdelay) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_LINK_CURDELAY);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->activation_delay);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_legacy_ios) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_LINK_IOS);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->in_io_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->out_io_id);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_legacy_delay) {
            result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_LINK_DELAY);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->initial_activation_delay);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS(
    behaviorlink,
    nmo_behaviorlink_state_t,
    nmo_behaviorlink_serialize,
    nmo_behaviorlink_deserialize,
    nmo_behaviorlink_finish_loading,
    nmo_behaviorlink_fields,
    CKPGUID_BEHAVIORLINK,
    "CKBehaviorLink",
    NMO_CID_BEHAVIORLINK,
    CKPGUID_OBJECT
)

nmo_status_t nmo_behaviorlink_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_behaviorlink_finish_loading");
    }

    nmo_behaviorlink_state_t *state = (nmo_behaviorlink_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;

    NMO_RETURN_IF_ERROR(nmo_object_finish_loading(&state->base, arena, repository));

    if (repo) {
        if (state->in_io_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->in_io_id) == NULL) {
            state->in_io_id = 0;
        }
        if (state->out_io_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->out_io_id) == NULL) {
            state->out_io_id = 0;
        }
    }

    if (state->activation_delay < 0) {
        state->activation_delay = 0;
    }
    if (state->initial_activation_delay < 0) {
        state->initial_activation_delay = 0;
    }

    NMO_RETURN_OK();
}


