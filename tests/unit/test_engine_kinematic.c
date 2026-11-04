#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_kinematicchain_schemas.h"

#include <math.h>

typedef struct ik_fixture {
    engine_fixture_t f;
    nmo_object_id_t root, middle, end;
} ik_fixture_t;

static nmo_object_id_t body(ik_fixture_t *g, const char *name, nmo_object_id_t parent, eng_vector_t p, uint32_t flags)
{
    nmo_object_id_t id = fixture_create(&g->f,NMO_CID_BODYPART,name);
    nmo_bodypart_state_t *state = fixture_state(&g->f,id);
    if (state == NULL) return 0;
    nmo_3dentity_state_t *entity = (nmo_3dentity_state_t *)state;
    entity->parent = nmo_ref_from_id(parent); entity->has_entityndata_chunk = 1;
    entity->world_matrix[12] = p.x; entity->world_matrix[13] = p.y; entity->world_matrix[14] = p.z;
    state->rotation_joint.flags = flags;
    return id;
}

static void begin(ik_fixture_t *g, bool bent, uint32_t axes)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->root = body(g,"Shoulder",g->f.owner_id,(eng_vector_t){0,0,0},axes);
    g->middle = bent ? body(g,"Elbow",g->root,(eng_vector_t){1,0,0},axes) : 0;
    g->end = body(g,"Hand",bent ? g->middle : g->root,(eng_vector_t){1,bent ? 1 : 0,0},7);
    ASSERT_TRUE(g->root != 0 && g->end != 0);
}

static void commit(ik_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL);
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static eng_vector_t position(ik_fixture_t *g, nmo_object_id_t id)
{
    return eng_entity_position(g->f.engine,eng_entity_index(g->f.engine,id),-1);
}

static void near(eng_vector_t actual, eng_vector_t expected, float tolerance)
{
    eng_vector_t difference = {actual.x-expected.x,actual.y-expected.y,actual.z-expected.z};
    ASSERT_TRUE(eng_vector_magnitude(&difference) <= tolerance);
}

static float distance(eng_vector_t a, eng_vector_t b)
{
    eng_vector_t delta = {a.x-b.x,a.y-b.y,a.z-b.z}; return eng_vector_magnitude(&delta);
}

static eng_matrix_t local(ik_fixture_t *g, nmo_object_id_t id)
{
    return g->f.engine->entities.data[eng_entity_index(g->f.engine,id)].local;
}

static void same_matrix(const eng_matrix_t *actual, const eng_matrix_t *expected)
{
    for (int row = 0; row < 4; row++) for (int column = 0; column < 4; column++)
        ASSERT_FLOAT_EQ(expected->m[row][column],actual->m[row][column],1e-6f);
}

TEST(engine_kinematic, named_axes_reach_circular_targets_without_moving_the_pivot_or_stretching)
{
    for (int axis = 0; axis < 3; axis++) {
        ik_fixture_t g; begin(&g,false,1u<<axis);
        nmo_3dentity_state_t *end = fixture_state(&g.f,g.end);
        const eng_vector_t offsets[] = {{0,1,0},{0,0,1},{1,0,0}};
        const eng_vector_t targets[] = {{0,cosf(.5f),sinf(.5f)},{sinf(.5f),0,cosf(.5f)},{cosf(.5f),sinf(.5f),0}};
        end->world_matrix[12] = offsets[axis].x; end->world_matrix[13] = offsets[axis].y; end->world_matrix[14] = offsets[axis].z;
        commit(&g); eng_matrix_t end_local = local(&g,g.end);
        ASSERT_TRUE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&targets[axis]));
        near(position(&g,g.end),targets[axis],.05f); near(position(&g,g.root),(eng_vector_t){0,0,0},1e-6f);
        ASSERT_FLOAT_EQ(1,distance(position(&g,g.root),position(&g,g.end)),1e-5f);
        eng_matrix_t actual = local(&g,g.end); same_matrix(&actual,&end_local); fixture_end(&g.f);
    }
}

TEST(engine_kinematic, rigid_segments_project_both_interior_and_unreachable_targets_to_their_radius)
{
    const float radii[] = {.25f,3};
    for (size_t i = 0; i < sizeof(radii)/sizeof(radii[0]); i++) {
        ik_fixture_t g; begin(&g,false,4); commit(&g);
        eng_vector_t direction = {cosf(.6f),sinf(.6f),0};
        eng_vector_t target = {direction.x*radii[i],direction.y*radii[i],0}, original = target;
        ASSERT_TRUE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&target));
        near(position(&g,g.end),direction,.05f); near(target,original,0); fixture_end(&g.f);
    }
}

