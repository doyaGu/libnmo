/**
 * @file ck3dobject_schemas.c
 * @brief CK3dObject schema definitions
 *
 * Implements schema for CK3dObject.
 * 
 * Based on CKRenderEngine RCK3dObject:
 * - CK3dObject inherits from CK3dEntity
 * - No additional serialized fields beyond CK3dEntity
 */

#include "object/builtin/nmo_3dobject_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    3dobject,
    nmo_3dobject_state_t,
    do {
        nmo_status_t result = nmo_3dentity_vtable.create(
            &state->entity, NULL, context);
        if (result != NMO_OK) return result;
    } while (0),
    nmo_3dentity_vtable.destroy(&state->entity, NULL, context))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_3dobject_fields[] = {
    NMO_FIELD_NAMED("entity", offsetof(nmo_3dobject_state_t, entity),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0)
};

/* =============================================================================
 * CK3dObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CK3dObject state from chunk
 * 
 * Reads CK3dEntity data only (CK3dObject adds no extra serialized fields).
 * 
 * @param chunk Chunk containing CK3dObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_3dobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_3dobject_state_t *out_state = (nmo_3dobject_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CK3dObject deserialize");
    }

    return nmo_3dentity_deserialize(&out_state->entity, chunk, NULL, context);
}

/* =============================================================================
 * CK3dObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CK3dObject state to chunk
 * 
 * @param state State to serialize
 * @param chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
nmo_status_t nmo_3dobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_3dobject_state_t *in_state = (const nmo_3dobject_state_t *)instance;
        nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CK3dObject serialize");
    }

    return nmo_3dentity_serialize(&in_state->entity, out_chunk, NULL, context);
}


nmo_status_t nmo_3dobject_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_object_default_validate(instance, type, context);
}

nmo_status_t nmo_3dobject_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_3dobject_remap_dependencies");
    }

    nmo_3dobject_state_t *state = (nmo_3dobject_state_t *)instance;
    return nmo_3dentity_remap_dependencies(&state->entity, NULL, context);
}

static nmo_status_t nmo_3dobject_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_3dobject_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_3dobject_post_delete(
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

static nmo_status_t nmo_3dobject_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_3dobject_state_t *source = src;
    nmo_3dobject_state_t *target = dst;
    nmo_type_descriptor_t entity_type = {
        .size = sizeof(nmo_3dentity_state_t),
    };
    return nmo_3dentity_vtable.copy(
        &source->entity, &target->entity, &entity_type, arena);
}

static nmo_status_t nmo_3dobject_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_3dobject_state_t *state = instance;
    return nmo_3dentity_vtable.validate(&state->entity, NULL, context);
}

static bool nmo_3dobject_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_3dobject_state_t *lhs = a;
    const nmo_3dobject_state_t *rhs = b;
    return nmo_3dentity_vtable.equals(&lhs->entity, &rhs->entity);
}

static uint32_t nmo_3dobject_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_3dobject_state_t *state = instance;
    return nmo_3dentity_vtable.hash(&state->entity);
}

nmo_type_vtable_t nmo_3dobject_vtable = {
    .prepare_dependencies = nmo_3dobject_prepare_dependencies,
    .remap_dependencies = nmo_3dobject_remap_dependencies,
    .pre_delete = nmo_3dobject_pre_delete,
    .post_delete = nmo_3dobject_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_3dobject_create,
        nmo_3dobject_destroy,
        nmo_3dobject_serialize,
        nmo_3dobject_deserialize,
        nmo_3dobject_copy,
        nmo_3dobject_validate,
        nmo_3dobject_equals,
        nmo_3dobject_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_3dobject_type,
    CKPGUID_OBJECT3D,
    "CK3dObject",
    NMO_CID_3DOBJECT,
    CKPGUID_3DENTITY,
    nmo_3dobject_state_t,
    &nmo_3dobject_vtable,
    nmo_3dobject_fields)
