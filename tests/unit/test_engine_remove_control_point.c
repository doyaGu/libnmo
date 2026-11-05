#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define GUID_REMOVE NMO_GUID(0x1234effa,0xffbec112)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct curve_fixture {
    engine_fixture_t f;
    nmo_object_id_t curve, points[3];
    nmo_object_id_t block, out;
} curve_fixture_t;

static nmo_object_id_t path(engine_fixture_t *f, const char *name, const int *order, size_t count, nmo_object_id_t *points)
{
    nmo_object_id_t id = fixture_create(f,NMO_CID_CURVE,name);
    nmo_curve_state_t *curve = fixture_state(f,id);
    if (curve == NULL) return 0;
    curve->base.has_entityndata_chunk = 1; curve->has_curve_data = curve->has_curveonly_chunk = 1;
    curve->control_point_count = (uint32_t)count;
    curve->control_point_ids = count != 0 ? nmo_arena_alloc(nmo_session_get_arena(f->session),count*sizeof(nmo_ref_t),_Alignof(nmo_ref_t)) : NULL;
    if (count != 0 && curve->control_point_ids == NULL) return 0;
    const eng_vector_t positions[] = {{0,0,0},{4,4,0},{8,0,0}};
    for (size_t i = 0; i < count; i++) {
        char point_name[64]; snprintf(point_name,sizeof(point_name),"%s Point %d",name,order[i]);
        points[i] = fixture_create(f,NMO_CID_CURVEPOINT,point_name);
        nmo_curvepoint_state_t *point = fixture_state(f,points[i]);
        if (point == NULL) return 0;
        point->base.has_entityndata_chunk = point->has_default_data = point->defaultdata_is_modern = 1;
        point->base.parent = point->curve = nmo_ref_from_id(id);
        point->base.world_matrix[12] = positions[order[i]].x; point->base.world_matrix[13] = positions[order[i]].y;
        point->bias = .2f*order[i]; point->tension = .1f*order[i]; point->continuity = -.1f*order[i];
        point->tangent_in = (nmo_vector_t){1,(float)order[i],0}; point->tangent_out = (nmo_vector_t){2,1,0};
        curve->control_point_ids[i] = nmo_ref_from_id(points[i]);
    }
    return id;
}

static void begin(curve_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->curve = path(&g->f,"Path",(int[]){0,1,2},3,g->points); ASSERT_TRUE(g->curve != 0);
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

static void length(nmo_engine_t *engine, nmo_object_id_t curve, float expected)
{
    float actual = NAN; ASSERT_TRUE(eng_curve_length(engine,curve,&actual)); ASSERT_TRUE(isfinite(actual)); ASSERT_FLOAT_EQ(expected,actual,1e-4f);
}

static void same_curve(nmo_engine_t *engine, nmo_object_id_t actual, nmo_object_id_t reference)
{
    float a = NAN, b = NAN; ASSERT_TRUE(eng_curve_length(engine,actual,&a)); ASSERT_TRUE(eng_curve_length(engine,reference,&b)); ASSERT_TRUE(isfinite(a) && isfinite(b)); ASSERT_FLOAT_EQ(b,a,1e-4f);
    const float steps[] = {0,.125f,.25f,.5f,.75f,.875f,1};
    for (size_t i = 0; i < COUNT(steps); i++) {
        eng_vector_t p, q; ASSERT_TRUE(eng_curve_position(engine,actual,steps[i],&p)); ASSERT_TRUE(eng_curve_position(engine,reference,steps[i],&q)); vector(p,q);
    }
}

TEST(engine_remove_control_point, removal_changes_curve_order_but_preserves_point_parent_transform_and_document)
{
    curve_fixture_t g; begin(&g);
    for (size_t i = 0; i < 3; i++) ((nmo_curvepoint_state_t *)fixture_state(&g.f,g.points[i]))->linear = 1;
    commit(&g); length(g.f.engine,g.curve,8*sqrtf(2));
    float before[16], after[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.points[1],before));
    ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[1])); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2); length(g.f.engine,g.curve,8);
    ASSERT_EQ(NMO_CID_CURVEPOINT,nmo_engine_object_class(g.f.engine,g.points[1])); ASSERT_EQ(g.curve,nmo_engine_entity_parent(g.f.engine,g.points[1]));
    ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.points[1],after)); ASSERT_EQ(0,memcmp(before,after,sizeof(before)));
    eng_curvepoint_properties_t properties; ASSERT_TRUE(eng_curvepoint_get_properties(g.f.engine,g.points[1],&properties)); ASSERT_FLOAT_EQ(.2f,properties.bias,0);
    eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.points[1]),&(eng_vector_t){100,100,0},-1,false); length(g.f.engine,g.curve,8);
    eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.curve),&(eng_vector_t){20,10,5},-1,false);
    vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.points[1]),-1),(eng_vector_t){120,110,5});
    const nmo_curve_state_t *loaded = fixture_state(&g.f,g.curve); ASSERT_EQ(3u,loaded->control_point_count); ASSERT_EQ(g.points[1],nmo_ref_runtime_id(&loaded->control_point_ids[1]));
    nmo_engine_t *second = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&second)); points(second,g.curve,g.points,3); nmo_engine_destroy(second); fixture_end(&g.f);
}

