#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"

#include <math.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f

static void same_matrix(const eng_matrix_t *actual, const eng_matrix_t *expected)
{
    for (int row = 0; row < 4; row++) for (int column = 0; column < 4; column++)
        ASSERT_FLOAT_EQ(expected->m[row][column],actual->m[row][column],1e-5f);
}

static void quaternion(nmo_quaternion_t actual, nmo_quaternion_t expected)
{
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-6f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-6f);
    ASSERT_FLOAT_EQ(expected.z,actual.z,1e-6f); ASSERT_FLOAT_EQ(expected.w,actual.w,1e-6f);
}

TEST(engine_quaternion, scaled_matrix_conversions_recover_all_rotation_branches_without_mutating_inputs)
{
    const float angles[] = {0,.7f,2.2f,-2.8f,PI_F};
    for (int axis = 0; axis < 3; axis++) for (size_t i = 0; i < COUNT(angles); i++) {
        float euler[3] = {0}; euler[axis] = angles[i];
        eng_matrix_t rotation, matrix, actual; eng_matrix_from_euler(&rotation,euler[0],euler[1],euler[2]); matrix = rotation;
        const float scale[] = {2e-30f,3e30f,4};
        for (int row = 0; row < 3; row++) for (int column = 0; column < 3; column++) matrix.m[row][column] *= scale[row];
        matrix.m[3][0] = 20; eng_matrix_t saved = matrix;
        nmo_quaternion_t q; ASSERT_TRUE(eng_quaternion_from_matrix(&matrix,&q));
        ASSERT_TRUE(eng_matrix_from_quaternion(&actual,&q)); same_matrix(&actual,&rotation);
        ASSERT_EQ(0,memcmp(&matrix,&saved,sizeof(matrix)));
    }
}

TEST(engine_quaternion, nonorthogonal_rows_and_reflections_follow_native_normalization_without_polar_decomposition)
{
    eng_matrix_t matrix; eng_matrix_identity(&matrix);
    matrix.m[1][0] = .6f; matrix.m[1][1] = .8f; matrix.m[2][2] = -99;
    nmo_quaternion_t q; ASSERT_TRUE(eng_quaternion_from_matrix(&matrix,&q));
    quaternion(q,(nmo_quaternion_t){0,0,.158113883f,.948683298f});
    ASSERT_FLOAT_EQ(.925f,q.z*q.z+q.w*q.w,1e-6f);
    eng_matrix_identity(&matrix); matrix.m[2][2] = -4;
    ASSERT_TRUE(eng_quaternion_from_matrix(&matrix,&q)); quaternion(q,(nmo_quaternion_t){0,0,0,1});
    matrix.m[0][0] = -2;
    ASSERT_TRUE(eng_quaternion_from_matrix(&matrix,&q)); quaternion(q,(nmo_quaternion_t){0,1,0,0});
}

TEST(engine_quaternion, quaternion_matrix_conversion_is_invariant_under_finite_nonzero_scale)
{
    const float scales[] = {1e-35f,-1,1e35f};
    eng_matrix_t expected; eng_matrix_from_euler(&expected,0,0,-PI_F*.5f);
    for (size_t i = 0; i < COUNT(scales); i++) {
        nmo_quaternion_t q = {0,0,scales[i],scales[i]}; eng_matrix_t actual;
        ASSERT_TRUE(eng_matrix_from_quaternion(&actual,&q)); same_matrix(&actual,&expected);
    }
}

