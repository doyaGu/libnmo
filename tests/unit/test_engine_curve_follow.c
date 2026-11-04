#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "type/nmo_param_guids.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_FOLLOW NMO_GUID(0x00ed010b,0x00fd010b)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct follow_fixture {
    engine_fixture_t f;
    nmo_object_id_t child, path, points[2], block, done, loop, curve;
    bool frames, legacy;
} follow_fixture_t;

static nmo_object_id_t local(follow_fixture_t *g, size_t index)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,index);
}

static nmo_object_id_t source(follow_fixture_t *g, size_t index)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,index)));
}

static void duration(follow_fixture_t *g, float value)
{
    if (g->legacy) {
        fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,1),(int32_t)value);
        ((nmo_parameter_state_t *)fixture_state(&g->f,source(g,1)))->type_guid = CKPGUID_INT;
    } else fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,1),&value,sizeof(value));
}

static void begin(follow_fixture_t *g, bool frames, bool legacy, bool loop)
{
    memset(g,0,sizeof(*g)); g->frames = frames; g->legacy = legacy; fixture_begin_document(&g->f);
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    nmo_3dentity_state_t *follower = fixture_state(&g->f,g->f.owner_id), *child = fixture_state(&g->f,g->child);
    follower->world_matrix[0] = 2; follower->world_matrix[5] = 3; follower->world_matrix[10] = 4;
    follower->world_matrix[12] = 3; follower->world_matrix[13] = 4; follower->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->f.owner_id); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    g->path = fixture_create(&g->f,NMO_CID_CURVE,"Path"); nmo_curve_state_t *path = fixture_state(&g->f,g->path);
    path->opened = 1; path->control_point_count = 2;
    path->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(g->f.session),2*sizeof(nmo_ref_t),_Alignof(nmo_ref_t)); ASSERT_NOT_NULL(path->control_point_ids);
    for (size_t i = 0; i < 2; i++) {
        g->points[i] = fixture_create(&g->f,NMO_CID_CURVEPOINT,i ? "End" : "Start");
        nmo_curvepoint_state_t *point = fixture_state(&g->f,g->points[i]); ASSERT_NOT_NULL(point);
        point->base.parent = nmo_ref_from_id(g->path); point->base.has_entityndata_chunk = 1;
        point->curve = nmo_ref_from_id(g->path); point->linear = 1;
        nmo_curvepoint_set_position(point,0,0,i ? 20 : 0); path->control_point_ids[i] = nmo_ref_from_id(g->points[i]);
    }
    g->f.script_id = fixture_add_script(&g->f,"Follow Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_FOLLOW,"Curve Follow"); g->done = fixture_block(&g->f,GUID_NOP,"Done"); g->loop = fixture_block(&g->f,GUID_NOP,"Loop");
    ASSERT_TRUE(g->block != 0 && g->done != 0 && g->loop != 0);
    if (frames || legacy) ((nmo_parameterin_state_t *)fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,1)))->type_guid = frames ? CKPGUID_FLOAT : CKPGUID_INT;
    if (legacy) ((nmo_behavior_state_t *)fixture_state(&g->f,g->block))->block_version = 0x10004;
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,local(g,3),frames ? "FALSE" : "TRUE"));
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->f.owner_id);
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->path); duration(g,100);
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,2),&g->curve));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    if (loop) fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,1),BLOCK_IN(&g->f,g->block,1),1);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->done,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,1),BLOCK_IN(&g->f,g->loop,0),0);
}

static void commit(follow_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_FOLLOW));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static float value(follow_fixture_t *g, nmo_object_id_t id)
{
    float result = NAN; read_value(&g->f,id,&result,sizeof(result)); return result;
}

static void position(follow_fixture_t *g, nmo_object_id_t id, float x, float y, float z)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,id,matrix));
    ASSERT_FLOAT_EQ(x,matrix[12],1e-4f); ASSERT_FLOAT_EQ(y,matrix[13],1e-4f); ASSERT_FLOAT_EQ(z,matrix[14],1e-4f);
}

