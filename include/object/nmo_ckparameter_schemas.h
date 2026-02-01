/**
 * @file nmo_ckparameter_schemas.h
 * @brief Public API for CKParameter family schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKParameter.
 * 
 * CKParameter is the base class for parameters that hold typed data values.
 * It stores a GUID-identified type and a variable-sized buffer.
 * 
 * Based on official Virtools SDK:
 * - CKParameter (reference/src/CKParameter.cpp:245-450)
 */

#ifndef NMO_CKPARAMETER_SCHEMAS_H
#define NMO_CKPARAMETER_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/* =============================================================================
 * CKParameter STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief Parameter data storage mode
 * 
 * CKParameter supports multiple ways to store data:
 * - MODE_BUFFER: Raw buffer (most common)
 * - MODE_OBJECT: Object reference (CK_ID)
 * - MODE_MANAGER: Manager-specific int value
 * - MODE_SUBCHUNK: Custom sub-chunk (SaveLoadFunction)
 * - MODE_NONE: No data (ParameterOut or ParameterOperation placeholder)
 */
typedef enum nmo_ckparameter_mode {
    NMO_CKPARAM_MODE_NONE = 3,       /**< No data stored */
    NMO_CKPARAM_MODE_BUFFER = 1,     /**< Raw buffer data */
    NMO_CKPARAM_MODE_OBJECT = 2,     /**< Object reference (CK_ID) */
    NMO_CKPARAM_MODE_MANAGER = 4,    /**< Manager-specific int */
    NMO_CKPARAM_MODE_SUBCHUNK = 0    /**< Custom sub-chunk */
} nmo_ckparameter_mode_t;

/**
 * @brief CKParameter state
 * 
 * Represents a parameter with typed data.
 * The actual data is stored in one of several formats based on mode.
 * 
 * Reference: reference/src/CKParameter.cpp:245-450
 */
typedef struct nmo_ckparameter_state {
    /* Parameter type identification */
    nmo_guid_t type_guid;              /**< Parameter type GUID */
    
    /* Storage mode and data */
    nmo_ckparameter_mode_t mode;       /**< How data is stored */
    
    /* Buffer mode (MODE_BUFFER) */
    uint8_t *buffer_data;              /**< Parameter data buffer */
    size_t buffer_size;                /**< Buffer size in bytes */
    
    /* Object mode (MODE_OBJECT) */
    nmo_object_id_t object_id;         /**< Referenced object ID */
    
    /* Manager mode (MODE_MANAGER) */
    nmo_guid_t manager_guid;           /**< Manager GUID */
    uint32_t manager_value;            /**< Manager-specific value */
    
    /* Sub-chunk mode (MODE_SUBCHUNK) */
    nmo_chunk_t *subchunk;             /**< Sub-chunk payload */
} nmo_ckparameter_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief CKParameter deserialize function pointer type
 * 
 * @param chunk Chunk containing CKParameter data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckparameter_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckparameter_state_t *out_state);

/**
 * @brief CKParameter serialize function pointer type
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckparameter_serialize_fn)(
    const nmo_ckparameter_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKParameter schema types
 * 
 * Registers schema types for CKParameter and derived classes.
 * Must be called during initialization before using CKParameter schemas.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ckparameter_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKParameter
 * 
 * @return Deserialize function pointer
 */
NMO_API nmo_ckparameter_deserialize_fn nmo_get_ckparameter_deserialize(void);

/**
 * @brief Get the serialize function for CKParameter
 * 
 * @return Serialize function pointer
 */
NMO_API nmo_ckparameter_serialize_fn nmo_get_ckparameter_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETER_SCHEMAS_H */
