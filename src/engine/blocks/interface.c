/**
 * @file interface.c
 * @brief Ports of the font, text and push button building blocks.
 *
 * Each function does what the block's function in Interface.dll does. The
 * font manager keeps fonts by name, numbered from 1 (CKFontManager's
 * CreateTextureFont, GetFont, DeleteFont); nothing draws them. A text block
 * stays active as it would to draw each frame, and reports an effect event
 * when it is turned on. PushButton tests the mouse against the 2D entity the
 * host says is under it, and 2D Picking asks the host's picker.
 */

#include "../engine_block_util.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Fonts
 * ============================================================================ */

void eng_fonts_clear(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->render.fonts.count; i++) {
        free(engine->render.fonts.data[i].name);
    }
    engine->render.fonts.count = 0;
}

int32_t nmo_engine_font_count(const nmo_engine_t *engine)
{
    return engine != NULL ? (int32_t)engine->render.fonts.count : 0;
}

const char *nmo_engine_font_name(const nmo_engine_t *engine, int32_t font)
{
    if (engine == NULL || font <= 0 || (size_t)font > engine->render.fonts.count) {
        return NULL;
    }
    return engine->render.fonts.data[font - 1].name;
}

/* CKFontManager::CreateTextureFont: a font of that name is replaced where it is. */
static int32_t font_create(nmo_engine_t *engine, const char *name, nmo_object_id_t texture)
{
    size_t length = strlen(name);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, name, length + 1);
    for (size_t i = 0; i < engine->render.fonts.count; i++) {
        eng_font_t *font = &engine->render.fonts.data[i];
        if (strcmp(font->name, name) == 0) {
            free(font->name);
            font->name = copy;
            font->texture = texture;
            return (int32_t)i + 1;
        }
    }
    eng_font_t *font = NULL;
    if (!ENG_VEC_PUSH(engine->render.fonts, font)) {
        free(copy);
        return -1;
    }
    font->name = copy;
    font->texture = texture;
    return (int32_t)engine->render.fonts.count;
}

/* CKFontManager::DeleteFont: the fonts after it move down one. */
static void font_delete(nmo_engine_t *engine, int32_t index)
{
    if (index <= 0 || (size_t)index > engine->render.fonts.count) {
        return;
    }
    free(engine->render.fonts.data[index - 1].name);
    memmove(&engine->render.fonts.data[index - 1], &engine->render.fonts.data[index],
            (engine->render.fonts.count - (size_t)index) * sizeof(eng_font_t));
    engine->render.fonts.count--;
}

