#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_ADD NMO_GUID(0x1234eefa,0xffaec112)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct curve_fixture {
    engine_fixture_t f;
    nmo_object_id_t curve, points[3];
    nmo_object_id_t block, out;
} curve_fixture_t;

static const eng_vector_t path_positions[] = {{0,0,0},{4,4,0},{8,0,0}};

static nmo_object_id_t path(engine_fixture_t *f, const char *name, const eng_vector_t *positions, size_t count, nmo_object_id_t *points)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CURVE,name);
    nmo_curve_state_t *curve = fixture_state(f,id);
    if (curve == NULL) return 0;
    curve->base.has_entityndata_chunk = 1; curve->has_curve_data = curve->has_curveonly_chunk = 1;
    curve->control_point_count = (uint32_t)count;
    curve->control_point_ids = count != 0 ? nmo_arena_alloc(nmo_session_get_arena(f->session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t)) : NULL;
    if (count != 0 && curve->control_point_ids == NULL) return 0;
    for (size_t i = 0; i < count; i++) {
        char point_name[64]; snprintf(point_name,sizeof(point_name),"%s Point %zu",name,i);
        points[i] = fixture_create(f,NMO_CID_CURVEPOINT,point_name);
        nmo_curvepoint_state_t *point = fixture_state(f,points[i]);
        if (point == NULL) return 0;
        point->base.has_entityndata_chunk = point->has_default_data = point->defaultdata_is_modern = 1;
        point->base.parent = point->curve = nmo_ref_from_id(id);
        point->base.world_matrix[12] = positions[i].x; point->base.world_matrix[13] = positions[i].y; point->base.world_matrix[14] = positions[i].z;
        curve->control_point_ids[i] = nmo_ref_from_id(points[i]);
    }
    return id;
}

static void begin(curve_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->curve = path(&g->f,"Path",path_positions,3,g->points); ASSERT_TRUE(g->curve != 0);
}

static void commit(curve_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void points(nmo_engine_t *engine, nmo_object_id_t curve, const nmo_object_id_t *expected, size_t count)
{
    const eng_curve_points_t *actual = eng_curve_points(engine,curve); ASSERT_NOT_NULL(actual); ASSERT_EQ(count,actual->count);
    for (size_t i = 0; i < count; i++) ASSERT_EQ(expected[i],actual->ids[i]);
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,1e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,1e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,1e-4f);
}

static void same_curve(nmo_engine_t *engine, nmo_object_id_t actual, nmo_object_id_t reference)
{
    float a = NAN, b = NAN; ASSERT_TRUE(eng_curve_length(engine,actual,&a)); ASSERT_TRUE(eng_curve_length(engine,reference,&b)); ASSERT_TRUE(isfinite(a) && isfinite(b)); ASSERT_FLOAT_EQ(b,a,1e-4f);
    const float steps[] = {0,.125f,.25f,.5f,.75f,.875f,1};
    for (size_t i = 0; i < COUNT(steps); i++) {
        eng_vector_t p, q; ASSERT_TRUE(eng_curve_position(engine,actual,steps[i],&p)); ASSERT_TRUE(eng_curve_position(engine,reference,steps[i],&q)); vector(p,q);
    }
}

static bool dynamic(const nmo_engine_t *engine, nmo_object_id_t id)
{
    for (size_t i = 0; i < engine->dynamic.objects.count; i++) if (engine->dynamic.objects.data[i] == id) return true;
    return false;
}

static nmo_object_id_t new_point(curve_fixture_t *g, eng_vector_t position, bool is_dynamic)
{
    nmo_object_id_t id = eng_create_related_object_with_options(g->f.engine,g->curve,NMO_CID_CURVEPOINT,"X",is_dynamic);
    if (id == 0) return 0;
    eng_entity_set_position(g->f.engine,eng_entity_index(g->f.engine,id),&position,-1,false);
    eng_entity_set_scale(g->f.engine,eng_entity_index(g->f.engine,id),&(eng_vector_t){.4f,.4f,.4f},false,true);
    return id;
}

