/**
 * @file nmo_object_types.h
 * @brief Virtools object type definitions integrated with type system
 * 
 * This module defines CKObject-derived types as proper type system entries,
 * replacing the legacy schema system. All Virtools object classes (CKObject,
 * CK3dEntity, CKBehavior, etc.) are registered in nmo_type_registry_t with:
 * - Proper GUID identification
 * - Class ID mapping for backward compatibility
 * - Inheritance relationships via base_type
 * - Serialization/deserialization vtable functions (CKObject implemented)
 * 
 * Architecture:
 * - Each CKObject-derived class is a type descriptor
 * - Type registry manages all relationships and lookups
 * - Vtable functions delegate to existing chunk-based serialization
 * - No separate schema registry needed
 * 
 * @see type/type_system.h for core type system APIs
 * @see object/legacy/ for reference implementations
 */

#ifndef NMO_OBJECT_TYPES_H
#define NMO_OBJECT_TYPES_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_type_registry_t nmo_type_registry_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;
typedef struct nmo_arena nmo_arena_t;

/* ============================================================================
 * Virtools Object Type GUIDs
 * 
 * GUID allocation strategy:
 * - Base DWORD1: 0x564B4F42 (ASCII "VKOB" = Virtools CKOBject)
 * - DWORD2: Class ID from nmo_class_ids.h
 * 
 * This ensures unique GUIDs while maintaining class ID mapping.
 * ============================================================================ */

/* Base object types */
#define NMO_GUID_CKOBJECT              ((nmo_guid_t){0x564B4F42, 0x00000001})
#define NMO_GUID_CKSCENEOBJECT         ((nmo_guid_t){0x564B4F42, 0x0000000B})
#define NMO_GUID_CKBEOBJECT            ((nmo_guid_t){0x564B4F42, 0x00000013})
#define NMO_GUID_CKRENDEROBJECT        ((nmo_guid_t){0x564B4F42, 0x0000002F})

/* 2D entities */
#define NMO_GUID_CK2DENTITY            ((nmo_guid_t){0x564B4F42, 0x0000001B})
#define NMO_GUID_CKSPRITE              ((nmo_guid_t){0x564B4F42, 0x0000001C})
#define NMO_GUID_CKSPRITETEXT          ((nmo_guid_t){0x564B4F42, 0x0000001D})

/* 3D entities */
#define NMO_GUID_CK3DENTITY            ((nmo_guid_t){0x564B4F42, 0x00000021})
#define NMO_GUID_CK3DOBJECT            ((nmo_guid_t){0x564B4F42, 0x00000029})
#define NMO_GUID_CKCAMERA              ((nmo_guid_t){0x564B4F42, 0x00000022})
#define NMO_GUID_CKLIGHT               ((nmo_guid_t){0x564B4F42, 0x00000026})
#define NMO_GUID_CKCHARACTER           ((nmo_guid_t){0x564B4F42, 0x00000028})

/* Resources */
#define NMO_GUID_CKMATERIAL            ((nmo_guid_t){0x564B4F42, 0x0000001E})
#define NMO_GUID_CKTEXTURE             ((nmo_guid_t){0x564B4F42, 0x0000001F})
#define NMO_GUID_CKMESH                ((nmo_guid_t){0x564B4F42, 0x00000020})

/* Behaviors and logic */
#define NMO_GUID_CKBEHAVIOR            ((nmo_guid_t){0x564B4F42, 0x00000008})
#define NMO_GUID_CKBEHAVIORIO          ((nmo_guid_t){0x564B4F42, 0x00000009})
#define NMO_GUID_CKBEHAVIORLINK        ((nmo_guid_t){0x564B4F42, 0x00000006})
#define NMO_GUID_CKPARAMETER           ((nmo_guid_t){0x564B4F42, 0x0000002E})
#define NMO_GUID_CKPARAMETERLOCAL      ((nmo_guid_t){0x564B4F42, 0x0000002D})

/* Scene management */
#define NMO_GUID_CKSCENE               ((nmo_guid_t){0x564B4F42, 0x0000000A})
#define NMO_GUID_CKLEVEL               ((nmo_guid_t){0x564B4F42, 0x00000015})
#define NMO_GUID_CKGROUP               ((nmo_guid_t){0x564B4F42, 0x00000017})

/* Data structures */
#define NMO_GUID_CKDATAARRAY           ((nmo_guid_t){0x564B4F42, 0x00000034})

/* Animation */
#define NMO_GUID_CKANIMATION           ((nmo_guid_t){0x564B4F42, 0x00000010})
#define NMO_GUID_CKKEYEDANIMATION      ((nmo_guid_t){0x564B4F42, 0x00000012})

/* ============================================================================
 * Object State Structures
 * 
 * Each CKObject-derived class has a corresponding state structure that holds
 * its serializable data. These are used by vtable functions.
 * ============================================================================ */

/**
 * @brief CKObject state (base class)
 */
