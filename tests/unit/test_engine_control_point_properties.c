#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/nmo_serialize_context.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))

typedef struct point_fixture {
    engine_fixture_t f;
    nmo_object_id_t curve, points[3];
} point_fixture_t;

static nmo_object_id_t path(engine_fixture_t *f, const char *name, nmo_object_id_t points[3])
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CURVE,name);
    nmo_curve_state_t *curve = fixture_state(f,id);
    if (curve == NULL) return 0;
    curve->base.has_entityndata_chunk = 1; curve->control_point_count = 3;
    curve->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(f->session),3*sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
    if (curve->control_point_ids == NULL) return 0;
    const eng_vector_t positions[] = {{0,0,0},{4,4,0},{8,0,0}};
    for (size_t i = 0; i < 3; i++) {
        char point_name[64]; snprintf(point_name,sizeof(point_name),"%s Point %zu",name,i);
        points[i] = fixture_create(f,NMO_CID_CURVEPOINT,point_name);
        nmo_curvepoint_state_t *point = fixture_state(f,points[i]);
        if (point == NULL) return 0;
        point->base.has_entityndata_chunk = 1; point->base.parent = nmo_ref_from_id(id); point->curve = nmo_ref_from_id(id);
        point->base.world_matrix[12] = positions[i].x; point->base.world_matrix[13] = positions[i].y;
        curve->control_point_ids[i] = nmo_ref_from_id(points[i]);
    }
    return id;
}

static void begin(point_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f); g->curve = path(&g->f,"Path",g->points); ASSERT_TRUE(g->curve != 0);
}

static void commit(point_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void properties(nmo_engine_t *engine, nmo_object_id_t point, float bias, float tension, float continuity, int32_t linear)
{
    eng_curvepoint_properties_t actual; ASSERT_TRUE(eng_curvepoint_get_properties(engine,point,&actual));
    ASSERT_TRUE(isfinite(actual.bias) && isfinite(actual.tension) && isfinite(actual.continuity));
    ASSERT_FLOAT_EQ(bias,actual.bias,0); ASSERT_FLOAT_EQ(tension,actual.tension,0); ASSERT_FLOAT_EQ(continuity,actual.continuity,0); ASSERT_EQ(linear,actual.linear);
}

static void set(nmo_engine_t *engine, nmo_object_id_t point, float bias, float tension, float continuity, int32_t linear)
{
    const eng_curvepoint_properties_t value = {bias,tension,continuity,linear}; ASSERT_TRUE(eng_curvepoint_set_properties(engine,point,&value));
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void same_curve(nmo_engine_t *engine, nmo_object_id_t actual, nmo_object_id_t reference)
{
    float a = NAN, b = NAN; ASSERT_TRUE(eng_curve_length(engine,actual,&a)); ASSERT_TRUE(eng_curve_length(engine,reference,&b));
    ASSERT_TRUE(isfinite(a) && isfinite(b)); ASSERT_FLOAT_EQ(b,a,1e-4f);
    const float steps[] = {0,.125f,.25f,.5f,.75f,.875f,1};
    for (size_t i = 0; i < COUNT(steps); i++) {
        eng_vector_t p, q; ASSERT_TRUE(eng_curve_position(engine,actual,steps[i],&p)); ASSERT_TRUE(eng_curve_position(engine,reference,steps[i],&q)); vector(p,q);
    }
}

static void explicit_tangents(point_fixture_t *g, const nmo_object_id_t points[3], nmo_vector_t in, nmo_vector_t out)
{
    const nmo_vector_t ends[] = {{2,2,0},{0,0,0},{2,-2,0}};
    for (size_t i = 0; i < 3; i++) {
        nmo_curvepoint_state_t *point = fixture_state(&g->f,points[i]); point->tangent_mode = 1;
        point->tangent_in = i == 1 ? in : ends[i]; point->tangent_out = i == 1 ? out : ends[i];
    }
}

TEST(engine_control_point_properties, runtime_values_keep_defaults_document_matrices_and_other_engines_unchanged)
{
    point_fixture_t g; begin(&g); commit(&g); properties(g.f.engine,g.points[1],0,0,0,0);
    float before[16], after[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.points[1],before));
    set(g.f.engine,g.points[1],3,-2,4,-7); properties(g.f.engine,g.points[1],3,-2,4,-7);
    ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.points[1],after)); ASSERT_EQ(0,memcmp(before,after,sizeof(after)));
    const nmo_curvepoint_state_t *state = fixture_state(&g.f,g.points[1]); ASSERT_FLOAT_EQ(0,state->bias,0); ASSERT_FLOAT_EQ(0,state->tension,0); ASSERT_FLOAT_EQ(0,state->continuity,0); ASSERT_EQ(0,state->linear);
    nmo_engine_t *second = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&second)); properties(second,g.points[1],0,0,0,0); nmo_engine_destroy(second); fixture_end(&g.f);
}

