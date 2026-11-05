#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_LOOK_POS NMO_GUID(0xee6e5e6e,0xe6e6e6e6)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct look_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, target, child, grandchild, block, out, target_parameter, point_parameter, speed_parameter;
} look_fixture_t;

static void point(look_fixture_t *g, eng_vector_t value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->block,0),&value,sizeof(value));
}

static void begin(look_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Parent"); g->target = fixture_create(&g->f,NMO_CID_3DENTITY,"Looking Entity");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child"); g->grandchild = fixture_create(&g->f,NMO_CID_3DENTITY,"Grandchild");
    nmo_3dentity_state_t *parent = fixture_state(&g->f,g->parent), *target = fixture_state(&g->f,g->target);
    nmo_3dentity_state_t *child = fixture_state(&g->f,g->child), *grand = fixture_state(&g->f,g->grandchild);
    ASSERT_NOT_NULL(parent); ASSERT_NOT_NULL(target); ASSERT_NOT_NULL(child); ASSERT_NOT_NULL(grand);
    parent->has_entityndata_chunk = target->has_entityndata_chunk = child->has_entityndata_chunk = grand->has_entityndata_chunk = 1;
    parent->world_matrix[0] = 2; parent->world_matrix[5] = 3; parent->world_matrix[10] = 4;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4;
    target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 5;
    child->parent = nmo_ref_from_id(g->target); child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 9;
    grand->parent = nmo_ref_from_id(g->child); grand->world_matrix[12] = 6; grand->world_matrix[13] = 9; grand->world_matrix[14] = 12;
    g->f.script_id = fixture_add_script(&g->f,"Look Pos Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_LOOK_POS,"Look At Pos"); g->out = fixture_block(&g->f,GUID_NOP,"After Look"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->target); point(g,(eng_vector_t){8,4,5});
    g->target_parameter = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    g->point_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,0)));
    g->speed_parameter = nmo_parameterin_source_id(fixture_state(&g->f,BLOCK_PIN(&g->f,g->block,1)));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void commit(look_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_LOOK_POS));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,2e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,2e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,2e-4f);
}

static void position(nmo_engine_t *engine, nmo_object_id_t target, eng_vector_t expected)
{
    vector(eng_entity_position(engine,eng_entity_index(engine,target),-1),expected);
}

static void basis(nmo_engine_t *engine, nmo_object_id_t target, eng_vector_t x, eng_vector_t y, eng_vector_t z)
{
    float m[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,target,m));
    vector((eng_vector_t){m[0],m[1],m[2]},x); vector((eng_vector_t){m[4],m[5],m[6]},y); vector((eng_vector_t){m[8],m[9],m[10]},z);
}

static void trigger(look_fixture_t *g, float delta)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,delta));
}

TEST(engine_look_at_pos, default_speed_looks_at_world_points_and_moves_descendants_without_scheduling)
{
    look_fixture_t g; begin(&g); commit(&g); float speed = 0; read_value(&g.f,g.speed_parameter,&speed,sizeof(speed)); ASSERT_FLOAT_EQ(1,speed,0);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    basis(g.f.engine,g.target,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
    position(g.f.engine,g.target,(eng_vector_t){3,4,5}); position(g.f.engine,g.child,(eng_vector_t){7,7,3}); position(g.f.engine,g.grandchild,(eng_vector_t){10,9,2});
    ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1000)); ASSERT_EQ(1u,runs(&g.f,g.block));
    ASSERT_FLOAT_EQ(2,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[0],0); fixture_end(&g.f);
}

TEST(engine_look_at_pos, following_speed_interpolates_or_extrapolates_normalized_directions_independently_of_delta)
{
    const float speeds[] = {0,.25f,.5f,1,2,-1}, deltas[] = {0,1,1000};
    for (size_t s = 0; s < COUNT(speeds); s++) for (size_t d = 0; d < COUNT(deltas); d++) {
        look_fixture_t g; begin(&g); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,1),&speeds[s],sizeof(float)); commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,deltas[d]));
        float n = hypotf(speeds[s],1-speeds[s]), x = speeds[s]/n, z = (1-speeds[s])/n;
        basis(g.f.engine,g.target,(eng_vector_t){2*z,0,-2*x},(eng_vector_t){0,3,0},(eng_vector_t){4*x,0,4*z});
        position(g.f.engine,g.target,(eng_vector_t){3,4,5}); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, hierarchy_can_keep_children_in_place_for_an_unparented_target)
{
    look_fixture_t g; begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),0); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    basis(g.f.engine,g.target,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
    position(g.f.engine,g.child,(eng_vector_t){5,7,9}); position(g.f.engine,g.grandchild,(eng_vector_t){6,9,12}); fixture_end(&g.f);
}

