#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_DISTANCE NMO_GUID(0xb7e6398a,0x113c66dd)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct distance_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, reference, block, out;
    int offset;
} distance_fixture_t;

static nmo_object_id_t local(distance_fixture_t *g)
{
    const nmo_behavior_state_t *state = fixture_state(&g->f,g->block);
    return nmo_behavior_ref_array_get_id(&state->local_parameters,0);
}

static nmo_object_id_t source(distance_fixture_t *g, int index)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,index)));
}

static void input_float(distance_fixture_t *g, int index, float value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,index),&value,sizeof(value));
}

static void begin(distance_fixture_t *g, bool legacy, bool time)
{
    memset(g,0,sizeof(*g)); g->offset = !legacy; fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Follower");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->reference = fixture_create(&g->f,NMO_CID_3DENTITY,"Reference");
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child);
    ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child);
    target->parent = nmo_ref_from_id(g->parent); target->has_entityndata_chunk = 1;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 2; child->world_matrix[13] = 3; child->world_matrix[14] = 4;
    nmo_3dentity_state_t *reference = fixture_state(&g->f,g->reference); ASSERT_NOT_NULL(reference);
    reference->world_matrix[12] = 10;
    g->f.script_id = fixture_add_script(&g->f,"Distance Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_DISTANCE,"Keep Distance"); g->out = fixture_block(&g->f,GUID_NOP,"After Distance");
    ASSERT_TRUE(g->block != 0 && g->out != 0);
    if (legacy) ASSERT_EQ(NMO_OK,nmo_script_edit_remove_parameter(g->f.tx,BLOCK_PIN(&g->f,g->block,0),true));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,local(g),time ? "TRUE" : "FALSE"));
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target);
    if (!legacy) { const eng_vector_t point = {0,0,0}; fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,0),&point,sizeof(point)); }
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,g->offset),(int32_t)g->reference);
    input_float(g,1+g->offset,4); input_float(g,2+g->offset,0);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(distance_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_DISTANCE));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void position(distance_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g->f.engine,id,matrix));
    ASSERT_TRUE(isfinite(matrix[12]) && isfinite(matrix[13]) && isfinite(matrix[14]));
    ASSERT_FLOAT_EQ(expected.x,matrix[12],1e-4f); ASSERT_FLOAT_EQ(expected.y,matrix[13],1e-4f); ASSERT_FLOAT_EQ(expected.z,matrix[14],1e-4f);
}

static void set_position(distance_fixture_t *g, nmo_object_id_t id, eng_vector_t point)
{
    eng_entity_set_position(g->f.engine,eng_entity_index(g->f.engine,id),&point,-1,false);
}

static void trigger(distance_fixture_t *g, float delta)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,delta));
}

TEST(engine_keep_distance, adjusts_toward_or_away_from_the_anchor_with_signed_distances)
{
    const float distances[] = {4,20,10,0,-2}; const float positions[] = {6,-10,0,10,12};
    for (int legacy = 0; legacy < 2; legacy++) for (size_t i = 0; i < COUNT(distances); i++) {
        distance_fixture_t g; begin(&g,legacy,false); input_float(&g,1+g.offset,distances[i]); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.target,(eng_vector_t){positions[i],0,0});
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_keep_distance, transforms_points_with_the_reference_world_matrix_and_reads_live_inputs)
{
    distance_fixture_t g; begin(&g,false,false);
    const eng_vector_t point = {2,0,0}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&point,sizeof(point)); commit(&g);
    eng_matrix_t matrix; eng_matrix_from_euler(&matrix,0,0,1.57079632679f);
    for (int j = 0; j < 3; j++) matrix.m[0][j] *= 3;
    matrix.m[3][0] = 8; eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.reference),&matrix,false);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.target,(eng_vector_t){4.8f,3.6f,0});
    set_position(&g,g.reference,(eng_vector_t){11.2f,2.4f,0}); trigger(&g,10); position(&g,g.target,(eng_vector_t){8,6,0});
    float distance = 2; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,2),&distance,sizeof(distance)));
    eng_vector_t changed = {0,0,0}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,0),&changed,sizeof(changed)));
    set_position(&g,g.reference,(eng_vector_t){18,6,0}); trigger(&g,10); position(&g,g.target,(eng_vector_t){16,6,0}); fixture_end(&g.f);
}

TEST(engine_keep_distance, absent_deleted_and_nonentity_references_use_world_points)
{
    for (int legacy = 0; legacy < 2; legacy++) for (int kind = 0; kind < 3; kind++) {
        distance_fixture_t g; begin(&g,legacy,false);
        if (!legacy) { const eng_vector_t point = {0,6,8}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&point,sizeof(point)); }
        if (kind != 1) fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,g.offset),kind == 0 ? 0 : (int32_t)g.block);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.reference);
        if (legacy) set_position(&g,g.target,(eng_vector_t){0,-6,-8});
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.target,legacy ? (eng_vector_t){0,-2.4f,-3.2f} : (eng_vector_t){0,3.6f,4.8f}); fixture_end(&g.f);
    }
}

