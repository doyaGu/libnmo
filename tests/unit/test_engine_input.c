#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "behavior/nmo_behavior_view.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#define GUID_NOP NMO_GUID(0x302561c4, 0x0d282980)
#define GUID_KEY_EVENT NMO_GUID(0x1af2274b, 0x6b8c1524)
#define GUID_KEY_WAITER NMO_GUID(0x016d010b, 0x017d010b)
#define GUID_SWITCH_ON_KEY NMO_GUID(0x1f49612e, 0x6cd63de0)
#define GUID_MOUSE_WAITER NMO_GUID(0x014d010b, 0x015d010b)
#define GUID_GET_MOUSE_POSITION NMO_GUID(0x5b1f1381, 0x5c640372)
#define GUID_TT_KEY_WAITER NMO_GUID(0x2ffb3ef0, 0x21807ae3)
#define GUID_INPUT_STRING NMO_GUID(0x693e7e4b, 0x100104c4)
#define GUID_TT_INPUT_STRING NMO_GUID(0x52fd6294, 0x612f51a5)
#define GUID_KEYBOARD_CONTROLLER NMO_GUID(0x43f1d000,0x1a2b3c4d)

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

typedef struct keyboard_fixture {
    engine_fixture_t f;
    nmo_object_id_t block, target_value, table;
} keyboard_fixture_t;

static void keyboard_begin(keyboard_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin(&g->f); g->block = fixture_block(&g->f,GUID_KEYBOARD_CONTROLLER,"Keyboard Controller"); ASSERT_TRUE(g->block != 0); nmo_object_id_t target = fixture_target(&g->f,g->block); ASSERT_TRUE(target != 0); fixture_input_int(&g->f,target,(int32_t)g->f.owner_id); g->target_value = nmo_parameterin_source_id(fixture_state(&g->f,target));
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block); g->table = nmo_behavior_ref_array_get_id(&state->local_parameters,0); ASSERT_TRUE(g->table != 0); fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
}

static void keyboard_ready(keyboard_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_KEYBOARD_CONTROLLER)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; nmo_engine_block_set_waiting_for_messages(&block,g->f.owner_id,true);
}

static void keyboard_received(keyboard_fixture_t *g, nmo_object_id_t receiver, const char *const *names, size_t count)
{
    ASSERT_EQ(count,g->f.engine->messages.received.count);
    for (size_t i = 0; i < count; i++) { const eng_received_t *message = &g->f.engine->messages.received.data[i]; ASSERT_EQ(receiver,message->object); ASSERT_EQ(receiver,message->sender); ASSERT_EQ(nmo_engine_message_type(g->f.engine,names[i]),message->type); }
}

