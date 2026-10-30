#include "engine_fixture.h"

#include "behavior/nmo_behavior_view.h"
#include "object/builtin/nmo_behavior_schemas.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_KEY_EVENT NMO_GUID(0x1af2274b, 0x6b8c1524)
#define GUID_KEY_WAITER NMO_GUID(0x016d010b, 0x017d010b)
#define GUID_SWITCH_ON_KEY NMO_GUID(0x1f49612e, 0x6cd63de0)
#define GUID_MOUSE_WAITER NMO_GUID(0x014d010b, 0x015d010b)
#define GUID_GET_MOUSE_POSITION NMO_GUID(0x5b1f1381, 0x5c640372)
#define GUID_TT_KEY_WAITER NMO_GUID(0x2ffb3ef0, 0x21807ae3)
#define GUID_INPUT_STRING NMO_GUID(0x693e7e4b, 0x100104c4)

#define KEY_SPACE 0x39u
#define KEY_ENTER 0x1Cu
#define KEY_SHIFT 0x2Au
#define KEY_BACKSPACE 0x0Eu
#define KEY_LEFT 0xCBu
#define KEY_C 0x2Eu
#define KEY_X 0x2Du

/* The keys go down for a frame, then up for a frame */
static void press(engine_fixture_t *f, const uint32_t *keys, int count)
{
    for (int i = 0; i < count; i++) {
        nmo_engine_set_key(f->engine, keys[i], true);
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f->engine, 16.0f));
    for (int i = 0; i < count; i++) {
        nmo_engine_set_key(f->engine, keys[i], false);
    }
    ASSERT_EQ(NMO_OK, nmo_engine_step(f->engine, 16.0f));
}

TEST(engine_input, key_event_reports_presses_and_releases)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t event = fixture_block(&f, GUID_KEY_EVENT, "Key Event");
    nmo_object_id_t pressed = fixture_block(&f, GUID_NOP, "Pressed");
    nmo_object_id_t released = fixture_block(&f, GUID_NOP, "Released");
    ASSERT_TRUE(event != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, event, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, event, 0), BLOCK_IN(&f, pressed, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, event, 1), BLOCK_IN(&f, released, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, event, 0), (int32_t)KEY_SPACE);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, pressed));
    nmo_engine_set_key(f.engine, KEY_SPACE, true);
    ASSERT_FALSE(nmo_engine_is_key_down(f.engine, KEY_SPACE)); /* from the next frame on */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_TRUE(nmo_engine_is_key_down(f.engine, KEY_SPACE));
    ASSERT_EQ(1, nmo_engine_key_buffer_count(f.engine));
    ASSERT_EQ(1u, runs(&f, pressed));
    /* Held: no second press */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0, nmo_engine_key_buffer_count(f.engine));
    ASSERT_EQ(1u, runs(&f, pressed));
    nmo_engine_set_key(f.engine, KEY_SPACE, false);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, released));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, event));
    fixture_end(&f);
}

TEST(engine_input, key_waiter_and_switch_on_key)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t waiter = fixture_block(&f, GUID_KEY_WAITER, "Wait Enter");
    nmo_object_id_t after = fixture_block(&f, GUID_NOP, "After Enter");
    nmo_object_id_t sw = fixture_block(&f, GUID_SWITCH_ON_KEY, "Switch");
    nmo_object_id_t on_space = fixture_block(&f, GUID_NOP, "On Space");
    nmo_object_id_t on_enter = fixture_block(&f, GUID_NOP, "On Enter");
    ASSERT_TRUE(waiter != 0 && sw != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, waiter, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, waiter, 0), BLOCK_IN(&f, after, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, sw, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sw, 0), BLOCK_IN(&f, on_space, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, sw, 1), BLOCK_IN(&f, on_enter, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, waiter, 0), (int32_t)KEY_ENTER);
    fixture_input_int(&f, BLOCK_PIN(&f, sw, 0), (int32_t)KEY_SPACE);
    fixture_input_int(&f, BLOCK_PIN(&f, sw, 1), (int32_t)KEY_ENTER);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_set_key(f.engine, KEY_SPACE, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* Switch On Key fires every frame the key is down; the waiter keeps waiting */
    ASSERT_EQ(2u, runs(&f, on_space));
    ASSERT_EQ(0u, runs(&f, on_enter));
    ASSERT_EQ(0u, runs(&f, after));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, waiter));
    nmo_engine_set_key(f.engine, KEY_SPACE, false);
    nmo_engine_set_key(f.engine, KEY_ENTER, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, after));
    ASSERT_EQ(1u, runs(&f, on_enter));
    ASSERT_EQ(2u, runs(&f, on_space));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, waiter));
    fixture_end(&f);
}

