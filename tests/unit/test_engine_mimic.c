#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "type/nmo_operations.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_MIMIC NMO_GUID(0x12db7b06,0x74c514c3)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct mimic_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, source_parent, source, child, block, out;
} mimic_fixture_t;

static nmo_object_id_t entity(mimic_fixture_t *g, const char *name, nmo_object_id_t parent,
    eng_vector_t position, eng_vector_t scale, float angle)
{
    nmo_object_id_t id = fixture_create(&g->f,NMO_CID_3DENTITY,name);
    nmo_3dentity_state_t *state = fixture_state(&g->f,id);
    if (state == NULL) return 0;
    state->parent = nmo_ref_from_id(parent); state->has_entityndata_chunk = 1;
    eng_matrix_t matrix; eng_matrix_from_euler(&matrix,0,0,angle);
    const float lengths[] = {scale.x,scale.y,scale.z};
    for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++) matrix.m[row][column] *= lengths[row];
    matrix.m[3][0] = position.x; matrix.m[3][1] = position.y; matrix.m[3][2] = position.z;
    memcpy(state->world_matrix,&matrix,sizeof(matrix)); return id;
}

static nmo_object_id_t local(mimic_fixture_t *g)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,0);
}

static void begin(mimic_fixture_t *g, bool time)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = entity(g,"Parent",0,(eng_vector_t){0},(eng_vector_t){1,1,1},0);
    g->source_parent = entity(g,"Source Parent",0,(eng_vector_t){0},(eng_vector_t){1,1,1},0);
    g->f.owner_id = entity(g,"Follower",g->parent,(eng_vector_t){10,20,30},(eng_vector_t){2,3,4},0);
    g->child = entity(g,"Child",g->f.owner_id,(eng_vector_t){12,23,34},(eng_vector_t){1,1,1},0);
    g->source = entity(g,"Source",g->source_parent,(eng_vector_t){30,60,90},(eng_vector_t){6,7,8},PI_F*.5f);
    ASSERT_TRUE(g->f.owner_id != 0 && g->source != 0 && g->child != 0);
    g->f.script_id = fixture_add_script(&g->f,"Mimic Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_MIMIC,"Mimic"); g->out = fixture_block(&g->f,GUID_NOP,"Out"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->f.owner_id);
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->source);
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,local(g),time ? "TRUE" : "FALSE"));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void settings(mimic_fixture_t *g, uint32_t mask, float position, float orientation, float scale)
{
    const float speeds[] = {position,orientation,scale};
    for (size_t i = 0; i < COUNT(speeds); i++) {
        fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,1+2*i),(mask & (1u<<i)) != 0);
        fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,2+2*i),&speeds[i],sizeof(float));
    }
}

static void commit(mimic_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_MIMIC));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void position(mimic_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    eng_vector_t actual = eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1);
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-5f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-5f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-5f);
}

static void pose(mimic_fixture_t *g, float angle, eng_vector_t scale)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,g->f.owner_id,matrix));
    ASSERT_FLOAT_EQ(scale.x*cosf(angle),matrix[0],1e-5f); ASSERT_FLOAT_EQ(scale.x*sinf(angle),matrix[1],1e-5f);
    ASSERT_FLOAT_EQ(-scale.y*sinf(angle),matrix[4],1e-5f); ASSERT_FLOAT_EQ(scale.y*cosf(angle),matrix[5],1e-5f); ASSERT_FLOAT_EQ(scale.z,matrix[10],1e-5f);
}