TEST(engine_add_control_point, insertion_uses_native_order_rejects_duplicates_and_keeps_world_transform)
{
    for (int at = 0; at < 4; at++) {
        curve_fixture_t g; begin(&g); commit(&g);
        eng_entity_set_scale(g.f.engine,eng_entity_index(g.f.engine,g.curve),&(eng_vector_t){2,3,4},false,true);
        eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.curve),&(eng_vector_t){20,10,5},-1,false);
        nmo_object_id_t added = new_point(&g,(eng_vector_t){3,6,9},false); ASSERT_TRUE(added != 0); float before[16], after[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,added,before));
        ASSERT_TRUE(eng_curve_insert_point(g.f.engine,g.curve,at < 3 ? g.points[at] : g.f.owner_id,added));
        nmo_object_id_t expected[4]; for (int i = 0, old = 0; i < 4; i++) expected[i] = i == at ? added : g.points[old++]; points(g.f.engine,g.curve,expected,4);
        ASSERT_EQ(g.curve,nmo_engine_entity_parent(g.f.engine,added)); ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,added,after)); ASSERT_EQ(0,memcmp(before,after,sizeof(before)));
        vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,added),false),(eng_vector_t){.4f,.4f,.4f}); ASSERT_FALSE(eng_curve_insert_point(g.f.engine,g.curve,0,added)); points(g.f.engine,g.curve,expected,4);
        eng_curvepoint_properties_t value; ASSERT_TRUE(eng_curvepoint_get_properties(g.f.engine,added,&value)); ASSERT_FLOAT_EQ(0,value.bias,0); ASSERT_FLOAT_EQ(0,value.tension,0); ASSERT_FLOAT_EQ(0,value.continuity,0); ASSERT_EQ(0,value.linear);
        const nmo_curve_state_t *original = fixture_state(&g.f,g.curve); ASSERT_EQ(3u,original->control_point_count); fixture_end(&g.f);
    }
}

TEST(engine_add_control_point, inserted_default_points_recompute_open_closed_tcb_and_fitted_curves)
{
    for (int at = 0; at < 4; at++) {
        curve_fixture_t g; begin(&g); eng_vector_t positions[4]; const eng_vector_t inserted = {12,3,0};
        for (int i = 0, old = 0; i < 4; i++) positions[i] = i == at ? inserted : path_positions[old++];
        nmo_object_id_t reference_points[4], reference = path(&g.f,"Reference",positions,4,reference_points); ASSERT_TRUE(reference != 0); commit(&g);
        nmo_object_id_t added = new_point(&g,inserted,false); ASSERT_TRUE(added != 0); ASSERT_TRUE(eng_curve_insert_point(g.f.engine,g.curve,at < 3 ? g.points[at] : 0,added));
        for (int opened = 0; opened < 2; opened++) for (int fitted = 0; fitted < 2; fitted++) {
            const eng_curve_properties_t properties = {opened,100,fitted ? .25f : 0};
            ASSERT_TRUE(eng_curve_set_properties(g.f.engine,g.curve,&properties)); ASSERT_TRUE(eng_curve_set_properties(g.f.engine,reference,&properties)); same_curve(g.f.engine,g.curve,reference);
        }
        fixture_end(&g.f);
    }
}

TEST(engine_add_control_point, empty_curves_accept_points_and_live_initial_conditions_restore_their_membership)
{
    curve_fixture_t g; begin(&g); commit(&g); ASSERT_TRUE(eng_curve_set_points(g.f.engine,g.curve,&(eng_curve_points_t){0}));
    nmo_object_id_t a = new_point(&g,(eng_vector_t){0,0,0},false), b = new_point(&g,(eng_vector_t){2,0,0},false); ASSERT_TRUE(a != 0 && b != 0);
    ASSERT_TRUE(eng_curve_insert_point(g.f.engine,g.curve,0,a)); points(g.f.engine,g.curve,&a,1); float length = NAN; ASSERT_TRUE(eng_curve_length(g.f.engine,g.curve,&length)); ASSERT_FLOAT_EQ(0,length,0);
    ASSERT_TRUE(eng_curve_insert_point(g.f.engine,g.curve,0,b)); points(g.f.engine,g.curve,(nmo_object_id_t[]){a,b},2); ASSERT_TRUE(eng_curve_length(g.f.engine,g.curve,&length)); ASSERT_FLOAT_EQ(2,length,1e-4f);
    eng_save_ic(g.f.engine,g.curve); ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,a)); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,(nmo_object_id_t[]){a,b},2);
    nmo_engine_destroy_object(g.f.engine,a); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,&b,1); fixture_end(&g.f);
}