typedef struct nmo_ckobject_state {
    uint32_t visibility_flags;              /**< Visibility flags */
} nmo_ckobject_state_t;

/* Visibility flag constants */
#define NMO_CKOBJECT_VISIBLE          0x01  /**< Object is visible */
#define NMO_CKOBJECT_HIERARCHICAL     0x02  /**< Object has hierarchical hide */

/**
 * @brief CK3dEntity state
 */
typedef struct nmo_ck3dentity_state {
    nmo_ckobject_state_t base;              /**< Base object state */
    uint32_t flags;                         /**< Entity flags */
    float world_matrix[16];                 /**< 4x4 world transformation matrix */
    uint32_t zorder;                        /**< Z-order for rendering */
    /* Add more fields as reverse-engineered */
} nmo_ck3dentity_state_t;

/**
 * @brief CKMesh state
 */
typedef struct nmo_ckmesh_state {
    nmo_ckobject_state_t base;              /**< Base object state */
    uint32_t vertex_count;                  /**< Number of vertices */
    uint32_t face_count;                    /**< Number of faces */
    /* Actual vertex/face data managed separately */
} nmo_ckmesh_state_t;

/**
 * @brief CKMaterial state
 */
typedef struct nmo_ckmaterial_state {
    nmo_ckobject_state_t base;              /**< Base object state */
    float ambient[4];                       /**< Ambient RGBA */
    float diffuse[4];                       /**< Diffuse RGBA */
    float specular[4];                      /**< Specular RGBA */
    float emissive[4];                      /**< Emissive RGBA */
    float power;                            /**< Specular power */
    uint32_t texture_id;                    /**< Reference to CKTexture */
} nmo_ckmaterial_state_t;

/* ============================================================================
 * Type Registration Functions
 * ============================================================================ */

/**
 * @brief Register all Virtools object types
 * 
 * Registers the complete CKObject class hierarchy as type descriptors in the
 * type registry. This includes:
 * - Base classes (CKObject, CKSceneObject, CKBeObject, CKRenderObject)
 * - 2D entities (CK2dEntity, CKSprite, CKSpriteText)
 * - 3D entities (CK3dEntity, CK3dObject, CKCamera, CKLight)
 * - Resources (CKMaterial, CKTexture, CKMesh)
 * - Behaviors (CKBehavior, CKBehaviorIO, CKBehaviorLink)
 * - Scene management (CKScene, CKLevel, CKGroup)
 * - Data structures (CKDataArray)
 * 
 * Must be called after nmo_type_registry_create() and before loading files.
 * 
 * @param registry Type registry to register into
 * @return nmo_ok() on success, error on failure
 * 
 * @note This replaces nmo_register_builtin_schemas() from legacy system
 */
NMO_API nmo_result_t nmo_register_object_types(nmo_type_registry_t *registry);

/**
 * @brief Register base object types only
 * 
 * Registers only the foundational classes:
 * - CKObject
 * - CKSceneObject
 * - CKBeObject
 * - CKRenderObject
 * 
 * Useful for testing or minimal configurations.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_base_object_types(nmo_type_registry_t *registry);

/**
 * @brief Register 2D entity types
 * 
 * Registers: CK2dEntity, CKSprite, CKSpriteText
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_2d_entity_types(nmo_type_registry_t *registry);

/**
 * @brief Register 3D entity types
 * 
 * Registers: CK3dEntity, CK3dObject, CKCamera, CKLight, CKCharacter
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_3d_entity_types(nmo_type_registry_t *registry);

/**
 * @brief Register resource types
 * 
 * Registers: CKMaterial, CKTexture, CKMesh
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_resource_types(nmo_type_registry_t *registry);

/**
 * @brief Register behavior types
 * 
 * Registers: CKBehavior, CKBehaviorIO, CKBehaviorLink, CKParameter
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_behavior_types(nmo_type_registry_t *registry);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Get object type by class ID
 * 
 * Convenience function to look up a type using the legacy class ID system.
 * 
 * @param registry Type registry
 * @param class_id Virtools class ID (e.g., NMO_CID_MESH = 32)
 * @return Type descriptor, or NULL if not found
 */
NMO_API const nmo_type_descriptor_t* nmo_get_object_type_by_class_id(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Check if a type is a CKObject-derived type
 * 
 * @param registry Type registry
 * @param type_guid Type GUID to check
 * @return 1 if derived from CKObject, 0 otherwise
 */
NMO_API int nmo_is_object_type(
    const nmo_type_registry_t *registry,
    nmo_guid_t type_guid);

/**
 * @brief Get the object class ID from a type GUID
 * 
 * Extracts the class ID from a Virtools object type GUID.
 * 
 * @param type_guid Type GUID
 * @return Class ID, or 0 if not a Virtools object type
 */
NMO_API nmo_class_id_t nmo_object_guid_to_class_id(nmo_guid_t type_guid);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_TYPES_H */