TEST(engine_input, keyboard_controller_maps_native_scan_codes_and_repeats_held_keys_without_consuming_on)
{
    keyboard_fixture_t g; keyboard_begin(&g); const nmo_behavior_state_t *state = fixture_state(&g.f,g.block); ASSERT_EQ(0u,state->outputs.count); ASSERT_EQ(0u,state->in_parameters.count); ASSERT_EQ(0u,state->out_parameters.count); keyboard_ready(&g);
    const char *names[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6"}; int32_t table[10]; read_value(&g.f,g.table,table,sizeof(table)); const void *data = NULL; size_t size = 0; ASSERT_EQ(NMO_OK,nmo_engine_param_value(g.f.engine,g.table,&data,&size,NULL)); ASSERT_EQ(sizeof(table),size); for (size_t i = 0; i < 10; i++) ASSERT_EQ(nmo_engine_message_type(g.f.engine,names[i]),table[i]);
    const uint32_t keys[] = {0x4b,0x4d,0x48,0x50,0xd2,0xc7,0xc9,0xd3,0xcf,0xd1};
    for (size_t i = 0; i < 10; i++) {
        nmo_engine_set_key(g.f.engine,keys[i],true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,&names[i],1); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(0,nmo_engine_key_buffer_count(g.f.engine)); keyboard_received(&g,g.f.owner_id,&names[i],1); nmo_engine_set_key(g.f.engine,keys[i],false); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,NULL,0);
    }
    ASSERT_EQ(30u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_input, keyboard_controller_sends_buttons_before_directions_and_ignores_main_arrow_keys)
{
    keyboard_fixture_t g; keyboard_begin(&g); keyboard_ready(&g); const uint32_t arrows[] = {0xc8,0xd0,0xcb,0xcd}; for (size_t i = 0; i < 4; i++) nmo_engine_set_key(g.f.engine,arrows[i],true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,NULL,0);
    const uint32_t keys[] = {0xd2,0xc7,0xc9,0xd3,0xcf,0xd1,0x48,0x50,0x4b,0x4d}; const char *names[] = {"Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6","Joy_Up","Joy_Down","Joy_Left","Joy_Right"}; for (size_t i = 0; i < 10; i++) nmo_engine_set_key(g.f.engine,keys[i],true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); keyboard_received(&g,g.f.owner_id,names,10); fixture_end(&g.f);
}

TEST(engine_input, keyboard_controller_off_wins_before_target_and_table_validation_and_held_keys_do_not_restart_it)
{
    keyboard_fixture_t g; keyboard_begin(&g); keyboard_ready(&g); int32_t table[10]; read_value(&g.f,g.table,table,sizeof(table)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&(nmo_object_id_t){0},sizeof(nmo_object_id_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,table,4)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_activate_input(&block,1,true); nmo_engine_set_key(g.f.engine,0xd2,true);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1))); keyboard_received(&g,g.f.owner_id,NULL,0); for (int i = 0; i < 3; i++) ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); ASSERT_EQ(1u,runs(&g.f,g.block));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,table,sizeof(table))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&g.f.owner_id,sizeof(g.f.owner_id))); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *names[] = {"Joy_Button1"}; keyboard_received(&g,g.f.owner_id,names,1); ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_input, keyboard_controller_invalid_targets_report_owner_errors_without_consuming_on)
{
    for (int kind = 0; kind < 4; kind++) {
        keyboard_fixture_t g; keyboard_begin(&g); nmo_object_id_t other = fixture_create(&g.f,kind == 3 ? NMO_CID_GROUP : NMO_CID_OBJECT,"Invalid Receiver"); ASSERT_TRUE(other != 0); keyboard_ready(&g); nmo_object_id_t target = kind == 0 ? 0 : kind == 1 ? 0x7ffffffeu : other; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target))); if (kind == 3) nmo_engine_destroy_object(g.f.engine,other); nmo_engine_set_key(g.f.engine,0xd2,true); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(0u,g.f.engine->messages.sent.count); fixture_end(&g.f);
    }
}

TEST(engine_input, keyboard_controller_retargets_live_groups_as_single_recipients_and_uses_the_target_as_sender)
{
    keyboard_fixture_t g; keyboard_begin(&g); nmo_object_id_t owner = g.f.owner_id, group = fixture_create(&g.f,NMO_CID_GROUP,"Receiver Group"), input = 0; ASSERT_TRUE(group != 0); g.f.owner_id = group; ASSERT_TRUE(fixture_add_script(&g.f,"Receiver Script",&input) != 0); g.f.owner_id = owner; keyboard_ready(&g); ASSERT_TRUE(eng_group_add(g.f.engine,group,owner)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,group,true); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&group,sizeof(group))); nmo_engine_set_key(g.f.engine,0xd2,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *names[] = {"Joy_Button1"}; keyboard_received(&g,group,names,1);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&owner,sizeof(owner))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,owner,names,1); fixture_end(&g.f);
}

TEST(engine_input, keyboard_controller_uses_live_message_aliases_and_ignores_failed_individual_sends)
{
    keyboard_fixture_t g; keyboard_begin(&g); keyboard_ready(&g); int32_t table[10]; read_value(&g.f,g.table,table,sizeof(table)); table[4] = table[5]; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,table,sizeof(table))); nmo_engine_set_key(g.f.engine,0xd2,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *names[] = {"Joy_Button2"}; keyboard_received(&g,g.f.owner_id,names,1);
    table[4] = -1; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,table,sizeof(table))); nmo_engine_set_key(g.f.engine,0xc7,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,names,1); ASSERT_TRUE(nmo_engine_behavior_is_active(g.f.engine,g.block)); fixture_end(&g.f);
}

TEST(engine_input, keyboard_controller_truncated_tables_fail_before_sending_any_key_messages)
{
    const size_t sizes[] = {4,28,39}; int32_t table[10] = {0};
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        keyboard_fixture_t g; keyboard_begin(&g); keyboard_ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,table,sizes[i])); nmo_engine_set_key(g.f.engine,0xd2,true); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(0u,g.f.engine->messages.sent.count); fixture_end(&g.f);
    }
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

static const char *output_text(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const void *text = NULL;
    nmo_engine_param_value(f->engine, BLOCK_POUT(f, block, pos), &text, NULL, NULL);
    return text != NULL ? (const char *)text : "";
}

