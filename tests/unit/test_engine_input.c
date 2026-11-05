#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "core/nmo_utils.h"

#include "behavior/nmo_behavior_view.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"

#include <math.h>

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
#define GUID_KEYBOARD_MAPPER NMO_GUID(0x053b2bde,0x2f8f1d74)
#define GUID_JOYSTICK_CONTROLLER NMO_GUID(0x0085ebc1,0x21935422)

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

static void joystick_neutral(const nmo_engine_joystick_state_t *state)
{
    ASSERT_FALSE(state->attached); ASSERT_EQ(0u,state->buttons); ASSERT_FLOAT_EQ(-1,state->point_of_view,0);
    for (size_t i = 0; i < 3; i++) { ASSERT_FLOAT_EQ(0,state->position[i],0); ASSERT_FLOAT_EQ(0,state->rotation[i],0); }
    for (size_t i = 0; i < 2; i++) ASSERT_FLOAT_EQ(0,state->sliders[i],0);
}

TEST(engine_input, joystick_states_take_effect_at_frame_boundaries_and_disconnects_clear_all_fields)
{
    engine_fixture_t f; fixture_begin(&f); fixture_commit_bare(&f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL)); nmo_engine_joystick_state_t observed; ASSERT_FALSE(nmo_engine_joystick_state(f.engine,3,&observed)); joystick_neutral(&observed);
    nmo_engine_joystick_state_t host = {.attached = true,.position = {.25f,-.5f,1.25f},.rotation = {1,2,3},.sliders = {-.75f,.5f},.point_of_view = .75f,.buttons = 0x80000021u}; nmo_engine_set_joystick(f.engine,3,&host); ASSERT_FALSE(nmo_engine_joystick_state(f.engine,3,NULL)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_TRUE(nmo_engine_joystick_state(f.engine,3,&observed));
    for (size_t i = 0; i < 3; i++) { ASSERT_FLOAT_EQ(host.position[i],observed.position[i],0); ASSERT_FLOAT_EQ(host.rotation[i],observed.rotation[i],0); } for (size_t i = 0; i < 2; i++) ASSERT_FLOAT_EQ(host.sliders[i],observed.sliders[i],0); ASSERT_FLOAT_EQ(host.point_of_view,observed.point_of_view,0); ASSERT_EQ(host.buttons,observed.buttons);
    host.position[0] = -.25f; nmo_engine_set_joystick(f.engine,3,&host); host.position[0] = 9; ASSERT_TRUE(nmo_engine_joystick_state(f.engine,3,&observed)); ASSERT_FLOAT_EQ(.25f,observed.position[0],0); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_TRUE(nmo_engine_joystick_state(f.engine,3,&observed)); ASSERT_FLOAT_EQ(-.25f,observed.position[0],0);
    nmo_engine_set_joystick(f.engine,3,NULL); ASSERT_TRUE(nmo_engine_joystick_state(f.engine,3,NULL)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_FALSE(nmo_engine_joystick_state(f.engine,3,&observed)); joystick_neutral(&observed); host.attached = false; nmo_engine_set_joystick(f.engine,3,&host); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_FALSE(nmo_engine_joystick_state(f.engine,3,&observed)); joystick_neutral(&observed); fixture_end(&f);
}

TEST(engine_input, four_joystick_slots_are_independent_and_invalid_indices_return_neutral_states)
{
    engine_fixture_t f; fixture_begin(&f); fixture_commit_bare(&f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(4,NMO_ENGINE_JOYSTICK_COUNT);
    for (int32_t i = 0; i < 4; i++) { nmo_engine_joystick_state_t state = {.attached = true,.position = {(float)i,0,0},.point_of_view = -1,.buttons = 1u << i}; nmo_engine_set_joystick(f.engine,i,&state); }
    const int32_t invalid[] = {-1,4,INT32_MAX}; nmo_engine_joystick_state_t state = {.attached = true,.buttons = UINT32_MAX}; nmo_engine_set_joystick(NULL,0,&state); for (size_t i = 0; i < 3; i++) { nmo_engine_set_joystick(f.engine,invalid[i],&state); ASSERT_FALSE(nmo_engine_joystick_state(f.engine,invalid[i],&state)); joystick_neutral(&state); } ASSERT_FALSE(nmo_engine_joystick_state(NULL,0,&state)); joystick_neutral(&state); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0));
    for (int32_t i = 0; i < 4; i++) { ASSERT_TRUE(nmo_engine_joystick_state(f.engine,i,&state)); ASSERT_FLOAT_EQ((float)i,state.position[0],0); ASSERT_EQ(1u << i,state.buttons); } fixture_end(&f);
}

TEST(engine_input, joystick_snapshots_remain_private_to_each_engine_and_survive_script_reactivation)
{
    engine_fixture_t f; fixture_begin(&f); fixture_commit_bare(&f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL)); nmo_engine_t *second = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(f.workspace,NULL,&second)); ASSERT_EQ(NMO_OK,nmo_engine_start(second,NMO_ENGINE_START_ALL)); nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,0,0},.point_of_view = -1,.buttons = 32}; nmo_engine_set_joystick(f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(second,0)); ASSERT_FALSE(nmo_engine_joystick_state(second,0,NULL)); state.position[0] = -.75f; nmo_engine_set_joystick(second,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(second,0)); ASSERT_TRUE(nmo_engine_joystick_state(f.engine,0,&state)); ASSERT_FLOAT_EQ(.75f,state.position[0],0); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,false,false)); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f.engine,f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,0)); ASSERT_TRUE(nmo_engine_joystick_state(f.engine,0,&state)); ASSERT_FLOAT_EQ(.75f,state.position[0],0); nmo_engine_destroy(second); fixture_end(&f);
}

