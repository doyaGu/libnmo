#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_serialize_context.h"
#include "type/nmo_operations.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_GET_PROPERTIES NMO_GUID(0x1251effa,0xaebec112)
#define GUID_SET_PROPERTIES NMO_GUID(0x1254effa,0xfebec112)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct point_fixture {
    engine_fixture_t f;
    nmo_object_id_t curve, points[3];
    nmo_object_id_t getter, setter, out, entry_link;
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

static nmo_object_id_t source(point_fixture_t *g, nmo_object_id_t pin)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,pin));
}

static void graph_begin(point_fixture_t *g, bool with_setter)
{
    begin(g); g->f.script_id = fixture_add_script(&g->f,"Point Properties Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->getter = fixture_block(&g->f,GUID_GET_PROPERTIES,"Get Control Point Properties"); g->out = fixture_block(&g->f,GUID_NOP,"After Point Properties");
    ASSERT_TRUE(g->getter != 0 && g->out != 0); fixture_input_int(&g->f,fixture_target(&g->f,g->getter),(int32_t)g->points[1]);
    nmo_object_id_t first = g->getter;
    if (with_setter) {
        g->setter = fixture_block(&g->f,GUID_SET_PROPERTIES,"Set Control Point Properties"); ASSERT_TRUE(g->setter != 0);
        fixture_input_int(&g->f,fixture_target(&g->f,g->setter),(int32_t)g->points[1]);
        fixture_link(&g->f,BLOCK_OUT(&g->f,g->setter,0),BLOCK_IN(&g->f,g->getter,0),0); first = g->setter;
    }
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_behavior_link(g->f.tx,g->f.script_id,g->f.script_in,BLOCK_IN(&g->f,first,0),0,&g->entry_link));
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->getter,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void graph_commit(point_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options);
    ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_GET_PROPERTIES)); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_SET_PROPERTIES));
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static nmo_object_id_t output(nmo_engine_t *engine, nmo_object_id_t block, size_t pos)
{
    nmo_object_t *object = eng_find_object(engine,block);
    const nmo_behavior_state_t *state = object != NULL ? nmo_object_get_state(object) : NULL;
    const eng_module_t *module = eng_module_of(engine,block,NULL);
    return state != NULL && module != NULL ? eng_module_id(module,nmo_behavior_ref_array_get_id(&state->out_parameters,pos)) : 0;
}

static void outputs(engine_fixture_t *f, nmo_object_id_t block, float bias, float tension, float continuity, int32_t linear)
{
    const float expected[] = {bias,tension,continuity};
    for (size_t i = 0; i < COUNT(expected); i++) {
        float actual = NAN; read_value(f,output(f->engine,block,i),&actual,sizeof(actual)); ASSERT_TRUE(isfinite(actual)); ASSERT_FLOAT_EQ(expected[i],actual,0);
    }
    int32_t actual_linear = 0; read_value(f,output(f->engine,block,3),&actual_linear,sizeof(actual_linear)); ASSERT_EQ(linear,actual_linear);
}

static void trigger(engine_fixture_t *f)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(f->engine,f->script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(f->engine,0));
}

static void inputs(point_fixture_t *g, eng_curvepoint_properties_t value)
{
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->setter,0),&value.bias,sizeof(float));
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->setter,1),&value.tension,sizeof(float));
    fixture_input_value(&g->f,BLOCK_PIN(&g->f,g->setter,2),&value.continuity,sizeof(float));
    fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->setter,3),value.linear);
}

TEST(engine_control_point_properties, getter_reads_loaded_fields_in_native_pin_order_without_scheduling)
{
    point_fixture_t g; graph_begin(&g,false); nmo_curvepoint_state_t *state = fixture_state(&g.f,g.points[1]);
    state->bias = .25f; state->tension = -.5f; state->continuity = 1.5f; state->linear = -7; graph_commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); outputs(&g.f,g.getter,.25f,-.5f,1.5f,-7);
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.getter,0))); ASSERT_EQ(1u,runs(&g.f,g.out));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,1000)); ASSERT_EQ(1u,runs(&g.f,g.getter)); fixture_end(&g.f);
}

