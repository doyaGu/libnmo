/**
 * @file nmo_beobject_schemas.h
 * @brief Public API for CKBeObject schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKBeObject.
 * CKBeObject is the base class for behavioral objects (objects with scripts/attributes).
 * 
 * CKBeObject adds scripts, priority, attributes, and single-activity flags on top of CKObject.
 * Many derived classes (CKRenderObject, CKMesh, CKTexture, etc.) do not override
 * Load/Save and inherit this serialization behavior directly.
 */

#ifndef NMO_CKBEOBJECT_SCHEMAS_H
#define NMO_CKBEOBJECT_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_sceneobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/** One modern CKBeObject attribute lane, kept atomic during read/remap/write. */
typedef struct nmo_beobject_attribute {
    nmo_ref_t parameter;
    uint32_t type_id;
    nmo_chunk_t *chunk;
} nmo_beobject_attribute_t;

/** One legacy CKBeObject attribute lane, kept atomic during read/remap/write. */
typedef struct nmo_beobject_legacy_attribute {
    int32_t compatible_class_id;
    char *name;
    char *category;
    nmo_guid_t parameter_guid;
    nmo_ref_t parameter;
} nmo_beobject_legacy_attribute_t;

/* =============================================================================
 * CKBeObject STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKBeObject state
 * 
 * Represents the behavioral object data including scripts, priority, and attributes.
 * Corresponds to data serialized in CKBeObject::Load/Save.
 * 
 * Reference: reference/src/CKBeObject.cpp:400-700
 */
typedef struct nmo_beobject_state {
    /* Base class state */
    nmo_sceneobject_state_t base;  /**< CKSceneObject base state */
    
    /* Scripts */
    nmo_array_t scripts;           /**< Script behavior references (nmo_ref_t) */
    uint8_t has_scripts_section;   /**< Preserve an empty scripts section */
    uint8_t scripts_use_legacy_identifier; /**< CK_STATESAVE_BEHAVIORS */
    
    /* Priority */
    int32_t priority;              /**< Execution priority (0 = default) */
    uint8_t has_data_section;      /**< Preserve CK_STATESAVE_DATAS */
    uint8_t data_is_legacy;        /**< DATAS uses the pre-v5 five-DWORD layout */
    uint32_t data_flags;           /**< DATAS first DWORD, preserved verbatim */
    uint32_t legacy_data_words[3]; /**< Uninterpreted pre-v5 DATAS words */
    uint8_t has_runtime_data_section; /**< Preserve non-file DATAS */
    int32_t runtime_data_value;     /**< Uninterpreted non-file DATAS value */
    
    /* Attributes */
    nmo_array_t attributes;                    /**< Modern attributes (nmo_beobject_attribute_t) */
    uint8_t has_attributes_section;            /**< Preserve an empty modern section */

    /* Single activity flags (file save only) */
    uint8_t has_single_activity;               /**< True if single activity flags exist */
    uint32_t single_activity_flags;            /**< Scene object activity flags */

    /* Legacy attributes (CK_STATESAVE_ATTRIBUTES, old format) */
    nmo_array_t legacy_attributes;             /**< nmo_beobject_legacy_attribute_t */
    uint8_t has_legacy_attributes;             /**< Preserve an empty legacy section */
    uint8_t legacy_attr_old_version;           /**< Very old format has no compatible class ID */
} nmo_beobject_state_t;

/* =============================================================================
 * SERIALIZATION API (Type System)
 * ============================================================================= */

NMO_API nmo_status_t nmo_beobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_beobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_beobject_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_beobject_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_beobject_attribute_array_append(
    nmo_array_t *attributes,
    nmo_object_id_t parameter_id,
    uint32_t type_id,
    nmo_chunk_t *chunk);

NMO_API nmo_status_t nmo_beobject_script_array_append(
    nmo_array_t *scripts,
    nmo_object_id_t script_id);

NMO_API int nmo_beobject_script_array_find(
    const nmo_array_t *scripts,
    nmo_object_id_t script_id,
    size_t *out_index);

NMO_API nmo_object_id_t nmo_beobject_script_array_get_id(
    const nmo_array_t *scripts,
    size_t index);

NMO_API nmo_status_t nmo_beobject_clone_attributes(
    nmo_arena_t *arena,
    nmo_array_t *destination,
    const nmo_array_t *source);

NMO_API nmo_status_t nmo_beobject_clone_legacy_attributes(
    nmo_arena_t *arena,
    nmo_array_t *destination,
    const nmo_array_t *source);

NMO_DECLARE_OBJECT_SCHEMA(nmo_beobject_vtable, nmo_register_beobject_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEOBJECT_SCHEMAS_H */
