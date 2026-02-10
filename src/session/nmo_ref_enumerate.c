/**
 * @file nmo_ref_enumerate.c
 * @brief Extensible reference enumeration implementation
 *
 * Phase 4.1: Provides registry for type-specific reference extractors.
 * Implements visitor pattern for DRY enumeration.
 */

#include "session/nmo_ref_enumerate.h"
#include "session/nmo_ref_graph.h"  /* For NMO_REF_* compatibility macros */
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_ck2dentity_schemas.h"
#include "object/nmo_ckmesh_schemas.h"
#include "object/nmo_ckmaterial_schemas.h"
#include "object/nmo_cktexture_schemas.h"
#include "object/nmo_ckcamera_schemas.h"
#include "object/nmo_cklight_schemas.h"
#include "object/nmo_ckbehavior_schemas.h"
#include "object/nmo_ckgroup_schemas.h"
#include "object/nmo_ckscene_schemas.h"
#include "object/nmo_cklevel_schemas.h"
#include "object/nmo_ckdataarray_schemas.h"
#include "object/nmo_ckparameter_schemas.h"
#include "object/nmo_ckbehaviorlink_schemas.h"
#include "object/nmo_ckbehaviorio_schemas.h"
#include "object/nmo_ckplace_schemas.h"
#include "object/nmo_cksprite_schemas.h"
#include "object/nmo_cksprite3d_schemas.h"
#include "object/nmo_ckanimation_schemas.h"
#include "object/nmo_cksound_schemas.h"
#include "object/nmo_ckcurve_schemas.h"

#include <string.h>

/* ============================================================================
 * Registry Structure
 * ============================================================================ */

/**
 * @brief Enumerator registry entry
 */
typedef struct nmo_ref_enumerator_entry {
    nmo_class_id_t class_id;
    nmo_ref_enumerator_fn enumerator;
} nmo_ref_enumerator_entry_t;

/**
 * @brief Enumerator registry
 */
struct nmo_ref_enumerator_registry {
    nmo_arena_t *arena;
    nmo_ref_enumerator_entry_t *entries;
    size_t entry_count;
    size_t entry_capacity;
};

/* ============================================================================
 * Registry API Implementation
 * ============================================================================ */

NMO_API nmo_ref_enumerator_registry_t *nmo_ref_enumerator_registry_create(
    nmo_arena_t *arena)
{
    if (!arena) {
        return NULL;
    }
    
    nmo_ref_enumerator_registry_t *registry = nmo_arena_alloc(
        arena, sizeof(nmo_ref_enumerator_registry_t), 
        _Alignof(nmo_ref_enumerator_registry_t));
    if (!registry) {
        return NULL;
    }
    
    registry->arena = arena;
    registry->entries = NULL;
    registry->entry_count = 0;
    registry->entry_capacity = 0;
    
    return registry;
}

NMO_API void nmo_ref_enumerator_registry_destroy(
    nmo_ref_enumerator_registry_t *registry)
{
    /* Arena-allocated, nothing to do */
    (void)registry;
}

/**
 * @brief Grow entry array if needed
 */
static bool grow_entries(nmo_ref_enumerator_registry_t *registry) {
    if (registry->entry_count < registry->entry_capacity) {
        return true;
    }
    
    size_t new_cap = registry->entry_capacity == 0 ? 64 : registry->entry_capacity * 2;
    nmo_ref_enumerator_entry_t *new_entries = nmo_arena_alloc(
        registry->arena, 
        new_cap * sizeof(nmo_ref_enumerator_entry_t),
        _Alignof(nmo_ref_enumerator_entry_t));
    if (!new_entries) {
        return false;
    }
    
    if (registry->entries && registry->entry_count > 0) {
        memcpy(new_entries, registry->entries, 
               registry->entry_count * sizeof(nmo_ref_enumerator_entry_t));
    }
    
    registry->entries = new_entries;
    registry->entry_capacity = new_cap;
    return true;
}

