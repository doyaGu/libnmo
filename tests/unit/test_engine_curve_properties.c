#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_serialize_context.h"
#include "type/nmo_operations.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_GET_PROPERTIES NMO_GUID(0x1251adfa,0xaebec002)
#define GUID_SET_PROPERTIES NMO_GUID(0x1257effa,0xf7bec112)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct curve_fixture {
    engine_fixture_t f;
    nmo_object_id_t curve, points[3];
    nmo_object_id_t setter, getter, out;
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

static nmo_object_id_t source(curve_fixture_t *g, nmo_object_id_t pin)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,pin));
}

static void graph_begin(curve_fixture_t *g, bool with_setter, size_t count)
{
    begin(g,count); g->f.script_id = fixture_add_script(&g->f,"Curve Properties Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->getter = fixture_block(&g->f,GUID_GET_PROPERTIES,"Get Curve Properties");
    g->out = fixture_block(&g->f,GUID_NOP,"After Properties"); ASSERT_TRUE(g->getter != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->getter),(int32_t)g->curve);
    nmo_object_id_t first = g->getter;
    if (with_setter) {
        g->setter = fixture_block(&g->f,GUID_SET_PROPERTIES,"Set Curve Properties"); ASSERT_TRUE(g->setter != 0);
        fixture_input_int(&g->f,fixture_target(&g->f,g->setter),(int32_t)g->curve);
        fixture_link(&g->f,BLOCK_OUT(&g->f,g->setter,0),BLOCK_IN(&g->f,g->getter,0),0); first = g->setter;
    }
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,first,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->getter,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void graph_commit(curve_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_GET_PROPERTIES)); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_SET_PROPERTIES));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static nmo_object_id_t output(nmo_engine_t *engine, nmo_object_id_t block, size_t pos)
{
    nmo_object_t *object = eng_find_object(engine,block);
    const nmo_behavior_state_t *state = object != NULL ? nmo_object_get_state(object) : NULL;
    const eng_module_t *module = eng_module_of(engine,block,NULL);
    return state != NULL && module != NULL ? eng_module_id(module,nmo_behavior_ref_array_get_id(&state->out_parameters,pos)) : 0;
}

static void outputs(engine_fixture_t *f, nmo_object_id_t block, int32_t opened, int32_t steps, float fitting, float length)
{
    int32_t actual_open = -1, actual_steps = -1; float actual_fitting = NAN, actual_length = NAN;
    read_value(f,output(f->engine,block,0),&actual_open,sizeof(actual_open)); read_value(f,output(f->engine,block,1),&actual_steps,sizeof(actual_steps));
    read_value(f,output(f->engine,block,2),&actual_fitting,sizeof(actual_fitting)); read_value(f,output(f->engine,block,3),&actual_length,sizeof(actual_length));
    ASSERT_EQ(opened,actual_open); ASSERT_EQ(steps,actual_steps); ASSERT_TRUE(isfinite(actual_fitting) && isfinite(actual_length));
    ASSERT_FLOAT_EQ(fitting,actual_fitting,0); ASSERT_FLOAT_EQ(length,actual_length,1e-4f);
}

static void trigger(engine_fixture_t *f)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f->engine,f->script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f->engine,0));
}

TEST(engine_curve_properties, getter_reads_native_fields_and_local_length_without_scheduling)
{
    curve_fixture_t g; graph_begin(&g,false,3); nmo_curve_state_t *state = fixture_state(&g.f,g.curve);
    state->opened = 7; state->step_count = (uint32_t)-9; state->fitting_coeff = -.5f; graph_commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); outputs(&g.f,g.getter,7,-9,-.5f,14);
    ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.getter,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1000)); ASSERT_EQ(1u,runs(&g.f,g.getter)); fixture_end(&g.f);
}

