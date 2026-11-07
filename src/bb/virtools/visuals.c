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
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"
#include "../../engine/engine_character.h"
#include "type/nmo_type_guids.h"

/* A temporary render callback, for the frame: Render Curve's on its curve, Show Object
   Information's on the render context */
typedef struct visuals_callback {
    int32_t node;
    nmo_object_id_t target;
} visuals_callback_t;

typedef ENG_VEC(visuals_callback_t) callback_list_t;

/* A mark of Mark System: a single one's four corners (in its entity's space), or a band one's
   two (and its center) in the world */
typedef struct visuals_mark {
    eng_vector_t positions[4];
    float color[4];
    uint32_t packed;
    uint32_t mode;
    nmo_object_id_t entity;
    float delta_alpha;
    float lifespan;
    float fade_time;
    int32_t next, prev;
    float uvs[4]; /* left, top, right, bottom; a band uses the first two */
} visuals_mark_t;

/* The marks of a material, newest first */
typedef struct visuals_mark_list {
    nmo_object_id_t material;
    int32_t first, last;
} visuals_mark_list_t;

/* A Mark System's marks: a pool of Maximum Marks, and a list for each material in use */
typedef struct visuals_marks {
    int32_t node;
    visuals_mark_t *pool;
    int32_t size, count, free;
    ENG_VEC(visuals_mark_list_t) lists;
} visuals_marks_t;

/* A render pass a block asked for as it ran, made with the frame: Texture Render's scene from a
   view into a texture's slot, or a ShadowCaster's shadow (`node`) */
typedef struct visuals_pass_request {
    uint64_t frame; /* the engine frame it was asked in: only that frame's render makes it */
    int32_t node;
    bool shadow;
    nmo_object_id_t texture;
    int32_t slot;
    eng_scene_view_t view;
} visuals_pass_request_t;

/* A ShadowCaster's camera, shadow texture and material (ShadowCasterStruct), and what its shadow
   pass draws: the caster in black if in range, the distance's white and the soft gradient */
typedef struct visuals_caster {
    int32_t node;
    nmo_object_id_t camera, texture, material;
    bool render, soft;
    float delta;
} visuals_caster_t;

/* A Motion Blur's last world matrices of its target, the newest first (VAR_MAT) */
#define BLUR_TRACKS 30
typedef struct visuals_blur {
    int32_t node;
    int32_t n;
    eng_matrix_t tab[BLUR_TRACKS];
} visuals_blur_t;

/* A Simple Shadow's data: the texture it last gave its material, and the floors */
typedef struct visuals_shadow {
    int32_t node;
    nmo_object_id_t texture;
    eng_floor_shadow_t floor;
} visuals_shadow_t;

/* The blocks drawing in render callbacks: Planar Filter and Display Progression Bar after
   the sprites (PostSpriteRenderCallBacks), Display Score after the scene (PostRenderCallBack),
   Render Curve as its curve renders and Show Object Information after the transparent
   entities, both for the frame */
