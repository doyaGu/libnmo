#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "type/nmo_param_guids.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_MOVE NMO_GUID(0x010d010b,0x011d010b)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct move_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, child, reference, block, done, loop, curve;
    int offset;
    bool frames, legacy;
} move_fixture_t;

static nmo_object_id_t local(move_fixture_t *g, size_t index)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,index);
}

static nmo_object_id_t source(move_fixture_t *g, size_t index)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,index)));
}

static void duration(move_fixture_t *g, float value)
{
    nmo_object_id_t pin = BLOCK_PIN(&g->f,g->block,1+g->offset);
    if (g->frames || g->legacy) fixture_input_int(&g->f,pin,(int32_t)value);
    else fixture_input_value(&g->f,pin,&value,sizeof(value));
}

static void begin(move_fixture_t *g, bool frames, bool legacy, bool old_layout)
{
    memset(g,0,sizeof(*g)); g->frames = frames; g->legacy = legacy; g->offset = !old_layout;
    fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent");
    g->f.owner_id = fixture_create(&g->f,NMO_CID_3DENTITY,"Mover");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    g->reference = fixture_create(&g->f,NMO_CID_3DENTITY,"Reference");
    nmo_3dentity_state_t *mover = fixture_state(&g->f,g->f.owner_id), *child = fixture_state(&g->f,g->child);
    ASSERT_NOT_NULL(mover); ASSERT_NOT_NULL(child);
    mover->parent = nmo_ref_from_id(g->parent); mover->has_entityndata_chunk = 1;
    mover->world_matrix[0] = 2; mover->world_matrix[5] = 3; mover->world_matrix[10] = 4;
    mover->world_matrix[12] = 10; mover->world_matrix[13] = 20; mover->world_matrix[14] = 30;
    child->parent = nmo_ref_from_id(g->f.owner_id); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 11; child->world_matrix[13] = 22; child->world_matrix[14] = 33;
    nmo_3dentity_state_t *ref = fixture_state(&g->f,g->reference); ASSERT_NOT_NULL(ref);
    ref->world_matrix[12] = 100; ref->world_matrix[13] = 200; ref->world_matrix[14] = 300;
    g->f.script_id = fixture_add_script(&g->f,"Move Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_MOVE,"Move To"); g->done = fixture_block(&g->f,GUID_NOP,"Done"); g->loop = fixture_block(&g->f,GUID_NOP,"Loop");
    ASSERT_TRUE(g->block != 0 && g->done != 0 && g->loop != 0);
    if (old_layout) ASSERT_EQ(NMO_OK,nmo_script_edit_remove_parameter(g->f.tx,BLOCK_PIN(&g->f,g->block,1),true));
    if (frames || legacy) ((nmo_parameterin_state_t *)fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,1+g->offset)))->type_guid = CKPGUID_INT;
    if (legacy) ((nmo_behavior_state_t *)fixture_state(&g->f,g->block))->block_version = 0x10004;
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,local(g,4),frames ? "FALSE" : "TRUE"));
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->f.owner_id);
    eng_vector_t goal = {30,40,50}; fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,0),&goal,sizeof(goal));
    if (g->offset) fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,1),0);
    duration(g,100);
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,2+g->offset),&g->curve));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,1),BLOCK_IN(&g->f,g->block,1),1);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->done,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,1),BLOCK_IN(&g->f,g->loop,0),0);
}

static void commit(move_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_MOVE));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static float value(move_fixture_t *g, nmo_object_id_t id)
{
    float result = NAN; read_value(&g->f,id,&result,sizeof(result)); return result;
}

static void position(move_fixture_t *g, nmo_object_id_t id, float x, float y, float z)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,id,matrix));
    ASSERT_FLOAT_EQ(x,matrix[12],1e-4f); ASSERT_FLOAT_EQ(y,matrix[13],1e-4f); ASSERT_FLOAT_EQ(z,matrix[14],1e-4f);
}

static void set_position(move_fixture_t *g, nmo_object_id_t id, eng_vector_t p)
{
    eng_entity_set_position(g->f.engine,eng_entity_index(g->f.engine,id),&p,-1,false);
}

