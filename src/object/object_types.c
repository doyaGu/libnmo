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
#include "object/nmo_ckobject_schemas.h"
#include "object/nmo_cksceneobject_schemas.h"
#include "object/nmo_ckrenderobject_schemas.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_ckparameter_schemas.h"
#include "object/nmo_ckparameterlocal_schemas.h"
#include "object/nmo_ckparameterin_schemas.h"
#include "object/nmo_ckparameterout_schemas.h"
#include "object/nmo_ckparameteroperation_schemas.h"
#include "object/nmo_ckbehavior_schemas.h"
#include "object/nmo_ckbehaviorio_schemas.h"
#include "object/nmo_ckbehaviorlink_schemas.h"
#include "object/nmo_ckmesh_schemas.h"
#include "object/nmo_ckpatchmesh_schemas.h"
#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_ck3dobject_schemas.h"
#include "object/nmo_ckcamera_schemas.h"
#include "object/nmo_cktargetcamera_schemas.h"
#include "object/nmo_cklight_schemas.h"
#include "object/nmo_cktargetlight_schemas.h"
#include "object/nmo_ckcharacter_schemas.h"
#include "object/nmo_ckcurve_schemas.h"
#include "object/nmo_cksprite3d_schemas.h"
#include "object/nmo_ckscene_schemas.h"
#include "object/nmo_cklevel_schemas.h"
#include "object/nmo_ckgroup_schemas.h"
#include "object/nmo_ckdataarray_schemas.h"
#include "object/nmo_ckanimation_schemas.h"
#include "object/nmo_ckmaterial_schemas.h"
#include "object/nmo_cktexture_schemas.h"
#include "object/nmo_ck2dentity_schemas.h"
#include "object/nmo_cksprite_schemas.h"
#include "object/nmo_ckspritetext_schemas.h"
#include "object/nmo_ckrendercontext_schemas.h"
#include "object/nmo_ckkinematicchain_schemas.h"
#include "object/nmo_cksynchro_schemas.h"
#include "object/nmo_ckplace_schemas.h"
#include "object/nmo_cksound_schemas.h"
#include "object/nmo_ckinterfaceobjectmanager_schemas.h"
#include "object/nmo_ckgrid_schemas.h"
#include "object/nmo_cklayer_schemas.h"

#include <stddef.h>
#include <stdalign.h>
#include <string.h>


/* ============================================================================
 * Base Object Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_base_object_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckobject_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cksceneobject_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckbeobject_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckrenderobject_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * 2D Entity Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_2d_entity_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ck2dentity_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cksprite_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckspritetext_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * 3D Entity Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_3d_entity_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ck3dentity_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ck3dobject_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckcamera_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cklight_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckcharacter_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Resource Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_resource_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckmaterial_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cktexture_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckmesh_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Behavior Types Registration
 * ============================================================================ */

nmo_status_t nmo_register_behavior_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckbehavior_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckbehaviorio_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckbehaviorlink_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Scene Management Types
 * ============================================================================ */

static nmo_status_t nmo_register_scene_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckscene_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cklevel_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckgroup_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Data Structure Types
 * ============================================================================ */

static nmo_status_t nmo_register_data_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckdataarray_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Animation Types
 * ============================================================================ */

static nmo_status_t nmo_register_animation_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckanimation_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckkeyedanimation_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckobjectanimation_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Parameter Types (Extended)
 * ============================================================================ */

nmo_status_t nmo_register_parameter_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckparameter_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckparameterin_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckparameterout_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckparameterlocal_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckparameteroperation_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Extended 3D Entity Types
 * ============================================================================ */

nmo_status_t nmo_register_extended_3d_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_cktargetcamera_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cktargetlight_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cksprite3d_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckcurve_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckcurvepoint_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckbodypart_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Utility Object Types
 * ============================================================================ */

nmo_status_t nmo_register_utility_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckrendercontext_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckkinematicchain_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cksynchro_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckstate_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckcriticalsection_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckplace_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cksound_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckwavesound_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckmidisound_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckinterfaceobjectmanager_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_cklayer_type(registry));

    NMO_RETURN_OK();
}

/* ============================================================================
 * Mesh Variant Types
 * ============================================================================ */

nmo_status_t nmo_register_mesh_types(nmo_type_registry_t *registry) {
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL type registry");

    NMO_RETURN_IF_ERROR(nmo_register_ckgrid_type(registry));
    NMO_RETURN_IF_ERROR(nmo_register_ckpatchmesh_type(registry));

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
    const nmo_type_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (!registry) {
        return NULL;
    }

    return nmo_type_registry_find_by_class_id(registry, class_id);
}

int nmo_is_object_type(
    const nmo_type_registry_t *registry,
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