TEST(engine_look_at_pos, local_scale_restoration_after_orientation_also_propagates_when_hierarchy_is_disabled)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        look_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
        target->parent = nmo_ref_from_id(g.parent); target->world_matrix[0] = 4; target->world_matrix[5] = 9; target->world_matrix[10] = 16;
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),hierarchy); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        basis(g.f.engine,g.target,(eng_vector_t){0,0,-8},(eng_vector_t){0,9,0},(eng_vector_t){8,0,0});
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,g.target),true),(eng_vector_t){2,3,4});
        position(g.f.engine,g.child,hierarchy ? (eng_vector_t){5,7,1} : (eng_vector_t){4,7,13});
        position(g.f.engine,g.grandchild,hierarchy ? (eng_vector_t){6.5f,9,-1} : (eng_vector_t){4.5f,9,19}); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, coincident_positions_leave_rolled_or_sheared_matrices_and_children_unchanged)
{
    look_fixture_t g; begin(&g); point(&g,(eng_vector_t){3,4,5}); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
    target->world_matrix[1] = .5f; target->world_matrix[4] = -.25f; commit(&g);
    const nmo_object_id_t ids[] = {g.target,g.child,g.grandchild}; float before[COUNT(ids)][16];
    for (size_t i = 0; i < COUNT(ids); i++) ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,ids[i],before[i]));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    for (size_t i = 0; i < COUNT(ids); i++) { float after[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,ids[i],after)); ASSERT_EQ(0,memcmp(before[i],after,sizeof(after))); }
    ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_look_at_pos, tiny_and_large_nonzero_displacements_are_normalized_without_a_cutoff)
{
    const float sizes[] = {1e-35f,1e35f};
    for (size_t i = 0; i < COUNT(sizes); i++) {
        look_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
        target->world_matrix[12] = target->world_matrix[13] = target->world_matrix[14] = 0;
        point(&g,(eng_vector_t){sizes[i],0,0}); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        basis(g.f.engine,g.target,(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0}); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, vertical_opposing_and_zero_old_directions_retain_native_nan_propagation)
{
    for (int kind = 0; kind < 3; kind++) {
        look_fixture_t g; begin(&g);
        if (kind == 0) point(&g,(eng_vector_t){3,9,5});
        if (kind == 1) { const float half = .5f; point(&g,(eng_vector_t){3,4,0}); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,1),&half,sizeof(half)); }
        if (kind == 2) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[10] = 0;
        commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float matrix[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.target,matrix));
        for (int axis = 0; axis < (kind == 0 ? 2 : 3); axis++) for (int c = 0; c < 3; c++) ASSERT_TRUE(isnan(matrix[4*axis+c]));
        if (kind == 0) { ASSERT_TRUE(isfinite(matrix[8]) && isfinite(matrix[9]) && isfinite(matrix[10])); ASSERT_FLOAT_EQ(4,matrix[9],1e-5f); }
        ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, live_positions_speeds_and_targets_are_read_on_each_activation)
{
    look_fixture_t g; begin(&g); commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    eng_vector_t p = {3,4,0}; float half = .5f;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.point_parameter,&p,sizeof(p)));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.speed_parameter,&half,sizeof(half))); trigger(&g,1000);
    float h = sqrtf(.5f); basis(g.f.engine,g.target,(eng_vector_t){-2*h,0,-2*h},(eng_vector_t){0,3,0},(eng_vector_t){4*h,0,-4*h});
    p = (eng_vector_t){5,0,0}; float one = 1;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.point_parameter,&p,sizeof(p)));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.speed_parameter,&one,sizeof(one)));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_parameter,&g.f.owner_id,sizeof(g.f.owner_id))); trigger(&g,0);
    basis(g.f.engine,g.f.owner_id,(eng_vector_t){0,0,-1},(eng_vector_t){0,1,0},(eng_vector_t){1,0,0}); ASSERT_EQ(3u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_look_at_pos, missing_deleted_and_nonentity_targets_consume_input_and_activate_output_before_owner_error)
{
    for (int kind = 0; kind < 3; kind++) {
        look_fixture_t g; begin(&g); if (kind != 1) fixture_input_int(&g.f,fixture_target(&g.f,g.block),kind == 0 ? 0 : (int32_t)g.block);
        commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.target);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); fixture_end(&g.f);
    }
}

