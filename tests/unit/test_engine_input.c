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
#define GUID_TT_JOYSTICK_WAITER NMO_GUID(0x594a136e,0x58135ef9)
#define GUID_TT_TEST_FOR_JOYSTICK NMO_GUID(0x229865eb,0x577619e4)
#define GUID_TT_JOYSTICK_VECTOR_MAPPER NMO_GUID(0x04f80efd,0x76b47b33)
#define GUID_JOYSTICK_WAITER NMO_GUID(0x012d010b,0x013d010b)
#define GUID_JOYSTICK_MAPPER NMO_GUID(0x03ed1bea,0x25692ecc)
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

typedef struct tt_joystick_test_fixture {
    engine_fixture_t f;
    nmo_object_id_t block, number_value, yes, no;
    uint64_t calls, yes_hits, no_hits;
} tt_joystick_test_fixture_t;

static void tt_joystick_test_begin(tt_joystick_test_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin(&g->f); g->block = fixture_block(&g->f,GUID_TT_TEST_FOR_JOYSTICK,"TT_TestForJoystick"); g->yes = fixture_block(&g->f,GUID_NOP,"Joystick Present"); g->no = fixture_block(&g->f,GUID_NOP,"Joystick Absent"); ASSERT_TRUE(g->block != 0 && g->yes != 0 && g->no != 0); ASSERT_EQ(0u,fixture_target(&g->f,g->block)); nmo_object_id_t number = BLOCK_PIN(&g->f,g->block,0); fixture_input_int(&g->f,number,0); g->number_value = nmo_parameterin_source_id(fixture_state(&g->f,number)); fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->yes,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,1),BLOCK_IN(&g->f,g->no,0),0);
}

static void tt_joystick_test_ready(tt_joystick_test_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit_bare(&g->f,&options); ASSERT_FALSE(nmo_engine_has_block(g->f.engine,GUID_TT_TEST_FOR_JOYSTICK)); ASSERT_EQ(NMO_OK,nmo_bb_terratools_register(g->f.engine)); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_TT_TEST_FOR_JOYSTICK)); ASSERT_EQ(NMO_OK,nmo_bb_virtools_register(g->f.engine)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void tt_joystick_test_step(tt_joystick_test_fixture_t *g, bool attached)
{
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0)); g->calls++; if (attached) g->yes_hits++; else g->no_hits++; ASSERT_EQ(g->calls,runs(&g->f,g->block)); ASSERT_EQ(g->yes_hits,runs(&g->f,g->yes)); ASSERT_EQ(g->no_hits,runs(&g->f,g->no)); nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; ASSERT_FALSE(nmo_engine_block_is_input_active(&block,0)); ASSERT_FALSE(nmo_engine_behavior_is_active(g->f.engine,g->block));
}

static void tt_joystick_test_fire(tt_joystick_test_fixture_t *g, int32_t slot, bool attached)
{
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g->f.engine,g->number_value,&slot,sizeof(slot))); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); tt_joystick_test_step(g,attached);
}

TEST(engine_input, tt_test_for_joystick_defaults_to_slot_zero_and_requires_reactivation_after_each_query)
{
    tt_joystick_test_fixture_t g; tt_joystick_test_begin(&g); tt_joystick_test_ready(&g); tt_joystick_test_step(&g,false); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,1,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.no)); ASSERT_EQ(0u,runs(&g.f,g.yes));
    tt_joystick_test_fire(&g,0,false); tt_joystick_test_fire(&g,1,true); state.attached = false; state.buttons = UINT32_MAX; state.position[0] = 1; nmo_engine_set_joystick(g.f.engine,1,&state); tt_joystick_test_fire(&g,1,false); fixture_end(&g.f);
}

TEST(engine_input, tt_test_for_joystick_uses_the_live_number_and_distinguishes_all_four_device_slots)
{
    tt_joystick_test_fixture_t g; tt_joystick_test_begin(&g); tt_joystick_test_ready(&g);
    for (int32_t selected = 0; selected < 4; selected++) {
        for (int32_t slot = 0; slot < 4; slot++) nmo_engine_set_joystick(g.f.engine,slot,NULL);
        nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,selected,&state);
        for (int32_t slot = 0; slot < 4; slot++) tt_joystick_test_fire(&g,slot,slot == selected);
    }
    ASSERT_EQ(4u,g.yes_hits); ASSERT_EQ(12u,g.no_hits); fixture_end(&g.f);
}

TEST(engine_input, tt_test_for_joystick_invalid_numbers_take_no_without_failing_even_when_all_devices_are_attached)
{
    tt_joystick_test_fixture_t g; tt_joystick_test_begin(&g); tt_joystick_test_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; for (int32_t slot = 0; slot < 4; slot++) nmo_engine_set_joystick(g.f.engine,slot,&state); const int32_t invalid[] = {-1,4,INT32_MIN,INT32_MAX}; for (size_t i = 0; i < 4; i++) tt_joystick_test_fire(&g,invalid[i],false); for (int32_t slot = 0; slot < 4; slot++) tt_joystick_test_fire(&g,slot,true); fixture_end(&g.f);
}

typedef struct joystick_query_update {
    nmo_object_id_t block;
    int32_t slot;
    bool enabled, disconnect, before, after;
} joystick_query_update_t;

/* A host update between two query nodes must only affect the next frame. */
static int joystick_query_queue_update(nmo_engine_block_t *block)
{
    joystick_query_update_t *update = nmo_engine_block_user_data(block);
    if (nmo_engine_block_id(block) == update->block && update->enabled) {
        nmo_engine_t *engine = nmo_engine_block_engine(block); update->before = nmo_engine_joystick_state(engine,update->slot,NULL); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; nmo_engine_set_joystick(engine,update->slot,update->disconnect ? NULL : &state); update->after = nmo_engine_joystick_state(engine,update->slot,NULL);
    }
    nmo_engine_block_activate_input(block,0,false); nmo_engine_block_activate_output(block,0,true); return NMO_ENGINE_BR_OK;
}

TEST(engine_input, tt_test_for_joystick_observes_one_snapshot_across_host_updates_between_query_nodes)
{
    tt_joystick_test_fixture_t g; tt_joystick_test_begin(&g); nmo_object_id_t update = fixture_block(&g.f,GUID_NOP,"Queue Joystick Update"), second = fixture_block(&g.f,GUID_TT_TEST_FOR_JOYSTICK,"Query After Update"), yes = fixture_block(&g.f,GUID_NOP,"Second Query Present"), no = fixture_block(&g.f,GUID_NOP,"Second Query Absent"); ASSERT_TRUE(update != 0 && second != 0 && yes != 0 && no != 0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,second,0),2); fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,0),BLOCK_IN(&g.f,update,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,1),BLOCK_IN(&g.f,update,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,update,0),BLOCK_IN(&g.f,second,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,second,0),BLOCK_IN(&g.f,yes,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,second,1),BLOCK_IN(&g.f,no,0),0);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); joystick_query_update_t change = {.block = update,.slot = 2,.enabled = true}; nmo_engine_block_desc_t desc = {.guid = GUID_NOP,.name = "Host Input Update",.function = joystick_query_queue_update,.user_data = &change}; ASSERT_EQ(NMO_OK,nmo_engine_register_block(g.f.engine,&desc)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){2},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL));
    tt_joystick_test_step(&g,false); ASSERT_FALSE(change.before); ASSERT_FALSE(change.after); ASSERT_FALSE(nmo_engine_joystick_state(g.f.engine,2,NULL)); ASSERT_EQ(1u,runs(&g.f,second)); ASSERT_EQ(0u,runs(&g.f,yes)); ASSERT_EQ(1u,runs(&g.f,no));
    change.disconnect = true; ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); tt_joystick_test_step(&g,true); ASSERT_TRUE(change.before); ASSERT_TRUE(change.after); ASSERT_TRUE(nmo_engine_joystick_state(g.f.engine,2,NULL)); ASSERT_EQ(2u,runs(&g.f,second)); ASSERT_EQ(1u,runs(&g.f,yes)); ASSERT_EQ(1u,runs(&g.f,no));
    change.enabled = false; ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); tt_joystick_test_step(&g,false); ASSERT_FALSE(nmo_engine_joystick_state(g.f.engine,2,NULL)); ASSERT_EQ(3u,runs(&g.f,second)); ASSERT_EQ(1u,runs(&g.f,yes)); ASSERT_EQ(2u,runs(&g.f,no)); fixture_end(&g.f);
}

typedef struct tt_vector_mapper_fixture {
    engine_fixture_t f;
    nmo_object_id_t block, done, input_value, mappings[4], output;
    uint64_t calls;
} tt_vector_mapper_fixture_t;

static void tt_vector_mapper_begin(tt_vector_mapper_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin(&g->f); g->block = fixture_block(&g->f,GUID_TT_JOYSTICK_VECTOR_MAPPER,"TT_JoystickVectorMapper"); g->done = fixture_block(&g->f,GUID_NOP,"Vector Mapped"); ASSERT_TRUE(g->block != 0 && g->done != 0); ASSERT_EQ(0u,fixture_target(&g->f,g->block)); eng_vector_t zero = {0}; nmo_object_id_t input = BLOCK_PIN(&g->f,g->block,0); fixture_input_value(&g->f,input,&zero,sizeof(zero)); g->input_value = nmo_parameterin_source_id(fixture_state(&g->f,input));
    for (size_t i = 0; i < 4; i++) { input = BLOCK_PIN(&g->f,g->block,i+1); fixture_input_int(&g->f,input,0); g->mappings[i] = nmo_parameterin_source_id(fixture_state(&g->f,input)); } g->output = BLOCK_POUT(&g->f,g->block,0); fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->done,0),0);
}

static void tt_vector_mapper_ready(tt_vector_mapper_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit_bare(&g->f,&options); ASSERT_FALSE(nmo_engine_has_block(g->f.engine,GUID_TT_JOYSTICK_VECTOR_MAPPER)); ASSERT_EQ(NMO_OK,nmo_bb_terratools_register(g->f.engine)); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_TT_JOYSTICK_VECTOR_MAPPER)); ASSERT_EQ(NMO_OK,nmo_bb_virtools_register(g->f.engine)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void tt_vector_mapper_value(tt_vector_mapper_fixture_t *g, const float expected[3])
{
    float output[3]; read_value(&g->f,g->output,output,sizeof(output)); ASSERT_TRUE(memcmp(expected,output,sizeof(output)) == 0);
}

static void tt_vector_mapper_step(tt_vector_mapper_fixture_t *g, const float expected[3])
{
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0)); g->calls++; tt_vector_mapper_value(g,expected); ASSERT_EQ(g->calls,runs(&g->f,g->block)); ASSERT_EQ(g->calls,runs(&g->f,g->done)); nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; ASSERT_FALSE(nmo_engine_block_is_input_active(&block,0)); ASSERT_FALSE(nmo_engine_behavior_is_active(g->f.engine,g->block));
}

static void tt_vector_mapper_fire(tt_vector_mapper_fixture_t *g, const float input[3], const int32_t *mappings, const float expected[3])
{
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g->f.engine,g->input_value,input,3*sizeof(float))); if (mappings != NULL) for (size_t i = 0; i < 4; i++) ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g->f.engine,g->mappings[i],mappings+i,sizeof(mappings[i]))); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); tt_vector_mapper_step(g,expected);
}