TEST(engine_remove_control_point, every_removal_recomputes_open_closed_tcb_explicit_and_fitted_curves)
{
    for (int removed = 0; removed < 3; removed++) for (int explicit_mode = 0; explicit_mode < 2; explicit_mode++) {
        curve_fixture_t g; begin(&g); int order[2], kept = 0;
        for (int i = 0; i < 3; i++) if (i != removed) order[kept++] = i;
        nmo_object_id_t reference_points[2], reference = path(&g.f,"Reference",order,2,reference_points); ASSERT_TRUE(reference != 0);
        for (size_t i = 0; i < 3; i++) ((nmo_curvepoint_state_t *)fixture_state(&g.f,g.points[i]))->tangent_mode = explicit_mode;
        for (size_t i = 0; i < 2; i++) ((nmo_curvepoint_state_t *)fixture_state(&g.f,reference_points[i]))->tangent_mode = explicit_mode;
        commit(&g); ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[removed]));
        for (int opened = 0; opened < 2; opened++) for (int fitted = 0; fitted < 2; fitted++) {
            eng_curve_properties_t properties = {opened,100,fitted ? .25f : 0};
            ASSERT_TRUE(eng_curve_set_properties(g.f.engine,g.curve,&properties)); ASSERT_TRUE(eng_curve_set_properties(g.f.engine,reference,&properties)); same_curve(g.f.engine,g.curve,reference);
        }
        fixture_end(&g.f);
    }
}

TEST(engine_remove_control_point, repeated_and_foreign_points_are_noops_and_empty_curves_have_zero_length)
{
    curve_fixture_t g; begin(&g); nmo_object_id_t other_points[3], other = path(&g.f,"Other",(int[]){0,1,2},3,other_points); ASSERT_TRUE(other != 0); commit(&g);
    ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,other_points[1])); points(g.f.engine,g.curve,g.points,3); points(g.f.engine,other,other_points,3);
    for (size_t i = 0; i < 3; i++) {
        ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[i])); ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[i])); points(g.f.engine,g.curve,&g.points[i+1],2-i);
        if (i >= 1) {
            length(g.f.engine,g.curve,0); eng_vector_t position = {7,8,9}; ASSERT_FALSE(eng_curve_position(g.f.engine,g.curve,.5f,&position)); vector(position,(eng_vector_t){7,8,9});
        }
        ASSERT_EQ(g.curve,nmo_engine_entity_parent(g.f.engine,g.points[i]));
    }
    fixture_end(&g.f);
}

