/**
 * @file cameras.c
 * @brief Ports of the Cameras building blocks that keep state.
 *
 * Each function does what the block's function in Cameras.dll does (the
 * reference is the Ballanced source of the blocks).
 */

#include "../engine_block_util.h"

#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

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
    eng_render_t *render = &b->engine->render;
    eng_camera_planes_t *planes = NULL;
    for (size_t i = 0; i < render->camera_planes.count && planes == NULL; i++) {
        if (render->camera_planes.data[i].id == camera) {
            planes = &render->camera_planes.data[i];
        }
    }
    if (planes == NULL && !ENG_VEC_PUSH(render->camera_planes, planes)) {
        return NMO_ENGINE_BR_OK;
    }
    planes->id = camera;
    planes->near_plane = get_in_float(b, 0, 0.1f);
    planes->far_plane = get_in_float(b, 1, 200.0f);
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, camera, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_cameras_blocks[] = {
    {NMO_GUID(0x652316a2, 0x01aa09a9), "Set Clipping Planes", bb_set_clipping_planes, NULL, NULL},
    {NMO_GUID(0x368f0ab1, 0x2d8957e4), "Set As Active Camera", bb_set_as_active_camera, NULL, NULL},
};

nmo_status_t eng_register_cameras(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_cameras_blocks);
}
