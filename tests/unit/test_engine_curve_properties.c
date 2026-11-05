#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/nmo_serialize_context.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))

typedef struct curve_fixture {
    engine_fixture_t f;
    nmo_object_id_t curve, points[3];
} curve_fixture_t;

static void begin(curve_fixture_t *g, size_t count)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->curve = fixture_create(&g->f,NMO_CID_CURVE,"Triangle Path");
    nmo_curve_state_t *curve = fixture_state(&g->f,g->curve); ASSERT_NOT_NULL(curve);
    curve->base.has_entityndata_chunk = 1; curve->control_point_count = (uint32_t)count;
    if (count != 0) {
        curve->control_point_ids = nmo_arena_alloc(nmo_session_get_arena(g->f.session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t));
        ASSERT_NOT_NULL(curve->control_point_ids);
    }
    const eng_vector_t positions[] = {{0,0,0},{6,0,0},{6,8,0}};
    const char *names[] = {"Point A","Point B","Point C"};
    for (size_t i = 0; i < count; i++) {
        g->points[i] = fixture_create(&g->f,NMO_CID_CURVEPOINT,names[i]);
        nmo_curvepoint_state_t *point = fixture_state(&g->f,g->points[i]); ASSERT_NOT_NULL(point);
        point->base.has_entityndata_chunk = 1; point->base.parent = nmo_ref_from_id(g->curve);
        point->curve = nmo_ref_from_id(g->curve); point->linear = 1;
        point->base.world_matrix[12] = positions[i].x; point->base.world_matrix[13] = positions[i].y;
        curve->control_point_ids[i] = nmo_ref_from_id(g->points[i]);
    }
}

static void commit(curve_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void properties(nmo_engine_t *engine, nmo_object_id_t curve, int32_t opened, int32_t steps, float fitting)
{
    eng_curve_properties_t actual; ASSERT_TRUE(eng_curve_get_properties(engine,curve,&actual));
    ASSERT_EQ(opened,actual.opened); ASSERT_EQ(steps,actual.step_count);
    ASSERT_TRUE(isfinite(actual.fitting_coeff)); ASSERT_FLOAT_EQ(fitting,actual.fitting_coeff,0);
}

static void set(nmo_engine_t *engine, nmo_object_id_t curve, int32_t opened, int32_t steps, float fitting)
{
    eng_curve_properties_t value = {opened,steps,fitting}; ASSERT_TRUE(eng_curve_set_properties(engine,curve,&value));
}

static void length(nmo_engine_t *engine, nmo_object_id_t curve, float expected)
{
    float actual = NAN; ASSERT_TRUE(eng_curve_length(engine,curve,&actual)); ASSERT_TRUE(isfinite(actual)); ASSERT_FLOAT_EQ(expected,actual,1e-4f);
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void sample(nmo_engine_t *engine, nmo_object_id_t curve, float step, eng_vector_t expected)
{
    eng_vector_t actual; ASSERT_TRUE(eng_curve_position(engine,curve,step,&actual)); vector(actual,expected);
}

TEST(engine_curve_properties, runtime_properties_preserve_native_defaults_and_leave_the_document_and_other_engines_unchanged)
{
    curve_fixture_t g; begin(&g,3); commit(&g); properties(g.f.engine,g.curve,1,100,0); length(g.f.engine,g.curve,14);
    set(g.f.engine,g.curve,0,-7,.5f); properties(g.f.engine,g.curve,0,-7,.5f); length(g.f.engine,g.curve,6);
    const nmo_curve_state_t *source = fixture_state(&g.f,g.curve); ASSERT_EQ(1u,source->opened); ASSERT_EQ(100u,source->step_count); ASSERT_FLOAT_EQ(0,source->fitting_coeff,0);
    nmo_engine_t *second = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&second));
    properties(second,g.curve,1,100,0); length(second,g.curve,14); nmo_engine_destroy(second); fixture_end(&g.f);
}

TEST(engine_curve_properties, opening_closing_and_fitting_change_length_and_positions_immediately_without_using_display_steps)
{
    curve_fixture_t g; begin(&g,3); commit(&g);
    const int32_t steps[] = {0,-1,1,INT_MAX};
    for (size_t i = 0; i < COUNT(steps); i++) {
        set(g.f.engine,g.curve,0,steps[i],0); length(g.f.engine,g.curve,24); sample(g.f.engine,g.curve,1,(eng_vector_t){0,0,0});
        sample(g.f.engine,g.curve,.75f,(eng_vector_t){3.6f,4.8f,0});
        set(g.f.engine,g.curve,1,steps[i],0); length(g.f.engine,g.curve,14); sample(g.f.engine,g.curve,1,(eng_vector_t){6,8,0});
        set(g.f.engine,g.curve,0,steps[i],.5f); length(g.f.engine,g.curve,6); sample(g.f.engine,g.curve,0,(eng_vector_t){3,2,0});
        set(g.f.engine,g.curve,1,steps[i],.5f); length(g.f.engine,g.curve,sqrtf(13)+sqrtf(18.25f));
        sample(g.f.engine,g.curve,0,(eng_vector_t){1.5f,0,0}); sample(g.f.engine,g.curve,1,(eng_vector_t){6,6,0});
    }
    set(g.f.engine,g.curve,0,100,1); length(g.f.engine,g.curve,12);
    set(g.f.engine,g.curve,0,100,2); length(g.f.engine,g.curve,48);
    set(g.f.engine,g.curve,0,100,-1); length(g.f.engine,g.curve,24);
    fixture_end(&g.f);
}