TEST(engine_look_at_pos, zero_speed_still_removes_roll_using_world_up)
{
    look_fixture_t g; begin(&g); nmo_3dentity_state_t *target = fixture_state(&g.f,g.target);
    target->world_matrix[0] = target->world_matrix[5] = 0; target->world_matrix[1] = 2; target->world_matrix[4] = -3;
    const float zero = 0; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,1),&zero,sizeof(zero)); commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    basis(g.f.engine,g.target,(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
    position(g.f.engine,g.child,(eng_vector_t){6,2,9}); position(g.f.engine,g.grandchild,(eng_vector_t){8,1,12}); fixture_end(&g.f);
}

static nmo_object_id_t saved_value(look_fixture_t *g, nmo_object_id_t pin, const char *value)
{
    nmo_object_id_t source = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,pin,&source) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,source,value) != NMO_OK) return 0;
    return source;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t loaded_source(nmo_engine_t *engine, nmo_object_id_t block, int pos)
{
    nmo_object_t *object = eng_find_object(engine,block);
    const nmo_behavior_state_t *behavior = object != NULL ? nmo_object_get_state(object) : NULL;
    const eng_module_t *module = eng_module_of(engine,block,NULL);
    if (behavior == NULL || module == NULL) return 0;
    nmo_object_id_t pin = pos < 0 ? nmo_behavior_target_parameter_id(behavior) : nmo_behavior_ref_array_get_id(&behavior->in_parameters,(size_t)pos);
    object = eng_find_object(engine,eng_module_id(module,pin));
    const nmo_parameterin_state_t *input = object != NULL ? nmo_object_get_state(object) : NULL;
    return input != NULL ? eng_module_id(module,nmo_parameterin_source_id(input)) : 0;
}

TEST(engine_look_at_pos, loaded_and_copied_graphs_keep_targets_positions_speeds_and_hierarchy_independent)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        look_fixture_t g; begin(&g); char text[32]; snprintf(text,sizeof(text),"%u",g.target);
        ASSERT_TRUE(saved_value(&g,fixture_target(&g.f,g.block),text) != 0);
        ASSERT_TRUE(saved_value(&g,BLOCK_PIN(&g.f,g.block,0),"8,4,5") != 0);
        /* The value editor stores the fraction; prototype defaults use percent text. */
        ASSERT_TRUE(saved_value(&g,BLOCK_PIN(&g.f,g.block,1),"1") != 0);
        ASSERT_TRUE(saved_value(&g,BLOCK_PIN(&g.f,g.block,2),hierarchy ? "TRUE" : "FALSE") != 0);
        fixture_commit_bare(&g.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_look_at_pos.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"look-at-pos.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Look At Pos",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const char *names[] = {"Owner","Parent","Looking Entity","Child","Grandchild","Look Pos Script","Look At Pos","After Look"};
            nmo_object_id_t ids[12] = {0}, copies[12] = {0};
            for (size_t i = 0; i < COUNT(names); i++) {
                nmo_object_t *object = nmo_object_repository_find_by_name(module->repo,names[i]); ASSERT_NOT_NULL(object);
                ids[i] = eng_module_id(module,nmo_object_get_id(object)); ASSERT_TRUE(ids[i] != 0);
            }
            for (int pos = -1; pos < 3; pos++) { ids[9+pos] = loaded_source(host.engine,ids[6],pos); ASSERT_TRUE(ids[9+pos] != 0); }
            float speed = 0; read_value(&host,ids[10],&speed,sizeof(speed)); ASSERT_FLOAT_EQ(1,speed,0);
            ASSERT_TRUE(ids[2] != g.target); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            basis(host.engine,ids[2],(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
            position(host.engine,ids[3],hierarchy ? (eng_vector_t){7,7,3} : (eng_vector_t){5,7,9});
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies));
            for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
            ASSERT_EQ(copies[2],nmo_engine_entity_parent(host.engine,copies[3])); ASSERT_EQ(copies[3],nmo_engine_entity_parent(host.engine,copies[4]));
            nmo_object_id_t target_reference = 0; read_value(&host,copies[8],&target_reference,sizeof(target_reference)); ASSERT_EQ(copies[2],target_reference);
            eng_vector_t forward = {3,4,10}; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&forward,sizeof(forward)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,ids[5],true,true));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            basis(host.engine,ids[2],(eng_vector_t){0,0,-2},(eng_vector_t){0,3,0},(eng_vector_t){4,0,0});
            basis(host.engine,copies[2],(eng_vector_t){2,0,0},(eng_vector_t){0,3,0},(eng_vector_t){0,0,4});
            position(host.engine,copies[3],(eng_vector_t){5,7,9}); position(host.engine,copies[4],(eng_vector_t){6,9,12});
            eng_vector_t original; read_value(&host,ids[9],&original,sizeof(original)); vector(original,(eng_vector_t){8,4,5});
            nmo_engine_destroy_object(host.engine,ids[2]); nmo_engine_destroy_object(host.engine,ids[0]);
            const eng_vector_t right = {8,4,5}; const float half = .5f;
            const int32_t copied_hierarchy = !hierarchy;
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&right,sizeof(right)));
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],&half,sizeof(half)));
            ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[11],&copied_hierarchy,sizeof(copied_hierarchy)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            float h = sqrtf(.5f); basis(host.engine,copies[2],(eng_vector_t){2*h,0,-2*h},(eng_vector_t){0,3,0},(eng_vector_t){4*h,0,4*h});
            position(host.engine,copies[3],copied_hierarchy ? (eng_vector_t){3+6*h,7,5+2*h} : (eng_vector_t){5,7,9});
            position(host.engine,copies[4],copied_hierarchy ? (eng_vector_t){3+10*h,9,5+4*h} : (eng_vector_t){6,9,12});
            int32_t original_hierarchy = -1; read_value(&host,ids[11],&original_hierarchy,sizeof(original_hierarchy)); ASSERT_EQ(hierarchy,original_hierarchy);
            ASSERT_EQ(2u,runs(&host,copies[7])); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100)); ASSERT_EQ(2u,runs(&host,copies[6])); fixture_end(&host);
        }
        ASSERT_FLOAT_EQ(2,((nmo_3dentity_state_t *)fixture_state(&g.f,g.target))->world_matrix[0],0); fixture_end(&g.f); remove(path);
    }
}