TEST(engine_quaternion, slerp_uses_shortest_arcs_and_allows_extrapolation)
{
    const nmo_quaternion_t a = {0,0,sinf(.2f),cosf(.2f)}, b = {0,0,sinf(.8f),cosf(.8f)};
    const nmo_quaternion_t opposite = {-b.x,-b.y,-b.z,-b.w};
    const float phases[] = {-.5f,0,.25f,.5f,1,1.5f};
    for (size_t i = 0; i < COUNT(phases); i++) {
        float angle = .2f+.6f*phases[i]; nmo_quaternion_t expected = {0,0,sinf(angle),cosf(angle)};
        quaternion(eng_quaternion_slerp(phases[i],a,b),expected);
        quaternion(eng_quaternion_slerp(phases[i],a,opposite),expected);
        quaternion(eng_quaternion_slerp(phases[i],(nmo_quaternion_t){0,0,0,1},(nmo_quaternion_t){1,0,0,0}),
            (nmo_quaternion_t){sinf(phases[i]*PI_F*.5f),0,0,cosf(phases[i]*PI_F*.5f)});
    }
}

TEST(engine_quaternion, close_slerp_keeps_the_unnormalized_linear_result)
{
    nmo_quaternion_t a = {0,0,0,1}, b = {0,0,sinf(.05f),cosf(.05f)};
    nmo_quaternion_t q = eng_quaternion_slerp(.5f,a,b);
    quaternion(q,(nmo_quaternion_t){0,0,b.z*.5f,.5f+b.w*.5f}); ASSERT_TRUE(q.z*q.z+q.w*q.w < 1);
    quaternion(eng_quaternion_slerp(.5f,(nmo_quaternion_t){1,0,0,1},(nmo_quaternion_t){1,0,0,2}),
        (nmo_quaternion_t){1,0,0,1.5f});
}

TEST(engine_quaternion, invalid_conversions_preserve_existing_outputs)
{
    eng_matrix_t matrix, before; eng_matrix_identity(&matrix); before = matrix; before.m[3][0] = 99;
    nmo_quaternion_t expected = {1,2,3,4}, q = expected;
    matrix.m[0][0] = 0; ASSERT_FALSE(eng_quaternion_from_matrix(&matrix,&q)); quaternion(q,expected);
    matrix.m[0][0] = NAN; ASSERT_FALSE(eng_quaternion_from_matrix(&matrix,&q)); quaternion(q,expected);
    matrix = before; q = (nmo_quaternion_t){0}; ASSERT_FALSE(eng_matrix_from_quaternion(&matrix,&q)); same_matrix(&matrix,&before);
    q.w = INFINITY; ASSERT_FALSE(eng_matrix_from_quaternion(&matrix,&q)); same_matrix(&matrix,&before);
}

static nmo_object_id_t entity(engine_fixture_t *f, const char *name, nmo_object_id_t parent, eng_vector_t p, eng_vector_t scale)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_3DENTITY,name);
    nmo_3dentity_state_t *state = fixture_state(f,id);
    if (state == NULL) return 0;
    state->parent = nmo_ref_from_id(parent); state->has_entityndata_chunk = 1;
    state->world_matrix[0] = scale.x; state->world_matrix[5] = scale.y; state->world_matrix[10] = scale.z;
    state->world_matrix[12] = p.x; state->world_matrix[13] = p.y; state->world_matrix[14] = p.z;
    return id;
}

static void position(engine_fixture_t *f, nmo_object_id_t id, eng_vector_t expected)
{
    eng_vector_t actual = eng_entity_position(f->engine,eng_entity_index(f->engine,id),-1);
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-5f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-5f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-5f);
}