TEST(engine_input, tt_joystick_vector_mapper_defaults_follow_native_overwrite_order_without_attached_devices)
{
    tt_vector_mapper_fixture_t g; tt_vector_mapper_begin(&g); tt_vector_mapper_ready(&g); for (size_t i = 0; i < NMO_ENGINE_JOYSTICK_COUNT; i++) ASSERT_FALSE(nmo_engine_joystick_state(g.f.engine,(int32_t)i,NULL));
    tt_vector_mapper_fire(&g,(float[3]){.75f,-2,4},NULL,(float[3]){-.75f,-.75f,0}); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.done)); tt_vector_mapper_fire(&g,(float[3]){-.75f,2,-4},NULL,(float[3]){0,0,0}); fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_vector_mapper_maps_each_signed_source_axis_to_each_output_channel)
{
    tt_vector_mapper_fixture_t g; tt_vector_mapper_begin(&g); tt_vector_mapper_ready(&g); const float inputs[][3] = {{2,3,5},{-2,-3,-5}}, magnitudes[][6] = {{2,0,3,0,5,0},{0,2,0,3,0,5}};
    for (size_t sample = 0; sample < 2; sample++) for (int32_t selector = 0; selector < 6; selector++) for (size_t channel = 0; channel < 4; channel++) {
        int32_t mappings[] = {-1,-1,-1,-1}; mappings[channel] = selector; float expected[3] = {0}; float value = magnitudes[sample][selector]; if (value != 0) expected[channel/2] = channel%2 ? -value : value; tt_vector_mapper_fire(&g,inputs[sample],mappings,expected);
    }
    ASSERT_EQ(48u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_vector_mapper_later_active_assignments_replace_rather_than_accumulate)
{
    tt_vector_mapper_fixture_t g; tt_vector_mapper_begin(&g); tt_vector_mapper_ready(&g); const float input[] = {2,3,-5};
    tt_vector_mapper_fire(&g,input,(int32_t[4]){0,2,5,3},(float[3]){-3,5,0});
    tt_vector_mapper_fire(&g,input,(int32_t[4]){0,3,2,5},(float[3]){2,-5,0});
    tt_vector_mapper_fire(&g,input,(int32_t[4]){0,0,5,5},(float[3]){-2,-5,0});
    tt_vector_mapper_fire(&g,input,(int32_t[4]){5,-1,-1,5},(float[3]){5,-5,0}); fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_vector_mapper_ignores_invalid_selectors_and_clears_all_previous_output_components)
{
    tt_vector_mapper_fixture_t g; tt_vector_mapper_begin(&g); tt_vector_mapper_ready(&g); const int32_t invalid[] = {-1,6,INT32_MIN,INT32_MAX}; const float input[] = {2,-3,5};
    for (size_t i = 0; i < 4; i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,(float[3]){17,18,19},3*sizeof(float))); int32_t mappings[] = {invalid[i],invalid[i],invalid[i],invalid[i]}; tt_vector_mapper_fire(&g,input,mappings,(float[3]){0,0,0}); mappings[1] = 1; mappings[2] = 4; tt_vector_mapper_fire(&g,input,mappings,(float[3]){0,5,0}); mappings[0] = 3; mappings[3] = 3; tt_vector_mapper_fire(&g,input,mappings,(float[3]){3,-3,0});
    }
    fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_vector_mapper_nonnegative_zero_can_overwrite_an_active_nonzero_mapping)
{
    tt_vector_mapper_fixture_t g; tt_vector_mapper_begin(&g); tt_vector_mapper_ready(&g);
    tt_vector_mapper_fire(&g,(float[3]){2,0,3},(int32_t[4]){0,2,4,2},(float[3]){-0.0f,-0.0f,0});
    tt_vector_mapper_fire(&g,(float[3]){2,-0.0f,3},(int32_t[4]){0,2,4,2},(float[3]){0,0,0});
    tt_vector_mapper_fire(&g,(float[3]){2,-0.0f,3},(int32_t[4]){0,3,4,3},(float[3]){2,3,0});
    tt_vector_mapper_fire(&g,(float[3]){-0.0f,2,3},(int32_t[4]){0,-1,0,-1},(float[3]){-0.0f,-0.0f,0}); fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_vector_mapper_nan_sources_are_inactive_and_infinite_magnitudes_are_preserved)
{
    tt_vector_mapper_fixture_t g; tt_vector_mapper_begin(&g); tt_vector_mapper_ready(&g);
    tt_vector_mapper_fire(&g,(float[3]){NAN,2,-3},(int32_t[4]){0,2,1,5},(float[3]){-2,-3,0});
    tt_vector_mapper_fire(&g,(float[3]){NAN,NAN,NAN},(int32_t[4]){0,3,4,5},(float[3]){0,0,0});
    tt_vector_mapper_fire(&g,(float[3]){INFINITY,-INFINITY,4},(int32_t[4]){0,3,3,0},(float[3]){-INFINITY,-INFINITY,0});
    tt_vector_mapper_fire(&g,(float[3]){INFINITY,-INFINITY,4},(int32_t[4]){3,-1,0,-1},(float[3]){INFINITY,INFINITY,0}); fixture_end(&g.f);
}

typedef struct joystick_waiter_fixture {
    engine_fixture_t f;
    nmo_object_id_t block, number_value, settings[4], values[5], observers[43];
    nmo_guid_t guid;
    uint64_t hits[43];
    size_t output_count;
} joystick_waiter_fixture_t;

static void joystick_waiter_begin_variant(joystick_waiter_fixture_t *g, bool tt)
{
    memset(g,0,sizeof(*g)); g->guid = tt ? GUID_TT_JOYSTICK_WAITER : GUID_JOYSTICK_WAITER; fixture_begin(&g->f); g->block = fixture_block(&g->f,g->guid,tt ? "TT Joystick Waiter" : "Joystick Waiter"); ASSERT_TRUE(g->block != 0); ASSERT_EQ(0u,fixture_target(&g->f,g->block)); nmo_object_id_t number = BLOCK_PIN(&g->f,g->block,0); fixture_input_int(&g->f,number,0); g->number_value = nmo_parameterin_source_id(fixture_state(&g->f,number)); const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    for (size_t i = 0; i < 4; i++) { g->settings[i] = nmo_behavior_ref_array_get_id(&state->local_parameters,i); g->values[i] = BLOCK_POUT(&g->f,g->block,i); ASSERT_TRUE(g->settings[i] != 0 && g->values[i] != 0); } if (tt) { g->values[4] = BLOCK_POUT(&g->f,g->block,4); ASSERT_TRUE(g->values[4] != 0); } fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
}

static void joystick_waiter_begin(joystick_waiter_fixture_t *g) { joystick_waiter_begin_variant(g,false); }
static void tt_joystick_waiter_begin(joystick_waiter_fixture_t *g) { joystick_waiter_begin_variant(g,true); }

static void joystick_waiter_output(joystick_waiter_fixture_t *g, const char *name)
{
    ASSERT_TRUE(g->output_count < 43); nmo_object_id_t output = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_add_io(g->f.tx,g->block,NMO_SCRIPT_EDIT_IO_OUTPUT,name,&output)); nmo_object_id_t observer = fixture_block(&g->f,GUID_NOP,name); ASSERT_TRUE(observer != 0); fixture_link(&g->f,output,BLOCK_IN(&g->f,observer,0),0); g->observers[g->output_count++] = observer;
}

/* Author the same compact output layout as the native settings editor. */
static void joystick_waiter_layout(joystick_waiter_fixture_t *g, uint32_t axes, uint32_t buttons)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    while (state->outputs.count) ASSERT_EQ(NMO_OK,nmo_script_edit_remove_io(g->f.tx,nmo_behavior_ref_array_get_id(&state->outputs,state->outputs.count-1),true));
    joystick_waiter_output(g,"Joystick Idle"); const char *names[] = {"Axis X Moved","Axis Y Moved","Axis Z Moved","Rotation X Moved","Rotation Y Moved","Rotation Z Moved","Slider 1 Moved","Slider 2 Moved","Point Of View Moved","Point Of View Centered"};
    for (uint32_t i = 0; i < 8; i++) if (axes & (1u << i)) joystick_waiter_output(g,names[i]);
    if (axes & 256u) { joystick_waiter_output(g,names[8]); joystick_waiter_output(g,names[9]); }
    for (uint32_t i = 0; i < 32; i++) if (buttons & (1u << i)) { char name[32]; snprintf(name,sizeof(name),"Button %u Pressed",i+1); joystick_waiter_output(g,name); }
}

static void joystick_waiter_configure(joystick_waiter_fixture_t *g, uint32_t axes, uint32_t buttons, uint32_t relative)
{
    const uint32_t flags[] = {axes,buttons,relative}; for (size_t i = 0; i < 3; i++) ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(g->f.tx,g->settings[i+1],(const uint8_t *)&flags[i],sizeof(flags[i]))); joystick_waiter_layout(g,axes,buttons);
}

static void joystick_waiter_ready(joystick_waiter_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; if (nmo_guid_equals(g->guid,GUID_TT_JOYSTICK_WAITER)) { fixture_commit_bare(&g->f,&options); ASSERT_FALSE(nmo_engine_has_block(g->f.engine,g->guid)); ASSERT_EQ(NMO_OK,nmo_bb_terratools_register(g->f.engine)); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,g->guid)); ASSERT_EQ(NMO_OK,nmo_bb_virtools_register(g->f.engine)); } else fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,g->guid)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; ASSERT_EQ(g->output_count,(size_t)nmo_engine_block_output_count(&block));
}

static void joystick_waiter_values(joystick_waiter_fixture_t *g, const nmo_engine_joystick_state_t *state)
{
    float position[3], rotation[3], sliders[2], pov; read_value(&g->f,g->values[0],position,sizeof(position)); read_value(&g->f,g->values[1],rotation,sizeof(rotation)); read_value(&g->f,g->values[2],sliders,sizeof(sliders)); read_value(&g->f,g->values[3],&pov,sizeof(pov));
    ASSERT_TRUE(memcmp(state->position,position,sizeof(position)) == 0); ASSERT_TRUE(memcmp(state->rotation,rotation,sizeof(rotation)) == 0); ASSERT_TRUE(memcmp(state->sliders,sliders,sizeof(sliders)) == 0); ASSERT_TRUE(memcmp(&state->point_of_view,&pov,sizeof(pov)) == 0);
}

static void joystick_waiter_step(joystick_waiter_fixture_t *g, uint64_t outputs, const nmo_engine_joystick_state_t *state)
{
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
    for (size_t i = 0; i < g->output_count; i++) { if (outputs & (UINT64_C(1) << i)) g->hits[i]++; ASSERT_EQ(g->hits[i],runs(&g->f,g->observers[i])); }
    joystick_waiter_values(g,state); nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; bool consumes_on = nmo_guid_equals(g->guid,GUID_TT_JOYSTICK_WAITER) && nmo_engine_block_version(&block) >= 0x20000u; ASSERT_EQ(!consumes_on,nmo_engine_block_is_input_active(&block,0)); ASSERT_TRUE(nmo_engine_behavior_is_active(g->f.engine,g->block));
}

TEST(engine_input, joystick_waiter_defaults_repeat_selected_axes_buttons_and_idle_while_publishing_all_raw_values)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_layout(&g,3,1); joystick_waiter_ready(&g); const uint32_t defaults[] = {3,1,448}; for (size_t i = 0; i < 3; i++) { uint32_t value = 0; read_value(&g.f,g.settings[i+1],&value,sizeof(value)); ASSERT_EQ(defaults[i],value); } float sensitivity[3]; read_value(&g.f,g.settings[0],sensitivity,sizeof(sensitivity)); ASSERT_FLOAT_EQ(.5f,sensitivity[0],0); ASSERT_FLOAT_EQ(.1f,sensitivity[1],0); ASSERT_FLOAT_EQ(.5f,sensitivity[2],0);
    nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,-.9f,2},.rotation = {3,4,5},.sliders = {6,7},.point_of_view = .25f,.buttons = UINT32_MAX}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,14,&state); joystick_waiter_step(&g,14,&state);
    state.position[0] = .5f; state.position[1] = -.5f; state.buttons = 0xfffffffeu; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); joystick_waiter_step(&g,1,&state); fixture_end(&g.f);
}