TEST(engine_control_point_properties, live_tcb_and_linear_changes_match_independently_specified_explicit_tangents)
{
    point_fixture_t g; begin(&g);
    const eng_curvepoint_properties_t values[] = {{1,0,0,0},{-1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,-1,0},{.5f,-1,-.5f,0},{1,0,0,-7}};
    const nmo_vector_t ins[] = {{4,4,0},{4,-4,0},{0,0,0},{4,-4,0},{4,4,0},{10,8,0},{4,4,0}};
    const nmo_vector_t outs[] = {{4,4,0},{4,-4,0},{0,0,0},{4,4,0},{4,-4,0},{6,0,0},{4,4,0}};
    nmo_object_id_t references[COUNT(values)], reference_points[3];
    for (size_t i = 0; i < COUNT(values); i++) {
        char name[32]; snprintf(name,sizeof(name),"Reference %zu",i); references[i] = path(&g.f,name,reference_points); ASSERT_TRUE(references[i] != 0);
        explicit_tangents(&g,reference_points,ins[i],outs[i]); ((nmo_curvepoint_state_t *)fixture_state(&g.f,reference_points[1]))->linear = values[i].linear;
    }
    commit(&g);
    for (size_t i = 0; i < COUNT(values); i++) { ASSERT_TRUE(eng_curvepoint_set_properties(g.f.engine,g.points[1],&values[i])); same_curve(g.f.engine,g.curve,references[i]); }
    fixture_end(&g.f);
}

TEST(engine_control_point_properties, editing_tcb_does_not_switch_explicit_tangent_mode_and_linear_only_controls_the_outgoing_segment)
{
    point_fixture_t g; begin(&g); nmo_object_id_t reference_points[3], reference = path(&g.f,"Reference",reference_points); ASSERT_TRUE(reference != 0);
    explicit_tangents(&g,g.points,(nmo_vector_t){4,0,0},(nmo_vector_t){4,0,0}); explicit_tangents(&g,reference_points,(nmo_vector_t){4,0,0},(nmo_vector_t){4,0,0});
    commit(&g); const eng_curvepoint_properties_t value = {NAN,INFINITY,-INFINITY,0};
    ASSERT_TRUE(eng_curvepoint_set_properties(g.f.engine,g.points[1],&value)); same_curve(g.f.engine,g.curve,reference);
    set(g.f.engine,g.points[2],1,2,3,42); same_curve(g.f.engine,g.curve,reference); properties(g.f.engine,g.points[2],1,2,3,42);
    set(g.f.engine,g.points[1],1,2,3,1); set(g.f.engine,reference_points[1],0,0,0,1); same_curve(g.f.engine,g.curve,reference);
    ASSERT_EQ(1,((nmo_curvepoint_state_t *)fixture_state(&g.f,g.points[1]))->tangent_mode); fixture_end(&g.f);
}

TEST(engine_control_point_properties, copied_points_keep_live_properties_and_drive_their_copied_curve_after_original_deletion)
{
    point_fixture_t g; begin(&g); commit(&g); set(g.f.engine,g.points[1],0,1,0,0);
    const nmo_object_id_t originals[] = {g.curve,g.points[0],g.points[1],g.points[2]}; nmo_object_id_t copies[COUNT(originals)] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,originals,COUNT(originals),false,copies));
    for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
    properties(g.f.engine,copies[2],0,1,0,0); set(g.f.engine,g.points[1],0,0,0,0); properties(g.f.engine,copies[2],0,1,0,0);
    float actual = NAN; ASSERT_TRUE(eng_curve_length(g.f.engine,copies[0],&actual)); ASSERT_FLOAT_EQ(8*sqrtf(2),actual,1e-4f);
    for (size_t i = 0; i < COUNT(originals); i++) nmo_engine_destroy_object(g.f.engine,originals[i]);
    ASSERT_TRUE(eng_curve_length(g.f.engine,copies[0],&actual)); ASSERT_FLOAT_EQ(8*sqrtf(2),actual,1e-4f);
    eng_vector_t p; ASSERT_TRUE(eng_curve_position(g.f.engine,copies[0],.5f,&p)); vector(p,(eng_vector_t){4,4,0}); fixture_end(&g.f);
}

