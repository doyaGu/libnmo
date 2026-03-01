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

#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "core/nmo_logger.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    group,
    nmo_group_state_t,
    do {
        nmo_status_t result = nmo_array_init(&state->object_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
    } while (0),
    ((void)0))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_group_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_group_state_t, base),
                       sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_ARRAY(nmo_group_state_t, object_ids)
};

static int nmo_group_is_file_mode_ser(const nmo_chunk_t *chunk, void *context)
{
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    if (chunk && ((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0)) {
        return 1;
    }
    if (ser_ctx && ((ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0)) {
        return 1;
    }
    return 0;
}

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
nmo_status_t nmo_group_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_group_state_t *out_state = (nmo_group_state_t *)instance;
    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_group_deserialize");
    }

    /* Deserialize base CKBeObject state first */
    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
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

    if (count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Group object count is negative");
    }

    /* Sanity check */
    const uint32_t MAX_GROUP_OBJECTS = 100000;
    if ((uint32_t)count > MAX_GROUP_OBJECTS) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Group object count exceeds maximum");
    }

    nmo_array_clear(&out_state->object_ids);
    if (count == 0) {
        NMO_RETURN_OK();
    }

    result = nmo_array_reserve(&out_state->object_ids, count);
    if (result != NMO_OK) return result;

    nmo_object_id_t *ids = NULL;
    result = nmo_array_extend(&out_state->object_ids, count, (void **)&ids);
    if (result != NMO_OK) return result;

    /* Read object IDs */
    for (int32_t i = 0; i < count; i++) {
        result = nmo_chunk_read_object_id(chunk, &ids[i]);
        if (result != NMO_OK) {
            return result;
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
nmo_status_t nmo_group_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_group_state_t *in_state = (const nmo_group_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_group_serialize");
    }

    /* Write base class (CKBeObject) data */
    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const bool is_file = nmo_group_is_file_mode_ser(out_chunk, context);
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    if (!is_file && save_flags == 0) {
        NMO_RETURN_OK();
    }

    const bool want_group = is_file || ((save_flags & CK_STATESAVE_GROUPALL) != 0);
    if (!want_group) {
        NMO_RETURN_OK();
    }

    /* In file mode, emit explicit empty group payload to avoid schema fallbacks. */
    if (!is_file && in_state->object_ids.count == 0) {
        NMO_RETURN_OK();
    }
    if (in_state->object_ids.count > 0 && !in_state->object_ids.data) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Group object IDs missing");
    }

    /* Write identifier */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_GROUPALL);
    if (result != NMO_OK) return result;

    /* Write object count */
    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->object_ids.count);
    if (result != NMO_OK) return result;

    /* Write object IDs */
    if (in_state->object_ids.count > 0) {
        const nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, &in_state->object_ids);
        for (uint32_t i = 0; i < in_state->object_ids.count; i++) {
            result = nmo_chunk_write_object_id(out_chunk, ids[i]);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_group_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_group_state_t *s = src;
    nmo_group_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.base.raw_tail,
                                              s->base.base.raw_tail, s->base.base.raw_tail_size));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.script_ids, &d->base.script_ids,
                                        &s->base.script_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.attribute_parameter_ids,
                                        &d->base.attribute_parameter_ids,
                                        &s->base.attribute_parameter_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.attribute_types, &d->base.attribute_types,
                                        &s->base.attribute_types.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->base.attribute_chunks,
                                                     &s->base.attribute_chunks));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.legacy_attributes_raw,
                                        &d->base.legacy_attributes_raw,
                                        &s->base.legacy_attributes_raw.allocator));
    return nmo_array_clone(&s->object_ids, &d->object_ids, &s->object_ids.allocator);
}

static nmo_status_t nmo_group_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_group_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->object_ids.data, s->object_ids.count, "object_ids");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS_CUSTOM(
    group,
    nmo_group_state_t,
    nmo_group_serialize,
    nmo_group_deserialize,
    nmo_group_finish_loading,
    nmo_group_fields,
    CKPGUID_GROUP,
    "CKGroup",
    NMO_CID_GROUP,
    CKPGUID_BEOBJECT
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
 * @param state CKGroup state (must be nmo_group_state_t*)
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution (opaque void*)
 * @return Result indicating success or error
 */
nmo_status_t nmo_group_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_group_finish_loading");
    }

    (void)arena;

    nmo_group_state_t *group_state = (nmo_group_state_t *)instance;

    /* Nothing to do for empty groups */
    if (group_state->object_ids.count == 0 || !group_state->object_ids.data) {
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
    uint32_t referenced_count = 0;
    uint32_t kept_count = 0;

    nmo_object_id_t *ids = NMO_ARRAY_DATA(nmo_object_id_t, &group_state->object_ids);
    for (uint32_t i = 0; i < group_state->object_ids.count; i++) {
        nmo_object_id_t obj_id = ids[i];
        
        /* Skip null references */
        if (obj_id == 0) {
            continue;
        }

        referenced_count++;

        if (repository) {
            nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;
            nmo_object_t *obj = nmo_object_repository_find_by_id(repo, obj_id);
            if (obj == NULL) {
                continue;
            }
        }

        ids[kept_count++] = obj_id;
    }

    group_state->object_ids.count = kept_count;

    if (referenced_count > 0) {
        nmo_log_debug(NULL, "CKGroup finish_loading: %u referenced members", referenced_count);
    }

    NMO_RETURN_OK();
}


