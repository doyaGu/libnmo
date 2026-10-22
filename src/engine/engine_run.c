/**
 * @file engine_run.c
 * @brief Starting the engine, activating scripts, and running frames.
 */

#include "engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "../runtime/runtime_internal.h"

/* CKSceneObjectDesc::Init: a new scene object activates and resets at start. */
#define ENG_DEFAULT_SCENE_FLAGS (CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET)

/* The scene activity flags of a script: the ones the file records, else the default. */
static uint32_t eng_script_scene_flags(nmo_engine_t *engine, const eng_node_t *node)
{
    nmo_object_repository_t *repo = nmo_workspace_internal_repository(engine->workspace);
    nmo_object_t *object = repo != NULL ? nmo_object_repository_find_by_id(repo, node->id) : NULL;
    const nmo_behavior_state_t *state =
        object != NULL && nmo_object_get_class_id(object) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(object)
            : NULL;
    if (state != NULL && state->has_single_activity) {
        return state->single_activity_flags;
    }
    return ENG_DEFAULT_SCENE_FLAGS;
}

static void eng_list_object(nmo_engine_t *engine, int32_t object)
{
    eng_object_t *entry = &engine->objects.data[object];
    if (entry->listed) {
        return;
    }
    int32_t *slot = NULL;
    if (ENG_VEC_PUSH(engine->listed, slot)) {
        *slot = object;
        engine->objects.data[object].listed = true;
    }
}

nmo_status_t nmo_engine_start(nmo_engine_t *engine, nmo_engine_start_mode_t mode)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (engine->started) {
        return NMO_ERR_INVALID_STATE;
    }
    engine->started = true;
    eng_bind_blocks(engine);

    /* CKFile sends CKM_BEHAVIORLOAD after a load, CKContext::Reset CKM_BEHAVIORRESET */
    for (size_t i = 0; i < engine->nodes.count; i++) {
        if (engine->nodes.data[i].is_block) {
            eng_block_callback(engine, (int32_t)i, NMO_ENGINE_MSG_LOAD);
        }
    }
    eng_reset_messages(engine); /* CKMessageManager::OnCKReset */
    for (size_t i = 0; i < engine->nodes.count; i++) {
        if (engine->nodes.data[i].is_block) {
            eng_block_callback(engine, (int32_t)i, NMO_ENGINE_MSG_RESET);
        }
    }

    /*
     * CKLevel launches its scene with CK_SCENEOBJECTRESET_RESET: every script
     * resets, and the ones whose flags say so activate (CKScene::Init).
     */
    for (size_t o = 0; o < engine->objects.count; o++) {
        const eng_object_t *object = &engine->objects.data[o];
        bool any_active = false;
        for (uint32_t s = 0; s < object->script_count; s++) {
            int32_t script = *eng_index(engine, object->first_script + s);
            bool active = false;
            switch (mode) {
            case NMO_ENGINE_START_SCENE_DEFAULT:
                active = (eng_script_scene_flags(engine, &engine->nodes.data[script]) &
                          CK_SCENEOBJECT_START_ACTIVATE) != 0;
                break;
            case NMO_ENGINE_START_ALL:
                active = true;
                break;
            case NMO_ENGINE_START_NONE:
                break; /* reset all the same, so a later activation without reset starts it */
            }
            eng_behavior_activate(engine, script, active, true);
            if (active) {
                any_active = true;
                eng_emit(engine, NMO_ENGINE_EVENT_ACTIVATE, script, 0, 1);
            }
        }
        if (any_active) {
            eng_list_object(engine, (int32_t)o);
        }
    }
    eng_sort_listed(engine);
    return NMO_OK;
}

nmo_status_t nmo_engine_activate_script(nmo_engine_t *engine, nmo_object_id_t script_id,
                                        bool active, bool reset)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    int32_t index = eng_map_get(&engine->node_map, script_id);
    if (index < 0 || (engine->nodes.data[index].flags & CKBEHAVIOR_SCRIPT) == 0) {
        return NMO_ERR_NOT_FOUND;
    }
    eng_node_t *node = &engine->nodes.data[index];
    int32_t object = eng_map_get(&engine->object_map, node->owner);
    /* CKScene::ActivateScript in the current scene */
    if (active) {
        eng_object_next_frame(engine, object, 1);
        node->flags |= CKBEHAVIOR_ACTIVATENEXTFRAME;
        if (reset) {
            node->flags |= CKBEHAVIOR_RESETNEXTFRAME;
        }
        node->flags &= ~(uint32_t)CKBEHAVIOR_DEACTIVATENEXTFRAME;
    } else {
        node->flags |= CKBEHAVIOR_DEACTIVATENEXTFRAME;
        node->flags &= ~(uint32_t)(CKBEHAVIOR_ACTIVATENEXTFRAME | CKBEHAVIOR_RESETNEXTFRAME);
    }
    return NMO_OK;
}

nmo_status_t nmo_engine_step(nmo_engine_t *engine, float delta_ms)
{
    if (engine == NULL || delta_ms < 0.0f) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!engine->started) {
        return NMO_ERR_INVALID_STATE;
    }
    engine->delta = delta_ms;
    eng_emit(engine, NMO_ENGINE_EVENT_FRAME_BEGIN, -1, 0, 0);
    nmo_status_t status = eng_manager_execute(engine);
    if (status == NMO_OK) {
        eng_dispatch_messages(engine); /* CKMessageManager::PostProcess */
    }
    eng_emit(engine, NMO_ENGINE_EVENT_FRAME_END, -1, 0, 0);
    engine->frame++;
    engine->time += delta_ms;
    return status;
}