TEST(engine_remove_control_point, copies_take_live_membership_remap_copied_points_and_keep_other_references_shared)
{
    curve_fixture_t g; begin(&g); commit(&g); ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[1]));
    const nmo_object_id_t originals[] = {g.curve,g.points[0],g.points[1],g.points[2]}; nmo_object_id_t copies[4] = {0};
    ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,originals,COUNT(originals),false,copies)); points(g.f.engine,copies[0],(nmo_object_id_t[]){copies[1],copies[3]},2); same_curve(g.f.engine,copies[0],g.curve);
    ASSERT_EQ(copies[0],nmo_engine_entity_parent(g.f.engine,copies[2])); ASSERT_TRUE(eng_curve_remove_point(g.f.engine,copies[0],copies[1])); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2);
    eng_dependencies_t none = {.flags = 1}; nmo_object_id_t shared = 0;
    ASSERT_EQ(NMO_OK,eng_copy_objects(g.f.engine,&g.curve,1,&none,false,false,&shared)); points(g.f.engine,shared,(nmo_object_id_t[]){g.points[0],g.points[2]},2);
    ASSERT_TRUE(eng_curve_remove_point(g.f.engine,shared,g.points[2])); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2);
    for (size_t i = 0; i < COUNT(originals); i++) nmo_engine_destroy_object(g.f.engine,originals[i]);
    points(g.f.engine,copies[0],&copies[3],1); length(g.f.engine,copies[0],0); points(g.f.engine,shared,NULL,0); fixture_end(&g.f);
}

static nmo_chunk_t *point_list_chunk(curve_fixture_t *g, bool modern, const nmo_object_id_t *ids, size_t count)
{
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(g->f.session));
    if (chunk == NULL) return NULL;
    chunk->class_id = NMO_CID_CURVE; nmo_chunk_set_data_version(chunk,modern ? 5 : 0);
    if (nmo_chunk_start_write(chunk) != NMO_OK || nmo_chunk_write_identifier(chunk,modern ? CK_STATESAVE_CURVEONLY : CK_STATESAVE_CURVECONTROLPOINT) != NMO_OK || nmo_chunk_write_object_sequence_start(chunk,(uint32_t)count) != NMO_OK) return NULL;
    for (size_t i = 0; i < count; i++) if (nmo_chunk_write_object_sequence_item(chunk,ids[i]) != NMO_OK) return NULL;
    if (modern && (nmo_chunk_write_float(chunk,0) != NMO_OK || nmo_chunk_write_dword(chunk,100) != NMO_OK || nmo_chunk_write_dword(chunk,1) != NMO_OK)) return NULL;
    nmo_chunk_close(chunk); return chunk;
}

TEST(engine_remove_control_point, initial_conditions_restore_only_saved_live_modern_or_legacy_membership)
{
    curve_fixture_t g; begin(&g); commit(&g); eng_save_ic(g.f.engine,g.curve);
    ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[1])); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,g.points,3);
    ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[1])); eng_save_ic(g.f.engine,g.curve);
    ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[0])); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2);
    for (int modern = 0; modern < 2; modern++) {
        nmo_chunk_t *chunk = point_list_chunk(&g,modern != 0,g.points,3); ASSERT_NOT_NULL(chunk); ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.curve,chunk)); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,g.points,3);
        ASSERT_TRUE(eng_curve_remove_point(g.f.engine,g.curve,g.points[1]));
    }
    nmo_chunk_t *properties = nmo_chunk_create(nmo_session_get_arena(g.f.session)); ASSERT_NOT_NULL(properties); properties->class_id = NMO_CID_CURVE;
    ASSERT_EQ(NMO_OK,nmo_chunk_start_write(properties)); ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(properties,CK_STATESAVE_CURVEFITCOEFF)); ASSERT_EQ(NMO_OK,nmo_chunk_write_float(properties,.25f)); nmo_chunk_close(properties);
    ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.curve,properties)); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2);
    eng_save_ic(g.f.engine,g.curve); nmo_engine_destroy_object(g.f.engine,g.points[0]); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,&g.points[2],1);
    nmo_chunk_t *empty = point_list_chunk(&g,true,NULL,0); ASSERT_NOT_NULL(empty); ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.curve,empty)); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,NULL,0); fixture_end(&g.f);
}

