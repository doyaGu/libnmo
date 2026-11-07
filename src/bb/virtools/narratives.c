/**
 * @file narratives.c
 * @brief Ports of the Narratives building blocks that start and stop scripts
 *        and objects, and create, load, copy and delete objects.
 *
 * Each function does what the block's function in Narratives.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Activating a script goes through nmo_engine_activate_script(),
 * which puts its owner in the behavior manager's list only when the owner is
 * active in the scene; the "Awake Object" setting activates an owner that is
 * not. TT Restore IC and TT Save IC (TT_Toolbox_RT.dll) are here with Restore
 * IC.
 */

#include "virtools.h"

#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"

#include <stdlib.h>
#include <string.h>

/* Sound Load's asynchronous load: the result it reports the frame after */
typedef struct sound_load_state {
    bool pending;
    bool loaded;
} sound_load_state_t;

/*
 * Sound Load (SoundLoader): the sound file into the target sound, which
 * stops (CKWaveSound::SetSoundFileName, then SetFileStreaming): Out, or
 * Failed when it is not a sound file. With "Use asynchonized loading" the
 * load thread's result comes on a later frame; the engine has it the next.
 */
static int bb_sound_load(blk_t *b)
{
    eng_sound_t *sound = eng_sound(b->engine, nmo_engine_block_target(b));
    if (sound == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    sound_load_state_t *state = (sound_load_state_t *)nmo_engine_block_state(b, sizeof(*state));
    if (state == NULL) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    int32_t async = 0;
    nmo_engine_block_get_local_value(b, 2, &async, sizeof(async));
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        size_t size = 0;
        const char *name = (const char *)nmo_engine_block_input_data(b, 0, &size);
        float length = name != NULL && size > 0 && memchr(name, '\0', size) != NULL
                           ? eng_wave_length(b->engine, name)
                           : -1.0f;
        eng_sound_stop(sound, 0.0f);
        if (length >= 0.0f) {
            sound->length = length;
            eng_sound_set_file(sound, name);
        }
        if (async == 0) {
            OUT_ON(b, length >= 0.0f ? 0 : 1);
            return NMO_ENGINE_BR_OK;
        }
        state->pending = true;
        state->loaded = length >= 0.0f;
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (nmo_engine_block_input_count(b) > 1 && IN_ACTIVE(b, 1)) {
        state->pending = false; /* the thread stopped */
        return NMO_ENGINE_BR_OK;
    }
    if (async != 0 && state->pending) {
        state->pending = false;
        OUT_ON(b, state->loaded ? 0 : 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * Texture Load (TextureLoader): the image file into a slot of the target
 * texture, which gets that many slots (CKBitmapData::LoadSlotImage); Error
 * when the file is not there or the texture plays a movie.
 */
static int bb_texture_load(blk_t *b)
{
    eng_texture_t *texture = eng_texture(b->engine, nmo_engine_block_target(b));
    if (texture == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (!IN_ACTIVE(b, 0)) {
        return NMO_ENGINE_BR_OK;
    }
    IN_OFF(b, 0);
    size_t size = 0;
    const char *name = (const char *)nmo_engine_block_input_data(b, 0, &size);
    int32_t slot = get_in_int(b, 1, 0);
    bool loaded = name != NULL && size > 1 && memchr(name, '\0', size) != NULL && name[0] != '\0' &&
                  slot >= 0 && !texture->movie && eng_texture_load(b->engine, texture, name, slot);
    if (!loaded) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_output_parameter_count(b) > 0) {
        nmo_object_id_t id = texture->id;
        nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * sprite Load (SpriteLoader, not in Ballance's Narratives.dll; ported from
 * the SDK's): the image file into a slot of the target sprite
 * (CKSprite::LoadImage), which takes the image's size, and on slot 0 shows
 * the whole of it (its source rect). Error when the file is not there or
 * cannot be read as an image.
 */
static int bb_sprite_load(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_texture_t *sprite = target != 0 && eng_object_class(b->engine, target) == NMO_CID_SPRITE
                                ? eng_texture(b->engine, target)
                                : NULL;
    if (sprite == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (!IN_ACTIVE(b, 0)) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    IN_OFF(b, 0);
    size_t size = 0;
    const char *name = (const char *)nmo_engine_block_input_data(b, 0, &size);
    int32_t slot = get_in_int(b, 1, 0);
    const eng_surface_t *image = NULL;
    if (name != NULL && size > 1 && memchr(name, '\0', size) != NULL && name[0] != '\0' && slot >= 0 &&
        !sprite->movie && eng_texture_load(b->engine, sprite, name, slot)) {
        image = eng_texture_surface(b->engine, sprite, slot);
    }
    if (image == NULL) {
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    float width = (float)image->width, height = (float)image->height;
    eng_2d_set_size(b->engine, target, width, height, false, false);
    eng_2d_t *entity = eng_2d_entity(b->engine, target);
    if (slot == 0 && entity != NULL) {
        entity->source = (nmo_rect_t){0.0f, 0.0f, width, height};
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Activate `script`, and with `awake` its owner when it is not active in the scene. */
static void activate_script(nmo_engine_t *engine, nmo_object_id_t script, bool reset, bool awake)
{
    nmo_engine_activate_script(engine, script, true, reset);
    int32_t node = eng_map_get(&engine->node_map, script);
    nmo_object_id_t owner = node >= 0 ? engine->nodes.data[node].owner : 0;
    if (awake && owner != 0 && !nmo_engine_object_is_active(engine, owner)) {
        nmo_engine_activate_object(engine, owner, true);
    }
}

static int bb_activate_script(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_BOOL)) {
        bool reset = get_in_int(b, 0, 1) != 0;
        int32_t awake = 1;
        nmo_engine_block_get_local_value(b, 0, &awake, sizeof(awake));
        for (int i = 1; i < nmo_engine_block_input_parameter_count(b); i++) {
            nmo_object_id_t script = nmo_engine_block_get_input_object(b, i);
            if (script != 0) {
                activate_script(engine, script, reset, awake != 0);
            }
        }
        return NMO_ENGINE_BR_OK;
    }
    /* The old version: the script, then "Reset ?" */
    nmo_object_id_t script = nmo_engine_block_get_input_object(b, 0);
    if (script == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    activate_script(engine, script, get_in_int(b, 1, 1) != 0, true);
    return NMO_ENGINE_BR_OK;
}

static int bb_deactivate_script(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    for (int i = 0; i < nmo_engine_block_input_parameter_count(b); i++) {
        nmo_object_id_t script = nmo_engine_block_get_input_object(b, i);
        if (script != 0) {
            nmo_engine_activate_script(engine, script, false, false);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* Activate the script, then stay active until it has finished. */
static int bb_execute_script(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t script = nmo_engine_block_get_input_object(b, 1);
    if (script == 0) {
        nmo_engine_block_activate_output(b, 0, true);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_is_input_active(b, 0)) {
        nmo_engine_block_activate_input(b, 0, false);
        nmo_engine_block_activate_output(b, 0, false);
        int32_t awake = 1;
        nmo_engine_block_get_local_value(b, 0, &awake, sizeof(awake));
        activate_script(engine, script, get_in_int(b, 0, 0) != 0, awake != 0);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (!nmo_engine_behavior_is_active(engine, script)) {
        nmo_engine_block_activate_output(b, 0, true);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Activate Object: the object in the scene, and with "Activate All Scripts" its scripts. */
static int bb_activate_object(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    nmo_object_id_t object = nmo_engine_block_get_input_object(b, 0);
    if (object == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    bool reset = get_in_int(b, 1, 0) != 0;
    nmo_engine_activate_object(engine, object, true);
    if (get_in_int(b, 2, 0) == 0) {
        return NMO_ENGINE_BR_OK;
    }
    bool reset_scripts = get_in_int(b, 3, reset ? 1 : 0) != 0; /* "Reset ?" before version 2 */
    int32_t index = eng_map_get(&engine->object_map, object);
    if (index < 0) {
        return NMO_ENGINE_BR_OK;
    }
    const eng_object_t *owner = &engine->objects.data[index];
    for (uint32_t s = 0; s < owner->script_count; s++) {
        int32_t script = *eng_index(engine, owner->first_script + s);
        nmo_engine_activate_script(engine, engine->nodes.data[script].id, true, reset_scripts);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_deactivate_object(blk_t *b)
{
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    nmo_object_id_t object = nmo_engine_block_get_input_object(b, 0);
    if (object == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_engine_activate_object(nmo_engine_block_engine(b), object, false);
    return NMO_ENGINE_BR_OK;
}

/*
 * Call Script: run a script now, within this block. With "Wait For
 * Completion?" until it ends (deactivating it after too many runs or an
 * infinite loop), else one frame of it; either way it ends inactive.
 */
static int bb_call_script(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_object_id_t script_id = nmo_engine_block_get_input_object(b, 1);
    int32_t script = script_id != 0 ? eng_map_get(&engine->node_map, script_id) : -1;
    if (script < 0 || (engine->nodes.data[script].flags & CKBEHAVIOR_SCRIPT) == 0) {
        nmo_engine_block_activate_output(b, 0, true);
        return NMO_ENGINE_BR_OK; /* In stays active, as in CK2 */
    }
    if (get_in_int(b, 0, 0) != 0) {
        eng_behavior_activate(engine, script, false, true);
    }
    if (get_in_int(b, 2, 1) != 0) {
        for (int32_t loop = 0;; loop++) {
            if (loop > engine->options.max_iterations) {
                eng_emit(engine, NMO_ENGINE_EVENT_INFINITE_LOOP, script, 0, loop);
                eng_behavior_activate(engine, script, false, false);
                break;
            }
            int result = eng_execute_behavior(engine, script);
            if (engine->failed || result == NMO_ENGINE_BR_INFINITELOOP) {
                eng_behavior_activate(engine, script, false, false);
                break;
            }
            if ((engine->nodes.data[script].flags & CKBEHAVIOR_ACTIVE) == 0) {
                break;
            }
        }
    } else {
        eng_execute_behavior(engine, script);
        eng_behavior_activate(engine, script, false, false);
    }
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    return NMO_ENGINE_BR_OK;
}

/* A 3D or 2D entity without a parent (the master object Object Load takes without a name). */
static bool is_root_entity(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = eng_find_object(engine, id);
    if (object == NULL) {
        return false;
    }
    const nmo_3dentity_state_t *entity3d = (const nmo_3dentity_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_3DENTITY);
    if (entity3d != NULL) {
        return nmo_ref_runtime_id(&entity3d->parent) == 0;
    }
    const nmo_2dentity_state_t *entity2d = (const nmo_2dentity_state_t *)
        nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_2DENTITY);
    return entity2d != NULL && (!entity2d->has_parent || nmo_ref_runtime_id(&entity2d->parent) == 0);
}

/* The Dynamic setting of Object Load, Object Copy and Object Create (local 0, TRUE by default) */
static bool dynamic_setting(blk_t *b)
{
    int32_t dynamic = 1;
    nmo_engine_block_get_local_value(b, 0, &dynamic, sizeof(dynamic));
    return dynamic != 0;
}

/*
 * Object Load (ObjectLoader): Load reads the file into the level, Loaded with
 * its objects (an object array output) and the master object (named, or the
 * last root entity of the filter class), or Failed. Unload destroys what the
 * last Load loaded. With Dynamic the objects load as dynamic objects.
 */
static int bb_object_load(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    if (nmo_engine_block_is_input_active(b, 0)) {
        nmo_engine_block_activate_input(b, 0, false);
        const char *name = input_string(b, 0);
        const char *master_name = input_string(b, 1);
        uint32_t filter = (uint32_t)get_in_int(b, 2, NMO_CID_3DOBJECT);
        int32_t file = -1;
        if (name == NULL || nmo_engine_load(engine, name, true, &file) != NMO_OK) {
            nmo_engine_block_activate_output(b, 2, true);
            return NMO_ENGINE_BR_OK;
        }
        nmo_engine_block_activate_output(b, 0, true);
        int32_t count = nmo_engine_loaded_object_count(engine, file);
        if (dynamic_setting(b)) {
            for (int32_t i = 0; i < count; i++) {
                eng_mark_dynamic(engine, nmo_engine_loaded_object(engine, file, i));
            }
        }
        uint32_t *ids = (uint32_t *)calloc(count > 0 ? (size_t)count : 1u, sizeof(uint32_t));
        if (ids == NULL) {
            return NMO_ENGINE_BR_GENERICERROR;
        }
        nmo_object_id_t master = 0;
        for (int32_t i = 0; i < count; i++) {
            nmo_object_id_t id = nmo_engine_loaded_object(engine, file, i);
            ids[i] = (uint32_t)id;
            uint32_t class_id = (uint32_t)nmo_engine_object_class(engine, id);
            if (class_id != filter &&
                !nmo_type_registry_is_class_derived_from(engine->types, class_id, filter)) {
                continue;
            }
            if (master_name != NULL && master_name[0] != '\0') {
                const char *object_name = nmo_engine_object_name(engine, id);
                if (object_name != NULL && strcmp(object_name, master_name) == 0) {
                    master = id;
                }
            } else if (is_root_entity(engine, id)) {
                master = id;
            }
        }
        nmo_engine_block_set_output_value(b, 0, ids, (size_t)(count > 0 ? count : 1) * sizeof(uint32_t));
        free(ids);
        uint32_t master_id = (uint32_t)master;
        nmo_engine_block_set_output_value(b, 1, &master_id, sizeof(master_id));
    }
    if (nmo_engine_block_is_input_active(b, 1)) {
        nmo_engine_block_activate_input(b, 1, false);
        nmo_engine_block_activate_output(b, 1, true);
        eng_param_t *loaded = block_output(b, 0);
        if (loaded != NULL && loaded->data != NULL) {
            for (uint32_t k = 0; k + sizeof(uint32_t) <= loaded->size; k += sizeof(uint32_t)) {
                uint32_t id = 0;
                memcpy(&id, loaded->data + k, sizeof(id));
                nmo_engine_destroy_object(engine, (nmo_object_id_t)id);
            }
            const uint32_t none = 0u;
            nmo_engine_block_set_output_value(b, 0, &none, sizeof(none));
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* Restore IC: the target's initial condition in the scene, if it has one. */
static int bb_restore_ic(blk_t *b)
{
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_restore_ic(b->engine, target);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* The classes TT Restore IC and TT Save IC take the hierarchy of (the 3D entity classes, as such) */
static bool tt_ic_hierarchy_class(uint32_t class_id)
{
    switch (class_id) {
    case NMO_CID_3DENTITY:
    case NMO_CID_CAMERA:
    case NMO_CID_TARGETCAMERA:
    case NMO_CID_CURVEPOINT:
    case NMO_CID_SPRITE3D:
    case NMO_CID_LIGHT:
    case NMO_CID_TARGETLIGHT:
    case NMO_CID_CHARACTER:
    case NMO_CID_3DOBJECT:
    case NMO_CID_BODYPART:
    case NMO_CID_CURVE: return true;
    default: return false;
    }
}

/* CK3dEntity::HierarchyParser: each descendant of the entity, depth first */
static void tt_ic_descendants(nmo_engine_t *engine, int32_t entity, bool save)
{
    for (int32_t c = engine->entities.data[entity].first_child; c >= 0;
         c = engine->entities.data[c].next_sibling) {
        nmo_object_id_t id = engine->entities.data[c].id;
        if (save) {
            if (eng_in_scene(engine, id)) {
                eng_save_ic(engine, id);
            }
        } else {
            eng_restore_ic(engine, id);
        }
        tt_ic_descendants(engine, c, save);
    }
}

/*
 * TT Restore IC and TT Save IC: the target's initial condition in the
 * scene restored, or saved from its state now; with Hierarchy, its
 * descendants' too (a 3D entity's).
 */
static int tt_ic(blk_t *b, bool save)
{
    nmo_engine_t *engine = b->engine;
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (!save) {
        eng_restore_ic(engine, target);
    } else if (eng_in_scene(engine, target)) {
        eng_save_ic(engine, target);
    }
    int32_t entity = eng_entity_index(engine, target);
    uint8_t hierarchy = 0;
    nmo_engine_block_get_input_value(b, 0, &hierarchy, sizeof(hierarchy));
    if (entity >= 0 && hierarchy != 0 &&
        tt_ic_hierarchy_class((uint32_t)nmo_engine_object_class(engine, target))) {
        tt_ic_descendants(engine, entity, save);
    }
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

static int bb_tt_restore_ic(blk_t *b)
{
    return tt_ic(b, false);
}

static int bb_tt_save_ic(blk_t *b)
{
    return tt_ic(b, true);
}

/*
 * Object Create (ObjectCreator): a new object of the class and name, added
 * to the level, in the Object output (a dynamic object with Dynamic);
 * nothing for a level.
 */
static int bb_object_create(blk_t *b)
{
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    uint32_t class_id = (uint32_t)get_in_int(b, 0, NMO_CID_3DENTITY);
    if (nmo_type_registry_is_class_derived_from(engine->types, class_id, NMO_CID_LEVEL)) {
        return NMO_ENGINE_BR_OK;
    }
    uint32_t id = (uint32_t)eng_create_object(engine, class_id, input_string(b, 1));
    if (dynamic_setting(b)) {
        eng_mark_dynamic(engine, id);
    }
    nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
    return NMO_ENGINE_BR_OK;
}

/*
 * Object Copy (ObjectCopier): copies of the Original inputs (no level), and
 * of what the Dependency Options copy with them, in the outputs in order;
 * the copies active with Awake Object (always before version 2), dynamic
 * objects with Dynamic.
 */
static int bb_object_copy(blk_t *b)
{
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    int count = nmo_engine_block_input_parameter_count(b);
    nmo_object_id_t *originals =
        count > 1 ? (nmo_object_id_t *)malloc((size_t)(count - 1) * sizeof(nmo_object_id_t)) : NULL;
    nmo_object_id_t *copies =
        count > 1 ? (nmo_object_id_t *)malloc((size_t)(count - 1) * sizeof(nmo_object_id_t)) : NULL;
    if (count > 1 && (originals == NULL || copies == NULL)) {
        free(originals);
        free(copies);
        return NMO_ENGINE_BR_GENERICERROR;
    }
    int32_t n = 0;
    for (int i = 1; i < count; i++) {
        nmo_object_id_t id = nmo_engine_block_get_input_object(b, i);
        int32_t class_id = id != 0 ? nmo_engine_object_class(engine, id) : 0;
        if (class_id == 0 ||
            nmo_type_registry_is_class_derived_from(engine->types, (uint32_t)class_id, NMO_CID_LEVEL)) {
            continue;
        }
        originals[n++] = id;
    }
    /* The options: the saved CKDependencies the input reads */
    eng_dependencies_t deps;
    const eng_node_t *node = &engine->nodes.data[b->node];
    eng_param_t *pin = eng_node_param(engine, node->first_pin, node->pin_count, 0);
    eng_param_t *source = pin != NULL ? eng_param_real_source(engine, pin) : NULL;
    eng_dependencies_from_param(engine, source != NULL ? source->id : 0, false, &deps);
    int32_t awake_setting = 1;
    nmo_engine_block_get_local_value(b, 1, &awake_setting, sizeof(awake_setting));
    bool awake = awake_setting != 0 || nmo_engine_block_version(b) < 0x00020000u;
    if (n > 0 && eng_copy_objects(engine, originals, n, &deps, awake, dynamic_setting(b), copies) ==
                     NMO_OK) {
        for (int32_t i = 0; i < n; i++) {
            uint32_t copy = (uint32_t)copies[i];
            nmo_engine_block_set_output_value(b, i, &copy, sizeof(copy));
        }
    }
    free(originals);
    free(copies);
    return NMO_ENGINE_BR_OK;
}

/*
 * Object Delete (ObjectDeleter): destroys the object (the target, before
 * version 2) with what its Dependency Options delete along.
 */
static int bb_object_delete(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    bool has_object = nmo_engine_block_input_parameter_count(b) > 1;
    nmo_object_id_t object = has_object ? nmo_engine_block_get_input_object(b, 0)
                                        : nmo_engine_block_target(b);
    if (object == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    const eng_node_t *node = &engine->nodes.data[b->node];
    eng_param_t *pin = eng_node_param(engine, node->first_pin, node->pin_count, has_object ? 1 : 0);
    eng_param_t *source = pin != NULL ? eng_param_real_source(engine, pin) : NULL;
    eng_dependencies_t deps;
    eng_dependencies_from_param(engine, source != NULL ? source->id : 0, true, &deps);
    eng_destroy_objects(engine, &object, 1, &deps);
    return NMO_ENGINE_BR_OK;
}

/*
 * Delete Dynamic Objects (DynamicDeleter): every dynamic object (one Object
 * Load, Object Copy or Object Create made with Dynamic) destroyed at the end
 * of the frame (CKContext::DestroyAllDynamicObjects).
 */
static int bb_delete_dynamic_objects(blk_t *b)
{
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    nmo_engine_block_engine(b)->dynamic.delete_all = true;
    return NMO_ENGINE_BR_OK;
}

/*
 * Activate Global State, Deactivate Global State and Is Global State Active:
 * CKStateObject::EnterState, LeaveState and IsStateActive. Without a state
 * object the DLL throws; the block stops with a parameter error.
 */
static eng_synchro_t *global_state(blk_t *b)
{
    eng_synchro_t *state = eng_synchro(b->engine, nmo_engine_block_get_input_object(b, 0));
    return state != NULL && state->is_state ? state : NULL;
}

static int set_global_state(blk_t *b, bool active)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_synchro_t *state = global_state(b);
    if (state == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    state->state_active = active;
    return NMO_ENGINE_BR_OK;
}

static int bb_activate_global_state(blk_t *b) { return set_global_state(b, true); }

static int bb_deactivate_global_state(blk_t *b) { return set_global_state(b, false); }

static int bb_is_global_state_active(blk_t *b)
{
    IN_OFF(b, 0);
    eng_synchro_t *state = global_state(b);
    if (state == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    OUT_ON(b, state->state_active ? 0 : 1);
    return NMO_ENGINE_BR_OK;
}

/* Set IC: the target's state now is its initial value in the current scene, when it is in it */
static int bb_set_ic(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (eng_in_scene(b->engine, target)) {
        eng_save_ic(b->engine, target);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Save State and Read State: CKSaveObjectState of the target into the
 * Object State output (the engine's state, by its number), and
 * CKReadObjectState of the Object State input onto the target.
 */
static int bb_save_state(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    uint32_t handle = (uint32_t)get_out_int(b, 0);
    handle = eng_save_state(b->engine, target, handle);
    set_out_int(b, 0, (int32_t)handle);
    return NMO_ENGINE_BR_OK;
}

static int bb_read_state(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    uint32_t handle = (uint32_t)get_in_int(b, 0, 0);
    if (handle != 0) {
        eng_read_state(b->engine, target, handle);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* Object Rename: CKObject::SetName */
static int bb_object_rename(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    nmo_object_t *object = target != 0 ? eng_find_object(b->engine, target) : NULL;
    if (object == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_set_name(object, input_string(b, 0));
    return NMO_ENGINE_BR_OK;
}

/*
 * Attach Script: a copy of Script (dynamic with Dynamic) moved to the
 * target, in Created Script; with Activate it is activated in the current
 * scene (reset with Reset), and the target too when it is not active.
 */
static int bb_attach_script(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool dynamic = get_in_int(b, 0, 1) != 0;
    nmo_object_id_t script = nmo_engine_block_get_input_object(b, 1);
    if (script == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    eng_dependencies_t deps;
    eng_default_copy_dependencies(&deps);
    nmo_object_id_t copy = 0;
    if (eng_copy_objects(engine, &script, 1, &deps, false, dynamic, &copy) != NMO_OK || copy == 0) {
        return NMO_ENGINE_BR_GENERICERROR;
    }
    nmo_engine_block_set_output_value(b, 0, &copy, sizeof(copy));
    eng_attach_script(engine, copy, target);
    if (get_in_int(b, 2, 1) != 0) {
        eng_add_to_level_scene(engine, copy);
        nmo_engine_activate_script(engine, copy, true, get_in_int(b, 3, 1) != 0);
        if (!nmo_engine_object_is_active(engine, target)) {
            nmo_engine_activate_object(engine, target, true);
        }
    }
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Dummy0: a prototype without inputs, outputs or function */
static int bb_dummy0(blk_t *b)
{
    (void)b;
    return NMO_ENGINE_BR_OK;
}

/*
 * The scene blocks. The engine runs the level scene only: a Scene input
 * naming another CKScene is one the engine never launches, and the blocks
 * leave it be.
 */

/* Get Current Scene: none while the level scene is the current one */
static int bb_get_current_scene(blk_t *b)
{
    nmo_object_id_t scene = 0;
    nmo_engine_block_set_output_value(b, 0, &scene, sizeof(scene));
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* The target and its scripts, in or out of the level scene (CKBeObject::AddToScene, RemoveFromScene) */
static void scene_membership(blk_t *b, nmo_object_id_t target, bool add)
{
    nmo_engine_t *engine = b->engine;
    int32_t object = eng_map_get(&engine->object_map, target);
    for (uint32_t s = 0; object >= 0 && s < engine->objects.data[object].script_count; s++) {
        int32_t node = engine->indexes.data[engine->objects.data[object].first_script + s];
        if (add) {
            eng_add_to_level_scene(engine, engine->nodes.data[node].id);
        } else {
            eng_remove_from_level_scene(engine, engine->nodes.data[node].id);
        }
    }
    if (add) {
        eng_add_to_level_scene(engine, target);
    } else {
        eng_remove_from_level_scene(engine, target);
        if (object >= 0) {
            nmo_engine_activate_object(engine, target, false); /* out of the behavior manager */
        }
    }
}

/* Add Object To Scene and Remove Object From Scene: the target, in the Scene (the current one) */
static int scene_object(blk_t *b, bool add)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target != 0 && nmo_engine_block_get_input_object(b, 0) == 0) {
        scene_membership(b, target, add);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_add_object_to_scene(blk_t *b) { return scene_object(b, true); }

static int bb_remove_object_from_scene(blk_t *b) { return scene_object(b, false); }

/*
 * Launch Scene: CKLevel::SetNextActiveScene of the Scene (the level scene
 * without one) with the Activity and Reset options, when it is not the
 * current scene or the reset option is Reset; an old block's boolean
 * activity resets as the scene says when TRUE, does nothing when FALSE.
 */
static int bb_launch_scene(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t activity = 0, reset = 0;
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 1), CKPGUID_BOOL)) {
        reset = get_in_int(b, 1, 0) != 0 ? 0 : 2;
    } else {
        activity = get_in_int(b, 1, 0);
        reset = get_in_int(b, 2, 0);
    }
    if (nmo_engine_block_get_input_object(b, 0) == 0 && reset == 1) {
        eng_launch_level_scene(b->engine, activity, reset);
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_narratives_blocks[] = {
    {NMO_GUID_INIT(0x02bc537c, 0x147c22e0), "Sound Load", bb_sound_load, NULL, NULL},
    {NMO_GUID_INIT(0x00e85ab1, 0x312a731c), "Texture Load", bb_texture_load, NULL, NULL},
    {NMO_GUID_INIT(0x00e85ae1, 0x317a771c), "sprite Load", bb_sprite_load, NULL, NULL},
    {NMO_GUID_INIT(0x4c7e7bc3, 0x0b693155), "Activate Script", bb_activate_script, NULL, NULL},
    {NMO_GUID_INIT(0x14367c05, 0x635b24f9), "Deactivate Script", bb_deactivate_script, NULL, NULL},
    {NMO_GUID_INIT(0x706c5a40, 0x5bb31a0b), "Execute Script", bb_execute_script, NULL, NULL},
    {NMO_GUID_INIT(0x5f003fba, 0x5e574f60), "Activate Object", bb_activate_object, NULL, NULL},
    {NMO_GUID_INIT(0x160f4b7d, 0x67de224e), "Deactivate Object", bb_deactivate_object, NULL, NULL},
    {NMO_GUID_INIT(0x1a7d5b17, 0x70630fed), "Call Script", bb_call_script, NULL, NULL},
    {NMO_GUID_INIT(0x7bd977d7, 0x26396c0c), "Object Load", bb_object_load, NULL, NULL},
    {NMO_GUID_INIT(0x766e4e44, 0x4fac6d52), "Restore IC", bb_restore_ic, NULL, NULL},
    {NMO_GUID_INIT(0x7e0f2f58, 0x232d3109), "TT Restore IC", bb_tt_restore_ic, NULL, NULL},
    {NMO_GUID_INIT(0x30362f34, 0x1935316f), "TT Save IC", bb_tt_save_ic, NULL, NULL},
    {NMO_GUID_INIT(0x271538e6, 0x2fae49ac), "Object Create", bb_object_create, NULL, NULL},
    {NMO_GUID_INIT(0x3f6b0ac7, 0x47d20f78), "Object Copy", bb_object_copy, NULL, NULL},
    {NMO_GUID_INIT(0x74120ded, 0x76524673), "Object Delete", bb_object_delete, NULL, NULL},
    {NMO_GUID_INIT(0x0cba3195, 0x53440e4c), "Delete Dynamic Objects", bb_delete_dynamic_objects, NULL,
     NULL},
    {NMO_GUID_INIT(0x373a615b, 0x769a6d53), "Activate Global State", bb_activate_global_state, NULL, NULL},
    {NMO_GUID_INIT(0x520d14a5, 0x797502ae), "Deactivate Global State", bb_deactivate_global_state, NULL,
     NULL},
    {NMO_GUID_INIT(0x04e65bdd, 0x473c3681), "Is Global State Active", bb_is_global_state_active, NULL, NULL},
    {NMO_GUID_INIT(0x72671a24, 0x161a2347), "Set IC", bb_set_ic, NULL, NULL},
    {NMO_GUID_INIT(0x200123d8, 0x65f802b6), "Save State", bb_save_state, NULL, NULL},
    {NMO_GUID_INIT(0x3ab6273f, 0x578f7118), "Read State", bb_read_state, NULL, NULL},
    {NMO_GUID_INIT(0x4cae18cc, 0x03520c5a), "Object Rename", bb_object_rename, NULL, NULL},
    {NMO_GUID_INIT(0x4ca718cc, 0x03520c5a), "Object Rename", bb_object_rename, NULL, NULL},
    {NMO_GUID_INIT(0x1dba5f4a, 0x14481212), "Attach Script", bb_attach_script, NULL, NULL},
    {NMO_GUID_INIT(0xd0b7adf3, 0xd3ff3cf6), "Dummy0", bb_dummy0, NULL, NULL},
    {NMO_GUID_INIT(0x00dc125f, 0x592b00a8), "Get Current Scene", bb_get_current_scene, NULL, NULL},
    {NMO_GUID_INIT(0x4f7d5221, 0x4c852520), "Add Object To Scene", bb_add_object_to_scene, NULL, NULL},
    {NMO_GUID_INIT(0x683925f3, 0x6fdd3f16), "Remove Object From Scene", bb_remove_object_from_scene, NULL,
     NULL},
    {NMO_GUID_INIT(0x188d6d43, 0x169613dd), "Launch Scene", bb_launch_scene, NULL, NULL},
};

nmo_status_t eng_register_narratives(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_narratives_blocks);
}