TEST(engine_keep_distance, attenuation_uses_frame_or_time_scaling_and_clamps_negative_values)
{
    const float attenuation[] = {-3,0,1,3}; const float deltas[] = {5,10,20};
    const float expected[][3] = {{3,3,3},{3,3,3},{1,1.5f,2},{3.0f/7,.75f,1.2f}};
    for (int time = 0; time < 2; time++) for (size_t a = 0; a < COUNT(attenuation); a++) for (size_t d = 0; d < COUNT(deltas); d++) {
        distance_fixture_t g; begin(&g,false,time); input_float(&g,3,attenuation[a]); input_float(&g,2,7); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,deltas[d])); position(&g,g.target,(eng_vector_t){expected[a][time ? d : 1],0,0}); fixture_end(&g.f);
    }
}

TEST(engine_keep_distance, repeated_attenuated_triggers_use_the_current_distance_without_internal_scheduling)
{
    distance_fixture_t g; begin(&g,false,false); input_float(&g,3,1); commit(&g);
    const float expected[] = {3,4.5f,5.25f};
    for (size_t step = 0; step < COUNT(expected); step++) { trigger(&g,1); position(&g,g.target,(eng_vector_t){expected[step],0,0}); }
    set_position(&g,g.reference,(eng_vector_t){15.25f,0,0}); trigger(&g,1); position(&g,g.target,(eng_vector_t){8.25f,0,0});
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(4u,runs(&g.f,g.block)); ASSERT_EQ(4u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_keep_distance, submillimeter_separation_uses_the_native_positive_z_fallback)
{
    const eng_vector_t points[] = {{0,0,0},{.0005f,0,0},{0,0,-.0005f},{.001f,0,0}};
    for (size_t i = 0; i < COUNT(points); i++) {
        distance_fixture_t g; begin(&g,false,false); commit(&g); set_position(&g,g.reference,points[i]);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.target,i == 3 ? (eng_vector_t){-3.999f,0,0} : (eng_vector_t){0,0,-3.999f}); fixture_end(&g.f);
    }
}

TEST(engine_keep_distance, hierarchy_preserves_world_bases_and_moves_children_under_scaled_parents)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        distance_fixture_t g; begin(&g,false,false); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),hierarchy); commit(&g);
        eng_matrix_t parent; eng_matrix_from_euler(&parent,.2f,-.7f,.3f);
        for (int j = 0; j < 3; j++) { parent.m[0][j] *= 2; parent.m[1][j] *= 3; parent.m[2][j] *= 4; }
        parent.m[3][0] = 100; eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.parent),&parent,true);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.target,(eng_vector_t){6,0,0});
        position(&g,g.child,(eng_vector_t){hierarchy ? 8.0f : 2.0f,3,4});
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix));
        ASSERT_FLOAT_EQ(2,matrix[0],1e-5f); ASSERT_FLOAT_EQ(3,matrix[5],1e-5f); ASSERT_FLOAT_EQ(4,matrix[10],1e-5f);
        ASSERT_FLOAT_EQ(0,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_keep_distance, zero_delta_preserves_native_infinite_and_nan_attenuation_results)
{
    for (int positive = 0; positive < 2; positive++) {
        distance_fixture_t g; begin(&g,false,true); input_float(&g,3,positive ? 1.0f : 0.0f); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (positive) position(&g,g.target,(eng_vector_t){0,0,0});
        else {
            float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix));
            ASSERT_TRUE(isnan(matrix[12])); ASSERT_TRUE(isnan(matrix[13])); ASSERT_TRUE(isnan(matrix[14]));
        }
        ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_keep_distance, missing_and_deleted_targets_report_owner_errors_after_setting_io)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        distance_fixture_t g; begin(&g,false,false);
        if (!deleted) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        commit(&g); if (deleted) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,10)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

TEST(engine_keep_distance, disconnected_optional_inputs_keep_native_distance_attenuation_and_hierarchy_defaults)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        distance_fixture_t g; begin(&g,legacy,false);
        nmo_object_id_t target = nmo_parameterin_source_id(fixture_state(&g.f,fixture_target(&g.f,g.block))), reference = source(&g,g.offset);
        size_t kept = 0;
        for (size_t i = 0; i < g.f.value_count; i++) if (g.f.values[i].param == target || g.f.values[i].param == reference) g.f.values[kept++] = g.f.values[i];
        g.f.value_count = kept;
        if (!legacy) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)),0);
        for (int index = 1+g.offset; index < 4+g.offset; index++) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,index)),0);
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.target,(eng_vector_t){9,0,0});
        position(&g,g.child,(eng_vector_t){11,3,4}); fixture_end(&g.f);
    }
}

