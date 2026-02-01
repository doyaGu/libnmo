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
 * 
 * This schema correctly delegates to CKBeObject deserializer, maintaining
 * the parent chain functionality as required by design.md §6.4.
 */

#include "object/nmo_ckrenderobject_schemas.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

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
nmo_result_t nmo_ckrenderobject_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckrenderobject_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrenderobject_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckrenderobject_state_t));

    nmo_ckbeobject_deserialize_fn parent_deserialize = nmo_get_ckbeobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
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
nmo_result_t nmo_ckrenderobject_serialize(
    const nmo_ckrenderobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrenderobject_serialize"));
    }

    nmo_ckbeobject_serialize_fn parent_serialize = nmo_get_ckbeobject_serialize();
    if (parent_serialize) {
        nmo_result_t result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * VTABLE IMPLEMENTATION
 * ============================================================================= */

static nmo_result_t vtable_read_ckrenderobject(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr) {
    (void)type;
    return nmo_ckrenderobject_deserialize(chunk, arena, (nmo_ckrenderobject_state_t *)out_ptr);
}

static nmo_result_t vtable_write_ckrenderobject(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena) {
    (void)type;
    return nmo_ckrenderobject_serialize((const nmo_ckrenderobject_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckrenderobject_vtable = {
    .read = vtable_read_ckrenderobject,
    .write = vtable_write_ckrenderobject,
    .validate = NULL
};

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKRenderObject schema types
 * 
 * Creates schema descriptors for CKRenderObject state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckrenderobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckrenderobject_schemas"));
    }

    /* Register minimal schema with vtable for abstract base class */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKRenderObjectState",
                                                      sizeof(nmo_ckrenderobject_state_t),
                                                      alignof(nmo_ckrenderobject_state_t));
    
    /* Set vtable for automated serialization */
    nmo_builder_set_vtable(&builder, &nmo_ckrenderobject_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Map class ID to schema */
    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKRenderObjectState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_RENDEROBJECT, type);
        if (result.code != NMO_OK) {
            return result;
        }
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKRenderObject
 * 
 * @return Deserialize function pointer
 */
nmo_ckrenderobject_deserialize_fn nmo_get_ckrenderobject_deserialize(void)
{
    return nmo_ckrenderobject_deserialize;
}

/**
 * @brief Get the serialize function for CKRenderObject
 * 
 * @return Serialize function pointer
 */
nmo_ckrenderobject_serialize_fn nmo_get_ckrenderobject_serialize(void)
{
    return nmo_ckrenderobject_serialize;
}
