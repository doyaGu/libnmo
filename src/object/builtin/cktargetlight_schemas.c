/**
 * @file cktargetlight_schemas.c
 * @brief CKTargetLight schema implementation
 */

#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(targetlight, nmo_targetlight_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_targetlight_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_targetlight_state_t, base),
                    sizeof(nmo_light_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_targetlight_state_t, has_target, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_targetlight_state_t, target_id)
};

static nmo_status_t nmo_targetlight_deserialize_internal(
    nmo_targetlight_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_targetlight_deserialize");
    }

    nmo_status_t result = nmo_light_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->has_target = 0;
    out_state->target_id = 0;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_TLIGHTTARGET) == NMO_OK) {
        result = nmo_chunk_read_object_id(chunk, &out_state->target_id);
        if (result != NMO_OK) {
            return result;
        }
        out_state->has_target = (out_state->target_id != 0);
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_status_t nmo_targetlight_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_targetlight_finish_loading");
    }

    nmo_targetlight_state_t *state = (nmo_targetlight_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;

    NMO_RETURN_IF_ERROR(nmo_light_finish_loading(&state->base, arena, repository));

    if (state->target_id != 0 && repo &&
        nmo_object_repository_find_by_id(repo, state->target_id) == NULL) {
        state->target_id = 0;
    }

    state->has_target = (state->target_id != 0);
    if (!state->has_target) {
        state->target_id = 0;
    }

    return nmo_object_default_validate(state, NULL, NULL);
}

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS(
    targetlight,
    nmo_targetlight_state_t,
    nmo_targetlight_serialize,
    nmo_targetlight_deserialize,
    nmo_targetlight_finish_loading,
    nmo_targetlight_fields,
    CKPGUID_TARGETLIGHT,
    "CKTargetLight",
    NMO_CID_TARGETLIGHT,
    CKPGUID_LIGHT
)

static nmo_status_t nmo_targetlight_serialize_internal(
    const nmo_targetlight_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_targetlight_serialize");
    }

    nmo_status_t result = nmo_light_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    if (!is_file) {
        uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
        if ((save_flags & CK_STATESAVE_TLIGHTONLY) == 0) {
            return NMO_OK;
        }
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_TLIGHTTARGET);
    if (result != NMO_OK) return result;
    result = nmo_chunk_write_object_id(out_chunk, in_state->target_id);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_targetlight_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_targetlight_state_t *out_state = (nmo_targetlight_state_t *)instance;
    return nmo_targetlight_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_targetlight_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_targetlight_state_t *in_state = (const nmo_targetlight_state_t *)instance;
    return nmo_targetlight_serialize_internal(in_state, out_chunk, context);
}