TEST(engine_mimic, position_orientation_and_scale_are_independently_enabled_and_weighted)
{
    for (uint32_t mask = 0; mask < 8; mask++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,mask,.25f,.5f,.5f); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100));
        position(&g,g.f.owner_id,(mask & 1) ? (eng_vector_t){15,30,45} : (eng_vector_t){10,20,30});
        pose(&g,(mask & 2) ? PI_F*.25f : 0,(mask & 4) ? (eng_vector_t){4,5,6} : (eng_vector_t){2,3,4});
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        float source[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.source,source));
        ASSERT_EQ(0,memcmp(source,((nmo_3dentity_state_t *)fixture_state(&g.f,g.source))->world_matrix,sizeof(source)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block));
        ASSERT_FLOAT_EQ(10,((nmo_3dentity_state_t *)fixture_state(&g.f,g.f.owner_id))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_mimic, time_scaling_caps_each_weight_only_above_one_and_preserves_negative_values)
{
    mimic_fixture_t g; begin(&g,true); settings(&g,7,2,.5f,-.5f); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.f.owner_id,(eng_vector_t){30,60,90});
    pose(&g,PI_F*.5f*.35f,(eng_vector_t){.6f,1.6f,2.6f}); fixture_end(&g.f);
    const float speeds[] = {2,-.5f};
    for (size_t i = 0; i < COUNT(speeds); i++) {
        begin(&g,false); settings(&g,1,speeds[i],0,0); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.f.owner_id,i == 0 ? (eng_vector_t){50,100,150} : (eng_vector_t){0,0,0}); fixture_end(&g.f);
    }
}

TEST(engine_mimic, orientation_uses_the_short_arc_extrapolation_and_normalized_close_angle_blends)
{
    const float factors[] = {.5f,1.5f,-.5f};
    for (size_t i = 0; i < COUNT(factors); i++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,2,0,factors[i],0);
        nmo_3dentity_state_t *follower = fixture_state(&g.f,g.f.owner_id), *source = fixture_state(&g.f,g.source);
        eng_matrix_t a,b; eng_matrix_from_euler(&a,0,0,170*PI_F/180); eng_matrix_from_euler(&b,0,0,-170*PI_F/180);
        for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++) {
            follower->world_matrix[4*row+column] = a.m[row][column]*(row+2);
            source->world_matrix[4*row+column] = b.m[row][column]*(row+6);
        }
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        pose(&g,(170+20*factors[i])*PI_F/180,(eng_vector_t){2,3,4}); fixture_end(&g.f);
    }
    mimic_fixture_t g; begin(&g,false); settings(&g,2,0,.25f,0);
    nmo_3dentity_state_t *source = fixture_state(&g.f,g.source); eng_matrix_t near_turn; eng_matrix_from_euler(&near_turn,0,0,.1f);
    memcpy(source->world_matrix,&near_turn,sizeof(near_turn)); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float angle = 2*atan2f(.25f*sinf(.05f),.75f+.25f*cosf(.05f));
    pose(&g,angle,(eng_vector_t){2,3,4}); fixture_end(&g.f);
}

TEST(engine_mimic, scale_copy_and_orientation_preservation_use_local_scale_under_different_parents)
{
    for (int orientation = 0; orientation < 2; orientation++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,orientation ? 2 : 4,0,1,1);
        nmo_3dentity_state_t *parent = fixture_state(&g.f,g.parent), *source_parent = fixture_state(&g.f,g.source_parent);
        parent->world_matrix[0] = 2; parent->world_matrix[5] = 3; parent->world_matrix[10] = 4;
        source_parent->world_matrix[0] = 5; source_parent->world_matrix[5] = 6; source_parent->world_matrix[10] = 7;
        nmo_3dentity_state_t *follower = fixture_state(&g.f,g.f.owner_id); follower->world_matrix[0] = 4; follower->world_matrix[5] = 9; follower->world_matrix[10] = 16;
        if (!orientation) {
            nmo_3dentity_state_t *source = fixture_state(&g.f,g.source);
            memset(source->world_matrix,0,sizeof(source->world_matrix)); source->world_matrix[0] = 30; source->world_matrix[5] = 42; source->world_matrix[10] = 56; source->world_matrix[15] = 1;
        }
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        eng_vector_t local_scale = eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),true);
        ASSERT_FLOAT_EQ(orientation ? 2 : 6,local_scale.x,1e-5f); ASSERT_FLOAT_EQ(orientation ? 3 : 7,local_scale.y,1e-5f); ASSERT_FLOAT_EQ(orientation ? 4 : 8,local_scale.z,1e-5f);
        pose(&g,orientation ? PI_F*.5f : 0,orientation ? (eng_vector_t){6,6,16} : (eng_vector_t){12,21,32});
        position(&g,g.f.owner_id,(eng_vector_t){10,20,30}); fixture_end(&g.f);
    }
}