TEST(engine_curve_properties, reported_length_stays_local_when_the_curve_and_its_points_move_or_scale)
{
    curve_fixture_t g; begin(&g,3); commit(&g); set(g.f.engine,g.curve,1,24,.5f);
    const eng_matrix_t world = {.m={{2,0,0,0},{0,3,0,0},{0,0,4,0},{10,20,30,1}}};
    eng_entity_set_world_matrix(g.f.engine,eng_entity_index(g.f.engine,g.curve),&world,false);
    length(g.f.engine,g.curve,sqrtf(13)+sqrtf(18.25f)); sample(g.f.engine,g.curve,0,(eng_vector_t){13,20,30}); sample(g.f.engine,g.curve,1,(eng_vector_t){22,38,30}); fixture_end(&g.f);
}

TEST(engine_curve_properties, copied_curves_take_live_properties_and_remapped_points_independently)
{
    curve_fixture_t g; begin(&g,3); commit(&g); set(g.f.engine,g.curve,0,13,.5f);
    const nmo_object_id_t originals[] = {g.curve,g.points[0],g.points[1],g.points[2]}; nmo_object_id_t copies[COUNT(originals)] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,originals,COUNT(originals),false,copies));
    for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
    properties(g.f.engine,copies[0],0,13,.5f); length(g.f.engine,copies[0],6);
    set(g.f.engine,g.curve,1,100,0); length(g.f.engine,g.curve,14); length(g.f.engine,copies[0],6);
    for (size_t i = 0; i < COUNT(originals); i++) nmo_engine_destroy_object(g.f.engine,originals[i]);
    length(g.f.engine,copies[0],6); sample(g.f.engine,copies[0],0,(eng_vector_t){3,2,0});
    eng_vector_t moved = {6,16,0}; eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,copies[3]),&moved,-1,false);
    length(g.f.engine,copies[0],(22+sqrtf(292))*.25f); fixture_end(&g.f);
}

TEST(engine_curve_properties, initial_conditions_restore_live_properties_and_only_present_legacy_fields)
{
    curve_fixture_t g; begin(&g,3); commit(&g); set(g.f.engine,g.curve,0,17,.5f); eng_save_ic(g.f.engine,g.curve);
    set(g.f.engine,g.curve,1,23,2); eng_restore_ic(g.f.engine,g.curve); properties(g.f.engine,g.curve,0,17,.5f); length(g.f.engine,g.curve,6);
    nmo_arena_t *arena = nmo_session_get_arena(g.f.session); nmo_chunk_t *partial = nmo_chunk_create(arena); ASSERT_NOT_NULL(partial);
    partial->class_id = NMO_CID_CURVE; ASSERT_EQ(NMO_OK,nmo_chunk_start_write(partial));
    ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(partial,CK_STATESAVE_CURVESTEPS)); ASSERT_EQ(NMO_OK,nmo_chunk_write_int(partial,-9)); nmo_chunk_close(partial);
    ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.curve,partial)); eng_restore_ic(g.f.engine,g.curve); properties(g.f.engine,g.curve,0,-9,.5f); length(g.f.engine,g.curve,6);
    nmo_curve_state_t saved = *(nmo_curve_state_t *)fixture_state(&g.f,g.curve); saved.opened = 1; saved.step_count = 11; saved.fitting_coeff = 1;
    nmo_chunk_t *full = nmo_chunk_create(arena); ASSERT_NOT_NULL(full); full->class_id = NMO_CID_CURVE; ASSERT_EQ(NMO_OK,nmo_chunk_start_write(full));
    nmo_serialize_context_t context = nmo_serialize_context_create_nonfile(arena,g.f.repo,CK_STATESAVE_ALL);
    ASSERT_EQ(NMO_OK,nmo_curve_serialize(&saved,full,NULL,&context)); nmo_chunk_close(full);
    ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.curve,full)); eng_restore_ic(g.f.engine,g.curve); properties(g.f.engine,g.curve,1,11,1); length(g.f.engine,g.curve,7); fixture_end(&g.f);
}

TEST(engine_curve_properties, empty_and_single_point_curves_keep_zero_length_and_invalid_curves_do_not_acquire_state)
{
    for (size_t count = 0; count < 2; count++) {
        curve_fixture_t g; begin(&g,count); commit(&g); set(g.f.engine,g.curve,0,-2,2); length(g.f.engine,g.curve,0);
        eng_vector_t value; ASSERT_FALSE(eng_curve_position(g.f.engine,g.curve,.5f,&value));
        eng_curve_properties_t properties = {1,2,3}, unchanged = properties;
        ASSERT_FALSE(eng_curve_get_properties(g.f.engine,g.f.owner_id,&properties)); ASSERT_EQ(0,memcmp(&unchanged,&properties,sizeof(properties)));
        ASSERT_FALSE(eng_curve_set_properties(g.f.engine,g.f.owner_id,&properties)); nmo_engine_destroy_object(g.f.engine,g.curve);
        ASSERT_FALSE(eng_curve_get_properties(g.f.engine,g.curve,&properties)); ASSERT_FALSE(eng_curve_set_properties(g.f.engine,g.curve,&properties)); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_curve_properties, runtime_properties_preserve_native_defaults_and_leave_the_document_and_other_engines_unchanged);
REGISTER_TEST(engine_curve_properties, opening_closing_and_fitting_change_length_and_positions_immediately_without_using_display_steps);
REGISTER_TEST(engine_curve_properties, reported_length_stays_local_when_the_curve_and_its_points_move_or_scale);
REGISTER_TEST(engine_curve_properties, copied_curves_take_live_properties_and_remapped_points_independently);
REGISTER_TEST(engine_curve_properties, initial_conditions_restore_live_properties_and_only_present_legacy_fields);
REGISTER_TEST(engine_curve_properties, empty_and_single_point_curves_keep_zero_length_and_invalid_curves_do_not_acquire_state);
TEST_MAIN_END()
