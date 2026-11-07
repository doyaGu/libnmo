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

#include "virtools.h"

#include "object/nmo_class_ids.h"
#include "type/nmo_type_guids.h"

/* The Planar Filter blocks drawing after the sprites (their PostSpriteRenderCallBacks) */
typedef struct visuals {
    ENG_VEC(int32_t) filters;
} visuals_t;

static void visuals_destroy(nmo_engine_t *engine, void *state);
static void visuals_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                           const eng_render_args_t *args);

static const eng_manager_desc_t visuals_manager = {
    .name = "Visuals Render",
    .state_size = sizeof(visuals_t),
    .destroy = visuals_destroy,
    .render = visuals_render,
};

/* Add or Remove PostSpriteRenderCallBack(PlanarFilterRender, beh): once each */
static void planar_filter_drawn(nmo_engine_t *engine, int32_t node, bool drawn)
{
    visuals_t *visuals = (visuals_t *)eng_manager_state(engine, &visuals_manager);
    for (size_t i = 0; i < visuals->filters.count; i++) {
        if (visuals->filters.data[i] == node) {
            if (!drawn) {
                visuals->filters.data[i] = visuals->filters.data[--visuals->filters.count];
            }
            return;
        }
    }
    int32_t *slot = NULL;
    if (drawn && ENG_VEC_PUSH(visuals->filters, slot)) {
        *slot = node;
    }
}

/*
 * Planar Filter: On draws the filtering quad over the view after the
 * sprites each frame until Off; Exit On or Exit Off.
 */
