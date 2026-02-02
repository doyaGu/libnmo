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
#include "object/nmo_class_ids.h"

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

/** Base DWORD1 for all CKObject-derived types ("VKOB") */
#define NMO_CKOBJECT_GUID_DWORD1 0x564B4F42u

/** Helper macro to build object GUIDs from class IDs */
#define NMO_OBJECT_GUID(_class_id) ((nmo_guid_t){NMO_CKOBJECT_GUID_DWORD1, (uint32_t)(_class_id)})

/* Base object types */
#define NMO_GUID_CKOBJECT              NMO_OBJECT_GUID(NMO_CID_OBJECT)
#define NMO_GUID_CKSCENEOBJECT         NMO_OBJECT_GUID(NMO_CID_SCENEOBJECT)
#define NMO_GUID_CKBEOBJECT            NMO_OBJECT_GUID(NMO_CID_BEOBJECT)
#define NMO_GUID_CKRENDEROBJECT        NMO_OBJECT_GUID(NMO_CID_RENDEROBJECT)

/* 2D entities */
#define NMO_GUID_CK2DENTITY            NMO_OBJECT_GUID(NMO_CID_2DENTITY)
#define NMO_GUID_CKSPRITE              NMO_OBJECT_GUID(NMO_CID_SPRITE)
#define NMO_GUID_CKSPRITETEXT          NMO_OBJECT_GUID(NMO_CID_SPRITETEXT)

/* 3D entities */
#define NMO_GUID_CK3DENTITY            NMO_OBJECT_GUID(NMO_CID_3DENTITY)
#define NMO_GUID_CK3DOBJECT            NMO_OBJECT_GUID(NMO_CID_3DOBJECT)
#define NMO_GUID_CKCAMERA              NMO_OBJECT_GUID(NMO_CID_CAMERA)
#define NMO_GUID_CKLIGHT               NMO_OBJECT_GUID(NMO_CID_LIGHT)
#define NMO_GUID_CKCHARACTER           NMO_OBJECT_GUID(NMO_CID_CHARACTER)

/* Resources */
#define NMO_GUID_CKMATERIAL            NMO_OBJECT_GUID(NMO_CID_MATERIAL)
#define NMO_GUID_CKTEXTURE             NMO_OBJECT_GUID(NMO_CID_TEXTURE)
#define NMO_GUID_CKMESH                NMO_OBJECT_GUID(NMO_CID_MESH)

/* Behaviors and logic */
#define NMO_GUID_CKBEHAVIOR            NMO_OBJECT_GUID(NMO_CID_BEHAVIOR)
#define NMO_GUID_CKBEHAVIORIO          NMO_OBJECT_GUID(NMO_CID_BEHAVIORIO)
#define NMO_GUID_CKBEHAVIORLINK        NMO_OBJECT_GUID(NMO_CID_BEHAVIORLINK)
#define NMO_GUID_CKPARAMETER           NMO_OBJECT_GUID(NMO_CID_PARAMETER)
#define NMO_GUID_CKPARAMETERLOCAL      NMO_OBJECT_GUID(NMO_CID_PARAMETERLOCAL)
#define NMO_GUID_CKSTATE               NMO_OBJECT_GUID(NMO_CID_STATE)
#define NMO_GUID_CKCRITICALSECTION     NMO_OBJECT_GUID(NMO_CID_CRITICALSECTION)

/* Scene management */
#define NMO_GUID_CKSCENE               NMO_OBJECT_GUID(NMO_CID_SCENE)
#define NMO_GUID_CKLEVEL               NMO_OBJECT_GUID(NMO_CID_LEVEL)
#define NMO_GUID_CKGROUP               NMO_OBJECT_GUID(NMO_CID_GROUP)

/* Data structures */
#define NMO_GUID_CKDATAARRAY           NMO_OBJECT_GUID(NMO_CID_DATAARRAY)

