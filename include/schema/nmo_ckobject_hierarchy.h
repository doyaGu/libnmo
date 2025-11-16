/**
 * @file nmo_ckobject_hierarchy.h
 * @brief CKObject class hierarchy schema registration
 */

#ifndef NMO_CKOBJECT_HIERARCHY_H
#define NMO_CKOBJECT_HIERARCHY_H

#include "nmo_schema_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all CKObject class hierarchy schemas
 *
 * Registers schema definitions for the complete Virtools CKObject class
 * hierarchy. Classes are registered in dependency order (parent before child).
 *
 * Includes:
 * - Base classes: CKObject, CKSceneObject, CKBeObject
 * - Render hierarchy: CKRenderObject, CK2dEntity, CK3dEntity, etc.
 * - Behavior hierarchy: CKBehavior, CKScriptBehavior
 * - Resource types: CKMesh, CKTexture, CKSound, etc.
 * - Utility types: CKParameter, CKGroup, CKLayer, etc.
 *
 * Note: 3D rendering-related classes (CK3dEntity subtree, CKMesh, etc.)
 *       are currently implemented as stubs pending further reverse engineering.
 *
 * @param registry Schema registry to add types to
 * @param arena Arena allocator for schema structures
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckobject_hierarchy(nmo_schema_registry_t *registry, nmo_arena_t *arena);

/**
 * @brief Get class name by class ID
 * @param class_id Virtools class ID (CKCID_* from CKDefines.h)
 * @return Class name, or NULL if not found
 */
const char *nmo_ckclass_get_name_by_id(uint32_t class_id);

/**
 * @brief Get class ID by class name
 * @param class_name Class name (e.g., "CKObject")
 * @return Class ID, or 0 if not found
 */
uint32_t nmo_ckclass_get_id_by_name(const char *class_name);

/**
 * @brief Get parent class name by child class name
 * @param class_name Child class name
 * @return Parent class name, or NULL if root or not found
 */
const char *nmo_ckclass_get_parent(const char *class_name);

/**
 * @brief Check if class uses CKBeObject deserializer
 * @param class_id Virtools class ID
 * @return 1 if uses CKBeObject, 0 if uses CKObject, -1 if not found
 */
int nmo_ckclass_uses_beobject(uint32_t class_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKOBJECT_HIERARCHY_H */