TEST(engine_quaternion, local_scale_restoration_follows_children_after_keep_children_rotation)
{
    for (int keep = 0; keep < 2; keep++) for (int scale = 0; scale < 2; scale++) {
        engine_fixture_t f; fixture_begin_document(&f);
        nmo_object_id_t object = entity(&f,"Object",0,(eng_vector_t){3,4,5},(eng_vector_t){2,3,4});
        nmo_object_id_t child = entity(&f,"Child",object,(eng_vector_t){5,7,9},(eng_vector_t){1,1,1});
        fixture_begin_edit(&f); fixture_commit(&f,NULL);
        const nmo_quaternion_t identity = {0,0,0,1};
        ASSERT_TRUE(eng_entity_set_quaternion(f.engine,eng_entity_index(f.engine,object),&identity,-1,keep,scale));
        position(&f,object,(eng_vector_t){3,4,5});
        position(&f,child,scale ? (keep ? (eng_vector_t){7,13,21} : (eng_vector_t){5,7,9}) :
            (keep ? (eng_vector_t){5,7,9} : (eng_vector_t){4,5,6}));
        eng_vector_t actual = eng_entity_scale(f.engine,eng_entity_index(f.engine,object),true);
        ASSERT_FLOAT_EQ(scale ? 2 : 1,actual.x,1e-6f); ASSERT_FLOAT_EQ(scale ? 3 : 1,actual.y,1e-6f);
        fixture_end(&f);
    }
}

TEST(engine_quaternion, reference_access_and_nonuniform_parents_preserve_world_pivots_and_local_scales)
{
    engine_fixture_t f; fixture_begin_document(&f);
    nmo_object_id_t parent = entity(&f,"Parent",0,(eng_vector_t){0,0,0},(eng_vector_t){2,3,4});
    nmo_object_id_t object = entity(&f,"Object",parent,(eng_vector_t){3,4,5},(eng_vector_t){4,9,16});
    nmo_object_id_t ref = entity(&f,"Reference",0,(eng_vector_t){20,30,40},(eng_vector_t){1,1,1});
    fixture_begin_edit(&f); fixture_commit(&f,NULL);
    int32_t index = eng_entity_index(f.engine,object), reference = eng_entity_index(f.engine,ref);
    const nmo_quaternion_t q = {0,0,-sinf(PI_F*.25f),cosf(PI_F*.25f)};
    ASSERT_TRUE(eng_entity_set_quaternion(f.engine,index,&q,-1,false,true)); position(&f,object,(eng_vector_t){3,4,5});
    eng_vector_t scale = eng_entity_scale(f.engine,index,true); ASSERT_FLOAT_EQ(2,scale.x,1e-6f); ASSERT_FLOAT_EQ(3,scale.y,1e-6f);
    scale = eng_entity_scale(f.engine,index,false); ASSERT_FLOAT_EQ(6,scale.x,1e-5f); ASSERT_FLOAT_EQ(6,scale.y,1e-5f); ASSERT_FLOAT_EQ(16,scale.z,1e-5f);
    ASSERT_TRUE(eng_entity_set_quaternion(f.engine,reference,&q,-1,false,false));
    const nmo_quaternion_t identity = {0,0,0,1};
    ASSERT_TRUE(eng_entity_set_quaternion(f.engine,index,&identity,reference,false,false));
    nmo_quaternion_t relative; ASSERT_TRUE(eng_entity_quaternion(f.engine,index,&relative,reference)); quaternion(relative,identity);
    position(&f,object,(eng_vector_t){3,4,5}); ASSERT_TRUE(eng_entity_quaternion(f.engine,index,&relative,-1)); quaternion(relative,q);
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_quaternion, scaled_matrix_conversions_recover_all_rotation_branches_without_mutating_inputs);
REGISTER_TEST(engine_quaternion, nonorthogonal_rows_and_reflections_follow_native_normalization_without_polar_decomposition);
REGISTER_TEST(engine_quaternion, quaternion_matrix_conversion_is_invariant_under_finite_nonzero_scale);
REGISTER_TEST(engine_quaternion, slerp_uses_shortest_arcs_and_allows_extrapolation);
REGISTER_TEST(engine_quaternion, close_slerp_keeps_the_unnormalized_linear_result);
REGISTER_TEST(engine_quaternion, invalid_conversions_preserve_existing_outputs);
REGISTER_TEST(engine_quaternion, local_scale_restoration_follows_children_after_keep_children_rotation);
REGISTER_TEST(engine_quaternion, reference_access_and_nonuniform_parents_preserve_world_pivots_and_local_scales);
TEST_MAIN_END()
