/**
 * @file engine_narratives.c
 * @brief Ports of the Narratives building blocks that start and stop scripts
 *        and objects.
 *
 * Each function does what the block's function in Narratives.dll does (the
 * reference is the Ballanced source of the blocks). Activating a script
 * goes through nmo_engine_activate_script(), which also puts its owner in
 * the behavior manager's list, so the "Awake Object" setting needs nothing
 * more.
 */

#include "engine_internal.h"

#include "object/nmo_object_enum_defs.h"
#include "type/nmo_param_guids.h"
#include "type/nmo_type_guids.h"

typedef nmo_engine_block_t blk_t;

static int32_t get_in_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static int bb_activate_script(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    nmo_engine_block_activate_input(b, 0, false);
    nmo_engine_block_activate_output(b, 0, true);
    if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_BOOL)) {
        bool reset = get_in_int(b, 0, 1) != 0;
        for (int i = 1; i < nmo_engine_block_input_parameter_count(b); i++) {
            nmo_object_id_t script = nmo_engine_block_get_input_object(b, i);
            if (script != 0) {
                nmo_engine_activate_script(engine, script, true, reset);
            }
        }
        return NMO_ENGINE_BR_OK;
    }
    /* The old version: the script, then "Reset ?" */
    nmo_object_id_t script = nmo_engine_block_get_input_object(b, 0);
    if (script == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_engine_activate_script(engine, script, true, get_in_int(b, 1, 1) != 0);
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
        nmo_engine_activate_script(engine, script, true, get_in_int(b, 0, 0) != 0);
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

static const nmo_engine_block_desc_t eng_narratives_blocks[] = {
    {NMO_GUID(0x4c7e7bc3, 0x0b693155), "Activate Script", bb_activate_script, NULL, NULL},
    {NMO_GUID(0x14367c05, 0x635b24f9), "Deactivate Script", bb_deactivate_script, NULL, NULL},
    {NMO_GUID(0x706c5a40, 0x5bb31a0b), "Execute Script", bb_execute_script, NULL, NULL},
    {NMO_GUID(0x5f003fba, 0x5e574f60), "Activate Object", bb_activate_object, NULL, NULL},
    {NMO_GUID(0x160f4b7d, 0x67de224e), "Deactivate Object", bb_deactivate_object, NULL, NULL},
    {NMO_GUID(0x1a7d5b17, 0x70630fed), "Call Script", bb_call_script, NULL, NULL},
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