TEST(engine_keep_distance, samples_descendant_references_before_propagating_the_target_translation)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        distance_fixture_t g; begin(&g,false,false);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)g.child); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),hierarchy); commit(&g);
        set_position(&g,g.child,(eng_vector_t){10,0,0}); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10));
        position(&g,g.target,(eng_vector_t){6,0,0}); position(&g,g.child,(eng_vector_t){hierarchy ? 16.0f : 10.0f,0,0}); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(distance_fixture_t *g, nmo_object_id_t pin, const char *text)
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

static void expect_position(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t expected)
{
    eng_vector_t actual = eng_entity_position(engine,eng_entity_index(engine,id),-1);
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

TEST(engine_keep_distance, loaded_and_copied_constraints_keep_reference_bindings_and_attenuation_settings_independent)
{
    for (int legacy = 0; legacy < 2; legacy++) for (int time = 0; time < 2; time++) {
        distance_fixture_t g; begin(&g,legacy,time);
        nmo_object_id_t parameters[6]; char text[32];
        snprintf(text,sizeof(text),"%u",g.target); parameters[0] = saved_input(&g,fixture_target(&g.f,g.block),text);
        snprintf(text,sizeof(text),"%u",g.reference); parameters[1] = saved_input(&g,BLOCK_PIN(&g.f,g.block,g.offset),text);
        parameters[2] = saved_input(&g,BLOCK_PIN(&g.f,g.block,1+g.offset),"4"); parameters[3] = saved_input(&g,BLOCK_PIN(&g.f,g.block,2+g.offset),"1");
        parameters[4] = saved_input(&g,BLOCK_PIN(&g.f,g.block,3+g.offset),"TRUE");
        if (!legacy) parameters[5] = saved_input(&g,BLOCK_PIN(&g.f,g.block,0),"2,0,0");
        size_t count = legacy ? 5 : 6;
        for (size_t i = 0; i < count; i++) ASSERT_TRUE(parameters[i] != 0);
        nmo_3dentity_state_t *reference = fixture_state(&g.f,g.reference);
        reference->world_matrix[0] = 0; reference->world_matrix[1] = 3;
        reference->world_matrix[4] = -1; reference->world_matrix[5] = 0; reference->world_matrix[12] = 8;
        fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_keep_distance.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"distance.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Keep Distance",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t original[] = {g.f.owner_id,g.parent,g.target,g.child,g.reference,g.f.script_id,g.block,g.out};
            nmo_object_id_t ids[14] = {0}, copies[14] = {0};
            for (size_t i = 0; i < COUNT(original); i++) { ids[i] = eng_module_id(module,original[i]); ASSERT_TRUE(ids[i] != 0); }
            for (size_t i = 0; i < count; i++) ids[8+i] = eng_module_id(module,parameters[i]);
            ASSERT_TRUE(ids[2] != g.target); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10));
            expect_position(host.engine,ids[2],legacy ? (eng_vector_t){2,0,0} : (eng_vector_t){2.4f,1.8f,0});
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,(int32_t)(8+count),false,copies));
            ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[2])); ASSERT_EQ(copies[2],nmo_engine_entity_parent(host.engine,copies[3]));
            eng_vector_t anchor = legacy ? (eng_vector_t){12,0,0} : (eng_vector_t){10.4f,1.8f,0};
            eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[4]),&anchor,-1,false);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,ids[5],true,true));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,20));
            eng_vector_t a = legacy ? (eng_vector_t){time ? 10.0f/3 : 3,0,0} : time ? (eng_vector_t){4,3,0} : (eng_vector_t){3.6f,2.7f,0};
            eng_vector_t b = legacy ? (eng_vector_t){time ? 6.0f : 5.0f,0,0} : time ? (eng_vector_t){5.6f,4.2f,0} : (eng_vector_t){4.8f,3.6f,0};
            expect_position(host.engine,ids[2],a); expect_position(host.engine,copies[2],b);
            expect_position(host.engine,copies[3],(eng_vector_t){b.x+2,b.y+3,4});
            float distance = 2, attenuation = 0; eng_vector_t zero = {0,0,0};
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],&distance,sizeof(distance)));
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[11],&attenuation,sizeof(attenuation)));
            if (!legacy) ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[13],&zero,sizeof(zero)));
            nmo_engine_destroy_object(host.engine,ids[2]); nmo_engine_destroy_object(host.engine,ids[4]); nmo_engine_destroy_object(host.engine,ids[1]);
            anchor = (eng_vector_t){100,0,0}; eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[4]),&anchor,-1,false);
            eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[2]),&zero,-1,false);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10));
            expect_position(host.engine,copies[2],(eng_vector_t){98,0,0}); expect_position(host.engine,copies[3],(eng_vector_t){100,3,4}); fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(0,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f); remove(path);
    }
}

