/**
 * @file ckgroup_schemas.c
 * @brief CKGroup schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKGroup (object groups).
 * CKGroup extends CKBeObject and contains an array of object references.
 * 
 * Based on official Virtools SDK (reference/src/CKGroup.cpp:185-220):
 * - CKGroup::Save writes identifier + object array
 * - CKGroup::Load reads object array using XObjectPointerArray::Load
 * - PostLoad ensures bidirectional group membership consistency
 */

#include "object/nmo_ckgroup_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "session/nmo_object_repository.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckgroup, nmo_ckgroup_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_ckgroup_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckgroup_state_t, base),
                    sizeof(nmo_ckbeobject_state_t), NMO_GUID_FIELD_VOID,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_ARRAY(nmo_ckgroup_state_t, object_ids),
    NMO_FIELD(nmo_ckgroup_state_t, object_count, NMO_GUID_FIELD_UINT32)
};

/* =============================================================================
 * CKGroup DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKGroup state from chunk
 * 
 * Implements the symmetric read operation for CKGroup::Load.
 * Reads the object ID array.
 * 
 * Reference: reference/src/CKGroup.cpp:197-220
 * 
 * @param chunk Chunk containing CKGroup data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckgroup_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckgroup_state_t *out_state = (nmo_ckgroup_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgroup_deserialize");
    }

    /* Deserialize base CKBeObject state first */
    nmo_status_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Seek group data identifier */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_GROUPALL);
    if (result != NMO_OK) {
        /* No group data - empty group is valid */
        NMO_RETURN_OK();
    }

    /* Read object array using XObjectPointerArray::Load format
     * Reference: XObjectArray.cpp - array is stored as [count, id1, id2, ...] */
    int32_t count;
    result = nmo_chunk_read_int(chunk, &count);
    if (result != NMO_OK) {
        return result;
    }

    if (count > 0) {
        /* Sanity check */
        const uint32_t MAX_GROUP_OBJECTS = 100000;
        if ((uint32_t)count > MAX_GROUP_OBJECTS) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Group object count exceeds maximum");
        }

        out_state->object_count = (uint32_t)count;
        out_state->object_ids = (nmo_object_id_t *)nmo_arena_alloc(
            arena,
            count * sizeof(nmo_object_id_t),
            _Alignof(nmo_object_id_t)
        );

        if (!out_state->object_ids) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object ID array");
        }

        /* Read object IDs */
        for (int32_t i = 0; i < count; i++) {
            result = nmo_chunk_read_object_id(chunk, &out_state->object_ids[i]);
            if (result != NMO_OK) {
                /* Partial read - save what we got */
                out_state->object_count = i;
                NMO_RETURN_OK();
            }
        }
    }

    NMO_RETURN_OK();
}

/* =============================================================================
 * CKGroup SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKGroup state to chunk
 * 
 * Implements the symmetric write operation for CKGroup::Save.
 * Writes the object ID array.
 * 
 * Reference: reference/src/CKGroup.cpp:185-195
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckgroup_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckgroup_state_t *in_state = (const nmo_ckgroup_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgroup_serialize");
    }

    /* Write base class (CKBeObject) data */
    nmo_status_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Only write data if group is non-empty */
    if (in_state->object_count == 0 || !in_state->object_ids) {
        NMO_RETURN_OK();
    }

    /* Write identifier */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_GROUPALL);
    if (result != NMO_OK) return result;

    /* Write object count */
    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->object_count);
    if (result != NMO_OK) return result;

    /* Write object IDs */
    for (uint32_t i = 0; i < in_state->object_count; i++) {
        result = nmo_chunk_write_object_id(out_chunk, in_state->object_ids[i]);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t ckgroup_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_ckgroup_state_t *s = src;
    nmo_ckgroup_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.base.raw_tail,
                                              s->base.base.raw_tail, s->base.base.raw_tail_size));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.script_ids,
                                              s->base.script_ids, sizeof(nmo_object_id_t), s->base.script_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.attribute_parameter_ids,
                                              s->base.attribute_parameter_ids, sizeof(nmo_object_id_t), s->base.attribute_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->base.attribute_types,
                                              s->base.attribute_types, sizeof(uint32_t), s->base.attribute_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_chunk_array(arena, &d->base.attribute_chunks,
                                                    s->base.attribute_chunks, s->base.attribute_chunk_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.legacy_attributes_raw,
                                              s->base.legacy_attributes_raw, s->base.legacy_attributes_size));
    return nmo_object_copy_array(arena, (void **)&d->object_ids,
                                 s->object_ids, sizeof(nmo_object_id_t), s->object_count);
}

static nmo_status_t ckgroup_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_ckgroup_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->object_ids, s->object_count, "object_ids");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS_CUSTOM(
    ckgroup,
    nmo_ckgroup_state_t,
    nmo_ckgroup_serialize,
    nmo_ckgroup_deserialize,
    nmo_ckgroup_finish_loading,
    nmo_ckgroup_fields,
    NMO_GUID_CKGROUP,
    "CKGroup",
    NMO_CID_GROUP,
    NMO_GUID_CKBEOBJECT
)


/* =============================================================================
 * CKGroup FINISH LOADING (Phase 15 - PostLoad equivalent)
 * ============================================================================= */

/**
 * @brief Finish loading CKGroup - establish bidirectional group membership
 * 
 * Called during Phase 15 after deserialization. Resolves object ID references
 * and establishes bidirectional relationships (groups know members, members know groups).
 * 
 * This is equivalent to CKGroup::PostLoad() in Virtools SDK.
 * 
 * @param state CKGroup state (must be nmo_ckgroup_state_t*)
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution (opaque void*)
 * @return Result indicating success or error
 */
nmo_status_t nmo_ckgroup_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgroup_finish_loading");
    }

    (void)arena;

    nmo_ckgroup_state_t *group_state = (nmo_ckgroup_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;

    /* Nothing to do for empty groups */
    if (group_state->object_count == 0 || !group_state->object_ids) {
        NMO_RETURN_OK();
    }

    /* Validate and resolve all member object references
     * 
     * Group membership resolution:
     * - Verify each object_id exists in repository
     * - Count resolved vs unresolved references for diagnostics
     * - External references (objects not in this file) are allowed
     * 
     * Note: Bidirectional membership (object->groups) is not currently tracked.
     * This would require extending nmo_object_t with a groups list, which is
     * deferred to future work. For now, groups->objects direction is sufficient
     * for most use cases.
     */
    uint32_t resolved_count = 0;
    uint32_t unresolved_count = 0;

    for (uint32_t i = 0; i < group_state->object_count; i++) {
        nmo_object_id_t obj_id = group_state->object_ids[i];
        
        /* Skip null references */
        if (obj_id == 0) {
            continue;
        }
        
        /* Try to resolve the object reference */
        if (repo != NULL) {
            nmo_object_t *obj = nmo_object_repository_find_by_id(repo, obj_id);
            if (obj != NULL) {
                resolved_count++;
                /* Object found - bidirectional link would go here:
                 * nmo_object_add_to_group(obj, group_id);
                 */
            } else {
                unresolved_count++;
                /* Object not found - may be external reference */
            }
        } else {
            /* No repository - can't resolve, but not an error */
            unresolved_count++;
        }
    }

    /* Log resolution statistics if logger available */
    if (unresolved_count > 0) {
        nmo_log_debug(NULL, "CKGroup finish_loading: %u resolved, %u unresolved (external refs)",
                      resolved_count, unresolved_count);
    }

    NMO_RETURN_OK();
}

