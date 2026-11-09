#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "type/nmo_param_guids.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_ORIENTATION NMO_GUID(0x625874aa,0xaa694132)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct orientation_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, reference, block, out;
} orientation_fixture_t;

static nmo_object_id_t source(orientation_fixture_t *g, int index)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,index)));
}

static void input(orientation_fixture_t *g, int index, eng_vector_t value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,index),&value,sizeof(value));
}

static void begin(orientation_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Oriented Entity");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->reference = fixture_create(&g->f,NMO_CID_3DENTITY,"Reference");
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child), *reference = fixture_state(&g->f,g->reference);
    ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(reference);
    target->parent = nmo_ref_from_id(g->parent); target->has_entityndata_chunk = 1;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->has_entityndata_chunk = 1;
    child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    reference->world_matrix[12] = 10; reference->world_matrix[13] = 20; reference->world_matrix[14] = 30;
    g->f.script_id = fixture_add_script(&g->f,"Orientation Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_ORIENTATION,"Set Orientation"); g->out = fixture_block(&g->f,GUID_NOP,"After Orientation"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target);
    input(g,0,(eng_vector_t){0,0,1}); input(g,1,(eng_vector_t){0,1,0}); input(g,4,(eng_vector_t){0,0,1}); input(g,5,(eng_vector_t){0,1,0});
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,3),0);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(orientation_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_ORIENTATION));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    /* CK SetOrientation replaces zero axis components with 1e-6 before propagation. */
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void position(orientation_fixture_t *g, nmo_object_id_t id, eng_vector_t expected)
{
    vector(eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1),expected);
}

static void runtime_basis(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,matrix));
    vector((eng_vector_t){matrix[0],matrix[1],matrix[2]},x); vector((eng_vector_t){matrix[4],matrix[5],matrix[6]},y); vector((eng_vector_t){matrix[8],matrix[9],matrix[10]},z);
}

static void basis(orientation_fixture_t *g, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    runtime_basis(g->f.engine,g->target,x,y,z);
}

static void trigger(orientation_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

TEST(engine_set_orientation, aligns_six_direction_axes_and_keeps_world_scale_without_internal_scheduling)
{
    const eng_vector_t directions[] = {{7,0,0},{-7,0,0},{0,7,0},{0,-7,0},{0,0,7},{0,0,-7}};
    const eng_vector_t right[] = {{0,0,-2},{0,0,2},{-2,0,0},{2,0,0},{2,0,0},{-2,0,0}};
    const eng_vector_t up[] = {{0,3,0},{0,3,0},{0,0,3},{0,0,3},{0,3,0},{0,3,0}};
    for (size_t i = 0; i < COUNT(directions); i++) {
        orientation_fixture_t g; begin(&g); input(&g,0,directions[i]); if (i == 2 || i == 3) input(&g,1,(eng_vector_t){0,0,3}); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); eng_vector_t dir = directions[i]; dir.x *= 4.0f/7; dir.y *= 4.0f/7; dir.z *= 4.0f/7;
        basis(&g,right[i],up[i],dir); position(&g,g.target,(eng_vector_t){3,4,5});
        ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

TEST(engine_set_orientation, removes_parallel_up_components_and_normalizes_both_desired_and_local_frames)
{
    const float magnitudes[] = {1,1e-30f};
    for (size_t i = 0; i < COUNT(magnitudes); i++) {
        float m = magnitudes[i]; orientation_fixture_t g; begin(&g);
        input(&g,0,(eng_vector_t){0,0,7*m}); input(&g,1,(eng_vector_t){0,6*m,11*m});
        input(&g,4,(eng_vector_t){0,0,9*m}); input(&g,5,(eng_vector_t){0,3*m,19*m}); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4}); fixture_end(&g.f);
    }
}

TEST(engine_set_orientation, maps_custom_local_axes_onto_the_desired_frame)
{
    const eng_vector_t local_dir[] = {{1,0,0},{0,1,0},{0,0,-1}}, local_up[] = {{0,1,0},{0,0,1},{0,1,0}};
    const eng_vector_t x[] = {{2,0,0},{0,-2,0},{0,0,2}}, y[] = {{0,3,0},{3,0,0},{0,3,0}}, z[] = {{0,0,4},{0,0,4},{-4,0,0}};
    for (size_t i = 0; i < COUNT(local_dir); i++) {
        orientation_fixture_t g; begin(&g); input(&g,0,(eng_vector_t){1,0,0}); input(&g,1,i == 1 ? (eng_vector_t){0,0,1} : (eng_vector_t){0,1,0});
        input(&g,4,local_dir[i]); input(&g,5,local_up[i]); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,x[i],y[i],z[i]); fixture_end(&g.f);
    }
}