TEST(engine_remove_control_point, deletion_prunes_cached_and_unread_lists_without_resurrecting_points_in_copies)
{
    for (int cached = 0; cached < 2; cached++) {
        curve_fixture_t g; begin(&g); commit(&g); if (cached) points(g.f.engine,g.curve,g.points,3);
        nmo_engine_destroy_object(g.f.engine,g.points[1]); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2);
        const nmo_object_id_t originals[] = {g.curve,g.points[0],g.points[2]}; nmo_object_id_t copies[3] = {0};
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,originals,COUNT(originals),false,copies)); points(g.f.engine,copies[0],&copies[1],2); same_curve(g.f.engine,copies[0],g.curve); fixture_end(&g.f);
    }
}

TEST(engine_remove_control_point, invalid_objects_leave_membership_unchanged_and_aliasing_views_can_be_replaced)
{
    curve_fixture_t g; begin(&g); commit(&g);
    const nmo_object_id_t invalid[] = {0,g.f.owner_id,g.curve};
    for (size_t i = 0; i < COUNT(invalid); i++) { ASSERT_FALSE(eng_curve_remove_point(g.f.engine,g.curve,invalid[i])); ASSERT_FALSE(eng_curve_remove_point(g.f.engine,invalid[i] == g.curve ? g.points[0] : invalid[i],g.points[1])); }
    points(g.f.engine,g.curve,g.points,3); const eng_curve_points_t *view = eng_curve_points(g.f.engine,g.curve); ASSERT_TRUE(eng_curve_set_points(g.f.engine,g.curve,view)); points(g.f.engine,g.curve,g.points,3);
    nmo_engine_destroy_object(g.f.engine,g.points[1]); ASSERT_FALSE(eng_curve_remove_point(g.f.engine,g.curve,g.points[1])); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2);
    nmo_engine_destroy_object(g.f.engine,g.curve); ASSERT_TRUE(eng_curve_points(g.f.engine,g.curve) == NULL); ASSERT_FALSE(eng_curve_remove_point(g.f.engine,g.curve,g.points[0])); fixture_end(&g.f);
}

static void graph_begin(curve_fixture_t *g)
{
    begin(g); g->f.script_id = fixture_add_script(&g->f,"Remove Point Script",&g->f.script_in); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_REMOVE,"Remove Control Point"); g->out = fixture_block(&g->f,GUID_NOP,"After Removal"); ASSERT_TRUE(g->block != 0 && g->out != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->curve); fixture_input_int(&g->f,BLOCK_PIN(&g->f,g->block,0),(int32_t)g->points[1]);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->out,0),0);
}

static void graph_commit(curve_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&g->f,&options);
    ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_REMOVE)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static nmo_object_id_t source(curve_fixture_t *g, nmo_object_id_t pin)
{
    return nmo_parameterin_source_id(fixture_state(&g->f,pin));
}

static void disconnect_point_input(curve_fixture_t *g)
{
    nmo_object_id_t pin = BLOCK_PIN(&g->f,g->block,0), parameter = source(g,pin);
    ASSERT_EQ(NMO_OK,nmo_script_edit_disconnect_parameter(g->f.tx,pin));
    /* Disconnect deletes the source; the fixture must not write it after commit. */
    for (size_t i = 0; i < g->f.value_count; i++) if (g->f.values[i].param == parameter) {
        memmove(&g->f.values[i],&g->f.values[i+1],(g->f.value_count-i-1)*sizeof(g->f.values[0])); g->f.value_count--; break;
    }
}

static void trigger(curve_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

TEST(engine_remove_control_point, block_removes_a_live_point_once_and_repeated_removal_completes_without_scheduling)
{
    curve_fixture_t g; graph_begin(&g); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2); ASSERT_EQ(g.curve,nmo_engine_entity_parent(g.f.engine,g.points[1])); ASSERT_EQ(1u,runs(&g.f,g.out));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block));
    trigger(&g); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2); ASSERT_EQ(2u,runs(&g.f,g.out));
    eng_save_ic(g.f.engine,g.curve); eng_restore_ic(g.f.engine,g.curve); points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2); fixture_end(&g.f);
}