NMO_API nmo_status_t nmo_ref_enumerator_register(
    nmo_ref_enumerator_registry_t *registry,
    nmo_class_id_t class_id,
    nmo_ref_enumerator_fn enumerator)
{
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL registry");
    NMO_ENSURE(enumerator != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL enumerator");
    
    /* Check for existing registration (update if found) */
    for (size_t i = 0; i < registry->entry_count; ++i) {
        if (registry->entries[i].class_id == class_id) {
            registry->entries[i].enumerator = enumerator;
            NMO_RETURN_OK();
        }
    }
    
    /* Add new entry */
    if (!grow_entries(registry)) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to grow enumerator registry");
    }
    
    nmo_ref_enumerator_entry_t *entry = &registry->entries[registry->entry_count++];
    entry->class_id = class_id;
    entry->enumerator = enumerator;
    
    NMO_RETURN_OK();
}

NMO_API nmo_ref_enumerator_fn nmo_ref_enumerator_lookup(
    nmo_ref_enumerator_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (!registry) {
        return NULL;
    }
    
    /* Try exact match */
    for (size_t i = 0; i < registry->entry_count; ++i) {
        if (registry->entries[i].class_id == class_id) {
            return registry->entries[i].enumerator;
        }
    }
    
    /* No inheritance lookup - we register all derived classes explicitly */
    return NULL;
}

/* ============================================================================
 * Enumeration API
 * ============================================================================ */

