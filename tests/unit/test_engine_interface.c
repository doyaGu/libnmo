#include "engine_fixture.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_CREATE_SYSTEM_FONT NMO_GUID(0x936334fc, 0xf243684f)
#define GUID_DELETE_FONT NMO_GUID(0x0153342c, 0xe543674f)
#define GUID_SET_FONT_PROPERTIES NMO_GUID(0xdacfbd61, 0x7a6e65e7)
#define GUID_TEXT_2D NMO_GUID(0x055b29fe, 0x662d5ca0)
#define GUID_PUSH_BUTTON NMO_GUID(0x761b7e64, 0x2b4c6911)

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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_interface, fonts_are_numbered_by_name);
REGISTER_TEST(engine_interface, text_2d_draws_until_off);
REGISTER_TEST(engine_interface, push_button_follows_the_mouse);
TEST_MAIN_END()