TEST(engine_remove_control_point, block_targets_and_input_points_can_switch_between_curves_and_accept_foreign_points)
{
    curve_fixture_t g; graph_begin(&g); nmo_object_id_t other_points[3], other = path(&g.f,"Other",(int[]){0,1,2},3,other_points); ASSERT_TRUE(other != 0);
    nmo_object_id_t target = source(&g,fixture_target(&g.f,g.block)), input = source(&g,BLOCK_PIN(&g.f,g.block,0)); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,target,&other,sizeof(other))); trigger(&g); points(g.f.engine,other,other_points,3);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,input,&other_points[0],sizeof(other_points[0]))); trigger(&g); points(g.f.engine,other,&other_points[1],2);
    points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2); ASSERT_EQ(other,nmo_engine_entity_parent(g.f.engine,other_points[0])); ASSERT_EQ(3u,runs(&g.f,g.out)); fixture_end(&g.f);
}

TEST(engine_remove_control_point, invalid_targets_consume_input_activate_output_and_report_owner_error_before_point_validation)
{
    for (int kind = 0; kind < 3; kind++) {
        curve_fixture_t g; graph_begin(&g); nmo_object_id_t target = source(&g,fixture_target(&g.f,g.block));
        disconnect_point_input(&g); graph_commit(&g);
        nmo_object_id_t bad = kind == 0 ? 0 : kind == 1 ? g.curve : g.points[0];
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,target,&bad,sizeof(bad))); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.curve);
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.out));
        if (kind != 1) points(g.f.engine,g.curve,g.points,3); fixture_end(&g.f);
    }
}

TEST(engine_remove_control_point, absent_deleted_or_wrong_class_points_return_native_invalid_parameter_after_io_changes)
{
    for (int kind = 0; kind < 3; kind++) {
        curve_fixture_t g; graph_begin(&g); nmo_object_id_t input = source(&g,BLOCK_PIN(&g.f,g.block,0));
        if (kind == 0) disconnect_point_input(&g);
        graph_commit(&g); if (kind == 1) nmo_engine_destroy_object(g.f.engine,g.points[1]);
        if (kind == 2) ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,input,&g.curve,sizeof(g.curve)));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(-1,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.out));
        if (kind == 1) points(g.f.engine,g.curve,(nmo_object_id_t[]){g.points[0],g.points[2]},2); else points(g.f.engine,g.curve,g.points,3); fixture_end(&g.f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_remove_control_point, removal_changes_curve_order_but_preserves_point_parent_transform_and_document);
REGISTER_TEST(engine_remove_control_point, every_removal_recomputes_open_closed_tcb_explicit_and_fitted_curves);
REGISTER_TEST(engine_remove_control_point, repeated_and_foreign_points_are_noops_and_empty_curves_have_zero_length);
REGISTER_TEST(engine_remove_control_point, copies_take_live_membership_remap_copied_points_and_keep_other_references_shared);
REGISTER_TEST(engine_remove_control_point, initial_conditions_restore_only_saved_live_modern_or_legacy_membership);
REGISTER_TEST(engine_remove_control_point, deletion_prunes_cached_and_unread_lists_without_resurrecting_points_in_copies);
REGISTER_TEST(engine_remove_control_point, invalid_objects_leave_membership_unchanged_and_aliasing_views_can_be_replaced);
REGISTER_TEST(engine_remove_control_point, block_removes_a_live_point_once_and_repeated_removal_completes_without_scheduling);
REGISTER_TEST(engine_remove_control_point, block_targets_and_input_points_can_switch_between_curves_and_accept_foreign_points);
REGISTER_TEST(engine_remove_control_point, invalid_targets_consume_input_activate_output_and_report_owner_error_before_point_validation);
REGISTER_TEST(engine_remove_control_point, absent_deleted_or_wrong_class_points_return_native_invalid_parameter_after_io_changes);
TEST_MAIN_END()
