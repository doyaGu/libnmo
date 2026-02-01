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

#include "object/nmo_ck3dobject_schemas.h"
#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

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
static nmo_result_t nmo_ck3dobject_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck3dobject_state_t *out_state)
{
    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CK3dObject deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    return nmo_ck3dentity_deserialize(chunk, arena, &out_state->entity);
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
static nmo_result_t nmo_ck3dobject_serialize(
    const nmo_ck3dobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CK3dObject serialize"));
    }

    return nmo_ck3dentity_serialize(&in_state->entity, out_chunk, arena);
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/* =============================================================================
 * VTABLE IMPLEMENTATION
 * ============================================================================= */

static nmo_result_t vtable_read_ck3dobject(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr) {
    (void)type;
    return nmo_ck3dobject_deserialize(chunk, arena, (nmo_ck3dobject_state_t *)out_ptr);
}

static nmo_result_t vtable_write_ck3dobject(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena) {
    (void)type;
    return nmo_ck3dobject_serialize((const nmo_ck3dobject_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ck3dobject_vtable = {
    .read = vtable_read_ck3dobject,
    .write = vtable_write_ck3dobject,
    .validate = NULL
};

/**
 * @brief Register CK3dObject schema
 */
/**
 * @brief Register CK3dObject state schema
 * 
 * Creates schema descriptor for CK3dObject state structure.
 * This is separate from class hierarchy registration in ckobject_hierarchy.c.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ck3dobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (registry == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ck3dobject_schemas"));
    }

    /* Register CK3dObject state structure */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CK3dObjectState",
                                                      sizeof(nmo_ck3dobject_state_t),
                                                      alignof(nmo_ck3dobject_state_t));

    /* Set vtable for automated serialization */
    nmo_builder_set_vtable(&builder, &nmo_ck3dobject_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Map class ID to schema */
    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CK3dObjectState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_3DOBJECT, type);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Get CK3dObject deserialize function pointer
 * 
 * Provides access to deserialization function for use in parser.c Phase 14.
 * 
 * @return Function pointer to nmo_ck3dobject_deserialize
 */
nmo_ck3dobject_deserialize_fn nmo_get_ck3dobject_deserialize(void) {
    return nmo_ck3dobject_deserialize;
}

/**
 * @brief Get CK3dObject serialize function pointer
 * 
 * Provides access to serialization function for use in save pipeline.
 * 
 * @return Function pointer to nmo_ck3dobject_serialize
 */
nmo_ck3dobject_serialize_fn nmo_get_ck3dobject_serialize(void) {
    return nmo_ck3dobject_serialize;
}

/**
 * @brief Finish loading CK3dObject
 * 
 * Performs reference resolution for mesh linkage and material setup.
 * 
 * @param state 3D object state
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution
 * @return Result indicating success or error
 */
nmo_result_t nmo_ck3dobject_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    /* Mesh reference resolution would go here */
    (void)state;
    (void)arena;
    (void)repository;
    return nmo_result_ok();
}

/**
 * @brief Get finish_loading function for CK3dObject
 * @return Finish loading function pointer
 */
nmo_ck3dobject_finish_loading_fn nmo_get_ck3dobject_finish_loading(void)
{
    return nmo_ck3dobject_finish_loading;
}

