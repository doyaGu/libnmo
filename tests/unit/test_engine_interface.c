#include "engine_fixture.h"
#include "engine/nmo_engine_render.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_CREATE_SYSTEM_FONT NMO_GUID(0x936334fc, 0xf243684f)
#define GUID_DELETE_FONT NMO_GUID(0x0153342c, 0xe543674f)
#define GUID_SET_FONT_PROPERTIES NMO_GUID(0xdacfbd61, 0x7a6e65e7)
#define GUID_TEXT_2D NMO_GUID(0x055b29fe, 0x662d5ca0)
#define GUID_PUSH_BUTTON NMO_GUID(0x761b7e64, 0x2b4c6911)
#define GUID_TT_PUSH_BUTTON2 NMO_GUID(0x14d325d1, 0x6748654e)
#define GUID_SHOW_MOUSE_CURSOR NMO_GUID(0x16f6368f, 0x506b60fc)
#define GUID_TEXT_DISPLAY NMO_GUID(0xf22d010a, 0xf2cd010a)

typedef struct effect_log {
    size_t effects;
    nmo_object_id_t target;
} effect_log_t;

static void on_event(const nmo_engine_event_t *event, void *user_data)
{
    effect_log_t *log = (effect_log_t *)user_data;
    if (event->kind == NMO_ENGINE_EVENT_EFFECT) {
        log->effects++;
        log->target = event->object_id;
    }
}

/* A Create System Font making font `name`, run after `previous` (an output) */
static nmo_object_id_t system_font(engine_fixture_t *f, const char *name, nmo_object_id_t previous)
{
    nmo_object_id_t block = fixture_block(f, GUID_CREATE_SYSTEM_FONT, name);
    if (block == 0) {
        return 0;
    }
    fixture_link(f, previous, BLOCK_IN(f, block, 0), 0);
    fixture_input_value(f, BLOCK_PIN(f, block, 0), name, strlen(name) + 1);
    return block;
}

static int32_t output_int(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    int32_t value = 0;
    read_value(f, BLOCK_POUT(f, block, pos), &value, sizeof(value));
    return value;
}

/* The `pos`th local parameter (or setting) of a block */
static nmo_object_id_t block_local(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

TEST(engine_interface, fonts_are_numbered_by_name)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t a = system_font(&f, "A", f.script_in);
    ASSERT_TRUE(a != 0);
    nmo_object_id_t b = system_font(&f, "B", BLOCK_OUT(&f, a, 0));
    nmo_object_id_t again = system_font(&f, "A", BLOCK_OUT(&f, b, 0));
    nmo_object_id_t properties = fixture_block(&f, GUID_SET_FONT_PROPERTIES, "Properties");
    nmo_object_id_t remove = fixture_block(&f, GUID_DELETE_FONT, "Delete");
    ASSERT_TRUE(b != 0 && again != 0 && properties != 0 && remove != 0);
    fixture_link(&f, BLOCK_OUT(&f, again, 0), BLOCK_IN(&f, properties, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, properties, 0), BLOCK_IN(&f, remove, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, properties, 0), 1);
    fixture_input_int(&f, BLOCK_PIN(&f, remove, 0), 1);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    /* A font is numbered from 1; making a font of the same name replaces it there */
    ASSERT_EQ(1, output_int(&f, a, 0));
    ASSERT_EQ(2, output_int(&f, b, 0));
    ASSERT_EQ(1, output_int(&f, again, 0));
    ASSERT_EQ(1u, runs(&f, properties));
    /* Deleting font 1 moves B down */
    ASSERT_EQ(1, nmo_engine_font_count(f.engine));
    ASSERT_STR_EQ("B", nmo_engine_font_name(f.engine, 1));
    ASSERT_NULL(nmo_engine_font_name(f.engine, 2));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, f.script_id));
    fixture_end(&f);
}

TEST(engine_interface, text_2d_draws_until_off)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t frame = fixture_create(&f, NMO_CID_2DENTITY, "Frame");
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t text = fixture_block(&f, GUID_TEXT_2D, "Text");
    nmo_object_id_t once = fixture_block(&f, GUID_TEXT_2D, "Once");
    nmo_object_id_t exit_on = fixture_block(&f, GUID_NOP, "Exit On");
    nmo_object_id_t exit_off = fixture_block(&f, GUID_NOP, "Exit Off");
    ASSERT_TRUE(text != 0 && once != 0 && fixture_target(&f, text) != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, text, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, text, 1), 3);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, once, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, text, 0), BLOCK_IN(&f, exit_on, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, text, 1), BLOCK_IN(&f, exit_off, 0), 0);
    fixture_input_int(&f, fixture_target(&f, text), (int32_t)frame);
    nmo_object_id_t once_properties = block_local(&f, once, 0);
    ASSERT_TRUE(once_properties != 0);
    fixture_commit(&f, NULL);
    /* "Multiple": draw once each time On is activated */
    int32_t multiple = 256;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, once_properties, &multiple,
                                                 sizeof(multiple)));
    effect_log_t log = {0};
    nmo_engine_set_trace(f.engine, on_event, &log);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, exit_on));
    ASSERT_EQ(2u, log.effects);
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, once));
    for (int frame_index = 1; frame_index < 3; frame_index++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
        ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, text));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, exit_off));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, text));
    ASSERT_EQ(2u, log.effects);
    fixture_end(&f);
}

/* The frame's sprite text draw, or NULL */
static const nmo_engine_draw_t *sprite_text_draw(const nmo_engine_frame_t *frame)
{
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_SPRITE_TEXT) {
            return &frame->draws[i];
        }
    }
    return NULL;
}

