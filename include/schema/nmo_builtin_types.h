/**
 * @file nmo_builtin_types.h
 * @brief Public header for built-in type schema registration
 */

#ifndef NMO_BUILTIN_TYPES_H
#define NMO_BUILTIN_TYPES_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;

/**
 * @brief Register all built-in type schemas
 *
 * Registers three categories of built-in types:
 * - Scalar types (u8-u64, i8-i64, f32, f64, bool, string)
 * - Mathematical types (Vector2/3/4, Quaternion, Matrix, Color, Box, Rect)
 * - Virtools-specific types (GUID, ObjectID, ClassID, ManagerID, FileVersion)
 *
 * @param registry Schema registry to populate
 * @param arena Arena for allocations (must remain valid for registry lifetime)
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_builtin_types(nmo_schema_registry_t *registry, nmo_arena_t *arena);

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
NMO_API nmo_result_t nmo_register_ckobject_hierarchy(nmo_schema_registry_t *registry, nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BUILTIN_TYPES_H */
