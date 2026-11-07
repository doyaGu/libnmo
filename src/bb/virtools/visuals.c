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

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "../../engine/engine_character.h"
#include "type/nmo_type_guids.h"

/* The blocks drawing in render callbacks: Planar Filter and Display Progression Bar after
   the sprites (PostSpriteRenderCallBacks), Display Score after the scene (PostRenderCallBack) */
typedef struct visuals {
    ENG_VEC(int32_t) filters;
    ENG_VEC(int32_t) bars;
    ENG_VEC(int32_t) scores;
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

typedef ENG_VEC(int32_t) node_list_t;

/* Add or Remove a render callback of the block `node`: once each */
static void node_drawn(node_list_t *list, int32_t node, bool drawn)
{
    for (size_t i = 0; i < list->count; i++) {
        if (list->data[i] == node) {
            if (!drawn) {
                list->data[i] = list->data[--list->count];
            }
            return;
        }
    }
    int32_t *slot = NULL;
    if (drawn && ENG_VEC_PUSH(*list, slot)) {
        *slot = node;
    }
}

static visuals_t *visuals_state(nmo_engine_t *engine)
{
    return (visuals_t *)eng_manager_state(engine, &visuals_manager);
}

/* Add or Remove PostSpriteRenderCallBack(PlanarFilterRender, beh) */
static void planar_filter_drawn(nmo_engine_t *engine, int32_t node, bool drawn)
{
    node_drawn((node_list_t *)&visuals_state(engine)->filters, node, drawn);
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

/* Hide Hierarchy: CKObject::Show(CKHIERARCHICALHIDE), the target and what is under it hidden */
static int bb_hide_hierarchy(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_show(b->engine, target, 2); /* CKHIERARCHICALHIDE */
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 2);
    return NMO_ENGINE_BR_OK;
}

/* FPS: a thousand over the frame's time in ms */
static int bb_fps(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    float fps = 1000.0f / nmo_engine_block_delta_time(b);
    nmo_engine_block_set_output_value(b, 0, &fps, sizeof(fps));
    return NMO_ENGINE_BR_OK;
}

/* Blink: each activation one frame of a cycle of Hidden frames hidden, then Shown frames shown */
static int bb_blink(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t hidden = get_in_int(b, 0, 0);
    hidden = hidden < 0 ? 0 : hidden;
    int32_t shown = get_in_int(b, 1, 10);
    shown = shown < 0 ? 0 : shown;
    if (hidden + shown > 0) {
        int32_t frame = 20;
        nmo_engine_block_get_local_value(b, 0, &frame, sizeof(frame));
        eng_show(b->engine, target, frame >= hidden ? 1 : 0);
        frame = (frame + 1) % (hidden + shown);
        nmo_engine_block_set_local_value(b, 0, &frame, sizeof(frame));
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, frame);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* A 3D entity's moveable flags; NULL for none */
static uint32_t *moveable_flags(nmo_engine_t *engine, nmo_object_id_t id)
{
    int32_t at = id != 0 ? eng_entity_index(engine, id) : -1;
    return at >= 0 ? &engine->entities.data[at].moveable_flags : NULL;
}

/* Set Z Buffer: whether the target tests and writes the Z buffer (NOZBUFFERTEST, NOZBUFFERWRITE) */
static int bb_set_z_buffer(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    uint32_t *flags = moveable_flags(b->engine, target);
    if (flags != NULL) {
        uint32_t f = *flags;
        f = get_in_int(b, 0, 1) != 0 ? f & ~(uint32_t)VX_MOVEABLE_NOZBUFFERTEST : f | VX_MOVEABLE_NOZBUFFERTEST;
        f = get_in_int(b, 1, 1) != 0 ? f & ~(uint32_t)VX_MOVEABLE_NOZBUFFERWRITE : f | VX_MOVEABLE_NOZBUFFERWRITE;
        *flags = f;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Render Order: rendered first or last (RENDERFIRST, RENDERLAST); unless first, the
   Rendering Priority (CK3dEntity::SetZOrder) */
static int bb_set_render_order(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    bool first = get_in_int(b, 0, 0) != 0, last = get_in_int(b, 1, 0) != 0;
    int32_t at = eng_entity_index(b->engine, target);
    if (at >= 0) {
        eng_entity_t *entity = &b->engine->entities.data[at];
        uint32_t f = entity->moveable_flags;
        f = first ? f | VX_MOVEABLE_RENDERFIRST : f & ~(uint32_t)VX_MOVEABLE_RENDERFIRST;
        f = last ? f | VX_MOVEABLE_RENDERLAST : f & ~(uint32_t)VX_MOVEABLE_RENDERLAST;
        if (!first) {
            entity->render_priority = get_in_int(b, 2, 0);
        }
        entity->moveable_flags = f;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set 3DSprite Mode: CKSprite3D::SetMode */
static int bb_set_3dsprite_mode(blk_t *b)
{
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    eng_sprite3d_t *sprite = eng_sprite3d(b->engine, target);
    if (sprite != NULL) {
        sprite->mode = (uint32_t)get_in_int(b, 0, 0);
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* SetMaterialMode (Make Transparent): the blend factors, blending (and no Z write) unless the
   destination is ZERO or it is plain alpha at 1, and the diffuse alpha */
static void transparent_material(nmo_engine_t *engine, nmo_object_id_t id, int32_t source, int32_t dest,
                                 float alpha)
{
    eng_material_t *material = id != 0 ? eng_material(engine, id) : NULL;
    if (material == NULL) {
        return;
    }
    material->desc.source_blend = source;
    material->desc.dest_blend = dest;
    bool opaque = dest == VXBLEND_ZERO ||
                  (alpha == 1.0f && dest == VXBLEND_INVSRCALPHA && source == VXBLEND_SRCALPHA);
    material->desc.alpha_blend = !opaque;
    material->desc.z_write = opaque;
    material->desc.diffuse[3] = alpha;
}

/* The materials of a mesh's groups, each once per group */
static void transparent_mesh(nmo_engine_t *engine, nmo_object_id_t mesh_id, int32_t source, int32_t dest,
                             float alpha)
{
    eng_mesh_copy_t *mesh = eng_mesh_copy(engine, mesh_id);
    nmo_engine_mesh_t desc;
    if (mesh == NULL || !nmo_engine_mesh(engine, mesh_id, &desc)) {
        return;
    }
    mesh->flags |= VXMESH_FORCETRANSPARENCY; /* CKMesh::SetTransparent */
    if (mesh->prelit && desc.vertex_count > 0) {
        if (mesh->colors == NULL) {
            mesh->colors = (uint32_t *)malloc((size_t)desc.vertex_count * sizeof(uint32_t));
            for (int32_t v = 0; mesh->colors != NULL && v < desc.vertex_count; v++) {
                mesh->colors[v] = desc.colors != NULL ? desc.colors[v] : 0xFFFFFFFFu;
            }
        }
        uint32_t a = (uint32_t)(uint8_t)(int64_t)(alpha * 255.0f);
        for (int32_t v = 0; mesh->colors != NULL && v < desc.vertex_count; v++) {
            mesh->colors[v] = (mesh->colors[v] & 0xFFFFFFu) | a << 24;
        }
    }
    mesh->version = ++engine->render.mesh_version;
    for (int32_t g = 0; g < desc.material_count; g++) {
        transparent_material(engine, nmo_engine_mesh_material(engine, mesh_id, g), source, dest, alpha);
    }
}

/* MakeTransparentFunc: a 2D entity's or 3D sprite's material, a character's body parts, or
   every mesh of a 3D entity */
static void make_transparent(nmo_engine_t *engine, nmo_object_id_t id, int32_t source, int32_t dest,
                             float alpha)
{
    nmo_class_id_t cls = eng_object_class(engine, id);
    if (id == 0) {
        return;
    }
    if (eng_2d_entity(engine, id) != NULL) {
        transparent_material(engine, eng_2d_entity(engine, id)->material, source, dest, alpha);
    } else if (cls == NMO_CID_CHARACTER) {
        size_t count = eng_character_bodypart_count(engine, id);
        for (size_t i = 0; i < count; i++) {
            make_transparent(engine, eng_character_bodypart(engine, id, i), source, dest, alpha);
        }
    } else if (eng_sprite3d(engine, id) != NULL) {
        transparent_material(engine, eng_sprite3d(engine, id)->material, source, dest, alpha);
    } else {
        nmo_object_t *object = eng_find_object(engine, id);
        const nmo_3dentity_state_t *state =
            object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                 engine->types, object, CKPGUID_3DENTITY)
                           : NULL;
        for (uint32_t m = 0; state != NULL && m < state->mesh_count; m++) {
            transparent_mesh(engine, eng_object_ref(engine, id, &state->mesh_ids[m]), source, dest, alpha);
        }
    }
}

/* MakeTransparentHierarchicalFunc: the children first, 3D or 2D, then the object */
static void make_transparent_hierarchy(nmo_engine_t *engine, nmo_object_id_t id, int32_t source,
                                       int32_t dest, float alpha, int depth)
{
    int32_t at = eng_entity_index(engine, id);
    if (depth < 256 && at >= 0) {
        for (int32_t c = engine->entities.data[at].first_child; c >= 0;
             c = engine->entities.data[c].next_sibling) {
            make_transparent_hierarchy(engine, engine->entities.data[c].id, source, dest, alpha, depth + 1);
        }
    } else if (depth < 256 && eng_2d_entity(engine, id) != NULL) {
        eng_load_2d_entities(engine);
        for (size_t i = 0; i < engine->render.entities_2d.count; i++) {
            const eng_2d_t *child = &engine->render.entities_2d.data[i];
            if (child->parent == id && !child->destroyed) {
                make_transparent_hierarchy(engine, child->id, source, dest, alpha, depth + 1);
            }
        }
    }
    make_transparent(engine, id, source, dest, alpha);
}

/* Make Transparent: the target's materials blended Source Blend over Destination Blend at
   Alpha, with its children when Hierarchy is TRUE */
static int bb_make_transparent(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t source = get_in_int(b, 0, VXBLEND_SRCALPHA), dest = get_in_int(b, 1, VXBLEND_INVSRCALPHA);
    float alpha = get_in_float(b, 2, 0.5f);
    bool hierarchy = get_in_int(b, 3, 1) != 0;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (target == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (hierarchy) {
        make_transparent_hierarchy(b->engine, target, source, dest, alpha, 0);
    } else {
        make_transparent(b->engine, target, source, dest, alpha);
    }
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, target, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Render Options: the render context's options, then its global render mode */
static int bb_set_render_options(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    eng_render_t *render = eng_render_settings(b->engine);
    render->render_options = (uint32_t)get_in_int(b, 0, 255);
    render->shading = get_in_int(b, 1, 4);
    render->use_textures = get_in_int(b, 2, 1) != 0;
    render->wireframe_layer = get_in_int(b, 3, 0) != 0;
    eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, (int32_t)render->render_options);
    return NMO_ENGINE_BR_OK;
}

/*
 * Use Z information (a callback only): on load and resume the objects with
 * the "ZBuffer only" attribute write only depth and are rendered first; on
 * these and on edits the render context clears its background as Clear
 * Background says.
 */
static int cb_use_z_information(blk_t *b, uint32_t message)
{
    nmo_engine_t *engine = b->engine;
    if (message == NMO_ENGINE_MSG_LOAD || message == NMO_ENGINE_MSG_RESUME) {
        const eng_attributes_t *attributes = &engine->attributes;
        for (size_t t = 0; t < attributes->types.count; t++) {
            const eng_attribute_type_t *type = &attributes->types.data[t];
            if (!type->present || type->name == NULL || strcmp(type->name, "ZBuffer only") != 0) {
                continue;
            }
            for (size_t i = 0; i < attributes->values.count; i++) {
                uint32_t *flags = attributes->values.data[i].type == (int32_t)t
                                      ? moveable_flags(engine, attributes->values.data[i].object)
                                      : NULL;
                if (flags != NULL) {
                    *flags |= VX_MOVEABLE_ZBUFONLY | VX_MOVEABLE_RENDERFIRST;
                }
            }
        }
    } else if (message != NMO_ENGINE_MSG_EDITED) {
        return NMO_ENGINE_BR_OK;
    }
    eng_render_t *render = eng_render_settings(engine);
    if (get_in_int(b, 0, 0) != 0) {
        render->render_options |= CK_RENDER_CLEARBACK;
    } else {
        render->render_options &= ~(uint32_t)CK_RENDER_CLEARBACK;
    }
    return NMO_ENGINE_BR_OK;
}

static nmo_object_id_t local_object(blk_t *b, int pos)
{
    nmo_object_id_t id = 0;
    nmo_engine_block_get_local_value(b, pos, &id, sizeof(id));
    return id;
}

/* CKSprite::RestoreInitialSize: its rect the bitmap's size, nothing else updated */
static void restore_initial_size(nmo_engine_t *engine, nmo_object_id_t sprite)
{
    eng_texture_t *bitmap = eng_texture(engine, sprite);
    eng_2d_t *entity = eng_2d_entity(engine, sprite);
    int32_t w = 0, h = 0;
    if (bitmap != NULL && entity != NULL) {
        eng_bitmap_size(engine, bitmap, &w, &h);
        entity->rect.right = entity->rect.left + (float)w;
        entity->rect.bottom = entity->rect.top + (float)h;
    }
}

/* CK2dEntity::UseSourceRect */
static void use_source_rect(nmo_engine_t *engine, nmo_object_id_t id, bool use)
{
    eng_2d_t *entity = eng_2d_entity(engine, id);
    if (entity != NULL) {
        entity->flags = use ? entity->flags | CK_2DENTITY_USESRCRECT
                            : entity->flags & ~(uint32_t)CK_2DENTITY_USESRCRECT;
    }
}

/* Show the sprite, render it, hide it again */
static void render_shown(nmo_engine_t *engine, nmo_object_id_t id, float width, float height)
{
    eng_show(engine, id, 1);
    eng_render_2d_entity(engine, id, width, height);
    eng_show(engine, id, 0);
}

/*
 * ProgressionBarPostRender: the empty bar at (X, Y) at its own size, then the
 * full bar cut to the Bar Progression of its width over it.
 */
static void draw_bar(nmo_engine_t *engine, int32_t node, float width, float height)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    nmo_object_id_t full = local_object(b, 1), empty = local_object(b, 0);
    int32_t x = get_in_int(b, 0, 0), y = get_in_int(b, 1, 0);
    float p = get_in_float(b, 2, 0.0f);
    p = p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p;
    eng_texture_t *bitmap = full != 0 ? eng_texture(engine, full) : NULL;
    if (bitmap != NULL) {
        int32_t sw = 0, sh = 0;
        eng_bitmap_size(engine, bitmap, &sw, &sh);
        float shown = (float)(int)(int64_t)((double)sw * (double)p);
        eng_2d_t *entity = eng_2d_entity(engine, full);
        if (entity != NULL) {
            entity->source = (nmo_rect_t){0.0f, 0.0f, shown, (float)sh};
        }
        use_source_rect(engine, full, true);
        eng_2d_set_rect(engine, full, (nmo_rect_t){(float)x, (float)y, (float)x + shown, (float)(y + sh)}, false);
    }
    if (empty != 0 && eng_2d_entity(engine, empty) != NULL) {
        eng_show(engine, empty, 1);
        restore_initial_size(engine, empty);
        use_source_rect(engine, empty, false);
        eng_2d_set_position(engine, empty, (float)x, (float)y, false, false, 0);
        eng_render_2d_entity(engine, empty, width, height);
        eng_show(engine, empty, 0);
    }
    if (bitmap != NULL) {
        render_shown(engine, full, width, height);
    }
}

/* ScorePostRender: the digit sprites of Score from (X, Y), each after the last one's width
   (drawn only when they are shown, as CK2dEntity::Render draws them) */
static void draw_score(nmo_engine_t *engine, int32_t node, float width, float height)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t score = get_in_int(b, 2, 0);
    score = score < 0 ? 0 : score;
    int32_t x = get_in_int(b, 0, 0), y = get_in_int(b, 1, 0);
    char *digits = eng_text_printf("%d", score);
    for (size_t i = 0; digits != NULL && digits[i] != '\0'; i++) {
        nmo_object_id_t sprite = local_object(b, digits[i] - '0');
        eng_2d_t *entity = sprite != 0 ? eng_2d_entity(engine, sprite) : NULL;
        if (entity == NULL) {
            break;
        }
        eng_2d_set_position(engine, sprite, (float)x, (float)y, false, false, 0);
        eng_render_2d_entity(engine, sprite, width, height);
        entity = eng_2d_entity(engine, sprite);
        x += (int32_t)(int64_t)(entity->rect.right - entity->rect.left);
    }
    free(digits);
}

/* Display Progression Bar and Display Score: On adds the render callback when the settings
   were set (Active), Off removes it */
static int on_off_drawn(blk_t *b, node_list_t *list, int active_local, int result)
{
    if (IN_ACTIVE(b, 1)) {
        node_drawn(list, b->node, false);
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        int32_t active = 0;
        nmo_engine_block_get_local_value(b, active_local, &active, sizeof(active));
        if (active != 0) {
            node_drawn(list, b->node, true);
            eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
        }
        OUT_ON(b, 0);
        IN_OFF(b, 0);
    }
    return result;
}

static int bb_display_progression_bar(blk_t *b)
{
    return on_off_drawn(b, (node_list_t *)&visuals_state(b->engine)->bars, 2, NMO_ENGINE_BR_ACTIVATENEXTFRAME);
}

/* DisplayProgressionBarCallBack: out of the render context when the script stops, back while
   the block is active */
static int cb_display_progression_bar(blk_t *b, uint32_t message)
{
    node_list_t *bars = (node_list_t *)&visuals_state(b->engine)->bars;
    if (message == NMO_ENGINE_MSG_RESET || message == NMO_ENGINE_MSG_PAUSE ||
        message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        node_drawn(bars, b->node, false);
    } else if ((message == NMO_ENGINE_MSG_RESUME || message == NMO_ENGINE_MSG_ACTIVATESCRIPT) &&
               nmo_engine_behavior_is_active(b->engine, block_node(b)->id)) {
        node_drawn(bars, b->node, true);
    }
    return NMO_ENGINE_BR_OK;
}

static int bb_display_score(blk_t *b)
{
    return on_off_drawn(b, (node_list_t *)&visuals_state(b->engine)->scores, 10, NMO_ENGINE_BR_OK);
}

/* DisplayScoreCallBack: drawn while the script runs, once the settings were set */
static int cb_display_score(blk_t *b, uint32_t message)
{
    node_list_t *scores = (node_list_t *)&visuals_state(b->engine)->scores;
    int32_t active = 0;
    nmo_engine_block_get_local_value(b, 10, &active, sizeof(active));
    if (message == NMO_ENGINE_MSG_DEACTIVATESCRIPT && active != 0) {
        node_drawn(scores, b->node, false);
    } else if (message == NMO_ENGINE_MSG_ACTIVATESCRIPT && active != 0) {
        node_drawn(scores, b->node, true);
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
    {NMO_GUID_INIT(0x27447f6b, 0x22aa0c59), "Hide Hierarchy", bb_hide_hierarchy, NULL, NULL},
    {NMO_GUID_INIT(0xa58a313a, 0xe7f8d32a), "FPS", bb_fps, NULL, NULL},
    {NMO_GUID_INIT(0xf00d010a, 0xfa0d010a), "Blink", bb_blink, NULL, NULL},
    {NMO_GUID_INIT(0x1d2d378d, 0x4a4f36b6), "Set Z Buffer", bb_set_z_buffer, NULL, NULL},
    {NMO_GUID_INIT(0x6f8b6782, 0x21554283), "Set Render Order", bb_set_render_order, NULL, NULL},
    {NMO_GUID_INIT(0x0c5d0457, 0xc64c7000), "Set 3DSprite Mode", bb_set_3dsprite_mode, NULL, NULL},
    {NMO_GUID_INIT(0x16cc600c, 0x702836d1), "Make Transparent", bb_make_transparent, NULL, NULL},
    {NMO_GUID_INIT(0x4d586c55, 0x5250236a), "Set Render Options", bb_set_render_options, NULL, NULL},
    {NMO_GUID_INIT(0x01f60adc, 0x39b02cb8), "Use Z information", NULL, cb_use_z_information, NULL},
    {NMO_GUID_INIT(0x04cf37cd, 0x7a246b85), "Display Progression Bar", bb_display_progression_bar,
     cb_display_progression_bar, NULL},
    {NMO_GUID_INIT(0x0fc45543, 0x55b012c3), "Display Score", bb_display_score, cb_display_score, NULL},
};

/* Planar Filter's full-view quads, the post sprite render callbacks */
static void visuals_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                           const eng_render_args_t *args)
{
    visuals_t *visuals = (visuals_t *)state;
    if (stage == ENG_RENDER_OVERLAY) {
        for (size_t i = 0; i < visuals->scores.count; i++) {
            draw_score(engine, visuals->scores.data[i], args->width, args->height);
        }
    } else if (stage == ENG_RENDER_POST) {
        draw_filters(engine, visuals, args->width, args->height);
        for (size_t i = 0; i < visuals->bars.count; i++) {
            draw_bar(engine, visuals->bars.data[i], args->width, args->height);
        }
    }
}

static void visuals_destroy(nmo_engine_t *engine, void *state)
{
    (void)engine;
    free(((visuals_t *)state)->filters.data);
    free(((visuals_t *)state)->bars.data);
    free(((visuals_t *)state)->scores.data);
}

nmo_status_t eng_register_visuals(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &visuals_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_visuals_blocks) : status;
}
