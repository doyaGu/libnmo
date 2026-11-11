/**
 * @file run.c
 * @brief Starting the engine, activating scripts, and running frames.
 */

#include "../engine_internal.h"

#include "format/nmo_chunk_api.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_level_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "../../runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>

/* CKSceneObjectDesc::Init: a new scene object activates and resets at start. */
#define ENG_DEFAULT_SCENE_FLAGS (CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET)

/* The scene activity flags of a script: the ones the file records, else the default. */
static uint32_t eng_script_scene_flags(nmo_engine_t *engine, const eng_node_t *node)
{
    nmo_object_t *object = eng_find_object(engine, node->id);
    const nmo_behavior_state_t *state =
        object != NULL && nmo_object_get_class_id(object) == NMO_CID_BEHAVIOR
            ? (const nmo_behavior_state_t *)nmo_object_get_state(object)
            : NULL;
    if (state != NULL && state->has_single_activity) {
        return state->single_activity_flags;
    }
    return ENG_DEFAULT_SCENE_FLAGS;
}

/*
 * Whether an object is active in the scene at a level launch: its activity
 * flags say whether it starts active (by default it does). A level is always.
 */
static bool eng_object_starts_active(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = eng_find_object(engine, id);
    if (object == NULL || nmo_object_get_class_id(object) == NMO_CID_LEVEL) {
        return true;
    }
    const nmo_beobject_state_t *be = (const nmo_beobject_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_BEOBJECT);
    uint32_t flags = be != NULL && be->has_single_activity ? be->single_activity_flags
                                                           : ENG_DEFAULT_SCENE_FLAGS;
    return (flags & CK_SCENEOBJECT_START_ACTIVATE) != 0;
}

/* An object of the level scene and the flags it starts with. */
typedef struct eng_scene_entry {
    nmo_object_id_t id;
    uint32_t flags; /* CK_SCENEOBJECT_FLAGS, ACTIVE as the scene has it */
    bool has_ic;    /* the scene keeps an initial value for it */
} eng_scene_entry_t;

static int eng_scene_entry_compare(const void *a, const void *b)
{
    nmo_object_id_t x = ((const eng_scene_entry_t *)a)->id;
    nmo_object_id_t y = ((const eng_scene_entry_t *)b)->id;
    return x < y ? -1 : x > y;
}

/*
 * The objects of the level scene of the document (the scene CKLevel launches
 * by default), sorted by ID; false when the document has no level scene. Its
 * chunk holds runtime IDs (see nmo_level_state_t).
 */