static int bb_planar_filter(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        planar_filter_drawn(b->engine, b->node, false);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        planar_filter_drawn(b->engine, b->node, true);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* PlanarFilterCallback: out of the render context when its script stops, back when it runs */
static int cb_planar_filter(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_RESET || message == NMO_ENGINE_MSG_PAUSE ||
        message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        planar_filter_drawn(b->engine, b->node, false);
    } else if ((message == NMO_ENGINE_MSG_ACTIVATESCRIPT || message == NMO_ENGINE_MSG_RESUME) &&
               nmo_engine_behavior_is_active(b->engine, block_node(b)->id) &&
               nmo_engine_parent_script_is_active(b->engine, block_node(b)->id)) {
        planar_filter_drawn(b->engine, b->node, true);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * PlanarFilterRender: the view's rect in Filtering Color, textured with
 * Texture (modulated, its alpha too), blended Source Blend and Dest Blend,
 * no Z. The Additional Color is the specular one the engine's draws have no
 * room for.
 */
static void draw_filters(nmo_engine_t *engine, const visuals_t *visuals, float width, float height)
{
    for (size_t i = 0; i < visuals->filters.count; i++) {
        nmo_engine_block_t handle = {engine, visuals->filters.data[i]};
        blk_t *b = &handle;
        float color[4] = {1.0f, 1.0f, 1.0f, 0.5f};
        nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
        nmo_engine_draw_t *draw = NULL;
        if (!ENG_VEC_PUSH(engine->render.draws, draw)) {
            return;
        }
        memset(draw, 0, sizeof(*draw));
        draw->kind = NMO_ENGINE_DRAW_QUAD;
        draw->rect[2] = width;
        draw->rect[3] = height;
        draw->uv[2] = draw->uv[3] = 1.0f;
        draw->texture = nmo_engine_block_get_input_object(b, 2);
        uint32_t argb = 0;
        for (int c = 0; c < 4; c++) {
            float v = color[c] < 0.0f ? 0.0f : color[c] > 1.0f ? 1.0f : color[c];
            argb |= (uint32_t)(v * 255.0f + 0.5f) << (c == 3 ? 24 : 16 - 8 * c);
        }
        draw->color = draw->color2 = argb;
        draw->blend = true;
        draw->source_blend = get_in_int(b, 3, 5); /* VXBLEND_SRCALPHA */
        draw->dest_blend = get_in_int(b, 4, 6);   /* VXBLEND_INVSRCALPHA */
        draw->nearest = get_in_int(b, 5, 2) == 1; /* VXTEXTUREFILTER_NEAREST */
    }
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

/* Hide 2D Entity and Show 2D Entity: CKObject::Show on the target alone */
static int show_2d(blk_t *b, int32_t show)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_show(b->engine, target, show);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, show);
    return NMO_ENGINE_BR_OK;
}

static int bb_hide_2d(blk_t *b)
{
    return show_2d(b, 0); /* CKHIDE */
}

static int bb_show_2d(blk_t *b)
{
    return show_2d(b, 1); /* CKSHOW */
}

/* Change Sprite Slot: CKSprite::SetCurrentSlot, a slot the sprite does not have ignored */
static int bb_change_sprite_slot(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_texture_t *sprite = target != 0 ? eng_texture(b->engine, target) : NULL;
    if (sprite == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_texture_set_slot(sprite, get_in_int(b, 0, 0));
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set 2D Parent: CK2dEntity::SetParent, NULL for the root */
static int bb_set_2d_parent(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_2d_set_parent(b->engine, target, nmo_engine_block_get_input_object(b, 0));
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set 2D Position: CK2dEntity::SetPosition in pixels from the Referential's corner; with no
   target nothing happens, the input left active */
static int bb_set_2d_position(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target != 0) {
        float position[2] = {0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, 0, position, sizeof(position));
        eng_2d_set_position(b->engine, target, position[0], position[1], false, false,
                            nmo_engine_block_get_input_object(b, 1));
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* Set Color Key 2D: the sprite's transparent color (RGBAFTOCOLOR), then its transparency */
static int bb_set_color_key_2d(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 0, color, sizeof(color));
    eng_texture_t *sprite = eng_texture(b->engine, target);
    if (sprite != NULL) {
        sprite->transparent_color = rgbaf_to_color(color);
        sprite->transparent = get_in_int(b, 1, 1) != 0;
        eng_texture_changed(b->engine, target);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Rectangles: CK2dEntity::SetRect (the children moved along), then SetSourceRect; with
   no target nothing happens */
static int bb_set_rectangles(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target != 0) {
        nmo_rect_t rect = {0.0f, 0.0f, 0.0f, 0.0f};
        if (nmo_engine_block_get_input_value(b, 0, &rect, sizeof(rect)) == NMO_OK) {
            eng_2d_set_rect(b->engine, target, rect, false);
        }
        if (nmo_engine_block_get_input_value(b, 1, &rect, sizeof(rect)) == NMO_OK) {
            eng_2d_t *entity = eng_2d_entity(b->engine, target);
            if (entity != NULL) {
                entity->source = rect;
            }
        }
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    }
    return NMO_ENGINE_BR_OK;
}

/* A 2D entity flag on or off */
static void set_2d_flag(eng_2d_t *entity, uint32_t flag, bool on)
{
    if (on) {
        entity->flags |= flag;
    } else {
        entity->flags &= ~flag;
    }
}

/*
 * Edit 2D Entity (Display2dSprite): the components its setting lists (1
 * position, 2 Z order, 4 background, 8 size, 16 pickable, 32 clip to
 * parent, 64 the stick flags), each from its inputs in that order; the
 * position and size homogeneous when the entity uses homogeneous coordinates.
 */
static int bb_edit_2d_entity(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_2d_t *entity = target != 0 ? eng_2d_entity(b->engine, target) : NULL;
    if (entity == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool homogeneous = (entity->flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) != 0;
    int32_t flags = get_local_int(b, 0, 7);
    int pos = 0;
    if (flags & 1) {
        if (nmo_guid_equals(nmo_engine_block_input_parameter_type(b, 0), CKPGUID_INT)) {
            /* an old integer position: none in homogeneous coordinates */
            if (!homogeneous) {
                eng_2d_set_position(b->engine, target, (float)get_in_int(b, 0, 0),
                                    (float)get_in_int(b, 1, 0), false, false, 0);
            }
        } else {
            eng_2d_set_position(b->engine, target, get_in_float(b, 0, 0.0f), get_in_float(b, 1, 0.0f),
                                homogeneous, false, 0);
        }
        pos += 2;
    }
    if (flags & 2) {
        entity = eng_2d_entity(b->engine, target);
        entity->z_order = get_in_int(b, pos++, 0);
    }
    if (flags & 4) {
        eng_2d_set_background(b->engine, target, get_in_int(b, pos++, 0) != 0);
    }
    if (flags & 8) {
        float width = get_in_float(b, pos, 0.0f), height = get_in_float(b, pos + 1, 0.0f);
        eng_2d_set_size(b->engine, target, width, height, homogeneous, false);
        pos += 2;
    }
    entity = eng_2d_entity(b->engine, target);
    if (flags & 16) {
        set_2d_flag(entity, CK_2DENTITY_NOTPICKABLE, get_in_int(b, pos++, 0) == 0);
    }
    if (flags & 32) {
        set_2d_flag(entity, CK_2DENTITY_CLIPTOPARENT, get_in_int(b, pos++, 0) != 0);
    }
    if (flags & 64) {
        set_2d_flag(entity, CK_2DENTITY_STICKLEFT, get_in_int(b, pos, 0) != 0);
        set_2d_flag(entity, CK_2DENTITY_STICKRIGHT, get_in_int(b, pos + 1, 0) != 0);
        set_2d_flag(entity, CK_2DENTITY_STICKTOP, get_in_int(b, pos + 2, 0) != 0);
        set_2d_flag(entity, CK_2DENTITY_STICKBOTTOM, get_in_int(b, pos + 3, 0) != 0);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* The texture's transparency as Sprite Multi Angle sets it: Transparent, and Color Key when on */
static void multi_angle_key(blk_t *b, nmo_object_id_t id)
{
    eng_texture_t *texture = id != 0 ? eng_texture(b->engine, id) : NULL;
    if (texture == NULL) {
        return;
    }
    texture->transparent = get_in_int(b, 2, 1) != 0;
    if (texture->transparent) {
        float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, 3, color, sizeof(color));
        texture->transparent_color = rgbaf_to_color(color);
    }
    eng_texture_changed(b->engine, id);
}

/*
 * Sprite Multi Angle: the 3D sprite its callback made where the owner was,
 * showing the texture of the angle the camera sees it from around the Y
 * axis: the textures from the sixth input on go round once, or with
 * Mirroring their mirror images make the other half.
 */
static int bb_sprite_multi_angle(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t sprite_id = 0, material_id = 0;
    nmo_engine_block_get_local_value(b, 0, &sprite_id, sizeof(sprite_id));
    nmo_engine_block_get_local_value(b, 1, &material_id, sizeof(material_id));
    eng_sprite3d_t *sprite = eng_sprite3d(engine, sprite_id);
    int32_t at = eng_entity_index(engine, sprite_id);
    if (sprite == NULL || at < 0) {
        return NMO_ENGINE_BR_OK;
    }
    bool mirror = get_in_int(b, 4, 0) != 0;
    eng_matrix_t eye;
    eng_render_viewpoint(engine, &eye);
    const float *position = engine->entities.data[at].world.m[3];
    float angle = (float)(atan2((double)(eye.m[3][2] - position[2]), (double)(eye.m[3][0] - position[0])) +
                          3.1415927);
    int32_t count = nmo_engine_block_input_parameter_count(b) - 5;
    sprite->uv_rect = (nmo_rect_t){0.0f, 0.0f, 1.0f, 1.0f};
    int32_t index;
    if (mirror) {
        index = (int32_t)(int64_t)((double)(2 * count - 2) * angle * 0.15915494);
        if (index >= count) {
            sprite->uv_rect = (nmo_rect_t){1.0f, 0.0f, 0.0f, 1.0f};
            index = 2 * count - index - 2;
        }
    } else {
        index = (int32_t)(int64_t)((double)count * angle * 0.15915494);
    }
    nmo_object_id_t texture = nmo_engine_block_get_input_object(b, 5 + index);
    multi_angle_key(b, texture);
    eng_material_t *material = eng_material(engine, material_id);
    if (material != NULL) {
        material->desc.texture = texture;
    }
    nmo_vector2_t size = {get_in_float(b, 0, 1.0f), get_in_float(b, 1, 1.0f)};
    eng_sprite3d_set_size(engine, sprite_id, size);
    eng_emit(engine, NMO_ENGINE_EVENT_EFFECT, b->node, sprite_id, index);
    return NMO_ENGINE_BR_OK;
}

static int cb_sprite_multi_angle(blk_t *b, uint32_t message)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t sprite_id = 0;
    nmo_engine_block_get_local_value(b, 0, &sprite_id, sizeof(sprite_id));
    if (message == NMO_ENGINE_MSG_LOAD) {
        /* A Y-rotating 3D sprite with a white material of the first texture, where the
           owner is, the owner hidden */
        sprite_id = eng_create_object(engine, NMO_CID_SPRITE3D, "MultiSprite");
        nmo_object_id_t material_id = eng_create_object(engine, NMO_CID_MATERIAL, "MultiMaterial");
        nmo_engine_block_set_local_value(b, 0, &sprite_id, sizeof(sprite_id));
        nmo_engine_block_set_local_value(b, 1, &material_id, sizeof(material_id));
        nmo_object_id_t texture = nmo_engine_block_get_input_object(b, 5);
        eng_material_t *material = eng_material(engine, material_id);
        if (material != NULL) {
            const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f}, specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            memcpy(material->desc.diffuse, white, sizeof(white));
            memcpy(material->desc.specular, specular, sizeof(specular));
            material->desc.texture = texture;
        }
        multi_angle_key(b, texture);
        nmo_vector2_t size = {get_in_float(b, 0, 1.0f), get_in_float(b, 1, 1.0f)};
        eng_sprite3d_set_size(engine, sprite_id, size);
        eng_sprite3d_t *sprite = eng_sprite3d(engine, sprite_id);
        if (sprite != NULL) {
            sprite->mode = VXSPRITE3D_YROTATE;
            sprite->material = material_id;
        }
        if (nmo_engine_parent_script_is_active(engine, nmo_engine_block_id(b))) {
            eng_add_to_level_scene(engine, sprite_id);
        }
        nmo_object_id_t owner = nmo_engine_block_owner(b);
        int32_t at = eng_entity_index(engine, sprite_id);
        int32_t ref = eng_entity_index(engine, owner);
        if (at >= 0) {
            const eng_vector_t origin = {0.0f, 0.0f, 0.0f};
            eng_entity_set_position(engine, at, &origin, ref, false);
        }
        if (owner != 0) {
            eng_show(engine, owner, 0);
        }
    } else if (message == NMO_ENGINE_MSG_ACTIVATESCRIPT && sprite_id != 0) {
        eng_show(engine, sprite_id, 1);
    } else if (message == NMO_ENGINE_MSG_DEACTIVATESCRIPT && sprite_id != 0) {
        eng_show(engine, sprite_id, 0);
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_visuals_blocks[] = {
    {NMO_GUID_INIT(0x00cd320b, 0x32ed010b), "Planar Filter", bb_planar_filter, cb_planar_filter,
     NULL},
    {NMO_GUID_INIT(0x5fb70201, 0x65595af3), "Statistics", bb_statistics, NULL, NULL},
    {NMO_GUID_INIT(0x16f6368f, 0x506b60fc), "Show Mouse Cursor", bb_show_mouse_cursor, NULL, NULL},
    {NMO_GUID_INIT(0xa85a213a, 0xef78d52a), "Show", bb_show, NULL, NULL},
    {NMO_GUID_INIT(0x31d97d82, 0x78d54d98), "Hide", bb_hide, NULL, NULL},
    {NMO_GUID_INIT(0x3f7e5bff, 0x02326a71), "Set 2D Material", bb_set_2d_material, NULL, NULL},
    {NMO_GUID_INIT(0x13579753, 0x13579753), "Hide 2D Entity", bb_hide_2d, NULL, NULL},
    {NMO_GUID_INIT(0xababa123, 0x123ababa), "Show 2D Entity", bb_show_2d, NULL, NULL},
    {NMO_GUID_INIT(0x70f8028c, 0x723215e7), "Change Sprite Slot", bb_change_sprite_slot, NULL, NULL},
    {NMO_GUID_INIT(0x4c1d16ac, 0x1877604a), "Sprite Movie Player", virtools_movie_player, NULL, NULL},
    {NMO_GUID_INIT(0x14c70f07, 0x4f2a100a), "Set 2D Parent", bb_set_2d_parent, NULL, NULL},
    {NMO_GUID_INIT(0x32d30604, 0x23b163fc), "Set 2D Position", bb_set_2d_position, NULL, NULL},
    {NMO_GUID_INIT(0x7bda540c, 0x39782384), "Set Color Key 2D", bb_set_color_key_2d, NULL, NULL},
    {NMO_GUID_INIT(0x3bcf7052, 0x59f40417), "Set Rectangles", bb_set_rectangles, NULL, NULL},
    {NMO_GUID_INIT(0x7fc54c91, 0x3f486670), "Edit 2D Entity", bb_edit_2d_entity, NULL, NULL},
    {NMO_GUID_INIT(0x1ef926bd, 0x01ad167f), "Sprite Multi Angle", bb_sprite_multi_angle,
     cb_sprite_multi_angle, NULL},
};

/* Planar Filter's full-view quads, the post sprite render callbacks */
static void visuals_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                           const eng_render_args_t *args)
{
    if (stage == ENG_RENDER_POST) {
        draw_filters(engine, (const visuals_t *)state, args->width, args->height);
    }
}

static void visuals_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    free(((visuals_t *)state)->filters.data);
}

nmo_status_t eng_register_visuals(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &visuals_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_visuals_blocks) : status;
}
