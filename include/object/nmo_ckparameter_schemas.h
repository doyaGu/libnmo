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
#include "nmo_ckobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

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
    /* Base CKObject state */
    nmo_ckobject_state_t base;

    /* Parameter type identification */
    nmo_guid_t type_guid;              /**< Parameter type GUID */
    
    /* Storage mode and data */
    nmo_ckparameter_mode_t mode;       /**< How data is stored */

    /* Whether parameter state was present in chunk */
    bool has_state;
    
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
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_ckparameter_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckparameter_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckparameter_vtable, nmo_register_ckparameter_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETER_SCHEMAS_H */