typedef struct joystick_fixture {
    engine_fixture_t f;
    nmo_object_id_t block, target_value, number_value, table, sensitivity, output;
} joystick_fixture_t;

static void joystick_begin(joystick_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin(&g->f); g->block = fixture_block(&g->f,GUID_JOYSTICK_CONTROLLER,"Joystick Controller"); ASSERT_TRUE(g->block != 0); nmo_object_id_t target = fixture_target(&g->f,g->block), number = BLOCK_PIN(&g->f,g->block,0); fixture_input_int(&g->f,target,(int32_t)g->f.owner_id); fixture_input_int(&g->f,number,0); g->target_value = nmo_parameterin_source_id(fixture_state(&g->f,target)); g->number_value = nmo_parameterin_source_id(fixture_state(&g->f,number)); const nmo_behavior_state_t *state = fixture_state(&g->f,g->block); g->table = nmo_behavior_ref_array_get_id(&state->local_parameters,0); g->sensitivity = nmo_behavior_ref_array_get_id(&state->local_parameters,1); g->output = BLOCK_POUT(&g->f,g->block,0); fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
}

static void joystick_ready(joystick_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_JOYSTICK_CONTROLLER)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; nmo_engine_block_set_waiting_for_messages(&block,g->f.owner_id,true);
}

static void joystick_received(joystick_fixture_t *g, nmo_object_id_t receiver, const char *const *names, size_t count)
{
    ASSERT_EQ(count,g->f.engine->messages.received.count);
    for (size_t i = 0; i < count; i++) { const eng_received_t *message = &g->f.engine->messages.received.data[i]; ASSERT_EQ(receiver,message->object); ASSERT_EQ(g->f.owner_id,message->sender); ASSERT_EQ(nmo_engine_message_type(g->f.engine,names[i]),message->type); }
}

TEST(engine_input, joystick_controller_repeats_six_buttons_then_directions_and_publishes_the_raw_position)
{
    joystick_fixture_t g; joystick_begin(&g); joystick_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.position = {-1,-1,.75f},.point_of_view = -1,.buttons = UINT32_MAX}; nmo_engine_set_joystick(g.f.engine,0,&state); const char *names[] = {"Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6","Joy_Up","Joy_Left"};
    for (int frame = 0; frame < 2; frame++) { ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,names,8); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(state.position[i],output[i],0); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); } ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_input, joystick_controller_uses_strict_per_axis_thresholds_and_does_not_generate_z_directions)
{
    joystick_fixture_t g; joystick_begin(&g); joystick_ready(&g); const float positions[][3] = {{-.5f,-.5f,10},{.5f,.5f,-10},{-.5001f,-.5001f,3},{.5001f,.5001f,-3}}; const char *negative[] = {"Joy_Up","Joy_Left"}, *positive[] = {"Joy_Down","Joy_Right"};
    for (size_t i = 0; i < 4; i++) { nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; memcpy(state.position,positions[i],sizeof(state.position)); nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,16)); joystick_received(&g,g.f.owner_id,i == 2 ? negative : positive,i < 2 ? 0 : 2); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); ASSERT_FLOAT_EQ(positions[i][2],output[2],0); } fixture_end(&g.f);
}

TEST(engine_input, joystick_controller_switches_live_slots_and_targets_and_uses_its_owner_as_sender)
{
    joystick_fixture_t g; joystick_begin(&g); nmo_object_id_t owner = g.f.owner_id, group = fixture_create(&g.f,NMO_CID_GROUP,"Joystick Receiver"), input = 0; ASSERT_TRUE(group != 0); g.f.owner_id = group; ASSERT_TRUE(fixture_add_script(&g.f,"Joystick Receiver Script",&input) != 0); g.f.owner_id = owner; joystick_ready(&g); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,group,true);
    nmo_engine_joystick_state_t first = {.attached = true,.position = {-.75f,0,.25f},.point_of_view = -1}, second = {.attached = true,.position = {0,.9f,-.25f},.point_of_view = -1,.buttons = 4}; nmo_engine_set_joystick(g.f.engine,0,&first); nmo_engine_set_joystick(g.f.engine,3,&second); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *left[] = {"Joy_Left"}; joystick_received(&g,owner,left,1);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){3},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&group,sizeof(group))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *names[] = {"Joy_Button3","Joy_Down"}; joystick_received(&g,group,names,2); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(second.position[i],output[i],0); fixture_end(&g.f);
}

TEST(engine_input, joystick_controller_preserves_custom_negative_and_nonfinite_sensitivity_comparisons)
{
    joystick_fixture_t g; joystick_begin(&g); joystick_ready(&g); const float sensitivity[][3] = {{.25f,.75f,-1},{-.5f,-.5f,0},{NAN,.5f,0},{.5f,.5f,0}}; const float position[][3] = {{.3f,-.7f,9},{0,0,8},{1,0,7},{NAN,-INFINITY,6}};
    for (size_t i = 0; i < 4; i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.sensitivity,sensitivity[i],sizeof(sensitivity[i]))); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; memcpy(state.position,position[i],sizeof(state.position)); nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *right[] = {"Joy_Right"}, *both[] = {"Joy_Up","Joy_Down","Joy_Left","Joy_Right"}, *up[] = {"Joy_Up"}; joystick_received(&g,g.f.owner_id,i == 0 ? right : i == 1 ? both : up,i == 0 || i == 3 ? 1 : i == 1 ? 4 : 0); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); if (i == 3) { ASSERT_TRUE(isnan(output[0])); ASSERT_TRUE(isinf(output[1]) && output[1] < 0); } ASSERT_FLOAT_EQ(position[i][2],output[2],0);
    }
    fixture_end(&g.f);
}