static void save_curve(follow_fixture_t *g, const float points[][2], size_t count)
{
    nmo_arena_t *arena = nmo_session_get_arena(g->f.session); nmo_parameter_state_t *state = fixture_state(&g->f,g->curve);
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

TEST(engine_curve_follow, time_counts_the_first_delta_and_completes_strictly_after_duration)
{
    follow_fixture_t g; begin(&g,false,false,true); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,0,0,5);
    ASSERT_FLOAT_EQ(.25f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f); ASSERT_FLOAT_EQ(25,value(&g,local(&g,0)),0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,75)); position(&g,g.f.owner_id,0,0,20);
    ASSERT_EQ(0u,runs(&g.f,g.done)); ASSERT_EQ(2u,runs(&g.f,g.loop)); ASSERT_FLOAT_EQ(100,value(&g,local(&g,0)),0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1)); ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_EQ(2u,runs(&g.f,g.loop));
    ASSERT_FLOAT_EQ(100,value(&g,local(&g,0)),0); ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(3u,runs(&g.f,g.block));
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix));
    ASSERT_FLOAT_EQ(2,matrix[0],0); ASSERT_FLOAT_EQ(3,matrix[5],0); ASSERT_FLOAT_EQ(4,matrix[10],0);
    ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.f.owner_id))->world_matrix[12],0); fixture_end(&g.f);
}

TEST(engine_curve_follow, completion_updates_both_outputs_but_preserves_the_last_elapsed_cursor)
{
    for (int overshoot = 0; overshoot < 2; overshoot++) {
        follow_fixture_t g; begin(&g,false,false,true);
        const float points[][2] = {{0,.2f},{.5f,.8f},{1,.4f}}; save_curve(&g,points,COUNT(points)); commit(&g);
        if (!overshoot) { ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,0,0,10); }
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,101)); position(&g,g.f.owner_id,0,0,8);
        ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_FLOAT_EQ(overshoot ? 0 : 25,value(&g,local(&g,0)),0);
        ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_FLOAT_EQ(.4f,value(&g,BLOCK_POUT(&g.f,g.block,1)),1e-6f); fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, legacy_integer_time_truncates_each_delta_and_preserves_integer_cursor_bits)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        follow_fixture_t g; begin(&g,false,legacy,true); duration(&g,5); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2.75f)); ASSERT_FLOAT_EQ((legacy ? 2 : 2.75f)/5,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2.75f)); ASSERT_EQ(legacy ? 0u : 1u,runs(&g.f,g.done));
        if (legacy) {
            position(&g,g.f.owner_id,0,0,16); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2.75f));
            int32_t elapsed = 0; read_value(&g.f,local(&g,0),&elapsed,sizeof(elapsed)); ASSERT_EQ(4,elapsed);
            ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
        }
        position(&g,g.f.owner_id,0,0,20); fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, frame_mode_starts_at_path_origin_then_uses_cached_fractional_frame_count)
{
    follow_fixture_t g; begin(&g,true,false,true); duration(&g,3.9f);
    const float points[][2] = {{0,.2f},{.5f,.8f},{1,.4f}}; save_curve(&g,points,COUNT(points)); commit(&g);
    float sentinel = .123f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&sentinel,sizeof(sentinel)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,999)); position(&g,g.f.owner_id,0,0,0);
    ASSERT_FLOAT_EQ(sentinel,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
    float changed = 100; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,1),&changed,sizeof(changed)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.f.owner_id,0,0,16);
    ASSERT_FLOAT_EQ(.5f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,50)); position(&g,g.f.owner_id,0,0,8);
    ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_EQ(2u,runs(&g.f,g.loop)); ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0);
    int32_t remaining = -1; read_value(&g.f,local(&g,0),&remaining,sizeof(remaining)); ASSERT_EQ(0,remaining); fixture_end(&g.f);
}