TEST(engine_add_control_point, created_points_share_the_curve_session_and_remap_with_its_copy)
{
    curve_fixture_t g; begin(&g); commit(&g); nmo_object_id_t added = new_point(&g,(eng_vector_t){12,3,0},true); ASSERT_TRUE(added != 0); ASSERT_TRUE(eng_curve_insert_point(g.f.engine,g.curve,0,added));
    const eng_module_t *origin = eng_module_of(g.f.engine,g.curve,NULL), *created = eng_module_of(g.f.engine,added,NULL); ASSERT_TRUE(origin->repo == created->repo); ASSERT_TRUE(dynamic(g.f.engine,added));
    const nmo_object_id_t originals[] = {g.curve,g.points[0],g.points[1],g.points[2],added}; nmo_object_id_t copies[5] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,originals,COUNT(originals),false,copies)); points(g.f.engine,copies[0],&copies[1],4); same_curve(g.f.engine,copies[0],g.curve); ASSERT_EQ(copies[0],nmo_engine_entity_parent(g.f.engine,copies[4]));
    for (size_t i = 0; i < COUNT(originals); i++) nmo_engine_destroy_object(g.f.engine,originals[i]);
    points(g.f.engine,copies[0],&copies[1],4); eng_vector_t end; ASSERT_TRUE(eng_curve_position(g.f.engine,copies[0],1,&end)); vector(end,(eng_vector_t){12,3,0}); fixture_end(&g.f);
}

TEST(engine_add_control_point, explicit_dynamic_flags_duplicate_names_and_invalid_insertions_preserve_other_state)
{
    curve_fixture_t g; begin(&g); commit(&g); eng_mark_dynamic(g.f.engine,g.curve);
    nmo_object_id_t added[20];
    for (size_t i = 0; i < COUNT(added); i++) {
        added[i] = new_point(&g,(eng_vector_t){(float)i,0,0},i%2 != 0); ASSERT_TRUE(added[i] != 0); ASSERT_EQ(i%2 != 0,dynamic(g.f.engine,added[i])); ASSERT_STR_EQ("X",nmo_engine_object_name(g.f.engine,added[i]));
        ASSERT_TRUE(eng_curve_insert_point(g.f.engine,g.curve,0,added[i]));
    }
    const eng_curve_points_t *list = eng_curve_points(g.f.engine,g.curve); ASSERT_NOT_NULL(list); ASSERT_EQ(23u,list->count);
    for (size_t i = 0; i < COUNT(added); i++) ASSERT_EQ(added[i],list->ids[i+3]);
    ASSERT_FALSE(eng_curve_insert_point(g.f.engine,0,0,added[0])); ASSERT_FALSE(eng_curve_insert_point(g.f.engine,g.f.owner_id,0,added[0])); ASSERT_FALSE(eng_curve_insert_point(g.f.engine,g.curve,0,g.f.owner_id));
    nmo_engine_destroy_object(g.f.engine,added[0]); ASSERT_FALSE(eng_curve_insert_point(g.f.engine,g.curve,0,added[0]));
    nmo_object_id_t inherited = eng_create_related_object(g.f.engine,g.curve,NMO_CID_CURVEPOINT); ASSERT_TRUE(inherited != 0 && dynamic(g.f.engine,inherited)); fixture_end(&g.f);
}

static void graph_begin(curve_fixture_t *g)
{
    begin(g); g->f.script_id = fixture_add_script(&g->f,"Add Point Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_ADD,"Add Control Point"); g->out = fixture_block(&g->f,GUID_NOP,"After Addition"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->curve);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static nmo_object_id_t source(curve_fixture_t *g, nmo_object_id_t pin)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,pin));
}

static void setting(curve_fixture_t *g, int pos, const char *value)
{
    const nmo_behavior_state_t *block = fixture_state(&g->f,g->block);
    nmo_object_id_t parameter = nmo_behavior_ref_array_get_id(&block->local_parameters,(size_t)pos); ASSERT_TRUE(parameter != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g->f.tx,parameter,value));
}

static void graph_commit(curve_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options);
    ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_ADD)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static nmo_object_id_t added(curve_fixture_t *g)
{
    nmo_object_id_t point = 0; read_value(&g->f,BLOCK_POUT(&g->f,g->block,0),&point,sizeof(point)); return point;
}