TEST(engine_set_orientation, arbitrary_local_frames_align_model_vectors_before_world_scale_is_restored)
{
    orientation_fixture_t g; begin(&g); input(&g,4,(eng_vector_t){1,1,0}); input(&g,5,(eng_vector_t){0,0,3});
    nmo_3dentity_state_t *saved = fixture_state(&g.f,g.target); saved->world_matrix[0] = saved->world_matrix[5] = saved->world_matrix[10] = 1; commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float h = sqrtf(.5f);
    basis(&g,(eng_vector_t){-h,0,h},(eng_vector_t){h,0,h},(eng_vector_t){0,1,0});
    eng_vector_t world_dir, world_up, local_dir = {1,1,0}, local_up = {0,0,3};
    eng_entity_transform_vector(g.f.engine,eng_entity_index(g.f.engine,g.target),&world_dir,&local_dir,-1,false);
    eng_entity_transform_vector(g.f.engine,eng_entity_index(g.f.engine,g.target),&world_up,&local_up,-1,false);
    eng_vector_normalize(&world_dir); eng_vector_normalize(&world_up); vector(world_dir,(eng_vector_t){0,0,1}); vector(world_up,(eng_vector_t){0,1,0}); fixture_end(&g.f);
}

TEST(engine_set_orientation, transforms_axes_through_scaled_and_sheared_references_without_reorthogonalizing_them)
{
    for (int shear = 0; shear < 2; shear++) {
        orientation_fixture_t g; begin(&g); input(&g,0,(eng_vector_t){1,0,1}); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),(int32_t)g.reference);
        nmo_3dentity_state_t *reference = fixture_state(&g.f,g.reference);
        reference->world_matrix[0] = shear ? 2.0f : 0.0f; reference->world_matrix[1] = shear ? 1.0f : 2.0f;
        reference->world_matrix[4] = shear ? 0.0f : -3.0f; reference->world_matrix[5] = shear ? 3.0f : 0.0f; reference->world_matrix[10] = 4; commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        if (shear) { float root = sqrtf(21); basis(&g,(eng_vector_t){4/root,2/root,-8/root},(eng_vector_t){0,3,0},(eng_vector_t){8/root,4/root,16/root}); }
        else { float root = sqrtf(5); basis(&g,(eng_vector_t){0,2/root,-4/root},(eng_vector_t){-3,0,0},(eng_vector_t){0,4/root,8/root}); }
        position(&g,g.target,(eng_vector_t){3,4,5}); fixture_end(&g.f);
    }
}

TEST(engine_set_orientation, hierarchy_updates_children_under_scaled_parents_while_preserving_world_scale)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        orientation_fixture_t g; begin(&g); input(&g,0,(eng_vector_t){1,0,0}); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),hierarchy); commit(&g);
        eng_matrix_t parent; eng_matrix_from_euler(&parent,.2f,-.7f,.3f);
        for (int j = 0; j < 3; j++) { parent.m[0][j] *= 2; parent.m[1][j] *= 3; parent.m[2][j] *= 4; }
        parent.m[3][0] = 100; eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.parent),&parent,true);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
        position(&g,g.child,hierarchy ? (eng_vector_t){7,7,3} : (eng_vector_t){5,7,9}); position(&g,g.target,(eng_vector_t){3,4,5});
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),false),(eng_vector_t){2,3,4});
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f);
    }
}

TEST(engine_set_orientation, self_references_snapshot_all_axes_before_applying_the_new_frame)
{
    orientation_fixture_t g; begin(&g); input(&g,0,(eng_vector_t){1,0,0}); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),(int32_t)g.target); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
    trigger(&g); basis(&g,(eng_vector_t){-2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,-4}); position(&g,g.target,(eng_vector_t){3,4,5}); fixture_end(&g.f);
}