TEST(engine_kinematic, bent_chains_distribute_motion_across_joints_and_keep_local_translations)
{
    ik_fixture_t g; begin(&g,true,4); commit(&g);
    eng_matrix_t original_root = local(&g,g.root), original_middle = local(&g,g.middle), original_end = local(&g,g.end);
    eng_vector_t target = {1.15f,.9f,0}; ASSERT_TRUE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&target));
    near(position(&g,g.end),target,.05f); near(position(&g,g.root),(eng_vector_t){0,0,0},1e-6f);
    ASSERT_FLOAT_EQ(1,distance(position(&g,g.root),position(&g,g.middle)),1e-5f);
    ASSERT_FLOAT_EQ(1,distance(position(&g,g.middle),position(&g,g.end)),1e-5f);
    eng_matrix_t root = local(&g,g.root), middle = local(&g,g.middle), end = local(&g,g.end);
    ASSERT_TRUE(fabsf(root.m[0][1]-original_root.m[0][1]) > .001f);
    ASSERT_TRUE(fabsf(middle.m[0][1]-original_middle.m[0][1]) > .001f);
    for (int axis = 0; axis < 3; axis++) ASSERT_FLOAT_EQ(original_middle.m[3][axis],middle.m[3][axis],1e-6f);
    same_matrix(&end,&original_end); fixture_end(&g.f);
}

TEST(engine_kinematic, transformed_parents_preserve_world_pivots_and_use_the_joint_world_axes)
{
    ik_fixture_t g; begin(&g,false,4); commit(&g);
    eng_matrix_t parent; eng_matrix_from_euler(&parent,0,0,.4f);
    parent.m[3][0] = 4; parent.m[3][1] = -3; parent.m[3][2] = 2;
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),&parent,false);
    eng_vector_t target = {4+cosf(.9f),-3+sinf(.9f),2};
    ASSERT_TRUE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&target)); near(position(&g,g.end),target,.05f);
    near(position(&g,g.root),(eng_vector_t){4,-3,2},1e-5f);
    eng_matrix_t actual = local(&g,g.f.owner_id); same_matrix(&actual,&parent); fixture_end(&g.f);
}

TEST(engine_kinematic, invalid_deleted_and_disconnected_effectors_preserve_the_pose)
{
    for (int kind = 0; kind < 6; kind++) {
        ik_fixture_t g; begin(&g,false,4);
        nmo_object_id_t other = body(&g,"Disconnected",g.f.owner_id,(eng_vector_t){0,1,0},7); commit(&g);
        eng_matrix_t root = local(&g,g.root), end = local(&g,g.end);
        eng_vector_t target = {0,1,0};
        if (kind == 0) ASSERT_FALSE(eng_kinematic_set_position(g.f.engine,g.root,g.end,NULL));
        if (kind == 1) ASSERT_FALSE(eng_kinematic_set_position(g.f.engine,0,g.end,&target));
        if (kind == 2) ASSERT_FALSE(eng_kinematic_set_position(g.f.engine,g.root,g.root,&target));
        if (kind == 3) ASSERT_FALSE(eng_kinematic_set_position(g.f.engine,g.root,other,&target));
        if (kind == 4) { nmo_engine_destroy_object(g.f.engine,g.end); ASSERT_FALSE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&target)); }
        if (kind == 5) { target.x = NAN; ASSERT_FALSE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&target)); }
        eng_matrix_t actual = local(&g,g.root); same_matrix(&actual,&root);
        if (kind != 4) { actual = local(&g,g.end); same_matrix(&actual,&end); }
        fixture_end(&g.f);
    }
}

TEST(engine_kinematic, inactive_joints_and_zero_radius_targets_do_not_change_the_hierarchy)
{
    for (int locked = 0; locked < 2; locked++) {
        ik_fixture_t g; begin(&g,false,locked ? 0 : 4); commit(&g);
        eng_matrix_t root = local(&g,g.root), end = local(&g,g.end);
        eng_vector_t target = {0,locked ? 1 : 0,0};
        ASSERT_FALSE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&target));
        eng_matrix_t actual = local(&g,g.root); same_matrix(&actual,&root); actual = local(&g,g.end); same_matrix(&actual,&end);
        fixture_end(&g.f);
    }
}

TEST(engine_kinematic, limited_joints_reach_valid_angles_and_reject_crossing_their_bounds)
{
    for (int beyond = 0; beyond < 2; beyond++) {
        ik_fixture_t g; begin(&g,false,4|64);
        nmo_bodypart_state_t *root = fixture_state(&g.f,g.root);
        root->rotation_joint.min.z = -.3f; root->rotation_joint.max.z = .3f; commit(&g);
        eng_vector_t target = {cosf(beyond ? .6f : .2f),sinf(beyond ? .6f : .2f),0};
        float initial = distance(position(&g,g.end),target);
        bool solved = eng_kinematic_set_position(g.f.engine,g.root,g.end,&target);
        if (!beyond) { ASSERT_TRUE(solved); near(position(&g,g.end),target,.05f); }
        eng_matrix_t matrix = local(&g,g.root); float x,y,z; eng_matrix_to_euler(&matrix,&x,&y,&z);
        ASSERT_TRUE(z >= -.30001f && z <= .30001f); ASSERT_TRUE(distance(position(&g,g.end),target) <= initial+1e-5f);
        ASSERT_EQ(4u|64u,root->rotation_joint.flags); fixture_end(&g.f);
    }
}