TEST(engine_input, joystick_waiter_compacts_all_axis_pov_and_32_button_outputs_in_native_order)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,511,UINT32_MAX,0); joystick_waiter_ready(&g); ASSERT_EQ(43u,g.output_count); nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,-.75f,.75f},.rotation = {-.75f,.75f,-.75f},.sliders = {.75f,-.75f},.point_of_view = .25f,.buttons = UINT32_MAX}; nmo_engine_set_joystick(g.f.engine,0,&state); const uint64_t all = (UINT64_C(1) << 43)-1; joystick_waiter_step(&g,all & ~UINT64_C(1) & ~(UINT64_C(1) << 10),&state);
    for (uint32_t i = 0; i < 32; i++) { state.buttons = 1u << i; memset(state.position,0,sizeof(state.position)); memset(state.rotation,0,sizeof(state.rotation)); memset(state.sliders,0,sizeof(state.sliders)); state.point_of_view = -1; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,(UINT64_C(1) << 10) | (UINT64_C(1) << (11+i)),&state); } fixture_end(&g.f);
}

TEST(engine_input, joystick_waiter_relative_axes_compare_each_sample_to_the_previous_output_values)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,255,0,255); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.settings[0],"0.75,0.125,99")); joystick_waiter_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.position = {.25f,-.25f,.25f},.rotation = {-.25f,.25f,-.25f},.sliders = {.25f,-.25f},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,510,&state); joystick_waiter_step(&g,1,&state);
    for (size_t i = 0; i < 3; i++) { state.position[i] += .125f; state.rotation[i] += .125f; } for (size_t i = 0; i < 2; i++) state.sliders[i] += .125f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state);
    for (size_t i = 0; i < 3; i++) { state.position[i] -= .25f; state.rotation[i] -= .25f; } for (size_t i = 0; i < 2; i++) state.sliders[i] -= .25f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,510,&state); joystick_waiter_step(&g,1,&state); fixture_end(&g.f);
}

static void joystick_waiter_legacy_layout(joystick_waiter_fixture_t *g, uint32_t version)
{ nmo_behavior_state_t *state = fixture_state(&g->f,g->block); state->block_version = version;
    while (state->outputs.count) ASSERT_EQ(NMO_OK,nmo_script_edit_remove_io(g->f.tx,nmo_behavior_ref_array_get_id(&state->outputs,state->outputs.count-1),true));
    const char *names[] = {"Up","Down","Left","Right","Button 1","Button 2","Button 3","Button 4","Button 5","Button 6"};
    for (size_t i = 0; i < 10; i++) joystick_waiter_output(g,names[i]);
}

static void joystick_waiter_legacy_begin(joystick_waiter_fixture_t *g, uint32_t version)
{
    joystick_waiter_begin(g); joystick_waiter_legacy_layout(g,version);
}

static void tt_joystick_waiter_difference(joystick_waiter_fixture_t *g, float expected)
{
    float difference = 0; read_value(&g->f,g->values[4],&difference,sizeof(difference)); if (isnan(expected)) ASSERT_TRUE(isnan(difference)); else ASSERT_TRUE(difference == expected);
}

TEST(engine_input, tt_joystick_waiter_defaults_repeat_axes_and_buttons_consume_on_and_publish_raw_samples)
{
    joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); joystick_waiter_layout(&g,3,1); joystick_waiter_ready(&g); float sliders[2]; read_value(&g.f,g.values[2],sliders,sizeof(sliders)); ASSERT_FLOAT_EQ(-1,sliders[0],0); ASSERT_FLOAT_EQ(-1,sliders[1],0); tt_joystick_waiter_difference(&g,0);
    nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,-.75f,2},.rotation = {3,4,5},.sliders = {.25f,-.25f},.point_of_view = .5f,.buttons = 1}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,14,&state); joystick_waiter_step(&g,14,&state); tt_joystick_waiter_difference(&g,0); state.position[0] = .5f; state.position[1] = -.5f; state.buttons = 0x80000000u; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_waiter_rebases_sliders_on_on_and_uses_z_sensitivity_for_relative_slider_events)
{
    joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); joystick_waiter_configure(&g,192,0,192); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.settings[0],"0.9,0.01,0.5")); joystick_waiter_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.sliders = {.25f,-.25f},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); tt_joystick_waiter_difference(&g,0);
    state.sliders[0] = .75f; state.sliders[1] = .25f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); tt_joystick_waiter_difference(&g,.5f); state.sliders[0] = 0; state.sliders[1] = -.5f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,6,&state); tt_joystick_waiter_difference(&g,-.75f); joystick_waiter_step(&g,1,&state); tt_joystick_waiter_difference(&g,0);
    state.sliders[0] = .125f; state.sliders[1] = -.375f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); tt_joystick_waiter_difference(&g,.125f); fixture_end(&g.f);
}

TEST(engine_input, legacy_tt_joystick_waiters_keep_fixed_outputs_and_on_and_leave_slider_difference_untouched)
{
    const uint32_t versions[] = {0,0x10000,0x1ffff};
    for (size_t i = 0; i < 3; i++) {
        joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); joystick_waiter_legacy_layout(&g,versions[i]); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.settings[0],"0.25,0.75,99")); joystick_waiter_ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[4],&(float){17},sizeof(float))); nmo_engine_joystick_state_t state = {.attached = true,.position = {.3f,-.8f,2},.rotation = {3,4,5},.sliders = {.25f,-.25f},.point_of_view = .5f,.buttons = UINT32_MAX}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,0x3f9,&state); joystick_waiter_step(&g,0x3f9,&state); tt_joystick_waiter_difference(&g,17);
        nmo_engine_set_joystick(g.f.engine,0,NULL); state = (nmo_engine_joystick_state_t){.point_of_view = -1}; joystick_waiter_step(&g,0,&state); tt_joystick_waiter_difference(&g,17); fixture_end(&g.f);
    }
}

TEST(engine_input, tt_joystick_waiter_on_only_rebases_sliders_and_difference_writes_require_relative_slider_one)
{
    joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); joystick_waiter_configure(&g,457,0,457); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.settings[0],"0.5,0.125,0.125")); joystick_waiter_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.position = {.25f,0,0},.rotation = {.25f,0,0},.sliders = {.25f,-.25f},.point_of_view = .5f}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,38,&state); tt_joystick_waiter_difference(&g,0); joystick_waiter_step(&g,1,&state);
    nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_activate_input(&block,0,true); state.position[0] = state.rotation[0] = .75f; state.sliders[0] = .75f; state.sliders[1] = .25f; state.point_of_view = -.5f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,70,&state); tt_joystick_waiter_difference(&g,0);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[4],&(float){17},sizeof(float))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){0},sizeof(uint32_t))); joystick_waiter_step(&g,78,&state); tt_joystick_waiter_difference(&g,17);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[1],&(uint32_t){128},sizeof(uint32_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){128},sizeof(uint32_t))); joystick_waiter_step(&g,1,&state); state.sliders[1] = .75f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,2,&state); tt_joystick_waiter_difference(&g,17);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[1],&(uint32_t){64},sizeof(uint32_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){64},sizeof(uint32_t))); state.sliders[0] = .625f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); tt_joystick_waiter_difference(&g,-.125f);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[1],&(uint32_t){0},sizeof(uint32_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[4],&(float){23},sizeof(float))); state.sliders[0] = 0; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); tt_joystick_waiter_difference(&g,23); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[1],&(uint32_t){64},sizeof(uint32_t))); joystick_waiter_step(&g,1,&state); tt_joystick_waiter_difference(&g,0); fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_waiter_compacts_all_axes_pov_and_32_buttons_in_native_order)
{
    joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); joystick_waiter_configure(&g,511,UINT32_MAX,0); joystick_waiter_ready(&g); ASSERT_EQ(43u,g.output_count); nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,-.75f,.75f},.rotation = {-.75f,.75f,-.75f},.sliders = {.75f,-.75f},.point_of_view = .25f,.buttons = UINT32_MAX}; nmo_engine_set_joystick(g.f.engine,0,&state); const uint64_t all = (UINT64_C(1) << 43)-1; joystick_waiter_step(&g,all & ~UINT64_C(1) & ~(UINT64_C(1) << 10),&state); tt_joystick_waiter_difference(&g,0);
    for (uint32_t i = 0; i < 32; i++) { state.buttons = 1u << i; memset(state.position,0,sizeof(state.position)); memset(state.rotation,0,sizeof(state.rotation)); memset(state.sliders,0,sizeof(state.sliders)); state.point_of_view = -1; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,(UINT64_C(1) << 10) | (UINT64_C(1) << (11+i)),&state); } fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_waiters_continue_after_disconnect_invalid_slots_and_reconnect_without_reactivation)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); if (legacy) joystick_waiter_legacy_layout(&g,0x10000); else joystick_waiter_configure(&g,192,0,192); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.settings[0],"0.5,0.125,0.125")); joystick_waiter_ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[4],&(float){17},sizeof(float))); nmo_engine_joystick_state_t state = {.attached = true,.sliders = {.5f,-.5f},.point_of_view = -1}, neutral = {.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,legacy ? 0 : 1,&state); tt_joystick_waiter_difference(&g,legacy ? 17 : 0);
        nmo_engine_set_joystick(g.f.engine,0,NULL); joystick_waiter_step(&g,legacy ? 0 : 6,&neutral); tt_joystick_waiter_difference(&g,legacy ? 17 : -.5f); joystick_waiter_step(&g,legacy ? 0 : 1,&neutral); tt_joystick_waiter_difference(&g,legacy ? 17 : 0);
        state.sliders[0] = .25f; state.sliders[1] = -.25f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,legacy ? 0 : 6,&state); tt_joystick_waiter_difference(&g,legacy ? 17 : .25f); const int32_t invalid[] = {-1,4,INT32_MAX};
        for (size_t i = 0; i < 3; i++) { ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&invalid[i],sizeof(invalid[i]))); joystick_waiter_step(&g,legacy ? 0 : i == 0 ? 6 : 1,&neutral); tt_joystick_waiter_difference(&g,legacy ? 17 : i == 0 ? -.25f : 0); }
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){0},sizeof(int32_t))); joystick_waiter_step(&g,legacy ? 0 : 6,&state); tt_joystick_waiter_difference(&g,legacy ? 17 : .25f); fixture_end(&g.f);
    }
}

TEST(engine_input, tt_joystick_waiter_off_preserves_on_all_values_and_difference_until_explicit_restart)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); if (legacy) joystick_waiter_legacy_layout(&g,0x10000); else joystick_waiter_configure(&g,511,UINT32_MAX,511); joystick_waiter_ready(&g); nmo_engine_joystick_state_t sentinel = {.position = {17,18,19},.rotation = {20,21,22},.sliders = {23,24},.point_of_view = 25}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[0],sentinel.position,sizeof(sentinel.position))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[1],sentinel.rotation,sizeof(sentinel.rotation))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[2],sentinel.sliders,sizeof(sentinel.sliders))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[3],&sentinel.point_of_view,sizeof(sentinel.point_of_view))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[4],&(float){17},sizeof(float)));
        nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_activate_input(&block,1,true); nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,-.75f,2},.rotation = {.75f,.75f,.75f},.sliders = {.5f,.5f},.point_of_view = -.5f,.buttons = 1}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(nmo_engine_block_is_input_active(&block,0)); ASSERT_FALSE(nmo_engine_block_is_input_active(&block,1)); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); joystick_waiter_values(&g,&sentinel); tt_joystick_waiter_difference(&g,17); for (size_t i = 0; i < g.output_count; i++) ASSERT_EQ(0u,runs(&g.f,g.observers[i]));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block)); joystick_waiter_values(&g,&sentinel); tt_joystick_waiter_difference(&g,17); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); joystick_waiter_step(&g,legacy ? 25 : 3198,&state); tt_joystick_waiter_difference(&g,legacy ? 17 : 0); fixture_end(&g.f);
    }
}

