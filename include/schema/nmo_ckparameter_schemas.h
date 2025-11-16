/**
 * @file nmo_ckparameter_schemas.h
 * @brief Public API for CKParameter family schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKParameter
 * and its derived classes (CKParameterIn, CKParameterOut, CKParameterLocal).
 * 
 * CKParameter is the base class for parameters that hold typed data values.
 * It stores a GUID-identified type and a variable-sized buffer.
 * 
 * Based on official Virtools SDK:
 * - CKParameter (reference/src/CKParameter.cpp:245-450)
 * - CKParameterIn (reference/src/CKParameterIn.cpp:140-250)
 * - CKParameterOut (reference/src/CKParameterOut.cpp:120-160)
 * - CKParameterLocal (reference/src/CKParameterLocal.cpp:100-140)
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
    uint8_t *subchunk_data;            /**< Sub-chunk raw data */
    size_t subchunk_size;              /**< Sub-chunk size */
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

/* =============================================================================
 * CKParameterIn STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKParameterIn state
 * 
 * Input parameters get data from a source (direct source or shared input).
 * They don't own data - they reference another parameter.
 * 
 * Reference: reference/src/CKParameterIn.cpp:170-250
 */
typedef struct nmo_ckparameterin_state {
    nmo_guid_t type_guid;              /**< Parameter type GUID */
    nmo_object_id_t source_id;         /**< Source parameter ID (direct or shared) */
    uint8_t is_shared;                 /**< TRUE if shared input, FALSE if direct source */
    uint8_t is_disabled;               /**< TRUE if parameter is disabled */
} nmo_ckparameterin_state_t;

/* =============================================================================
 * CKParameterOut STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKParameterOut state
 * 
 * Output parameters own data and can have multiple destinations.
 * Inherits from CKParameter.
 * 
 * Reference: reference/src/CKParameterOut.cpp:120-160
 */
typedef struct nmo_ckparameterout_state {
    /* Inherits CKParameter data (stored separately) */
    
    /* Destination parameters */
    nmo_object_id_t *destination_ids;  /**< Array of destination parameter IDs */
    uint32_t destination_count;        /**< Number of destinations */
} nmo_ckparameterout_state_t;

/* =============================================================================
 * CKParameterLocal STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKParameterLocal state
 * 
 * Local parameters are behavior-local storage.
 * Can be "myself" parameters that reference the owner object.
 * Inherits from CKParameter.
 * 
 * Reference: reference/src/CKParameterLocal.cpp:100-140
 */
typedef struct nmo_ckparameterlocal_state {
    /* Inherits CKParameter data (stored separately) */
    
    uint8_t is_myself;                 /**< TRUE if "myself" parameter */
    uint8_t is_setting;                /**< TRUE if behavior setting */
} nmo_ckparameterlocal_state_t;

/* =============================================================================
 * CKParameterOperation STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKParameterOperation state
 * 
 * Parameter operations perform computations on input parameters.
 * Operations are identified by GUID and have 2 inputs and 1 output.
 * 
 * Reference: reference/src/CKParameterOperation.cpp:155-300
 */
typedef struct nmo_ckparameteroperation_state {
    nmo_guid_t operation_guid;         /**< Operation GUID identifier */
    nmo_object_id_t input1_id;         /**< First input parameter ID */
    nmo_object_id_t input2_id;         /**< Second input parameter ID */
    nmo_object_id_t output_id;         /**< Output parameter ID */
    nmo_object_id_t owner_id;          /**< Owner behavior ID */
} nmo_ckparameteroperation_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES - Derived Classes
 * ============================================================================= */

typedef nmo_result_t (*nmo_ckparameterin_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckparameterin_state_t *out_state);

typedef nmo_result_t (*nmo_ckparameterin_serialize_fn)(
    const nmo_ckparameterin_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

typedef nmo_result_t (*nmo_ckparameterout_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckparameterout_state_t *out_state);

typedef nmo_result_t (*nmo_ckparameterout_serialize_fn)(
    const nmo_ckparameterout_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

typedef nmo_result_t (*nmo_ckparameterlocal_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckparameterlocal_state_t *out_state);

typedef nmo_result_t (*nmo_ckparameterlocal_serialize_fn)(
    const nmo_ckparameterlocal_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

typedef nmo_result_t (*nmo_ckparameteroperation_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckparameteroperation_state_t *out_state);

typedef nmo_result_t (*nmo_ckparameteroperation_serialize_fn)(
    const nmo_ckparameteroperation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - Derived Class Accessors
 * ============================================================================= */

NMO_API nmo_ckparameterin_deserialize_fn nmo_get_ckparameterin_deserialize(void);
NMO_API nmo_ckparameterin_serialize_fn nmo_get_ckparameterin_serialize(void);

NMO_API nmo_ckparameterout_deserialize_fn nmo_get_ckparameterout_deserialize(void);
NMO_API nmo_ckparameterout_serialize_fn nmo_get_ckparameterout_serialize(void);

NMO_API nmo_ckparameterlocal_deserialize_fn nmo_get_ckparameterlocal_deserialize(void);
NMO_API nmo_ckparameterlocal_serialize_fn nmo_get_ckparameterlocal_serialize(void);

NMO_API nmo_ckparameteroperation_deserialize_fn nmo_get_ckparameteroperation_deserialize(void);
NMO_API nmo_ckparameteroperation_serialize_fn nmo_get_ckparameteroperation_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETER_SCHEMAS_H */