TEST(engine_input, mouse_waiter_and_position)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t waiter = fixture_block(&f, GUID_MOUSE_WAITER, "Mouse Waiter");
    nmo_object_id_t moved = fixture_block(&f, GUID_NOP, "Moved");
    nmo_object_id_t clicked = fixture_block(&f, GUID_NOP, "Left Down");
    nmo_object_id_t get = fixture_block(&f, GUID_GET_MOUSE_POSITION, "Get Position");
    ASSERT_TRUE(waiter != 0 && get != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, waiter, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, waiter, 0), BLOCK_IN(&f, moved, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, waiter, 1), BLOCK_IN(&f, clicked, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, waiter, 1), BLOCK_IN(&f, get, 0), 0);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(0u, runs(&f, moved));
    nmo_engine_set_mouse_position(f.engine, 120.0f, 80.0f);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, moved));
    float dx = 0.0f, dy = 0.0f;
    nmo_engine_mouse_relative(f.engine, &dx, &dy, NULL);
    ASSERT_TRUE(dx == 120.0f && dy == 80.0f);
    nmo_engine_set_mouse_button(f.engine, 0, true);
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, moved)); /* it did not move again */
    ASSERT_EQ(1u, runs(&f, clicked));
    int32_t x = 0;
    read_value(&f, BLOCK_POUT(&f, get, 0), &x, sizeof(x));
    ASSERT_EQ(120, x);
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, waiter)); /* "Stay Active" */
    fixture_end(&f);
}

TEST(engine_input, key_names_round_trip)
{
    ASSERT_EQ(KEY_SPACE, nmo_behavior_keyboard_key_from_name("Space"));
    ASSERT_STR_EQ("Enter", nmo_behavior_keyboard_key_name(KEY_ENTER));
    ASSERT_EQ(0u, nmo_behavior_keyboard_key_from_name("Nope"));
}

TEST(engine_input, tt_key_waiter_stops_on_off)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t waiter = fixture_block(&f, GUID_TT_KEY_WAITER, "Wait Space");
    nmo_object_id_t after = fixture_block(&f, GUID_NOP, "After Space");
    ASSERT_TRUE(waiter != 0 && after != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, waiter, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, waiter, 1), 1);
    fixture_link(&f, BLOCK_OUT(&f, waiter, 0), BLOCK_IN(&f, after, 0), 0);
    fixture_input_int(&f, BLOCK_PIN(&f, waiter, 0), (int32_t)KEY_SPACE);
    fixture_commit(&f, NULL);

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, waiter));
    /* Off at frame 1: no more waiting, the key comes too late */
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, waiter));
    const uint32_t space[] = {KEY_SPACE};
    press(&f, space, 1);
    ASSERT_EQ(0u, runs(&f, after));
    fixture_end(&f);
}

TEST(engine_input, input_string_edits_until_enter)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t input = fixture_block(&f, GUID_INPUT_STRING, "Input");
    nmo_object_id_t on = fixture_block(&f, GUID_NOP, "On");
    nmo_object_id_t off = fixture_block(&f, GUID_NOP, "Off");
    nmo_object_id_t updated = fixture_block(&f, GUID_NOP, "Updated");
    ASSERT_TRUE(input != 0 && on != 0 && off != 0 && updated != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, input, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, input, 0), BLOCK_IN(&f, on, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, input, 1), BLOCK_IN(&f, off, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, input, 2), BLOCK_IN(&f, updated, 0), 0);
    fixture_input_value(&f, BLOCK_PIN(&f, input, 0), "ab", 3u);
    fixture_commit(&f, NULL);
    /* The DLL declares the settings before the locals (the prototype data the other way):
     * local 0 is Max Size, local 4 Use Caret */
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(&f, input);
    ASSERT_TRUE(state != NULL && state->local_parameters.count >= 8);
    const int32_t max_size = 512, use_caret = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&state->local_parameters, 0),
                          &max_size, sizeof(max_size)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&state->local_parameters, 4),
                          &use_caret, sizeof(use_caret)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    ASSERT_EQ(1u, runs(&f, on));
    const void *text = NULL;
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, input, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("ab", (const char *)text);
    /* Typed at the end (Shift makes it upper case), taken back, then typed one to the left */
    const uint32_t shift_c[] = {KEY_SHIFT, KEY_C};
    press(&f, shift_c, 2);
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, input, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("abC", (const char *)text);
    ASSERT_EQ(1u, runs(&f, updated));
    const uint32_t backspace[] = {KEY_BACKSPACE}, left[] = {KEY_LEFT}, x[] = {KEY_X};
    press(&f, backspace, 1);
    press(&f, left, 1);
    press(&f, x, 1);
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, input, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("axb", (const char *)text);
    ASSERT_EQ(4u, runs(&f, updated));
    ASSERT_EQ(0u, runs(&f, off));
    /* Enter ends it: Off */
    const uint32_t enter[] = {KEY_ENTER};
    press(&f, enter, 1);
    ASSERT_EQ(1u, runs(&f, off));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, input));
    ASSERT_EQ(NMO_OK, nmo_engine_param_value(f.engine, BLOCK_POUT(&f, input, 0), &text, NULL, NULL));
    ASSERT_STR_EQ("axb", (const char *)text);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_input, key_event_reports_presses_and_releases);
REGISTER_TEST(engine_input, key_waiter_and_switch_on_key);
REGISTER_TEST(engine_input, mouse_waiter_and_position);
REGISTER_TEST(engine_input, key_names_round_trip);
REGISTER_TEST(engine_input, tt_key_waiter_stops_on_off);
REGISTER_TEST(engine_input, input_string_edits_until_enter);
TEST_MAIN_END()
