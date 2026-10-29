/**
 * @file engine_interface.c
 * @brief Ports of the font, text and push button building blocks.
 *
 * Each function does what the block's function in Interface.dll does. The
 * font manager keeps fonts by name, numbered from 1 (CKFontManager's
 * CreateTextureFont, GetFont, DeleteFont); nothing draws them. A text block
 * stays active as it would to draw each frame, and reports an effect event
 * when it is turned on. PushButton tests the mouse against the 2D entity the
 * host says is under it.
 */

#include "engine_internal.h"

#include <stdlib.h>
#include <string.h>

typedef nmo_engine_block_t blk_t;

#define IN_ACTIVE(b, i) nmo_engine_block_is_input_active((b), (i))
#define IN_OFF(b, i) nmo_engine_block_activate_input((b), (i), false)
#define OUT_ON(b, i) nmo_engine_block_activate_output((b), (i), true)

static int32_t get_in_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_input_value(b, pos, &value, sizeof(value));
    return value;
}

static int32_t get_local_int(blk_t *b, int pos, int32_t fallback)
{
    int32_t value = fallback;
    nmo_engine_block_get_local_value(b, pos, &value, sizeof(value));
    return value;
}

static void set_local_int(blk_t *b, int pos, int32_t value)
{
    nmo_engine_block_set_local_value(b, pos, &value, sizeof(value));
}

/* The string an input reads, or NULL when it reads none. */
static const char *input_string(blk_t *b, int pos)
{
    size_t size = 0;
    const char *text = (const char *)nmo_engine_block_input_data(b, pos, &size);
    return text != NULL && size > 0 && memchr(text, '\0', size) != NULL ? text : NULL;
}

/* ============================================================================
 * Fonts
 * ============================================================================ */

void eng_fonts_clear(nmo_engine_t *engine)
{
    for (size_t i = 0; i < engine->fonts.count; i++) {
        free(engine->fonts.data[i].name);
    }
    engine->fonts.count = 0;
}

int32_t nmo_engine_font_count(const nmo_engine_t *engine)
{
    return engine != NULL ? (int32_t)engine->fonts.count : 0;
}

const char *nmo_engine_font_name(const nmo_engine_t *engine, int32_t font)
{
    if (engine == NULL || font <= 0 || (size_t)font > engine->fonts.count) {
        return NULL;
    }
    return engine->fonts.data[font - 1].name;
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
    for (size_t i = 0; i < engine->fonts.count; i++) {
        eng_font_t *font = &engine->fonts.data[i];
        if (strcmp(font->name, name) == 0) {
            free(font->name);
            font->name = copy;
            font->texture = texture;
            return (int32_t)i + 1;
        }
    }
    eng_font_t *font = NULL;
    if (!ENG_VEC_PUSH(engine->fonts, font)) {
        free(copy);
        return -1;
    }
    font->name = copy;
    font->texture = texture;
    return (int32_t)engine->fonts.count;
}

/* CKFontManager::DeleteFont: the fonts after it move down one. */
static void font_delete(nmo_engine_t *engine, int32_t index)
{
    if (index <= 0 || (size_t)index > engine->fonts.count) {
        return;
    }
    free(engine->fonts.data[index - 1].name);
    memmove(&engine->fonts.data[index - 1], &engine->fonts.data[index],
            (engine->fonts.count - (size_t)index) * sizeof(eng_font_t));
    engine->fonts.count--;
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

static const nmo_engine_block_desc_t eng_interface_blocks[] = {
    {NMO_GUID(0x936334fc, 0xf243684f), "Create System Font", bb_create_system_font, NULL, NULL},
    {NMO_GUID(0x7d644088, 0x40dd0349), "Create Font", bb_create_font, NULL, NULL},
    {NMO_GUID(0x260e4eb0, 0x0e256b90), "TT_CreateFontEx", bb_create_font, NULL, NULL},
    {NMO_GUID(0x0153342c, 0xe543674f), "Delete Font", bb_delete_font, NULL, NULL},
    {NMO_GUID(0xdacfbd61, 0x7a6e65e7), "Set Font Properties", bb_set_font_properties, NULL, NULL},
    {NMO_GUID(0x055b29fe, 0x662d5ca0), "2D Text", bb_text_2d, NULL, NULL},
    {NMO_GUID(0x1f5b2fe7, 0x6d355175), "3D Text", bb_text_3d, NULL, NULL},
    {NMO_GUID(0x761b7e64, 0x2b4c6911), "PushButton", bb_push_button, NULL, NULL},
};

nmo_status_t eng_register_interface(nmo_engine_t *engine)
{
    for (size_t i = 0; i < sizeof(eng_interface_blocks) / sizeof(eng_interface_blocks[0]); i++) {
        nmo_status_t status = nmo_engine_register_block(engine, &eng_interface_blocks[i]);
        if (status != NMO_OK) {
            return status;
        }
    }
    return NMO_OK;
}
