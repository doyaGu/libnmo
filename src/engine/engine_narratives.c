/**
 * @file engine_narratives.c
 * @brief Ports of the Narratives building blocks that start and stop scripts
 *        and objects, and load and delete objects.
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

/* Object Delete (ObjectDeleter): destroys the object (the target, before version 2). */
static int bb_object_delete(blk_t *b)
{
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    nmo_object_id_t object = nmo_engine_block_input_parameter_count(b) > 1
                                 ? nmo_engine_block_get_input_object(b, 0)
                                 : nmo_engine_block_target(b);
    if (object == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_engine_destroy_object(nmo_engine_block_engine(b), object);
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
