#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_AROUND NMO_GUID(0xfdfd999e,0xdefeded8)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct around_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, reference, block, out;
} around_fixture_t;

static nmo_object_id_t source(around_fixture_t *g, int index)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,index)));
}

static void angle(around_fixture_t *g, float value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,1),&value,sizeof(value));
}

static void begin(around_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Orbiter");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->reference = fixture_create(&g->f,NMO_CID_3DENTITY,"Reference");
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child), *reference = fixture_state(&g->f,g->reference);
    ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(reference);
    target->parent = nmo_ref_from_id(g->parent); target->has_entityndata_chunk = 1;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    reference->world_matrix[12] = 10; reference->world_matrix[13] = 20; reference->world_matrix[14] = 30;
    g->f.script_id = fixture_add_script(&g->f,"Orbit Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_AROUND,"Rotate Around"); g->out = fixture_block(&g->f,GUID_NOP,"After Orbit"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target);
    const eng_vector_t axis = {0,1,0}; fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,0),&axis,sizeof(axis));
    angle(g,PI_F*.5f); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,2),0);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(around_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_AROUND));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void position(around_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    vector(eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1),expected);
}

static void runtime_basis(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,matrix));
    vector((eng_vector_t){matrix[0],matrix[1],matrix[2]},x); vector((eng_vector_t){matrix[4],matrix[5],matrix[6]},y); vector((eng_vector_t){matrix[8],matrix[9],matrix[10]},z);
}

static void basis(around_fixture_t *g, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    runtime_basis(g->f.engine,g->target,x,y,z);
}

static void set_position(around_fixture_t *g, nmo_object_id_t id, eng_vector_t point)
{
    eng_entity_set_position(g->f.engine,eng_entity_index(g->f.engine,id),&point,-1,false);
}

static void trigger(around_fixture_t *g, float delta)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,delta));
}

TEST(engine_rotate_around, rotates_about_signed_and_nonunit_axes_without_scheduling_another_frame)
{
    const eng_vector_t axes[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{7,7,7},{-2,-2,-2}};
    const eng_vector_t expected[] = {{3,5,-4},{3,-5,4},{-5,4,3},{5,4,-3},{4,-3,5},{-4,3,5},{4,5,3},{5,3,4}};
    for (size_t i = 0; i < COUNT(axes); i++) {
        around_fixture_t g; begin(&g); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&axes[i],sizeof(axes[i]));
        if (i >= 6) angle(&g,PI_F*2/3);
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,expected[i]);
        basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, orientation_and_hierarchy_flags_apply_to_both_rotation_and_translation)
{
    for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        around_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),hierarchy); commit(&g);
        eng_matrix_t parent; eng_matrix_from_euler(&parent,.2f,-.7f,.3f);
        for (int j = 0; j < 3; j++) { parent.m[0][j] *= 2; parent.m[1][j] *= 3; parent.m[2][j] *= 4; }
        parent.m[3][0] = 100; eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.parent),&parent,true);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){-5,4,3});
        position(&g,g.child,!hierarchy ? (eng_vector_t){5,7,9} : keep_orientation ? (eng_vector_t){-3,7,7} : (eng_vector_t){-9,7,5});
        if (keep_orientation) basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        else basis(&g,(eng_vector_t){0,0,2},(eng_vector_t){0,3,0},(eng_vector_t){-4,0,0});
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),false),(eng_vector_t){2,3,4});
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, orbits_in_scaled_and_sheared_reference_space_while_rotating_orientation_about_the_world_axis)
{
    for (int shear = 0; shear < 2; shear++) for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) {
        around_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.reference); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation);
        nmo_3dentity_state_t *reference = fixture_state(&g.f,g.reference), *target = fixture_state(&g.f,g.target);
        reference->world_matrix[0] = shear ? 2 : 0; reference->world_matrix[1] = shear ? 1 : 2;
        reference->world_matrix[4] = shear ? 0 : -3; reference->world_matrix[5] = shear ? 3 : 0; reference->world_matrix[10] = 4;
        target->world_matrix[12] = shear ? 12 : 4; target->world_matrix[13] = shear ? 27 : 22; target->world_matrix[14] = 42; commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,shear ? (eng_vector_t){4,23,34} : (eng_vector_t){4,14,34});
        if (keep_orientation) basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        else if (shear) basis(&g,(eng_vector_t){0,0,2},(eng_vector_t){0,3,0},(eng_vector_t){-4,0,0});
        else basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,0,3},(eng_vector_t){0,-4,0});
        fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, descendant_references_are_sampled_after_orientation_propagation)
{
    for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        around_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.child);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),hierarchy); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.target,!keep_orientation && hierarchy ? (eng_vector_t){1,4,11} : (eng_vector_t){9,4,7});
        position(&g,g.child,!hierarchy ? (eng_vector_t){5,7,9} : keep_orientation ? (eng_vector_t){11,7,11} : (eng_vector_t){-3,7,13}); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, self_references_keep_the_pivot_position_and_use_the_current_local_axis)
{
    for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) {
        around_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.target);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.target,(eng_vector_t){3,4,5});
        if (keep_orientation) basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
        else { basis(&g,(eng_vector_t){0,0,2},(eng_vector_t){0,3,0},(eng_vector_t){-4,0,0}); } fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, tiny_angles_move_the_orbit_even_when_orientation_rotation_is_below_its_threshold)
{
    around_fixture_t g; begin(&g); angle(&g,1e-8f); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,4),0); commit(&g);
    set_position(&g,g.target,(eng_vector_t){0,0,1e8f}); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){-1,0,1e8f});
    basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
}

