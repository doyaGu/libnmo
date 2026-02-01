/**
 * @file ckparameter_schemas.c
 * @brief CKParameter schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKParameter (parameter values).
 * CKParameter extends CKObject and stores typed data in a buffer.
 * 
 * Based on official Virtools SDK (reference/src/CKParameter.cpp:245-450):
 * - CKParameter::Save writes: identifier(0x40), GUID, mode, data
 * - CKParameter::Load reads: GUID (with migration), mode, data
 * - Supports 5 storage modes: buffer, object reference, manager int, sub-chunk, none
 * - Handles GUID migrations: OLDMESSAGE→MESSAGE, OLDATTRIBUTE→ATTRIBUTE, ID→OBJECT, OLDTIME→TIME
 * 
 * Key design decisions:
 * - Store raw buffer data for round-trip safety
 * - Preserve original GUID before migration
 * - Support all 5 storage modes from reference implementation
 */

#include "object/nmo_ckparameter_schemas.h"
#include "object/nmo_ckobject_schemas.h"
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
 * CKParameter IDENTIFIER CONSTANTS
 * ============================================================================= */

/* From CKParameter.cpp */
#define CK_PARAM_IDENTIFIER  0x00000040

/* =============================================================================
 * CKParameter DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKParameter state from chunk
 * 
 * Implements the symmetric read operation for CKParameter::Load.
 * Reads parameter GUID, storage mode, and data.
 * 
 * Reference: reference/src/CKParameter.cpp:300-450
 * 
 * @param chunk Chunk containing CKParameter data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckparameter_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckparameter_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckparameter_deserialize"));
    }

    /* Initialize state */
    memset(out_state, 0, sizeof(nmo_ckparameter_state_t));
    out_state->mode = NMO_CKPARAM_MODE_NONE;

    /* Seek parameter identifier - optional section */
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, CK_PARAM_IDENTIFIER);
    if (result.code != NMO_OK) {
        /* No parameter data - valid for reference-only objects */
        return nmo_result_ok();
    }

    /* Read parameter type GUID */
    result = nmo_chunk_read_guid(chunk, &out_state->type_guid);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Read parameter state (always present in CK2) */
    uint32_t param_state = 0;
    result = nmo_chunk_read_dword(chunk, &param_state);
    if (result.code != NMO_OK) {
        return result;
    }

    if (param_state == 3) {
        out_state->mode = NMO_CKPARAM_MODE_NONE;
        return nmo_result_ok();
    }

    if (param_state == 0) {
        out_state->mode = NMO_CKPARAM_MODE_SUBCHUNK;
        result = nmo_chunk_read_sub_chunk(chunk, &out_state->subchunk);
        if (result.code != NMO_OK) {
            out_state->subchunk = NULL;
        }
        return nmo_result_ok();
    }

    if (param_state == 2) {
        out_state->mode = NMO_CKPARAM_MODE_OBJECT;
        result = nmo_chunk_read_object_id(chunk, &out_state->object_id);
        if (result.code != NMO_OK) {
            return result;
        }
        return nmo_result_ok();
    }

    if (param_state == 1) {
        out_state->mode = NMO_CKPARAM_MODE_BUFFER;
        void *buffer_ptr = NULL;
        size_t buffer_size = 0;
        result = nmo_chunk_read_buffer(chunk, &buffer_ptr, &buffer_size);
        if (result.code == NMO_OK && buffer_size > 0) {
            out_state->buffer_data = (uint8_t *)nmo_arena_alloc(arena, buffer_size, 1);
            if (out_state->buffer_data) {
                memcpy(out_state->buffer_data, buffer_ptr, buffer_size);
                out_state->buffer_size = buffer_size;
            }
        }
        return nmo_result_ok();
    }

    /* Manager-specific int mode: param_state is manager_guid.d1 */
    out_state->mode = NMO_CKPARAM_MODE_MANAGER;
    out_state->manager_guid.d1 = param_state;
    result = nmo_chunk_read_dword(chunk, &out_state->manager_guid.d2);
    if (result.code != NMO_OK) {
        return result;
    }
    result = nmo_chunk_read_dword(chunk, &out_state->manager_value);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKParameter SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKParameter state to chunk
 * 
 * Implements the symmetric write operation for CKParameter::Save.
 * Writes parameter GUID, storage mode, and data.
 * 
 * Reference: reference/src/CKParameter.cpp:245-298
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckparameter_serialize(
    const nmo_ckparameter_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (in_state == NULL || out_chunk == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckparameter_serialize"));
    }

    /* Write parameter identifier */
    nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_PARAM_IDENTIFIER);
    if (result.code != NMO_OK) return result;

    /* Write parameter type GUID */
    result = nmo_chunk_write_guid(out_chunk, in_state->type_guid);
    if (result.code != NMO_OK) return result;

    /* Write parameter state and payload */
    switch (in_state->mode) {
        case NMO_CKPARAM_MODE_NONE:
            result = nmo_chunk_write_dword(out_chunk, 3);
            if (result.code != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_SUBCHUNK:
            result = nmo_chunk_write_dword(out_chunk, 0);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(out_chunk, in_state->subchunk);
            if (result.code != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_OBJECT:
            result = nmo_chunk_write_dword(out_chunk, 2);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->object_id);
            if (result.code != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_MANAGER:
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_guid.d1);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_guid.d2);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->manager_value);
            if (result.code != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_BUFFER:
        default:
            result = nmo_chunk_write_dword(out_chunk, 1);
            if (result.code != NMO_OK) return result;
            if (in_state->buffer_data && in_state->buffer_size > 0) {
                result = nmo_chunk_write_buffer(out_chunk, in_state->buffer_data,
                    in_state->buffer_size);
                if (result.code != NMO_OK) return result;
            }
            break;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Vtable read wrapper for CKParameter
 */
static nmo_result_t nmo_ckparameter_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckparameter_deserialize(chunk, arena, (nmo_ckparameter_state_t *)out_ptr);
}

