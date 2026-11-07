/**
 * @file scene.c
 * @brief Initial conditions of the level scene's objects (CKScene's object
 *        initial values), of what the engine models.
 */

#include "../engine_internal.h"

#include "object/builtin/nmo_targetlight_schemas.h"

#include "format/nmo_object.h"
#include "format/nmo_chunk_api.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_statesave_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Initial conditions (CKScene::SetObjectInitialValue, GetObjectInitialValue)
 * ============================================================================ */

static void eng_release_ic(eng_ic_t *ic)
{
    if (ic->has_array) eng_array_release(&ic->array);
    free(ic->curve_points.ids);
}

/* Take ownership of the value only after its replacement can be stored. */
static nmo_status_t eng_store_ic(nmo_engine_t *engine, const eng_ic_t *value)
{
    int32_t index = eng_map_get(&engine->ics.map, value->id);
    eng_ic_t *ic = NULL;
    if (index >= 0) {
        ic = &engine->ics.list.data[index];
        eng_release_ic(ic);
    } else if (ENG_VEC_PUSH(engine->ics.list, ic)) {
        if (!eng_map_put(&engine->ics.map, value->id, (int32_t)(engine->ics.list.count - 1u))) {
            engine->ics.list.count--;
            return NMO_ERR_NOMEM;
        }
    } else {
        return NMO_ERR_NOMEM;
    }
    *ic = *value;
    return NMO_OK;
}

