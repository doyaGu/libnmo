/**
 * @file class_hierarchy.c
 * @brief Implementation of dynamic class hierarchy queries
 */

#include "schema/nmo_class_hierarchy.h"
#include "schema/nmo_ckobject_hierarchy.h"
#include "schema/nmo_schema_registry.h"
#include <string.h>

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get class name by ID (internal helper)
 */
static const char *get_class_name(nmo_class_id_t class_id)
{
    return nmo_ckclass_get_name_by_id(class_id);
}

/**
 * @brief Get parent name by class ID (internal helper)
 */
static const char *get_parent_name(nmo_class_id_t class_id)
{
    const char *class_name = get_class_name(class_id);
    if (!class_name) {
        return NULL;
    }
    return nmo_ckclass_get_parent(class_name);
}

/* =============================================================================
 * CLASS HIERARCHY QUERIES
 * ============================================================================= */

int nmo_class_is_derived_from(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t child_id,
    nmo_class_id_t parent_id)
{
    (void)registry; /* Not needed yet - using static table */
    
    /* Special case: class is its own "child" */
    if (child_id == parent_id) {
        return 1;
    }
    
    /* Invalid IDs */
    if (child_id == 0 || parent_id == 0) {
        return 0;
    }
    
    /* Walk up the inheritance chain */
    nmo_class_id_t current_id = child_id;
    while (current_id != 0) {
        /* Get parent */
        const char *class_name = get_class_name(current_id);
        if (!class_name) {
            return 0;
        }
        
        const char *parent_name = nmo_ckclass_get_parent(class_name);
        if (!parent_name) {
            /* Reached root */
            return 0;
        }
        
        nmo_class_id_t parent_class_id = nmo_ckclass_get_id_by_name(parent_name);
        if (parent_class_id == parent_id) {
            return 1;
        }
        
        current_id = parent_class_id;
    }
    
    return 0;
}

nmo_class_id_t nmo_class_get_parent(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id)
{
    (void)registry;
    
    if (class_id == 0) {
        return 0;
    }
    
    const char *class_name = get_class_name(class_id);
    if (!class_name) {
        return 0;
    }
    
    const char *parent_name = nmo_ckclass_get_parent(class_name);
    if (!parent_name) {
        return 0;
    }
    
    return nmo_ckclass_get_id_by_name(parent_name);
}

int nmo_class_get_ancestors(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_class_id_t *ancestors,
    size_t max_count)
{
    (void)registry;
    
    if (!ancestors || max_count == 0) {
        return -1;
    }
    
    size_t count = 0;
    nmo_class_id_t current_id = class_id;
    
    while (count < max_count) {
        nmo_class_id_t parent_id = nmo_class_get_parent(NULL, current_id);
        if (parent_id == 0) {
            break;
        }
        
        ancestors[count++] = parent_id;
        current_id = parent_id;
    }
    
    return (int)count;
}

nmo_class_id_t nmo_class_get_common_ancestor(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id1,
    nmo_class_id_t class_id2)
{
    (void)registry;
    
    if (class_id1 == 0 || class_id2 == 0) {
        return 0;
    }
    
    /* Quick check: if one is ancestor of the other */
    if (nmo_class_is_derived_from(NULL, class_id1, class_id2)) {
        return class_id2;
    }
    if (nmo_class_is_derived_from(NULL, class_id2, class_id1)) {
        return class_id1;
    }
    
    /* Get all ancestors of class1 */
    nmo_class_id_t ancestors1[32];
    int count1 = nmo_class_get_ancestors(NULL, class_id1, ancestors1, 32);
    if (count1 < 0) {
        return 0;
    }
    
    /* Check each ancestor of class2 against ancestors of class1 */
    nmo_class_id_t current = class_id2;
    while (current != 0) {
        /* Check if current is in ancestors1 */
        for (int i = 0; i < count1; i++) {
            if (ancestors1[i] == current) {
                return current;
            }
        }
        
        /* Move to parent */
        current = nmo_class_get_parent(NULL, current);
    }
    
    return 0;
}

int nmo_class_get_derivation_level(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id)
{
    (void)registry;
    
    if (class_id == 0) {
        return -1;
    }
    
    /* CKObject is level 0 */
    if (class_id == 1) {
        return 0;
    }
    
    int level = 0;
    nmo_class_id_t current = class_id;
    
    while (current != 0) {
        nmo_class_id_t parent = nmo_class_get_parent(NULL, current);
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
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id)
{
    (void)registry;
    
    /* Use the existing implementation from ckobject_hierarchy.c */
    return nmo_ckclass_uses_beobject(class_id);
}

int nmo_class_is_render_object(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id)
{
    /* CKRenderObject has ID 47 */
    return nmo_class_is_derived_from(registry, class_id, 47);
}

int nmo_class_is_3d_entity(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id)
{
    /* CK3dEntity has ID 33 */
    return nmo_class_is_derived_from(registry, class_id, 33);
}

int nmo_class_is_2d_entity(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id)
{
    /* CK2dEntity has ID 27 */
    return nmo_class_is_derived_from(registry, class_id, 27);
}