TEST(engine_rotate_around, resamples_live_angles_axes_reference_transforms_and_current_positions)
{
    around_fixture_t g; begin(&g); const eng_vector_t axis = {0,0,1}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&axis,sizeof(axis));
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.reference); commit(&g); set_position(&g,g.reference,(eng_vector_t){10,0,0});
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){14,7,5});
    float negative = -PI_F*.5f; eng_vector_t y = {0,1,0};
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,0),&y,sizeof(y))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,1),&negative,sizeof(negative)));
    set_position(&g,g.reference,(eng_vector_t){20,0,0}); trigger(&g,1000); position(&g,g.target,(eng_vector_t){25,7,6});
    basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
}

TEST(engine_rotate_around, missing_deleted_and_nonentity_references_use_the_world_origin)
{
    for (int kind = 0; kind < 3; kind++) {
        around_fixture_t g; begin(&g);
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),kind == 1 ? (int32_t)g.reference : kind == 2 ? (int32_t)g.block : 0);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.reference);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(&g,g.target,(eng_vector_t){-5,4,3}); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, zero_axes_keep_native_nan_results_instead_of_silently_becoming_noops)
{
    for (int zero_angle = 0; zero_angle < 2; zero_angle++) {
        around_fixture_t g; begin(&g); const eng_vector_t zero = {0,0,0}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&zero,sizeof(zero));
        if (zero_angle) angle(&g,0);
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix));
        ASSERT_TRUE(isnan(matrix[12]) && isnan(matrix[13]) && isnan(matrix[14]));
        basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, missing_and_deleted_targets_report_owner_errors_before_consuming_input)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        around_fixture_t g; begin(&g); if (!deleted) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        commit(&g); if (deleted) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

TEST(engine_rotate_around, disconnected_inputs_preserve_native_axis_angle_orientation_and_hierarchy_defaults)
{
    for (int missing_angle = 0; missing_angle < 2; missing_angle++) {
        around_fixture_t g; begin(&g);
        nmo_object_id_t target = nmo_parameterin_source_id(fixture_state(&g.f,fixture_target(&g.f,g.block))), rotation = source(&g,1);
        size_t kept = 0;
        for (size_t i = 0; i < g.f.value_count; i++) if (g.f.values[i].param == target || (!missing_angle && g.f.values[i].param == rotation)) g.f.values[kept++] = g.f.values[i];
        g.f.value_count = kept;
        for (int i = 0; i < 5; i++) if (missing_angle || i != 1) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,i)),0);
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        position(&g,g.target,missing_angle ? (eng_vector_t){3,4,5} : (eng_vector_t){-5,4,3});
        position(&g,g.child,missing_angle ? (eng_vector_t){5,7,9} : (eng_vector_t){-3,7,7});
        basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(around_fixture_t *g, nmo_object_id_t pin, const char *text)
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
    vector(eng_entity_position(engine,eng_entity_index(engine,id),-1),expected);
}