static void trigger(curve_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

TEST(engine_add_control_point, block_defaults_append_unique_dynamic_points_and_preserve_percentage_without_scheduling)
{
    curve_fixture_t g; graph_begin(&g); graph_commit(&g); const float sentinel = 42;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,1),&sentinel,sizeof(sentinel))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    nmo_object_id_t first = added(&g); ASSERT_TRUE(first != 0); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[1],g.points[2],first},4);
    ASSERT_STR_EQ("X",nmo_engine_object_name(g.f.engine,first)); ASSERT_TRUE(dynamic(g.f.engine,first)); ASSERT_EQ(g.curve,nmo_engine_entity_parent(g.f.engine,first));
    vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,first),-1),(eng_vector_t){0,0,0}); vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,first),false),(eng_vector_t){.4f,.4f,.4f});
    float percentage = NAN; read_value(&g.f,BLOCK_POUT(&g.f,g.block,1),&percentage,sizeof(percentage)); ASSERT_FLOAT_EQ(sentinel,percentage,0); ASSERT_EQ(1u,runs(&g.f,g.out)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block)); trigger(&g); nmo_object_id_t second = added(&g); ASSERT_TRUE(second != 0 && second != first); ASSERT_STR_EQ("X",nmo_engine_object_name(g.f.engine,second));
    points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[1],g.points[2],first,second},5); fixture_end(&g.f);
}

TEST(engine_add_control_point, block_accepts_negative_and_end_indices_but_rejects_indices_beyond_live_count)
{
    const int32_t indices[] = {INT32_MIN,-7,-1,0,1,2,3,4};
    for (size_t i = 0; i < COUNT(indices); i++) {
        curve_fixture_t g; graph_begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),indices[i]); graph_commit(&g);
        size_t before = nmo_engine_entity_count(g.f.engine); const nmo_object_id_t sentinel = g.points[0]; const float percentage = 17;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&sentinel,sizeof(sentinel))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,1),&percentage,sizeof(percentage)));
        if (indices[i] > 3) {
            ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result);
            ASSERT_EQ(before,nmo_engine_entity_count(g.f.engine)); ASSERT_EQ(sentinel,added(&g)); points(g.f.engine,g.curve,g.points,3); ASSERT_EQ(0u,runs(&g.f,g.out));
            ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
            float unchanged = NAN; read_value(&g.f,BLOCK_POUT(&g.f,g.block,1),&unchanged,sizeof(unchanged)); ASSERT_FLOAT_EQ(percentage,unchanged,0);
        } else {
            ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); nmo_object_id_t expected[4], point = added(&g); ASSERT_TRUE(point != 0);
            int at = indices[i] < 0 ? 3 : indices[i]; for (int n = 0, old = 0; n < 4; n++) expected[n] = n == at ? point : g.points[old++]; points(g.f.engine,g.curve,expected,4);
        }
        fixture_end(&g.f);
    }
    curve_fixture_t g; graph_begin(&g); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),3); graph_commit(&g); ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[1]));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result); fixture_end(&g.f);
}

TEST(engine_add_control_point, disconnected_position_reference_and_index_use_native_zero_fallbacks)
{
    curve_fixture_t g; graph_begin(&g); for (int i = 0; i < 3; i++) ASSERT_EQ(NMO_OK,nmo_script_edit_disconnect_parameter(g.f.tx,BLOCK_PIN(&g.f,g.block,i)));
    graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); nmo_object_id_t point = added(&g); ASSERT_TRUE(point != 0);
    points(g.f.engine,g.curve,(nmo_object_id_t[]){point,g.points[0],g.points[1],g.points[2]},4); vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,point),-1),(eng_vector_t){0,0,0}); fixture_end(&g.f);
}