TEST(engine_curve_follow, single_frame_evaluates_the_curve_endpoint_without_overwriting_cursors)
{
    const float frames[] = {.5f,1,1.9f};
    for (size_t i = 0; i < COUNT(frames); i++) {
        follow_fixture_t g; begin(&g,true,false,true); duration(&g,frames[i]);
        const float points[][2] = {{0,.2f},{1,.4f}}; save_curve(&g,points,COUNT(points));
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,local(&g,0),"77")); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.f.owner_id,0,0,8);
        ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_EQ(0u,runs(&g.f,g.loop));
        int32_t cached = 0; read_value(&g.f,local(&g,0),&cached,sizeof(cached)); ASSERT_EQ(77,cached);
        ASSERT_FLOAT_EQ(1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_FLOAT_EQ(.4f,value(&g,BLOCK_POUT(&g.f,g.block,1)),1e-6f); fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, path_transforms_and_hierarchy_are_resampled_without_changing_orientation_or_scale)
{
    for (int frames = 0; frames < 2; frames++) for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        follow_fixture_t g; begin(&g,frames,false,true); duration(&g,frames ? 3 : 100);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),hierarchy); commit(&g);
        eng_matrix_t path; eng_matrix_from_euler(&path,0,1.57079632679f,0);
        for (int j = 0; j < 3; j++) path.m[2][j] *= 2;
        path.m[3][0] = 100; path.m[3][1] = 50; path.m[3][2] = 10;
        eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.path),&path,false);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,frames ? 100 : 110,50,10);
        position(&g,g.child,hierarchy ? (frames ? 102 : 112) : 5,hierarchy ? 53 : 7,hierarchy ? 14 : 9);
        path.m[3][0] = 200; eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.path),&path,false);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,220,50,10);
        position(&g,g.child,hierarchy ? 222 : 5,hierarchy ? 53 : 7,hierarchy ? 14 : 9);
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.f.owner_id,matrix));
        ASSERT_FLOAT_EQ(2,matrix[0],1e-5f); ASSERT_FLOAT_EQ(3,matrix[5],1e-5f); ASSERT_FLOAT_EQ(4,matrix[10],1e-5f); fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, explicit_triggers_are_required_and_restart_wins_over_loop_in)
{
    for (int frames = 0; frames < 2; frames++) {
        follow_fixture_t g; begin(&g,frames,false,false); duration(&g,frames ? 3 : 100); commit(&g);
        nmo_engine_block_t block = {.engine=g.f.engine,.node=eng_map_get(&g.f.engine->node_map,g.block)};
        nmo_engine_block_activate_input(&block,1,true);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); ASSERT_EQ(1u,runs(&g.f,g.block));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25));
        position(&g,g.f.owner_id,0,0,frames ? 0 : 5); ASSERT_EQ(2u,runs(&g.f,g.block)); ASSERT_EQ(0u,runs(&g.f,g.done)); fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, live_duration_and_path_geometry_control_absolute_positions)
{
    for (int frames = 0; frames < 2; frames++) {
        follow_fixture_t g; begin(&g,frames,false,true); duration(&g,frames ? 3 : 100); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,0,0,frames ? 0 : 5);
        float changed = 200; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,1),&changed,sizeof(changed)));
        eng_vector_t end = {0,0,40}, elsewhere = {-100,-200,-300};
        eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.points[1]),&end,-1,false);
        eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),&elsewhere,-1,false);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,0,0,frames ? 20 : 10);
        ASSERT_FLOAT_EQ(frames ? .5f : .25f,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f); fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, missing_progression_curves_are_only_optional_on_the_initial_frame)
{
    for (int mode = 0; mode < 3; mode++) {
        follow_fixture_t g; begin(&g,mode != 0,false,true); duration(&g,mode == 2 ? 1 : 3);
        nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,2)),0); commit(&g);
        ASSERT_EQ(mode == 0 ? NMO_ERR_VALIDATION_FAILED : NMO_OK,nmo_engine_step(g.f.engine,1));
        if (mode == 0) {
            ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); position(&g,g.f.owner_id,3,4,5);
        } else if (mode == 1) {
            position(&g,g.f.owner_id,0,0,0); ASSERT_EQ(1u,runs(&g.f,g.loop));
            ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,1));
            ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,1))); position(&g,g.f.owner_id,0,0,0);
        } else { position(&g,g.f.owner_id,0,0,20); ASSERT_EQ(1u,runs(&g.f,g.done)); }
        fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, load_selects_duration_type_and_upgrades_only_old_float_time_runtime_versions)
{
    for (int mode = 0; mode < 3; mode++) {
        follow_fixture_t g; begin(&g,mode == 2,mode == 1,true); duration(&g,5);
        nmo_behavior_state_t *saved = fixture_state(&g.f,g.block); saved->block_version = 0x10004; commit(&g);
        nmo_engine_block_t block = {.engine=g.f.engine,.node=eng_map_get(&g.f.engine->node_map,g.block)};
        ASSERT_EQ(mode == 0 ? 0x10005u : 0x10004u,nmo_engine_block_version(&block));
        nmo_guid_t type = mode == 0 ? CKPGUID_TIME : mode == 1 ? CKPGUID_INT : CKPGUID_FLOAT;
        ASSERT_TRUE(nmo_guid_equals(type,nmo_engine_block_input_parameter_type(&block,1))); ASSERT_EQ(0x10004u,saved->block_version);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,2.75f)); position(&g,g.f.owner_id,0,0,mode == 0 ? 11 : mode == 1 ? 8 : 0); fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, zero_and_negative_durations_preserve_native_division_and_completion_order)
{
    const float durations[] = {0,-10};
    for (size_t i = 0; i < COUNT(durations); i++) {
        follow_fixture_t g; begin(&g,false,false,true); duration(&g,durations[i]); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,5)); position(&g,g.f.owner_id,0,0,i ? 0 : 20);
        ASSERT_FLOAT_EQ(i ? -.5f : 1,value(&g,BLOCK_POUT(&g.f,g.block,0)),0); ASSERT_FLOAT_EQ(0,value(&g,local(&g,0)),0);
        ASSERT_EQ(1u,runs(&g.f,g.done)); fixture_end(&g.f);
    }
}