TEST(engine_set_orientation, mirrored_and_collapsed_axes_use_native_positive_world_scale_lengths)
{
    for (int collapsed = 0; collapsed < 2; collapsed++) {
        orientation_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
        target->world_matrix[0] = collapsed ? 0.0f : -2.0f; target->world_matrix[5] = collapsed ? 0.0f : 3.0f; target->world_matrix[10] = -4; commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        eng_vector_t scale = eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),false);
        ASSERT_FLOAT_EQ(collapsed ? 1e-6f : 2,scale.x,1e-10f); ASSERT_FLOAT_EQ(collapsed ? 1e-6f : 3,scale.y,1e-10f); ASSERT_FLOAT_EQ(4,scale.z,1e-6f);
        float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix)); ASSERT_TRUE(matrix[0] > 0 && matrix[5] > 0 && matrix[10] > 0); fixture_end(&g.f);
    }
}

TEST(engine_set_orientation, zero_and_parallel_frames_preserve_native_nan_axes_and_leave_position_unchanged)
{
    const int indices[] = {0,1,4,5}; const eng_vector_t values[] = {{0,0,0},{0,0,2},{0,0,0},{0,0,-2}};
    for (size_t i = 0; i < COUNT(indices); i++) {
        orientation_fixture_t g; begin(&g); input(&g,indices[i],values[i]); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),0); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix));
        for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++) ASSERT_TRUE(isnan(matrix[4*row+column]));
        position(&g,g.target,(eng_vector_t){3,4,5}); position(&g,g.child,(eng_vector_t){5,7,9}); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_set_orientation, missing_deleted_and_nonentity_references_use_world_axes)
{
    for (int kind = 0; kind < 3; kind++) {
        orientation_fixture_t g; begin(&g); input(&g,0,(eng_vector_t){1,0,0});
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,3),kind == 1 ? (int32_t)g.reference : kind == 2 ? (int32_t)g.block : 0);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.reference);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0}); fixture_end(&g.f);
    }
}

TEST(engine_set_orientation, disconnected_inputs_keep_native_axis_and_hierarchy_defaults)
{
    orientation_fixture_t g; begin(&g); input(&g,0,(eng_vector_t){1,0,0});
    nmo_object_id_t target = nmo_parameterin_source_id(fixture_state(&g.f,fixture_target(&g.f,g.block))), direction = source(&g,0); size_t kept = 0;
    for (size_t i = 0; i < g.f.value_count; i++) if (g.f.values[i].param == target || g.f.values[i].param == direction) g.f.values[kept++] = g.f.values[i];
    g.f.value_count = kept;
    for (int i = 1; i < 6; i++) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,i)),0);
    commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); basis(&g,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
    position(&g,g.child,(eng_vector_t){7,7,3}); fixture_end(&g.f);
}

TEST(engine_set_orientation, missing_and_deleted_targets_report_owner_errors_before_consuming_input)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        orientation_fixture_t g; begin(&g); if (!deleted) fixture_input_int(&g.f,fixture_target(&g.f,g.block),0);
        commit(&g); if (deleted) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

static nmo_object_id_t saved_input(orientation_fixture_t *g, nmo_object_id_t pin, const char *text)
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