TEST(engine_add_control_point, reference_coordinates_are_applied_before_parenting_without_inheriting_curve_scale)
{
    for (int deleted = 0; deleted < 2; deleted++) {
        curve_fixture_t g; graph_begin(&g); nmo_object_id_t ref = fixture_create(&g.f,NMO_CID_3DENTITY,"Reference"); ASSERT_TRUE(ref != 0);
        nmo_3dentity_state_t *state = fixture_state(&g.f,ref); state->has_entityndata_chunk = 1;
        const float matrix[16] = {0,2,0,0,-3,0,0,0,0,0,4,0,10,20,30,1}; memcpy(state->world_matrix,matrix,sizeof(matrix));
        const eng_vector_t position = {1,2,3}; fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&position,sizeof(position)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,1),(int32_t)ref); graph_commit(&g);
        eng_entity_set_scale(g.f.engine,eng_entity_index(g.f.engine,g.curve),&(eng_vector_t){2,3,4},false,true); eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.curve),&(eng_vector_t){-5,7,1},-1,false);
        if (deleted) nmo_engine_destroy_object(g.f.engine,ref);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); nmo_object_id_t point = added(&g); ASSERT_TRUE(point != 0); eng_vector_t expected = deleted ? position : (eng_vector_t){4,22,42};
        vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,point),-1),expected); vector(eng_entity_scale(g.f.engine,eng_entity_index(g.f.engine,point),false),(eng_vector_t){.4f,.4f,.4f}); ASSERT_EQ(g.curve,nmo_engine_entity_parent(g.f.engine,point));
        eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.curve),&(eng_vector_t){-3,10,5},-1,false); expected.x += 2; expected.y += 3; expected.z += 4;
        vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,point),-1),expected); fixture_end(&g.f);
    }
}

TEST(engine_add_control_point, percentage_uses_the_new_point_length_before_curve_update_including_zero_length_nan)
{
    for (int zero_length = 0; zero_length < 2; zero_length++) {
        curve_fixture_t g; graph_begin(&g); setting(&g,1,"TRUE"); fixture_input_int(&g.f,BLOCK_PIN(&g.f,g.block,2),1);
        if (zero_length) for (size_t i = 0; i < 3; i++) { nmo_curvepoint_state_t *state = fixture_state(&g.f,g.points[i]); state->base.world_matrix[12] = state->base.world_matrix[13] = 0; }
        graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float percentage = 42; read_value(&g.f,BLOCK_POUT(&g.f,g.block,1),&percentage,sizeof(percentage));
        if (zero_length) ASSERT_TRUE(isnan(percentage)); else ASSERT_FLOAT_EQ(0,percentage,0); ASSERT_TRUE(added(&g) != 0); fixture_end(&g.f);
    }
}

TEST(engine_add_control_point, explicit_static_setting_overrides_a_dynamic_curve)
{
    curve_fixture_t g; graph_begin(&g); setting(&g,0,"FALSE"); graph_commit(&g); eng_mark_dynamic(g.f.engine,g.curve); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    nmo_object_id_t point = added(&g); ASSERT_TRUE(point != 0); ASSERT_FALSE(dynamic(g.f.engine,point)); ASSERT_TRUE(dynamic(g.f.engine,g.curve)); fixture_end(&g.f);
}

TEST(engine_add_control_point, invalid_targets_change_io_but_preserve_outputs_and_do_not_create_points)
{
    for (int kind = 0; kind < 3; kind++) {
        curve_fixture_t g; graph_begin(&g); nmo_object_id_t target = source(&g,fixture_target(&g.f,g.block)); graph_commit(&g);
        nmo_object_id_t bad = kind == 0 ? 0 : kind == 1 ? g.curve : g.points[0]; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,target,&bad,sizeof(bad))); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.curve);
        size_t before = nmo_engine_entity_count(g.f.engine); nmo_object_id_t sentinel = g.points[0]; const float percentage = 19;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&sentinel,sizeof(sentinel))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,1),&percentage,sizeof(percentage)));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_EQ(before,nmo_engine_entity_count(g.f.engine)); ASSERT_EQ(sentinel,added(&g)); float value = NAN; read_value(&g.f,BLOCK_POUT(&g.f,g.block,1),&value,sizeof(value)); ASSERT_FLOAT_EQ(percentage,value,0);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.out)); fixture_end(&g.f);
    }
}

