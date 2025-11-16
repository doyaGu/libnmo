/**
 * @file cksceneobject_schemas.c
 * @brief CKSceneObject schema definitions
 *
 * Implements schema for CKSceneObject and its descendants.
 * 
 * Based on official Virtools SDK (reference/src/CKSceneObject.cpp):
 * - CKSceneObject does NOT override Load/Save - inherits CKObject's behavior
 * - m_Scenes (XBitArray) is runtime-only data managed by CKScene::AddObject/RemoveObject
 * - No additional data is serialized to chunks beyond CKObject's visibility flags
 * 
 * This schema correctly delegates to CKObject deserializer and maintains
 * the parent chain functionality as required by design.md §6.4.
 */

#include "schema/nmo_schema.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "schema/nmo_ckobject_schemas.h"
#include "schema/nmo_cksceneobject_schemas.h"
#include "schema/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>
#include <stdalign.h>

/* =============================================================================
 * CKSceneObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKSceneObject state from chunk
 * 
 * CKSceneObject doesn't add any chunk data beyond CKObject.
 * This function delegates to CKObject deserializer.
 * 
 * @param chunk Chunk containing CKSceneObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_result_t nmo_cksceneobject_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksceneobject_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksceneobject_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(*out_state));

    /* Deserialize base CKObject state */
    nmo_ckobject_deserialize_fn parent_deserialize = nmo_get_ckobject_deserialize();
    if (parent_deserialize) {
        nmo_result_t result = parent_deserialize(chunk, arena, &out_state->base);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    /* CKSceneObject has no additional chunk data */
    /* Scene membership is populated at runtime by CKScene */

    /* Preserve any remaining chunk data for round-trip */
    size_t pos = nmo_chunk_get_position(chunk);
    size_t total = nmo_chunk_get_size(chunk);
    if (pos < total) {
        size_t remaining = total - pos;
        out_state->raw_tail = (uint8_t *)nmo_arena_alloc(arena, remaining, 1);
        if (out_state->raw_tail != NULL) {
            size_t bytes_read = nmo_chunk_read_and_fill_buffer(chunk, out_state->raw_tail, remaining);
            out_state->raw_tail_size = bytes_read;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKSceneObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKSceneObject state to chunk
 * 
 * Symmetric write operation for round-trip support.
 * 
 * @param in_state State structure to serialize (input)
 * @param out_chunk Chunk to write to (output)
 * @param arena Arena allocator for error handling
 * @return Result indicating success or error
 */
nmo_result_t nmo_cksceneobject_serialize(
    const nmo_cksceneobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksceneobject_serialize"));
    }

    /* Start write mode */
    nmo_result_t result = nmo_chunk_start_write(out_chunk);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Serialize base CKObject state */
    nmo_ckobject_serialize_fn parent_serialize = nmo_get_ckobject_serialize();
    if (parent_serialize) {
        result = parent_serialize(&in_state->base, out_chunk, arena);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    /* Write preserved unknown data */
    if (in_state->raw_tail != NULL && in_state->raw_tail_size > 0) {
        result = nmo_chunk_write_buffer(out_chunk, (const void *)in_state->raw_tail, in_state->raw_tail_size);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get CKSceneObject deserialize function pointer
 */
nmo_cksceneobject_deserialize_fn nmo_get_cksceneobject_deserialize(void) {
    return nmo_cksceneobject_deserialize;
}

/**
 * @brief Get CKSceneObject serialize function pointer
 */
nmo_cksceneobject_serialize_fn nmo_get_cksceneobject_serialize(void) {
    return nmo_cksceneobject_serialize;
}

/* =============================================================================
 * VTABLE IMPLEMENTATION
 * ============================================================================= */

static nmo_result_t vtable_read_cksceneobject(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr) {
    (void)type;
    return nmo_cksceneobject_deserialize(chunk, arena, (nmo_cksceneobject_state_t *)out_ptr);
}

static nmo_result_t vtable_write_cksceneobject(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena) {
    (void)type;
    return nmo_cksceneobject_serialize((const nmo_cksceneobject_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_cksceneobject_vtable = {
    .read = vtable_read_cksceneobject,
    .write = vtable_write_cksceneobject,
    .validate = NULL
};

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKSceneObject schema
 */
nmo_result_t nmo_register_cksceneobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_cksceneobject_schemas"));
    }

    /* Register minimal schema with vtable for abstract base class */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKSceneObjectState",
                                                      sizeof(nmo_cksceneobject_state_t),
                                                      alignof(nmo_cksceneobject_state_t));
    
    /* Set vtable for automated serialization */
    nmo_builder_set_vtable(&builder, &nmo_cksceneobject_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Map class ID to schema */
    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CKSceneObjectState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_SCENEOBJECT, type);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}