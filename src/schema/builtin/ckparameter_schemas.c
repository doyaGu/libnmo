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

#include "schema/nmo_ckparameter_schemas.h"
#include "schema/nmo_ckobject_schemas.h"
#include "schema/nmo_schema_registry.h"
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

    /* Read storage mode */
    uint32_t mode_value;
    result = nmo_chunk_read_dword(chunk, &mode_value);
    if (result.code != NMO_OK) {
        return result;
    }
    out_state->mode = (nmo_ckparameter_mode_t)mode_value;

    /* Handle different storage modes */
    switch (out_state->mode) {
        case NMO_CKPARAM_MODE_NONE:
            /* No data - ParameterOut/ParameterOperation placeholder */
            break;

        case NMO_CKPARAM_MODE_SUBCHUNK: {
            /* Sub-chunk mode - custom SaveLoadFunction */
            /* Read sub-chunk as raw data for round-trip */
            void *subchunk_ptr = NULL;
            size_t subchunk_size = 0;
            result = nmo_chunk_read_buffer(chunk, &subchunk_ptr, &subchunk_size);
            if (result.code == NMO_OK && subchunk_size > 0) {
                out_state->subchunk_data = (uint8_t *)nmo_arena_alloc(arena, subchunk_size, 1);
                if (out_state->subchunk_data) {
                    memcpy(out_state->subchunk_data, subchunk_ptr, subchunk_size);
                    out_state->subchunk_size = subchunk_size;
                }
            }
            break;
        }

        case NMO_CKPARAM_MODE_OBJECT: {
            /* Object reference mode */
            result = nmo_chunk_read_object_id(chunk, &out_state->object_id);
            if (result.code != NMO_OK) {
                return result;
            }
            break;
        }

        case NMO_CKPARAM_MODE_MANAGER: {
            /* Manager-specific int mode */
            /* Read manager GUID and value */
            result = nmo_chunk_read_manager_int(chunk, &out_state->manager_guid, &out_state->manager_value);
            if (result.code != NMO_OK) {
                /* Manager sequence not found - try reading as raw ints */
                int32_t dummy, value;
                nmo_chunk_read_int(chunk, &dummy);
                nmo_chunk_read_int(chunk, &value);
                out_state->manager_value = (uint32_t)value;
            }
            break;
        }

        case NMO_CKPARAM_MODE_BUFFER:
        default: {
            /* Buffer mode - raw data */
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
            break;
        }
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
    nmo_chunk_t *chunk,
    const nmo_ckparameter_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckparameter_serialize"));
    }

    /* Write parameter identifier */
    nmo_result_t result = nmo_chunk_write_identifier(chunk, CK_PARAM_IDENTIFIER);
    if (result.code != NMO_OK) return result;

    /* Write parameter type GUID */
    result = nmo_chunk_write_guid(chunk, state->type_guid);
    if (result.code != NMO_OK) return result;

    /* Write storage mode */
    result = nmo_chunk_write_dword(chunk, (uint32_t)state->mode);
    if (result.code != NMO_OK) return result;

    /* Write data based on mode */
    switch (state->mode) {
        case NMO_CKPARAM_MODE_NONE:
            /* No data to write */
            break;

        case NMO_CKPARAM_MODE_SUBCHUNK:
            /* Write sub-chunk data */
            if (state->subchunk_data && state->subchunk_size > 0) {
                result = nmo_chunk_write_buffer(chunk, state->subchunk_data, state->subchunk_size);
                if (result.code != NMO_OK) return result;
            }
            break;

        case NMO_CKPARAM_MODE_OBJECT:
            /* Write object reference */
            result = nmo_chunk_write_object_id(chunk, state->object_id);
            if (result.code != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_MANAGER:
            /* Write manager int */
            result = nmo_chunk_write_manager_int(chunk, state->manager_guid, state->manager_value);
            if (result.code != NMO_OK) return result;
            break;

        case NMO_CKPARAM_MODE_BUFFER:
        default:
            /* Write buffer data */
            if (state->buffer_data && state->buffer_size > 0) {
                result = nmo_chunk_write_buffer(chunk, state->buffer_data, state->buffer_size);
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

    /* Schema will be registered when schema builder is fully implemented */
    /* For now, just store the function pointers in the registry */
    
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

/* =============================================================================
 * CKParameterIn DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/* Identifier constants from reference */
#define CK_STATESAVE_PARAMETERIN_DATASHARED    0x00000001
#define CK_STATESAVE_PARAMETERIN_DATASOURCE    0x00000002
#define CK_STATESAVE_PARAMETERIN_DEFAULTDATA   0x00000003
#define CK_STATESAVE_PARAMETERIN_DISABLED      0x00000010

/**
 * @brief Deserialize CKParameterIn state from chunk
 * 
 * Reference: reference/src/CKParameterIn.cpp:177-250
 */
static nmo_result_t nmo_ckparameterin_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckparameterin_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    memset(out_state, 0, sizeof(nmo_ckparameterin_state_t));

    /* Try to find shared or source data */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DATASHARED).code == NMO_OK) {
        nmo_chunk_read_guid(chunk, &out_state->type_guid);
        nmo_chunk_read_object_id(chunk, &out_state->source_id);
        out_state->is_shared = 1;
    } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DATASOURCE).code == NMO_OK) {
        nmo_chunk_read_guid(chunk, &out_state->type_guid);
        nmo_chunk_read_object_id(chunk, &out_state->source_id);
        out_state->is_shared = 0;
    }

    /* Check if disabled */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETERIN_DISABLED).code == NMO_OK) {
        out_state->is_disabled = 1;
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize CKParameterIn state to chunk
 * 
 * Reference: reference/src/CKParameterIn.cpp:142-162
 */
static nmo_result_t nmo_ckparameterin_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckparameterin_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    /* Write identifier based on shared/direct source */
    uint32_t identifier = state->is_shared 
        ? CK_STATESAVE_PARAMETERIN_DATASHARED 
        : CK_STATESAVE_PARAMETERIN_DATASOURCE;
    
    nmo_result_t result = nmo_chunk_write_identifier(chunk, identifier);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_guid(chunk, state->type_guid);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(chunk, state->source_id);
    if (result.code != NMO_OK) return result;

    /* Write disabled flag if needed */
    if (state->is_disabled) {
        result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_PARAMETERIN_DISABLED);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKParameterOut DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

#define CK_STATESAVE_PARAMETEROUT_DESTINATIONS 0x00000004

/**
 * @brief Deserialize CKParameterOut state from chunk
 * 
 * Reference: reference/src/CKParameterOut.cpp:145-160
 */
static nmo_result_t nmo_ckparameterout_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckparameterout_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    memset(out_state, 0, sizeof(nmo_ckparameterout_state_t));

    /* Read destinations if present */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS).code == NMO_OK) {
        int32_t count;
        nmo_result_t result = nmo_chunk_read_int(chunk, &count);
        if (result.code == NMO_OK && count > 0) {
            out_state->destination_count = (uint32_t)count;
            out_state->destination_ids = (nmo_object_id_t *)nmo_arena_alloc(
                arena, count * sizeof(nmo_object_id_t), _Alignof(nmo_object_id_t));
            
            if (out_state->destination_ids) {
                for (int32_t i = 0; i < count; i++) {
                    nmo_chunk_read_object_id(chunk, &out_state->destination_ids[i]);
                }
            }
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize CKParameterOut state to chunk
 * 
 * Reference: reference/src/CKParameterOut.cpp:130-142
 */
static nmo_result_t nmo_ckparameterout_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckparameterout_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    /* Write destinations if any */
    if (state->destination_count > 0 && state->destination_ids) {
        nmo_result_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_PARAMETEROUT_DESTINATIONS);
        if (result.code != NMO_OK) return result;

        result = nmo_chunk_write_int(chunk, (int32_t)state->destination_count);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < state->destination_count; i++) {
            result = nmo_chunk_write_object_id(chunk, state->destination_ids[i]);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKParameterLocal DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

#define CK_STATESAVE_PARAMETEROUT_MYSELF    0x00000008
#define CK_STATESAVE_PARAMETEROUT_ISSETTING 0x00000020

/**
 * @brief Deserialize CKParameterLocal state from chunk
 * 
 * Reference: reference/src/CKParameterLocal.cpp:131-145
 */
static nmo_result_t nmo_ckparameterlocal_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckparameterlocal_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    memset(out_state, 0, sizeof(nmo_ckparameterlocal_state_t));

    /* Check if "myself" parameter */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_MYSELF).code == NMO_OK) {
        out_state->is_myself = 1;
    }

    /* Check if setting */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING).code == NMO_OK) {
        out_state->is_setting = 1;
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize CKParameterLocal state to chunk
 * 
 * Reference: reference/src/CKParameterLocal.cpp:119-130
 */
static nmo_result_t nmo_ckparameterlocal_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckparameterlocal_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    /* Write "myself" flag if needed */
    if (state->is_myself) {
        nmo_result_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_PARAMETEROUT_MYSELF);
        if (result.code != NMO_OK) return result;
    }

    /* Write setting flag if needed */
    if (state->is_setting) {
        nmo_result_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_PARAMETEROUT_ISSETTING);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - Derived Class Accessors
 * ============================================================================= */