TEST(engine_curve_properties, setter_and_getter_share_live_open_step_and_fitting_changes_in_the_same_frame)
{
    curve_fixture_t g; graph_begin(&g,true,3);
    const eng_curve_properties_t values[] = {{1,4,0},{0,-4,.5f},{1,0,1},{0,INT_MAX,2}}; const float lengths[] = {14,6,7,48};
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,0),1); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,1),4);
    const float zero = 0; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.setter,2),&zero,sizeof(zero));
    const nmo_object_id_t parameters[] = {source(&g,BLOCK_PIN(&g.f,g.setter,0)),source(&g,BLOCK_PIN(&g.f,g.setter,1)),source(&g,BLOCK_PIN(&g.f,g.setter,2))}; graph_commit(&g);
    for (size_t i = 0; i < COUNT(values); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,parameters[0],&values[i].opened,sizeof(int32_t)));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,parameters[1],&values[i].step_count,sizeof(int32_t)));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,parameters[2],&values[i].fitting_coeff,sizeof(float))); trigger(&g.f);
        outputs(&g.f,g.getter,values[i].opened,values[i].step_count,values[i].fitting_coeff,lengths[i]);
    }
    ASSERT_EQ(4u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(4u,runs(&g.f,g.setter));
    ASSERT_FLOAT_EQ(0,((nmo_curve_state_t *)fixture_state(&g.f,g.curve))->fitting_coeff,0); fixture_end(&g.f);
}

TEST(engine_curve_properties, disconnected_setter_inputs_use_native_defaults_and_connected_zeros_remain_values)
{
    for (int connected = 0; connected < 2; connected++) {
        curve_fixture_t g; graph_begin(&g,true,3);
        if (connected) {
            const float zero = 0; fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,0),0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,1),0);
            fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.setter,2),&zero,sizeof(zero));
        } else for (int i = 0; i < 3; i++) ASSERT_EQ(NMO_OK,nmo_script_edit_disconnect_parameter(g.f.tx,BLOCK_PIN(&g.f,g.setter,i)));
        graph_commit(&g); set(g.f.engine,g.curve,0,17,.5f); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        outputs(&g.f,g.getter,connected ? 0 : 1,connected ? 0 : 100,0,connected ? 24 : 14); fixture_end(&g.f);
    }
}

TEST(engine_curve_properties, setter_preserves_negative_steps_and_nonfinite_fitting_with_truthy_open_inputs)
{
    const float fitting[] = {-1,-0.0f,NAN,INFINITY};
    for (size_t i = 0; i < COUNT(fitting); i++) {
        curve_fixture_t g; graph_begin(&g,true,3); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,0),-7); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,1),-13);
        fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.setter,2),&fitting[i],sizeof(float)); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        eng_curve_properties_t actual; ASSERT_TRUE(eng_curve_get_properties(g.f.engine,g.curve,&actual)); ASSERT_EQ(1,actual.opened); ASSERT_EQ(-13,actual.step_count);
        float stored = 0, measured = 0; read_value(&g.f,output(g.f.engine,g.getter,2),&stored,sizeof(stored)); read_value(&g.f,output(g.f.engine,g.getter,3),&measured,sizeof(measured));
        if (i == 2) { ASSERT_TRUE(isnan(actual.fitting_coeff) && isnan(stored)); ASSERT_FLOAT_EQ(14,measured,1e-4f); }
        else if (i == 3) { ASSERT_TRUE(isinf(actual.fitting_coeff) && isinf(stored) && isnan(measured)); }
        else { ASSERT_FLOAT_EQ(fitting[i],stored,0); ASSERT_FLOAT_EQ(14,measured,1e-4f); if (i == 1) ASSERT_TRUE(signbit(stored)); }
        fixture_end(&g.f);
    }
}

TEST(engine_curve_properties, live_getter_and_setter_targets_can_change_independently)
{
    curve_fixture_t g; graph_begin(&g,true,3); nmo_object_id_t other = fixture_create(&g.f,NMO_CID_CURVE,"Empty Path"); ASSERT_TRUE(other != 0);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,0),0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,1),17);
    const float half = .5f; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.setter,2),&half,sizeof(half));
    nmo_object_id_t setter_target = source(&g,fixture_target(&g.f,g.setter)), getter_target = source(&g,fixture_target(&g.f,g.getter)); graph_commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); outputs(&g.f,g.getter,0,17,.5f,6);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,setter_target,&other,sizeof(other))); trigger(&g.f);
    outputs(&g.f,g.getter,0,17,.5f,6); properties(g.f.engine,other,0,17,.5f);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,getter_target,&other,sizeof(other))); trigger(&g.f); outputs(&g.f,g.getter,0,17,.5f,0); fixture_end(&g.f);
}