static bool eng_level_scene(nmo_engine_t *engine, eng_scene_entry_t **out_entries,
                            size_t *out_count, nmo_status_t *out_status)
{
    *out_entries = NULL;
    *out_count = 0;
    *out_status = NMO_OK;
    const eng_module_t *module = &engine->modules.data[0];
    size_t count = 0;
    nmo_object_t **objects =
        module->repo != NULL ? nmo_object_repository_get_all(module->repo, &count) : NULL;
    nmo_chunk_t *chunk = NULL;
    for (size_t i = 0; objects != NULL && i < count && chunk == NULL; i++) {
        if (nmo_object_get_class_id(objects[i]) == NMO_CID_LEVEL) {
            const nmo_level_state_t *level = (const nmo_level_state_t *)nmo_object_get_state(objects[i]);
            chunk = level != NULL ? level->level_scene_chunk : NULL;
        }
    }
    nmo_scene_state_t scene;
    memset(&scene, 0, sizeof(scene));
    if (chunk == NULL) {
        return false;
    }
    *out_status = nmo_scene_vtable.create(&scene, NULL, NULL);
    if (*out_status != NMO_OK) return false;
    *out_status = nmo_chunk_start_read(chunk);
    if (*out_status == NMO_OK) *out_status = nmo_scene_deserialize(&scene, chunk, NULL, NULL);
    bool read = *out_status == NMO_OK;
    /* A level scene saved without the file's context names the objects by the IDs they were
       saved with (CKLevel::Load's remap); one saved with it has the document's already */
    bool own_ids = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) == 0;
    (void)nmo_chunk_start_read(chunk);
    eng_scene_entry_t *entries =
        read && scene.object_descs.count > 0
            ? (eng_scene_entry_t *)malloc(scene.object_descs.count * sizeof(*entries))
            : NULL;
    if (entries != NULL) {
        const nmo_scene_object_desc_t *descs = (const nmo_scene_object_desc_t *)scene.object_descs.data;
        for (size_t i = 0; i < scene.object_descs.count; i++) {
            entries[i].id = own_ids ? eng_module_file_object(module, descs[i].ref.raw_id) : descs[i].ref.raw_id;
            entries[i].flags = descs[i].flags;
            entries[i].has_ic = descs[i].initial_value != NULL;
            if (entries[i].has_ic) {
                *out_status = eng_load_ic(engine, entries[i].id, descs[i].initial_value);
                if (*out_status != NMO_OK) break;
            }
        }
        *out_count = scene.object_descs.count;
        if (*out_status == NMO_OK) qsort(entries, *out_count, sizeof(*entries), eng_scene_entry_compare);
    } else if (read && scene.object_descs.count > 0) {
        *out_status = NMO_ERR_NOMEM;
    }
    if (read && *out_status == NMO_OK) {
        nmo_object_id_t background = scene.background_texture.raw_id, camera = scene.starting_camera.raw_id;
        if (own_ids) {
            background = eng_module_file_object(module, background);
            camera = eng_module_file_object(module, camera);
        }
        eng_render_apply_scene(engine, scene.background_color, background,
                               scene.ambient_light_color, (int32_t)scene.fog_mode,
                               scene.fog_color, scene.fog_start, scene.fog_end,
                               scene.fog_density, camera);
    }
    nmo_scene_vtable.destroy(&scene, NULL, NULL);
    *out_entries = entries;
    return read;
}

