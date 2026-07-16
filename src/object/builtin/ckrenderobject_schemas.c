/**
 * @file ckrenderobject_schemas.c
 * @brief CKRenderObject schema definitions
 *
 * Implements schema for CKRenderObject and its descendants.
 * 
 * Based on official Virtools SDK (reference/include/CKRenderObject.h):
 * - CKRenderObject is an ABSTRACT BASE CLASS (all methods pure virtual)
 * - It does NOT override Load/Save - inherits CKBeObject's behavior
 * - No additional data is serialized to chunks beyond CKBeObject
 * - Runtime rendering data (callbacks, Z-order) is NOT persisted
 */

#include "object/builtin/nmo_renderobject_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(renderobject, nmo_renderobject_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_renderobject_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_renderobject_state_t, base),
                    sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0)
};

/* =============================================================================
 * CKRenderObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKRenderObject state from chunk
 * 
 * CKRenderObject is an abstract base class with no Load/Save implementation.
 * This function delegates to CKBeObject deserializer to maintain proper
 * inheritance chain behavior.
 * 
 * Reference: reference/include/CKRenderObject.h (abstract class)
 * No corresponding Load/Save in reference/src/ - uses parent CKBeObject
 * 
 * @param chunk Chunk containing CKRenderObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_renderobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_renderobject_state_t *out_state = (nmo_renderobject_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_renderobject_deserialize");
    }

    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKRenderObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKRenderObject state to chunk
 * 
 * CKRenderObject has no additional data beyond CKBeObject.
 * This function delegates to CKBeObject serializer.
 * 
 * Reference: reference/include/CKRenderObject.h (abstract class, no Save)
 * 
 * @param in_state Input state structure
 * @param out_chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
nmo_status_t nmo_renderobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_renderobject_state_t *in_state = (const nmo_renderobject_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_renderobject_serialize");
    }

    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_renderobject_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_renderobject_state_t *s = src;
    nmo_renderobject_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.scripts, &d->base.scripts,
                                        &s->base.scripts.allocator));
    NMO_RETURN_IF_ERROR(nmo_beobject_clone_attributes(
        arena, &d->base.attributes, &s->base.attributes));
    NMO_RETURN_OK();
}

static nmo_status_t nmo_renderobject_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_renderobject_state_t *s = instance;
    size_t script_count = nmo_array_size(&s->base.scripts);
    size_t attribute_count = nmo_array_size(&s->base.attributes);

    NMO_VALIDATE_COUNT(nmo_array_data(&s->base.scripts), (uint32_t)script_count,
                       "scripts");
    NMO_VALIDATE_COUNT(nmo_array_data(&s->base.attributes), (uint32_t)attribute_count,
                       "attributes");
    NMO_RETURN_OK();
}

nmo_status_t nmo_renderobject_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_renderobject_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_renderobject_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_renderobject_remap_dependencies");
    }

    nmo_renderobject_state_t *state = (nmo_renderobject_state_t *)instance;
    NMO_RETURN_IF_ERROR(nmo_beobject_remap_dependencies(&state->base, NULL, context));
    return nmo_renderobject_validate(state, NULL, NULL);
}

static nmo_status_t nmo_renderobject_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_renderobject_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_renderobject_post_delete(
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

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(renderobject, nmo_renderobject_state_t)

nmo_type_vtable_t nmo_renderobject_vtable = {
    .prepare_dependencies = nmo_renderobject_prepare_dependencies,
    .remap_dependencies = nmo_renderobject_remap_dependencies,
    .pre_delete = nmo_renderobject_pre_delete,
    .post_delete = nmo_renderobject_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_renderobject_create,
        nmo_renderobject_destroy,
        nmo_renderobject_serialize,
        nmo_renderobject_deserialize,
        nmo_renderobject_copy,
        nmo_renderobject_validate,
        nmo_renderobject_equals,
        nmo_renderobject_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_renderobject_type,
    CKPGUID_RENDEROBJECT,
    "CKRenderObject",
    NMO_CID_RENDEROBJECT,
    CKPGUID_BEOBJECT,
    nmo_renderobject_state_t,
    &nmo_renderobject_vtable,
    nmo_renderobject_fields)