TEST(engine_look_at_pos, bezier_progression_drives_repeated_direction_blends_until_completion)
{
    look_fixture_t g; begin(&g); const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id);
    nmo_object_id_t first_link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,first_link));
    nmo_object_id_t progress = fixture_block(&g.f,NMO_GUID(0x6bb8699a,0x29fb6a4b),"Progress"); ASSERT_TRUE(progress != 0);
    const float values[] = {40,0,1};
    for (size_t i = 0; i < COUNT(values); i++) fixture_input_value(&g.f,BLOCK_PIN(&g.f,progress,i),&values[i],sizeof(float));
    nmo_object_id_t curve = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,progress,3),&curve));
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,progress,3),BLOCK_PIN(&g.f,g.block,1)));
    fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,progress,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,progress,1),BLOCK_IN(&g.f,progress,1),1);
    for (int out = 0; out < 2; out++) fixture_link(&g.f,BLOCK_OUT(&g.f,progress,out),BLOCK_IN(&g.f,g.block,0),0);
    commit(&g);
    /* Successive unit X/Z directions for weights 1/4, 1/2, 3/4, 1, 1. */
    const float directions[][2] = {{.316227766017f,.948683298051f},{.811242185176f,.584710284664f},{.988435325376f,.151643026707f},{1,0},{1,0}};
    for (size_t frame = 0; frame < COUNT(directions); frame++) {
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); float x = directions[frame][0], z = directions[frame][1];
        basis(g.f.engine,g.target,(eng_vector_t){2*z,0,-2*x},(eng_vector_t){0,3,0},(eng_vector_t){4*x,0,4*z});
        position(g.f.engine,g.child,(eng_vector_t){3+2*z+4*x,7,5-2*x+4*z}); position(g.f.engine,g.target,(eng_vector_t){3,4,5});
    }
    ASSERT_EQ(5u,runs(&g.f,progress)); ASSERT_EQ(5u,runs(&g.f,g.block)); ASSERT_EQ(5u,runs(&g.f,g.out));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(5u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_look_at_pos, default_speed_looks_at_world_points_and_moves_descendants_without_scheduling);
REGISTER_TEST(engine_look_at_pos, following_speed_interpolates_or_extrapolates_normalized_directions_independently_of_delta);
REGISTER_TEST(engine_look_at_pos, hierarchy_can_keep_children_in_place_for_an_unparented_target);
REGISTER_TEST(engine_look_at_pos, local_scale_restoration_after_orientation_also_propagates_when_hierarchy_is_disabled);
REGISTER_TEST(engine_look_at_pos, coincident_positions_leave_rolled_or_sheared_matrices_and_children_unchanged);
REGISTER_TEST(engine_look_at_pos, tiny_and_large_nonzero_displacements_are_normalized_without_a_cutoff);
REGISTER_TEST(engine_look_at_pos, vertical_opposing_and_zero_old_directions_retain_native_nan_propagation);
REGISTER_TEST(engine_look_at_pos, live_positions_speeds_and_targets_are_read_on_each_activation);
REGISTER_TEST(engine_look_at_pos, missing_deleted_and_nonentity_targets_consume_input_and_activate_output_before_owner_error);
REGISTER_TEST(engine_look_at_pos, zero_speed_still_removes_roll_using_world_up);
REGISTER_TEST(engine_look_at_pos, loaded_and_copied_graphs_keep_targets_positions_speeds_and_hierarchy_independent);
REGISTER_TEST(engine_look_at_pos, bezier_progression_drives_repeated_direction_blends_until_completion);
TEST_MAIN_END()