TEST(engine_add_control_point, output_points_drive_properties_curve_following_and_removal_in_the_same_frame)
{
    curve_fixture_t g; graph_begin(&g);
    for (size_t i = 0; i < 3; i++) ((nmo_curvepoint_state_t *)fixture_state(&g.f,g.points[i]))->linear = 1;
    fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&(eng_vector_t){12,0,0},sizeof(eng_vector_t));
    nmo_object_id_t setter = fixture_block(&g.f,NMO_GUID(0x1254effa,0xfebec112),"Edit New Point"), getter = fixture_block(&g.f,NMO_GUID(0x1251effa,0xaebec112),"Read New Point");
    nmo_object_id_t properties = fixture_block(&g.f,NMO_GUID(0x1251adfa,0xaebec002),"Read Extended Curve"), follow = fixture_block(&g.f,NMO_GUID(0x676776d0,0x20d457bd),"Follow Extended Curve");
    nmo_object_id_t remover = fixture_block(&g.f,NMO_GUID(0x1234effa,0xffbec112),"Remove New Point"); ASSERT_TRUE(setter != 0 && getter != 0 && properties != 0 && follow != 0 && remover != 0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,g.block,0),fixture_target(&g.f,setter))); ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,g.block,0),fixture_target(&g.f,getter)));
    const float values[] = {.25f,.5f,.75f}; for (int i = 0; i < 3; i++) fixture_input_value(&g.f,BLOCK_PIN(&g.f,setter,i),&values[i],sizeof(float)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,setter,3),-7);
    fixture_input_int(&g.f,fixture_target(&g.f,properties),(int32_t)g.curve); fixture_input_int(&g.f,fixture_target(&g.f,follow),(int32_t)g.f.owner_id);
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,follow,0),(int32_t)g.curve); fixture_input_value(&g.f,BLOCK_PIN(&g.f,follow,1),&(float){1},sizeof(float)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,follow,2),0);
    fixture_input_int(&g.f,fixture_target(&g.f,remover),(int32_t)g.curve); ASSERT_EQ(NMO_OK,nmo_script_edit_connect_parameter(g.f.tx,BLOCK_POUT(&g.f,g.block,0),BLOCK_PIN(&g.f,remover,0)));
    const nmo_object_id_t chain[] = {g.out,setter,getter,properties,follow,remover};
    for (size_t i = 1; i < COUNT(chain); i++) fixture_link(&g.f,BLOCK_OUT(&g.f,chain[i-1],0),BLOCK_IN(&g.f,chain[i],0),0);
    graph_commit(&g); nmo_object_id_t previous = 0;
    for (int frame = 0; frame < 2; frame++) {
        if (frame) trigger(&g); else ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        nmo_object_id_t point = added(&g); ASSERT_TRUE(point != 0 && point != previous); previous = point; points(g.f.engine,g.curve,g.points,3); ASSERT_EQ(g.curve,nmo_engine_entity_parent(g.f.engine,point));
        eng_curvepoint_properties_t actual; ASSERT_TRUE(eng_curvepoint_get_properties(g.f.engine,point,&actual)); ASSERT_EQ(-7,actual.linear);
        for (int i = 0; i < 3; i++) { float value = NAN; read_value(&g.f,BLOCK_POUT(&g.f,getter,i),&value,sizeof(value)); ASSERT_FLOAT_EQ(values[i],value,0); }
        float length = NAN; read_value(&g.f,BLOCK_POUT(&g.f,properties,3),&length,sizeof(length)); ASSERT_FLOAT_EQ(8*sqrtf(2)+4,length,1e-4f);
        vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),-1),(eng_vector_t){12,0,0});
    }
    ASSERT_EQ(2u,runs(&g.f,remover)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
}

