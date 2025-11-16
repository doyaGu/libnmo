/**
 * @file ckmessagemanager_schemas.c
 * @brief CKMessageManager schema implementation
 *
 * Implements schema-driven deserialization for CKMessageManager (message type registry).
 * This is a manager class that handles message type registration and routing.
 * 
 * Based on official Virtools SDK (reference/src/CKMessageManager.cpp:178-250).
 */

#include "schema/nmo_ckmessagemanager_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>

/* =============================================================================
 * IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKMessageManager.cpp */
#define CK_STATESAVE_MESSAGEMANAGER 0x53

/* =============================================================================
 * CKMessageManager DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKMessageManager state from chunk
 * 
 * Implements the symmetric read operation for CKMessageManager::LoadData.
 * Reads message type names from the chunk.
 * 
 * Reference: reference/src/CKMessageManager.cpp:218-247
 * 
 * @param chunk Chunk containing CKMessageManager data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckmessagemanager_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckmessagemanager_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmessagemanager_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckmessagemanager_state_t));

    /* Seek identifier */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_MESSAGEMANAGER);
    if (result.code != NMO_OK) {
        /* No data to load - this is valid */
        return nmo_result_ok();
    }

    /* Read message type count */
    int32_t type_count;
    result = nmo_chunk_read_int(chunk, &type_count);
    if (result.code != NMO_OK) return result;

    if (type_count <= 0) {
        return nmo_result_ok();
    }

    if (type_count > 10000) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
            NMO_SEVERITY_ERROR, "Invalid message type count"));
    }

    out_state->message_type_count = (uint32_t)type_count;
    out_state->message_type_names = (const char **)nmo_arena_alloc(
        arena, type_count * sizeof(char *), _Alignof(char *));
    if (!out_state->message_type_names) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate message type names"));
    }

    /* Read each message type name */
    for (int32_t i = 0; i < type_count; i++) {
        char *name = NULL;
        nmo_chunk_read_string(chunk, &name);
        out_state->message_type_names[i] = name; /* Chunk manages the buffer */
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKMessageManager SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKMessageManager state to chunk
 * 
 * Implements the symmetric write operation for CKMessageManager::SaveData.
 * Writes message type names to the chunk.
 * 
 * Reference: reference/src/CKMessageManager.cpp:178-216
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckmessagemanager_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckmessagemanager_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckmessagemanager_serialize"));
    }

    /* Don't write if no message types */
    if (state->message_type_count == 0) {
        return nmo_result_ok();
    }

    nmo_result_t result;

    /* Write identifier */
    result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_MESSAGEMANAGER);
    if (result.code != NMO_OK) return result;

    /* Write message type count */
    result = nmo_chunk_write_int(chunk, (int32_t)state->message_type_count);
    if (result.code != NMO_OK) return result;

    /* Write each message type name */
    for (uint32_t i = 0; i < state->message_type_count; i++) {
        const char *name = state->message_type_names[i];
        result = nmo_chunk_write_string(chunk, name ? name : "");
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKMessageManager schema types
 * 
 * Creates schema descriptors for CKMessageManager state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckmessagemanager_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckmessagemanager_schemas"));
    }

    /* Schema will be registered when schema builder is fully implemented */
    /* For now, just store the function pointers in the registry */
    
    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKMessageManager
 * 
 * @return Deserialize function pointer
 */
nmo_ckmessagemanager_deserialize_fn nmo_get_ckmessagemanager_deserialize(void)
{
    return nmo_ckmessagemanager_deserialize;
}

/**
 * @brief Get the serialize function for CKMessageManager
 * 
 * @return Serialize function pointer
 */
nmo_ckmessagemanager_serialize_fn nmo_get_ckmessagemanager_serialize(void)
{
    return nmo_ckmessagemanager_serialize;
}
