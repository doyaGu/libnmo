/**
 * @file ckrendercontext_schemas.c
 * @brief CKRenderContext schema implementation
 */

#include "object/builtin/nmo_rendercontext_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <string.h>
#include <stddef.h>
#include <stdalign.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(rendercontext, nmo_rendercontext_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_rendercontext_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_rendercontext_state_t, base),
                    sizeof(nmo_object_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0)
};

static nmo_status_t nmo_rendercontext_deserialize_internal(
    nmo_rendercontext_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_rendercontext_deserialize");
    }

    nmo_status_t result = nmo_object_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    rendercontext,
    nmo_rendercontext_state_t,
    nmo_rendercontext_serialize,
    nmo_rendercontext_deserialize,
    nmo_rendercontext_fields,
    CKPGUID_RENDERCONTEXT,
    "CKRenderContext",
    NMO_CID_RENDERCONTEXT,
    CKPGUID_OBJECT
)

static nmo_status_t nmo_rendercontext_serialize_internal(
    const nmo_rendercontext_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_rendercontext_serialize");
    }

    nmo_status_t result = nmo_object_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_rendercontext_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_rendercontext_state_t *out_state = (nmo_rendercontext_state_t *)instance;
    return nmo_rendercontext_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_rendercontext_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_rendercontext_state_t *in_state = (const nmo_rendercontext_state_t *)instance;
    return nmo_rendercontext_serialize_internal(in_state, out_chunk, context);
}