/* Whether the level scene starts an object or script active (CKScene::Init). */
static bool eng_scene_starts_active(const eng_scene_entry_t *entries, size_t count,
                                    nmo_object_id_t id)
{
    eng_scene_entry_t key = {id, 0u, false};
    const eng_scene_entry_t *entry = entries != NULL
        ? (const eng_scene_entry_t *)bsearch(&key, entries, count, sizeof(*entries), eng_scene_entry_compare)
        : NULL;
    return entry != NULL && (entry->flags & CK_SCENEOBJECT_START_ACTIVATE) != 0;
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

static void eng_warn_new_scene(nmo_engine_t *engine);

nmo_status_t nmo_engine_start(nmo_engine_t *engine, nmo_engine_start_mode_t mode)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (engine->started) {
        return NMO_ERR_INVALID_STATE;
    }
    engine->started = true;
    eng_bind_blocks(engine, 0);

    /* CKFile sends CKM_BEHAVIORLOAD after a load, CKContext::Reset CKM_BEHAVIORRESET */
    for (size_t i = 0; i < engine->nodes.count; i++) {
        if (engine->nodes.data[i].is_block) {
            eng_block_callback(engine, (int32_t)i, NMO_ENGINE_MSG_LOAD);
        }
    }
    eng_reset_messages(engine); /* CKMessageManager::OnCKReset */
    eng_managers_reset(engine);
    eng_midi_reset(engine); /* MidiManager::OnCKReset */
    for (size_t i = 0; i < engine->nodes.count; i++) {
        if (engine->nodes.data[i].is_block) {
            eng_block_callback(engine, (int32_t)i, NMO_ENGINE_MSG_RESET);
        }
    }

    /*
     * CKLevel launches its scene with CK_SCENEOBJECTRESET_RESET: every script
     * resets, and the ones whose flags in the level scene say so activate
     * (CKScene::Init); what is not in the scene stays inactive. A document
     * without a level scene uses the flags its objects were saved with.
     */
    eng_scene_entry_t *scene = NULL;
    size_t scene_count = 0;
    nmo_status_t scene_status = NMO_OK;
    engine->has_level_scene = eng_level_scene(engine, &scene, &scene_count, &scene_status);
    if (scene_status != NMO_OK) {
        free(scene);
        engine->started = false;
        return scene_status;
    }
    for (size_t i = 0; i < scene_count; i++) {
        eng_add_to_level_scene(engine, scene[i].id);
        if (scene[i].has_ic) {
            eng_restore_ic(engine, scene[i].id);
        }
    }
    bool has_scene = mode == NMO_ENGINE_START_SCENE_DEFAULT && engine->has_level_scene;
    for (size_t i = 0; i < engine->entities.count; i++) {
        eng_entity_t *entity = &engine->entities.data[i];
        entity->scene_active = mode != NMO_ENGINE_START_SCENE_DEFAULT ||
            (has_scene ? eng_scene_starts_active(scene,scene_count,entity->id) : eng_object_starts_active(engine,entity->id));
    }
    for (size_t o = 0; o < engine->objects.count; o++) {
        eng_object_t *object = &engine->objects.data[o];
        object->active = mode != NMO_ENGINE_START_SCENE_DEFAULT ||
                         (has_scene ? eng_scene_starts_active(scene, scene_count, object->id) ||
                                          eng_find_object(engine, object->id) == NULL ||
                                          nmo_object_get_class_id(eng_find_object(engine, object->id)) == NMO_CID_LEVEL
                                    : eng_object_starts_active(engine, object->id));
        bool any_active = false;
        for (uint32_t s = 0; s < object->script_count; s++) {
            int32_t script = *eng_index(engine, object->first_script + s);
            bool active = false;
            switch (mode) {
            case NMO_ENGINE_START_SCENE_DEFAULT:
                active = has_scene
                             ? eng_scene_starts_active(scene, scene_count,
                                                       engine->nodes.data[script].id)
                             : (eng_script_scene_flags(engine, &engine->nodes.data[script]) &
                                CK_SCENEOBJECT_START_ACTIVATE) != 0;
                break;
            case NMO_ENGINE_START_ALL:
                active = true;
                break;
            case NMO_ENGINE_START_NONE:
                break; /* reset all the same, so a later activation without reset starts it */
            }
            eng_behavior_activate(engine, script, active, true);
            engine->nodes.data[script].scene_active = active; /* CKScene::Launch: no callback */
            if (active) {
                any_active = true;
                eng_emit(engine, NMO_ENGINE_EVENT_ACTIVATE, script, 0, 1);
            }
        }
        if ((any_active || eng_character_automatic(engine, object->id)) && object->active) {
            eng_list_object(engine, (int32_t)o);
        }
    }
    for (size_t i = 0; i < scene_count; i++) {
        /* CKScene::Init's defaults: the objects that start active are */
        if (scene[i].flags & CK_SCENEOBJECT_START_ACTIVATE) {
            scene[i].flags |= CK_SCENEOBJECT_ACTIVE;
        }
    }
    free(engine->scene_entries);
    engine->scene_entries = scene; /* kept for a launch of the level scene */
    engine->scene_entry_count = scene_count;
    eng_sort_listed(engine);
    eng_warn_new_scene(engine);
    eng_midi_play(engine); /* MidiManager::OnCKPlay */
    return NMO_OK;
}

/* CKContext::WarnAllBehaviors(CKM_BEHAVIORNEWSCENE), as CKLevel::LaunchScene ends: every block
   that has an owner hears of the scene */
static void eng_warn_new_scene(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->nodes.count; i++) {
        if (engine->nodes.data[i].is_block && engine->nodes.data[i].owner != 0) {
            eng_block_callback(engine, (int32_t)i, NMO_ENGINE_MSG_NEWSCENE);
        }
    }
}

