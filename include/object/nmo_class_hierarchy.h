/**
 * @file nmo_class_hierarchy.h
 * @brief Class hierarchy query API backed by the unified type system
 * 
 * This module provides runtime queries for class inheritance relationships,
 * replacing hardcoded class ID range checks with proper type system lookups.
 * 
 * Design Principle: Never hardcode class ID ranges or inheritance checks in
 * business logic. Always use these APIs to query the type system dynamically.
 */

#ifndef NMO_CLASS_HIERARCHY_H
#define NMO_CLASS_HIERARCHY_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_type_registry_t nmo_type_registry_t;

/* =============================================================================
 * CLASS HIERARCHY QUERIES
 * ============================================================================= */

/**
 * @brief Check if a class is derived from another class
 * 
 * This function checks the complete inheritance chain, not just direct parent.
 * 
 * @param registry Type registry
 * @param child_id Class ID to check
 * @param parent_id Potential ancestor class ID
 * @return 1 if child is derived from parent (or is parent), 0 otherwise
 */
NMO_API int nmo_class_is_derived_from(
    const nmo_type_registry_t *registry,
    nmo_class_id_t child_id,
    nmo_class_id_t parent_id);

/**
 * @brief Get the direct parent class ID
 * 
 * @param registry Type registry
 * @param class_id Class ID
 * @return Parent class ID, or 0 if root class or not found
 */
NMO_API nmo_class_id_t nmo_class_get_parent(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Get all ancestor class IDs in order (parent, grandparent, ...)
 * 
 * @param registry Type registry
 * @param class_id Class ID
 * @param ancestors Output array for ancestor IDs (caller allocated)
 * @param max_count Maximum number of ancestors to store
 * @return Number of ancestors filled, or -1 on error
 * 
 * The array is filled from nearest to root:
 * ancestors[0] = parent, ancestors[1] = grandparent, etc.
 */
NMO_API int nmo_class_get_ancestors(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_class_id_t *ancestors,
    size_t max_count);

/**
 * @brief Find the nearest common ancestor of two classes
 * 
 * @param registry Type registry
 * @param class_id1 First class ID
 * @param class_id2 Second class ID
 * @return Common ancestor class ID, or 0 if no common ancestor
 */
NMO_API nmo_class_id_t nmo_class_get_common_ancestor(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id1,
    nmo_class_id_t class_id2);

/**
 * @brief Get derivation level (depth in inheritance tree)
 * 
 * @param registry Type registry
 * @param class_id Class ID
 * @return Derivation level (0 for CKObject, 1 for direct children, etc.)
 *         or -1 if not found
 */
NMO_API int nmo_class_get_derivation_level(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/* =============================================================================
 * SPECIAL CLASS QUERIES
 * ============================================================================= */

/**
 * @brief Check if class uses CKBeObject deserialization
 * 
 * This determines which deserialization path to use:
 * - CKBeObject and descendants: Use CKBeObject::Load() with attributes, scripts, etc.
 * - Others: Use CKObject::Load() with basic data only
 * 
 * @param registry Type registry
 * @param class_id Class ID to check
 * @return 1 if uses CKBeObject deserializer, 0 otherwise
 */
NMO_API int nmo_class_uses_beobject_deserializer(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Check if class is a render object (supports rendering)
 * 
 * @param registry Type registry
 * @param class_id Class ID to check
 * @return 1 if derived from CKRenderObject, 0 otherwise
 */
NMO_API int nmo_class_is_render_object(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Check if class is a 3D entity
 * 
 * @param registry Type registry
 * @param class_id Class ID to check
 * @return 1 if derived from CK3dEntity, 0 otherwise
 */
NMO_API int nmo_class_is_3d_entity(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

/**
 * @brief Check if class is a 2D entity
 * 
 * @param registry Type registry
 * @param class_id Class ID to check
 * @return 1 if derived from CK2dEntity, 0 otherwise
 */
NMO_API int nmo_class_is_2d_entity(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CLASS_HIERARCHY_H */