TEST(engine_set_orientation, loaded_and_copied_graphs_keep_local_axes_references_and_hierarchy_independent)
{
    for (int custom = 0; custom < 2; custom++) for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        orientation_fixture_t g; begin(&g); nmo_object_id_t parameters[7]; char text[32];
        snprintf(text,sizeof(text),"%u",g.target); parameters[0] = saved_input(&g,fixture_target(&g.f,g.block),text);
        parameters[1] = saved_input(&g,BLOCK_PIN(&g.f,g.block,0),"1,0,1"); parameters[2] = saved_input(&g,BLOCK_PIN(&g.f,g.block,1),"0,1,0");
        parameters[3] = saved_input(&g,BLOCK_PIN(&g.f,g.block,2),hierarchy ? "TRUE" : "FALSE");
        snprintf(text,sizeof(text),"%u",g.reference); parameters[4] = saved_input(&g,BLOCK_PIN(&g.f,g.block,3),text);
        parameters[5] = saved_input(&g,BLOCK_PIN(&g.f,g.block,4),custom ? "1,0,0" : "0,0,1"); parameters[6] = saved_input(&g,BLOCK_PIN(&g.f,g.block,5),"0,1,0");
        for (size_t i = 0; i < COUNT(parameters); i++) ASSERT_TRUE(parameters[i] != 0);
        nmo_3dentity_state_t *reference = fixture_state(&g.f,g.reference);
        reference->world_matrix[0] = 2; reference->world_matrix[5] = 3; reference->world_matrix[10] = 4; fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_set_orientation.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"orientation.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Set Orientation",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t original[] = {g.f.owner_id,g.parent,g.target,g.child,g.reference,g.f.script_id,g.block,g.out};
            nmo_object_id_t ids[15] = {0}, copies[15] = {0};
            for (size_t i = 0; i < COUNT(original); i++) { ids[i] = eng_module_id(module,original[i]); ASSERT_TRUE(ids[i] != 0); }
            for (size_t i = 0; i < COUNT(parameters); i++) ids[8+i] = eng_module_id(module,parameters[i]);
            ASSERT_TRUE(ids[2] != g.target); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            float root = sqrtf(5); eng_vector_t x = {2/root,0,(custom ? 4 : -4)/root}, y = {0,3,0}, z = {(custom ? -4 : 4)/root,0,8/root};
            runtime_basis(host.engine,ids[2],x,y,z); expect_position(host.engine,ids[2],(eng_vector_t){3,4,5});
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[2])); ASSERT_EQ(copies[2],nmo_engine_entity_parent(host.engine,copies[3]));
            runtime_basis(host.engine,copies[2],x,y,z);
            eng_matrix_t moved; eng_matrix_identity(&moved); moved.m[3][0] = 100; moved.m[3][1] = 200; moved.m[3][2] = 300;
            eng_entity_set_world_matrix(host.engine,eng_entity_index(host.engine,copies[4]),&moved,false);
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,10));
            float h = sqrtf(2);
            runtime_basis(host.engine,copies[2],(eng_vector_t){h,0,custom ? h : -h},y,(eng_vector_t){custom ? -2*h : 2*h,0,2*h});
            runtime_basis(host.engine,ids[2],x,y,z); expect_position(host.engine,copies[2],(eng_vector_t){3,4,5});
            expect_position(host.engine,copies[3],!hierarchy ? (eng_vector_t){5,7,9} : custom ? (eng_vector_t){3-h,7,5+3*h} : (eng_vector_t){3+3*h,7,5+h});
            nmo_engine_destroy_object(host.engine,ids[2]); nmo_engine_destroy_object(host.engine,ids[4]); nmo_engine_destroy_object(host.engine,ids[1]);
            eng_vector_t backward = {0,0,-1}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&backward,sizeof(backward)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            runtime_basis(host.engine,copies[2],custom ? (eng_vector_t){0,0,-2} : (eng_vector_t){-2,0,0},y,custom ? (eng_vector_t){4,0,0} : (eng_vector_t){0,0,-4});
            expect_position(host.engine,copies[3],!hierarchy ? (eng_vector_t){5,7,9} : custom ? (eng_vector_t){7,7,3} : (eng_vector_t){1,7,1});
            ASSERT_EQ(1u,runs(&host,ids[6])); ASSERT_EQ(2u,runs(&host,copies[6])); fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(3,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[12],0); fixture_end(&g.f); remove(path);
    }
}