void eng_launch_level_scene(nmo_engine_t *engine, int32_t activity, int32_t reset)
{
    engine->scene_launch = true;
    engine->scene_launch_activity = activity;
    engine->scene_launch_reset = reset;
}

/*
 * CKLevel::LaunchScene of the level scene (CKScene::Init): the behavior
 * manager empties; each object of the scene resets to its initial value and
 * becomes active or not as the options say (the scene's flags for their
 * defaults); a script activates or deactivates so, keeping its activity
 * with Do Nothing; the active objects with scripts are processed again.
 */
static void eng_relaunch_level_scene(nmo_engine_t *engine)
{
    engine->scene_launch = false;
    for (size_t k = 0; k < engine->listed.count; k++) {
        engine->objects.data[engine->listed.data[k]].listed = false;
    }
    engine->listed.count = 0;
    for (size_t i = 0; i < engine->scene_entry_count; i++) {
        eng_scene_entry_t *desc = &engine->scene_entries[i];
        if (!eng_in_scene(engine, desc->id)) {
            continue;
        }
        bool active = engine->scene_launch_activity == 1; /* CK_SCENEOBJECTACTIVITY_ACTIVATE */
        bool nothing = engine->scene_launch_activity == 3;
        bool reset = engine->scene_launch_reset == 1; /* CK_SCENEOBJECTRESET_RESET */
        if (engine->scene_launch_reset == 0) {
            reset = (desc->flags & CK_SCENEOBJECT_START_RESET) != 0;
        }
        if (engine->scene_launch_activity == 0) {
            active = (desc->flags & CK_SCENEOBJECT_START_ACTIVATE) != 0;
            nothing = (desc->flags & CK_SCENEOBJECT_START_LEAVE) != 0;
        }
        if (reset && desc->has_ic) {
            eng_restore_ic(engine, desc->id);
        }
        if (!nothing) {
            desc->flags = active ? desc->flags | CK_SCENEOBJECT_ACTIVE : desc->flags & ~(uint32_t)CK_SCENEOBJECT_ACTIVE;
        }
        bool now_active = (desc->flags & CK_SCENEOBJECT_ACTIVE) != 0;
        int32_t node = eng_map_get(&engine->node_map, desc->id);
        if (node >= 0 && (engine->nodes.data[node].flags & CKBEHAVIOR_SCRIPT) != 0) {
            if (nothing) {
                reset = false;
                now_active = nmo_engine_behavior_is_active(engine, desc->id);
            }
            eng_behavior_activate(engine, node, now_active, reset);
            engine->nodes.data[node].scene_active = now_active;
            continue;
        }
        int32_t entity = eng_entity_index(engine, desc->id);
        if (entity >= 0) {
            engine->entities.data[entity].scene_active = now_active;
        }
        int32_t object = eng_map_get(&engine->object_map, desc->id);
        if (object >= 0) {
            engine->objects.data[object].active = now_active;
            if (now_active && engine->objects.data[object].script_count > 0) {
                eng_list_object(engine, object);
            }
        }
    }
    eng_sort_listed(engine);
    eng_warn_new_scene(engine);
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
    /*
     * CKScene::ActivateScript in the current scene: its owner joins if active
     * there; the script's blocks hear of it when its activity in the scene changes.
     */
    if (active) {
        if (object >= 0 && engine->objects.data[object].active &&
            !engine->objects.data[object].destroyed) {
            eng_object_next_frame(engine, object, 1);
        }
        node->flags |= CKBEHAVIOR_ACTIVATENEXTFRAME;
        if (reset) {
            node->flags |= CKBEHAVIOR_RESETNEXTFRAME;
        }
        node->flags &= ~(uint32_t)CKBEHAVIOR_DEACTIVATENEXTFRAME;
    } else {
        node->flags |= CKBEHAVIOR_DEACTIVATENEXTFRAME;
        node->flags &= ~(uint32_t)(CKBEHAVIOR_ACTIVATENEXTFRAME | CKBEHAVIOR_RESETNEXTFRAME);
    }
    if (node->scene_active != active) {
        node->scene_active = active;
        eng_graph_callback(engine, index,
                           active ? NMO_ENGINE_MSG_ACTIVATESCRIPT : NMO_ENGINE_MSG_DEACTIVATESCRIPT);
    }
    return NMO_OK;
}