TEST(engine_interface, text_display_shows_its_text_until_off)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t display = fixture_block(&f, GUID_TEXT_DISPLAY, "Text Display");
    nmo_object_id_t exit_off = fixture_block(&f, GUID_NOP, "Exit Off");
    ASSERT_TRUE(display != 0 && exit_off != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, display, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, display, 1), 2);
    fixture_link(&f, BLOCK_OUT(&f, display, 1), BLOCK_IN(&f, exit_off, 0), 0);
    const float offset[2] = {10.0f, 20.0f};
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, display, 0), offset, sizeof(offset));
    fixture_input_value(&f, BLOCK_PIN(&f, display, 1), red, sizeof(red));
    /* Left as CK reads the "Left" default (the edit layer's Sprite Text Alignment names differ) */
    fixture_input_int(&f, BLOCK_PIN(&f, display, 2), 2);
    fixture_input_int(&f, BLOCK_PIN(&f, display, 3), 16);
    fixture_input_value(&f, BLOCK_PIN(&f, display, 4), "Hello", 6);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));

    /* At Offset, the Sprite Size setting's 320 x 25, the text with a space after it, red
       reaching GDI as blue, Arial (no font info) at Size points, Left */
    const nmo_engine_draw_t *draw = sprite_text_draw(&frame);
    ASSERT_NOT_NULL(draw);
    ASSERT_FLOAT_EQ(10.0f, draw->rect[0], 0.0f);
    ASSERT_FLOAT_EQ(20.0f, draw->rect[1], 0.0f);
    ASSERT_FLOAT_EQ(330.0f, draw->rect[2], 0.0f);
    ASSERT_FLOAT_EQ(45.0f, draw->rect[3], 0.0f);
    ASSERT_STR_EQ("Hello ", draw->text);
    ASSERT_EQ(0xFF0000FFu, draw->color);
    ASSERT_STR_EQ("Arial", draw->font_name);
    ASSERT_EQ(16, draw->font_size);
    ASSERT_EQ(2u, draw->text_align);
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, display));

    /* Off two frames on: hidden, Exit Off, the block done */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(1u, runs(&f, exit_off));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, display));
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    ASSERT_NULL(sprite_text_draw(&frame));
    fixture_end(&f);
}

TEST(engine_interface, push_button_follows_the_mouse)
{
    engine_fixture_t f;
    fixture_begin(&f);
    /* PushButton has no target parameter: its frame is the script's owner */
    nmo_object_id_t frame = f.owner_id;
    nmo_object_id_t button = fixture_block(&f, GUID_PUSH_BUTTON, "Button");
    ASSERT_TRUE(button != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, button, 0), 0);
    /* Pressed, Released, Active, Enter Button, Exit Button, In Button */
    nmo_object_id_t outputs[6];
    for (int i = 0; i < 6; i++) {
        outputs[i] = fixture_block(&f, GUID_NOP, "Output");
        ASSERT_TRUE(outputs[i] != 0);
        fixture_link(&f, BLOCK_OUT(&f, button, (size_t)i), BLOCK_IN(&f, outputs[i], 0), 0);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_set_entity_under_mouse(f.engine, frame);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* enter */
    nmo_engine_set_mouse_button(f.engine, 0, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* press */
    nmo_engine_set_mouse_button(f.engine, 0, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* release over the button */
    nmo_engine_set_entity_under_mouse(f.engine, 0);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* leave */

    const uint64_t expected[6] = {1u, 1u, 1u, 1u, 1u, 3u};
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(expected[i], runs(&f, outputs[i]));
    }
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, button));
    fixture_end(&f);
}

TEST(engine_interface, tt_push_button2_needs_the_cursor_shown)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t frame = fixture_create(&f, NMO_CID_2DENTITY, "Frame");
    ASSERT_TRUE(frame != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t button = fixture_block(&f, GUID_TT_PUSH_BUTTON2, "Button");
    nmo_object_id_t hide = fixture_block(&f, GUID_SHOW_MOUSE_CURSOR, "Hide");
    ASSERT_TRUE(button != 0 && hide != 0 && fixture_target(&f, button) != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, button, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, hide, 0), 4);
    fixture_input_int(&f, fixture_target(&f, button), (int32_t)frame);
    fixture_input_int(&f, BLOCK_PIN(&f, hide, 0), 0); /* Show: no */
    /* Released, Roll Over, Mouse Down */
    nmo_object_id_t outputs[3];
    for (int i = 0; i < 3; i++) {
        outputs[i] = fixture_block(&f, GUID_NOP, "Output");
        ASSERT_TRUE(outputs[i] != 0);
        fixture_link(&f, BLOCK_OUT(&f, button, (size_t)i), BLOCK_IN(&f, outputs[i], 0), 0);
    }
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_TRUE(nmo_engine_cursor_is_visible(f.engine));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    nmo_engine_set_entity_under_mouse(f.engine, frame);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* roll over */
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    nmo_engine_set_mouse_button(f.engine, 0, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f)); /* mouse down */
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    nmo_engine_set_mouse_button(f.engine, 0, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(0u, runs(&f, outputs[0]));
    /* The cursor hidden in frame 4: the mouse is over nothing the button sees */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 20.0f));
    ASSERT_FALSE(nmo_engine_cursor_is_visible(f.engine));
    ASSERT_EQ(1u, runs(&f, outputs[0]));
    ASSERT_EQ(1u, runs(&f, outputs[1]));
    ASSERT_EQ(1u, runs(&f, outputs[2]));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_interface, fonts_are_numbered_by_name);
REGISTER_TEST(engine_interface, text_2d_draws_until_off);
REGISTER_TEST(engine_interface, text_display_shows_its_text_until_off);
REGISTER_TEST(engine_interface, push_button_follows_the_mouse);
REGISTER_TEST(engine_interface, tt_push_button2_needs_the_cursor_shown);
TEST_MAIN_END()
