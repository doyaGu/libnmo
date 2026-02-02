/**
 * @file ckrendercontext_schemas.c
 * @brief CKRenderContext schema implementation
 */

#include "object/nmo_ckrendercontext_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

static nmo_result_t nmo_ckrendercontext_deserialize_internal(
    nmo_ckrendercontext_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrendercontext_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckrendercontext,
    nmo_ckrendercontext_state_t,
    nmo_ckrendercontext_serialize,
    nmo_ckrendercontext_deserialize,
    NMO_GUID_CKRENDERCONTEXT,
    "CKRenderContext",
    NMO_CID_RENDERCONTEXT,
    NMO_GUID_CKOBJECT
)

static nmo_result_t nmo_ckrendercontext_serialize_internal(
    const nmo_ckrendercontext_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrendercontext_serialize"));
    }

    nmo_result_t result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

nmo_result_t nmo_ckrendercontext_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckrendercontext_state_t *out_state = (nmo_ckrendercontext_state_t *)instance;
    return nmo_ckrendercontext_deserialize_internal(out_state, chunk, context);
}

nmo_result_t nmo_ckrendercontext_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckrendercontext_state_t *in_state = (const nmo_ckrendercontext_state_t *)instance;
    return nmo_ckrendercontext_serialize_internal(in_state, out_chunk, context);
}
