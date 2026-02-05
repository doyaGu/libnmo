/**
 * @file nmo_ref_enumerate.h
 * @brief Extensible reference enumeration for all object types
 *
 * Phase 4.1: Provides a registry for type-specific reference extractors.
 * Types can register their own reference enumerator callbacks.
 * Default enumerators are provided for all built-in schema types.
 *
 * Design principles:
 * - Open/Closed: New types can register enumerators without modifying existing code
 * - DRY: Common patterns (arrays of IDs, optional refs) are handled by helpers
 * - SOLID: Single responsibility - enumerate refs, nothing else
 * 
 * ARCHITECTURE NOTE:
 * This module uses nmo_ref_kind_t from nmo_ref_graph.h (Session layer).
 * The Type layer provides generic enumeration via nmo_type_ref_visitor_fn
 * with opaque uint32_t ref_kind. This layer provides concrete semantics.
 */

#ifndef NMO_REF_ENUMERATE_H
#define NMO_REF_ENUMERATE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "object/nmo_class_ids.h"
#include "session/nmo_ref_graph.h"  /* For nmo_ref_kind_t (Session layer) */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_object nmo_object_t;

/**
 * @brief Reference visitor callback (Session layer)
 *
 * Called for each reference found by an enumerator.
 * Uses nmo_ref_kind_t from this layer for semantic meaning.
 *
 * @param user_data User-provided context
 * @param target_id Referenced object ID (non-zero)
 * @param kind Reference kind (Session layer enum)
 * @param field_name Field name containing the reference
 * @param index Array index (0 for non-array fields)
 * @return true to continue enumeration, false to stop
 */
typedef bool (*nmo_ref_visitor_fn)(
    void *user_data,
    uint32_t target_id,
    nmo_ref_kind_t kind,
    const char *field_name,
    uint32_t index
);

/**
 * @brief Reference enumerator callback
 *
 * Enumerates all references from a specific object.
 * Implementations should call the visitor for each non-null reference.
 *
 * @param obj Object to enumerate references from
 * @param state Object state pointer (from nmo_object_get_state)
 * @param visitor Callback for each reference
 * @param user_data User context passed to visitor
 * @return NMO_OK on success
 */