TEST(engine_control_point_properties, setter_and_getter_exchange_live_values_without_clamping_or_canonicalizing_linear)
{
    point_fixture_t g; graph_begin(&g,true); inputs(&g,(eng_curvepoint_properties_t){0,0,0,0});
    nmo_object_id_t parameters[4]; for (int i = 0; i < 4; i++) parameters[i] = source(&g,BLOCK_PIN(&g.f,g.setter,i)); graph_commit(&g);
    const eng_curvepoint_properties_t values[] = {{1,2,3,-7},{-.5f,.25f,2,42},{2,-1,-.5f,0},{-0.0f,0,0,1}};
    for (size_t i = 0; i < COUNT(values); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,parameters[0],&values[i].bias,sizeof(float)));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,parameters[1],&values[i].tension,sizeof(float)));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,parameters[2],&values[i].continuity,sizeof(float)));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,parameters[3],&values[i].linear,sizeof(int32_t))); trigger(&g.f);
        outputs(&g.f,g.getter,values[i].bias,values[i].tension,values[i].continuity,values[i].linear);
    }
    float negative_zero = 1; read_value(&g.f,output(g.f.engine,g.getter,0),&negative_zero,sizeof(negative_zero)); ASSERT_TRUE(signbit(negative_zero));
    ASSERT_EQ(4u,runs(&g.f,g.out)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(4u,runs(&g.f,g.setter)); fixture_end(&g.f);
}

TEST(engine_control_point_properties, disconnected_inputs_reset_all_four_properties_to_native_zero_defaults)
{
    point_fixture_t g; graph_begin(&g,true);
    for (int i = 0; i < 4; i++) ASSERT_EQ(NMO_OK,nmo_script_edit_disconnect_parameter(g.f.tx,BLOCK_PIN(&g.f,g.setter,i)));
    graph_commit(&g); set(g.f.engine,g.points[1],1,2,3,-7); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); outputs(&g.f,g.getter,0,0,0,0); fixture_end(&g.f);
}

TEST(engine_control_point_properties, nonfinite_tcb_values_are_written_and_read_without_switching_explicit_tangents)
{
    point_fixture_t g; graph_begin(&g,true); explicit_tangents(&g,g.points,(nmo_vector_t){4,0,0},(nmo_vector_t){4,0,0});
    inputs(&g,(eng_curvepoint_properties_t){NAN,INFINITY,-INFINITY,-7}); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float b = 0, t = 0, c = 0; read_value(&g.f,output(g.f.engine,g.getter,0),&b,sizeof(b)); read_value(&g.f,output(g.f.engine,g.getter,1),&t,sizeof(t)); read_value(&g.f,output(g.f.engine,g.getter,2),&c,sizeof(c));
    ASSERT_TRUE(isnan(b) && isinf(t) && t > 0 && isinf(c) && c < 0); int32_t linear = 0; read_value(&g.f,output(g.f.engine,g.getter,3),&linear,sizeof(linear)); ASSERT_EQ(-7,linear);
    float length = NAN; ASSERT_TRUE(eng_curve_length(g.f.engine,g.curve,&length)); ASSERT_TRUE(isfinite(length));
    ASSERT_EQ(1,((nmo_curvepoint_state_t *)fixture_state(&g.f,g.points[1]))->tangent_mode); fixture_end(&g.f);
}

TEST(engine_control_point_properties, setter_and_getter_targets_can_switch_between_points_independently)
{
    point_fixture_t g; graph_begin(&g,true); inputs(&g,(eng_curvepoint_properties_t){1,2,3,-7});
    nmo_object_id_t setter_target = source(&g,fixture_target(&g.f,g.setter)), getter_target = source(&g,fixture_target(&g.f,g.getter)); graph_commit(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); outputs(&g.f,g.getter,1,2,3,-7);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,getter_target,&g.points[0],sizeof(g.points[0]))); trigger(&g.f); outputs(&g.f,g.getter,0,0,0,0);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,setter_target,&g.points[0],sizeof(g.points[0]))); trigger(&g.f); outputs(&g.f,g.getter,1,2,3,-7); fixture_end(&g.f);
}