TEST(engine_input, joystick_controller_off_precedes_invalid_targets_missing_devices_and_bad_tables)
{
    joystick_fixture_t g; joystick_begin(&g); joystick_ready(&g); const float sentinel[] = {17,18,19}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,sentinel,sizeof(sentinel))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&(nmo_object_id_t){0},sizeof(nmo_object_id_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,&(int32_t){0},sizeof(int32_t))); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1))); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(sentinel[i],output[i],0); ASSERT_EQ(0u,g.f.engine->messages.received.count); fixture_end(&g.f);
}

TEST(engine_input, joystick_controller_reports_missing_devices_and_invalid_targets_without_changing_output)
{
    for (int kind = 0; kind < 7; kind++) {
        joystick_fixture_t g; joystick_begin(&g); nmo_object_id_t other = fixture_create(&g.f,kind == 6 ? NMO_CID_GROUP : NMO_CID_OBJECT,"Invalid Joystick Target"); ASSERT_TRUE(other != 0); joystick_ready(&g); const float sentinel[] = {17,18,19}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,sentinel,sizeof(sentinel))); int32_t number = kind == 0 ? 0 : kind == 1 ? -1 : kind == 2 ? 4 : 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&number,sizeof(number)));
        if (kind >= 3) { nmo_object_id_t target = kind == 3 ? 0 : kind == 4 ? 0x7ffffffeu : other; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target))); if (kind == 6) nmo_engine_destroy_object(g.f.engine,other); }
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(kind < 3 ? NMO_ENGINE_BR_GENERICERROR : NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(sentinel[i],output[i],0); ASSERT_EQ(0u,g.f.engine->messages.sent.count); fixture_end(&g.f);
    }
}

TEST(engine_input, joystick_controller_truncated_tables_do_not_send_partial_messages_or_replace_output)
{
    const size_t sizes[] = {4,28,39};
    for (size_t i = 0; i < 3; i++) {
        joystick_fixture_t g; joystick_begin(&g); joystick_ready(&g); int32_t table[10]; read_value(&g.f,g.table,table,sizeof(table)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,table,sizes[i])); const float sentinel[] = {17,18,19}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,sentinel,sizeof(sentinel))); nmo_engine_joystick_state_t state = {.attached = true,.buttons = 63}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result); ASSERT_EQ(0u,g.f.engine->messages.sent.count); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t j = 0; j < 3; j++) ASSERT_FLOAT_EQ(sentinel[j],output[j],0); fixture_end(&g.f);
    }
}

TEST(engine_input, joystick_controller_disconnects_stop_sending_and_reconnection_requires_explicit_restart)
{
    joystick_fixture_t g; joystick_begin(&g); nmo_engine_options_t options = nmo_engine_options_default(); fixture_commit(&g.f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,g.f.owner_id,true); nmo_engine_joystick_state_t state = {.attached = true,.position = {.25f,0,0},.point_of_view = -1,.buttons = 1}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *first[] = {"Joy_Button1"}; joystick_received(&g,g.f.owner_id,first,1);
    nmo_engine_set_joystick(g.f.engine,0,NULL); ASSERT_TRUE(nmo_engine_joystick_state(g.f.engine,0,NULL)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_FALSE(nmo_engine_joystick_state(g.f.engine,0,NULL)); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); joystick_received(&g,g.f.owner_id,NULL,0); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); ASSERT_FLOAT_EQ(.25f,output[0],0);
    state.position[0] = .3f; state.buttons = 2; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,NULL,0); ASSERT_EQ(2u,runs(&g.f,g.block)); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *second[] = {"Joy_Button2"}; joystick_received(&g,g.f.owner_id,second,1); read_value(&g.f,g.output,output,sizeof(output)); ASSERT_FLOAT_EQ(.3f,output[0],0); ASSERT_EQ(3u,runs(&g.f,g.block)); fixture_end(&g.f);
}

typedef struct keyboard_fixture {
    engine_fixture_t f;
    nmo_object_id_t block, target_value, table, count;
    nmo_guid_t guid;
} keyboard_fixture_t;

static void keyboard_begin_variant(keyboard_fixture_t *g, nmo_guid_t guid, const char *name)
{
    memset(g,0,sizeof(*g)); g->guid = guid; fixture_begin(&g->f); g->block = fixture_block(&g->f,guid,name); ASSERT_TRUE(g->block != 0); nmo_object_id_t target = fixture_target(&g->f,g->block); ASSERT_TRUE(target != 0); fixture_input_int(&g->f,target,(int32_t)g->f.owner_id); g->target_value = nmo_parameterin_source_id(fixture_state(&g->f,target));
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block); g->table = nmo_behavior_ref_array_get_id(&state->local_parameters,0); ASSERT_TRUE(g->table != 0); fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    if (state->local_parameters.count > 1) g->count = nmo_behavior_ref_array_get_id(&state->local_parameters,1);
}