TEST(engine_set_orientation, bezier_progression_and_vector_interpolation_drive_custom_local_axes_through_completion)
{
    orientation_fixture_t g; begin(&g); input(&g,4,(eng_vector_t){1,0,0});
    const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
    nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
    nmo_object_id_t interpolate = fixture_block(&g.f,NMO_GUID(0x35503950,0x2dde7a65),"Interpolate Direction"); ASSERT_TRUE(interpolate != 0);
    for (int i = 0; i < 3; i++) ASSERT_EQ(NMO_OK,nmo_script_edit_remove_parameter(g.f.tx,BLOCK_PIN(&g.f,interpolate,0),true));
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_parameter(g.f.tx,BLOCK_POUT(&g.f,interpolate,0),true));
    nmo_object_id_t a = 0, b = 0, mix = 0, result = 0;
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,interpolate,NMO_SCRIPT_EDIT_PARAM_IN,CKPGUID_VECTOR,"A",&a));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,interpolate,NMO_SCRIPT_EDIT_PARAM_IN,CKPGUID_VECTOR,"B",&b));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,interpolate,NMO_SCRIPT_EDIT_PARAM_IN,CKPGUID_PERCENTAGE,"Value",&mix));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,interpolate,NMO_SCRIPT_EDIT_PARAM_OUT,CKPGUID_VECTOR,"C",&result));
    eng_vector_t start = {0,0,1}, end = {1,0,0}; fixture_input_value(&g.f,a,&start,sizeof(start)); fixture_input_value(&g.f,b,&end,sizeof(end));
    nmo_object_id_t previous = source(&g,0); size_t kept = 0;
    for (size_t i = 0; i < g.f.value_count; i++) if (g.f.values[i].param != previous) g.f.values[kept++] = g.f.values[i];
    g.f.value_count = kept;
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,result,BLOCK_PIN(&g.f,g.block,0)));
    nmo_object_id_t progress = fixture_block(&g.f,NMO_GUID(0x6bb8699a,0x29fb6a4b),"Progress"); ASSERT_TRUE(progress != 0);
    const float values[] = {40,0,1};
    for (size_t i = 0; i < COUNT(values); i++) fixture_input_value(&g.f,BLOCK_PIN(&g.f,progress,i),&values[i],sizeof(float));
    nmo_object_id_t curve = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,progress,3),&curve));
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,progress,3),mix));
    fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,progress,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,progress,1),BLOCK_IN(&g.f,progress,1),1);
    for (int output = 0; output < 2; output++) fixture_link(&g.f,BLOCK_OUT(&g.f,progress,output),BLOCK_IN(&g.f,interpolate,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,interpolate,0),BLOCK_IN(&g.f,g.block,0),0); commit(&g);
    for (int step = 1; step <= 5; step++) {
        float p = step < 4 ? (float)step*.25f : 1, length = sqrtf(p*p+(1-p)*(1-p)), x = p/length, z = (1-p)/length;
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); eng_vector_t current; read_value(&g.f,result,&current,sizeof(current)); vector(current,(eng_vector_t){p,0,1-p});
        basis(&g,(eng_vector_t){2*x,0,2*z},(eng_vector_t){0,3,0},(eng_vector_t){-4*z,0,4*x});
        position(&g,g.target,(eng_vector_t){3,4,5}); position(&g,g.child,(eng_vector_t){3+2*x-4*z,7,5+2*z+4*x});
    }
    ASSERT_EQ(5u,runs(&g.f,progress)); ASSERT_EQ(5u,runs(&g.f,interpolate)); ASSERT_EQ(5u,runs(&g.f,g.block)); ASSERT_EQ(5u,runs(&g.f,g.out));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(5u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_set_orientation, aligns_six_direction_axes_and_keeps_world_scale_without_internal_scheduling);
REGISTER_TEST(engine_set_orientation, removes_parallel_up_components_and_normalizes_both_desired_and_local_frames);
REGISTER_TEST(engine_set_orientation, maps_custom_local_axes_onto_the_desired_frame);
REGISTER_TEST(engine_set_orientation, arbitrary_local_frames_align_model_vectors_before_world_scale_is_restored);
REGISTER_TEST(engine_set_orientation, transforms_axes_through_scaled_and_sheared_references_without_reorthogonalizing_them);
REGISTER_TEST(engine_set_orientation, hierarchy_updates_children_under_scaled_parents_while_preserving_world_scale);
REGISTER_TEST(engine_set_orientation, self_references_snapshot_all_axes_before_applying_the_new_frame);
REGISTER_TEST(engine_set_orientation, mirrored_and_collapsed_axes_use_native_positive_world_scale_lengths);
REGISTER_TEST(engine_set_orientation, zero_and_parallel_frames_preserve_native_nan_axes_and_leave_position_unchanged);
REGISTER_TEST(engine_set_orientation, missing_deleted_and_nonentity_references_use_world_axes);
REGISTER_TEST(engine_set_orientation, disconnected_inputs_keep_native_axis_and_hierarchy_defaults);
REGISTER_TEST(engine_set_orientation, missing_and_deleted_targets_report_owner_errors_before_consuming_input);
REGISTER_TEST(engine_set_orientation, loaded_and_copied_graphs_keep_local_axes_references_and_hierarchy_independent);
REGISTER_TEST(engine_set_orientation, bezier_progression_and_vector_interpolation_drive_custom_local_axes_through_completion);
TEST_MAIN_END()