TEST(engine_mimic, orientation_scale_restoration_moves_children_even_when_hierarchy_is_disabled)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,2,0,0,0);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,7),hierarchy); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); pose(&g,0,(eng_vector_t){2,3,4});
        position(&g,g.child,hierarchy ? (eng_vector_t){12,23,34} : (eng_vector_t){14,29,46}); fixture_end(&g.f);
    }
}

TEST(engine_mimic, hierarchy_changes_propagate_position_and_scale_while_sampling_live_descendant_sources)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        mimic_fixture_t g; begin(&g,false); settings(&g,1,1,0,0);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,7),hierarchy);
        ((nmo_3dentity_state_t *)fixture_state(&g.f,g.source))->parent = nmo_ref_from_id(g.f.owner_id);
        fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,0),BLOCK_IN(&g.f,g.block,0),1); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.f.owner_id,(eng_vector_t){30,60,90});
        position(&g,g.source,hierarchy ? (eng_vector_t){50,100,150} : (eng_vector_t){30,60,90});
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.f.owner_id,hierarchy ? (eng_vector_t){50,100,150} : (eng_vector_t){30,60,90}); ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_mimic, absent_sources_are_noops_and_absent_targets_report_after_setting_io)
{
    for (int kind = 0; kind < 4; kind++) {
        mimic_fixture_t g; begin(&g,false);
        if (kind == 0) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),0);
        if (kind == 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,0),(int32_t)g.f.script_id);
        if (kind == 3) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        commit(&g); if (kind == 2) nmo_engine_destroy_object(g.f.engine,g.source);
        ASSERT_EQ(kind == 3 ? NMO_ERR_VALIDATION_FAILED : NMO_OK,nmo_engine_step(g.f.engine,10));
        if (kind == 3) {
            nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
            ASSERT_EQ(g.block,failure.behavior_id); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
            ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        } else ASSERT_EQ(1u,runs(&g.f,g.out));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        position(&g,g.f.owner_id,(eng_vector_t){10,20,30}); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(mimic_fixture_t *g, nmo_object_id_t pin, const char *text)
{
    nmo_object_id_t source = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,pin,&source) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,source,text) != NMO_OK) return 0;
    return source;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static void place(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t position, eng_vector_t scale, float angle)
{
    eng_matrix_t matrix; eng_matrix_from_euler(&matrix,0,0,angle);
    const float lengths[] = {scale.x,scale.y,scale.z};
    for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++) matrix.m[row][column] *= lengths[row];
    matrix.m[3][0] = position.x; matrix.m[3][1] = position.y; matrix.m[3][2] = position.z;
    eng_entity_set_world_matrix(engine,eng_entity_index(engine,id),&matrix,false);
}

static void expect_pose(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t position, eng_vector_t scale, float angle)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,matrix));
    ASSERT_FLOAT_EQ(position.x,matrix[12],1e-4f); ASSERT_FLOAT_EQ(position.y,matrix[13],1e-4f); ASSERT_FLOAT_EQ(position.z,matrix[14],1e-4f);
    ASSERT_FLOAT_EQ(scale.x*cosf(angle),matrix[0],1e-4f); ASSERT_FLOAT_EQ(scale.x*sinf(angle),matrix[1],1e-4f);
    ASSERT_FLOAT_EQ(-scale.y*sinf(angle),matrix[4],1e-4f); ASSERT_FLOAT_EQ(scale.y*cosf(angle),matrix[5],1e-4f); ASSERT_FLOAT_EQ(scale.z,matrix[10],1e-4f);
}