static void keyboard_begin(keyboard_fixture_t *g) { keyboard_begin_variant(g,GUID_KEYBOARD_CONTROLLER,"Keyboard Controller"); }
static void mapper_begin(keyboard_fixture_t *g) { keyboard_begin_variant(g,GUID_KEYBOARD_MAPPER,"Keyboard Mapper"); }

static void keyboard_ready(keyboard_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,g->guid)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; nmo_engine_block_set_waiting_for_messages(&block,g->f.owner_id,true);
}

static void keyboard_received(keyboard_fixture_t *g, nmo_object_id_t receiver, const char *const *names, size_t count)
{
    ASSERT_EQ(count,g->f.engine->messages.received.count);
    for (size_t i = 0; i < count; i++) { const eng_received_t *message = &g->f.engine->messages.received.data[i]; ASSERT_EQ(receiver,message->object); ASSERT_EQ(receiver,message->sender); ASSERT_EQ(nmo_engine_message_type(g->f.engine,names[i]),message->type); }
}

static void mapper_record(uint8_t *record, uint32_t key, int32_t type, const char *name)
{
    memset(record,0,40); nmo_write_u32_le(record,key); nmo_write_u32_le(record+4,(uint32_t)type); size_t length = strlen(name); memcpy(record+8,name,length < 32 ? length : 32);
}

static void mapper_saved(keyboard_fixture_t *g, const void *records, size_t bytes, int32_t count)
{
    nmo_parameter_state_t *state = fixture_state(&g->f,g->table); nmo_array_dispose(&state->buffer_data); ASSERT_EQ(NMO_OK,nmo_array_alloc(&state->buffer_data,1,bytes,NULL)); if (bytes != 0) memcpy(state->buffer_data.data,records,bytes); state->mode = CKPARAM_MODE_BUFFER; state->has_state = true; char text[32]; snprintf(text,sizeof(text),"%d",count); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,g->count,text));
}

TEST(engine_input, keyboard_mapper_loads_named_bindings_and_repeats_duplicate_keys_in_record_order)
{
    keyboard_fixture_t g; mapper_begin(&g); uint8_t records[120]; mapper_record(records,KEY_X,999,"Second"); mapper_record(records+40,KEY_C,998,"First"); mapper_record(records+80,KEY_X,997,"Second"); mapper_saved(&g,records,sizeof(records),3); keyboard_ready(&g); uint8_t runtime[120]; read_value(&g.f,g.table,runtime,sizeof(runtime));
    for (size_t i = 0; i < 3; i++) { ASSERT_EQ(nmo_read_u32_le(records+40*i),nmo_read_u32_le(runtime+40*i)); ASSERT_EQ(nmo_engine_message_type(g.f.engine,i == 1 ? "First" : "Second"),(int32_t)nmo_read_u32_le(runtime+40*i+4)); }
    nmo_engine_set_key(g.f.engine,KEY_X,true); nmo_engine_set_key(g.f.engine,KEY_C,true); const char *names[] = {"Second","First","Second"}; ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,names,3); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,names,3); ASSERT_EQ(0,nmo_engine_key_buffer_count(g.f.engine)); nmo_engine_set_key(g.f.engine,KEY_X,false); nmo_engine_set_key(g.f.engine,KEY_C,false); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,NULL,0); ASSERT_TRUE(memcmp(records,((nmo_parameter_state_t *)fixture_state(&g.f,g.table))->buffer_data.data,sizeof(records)) == 0); fixture_end(&g.f);
}

TEST(engine_input, keyboard_mapper_uses_the_behavior_owner_as_sender_when_targeting_a_group)
{
    keyboard_fixture_t g; mapper_begin(&g); uint8_t record[40]; mapper_record(record,KEY_C,-1,"Mapped"); mapper_saved(&g,record,sizeof(record),1); nmo_object_id_t owner = g.f.owner_id, group = fixture_create(&g.f,NMO_CID_GROUP,"Mapper Receiver"), input = 0; ASSERT_TRUE(group != 0); g.f.owner_id = group; ASSERT_TRUE(fixture_add_script(&g.f,"Receiver Script",&input) != 0); g.f.owner_id = owner; keyboard_ready(&g); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,group,true); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&group,sizeof(group))); nmo_engine_set_key(g.f.engine,KEY_C,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,g.f.engine->messages.received.count); const eng_received_t *message = &g.f.engine->messages.received.data[0]; ASSERT_EQ(group,message->object); ASSERT_EQ(owner,message->sender); ASSERT_EQ(nmo_engine_message_type(g.f.engine,"Mapped"),message->type); fixture_end(&g.f);
}

TEST(engine_input, keyboard_mapper_nonpositive_counts_are_idle_and_only_the_active_prefix_is_loaded)
{
    const char *counts[] = {"0","-1","-2147483648"};
    for (size_t i = 0; i < 3; i++) {
        keyboard_fixture_t g; mapper_begin(&g); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.count,counts[i])); keyboard_ready(&g); nmo_engine_set_key(g.f.engine,KEY_X,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,NULL,0); ASSERT_TRUE(nmo_engine_behavior_is_active(g.f.engine,g.block)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); fixture_end(&g.f);
    }
    keyboard_fixture_t g; mapper_begin(&g); uint8_t records[83]; mapper_record(records,KEY_X,-1,"Prefix"); mapper_record(records+40,KEY_X,123,"Unused"); memset(records+80,0xe7,3); mapper_saved(&g,records,sizeof(records),1); keyboard_ready(&g); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"Unused")); uint8_t runtime[83]; read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_TRUE(memcmp(records+40,runtime+40,43) == 0); nmo_engine_set_key(g.f.engine,KEY_X,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *names[] = {"Prefix"}; keyboard_received(&g,g.f.owner_id,names,1); fixture_end(&g.f);
}

