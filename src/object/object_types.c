/**
 * @file object_types.c
 * @brief Implementation of Virtools object type registration
 * 
 * Registers all CKObject-derived classes as types in the type registry,
 * replacing the legacy schema system with unified type system integration.
 * 
 * Direct integration with object schema implementations via type system vtables.
 */

#include "object/nmo_object_types.h"
#include "object/nmo_object_enums.h"
#include "object/nmo_object_structs.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_string.h"
#include "object/nmo_class_ids.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_hash.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"

/* Schema headers for vtable integration */
#include "object/nmo_object_schemas.h"
#include "object/nmo_sceneobject_schemas.h"
#include "object/nmo_renderobject_schemas.h"
#include "object/nmo_beobject_schemas.h"
#include "object/nmo_parameter_schemas.h"
#include "object/nmo_parameterlocal_schemas.h"
#include "object/nmo_parameterin_schemas.h"
#include "object/nmo_parameterout_schemas.h"
#include "object/nmo_parameteroperation_schemas.h"
#include "object/nmo_behavior_schemas.h"
#include "object/nmo_behaviorio_schemas.h"
#include "object/nmo_behaviorlink_schemas.h"
#include "object/nmo_mesh_schemas.h"
#include "object/nmo_patchmesh_schemas.h"
#include "object/nmo_3dentity_schemas.h"
#include "object/nmo_3dobject_schemas.h"
#include "object/nmo_camera_schemas.h"
#include "object/nmo_targetcamera_schemas.h"
#include "object/nmo_light_schemas.h"
#include "object/nmo_targetlight_schemas.h"
#include "object/nmo_character_schemas.h"
#include "object/nmo_curve_schemas.h"
#include "object/nmo_sprite3d_schemas.h"
#include "object/nmo_scene_schemas.h"
#include "object/nmo_level_schemas.h"
#include "object/nmo_group_schemas.h"
#include "object/nmo_dataarray_schemas.h"
#include "object/nmo_animation_schemas.h"
#include "object/nmo_material_schemas.h"
#include "object/nmo_texture_schemas.h"
#include "object/nmo_2dentity_schemas.h"
#include "object/nmo_sprite_schemas.h"
#include "object/nmo_spritetext_schemas.h"
#include "object/nmo_rendercontext_schemas.h"
#include "object/nmo_kinematicchain_schemas.h"
#include "object/nmo_synchro_schemas.h"
#include "object/nmo_place_schemas.h"
#include "object/nmo_sound_schemas.h"
#include "object/nmo_interfaceobjectmanager_schemas.h"
#include "object/nmo_grid_schemas.h"
#include "object/nmo_layer_schemas.h"

#include <stddef.h>
#include <stdalign.h>
#include <string.h>


/* ============================================================================
 * Base Object Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_base_object_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_object_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_sceneobject_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_beobject_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_renderobject_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * 2D Entity Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_2d_entity_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_2dentity_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_sprite_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_spritetext_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * 3D Entity Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_3d_entity_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_3dentity_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_3dobject_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_camera_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_light_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_character_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Resource Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_resource_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_material_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_texture_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_mesh_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Behavior Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_behavior_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_behavior_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_behaviorio_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_behaviorlink_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Scene Management Types
 * ============================================================================ */

static nmo_status_t nmo_register_scene_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_scene_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_level_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_group_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Data Structure Types
 * ============================================================================ */

static nmo_status_t nmo_register_data_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_dataarray_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Animation Types
 * ============================================================================ */

static nmo_status_t nmo_register_animation_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_animation_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_keyedanimation_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_objectanimation_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Parameter Types (Extended)
 * ============================================================================ */

nmo_status_t nmo_register_parameter_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_parameter_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_parameterin_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_parameterout_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_parameterlocal_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_parameteroperation_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Extended 3D Entity Types
 * ============================================================================ */

nmo_status_t nmo_register_extended_3d_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_targetcamera_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_targetlight_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_sprite3d_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_curve_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_curvepoint_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_bodypart_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Utility Object Types
 * ============================================================================ */

nmo_status_t nmo_register_utility_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_rendercontext_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_kinematicchain_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_synchro_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_state_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_criticalsection_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_place_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_sound_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_wavesound_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_midisound_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_interfaceobjectmanager_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_layer_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Mesh Variant Types
 * ============================================================================ */

nmo_status_t nmo_register_mesh_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_grid_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_patchmesh_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Main Registration Function
 * ============================================================================ */

nmo_status_t nmo_register_object_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    if (nmo_type_registry_find_by_guid(registry, NMO_TYPE_GUID_INT) == NULL) {
        NMO_RETURN_IF_ERROR(nmo_register_builtin_types(registry));
    }

    NMO_RETURN_IF_ERROR(nmo_register_object_enums(registry));
    NMO_RETURN_IF_ERROR(nmo_register_object_structs(registry));

    /* Register in hierarchy order (base types first) */
    NMO_RETURN_IF_ERROR(nmo_register_base_object_types(registry));

    /* CKObject-level types */
    NMO_RETURN_IF_ERROR(nmo_register_behavior_types(registry));
    NMO_RETURN_IF_ERROR(nmo_register_parameter_types(registry));
    NMO_RETURN_IF_ERROR(nmo_register_utility_types(registry));

    /* CKSceneObject subtree */
    NMO_RETURN_IF_ERROR(nmo_register_animation_types(registry));

    /* CKBeObject subtree */
    NMO_RETURN_IF_ERROR(nmo_register_scene_types(registry));
    NMO_RETURN_IF_ERROR(nmo_register_data_types(registry));
    NMO_RETURN_IF_ERROR(nmo_register_resource_types(registry));

    /* CKRenderObject subtree */
    NMO_RETURN_IF_ERROR(nmo_register_2d_entity_types(registry));
    NMO_RETURN_IF_ERROR(nmo_register_3d_entity_types(registry));
    NMO_RETURN_IF_ERROR(nmo_register_extended_3d_types(registry));
    NMO_RETURN_IF_ERROR(nmo_register_mesh_types(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

const nmo_type_descriptor_t* nmo_get_object_type_by_class_id(
    nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (!registry) {
        return NULL;
    }

    return nmo_type_registry_find_by_class_id(registry, class_id);
}

int nmo_is_object_type(
    nmo_type_registry_t *registry,
    nmo_guid_t type_guid)
{
    if (!registry) {
        return 0;
    }

    /* Look up both type descriptors first */
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, type_guid);
    const nmo_type_descriptor_t *ckobject_type = nmo_type_registry_find_by_guid(registry, CKPGUID_OBJECT);
    
    if (!type || !ckobject_type) {
        return 0;
    }

    /* Check if this type is derived from CKObject */
    return nmo_type_is_derived_from(registry, type->id, ckobject_type->id);
}