typedef nmo_status_t (*nmo_ref_enumerator_fn)(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Reference enumerator registry
 *
 * Maps class IDs to their reference enumerator functions.
 */
typedef struct nmo_ref_enumerator_registry nmo_ref_enumerator_registry_t;

/* ============================================================================
 * Registry API
 * ============================================================================ */

/**
 * @brief Create enumerator registry
 *
 * @param arena Arena for allocations
 * @return Registry or NULL on failure
 */
NMO_API nmo_ref_enumerator_registry_t *nmo_ref_enumerator_registry_create(
    nmo_arena_t *arena
);

/**
 * @brief Destroy enumerator registry
 *
 * @param registry Registry to destroy
 */
NMO_API void nmo_ref_enumerator_registry_destroy(
    nmo_ref_enumerator_registry_t *registry
);

/**
 * @brief Register enumerator for a class ID
 *
 * @param registry Registry instance
 * @param class_id Class ID to register for
 * @param enumerator Enumerator callback
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_ref_enumerator_register(
    nmo_ref_enumerator_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_ref_enumerator_fn enumerator
);

/**
 * @brief Lookup enumerator for a class ID
 *
 * Also searches base classes if no exact match found.
 *
 * @param registry Registry instance
 * @param class_id Class ID to lookup
 * @return Enumerator function or NULL if not found
 */
NMO_API nmo_ref_enumerator_fn nmo_ref_enumerator_lookup(
    nmo_ref_enumerator_registry_t *registry,
    nmo_class_id_t class_id
);

/**
 * @brief Register all built-in enumerators
 *
 * Registers enumerators for all known CK classes:
 * - CKBeObject (scripts, attributes)
 * - CK3dEntity (parent, meshes, animations, etc.)
 * - CKMesh (materials)
 * - CKMaterial (textures)
 * - CKCamera, CKLight
 * - CKBehavior (owner, parameters, sub-behaviors)
 * - CKGroup (members)
 * - And more...
 *
 * @param registry Registry instance
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_ref_enumerator_register_builtins(
    nmo_ref_enumerator_registry_t *registry
);

/* ============================================================================
 * Enumeration API
 * ============================================================================ */

/**
 * @brief Enumerate all references from an object
 *
 * Uses registered enumerator for the object's class.
 * Falls back to base class enumerators if no exact match.
 *
 * @param registry Enumerator registry
 * @param obj Object to enumerate
 * @param visitor Visitor callback
 * @param user_data User context
 * @return NMO_OK on success, NMO_ERR_NOT_FOUND if no enumerator registered
 */
NMO_API nmo_status_t nmo_ref_enumerate_object(
    nmo_ref_enumerator_registry_t *registry,
    nmo_object_t *obj,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/* ============================================================================
 * Helper Macros for Writing Enumerators (DRY)
 * ============================================================================ */

/**
 * @brief Visit a single optional reference
 * 
 * Usage: NMO_REF_VISIT(visitor, ud, state->parent_id, NMO_REF_HIERARCHY, "parent");
 */
#define NMO_REF_VISIT(visitor, user_data, id, kind, field) \
    do { \
        nmo_object_id_t _id = (id); \
        if (_id != 0) { \
            if (!(visitor)((user_data), _id, (kind), (field), 0)) { \
                return NMO_OK; \
            } \
        } \
    } while(0)

/**
 * @brief Visit an array of object IDs
 *
 * Usage: NMO_REF_VISIT_ARRAY(visitor, ud, state->mesh_ids, state->mesh_count, 
 *                            NMO_REF_MESH, "meshes");
 */
#define NMO_REF_VISIT_ARRAY(visitor, user_data, arr, count, kind, field) \
    do { \
        if ((arr) != NULL) { \
            for (uint32_t _i = 0; _i < (count); ++_i) { \
                nmo_object_id_t _id = (arr)[_i]; \
                if (_id != 0) { \
                    if (!(visitor)((user_data), _id, (kind), (field), _i)) { \
                        return NMO_OK; \
                    } \
                } \
            } \
        } \
    } while(0)

/**
 * @brief Visit fixed-size array of object IDs
 *
 * Usage: NMO_REF_VISIT_FIXED(visitor, ud, state->texture_ids, 4, 
 *                             NMO_REF_TEXTURE, "textures");
 */
#define NMO_REF_VISIT_FIXED(visitor, user_data, arr, size, kind, field) \
    do { \
        for (int _i = 0; _i < (int)(size); ++_i) { \
            nmo_object_id_t _id = (arr)[_i]; \
            if (_id != 0) { \
                if (!(visitor)((user_data), _id, (kind), (field), (uint32_t)_i)) { \
                    return NMO_OK; \
                } \
            } \
        } \
    } while(0)

/* ============================================================================
 * Built-in Enumerators (for direct use or extension)
 * ============================================================================ */

/**
 * @brief Enumerator for CKBeObject
 *
 * Enumerates: scripts, attribute_parameters
 */
NMO_API nmo_status_t nmo_ref_enum_beobject(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CK3dEntity
 *
 * Enumerates: parent, place, current_mesh, meshes, animations, skin_bones
 * Also calls beobject enumerator for base refs.
 */
NMO_API nmo_status_t nmo_ref_enum_3dentity(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKMesh
 *
 * Enumerates: material_channels, material_groups
 * Also calls beobject enumerator for base refs.
 */
NMO_API nmo_status_t nmo_ref_enum_mesh(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKMaterial
 *
 * Enumerates: textures, effect_parameter
 * Also calls beobject enumerator for base refs.
 */
NMO_API nmo_status_t nmo_ref_enum_material(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKBehavior
 *
 * Enumerates: owner, target, sub_behaviors, sub_behavior_links,
 *             inputs, outputs, in/out/local parameters, operations
 */
NMO_API nmo_status_t nmo_ref_enum_behavior(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKGroup
 *
 * Enumerates: members
 * Also calls beobject enumerator for base refs.
 */
NMO_API nmo_status_t nmo_ref_enum_group(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKScene
 *
 * Enumerates: objects_in_scene, level_ref
 */
NMO_API nmo_status_t nmo_ref_enum_scene(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKLevel
 *
 * Enumerates: scenes, render_context
 */
NMO_API nmo_status_t nmo_ref_enum_level(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKDataArray
 *
 * Enumerates: element references (if any)
 */
NMO_API nmo_status_t nmo_ref_enum_dataarray(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKParameter types
 *
 * Enumerates: owner, shared source, data references
 */
NMO_API nmo_status_t nmo_ref_enum_parameter(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKBehaviorLink
 *
 * Enumerates: source, target IO connections
 */
NMO_API nmo_status_t nmo_ref_enum_behaviorlink(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

/**
 * @brief Enumerator for CKBehaviorIO
 *
 * Enumerates: owner
 */
NMO_API nmo_status_t nmo_ref_enum_behaviorio(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data
);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REF_ENUMERATE_H */