TEST(engine_rotate_around, loaded_and_copied_orbits_keep_parameters_references_and_hierarchy_independent)
{
    for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        around_fixture_t g; begin(&g); nmo_object_id_t parameters[6]; char text[32];
        snprintf(text,sizeof(text),"%u",g.target); parameters[0] = saved_input(&g,fixture_target(&g.f,g.block),text);
        parameters[1] = saved_input(&g,BLOCK_PIN(&g.f,g.block,0),"0,1,0"); parameters[2] = saved_input(&g,BLOCK_PIN(&g.f,g.block,1),"0");
        float quarter = PI_F*.5f; ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_bytes(g.f.tx,parameters[2],(const uint8_t *)&quarter,sizeof(quarter)));
        snprintf(text,sizeof(text),"%u",g.reference); parameters[3] = saved_input(&g,BLOCK_PIN(&g.f,g.block,2),text);
        parameters[4] = saved_input(&g,BLOCK_PIN(&g.f,g.block,3),keep_orientation ? "TRUE" : "FALSE");
        parameters[5] = saved_input(&g,BLOCK_PIN(&g.f,g.block,4),hierarchy ? "TRUE" : "FALSE");
        for (size_t i = 0; i < COUNT(parameters); i++) ASSERT_TRUE(parameters[i] != 0);
        nmo_3dentity_state_t *reference = fixture_state(&g.f,g.reference), *target = fixture_state(&g.f,g.target), *child = fixture_state(&g.f,g.child);
        reference->world_matrix[0] = 2; reference->world_matrix[5] = 3; reference->world_matrix[10] = 4;
        target->world_matrix[12] = 12; target->world_matrix[13] = 26; target->world_matrix[14] = 42;
        child->world_matrix[12] = 14; child->world_matrix[13] = 29; child->world_matrix[14] = 46; fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_rotate_around.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"orbit.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Rotate Around",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t original[] = {g.f.owner_id,g.parent,g.target,g.child,g.reference,g.f.script_id,g.block,g.out};
            nmo_object_id_t ids[14] = {0}, copies[14] = {0};
            for (size_t i = 0; i < COUNT(original); i++) { ids[i] = eng_module_id(module,original[i]); ASSERT_TRUE(ids[i] != 0); }
            for (size_t i = 0; i < COUNT(parameters); i++) ids[8+i] = eng_module_id(module,parameters[i]);
            ASSERT_TRUE(ids[2] != g.target); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10)); expect_position(host.engine,ids[2],(eng_vector_t){4,26,34});
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[2])); ASSERT_EQ(copies[2],nmo_engine_entity_parent(host.engine,copies[3]));
            float matrix[16], copied[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(host.engine,ids[2],matrix)); ASSERT_TRUE(nmo_engine_entity_world_matrix(host.engine,copies[2],copied));
            for (size_t i = 0; i < COUNT(matrix); i++) ASSERT_FLOAT_EQ(matrix[i],copied[i],0);
            eng_vector_t pivot = {30,20,30}; eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[4]),&pivot,-1,false);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,ids[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true));
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100)); expect_position(host.engine,ids[2],(eng_vector_t){8,26,18}); expect_position(host.engine,copies[2],(eng_vector_t){28,26,-22});
            expect_position(host.engine,copies[3],!hierarchy ? (eng_vector_t){14,29,46} : keep_orientation ? (eng_vector_t){30,29,-18} : (eng_vector_t){26,29,-26});
            runtime_basis(host.engine,copies[2],(eng_vector_t){keep_orientation ? 2 : -2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,keep_orientation ? 4 : -4});
            nmo_engine_destroy_object(host.engine,ids[2]); nmo_engine_destroy_object(host.engine,ids[4]); nmo_engine_destroy_object(host.engine,ids[1]);
            float negative = -quarter; eng_vector_t z = {0,0,1}; int32_t yes = 1;
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&z,sizeof(z))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],&negative,sizeof(negative)));
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[12],&yes,sizeof(yes)));
            pivot = (eng_vector_t){100,200,300}; eng_vector_t here = {102,206,312};
            eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[4]),&pivot,-1,false); eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[2]),&here,-1,false);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,1));
            expect_position(host.engine,copies[2],(eng_vector_t){96,203,312});
            runtime_basis(host.engine,copies[2],(eng_vector_t){keep_orientation ? 2 : -2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,keep_orientation ? 4 : -4}); fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(12,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f); remove(path);
    }
}