typedef struct visuals {
    ENG_VEC(int32_t) filters;
    ENG_VEC(int32_t) bars;
    ENG_VEC(int32_t) scores;
    callback_list_t curves;
    callback_list_t infos;
    ENG_VEC(struct visuals_shadow) shadows;
    ENG_VEC(visuals_marks_t) mark_systems;
    callback_list_t marks;
    ENG_VEC(struct visuals_blur) blurs;
    ENG_VEC(struct visuals_pass_request) passes;
    ENG_VEC(struct visuals_caster) casters;
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

/* CK3dEntity::GetCurrentMesh; 0 for none */
static nmo_object_id_t entity_current_mesh(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_object_t *object = id != 0 ? eng_find_object(engine, id) : NULL;
    const nmo_3dentity_state_t *state =
        object != NULL ? (const nmo_3dentity_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                             engine->types, object, CKPGUID_3DENTITY)
                       : NULL;
    return state != NULL ? eng_object_ref(engine, id, &state->current_mesh) : 0;
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

/* ============================================================================
 * Render Curve and Show Object Information: geometry drawn in render callbacks
 * ============================================================================ */

/* Add or Remove a temporary render callback of the block `node` on `target`: once each */
static void callback_drawn(callback_list_t *list, int32_t node, nmo_object_id_t target, bool drawn)
{
    for (size_t i = 0; i < list->count; i++) {
        if (list->data[i].node == node && list->data[i].target == target) {
            if (!drawn) {
                list->data[i] = list->data[--list->count];
            }
            return;
        }
    }
    visuals_callback_t *slot = NULL;
    if (drawn && ENG_VEC_PUSH(*list, slot)) {
        slot->node = node;
        slot->target = target;
    }
}

static eng_vector_t vec_sub(eng_vector_t a, eng_vector_t b)
{
    return (eng_vector_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static eng_vector_t vec_add(eng_vector_t a, eng_vector_t b)
{
    return (eng_vector_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

static eng_vector_t vec_scale(eng_vector_t a, float s)
{
    return (eng_vector_t){a.x * s, a.y * s, a.z * s};
}

/* VxVector Normalize */
static eng_vector_t vec_normalized(eng_vector_t a)
{
    eng_vector_normalize(&a);
    return a;
}

static eng_vector_t vec_cross_normalized(eng_vector_t a, eng_vector_t b)
{
    return vec_normalized(eng_vector_cross(&a, &b));
}

/* A vertex of a block's VxDrawPrimitiveData, in the space of the matrix it is drawn with */
typedef struct prim_vertex {
    eng_vector_t p, n;
    float u, v;
} prim_vertex_t;

static bool push_prim_vertex(nmo_engine_t *engine, const eng_matrix_t *matrix, const prim_vertex_t *src,
                             uint32_t color)
{
    nmo_engine_vertex_t *vertex = NULL;
    if (!ENG_VEC_PUSH(engine->render.vertices, vertex)) {
        return false;
    }
    eng_vector_t p, n;
    eng_vector_transform(&p, matrix, &src->p);
    eng_vector_rotate(&n, matrix, &src->n);
    eng_vector_normalize(&n);
    *vertex = (nmo_engine_vertex_t){p.x, p.y, p.z, src->u, src->v, color, n.x, n.y, n.z};
    return true;
}

/* The primitive draw of the vertices pushed since `first`, sorted at `position` when transparent */
static nmo_engine_draw_t *push_primitive(nmo_engine_t *engine, nmo_object_id_t object, int32_t first,
                                         eng_vector_t position)
{
    int32_t count = (int32_t)engine->render.vertices.count - first;
    nmo_engine_draw_t *draw = count > 0 ? eng_render_push(engine) : NULL;
    if (draw == NULL) {
        return NULL;
    }
    draw->kind = NMO_ENGINE_DRAW_PRIMITIVE; /* zeroed but for the render context's stencil */
    draw->object = object;
    draw->first_vertex = first;
    draw->vertex_count = count;
    draw->world[0] = draw->world[5] = draw->world[10] = draw->world[15] = 1.0f;
    draw->world[12] = position.x, draw->world[13] = position.y, draw->world[14] = position.z;
    return draw;
}

/* A VX_TRIANGLESTRIP of `v` through `indices` (NULL: in order) as triangles, every other one
   turned back, the degenerate ones joining strips left out */
static void push_strip(nmo_engine_t *engine, const eng_matrix_t *matrix, const prim_vertex_t *v,
                       const int32_t *indices, int32_t count, uint32_t color)
{
    for (int32_t k = 0; k + 2 < count; k++) {
        int32_t i0 = indices != NULL ? indices[k] : k, i1 = indices != NULL ? indices[k + 1] : k + 1;
        int32_t i2 = indices != NULL ? indices[k + 2] : k + 2;
        if (k & 1) {
            int32_t t = i0;
            i0 = i1;
            i1 = t;
        }
        if (i0 == i1 || i1 == i2 || i0 == i2) {
            continue;
        }
        if (!push_prim_vertex(engine, matrix, &v[i0], color) ||
            !push_prim_vertex(engine, matrix, &v[i1], color) ||
            !push_prim_vertex(engine, matrix, &v[i2], color)) {
            return;
        }
    }
}

/* The inputs of Render Curve as CurveRenderCallback reads them */
typedef struct curve_inputs {
    eng_vector_t up;
    float size[2], texscale[2], offset;
    int32_t tesselation;
    bool lighting;
} curve_inputs_t;

/* RenderPlane: a ribbon Size.x wide each side across Up carried along the curve */
static void curve_plane(nmo_engine_t *engine, const eng_matrix_t *world, const eng_vector_t *points,
                        int32_t tc, eng_vector_t pos, const curve_inputs_t *in, uint32_t color)
{
    prim_vertex_t *v = (prim_vertex_t *)calloc((size_t)(tc + 1) * 2, sizeof(*v));
    if (v == NULL) {
        return;
    }
    float delta = 1.0f / (float)tc, a = 0.0f;
    eng_vector_t up = in->up;
    for (int32_t i = 0; i <= tc; i++) {
        eng_vector_t old = pos;
        pos = points[i];
        eng_vector_t dir = vec_normalized(vec_sub(pos, old));
        eng_vector_t right = vec_scale(vec_cross_normalized(up, dir), in->size[0]);
        up = vec_cross_normalized(dir, right);
        float u = in->offset + a * in->texscale[0];
        v[2 * i] = (prim_vertex_t){vec_sub(pos, right), {0, 0, 0}, u, 0.0f};
        v[2 * i + 1] = (prim_vertex_t){vec_add(pos, right), {0, 0, 0}, u, in->texscale[1]};
        a += delta;
    }
    push_strip(engine, world, v, NULL, 2 * (tc + 1), color);
    free(v);
}

/* RenderCross: a ribbon Size.x along the up axis, then one Size.y along the right axis, both
   textured the same */
static void curve_cross(nmo_engine_t *engine, const eng_matrix_t *world, const eng_vector_t *points,
                        int32_t tc, eng_vector_t pos, const curve_inputs_t *in, uint32_t color)
{
    prim_vertex_t *v = (prim_vertex_t *)calloc((size_t)(tc + 1) * 4, sizeof(*v));
    if (v == NULL) {
        return;
    }
    prim_vertex_t *v2 = v + 2 * (tc + 1);
    float delta = 1.0f / (float)tc, a = 0.0f;
    for (int32_t i = 0; i <= tc; i++) {
        eng_vector_t old = pos;
        pos = points[i];
        eng_vector_t dir = vec_normalized(vec_sub(pos, old));
        eng_vector_t right = vec_cross_normalized(in->up, dir);
        eng_vector_t up = vec_cross_normalized(dir, right);
        float u = in->offset + a * in->texscale[0];
        eng_vector_t ux = vec_scale(up, in->size[0]), ry = vec_scale(right, in->size[1]);
        v[2 * i] = (prim_vertex_t){vec_add(pos, ux), {0, 0, 0}, u, 0.0f};
        v[2 * i + 1] = (prim_vertex_t){vec_sub(pos, ux), {0, 0, 0}, u, in->texscale[1]};
        v2[2 * i] = (prim_vertex_t){vec_add(pos, ry), {0, 0, 0}, u, 0.0f};
        v2[2 * i + 1] = (prim_vertex_t){vec_sub(pos, ry), {0, 0, 0}, u, in->texscale[1]};
        a += delta;
    }
    push_strip(engine, world, v, NULL, 2 * (tc + 1), color);
    push_strip(engine, world, v2, NULL, 2 * (tc + 1), color);
    free(v);
}

/* RenderTube: Tube Tesselation rings of the ellipse Size around the curve, a strip between each
   ring and the next, run forth and back */
static void curve_tube(nmo_engine_t *engine, const eng_matrix_t *world, const eng_vector_t *points,
                       int32_t tc, eng_vector_t start, const curve_inputs_t *in, uint32_t color)
{
    int32_t rtc = in->tesselation, n = tc + 1;
    prim_vertex_t *v = (prim_vertex_t *)calloc((size_t)rtc * (size_t)n, sizeof(*v));
    int32_t *indices = (int32_t *)malloc((size_t)rtc * 2u * (size_t)n * sizeof(int32_t));
    if (v == NULL || indices == NULL) {
        free(v);
        free(indices);
        return;
    }
    const float pi = 3.1415927f;
    float delta = 1.0f / (float)tc, pdelta = 2.0f * pi / (float)rtc, inv2pi = 1.0f / (2.0f * pi);
    float p = 0.0f;
    for (int32_t j = 0; j < rtc; j++) {
        float a = 0.0f;
        eng_vector_t pos = start;
        float psin = in->size[1] * sinf(p), pcos = in->size[0] * cosf(p);
        for (int32_t i = 0; i <= tc; i++) {
            eng_vector_t old = pos;
            pos = points[i];
            eng_vector_t dir = vec_normalized(vec_sub(pos, old));
            eng_vector_t right = vec_cross_normalized(in->up, dir);
            eng_vector_t up = vec_cross_normalized(dir, right);
            eng_vector_t offset = vec_add(vec_scale(right, pcos), vec_scale(up, psin));
            v[j * n + i] = (prim_vertex_t){vec_add(pos, offset), vec_normalized(offset),
                                           in->offset + a * in->texscale[0], in->texscale[1] * p * inv2pi};
            a += delta;
        }
        p += pdelta;
    }
    int32_t count = 0;
    for (int32_t j = 0; j < rtc; j++) {
        int32_t next = (j + 1) % rtc;
        for (int32_t i = 0; i <= tc; i++) {
            int32_t k = (j & 1) ? tc - i : i;
            indices[count++] = j * n + k;
            indices[count++] = next * n + k;
        }
    }
    push_strip(engine, world, v, indices, count, color);
    free(v);
    free(indices);
}

/* RenderTwoDim: in view space, a ribbon across the curve's projection Size.x wide at the
   render context's scale whatever its depth (the first segment leans to the 13th vertex, the
   one the DLL reads there) */
static void curve_two_dim(nmo_engine_t *engine, const eng_matrix_t *world, const eng_matrix_t *eye,
                          const eng_vector_t *points, int32_t tc, const curve_inputs_t *in,
                          uint32_t color, float width, float height)
{
    prim_vertex_t *v = (prim_vertex_t *)calloc((size_t)(tc + 1) * 2, sizeof(*v));
    eng_vector_t *view_points = (eng_vector_t *)malloc((size_t)(tc + 1) * sizeof(eng_vector_t));
    if (v == NULL || view_points == NULL) {
        free(v);
        free(view_points);
        return;
    }
    eng_matrix_t to_view;
    eng_matrix_inverse(&to_view, eye);
    for (int32_t i = 0; i <= tc; i++) {
        eng_vector_t w;
        eng_vector_transform(&w, world, &points[i]);
        eng_vector_transform(&view_points[i], &to_view, &w);
    }
    float dx = 1.0f / width, dy = 1.0f / height;
    float delta = 1.0f / (float)tc, a = 0.0f;
    eng_vector_t pos = view_points[0];
    pos = vec_sub(pos, vec_normalized(vec_sub(view_points[tc < 12 ? tc : 12], pos)));
    const eng_vector_t up = {0.0f, 0.0f, 1.0f};
    for (int32_t i = 0; i <= tc; i++) {
        eng_vector_t old = pos;
        pos = view_points[i];
        eng_vector_t dir = vec_normalized(vec_sub(pos, old));
        dir.z = 0.0f;
        eng_vector_t right = vec_scale(vec_cross_normalized(up, dir), in->size[0]);
        right.z = 0.0f;
        right.x *= pos.z * dx;
        right.y *= pos.z * dy;
        float u = in->offset + a * in->texscale[0];
        v[2 * i] = (prim_vertex_t){vec_sub(pos, right), {0, 0, 0}, u, in->texscale[1]};
        v[2 * i + 1] = (prim_vertex_t){vec_add(pos, right), {0, 0, 0}, u, 0.0f};
        a += delta;
    }
    push_strip(engine, eye, v, NULL, 2 * (tc + 1), color);
    free(v);
    free(view_points);
}

/* RenderLine: the curve's line mesh in the material's color, unlit and untextured */
static void curve_line(nmo_engine_t *engine, const eng_matrix_t *world, const eng_vector_t *points,
                       int32_t tc, uint32_t color)
{
    for (int32_t i = 0; i < tc; i++) {
        prim_vertex_t a = {points[i], {0, 0, 0}, 0.0f, 0.0f};
        prim_vertex_t b = {points[i + 1], {0, 0, 0}, 0.0f, 0.0f};
        if (!push_prim_vertex(engine, world, &a, color) || !push_prim_vertex(engine, world, &b, color)) {
            return;
        }
    }
}

enum { CURVE_PLANE, CURVE_CROSS, CURVE_TUBE, CURVE_TWODIM, CURVE_LINE };

/*
 * CurveRenderCallback: the curve's line mesh drawn as Render Mode says, with
 * Material set as current and its diffuse color on the vertices (white
 * without one). It runs as the curve renders: after the opaque entities, or
 * with the transparent ones when the curve renders last.
 */
static void draw_curve(nmo_engine_t *engine, const visuals_callback_t *callback,
                       const eng_render_args_t *args)
{
    nmo_engine_block_t handle = {engine, callback->node};
    blk_t *b = &handle;
    int32_t index = eng_entity_index(engine, callback->target);
    if (index < 0 || nmo_engine_object_class(engine, callback->target) != NMO_CID_CURVE ||
        !eng_render_entity_drawn(engine, callback->target)) {
        return;
    }
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 0);
    nmo_engine_material_t m;
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (material != 0 && nmo_engine_material(engine, material, &m)) {
        memcpy(diffuse, m.diffuse, sizeof(diffuse));
    } else {
        material = 0;
    }
    uint32_t color = rgbaf_to_color(diffuse);
    curve_inputs_t in = {.size = {1.0f, 1.0f}, .texscale = {1.0f, 1.0f}};
    nmo_engine_block_get_input_value(b, 1, in.size, sizeof(in.size));
    nmo_engine_block_get_input_value(b, 2, in.texscale, sizeof(in.texscale));
    in.offset = get_in_float(b, 3, 0.0f);
    in.up = get_in_vector(b, 4, 0.0f, 1.0f, 0.0f);
    in.lighting = get_in_int(b, 5, 0) != 0;
    in.tesselation = get_in_int(b, 6, 3);
    in.tesselation = in.tesselation < 3 ? 3 : in.tesselation;
    int32_t mode = get_local_int(b, 0, CURVE_CROSS);

    eng_vector_t *points = NULL;
    int32_t count = 0;
    if (!eng_curve_mesh_points(engine, callback->target, &points, &count)) {
        return;
    }
    int32_t tc = count - 1;
    eng_vector_t ahead, start;
    eng_curve_local_position(engine, callback->target, 0.01f, &ahead);
    eng_curve_local_position(engine, callback->target, 0.0f, &start);
    eng_vector_t before = vec_sub(start, vec_normalized(vec_sub(ahead, start)));

    const eng_entity_t *entity = &engine->entities.data[index];
    eng_matrix_t world = entity->world;
    eng_matrix_t eye;
    memcpy(eye.m, args->view, sizeof(eye.m));
    int32_t first = (int32_t)engine->render.vertices.count;
    switch (mode) {
    case CURVE_PLANE: curve_plane(engine, &world, points, tc, before, &in, color); break;
    case CURVE_CROSS: curve_cross(engine, &world, points, tc, before, &in, color); break;
    case CURVE_TUBE: curve_tube(engine, &world, points, tc, before, &in, color); break;
    case CURVE_TWODIM:
        curve_two_dim(engine, &world, &eye, points, tc, &in, color, args->width, args->height);
        break;
    case CURVE_LINE: curve_line(engine, &world, points, tc, color); break;
    default: break;
    }
    free(points);
    eng_vector_t position = {world.m[3][0], world.m[3][1], world.m[3][2]};
    nmo_engine_draw_t *draw = push_primitive(engine, callback->target, first, position);
    if (draw == NULL) {
        return;
    }
    entity = &engine->entities.data[index];
    draw->material = material;
    draw->lines = mode == CURVE_LINE;
    draw->lit = mode == CURVE_TUBE && in.lighting;
    draw->two_sided = mode == CURVE_TUBE || mode == CURVE_TWODIM; /* VXCULL_NONE */
    draw->transparent = (entity->moveable_flags & VX_MOVEABLE_RENDERLAST) != 0 &&
                        (entity->moveable_flags & VX_MOVEABLE_RENDERFIRST) == 0;
}

/*
 * Render Curve: On adds CurveRenderCallback to the curve's post render
 * callbacks for the frame, each frame until Off removes it; Exit On, Exit Off.
 */
static int bb_render_curve(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    bool entity = eng_entity_index(b->engine, target) >= 0;
    callback_list_t *curves = &visuals_state(b->engine)->curves;
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        if (entity) {
            callback_drawn(curves, b->node, target, false);
        }
        return NMO_ENGINE_BR_OK;
    }
    if (!entity) {
        return NMO_ENGINE_BR_OK;
    }
    callback_drawn(curves, b->node, target, true);
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* VxBbox::TransformFrom */
static void box_transform(eng_vector_t *min, eng_vector_t *max, const eng_matrix_t *m)
{
    const float low[3] = {min->x, min->y, min->z}, high[3] = {max->x, max->y, max->z};
    eng_vector_t center = {(low[0] + high[0]) * 0.5f, (low[1] + high[1]) * 0.5f, (low[2] + high[2]) * 0.5f};
    eng_vector_t c;
    eng_vector_transform(&c, m, &center);
    float half[3];
    for (int k = 0; k < 3; k++) {
        half[k] = (fabsf((high[0] - low[0]) * m->m[0][k]) + fabsf((high[1] - low[1]) * m->m[1][k]) +
                   fabsf((high[2] - low[2]) * m->m[2][k])) * 0.5f;
    }
    *min = (eng_vector_t){c.x - half[0], c.y - half[1], c.z - half[2]};
    *max = (eng_vector_t){c.x + half[0], c.y + half[1], c.z + half[2]};
}

static uint32_t input_color(blk_t *b, int pos)
{
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, pos, color, sizeof(color));
    return rgbaf_to_color(color);
}

/* The bounding box's twelve edges in Bounding Box Color, in the world or the entity's space */
static void info_box(nmo_engine_t *engine, blk_t *b, int32_t index, eng_vector_t position)
{
    bool local = get_in_int(b, 2, 0) != 0, hierarchical = get_in_int(b, 3, 0) != 0;
    const eng_matrix_t *world = &engine->entities.data[index].world;
    eng_vector_t min, max;
    bool valid;
    if (hierarchical) {
        valid = eng_entity_hierarchical_box(engine, index, &min, &max);
        if (valid && local) {
            eng_matrix_t inverse;
            eng_matrix_inverse(&inverse, world);
            box_transform(&min, &max, &inverse);
        }
    } else {
        valid = local ? eng_entity_local_box(engine, index, &min, &max)
                      : eng_entity_world_box(engine, index, &min, &max);
    }
    if (!valid) {
        return;
    }
    eng_matrix_t identity = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    const eng_matrix_t *matrix = local ? &engine->entities.data[index].world : &identity;
    uint32_t color = input_color(b, 1);
    const eng_vector_t corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z}};
    static const int edges[24] = {0, 1, 0, 3, 2, 1, 2, 3, 2, 6, 6, 7, 6, 5, 5, 1, 5, 4, 4, 7, 4, 0, 7, 3};
    int32_t first = (int32_t)engine->render.vertices.count;
    for (int i = 0; i < 24; i++) {
        prim_vertex_t v = {corners[edges[i]], {0, 0, 0}, 0.0f, 0.0f};
        if (!push_prim_vertex(engine, matrix, &v, color)) {
            return;
        }
    }
    nmo_engine_draw_t *draw = push_primitive(engine, engine->entities.data[index].id, first, position);
    if (draw != NULL) {
        draw->lines = true;
        draw->post = true;
    }
}

/* Lines from the mesh's vertices along their normals, or from its faces' centers along theirs:
   in a color, or lit with a material (a face's line gets its center for a normal, as the DLL
   writes it) */
static void info_normals(nmo_engine_t *engine, blk_t *b, int32_t index, const nmo_engine_mesh_t *mesh,
                         bool faces, eng_vector_t position)
{
    uint32_t color = input_color(b, faces ? 8 : 5);
    float size = get_in_float(b, faces ? 9 : 6, 1.0f);
    nmo_object_id_t material = local_object(b, faces ? 1 : 0);
    nmo_engine_material_t m;
    if (material != 0 && !nmo_engine_material(engine, material, &m)) {
        material = 0;
    }
    const eng_matrix_t *world = &engine->entities.data[index].world;
    int32_t count = faces ? mesh->face_count : mesh->vertex_count;
    int32_t first = (int32_t)engine->render.vertices.count;
    for (int32_t i = 0; i < count; i++) {
        eng_vector_t from, normal, lit_normal;
        if (faces) {
            const nmo_vector_t *n = &mesh->faces[i].normal;
            const uint16_t *f = &mesh->indices[3 * i];
            eng_vector_t sum = {0, 0, 0};
            for (int k = 0; k < 3; k++) {
                const nmo_vector_t *p = &mesh->vertices[f[k]].position;
                sum = vec_add(sum, (eng_vector_t){p->x, p->y, p->z});
            }
            from = vec_scale(sum, 0.33333f);
            normal = (eng_vector_t){n->x, n->y, n->z};
            lit_normal = from;
        } else {
            const nmo_vertex_t *v = &mesh->vertices[i];
            from = (eng_vector_t){v->position.x, v->position.y, v->position.z};
            normal = (eng_vector_t){v->normal.x, v->normal.y, v->normal.z};
            lit_normal = vec_normalized(normal);
        }
        prim_vertex_t a = {from, lit_normal, 0.0f, 0.0f};
        prim_vertex_t c = {vec_add(from, vec_scale(normal, size)), lit_normal, 0.0f, 0.0f};
        if (!push_prim_vertex(engine, world, &a, color) || !push_prim_vertex(engine, world, &c, color)) {
            return;
        }
    }
    nmo_engine_draw_t *draw = push_primitive(engine, engine->entities.data[index].id, first, position);
    if (draw != NULL) {
        draw->lines = true;
        draw->post = true;
        draw->material = material;
        draw->lit = material != 0;
    }
}

/* The mesh's bounding sphere: three circles of 32 points around its barycenter, in the XY, YZ
   and ZX planes */
static void info_sphere(nmo_engine_t *engine, blk_t *b, int32_t index, nmo_object_id_t mesh,
                        eng_vector_t position)
{
    eng_vector_t center;
    float radius = 0.0f;
    if (!eng_mesh_sphere(engine, mesh, &center, &radius)) {
        return;
    }
    uint32_t color = input_color(b, 11);
    const eng_matrix_t *world = &engine->entities.data[index].world;
    const int count = 32;
    float delta = 3.1415927f * 2.0f / (float)(count - 1);
    int32_t first = (int32_t)engine->render.vertices.count;
    for (int axis0 = 0, axis1 = 1, circle = 0; circle < 3; circle++) {
        prim_vertex_t points[32];
        float t = 0.0f;
        for (int j = 0; j < count; j++, t += delta) {
            float p[3] = {center.x, center.y, center.z};
            p[axis0] += radius * cosf(t);
            p[axis1] += radius * sinf(t);
            points[j] = (prim_vertex_t){{p[0], p[1], p[2]}, {0, 0, 0}, 0.0f, 0.0f};
        }
        for (int j = 0; j + 1 < count; j++) {
            if (!push_prim_vertex(engine, world, &points[j], color) ||
                !push_prim_vertex(engine, world, &points[j + 1], color)) {
                return;
            }
        }
        axis0++;
        axis1 = axis1 == 2 ? 0 : axis1 + 1;
    }
    nmo_engine_draw_t *draw = push_primitive(engine, engine->entities.data[index].id, first, position);
    if (draw != NULL) {
        draw->lines = true;
        draw->post = true;
    }
}

/*
 * ShowBoundingBoxRenderCallBack: the target's bounding box, its mesh's vertex
 * and face normals and its bounding sphere, as lines after the transparent
 * entities. (The original's projection offset against Z fighting is in the
 * Ballanced source only, not in Ballance's DLL.)
 */
static void draw_info(nmo_engine_t *engine, int32_t node)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t index = eng_entity_index(engine, target);
    if (index < 0) {
        return;
    }
    const eng_matrix_t *world = &engine->entities.data[index].world;
    eng_vector_t position = {world->m[3][0], world->m[3][1], world->m[3][2]};
    if (get_in_int(b, 0, 1) != 0) {
        info_box(engine, b, index, position);
    }
    nmo_object_id_t mesh_id = entity_current_mesh(engine, target);
    nmo_engine_mesh_t mesh;
    bool has_mesh = mesh_id != 0 && nmo_engine_mesh(engine, mesh_id, &mesh);
    if (has_mesh && get_in_int(b, 4, 0) != 0) {
        info_normals(engine, b, index, &mesh, false, position);
    }
    if (has_mesh && get_in_int(b, 7, 0) != 0) {
        info_normals(engine, b, index, &mesh, true, position);
    }
    if (has_mesh && get_in_int(b, 10, 0) != 0) {
        info_sphere(engine, b, index, mesh_id, position);
    }
}

/* Show Object Information: In adds the render callback for the next frame (looped to keep
   it); Out */
static int bb_show_object_information(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (eng_entity_index(b->engine, target) >= 0) {
        callback_drawn(&visuals_state(b->engine)->infos, b->node, 0, true);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Simple Shadow: its material (white, its texture clamped and copied) is made
 * as it loads; while active, each frame it goes on the floors (objects with
 * the Floor attribute, shown) under the owner as a channel multiplying them,
 * with UVs from the owner's position over each floor, and off the floors the
 * owner left. Off, a pause, reset or the script's deactivation take it off
 * the floors. The DLL also skips floors that were all outside the last
 * frame's frustum; the engine keeps no frustum test of the last render.
 */
static visuals_shadow_t *simple_shadow(nmo_engine_t *engine, int32_t node)
{
    visuals_t *visuals = visuals_state(engine);
    for (size_t i = 0; i < visuals->shadows.count; i++) {
        if (visuals->shadows.data[i].node == node) {
            return &visuals->shadows.data[i];
        }
    }
    visuals_shadow_t *shadow = NULL;
    nmo_object_id_t material = eng_floor_shadow_material(engine, "SimpleShadow Material");
    if (material == 0 || !ENG_VEC_PUSH(visuals->shadows, shadow)) {
        return NULL;
    }
    memset(shadow, 0, sizeof(*shadow));
    shadow->node = node;
    shadow->texture = (nmo_object_id_t)-1;
    shadow->floor.material = material;
    return shadow;
}

static int bb_simple_shadow(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    visuals_shadow_t *shadow = simple_shadow(engine, b->node);
    if (shadow == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, nmo_engine_block_output_count(b) >= 2 ? 1 : 0);
        eng_floor_shadow_remove(engine, &shadow->floor);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t texture = nmo_engine_block_get_input_object(b, 0);
    if (texture != shadow->texture) {
        eng_material_t *material = eng_material(engine, shadow->floor.material);
        shadow->texture = texture;
        if (material != NULL) {
            material->desc.texture = texture;
        }
    }
    eng_floor_shadow_place(engine, &shadow->floor, nmo_engine_block_owner(b), get_in_float(b, 1, 2.0f),
                           get_in_float(b, 2, 100.0f));
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_simple_shadow(blk_t *b, uint32_t message)
{
    visuals_shadow_t *shadow = simple_shadow(b->engine, b->node);
    if (shadow != NULL && (message == NMO_ENGINE_MSG_PAUSE || message == NMO_ENGINE_MSG_RESET ||
                           message == NMO_ENGINE_MSG_DEACTIVATESCRIPT)) {
        eng_floor_shadow_remove(b->engine, &shadow->floor);
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Solid Trail: as it loads, a trail entity is made in the level scene with a
 * mesh of 41 vertex pairs and 80 faces between them (prelit, transparent; its
 * material white at alpha 0.1, added (ONE, ONE), two sided, no z write), all
 * at the two vertices of the owner's mesh farthest apart, in the world. On
 * puts the pairs back there and shows it, Off hides it. While active, every
 * Frame Delay frames each pair takes the place of the one before it, closer
 * to its partner by the Width Attenuation, and the front pair takes the two
 * vertices where they are now; the colors fade from the Color at the front
 * to black over the last Length of the pairs, so the added trail fades out.
 * Its locals are CK's: the trail entity, the two vertex indices, the frame
 * delay count and whether it is active.
 */
#define TRAIL_TRACKS 40

/* The trail's mesh copy, where the vertices move; NULL when there is none */
static eng_mesh_copy_t *trail_mesh(nmo_engine_t *engine, blk_t *b)
{
    nmo_object_id_t mesh = entity_current_mesh(engine, (nmo_object_id_t)get_local_int(b, 0, 0));
    eng_mesh_copy_t *copy = mesh != 0 ? eng_mesh_copy(engine, mesh) : NULL;
    nmo_engine_mesh_t desc;
    if (copy == NULL || !nmo_engine_mesh(engine, mesh, &desc) || desc.vertex_count != 2 + TRAIL_TRACKS * 2) {
        return NULL;
    }
    if (copy->colors == NULL) {
        copy->colors = (uint32_t *)malloc((size_t)desc.vertex_count * sizeof(uint32_t));
        if (copy->colors == NULL) {
            return NULL;
        }
        memcpy(copy->colors, desc.colors, (size_t)desc.vertex_count * sizeof(uint32_t));
    }
    return copy;
}

/* The owner's vertices `li` and `lj` in the world; false without its mesh */
static bool trail_ends(nmo_engine_t *engine, blk_t *b, eng_vector_t *a, eng_vector_t *c)
{
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int32_t index = eng_entity_index(engine, owner);
    nmo_engine_mesh_t mesh;
    int32_t li = get_local_int(b, 1, 0), lj = get_local_int(b, 2, 0);
    if (index < 0 || !nmo_engine_mesh(engine, entity_current_mesh(engine, owner), &mesh) || li < 0 ||
        lj < 0 || li >= mesh.vertex_count || lj >= mesh.vertex_count) {
        return false;
    }
    const nmo_vector_t *p = &mesh.vertices[li].position, *q = &mesh.vertices[lj].position;
    eng_vector_t local_a = {p->x, p->y, p->z}, local_c = {q->x, q->y, q->z};
    eng_vector_transform(a, &engine->entities.data[index].world, &local_a);
    eng_vector_transform(c, &engine->entities.data[index].world, &local_c);
    return true;
}

/* ResetMesh: every pair at the owner's two vertices */
static void trail_reset(nmo_engine_t *engine, blk_t *b)
{
    eng_mesh_copy_t *copy = trail_mesh(engine, b);
    eng_vector_t a, c;
    if (copy == NULL || !trail_ends(engine, b, &a, &c)) {
        return;
    }
    for (int i = 0; i < TRAIL_TRACKS + 1; i++) {
        copy->vertices[i * 2].position = (nmo_vector_t){a.x, a.y, a.z};
        copy->vertices[i * 2 + 1].position = (nmo_vector_t){c.x, c.y, c.z};
    }
    copy->version = ++engine->render.mesh_version;
}

/* The trail entity, its mesh and material, as CKM_BEHAVIORLOAD makes them */
static void trail_create(nmo_engine_t *engine, blk_t *b)
{
    nmo_object_id_t owner = nmo_engine_block_owner(b);
    int32_t index = eng_entity_index(engine, owner);
    nmo_engine_mesh_t source;
    if (index < 0 || !nmo_engine_mesh(engine, entity_current_mesh(engine, owner), &source)) {
        return;
    }
    /* The two vertices farthest apart */
    float max = 0.0f;
    int32_t li = 0, lj = 0;
    for (int32_t i = 0; i < source.vertex_count - 1; i++) {
        for (int32_t j = i; j < source.vertex_count; j++) {
            const nmo_vector_t *p = &source.vertices[i].position, *q = &source.vertices[j].position;
            float dx = p->x - q->x, dy = p->y - q->y, dz = p->z - q->z;
            float mag = dx * dx + dy * dy + dz * dz;
            if (mag > max) {
                max = mag;
                li = i;
                lj = j;
            }
        }
    }
    const eng_matrix_t *world = &engine->entities.data[index].world;
    eng_vector_t first = {0.0f, 0.0f, 0.0f}, second = {0.0f, 0.0f, 0.0f};
    if (source.vertex_count > 0) {
        const nmo_vector_t *p = &source.vertices[li].position, *q = &source.vertices[lj].position;
        eng_vector_t a = {p->x, p->y, p->z}, c = {q->x, q->y, q->z};
        eng_vector_transform(&first, world, &a);
        eng_vector_transform(&second, world, &c);
    }
    nmo_mesh_state_t *mesh = NULL;
    nmo_object_id_t mesh_id = eng_create_mesh(engine, "STMesh", 2 + TRAIL_TRACKS * 2, TRAIL_TRACKS * 2, &mesh);
    if (mesh_id == 0) {
        return;
    }
    float color[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 2, color, sizeof(color));
    uint32_t argb = 0xFF000000u | ((uint32_t)((int)(255.0f * color[0]) & 0xFF) << 16) |
                    ((uint32_t)((int)(255.0f * color[1]) & 0xFF) << 8) | (uint32_t)((int)(255.0f * color[2]) & 0xFF);
    mesh->flags |= VXMESH_PRELITMODE | VXMESH_FORCETRANSPARENCY;
    for (int32_t v = 0; v < 2 + TRAIL_TRACKS * 2; v++) {
        const eng_vector_t *at = (v & 1) ? &second : &first;
        mesh->vertices[v].position = (nmo_vector_t){at->x, at->y, at->z};
        mesh->vertex_colors[v] = argb;
        mesh->vertex_specular[v] = 0;
        mesh->vertices[v].uv.x = (float)(v & 1);
        mesh->vertices[v].uv.y = v < 2 ? 0.0f : (float)(((v - 2) / 2 / 2) % 2);
    }
    for (int32_t i = 0; i < TRAIL_TRACKS; i++) {
        const uint16_t faces[2][3] = {{(uint16_t)(i * 2), (uint16_t)((i + 1) * 2), (uint16_t)(i * 2 + 1)},
                                      {(uint16_t)((i + 1) * 2), (uint16_t)((i + 1) * 2 + 1), (uint16_t)(i * 2 + 1)}};
        memcpy(&mesh->face_vertex_indices[i * 6], faces, sizeof(faces));
    }
    /* BuildFaceNormals */
    for (int32_t f = 0; f < TRAIL_TRACKS * 2; f++) {
        const uint16_t *k = &mesh->face_vertex_indices[f * 3];
        eng_vector_t p0 = {mesh->vertices[k[0]].position.x, mesh->vertices[k[0]].position.y, mesh->vertices[k[0]].position.z};
        eng_vector_t p1 = {mesh->vertices[k[1]].position.x, mesh->vertices[k[1]].position.y, mesh->vertices[k[1]].position.z};
        eng_vector_t p2 = {mesh->vertices[k[2]].position.x, mesh->vertices[k[2]].position.y, mesh->vertices[k[2]].position.z};
        eng_vector_t n = vec_cross_normalized(vec_sub(p1, p0), vec_sub(p2, p0));
        mesh->faces[f].normal = (nmo_vector_t){n.x, n.y, n.z};
    }
    nmo_object_id_t material_id = eng_create_object(engine, NMO_CID_MATERIAL, "STMaterial");
    eng_material_t *material = material_id != 0 ? eng_material(engine, material_id) : NULL;
    if (material == NULL && material_id != 0) {
        material = eng_material_new(engine, material_id);
    }
    if (material != NULL) {
        const float diffuse[4] = {1.0f, 1.0f, 1.0f, 0.1f};
        memcpy(material->desc.diffuse, diffuse, sizeof(diffuse));
        material->desc.source_blend = VXBLEND_ONE;
        material->desc.dest_blend = VXBLEND_ONE;
        material->desc.alpha_blend = true;
        material->desc.z_write = false;
        material->desc.two_sided = true;
        material->desc.texture = 0;
        eng_mesh_apply_material(engine, mesh_id, material_id);
    }
    nmo_object_id_t entity = eng_create_object(engine, NMO_CID_3DENTITY, "STEntity");
    if (entity == 0) {
        return;
    }
    eng_entity_set_current_mesh(engine, entity, mesh_id);
    set_local_int(b, 0, (int32_t)entity);
    set_local_int(b, 1, li);
    set_local_int(b, 2, lj);
    set_local_int(b, 4, 0);
}

static int bb_solid_trail(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = (nmo_object_id_t)get_local_int(b, 0, 0);
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        eng_show(engine, entity, 0);
        set_local_int(b, 4, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        trail_reset(engine, b);
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_show(engine, entity, 1);
        set_local_int(b, 4, 1);
    }
    int32_t count = get_local_int(b, 3, 0);
    if (count != 0) {
        set_local_int(b, 3, count - 1);
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    int32_t delay = get_in_int(b, 3, 1);
    set_local_int(b, 3, (delay != 0 ? delay : 1) - 1);
    float width = get_in_float(b, 0, 0.02f), length = get_in_float(b, 1, 0.9f);
    float color[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 2, color, sizeof(color));
    eng_mesh_copy_t *copy = trail_mesh(engine, b);
    eng_vector_t a, c;
    if (copy == NULL || !trail_ends(engine, b, &a, &c)) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    nmo_object_id_t material = nmo_engine_mesh_material(engine, copy->mesh, 0);
    eng_material_t *trail_material = material != 0 ? eng_material(engine, material) : NULL;
    if (trail_material != NULL) {
        trail_material->desc.texture = nmo_engine_block_get_input_object(b, 4);
    }
    float la = 0.0f;
    for (int i = TRAIL_TRACKS; i > 0; i--) {
        nmo_vector_t *second = &copy->vertices[(i - 1) * 2 + 1].position;
        nmo_vector_t *first = &copy->vertices[(i - 1) * 2].position;
        nmo_vector_t fts = {(second->x - first->x) * width, (second->y - first->y) * width,
                            (second->z - first->z) * width};
        copy->vertices[i * 2].position = (nmo_vector_t){first->x + fts.x, first->y + fts.y, first->z + fts.z};
        copy->vertices[i * 2 + 1].position =
            (nmo_vector_t){second->x - fts.x, second->y - fts.y, second->z - fts.z};
        const float faded[4] = {color[0] * la, color[1] * la, color[2] * la, 1.0f};
        copy->colors[i * 2] = copy->colors[i * 2 + 1] = rgbaf_to_color(faded);
        if ((float)i < (float)TRAIL_TRACKS * length) {
            la += 1.0f / ((float)TRAIL_TRACKS * length);
        }
    }
    const float front[4] = {color[0], color[1], color[2], 1.0f};
    copy->vertices[0].position = (nmo_vector_t){a.x, a.y, a.z};
    copy->vertices[1].position = (nmo_vector_t){c.x, c.y, c.z};
    copy->colors[0] = copy->colors[1] = rgbaf_to_color(front);
    copy->version = ++engine->render.mesh_version;
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_solid_trail(blk_t *b, uint32_t message)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t entity = (nmo_object_id_t)get_local_int(b, 0, 0);
    switch (message) {
    case NMO_ENGINE_MSG_LOAD:
        trail_create(engine, b);
        break;
    case NMO_ENGINE_MSG_PAUSE:
        eng_show(engine, entity, 0);
        break;
    case NMO_ENGINE_MSG_RESUME:
    case NMO_ENGINE_MSG_ACTIVATESCRIPT:
        if (get_local_int(b, 4, 0) != 0) {
            eng_show(engine, entity, 1);
        }
        trail_reset(engine, b);
        break;
    case NMO_ENGINE_MSG_RESET:
        trail_reset(engine, b);
        break;
    case NMO_ENGINE_MSG_DEACTIVATESCRIPT:
        if (get_local_int(b, 4, 0) != 0) {
            eng_show(engine, entity, 0);
        }
        break;
    default:
        break;
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Mark System: marks left on surfaces, each a quad of the Material's (one
 * draw a material, no culling, in the marks' colors, drawn with the depth
 * nudged toward the eye against z fighting). Mark adds one at Position along
 * Normal, Size wide: a single mark, a square in the Referential's space (its
 * UVs the whole texture unless UVs says), or with Continuous Marks a band
 * mark in the world, joined to the one before it unless that one ended the
 * band (End Mark ends it). A mark lives Lifespan ms (0: forever) and fades
 * out over its last Fading Time. On clears the marks; while active the block
 * draws them each frame after the opaque objects, or right after the
 * Supporting Entity renders when there is one, and they age only when drawn.
 * A single mark is not added once Maximum Marks are in use; a band mark
 * takes the place of its material's oldest. The DLL does not draw a single
 * mark whose entity was outside the last frame's frustum; the engine keeps
 * no frustum test of the last render, so it draws them all. The DLL keeps
 * the materials in a hash table and draws them in its order; the engine
 * draws them in the order they were first used.
 */
#define MARK_SINGLE 0
#define MARK_BAND_START 1
#define MARK_BAND_IN 2
#define MARK_BAND_END 3
#define MARK_STYLE 3
#define MARK_STANDARD_UVS 4
#define MARK_EPSILON 1.1920929e-7f

static visuals_marks_t *mark_system(nmo_engine_t *engine, int32_t node)
{
    visuals_t *visuals = visuals_state(engine);
    for (size_t i = 0; i < visuals->mark_systems.count; i++) {
        if (visuals->mark_systems.data[i].node == node) {
            return &visuals->mark_systems.data[i];
        }
    }
    visuals_marks_t *marks = NULL;
    if (!ENG_VEC_PUSH(visuals->mark_systems, marks)) {
        return NULL;
    }
    memset(marks, 0, sizeof(*marks));
    marks->node = node;
    marks->free = -1;
    return marks;
}

/* MarkSystem::Clear: every mark free */
static void marks_clear(visuals_marks_t *marks)
{
    for (int32_t i = 0; i < marks->size; i++) {
        marks->pool[i].next = i + 1 < marks->size ? i + 1 : -1;
    }
    marks->free = marks->size > 0 ? 0 : -1;
    marks->count = 0;
    marks->lists.count = 0;
}

/* MarkSystem::CheckSize */
static void marks_check_size(visuals_marks_t *marks, int32_t size)
{
    size = size > 0 ? size : 0;
    if (size == marks->size) {
        return;
    }
    visuals_mark_t *pool = size > 0 ? (visuals_mark_t *)calloc((size_t)size, sizeof(visuals_mark_t)) : NULL;
    free(marks->pool);
    marks->pool = pool;
    marks->size = pool != NULL ? size : 0;
    marks_clear(marks);
}

/* m_MaterialMarks.InsertUnique(material) */
static int32_t marks_list(visuals_marks_t *marks, nmo_object_id_t material)
{
    for (size_t i = 0; i < marks->lists.count; i++) {
        if (marks->lists.data[i].material == material) {
            return (int32_t)i;
        }
    }
    visuals_mark_list_t *list = NULL;
    if (!ENG_VEC_PUSH(marks->lists, list)) {
        return -1;
    }
    *list = (visuals_mark_list_t){material, -1, -1};
    return (int32_t)marks->lists.count - 1;
}

/* FreeLastMark: the list's oldest mark taken out of it; -1 for an empty list */
static int32_t marks_free_last(visuals_marks_t *marks, visuals_mark_list_t *list)
{
    if (list->first < 0) {
        return -1;
    }
    int32_t m = list->last;
    visuals_mark_t *mark = &marks->pool[m];
    if (list->first == list->last) {
        list->first = list->last = -1;
        return m;
    }
    if ((mark->mode & MARK_STYLE) == MARK_BAND_START && mark->prev >= 0) {
        marks->pool[mark->prev].mode = MARK_BAND_START;
    }
    marks->pool[mark->prev].next = mark->next;
    list->last = mark->prev;
    if (mark->next >= 0) {
        marks->pool[mark->next].prev = mark->prev;
    }
    mark->next = mark->prev = -1;
    return m;
}

/* AddNewMark: first in the list */
static void marks_add_new(visuals_marks_t *marks, visuals_mark_list_t *list, int32_t m)
{
    visuals_mark_t *mark = &marks->pool[m];
    mark->prev = -1;
    mark->next = list->first;
    if (list->first >= 0) {
        marks->pool[list->first].prev = m;
    } else {
        list->last = m;
    }
    list->first = m;
}

static eng_vector_t marks_v(float x, float y, float z)
{
    return (eng_vector_t){x, y, z};
}

static void marks_set_color(visuals_mark_t *mark, const float color[4], float fade_time)
{
    memcpy(mark->color, color, sizeof(mark->color));
    mark->packed = vxcolor_to_color(color);
    mark->delta_alpha = -color[3] / fade_time;
}

/* AddSingleMark */
static void marks_add_single(nmo_engine_t *engine, visuals_marks_t *marks, eng_vector_t point, eng_vector_t normal,
                             nmo_object_id_t ref, const float color[4], float size, float life,
                             nmo_object_id_t material, float fade_time, const float uvs[4], float offset)
{
    if (marks->free < 0) {
        return;
    }
    int32_t l = marks_list(marks, material);
    if (l < 0) {
        return;
    }
    int32_t m = marks->free;
    marks->free = marks->pool[m].next;
    marks->count++;
    marks_add_new(marks, &marks->lists.data[l], m);
    visuals_mark_t *mark = &marks->pool[m];
    mark->entity = ref;
    int32_t index = ref != 0 ? eng_entity_index(engine, ref) : -1;
    eng_vector_t scale = {1.0f, 1.0f, 1.0f};
    if (index >= 0) {
        scale = eng_entity_scale(engine, index, false);
        offset /= eng_vector_magnitude(&scale);
        scale = marks_v(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
    }
    point = vec_add(point, vec_scale(normal, offset));
    float half = size * 0.5f;
    eng_vector_t y = {0.0f, 1.0f, 0.0f};
    eng_vector_t v = fabsf(normal.x) < MARK_EPSILON && fabsf(normal.z) < MARK_EPSILON
                         ? marks_v(0.0f, 0.0f, 1.0f)
                         : vec_cross_normalized(y, normal);
    eng_vector_t w = vec_cross_normalized(normal, v);
    v = vec_scale(v, half);
    w = vec_scale(w, half);
    if (index >= 0) {
        v = marks_v(v.x * scale.x, v.y * scale.y, v.z * scale.z);
        w = marks_v(w.x * scale.x, w.y * scale.y, w.z * scale.z);
    }
    mark->positions[0] = vec_add(vec_add(point, v), w);
    mark->positions[1] = vec_add(vec_sub(point, v), w);
    mark->positions[2] = vec_sub(vec_add(point, v), w);
    mark->positions[3] = vec_sub(vec_sub(point, v), w);
    mark->lifespan = life;
    mark->fade_time = fade_time;
    marks_set_color(mark, color, fade_time);
    mark->mode = MARK_SINGLE;
    if (uvs[0] == uvs[2] || uvs[3] == uvs[1]) { /* VxRect::IsEmpty */
        mark->mode |= MARK_STANDARD_UVS;
    } else {
        memcpy(mark->uvs, uvs, sizeof(mark->uvs));
    }
}

/* AddContinuousMark */
static void marks_add_band(nmo_engine_t *engine, visuals_marks_t *marks, eng_vector_t point, eng_vector_t normal,
                           nmo_object_id_t ref, const float color[4], float size, float life, float fade_time,
                           bool last, nmo_object_id_t material, float offset)
{
    int32_t index = ref != 0 ? eng_entity_index(engine, ref) : -1;
    if (index >= 0) {
        eng_vector_t p = point, n = normal;
        eng_vector_transform(&point, &engine->entities.data[index].world, &p);
        eng_vector_rotate(&normal, &engine->entities.data[index].world, &n);
    }
    if (normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f) {
        normal = marks_v(0.0f, 1.0f, 0.0f);
    }
    normal = vec_normalized(normal);
    point = vec_add(point, vec_scale(normal, offset));
    int32_t l = marks_list(marks, material);
    if (l < 0) {
        return;
    }
    int32_t old = marks->lists.data[l].first;
    bool first = true, second = false;
    eng_vector_t last_position = {0.0f, 0.0f, 0.0f};
    if (old >= 0 && (marks->pool[old].mode & MARK_STYLE) != MARK_BAND_END) {
        second = (marks->pool[old].mode & MARK_STYLE) == MARK_BAND_START;
        first = false;
        last_position = marks->pool[old].positions[2];
        eng_vector_t d = vec_sub(last_position, point);
        if (d.x * d.x + d.y * d.y + d.z * d.z <= 0.01f) {
            return; /* not moved enough */
        }
    }
    if (first && last) {
        return;
    }
    int32_t m = marks->free;
    if (m >= 0) {
        marks->free = marks->pool[m].next;
        marks->count++;
    } else {
        m = marks_free_last(marks, &marks->lists.data[l]);
        if (m < 0) {
            return;
        }
    }
    marks_add_new(marks, &marks->lists.data[l], m);
    visuals_mark_t *mark = &marks->pool[m];
    float half = size * 0.5f;
    eng_vector_t dir = first ? marks_v(0.0f, 1.0f, 0.0f) : vec_normalized(vec_sub(point, last_position));
    eng_vector_t v = vec_scale(vec_cross_normalized(dir, normal), half);
    mark->positions[0] = vec_add(point, v);
    mark->positions[1] = vec_sub(point, v);
    mark->positions[2] = point;
    mark->lifespan = life;
    mark->fade_time = fade_time;
    if (second && old != m) {
        marks->pool[old].positions[0] = vec_add(marks->pool[old].positions[2], v);
        marks->pool[old].positions[1] = vec_sub(marks->pool[old].positions[2], v);
    }
    if (!first && mark->next >= 0) {
        const visuals_mark_t *before = &marks->pool[mark->next];
        float start = (before->uvs[0] + before->uvs[2]) * 0.5f;
        eng_vector_t from = before->positions[2];
        eng_vector_t d = vec_normalized(vec_sub(mark->positions[2], from));
        eng_vector_t p1 = vec_sub(mark->positions[1], from), p0 = vec_sub(mark->positions[0], from);
        mark->uvs[0] = start + p1.x * d.x + p1.y * d.y + p1.z * d.z;
        mark->uvs[2] = start + p0.x * d.x + p0.y * d.y + p0.z * d.z;
    } else {
        mark->uvs[0] = mark->uvs[2] = 0.0f;
    }
    marks_set_color(mark, color, fade_time);
    mark->mode = first ? MARK_BAND_START : last ? MARK_BAND_END : MARK_BAND_IN;
}

/* MarkSystem::Update: the dead marks freed, the others aged and faded, empty lists dropped */
static void marks_update(visuals_marks_t *marks, float delta)
{
    for (size_t l = 0; l < marks->lists.count;) {
        visuals_mark_list_t *list = &marks->lists.data[l];
        int32_t m = list->first;
        while (m >= 0) {
            visuals_mark_t *mark = &marks->pool[m];
            if (mark->lifespan < 0.0f) {
                if ((mark->mode & MARK_STYLE) == MARK_BAND_START && mark->prev >= 0) {
                    marks->pool[mark->prev].mode = MARK_BAND_START;
                }
                int32_t next = mark->next;
                if (mark->prev >= 0) {
                    marks->pool[mark->prev].next = mark->next;
                } else {
                    list->first = mark->next;
                }
                if (mark->next >= 0) {
                    marks->pool[mark->next].prev = mark->prev;
                } else {
                    list->last = mark->prev;
                }
                mark->next = marks->free;
                marks->free = m;
                marks->count--;
                m = next;
                continue;
            }
            if (fabsf(mark->lifespan) > MARK_EPSILON) { /* not a mark forever */
                mark->lifespan -= delta;
                if (fabsf(mark->lifespan) < MARK_EPSILON) {
                    mark->lifespan -= 1.0f;
                }
                if (mark->lifespan < mark->fade_time) {
                    mark->color[3] += mark->delta_alpha * delta;
                    mark->packed = vxcolor_to_color(mark->color);
                }
            }
            m = mark->next;
        }
        if (list->first < 0) {
            memmove(list, list + 1, (marks->lists.count - l - 1) * sizeof(*list));
            marks->lists.count--;
        } else {
            l++;
        }
    }
}

/* MarkSystem::RenderMarkSystem, as the render context's or the Supporting Entity's post render
   callback */
static void draw_marks(nmo_engine_t *engine, const visuals_callback_t *callback, bool transparent)
{
    nmo_engine_block_t handle = {engine, callback->node};
    blk_t *b = &handle;
    visuals_marks_t *marks = mark_system(engine, callback->node);
    if (marks == NULL) {
        return;
    }
    marks_update(marks, engine->delta);
    static const float single_uvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    float factor = get_in_float(b, 10, 0.2f);
    size_t capacity = (size_t)(marks->count > 0 ? marks->count : 1) * 4u;
    prim_vertex_t *vertices = (prim_vertex_t *)malloc(capacity * sizeof(prim_vertex_t));
    uint32_t *colors = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    int32_t *indices = (int32_t *)malloc(capacity / 4u * 6u * sizeof(int32_t));
    eng_matrix_t identity;
    eng_matrix_identity(&identity);
    for (size_t l = 0; vertices != NULL && colors != NULL && indices != NULL && l < marks->lists.count; l++) {
        const visuals_mark_list_t *list = &marks->lists.data[l];
        int32_t vertex_count = 0, index_count = 0;
        for (int32_t m = list->first; m >= 0; m = marks->pool[m].next) {
            visuals_mark_t *mark = &marks->pool[m];
            if ((mark->mode & MARK_STYLE) == MARK_SINGLE) {
                eng_vector_t corners[4];
                memcpy(corners, mark->positions, sizeof(corners));
                if (mark->entity != 0) {
                    int32_t index = eng_entity_index(engine, mark->entity);
                    if (index < 0 || engine->entities.data[index].destroyed) {
                        mark->lifespan = -1.0f; /* its referential was destroyed */
                        continue;
                    }
                    for (int k = 0; k < 4; k++) {
                        eng_vector_transform(&corners[k], &engine->entities.data[index].world,
                                             &mark->positions[k]);
                    }
                }
                for (int k = 0; k < 4 && (size_t)vertex_count + 4u <= capacity; k++) {
                    prim_vertex_t *v = &vertices[vertex_count + k];
                    v->p = corners[k];
                    v->n = marks_v(0.0f, 0.0f, 0.0f);
                    if (mark->mode & MARK_STANDARD_UVS) {
                        v->u = single_uvs[k * 2];
                        v->v = single_uvs[k * 2 + 1];
                    } else {
                        v->u = (k & 1) ? mark->uvs[2] : mark->uvs[0];
                        v->v = (k & 2) ? mark->uvs[3] : mark->uvs[1];
                    }
                    colors[vertex_count + k] = mark->packed;
                }
                const int32_t quad[6] = {0, 1, 2, 1, 3, 2};
                for (int k = 0; k < 6; k++) {
                    indices[index_count++] = vertex_count + quad[k];
                }
                vertex_count += 4;
            } else {
                uint32_t color = vxcolor_to_color(mark->color);
                for (int k = 0; k < 2; k++) {
                    prim_vertex_t *v = &vertices[vertex_count + k];
                    v->p = mark->positions[k];
                    v->n = marks_v(0.0f, 0.0f, 0.0f);
                    v->u = (float)k;
                    v->v = factor * mark->uvs[k * 2];
                    colors[vertex_count + k] = color;
                }
                if ((mark->mode & MARK_STYLE) != MARK_BAND_START) { /* joined to the mark before it */
                    const int32_t band[6] = {0, 2, 1, 2, 3, 1};
                    for (int k = 0; k < 6; k++) {
                        indices[index_count++] = vertex_count + band[k];
                    }
                }
                vertex_count += 2;
            }
        }
        if (vertex_count <= 2) {
            continue;
        }
        int32_t first = (int32_t)engine->render.vertices.count;
        for (int32_t t = 0; t + 2 < index_count; t += 3) {
            if (indices[t] >= vertex_count || indices[t + 1] >= vertex_count || indices[t + 2] >= vertex_count) {
                continue; /* a band joined to nothing drawn */
            }
            for (int k = 0; k < 3; k++) {
                push_prim_vertex(engine, &identity, &vertices[indices[t + k]], colors[indices[t + k]]);
            }
        }
        nmo_engine_draw_t *draw = push_primitive(engine, callback->target, first, vertices[0].p);
        if (draw != NULL) {
            draw->material = eng_find_object(engine, list->material) != NULL ? list->material : 0;
            draw->two_sided = true;
            draw->projection_z_scale = 1.001f;
            draw->transparent = transparent;
        }
    }
    free(vertices);
    free(colors);
    free(indices);
}

/* RenderMarkFrame: the Supporting Entity's temporary post render callback */
static void draw_marks_after(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    const visuals_callback_t callback = {node, entity};
    draw_marks(engine, &callback, transparent);
}

static int bb_mark_system(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    visuals_marks_t *marks = mark_system(engine, b->node);
    if (marks == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        marks_clear(marks);
    } else if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t frame = (nmo_object_id_t)get_local_int(b, 3, 0);
    if (frame != 0) {
        eng_entity_callback_t callback = {frame, ENG_ENTITY_POST_RENDER, draw_marks_after, b->node, true};
        eng_add_entity_callback(engine, &callback);
    } else {
        callback_drawn(&visuals_state(engine)->marks, b->node, 0, true);
    }
    bool end_mark = false;
    if (nmo_engine_block_input_count(b) > 3 && IN_ACTIVE(b, 3)) {
        IN_OFF(b, 3);
        OUT_ON(b, 3);
        end_mark = true;
    } else if (!IN_ACTIVE(b, 2)) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    if (IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        OUT_ON(b, 2);
    }
    eng_vector_t point = get_in_vector(b, 0, 0.0f, 0.0f, 0.0f);
    nmo_object_id_t ref = nmo_engine_block_get_input_object(b, 2);
    eng_vector_t normal = get_in_vector(b, 1, 0.0f, 0.0f, 0.0f);
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 3, color, sizeof(color));
    float size = get_in_float(b, 4, 1.0f), life = get_in_float(b, 6, 5000.0f);
    float offset = get_in_float(b, 9, 0.0f);
    nmo_object_id_t material = nmo_engine_block_get_input_object(b, 5);
    float fade_time = get_in_float(b, 7, 1000.0f);
    float uvs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 8, uvs, sizeof(uvs));
    if (get_local_int(b, 2, 0) != 0) {
        marks_add_band(engine, marks, point, normal, ref, color, size, life, fade_time, end_mark, material, offset);
    } else {
        marks_add_single(engine, marks, point, normal, ref, color, size, life, material, fade_time, uvs, offset);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_mark_system(blk_t *b, uint32_t message)
{
    visuals_marks_t *marks = mark_system(b->engine, b->node);
    if (marks != NULL && (message == NMO_ENGINE_MSG_LOAD || message == NMO_ENGINE_MSG_RESET)) {
        marks_clear(marks);
        marks_check_size(marks, get_local_int(b, 1, 200));
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Vertex Render: while on, the target renders as a sprite of the Light
 * Texture at each vertex of its mesh instead of the mesh (its render
 * callback; the mesh is made transparent so it renders last): a square
 * facing the camera, Light Size wide, added (ONE, ONE) without z write, the
 * texture alone or, with Use Vertex Color, modulated by the vertex's prelit
 * color; with Use Vertex Weight each square is the vertex's weight times as
 * wide. Enable Fog Influence off draws them without the fog. On sets the
 * texture; Off, a pause, reset or the script's deactivation give the mesh
 * back.
 */
static void vertex_render_draw(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t index = eng_entity_index(engine, entity);
    nmo_object_id_t mesh_id = entity_current_mesh(engine, entity);
    nmo_engine_mesh_t mesh;
    nmo_object_id_t material_id = (nmo_object_id_t)get_local_int(b, 1, 0);
    eng_material_t *material = material_id != 0 ? eng_material(engine, material_id) : NULL;
    if (index < 0 || !nmo_engine_mesh(engine, mesh_id, &mesh) || mesh.vertex_count == 0 || material == NULL) {
        return;
    }
    bool use_color = get_local_int(b, 3, 0) != 0, use_weight = get_local_int(b, 4, 0) != 0;
    material->desc.texture_blend = use_color ? VXTEXTUREBLEND_MODULATEALPHA : VXTEXTUREBLEND_DECAL;
    /* The camera's up and right in the entity's space */
    eng_matrix_t eye;
    eng_render_viewpoint(engine, &eye);
    eng_vector_t up_world = {eye.m[1][0], eye.m[1][1], eye.m[1][2]};
    eng_vector_t right_world = {eye.m[0][0], eye.m[0][1], eye.m[0][2]};
    eng_vector_t up, right;
    eng_entity_transform_vector(engine, index, &up, &up_world, -1, true);
    eng_entity_transform_vector(engine, index, &right, &right_world, -1, true);
    float half = get_in_float(b, 1, 1.0f) * 0.5f;
    up = vec_scale(vec_normalized(up), half);
    right = vec_scale(vec_normalized(right), half);
    const eng_vector_t offsets[4] = {vec_add(vec_scale(right, -1.0f), up), vec_add(right, up),
                                     vec_sub(right, up), vec_sub(vec_scale(right, -1.0f), up)};
    const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    const float *weights = NULL;
    if (use_weight) {
        nmo_object_t *object = eng_find_object(engine, mesh_id);
        const nmo_mesh_state_t *state =
            object != NULL ? (const nmo_mesh_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
                                 engine->types, object, CKPGUID_MESH)
                           : NULL;
        if (state != NULL && state->vertex_weights != NULL && state->vertex_weight_count >= (uint32_t)mesh.vertex_count) {
            weights = state->vertex_weights;
        }
    }
    const eng_matrix_t *world = &engine->entities.data[index].world;
    int32_t first = (int32_t)engine->render.vertices.count;
    for (int32_t v = 0; v < mesh.vertex_count; v++) {
        const nmo_vector_t *p = &mesh.vertices[v].position;
        uint32_t color = use_color && mesh.colors != NULL ? mesh.colors[v] : 0xFFFFFFFFu;
        prim_vertex_t corners[4];
        for (int k = 0; k < 4; k++) {
            eng_vector_t offset = weights != NULL ? vec_scale(offsets[k], weights[v]) : offsets[k];
            corners[k].p = vec_add(marks_v(p->x, p->y, p->z), offset);
            corners[k].n = marks_v(0.0f, 0.0f, 0.0f);
            corners[k].u = uvs[k][0];
            corners[k].v = uvs[k][1];
        }
        const int quad[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++) {
            push_prim_vertex(engine, world, &corners[quad[k]], color);
        }
    }
    nmo_engine_draw_t *draw =
        push_primitive(engine, entity, first, marks_v(world->m[3][0], world->m[3][1], world->m[3][2]));
    if (draw != NULL) {
        draw->material = material_id;
        draw->transparent = transparent;
        draw->no_fog = get_in_int(b, 2, 1) == 0;
    }
}

/* CKMesh::SetTransparent on the target's mesh */
static void vertex_render_transparent(nmo_engine_t *engine, nmo_object_id_t target, bool transparent)
{
    eng_mesh_copy_t *copy = eng_mesh_copy(engine, entity_current_mesh(engine, target));
    if (copy != NULL) {
        copy->flags = transparent ? copy->flags | VXMESH_FORCETRANSPARENCY : copy->flags & ~(uint32_t)VXMESH_FORCETRANSPARENCY;
        copy->version = ++engine->render.mesh_version;
    }
}

static void vertex_render_off(nmo_engine_t *engine, blk_t *b, nmo_object_id_t target)
{
    set_local_int(b, 2, 0);
    eng_remove_entity_callback(engine, target, ENG_ENTITY_RENDER, vertex_render_draw, b->node);
    vertex_render_transparent(engine, target, false);
}

static int bb_vertex_render(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (get_local_int(b, 2, 0) == 0) {
        eng_entity_callback_t callback = {target, ENG_ENTITY_RENDER, vertex_render_draw, b->node, false};
        eng_add_entity_callback(engine, &callback);
        vertex_render_transparent(engine, target, true);
        set_local_int(b, 2, 1);
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_material_t *material = eng_material(engine, (nmo_object_id_t)get_local_int(b, 1, 0));
        if (material != NULL) {
            material->desc.texture = nmo_engine_block_get_input_object(b, 0);
        }
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 0);
        vertex_render_off(engine, b, target);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_vertex_render(blk_t *b, uint32_t message)
{
    nmo_engine_t *engine = b->engine;
    if (message == NMO_ENGINE_MSG_LOAD) {
        set_local_int(b, 2, 0);
        nmo_object_id_t id = eng_create_object(engine, NMO_CID_MATERIAL, "MLMaterial");
        eng_material_t *material = id != 0 ? eng_material(engine, id) : NULL;
        if (material == NULL && id != 0) {
            material = eng_material_new(engine, id);
        }
        if (material == NULL) {
            return NMO_ENGINE_BR_OK;
        }
        set_local_int(b, 1, (int32_t)id);
        material->desc.alpha_blend = true;
        material->desc.source_blend = VXBLEND_ONE;
        material->desc.dest_blend = VXBLEND_ONE;
        material->desc.z_write = false;
        material->desc.address_mode = VXTEXTURE_ADDRESSCLAMP;
        material->desc.texture_blend = VXTEXTUREBLEND_MODULATEALPHA;
        material->desc.texture = nmo_engine_block_get_input_object(b, 0);
    } else if (message == NMO_ENGINE_MSG_PAUSE || message == NMO_ENGINE_MSG_RESET ||
               message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        nmo_object_id_t target = nmo_engine_block_target(b);
        if (target != 0 && get_local_int(b, 2, 0) != 0) {
            vertex_render_off(engine, b, target);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * Motion Blur: while on, the target renders last (RENDERLAST) through its
 * render callback, which draws its mesh at its last Number of Trails world
 * matrices (1 to 30, kept each frame) instead of once, its materials alpha
 * blended (srcalpha, invsrcalpha) at a fraction of their alpha: the copy a
 * third of the way back the most opaque, fading toward the oldest and toward
 * the newest (the target where it is, at 1 / (third + 1)). The trails are
 * drawn from the third back to the oldest, then from the third forward. When
 * the newest and the oldest matrix are the same, the mesh draws once, as it
 * is. Off, a pause, reset or the script's deactivation end it.
 */
static visuals_blur_t *motion_blur(nmo_engine_t *engine, int32_t node)
{
    visuals_t *visuals = visuals_state(engine);
    for (size_t i = 0; i < visuals->blurs.count; i++) {
        if (visuals->blurs.data[i].node == node) {
            return &visuals->blurs.data[i];
        }
    }
    visuals_blur_t *blur = NULL;
    if (!ENG_VEC_PUSH(visuals->blurs, blur)) {
        return NULL;
    }
    memset(blur, 0, sizeof(*blur));
    blur->node = node;
    blur->n = 1;
    return blur;
}

static void blur_trail(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh, const eng_matrix_t *world,
                       float alpha, bool transparent, const eng_matrix_t *at)
{
    nmo_engine_draw_t *draw = eng_render_entity_mesh(engine, entity, mesh, world->m[0], transparent);
    if (draw != NULL) {
        draw->fade = true;
        draw->fade_alpha = alpha;
        draw->has_sort_position = true;
        draw->sort_position[0] = at->m[3][0];
        draw->sort_position[1] = at->m[3][1];
        draw->sort_position[2] = at->m[3][2];
    }
}

/* rendercallback */
static void motion_blur_draw(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node, bool transparent)
{
    visuals_blur_t *blur = motion_blur(engine, node);
    int32_t index = eng_entity_index(engine, entity);
    nmo_object_id_t mesh = entity_current_mesh(engine, entity);
    if (blur == NULL || index < 0 || mesh == 0) {
        return;
    }
    const eng_matrix_t *at = &engine->entities.data[index].world;
    int32_t n = blur->n;
    if (memcmp(&blur->tab[0], &blur->tab[n - 1], sizeof(eng_matrix_t)) == 0) {
        eng_render_entity_mesh(engine, entity, mesh, at->m[0], transparent);
        return;
    }
    int32_t mid = (int32_t)((float)n * 0.33333334f);
    for (int32_t a = mid; a < n; a++) {
        blur_trail(engine, entity, mesh, &blur->tab[a], (float)(n - a) / (float)(n - mid), transparent, at);
    }
    for (int32_t a = mid - 1; a >= 0; a--) {
        blur_trail(engine, entity, mesh, &blur->tab[a], (float)(a + 1) / (float)(mid + 1), transparent, at);
    }
}

static void motion_blur_off(nmo_engine_t *engine, blk_t *b, nmo_object_id_t target)
{
    set_local_int(b, 1, 0);
    eng_remove_entity_callback(engine, target, ENG_ENTITY_RENDER, motion_blur_draw, b->node);
    int32_t index = eng_entity_index(engine, target);
    if (index >= 0) {
        engine->entities.data[index].moveable_flags &= ~(uint32_t)VX_MOVEABLE_RENDERLAST;
    }
}

static int bb_motion_blur(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t index = eng_entity_index(engine, target);
    visuals_blur_t *blur = motion_blur(engine, b->node);
    if (index < 0 || blur == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (get_local_int(b, 1, 0) == 0) {
        blur->n = 10;
        for (int a = 0; a < BLUR_TRACKS; a++) {
            blur->tab[a] = engine->entities.data[index].world;
        }
        set_local_int(b, 1, 1);
        engine->entities.data[index].moveable_flags |= VX_MOVEABLE_RENDERLAST;
        eng_entity_callback_t callback = {target, ENG_ENTITY_RENDER, motion_blur_draw, b->node, false};
        eng_add_entity_callback(engine, &callback);
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        motion_blur_off(engine, b, target);
        return NMO_ENGINE_BR_OK;
    }
    int32_t n = get_in_int(b, 0, 10);
    n = n > BLUR_TRACKS ? BLUR_TRACKS : n < 1 ? 1 : n;
    blur->n = n;
    for (int32_t a = n - 1; a > 0; a--) {
        blur->tab[a] = blur->tab[a - 1];
    }
    blur->tab[0] = engine->entities.data[index].world;
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

static int cb_motion_blur(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        set_local_int(b, 1, 0);
    } else if (message == NMO_ENGINE_MSG_PAUSE || message == NMO_ENGINE_MSG_RESET ||
               message == NMO_ENGINE_MSG_DEACTIVATESCRIPT) {
        nmo_object_id_t target = nmo_engine_block_target(b);
        if (target != 0 && get_local_int(b, 1, 0) != 0) {
            motion_blur_off(b->engine, b, target);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/* The attribute type named `name`; -1 for none */
static int32_t attribute_type_named(const nmo_engine_t *engine, const char *name)
{
    for (size_t t = 0; t < engine->attributes.types.count; t++) {
        const eng_attribute_type_t *type = &engine->attributes.types.data[t];
        if (type->present && type->name != NULL && strcmp(type->name, name) == 0) {
            return (int32_t)t;
        }
    }
    return -1;
}

/* A field of a structure attribute's value (its parameters' ids, one a field): an int, or with
   `object` the object it names (an engine ID) */
static int32_t attribute_field(nmo_engine_t *engine, const eng_attribute_t *attribute, uint32_t field,
                               bool object, int32_t fallback)
{
    if (!attribute->has_value || attribute->value.size / 4 <= field) {
        return fallback;
    }
    nmo_object_id_t local;
    memcpy(&local, attribute->value.data + 4 * field, 4);
    const eng_module_t *module = eng_module_of(engine, attribute->object, NULL);
    nmo_object_id_t id = module != NULL ? eng_module_id(module, local) : 0;
    int32_t param = eng_map_get(&engine->param_map, id);
    if (param >= 0) {
        return eng_param_read_int(&engine->params.data[param]);
    }
    const nmo_parameter_state_t *state = (const nmo_parameter_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(
        engine->types, eng_find_object(engine, id), CKPGUID_PARAMETER);
    uint8_t *bytes = NULL;
    uint32_t size = 0;
    if (state != NULL && eng_parameter_state_bytes(module, state, 4, &bytes, &size) && size >= 4) {
        memcpy(&fallback, bytes, 4);
        if (object) {
            fallback = (int32_t)eng_module_id(module, (nmo_object_id_t)fallback);
        }
    }
    free(bytes);
    return fallback;
}

/* CK3dEntity::HierarchyParser: the entity's descendants, depth first; -1 after the last */
static int32_t hierarchy_next(const nmo_engine_t *engine, int32_t root, int32_t current)
{
    if (current < 0) {
        return engine->entities.data[root].first_child;
    }
    if (engine->entities.data[current].first_child >= 0) {
        return engine->entities.data[current].first_child;
    }
    while (current != root && engine->entities.data[current].next_sibling < 0) {
        current = engine->entities.data[current].parent;
    }
    return current == root || current < 0 ? -1 : engine->entities.data[current].next_sibling;
}

/* IsVisible() && !IsHiddenByParent() */
static bool entity_shown(nmo_engine_t *engine, int32_t index)
{
    return nmo_engine_object_is_visible(engine, engine->entities.data[index].id) &&
           !eng_render_hidden_by_parent(engine, index);
}

/* The entity's current mesh when it is shown (VXMESH_VISIBLE); 0 otherwise */
static nmo_object_id_t entity_shown_mesh(nmo_engine_t *engine, nmo_object_id_t entity)
{
    nmo_object_id_t mesh = entity_current_mesh(engine, entity);
    return mesh != 0 && nmo_engine_object_is_visible(engine, mesh) ? mesh : 0;
}

/* VxPlane(a, b, c): its normal and D */
typedef struct visuals_plane {
    eng_vector_t n;
    float d;
} visuals_plane_t;

static visuals_plane_t plane_through(eng_vector_t a, eng_vector_t b, eng_vector_t c)
{
    double bx = (double)b.x - a.x, by = (double)b.y - a.y, bz = (double)b.z - a.z;
    double cx = (double)c.x - a.x, cy = (double)c.y - a.y, cz = (double)c.z - a.z;
    double nx = by * cz - bz * cy, ny = bz * cx - bx * cz, nz = bx * cy - by * cx;
    double length = nx * nx + ny * ny + nz * nz;
    visuals_plane_t plane = {{0.0f, 0.0f, 0.0f}, 0.0f};
    if (length != 0.0) {
        double inverse = 1.0 / sqrt(length);
        plane.n = marks_v((float)(nx * inverse), (float)(ny * inverse), (float)(nz * inverse));
    }
    plane.d = -(plane.n.x * a.x + plane.n.y * a.y + plane.n.z * a.z);
    return plane;
}

/* VxPlane::Classify(box, matrix): the distance of the transformed box past the plane, 0 across */
static float plane_classify(const visuals_plane_t *plane, eng_vector_t min, eng_vector_t max,
                            const eng_matrix_t *m)
{
    float radius = 0.0f;
    const float half[3] = {(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    for (int i = 0; i < 3; i++) {
        eng_vector_t axis = marks_v(m->m[i][0] * half[i], m->m[i][1] * half[i], m->m[i][2] * half[i]);
        radius += fabsf(plane->n.x * axis.x + plane->n.y * axis.y + plane->n.z * axis.z);
    }
    eng_vector_t center = marks_v((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f), c;
    eng_vector_transform(&c, m, &center);
    float distance = plane->n.x * c.x + plane->n.y * c.y + plane->n.z * c.z + plane->d;
    return distance > radius ? distance - radius : distance < -radius ? distance + radius : 0.0f;
}

/* The four planes through the light (or eye) and the edges of the plane entity's local box
   across `normal` (left, right, top, down), in the world */
static void plane_edges(nmo_engine_t *engine, int32_t plane, eng_vector_t normal, eng_vector_t from,
                        visuals_plane_t out[4])
{
    eng_vector_t min, max;
    eng_entity_local_box(engine, plane, &min, &max);
    float lo[3] = {min.x, min.y, min.z}, hi[3] = {max.x, max.y, max.z};
    float smallest = normal.x;
    int x = 1, y = 2;
    if (smallest < normal.y) {
        smallest = normal.y;
        x = 0;
        y = 2;
    }
    if (smallest < normal.z) {
        x = 0;
        y = 1;
    }
    eng_vector_t corners[4];
    for (int k = 0; k < 4; k++) {
        float p[3] = {hi[0], hi[1], hi[2]};
        if (k == 0 || k == 1) {
            p[x] = lo[x];
        }
        if (k == 0 || k == 3) {
            p[y] = lo[y];
        }
        eng_vector_t local = marks_v(p[0], p[1], p[2]);
        eng_vector_transform(&corners[k], &engine->entities.data[plane].world, &local);
    }
    out[0] = plane_through(corners[1], corners[0], from);
    out[1] = plane_through(corners[2], corners[1], from);
    out[2] = plane_through(corners[3], corners[2], from);
    out[3] = plane_through(corners[0], corners[3], from);
}

/* The viewpoint's position in the plane entity's space */
static eng_vector_t viewpoint_in(nmo_engine_t *engine, int32_t plane)
{
    eng_matrix_t eye, inverse;
    eng_render_viewpoint(engine, &eye);
    eng_matrix_inverse(&inverse, &engine->entities.data[plane].world);
    eng_vector_t at = marks_v(eye.m[3][0], eye.m[3][1], eye.m[3][2]), out;
    eng_vector_transform(&out, &inverse, &at);
    return out;
}

/* A mesh draw an entity's render callback makes, sorted with the plane entity `at` */
static nmo_engine_draw_t *plane_mesh_draw(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh,
                                          const eng_matrix_t *world, bool transparent, const eng_matrix_t *at)
{
    nmo_engine_draw_t *draw = eng_render_entity_mesh(engine, entity, mesh, world->m[0], transparent);
    if (draw != NULL) {
        draw->has_sort_position = true;
        draw->sort_position[0] = at->m[3][0];
        draw->sort_position[1] = at->m[3][1];
        draw->sort_position[2] = at->m[3][2];
    }
    return draw;
}

/*
 * Planar Reflection: each frame it is active, the target (a plane, seen from
 * the side its Plane Normal points to) draws, before itself, the entities
 * with the Reflected Object attribute of one of its Reflection Group(s)
 * mirrored through it: an entity's own mesh and its shown descendants, or
 * the attribute's mesh when it names one, with their winding inverted. With
 * Discard Off Mirror Reflection the plane first marks its pixels in a free
 * stencil bit (its mesh drawn to the stencil only) and the reflections are
 * drawn only there. The reflected entities draw their meshes only: their
 * own render callbacks do not run in the reflection.
 */
static void planar_reflection_draw(nmo_engine_t *engine, nmo_object_id_t plane, int32_t node, bool transparent)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t index = eng_entity_index(engine, plane);
    if (index < 0) {
        return;
    }
    int32_t plane_flag = get_in_int(b, 0, 1);
    eng_vector_t local_normal = get_in_vector(b, 1, 0.0f, 1.0f, 0.0f);
    eng_vector_t eye = viewpoint_in(engine, index);
    if (eye.x * local_normal.x + eye.y * local_normal.y + eye.z * local_normal.z < 0.0f) {
        return;
    }
    const eng_matrix_t at = engine->entities.data[index].world;
    eng_vector_t n;
    eng_vector_rotate(&n, &at, &local_normal);
    n = vec_normalized(n);
    eng_vector_t position = marks_v(at.m[3][0], at.m[3][1], at.m[3][2]);
    float dp = n.x * position.x + n.y * position.y + n.z * position.z;
    eng_matrix_t reflect;
    eng_matrix_identity(&reflect);
    const float nv[3] = {n.x, n.y, n.z};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            reflect.m[i][j] = (i == j ? 1.0f : 0.0f) - 2.0f * nv[i] * nv[j];
        }
        reflect.m[3][i] = 2.0f * dp * nv[i];
    }
    nmo_engine_stencil_t saved = engine->render.stencil;
    bool masked = get_local_int(b, 0, 1) != 0;
    if (masked) {
        int32_t bit = eng_render_first_free_stencil_bit(engine);
        nmo_engine_stencil_t *stencil = &engine->render.stencil;
        stencil->enabled = true;
        stencil->ref = 0xFF;
        stencil->write_mask = 1u << bit;
        stencil->func = VXCMP_ALWAYS;
        stencil->fail = stencil->zfail = VXSTENCILOP_KEEP;
        stencil->pass = VXSTENCILOP_REPLACE;
        nmo_object_id_t mesh = entity_shown_mesh(engine, plane);
        if (mesh != 0) {
            nmo_engine_draw_t *draw = plane_mesh_draw(engine, plane, mesh, &at, transparent, &at);
            if (draw != NULL) {
                draw->stencil_only = true;
            }
            eng_render_use_stencil_bits(engine, 1u << bit);
        }
        stencil->pass = VXSTENCILOP_KEEP;
        stencil->ref = 0xFF;
        stencil->mask = 1u << bit;
        stencil->func = VXCMP_EQUAL;
    }
    int32_t type = attribute_type_named(engine, "Reflected Object");
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        eng_attribute_t attribute = engine->attributes.values.data[i];
        int32_t entity = attribute.type == type ? eng_entity_index(engine, attribute.object) : -1;
        if (entity < 0 || entity == index || engine->entities.data[entity].destroyed ||
            !(plane_flag & attribute_field(engine, &attribute, 0, false, 1))) {
            continue;
        }
        nmo_object_id_t mesh = (nmo_object_id_t)attribute_field(engine, &attribute, 1, true, 0);
        eng_matrix_t world;
        eng_matrix_multiply(&world, &reflect, &engine->entities.data[entity].world);
        if (mesh != 0) {
            nmo_engine_draw_t *draw = plane_mesh_draw(engine, attribute.object, mesh, &world, transparent, &at);
            if (draw != NULL) {
                draw->mirrored = !draw->mirrored;
            }
            continue;
        }
        nmo_object_id_t own = entity_shown_mesh(engine, attribute.object);
        if (own != 0 && nmo_engine_object_is_visible(engine, attribute.object)) {
            nmo_engine_draw_t *draw = plane_mesh_draw(engine, attribute.object, own, &world, transparent, &at);
            if (draw != NULL) {
                draw->mirrored = !draw->mirrored;
            }
        }
        for (int32_t child = hierarchy_next(engine, entity, -1); child >= 0; child = hierarchy_next(engine, entity, child)) {
            nmo_object_id_t child_mesh = entity_shown_mesh(engine, engine->entities.data[child].id);
            if (child_mesh == 0 || !entity_shown(engine, child)) {
                continue;
            }
            eng_matrix_multiply(&world, &reflect, &engine->entities.data[child].world);
            nmo_engine_draw_t *draw =
                plane_mesh_draw(engine, engine->entities.data[child].id, child_mesh, &world, transparent, &at);
            if (draw != NULL) {
                draw->mirrored = !draw->mirrored;
            }
        }
    }
    if (masked) {
        engine->render.stencil = saved;
    }
}

static int bb_planar_reflection(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (eng_entity_index(b->engine, target) >= 0) {
        eng_entity_callback_t callback = {target, ENG_ENTITY_PRE_RENDER, planar_reflection_draw, b->node, true};
        eng_add_entity_callback(b->engine, &callback);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * Planar Shadow: each frame it is active, after the target (a plane, seen
 * from the side its Plane Normal points to) draws, the entities with the
 * Planar Shadow Object attribute of one of its Shadowing Group(s) cast their
 * shadows on it from the Light: each mesh (the entity's and its shown
 * descendants', or the attribute's mesh) flattened onto the plane along the
 * light's direction (a directional light) or from its position, in Color,
 * alpha blended without z write or culling, the depth nudged toward the eye.
 * With a point or spot light, an entity outside the four planes through the
 * light and the plane's edges casts none. With Discard Off Plane Shadow the
 * plane marks its pixels in a free stencil bit as it draws and the shadows
 * are drawn only there.
 */
#define CK_3DENTITY_RESERVED0 0x20

/* PlanarShadowPreRenderCallBack */
static void planar_shadow_mask(nmo_engine_t *engine, nmo_object_id_t plane, int32_t node, bool transparent)
{
    (void)node;
    (void)transparent;
    int32_t index = eng_entity_index(engine, plane);
    nmo_engine_stencil_t *stencil = &engine->render.stencil;
    if (index < 0) {
        return;
    }
    bool was = stencil->enabled;
    if (was) {
        engine->entities.data[index].entity_flags |= CK_3DENTITY_RESERVED0;
    } else {
        engine->entities.data[index].entity_flags &= ~(uint32_t)CK_3DENTITY_RESERVED0;
    }
    stencil->enabled = true;
    int32_t bit = eng_render_first_free_stencil_bit(engine);
    eng_render_use_stencil_bits(engine, 1u << bit);
    if (!was) {
        stencil->ref = 0xFF;
        stencil->mask = 0;
        stencil->func = VXCMP_ALWAYS;
    }
    stencil->write_mask = 1u << bit;
    stencil->fail = stencil->zfail = VXSTENCILOP_KEEP;
    stencil->pass = VXSTENCILOP_REPLACE;
}

/* RenderShadow: the mesh's faces flattened on the plane, in the plane's space, drawn at it */
static void planar_shadow_cast(nmo_engine_t *engine, int32_t plane, int32_t entity, nmo_object_id_t mesh_id,
                               int32_t light, uint32_t color, eng_vector_t local_normal, bool directional,
                               bool transparent)
{
    nmo_engine_mesh_t mesh;
    if (!nmo_engine_mesh(engine, mesh_id, &mesh) || mesh.face_count == 0) {
        return;
    }
    const eng_matrix_t *at = &engine->entities.data[plane].world;
    eng_matrix_t inverse, to_plane;
    eng_matrix_inverse(&inverse, at);
    eng_matrix_multiply(&to_plane, &inverse, &engine->entities.data[entity].world);
    eng_vector_t n = vec_normalized(local_normal);
    const eng_matrix_t *lw = &engine->entities.data[light].world;
    float s[4][4];
    if (directional) {
        eng_vector_t z = marks_v(lw->m[2][0], lw->m[2][1], lw->m[2][2]), l;
        eng_vector_rotate(&l, &inverse, &z);
        l = vec_normalized(l);
        float inv = 1.0f / (l.x * n.x + l.y * n.y + l.z * n.z);
        const float nv[3] = {n.x, n.y, n.z}, lv[3] = {l.x, l.y, l.z};
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                s[i][j] = (i == j ? 1.0f : 0.0f) - nv[i] * lv[j] * inv;
            }
            s[i][3] = 0.0f;
            s[3][i] = 0.0f; /* the plane's position in its own space is the origin */
        }
        s[3][3] = 1.0f;
    } else {
        eng_vector_t origin = marks_v(lw->m[3][0], lw->m[3][1], lw->m[3][2]), p;
        eng_vector_transform(&p, &inverse, &origin);
        float dp = p.x * n.x + p.y * n.y + p.z * n.z;
        const float nv[3] = {n.x, n.y, n.z}, pv[3] = {p.x, p.y, p.z};
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                s[i][j] = (i == j ? dp : 0.0f) - nv[i] * pv[j];
            }
            s[i][3] = -nv[i];
            s[3][i] = 0.0f;
        }
        s[3][3] = dp;
    }
    eng_matrix_t identity;
    eng_matrix_identity(&identity);
    int32_t first = (int32_t)engine->render.vertices.count;
    for (int32_t f = 0; f < mesh.face_count; f++) {
        for (int k = 0; k < 3; k++) {
            const nmo_vector_t *v = &mesh.vertices[mesh.indices[f * 3 + k]].position;
            eng_vector_t local = marks_v(v->x, v->y, v->z), q;
            eng_vector_transform(&q, &to_plane, &local);
            float out[4];
            for (int j = 0; j < 4; j++) {
                out[j] = q.x * s[0][j] + q.y * s[1][j] + q.z * s[2][j] + s[3][j];
            }
            prim_vertex_t vertex = {marks_v(out[0] / out[3], out[1] / out[3], out[2] / out[3]),
                                    marks_v(0.0f, 0.0f, 0.0f), 0.0f, 0.0f};
            push_prim_vertex(engine, at, &vertex, color);
        }
    }
    nmo_engine_draw_t *draw = push_primitive(engine, engine->entities.data[plane].id, first,
                                             marks_v(at->m[3][0], at->m[3][1], at->m[3][2]));
    if (draw != NULL) {
        draw->blend = true;
        draw->source_blend = VXBLEND_SRCALPHA;
        draw->dest_blend = VXBLEND_INVSRCALPHA;
        draw->no_z_write = true;
        draw->two_sided = true;
        draw->transparent = transparent;
        draw->projection_z_scale = 1.005f;
    }
}

/* PlanarShadowPostRenderCallBack */
static void planar_shadow_draw(nmo_engine_t *engine, nmo_object_id_t plane_id, int32_t node, bool transparent)
{
    nmo_engine_block_t handle = {engine, node};
    blk_t *b = &handle;
    int32_t plane = eng_entity_index(engine, plane_id);
    if (plane < 0) {
        return;
    }
    bool masked = get_local_int(b, 0, 1) != 0;
    nmo_engine_stencil_t saved = engine->render.stencil; /* the Cleaner's */
    bool was = (engine->entities.data[plane].entity_flags & CK_3DENTITY_RESERVED0) != 0;
    engine->entities.data[plane].entity_flags &= ~(uint32_t)CK_3DENTITY_RESERVED0;
    int32_t plane_flag = get_in_int(b, 0, 1);
    nmo_object_id_t light_id = nmo_engine_block_get_input_object(b, 1);
    int32_t light = eng_entity_index(engine, light_id);
    eng_light_t *desc = light >= 0 ? eng_light(engine, light_id) : NULL;
    float color_in[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 2, color_in, sizeof(color_in));
    uint32_t color = vxcolor_to_color(color_in);
    eng_vector_t local_normal = get_in_vector(b, 3, 0.0f, 1.0f, 0.0f);
    eng_vector_t eye = viewpoint_in(engine, plane);
    if (light >= 0 && eye.x * local_normal.x + eye.y * local_normal.y + eye.z * local_normal.z >= 0.0f) {
        bool directional = desc != NULL && desc->type == VX_LIGHTDIREC;
        const eng_matrix_t *lw = &engine->entities.data[light].world;
        visuals_plane_t edges[4];
        plane_edges(engine, plane, local_normal, marks_v(lw->m[3][0], lw->m[3][1], lw->m[3][2]), edges);
        if (masked) {
            nmo_engine_stencil_t *stencil = &engine->render.stencil;
            stencil->enabled = true;
            stencil->pass = VXSTENCILOP_KEEP;
            stencil->ref = 0xFF;
            stencil->mask = saved.write_mask;
            stencil->write_mask = saved.write_mask;
            stencil->func = VXCMP_EQUAL;
        }
        int32_t type = attribute_type_named(engine, "Planar Shadow Object");
        for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
            eng_attribute_t attribute = engine->attributes.values.data[i];
            int32_t entity = attribute.type == type ? eng_entity_index(engine, attribute.object) : -1;
            if (entity < 0 || engine->entities.data[entity].destroyed || !entity_shown(engine, entity) ||
                !(plane_flag & attribute_field(engine, &attribute, 0, false, 1))) {
                continue;
            }
            nmo_object_id_t mesh = (nmo_object_id_t)attribute_field(engine, &attribute, 1, true, 0);
            bool hierarchy = mesh == 0;
            if (hierarchy) {
                mesh = entity_shown_mesh(engine, attribute.object);
            }
            if (mesh != 0) {
                bool culled = false;
                if (!directional) {
                    eng_vector_t min, max;
                    eng_entity_local_box(engine, entity, &min, &max);
                    for (int e = 0; e < 4; e++) {
                        culled = culled || plane_classify(&edges[e], min, max, &engine->entities.data[entity].world) < 0.0f;
                    }
                }
                if (!culled) {
                    planar_shadow_cast(engine, plane, entity, mesh, light, color, local_normal, directional, transparent);
                }
            }
            for (int32_t child = hierarchy ? hierarchy_next(engine, entity, -1) : -1; child >= 0;
                 child = hierarchy_next(engine, entity, child)) {
                nmo_object_id_t child_mesh = entity_shown(engine, child) ? entity_shown_mesh(engine, engine->entities.data[child].id) : 0;
                if (child_mesh == 0) {
                    continue;
                }
                bool culled = false;
                if (!directional) {
                    eng_vector_t min, max;
                    eng_entity_local_box(engine, child, &min, &max);
                    for (int e = 0; e < 4; e++) {
                        culled = culled || plane_classify(&edges[e], min, max, &engine->entities.data[child].world) < 0.0f;
                    }
                }
                if (!culled) {
                    planar_shadow_cast(engine, plane, child, child_mesh, light, color, local_normal, directional, transparent);
                }
            }
        }
    }
    if (masked) {
        engine->render.stencil = saved;
        if (!was) {
            engine->render.stencil.enabled = false;
        }
    }
}

static int bb_planar_shadow(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    nmo_object_id_t target = nmo_engine_block_target(b);
    if (eng_entity_index(b->engine, target) >= 0) {
        if (get_local_int(b, 0, 1) != 0) {
            eng_entity_callback_t pre = {target, ENG_ENTITY_PRE_RENDER, planar_shadow_mask, b->node, true};
            eng_add_entity_callback(b->engine, &pre);
        }
        eng_entity_callback_t post = {target, ENG_ENTITY_POST_RENDER, planar_shadow_draw, b->node, true};
        eng_add_entity_callback(b->engine, &post);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * Texture Render: renders the scene into the target texture from the Camera
 * (the attached one when none), its background the Background Color and
 * Texture, with the Render Options (but back to front); a cube map gets the
 * six faces from the camera's position, 90 degrees wide, or one face a frame
 * (the current slot, then the next) with Render Face by Face. CK renders as
 * the block runs; the engine makes the pass with the frame, before its
 * draws, from the camera's matrix as the block left it. The DLL needs a
 * Camera for a cube map (it would crash without); the engine gives an owner
 * error. Version 1 blocks (their own texture, as an output) are not ported.
 */
static bool visuals_request_pass(nmo_engine_t *engine, const visuals_pass_request_t *request)
{
    visuals_t *visuals = visuals_state(engine);
    if (visuals->passes.count > 0 && visuals->passes.data[0].frame != engine->frame) {
        visuals->passes.count = 0; /* asked in a frame no render followed */
    }
    visuals_pass_request_t *slot = NULL;
    if (!ENG_VEC_PUSH(visuals->passes, slot)) {
        return false;
    }
    *slot = *request;
    slot->frame = engine->frame;
    return true;
}

static int bb_texture_render(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    eng_texture_t *texture = target != 0 ? eng_texture(engine, target) : NULL;
    if (texture == NULL || texture->sprite) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t input = nmo_engine_block_get_input_object(b, 0);
    nmo_object_id_t camera = input != 0 ? input : engine->render.camera;
    visuals_pass_request_t request;
    memset(&request, 0, sizeof(request));
    request.node = b->node;
    request.texture = target;
    request.view.camera = camera;
    float color[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    nmo_engine_block_get_input_value(b, 1, color, sizeof(color));
    request.view.background_color = vxcolor_to_color(color);
    request.view.background_texture = nmo_engine_block_get_input_object(b, 2);
    request.view.render_options = (uint32_t)get_local_int(b, 3, (int32_t)engine->render.render_options) &
                                  ~(uint32_t)CK_RENDER_DOBACKTOFRONT;
    int32_t index = camera != 0 ? eng_entity_index(engine, camera) : -1;
    eng_matrix_t eye;
    eng_matrix_identity(&eye);
    if (index >= 0) {
        eye = engine->entities.data[index].world;
    }
    nmo_object_t *object = eng_find_object(engine, target);
    const nmo_texture_state_t *state = object != NULL
        ? (const nmo_texture_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_TEXTURE)
        : NULL;
    if (state != NULL && state->is_cubemap) {
        if (input == 0) {
            return NMO_ENGINE_BR_OWNERERROR;
        }
        /* Each face's referential, at the camera */
        static const float faces[6][3][3] = {
            {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},  {{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}},
            {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},   {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}}};
        request.view.has_fov = true;
        request.view.fov = 3.14159265358979f / 2.0f;
        bool face_by_face = get_local_int(b, 4, 0) != 0;
        int32_t current = texture->current_slot;
        for (int32_t face = 0; face < 6; face++) {
            if (face_by_face && face != current) {
                continue;
            }
            eng_matrix_t at;
            eng_matrix_identity(&at);
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    at.m[r][c] = faces[face][r][c];
                }
                at.m[3][r] = eye.m[3][r];
            }
            memcpy(request.view.eye, at.m, sizeof(request.view.eye));
            request.slot = face;
            visuals_request_pass(engine, &request);
        }
        if (face_by_face) {
            eng_texture_set_slot(texture, (current + 1) % 6);
        }
        return NMO_ENGINE_BR_OK;
    }
    memcpy(request.view.eye, eye.m, sizeof(request.view.eye));
    visuals_request_pass(engine, &request);
    return NMO_ENGINE_BR_OK;
}

/*
 * ShadowCaster: each frame it is active, the target's shadow is drawn from
 * the Light into a square texture (Shadow Resolution Size, to a power of two)
 * through a camera at the light aimed at the target's hierarchy and backed
 * off until it fits a 0.2 radian view: white, the target in black, then
 * lightened toward white as the light gets farther than Min Light Distance,
 * not drawn at all past Max Light Distance (Soft Shadow adds a white to gray
 * gradient). The objects with the Shadow Caster Receiver attribute inside the
 * light's view get it as a material channel multiplying them (ZERO,
 * SRCCOLOR, unlit) with UVs projected from the light, on their faces turned
 * to it. Off or a pause takes the channel off them. CK renders as the block
 * runs; the engine makes the pass with the frame. The DLL also skips a
 * receiver that was outside the last frame's view; the engine keeps no
 * frustum test of the last render. Its test of a receiver against the
 * light's view checks the box's corners against each plane.
 */
#define SHADOW_FOV 0.2f

static visuals_caster_t *shadow_caster(nmo_engine_t *engine, blk_t *b)
{
    visuals_t *visuals = visuals_state(engine);
    for (size_t i = 0; i < visuals->casters.count; i++) {
        if (visuals->casters.data[i].node == b->node) {
            return &visuals->casters.data[i];
        }
    }
    nmo_object_id_t camera = eng_create_object(engine, NMO_CID_TARGETCAMERA, "ShadowCaster Camera");
    nmo_object_id_t texture = eng_create_object(engine, NMO_CID_TEXTURE, "ShadowCaster Texture");
    nmo_object_id_t material = eng_create_object(engine, NMO_CID_MATERIAL, "SC Material");
    eng_material_t *shade = material != 0 ? eng_material(engine, material) : NULL;
    if (shade == NULL && material != 0) {
        shade = eng_material_new(engine, material);
    }
    eng_texture_t *image = texture != 0 ? eng_texture(engine, texture) : NULL;
    visuals_caster_t *caster = NULL;
    if (camera == 0 || shade == NULL || image == NULL || !ENG_VEC_PUSH(visuals->casters, caster)) {
        return NULL;
    }
    memset(caster, 0, sizeof(*caster));
    caster->node = b->node;
    caster->camera = camera;
    caster->texture = texture;
    caster->material = material;
    nmo_object_t *object = eng_find_object(engine, camera);
    nmo_camera_state_t *view = object != NULL
        ? (nmo_camera_state_t *)nmo_type_query_object_get_ancestor_state_by_guid(engine->types, object, CKPGUID_CAMERA)
        : NULL;
    if (view != NULL) {
        view->aspect_width = view->aspect_height = 1;
    }
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    memcpy(shade->desc.diffuse, white, sizeof(white));
    memcpy(shade->desc.specular, white, sizeof(white));
    shade->desc.power = 0.0f;
    shade->desc.texture = texture;
    shade->desc.texture_blend = VXTEXTUREBLEND_DECAL;
    shade->desc.address_mode = VXTEXTURE_ADDRESSCLAMP;
    shade->desc.min_filter = shade->desc.mag_filter = VXTEXTUREFILTER_LINEAR;
    shade->desc.border_color = 0xFFFFFFFFu;
    int32_t size = get_local_int(b, 1, 64), power = size & -size;
    while (power < size) {
        power <<= 1;
    }
    eng_texture_create(engine, image, power, power, 0);
    return caster;
}

/* The receivers' channels of the caster's material, taken off */
static void shadow_caster_remove(nmo_engine_t *engine, const visuals_caster_t *caster)
{
    int32_t type = attribute_type_named(engine, "Shadow Caster Receiver");
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        eng_mesh_copy_t *copy = attribute->type == type
            ? eng_mesh_copy(engine, entity_current_mesh(engine, attribute->object)) : NULL;
        int32_t channel = copy != NULL ? eng_mesh_channel(copy, caster->material) : -1;
        if (channel >= 0) {
            eng_mesh_remove_channel(copy, channel);
            copy->version = ++engine->render.mesh_version;
        }
    }
}

/* The light frustum's clip flags (VxFrustum::Classify) of a point in its camera's space */
static uint32_t shadow_classify(eng_vector_t p, float near_plane, float far_plane, float t)
{
    uint32_t flags = 0;
    if (p.z < near_plane) {
        flags |= VXCLIP_FRONT;
    } else if (p.z > far_plane) {
        flags |= VXCLIP_BACK;
    }
    if (p.x < -t * p.z) {
        flags |= VXCLIP_LEFT;
    } else if (p.x > t * p.z) {
        flags |= VXCLIP_RIGHT;
    }
    if (p.y < -t * p.z) {
        flags |= VXCLIP_BOTTOM;
    } else if (p.y > t * p.z) {
        flags |= VXCLIP_TOP;
    }
    return flags;
}

static int bb_shadow_caster(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    nmo_object_id_t target = nmo_engine_block_target(b);
    int32_t index = eng_entity_index(engine, target);
    visuals_caster_t *caster = index >= 0 ? shadow_caster(engine, b) : NULL;
    if (caster == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        shadow_caster_remove(engine, caster);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
    }
    int32_t type = attribute_type_named(engine, "Shadow Caster Receiver");
    size_t receivers = 0;
    for (size_t i = 0; type >= 0 && i < engine->attributes.values.count; i++) {
        receivers += engine->attributes.values.data[i].type == type;
    }
    if (receivers == 0) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    eng_set_camera_fov(engine, caster->camera, SHADOW_FOV);
    float tanfov = tanf(SHADOW_FOV * 0.5f);
    nmo_object_id_t light_id = nmo_engine_block_get_input_object(b, 0);
    int32_t light = light_id != 0 ? eng_entity_index(engine, light_id) : -1;
    eng_light_t *light_desc = light >= 0 ? eng_light(engine, light_id) : NULL;
    int32_t camera = eng_entity_index(engine, caster->camera);
    if (light < 0 || camera < 0) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }
    bool perspective = light_desc == NULL || light_desc->type != VX_LIGHTDIREC;
    eng_vector_t barycenter;
    eng_entity_bary_center(engine, index, &barycenter);
    float lmin = get_in_float(b, 1, 5.0f), lmax = get_in_float(b, 2, 50.0f);
    caster->soft = get_in_int(b, 3, 0) != 0;

    /* RenderObjectsToTexture: the camera at the light, aimed at the hierarchy, backed off to fit it */
    eng_vector_t bmin = {0.0f, 0.0f, 0.0f}, bmax = {0.0f, 0.0f, 0.0f};
    eng_entity_hierarchical_box(engine, index, &bmin, &bmax);
    eng_vector_t center = marks_v((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f);
    const eng_matrix_t *lw = &engine->entities.data[light].world;
    eng_vector_t light_at = marks_v(lw->m[3][0], lw->m[3][1], lw->m[3][2]);
    eng_entity_set_position(engine, camera, &light_at, -1, false);
    eng_entity_look_at(engine, camera, &center, -1, false);
    eng_vector_t to_light = vec_sub(light_at, center);
    float distance = eng_vector_magnitude(&to_light);
    caster->render = distance < lmax;
    if (!caster->render) {
        distance = lmax;
    }
    float delta = (distance - lmin) / (lmax - lmin);
    caster->delta = delta >= 0.0f ? (delta < 1.0f ? delta : 1.0f) : 0.0f;
    eng_matrix_t inverse;
    eng_matrix_inverse(&inverse, &engine->entities.data[camera].world);
    float minx = 0.0f, maxx = 0.0f, miny = 0.0f, maxy = 0.0f, minz = 0.0f;
    for (int k = 0; k < 8; k++) {
        eng_vector_t corner = marks_v((k & 1) ? bmax.x : bmin.x, (k & 2) ? bmax.y : bmin.y, (k & 4) ? bmax.z : bmin.z), p;
        eng_vector_transform(&p, &inverse, &corner);
        if (k == 0 || p.x < minx) minx = p.x;
        if (k == 0 || p.x > maxx) maxx = p.x;
        if (k == 0 || p.y < miny) miny = p.y;
        if (k == 0 || p.y > maxy) maxy = p.y;
        if (k == 0 || p.z < minz) minz = p.z;
    }
    maxx -= minx;
    maxy -= miny;
    float fit = (maxx > maxy ? maxx : maxy) * 0.5f / tanfov;
    eng_vector_t back = {0.0f, 0.0f, -(fit - minz)};
    eng_entity_translate(engine, camera, &back, camera, false);
    visuals_pass_request_t request;
    memset(&request, 0, sizeof(request));
    request.node = b->node;
    request.shadow = true;
    request.texture = caster->texture;
    visuals_request_pass(engine, &request);
    bool in_range = caster->render;

    /* The receivers: the channel and its UVs from the light */
    const eng_matrix_t cam = engine->entities.data[camera].world;
    eng_matrix_inverse(&inverse, &cam);
    float near_plane = 1.0f, far_plane = 4000.0f;
    eng_camera_planes(engine, caster->camera, &near_plane, &far_plane);
    eng_vector_t bary_cam;
    eng_vector_transform(&bary_cam, &inverse, &barycenter);
    for (size_t i = 0; i < engine->attributes.values.count; i++) {
        const eng_attribute_t *attribute = &engine->attributes.values.data[i];
        int32_t o = attribute->type == type ? eng_entity_index(engine, attribute->object) : -1;
        eng_mesh_copy_t *copy = o >= 0 ? eng_mesh_copy(engine, entity_current_mesh(engine, attribute->object)) : NULL;
        nmo_engine_mesh_t mesh;
        if (copy == NULL || !nmo_engine_mesh(engine, copy->mesh, &mesh)) {
            continue;
        }
        int32_t channel = eng_mesh_channel(copy, caster->material);
        eng_matrix_t to_cam;
        eng_matrix_multiply(&to_cam, &inverse, &engine->entities.data[o].world);
        bool outside = !in_range;
        if (!outside) { /* !VxIntersect::FrustumOBB, by the box's corners */
            eng_vector_t lmin_box, lmax_box;
            eng_entity_local_box(engine, o, &lmin_box, &lmax_box);
            uint32_t all = 0xFFFFFFFFu;
            for (int k = 0; k < 8; k++) {
                eng_vector_t corner = marks_v((k & 1) ? lmax_box.x : lmin_box.x, (k & 2) ? lmax_box.y : lmin_box.y,
                                              (k & 4) ? lmax_box.z : lmin_box.z), p;
                eng_vector_transform(&p, &to_cam, &corner);
                all &= shadow_classify(p, near_plane, far_plane, tanfov);
            }
            outside = all != 0;
        }
        if (outside) {
            if (channel >= 0) {
                copy->channels.data[channel].active = false;
                copy->version = ++engine->render.mesh_version;
            }
            continue;
        }
        if (channel < 0) {
            channel = eng_mesh_add_channel(copy, caster->material);
            if (channel < 0) {
                continue;
            }
            copy->channels.data[channel].lit = false;
            copy->channels.data[channel].source_blend = VXBLEND_ZERO;
            copy->channels.data[channel].dest_blend = VXBLEND_SRCCOLOR;
            copy->channels.data[channel].mask = (uint16_t)(1u << channel);
        }
        copy->channels.data[channel].active = true;
        copy->version = ++engine->render.mesh_version;
        if (!nmo_engine_object_is_visible(engine, attribute->object)) {
            copy->channels.data[channel].active = false;
            continue;
        }
        /* AddChannel(material, FALSE): UVs of its own */
        nmo_engine_channel_t *ch = &copy->channels.data[channel];
        if (ch->uvs == NULL && mesh.vertex_count > 0) {
            ch->uvs = (float *)calloc((size_t)mesh.vertex_count * 2u, sizeof(float));
        }
        size_t stride = 2u * sizeof(float);
        int32_t count = ch->uvs != NULL ? mesh.vertex_count : 0;
        float *uvs = (float *)ch->uvs;
        nmo_face_t *faces = eng_mesh_copy_faces(engine, copy);
        uint32_t *flags = count > 0 ? (uint32_t *)calloc((size_t)count, sizeof(uint32_t)) : NULL;
        if (uvs == NULL || flags == NULL) {
            free(flags);
            continue;
        }
        eng_vector_t camdir;
        eng_entity_orientation(engine, camera, &camdir, NULL, NULL, o);
        eng_matrix_t m = to_cam;
        if (!perspective) {
            float tx = 0.5f / maxx, ty = 1.0f / maxy;
            for (int r = 0; r < 4; r++) {
                m.m[r][0] *= tx;
                m.m[r][1] = -(m.m[r][1] * ty);
            }
            m.m[3][0] += 0.5f;
            m.m[3][1] += 0.5f;
        }
        for (int32_t v = 0; v < count && v < mesh.vertex_count; v++) {
            const nmo_vector_t *q = &copy->vertices[v].position;
            eng_vector_t local = marks_v(q->x, q->y, q->z), p;
            eng_vector_transform(&p, &m, &local);
            float *uv = (float *)((uint8_t *)uvs + (size_t)v * stride);
            if (perspective) {
                flags[v] = shadow_classify(p, bary_cam.z, far_plane, tanfov);
                float inv = 0.5f / (tanfov * p.z);
                uv[0] = 0.5f + p.x * inv;
                uv[1] = 0.5f - p.y * inv;
            } else {
                uv[0] = p.x;
                uv[1] = p.y;
                flags[v] = (p.z < bary_cam.z ? VXCLIP_FRONT : 0u) | (uv[0] < 0.0f ? VXCLIP_LEFT : 0u) |
                           (uv[0] > 1.0f ? VXCLIP_RIGHT : 0u) | (uv[1] < 0.0f ? VXCLIP_TOP : 0u) |
                           (uv[1] > 1.0f ? VXCLIP_BOTTOM : 0u);
            }
        }
        uint16_t bit = (uint16_t)(1u << channel);
        for (int32_t f = 0; faces != NULL && f < mesh.face_count; f++) {
            const uint16_t *k = &mesh.indices[f * 3];
            bool clipped = k[0] < count && k[1] < count && k[2] < count && (flags[k[0]] & flags[k[1]] & flags[k[2]]) != 0;
            const nmo_vector_t *n = &faces[f].normal;
            if (!clipped && n->x * camdir.x + n->y * camdir.y + n->z * camdir.z < 0.0f) {
                faces[f].channel_mask |= bit;
            } else {
                faces[f].channel_mask &= (uint16_t)~bit;
            }
        }
        free(flags);
        copy->version = ++engine->render.mesh_version;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* HierarchicalRendering in black */
static void shadow_caster_draw(nmo_engine_t *engine, int32_t index)
{
    nmo_object_id_t id = engine->entities.data[index].id;
    if (eng_object_visibility(engine, id) & 0x200) { /* IsHierarchicallyHide */
        return;
    }
    nmo_object_id_t mesh = nmo_engine_object_is_visible(engine, id) ? entity_shown_mesh(engine, id) : 0;
    if (mesh != 0) {
        nmo_engine_draw_t *draw = eng_render_entity_mesh(engine, id, mesh, engine->entities.data[index].world.m[0], false);
        if (draw != NULL) {
            draw->blackened = true;
            draw->no_z_test = true;
        }
    }
    for (int32_t child = engine->entities.data[index].first_child; child >= 0;
         child = engine->entities.data[child].next_sibling) {
        shadow_caster_draw(engine, child);
    }
}

/* DrawFillRectangleGradient over the whole pass, added */
static void shadow_caster_fill(nmo_engine_t *engine, const nmo_engine_frame_t *frame, const float top[4],
                               const float bottom[4])
{
    nmo_engine_draw_t *draw = eng_render_push(engine);
    if (draw != NULL) {
        draw->kind = NMO_ENGINE_DRAW_QUAD;
        draw->rect[2] = (float)frame->width;
        draw->rect[3] = (float)frame->height;
        draw->color = vxcolor_to_color(top);
        draw->color2 = vxcolor_to_color(bottom);
        draw->blend = true;
        draw->source_blend = draw->dest_blend = VXBLEND_ONE;
    }
}

/* The shadow pass: cleared white, the target in black and the distance's white added */
static void shadow_caster_pass(nmo_engine_t *engine, int32_t node)
{
    visuals_caster_t *caster = NULL;
    visuals_t *visuals = visuals_state(engine);
    for (size_t i = 0; i < visuals->casters.count; i++) {
        if (visuals->casters.data[i].node == node) {
            caster = &visuals->casters.data[i];
        }
    }
    int32_t camera = caster != NULL ? eng_entity_index(engine, caster->camera) : -1;
    nmo_engine_frame_t *frame = camera >= 0 ? eng_render_begin_pass(engine, caster->texture, 0) : NULL;
    if (frame == NULL) {
        return;
    }
    frame->camera = caster->camera;
    memcpy(frame->view, engine->entities.data[camera].world.m, sizeof(frame->view));
    frame->orthographic = false;
    eng_camera_fov(engine, caster->camera, &frame->fov);
    eng_camera_planes(engine, caster->camera, &frame->near_plane, &frame->far_plane);
    frame->background_color = 0xFFFFFFFFu;
    frame->background_texture = 0;
    frame->render_options = CK_RENDER_CLEARBACK | CK_RENDER_CLEARZ | CK_RENDER_CLEARVIEWPORT;
    nmo_engine_frame_t view = *frame;
    if (caster->render) {
        nmo_engine_block_t handle = {engine, node};
        int32_t index = eng_entity_index(engine, nmo_engine_block_target(&handle));
        if (index >= 0) {
            shadow_caster_draw(engine, index);
        }
        const float lift[4] = {caster->delta, caster->delta, caster->delta, caster->delta};
        shadow_caster_fill(engine, &view, lift, lift);
        if (caster->soft) {
            const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f}, gray[4] = {0.2f, 0.2f, 0.2f, 1.0f};
            shadow_caster_fill(engine, &view, white, gray);
        }
    }
    eng_render_end_pass(engine);
}

static int cb_shadow_caster(blk_t *b, uint32_t message)
{
    if (message == NMO_ENGINE_MSG_LOAD) {
        shadow_caster(b->engine, b);
    } else if (message == NMO_ENGINE_MSG_PAUSE) {
        visuals_caster_t *caster = shadow_caster(b->engine, b);
        if (caster != NULL) {
            shadow_caster_remove(b->engine, caster);
        }
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
    {NMO_GUID_INIT(0x215c7794, 0x7ac40f5d), "Render Curve", bb_render_curve, NULL, NULL},
    {NMO_GUID_INIT(0x17cb4c57, 0x00f525fb), "Show Object Information", bb_show_object_information,
     NULL, NULL},
    {NMO_GUID_INIT(0x79b95ed0, 0x3e4a01f5), "Simple Shadow", bb_simple_shadow, cb_simple_shadow, NULL},
    {NMO_GUID_INIT(0xd00d010a, 0xe00d010a), "Solid Trail", bb_solid_trail, cb_solid_trail, NULL},
    {NMO_GUID_INIT(0x09a06075, 0x760a4720), "Mark System", bb_mark_system, cb_mark_system, NULL},
    {NMO_GUID_INIT(0xcc030377, 0x897aaaaa), "Vertex Render", bb_vertex_render, cb_vertex_render, NULL},
    {NMO_GUID_INIT(0x06d0b04b, 0x1d4ea106), "Motion Blur", bb_motion_blur, cb_motion_blur, NULL},
    {NMO_GUID_INIT(0x44fe1b3c, 0x29ba3445), "Planar Reflection", bb_planar_reflection, NULL, NULL},
    {NMO_GUID_INIT(0x731072fa, 0x56cf087f), "Planar Shadow", bb_planar_shadow, NULL, NULL},
    {NMO_GUID_INIT(0x67fc7084, 0x141154f7), "Texture Render", bb_texture_render, NULL, NULL},
    {NMO_GUID_INIT(0x0718360b, 0x71e5002f), "ShadowCaster", bb_shadow_caster, cb_shadow_caster, NULL},
};

/* The render callbacks, at the stages the render context calls them */
static void visuals_render(nmo_engine_t *engine, void *state, eng_render_stage_t stage,
                           const eng_render_args_t *args)
{
    visuals_t *visuals = (visuals_t *)state;
    if (stage == ENG_RENDER_PASSES) {
        for (size_t i = 0; i < visuals->passes.count; i++) {
            visuals_pass_request_t request = visuals->passes.data[i];
            if (request.shadow) {
                shadow_caster_pass(engine, request.node);
            } else {
                eng_render_scene_pass(engine, request.texture, request.slot, &request.view);
            }
        }
        visuals->passes.count = 0;
        return;
    }
    if (stage == ENG_RENDER_SCENE) {
        for (size_t i = 0; i < visuals->curves.count; i++) {
            draw_curve(engine, &visuals->curves.data[i], args);
        }
        for (size_t i = 0; i < visuals->marks.count; i++) {
            draw_marks(engine, &visuals->marks.data[i], false);
        }
        for (size_t i = 0; i < visuals->infos.count; i++) {
            draw_info(engine, visuals->infos.data[i].node);
        }
        if (!args->pass) { /* temporary callbacks, the frame's */
            visuals->curves.count = 0;
            visuals->marks.count = 0;
            visuals->infos.count = 0;
        }
    } else if (stage == ENG_RENDER_OVERLAY) {
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
    free(((visuals_t *)state)->curves.data);
    free(((visuals_t *)state)->infos.data);
    free(((visuals_t *)state)->shadows.data);
    for (size_t i = 0; i < ((visuals_t *)state)->mark_systems.count; i++) {
        free(((visuals_t *)state)->mark_systems.data[i].pool);
        free(((visuals_t *)state)->mark_systems.data[i].lists.data);
    }
    free(((visuals_t *)state)->mark_systems.data);
    free(((visuals_t *)state)->marks.data);
    free(((visuals_t *)state)->blurs.data);
    free(((visuals_t *)state)->passes.data);
    free(((visuals_t *)state)->casters.data);
}

nmo_status_t eng_register_visuals(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine, &visuals_manager, NULL);
    return status == NMO_OK ? ENG_REGISTER_BLOCKS(engine, eng_visuals_blocks) : status;
}