nmo_status_t nmo_engine_activate_object(nmo_engine_t *engine, nmo_object_id_t object_id,
                                        bool active)
{
    if (engine == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    int32_t entity = eng_entity_index(engine,object_id);
    if (entity >= 0) engine->entities.data[entity].scene_active = active;
    int32_t index = eng_map_get(&engine->object_map, object_id);
    if (index < 0) {
        return NMO_OK; /* An entity without scripts still has scene activity. */
    }
    /* CKScene::ActivateBeObject in the current scene: its scripts' blocks hear of it */
    eng_object_t *object = &engine->objects.data[index];
    if (object->active != active && !object->destroyed) {
        object->active = active;
        eng_object_next_frame(engine, index, active ? 1 : -1);
        for (uint32_t s = 0; s < engine->objects.data[index].script_count; s++) {
            int32_t script = *eng_index(engine, engine->objects.data[index].first_script + s);
            eng_graph_callback(engine, script,
                               active ? NMO_ENGINE_MSG_ACTIVATESCRIPT
                                      : NMO_ENGINE_MSG_DEACTIVATESCRIPT);
        }
    }
    return NMO_OK;
}

bool nmo_engine_object_is_active(const nmo_engine_t *engine, nmo_object_id_t object_id)
{
    if (engine == NULL) {
        return false;
    }
    int32_t index = eng_map_get(&engine->object_map, object_id);
    if (index >= 0) return engine->objects.data[index].active;
    int32_t entity = eng_entity_index(engine,object_id);
    return entity < 0 || engine->entities.data[entity].scene_active;
}

/* The activity flags an object or script was saved with; false when it has none. */
static bool eng_saved_activity(nmo_engine_t *engine, nmo_object_id_t id, uint32_t *out_flags)
{
    nmo_object_t *object = eng_find_object(engine, id);
    if (object == NULL) {
        return false;
    }
    if (nmo_object_get_class_id(object) == NMO_CID_BEHAVIOR) {
        const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)nmo_object_get_state(object);
        *out_flags = state != NULL ? state->single_activity_flags : 0u;
        return state != NULL && state->has_single_activity;
    }
    const nmo_beobject_state_t *be = (const nmo_beobject_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_BEOBJECT);
    *out_flags = be != NULL ? be->single_activity_flags : 0u;
    return be != NULL && be->has_single_activity;
}

/*
 * CKScene::AddObject in the level scene: an object (or script) saved with
 * activity flags activates when they say START_ACTIVATE, a reset with
 * START_RESET, or deactivates with START_DEACTIVATE.
 */
static void eng_add_to_scene(nmo_engine_t *engine, nmo_object_id_t id, bool script)
{
    uint32_t flags = 0;
    if (!eng_saved_activity(engine, id, &flags)) {
        return;
    }
    bool reset = (flags & CK_SCENEOBJECT_START_RESET) != 0;
    if (flags & CK_SCENEOBJECT_START_ACTIVATE) {
        if (script) {
            nmo_engine_activate_script(engine, id, true, reset);
        } else {
            nmo_engine_activate_object(engine, id, true);
        }
    } else if (flags & CK_SCENEOBJECT_START_DEACTIVATE) {
        if (script) {
            nmo_engine_activate_script(engine, id, false, false);
        } else {
            nmo_engine_activate_object(engine, id, false);
        }
    }
}

bool eng_in_scene(const nmo_engine_t *engine, nmo_object_id_t id)
{
    return !engine->has_level_scene || eng_map_get(&engine->scene_objects, id) >= 0;
}