static void save_curve(move_fixture_t *g, const float points[][2], size_t count)
{
    nmo_arena_t *arena = nmo_session_get_arena(g->f.session);
    nmo_parameter_state_t *state = fixture_state(&g->f,g->curve);
    nmo_chunk_t *curve = nmo_chunk_create(arena), *saved = nmo_chunk_create(arena); ASSERT_NOT_NULL(curve); ASSERT_NOT_NULL(saved);
    nmo_chunk_start_write(curve); nmo_chunk_set_data_version(curve,7); nmo_chunk_write_identifier(curve,0x100);
    nmo_chunk_write_int(curve,(int32_t)count);
    for (size_t i = 0; i < count; i++) nmo_chunk_write_int(curve,0);
    nmo_chunk_write_float(curve,0);
    for (size_t i = 0; i < count; i++) {
        nmo_chunk_write_dword(curve,0x10000002u);
        const float data[] = {points[i][0],points[i][1],0,0,0,0,0,0,0,0,0};
        for (size_t j = 0; j < COUNT(data); j++) nmo_chunk_write_float(curve,data[j]);
    }
    nmo_chunk_close(curve); nmo_chunk_start_write(saved); nmo_chunk_write_identifier(saved,0x0e);
    nmo_chunk_write_sub_chunk(saved,curve); nmo_chunk_close(saved);
    nmo_array_dispose(&state->buffer_data); state->mode = CKPARAM_MODE_SUBCHUNK; state->subchunk = saved; state->has_state = true;
}

TEST(engine_move_to, time_motion_counts_the_first_delta_and_finishes_only_after_the_duration)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        move_fixture_t g; begin(&g,false,false,false);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),hierarchy); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,15,25,35);
        ASSERT_FLOAT_EQ(.25f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f); ASSERT_EQ(0u,runs(&g.f,g.done));
        position(&g,g.child,hierarchy ? 16 : 11,hierarchy ? 27 : 22,hierarchy ? 38 : 33);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,50));
        position(&g,g.f.owner_id,30,40,50); ASSERT_EQ(0u,runs(&g.f,g.done)); ASSERT_EQ(3u,runs(&g.f,g.loop));
        ASSERT_FLOAT_EQ(100,value(&g,local(&g,0)),0); ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1)); ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_EQ(3u,runs(&g.f,g.loop));
        ASSERT_FLOAT_EQ(100,value(&g,local(&g,0)),0); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1)); ASSERT_EQ(4u,runs(&g.f,g.block));
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix));
        ASSERT_FLOAT_EQ(2,matrix[0],0); ASSERT_FLOAT_EQ(3,matrix[5],0); ASSERT_FLOAT_EQ(4,matrix[10],0);
        position(&g,g.parent,0,0,0); ASSERT_FLOAT_EQ(10,((nmo_3dentity_state_t *)fixture_state(&g.f,g.f.owner_id))->world_matrix[12],0);
        fixture_end(&g.f);
    }
}

TEST(engine_move_to, completion_moves_to_the_curve_endpoint_without_overwriting_loop_outputs)
{
    move_fixture_t g; begin(&g,false,false,false);
    const float points[][2] = {{0,.2f},{.5f,.8f},{1,.4f}}; save_curve(&g,points,COUNT(points)); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,20,30,40);
    ASSERT_FLOAT_EQ(.25f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f); ASSERT_FLOAT_EQ(.5f,value(&g,BLOCK_POUT(&g.f,g.block,1)),1e-6f);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); position(&g,g.f.owner_id,18,28,38);
    ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_FLOAT_EQ(.25f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f);
    ASSERT_FLOAT_EQ(.5f,value(&g,BLOCK_POUT(&g.f,g.block,1)),1e-6f); ASSERT_FLOAT_EQ(25,value(&g,local(&g,0)),0); fixture_end(&g.f);
}

TEST(engine_move_to, time_mode_resamples_destination_points_and_reference_transforms_from_the_original_position)
{
    move_fixture_t g; begin(&g,false,false,false);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)g.reference);
    eng_vector_t point = {2,0,0}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&point,sizeof(point)); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,33,65,97.5f);
    eng_matrix_t ref; eng_matrix_from_euler(&ref,0,0,1.57079632679f);
    for (int column = 0; column < 3; column++) ref.m[0][column] *= 2;
    ref.m[3][0] = 200; ref.m[3][1] = 100; ref.m[3][2] = 50;
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.reference),&ref,false);
    set_position(&g,g.f.owner_id,(eng_vector_t){-500,-500,-500});
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,105,62,40);
    point = (eng_vector_t){0,0,0}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,0),&point,sizeof(point)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,152.5f,80,45);
    fixture_end(&g.f);
}