TEST(engine_input, keyboard_mapper_live_keys_ids_and_counts_change_without_rebuilding_names_on_edit)
{
    keyboard_fixture_t g; mapper_begin(&g); uint8_t record[40]; mapper_record(record,KEY_C,-1,"Initial"); mapper_saved(&g,record,sizeof(record),1); keyboard_ready(&g); int32_t initial = nmo_engine_message_type(g.f.engine,"Initial"); mapper_record(record,KEY_X,initial,"Edited Name"); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,record,sizeof(record))); int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(g.f.engine,g.block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"Edited Name")); nmo_engine_set_key(g.f.engine,KEY_X,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *names[] = {"Initial"}; keyboard_received(&g,g.f.owner_id,names,1);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){0},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,NULL,0); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){1},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,names,1); fixture_end(&g.f);
}

TEST(engine_input, keyboard_mapper_out_of_range_keys_and_empty_names_do_not_block_valid_records)
{
    keyboard_fixture_t g; mapper_begin(&g); uint8_t records[280]; const uint32_t keys[] = {UINT32_MAX,256,0x80000000u,KEY_X,KEY_X,0,255}; const char *names[] = {"Negative","Too Large","Signed Limit","","Good","Zero","Last"};
    for (size_t i = 0; i < 7; i++) mapper_record(records+40*i,keys[i],999,names[i]); mapper_saved(&g,records,sizeof(records),7); keyboard_ready(&g); uint8_t runtime[280]; read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_EQ(UINT32_MAX,nmo_read_u32_le(runtime+3*40+4)); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"")); nmo_engine_set_key(g.f.engine,KEY_X,true); nmo_engine_set_key(g.f.engine,0,true); nmo_engine_set_key(g.f.engine,255,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *received[] = {"Good","Zero","Last"}; keyboard_received(&g,g.f.owner_id,received,3); fixture_end(&g.f);
}

TEST(engine_input, keyboard_mapper_off_precedes_invalid_state_and_preserves_on_until_restart)
{
    keyboard_fixture_t g; mapper_begin(&g); uint8_t record[40]; mapper_record(record,KEY_X,-1,"Held"); mapper_saved(&g,record,sizeof(record),1); keyboard_ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){2},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&(nmo_object_id_t){0},sizeof(nmo_object_id_t))); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_activate_input(&block,1,true); nmo_engine_set_key(g.f.engine,KEY_X,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1))); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){1},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&g.f.owner_id,sizeof(g.f.owner_id))); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *names[] = {"Held"}; keyboard_received(&g,g.f.owner_id,names,1); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); fixture_end(&g.f);
}

TEST(engine_input, keyboard_mapper_invalid_targets_consume_on_before_reporting_owner_errors)
{
    for (int kind = 0; kind < 4; kind++) {
        keyboard_fixture_t g; mapper_begin(&g); uint8_t record[40]; mapper_record(record,KEY_C,-1,"Target"); mapper_saved(&g,record,sizeof(record),1); nmo_object_id_t other = fixture_create(&g.f,kind == 3 ? NMO_CID_GROUP : NMO_CID_OBJECT,"Invalid Mapper Target"); ASSERT_TRUE(other != 0); keyboard_ready(&g); nmo_object_id_t target = kind == 0 ? 0 : kind == 1 ? 0x7ffffffeu : other; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target))); if (kind == 3) nmo_engine_destroy_object(g.f.engine,other); nmo_engine_set_key(g.f.engine,KEY_C,true); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(0u,g.f.engine->messages.sent.count); fixture_end(&g.f);
    }
}

TEST(engine_input, keyboard_mapper_truncated_records_and_wide_counts_fail_before_partial_load_or_delivery)
{
    const struct { size_t bytes; int32_t count; bool load; } cases[] = {{39,1,false},{79,2,false},{80,INT32_MAX,false},{79,2,true}};
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        keyboard_fixture_t g; mapper_begin(&g); uint8_t records[80]; mapper_record(records,KEY_X,-1,"First Bound"); mapper_record(records+40,KEY_X,-1,"Second Bound"); mapper_saved(&g,records,cases[i].load ? cases[i].bytes : sizeof(records),cases[i].load ? cases[i].count : 2); keyboard_ready(&g); if (cases[i].load) ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"First Bound")); else { ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,records,cases[i].bytes)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&cases[i].count,sizeof(cases[i].count))); }
        nmo_engine_set_key(g.f.engine,KEY_X,true); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(0u,g.f.engine->messages.sent.count); fixture_end(&g.f);
    }
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

static const char *keyboard_resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