nmo_status_t eng_load_ic(nmo_engine_t *engine, nmo_object_id_t id, nmo_chunk_t *chunk)
{
    nmo_object_t *object = eng_find_object(engine, id);
    if (object == NULL || (eng_entity_index(engine, id) < 0 &&
                          object->class_id != NMO_CID_DATAARRAY && object->class_id != NMO_CID_MATERIAL)) {
        return NMO_OK; /* The engine does not model this object's initial values. */
    }
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_class_id(engine->types, object->class_id);
    if (type == NULL || type->vtable == NULL || type->vtable->deserialize == NULL) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) return NMO_ERR_NOMEM;
    nmo_object_t decoded = {.id = object->id, .class_id = object->class_id,
                            .type_guid = object->type_guid, .state_size = object->state_size};
    decoded.state = nmo_arena_alloc(arena, decoded.state_size, NMO_MAX_ALIGN);
    nmo_chunk_t *saved = nmo_chunk_clone(chunk, arena);
    nmo_status_t status = decoded.state != NULL && saved != NULL ? NMO_OK : NMO_ERR_NOMEM;
    const eng_module_t *module = eng_module_of(engine, id, NULL);
    const nmo_type_runtime_t runtime = {.types = engine->types, .ops = engine->operations_registry};
    nmo_deserialize_context_t context = nmo_deserialize_context_create(arena, module->repo, &runtime, 0);
    context.object = &decoded;
    bool created = false;
    if (status == NMO_OK) {
        memset(decoded.state, 0, decoded.state_size);
        status = type->vtable->create != NULL ? type->vtable->create(decoded.state, type, &context) : NMO_OK;
        created = status == NMO_OK;
    }
    if (status == NMO_OK) status = nmo_chunk_start_read(saved);
    if (status == NMO_OK) status = type->vtable->deserialize(decoded.state, saved, type, &context);
    eng_ic_t ic = {.id = id};
    if (status == NMO_OK) {
        const nmo_curvepoint_state_t *point = nmo_type_query_object_get_ancestor_state_by_guid(engine->types,&decoded,CKPGUID_CURVEPOINT);
        if (point != NULL) {
            ic.curvepoint_mask = (point->has_tcb_chunk || (point->has_default_data && point->defaultdata_is_modern) ? ENG_CURVEPOINT_TCB : 0) |
                (point->has_default_data ? ENG_CURVEPOINT_LINEAR : 0);
            ic.curvepoint_properties = (eng_curvepoint_properties_t){point->bias,point->tension,point->continuity,point->linear};
        }
    }
    if (status == NMO_OK) {
        const nmo_curve_state_t *curve = nmo_type_query_object_get_ancestor_state_by_guid(engine->types,&decoded,CKPGUID_CURVE);
        if (curve != NULL) {
            ic.curve_mask = curve->has_curveonly_chunk ? ENG_CURVE_PROPERTIES_ALL :
                (curve->has_open_chunk ? ENG_CURVE_OPEN : 0) | (curve->has_steps_chunk ? ENG_CURVE_STEPS : 0) |
                (curve->has_fitting_chunk ? ENG_CURVE_FITTING : 0);
            ic.curve_properties = (eng_curve_properties_t){(int32_t)curve->opened,(int32_t)curve->step_count,curve->fitting_coeff};
            ic.has_curve_points = curve->has_curveonly_chunk || curve->has_controlpoints_chunk;
            if (ic.has_curve_points && curve->control_point_count != 0) {
                ic.curve_points.ids = malloc((size_t)curve->control_point_count*sizeof(*ic.curve_points.ids));
                if (ic.curve_points.ids == NULL) status = NMO_ERR_NOMEM;
                else for (uint32_t i = 0; i < curve->control_point_count; i++) {
                    ic.curve_points.ids[ic.curve_points.count++] = eng_module_id(module,nmo_ref_runtime_id(&curve->control_point_ids[i]));
                }
            }
        }
    }
    if (status == NMO_OK) {
        const nmo_sprite3d_state_t *sprite = nmo_type_query_object_get_ancestor_state_by_guid(engine->types,&decoded,CKPGUID_SPRITE3D);
        if (sprite != NULL && nmo_chunk_seek_identifier(saved,CK_STATESAVE_SPRITE3DDATA) == NMO_OK) {
            ic.has_sprite3d = true;
            eng_sprite3d_from_state(engine,id,sprite,&ic.sprite3d);
        }
    }
    if (status == NMO_OK) {
        const nmo_object_state_t *base = nmo_type_query_object_get_ancestor_state_by_guid(
            engine->types, &decoded, CKPGUID_OBJECT);
        ic.visibility = base != NULL && (base->visibility_flags & NMO_CKOBJECT_VISIBLE) ? 0x40 : 0;
        if (base != NULL && (base->visibility_flags & NMO_CKOBJECT_HIERARCHICAL)) ic.visibility |= 0x200;
        const nmo_3dentity_state_t *entity = nmo_type_query_object_get_ancestor_state_by_guid(
            engine->types, &decoded, CKPGUID_3DENTITY);
        if (entity != NULL) {
            /* CKPlace's non-file Load stops at CKBeObject; it leaves 3D flags alone. */
            ic.has_entity_flags = !nmo_type_registry_is_class_derived_from(engine->types,object->class_id,NMO_CID_PLACE) && (entity->has_entityndata_chunk || entity->has_flags_chunk);
            ic.entity_flags = entity->entity_flags;
            ic.has_matrix = entity->has_entityndata_chunk || entity->has_matrix_chunk;
            memcpy(ic.world, entity->world_matrix, sizeof(ic.world));
            ic.has_parent = entity->has_entityndata_chunk || entity->has_parent_chunk;
            ic.parent = eng_module_id(module, nmo_ref_runtime_id(&entity->parent));
        }
        const nmo_targetcamera_state_t *target_camera = nmo_type_query_object_get_ancestor_state_by_guid(engine->types,&decoded,CKPGUID_TARGETCAMERA);
        if (target_camera != NULL && nmo_chunk_seek_identifier(saved,CK_STATESAVE_TCAMERATARGET) == NMO_OK) {
            ic.has_camera_target = true;
            ic.camera_target = eng_module_id(module,nmo_ref_runtime_id(&target_camera->target));
        }
        const nmo_camera_state_t *camera = nmo_type_query_object_get_ancestor_state_by_guid(
            engine->types, &decoded, CKPGUID_CAMERA);
        if (camera != NULL) {
            ic.has_projection_type = camera->has_cameraonly_chunk || camera->has_proj_chunk;
            ic.projection_type = camera->projection_type;
            ic.has_orthographic_zoom = camera->has_cameraonly_chunk || camera->has_ortho_chunk;
            ic.orthographic_zoom = camera->orthographic_zoom;
            ic.has_fov = camera->has_cameraonly_chunk || camera->has_fov_chunk;
            ic.fov = camera->fov;
            ic.has_planes = camera->has_cameraonly_chunk || camera->has_planes_chunk;
            ic.near_plane = camera->near_plane;
            ic.far_plane = camera->far_plane;
        }
        const nmo_light_state_t *light = nmo_type_query_object_get_ancestor_state_by_guid(
            engine->types, &decoded, CKPGUID_LIGHT);
        if (light != NULL && light->has_light_data_chunk) {
            ic.has_light = true;
            eng_light_t *l = &ic.light;
            memset(l, 0, sizeof(*l));
            l->id = id;
            l->color[0] = light->light_data.diffuse.r;
            l->color[1] = light->light_data.diffuse.g;
            l->color[2] = light->light_data.diffuse.b;
            l->color[3] = light->light_data.diffuse.a;
            l->active = (light->flags & NMO_LIGHT_FLAG_ACTIVE) != 0;
            l->type = (int32_t)light->light_data.type;
            l->range = light->light_data.range;
            l->attenuation[0] = light->light_data.attenuation0;
            l->attenuation[1] = light->light_data.attenuation1;
            l->attenuation[2] = light->light_data.attenuation2;
            l->hot_spot = light->light_data.inner_spot_cone;
            l->fall_off = light->light_data.outer_spot_cone;
            l->fall_off_shape = light->light_data.falloff;
            l->specular = (light->flags & NMO_LIGHT_FLAG_SPECULAR) != 0;
            const nmo_targetlight_state_t *target_light = nmo_type_query_object_get_ancestor_state_by_guid(
                engine->types, &decoded, CKPGUID_TARGETLIGHT);
            if (target_light != NULL && target_light->has_target) {
                l->target = eng_module_id(module, nmo_ref_runtime_id(&target_light->target));
            }
        }
        if (object->class_id == NMO_CID_MATERIAL) {
            const nmo_material_state_t *material = decoded.state;
            ic.has_material = material->has_material_data != 0;
            if (ic.has_material) eng_material_from_state(engine, id, material, &ic.material);
        }
        if (object->class_id == NMO_CID_DATAARRAY &&
            nmo_chunk_seek_identifier(saved, CK_STATESAVE_DATAARRAYFORMAT) == NMO_OK) {
            ic.has_array = true;
            status = eng_array_from_state(engine, module, id, decoded.state, &ic.array);
        }
        if (status == NMO_OK) status = eng_store_ic(engine, &ic);
    }
    if (status != NMO_OK) eng_release_ic(&ic);
    if (created && type->vtable->destroy != NULL) type->vtable->destroy(decoded.state, type, &context);
    nmo_arena_destroy(arena);
    return status;
}