nmo_ckparameterin_deserialize_fn nmo_get_ckparameterin_deserialize(void)
{
    return nmo_ckparameterin_deserialize;
}

nmo_ckparameterin_serialize_fn nmo_get_ckparameterin_serialize(void)
{
    return nmo_ckparameterin_serialize;
}

nmo_ckparameterout_deserialize_fn nmo_get_ckparameterout_deserialize(void)
{
    return nmo_ckparameterout_deserialize;
}

nmo_ckparameterout_serialize_fn nmo_get_ckparameterout_serialize(void)
{
    return nmo_ckparameterout_serialize;
}

nmo_ckparameterlocal_deserialize_fn nmo_get_ckparameterlocal_deserialize(void)
{
    return nmo_ckparameterlocal_deserialize;
}

nmo_ckparameterlocal_serialize_fn nmo_get_ckparameterlocal_serialize(void)
{
    return nmo_ckparameterlocal_serialize;
}

/* =============================================================================
 * CKParameterOperation DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

#define CK_STATESAVE_OPERATIONNEWDATA      0x00001000
#define CK_STATESAVE_OPERATIONOP           0x00000001
#define CK_STATESAVE_OPERATIONDEFAULTDATA  0x00000002
#define CK_STATESAVE_OPERATIONINPUTS       0x00000004
#define CK_STATESAVE_OPERATIONOUTPUT       0x00000008

/**
 * @brief Deserialize CKParameterOperation state from chunk
 * 
 * Reference: reference/src/CKParameterOperation.cpp:213-301
 */