/* Animation */
#define NMO_GUID_CKANIMATION           NMO_OBJECT_GUID(NMO_CID_ANIMATION)
#define NMO_GUID_CKKEYEDANIMATION      NMO_OBJECT_GUID(NMO_CID_KEYEDANIMATION)
#define NMO_GUID_CKOBJECTANIMATION     NMO_OBJECT_GUID(NMO_CID_OBJECTANIMATION)

/* Parameters (extended) */
#define NMO_GUID_CKPARAMETERIN         NMO_OBJECT_GUID(NMO_CID_PARAMETERIN)
#define NMO_GUID_CKPARAMETEROUT        NMO_OBJECT_GUID(NMO_CID_PARAMETEROUT)
#define NMO_GUID_CKPARAMETEROPERATION  NMO_OBJECT_GUID(NMO_CID_PARAMETEROPERATION)

/* Extended 3D types */
#define NMO_GUID_CKTARGETCAMERA        NMO_OBJECT_GUID(NMO_CID_TARGETCAMERA)
#define NMO_GUID_CKTARGETLIGHT         NMO_OBJECT_GUID(NMO_CID_TARGETLIGHT)
#define NMO_GUID_CKSPRITE3D            NMO_OBJECT_GUID(NMO_CID_SPRITE3D)
#define NMO_GUID_CKCURVE               NMO_OBJECT_GUID(NMO_CID_CURVE)
#define NMO_GUID_CKCURVEPOINT          NMO_OBJECT_GUID(NMO_CID_CURVEPOINT)
#define NMO_GUID_CKBODYPART            NMO_OBJECT_GUID(NMO_CID_BODYPART)

/* Utility types */
#define NMO_GUID_CKRENDERCONTEXT       NMO_OBJECT_GUID(NMO_CID_RENDERCONTEXT)
#define NMO_GUID_CKKINEMATICCHAIN      NMO_OBJECT_GUID(NMO_CID_KINEMATICCHAIN)
#define NMO_GUID_CKSYNCHRO             NMO_OBJECT_GUID(NMO_CID_SYNCHRO)
#define NMO_GUID_CKPLACE               NMO_OBJECT_GUID(NMO_CID_PLACE)
#define NMO_GUID_CKSOUND               NMO_OBJECT_GUID(NMO_CID_SOUND)
#define NMO_GUID_CKWAVESOUND           NMO_OBJECT_GUID(NMO_CID_WAVESOUND)
#define NMO_GUID_CKMIDISOUND           NMO_OBJECT_GUID(NMO_CID_MIDISOUND)
#define NMO_GUID_CKINTERFACEOBJECTMANAGER NMO_OBJECT_GUID(NMO_CID_INTERFACEOBJECTMANAGER)

/* Mesh variants */
#define NMO_GUID_CKGRID                NMO_OBJECT_GUID(NMO_CID_GRID)
#define NMO_GUID_CKLAYER               NMO_OBJECT_GUID(NMO_CID_LAYER)
#define NMO_GUID_CKPATCHMESH           NMO_OBJECT_GUID(NMO_CID_PATCHMESH)

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

/**
 * @brief Register parameter types
 * 
 * Registers: CKParameterIn, CKParameterOut, CKParameterOperation
 * Requires base types and CKParameter to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_parameter_types(nmo_type_registry_t *registry);

/**
 * @brief Register extended 3D entity types
 * 
 * Registers: CKTargetCamera, CKTargetLight, CKSprite3D, CKCurve
 * Requires base types and CK3dEntity to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_extended_3d_types(nmo_type_registry_t *registry);

/**
 * @brief Register utility object types
 * 
 * Registers: CKRenderContext, CKKinematicChain, CKSynchro, CKPlace, CKSound,
 *            CKInterfaceObjectManager
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_utility_types(nmo_type_registry_t *registry);

/**
 * @brief Register mesh variant types
 * 
 * Registers: CKGrid, CKLayer, CKPatchMesh
 * Requires base types and CKMesh to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_result_t nmo_register_mesh_types(nmo_type_registry_t *registry);

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
