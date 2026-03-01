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

nmo_status_t nmo_rendercontext_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_rendercontext_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_rendercontext_remap_dependencies");
    }

    nmo_rendercontext_state_t *state = (nmo_rendercontext_state_t *)instance;
    NMO_RETURN_IF_ERROR(nmo_object_remap_dependencies(&state->base, NULL, context));
    return nmo_object_default_validate(state, NULL, NULL);
}

static nmo_status_t nmo_rendercontext_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_rendercontext_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_rendercontext_post_delete(
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

NMO_DEFINE_OBJECT_STATE_OPS(rendercontext, nmo_rendercontext_state_t)

nmo_type_vtable_t nmo_rendercontext_vtable = {
    .prepare_dependencies = nmo_rendercontext_prepare_dependencies,
    .remap_dependencies = nmo_rendercontext_remap_dependencies,
    .pre_delete = nmo_rendercontext_pre_delete,
    .post_delete = nmo_rendercontext_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_rendercontext_create,
        nmo_rendercontext_destroy,
        nmo_rendercontext_serialize,
        nmo_rendercontext_deserialize,
        nmo_rendercontext_copy,
        nmo_rendercontext_validate,
        nmo_rendercontext_equals,
        nmo_rendercontext_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_rendercontext_type,
    CKPGUID_RENDERCONTEXT,
    "CKRenderContext",
    NMO_CID_RENDERCONTEXT,
    CKPGUID_OBJECT,
    nmo_rendercontext_state_t,
    &nmo_rendercontext_vtable,
    nmo_rendercontext_fields)

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