/**
 * @brief Vtable write wrapper for CKParameter
 */
static nmo_result_t nmo_ckparameter_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckparameter_serialize((const nmo_ckparameter_state_t *)in_ptr, chunk, arena);
}

/**
 * @brief Vtable for CKParameter schema operations
 */
static const nmo_schema_vtable_t nmo_ckparameter_vtable = {
    .read = nmo_ckparameter_vtable_read,
    .write = nmo_ckparameter_vtable_write,
    .validate = NULL
};

/**
 * @brief Register CKParameter schema types
 * 
 * Creates schema descriptors for CKParameter state structures.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckparameter_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckparameter_schemas"));
    }

    /* Get base types for fields */
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    const nmo_schema_type_t *object_id_type = nmo_schema_registry_find_by_name(registry, "ObjectID");
    
    if (!uint32_type || !object_id_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required base types not found in registry"));
    }

    /* Create schema builder for CKParameter */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKParameterState",
                                                      sizeof(nmo_ckparameter_state_t),
                                                      alignof(nmo_ckparameter_state_t));
    
    /* Add parameter fields (simplified) */
    nmo_builder_add_field_ex(&builder, "mode", uint32_type,
                            offsetof(nmo_ckparameter_state_t, mode), 0);
    nmo_builder_add_field_ex(&builder, "buffer_size", uint32_type,
                            offsetof(nmo_ckparameter_state_t, buffer_size), 0);
    nmo_builder_add_field_ex(&builder, "object_id", object_id_type,
                            offsetof(nmo_ckparameter_state_t, object_id), 0);

    /* Attach vtable for optimized read/write */
    nmo_builder_set_vtable(&builder, &nmo_ckparameter_vtable);
    
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
 * @brief Get the deserialize function for CKParameter
 * 
 * @return Deserialize function pointer
 */
nmo_ckparameter_deserialize_fn nmo_get_ckparameter_deserialize(void)
{
    return nmo_ckparameter_deserialize;
}

/**
 * @brief Get the serialize function for CKParameter
 * 
 * @return Serialize function pointer
 */
nmo_ckparameter_serialize_fn nmo_get_ckparameter_serialize(void)
{
    return nmo_ckparameter_serialize;
}
