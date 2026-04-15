/**
 * @file nmo_parameter_schemas.h
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
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_enum_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_type_registry nmo_type_registry_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

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
typedef CK_PARAMETER_MODE nmo_parameter_mode_t;

/**
 * @brief CKParameter state
 * 
 * Represents a parameter with typed data.
 * The actual data is stored in one of several formats based on mode.
 * 
 * Reference: reference/src/CKParameter.cpp:245-450
 */
typedef struct nmo_parameter_state {
    /* Base CKObject state */
    nmo_object_state_t base;

    /* Parameter type identification */
    nmo_guid_t type_guid;              /**< Parameter type GUID */
    
    /* Storage mode and data */
    nmo_parameter_mode_t mode;       /**< How data is stored */

    /* Whether parameter state was present in chunk */
    bool has_state;
    
    /* Buffer mode (MODE_BUFFER) */
    nmo_array_t buffer_data;           /**< Parameter data buffer (uint8_t) */
    
    /* Object mode (MODE_OBJECT) */
    nmo_object_id_t object_id;         /**< Referenced object ID */
    
    /* Manager mode (MODE_MANAGER) */
    nmo_guid_t manager_guid;           /**< Manager GUID */
    uint32_t manager_value;            /**< Manager-specific value */
    
    /* Sub-chunk mode (MODE_SUBCHUNK) */
    nmo_chunk_t *subchunk;             /**< Sub-chunk payload */
} nmo_parameter_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_parameter_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameter_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameter_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_parameter_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_parameter_vtable, nmo_register_parameter_type)

/* =============================================================================
 * PARAMETER STATE HELPERS
 * ============================================================================= */

/**
 * @brief Get mutable parameter state from any parameter object class.
 *
 * Handles CKParameter, CKParameterOut, and CKParameterLocal class hierarchy.
 * Returns NULL if object is not a parameter type or has no state.
 */
NMO_API nmo_parameter_state_t *nmo_parameter_get_mutable_state(nmo_object_t *obj);

/**
 * @brief Get const parameter state from any parameter object class.
 */
NMO_API const nmo_parameter_state_t *nmo_parameter_get_state(const nmo_object_t *obj);

/**
 * @brief Get a parameter's buffer value as a string.
 *
 * @param obj       Parameter object (CKParameter, CKParameterOut, CKParameterLocal)
 * @param registry  Type registry for type resolution
 * @param out_buf   Output buffer for string representation
 * @param buf_size  Size of output buffer
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_parameter_get_value(
    const nmo_object_t *obj,
    const nmo_type_registry_t *registry,
    char *out_buf,
    size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPARAMETER_SCHEMAS_H */