TEST(engine_curve_follow, invalid_owners_paths_and_frame_counts_preserve_native_input_consumption)
{
    for (int frames = 0; frames < 2; frames++) for (int kind = 0; kind < 4; kind++) {
        follow_fixture_t g; begin(&g,frames,false,true);
        if (kind == 0) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        if (kind == 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0);
        if (kind == 3) duration(&g,-1);
        commit(&g); if (kind == 2) nmo_engine_destroy_object(g.f.engine,g.path);
        if (!frames && kind == 3) { fixture_end(&g.f); continue; }
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,1)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(kind == 0 ? NMO_ENGINE_BR_OWNERERROR : NMO_ENGINE_BR_PARAMETERERROR,failure.result);
        ASSERT_EQ(!frames || kind == 0,nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,1)));
        position(&g,g.f.owner_id,3,4,5); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(follow_fixture_t *g, nmo_object_id_t pin, const char *text)
{
    nmo_object_id_t parameter = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,pin,&parameter) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,parameter,text) != NMO_OK) return 0;
    return parameter;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t runtime_local(nmo_engine_t *engine, nmo_object_id_t block, size_t index)
{
    const nmo_behavior_state_t *state = nmo_object_get_state(eng_find_object(engine,block));
    return eng_module_id(eng_module_of(engine,block,NULL),nmo_behavior_ref_array_get_id(&state->local_parameters,index));
}