TEST(engine_mimic, loaded_and_copied_graphs_keep_live_sources_speeds_and_targets_independent)
{
    for (int time = 0; time < 2; time++) {
        mimic_fixture_t g; begin(&g,time); nmo_object_id_t sources[9]; char text[32];
        snprintf(text,sizeof(text),"%u",g.f.owner_id); sources[0] = saved_input(&g,fixture_target(&g.f,g.block),text);
        snprintf(text,sizeof(text),"%u",g.source); sources[1] = saved_input(&g,BLOCK_PIN(&g.f,g.block,0),text);
        for (size_t i = 1; i < 8; i++) sources[1+i] = saved_input(&g,BLOCK_PIN(&g.f,g.block,i),i%2 || i == 7 ? "TRUE" : "0.5");
        for (size_t i = 0; i < COUNT(sources); i++) ASSERT_TRUE(sources[i] != 0);
        fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_mimic.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
            fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"mimic.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t ids[17], copies[17] = {0};
            const char *names[] = {"Parent","Follower","Child","Source Parent","Source","Mimic Script","Mimic","Out"};
            for (size_t i = 0; i < COUNT(names); i++) {
                ids[i] = nmo_engine_object_named(host.engine,names[i],i < 5 ? NMO_CID_3DENTITY : NMO_CID_BEHAVIOR); ASSERT_TRUE(ids[i] != 0);
            }
            ASSERT_TRUE(ids[1] != g.f.owner_id);
            const eng_module_t *module = eng_module_of(host.engine,ids[6],NULL);
            for (size_t i = 0; i < COUNT(sources); i++) ids[8+i] = eng_module_id(module,sources[i]);
            float amount = time ? .35f : .5f;
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10));
            eng_vector_t initial = {10+20*amount,20+40*amount,30+60*amount}, scale = {2+4*amount,3+4*amount,4+4*amount};
            float angle = PI_F*.5f*amount;
            expect_pose(host.engine,ids[1],initial,scale,angle);
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            place(host.engine,copies[4],(eng_vector_t){50,80,110},(eng_vector_t){10,11,12},0);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10));
            eng_vector_t copied_position = {initial.x+(50-initial.x)*amount,initial.y+(80-initial.y)*amount,initial.z+(110-initial.z)*amount};
            eng_vector_t copied_scale = {scale.x+(10-scale.x)*amount,scale.y+(11-scale.y)*amount,scale.z+(12-scale.z)*amount};
            expect_pose(host.engine,copies[1],copied_position,copied_scale,angle*(1-amount)); expect_pose(host.engine,ids[1],initial,scale,angle);
            expect_pose(host.engine,ids[4],(eng_vector_t){30,60,90},(eng_vector_t){6,7,8},PI_F*.5f);
            nmo_engine_destroy_object(host.engine,ids[1]); nmo_engine_destroy_object(host.engine,ids[4]);
            place(host.engine,copies[4],(eng_vector_t){70,100,130},(eng_vector_t){14,15,16},PI_F*.5f);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10));
            eng_vector_t next_position = {copied_position.x+(70-copied_position.x)*amount,copied_position.y+(100-copied_position.y)*amount,copied_position.z+(130-copied_position.z)*amount};
            eng_vector_t next_scale = {copied_scale.x+(14-copied_scale.x)*amount,copied_scale.y+(15-copied_scale.y)*amount,copied_scale.z+(16-copied_scale.z)*amount};
            float previous_angle = angle*(1-amount);
            expect_pose(host.engine,copies[1],next_position,next_scale,previous_angle+(PI_F*.5f-previous_angle)*amount);
            fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(10,((nmo_3dentity_state_t *)fixture_state(&g.f,g.f.owner_id))->world_matrix[12],0);
        fixture_end(&g.f); remove(path);
    }
}