TEST(engine_input, loaded_joystick_controllers_rebind_messages_and_copies_keep_slots_thresholds_and_output_state)
{
    joystick_fixture_t source; joystick_begin(&source); nmo_object_id_t owner_id = source.f.owner_id, receiver = fixture_create(&source.f,NMO_CID_GROUP,"Saved Joystick Receiver"), input = 0; ASSERT_TRUE(receiver != 0); source.f.owner_id = receiver; nmo_object_id_t receiver_script = fixture_add_script(&source.f,"Saved Joystick Receiver Script",&input); ASSERT_TRUE(receiver_script != 0); source.f.owner_id = owner_id; char text[32]; snprintf(text,sizeof(text),"%u",receiver); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.target_value,text)); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.number_value,"2")); fixture_commit_bare(&source.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source.f,owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET; const char *path = NMO_TEST_SCRATCH_FILE("engine_joystick_controller.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        joystick_fixture_t g = {0}; fixture_begin_document(&g.f); const char *types[] = {"Joy_Down","Joy_Button4","Host Only","Joy_Left","Joy_Button6","Joy_Right","Joy_Up","Joy_Button2","Joy_Button1","Joy_Button5","Joy_Button3"}; fixture_install_messages(&g.f,types,sizeof(types)/sizeof(types[0])); fixture_begin_edit(&g.f); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(g.f.engine,keyboard_resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(g.f.engine,"joystick.cmo",true,&file));
        g.block = nmo_engine_object_named(g.f.engine,"Joystick Controller",NMO_CID_BEHAVIOR); ASSERT_TRUE(g.block != 0); const eng_module_t *module = eng_module_of(g.f.engine,g.block,NULL); g.f.owner_id = eng_module_id(module,owner_id); g.f.script_id = eng_module_id(module,source.f.script_id); g.target_value = eng_module_id(module,source.target_value); g.number_value = eng_module_id(module,source.number_value); g.table = eng_module_id(module,source.table); g.sensitivity = eng_module_id(module,source.sensitivity); g.output = eng_module_id(module,source.output); nmo_object_id_t target = eng_module_id(module,receiver), target_script = eng_module_id(module,receiver_script); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_SCENE_DEFAULT)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,target,true);
        int32_t table[10], slot = -1; read_value(&g.f,g.table,table,sizeof(table)); read_value(&g.f,g.number_value,&slot,sizeof(slot)); ASSERT_EQ(2,slot); const char *names[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6"}; for (size_t i = 0; i < 10; i++) ASSERT_EQ(nmo_engine_message_type(g.f.engine,names[i]),table[i]); const float sensitivity[] = {.25f,.75f,-1}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.sensitivity,sensitivity,sizeof(sensitivity))); nmo_engine_joystick_state_t state = {.attached = true,.position = {-.5f,.8f,-.33f},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,2,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *initial[] = {"Joy_Down","Joy_Left"}; joystick_received(&g,target,initial,2);
        table[0] = table[4]; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,table,sizeof(table))); nmo_object_id_t ids[] = {g.f.owner_id,target,g.f.script_id,target_script,g.block,g.target_value,g.number_value,g.table,g.sensitivity,g.output}, copies[10] = {0}; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,10,false,copies)); for (size_t i = 0; i < 10; i++) ASSERT_TRUE(copies[i] != 0); for (size_t i = 0; i < 10; i++) nmo_engine_destroy_object(g.f.engine,ids[i]);
        g.f.owner_id = copies[0]; target = copies[1]; g.f.script_id = copies[2]; g.block = copies[4]; g.target_value = copies[5]; g.number_value = copies[6]; g.table = copies[7]; g.sensitivity = copies[8]; g.output = copies[9]; nmo_object_id_t mapped = 0; read_value(&g.f,g.target_value,&mapped,sizeof(mapped)); ASSERT_EQ(target,mapped); read_value(&g.f,g.number_value,&slot,sizeof(slot)); ASSERT_EQ(2,slot); float output[3], copied_sensitivity[3]; read_value(&g.f,g.output,output,sizeof(output)); read_value(&g.f,g.sensitivity,copied_sensitivity,sizeof(copied_sensitivity)); for (size_t i = 0; i < 3; i++) { ASSERT_FLOAT_EQ(state.position[i],output[i],0); ASSERT_FLOAT_EQ(sensitivity[i],copied_sensitivity[i],0); }
        block = (nmo_engine_block_t){g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,target,true); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); state = (nmo_engine_joystick_state_t){.attached = true,.position = {-.4f,.2f,.9f},.point_of_view = -1,.buttons = 32}; nmo_engine_set_joystick(g.f.engine,2,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *cached[] = {"Joy_Button6","Joy_Button1"}; joystick_received(&g,target,cached,2); read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(state.position[i],output[i],0);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){0},sizeof(int32_t))); state = (nmo_engine_joystick_state_t){.attached = true,.position = {0,-.9f,.4f},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *up[] = {"Joy_Up"}; joystick_received(&g,target,up,1); fixture_end(&g.f);
    }
    ASSERT_EQ(0u,((nmo_parameter_state_t *)fixture_state(&source.f,source.table))->buffer_data.count); fixture_end(&source.f); remove(path);
}

static void mapper_received(keyboard_fixture_t *g, nmo_object_id_t receiver, const char *const *names, size_t count)
{
    ASSERT_EQ(count,g->f.engine->messages.received.count);
    for (size_t i = 0; i < count; i++) { const eng_received_t *message = &g->f.engine->messages.received.data[i]; ASSERT_EQ(receiver,message->object); ASSERT_EQ(g->f.owner_id,message->sender); ASSERT_EQ(nmo_engine_message_type(g->f.engine,names[i]),message->type); }
}