static void expect_position(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t expected)
{
    eng_vector_t actual = eng_entity_position(engine,eng_entity_index(engine,id),-1);
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

TEST(engine_curve_follow, loaded_and_copied_graphs_keep_cursors_curves_and_paths_independent)
{
    for (int mode = 0; mode < 3; mode++) {
        bool frames = mode == 1; follow_fixture_t g; begin(&g,frames,mode == 2,true);
        nmo_object_id_t parameters[5]; char text[32];
        snprintf(text,sizeof(text),"%u",g.f.owner_id); parameters[0] = saved_input(&g,fixture_target(&g.f,g.block),text);
        snprintf(text,sizeof(text),"%u",g.path); parameters[1] = saved_input(&g,BLOCK_PIN(&g.f,g.block,0),text);
        parameters[2] = saved_input(&g,BLOCK_PIN(&g.f,g.block,1),"4"); parameters[3] = g.curve;
        parameters[4] = saved_input(&g,BLOCK_PIN(&g.f,g.block,3),"TRUE");
        for (size_t i = 0; i < COUNT(parameters); i++) ASSERT_TRUE(parameters[i] != 0);
        const float points[][2] = {{0,0},{.5f,.25f},{1,1}}; save_curve(&g,points,COUNT(points)); fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_curve_follow.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"follow.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Curve Follow",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t original[] = {g.f.owner_id,g.child,g.path,g.points[0],g.points[1],g.f.script_id,g.block,g.done,g.loop};
            nmo_object_id_t ids[14], copies[14] = {0};
            for (size_t i = 0; i < COUNT(original); i++) { ids[i] = eng_module_id(module,original[i]); ASSERT_TRUE(ids[i] != 0); }
            for (size_t i = 0; i < COUNT(parameters); i++) ids[9+i] = eng_module_id(module,parameters[i]);
            ASSERT_TRUE(ids[0] != g.f.owner_id); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,1.75f));
            expect_position(host.engine,ids[0],(eng_vector_t){0,0,frames ? 0 : mode == 2 ? 2.5f : 4.375f});
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            ASSERT_EQ(copies[0],nmo_engine_entity_parent(host.engine,copies[1])); ASSERT_EQ(copies[2],nmo_engine_entity_parent(host.engine,copies[3]));
            for (size_t i = 0; i < 4; i++) {
                const void *a = NULL, *b = NULL; size_t a_size = 0, b_size = 0;
                ASSERT_EQ(NMO_OK,nmo_engine_param_value(host.engine,runtime_local(host.engine,ids[6],i),&a,&a_size,NULL));
                ASSERT_EQ(NMO_OK,nmo_engine_param_value(host.engine,runtime_local(host.engine,copies[6],i),&b,&b_size,NULL));
                ASSERT_EQ(a_size,b_size); ASSERT_EQ(0,memcmp(a,b,a_size));
            }
            eng_matrix_t moved; eng_matrix_from_euler(&moved,0,1.57079632679f,0);
            for (int j = 0; j < 3; j++) moved.m[2][j] *= 2;
            moved.m[3][0] = 100; moved.m[3][1] = 30; moved.m[3][2] = 50;
            eng_entity_set_world_matrix(host.engine,eng_entity_index(host.engine,copies[2]),&moved,false);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,1.75f));
            expect_position(host.engine,ids[0],(eng_vector_t){0,0,frames ? 20.0f/6 : mode == 2 ? 5 : 16.25f});
            expect_position(host.engine,copies[0],(eng_vector_t){100+(frames ? 0 : mode == 2 ? 5 : 8.75f),30,50});
            for (int step = 0; step < 5; step++) ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,1.75f));
            expect_position(host.engine,ids[0],(eng_vector_t){0,0,20}); expect_position(host.engine,copies[0],(eng_vector_t){140,30,50});
            ASSERT_EQ(1u,runs(&host,ids[7])); ASSERT_EQ(1u,runs(&host,copies[7]));
            nmo_engine_destroy_object(host.engine,ids[0]); nmo_engine_destroy_object(host.engine,ids[2]);
            nmo_engine_destroy_object(host.engine,ids[3]); nmo_engine_destroy_object(host.engine,ids[4]);
            moved.m[3][0] = 200; eng_entity_set_world_matrix(host.engine,eng_entity_index(host.engine,copies[2]),&moved,false);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true));
            for (int step = 0; step < 6; step++) ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,1.75f));
            expect_position(host.engine,copies[0],(eng_vector_t){240,30,50}); fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.f.owner_id))->world_matrix[12],0); fixture_end(&g.f); remove(path);
    }
}