TEST(engine_input, tt_joystick_waiter_preserves_native_unordered_relative_axis_and_pov_comparisons)
{
    joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); joystick_waiter_configure(&g,511,0,511); joystick_waiter_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.position = {NAN,NAN,NAN},.rotation = {NAN,NAN,NAN},.sliders = {NAN,NAN},.point_of_view = NAN}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,510,&state); tt_joystick_waiter_difference(&g,NAN); joystick_waiter_step(&g,510,&state);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){0},sizeof(uint32_t))); joystick_waiter_step(&g,UINT64_C(1) << 10,&state); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){511},sizeof(uint32_t))); state = (nmo_engine_joystick_state_t){.attached = true,.point_of_view = .5f}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,510,&state); tt_joystick_waiter_difference(&g,NAN); joystick_waiter_step(&g,1,&state); tt_joystick_waiter_difference(&g,0);
    state.point_of_view = NAN; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); state.point_of_view = -.5f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); state.point_of_view = .5f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,UINT64_C(1) << 9,&state); fixture_end(&g.f);
}

TEST(engine_input, tt_joystick_waiter_compares_relative_deltas_before_rounding_slider_difference_to_float)
{
    joystick_waiter_fixture_t g; tt_joystick_waiter_begin(&g); joystick_waiter_configure(&g,73,0,73); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.settings[0],"99,0.5,0.5")); joystick_waiter_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[0],(float[3]){0x1p-26f,0,0},3*sizeof(float))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[1],(float[3]){-0x1p-26f,0,0},3*sizeof(float))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[2],(float[2]){-0x1p-26f,0},2*sizeof(float))); state.position[0] = -.5f; state.rotation[0] = .5f; state.sliders[0] = .5f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,14,&state); tt_joystick_waiter_difference(&g,.5f); fixture_end(&g.f);
}

TEST(engine_input, legacy_joystick_waiters_keep_fixed_direction_thresholds_and_six_buttons_without_idle)
{
    const uint32_t versions[] = {0,0x10000,0x1ffff};
    for (size_t i = 0; i < 3; i++) {
        joystick_waiter_fixture_t g; joystick_waiter_legacy_begin(&g,versions[i]); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.settings[0],"0.25,0.75,99")); joystick_waiter_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.position = {.3f,-.8f,2},.rotation = {3,4,5},.sliders = {6,7},.point_of_view = .25f,.buttons = UINT32_MAX}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,0x3f9,&state); joystick_waiter_step(&g,0x3f9,&state);
        state.position[0] *= -1; state.position[1] *= -1; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,0x3f6,&state); state.position[0] = .25f; state.position[1] = -.75f; state.buttons = 0xffffffc0u; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,0,&state); fixture_end(&g.f);
    }
}

TEST(engine_input, legacy_joystick_waiters_ignore_v2_masks_and_use_fixed_button_indices)
{
    joystick_waiter_fixture_t g; joystick_waiter_legacy_begin(&g,0x10000); for (size_t i = 1; i < 4; i++) ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(g.f.tx,g.settings[i],(const uint8_t *)&(uint32_t){0},sizeof(uint32_t))); joystick_waiter_ready(&g);
    for (uint32_t i = 0; i < 32; i++) { nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1,.buttons = 1u << i}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,i < 6 ? UINT64_C(1) << (4+i) : 0,&state); } fixture_end(&g.f);
}

TEST(engine_input, joystick_waiter_sparse_masks_keep_axis_pov_and_high_button_output_positions)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,1|4|16|64|256,0x80000002u,64|256); joystick_waiter_ready(&g); ASSERT_EQ(9u,g.output_count); nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,0,-.75f},.rotation = {0,.75f,0},.sliders = {.25f,0},.point_of_view = .5f,.buttons = 0x80000002u}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,0x1be,&state); joystick_waiter_step(&g,0x18e,&state);
    memset(state.position,0,sizeof(state.position)); memset(state.rotation,0,sizeof(state.rotation)); state.buttons = 0x7ffffffdu; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); state.point_of_view = -1; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,UINT64_C(1) << 6,&state); joystick_waiter_step(&g,1,&state); fixture_end(&g.f);
}

TEST(engine_input, joystick_waiter_pov_relative_events_use_exact_changes_and_absolute_mode_repeats_centered_outputs)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,256,0,256); joystick_waiter_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,4,&state); joystick_waiter_step(&g,1,&state);
    state.point_of_view = 0; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,2,&state); joystick_waiter_step(&g,1,&state); state.point_of_view = nextafterf(0,1); nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,2,&state);
    state.point_of_view = NAN; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,4,&state); joystick_waiter_step(&g,4,&state); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){0},sizeof(uint32_t))); state.point_of_view = -2; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,4,&state); joystick_waiter_step(&g,4,&state); state.point_of_view = .75f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,2,&state); joystick_waiter_step(&g,2,&state); fixture_end(&g.f);
}

TEST(engine_input, joystick_waiter_preserves_negative_nonfinite_thresholds_and_raw_nonfinite_samples)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,255,0,0); joystick_waiter_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; const float negative[] = {-1,-1,NAN}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[0],negative,sizeof(negative))); nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,510,&state);
    const float nan_absolute[] = {NAN,-1,0}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[0],nan_absolute,sizeof(nan_absolute))); joystick_waiter_step(&g,1,&state); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){255},sizeof(uint32_t))); joystick_waiter_step(&g,510,&state);
    const float nan_relative[] = {.5f,NAN,0}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[0],nan_relative,sizeof(nan_relative))); joystick_waiter_step(&g,1,&state); for (size_t i = 0; i < 3; i++) { state.position[i] = NAN; state.rotation[i] = INFINITY; } state.sliders[0] = -INFINITY; state.sliders[1] = NAN; state.point_of_view = NAN; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state);
    const float infinite[] = {INFINITY,INFINITY,0}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[0],infinite,sizeof(infinite))); joystick_waiter_step(&g,1,&state); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){0},sizeof(uint32_t))); joystick_waiter_step(&g,1,&state); fixture_end(&g.f);
}

TEST(engine_input, joystick_waiter_live_slots_masks_and_edited_output_history_take_effect_without_resetting_samples)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,1,0,1); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,g.settings[0],"0.1,0.125,99")); joystick_waiter_ready(&g); nmo_engine_joystick_state_t first = {.attached = true,.position = {.25f,0,0},.point_of_view = -1}, second = first; nmo_engine_set_joystick(g.f.engine,0,&first); nmo_engine_set_joystick(g.f.engine,3,&second); joystick_waiter_step(&g,2,&first);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){3},sizeof(int32_t))); joystick_waiter_step(&g,1,&second); second.position[0] = .5f; nmo_engine_set_joystick(g.f.engine,3,&second); joystick_waiter_step(&g,2,&second); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[0],(float[3]){0,0,0},3*sizeof(float))); joystick_waiter_step(&g,2,&second);
    int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(g.f.engine,g.block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); joystick_waiter_step(&g,1,&second); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[3],&(uint32_t){0},sizeof(uint32_t))); joystick_waiter_step(&g,2,&second);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[1],&(uint32_t){0x80000000u},sizeof(uint32_t))); joystick_waiter_step(&g,1,&second); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.settings[2],&(uint32_t){1},sizeof(uint32_t))); second.buttons = 1; nmo_engine_set_joystick(g.f.engine,3,&second); joystick_waiter_step(&g,2,&second); fixture_end(&g.f);
}

TEST(engine_input, joystick_waiter_off_precedes_missing_devices_and_preserves_on_outputs_and_history_until_restart)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        joystick_waiter_fixture_t g; if (legacy) joystick_waiter_legacy_begin(&g,0x10000); else { joystick_waiter_begin(&g); joystick_waiter_configure(&g,3,1,0); } joystick_waiter_ready(&g); nmo_engine_joystick_state_t sentinel = {.attached = true,.position = {17,18,19},.rotation = {20,21,22},.sliders = {23,24},.point_of_view = 25}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[0],sentinel.position,sizeof(sentinel.position))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[1],sentinel.rotation,sizeof(sentinel.rotation))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[2],sentinel.sliders,sizeof(sentinel.sliders))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[3],&sentinel.point_of_view,sizeof(sentinel.point_of_view))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){INT32_MAX},sizeof(int32_t)));
        nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1))); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); joystick_waiter_values(&g,&sentinel); for (size_t i = 0; i < g.output_count; i++) ASSERT_EQ(0u,runs(&g.f,g.observers[i]));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){0},sizeof(int32_t))); nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,-.75f,0},.point_of_view = -1,.buttons = 1}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block)); joystick_waiter_values(&g,&sentinel); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); joystick_waiter_step(&g,legacy ? 25 : 14,&state); fixture_end(&g.f);
    }
}

TEST(engine_input, joystick_waiter_missing_and_invalid_slots_preserve_on_all_output_values_and_emit_no_events)
{
    const int32_t slots[] = {0,-1,4,INT32_MAX};
    for (int legacy = 0; legacy < 2; legacy++) for (size_t i = 0; i < 4; i++) {
        joystick_waiter_fixture_t g; if (legacy) joystick_waiter_legacy_begin(&g,0x10000); else { joystick_waiter_begin(&g); joystick_waiter_configure(&g,511,UINT32_MAX,511); } joystick_waiter_ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&slots[i],sizeof(slots[i]))); nmo_engine_joystick_state_t sentinel = {.position = {1,2,3},.rotation = {4,5,6},.sliders = {7,8},.point_of_view = 9}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[0],sentinel.position,sizeof(sentinel.position))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[1],sentinel.rotation,sizeof(sentinel.rotation))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[2],sentinel.sliders,sizeof(sentinel.sliders))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.values[3],&sentinel.point_of_view,sizeof(sentinel.point_of_view)));
        nmo_engine_joystick_state_t attached = {.attached = true,.buttons = UINT32_MAX}; if (i > 0) nmo_engine_set_joystick(g.f.engine,0,&attached); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_GENERICERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); joystick_waiter_values(&g,&sentinel); for (size_t j = 0; j < g.output_count; j++) ASSERT_EQ(0u,runs(&g.f,g.observers[j])); fixture_end(&g.f);
    }
}

TEST(engine_input, joystick_waiter_missing_selected_outputs_do_not_turn_active_commands_into_idle_events)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,1,1,0); const nmo_behavior_state_t *state = fixture_state(&g.f,g.block); while (state->outputs.count > 1) ASSERT_EQ(NMO_OK,nmo_script_edit_remove_io(g.f.tx,nmo_behavior_ref_array_get_id(&state->outputs,state->outputs.count-1),true)); g.output_count = 1; joystick_waiter_ready(&g); nmo_engine_joystick_state_t sample = {.attached = true,.position = {.75f,0,0},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&sample); joystick_waiter_step(&g,0,&sample); sample.position[0] = 0; sample.buttons = 1; nmo_engine_set_joystick(g.f.engine,0,&sample); joystick_waiter_step(&g,0,&sample); sample.buttons = 0; nmo_engine_set_joystick(g.f.engine,0,&sample); joystick_waiter_step(&g,1,&sample); fixture_end(&g.f);
}

TEST(engine_input, joystick_waiter_disconnects_stop_execution_and_explicit_reactivation_keeps_relative_history)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,1,0,1); fixture_commit(&g.f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_joystick_state_t state = {.attached = true,.position = {.25f,0,0},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,2,&state);
    nmo_engine_set_joystick(g.f.engine,0,NULL); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); joystick_waiter_values(&g,&state); nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(2u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.observers[1])); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); joystick_waiter_step(&g,1,&state); fixture_end(&g.f);
}