TEST(engine_input, tt_input_string_keeps_editing_past_enter)
{
    engine_fixture_t f;
    fixture_begin(&f);
    nmo_object_id_t input = fixture_block(&f, GUID_TT_INPUT_STRING, "Input");
    nmo_object_id_t on = fixture_block(&f, GUID_NOP, "On");
    nmo_object_id_t off = fixture_block(&f, GUID_NOP, "Off");
    nmo_object_id_t updated = fixture_block(&f, GUID_NOP, "Updated");
    ASSERT_TRUE(input != 0 && on != 0 && off != 0 && updated != 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, input, 0), 0);
    fixture_link(&f, f.script_in, BLOCK_IN(&f, input, 1), 8);
    fixture_link(&f, BLOCK_OUT(&f, input, 0), BLOCK_IN(&f, on, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, input, 1), BLOCK_IN(&f, off, 0), 0);
    fixture_link(&f, BLOCK_OUT(&f, input, 2), BLOCK_IN(&f, updated, 0), 0);
    fixture_input_value(&f, BLOCK_PIN(&f, input, 0), "ab", 3u);
    fixture_commit(&f, NULL);
    /* The DLL declares the settings first: local 0 is Max Size, local 2 Multiline */
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(&f, input);
    ASSERT_TRUE(state != NULL && state->local_parameters.count >= 7);
    const int32_t max_size = 512, single_line = 0;
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&state->local_parameters, 0),
                          &max_size, sizeof(max_size)));
    ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(
                          f.engine, nmo_behavior_ref_array_get_id(&state->local_parameters, 2),
                          &single_line, sizeof(single_line)));

    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    /* On: the Reset String in both */
    ASSERT_EQ(1u, runs(&f, on));
    ASSERT_STR_EQ("ab", output_text(&f, input, 0));
    ASSERT_STR_EQ("ab", output_text(&f, input, 1));
    const uint32_t c[] = {KEY_C}, enter[] = {KEY_ENTER};
    press(&f, c, 1);
    ASSERT_STR_EQ("abc", output_text(&f, input, 0));
    ASSERT_STR_EQ("abc", output_text(&f, input, 1));
    ASSERT_EQ(1u, runs(&f, updated));
    /* Enter on one line neither ends it nor types */
    press(&f, enter, 1);
    ASSERT_EQ(0u, runs(&f, off));
    ASSERT_EQ(1u, runs(&f, updated));
    ASSERT_TRUE(nmo_engine_behavior_is_active(f.engine, input));
    uint32_t key = 0;
    read_value(&f, BLOCK_POUT(&f, input, 2), &key, sizeof(key));
    ASSERT_EQ(KEY_ENTER, key);
    /* Off (frame 8): the text without its caret */
    for (int frame = 0; frame < 4; frame++) {
        ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 16.0f));
    }
    ASSERT_EQ(1u, runs(&f, off));
    ASSERT_STR_EQ("abc", output_text(&f, input, 0));
    ASSERT_FALSE(nmo_engine_behavior_is_active(f.engine, input));
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_input, key_event_reports_presses_and_releases);
REGISTER_TEST(engine_input, key_waiter_and_switch_on_key);
REGISTER_TEST(engine_input, mouse_waiter_and_position);
REGISTER_TEST(engine_input, key_names_round_trip);
REGISTER_TEST(engine_input, tt_key_waiter_stops_on_off);
REGISTER_TEST(engine_input, input_string_edits_until_enter);
REGISTER_TEST(engine_input, tt_input_string_keeps_editing_past_enter);
REGISTER_TEST(engine_input, keyboard_controller_maps_native_scan_codes_and_repeats_held_keys_without_consuming_on);
REGISTER_TEST(engine_input, keyboard_controller_sends_buttons_before_directions_and_ignores_main_arrow_keys);
REGISTER_TEST(engine_input, keyboard_controller_off_wins_before_target_and_table_validation_and_held_keys_do_not_restart_it);
REGISTER_TEST(engine_input, keyboard_controller_invalid_targets_report_owner_errors_without_consuming_on);
REGISTER_TEST(engine_input, keyboard_controller_retargets_live_groups_as_single_recipients_and_uses_the_target_as_sender);
REGISTER_TEST(engine_input, keyboard_controller_uses_live_message_aliases_and_ignores_failed_individual_sends);
REGISTER_TEST(engine_input, keyboard_controller_truncated_tables_fail_before_sending_any_key_messages);
TEST_MAIN_END()
