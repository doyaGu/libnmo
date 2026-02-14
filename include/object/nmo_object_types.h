/**
 * @file nmo_object_types.h
 * @brief Virtools object type definitions integrated with type system
 * 
 * This module defines CKObject-derived types as proper type system entries,
 * replacing the legacy schema system. All Virtools object classes (CKObject,
 * CK3dEntity, CKBehavior, etc.) are registered in nmo_type_registry_t with:
 * - Proper GUID identification
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
#include "core/nmo_math.h"
#include "core/nmo_color.h"
#include "core/nmo_error.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_statesave_ids.h"
#include "object/nmo_object_guids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_arena nmo_arena_t;

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
 * Must be called after nmo_type_registry_create(),
 * nmo_register_builtin_types(), and before loading files.
 * 
 * @param registry Type registry to register into
 * @return nmo_ok() on success, error on failure
 * 
 * @note This replaces nmo_register_builtin_schemas() from legacy system
 */
NMO_API nmo_status_t nmo_register_object_types(nmo_type_registry_t *registry);

/**
 * @brief Register struct/union types used by object schemas
 *
 * Typically called by nmo_register_object_types().
 */
NMO_API nmo_status_t nmo_register_object_structs(nmo_type_registry_t *registry);

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
NMO_API nmo_status_t nmo_register_base_object_types(nmo_type_registry_t *registry);

/**
 * @brief Register 2D entity types
 * 
 * Registers: CK2dEntity, CKSprite, CKSpriteText
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_status_t nmo_register_2d_entity_types(nmo_type_registry_t *registry);

/**
 * @brief Register 3D entity types
 * 
 * Registers: CK3dEntity, CK3dObject, CKCamera, CKLight, CKCharacter
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_status_t nmo_register_3d_entity_types(nmo_type_registry_t *registry);

/**
 * @brief Register resource types
 * 
 * Registers: CKMaterial, CKTexture, CKMesh
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_status_t nmo_register_resource_types(nmo_type_registry_t *registry);

/**
 * @brief Register behavior types
 * 
 * Registers: CKBehavior, CKBehaviorIO, CKBehaviorLink, CKParameter
 * Requires base types to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_status_t nmo_register_behavior_types(nmo_type_registry_t *registry);

/**
 * @brief Register parameter types
 * 
 * Registers: CKParameterIn, CKParameterOut, CKParameterOperation
 * Requires base types and CKParameter to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_status_t nmo_register_parameter_types(nmo_type_registry_t *registry);

/**
 * @brief Register extended 3D entity types
 * 
 * Registers: CKTargetCamera, CKTargetLight, CKSprite3D, CKCurve
 * Requires base types and CK3dEntity to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_status_t nmo_register_extended_3d_types(nmo_type_registry_t *registry);

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
NMO_API nmo_status_t nmo_register_utility_types(nmo_type_registry_t *registry);

/**
 * @brief Register mesh variant types
 * 
 * Registers: CKGrid, CKLayer, CKPatchMesh
 * Requires base types and CKMesh to be registered first.
 * 
 * @param registry Type registry
 * @return nmo_ok() on success, error on failure
 */
NMO_API nmo_status_t nmo_register_mesh_types(nmo_type_registry_t *registry);

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
    nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Check if a type is a CKObject-derived type
 * 
 * @param registry Type registry
 * @param type_guid Type GUID to check
 * @return 1 if derived from CKObject, 0 otherwise
 */
NMO_API int nmo_is_object_type(
    nmo_type_registry_t *registry,
    nmo_guid_t type_guid);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_TYPES_H */
