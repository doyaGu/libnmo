/**
 * @file ckbehaviorio_schemas.c
 * @brief CKBehaviorIO schema implementation
 *
 * Implements schema-driven deserialization for CKBehaviorIO (behavior I/O endpoints).
 * CKBehaviorIO extends CKObject and is a simple class storing only I/O flags.
 * 
 * Based on official Virtools SDK (reference/src/CKBehaviorIO.cpp:19-48).
 */

#include "schema/nmo_ckbehaviorio_schemas.h"
#include "schema/nmo_ckobject_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "schema/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* =============================================================================
 * IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From reference/src/CKBehaviorIO.cpp */
#define CK_STATESAVE_BEHAV_IOFLAGS 0x00000001

/* =============================================================================
 * CKBehaviorIO DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBehaviorIO state from chunk
 * 
 * Implements the symmetric read operation for CKBehaviorIO::Load.
 * Reads I/O flags that determine the endpoint type and characteristics.
 * 
 * Reference: reference/src/CKBehaviorIO.cpp:39-48
 * 
 * @param chunk Chunk containing CKBehaviorIO data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckbehaviorio_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckbehaviorio_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehaviorio_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckbehaviorio_state_t));

    /* Read I/O flags */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_IOFLAGS);
    if (result.code == NMO_OK) {
        result = nmo_chunk_read_dword(chunk, &out_state->old_flags);
        if (result.code != NMO_OK) return result;
    }
    /* Note: If identifier not found, old_flags remains 0 (valid for older versions) */

    return nmo_result_ok();
}

/* =============================================================================
 * CKBehaviorIO SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBehaviorIO state to chunk
 * 
 * Implements the symmetric write operation for CKBehaviorIO::Save.
 * Writes I/O flags that determine the endpoint type.
 * 
 * Reference: reference/src/CKBehaviorIO.cpp:19-36
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckbehaviorio_serialize(
    const nmo_ckbehaviorio_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehaviorio_serialize"));
    }

    nmo_result_t result;

    /* Write I/O flags */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_IOFLAGS);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->old_flags);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKBehaviorIO schema types
 * 
 * Creates schema descriptors for CKBehaviorIO state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
/* =============================================================================
 * VTABLE IMPLEMENTATION
 * ============================================================================= */

static nmo_result_t vtable_read_ckbehaviorio(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr) {
    (void)type;
    return nmo_ckbehaviorio_deserialize(chunk, arena, (nmo_ckbehaviorio_state_t *)out_ptr);
}

static nmo_result_t vtable_write_ckbehaviorio(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena) {
    (void)type;
    return nmo_ckbehaviorio_serialize((const nmo_ckbehaviorio_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckbehaviorio_vtable = {
    .read = vtable_read_ckbehaviorio,
    .write = vtable_write_ckbehaviorio,
    .validate = NULL
};

nmo_result_t nmo_register_ckbehaviorio_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckbehaviorio_schemas"));
    }

    /* Register minimal schema with vtable */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKBehaviorIOState",
                                                      sizeof(nmo_ckbehaviorio_state_t),
                                                      alignof(nmo_ckbehaviorio_state_t));
    
    nmo_builder_set_vtable(&builder, &nmo_ckbehaviorio_vtable);
    
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
 * @brief Get the deserialize function for CKBehaviorIO
 * 
 * @return Deserialize function pointer
 */
nmo_ckbehaviorio_deserialize_fn nmo_get_ckbehaviorio_deserialize(void)
{
    return nmo_ckbehaviorio_deserialize;
}

/**
 * @brief Get the serialize function for CKBehaviorIO
 * 
 * @return Serialize function pointer
 */
nmo_ckbehaviorio_serialize_fn nmo_get_ckbehaviorio_serialize(void)
{
    return nmo_ckbehaviorio_serialize;
}
