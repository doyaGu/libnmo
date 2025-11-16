/**
 * @file ckbehaviorlink_schemas.c
 * @brief CKBehaviorLink schema implementation
 *
 * Implements schema-driven deserialization for CKBehaviorLink (behavior graph connections).
 * CKBehaviorLink extends CKObject and stores timing delays plus I/O endpoint references.
 * 
 * Based on official Virtools SDK (reference/src/CKBehaviorLink.cpp:49-121).
 */

#include "schema/nmo_ckbehaviorlink_schemas.h"
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

/* From reference/src/CKBehaviorLink.cpp */
#define CK_STATESAVE_BEHAV_LINK_NEWDATA   0x00000001
#define CK_STATESAVE_BEHAV_LINK_CURDELAY  0x00000002  /* Legacy format */
#define CK_STATESAVE_BEHAV_LINK_IOS       0x00000004  /* Legacy format */

/* =============================================================================
 * CKBehaviorLink DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBehaviorLink state from chunk
 * 
 * Implements the symmetric read operation for CKBehaviorLink::Load.
 * Reads activation delays and I/O endpoint references.
 * Supports both new format (NEWDATA) and legacy format (CURDELAY + IOS).
 * 
 * Reference: reference/src/CKBehaviorLink.cpp:73-121
 * 
 * @param chunk Chunk containing CKBehaviorLink data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckbehaviorlink_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckbehaviorlink_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehaviorlink_deserialize"));
    }

    /* Initialize state with defaults */
    memset(out_state, 0, sizeof(nmo_ckbehaviorlink_state_t));
    out_state->activation_delay = 1;
    out_state->initial_activation_delay = 1;

    nmo_result_t result;

    /* Try new format first (preferred) */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_LINK_NEWDATA);
    if (result.code == NMO_OK) {
        /* New format: packed delays (lower 16 bits = activation, upper 16 bits = initial) */
        uint32_t delays;
        result = nmo_chunk_read_dword(chunk, &delays);
        if (result.code != NMO_OK) return result;

        out_state->activation_delay = (int16_t)(delays & 0xFFFF);
        out_state->initial_activation_delay = (int16_t)((delays >> 16) & 0xFFFF);

        /* Read I/O object references */
        result = nmo_chunk_read_object_id(chunk, &out_state->in_io_id);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_read_object_id(chunk, &out_state->out_io_id);
        if (result.code != NMO_OK) return result;
    } else {
        /* Legacy format support */
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_LINK_CURDELAY);
        if (result.code == NMO_OK) {
            int32_t delay;
            result = nmo_chunk_read_int(chunk, &delay);
            if (result.code != NMO_OK) return result;
            out_state->activation_delay = (int16_t)delay;
        }

        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAV_LINK_IOS);
        if (result.code == NMO_OK) {
            result = nmo_chunk_read_object_id(chunk, &out_state->in_io_id);
            if (result.code != NMO_OK) return result;

            result = nmo_chunk_read_object_id(chunk, &out_state->out_io_id);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKBehaviorLink SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBehaviorLink state to chunk
 * 
 * Implements the symmetric write operation for CKBehaviorLink::Save.
 * Writes activation delays and I/O endpoint references in new format.
 * 
 * Reference: reference/src/CKBehaviorLink.cpp:49-71
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckbehaviorlink_serialize(
    const nmo_ckbehaviorlink_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbehaviorlink_serialize"));
    }

    nmo_result_t result;

    /* Write new format identifier */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BEHAV_LINK_NEWDATA);
    if (result.code != NMO_OK) return result;

    /* Pack delays into single DWORD (lower 16 bits = activation, upper 16 bits = initial) */
    uint32_t delays = ((uint32_t)in_state->activation_delay & 0xFFFF) |
                      (((uint32_t)in_state->initial_activation_delay & 0xFFFF) << 16);
    result = nmo_chunk_write_dword(out_chunk, delays);
    if (result.code != NMO_OK) return result;

    /* Write I/O object references */
    result = nmo_chunk_write_object_id(out_chunk, in_state->in_io_id);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(out_chunk, in_state->out_io_id);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKBehaviorLink schema types
 * 
 * Creates schema descriptors for CKBehaviorLink state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
/* =============================================================================
 * VTABLE IMPLEMENTATION
 * ============================================================================= */

static nmo_result_t vtable_read_ckbehaviorlink(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr) {
    (void)type;
    return nmo_ckbehaviorlink_deserialize(chunk, arena, (nmo_ckbehaviorlink_state_t *)out_ptr);
}

static nmo_result_t vtable_write_ckbehaviorlink(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena) {
    (void)type;
    return nmo_ckbehaviorlink_serialize((const nmo_ckbehaviorlink_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckbehaviorlink_vtable = {
    .read = vtable_read_ckbehaviorlink,
    .write = vtable_write_ckbehaviorlink,
    .validate = NULL
};

nmo_result_t nmo_register_ckbehaviorlink_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckbehaviorlink_schemas"));
    }

    /* Register minimal schema with vtable */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKBehaviorLinkState",
                                                      sizeof(nmo_ckbehaviorlink_state_t),
                                                      alignof(nmo_ckbehaviorlink_state_t));
    
    nmo_builder_set_vtable(&builder, &nmo_ckbehaviorlink_vtable);
    
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
 * @brief Get the deserialize function for CKBehaviorLink
 * 
 * @return Deserialize function pointer
 */
nmo_ckbehaviorlink_deserialize_fn nmo_get_ckbehaviorlink_deserialize(void)
{
    return nmo_ckbehaviorlink_deserialize;
}

/**
 * @brief Get the serialize function for CKBehaviorLink
 * 
 * @return Serialize function pointer
 */
nmo_ckbehaviorlink_serialize_fn nmo_get_ckbehaviorlink_serialize(void)
{
    return nmo_ckbehaviorlink_serialize;
}