TEST(engine_move_to, frame_mode_initializes_without_motion_then_uses_curve_differences_and_cached_frame_counts)
{
    move_fixture_t g; begin(&g,true,false,false); duration(&g,2);
    const float points[][2] = {{0,.2f},{.5f,.8f},{1,.4f}}; save_curve(&g,points,COUNT(points)); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,999)); position(&g,g.f.owner_id,10,20,30);
    ASSERT_FLOAT_EQ(0,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_FLOAT_EQ(.2f,value(&g,BLOCK_POUT(&g.f,g.block,1)),1e-6f);
    int32_t changed = 999; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,2),&changed,sizeof(changed)));
    eng_vector_t point = {-100,-100,-100}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,0),&point,sizeof(point)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.f.owner_id,22,32,42);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,5)); position(&g,g.f.owner_id,14,24,34);
    ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_EQ(2u,runs(&g.f,g.loop)); ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
    ASSERT_FLOAT_EQ(.4f,value(&g,BLOCK_POUT(&g.f,g.block,1)),1e-6f); fixture_end(&g.f);
}

TEST(engine_move_to, frame_motion_ignores_reference_translation_but_uses_its_live_basis)
{
    move_fixture_t g; begin(&g,true,false,false); duration(&g,2);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)g.reference);
    eng_vector_t zero = {0}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&zero,sizeof(zero)); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); set_position(&g,g.reference,(eng_vector_t){1000,2000,3000});
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.f.owner_id,55,110,165);
    eng_matrix_t ref; eng_matrix_from_euler(&ref,0,0,1.57079632679f); ref.m[3][0] = 1000;
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.reference),&ref,false);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.f.owner_id,-35,155,300);
    ASSERT_EQ(1u,runs(&g.f,g.done)); fixture_end(&g.f);
}

TEST(engine_move_to, nonpositive_frame_counts_snap_once_without_replacing_cached_locals)
{
    const int32_t counts[] = {0,-2};
    for (size_t i = 0; i < COUNT(counts); i++) {
        move_fixture_t g; begin(&g,true,false,false); duration(&g,(float)counts[i]);
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,local(&g,0),"77"));
        const float points[][2] = {{0,.2f},{1,.4f}}; save_curve(&g,points,COUNT(points)); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.f.owner_id,30,40,50);
        ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_EQ(0u,runs(&g.f,g.loop));
        int32_t cached = 0; read_value(&g.f,local(&g,0),&cached,sizeof(cached)); ASSERT_EQ(77,cached);
        ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_FLOAT_EQ(.4f,value(&g,BLOCK_POUT(&g.f,g.block,1)),1e-6f);
        fixture_end(&g.f);
    }
}

TEST(engine_move_to, legacy_integer_clocks_truncate_each_delta_and_support_layouts_without_a_reference)
{
    for (int legacy = 0; legacy < 2; legacy++) for (int old_layout = 0; old_layout < 2; old_layout++) {
        move_fixture_t g; begin(&g,false,legacy,old_layout); duration(&g,5); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2.75f));
        float elapsed = legacy ? 2 : 2.75f;
        ASSERT_FLOAT_EQ(elapsed/5,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f);
        position(&g,g.f.owner_id,10+20*elapsed/5,20+20*elapsed/5,30+20*elapsed/5);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2.75f));
        ASSERT_EQ(legacy ? 0u : 1u,runs(&g.f,g.done));
        if (legacy) {
            ASSERT_FLOAT_EQ(.8f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f);
            ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2.75f)); ASSERT_EQ(1u,runs(&g.f,g.done));
            int32_t cached = 0; read_value(&g.f,local(&g,0),&cached,sizeof(cached)); ASSERT_EQ(4,cached);
        }
        fixture_end(&g.f);
    }
}

TEST(engine_move_to, zero_and_negative_time_durations_keep_native_division_and_completion_order)
{
    const float durations[] = {0,-10};
    for (size_t i = 0; i < COUNT(durations); i++) {
        move_fixture_t g; begin(&g,false,false,false); duration(&g,durations[i]); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,5)); ASSERT_EQ(1u,runs(&g.f,g.done));
        position(&g,g.f.owner_id,i == 0 ? 30 : 10,i == 0 ? 40 : 20,i == 0 ? 50 : 30);
        ASSERT_FLOAT_EQ(0,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_FLOAT_EQ(0,value(&g,local(&g,0)),0); fixture_end(&g.f);
    }
}