typedef struct joystick_fixture {
    engine_fixture_t f;
    nmo_object_id_t block, target_value, number_value, table, count, sensitivity, output;
    nmo_guid_t guid;
} joystick_fixture_t;

static void joystick_begin_variant(joystick_fixture_t *g, bool mapper)
{
    memset(g,0,sizeof(*g)); g->guid = mapper ? GUID_JOYSTICK_MAPPER : GUID_JOYSTICK_CONTROLLER; fixture_begin(&g->f); g->block = fixture_block(&g->f,g->guid,mapper ? "Joystick Mapper" : "Joystick Controller"); ASSERT_TRUE(g->block != 0); nmo_object_id_t target = fixture_target(&g->f,g->block), number = BLOCK_PIN(&g->f,g->block,0); fixture_input_int(&g->f,target,(int32_t)g->f.owner_id); fixture_input_int(&g->f,number,0); g->target_value = nmo_parameterin_source_id(fixture_state(&g->f,target)); g->number_value = nmo_parameterin_source_id(fixture_state(&g->f,number)); const nmo_behavior_state_t *state = fixture_state(&g->f,g->block); g->table = nmo_behavior_ref_array_get_id(&state->local_parameters,0); if (mapper) g->count = nmo_behavior_ref_array_get_id(&state->local_parameters,1); g->sensitivity = nmo_behavior_ref_array_get_id(&state->local_parameters,mapper ? 2 : 1); g->output = BLOCK_POUT(&g->f,g->block,0); fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
}

static void joystick_begin(joystick_fixture_t *g) { joystick_begin_variant(g,false); }
static void joystick_mapper_begin(joystick_fixture_t *g) { joystick_begin_variant(g,true); }

static void joystick_ready(joystick_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,g->guid)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); nmo_engine_block_t block = {g->f.engine,eng_map_get(&g->f.engine->node_map,g->block)}; nmo_engine_block_set_waiting_for_messages(&block,g->f.owner_id,true);
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

static void mapper_save_bindings(engine_fixture_t *f, nmo_object_id_t table, nmo_object_id_t count_id, const void *records, size_t bytes, int32_t count)
{
    nmo_parameter_state_t *state = fixture_state(f,table); nmo_array_dispose(&state->buffer_data); ASSERT_EQ(NMO_OK,nmo_array_alloc(&state->buffer_data,1,bytes,NULL)); if (bytes != 0) memcpy(state->buffer_data.data,records,bytes); state->mode = CKPARAM_MODE_BUFFER; state->has_state = true; char text[32]; snprintf(text,sizeof(text),"%d",count); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(f->tx,count_id,text));
}

static void mapper_saved(keyboard_fixture_t *g, const void *records, size_t bytes, int32_t count)
{
    mapper_save_bindings(&g->f,g->table,g->count,records,bytes,count);
}

static void joystick_mapper_saved(joystick_fixture_t *g, const void *records, size_t bytes, int32_t count)
{
    mapper_save_bindings(&g->f,g->table,g->count,records,bytes,count);
}

TEST(engine_input, joystick_mapper_loads_names_and_repeats_bindings_in_record_order_with_raw_analog_output)
{
    joystick_fixture_t g; joystick_mapper_begin(&g); uint8_t records[240]; const uint32_t commands[] = {1043,1008,1012,1010,1008,1011}; const char *names[] = {"High Button","Forward","Action","Left","Forward","Right"};
    for (size_t i = 0; i < 6; i++) mapper_record(records+40*i,commands[i],999,names[i]); joystick_mapper_saved(&g,records,sizeof(records),6); joystick_ready(&g); uint8_t runtime[240]; read_value(&g.f,g.table,runtime,sizeof(runtime));
    for (size_t i = 0; i < 6; i++) { ASSERT_EQ(commands[i],nmo_read_u32_le(runtime+40*i)); ASSERT_EQ(nmo_engine_message_type(g.f.engine,names[i]),(int32_t)nmo_read_u32_le(runtime+40*i+4)); }
    nmo_engine_joystick_state_t state = {.attached = true,.position = {-1,-.75f,1.5f},.point_of_view = -1,.buttons = 0x80000001u}; nmo_engine_set_joystick(g.f.engine,0,&state); const char *expected[] = {"High Button","Forward","Action","Left","Forward"};
    for (int frame = 0; frame < 2; frame++) { ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,expected,5); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(state.position[i],output[i],0); }
    state.position[0] = state.position[1] = 0; state.buttons = 0; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,NULL,0); ASSERT_TRUE(nmo_engine_behavior_is_active(g.f.engine,g.block)); ASSERT_TRUE(memcmp(records,((nmo_parameter_state_t *)fixture_state(&g.f,g.table))->buffer_data.data,sizeof(records)) == 0); fixture_end(&g.f);
}

TEST(engine_input, joystick_mapper_supports_all_32_buttons_independently)
{
    joystick_fixture_t g; joystick_mapper_begin(&g); uint8_t records[32*40]; char names[32][24];
    for (uint32_t i = 0; i < 32; i++) { snprintf(names[i],sizeof(names[i]),"Button %u",i+1); mapper_record(records+40*i,1012+i,-1,names[i]); } joystick_mapper_saved(&g,records,sizeof(records),32); joystick_ready(&g);
    for (uint32_t i = 0; i < 32; i++) { nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1,.buttons = 1u << i}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *expected[] = {names[i]}; joystick_received(&g,g.f.owner_id,expected,1); } fixture_end(&g.f);
}

TEST(engine_input, joystick_mapper_direction_comparisons_preserve_strict_negative_and_nonfinite_thresholds)
{
    joystick_fixture_t g; joystick_mapper_begin(&g); uint8_t records[160]; const char *names[] = {"Up","Down","Left","Right"};
    for (uint32_t i = 0; i < 4; i++) mapper_record(records+40*i,1008+i,-1,names[i]); joystick_mapper_saved(&g,records,sizeof(records),4); joystick_ready(&g);
    nmo_engine_joystick_state_t state = {.attached = true,.position = {.5f,-.5f,20},.point_of_view = -1};
    for (int sign = 0; sign < 2; sign++) { nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,NULL,0); state.position[0] *= -1; state.position[1] *= -1; }
    state.position[0] = nextafterf(.5f,1); state.position[1] = nextafterf(-.5f,-1); nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *first[] = {"Up","Right"}; joystick_received(&g,g.f.owner_id,first,2);
    state.position[0] *= -1; state.position[1] *= -1; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *second[] = {"Down","Left"}; joystick_received(&g,g.f.owner_id,second,2);
    const float negative[] = {-1,-1,NAN}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.sensitivity,negative,sizeof(negative))); state.position[0] = state.position[1] = 0; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,names,4);
    const float nonfinite[] = {NAN,INFINITY,-INFINITY}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.sensitivity,nonfinite,sizeof(nonfinite))); state.position[0] = INFINITY; state.position[1] = -INFINITY; state.position[2] = NAN; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,NULL,0); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); ASSERT_TRUE(isinf(output[0]) && output[0] > 0); ASSERT_TRUE(isinf(output[1]) && output[1] < 0); ASSERT_TRUE(isnan(output[2])); fixture_end(&g.f);
}

TEST(engine_input, joystick_mapper_reads_live_slots_targets_counts_and_cached_ids_without_rebinding_on_edit)
{
    joystick_fixture_t g; joystick_mapper_begin(&g); uint8_t records[125]; mapper_record(records,1008,500,"Forward"); mapper_record(records+40,1012,501,"12345678901234567890123456789012"); mapper_record(records+80,1043,502,"Inactive"); memset(records+120,0xe7,5); joystick_mapper_saved(&g,records,sizeof(records),2);
    nmo_object_id_t owner = g.f.owner_id, group = fixture_create(&g.f,NMO_CID_GROUP,"Joystick Mapper Receiver"), input = 0; ASSERT_TRUE(group != 0); g.f.owner_id = group; ASSERT_TRUE(fixture_add_script(&g.f,"Joystick Mapper Receiver Script",&input) != 0); g.f.owner_id = owner; joystick_ready(&g); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"Inactive")); uint8_t runtime[125]; read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_TRUE(memcmp(records+80,runtime+80,45) == 0);
    nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,group,true); ASSERT_TRUE(eng_group_add(g.f.engine,group,owner)); nmo_engine_joystick_state_t first = {.attached = true,.position = {0,-1,.25f},.point_of_view = -1}, second = {.attached = true,.position = {.75f,0,-.75f},.point_of_view = -1,.buttons = 1}; nmo_engine_set_joystick(g.f.engine,0,&first); nmo_engine_set_joystick(g.f.engine,3,&second); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *forward[] = {"Forward"}; joystick_received(&g,owner,forward,1);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&group,sizeof(group))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){3},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *button[] = {"12345678901234567890123456789012"}; joystick_received(&g,group,button,1);
    nmo_write_u32_le(runtime+40,1011); nmo_write_u32_le(runtime+44,nmo_read_u32_le(runtime+4)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,runtime,sizeof(runtime))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){1},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,group,NULL,0);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){2},sizeof(int32_t))); int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(g.f.engine,g.block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,group,forward,1); read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_TRUE(memcmp(records+80,runtime+80,45) == 0); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"Inactive")); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(second.position[i],output[i],0); fixture_end(&g.f);
}

TEST(engine_input, joystick_mapper_nonpositive_counts_still_publish_attached_device_positions)
{
    const int32_t counts[] = {0,-1,INT32_MIN};
    for (size_t i = 0; i < 3; i++) {
        joystick_fixture_t g; joystick_mapper_begin(&g); joystick_mapper_saved(&g,NULL,0,counts[i]); joystick_ready(&g); nmo_engine_joystick_state_t state = {.attached = true,.position = {-.9f,.9f,.6f},.point_of_view = -1,.buttons = UINT32_MAX}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,NULL,0); ASSERT_TRUE(nmo_engine_behavior_is_active(g.f.engine,g.block)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t axis = 0; axis < 3; axis++) ASSERT_FLOAT_EQ(state.position[axis],output[axis],0); fixture_end(&g.f);
    }
}

TEST(engine_input, joystick_mapper_off_wins_before_invalid_state_and_preserves_on_until_explicit_restart)
{
    joystick_fixture_t g; joystick_mapper_begin(&g); uint8_t record[40]; mapper_record(record,1012,-1,"Action"); joystick_mapper_saved(&g,record,sizeof(record),1); joystick_ready(&g); const float sentinel[] = {17,18,19}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,sentinel,sizeof(sentinel))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){2},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&(nmo_object_id_t){0},sizeof(nmo_object_id_t))); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_activate_input(&block,1,true); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1))); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(sentinel[i],output[i],0);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){1},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&g.f.owner_id,sizeof(g.f.owner_id))); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1,.buttons = 1}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block)); joystick_received(&g,g.f.owner_id,NULL,0); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *action[] = {"Action"}; joystick_received(&g,g.f.owner_id,action,1); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); fixture_end(&g.f);
}

TEST(engine_input, joystick_mapper_owner_and_device_errors_consume_on_and_preserve_output_without_sending)
{
    for (int kind = 0; kind < 8; kind++) {
        joystick_fixture_t g; joystick_mapper_begin(&g); uint8_t record[40]; mapper_record(record,1012,-1,"Action"); joystick_mapper_saved(&g,record,sizeof(record),kind == 7 ? 0 : 1); nmo_object_id_t other = fixture_create(&g.f,kind == 3 ? NMO_CID_GROUP : NMO_CID_OBJECT,"Invalid Joystick Mapper Target"); ASSERT_TRUE(other != 0); joystick_ready(&g); const float sentinel[] = {17,18,19}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,sentinel,sizeof(sentinel)));
        if (kind < 4) { nmo_object_id_t target = kind == 0 ? 0 : kind == 1 ? 0x7ffffffeu : other; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target))); if (kind == 3) nmo_engine_destroy_object(g.f.engine,other); }
        else if (kind == 5 || kind == 6) { int32_t slot = kind == 5 ? -1 : 4; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&slot,sizeof(slot))); }
        nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1,.buttons = 1}; if (kind < 4 || kind == 5 || kind == 6) nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(kind < 4 ? NMO_ENGINE_BR_OWNERERROR : NMO_ENGINE_BR_GENERICERROR,failure.result); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(0u,g.f.engine->messages.sent.count); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(sentinel[i],output[i],0); fixture_end(&g.f);
    }
}

