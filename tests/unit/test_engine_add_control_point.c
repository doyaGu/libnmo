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
TEST_MAIN_END()