TEST(engine_input, loaded_keyboard_mappers_rebind_names_while_copies_keep_cached_ids_counts_and_distinct_sender_targets)
{
    keyboard_fixture_t source; mapper_begin(&source); uint8_t records[125]; mapper_record(records,0x11,900,"Custom One"); mapper_record(records+40,0x11,901,"Custom Two"); mapper_record(records+80,KEY_X,902,"Inactive"); memset(records+120,0xa9,5); mapper_saved(&source,records,sizeof(records),2); nmo_object_id_t owner_id = source.f.owner_id, receiver = fixture_create(&source.f,NMO_CID_GROUP,"Saved Mapper Receiver"), input = 0; ASSERT_TRUE(receiver != 0); source.f.owner_id = receiver; nmo_object_id_t receiver_script = fixture_add_script(&source.f,"Saved Receiver Script",&input); ASSERT_TRUE(receiver_script != 0); source.f.owner_id = owner_id; char text[32]; snprintf(text,sizeof(text),"%u",receiver); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.target_value,text)); fixture_commit_bare(&source.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source.f,owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET; const char *path = NMO_TEST_SCRATCH_FILE("engine_keyboard_mapper.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        keyboard_fixture_t g = {0}; fixture_begin_document(&g.f); const char *types[] = {"Custom Two","Host Only","Custom One"}; fixture_install_messages(&g.f,types,3); fixture_begin_edit(&g.f); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(g.f.engine,keyboard_resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(g.f.engine,"mapper.cmo",true,&file));
        g.block = nmo_engine_object_named(g.f.engine,"Keyboard Mapper",NMO_CID_BEHAVIOR); ASSERT_TRUE(g.block != 0); const eng_module_t *module = eng_module_of(g.f.engine,g.block,NULL); g.f.owner_id = eng_module_id(module,owner_id); g.f.script_id = eng_module_id(module,source.f.script_id); g.target_value = eng_module_id(module,source.target_value); g.table = eng_module_id(module,source.table); g.count = eng_module_id(module,source.count); nmo_object_id_t target = eng_module_id(module,receiver), target_script = eng_module_id(module,receiver_script); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_SCENE_DEFAULT)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,target,true);
        uint8_t runtime[125]; read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_EQ(nmo_engine_message_type(g.f.engine,"Custom One"),(int32_t)nmo_read_u32_le(runtime+4)); ASSERT_EQ(nmo_engine_message_type(g.f.engine,"Custom Two"),(int32_t)nmo_read_u32_le(runtime+44)); ASSERT_TRUE(memcmp(records+80,runtime+80,45) == 0); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"Inactive")); nmo_engine_set_key(g.f.engine,0x11,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *names[] = {"Custom One","Custom Two"}; mapper_received(&g,target,names,2);
        nmo_write_u32_le(runtime+4,nmo_read_u32_le(runtime+44)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,runtime,sizeof(runtime))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){1},sizeof(int32_t))); nmo_object_id_t ids[] = {g.f.owner_id,target,g.f.script_id,target_script,g.block,g.target_value,g.table,g.count}, copies[8] = {0}; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,8,false,copies)); for (size_t i = 0; i < 8; i++) ASSERT_TRUE(copies[i] != 0); for (size_t i = 0; i < 8; i++) nmo_engine_destroy_object(g.f.engine,ids[i]);
        g.f.owner_id = copies[0]; target = copies[1]; g.f.script_id = copies[2]; g.block = copies[4]; g.target_value = copies[5]; g.table = copies[6]; g.count = copies[7]; nmo_object_id_t mapped_target = 0; read_value(&g.f,g.target_value,&mapped_target,sizeof(mapped_target)); ASSERT_EQ(target,mapped_target); int32_t count = 0; read_value(&g.f,g.count,&count,sizeof(count)); ASSERT_EQ(1,count); block = (nmo_engine_block_t){g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,target,true); int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(g.f.engine,g.block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); mapper_received(&g,target,&names[1],1);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){2},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *duplicates[] = {"Custom Two","Custom Two"}; mapper_received(&g,target,duplicates,2); read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_TRUE(memcmp(records+80,runtime+80,45) == 0); fixture_end(&g.f);
    }
    ASSERT_TRUE(memcmp(records,((nmo_parameter_state_t *)fixture_state(&source.f,source.table))->buffer_data.data,sizeof(records)) == 0); fixture_end(&source.f); remove(path);
}