bool nmo_engine_parent_script_is_active(const nmo_engine_t *engine, nmo_object_id_t behavior_id)
{
    int32_t index = engine != NULL ? eng_map_get(&engine->node_map, behavior_id) : -1;
    const eng_node_t *node = index >= 0 ? &engine->nodes.data[index] : NULL;
    if (node == NULL || node->owner == 0) {
        return false;
    }
    nmo_object_t *owner = eng_find_object(engine, node->owner);
    if (owner != NULL && nmo_object_get_class_id(owner) == NMO_CID_LEVEL) {
        return true;
    }
    if (!eng_in_scene(engine, node->owner)) {
        return false;
    }
    while (node->parent >= 0) {
        node = &engine->nodes.data[node->parent];
    }
    return node->scene_active;
}

void eng_remove_from_level_scene(nmo_engine_t *engine, nmo_object_id_t id)
{
    if (id != 0 && eng_map_get(&engine->scene_objects, id) >= 0) {
        eng_map_put(&engine->scene_objects, id, -1);
    }
}

/* CKLevel::AddObject: the object is in the level scene from now on. */
void eng_add_to_level_scene(nmo_engine_t *engine, nmo_object_id_t id)
{
    if (id != 0 && eng_map_get(&engine->scene_objects, id) < 0) {
        eng_map_put(&engine->scene_objects, id, 1);
    }
}

void eng_add_module_to_level(nmo_engine_t *engine, int32_t module)
{
    /* CKBeObject::AddToScene: the object, then its scripts */
    int32_t count = nmo_engine_loaded_object_count(engine, module);
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t id = nmo_engine_loaded_object(engine, module, i);
        eng_add_to_level_scene(engine, id);
        /* CKScene::AddObject: an object saved with an initial value keeps its state now as one */
        uint32_t flags = 0;
        if (eng_saved_activity(engine, id, &flags) && (flags & CK_SCENEOBJECT_INTERNAL_IC) != 0) {
            eng_save_ic(engine, id);
        }
        eng_add_to_scene(engine, id, false);
        int32_t index = eng_map_get(&engine->object_map, id);
        if (index < 0) {
            continue; /* no script: nothing for the behavior manager */
        }
        const eng_object_t *object = &engine->objects.data[index];
        for (uint32_t s = 0; s < object->script_count; s++) {
            int32_t script = *eng_index(engine, object->first_script + s);
            eng_add_to_scene(engine, engine->nodes.data[script].id, true);
        }
    }
}

void nmo_engine_destroy_object(nmo_engine_t *engine, nmo_object_id_t id)
{
    if (engine == NULL) {
        return;
    }
    if (engine->dynamic.defer) {
        nmo_object_id_t *slot = NULL;
        if (id != 0 && ENG_VEC_PUSH(engine->dynamic.deferred, slot)) {
            *slot = id;
        }
        return;
    }
    eng_camera_forget_object(engine,id);
    if (id != 0) {
        eng_map_put(&engine->destroyed, id, 1);
    }
    eng_curve_forget_object(engine, id);
    eng_portals_forget_object(engine, id);
    eng_entity_destroy(engine, id);
    eng_2d_destroy(engine, id);
    eng_forget_object(engine, id);
    int32_t index = eng_map_get(&engine->object_map, id);
    if (index >= 0) {
        eng_object_t *object = &engine->objects.data[index];
        if (object->listed) {
            eng_object_next_frame(engine, index, -1);
        }
        object->active = false;
        object->destroyed = true;
        for (uint32_t s = 0; s < object->script_count; s++) {
            int32_t script = *eng_index(engine, object->first_script + s);
            eng_behavior_activate(engine, script, false, false);
            engine->nodes.data[script].flags &= ~(uint32_t)ENG_NEXT_FRAME_FLAGS;
        }
        return;
    }
    int32_t node = eng_map_get(&engine->node_map, id);
    if (node >= 0 && (engine->nodes.data[node].flags & CKBEHAVIOR_SCRIPT) != 0) {
        eng_behavior_activate(engine, node, false, false);
        engine->nodes.data[node].flags &= ~(uint32_t)ENG_NEXT_FRAME_FLAGS;
    }
}

