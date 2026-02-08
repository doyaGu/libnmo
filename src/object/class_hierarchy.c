/**
 * @file class_hierarchy.c
 * @brief Implementation of dynamic class hierarchy queries
 * 
 * This module provides class hierarchy queries via the unified type system.
 * Legacy registry-free queries have been removed.
 */

#include "object/nmo_class_hierarchy.h"
#include "object/nmo_object_types.h"

#include "object/nmo_class_ids.h"
#include "type/nmo_type_system.h"
#include <stddef.h>

/* =============================================================================
 * CLASS HIERARCHY QUERIES
 * ============================================================================= */

int nmo_class_is_derived_from(
    const nmo_type_registry_t *registry,
    nmo_class_id_t child_id,
    nmo_class_id_t parent_id)
{
    if (!registry) {
        return 0;
    }

    /* Special case: class is its own "child" */
    if (child_id == parent_id) {
        return 1;
    }

    /* Invalid IDs */
    if (child_id == 0 || parent_id == 0) {
        return 0;
    }

    /* Look up type descriptors by class ID */
    const nmo_type_descriptor_t *child_type =
        nmo_type_registry_find_by_class_id(registry, child_id);
    const nmo_type_descriptor_t *parent_type =
        nmo_type_registry_find_by_class_id(registry, parent_id);

    if (!child_type || !parent_type) {
        return 0;
    }

    /* Use type system inheritance check */
    return nmo_type_is_derived_from(registry, child_type->id, parent_type->id);
}

nmo_class_id_t nmo_class_get_parent(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (!registry || class_id == 0) {
        return 0;
    }

    /* Look up type descriptor */
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id(registry, class_id);
    if (!type) {
        return 0;
    }

    /* Get base type GUID and convert to class ID */
    if (nmo_guid_is_null(type->base_type)) {
        return 0;  /* No parent (root class) */
    }

    const nmo_type_descriptor_t *base_type =
        nmo_type_registry_find_by_guid(registry, type->base_type);
    if (!base_type) {
        return 0;
    }

    return base_type->class_id;
}

int nmo_class_get_ancestors(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_class_id_t *ancestors,
    size_t max_count)
{
    if (!registry || !ancestors || max_count == 0) {
        return -1;
    }

    size_t count = 0;
    nmo_class_id_t current_id = class_id;

    while (count < max_count) {
        nmo_class_id_t parent_id = nmo_class_get_parent(registry, current_id);
        if (parent_id == 0) {
            break;
        }

        ancestors[count++] = parent_id;
        current_id = parent_id;
    }

    return (int)count;
}

nmo_class_id_t nmo_class_get_common_ancestor(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id1,
    nmo_class_id_t class_id2)
{
    if (!registry || class_id1 == 0 || class_id2 == 0) {
        return 0;
    }

    /* Quick check: if one is ancestor of the other */
    if (nmo_class_is_derived_from(registry, class_id1, class_id2)) {
        return class_id2;
    }
    if (nmo_class_is_derived_from(registry, class_id2, class_id1)) {
        return class_id1;
    }

    /* Get all ancestors of class1 */
    nmo_class_id_t ancestors1[32];
    int count1 = nmo_class_get_ancestors(registry, class_id1, ancestors1, 32);
    if (count1 < 0) {
        return 0;
    }

    /* Check each ancestor of class2 against ancestors of class1 */
    nmo_class_id_t current = class_id2;
    while (current != 0) {
        for (int i = 0; i < count1; i++) {
            if (ancestors1[i] == current) {
                return current;
            }
        }

        current = nmo_class_get_parent(registry, current);
    }

    return 0;
}

int nmo_class_get_derivation_level(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (!registry || class_id == 0) {
        return -1;
    }

    /* CKObject is level 0 */
    if (class_id == NMO_CID_OBJECT) {
        return 0;
    }

    int level = 0;
    nmo_class_id_t current = class_id;

    while (current != 0) {
        nmo_class_id_t parent = nmo_class_get_parent(registry, current);
        if (parent == 0) {
            break;
        }
        level++;
        current = parent;
    }

    return level;
}

/* =============================================================================
 * SPECIAL CLASS QUERIES
 * ============================================================================= */

int nmo_class_uses_beobject_deserializer(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (!registry) {
        return 0;
    }

    /* Check if class derives from CKBeObject */
    return nmo_class_is_derived_from(registry, class_id, NMO_CID_BEOBJECT);
}

int nmo_class_is_render_object(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    return nmo_class_is_derived_from(registry, class_id, NMO_CID_RENDEROBJECT);
}

int nmo_class_is_3d_entity(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    return nmo_class_is_derived_from(registry, class_id, NMO_CID_3DENTITY);
}

int nmo_class_is_2d_entity(
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    return nmo_class_is_derived_from(registry, class_id, NMO_CID_2DENTITY);
}