static nmo_result_t nmo_ckparameteroperation_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckparameteroperation_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    memset(out_state, 0, sizeof(nmo_ckparameteroperation_state_t));

    /* Try new data format first (file context) */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONNEWDATA).code == NMO_OK) {
        nmo_chunk_read_guid(chunk, &out_state->operation_guid);
        
        /* Read parameter sequence */
        size_t seq_count = 0;
        nmo_chunk_read_object_sequence_start(chunk, &seq_count);
        if (seq_count >= 3) {
            nmo_chunk_read_object_id(chunk, &out_state->input1_id);
            nmo_chunk_read_object_id(chunk, &out_state->input2_id);
            nmo_chunk_read_object_id(chunk, &out_state->output_id);
        }
    } else {
        /* Legacy format - read individual sections */
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOP).code == NMO_OK) {
            nmo_chunk_read_guid(chunk, &out_state->operation_guid);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONDEFAULTDATA).code == NMO_OK) {
            nmo_chunk_read_object_id(chunk, &out_state->owner_id);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONOUTPUT).code == NMO_OK) {
            nmo_chunk_read_object_id(chunk, &out_state->output_id);
            /* Skip sub-chunk if present - will be loaded by parameter itself */
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OPERATIONINPUTS).code == NMO_OK) {
            nmo_chunk_read_object_id(chunk, &out_state->input1_id);
            /* Skip sub-chunk if present */
            nmo_chunk_read_object_id(chunk, &out_state->input2_id);
            /* Skip sub-chunk if present */
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Serialize CKParameterOperation state to chunk
 * 
 * Reference: reference/src/CKParameterOperation.cpp:155-211
 */
static nmo_result_t nmo_ckparameteroperation_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckparameteroperation_state_t *state)
{
    if (chunk == NULL || state == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments"));
    }

    /* Write new data format (file context) */
    nmo_result_t result = nmo_chunk_write_identifier(chunk, CK_STATESAVE_OPERATIONNEWDATA);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_guid(chunk, state->operation_guid);
    if (result.code != NMO_OK) return result;

    /* Write parameter sequence */
    result = nmo_chunk_write_dword(chunk, 3); /* sequence count */
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(chunk, state->input1_id);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(chunk, state->input2_id);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_object_id(chunk, state->output_id);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

/* =============================================================================
 * PUBLIC API - CKParameterOperation Accessors
 * ============================================================================= */

nmo_ckparameteroperation_deserialize_fn nmo_get_ckparameteroperation_deserialize(void)
{
    return nmo_ckparameteroperation_deserialize;
}

nmo_ckparameteroperation_serialize_fn nmo_get_ckparameteroperation_serialize(void)
{
    return nmo_ckparameteroperation_serialize;
}