/*
 * CKObjectManager::PostProcess: the objects behaviors destroyed this frame,
 * then the dynamic objects, when Delete Dynamic Objects asked for it.
 */
static void eng_delete_dynamic_objects(nmo_engine_t *engine)
{
    nmo_object_id_t *deferred = engine->dynamic.deferred.data;
    size_t deferred_count = engine->dynamic.deferred.count;
    engine->dynamic.deferred.data = NULL;
    engine->dynamic.deferred.count = engine->dynamic.deferred.capacity = 0;
    for (size_t i = 0; i < deferred_count; i++) {
        if (eng_map_get(&engine->destroyed, deferred[i]) < 0) {
            nmo_engine_destroy_object(engine, deferred[i]);
        }
    }
    free(deferred);
    if (!engine->dynamic.delete_all) {
        return;
    }
    engine->dynamic.delete_all = false;
    nmo_object_id_t *ids = engine->dynamic.objects.data;
    size_t count = engine->dynamic.objects.count;
    engine->dynamic.objects.data = NULL;
    engine->dynamic.objects.count = engine->dynamic.objects.capacity = 0;
    for (size_t i = 0; i < count; i++) {
        if (eng_map_get(&engine->destroyed, ids[i]) < 0) {
            nmo_engine_destroy_object(engine, ids[i]);
        }
    }
    free(ids);
}

bool nmo_engine_exit_requested(const nmo_engine_t *engine)
{
    return engine != NULL && engine->host.exit_requested;
}

nmo_status_t nmo_engine_step(nmo_engine_t *engine, float delta_ms)
{
    if (engine == NULL || delta_ms < 0.0f) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!engine->started) {
        return NMO_ERR_INVALID_STATE;
    }
    if (engine->failed) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    engine->delta = delta_ms;
    /* CKTimeManager::PreProcess: the last frame's statistics become the profile's */
    engine->profile.last = engine->profile.frame;
    engine->profile.last.frame_time = delta_ms;
    memset(&engine->profile.frame, 0, sizeof(engine->profile.frame));
    eng_input_begin_frame(engine);
    eng_clear_player_messages(engine);
    eng_sounds_begin_frame(engine);
    eng_midi_sounds_begin_frame(engine);
    if (engine->scene_launch) {
        eng_relaunch_level_scene(engine); /* CKLevel::PreProcess */
    }
    eng_entities_save_last_frame(engine); /* CKRenderManager::PreProcess */
    eng_managers_pre_process(engine);
    eng_midi_pre_process(engine); /* MidiManager::PreProcess */
    eng_emit(engine, NMO_ENGINE_EVENT_FRAME_BEGIN, -1, 0, 0);
    engine->dynamic.defer = true;
    nmo_status_t status = engine->failed ? NMO_ERR_VALIDATION_FAILED : eng_manager_execute(engine);
    engine->dynamic.defer = false;
    if (status == NMO_OK) {
        eng_dispatch_messages(engine); /* CKMessageManager::PostProcess */
        eng_managers_post_process(engine); /* the packs' managers' PostProcess */
        eng_sounds_end_frame(engine); /* DX7SoundManager::PostProcess */
        eng_midi_post_process(engine); /* MidiManager::PostProcess */
        eng_delete_dynamic_objects(engine); /* CKObjectManager::PostProcess */
        if (!engine->failed) eng_animations_end_frame(engine);
    }
    if (engine->failed) {
        status = NMO_ERR_VALIDATION_FAILED;
    }
    eng_emit(engine, NMO_ENGINE_EVENT_FRAME_END, -1, 0, (int32_t)status);
    engine->frame++;
    engine->time += delta_ms;
    return status;
}