NMO_API nmo_status_t nmo_ref_enumerate_object(
    nmo_ref_enumerator_registry_t *registry,
    nmo_object_t *obj,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL registry");
    NMO_ENSURE(obj != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL object");
    NMO_ENSURE(visitor != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL visitor");
    
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    nmo_ref_enumerator_fn enumerator = nmo_ref_enumerator_lookup(registry, class_id);
    
    if (!enumerator) {
        /* No enumerator registered for this class or its parents */
        NMO_RETURN_OK(); /* Not an error - just no refs to enumerate */
    }
    
    const void *state = nmo_object_get_state(obj);
    if (!state) {
        NMO_RETURN_OK(); /* No state, no refs */
    }
    
    return enumerator(obj, state, visitor, user_data);
}

/* ============================================================================
 * Built-in Enumerators
 * ============================================================================ */

/**
 * @brief Enumerate CKBeObject references (scripts, attributes)
 */
NMO_API nmo_status_t nmo_ref_enum_beobject(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckbeobject_state_t *beobj = (const nmo_ckbeobject_state_t *)state;
    (void)obj;
    
    /* Scripts */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beobj->script_ids, beobj->script_count,
                        NMO_REF_SCRIPT, "scripts");
    
    /* Attribute parameters */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beobj->attribute_parameter_ids, 
                        beobj->attribute_count, NMO_REF_PARAMETER, "attribute_parameters");
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CK3dEntity references
 */
NMO_API nmo_status_t nmo_ref_enum_3dentity(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ck3dentity_state_t *entity = (const nmo_ck3dentity_state_t *)state;
    
    /* First enumerate BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &entity->base.base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Parent */
    NMO_REF_VISIT(visitor, user_data, entity->parent_id, NMO_REF_HIERARCHY, "parent");
    
    /* Place */
    NMO_REF_VISIT(visitor, user_data, entity->place_id, NMO_REF_PLACE, "place");
    
    /* Current mesh */
    NMO_REF_VISIT(visitor, user_data, entity->current_mesh_id, NMO_REF_MESH, "current_mesh");
    
    /* Mesh array */
    NMO_REF_VISIT_ARRAY(visitor, user_data, entity->mesh_ids, entity->mesh_count,
                        NMO_REF_MESH, "meshes");
    
    /* Animation array */
    NMO_REF_VISIT_ARRAY(visitor, user_data, entity->animation_ids, entity->animation_count,
                        NMO_REF_ANIMATION, "animations");
    
    /* Skin bones */
    if (entity->skin) {
        for (uint32_t i = 0; i < entity->skin->bone_count && entity->skin->bones; ++i) {
            nmo_object_id_t bone_id = entity->skin->bones[i].bone_id;
            if (bone_id != 0) {
                if (!visitor(user_data, bone_id, NMO_REF_SKIN_BONE, "skin_bones", i)) {
                    NMO_RETURN_OK();
                }
            }
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKMesh references
 */
NMO_API nmo_status_t nmo_ref_enum_mesh(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ck_mesh_state_t *mesh = (const nmo_ck_mesh_state_t *)state;
    
    /* First enumerate BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &mesh->beobject, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Material channels */
    for (uint32_t i = 0; i < mesh->material_channel_count && mesh->material_channels; ++i) {
        nmo_object_id_t mat_id = mesh->material_channels[i].material_id;
        if (mat_id != 0) {
            if (!visitor(user_data, mat_id, NMO_REF_MATERIAL, "material_channels", i)) {
                NMO_RETURN_OK();
            }
        }
    }
    
    /* Material groups */
    for (uint32_t i = 0; i < mesh->material_group_count && mesh->material_groups; ++i) {
        nmo_object_id_t mat_id = mesh->material_groups[i].material_id;
        if (mat_id != 0) {
            if (!visitor(user_data, mat_id, NMO_REF_MATERIAL, "material_groups", i)) {
                NMO_RETURN_OK();
            }
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKMaterial references
 */
NMO_API nmo_status_t nmo_ref_enum_material(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ck_material_state_t *mat = (const nmo_ck_material_state_t *)state;
    
    /* First enumerate BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &mat->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Texture slots (up to 4) */
    NMO_REF_VISIT_FIXED(visitor, user_data, mat->texture_ids, 4, 
                        NMO_REF_TEXTURE, "textures");
    
    /* Effect parameter */
    if (mat->has_effect_param) {
        NMO_REF_VISIT(visitor, user_data, mat->effect_parameter_id, 
                      NMO_REF_PARAMETER, "effect_parameter");
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKBehavior references
 */
NMO_API nmo_status_t nmo_ref_enum_behavior(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckbehavior_state_t *beh = (const nmo_ckbehavior_state_t *)state;
    (void)obj;
    
    /* Owner */
    NMO_REF_VISIT(visitor, user_data, beh->owner_id, NMO_REF_OWNER, "owner");
    
    /* Target parameter */
    NMO_REF_VISIT(visitor, user_data, beh->target_parameter_id, NMO_REF_TARGET, "target_parameter");
    
    /* Sub-behaviors */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beh->sub_behaviors, beh->sub_behavior_count,
                        NMO_REF_OWNER, "sub_behaviors");
    
    /* Sub-behavior links */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beh->sub_behavior_links, beh->sub_behavior_link_count,
                        NMO_REF_BEHAVIOR_LINK, "sub_behavior_links");
    
    /* Inputs */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beh->inputs, beh->input_count,
                        NMO_REF_OWNER, "inputs");
    
    /* Outputs */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beh->outputs, beh->output_count,
                        NMO_REF_OWNER, "outputs");
    
    /* Input parameters */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beh->in_parameters, beh->in_parameter_count,
                        NMO_REF_PARAMETER, "in_parameters");
    
    /* Output parameters */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beh->out_parameters, beh->out_parameter_count,
                        NMO_REF_PARAMETER, "out_parameters");
    
    /* Local parameters */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beh->local_parameters, beh->local_parameter_count,
                        NMO_REF_PARAMETER, "local_parameters");
    
    /* Operations */
    NMO_REF_VISIT_ARRAY(visitor, user_data, beh->operations, beh->operation_count,
                        NMO_REF_PARAMETER, "operations");
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKGroup references
 */
NMO_API nmo_status_t nmo_ref_enum_group(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckgroup_state_t *group = (const nmo_ckgroup_state_t *)state;
    
    /* First enumerate BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &group->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Group members */
    NMO_REF_VISIT_ARRAY(visitor, user_data, group->object_ids, group->object_count,
                        NMO_REF_GROUP_MEMBER, "members");
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKScene references
 */
NMO_API nmo_status_t nmo_ref_enum_scene(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckscene_state_t *scene = (const nmo_ckscene_state_t *)state;
    
    /* First enumerate BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &scene->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Level reference */
    NMO_REF_VISIT(visitor, user_data, scene->level_id, NMO_REF_SCENE, "level");
    
    /* Scene objects */
    if (scene->object_descs) {
        for (uint32_t i = 0; i < scene->object_count; ++i) {
            nmo_object_id_t obj_id = scene->object_descs[i].object_id;
            if (obj_id != 0) {
                if (!visitor(user_data, obj_id, NMO_REF_SCENE, "scene_objects", i)) {
                    NMO_RETURN_OK();
                }
            }
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKLevel references
 */
NMO_API nmo_status_t nmo_ref_enum_level(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_cklevel_state_t *level = (const nmo_cklevel_state_t *)state;
    
    /* First enumerate BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &level->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Scenes */
    NMO_REF_VISIT_ARRAY(visitor, user_data, level->scene_ids, level->scene_count,
                        NMO_REF_SCENE, "scenes");
    
    /* Current scene */
    NMO_REF_VISIT(visitor, user_data, level->current_scene_id, NMO_REF_SCENE, "current_scene");
    
    /* Level scene */
    NMO_REF_VISIT(visitor, user_data, level->level_scene_id, NMO_REF_SCENE, "level_scene");
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKDataArray references
 */
NMO_API nmo_status_t nmo_ref_enum_dataarray(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckdataarray_state_t *arr = (const nmo_ckdataarray_state_t *)state;
    
    /* First enumerate BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &arr->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Iterate rows and find OBJECT/PARAMETER columns */
    if (arr->rows && arr->column_formats) {
        for (uint32_t row = 0; row < arr->row_count; ++row) {
            nmo_ckdataarray_row_t *r = &arr->rows[row];
            if (!r->cells) continue;
            
            for (uint32_t col = 0; col < arr->column_count && col < r->column_count; ++col) {
                nmo_ck_arraytype_t type = arr->column_formats[col].type;
                nmo_object_id_t ref_id = 0;
                
                if (type == CKARRAYTYPE_OBJECT) {
                    ref_id = r->cells[col].object_id;
                } else if (type == CKARRAYTYPE_PARAMETER) {
                    ref_id = r->cells[col].parameter_id;
                }
                
                if (ref_id != 0) {
                    uint32_t idx = row * arr->column_count + col;
                    if (!visitor(user_data, ref_id, NMO_REF_DATA_ARRAY, "cells", idx)) {
                        NMO_RETURN_OK();
                    }
                }
            }
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKParameter references
 */
NMO_API nmo_status_t nmo_ref_enum_parameter(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckparameter_state_t *param = (const nmo_ckparameter_state_t *)state;
    (void)obj;
    
    /* Object mode reference */
    if (param->mode == CKPARAM_MODE_OBJECT && param->object_id != 0) {
        NMO_REF_VISIT(visitor, user_data, param->object_id, NMO_REF_PARAMETER, "value");
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKBehaviorLink references
 */
NMO_API nmo_status_t nmo_ref_enum_behaviorlink(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckbehaviorlink_state_t *link = (const nmo_ckbehaviorlink_state_t *)state;
    (void)obj;
    
    /* Input IO */
    NMO_REF_VISIT(visitor, user_data, link->in_io_id, NMO_REF_BEHAVIOR_LINK, "in_io");
    
    /* Output IO */
    NMO_REF_VISIT(visitor, user_data, link->out_io_id, NMO_REF_BEHAVIOR_LINK, "out_io");
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKBehaviorIO references
 */
NMO_API nmo_status_t nmo_ref_enum_behaviorio(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    /* CKBehaviorIO has no object references, just flags */
    (void)obj;
    (void)state;
    (void)visitor;
    (void)user_data;
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKCamera references (3DEntity + target)
 */
static nmo_status_t nmo_ref_enum_camera(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckcamera_state_t *cam = (const nmo_ckcamera_state_t *)state;
    
    /* 3DEntity base refs */
    return nmo_ref_enum_3dentity(obj, &cam->entity, visitor, user_data);
}

/**
 * @brief Enumerate CKLight references (3DEntity + target)
 */
static nmo_status_t nmo_ref_enum_light(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_cklight_state_t *light = (const nmo_cklight_state_t *)state;
    
    /* 3DEntity base refs */
    return nmo_ref_enum_3dentity(obj, &light->entity, visitor, user_data);
}

/**
 * @brief Enumerate CKPlace references
 */
static nmo_status_t nmo_ref_enum_place(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckplace_state_t *place = (const nmo_ckplace_state_t *)state;
    
    /* First enumerate BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &place->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Camera */
    if (place->has_camera) {
        NMO_REF_VISIT(visitor, user_data, place->camera_id, NMO_REF_TARGET, "camera");
    }
    
    /* Level */
    if (place->has_level) {
        NMO_REF_VISIT(visitor, user_data, place->level_id, NMO_REF_SCENE, "level");
    }
    
    /* Portals */
    if (place->portals) {
        for (uint32_t i = 0; i < place->portal_count; ++i) {
            NMO_REF_VISIT(visitor, user_data, place->portals[i].place_id, 
                          NMO_REF_PLACE, "portal_places");
            NMO_REF_VISIT(visitor, user_data, place->portals[i].portal_id, 
                          NMO_REF_PLACE, "portals");
        }
    }
    
    /* References */
    NMO_REF_VISIT_ARRAY(visitor, user_data, place->reference_ids, place->reference_count,
                        NMO_REF_SCENE, "references");
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * Additional Built-in Enumerators (Phase 4.2)
 * ============================================================================ */

/**
 * @brief Enumerate CK2dEntity references (parent, material)
 */
static nmo_status_t nmo_ref_enum_2dentity(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ck2dentity_state_t *entity = (const nmo_ck2dentity_state_t *)state;
    
    /* BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &entity->base.base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Parent */
    if (entity->has_parent) {
        NMO_REF_VISIT(visitor, user_data, entity->parent_id, NMO_REF_HIERARCHY, "parent");
    }
    
    /* Material */
    if (entity->has_material) {
        NMO_REF_VISIT(visitor, user_data, entity->material_id, NMO_REF_MATERIAL, "material");
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKSprite references (extends 2DEntity)
 */
static nmo_status_t nmo_ref_enum_sprite(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_cksprite_state_t *sprite = (const nmo_cksprite_state_t *)state;
    
    /* 2DEntity base refs */
    nmo_status_t status = nmo_ref_enum_2dentity(obj, &sprite->entity, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Sprite reference (clone source) */
    if (sprite->has_sprite_ref) {
        NMO_REF_VISIT(visitor, user_data, sprite->sprite_ref_id, NMO_REF_TEXTURE, "sprite_ref");
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKTexture references (BeObject only, no additional refs)
 */
static nmo_status_t nmo_ref_enum_texture(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_cktexture_state_t *tex = (const nmo_cktexture_state_t *)state;
    
    /* BeObject base refs only */
    return nmo_ref_enum_beobject(obj, &tex->base, visitor, user_data);
}

/**
 * @brief Enumerate CKAnimation references
 */
static nmo_status_t nmo_ref_enum_animation(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckanimation_state_t *anim = (const nmo_ckanimation_state_t *)state;
    (void)obj;
    
    /* Root entity */
    if (anim->has_root_entity) {
        NMO_REF_VISIT(visitor, user_data, anim->root_entity_id, NMO_REF_TARGET, "root_entity");
    }
    
    /* Character */
    if (anim->has_character) {
        NMO_REF_VISIT(visitor, user_data, anim->character_id, NMO_REF_TARGET, "character");
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKKeyedAnimation references
 */
static nmo_status_t nmo_ref_enum_keyedanimation(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckkeyedanimation_state_t *anim = (const nmo_ckkeyedanimation_state_t *)state;
    
    /* Base animation refs */
    nmo_status_t status = nmo_ref_enum_animation(obj, &anim->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Animation IDs */
    NMO_REF_VISIT_ARRAY(visitor, user_data, anim->animation_ids, anim->animation_count,
                        NMO_REF_ANIMATION, "animations");
    
    /* Subanims */
    for (uint32_t i = 0; i < anim->subanim_count && anim->subanims; ++i) {
        if (anim->subanims[i].object_id != 0) {
            if (!visitor(user_data, anim->subanims[i].object_id, NMO_REF_ANIMATION, "subanims", i)) {
                NMO_RETURN_OK();
            }
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKObjectAnimation references
 */
static nmo_status_t nmo_ref_enum_objectanimation(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckobjectanimation_state_t *anim = (const nmo_ckobjectanimation_state_t *)state;
    (void)obj;
    
    /* Entity */
    NMO_REF_VISIT(visitor, user_data, anim->entity_id, NMO_REF_TARGET, "entity");
    
    /* Merge animations */
    if (anim->has_merge) {
        NMO_REF_VISIT(visitor, user_data, anim->anim1_id, NMO_REF_ANIMATION, "anim1");
        NMO_REF_VISIT(visitor, user_data, anim->anim2_id, NMO_REF_ANIMATION, "anim2");
    }
    
    /* Shared animation */
    if (anim->has_shared_anim) {
        NMO_REF_VISIT(visitor, user_data, anim->shared_anim_id, NMO_REF_ANIMATION, "shared_anim");
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKSound/CKWaveSound references
 */
static nmo_status_t nmo_ref_enum_wavesound(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckwavesound_state_t *sound = (const nmo_ckwavesound_state_t *)state;
    
    /* BeObject base refs */
    nmo_status_t status = nmo_ref_enum_beobject(obj, &sound->base.base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Attached object */
    NMO_REF_VISIT(visitor, user_data, sound->attached_object_id, NMO_REF_TARGET, "attached_object");
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKCurve references (3DEntity + control points)
 */
static nmo_status_t nmo_ref_enum_curve(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_ckcurve_state_t *curve = (const nmo_ckcurve_state_t *)state;
    
    /* 3DEntity base refs */
    nmo_status_t status = nmo_ref_enum_3dentity(obj, &curve->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Control points */
    if (curve->has_curve_data) {
        NMO_REF_VISIT_ARRAY(visitor, user_data, curve->control_point_ids, 
                            curve->control_point_count, NMO_REF_TARGET, "control_points");
    }
    
    /* Sub-points */
    for (uint32_t i = 0; i < curve->sub_point_count && curve->sub_points; ++i) {
        if (curve->sub_points[i].point_id != 0) {
            if (!visitor(user_data, curve->sub_points[i].point_id, NMO_REF_TARGET, "sub_points", i)) {
                NMO_RETURN_OK();
            }
        }
    }
    
    NMO_RETURN_OK();
}

/**
 * @brief Enumerate CKSprite3D references (3DEntity + material)
 */
static nmo_status_t nmo_ref_enum_sprite3d(
    nmo_object_t *obj,
    const void *state,
    nmo_ref_visitor_fn visitor,
    void *user_data)
{
    const nmo_cksprite3d_state_t *sprite = (const nmo_cksprite3d_state_t *)state;
    
    /* 3DEntity base refs */
    nmo_status_t status = nmo_ref_enum_3dentity(obj, &sprite->base, visitor, user_data);
    if (status != NMO_OK) {
        return status;
    }
    
    /* Material (always present, check for non-zero) */
    NMO_REF_VISIT(visitor, user_data, sprite->material_id, NMO_REF_MATERIAL, "material");
    
    NMO_RETURN_OK();
}

/* ============================================================================
 * Register All Built-in Enumerators
 * ============================================================================ */

NMO_API nmo_status_t nmo_ref_enumerator_register_builtins(
    nmo_ref_enumerator_registry_t *registry)
{
    NMO_ENSURE(registry != NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
               "NULL registry");
    
    nmo_status_t status;
    
    /* Base classes */
    status = nmo_ref_enumerator_register(registry, NMO_CID_BEOBJECT, nmo_ref_enum_beobject);
    if (status != NMO_OK) return status;
    
    /* 2D objects */
    status = nmo_ref_enumerator_register(registry, NMO_CID_2DENTITY, nmo_ref_enum_2dentity);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_SPRITE, nmo_ref_enum_sprite);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_SPRITETEXT, nmo_ref_enum_2dentity);
    if (status != NMO_OK) return status;
    
    /* 3D objects */
    status = nmo_ref_enumerator_register(registry, NMO_CID_3DENTITY, nmo_ref_enum_3dentity);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_3DOBJECT, nmo_ref_enum_3dentity);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_CHARACTER, nmo_ref_enum_3dentity);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_BODYPART, nmo_ref_enum_3dentity);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_SPRITE3D, nmo_ref_enum_sprite3d);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_CURVE, nmo_ref_enum_curve);
    if (status != NMO_OK) return status;
    
    /* Render objects */
    status = nmo_ref_enumerator_register(registry, NMO_CID_MESH, nmo_ref_enum_mesh);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_MATERIAL, nmo_ref_enum_material);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_TEXTURE, nmo_ref_enum_texture);
    if (status != NMO_OK) return status;
    
    /* Camera/Light */
    status = nmo_ref_enumerator_register(registry, NMO_CID_CAMERA, nmo_ref_enum_camera);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_TARGETCAMERA, nmo_ref_enum_camera);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_LIGHT, nmo_ref_enum_light);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_TARGETLIGHT, nmo_ref_enum_light);
    if (status != NMO_OK) return status;
    
    /* Behavior system */
    status = nmo_ref_enumerator_register(registry, NMO_CID_BEHAVIOR, nmo_ref_enum_behavior);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_BEHAVIORLINK, nmo_ref_enum_behaviorlink);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_BEHAVIORIO, nmo_ref_enum_behaviorio);
    if (status != NMO_OK) return status;
    
    /* Parameters */
    status = nmo_ref_enumerator_register(registry, NMO_CID_PARAMETER, nmo_ref_enum_parameter);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_PARAMETERIN, nmo_ref_enum_parameter);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_PARAMETEROUT, nmo_ref_enum_parameter);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_PARAMETERLOCAL, nmo_ref_enum_parameter);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_PARAMETEROPERATION, nmo_ref_enum_parameter);
    if (status != NMO_OK) return status;
    
    /* Containers */
    status = nmo_ref_enumerator_register(registry, NMO_CID_GROUP, nmo_ref_enum_group);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_SCENE, nmo_ref_enum_scene);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_LEVEL, nmo_ref_enum_level);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_DATAARRAY, nmo_ref_enum_dataarray);
    if (status != NMO_OK) return status;
    
    /* Places */
    status = nmo_ref_enumerator_register(registry, NMO_CID_PLACE, nmo_ref_enum_place);
    if (status != NMO_OK) return status;
    
    /* Animation types */
    status = nmo_ref_enumerator_register(registry, NMO_CID_ANIMATION, nmo_ref_enum_animation);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_KEYEDANIMATION, nmo_ref_enum_keyedanimation);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_OBJECTANIMATION, nmo_ref_enum_objectanimation);
    if (status != NMO_OK) return status;
    
    /* Sound types */
    status = nmo_ref_enumerator_register(registry, NMO_CID_SOUND, nmo_ref_enum_beobject);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_WAVESOUND, nmo_ref_enum_wavesound);
    if (status != NMO_OK) return status;
    
    status = nmo_ref_enumerator_register(registry, NMO_CID_MIDISOUND, nmo_ref_enum_beobject);
    if (status != NMO_OK) return status;
    
    NMO_RETURN_OK();
}
