/**
 * @file engine_narratives.c
 * @brief Ports of the Narratives building blocks that start and stop scripts
 *        and objects, and create, load, copy and delete objects.
 *
 * Each function does what the block's function in Narratives.dll does (the
 * reference is the Ballanced source of the blocks). Activating a script
 * goes through nmo_engine_activate_script(), which puts its owner in the
 * behavior manager's list only when the owner is active in the scene; the
 * "Awake Object" setting activates an owner that is not.
 */

#include "engine_internal.h"

#include "format/nmo_object.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_ref.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_type_query.h"

#include <stdlib.h>
#include <string.h>

typedef nmo_engine_block_t blk_t;

static int32_t get_in_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
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
                eng_behavior_activate(engine, script, false, false);
                break;
            }
            if (eng_execute_behavior(engine, script) == NMO_ENGINE_BR_INFINITELOOP) {
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

/* The string an input reads, or NULL when it reads none. */
static const char *input_string(blk_t *b, int pos)
{
    size_t size = 0;
    const char *text = (const char *)nmo_engine_block_input_data(b, pos, &size);
    return text != NULL && size > 0 && memchr(text, '\0', size) != NULL ? text : NULL;
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

static eng_param_t *block_output(blk_t *b, int pos)
{
    const eng_node_t *node = &b->engine->nodes.data[b->node];
    return eng_node_param(b->engine, node->first_pout, node->pout_count, pos);
}

/*
 * Object Load (ObjectLoader): Load reads the file into the level, Loaded with
 * its objects (an object array output) and the master object (named, or the
 * last root entity of the filter class), or Failed. Unload destroys what the
 * last Load loaded.
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

/* ============================================================================
 * Initial conditions (CKScene::SetObjectInitialValue, GetObjectInitialValue)
 * ============================================================================ */

/* CKSaveObjectState into the level scene's initial value: what the engine models of the object */
void eng_save_ic(nmo_engine_t *engine, nmo_object_id_t id)
{
    const eng_array_t *array = eng_find_array(engine, id);
    float world[16];
    bool has_matrix = nmo_engine_entity_world_matrix(engine, id, world);
    if (array == NULL && !has_matrix) {
        return;
    }
    int32_t index = eng_map_get(&engine->ic_map, id);
    eng_ic_t *ic = NULL;
    if (index >= 0) {
        ic = &engine->ics.data[index];
        if (ic->has_array) {
            eng_array_release(&ic->array);
        }
    } else if (ENG_VEC_PUSH(engine->ics, ic)) {
        if (!eng_map_put(&engine->ic_map, id, (int32_t)(engine->ics.count - 1u))) {
            engine->ics.count--;
            return;
        }
    } else {
        return;
    }
    memset(ic, 0, sizeof(*ic));
    ic->id = id;
    ic->has_array = array != NULL && eng_array_clone(&ic->array, array);
    ic->has_matrix = has_matrix;
    if (has_matrix) {
        memcpy(ic->world, world, sizeof(world));
    }
}

/* CKReadObjectState of the object's initial value in the level scene, when it is in it and has one */
static void restore_ic(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t index = eng_in_scene(engine, id) ? eng_map_get(&engine->ic_map, id) : -1;
    if (index < 0) {
        return;
    }
    const eng_ic_t *ic = &engine->ics.data[index];
    eng_array_t *array = ic->has_array ? eng_find_array(engine, id) : NULL;
    eng_array_t copy;
    memset(&copy, 0, sizeof(copy));
    if (array != NULL && eng_array_clone(&copy, &ic->array)) {
        eng_array_release(array);
        copy.id = array->id;
        *array = copy;
    }
    if (ic->has_matrix) {
        nmo_engine_set_entity_world_matrix(engine, id, ic->world, true);
    }
}

void eng_free_ics(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->ics.count; i++) {
        if (engine->ics.data[i].has_array) {
            eng_array_release(&engine->ics.data[i].array);
        }
    }
    free(engine->ics.data);
    free(engine->ic_map.slots);
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
    restore_ic(b->engine, target);
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
            restore_ic(engine, id);
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
        restore_ic(engine, target);
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
 * to the level, in the Object output; nothing for a level.
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
    nmo_engine_block_set_output_value(b, 0, &id, sizeof(id));
    return NMO_ENGINE_BR_OK;
}

/*
 * Object Copy (ObjectCopier): copies of the Original inputs (no level), and
 * of what the Dependency Options copy with them, in the outputs in order;
 * the copies active with Awake Object (always before version 2).
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
    if (n > 0 && eng_copy_objects(engine, originals, n, &deps, awake, copies) == NMO_OK) {
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

static const nmo_engine_block_desc_t eng_narratives_blocks[] = {
    {NMO_GUID(0x4c7e7bc3, 0x0b693155), "Activate Script", bb_activate_script, NULL, NULL},
    {NMO_GUID(0x14367c05, 0x635b24f9), "Deactivate Script", bb_deactivate_script, NULL, NULL},
    {NMO_GUID(0x706c5a40, 0x5bb31a0b), "Execute Script", bb_execute_script, NULL, NULL},
    {NMO_GUID(0x5f003fba, 0x5e574f60), "Activate Object", bb_activate_object, NULL, NULL},
    {NMO_GUID(0x160f4b7d, 0x67de224e), "Deactivate Object", bb_deactivate_object, NULL, NULL},
    {NMO_GUID(0x1a7d5b17, 0x70630fed), "Call Script", bb_call_script, NULL, NULL},
    {NMO_GUID(0x7bd977d7, 0x26396c0c), "Object Load", bb_object_load, NULL, NULL},
    {NMO_GUID(0x766e4e44, 0x4fac6d52), "Restore IC", bb_restore_ic, NULL, NULL},
    {NMO_GUID(0x7e0f2f58, 0x232d3109), "TT Restore IC", bb_tt_restore_ic, NULL, NULL},
    {NMO_GUID(0x30362f34, 0x1935316f), "TT Save IC", bb_tt_save_ic, NULL, NULL},
    {NMO_GUID(0x271538e6, 0x2fae49ac), "Object Create", bb_object_create, NULL, NULL},
    {NMO_GUID(0x3f6b0ac7, 0x47d20f78), "Object Copy", bb_object_copy, NULL, NULL},
    {NMO_GUID(0x74120ded, 0x76524673), "Object Delete", bb_object_delete, NULL, NULL},
};

nmo_status_t eng_register_narratives(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_narratives_blocks) / sizeof(eng_narratives_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_narratives_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