TEST(engine_control_point_properties, initial_conditions_restore_live_and_serialized_properties_without_resetting_absent_fields)
{
    point_fixture_t g; begin(&g); commit(&g); set(g.f.engine,g.points[1],1,2,3,-7); eng_save_ic(g.f.engine,g.points[1]);
    set(g.f.engine,g.points[1],0,0,0,0); eng_restore_ic(g.f.engine,g.points[1]); properties(g.f.engine,g.points[1],1,2,3,-7);
    nmo_arena_t *arena = nmo_session_get_arena(g.f.session); nmo_chunk_t *partial = nmo_chunk_create(arena); ASSERT_NOT_NULL(partial);
    partial->class_id = NMO_CID_CURVEPOINT; ASSERT_EQ(NMO_OK,nmo_chunk_start_write(partial));
    ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(partial,CK_STATESAVE_CURVEPOINTTCB));
    ASSERT_EQ(NMO_OK,nmo_chunk_write_float(partial,5)); ASSERT_EQ(NMO_OK,nmo_chunk_write_float(partial,6)); ASSERT_EQ(NMO_OK,nmo_chunk_write_float(partial,4)); nmo_chunk_close(partial);
    ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.points[1],partial)); eng_restore_ic(g.f.engine,g.points[1]); properties(g.f.engine,g.points[1],4,5,6,-7);
    nmo_chunk_t *linear = nmo_chunk_create(arena); ASSERT_NOT_NULL(linear); linear->class_id = NMO_CID_CURVEPOINT; ASSERT_EQ(NMO_OK,nmo_chunk_start_write(linear));
    ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(linear,CK_STATESAVE_CURVEPOINTDEFAULTDATA)); ASSERT_EQ(NMO_OK,nmo_chunk_write_object_id(linear,g.curve));
    ASSERT_EQ(NMO_OK,nmo_chunk_write_int(linear,0)); ASSERT_EQ(NMO_OK,nmo_chunk_write_int(linear,42));
    const nmo_vector_t position = {4,4,0}; ASSERT_EQ(NMO_OK,nmo_chunk_write_vector3(linear,&position)); nmo_chunk_close(linear);
    ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.points[1],linear)); eng_restore_ic(g.f.engine,g.points[1]); properties(g.f.engine,g.points[1],4,5,6,42);
    nmo_curvepoint_state_t saved = *(nmo_curvepoint_state_t *)fixture_state(&g.f,g.points[1]); saved.bias = .25f; saved.tension = .5f; saved.continuity = .75f; saved.linear = -3;
    nmo_chunk_t *full = nmo_chunk_create(arena); ASSERT_NOT_NULL(full); full->class_id = NMO_CID_CURVEPOINT; ASSERT_EQ(NMO_OK,nmo_chunk_start_write(full));
    nmo_serialize_context_t context = nmo_serialize_context_create_nonfile(arena,g.f.repo,CK_STATESAVE_ALL);
    ASSERT_EQ(NMO_OK,nmo_curvepoint_serialize(&saved,full,NULL,&context)); nmo_chunk_close(full);
    ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.points[1],full)); eng_restore_ic(g.f.engine,g.points[1]); properties(g.f.engine,g.points[1],.25f,.5f,.75f,-3); fixture_end(&g.f);
}

TEST(engine_control_point_properties, invalid_or_deleted_points_do_not_acquire_properties_or_replace_outputs)
{
    point_fixture_t g; begin(&g); commit(&g); eng_curvepoint_properties_t value = {1,2,3,4}, original = value;
    const nmo_object_id_t invalid[] = {0,g.f.owner_id,g.curve};
    for (size_t i = 0; i < COUNT(invalid); i++) {
        ASSERT_FALSE(eng_curvepoint_get_properties(g.f.engine,invalid[i],&value)); ASSERT_EQ(0,memcmp(&original,&value,sizeof(value)));
        ASSERT_FALSE(eng_curvepoint_set_properties(g.f.engine,invalid[i],&value));
    }
    set(g.f.engine,g.points[1],1,2,3,-7); nmo_engine_destroy_object(g.f.engine,g.points[1]);
    ASSERT_FALSE(eng_curvepoint_get_properties(g.f.engine,g.points[1],&value)); ASSERT_FALSE(eng_curvepoint_set_properties(g.f.engine,g.points[1],&value)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_control_point_properties, runtime_values_keep_defaults_document_matrices_and_other_engines_unchanged);
REGISTER_TEST(engine_control_point_properties, live_tcb_and_linear_changes_match_independently_specified_explicit_tangents);
REGISTER_TEST(engine_control_point_properties, editing_tcb_does_not_switch_explicit_tangent_mode_and_linear_only_controls_the_outgoing_segment);
REGISTER_TEST(engine_control_point_properties, copied_points_keep_live_properties_and_drive_their_copied_curve_after_original_deletion);
REGISTER_TEST(engine_control_point_properties, initial_conditions_restore_live_and_serialized_properties_without_resetting_absent_fields);
REGISTER_TEST(engine_control_point_properties, invalid_or_deleted_points_do_not_acquire_properties_or_replace_outputs);
TEST_MAIN_END()