TEST(engine_input, joystick_mapper_truncated_bindings_and_wide_counts_fail_before_partial_load_or_delivery)
{
    const size_t sizes[] = {0,39,79,40}; const int32_t counts[] = {1,1,2,INT32_MAX};
    for (size_t i = 0; i < 4; i++) {
        joystick_fixture_t g; joystick_mapper_begin(&g); uint8_t records[80]; mapper_record(records,1012,999,"Partial"); mapper_record(records+40,1012,998,"Tail"); joystick_mapper_saved(&g,records,sizes[i],counts[i]); joystick_ready(&g); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"Partial")); if (sizes[i]) { uint8_t runtime[80]; read_value(&g.f,g.table,runtime,sizes[i]); ASSERT_TRUE(memcmp(records,runtime,sizes[i]) == 0); }
        const float sentinel[] = {17,18,19}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.output,sentinel,sizeof(sentinel))); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1,.buttons = 1}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(0u,g.f.engine->messages.sent.count); float output[3]; read_value(&g.f,g.output,output,sizeof(output)); for (size_t axis = 0; axis < 3; axis++) ASSERT_FLOAT_EQ(sentinel[axis],output[axis],0); fixture_end(&g.f);
    }
}

TEST(engine_input, joystick_mapper_wraps_native_button_shift_counts_and_continues_after_empty_message_names)
{
    joystick_fixture_t g; joystick_mapper_begin(&g); uint8_t records[280]; const uint32_t commands[] = {1012,1044,980,UINT32_MAX,0x80000000u,1007,1043}; const char *names[] = {"","Wrapped High","Wrapped Low","Negative","Signed Limit","Below Directions","Last"};
    for (size_t i = 0; i < 7; i++) mapper_record(records+40*i,commands[i],999,names[i]); joystick_mapper_saved(&g,records,sizeof(records),7); joystick_ready(&g); uint8_t runtime[280]; read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_EQ(UINT32_MAX,nmo_read_u32_le(runtime+4)); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"")); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1,.buttons = 0x80000001u | (1u << 11) | (1u << 12) | (1u << 27)}; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,g.f.owner_id,names+1,6); ASSERT_TRUE(nmo_engine_behavior_is_active(g.f.engine,g.block)); fixture_end(&g.f);
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

TEST(engine_input, loaded_tt_joystick_queries_and_copies_preserve_numbers_and_links_but_read_current_host_devices)
{
    tt_joystick_test_fixture_t source; tt_joystick_test_begin(&source); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.number_value,"3")); fixture_commit_bare(&source.f,NULL); nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET; const char *path = NMO_TEST_SCRATCH_FILE("engine_tt_test_for_joystick.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        tt_joystick_test_fixture_t g = {0}; fixture_begin_document(&g.f); fixture_begin_edit(&g.f); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(g.f.engine,keyboard_resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(g.f.engine,"joystick-query.cmo",true,&file));
        g.block = nmo_engine_object_named(g.f.engine,"TT_TestForJoystick",NMO_CID_BEHAVIOR); ASSERT_TRUE(g.block != 0); const eng_module_t *module = eng_module_of(g.f.engine,g.block,NULL); g.f.owner_id = eng_module_id(module,source.f.owner_id); g.f.script_id = eng_module_id(module,source.f.script_id); g.number_value = eng_module_id(module,source.number_value); g.yes = eng_module_id(module,source.yes); g.no = eng_module_id(module,source.no); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_SCENE_DEFAULT)); int32_t number = -1; read_value(&g.f,g.number_value,&number,sizeof(number)); ASSERT_EQ(3,number); nmo_engine_joystick_state_t state = {.attached = true,.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); tt_joystick_test_step(&g,false);
        nmo_engine_set_joystick(g.f.engine,3,&state); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); tt_joystick_test_step(&g,true); tt_joystick_test_fire(&g,2,false); nmo_object_id_t ids[] = {g.f.owner_id,g.f.script_id,g.block,g.yes,g.no,g.number_value}, copies[6] = {0}; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,6,false,copies)); for (size_t i = 0; i < 6; i++) ASSERT_TRUE(copies[i] != 0); for (size_t i = 0; i < 6; i++) nmo_engine_destroy_object(g.f.engine,ids[i]);
        g.f.owner_id = copies[0]; g.f.script_id = copies[1]; g.block = copies[2]; g.yes = copies[3]; g.no = copies[4]; g.number_value = copies[5]; g.calls = runs(&g.f,g.block); g.yes_hits = runs(&g.f,g.yes); g.no_hits = runs(&g.f,g.no); read_value(&g.f,g.number_value,&number,sizeof(number)); ASSERT_EQ(2,number); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); tt_joystick_test_step(&g,false); nmo_engine_set_joystick(g.f.engine,2,&state); tt_joystick_test_fire(&g,2,true); nmo_engine_set_joystick(g.f.engine,2,NULL); tt_joystick_test_fire(&g,2,false); fixture_end(&g.f);
    }
    const nmo_parameter_state_t *number = fixture_state(&source.f,source.number_value); ASSERT_EQ(sizeof(int32_t),number->buffer_data.count); ASSERT_TRUE(memcmp(number->buffer_data.data,&(int32_t){3},sizeof(int32_t)) == 0); fixture_end(&source.f); remove(path);
}

TEST(engine_input, tt_test_for_joystick_routes_missing_devices_to_fallback_before_starting_the_waiter)
{
    for (int attached = 0; attached < 2; attached++) {
        joystick_waiter_fixture_t g; joystick_waiter_begin(&g); const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id); ASSERT_EQ(1u,script->sub_behavior_links.count); ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0))); joystick_waiter_configure(&g,1,0,0);
        nmo_object_id_t query = fixture_block(&g.f,GUID_TT_TEST_FOR_JOYSTICK,"Check Before Waiting"), fallback = fixture_block(&g.f,GUID_NOP,"No Device Fallback"); ASSERT_TRUE(query != 0 && fallback != 0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,query,0),3); fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,query,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,query,0),BLOCK_IN(&g.f,g.block,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,query,1),BLOCK_IN(&g.f,fallback,0),0); joystick_waiter_ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){3},sizeof(int32_t)));
        nmo_engine_joystick_state_t decoy = {.attached = true,.position = {1,2,3},.point_of_view = -1}, selected = {.attached = true,.position = {.75f,.25f,-.5f},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&decoy); if (attached) nmo_engine_set_joystick(g.f.engine,3,&selected);
        if (attached) { joystick_waiter_step(&g,2,&selected); ASSERT_EQ(0u,runs(&g.f,fallback)); joystick_waiter_step(&g,2,&selected); ASSERT_EQ(2u,runs(&g.f,g.block)); }
        else {
            ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,fallback)); ASSERT_EQ(0u,runs(&g.f,g.block)); nmo_engine_joystick_state_t defaults = {0}; joystick_waiter_values(&g,&defaults); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,fallback)); ASSERT_EQ(0u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,query));
            nmo_engine_set_joystick(g.f.engine,3,&selected); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); joystick_waiter_step(&g,2,&selected); ASSERT_EQ(1u,runs(&g.f,fallback)); ASSERT_EQ(1u,runs(&g.f,g.block));
        }
        ASSERT_EQ(attached ? 1u : 2u,runs(&g.f,query)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,query)}; ASSERT_FALSE(nmo_engine_block_is_input_active(&block,0)); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,query)); fixture_end(&g.f);
    }
}

TEST(engine_input, loaded_tt_joystick_vector_mappers_and_copies_keep_live_input_assignments_and_cached_output)
{
    tt_vector_mapper_fixture_t source; tt_vector_mapper_begin(&source); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.input_value,"3,-2,-0.75")); const int32_t saved_mappings[] = {4,5,2,3}; for (size_t i = 0; i < 4; i++) { char text[16]; snprintf(text,sizeof(text),"%d",saved_mappings[i]); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.mappings[i],text)); } fixture_commit_bare(&source.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET; const char *path = NMO_TEST_SCRATCH_FILE("engine_tt_joystick_vector_mapper.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        tt_vector_mapper_fixture_t g = {0}; fixture_begin_document(&g.f); fixture_begin_edit(&g.f); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(g.f.engine,keyboard_resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(g.f.engine,"vector-mapper.cmo",true,&file));
        g.block = nmo_engine_object_named(g.f.engine,"TT_JoystickVectorMapper",NMO_CID_BEHAVIOR); ASSERT_TRUE(g.block != 0); const eng_module_t *module = eng_module_of(g.f.engine,g.block,NULL); g.f.owner_id = eng_module_id(module,source.f.owner_id); g.f.script_id = eng_module_id(module,source.f.script_id); g.done = eng_module_id(module,source.done); g.input_value = eng_module_id(module,source.input_value); g.output = eng_module_id(module,source.output); for (size_t i = 0; i < 4; i++) g.mappings[i] = eng_module_id(module,source.mappings[i]); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_SCENE_DEFAULT)); tt_vector_mapper_step(&g,(float[3]){-.75f,-2,0});
        const int32_t mappings[] = {2,3,4,5}; tt_vector_mapper_fire(&g,(float[3]){9,.5f,-.25f},mappings,(float[3]){.5f,-.25f,0}); nmo_object_id_t ids[] = {g.f.owner_id,g.f.script_id,g.block,g.done,g.input_value,g.mappings[0],g.mappings[1],g.mappings[2],g.mappings[3],g.output}, copies[10] = {0}; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,10,false,copies)); for (size_t i = 0; i < 10; i++) ASSERT_TRUE(copies[i] != 0); for (size_t i = 0; i < 10; i++) nmo_engine_destroy_object(g.f.engine,ids[i]);
        g.f.owner_id = copies[0]; g.f.script_id = copies[1]; g.block = copies[2]; g.done = copies[3]; g.input_value = copies[4]; for (size_t i = 0; i < 4; i++) { g.mappings[i] = copies[5+i]; int32_t value = -1; read_value(&g.f,g.mappings[i],&value,sizeof(value)); ASSERT_EQ(mappings[i],value); } g.output = copies[9]; g.calls = runs(&g.f,g.block); tt_vector_mapper_value(&g,(float[3]){.5f,-.25f,0}); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); tt_vector_mapper_step(&g,(float[3]){.5f,-.25f,0}); tt_vector_mapper_fire(&g,(float[3]){-9,-.5f,.25f},NULL,(float[3]){-.5f,.25f,0}); fixture_end(&g.f);
    }
    for (size_t i = 0; i < 4; i++) { const nmo_parameter_state_t *parameter = fixture_state(&source.f,source.mappings[i]); ASSERT_EQ(sizeof(int32_t),parameter->buffer_data.count); ASSERT_TRUE(memcmp(parameter->buffer_data.data,saved_mappings+i,sizeof(int32_t)) == 0); } fixture_end(&source.f); remove(path);
}