TEST(engine_input, loaded_keyboard_controllers_rebuild_message_ids_and_copies_keep_live_tables_after_source_deletion)
{
    keyboard_fixture_t source; keyboard_begin(&source); char text[32]; snprintf(text,sizeof(text),"%u",source.f.owner_id); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.target_value,text)); const int32_t obsolete[10] = {100,101,102,103,104,105,106,107,108,109}; nmo_parameter_state_t *saved = fixture_state(&source.f,source.table); nmo_array_dispose(&saved->buffer_data); ASSERT_EQ(NMO_OK,nmo_array_alloc(&saved->buffer_data,1,sizeof(obsolete),NULL)); memcpy(saved->buffer_data.data,obsolete,sizeof(obsolete)); saved->mode = CKPARAM_MODE_BUFFER; saved->has_state = true; fixture_commit_bare(&source.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET; const char *path = NMO_TEST_SCRATCH_FILE("engine_keyboard_controller.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        keyboard_fixture_t g = {0}; fixture_begin_document(&g.f); const char *types[] = {"Joy_Button6","Host Only","Joy_Right","Joy_Button1","Joy_Up","Joy_Button4","Joy_Left","Joy_Button3","Joy_Down","Joy_Button5","Joy_Button2"}; fixture_install_messages(&g.f,types,sizeof(types)/sizeof(types[0])); fixture_begin_edit(&g.f); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(g.f.engine,keyboard_resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(g.f.engine,"keyboard.cmo",true,&file));
        g.block = nmo_engine_object_named(g.f.engine,"Keyboard Controller",NMO_CID_BEHAVIOR); ASSERT_TRUE(g.block != 0); const eng_module_t *module = eng_module_of(g.f.engine,g.block,NULL); g.f.owner_id = eng_module_id(module,source.f.owner_id); g.f.script_id = eng_module_id(module,source.f.script_id); g.target_value = eng_module_id(module,source.target_value); g.table = eng_module_id(module,source.table); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_SCENE_DEFAULT)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,g.f.owner_id,true);
        nmo_object_id_t target = 0; read_value(&g.f,g.target_value,&target,sizeof(target)); ASSERT_EQ(g.f.owner_id,target); int32_t table[10]; read_value(&g.f,g.table,table,sizeof(table)); const char *names[] = {"Joy_Left","Joy_Right","Joy_Up","Joy_Down","Joy_Button1","Joy_Button2","Joy_Button3","Joy_Button4","Joy_Button5","Joy_Button6"}; for (size_t i = 0; i < 10; i++) ASSERT_EQ(nmo_engine_message_type(g.f.engine,names[i]),table[i]); nmo_engine_set_key(g.f.engine,0x48,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,&names[2],1);
        table[2] = table[4]; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,table,sizeof(table))); nmo_object_id_t ids[] = {g.f.owner_id,g.f.script_id,g.block,g.target_value,g.table}, copies[5] = {0}; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,5,false,copies)); for (size_t i = 0; i < 5; i++) ASSERT_TRUE(copies[i] != 0); for (size_t i = 0; i < 5; i++) nmo_engine_destroy_object(g.f.engine,ids[i]);
        g.f.owner_id = copies[0]; g.f.script_id = copies[1]; g.block = copies[2]; g.target_value = copies[3]; g.table = copies[4]; block = (nmo_engine_block_t){g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,g.f.owner_id,true); read_value(&g.f,g.target_value,&target,sizeof(target)); ASSERT_EQ(g.f.owner_id,target); int32_t copied[10]; read_value(&g.f,g.table,copied,sizeof(copied)); ASSERT_EQ(table[4],copied[2]); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,&names[4],1);
        nmo_engine_set_key(g.f.engine,0x48,false); nmo_engine_set_key(g.f.engine,0xc7,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); keyboard_received(&g,g.f.owner_id,&names[5],1); ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
    ASSERT_EQ(sizeof(obsolete),saved->buffer_data.count); ASSERT_TRUE(memcmp(obsolete,saved->buffer_data.data,sizeof(obsolete)) == 0); fixture_end(&source.f); remove(path);
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
REGISTER_TEST(engine_input, joystick_states_take_effect_at_frame_boundaries_and_disconnects_clear_all_fields);
REGISTER_TEST(engine_input, four_joystick_slots_are_independent_and_invalid_indices_return_neutral_states);
REGISTER_TEST(engine_input, joystick_snapshots_remain_private_to_each_engine_and_survive_script_reactivation);
REGISTER_TEST(engine_input, joystick_controller_repeats_six_buttons_then_directions_and_publishes_the_raw_position);
REGISTER_TEST(engine_input, joystick_controller_uses_strict_per_axis_thresholds_and_does_not_generate_z_directions);
REGISTER_TEST(engine_input, joystick_controller_switches_live_slots_and_targets_and_uses_its_owner_as_sender);
REGISTER_TEST(engine_input, joystick_controller_preserves_custom_negative_and_nonfinite_sensitivity_comparisons);
REGISTER_TEST(engine_input, joystick_controller_off_precedes_invalid_targets_missing_devices_and_bad_tables);
REGISTER_TEST(engine_input, joystick_controller_reports_missing_devices_and_invalid_targets_without_changing_output);
REGISTER_TEST(engine_input, joystick_controller_truncated_tables_do_not_send_partial_messages_or_replace_output);
REGISTER_TEST(engine_input, joystick_controller_disconnects_stop_sending_and_reconnection_requires_explicit_restart);
REGISTER_TEST(engine_input, loaded_joystick_controllers_rebind_messages_and_copies_keep_slots_thresholds_and_output_state);
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
REGISTER_TEST(engine_input, loaded_keyboard_controllers_rebuild_message_ids_and_copies_keep_live_tables_after_source_deletion);
REGISTER_TEST(engine_input, keyboard_mapper_loads_named_bindings_and_repeats_duplicate_keys_in_record_order);
REGISTER_TEST(engine_input, keyboard_mapper_uses_the_behavior_owner_as_sender_when_targeting_a_group);
REGISTER_TEST(engine_input, keyboard_mapper_nonpositive_counts_are_idle_and_only_the_active_prefix_is_loaded);
REGISTER_TEST(engine_input, keyboard_mapper_live_keys_ids_and_counts_change_without_rebuilding_names_on_edit);
REGISTER_TEST(engine_input, keyboard_mapper_out_of_range_keys_and_empty_names_do_not_block_valid_records);
REGISTER_TEST(engine_input, keyboard_mapper_off_precedes_invalid_state_and_preserves_on_until_restart);
REGISTER_TEST(engine_input, keyboard_mapper_invalid_targets_consume_on_before_reporting_owner_errors);
REGISTER_TEST(engine_input, keyboard_mapper_truncated_records_and_wide_counts_fail_before_partial_load_or_delivery);
REGISTER_TEST(engine_input, loaded_keyboard_mappers_rebind_names_while_copies_keep_cached_ids_counts_and_distinct_sender_targets);
TEST_MAIN_END()
