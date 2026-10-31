/**
 * @file visuals.c
 * @brief Ports of the Visuals building blocks that keep state.
 *
 * Each function does what the block's function in Visuals.dll does (the
 * reference is the Ballanced source of the blocks, or the DLL where they
 * differ). Show and Hide change what the render state shows, Set 2D
 * Material a 2D entity's material. Show Mouse Cursor sets the cursor
 * visibility the engine keeps;
 * Statistics reports the frame counts CK2 keeps (the engine measures no time
 * but the frame's, and draws nothing).
 */

#include "../engine_block_util.h"

/*
 * Planar Filter: On draws the filtering quad over the view after the
 * sprites each frame (an effect) until Off; Exit On or Exit Off.
 */
static int bb_planar_filter(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * Statistics (Visuals): the last frame's statistics the settings ask for,
 * each frame while on; On and Off turn the profiling on and off. The frame
 * rate comes from the frame's length; the engine measures no other time
 * (the interface time is the whole frame, as CK2 estimates it) and draws
 * nothing.
 */
static int bb_statistics(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        engine->profile.enabled = true;
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        engine->profile.enabled = false;
        return NMO_ENGINE_BR_OK;
    }
    const eng_stats_t *stats = &engine->profile.last;
    int32_t flags = 1;
    nmo_engine_block_get_local_value(b, 0, &flags, sizeof(flags));
    if (flags != 0) {
        float fps = 1000.0f / stats->frame_time;
        set_out(b, 0, &fps, sizeof(fps));
    }
    /* General Time: interface, then processing, rendering, parametric operations,
       animation, IK, total behavior and behavior code time */
    flags = 0xFF;
    nmo_engine_block_get_local_value(b, 1, &flags, sizeof(flags));
    for (int i = 0; i < 8; i++) {
        if ((flags & (1 << i)) != 0) {
            float time = i == 0 ? stats->frame_time : 0.0f;
            set_out(b, 1 + i, &time, sizeof(time));
        }
    }
    flags = 0x1F;
    nmo_engine_block_get_local_value(b, 2, &flags, sizeof(flags));
    const int32_t behavioral[5] = {stats->active_objects_executed, stats->behaviors_executed,
                                   stats->building_blocks_executed, stats->links_parsed,
                                   stats->links_delayed};
    for (int i = 0; i < 5; i++) {
        if ((flags & (1 << i)) != 0) {
            set_out(b, 9 + i, &behavioral[i], sizeof(behavioral[i]));
        }
    }
    flags = 0x1F;
    nmo_engine_block_get_local_value(b, 3, &flags, sizeof(flags));
    for (int i = 0; i < 5; i++) {
        if ((flags & (1 << i)) != 0) {
            const int32_t none = 0;
            set_out(b, 14 + i, &none, sizeof(none));
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* Show Mouse Cursor (Visuals): shows or hides the cursor (CKInputManager::ShowCursor). */
static int bb_show_mouse_cursor(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_engine_block_engine(b)->input.cursor_hidden = get_in_int(b, 0, 0) == 0;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    return NMO_ENGINE_BR_OK;
}

/* ShowHierarchy (Hide.cpp): the object, then its 3D or 2D children */
static void show_hierarchy(nmo_engine_t *engine, nmo_object_id_t id, int32_t show)
{
    eng_show(engine, id, show);
    int32_t entity = eng_entity_index(engine, id);
    if (entity >= 0) {
        for (int32_t c = engine->entities.data[entity].first_child; c >= 0;
             c = engine->entities.data[c].next_sibling) {
            show_hierarchy(engine, engine->entities.data[c].id, show);
        }
        return;
    }
    if (eng_2d_entity(engine, id) == NULL) {
        return;
    }
    for (size_t i = 0; i < engine->render.entities_2d.count; i++) {
        const eng_2d_t *child = &engine->render.entities_2d.data[i];
        if (child->parent == id && !child->destroyed) {
            show_hierarchy(engine, child->id, show);
        }
    }
}

/* Show and Hide: the target, with its children when Hierarchy is TRUE */
static int show_or_hide(blk_t *b, int32_t show)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (get_in_int(b, 0, 0) != 0) {
        show_hierarchy(b->engine, target, show);
    } else {
        eng_show(b->engine, target, show);
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, show);
    return NMO_ENGINE_BR_OK;
}

static int bb_show(blk_t *b)
{
    return show_or_hide(b, 1); /* CKSHOW */
}

static int bb_hide(blk_t *b)
{
    return show_or_hide(b, 0); /* CKHIDE */
}

/* Set 2D Material: CK2dEntity::SetMaterial, NULL included */
static int bb_set_2d_material(blk_t *b)
{
    eng_2d_t *entity = eng_2d_entity(b->engine, nmo_engine_block_target(b));
    if (entity == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    entity->material = nmo_engine_block_get_input_object(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, entity->id, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_visuals_blocks[] = {
    {NMO_GUID(0x00cd320b, 0x32ed010b), "Planar Filter", bb_planar_filter, NULL, NULL},
    {NMO_GUID(0x5fb70201, 0x65595af3), "Statistics", bb_statistics, NULL, NULL},
    {NMO_GUID(0x16f6368f, 0x506b60fc), "Show Mouse Cursor", bb_show_mouse_cursor, NULL, NULL},
    {NMO_GUID(0xa85a213a, 0xef78d52a), "Show", bb_show, NULL, NULL},
    {NMO_GUID(0x31d97d82, 0x78d54d98), "Hide", bb_hide, NULL, NULL},
    {NMO_GUID(0x3f7e5bff, 0x02326a71), "Set 2D Material", bb_set_2d_material, NULL, NULL},
};

nmo_status_t eng_register_visuals(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_visuals_blocks);
}
