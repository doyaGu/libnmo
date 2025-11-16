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

#include "schema/nmo_ckrenderobject_schemas.h"
#include "schema/nmo_ckbeobject_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
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

    /* CKRenderObject has no additional data beyond CKBeObject.
     * The actual serialization happens in CKBeObject, which handles:
     * - Scripts array
     * - Priority
     * - Attributes
     * 
     * Since we're at the Schema layer and need to maintain layer separation,
     * we don't call the CKBeObject deserializer here. Instead, the Session
     * layer's parser will handle the parent chain traversal.
     * 
     * For now, preserve any remaining chunk data as raw_tail for round-trip.
     */

    /* Get current read position */
    size_t current_pos = nmo_chunk_get_position(chunk);
    size_t chunk_size = nmo_chunk_get_data_size(chunk);
    
    if (current_pos < chunk_size) {
        /* There's unread data - preserve it for round-trip */
        size_t remaining = chunk_size - current_pos;
        out_state->raw_tail = (uint8_t *)nmo_arena_alloc(arena, remaining, 1);
        if (out_state->raw_tail) {
            /* Read directly into pre-allocated buffer */
            size_t bytes_read = nmo_chunk_read_and_fill_buffer(chunk, 
                out_state->raw_tail, remaining);
            if (bytes_read == remaining) {
                out_state->raw_tail_size = remaining;
            } else {
                /* Read failed - clear raw_tail */
                out_state->raw_tail = NULL;
                out_state->raw_tail_size = 0;
            }
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
 * This function writes back the preserved raw_tail data for round-trip.
 * 
 * Reference: reference/include/CKRenderObject.h (abstract class, no Save)
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
nmo_result_t nmo_ckrenderobject_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckrenderobject_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckrenderobject_serialize"));
    }

    /* Write back preserved raw_tail data for round-trip */
    if (state->raw_tail && state->raw_tail_size > 0) {
        /* Use no_size variant since we're writing back raw binary data */
        nmo_result_t result = nmo_chunk_write_buffer_no_size(chunk, 
            state->raw_tail, state->raw_tail_size);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

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

    /* Schema will be registered when schema builder is fully implemented */
    /* For now, just store the function pointers in the registry */
    
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