TEST(engine_add_control_point, deferred_dynamic_cleanup_removes_dynamic_points_and_preserves_static_points_after_curve_deletion)
{
    for (int mode = 0; mode < 3; mode++) {
        curve_fixture_t g; graph_begin(&g); setting(&g,0,mode == 0 ? "TRUE" : "FALSE"); fixture_input_value(&g.f,BLOCK_PIN(&g.f,g.block,0),&(eng_vector_t){12,3,0},sizeof(eng_vector_t));
        nmo_object_id_t cleanup = fixture_block(&g.f,NMO_GUID(0x0cba3195,0x53440e4c),"Delete Dynamic Points"), getter = fixture_block(&g.f,NMO_GUID(0x1251adfa,0xaebec002),"Before Cleanup"); ASSERT_TRUE(cleanup != 0 && getter != 0);
        fixture_input_int(&g.f,fixture_target(&g.f,getter),(int32_t)g.curve); fixture_link(&g.f,BLOCK_OUT(&g.f,g.out,0),BLOCK_IN(&g.f,cleanup,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,cleanup,0),BLOCK_IN(&g.f,getter,0),0);
        graph_commit(&g); if (mode == 2) eng_mark_dynamic(g.f.engine,g.curve);
        float original_length = NAN; ASSERT_TRUE(eng_curve_length(g.f.engine,g.curve,&original_length)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
        nmo_object_id_t point = added(&g); ASSERT_TRUE(point != 0); float before_cleanup = NAN; read_value(&g.f,BLOCK_POUT(&g.f,getter,3),&before_cleanup,sizeof(before_cleanup)); ASSERT_TRUE(isfinite(before_cleanup) && before_cleanup > original_length);
        if (mode == 0) { ASSERT_TRUE(eng_entity_index(g.f.engine,point) < 0); points(g.f.engine,g.curve,g.points,3); }
        else {
            ASSERT_TRUE(eng_entity_index(g.f.engine,point) >= 0); vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,point),-1),(eng_vector_t){12,3,0});
            if (mode == 2) { ASSERT_TRUE(eng_entity_index(g.f.engine,g.curve) < 0); ASSERT_EQ(0u,nmo_engine_entity_parent(g.f.engine,point)); }
            else points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[1],g.points[2],point},4);
        }
        ASSERT_EQ(1u,runs(&g.f,getter)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
    }
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static nmo_object_id_t runtime_parameter(nmo_engine_t *engine, nmo_object_id_t block, int pos, bool output)
{
    nmo_object_t *object = eng_find_object(engine,block);
    const nmo_behavior_state_t *behavior = object != NULL ? nmo_object_get_state(object) : NULL;
    const eng_module_t *module = eng_module_of(engine,block,NULL);
    if (behavior == NULL || module == NULL) return 0;
    nmo_object_id_t pin = output ? nmo_behavior_ref_array_get_id(&behavior->out_parameters,(size_t)pos) : pos < 0 ? nmo_behavior_target_parameter_id(behavior) : nmo_behavior_ref_array_get_id(&behavior->in_parameters,(size_t)pos);
    if (output) return eng_module_id(module,pin);
    object = eng_find_object(engine,eng_module_id(module,pin));
    const nmo_parameterin_state_t *input = object != NULL ? nmo_object_get_state(object) : NULL;
    return input != NULL ? eng_module_id(module,nmo_parameterin_source_id(input)) : 0;
}