TEST(engine_curve_follow, progression_and_value_drive_downstream_blocks_through_the_completion_frame)
{
    follow_fixture_t g; begin(&g,false,false,true);
    nmo_curvepoint_state_t *end = fixture_state(&g.f,g.points[1]); nmo_curvepoint_set_position(end,16,0,12);
    nmo_3dentity_state_t *observer = fixture_state(&g.f,g.child); observer->parent = nmo_ref_from_id(0);
    const float points[][2] = {{0,.2f},{.5f,.8f},{1,.4f}}; save_curve(&g,points,COUNT(points));
    nmo_object_id_t follower = fixture_block(&g.f,NMO_GUID(0x676776d0,0x20d457bd),"Oriented Follower"); ASSERT_TRUE(follower != 0);
    fixture_input_int(&g.f,fixture_target(&g.f,follower),(int32_t)g.child); fixture_input_int(&g.f,BLOCK_PIN(&g.f,follower,0),(int32_t)g.path);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,follower,2),1);
    nmo_object_id_t interpolate = fixture_block(&g.f,NMO_GUID(0x35503950,0x2dde7a65),"Observe Value"); ASSERT_TRUE(interpolate != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,g.block,0),BLOCK_PIN(&g.f,follower,1)));
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,g.block,1),BLOCK_PIN(&g.f,interpolate,0)));
    for (int output = 0; output < 2; output++) {
        fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,output),BLOCK_IN(&g.f,follower,0),0);
        fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,output),BLOCK_IN(&g.f,interpolate,0),0);
    }
    commit(&g); const float values[] = {.5f,.8f,.6f,.4f,.4f};
    for (size_t step = 0; step < COUNT(values); step++) {
        float progression = step < 4 ? (float)(step+1)*.25f : 1;
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); position(&g,g.f.owner_id,16*values[step],0,12*values[step]);
        position(&g,g.child,16*progression,0,12*progression);
        ASSERT_FLOAT_EQ(progression,value(&g,BLOCK_POUT(&g.f,g.block,0)),1e-6f); ASSERT_FLOAT_EQ(values[step],value(&g,BLOCK_POUT(&g.f,g.block,1)),1e-6f);
        ASSERT_FLOAT_EQ((1+values[step])*.5f,value(&g,BLOCK_POUT(&g.f,interpolate,0)),1e-6f);
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.child,matrix));
        ASSERT_FLOAT_EQ(.8f,matrix[8],1e-5f); ASSERT_FLOAT_EQ(0,matrix[9],1e-5f); ASSERT_FLOAT_EQ(.6f,matrix[10],1e-5f);
    }
    ASSERT_EQ(1u,runs(&g.f,g.done)); ASSERT_EQ(4u,runs(&g.f,g.loop)); ASSERT_EQ(5u,runs(&g.f,follower)); ASSERT_EQ(5u,runs(&g.f,interpolate));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,25)); ASSERT_EQ(5u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_curve_follow, time_counts_the_first_delta_and_completes_strictly_after_duration);
REGISTER_TEST(engine_curve_follow, completion_updates_both_outputs_but_preserves_the_last_elapsed_cursor);
REGISTER_TEST(engine_curve_follow, legacy_integer_time_truncates_each_delta_and_preserves_integer_cursor_bits);
REGISTER_TEST(engine_curve_follow, frame_mode_starts_at_path_origin_then_uses_cached_fractional_frame_count);
REGISTER_TEST(engine_curve_follow, single_frame_evaluates_the_curve_endpoint_without_overwriting_cursors);
REGISTER_TEST(engine_curve_follow, path_transforms_and_hierarchy_are_resampled_without_changing_orientation_or_scale);
REGISTER_TEST(engine_curve_follow, explicit_triggers_are_required_and_restart_wins_over_loop_in);
REGISTER_TEST(engine_curve_follow, live_duration_and_path_geometry_control_absolute_positions);
REGISTER_TEST(engine_curve_follow, missing_progression_curves_are_only_optional_on_the_initial_frame);
REGISTER_TEST(engine_curve_follow, load_selects_duration_type_and_upgrades_only_old_float_time_runtime_versions);
REGISTER_TEST(engine_curve_follow, zero_and_negative_durations_preserve_native_division_and_completion_order);
REGISTER_TEST(engine_curve_follow, invalid_owners_paths_and_frame_counts_preserve_native_input_consumption);
REGISTER_TEST(engine_curve_follow, loaded_and_copied_graphs_keep_cursors_curves_and_paths_independent);
REGISTER_TEST(engine_curve_follow, progression_and_value_drive_downstream_blocks_through_the_completion_frame);
TEST_MAIN_END()