TEST(engine_control_point_properties, invalid_targets_consume_input_activate_out_and_preserve_getter_values)
{
    for (int setter = 0; setter < 2; setter++) for (int kind = 0; kind < 3; kind++) {
        point_fixture_t g; graph_begin(&g,setter != 0);
        if (setter) inputs(&g,(eng_curvepoint_properties_t){1,2,3,-7});
        else { nmo_curvepoint_state_t *state = fixture_state(&g.f,g.points[1]); state->bias = 1; state->tension = 2; state->continuity = 3; state->linear = -7; }
        graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        nmo_object_id_t block = setter ? g.setter : g.getter;
        if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.points[1]);
        else { nmo_object_id_t bad = kind == 0 ? 0 : g.curve; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,fixture_target(&g.f,block)),&bad,sizeof(bad))); }
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,block,0)));
        outputs(&g.f,g.getter,1,2,3,-7); ASSERT_EQ(1u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

static void count_operations(const nmo_engine_event_t *event, void *data)
{
    if (event->kind == NMO_ENGINE_EVENT_OPERATION && event->result == NMO_OK) (*(size_t *)data)++;
}

TEST(engine_control_point_properties, invalid_setter_targets_skip_lazy_input_operations)
{
    point_fixture_t g; graph_begin(&g,true); nmo_object_id_t a = 0, b = 0, operation = 0;
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_FLOAT,"A",&a));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_parameter(g.f.tx,g.f.script_id,NMO_SCRIPT_EDIT_PARAM_LOCAL,CKPGUID_FLOAT,"B",&b));
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,a,"0.2")); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,b,"0.3"));
    ASSERT_EQ(NMO_OK,nmo_script_edit_add_operation(g.f.tx,g.f.script_id,NMO_OP_GUID_ADD,a,b,0,&operation));
    const nmo_parameteroperation_state_t *state = fixture_state(&g.f,operation); ASSERT_NOT_NULL(state);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,nmo_parameteroperation_out_id(state),BLOCK_PIN(&g.f,g.setter,0)));
    graph_commit(&g); size_t operations = 0; nmo_engine_set_trace(g.f.engine,count_operations,&operations);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); outputs(&g.f,g.getter,.5f,0,0,0); ASSERT_EQ(1u,operations);
    const nmo_object_id_t absent = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,source(&g,fixture_target(&g.f,g.setter)),&absent,sizeof(absent)));
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g.f.engine,g.f.script_id,true,true)); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,operations); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_control_point_properties, runtime_values_keep_defaults_document_matrices_and_other_engines_unchanged);
REGISTER_TEST(engine_control_point_properties, live_tcb_and_linear_changes_match_independently_specified_explicit_tangents);
REGISTER_TEST(engine_control_point_properties, editing_tcb_does_not_switch_explicit_tangent_mode_and_linear_only_controls_the_outgoing_segment);
REGISTER_TEST(engine_control_point_properties, copied_points_keep_live_properties_and_drive_their_copied_curve_after_original_deletion);
REGISTER_TEST(engine_control_point_properties, initial_conditions_restore_live_and_serialized_properties_without_resetting_absent_fields);
REGISTER_TEST(engine_control_point_properties, invalid_or_deleted_points_do_not_acquire_properties_or_replace_outputs);
REGISTER_TEST(engine_control_point_properties, getter_reads_loaded_fields_in_native_pin_order_without_scheduling);
REGISTER_TEST(engine_control_point_properties, setter_and_getter_exchange_live_values_without_clamping_or_canonicalizing_linear);
REGISTER_TEST(engine_control_point_properties, disconnected_inputs_reset_all_four_properties_to_native_zero_defaults);
REGISTER_TEST(engine_control_point_properties, nonfinite_tcb_values_are_written_and_read_without_switching_explicit_tangents);
REGISTER_TEST(engine_control_point_properties, setter_and_getter_targets_can_switch_between_points_independently);
REGISTER_TEST(engine_control_point_properties, invalid_targets_consume_input_activate_out_and_preserve_getter_values);
REGISTER_TEST(engine_control_point_properties, invalid_setter_targets_skip_lazy_input_operations);
TEST_MAIN_END()
