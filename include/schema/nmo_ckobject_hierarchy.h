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

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKOBJECT_HIERARCHY_H */