/* CKSaveObjectState into the level scene's initial value: what the engine models of the object */
void eng_save_ic(nmo_engine_t *engine, nmo_object_id_t id)
{
    const eng_array_t *array = eng_find_array(engine, id);
    float world[16];
    bool has_matrix = nmo_engine_entity_world_matrix(engine, id, world);
    uint32_t projection_type = 0;
    bool has_projection_type = eng_camera_projection_type(engine,id,&projection_type);
    float orthographic_zoom = 0.0f;
    bool has_orthographic_zoom = eng_camera_orthographic_zoom(engine,id,&orthographic_zoom);
    float fov = 0.0f;
    bool has_fov = eng_camera_fov(engine,id,&fov);
    float near_plane = 0.0f, far_plane = 0.0f;
    bool has_planes = eng_camera_planes(engine, id, &near_plane, &far_plane);
    nmo_object_t *object = eng_find_object(engine, id);
    uint32_t class_id = object != NULL ? nmo_object_get_class_id(object) : 0u;
    const eng_material_t *material = class_id == NMO_CID_MATERIAL ? eng_material(engine, id) : NULL;
    const eng_light_t *light = has_matrix ? eng_light(engine, id) : NULL;
    const eng_sprite3d_t *sprite = has_matrix ? eng_sprite3d(engine,id) : NULL;
    if (array == NULL && !has_matrix && material == NULL) {
        return;
    }
    eng_ic_t value;
    memset(&value, 0, sizeof(value));
    eng_ic_t *ic = &value;
    ic->id = id;
    ic->has_array = array != NULL && eng_array_clone(&ic->array, array);
    if (array != NULL && !ic->has_array) return;
    ic->has_matrix = has_matrix;
    ic->has_parent = has_matrix;
    if (has_matrix) {
        memcpy(ic->world, world, sizeof(world));
        int32_t entity = eng_entity_index(engine, id);
        ic->has_entity_flags = !nmo_type_registry_is_class_derived_from(engine->types,class_id,NMO_CID_PLACE);
        ic->entity_flags = engine->entities.data[entity].entity_flags;
        int32_t parent = entity >= 0 ? engine->entities.data[entity].parent : -1;
        ic->parent = parent >= 0 ? engine->entities.data[parent].id : 0;
    }
    ic->has_camera_target = has_matrix && nmo_type_registry_is_class_derived_from(engine->types,class_id,NMO_CID_TARGETCAMERA);
    if (ic->has_camera_target) ic->camera_target = eng_camera_target(engine,id);
    ic->has_projection_type = has_projection_type;
    ic->projection_type = projection_type;
    ic->has_orthographic_zoom = has_orthographic_zoom;
    ic->orthographic_zoom = orthographic_zoom;
    ic->has_fov = has_fov;
    ic->fov = fov;
    ic->has_planes = has_planes;
    ic->near_plane = near_plane;
    ic->far_plane = far_plane;
    if (material != NULL) {
        ic->has_material = true;
        ic->material = material->desc;
    }
    if (light != NULL) {
        ic->has_light = true;
        ic->light = *light;
    }
    if (sprite != NULL) { ic->has_sprite3d = true; ic->sprite3d = *sprite; }
    ic->visibility = eng_object_visibility(engine, id);
    if (eng_curve_get_properties(engine,id,&ic->curve_properties)) ic->curve_mask = ENG_CURVE_PROPERTIES_ALL;
    if (ic->curve_mask != 0) {
        const eng_curve_points_t *points = eng_curve_points(engine,id);
        if (points == NULL || !eng_curve_clone_points(&ic->curve_points,points)) { eng_release_ic(ic); return; }
        ic->has_curve_points = true;
    }
    if (eng_curvepoint_get_properties(engine,id,&ic->curvepoint_properties)) ic->curvepoint_mask = ENG_CURVEPOINT_PROPERTIES_ALL;
    if (eng_store_ic(engine, ic) != NMO_OK) eng_release_ic(ic);
}