/* Create System Font: Success with the font, from a system font made into a texture. */
static int bb_create_system_font(blk_t *b)
{
    IN_OFF(b, 0);
    const char *name = input_string(b, 0);
    if (name == NULL || get_in_int(b, 1, -1) == -1) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t font = font_create(nmo_engine_block_engine(b), name, 0);
    nmo_engine_block_set_output_value(b, 0, &font, sizeof(font));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Create Font: the font of a texture. TT_CreateFontEx too: its character coordinates draw. */
static int bb_create_font(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    const char *name = input_string(b, 0);
    if (name == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t texture = nmo_engine_block_get_input_object(b, 1);
    if (texture == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t font = font_create(nmo_engine_block_engine(b), name, texture);
    nmo_engine_block_set_output_value(b, 0, &font, sizeof(font));
    return NMO_ENGINE_BR_OK;
}

static int bb_delete_font(blk_t *b)
{
    IN_OFF(b, 0);
    int32_t font = get_in_int(b, 0, -1);
    if (font != -1) {
        font_delete(nmo_engine_block_engine(b), font);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* Set Font Properties: the spacing, scale, colors and shadow drawing would use. */
static int bb_set_font_properties(blk_t *b)
{
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/* ============================================================================
 * Text
 * ============================================================================ */

#define TEXT_MULTIPLE 256 /* draw once each time On is activated */

/* 2D Text: Off first; On draws from then on, every frame (or once, with Multiple). */
static int bb_text_2d(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_target(b), 0);
        if (get_local_int(b, 0, 0) & TEXT_MULTIPLE) {
            return NMO_ENGINE_BR_OK;
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* 3D Text: as 2D Text, with On tested before Off. */
static int bb_text_3d(blk_t *b)
{
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, nmo_engine_block_target(b), 0);
        if (get_local_int(b, 0, 0) & TEXT_MULTIPLE) {
            return NMO_ENGINE_BR_OK;
        }
    }
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * Text Display: its text sprite shown with On (Exit On), hidden with Off
 * (Exit Off, tested first); while shown it stays active to draw the text.
 */
static int bb_text_display(blk_t *b)
{
    if (IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        OUT_ON(b, 1);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 0);
        return NMO_ENGINE_BR_OK;
    }
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        OUT_ON(b, 0);
        eng_emit(b->engine, NMO_ENGINE_EVENT_EFFECT, b->node, 0, 1);
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/* ============================================================================
 * PushButton
 * ============================================================================ */

/* The button state (local 0) */
#define BUTTON_RELEASED 0
#define BUTTON_PRESSED 1
#define BUTTON_ROLLOVER 2
#define BUTTON_PUSHED 3
#define BUTTON_DISABLED 4

/* The "Events" setting (local 1): the outputs the button has, in this order */
enum {
    BOM_PRESSED = 1,
    BOM_RELEASED = 2,
    BOM_ACTIVE = 4,
    BOM_ENTERREGION = 8,
    BOM_EXITREGION = 16,
    BOM_INREGION = 32,
};

static int bb_push_button(blk_t *b)
{
    int32_t state = BUTTON_RELEASED;
    if (IN_ACTIVE(b, 0)) {
        IN_OFF(b, 0);
        set_local_int(b, 0, state);
    }
    if (nmo_engine_block_input_count(b) > 1 && IN_ACTIVE(b, 1)) {
        IN_OFF(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    if (nmo_engine_block_input_count(b) > 2 && IN_ACTIVE(b, 2)) {
        IN_OFF(b, 2);
        state = BUTTON_DISABLED;
        set_local_int(b, 0, state);
    }
    int32_t old = get_local_int(b, 0, 0);
    if (old & BUTTON_DISABLED) {
        return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
    }

    nmo_engine_t *engine = nmo_engine_block_engine(b);
    if (nmo_engine_block_target(b) == nmo_engine_entity_under_mouse(engine)) {
        state |= BUTTON_ROLLOVER;
    }
    if (nmo_engine_is_mouse_button_down(engine, 0)) {
        state |= BUTTON_PRESSED;
    }
    if (state != old) {
        set_local_int(b, 0, state);
    }

    if (nmo_engine_block_version(b) > 0x00020000u) {
        int32_t events = get_local_int(b, 1, 63);
        bool pressed = (state & BUTTON_PRESSED) != 0, over = (state & BUTTON_ROLLOVER) != 0;
        bool was_over = (old & BUTTON_ROLLOVER) != 0;
        const bool fired[6] = {
            !(old & BUTTON_PRESSED) && pressed && over, /* Pressed */
            old == BUTTON_PUSHED && !pressed && over,   /* Released */
            pressed && over,                            /* Active */
            !was_over && over,                          /* Enter Button */
            was_over && !over,                          /* Exit Button */
            over,                                       /* In Button */
        };
        int pos = 0;
        for (int bit = 0; bit < 6; bit++) {
            if ((events & (1 << bit)) == 0) {
                continue;
            }
            if (fired[bit] && pos < nmo_engine_block_output_count(b)) {
                OUT_ON(b, pos);
            }
            pos++;
        }
    } else if (state != old) {
        /* The old button: Released (0) on leaving, Roll Over (2) on entering, Pressed (1) on a click */
        if ((old & BUTTON_ROLLOVER) && !(state & BUTTON_ROLLOVER)) {
            OUT_ON(b, BUTTON_RELEASED);
        }
        if (!(old & BUTTON_ROLLOVER) && (state & BUTTON_ROLLOVER)) {
            OUT_ON(b, BUTTON_ROLLOVER);
        }
        if (old == BUTTON_PUSHED && state == BUTTON_ROLLOVER) {
            OUT_ON(b, BUTTON_PRESSED);
        }
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

/*
 * 2D Picking: what the render context picks at the mouse (Use Mouse
 * Coordinates?) or at Pos (in screen coordinates unless window relative):
 * True with the 3D entity, the point and normal in world space, the UVs,
 * distance and face, and the sprite; else False and no object. There is no
 * window, so screen and window coordinates are the same.
 */
static int bb_picking_2d(blk_t *b)
{
    nmo_engine_t *engine = nmo_engine_block_engine(b);
    IN_OFF(b, 0);
    int32_t x = 0, y = 0;
    if (get_local_int(b, 1, 0) != 0) {
        float mouse[2] = {0.0f, 0.0f};
        nmo_engine_mouse_position(engine, &mouse[0], &mouse[1]);
        x = (int32_t)(int64_t)mouse[0];
        y = (int32_t)(int64_t)mouse[1];
    } else if (get_local_int(b, 0, 0) != 0) {
        float pos[2] = {0.0f, 0.0f};
        nmo_engine_block_get_input_value(b, 0, pos, sizeof(pos));
        x = (int32_t)(int64_t)pos[0];
        y = (int32_t)(int64_t)pos[1];
    } else {
        x = get_in_int(b, 0, 0); /* before version 2: X and Y */
        y = get_in_int(b, 1, 0);
    }
    nmo_engine_pick_t pick;
    memset(&pick, 0, sizeof(pick));
    bool hit = engine->input.pick != NULL && engine->input.pick(engine->input.pick_user_data, x, y, &pick);
    if (!hit || (pick.object == 0 && pick.sprite == 0)) {
        const uint32_t none = 0;
        nmo_engine_block_set_output_value(b, 0, &none, sizeof(none));
        OUT_ON(b, 1);
        return NMO_ENGINE_BR_OK;
    }
    uint32_t object = (uint32_t)pick.object;
    nmo_engine_block_set_output_value(b, 0, &object, sizeof(object));
    int32_t entity = pick.object != 0 ? eng_entity_index(engine, pick.object) : -1;
    if (entity >= 0) {
        eng_vector_t local = {pick.point[0], pick.point[1], pick.point[2]}, world;
        eng_entity_transform(engine, entity, &world, &local, -1);
        nmo_engine_block_set_output_value(b, 1, &world, sizeof(world));
        eng_vector_t normal = {pick.normal[0], pick.normal[1], pick.normal[2]}, world_normal;
        eng_entity_transform_vector(engine, entity, &world_normal, &normal, -1, false);
        nmo_engine_block_set_output_value(b, 2, &world_normal, sizeof(world_normal));
        nmo_engine_block_set_output_value(b, 3, pick.uv, sizeof(pick.uv));
        nmo_engine_block_set_output_value(b, 4, &pick.distance, sizeof(pick.distance));
        nmo_engine_block_set_output_value(b, 5, &pick.face, sizeof(pick.face));
    }
    uint32_t sprite = (uint32_t)pick.sprite;
    nmo_engine_block_set_output_value(b, 6, &sprite, sizeof(sprite));
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_interface_blocks[] = {
    {NMO_GUID(0x936334fc, 0xf243684f), "Create System Font", bb_create_system_font, NULL, NULL},
    {NMO_GUID(0x7d644088, 0x40dd0349), "Create Font", bb_create_font, NULL, NULL},
    {NMO_GUID(0x260e4eb0, 0x0e256b90), "TT_CreateFontEx", bb_create_font, NULL, NULL},
    {NMO_GUID(0x0153342c, 0xe543674f), "Delete Font", bb_delete_font, NULL, NULL},
    {NMO_GUID(0xdacfbd61, 0x7a6e65e7), "Set Font Properties", bb_set_font_properties, NULL, NULL},
    {NMO_GUID(0x055b29fe, 0x662d5ca0), "2D Text", bb_text_2d, NULL, NULL},
    {NMO_GUID(0xf22d010a, 0xf2cd010a), "Text Display", bb_text_display, NULL, NULL},
    {NMO_GUID(0x1f5b2fe7, 0x6d355175), "3D Text", bb_text_3d, NULL, NULL},
    {NMO_GUID(0x761b7e64, 0x2b4c6911), "PushButton", bb_push_button, NULL, NULL},
    {NMO_GUID(0x1616120f, 0x5b2f48ce), "2D Picking", bb_picking_2d, NULL, NULL},
};

nmo_status_t eng_register_interface(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_interface_blocks);
}