TEST(engine_move_to, missing_owners_and_curves_fail_before_consuming_inputs_or_activating_outputs)
{
    for (int frames = 0; frames < 2; frames++) for (int missing_owner = 0; missing_owner < 2; missing_owner++) {
        move_fixture_t g; begin(&g,frames,false,false);
        if (missing_owner) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        else ASSERT_EQ(NMO_OK,nmo_script_edit_disconnect_parameter(g.f.tx,BLOCK_PIN(&g.f,g.block,2+g.offset)));
        commit(&g);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,10));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(missing_owner ? NMO_ENGINE_BR_OWNERERROR : NMO_ENGINE_BR_PARAMETERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.done)); ASSERT_EQ(0u,runs(&g.f,g.loop));
        position(&g,g.f.owner_id,10,20,30); fixture_end(&g.f);
    }
}

TEST(engine_move_to, restarting_captures_the_current_position_and_in_wins_without_consuming_loop_in)
{
    move_fixture_t g; begin(&g,false,false,false); commit(&g);
    nmo_engine_block_t block = {.engine=g.f.engine,.node=eng_map_get(&g.f.engine->node_map,g.block)};
    nmo_engine_block_activate_input(&block,1,true);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,15,25,35);
    ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1)));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,18.75f,28.75f,38.75f);
    ASSERT_FLOAT_EQ(.25f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f);
    eng_vector_t origin; read_value(&g.f,local(&g,3),&origin,sizeof(origin)); ASSERT_FLOAT_EQ(15,origin.x,0);
    ASSERT_EQ(0u,runs(&g.f,g.done)); fixture_end(&g.f);
}

TEST(engine_move_to, old_float_time_versions_upgrade_only_runtime_metadata_and_frames_support_old_layouts)
{
    for (int frames = 0; frames < 2; frames++) {
        move_fixture_t g; begin(&g,frames,false,true); duration(&g,2);
        nmo_behavior_state_t *saved = fixture_state(&g.f,g.block); saved->block_version = 0x10004; commit(&g);
        nmo_engine_block_t block = {.engine=g.f.engine,.node=eng_map_get(&g.f.engine->node_map,g.block)};
        ASSERT_EQ(frames ? 0x10004u : 0x10005u,nmo_engine_block_version(&block));
        ASSERT_TRUE(nmo_guid_equals(frames ? CKPGUID_INT : CKPGUID_TIME,nmo_engine_block_input_parameter_type(&block,1)));
        ASSERT_EQ(0x10004u,saved->block_version);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1)); position(&g,g.f.owner_id,frames ? 10 : 20,frames ? 20 : 30,frames ? 30 : 40);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1));
        if (frames) { ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1)); ASSERT_EQ(1u,runs(&g.f,g.done)); }
        else ASSERT_EQ(0u,runs(&g.f,g.done));
        fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_move_to, time_motion_counts_the_first_delta_and_finishes_only_after_the_duration);
REGISTER_TEST(engine_move_to, completion_moves_to_the_curve_endpoint_without_overwriting_loop_outputs);
REGISTER_TEST(engine_move_to, time_mode_resamples_destination_points_and_reference_transforms_from_the_original_position);
REGISTER_TEST(engine_move_to, frame_mode_initializes_without_motion_then_uses_curve_differences_and_cached_frame_counts);
REGISTER_TEST(engine_move_to, frame_motion_ignores_reference_translation_but_uses_its_live_basis);
REGISTER_TEST(engine_move_to, nonpositive_frame_counts_snap_once_without_replacing_cached_locals);
REGISTER_TEST(engine_move_to, legacy_integer_clocks_truncate_each_delta_and_support_layouts_without_a_reference);
REGISTER_TEST(engine_move_to, zero_and_negative_time_durations_keep_native_division_and_completion_order);
REGISTER_TEST(engine_move_to, missing_owners_and_curves_fail_before_consuming_inputs_or_activating_outputs);
REGISTER_TEST(engine_move_to, restarting_captures_the_current_position_and_in_wins_without_consuming_loop_in);
REGISTER_TEST(engine_move_to, old_float_time_versions_upgrade_only_runtime_metadata_and_frames_support_old_layouts);
TEST_MAIN_END()