TEST(engine_mimic, move_to_outputs_drive_full_pose_following_through_the_final_completion)
{
    mimic_fixture_t g; begin(&g,false); settings(&g,7,1,1,1);
    const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
    nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
    nmo_object_id_t move = fixture_block(&g.f,NMO_GUID(0x010d010b,0x011d010b),"Move Source"); ASSERT_TRUE(move != 0);
    fixture_input_int(&g.f,fixture_target(&g.f,move),(int32_t)g.source);
    eng_vector_t goal = {70,100,130}; float duration = 40;
    fixture_input_value(&g.f,BLOCK_PIN(&g.f,move,0),&goal,sizeof(goal)); fixture_input_value(&g.f,BLOCK_PIN(&g.f,move,2),&duration,sizeof(duration));
    nmo_object_id_t curve = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,move,3),&curve));
    fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,move,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,move,1),BLOCK_IN(&g.f,move,1),1);
    fixture_link(&g.f,BLOCK_OUT(&g.f,move,1),BLOCK_IN(&g.f,g.block,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,move,0),BLOCK_IN(&g.f,g.block,0),0); commit(&g);
    for (int step = 1; step <= 5; step++) {
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10));
        float progression = step < 4 ? step*.25f : 1;
        eng_vector_t expected = {30+40*progression,60+40*progression,90+40*progression};
        position(&g,g.f.owner_id,expected); pose(&g,PI_F*.5f,(eng_vector_t){6,7,8});
        position(&g,g.child,(eng_vector_t){expected.x-7,expected.y+6,expected.z+8});
    }
    ASSERT_EQ(5u,runs(&g.f,g.block)); ASSERT_EQ(5u,runs(&g.f,g.out));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(5u,runs(&g.f,g.block)); fixture_end(&g.f);
}

static void count_operations(const nmo_engine_event_t *event, void *data)
{
    if (event->kind == NMO_ENGINE_EVENT_OPERATION) (*(size_t *)data)++;
}

TEST(engine_mimic, invalid_targets_do_not_evaluate_operations_connected_to_the_source_input)
{
    for (int valid = 0; valid < 2; valid++) {
        mimic_fixture_t g; begin(&g,false);
        /* Rewire the source to an integer operation producing a real object ID. */
        ASSERT_EQ(2u,g.f.value_count); g.f.value_count--;
        settings(&g,1,1,0,0);
        if (!valid) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        nmo_object_id_t a = 0, b = 0, op = 0;
        ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_INT,"Source ID",&a));
        ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_INT,"Zero",&b));
        char text[32]; snprintf(text,sizeof(text),"%u",g.source);
        ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,a,text)); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,b,"0"));
        ASSERT_EQ(NMO_OK,nmo_script_edit_add_operation(g.f.tx,g.f.script_id,NMO_OP_GUID_ADD,a,b,0,&op));
        const nmo_parameteroperation_state_t *operation = fixture_state(&g.f,op); ASSERT_NOT_NULL(operation);
        ((nmo_parameterin_state_t *)fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)))->type_guid = CKPGUID_INT;
        ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,nmo_parameteroperation_out_id(operation),BLOCK_PIN(&g.f,g.block,0)));
        commit(&g); size_t operations = 0; nmo_engine_set_trace(g.f.engine,count_operations,&operations);
        ASSERT_EQ(valid ? NMO_OK : NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        ASSERT_EQ(valid ? 1u : 0u,operations);
        position(&g,g.f.owner_id,valid ? (eng_vector_t){30,60,90} : (eng_vector_t){10,20,30}); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_mimic, position_orientation_and_scale_are_independently_enabled_and_weighted);
REGISTER_TEST(engine_mimic, time_scaling_caps_each_weight_only_above_one_and_preserves_negative_values);
REGISTER_TEST(engine_mimic, orientation_uses_the_short_arc_extrapolation_and_normalized_close_angle_blends);
REGISTER_TEST(engine_mimic, scale_copy_and_orientation_preservation_use_local_scale_under_different_parents);
REGISTER_TEST(engine_mimic, orientation_scale_restoration_moves_children_even_when_hierarchy_is_disabled);
REGISTER_TEST(engine_mimic, hierarchy_changes_propagate_position_and_scale_while_sampling_live_descendant_sources);
REGISTER_TEST(engine_mimic, absent_sources_are_noops_and_absent_targets_report_after_setting_io);
REGISTER_TEST(engine_mimic, loaded_and_copied_graphs_keep_live_sources_speeds_and_targets_independent);
REGISTER_TEST(engine_mimic, move_to_outputs_drive_full_pose_following_through_the_final_completion);
REGISTER_TEST(engine_mimic, invalid_targets_do_not_evaluate_operations_connected_to_the_source_input);
TEST_MAIN_END()