TEST(engine_kinematic, already_reached_and_zero_length_segments_remain_finite_and_unchanged)
{
    for (int zero = 0; zero < 2; zero++) {
        ik_fixture_t g; begin(&g,false,7);
        if (zero) ((nmo_3dentity_state_t *)fixture_state(&g.f,g.end))->world_matrix[12] = 0;
        commit(&g); eng_matrix_t root = local(&g,g.root), end = local(&g,g.end);
        eng_vector_t target = position(&g,g.end);
        ASSERT_TRUE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&target));
        eng_matrix_t actual = local(&g,g.root); same_matrix(&actual,&root); actual = local(&g,g.end); same_matrix(&actual,&end);
        fixture_end(&g.f);
    }
}

TEST(engine_kinematic, multiple_axes_solve_a_spatial_target_and_zero_jacobians_keep_finite_poses)
{
    for (int singular = 0; singular < 2; singular++) {
        ik_fixture_t g; begin(&g,!singular,singular ? 1 : 7); commit(&g);
        eng_vector_t target = singular ? (eng_vector_t){0,1,0} : (eng_vector_t){1.1f,.8f,.3f};
        eng_matrix_t root = local(&g,g.root), end = local(&g,g.end);
        ASSERT_TRUE(eng_kinematic_set_position(g.f.engine,g.root,g.end,&target));
        if (singular) {
            eng_matrix_t actual = local(&g,g.root); same_matrix(&actual,&root); actual = local(&g,g.end); same_matrix(&actual,&end);
        } else near(position(&g,g.end),target,.05f);
        fixture_end(&g.f);
    }
}

TEST(engine_kinematic, runtime_chain_effectors_remap_on_copy_without_changing_loaded_references)
{
    ik_fixture_t g; begin(&g,true,4);
    nmo_object_id_t chain = fixture_create(&g.f,NMO_CID_KINEMATICCHAIN,"Chain");
    nmo_kinematicchain_state_t *saved = fixture_state(&g.f,chain); ASSERT_NOT_NULL(saved);
    saved->has_chain_data = 1; saved->start_effector = nmo_ref_from_id(g.root); saved->end_effector = nmo_ref_from_id(g.end);
    commit(&g); nmo_object_id_t start = 0, end = 0;
    ASSERT_TRUE(eng_kinematic_effectors(g.f.engine,chain,&start,&end)); ASSERT_EQ(g.root,start); ASSERT_EQ(g.end,end);
    ASSERT_TRUE(eng_kinematic_set_effectors(g.f.engine,chain,g.root,g.middle));
    ASSERT_EQ(g.end,nmo_ref_runtime_id(&saved->end_effector));
    nmo_object_id_t ids[] = {g.root,g.middle,g.end,chain}, copies[4] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,ids,4,false,copies));
    ASSERT_TRUE(eng_kinematic_effectors(g.f.engine,copies[3],&start,&end)); ASSERT_EQ(copies[0],start); ASSERT_EQ(copies[1],end);
    nmo_engine_destroy_object(g.f.engine,g.middle);
    ASSERT_TRUE(eng_kinematic_effectors(g.f.engine,chain,&start,&end)); ASSERT_EQ(g.root,start); ASSERT_EQ(0u,end);
    ASSERT_TRUE(eng_kinematic_effectors(g.f.engine,copies[3],&start,&end)); ASSERT_EQ(copies[1],end);
    ASSERT_FALSE(eng_kinematic_set_effectors(g.f.engine,g.root,copies[0],copies[1]));
    nmo_engine_destroy_object(g.f.engine,chain); ASSERT_FALSE(eng_kinematic_effectors(g.f.engine,chain,NULL,NULL));
    fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_kinematic, named_axes_reach_circular_targets_without_moving_the_pivot_or_stretching);
REGISTER_TEST(engine_kinematic, rigid_segments_project_both_interior_and_unreachable_targets_to_their_radius);
REGISTER_TEST(engine_kinematic, bent_chains_distribute_motion_across_joints_and_keep_local_translations);
REGISTER_TEST(engine_kinematic, transformed_parents_preserve_world_pivots_and_use_the_joint_world_axes);
REGISTER_TEST(engine_kinematic, invalid_deleted_and_disconnected_effectors_preserve_the_pose);
REGISTER_TEST(engine_kinematic, inactive_joints_and_zero_radius_targets_do_not_change_the_hierarchy);
REGISTER_TEST(engine_kinematic, limited_joints_reach_valid_angles_and_reject_crossing_their_bounds);
REGISTER_TEST(engine_kinematic, already_reached_and_zero_length_segments_remain_finite_and_unchanged);
REGISTER_TEST(engine_kinematic, multiple_axes_solve_a_spatial_target_and_zero_jacobians_keep_finite_poses);
REGISTER_TEST(engine_kinematic, runtime_chain_effectors_remap_on_copy_without_changing_loaded_references);
TEST_MAIN_END()