TEST(engine_input, joystick_waiter_vector_mapper_and_translate_deliver_current_remapped_axes_in_one_frame)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,1,0,0); nmo_object_id_t mapper = fixture_block(&g.f,GUID_TT_JOYSTICK_VECTOR_MAPPER,"Map Joystick Axes"), translate = fixture_block(&g.f,NMO_GUID(0x000d000d,0x000d000d),"Translate Remapped Joystick"); ASSERT_TRUE(mapper != 0 && translate != 0); ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,g.values[0],BLOCK_PIN(&g.f,mapper,0))); const int32_t mappings[] = {2,3,4,5}; for (size_t i = 0; i < 4; i++) fixture_input_int(&g.f,BLOCK_PIN(&g.f,mapper,i+1),mappings[i]); fixture_input_int(&g.f,fixture_target(&g.f,translate),(int32_t)g.f.owner_id); fixture_input_int(&g.f,BLOCK_PIN(&g.f,translate,1),0); ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,mapper,0),BLOCK_PIN(&g.f,translate,0))); fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,1),BLOCK_IN(&g.f,mapper,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,mapper,0),BLOCK_IN(&g.f,translate,0),0); joystick_waiter_ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){3},sizeof(int32_t)));
    nmo_engine_joystick_state_t decoy = {.attached = true,.position = {1,2,3},.point_of_view = -1}, selected = {.attached = true,.position = {.75f,.25f,-.5f},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&decoy); nmo_engine_set_joystick(g.f.engine,3,&selected); joystick_waiter_step(&g,2,&selected); float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); ASSERT_FLOAT_EQ(.25f,matrix[12],1e-6f); ASSERT_FLOAT_EQ(-.5f,matrix[13],1e-6f); ASSERT_FLOAT_EQ(0,matrix[14],1e-6f); ASSERT_EQ(1u,runs(&g.f,mapper)); ASSERT_EQ(1u,runs(&g.f,translate));
    selected.position[0] = -.75f; selected.position[1] = -.5f; selected.position[2] = .25f; nmo_engine_set_joystick(g.f.engine,3,&selected); joystick_waiter_step(&g,2,&selected); ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); ASSERT_FLOAT_EQ(-.25f,matrix[12],1e-6f); ASSERT_FLOAT_EQ(-.25f,matrix[13],1e-6f); ASSERT_FLOAT_EQ(0,matrix[14],1e-6f); ASSERT_EQ(2u,runs(&g.f,mapper)); ASSERT_EQ(2u,runs(&g.f,translate));
    selected.position[0] = .5f; selected.position[1] = selected.position[2] = 99; nmo_engine_set_joystick(g.f.engine,3,&selected); joystick_waiter_step(&g,1,&selected); ASSERT_EQ(2u,runs(&g.f,mapper)); ASSERT_EQ(2u,runs(&g.f,translate)); ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); ASSERT_FLOAT_EQ(-.25f,matrix[12],1e-6f); ASSERT_FLOAT_EQ(-.25f,matrix[13],1e-6f); ASSERT_FLOAT_EQ(0,matrix[14],1e-6f); fixture_end(&g.f);
}

TEST(engine_input, loaded_joystick_waiters_and_copies_preserve_versions_layouts_settings_and_relative_history)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        joystick_waiter_fixture_t source; if (legacy) joystick_waiter_legacy_begin(&source,0x10000); else { joystick_waiter_begin(&source); joystick_waiter_configure(&source,353,0x80000000u,353); }
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.number_value,"2")); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.settings[0],"0.5,0.125,99")); nmo_engine_joystick_state_t saved = {.attached = true,.position = {.25f,0,0},.rotation = {0,0,.25f},.sliders = {.25f,0},.point_of_view = .25f};
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(source.f.tx,source.values[0],(const uint8_t *)saved.position,sizeof(saved.position))); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(source.f.tx,source.values[1],(const uint8_t *)saved.rotation,sizeof(saved.rotation))); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(source.f.tx,source.values[2],(const uint8_t *)saved.sliders,sizeof(saved.sliders))); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(source.f.tx,source.values[3],(const uint8_t *)&saved.point_of_view,sizeof(saved.point_of_view))); fixture_commit_bare(&source.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = legacy ? NMO_TEST_SCRATCH_FILE("engine_joystick_waiter_v1.cmo") : NMO_TEST_SCRATCH_FILE("engine_joystick_waiter_v2.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            joystick_waiter_fixture_t g = {0}; fixture_begin_document(&g.f); fixture_begin_edit(&g.f); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(g.f.engine,keyboard_resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(g.f.engine,"joystick-waiter.cmo",true,&file));
            g.block = nmo_engine_object_named(g.f.engine,"Joystick Waiter",NMO_CID_BEHAVIOR); ASSERT_TRUE(g.block != 0); const eng_module_t *module = eng_module_of(g.f.engine,g.block,NULL); g.f.owner_id = eng_module_id(module,source.f.owner_id); g.f.script_id = eng_module_id(module,source.f.script_id); g.number_value = eng_module_id(module,source.number_value); g.output_count = source.output_count; for (size_t i = 0; i < 4; i++) { g.settings[i] = eng_module_id(module,source.settings[i]); g.values[i] = eng_module_id(module,source.values[i]); } for (size_t i = 0; i < g.output_count; i++) g.observers[i] = eng_module_id(module,source.observers[i]);
            ASSERT_FALSE(nmo_engine_joystick_state(g.f.engine,2,NULL)); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_SCENE_DEFAULT)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; ASSERT_EQ(legacy ? 0x10000u : 0x20000u,nmo_engine_block_version(&block)); ASSERT_EQ(g.output_count,(size_t)nmo_engine_block_output_count(&block)); nmo_engine_set_joystick(g.f.engine,2,&saved); joystick_waiter_step(&g,legacy ? 0 : 1,&saved);
            nmo_engine_joystick_state_t state = saved; state.position[0] = state.rotation[2] = state.sliders[0] = .75f; state.point_of_view = -1; state.buttons = 0x80000001u; nmo_engine_set_joystick(g.f.engine,2,&state); joystick_waiter_step(&g,legacy ? 24 : 110,&state);
            nmo_object_id_t ids[55] = {g.f.owner_id,g.f.script_id,g.block,g.number_value}, copies[55] = {0}; for (size_t i = 0; i < 4; i++) { ids[4+i] = g.settings[i]; ids[8+i] = g.values[i]; } for (size_t i = 0; i < g.output_count; i++) ids[12+i] = g.observers[i]; int32_t count = (int32_t)(12+g.output_count); ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,count,false,copies)); for (int32_t i = 0; i < count; i++) ASSERT_TRUE(copies[i] != 0); for (int32_t i = 0; i < count; i++) nmo_engine_destroy_object(g.f.engine,ids[i]);
            g.f.owner_id = copies[0]; g.f.script_id = copies[1]; g.block = copies[2]; g.number_value = copies[3]; for (size_t i = 0; i < 4; i++) { g.settings[i] = copies[4+i]; g.values[i] = copies[8+i]; } for (size_t i = 0; i < g.output_count; i++) { g.observers[i] = copies[12+i]; g.hits[i] = runs(&g.f,g.observers[i]); }
            int32_t slot = -1; read_value(&g.f,g.number_value,&slot,sizeof(slot)); ASSERT_EQ(2,slot); float sensitivity[3]; read_value(&g.f,g.settings[0],sensitivity,sizeof(sensitivity)); ASSERT_FLOAT_EQ(.5f,sensitivity[0],0); ASSERT_FLOAT_EQ(.125f,sensitivity[1],0); ASSERT_FLOAT_EQ(99,sensitivity[2],0); if (!legacy) { const uint32_t expected[] = {353,0x80000000u,353}; for (size_t i = 0; i < 3; i++) { uint32_t flags = 0; read_value(&g.f,g.settings[i+1],&flags,sizeof(flags)); ASSERT_EQ(expected[i],flags); } } joystick_waiter_values(&g,&state);
            block = (nmo_engine_block_t){g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; ASSERT_EQ(legacy ? 0x10000u : 0x20000u,nmo_engine_block_version(&block)); ASSERT_EQ(g.output_count,(size_t)nmo_engine_block_output_count(&block)); int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(g.f.engine,g.block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); state.buttons = 0; nmo_engine_set_joystick(g.f.engine,2,&state); joystick_waiter_step(&g,legacy ? 8 : 1,&state);
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){0},sizeof(int32_t))); state.position[0] = state.rotation[2] = state.sliders[0] = .875f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,legacy ? 8 : 1,&state); state.position[0] = state.rotation[2] = state.sliders[0] = .625f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,legacy ? 8 : 14,&state); fixture_end(&g.f);
        }
        const nmo_behavior_state_t *original = fixture_state(&source.f,source.block); ASSERT_EQ(legacy ? 0x10000u : 0x20000u,original->block_version); const nmo_parameter_state_t *position = fixture_state(&source.f,source.values[0]); ASSERT_EQ(sizeof(saved.position),position->buffer_data.count); ASSERT_TRUE(memcmp(saved.position,position->buffer_data.data,sizeof(saved.position)) == 0); fixture_end(&source.f); remove(path);
    }
}

TEST(engine_input, joystick_waiter_axis_outputs_drive_translations_using_the_current_analog_position)
{
    joystick_waiter_fixture_t g; joystick_waiter_begin(&g); joystick_waiter_configure(&g,1,0,0); nmo_object_id_t translate = fixture_block(&g.f,NMO_GUID(0x000d000d,0x000d000d),"Translate From Joystick"); ASSERT_TRUE(translate != 0); fixture_input_int(&g.f,fixture_target(&g.f,translate),(int32_t)g.f.owner_id); fixture_input_int(&g.f,BLOCK_PIN(&g.f,translate,1),0); ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,g.values[0],BLOCK_PIN(&g.f,translate,0))); fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,1),BLOCK_IN(&g.f,translate,0),0); joystick_waiter_ready(&g);
    nmo_engine_joystick_state_t state = {.attached = true,.position = {.75f,.25f,-.5f},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,2,&state); float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(state.position[i],matrix[12+i],1e-6f); ASSERT_EQ(1u,runs(&g.f,translate));
    state.position[0] = -.75f; state.position[1] = .5f; state.position[2] = .25f; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,2,&state); ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); ASSERT_FLOAT_EQ(0,matrix[12],1e-6f); ASSERT_FLOAT_EQ(.75f,matrix[13],1e-6f); ASSERT_FLOAT_EQ(-.25f,matrix[14],1e-6f); ASSERT_EQ(2u,runs(&g.f,translate));
    state.position[0] = .5f; state.position[1] = state.position[2] = 99; nmo_engine_set_joystick(g.f.engine,0,&state); joystick_waiter_step(&g,1,&state); ASSERT_EQ(2u,runs(&g.f,translate)); ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix)); ASSERT_FLOAT_EQ(0,matrix[12],1e-6f); ASSERT_FLOAT_EQ(.75f,matrix[13],1e-6f); ASSERT_FLOAT_EQ(-.25f,matrix[14],1e-6f); fixture_end(&g.f);
}

