/**
 * @file cameras.c
 * @brief Ports of the Cameras building blocks that keep state.
 *
 * Each function does what the block's function in Cameras.dll does (the
 * reference is the Ballanced source of the blocks).
 */

#include "virtools.h"

#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>

/* Set As Active Camera: the render context looks through the target camera */
static int bb_set_as_active_camera(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = camera != 0 ? eng_find_object(b->engine, camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types, object,
                                                         CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    b->engine->render.camera = camera; /* AttachViewpointToCamera */
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, camera, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Clipping Planes: the camera's front and back planes */
static int bb_set_clipping_planes(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = camera != 0 ? eng_find_object(b->engine, camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types, object,
                                                         CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_set_camera_planes(b->engine, camera, get_in_float(b, 0, 0.1f), get_in_float(b, 1, 200.0f));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, camera, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set FOV stores radians verbatim, including non-finite values. */
static int bb_set_fov(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (!eng_set_camera_fov(b->engine,camera,get_in_float(b,0,50.0f*3.14159265358979323846f/180))) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    IN_OFF(b,0);
    OUT_ON(b,0);
    eng_emit(b->engine,NMO_ENGINE_EVENT_EFFECT,b->node,camera,0);
    return NMO_ENGINE_BR_OK;
}

/* Set Zoom converts a focal length in millimeters into a horizontal FOV. */
static int bb_set_zoom(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float focal_length = get_in_float(b,0,50);
    if (focal_length <= 0.0f) return NMO_ENGINE_BR_PARAMETERERROR;
    if (!eng_set_camera_fov(b->engine,camera,2.0f*atanf(18.0f/focal_length))) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    IN_OFF(b,0);
    OUT_ON(b,0);
    eng_emit(b->engine,NMO_ENGINE_EVENT_EFFECT,b->node,camera,0);
    return NMO_ENGINE_BR_OK;
}

/* Orthographic Zoom advances IO before checking the target, including on errors. */
static int bb_orthographic_zoom(blk_t *b)
{
    IN_OFF(b,0);
    OUT_ON(b,0);
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float zoom = get_in_float(b,0,1);
    if (nmo_engine_block_version(b) < 0x00020000u) zoom *= 0.01f;
    if (!eng_set_camera_orthographic_zoom(b->engine,camera,zoom)) return NMO_ENGINE_BR_GENERICERROR;
    eng_emit(b->engine,NMO_ENGINE_EVENT_EFFECT,b->node,camera,0);
    return NMO_ENGINE_BR_OK;
}

/* Set Projection stores the integer verbatim; the camera tests its low bit. */
static int bb_set_projection(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b,0);
    OUT_ON(b,0);
    if (!eng_set_camera_projection_type(b->engine,camera,(uint32_t)get_in_int(b,0,1))) return NMO_ENGINE_BR_GENERICERROR;
    eng_emit(b->engine,NMO_ENGINE_EVENT_EFFECT,b->node,camera,0);
    return NMO_ENGINE_BR_OK;
}

/* Get Current Camera reports the camera attached to the main render context. */
static int bb_get_current_camera(blk_t *b)
{
    IN_OFF(b,0);
    OUT_ON(b,0);
    nmo_object_id_t camera = b->engine->render.camera;
    nmo_object_t *object = eng_entity_index(b->engine,camera) >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_CAMERA) == NULL) {
        camera = 0;
    }
    nmo_engine_block_set_output_value(b,0,&camera,sizeof(camera));
    return NMO_ENGINE_BR_OK;
}

/* Dolly translates a target camera along its own scaled Z axis. */
static int bb_dolly(blk_t *b)
{
    nmo_object_id_t camera = nmo_engine_block_target(b);
    int32_t entity = eng_entity_index(b->engine,camera);
    nmo_object_t *object = entity >= 0 ? eng_find_object(b->engine,camera) : NULL;
    if (object == NULL ||
        nmo_type_query_object_get_ancestor_state_by_guid(b->engine->types,object,CKPGUID_TARGETCAMERA) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b,0);
    OUT_ON(b,0);
    eng_vector_t move = {0,0,get_in_float(b,0,1)};
    eng_entity_translate(b->engine,entity,&move,entity,get_in_int(b,1,1) == 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_cameras_blocks[] = {
    {NMO_GUID_INIT(0xb98b83bb, 0x87787878), "Dolly", bb_dolly, NULL, NULL},
    {NMO_GUID_INIT(0x368f22b1, 0x222957e4), "Get Current Camera", bb_get_current_camera, NULL, NULL},
    {NMO_GUID_INIT(0xe444e666, 0x4eee6eee), "Set Projection", bb_set_projection, NULL, NULL},
    {NMO_GUID_INIT(0x0a125555, 0x0eee5488), "Orthographic Zoom", bb_orthographic_zoom, NULL, NULL},
    {NMO_GUID_INIT(0x40540a0d, 0x000aaaaa), "Set Zoom", bb_set_zoom, NULL, NULL},
    {NMO_GUID_INIT(0xaaaacccc, 0xaaaacccc), "Set FOV", bb_set_fov, NULL, NULL},
    {NMO_GUID_INIT(0x652316a2, 0x01aa09a9), "Set Clipping Planes", bb_set_clipping_planes, NULL, NULL},
    {NMO_GUID_INIT(0x368f0ab1, 0x2d8957e4), "Set As Active Camera", bb_set_as_active_camera, NULL, NULL},
};

nmo_status_t eng_register_cameras(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_cameras_blocks);
}
