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

#include "schema/nmo_ckgroup_schemas.h"
#include "schema/nmo_ckbeobject_schemas.h"
#include "schema/nmo_ckobject_schemas.h"
#include "schema/nmo_schema.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * CKGroup IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKGroup.cpp */
#define CK_STATESAVE_GROUPALL  0x00000001

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
static nmo_result_t nmo_ckgroup_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckgroup_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgroup_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckgroup_state_t));
    
    /* Deserialize base CKBeObject state first */
    nmo_ckbeobject_deserialize_fn parent_deserialize = nmo_get_ckbeobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) return result;
    }

    /* Seek group data identifier */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_GROUPALL);
    if (result.code != NMO_OK) {
        /* No group data - empty group is valid */
        return nmo_result_ok();
    }

    /* Read object array using XObjectPointerArray::Load format
     * Reference: XObjectArray.cpp - array is stored as [count, id1, id2, ...] */
    int32_t count;
    result = nmo_chunk_read_int(chunk, &count);
    if (result.code != NMO_OK) {
        return result;
    }

    if (count > 0) {
        /* Sanity check */
        const uint32_t MAX_GROUP_OBJECTS = 100000;
        if ((uint32_t)count > MAX_GROUP_OBJECTS) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                NMO_SEVERITY_ERROR, "Group object count exceeds maximum"));
        }

        out_state->object_count = (uint32_t)count;
        out_state->object_ids = (nmo_object_id_t *)nmo_arena_alloc(
            arena,
            count * sizeof(nmo_object_id_t),
            _Alignof(nmo_object_id_t)
        );

        if (!out_state->object_ids) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                NMO_SEVERITY_ERROR, "Failed to allocate object ID array"));
        }

        /* Read object IDs */
        for (int32_t i = 0; i < count; i++) {
            result = nmo_chunk_read_object_id(chunk, &out_state->object_ids[i]);
            if (result.code != NMO_OK) {
                /* Partial read - save what we got */
                out_state->object_count = i;
                return nmo_result_ok();
            }
        }
    }

    return nmo_result_ok();
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
static nmo_result_t nmo_ckgroup_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckgroup_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgroup_serialize"));
    }

    /* Write base class (CKBeObject) data */
    nmo_ckbeobject_serialize_fn parent_serialize = nmo_get_ckbeobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(chunk, &state->base);
        if (result.code != NMO_OK) return result;
    }

    /* Only write data if group is non-empty */
    if (state->object_count == 0 || !state->object_ids) {
        return nmo_result_ok();
    }

    /* Write identifier */
    nmo_result_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_GROUPALL);
    if (result.code != NMO_OK) return result;

    /* Write object count */
    result = nmo_chunk_write_int(chunk, (int32_t)state->object_count);
    if (result.code != NMO_OK) return result;

    /* Write object IDs */
    for (uint32_t i = 0; i < state->object_count; i++) {
        result = nmo_chunk_write_object_id(chunk, state->object_ids[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA VTABLE (for schema registry integration)
 * ============================================================================= */

/**
 * @brief Vtable read wrapper for CKGroup
 */
static nmo_result_t nmo_ckgroup_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckgroup_deserialize(chunk, arena, (nmo_ckgroup_state_t *)out_ptr);
}

/**
 * @brief Vtable write wrapper for CKGroup
 */
static nmo_result_t nmo_ckgroup_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr)
{
    (void)type;
    return nmo_ckgroup_serialize(chunk, (const nmo_ckgroup_state_t *)in_ptr);
}

/**
 * @brief Vtable for CKGroup schema
 */
static const nmo_schema_vtable_t nmo_ckgroup_vtable = {
    .read = nmo_ckgroup_vtable_read,
    .write = nmo_ckgroup_vtable_write,
    .validate = NULL
};

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKGroup schema types with vtable
 * 
 * Creates schema descriptors for CKGroup state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckgroup_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckgroup_schemas"));
    }

    /* Get base types */
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "uint32_t");
    const nmo_schema_type_t *object_id_type = nmo_schema_registry_find_by_name(registry, "nmo_object_id_t");
    
    if (!uint32_type || !object_id_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required base types not found in registry"));
    }

    /* Register CKGroup state structure with vtable */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKGroupState",
                                                      sizeof(nmo_ckgroup_state_t),
                                                      alignof(nmo_ckgroup_state_t));
    
    nmo_builder_add_field_ex(&builder, "object_count", uint32_type,
                            offsetof(nmo_ckgroup_state_t, object_count), 0);
    
    /* Attach vtable for optimized read/write */
    nmo_builder_set_vtable(&builder, &nmo_ckgroup_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKGroup
 * 
 * @return Deserialize function pointer
 */
nmo_ckgroup_deserialize_fn nmo_get_ckgroup_deserialize(void)
{
    return nmo_ckgroup_deserialize;
}

/**
 * @brief Get the serialize function for CKGroup
 * 
 * @return Serialize function pointer
 */
nmo_ckgroup_serialize_fn nmo_get_ckgroup_serialize(void)
{
    return nmo_ckgroup_serialize;
}

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
static nmo_result_t nmo_ckgroup_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    if (!state || !repository) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckgroup_finish_loading"));
    }

    nmo_ckgroup_state_t *group_state = (nmo_ckgroup_state_t *)state;

    /* Nothing to do for empty groups */
    if (group_state->object_count == 0 || !group_state->object_ids) {
        return nmo_result_ok();
    }

    /* TODO: Establish bidirectional group membership
     * For each object_id in the group:
     * 1. Resolve object_id to nmo_object_t* using repository
     * 2. Add this group to the object's group list
     * 3. Handle missing objects gracefully (external references)
     * 
     * This requires:
     * - nmo_object_t to have a groups list (future enhancement)
     * - Or store group membership in a separate index
     * 
     * For now, just log that we'd process these objects.
     */

    /* Validate all object IDs are resolvable (Phase 15 validation) */
    for (uint32_t i = 0; i < group_state->object_count; i++) {
        nmo_object_id_t obj_id = group_state->object_ids[i];
        
        /* Skip null references */
        if (obj_id == 0) continue;
        
        /* Check if object exists in repository
         * Note: nmo_object_repository_find_by_id is declared in nmo_object_repository.h
         * which we'll need to include if we want to actually resolve references.
         * For now, this is a placeholder that establishes the pattern.
         */
        (void)obj_id; /* Suppress unused warning until we implement resolution */
    }

    return nmo_result_ok();
}

/**
 * @brief Get the finish_loading function for CKGroup
 * 
 * @return Finish loading function pointer
 */
nmo_ckgroup_finish_loading_fn nmo_get_ckgroup_finish_loading(void)
{
    return nmo_ckgroup_finish_loading;
}
