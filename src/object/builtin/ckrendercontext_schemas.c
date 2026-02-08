/**
 * @file ckrendercontext_schemas.c
 * @brief CKRenderContext schema implementation
 */

#include "object/nmo_ckrendercontext_schemas.h"
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

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckrendercontext, nmo_ckrendercontext_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_ckrendercontext_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckrendercontext_state_t, base),
                    sizeof(nmo_ckobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0)
};

static nmo_status_t nmo_ckrendercontext_deserialize_internal(
    nmo_ckrendercontext_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrendercontext_deserialize");
    }

    nmo_status_t result = nmo_ckobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    ckrendercontext,
    nmo_ckrendercontext_state_t,
    nmo_ckrendercontext_serialize,
    nmo_ckrendercontext_deserialize,
    nmo_ckrendercontext_fields,
    CKPGUID_RENDERCONTEXT,
    "CKRenderContext",
    NMO_CID_RENDERCONTEXT,
    CKPGUID_OBJECT
)

static nmo_status_t nmo_ckrendercontext_serialize_internal(
    const nmo_ckrendercontext_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrendercontext_serialize");
    }

    nmo_status_t result = nmo_ckobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_ckrendercontext_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckrendercontext_state_t *out_state = (nmo_ckrendercontext_state_t *)instance;
    return nmo_ckrendercontext_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_ckrendercontext_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckrendercontext_state_t *in_state = (const nmo_ckrendercontext_state_t *)instance;
    return nmo_ckrendercontext_serialize_internal(in_state, out_chunk, context);
}