TEST(engine_curve_properties, invalid_targets_consume_input_activate_out_and_preserve_getter_values)
{
    for (int setter = 0; setter < 2; setter++) for (int kind = 0; kind < 3; kind++) {
        curve_fixture_t g; graph_begin(&g,setter != 0,3);
        if (setter) { fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,0),1); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,1),100); }
        graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        nmo_object_id_t block = setter ? g.setter : g.getter;
        if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.curve);
        else { nmo_object_id_t bad = kind == 0 ? 0 : g.f.owner_id; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,fixture_target(&g.f,block)),&bad,sizeof(bad))); }
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,block,0)));
        outputs(&g.f,g.getter,1,100,0,14); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_curve_properties, opening_an_already_open_curve_preserves_its_loaded_truthy_flag_until_closed)
{
    const int32_t initial[] = {0,7,-3};
    for (size_t i = 0; i < COUNT(initial); i++) {
        curve_fixture_t g; graph_begin(&g,true,3); ((nmo_curve_state_t *)fixture_state(&g.f,g.curve))->opened = (uint32_t)initial[i];
        fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,0),-7); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,1),100);
        nmo_object_id_t open_parameter = source(&g,BLOCK_PIN(&g.f,g.setter,0)); graph_commit(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); outputs(&g.f,g.getter,initial[i] ? initial[i] : 1,100,0,14);
        const int32_t closed = 0, opened = 1;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,open_parameter,&closed,sizeof(closed))); trigger(&g.f); outputs(&g.f,g.getter,0,100,0,24);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,open_parameter,&opened,sizeof(opened))); trigger(&g.f); outputs(&g.f,g.getter,1,100,0,14); fixture_end(&g.f);
    }
}

static void count_operations(const nmo_engine_event_t *event, void *data)
{
    if (event->kind == NMO_ENGINE_EVENT_OPERATION && event->result == NMO_OK) (*(size_t *)data)++;
}

TEST(engine_curve_properties, missing_setter_targets_skip_lazy_fitting_operations)
{
    curve_fixture_t g; graph_begin(&g,true,3); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.setter,0),1);
    nmo_object_id_t a = 0, b = 0, operation = 0;
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_FLOAT,"A",&a));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_FLOAT,"B",&b));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,a,"0.2")); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,b,"0.3"));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_operation(g.f.tx,g.f.script_id,NMO_OP_GUID_ADD,a,b,0,&operation));
    const nmo_parameteroperation_state_t *state = fixture_state(&g.f,operation); ASSERT_NOT_NULL(state);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,nmo_parameteroperation_out_id(state),BLOCK_PIN(&g.f,g.setter,2)));
    graph_commit(&g); size_t operations = 0; nmo_engine_set_trace(g.f.engine,count_operations,&operations);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,operations); outputs(&g.f,g.getter,1,0,.5f,sqrtf(13)+sqrtf(18.25f));
    const nmo_object_id_t absent = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,fixture_target(&g.f,g.setter)),&absent,sizeof(absent)));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
    ASSERT_EQ(1u,operations); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_curve_properties, runtime_properties_preserve_native_defaults_and_leave_the_document_and_other_engines_unchanged);
REGISTER_TEST(engine_curve_properties, opening_closing_and_fitting_change_length_and_positions_immediately_without_using_display_steps);
REGISTER_TEST(engine_curve_properties, reported_length_stays_local_when_the_curve_and_its_points_move_or_scale);
REGISTER_TEST(engine_curve_properties, copied_curves_take_live_properties_and_remapped_points_independently);
REGISTER_TEST(engine_curve_properties, initial_conditions_restore_live_properties_and_only_present_legacy_fields);
REGISTER_TEST(engine_curve_properties, empty_and_single_point_curves_keep_zero_length_and_invalid_curves_do_not_acquire_state);
REGISTER_TEST(engine_curve_properties, getter_reads_native_fields_and_local_length_without_scheduling);
REGISTER_TEST(engine_curve_properties, setter_and_getter_share_live_open_step_and_fitting_changes_in_the_same_frame);
REGISTER_TEST(engine_curve_properties, disconnected_setter_inputs_use_native_defaults_and_connected_zeros_remain_values);
REGISTER_TEST(engine_curve_properties, setter_preserves_negative_steps_and_nonfinite_fitting_with_truthy_open_inputs);
REGISTER_TEST(engine_curve_properties, opening_an_already_open_curve_preserves_its_loaded_truthy_flag_until_closed);
REGISTER_TEST(engine_curve_properties, live_getter_and_setter_targets_can_change_independently);
REGISTER_TEST(engine_curve_properties, invalid_targets_consume_input_activate_out_and_preserve_getter_values);
REGISTER_TEST(engine_curve_properties, missing_setter_targets_skip_lazy_fitting_operations);
TEST_MAIN_END()