TEST(engine_add_control_point, loaded_and_copied_graphs_remap_generated_points_and_keep_future_insertions_independent)
{
    curve_fixture_t g; graph_begin(&g); setting(&g,0,"FALSE"); char text[32]; snprintf(text,sizeof(text),"%u",g.curve);
    ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,source(&g,fixture_target(&g.f,g.block)),text));
    nmo_object_id_t position_source = 0; ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g.f.tx,BLOCK_PIN(&g.f,g.block,0),&position_source)); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,position_source,"12,3,0"));
    fixture_commit_bare(&g.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *file_path = NMO_TEST_SCRATCH_FILE("engine_add_control_point.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,file_path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
        if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)file_path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"add-control-point.cmo",true,&file));
        if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t block = nmo_engine_object_named(host.engine,"Add Control Point",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0); const eng_module_t *module = eng_module_of(host.engine,block,NULL);
        const char *names[] = {"Owner","Path","Path Point 0","Path Point 1","Path Point 2","Add Point Script","Add Control Point","After Addition"}; nmo_object_id_t ids[12] = {0}, copies[12] = {0};
        for (size_t i = 0; i < COUNT(names); i++) { nmo_object_t *object = nmo_object_repository_find_by_name(module->repo,names[i]); ASSERT_NOT_NULL(object); ids[i] = eng_module_id(module,nmo_object_get_id(object)); ASSERT_TRUE(ids[i] != 0); }
        ids[8] = runtime_parameter(host.engine,block,-1,false); ids[9] = runtime_parameter(host.engine,block,0,false); ids[10] = runtime_parameter(host.engine,block,2,false); ASSERT_TRUE(ids[8] != 0 && ids[9] != 0 && ids[10] != 0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); read_value(&host,runtime_parameter(host.engine,block,0,true),&ids[11],sizeof(ids[11])); ASSERT_TRUE(ids[11] != 0);
        points(host.engine,ids[1],(nmo_object_id_t[]){ids[2],ids[3],ids[4],ids[11]},4); vector(eng_entity_position(host.engine,eng_entity_index(host.engine,ids[11]),-1),(eng_vector_t){12,3,0});
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies)); for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
        points(host.engine,copies[1],(nmo_object_id_t[]){copies[2],copies[3],copies[4],copies[11]},4); same_curve(host.engine,copies[1],ids[1]); ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[11]));
        nmo_object_id_t reference = 0; read_value(&host,copies[8],&reference,sizeof(reference)); ASSERT_EQ(copies[1],reference); read_value(&host,runtime_parameter(host.engine,copies[6],0,true),&reference,sizeof(reference)); ASSERT_EQ(copies[11],reference);
        const eng_vector_t first_position = {-4,2,0}; const int32_t first = 0;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&first_position,sizeof(first_position))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],&first,sizeof(first)));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); nmo_object_id_t first_point = 0; read_value(&host,runtime_parameter(host.engine,copies[6],0,true),&first_point,sizeof(first_point)); ASSERT_TRUE(first_point != 0);
        points(host.engine,copies[1],(nmo_object_id_t[]){first_point,copies[2],copies[3],copies[4],copies[11]},5); points(host.engine,ids[1],(nmo_object_id_t[]){ids[2],ids[3],ids[4],ids[11]},4);
        nmo_engine_destroy_object(host.engine,ids[1]); for (size_t i = 2; i < 5; i++) nmo_engine_destroy_object(host.engine,ids[i]); nmo_engine_destroy_object(host.engine,ids[11]); nmo_engine_destroy_object(host.engine,ids[0]);
        eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[1]),&(eng_vector_t){20,10,5},-1,false);
        const eng_vector_t last_position = {16,0,0}; const int32_t last = -1;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&last_position,sizeof(last_position))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],&last,sizeof(last)));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); nmo_object_id_t last_point = 0; read_value(&host,runtime_parameter(host.engine,copies[6],0,true),&last_point,sizeof(last_point)); ASSERT_TRUE(last_point != 0 && last_point != first_point);
        points(host.engine,copies[1],(nmo_object_id_t[]){first_point,copies[2],copies[3],copies[4],copies[11],last_point},6); vector(eng_entity_position(host.engine,eng_entity_index(host.engine,last_point),-1),last_position); ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,last_point));
        ASSERT_EQ(2u,runs(&host,copies[7])); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100)); ASSERT_EQ(2u,runs(&host,copies[6])); fixture_end(&host);
    }
    const nmo_curve_state_t *original = fixture_state(&g.f,g.curve); ASSERT_EQ(3u,original->control_point_count); fixture_end(&g.f); remove(file_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_add_control_point, insertion_uses_native_order_rejects_duplicates_and_keeps_world_transform);
REGISTER_TEST(engine_add_control_point, inserted_default_points_recompute_open_closed_tcb_and_fitted_curves);
REGISTER_TEST(engine_add_control_point, empty_curves_accept_points_and_live_initial_conditions_restore_their_membership);
REGISTER_TEST(engine_add_control_point, created_points_share_the_curve_session_and_remap_with_its_copy);
REGISTER_TEST(engine_add_control_point, explicit_dynamic_flags_duplicate_names_and_invalid_insertions_preserve_other_state);
REGISTER_TEST(engine_add_control_point, block_defaults_append_unique_dynamic_points_and_preserve_percentage_without_scheduling);
REGISTER_TEST(engine_add_control_point, block_accepts_negative_and_end_indices_but_rejects_indices_beyond_live_count);
REGISTER_TEST(engine_add_control_point, disconnected_position_reference_and_index_use_native_zero_fallbacks);
REGISTER_TEST(engine_add_control_point, reference_coordinates_are_applied_before_parenting_without_inheriting_curve_scale);
REGISTER_TEST(engine_add_control_point, percentage_uses_the_new_point_length_before_curve_update_including_zero_length_nan);
REGISTER_TEST(engine_add_control_point, explicit_static_setting_overrides_a_dynamic_curve);
REGISTER_TEST(engine_add_control_point, invalid_targets_change_io_but_preserve_outputs_and_do_not_create_points);
REGISTER_TEST(engine_add_control_point, output_points_drive_properties_curve_following_and_removal_in_the_same_frame);
REGISTER_TEST(engine_add_control_point, deferred_dynamic_cleanup_removes_dynamic_points_and_preserves_static_points_after_curve_deletion);
REGISTER_TEST(engine_add_control_point, loaded_and_copied_graphs_remap_generated_points_and_keep_future_insertions_independent);
TEST_MAIN_END()