/* CKReadObjectState of the object's initial value in the level scene, when it is in it and has one */
void eng_restore_ic(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_in_scene(engine, id) ? eng_map_get(&engine->ics.map, id) : -1;
    if (index < 0) {
        return;
    }
    const eng_ic_t *ic = &engine->ics.list.data[index];
    eng_array_t *array = ic->has_array ? eng_find_array(engine, id) : NULL;
    eng_array_t copy;
    memset(&copy, 0, sizeof(copy));
    if (array != NULL && eng_array_clone(&copy, &ic->array)) {
        eng_array_release(array);
        copy.id = array->id;
        *array = copy;
    }
    eng_set_object_visibility(engine, id, ic->visibility);
    int32_t entity_flags_index = ic->has_entity_flags ? eng_entity_index(engine,id) : -1;
    if (entity_flags_index >= 0) engine->entities.data[entity_flags_index].entity_flags = ic->entity_flags;
    if (ic->has_parent) {
        /* CK3dEntity::Load: the parent (keeping where it is), then the world matrix */
        int32_t entity = eng_entity_index(engine, id);
        if (entity >= 0) {
            eng_entity_set_parent(engine, entity,
                                  ic->parent != 0 ? eng_entity_index(engine, ic->parent) : -1, true);
        }
    }
    if (ic->has_matrix) nmo_engine_set_entity_world_matrix(engine, id, ic->world, true);
    /* CKCamera, CKMaterial and CKLight's own members */
    if (ic->has_camera_target) eng_restore_camera_target(engine,id,ic->camera_target);
    if (ic->has_projection_type) eng_set_camera_projection_type(engine,id,ic->projection_type);
    if (ic->has_orthographic_zoom) eng_set_camera_orthographic_zoom(engine,id,ic->orthographic_zoom);
    if (ic->has_fov) eng_set_camera_fov(engine,id,ic->fov);
    if (ic->has_planes) {
        eng_set_camera_planes(engine, id, ic->near_plane, ic->far_plane);
    }
    eng_material_t *material = ic->has_material ? eng_material(engine, id) : NULL;
    if (material != NULL) {
        material->desc = ic->material;
    }
    eng_light_t *light = ic->has_light ? eng_light(engine, id) : NULL;
    if (light != NULL) {
        nmo_object_id_t target = ic->light.target;
        *light = ic->light;
        light->target = 0;
        eng_set_light_target(engine, id, target); /* the IC's target, with its flags */
    }
    eng_sprite3d_t *sprite = ic->has_sprite3d ? eng_sprite3d(engine,id) : NULL;
    if (sprite != NULL) *sprite = ic->sprite3d;
    eng_curve_properties_t curve;
    if (ic->has_curve_points) eng_curve_set_points(engine,id,&ic->curve_points);
    if (ic->curve_mask != 0 && eng_curve_get_properties(engine,id,&curve)) {
        if (ic->curve_mask & ENG_CURVE_OPEN) curve.opened = ic->curve_properties.opened;
        if (ic->curve_mask & ENG_CURVE_STEPS) curve.step_count = ic->curve_properties.step_count;
        if (ic->curve_mask & ENG_CURVE_FITTING) curve.fitting_coeff = ic->curve_properties.fitting_coeff;
        eng_curve_set_properties(engine,id,&curve);
    }
    eng_curvepoint_properties_t point;
    if (ic->curvepoint_mask != 0 && eng_curvepoint_get_properties(engine,id,&point)) {
        if (ic->curvepoint_mask & ENG_CURVEPOINT_TCB) {
            point.bias = ic->curvepoint_properties.bias;
            point.tension = ic->curvepoint_properties.tension;
            point.continuity = ic->curvepoint_properties.continuity;
        }
        if (ic->curvepoint_mask & ENG_CURVEPOINT_LINEAR) point.linear = ic->curvepoint_properties.linear;
        eng_curvepoint_set_properties(engine,id,&point);
    }
}

void eng_free_ics(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->ics.list.count; i++) {
        eng_release_ic(&engine->ics.list.data[i]);
    }
    free(engine->ics.list.data);
    free(engine->ics.map.slots);
}