TEST(engine_keep_distance, curve_follow_outputs_drive_the_constraint_in_the_same_frame_and_stop_after_completion)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        distance_fixture_t g; begin(&g,legacy,true);
        ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12] = -10;
        ((nmo_3dentity_state_t *)fixture_state(&g.f,g.child))->world_matrix[12] = -8;
        nmo_object_id_t path_id = fixture_create(&g.f,NMO_CID_CURVE,"Motion Path");
        nmo_curve_state_t *path = fixture_state(&g.f,path_id); ASSERT_NOT_NULL(path); path->opened = 1; path->control_point_count = 2;
        path->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(g.f.session),2*sizeof(nmo_ref_t),_Alignof(nmo_ref_t)); ASSERT_NOT_NULL(path->control_point_ids);
        for (size_t i = 0; i < 2; i++) {
            nmo_object_id_t id = fixture_create(&g.f,NMO_CID_CURVEPOINT,"Point"); nmo_curvepoint_state_t *point = fixture_state(&g.f,id); ASSERT_NOT_NULL(point);
            point->base.parent = nmo_ref_from_id(path_id); point->base.has_entityndata_chunk = 1; point->curve = nmo_ref_from_id(path_id); point->linear = 1;
            nmo_curvepoint_set_position(point,i ? 20.0f : 0.0f,0,0); path->control_point_ids[i] = nmo_ref_from_id(id);
        }
        const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
        ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
        nmo_object_id_t driver = fixture_block(&g.f,NMO_GUID(0x00ed010b,0x00fd010b),"Move Reference"); ASSERT_TRUE(driver != 0);
        fixture_input_int(&g.f,fixture_target(&g.f,driver),(int32_t)g.reference); fixture_input_int(&g.f,BLOCK_PIN(&g.f,driver,0),(int32_t)path_id);
        float duration = 40; fixture_input_value(&g.f,BLOCK_PIN(&g.f,driver,1),&duration,sizeof(duration));
        nmo_object_id_t curve = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,driver,2),&curve));
        fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,driver,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,driver,1),BLOCK_IN(&g.f,driver,1),1);
        for (int output = 0; output < 2; output++) fixture_link(&g.f,BLOCK_OUT(&g.f,driver,output),BLOCK_IN(&g.f,g.block,0),0);
        commit(&g);
        for (int step = 1; step <= 5; step++) {
            float x = step < 4 ? (float)step*5 : 20;
            ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.reference,(eng_vector_t){x,0,0});
            position(&g,g.target,(eng_vector_t){x-4,0,0}); position(&g,g.child,(eng_vector_t){x-2,3,4});
        }
        ASSERT_EQ(5u,runs(&g.f,driver)); ASSERT_EQ(5u,runs(&g.f,g.block)); ASSERT_EQ(5u,runs(&g.f,g.out));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(5u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_keep_distance, adjusts_toward_or_away_from_the_anchor_with_signed_distances);
REGISTER_TEST(engine_keep_distance, transforms_points_with_the_reference_world_matrix_and_reads_live_inputs);
REGISTER_TEST(engine_keep_distance, absent_deleted_and_nonentity_references_use_world_points);
REGISTER_TEST(engine_keep_distance, attenuation_uses_frame_or_time_scaling_and_clamps_negative_values);
REGISTER_TEST(engine_keep_distance, repeated_attenuated_triggers_use_the_current_distance_without_internal_scheduling);
REGISTER_TEST(engine_keep_distance, submillimeter_separation_uses_the_native_positive_z_fallback);
REGISTER_TEST(engine_keep_distance, hierarchy_preserves_world_bases_and_moves_children_under_scaled_parents);
REGISTER_TEST(engine_keep_distance, zero_delta_preserves_native_infinite_and_nan_attenuation_results);
REGISTER_TEST(engine_keep_distance, missing_and_deleted_targets_report_owner_errors_after_setting_io);
REGISTER_TEST(engine_keep_distance, disconnected_optional_inputs_keep_native_distance_attenuation_and_hierarchy_defaults);
REGISTER_TEST(engine_keep_distance, samples_descendant_references_before_propagating_the_target_translation);
REGISTER_TEST(engine_keep_distance, loaded_and_copied_constraints_keep_reference_bindings_and_attenuation_settings_independent);
REGISTER_TEST(engine_keep_distance, curve_follow_outputs_drive_the_constraint_in_the_same_frame_and_stop_after_completion);
TEST_MAIN_END()