TEST(engine_rotate_around, curve_follow_moves_the_pivot_before_each_orbit_including_the_completion_frame)
{
    for (int keep_orientation = 0; keep_orientation < 2; keep_orientation++) {
        around_fixture_t g; begin(&g); const eng_vector_t axis = {0,0,1}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&axis,sizeof(axis));
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),(int32_t)g.reference); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),keep_orientation);
        nmo_object_id_t path_id = fixture_create(&g.f,NMO_CID_CURVE,"Pivot Path"); nmo_curve_state_t *path = fixture_state(&g.f,path_id); ASSERT_NOT_NULL(path);
        path->opened = 1; path->control_point_count = 2;
        path->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(g.f.session),2*sizeof(nmo_ref_t),_Alignof(nmo_ref_t)); ASSERT_NOT_NULL(path->control_point_ids);
        for (size_t i = 0; i < 2; i++) {
            nmo_object_id_t id = fixture_create(&g.f,NMO_CID_CURVEPOINT,"Point"); nmo_curvepoint_state_t *point = fixture_state(&g.f,id); ASSERT_NOT_NULL(point);
            point->base.parent = nmo_ref_from_id(path_id); point->base.has_entityndata_chunk = 1; point->curve = nmo_ref_from_id(path_id); point->linear = 1;
            nmo_curvepoint_set_position(point,i ? 20 : 0,0,0); path->control_point_ids[i] = nmo_ref_from_id(id);
        }
        const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
        ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
        nmo_object_id_t driver = fixture_block(&g.f,NMO_GUID(0x00ed010b,0x00fd010b),"Move Pivot"); ASSERT_TRUE(driver != 0);
        fixture_input_int(&g.f,fixture_target(&g.f,driver),(int32_t)g.reference); fixture_input_int(&g.f,BLOCK_PIN(&g.f,driver,0),(int32_t)path_id);
        float duration = 40; fixture_input_value(&g.f,BLOCK_PIN(&g.f,driver,1),&duration,sizeof(duration));
        nmo_object_id_t curve = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,driver,2),&curve));
        fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,driver,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,driver,1),BLOCK_IN(&g.f,driver,1),1);
        for (int output = 0; output < 2; output++) fixture_link(&g.f,BLOCK_OUT(&g.f,driver,output),BLOCK_IN(&g.f,g.block,0),0);
        commit(&g);
        const eng_vector_t positions[] = {{9,2,5},{12,1,5},{16,3,5},{23,4,5},{24,-3,5}};
        const eng_vector_t offsets[] = {{3,-2,4},{-2,-3,4},{-3,2,4},{2,3,4},{3,-2,4}};
        for (size_t step = 0; step < COUNT(positions); step++) {
            ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); position(&g,g.reference,(eng_vector_t){step < 4 ? (float)(step+1)*5 : 20,0,0}); position(&g,g.target,positions[step]);
            eng_vector_t offset = keep_orientation ? (eng_vector_t){2,3,4} : offsets[step];
            position(&g,g.child,(eng_vector_t){positions[step].x+offset.x,positions[step].y+offset.y,positions[step].z+offset.z});
            float rotation = keep_orientation ? 0 : (float)(step+1)*PI_F*.5f, c = cosf(rotation), s = sinf(rotation);
            basis(&g,(eng_vector_t){2*c,-2*s,0},(eng_vector_t){3*s,3*c,0},(eng_vector_t){0,0,4});
        }
        ASSERT_EQ(5u,runs(&g.f,driver)); ASSERT_EQ(5u,runs(&g.f,g.block)); ASSERT_EQ(5u,runs(&g.f,g.out));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(5u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_rotate_around, rotates_about_signed_and_nonunit_axes_without_scheduling_another_frame);
REGISTER_TEST(engine_rotate_around, orientation_and_hierarchy_flags_apply_to_both_rotation_and_translation);
REGISTER_TEST(engine_rotate_around, orbits_in_scaled_and_sheared_reference_space_while_rotating_orientation_about_the_world_axis);
REGISTER_TEST(engine_rotate_around, descendant_references_are_sampled_after_orientation_propagation);
REGISTER_TEST(engine_rotate_around, self_references_keep_the_pivot_position_and_use_the_current_local_axis);
REGISTER_TEST(engine_rotate_around, tiny_angles_move_the_orbit_even_when_orientation_rotation_is_below_its_threshold);
REGISTER_TEST(engine_rotate_around, resamples_live_angles_axes_reference_transforms_and_current_positions);
REGISTER_TEST(engine_rotate_around, missing_deleted_and_nonentity_references_use_the_world_origin);
REGISTER_TEST(engine_rotate_around, zero_axes_keep_native_nan_results_instead_of_silently_becoming_noops);
REGISTER_TEST(engine_rotate_around, missing_and_deleted_targets_report_owner_errors_before_consuming_input);
REGISTER_TEST(engine_rotate_around, disconnected_inputs_preserve_native_axis_angle_orientation_and_hierarchy_defaults);
REGISTER_TEST(engine_rotate_around, loaded_and_copied_orbits_keep_parameters_references_and_hierarchy_independent);
REGISTER_TEST(engine_rotate_around, curve_follow_moves_the_pivot_before_each_orbit_including_the_completion_frame);
TEST_MAIN_END()