TEST(engine_input, loaded_joystick_mappers_rebind_active_names_and_copies_keep_cached_bindings_and_analog_state)
{
    joystick_fixture_t source; joystick_mapper_begin(&source); uint8_t records[125]; mapper_record(records,1009,900,"Custom Down"); mapper_record(records+40,1010,901,"Custom Left"); mapper_record(records+80,1043,902,"Inactive"); memset(records+120,0xd3,5); joystick_mapper_saved(&source,records,sizeof(records),2);
    nmo_object_id_t owner_id = source.f.owner_id, receiver = fixture_create(&source.f,NMO_CID_GROUP,"Saved Joystick Mapper Receiver"), input = 0; ASSERT_TRUE(receiver != 0); source.f.owner_id = receiver; nmo_object_id_t receiver_script = fixture_add_script(&source.f,"Saved Joystick Mapper Receiver Script",&input); ASSERT_TRUE(receiver_script != 0); source.f.owner_id = owner_id;
    char text[32]; snprintf(text,sizeof(text),"%u",receiver); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.target_value,text)); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.number_value,"2")); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(source.f.tx,source.sensitivity,"0.25,0.75,-1")); fixture_commit_bare(&source.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source.f,owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE; nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET; const char *path = NMO_TEST_SCRATCH_FILE("engine_joystick_mapper.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        joystick_fixture_t g = {0}; fixture_begin_document(&g.f); const char *types[] = {"Custom Left","Host Only","Custom Down"}; fixture_install_messages(&g.f,types,3); fixture_begin_edit(&g.f); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g.f,&options); if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL)); nmo_engine_set_file_resolver(g.f.engine,keyboard_resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(g.f.engine,"joystick-mapper.cmo",true,&file));
        g.block = nmo_engine_object_named(g.f.engine,"Joystick Mapper",NMO_CID_BEHAVIOR); ASSERT_TRUE(g.block != 0); const eng_module_t *module = eng_module_of(g.f.engine,g.block,NULL); g.f.owner_id = eng_module_id(module,owner_id); g.f.script_id = eng_module_id(module,source.f.script_id); g.target_value = eng_module_id(module,source.target_value); g.number_value = eng_module_id(module,source.number_value); g.table = eng_module_id(module,source.table); g.count = eng_module_id(module,source.count); g.sensitivity = eng_module_id(module,source.sensitivity); g.output = eng_module_id(module,source.output); nmo_object_id_t target = eng_module_id(module,receiver), target_script = eng_module_id(module,receiver_script); if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_SCENE_DEFAULT)); nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,target,true);
        uint8_t runtime[125]; read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_EQ(nmo_engine_message_type(g.f.engine,"Custom Down"),(int32_t)nmo_read_u32_le(runtime+4)); ASSERT_EQ(nmo_engine_message_type(g.f.engine,"Custom Left"),(int32_t)nmo_read_u32_le(runtime+44)); ASSERT_TRUE(memcmp(records+80,runtime+80,45) == 0); ASSERT_EQ(-1,nmo_engine_message_type(g.f.engine,"Inactive")); int32_t slot = -1, count = -1; read_value(&g.f,g.number_value,&slot,sizeof(slot)); read_value(&g.f,g.count,&count,sizeof(count)); ASSERT_EQ(2,slot); ASSERT_EQ(2,count);
        nmo_engine_joystick_state_t state = {.attached = true,.position = {-.5f,.8f,-.33f},.point_of_view = -1}; nmo_engine_set_joystick(g.f.engine,2,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *initial[] = {"Custom Down","Custom Left"}; joystick_received(&g,target,initial,2);
        nmo_write_u32_le(runtime+4,nmo_read_u32_le(runtime+44)); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.table,runtime,sizeof(runtime))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){1},sizeof(int32_t))); nmo_object_id_t ids[] = {g.f.owner_id,target,g.f.script_id,target_script,g.block,g.target_value,g.number_value,g.table,g.count,g.sensitivity,g.output}, copies[11] = {0}; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,11,false,copies)); for (size_t i = 0; i < 11; i++) ASSERT_TRUE(copies[i] != 0); for (size_t i = 0; i < 11; i++) nmo_engine_destroy_object(g.f.engine,ids[i]);
        g.f.owner_id = copies[0]; target = copies[1]; g.f.script_id = copies[2]; g.block = copies[4]; g.target_value = copies[5]; g.number_value = copies[6]; g.table = copies[7]; g.count = copies[8]; g.sensitivity = copies[9]; g.output = copies[10]; nmo_object_id_t mapped = 0; read_value(&g.f,g.target_value,&mapped,sizeof(mapped)); ASSERT_EQ(target,mapped); read_value(&g.f,g.number_value,&slot,sizeof(slot)); read_value(&g.f,g.count,&count,sizeof(count)); ASSERT_EQ(2,slot); ASSERT_EQ(1,count); const float sensitivity[] = {.25f,.75f,-1}; float output[3], copied_sensitivity[3]; read_value(&g.f,g.output,output,sizeof(output)); read_value(&g.f,g.sensitivity,copied_sensitivity,sizeof(copied_sensitivity)); for (size_t i = 0; i < 3; i++) { ASSERT_FLOAT_EQ(state.position[i],output[i],0); ASSERT_FLOAT_EQ(sensitivity[i],copied_sensitivity[i],0); }
        block = (nmo_engine_block_t){g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)}; nmo_engine_block_set_waiting_for_messages(&block,target,true); int32_t result = -1; ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(g.f.engine,g.block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); state.position[2] = .9f; nmo_engine_set_joystick(g.f.engine,2,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,target,initial+1,1);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.count,&(int32_t){2},sizeof(int32_t))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); const char *duplicates[] = {"Custom Left","Custom Left"}; joystick_received(&g,target,duplicates,2); read_value(&g.f,g.table,runtime,sizeof(runtime)); ASSERT_TRUE(memcmp(records+80,runtime+80,45) == 0);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.number_value,&(int32_t){0},sizeof(int32_t))); state.position[0] = 0; nmo_engine_set_joystick(g.f.engine,0,&state); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); joystick_received(&g,target,initial+1,1); read_value(&g.f,g.output,output,sizeof(output)); for (size_t i = 0; i < 3; i++) ASSERT_FLOAT_EQ(state.position[i],output[i],0); fixture_end(&g.f);
    }
    ASSERT_TRUE(memcmp(records,((nmo_parameter_state_t *)fixture_state(&source.f,source.table))->buffer_data.data,sizeof(records)) == 0); fixture_end(&source.f); remove(path);
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
REGISTER_TEST(engine_input, tt_joystick_waiter_on_only_rebases_sliders_and_difference_writes_require_relative_slider_one);
REGISTER_TEST(engine_input, tt_joystick_waiter_compacts_all_axes_pov_and_32_buttons_in_native_order);
REGISTER_TEST(engine_input, tt_joystick_waiters_continue_after_disconnect_invalid_slots_and_reconnect_without_reactivation);
REGISTER_TEST(engine_input, tt_joystick_waiter_off_preserves_on_all_values_and_difference_until_explicit_restart);
REGISTER_TEST(engine_input, tt_joystick_waiter_preserves_native_unordered_relative_axis_and_pov_comparisons);
REGISTER_TEST(engine_input, tt_joystick_waiter_compares_relative_deltas_before_rounding_slider_difference_to_float);

REGISTER_TEST(engine_input, tt_joystick_waiter_defaults_repeat_axes_and_buttons_consume_on_and_publish_raw_samples);
REGISTER_TEST(engine_input, tt_joystick_waiter_rebases_sliders_on_on_and_uses_z_sensitivity_for_relative_slider_events);
REGISTER_TEST(engine_input, legacy_tt_joystick_waiters_keep_fixed_outputs_and_on_and_leave_slider_difference_untouched);

REGISTER_TEST(engine_input, loaded_tt_joystick_queries_and_copies_preserve_numbers_and_links_but_read_current_host_devices);
REGISTER_TEST(engine_input, tt_test_for_joystick_routes_missing_devices_to_fallback_before_starting_the_waiter);

REGISTER_TEST(engine_input, tt_test_for_joystick_invalid_numbers_take_no_without_failing_even_when_all_devices_are_attached);
REGISTER_TEST(engine_input, tt_test_for_joystick_observes_one_snapshot_across_host_updates_between_query_nodes);

REGISTER_TEST(engine_input, tt_test_for_joystick_defaults_to_slot_zero_and_requires_reactivation_after_each_query);
REGISTER_TEST(engine_input, tt_test_for_joystick_uses_the_live_number_and_distinguishes_all_four_device_slots);

REGISTER_TEST(engine_input, loaded_tt_joystick_vector_mappers_and_copies_keep_live_input_assignments_and_cached_output);
REGISTER_TEST(engine_input, joystick_waiter_vector_mapper_and_translate_deliver_current_remapped_axes_in_one_frame);

REGISTER_TEST(engine_input, tt_joystick_vector_mapper_later_active_assignments_replace_rather_than_accumulate);
REGISTER_TEST(engine_input, tt_joystick_vector_mapper_ignores_invalid_selectors_and_clears_all_previous_output_components);
REGISTER_TEST(engine_input, tt_joystick_vector_mapper_nonnegative_zero_can_overwrite_an_active_nonzero_mapping);
REGISTER_TEST(engine_input, tt_joystick_vector_mapper_nan_sources_are_inactive_and_infinite_magnitudes_are_preserved);

REGISTER_TEST(engine_input, tt_joystick_vector_mapper_defaults_follow_native_overwrite_order_without_attached_devices);
REGISTER_TEST(engine_input, tt_joystick_vector_mapper_maps_each_signed_source_axis_to_each_output_channel);

REGISTER_TEST(engine_input, loaded_joystick_waiters_and_copies_preserve_versions_layouts_settings_and_relative_history);
REGISTER_TEST(engine_input, joystick_waiter_axis_outputs_drive_translations_using_the_current_analog_position);

REGISTER_TEST(engine_input, joystick_waiter_sparse_masks_keep_axis_pov_and_high_button_output_positions);
REGISTER_TEST(engine_input, joystick_waiter_pov_relative_events_use_exact_changes_and_absolute_mode_repeats_centered_outputs);
REGISTER_TEST(engine_input, joystick_waiter_preserves_negative_nonfinite_thresholds_and_raw_nonfinite_samples);
REGISTER_TEST(engine_input, joystick_waiter_live_slots_masks_and_edited_output_history_take_effect_without_resetting_samples);
REGISTER_TEST(engine_input, joystick_waiter_off_precedes_missing_devices_and_preserves_on_outputs_and_history_until_restart);
REGISTER_TEST(engine_input, joystick_waiter_missing_and_invalid_slots_preserve_on_all_output_values_and_emit_no_events);
REGISTER_TEST(engine_input, joystick_waiter_missing_selected_outputs_do_not_turn_active_commands_into_idle_events);
REGISTER_TEST(engine_input, joystick_waiter_disconnects_stop_execution_and_explicit_reactivation_keeps_relative_history);

REGISTER_TEST(engine_input, legacy_joystick_waiters_keep_fixed_direction_thresholds_and_six_buttons_without_idle);
REGISTER_TEST(engine_input, legacy_joystick_waiters_ignore_v2_masks_and_use_fixed_button_indices);

REGISTER_TEST(engine_input, joystick_waiter_defaults_repeat_selected_axes_buttons_and_idle_while_publishing_all_raw_values);
REGISTER_TEST(engine_input, joystick_waiter_compacts_all_axis_pov_and_32_button_outputs_in_native_order);
REGISTER_TEST(engine_input, joystick_waiter_relative_axes_compare_each_sample_to_the_previous_output_values);

REGISTER_TEST(engine_input, loaded_joystick_mappers_rebind_active_names_and_copies_keep_cached_bindings_and_analog_state);

REGISTER_TEST(engine_input, joystick_mapper_direction_comparisons_preserve_strict_negative_and_nonfinite_thresholds);
REGISTER_TEST(engine_input, joystick_mapper_reads_live_slots_targets_counts_and_cached_ids_without_rebinding_on_edit);
REGISTER_TEST(engine_input, joystick_mapper_nonpositive_counts_still_publish_attached_device_positions);
REGISTER_TEST(engine_input, joystick_mapper_off_wins_before_invalid_state_and_preserves_on_until_explicit_restart);
REGISTER_TEST(engine_input, joystick_mapper_owner_and_device_errors_consume_on_and_preserve_output_without_sending);
REGISTER_TEST(engine_input, joystick_mapper_truncated_bindings_and_wide_counts_fail_before_partial_load_or_delivery);
REGISTER_TEST(engine_input, joystick_mapper_wraps_native_button_shift_counts_and_continues_after_empty_message_names);

REGISTER_TEST(engine_input, joystick_mapper_loads_names_and_repeats_bindings_in_record_order_with_raw_analog_output);
REGISTER_TEST(engine_input, joystick_mapper_supports_all_32_buttons_independently);
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
