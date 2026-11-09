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
        if (kind != 1) { points(g.f.engine,g.curve,g.points,3); } fixture_end(&g.f);
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

static nmo_object_id_t get_length_after(curve_fixture_t *g, nmo_object_id_t after)
{
    nmo_object_id_t block = fixture_block(&g->f,NMO_GUID(0x1251adfa,0xaebec002),"Edited Curve Properties");
    if (block == 0) return 0;
    fixture_input_int(&g->f,fixture_target(&g->f,block),(int32_t)g->curve);
    fixture_link(&g->f,BLOCK_OUT(&g->f,after,0),BLOCK_IN(&g->f,block,0),0); return block;
}

TEST(engine_remove_control_point, downstream_properties_and_position_on_curve_use_the_edited_path_in_the_same_frame)
{
    curve_fixture_t g; graph_begin(&g);
    for (size_t i = 0; i < 3; i++) ((nmo_curvepoint_state_t *)fixture_state(&g.f,g.points[i]))->linear = 1;
    nmo_object_id_t getter = get_length_after(&g,g.out), follow = fixture_block(&g.f,NMO_GUID(0x676776d0,0x20d457bd),"Follow Edited Curve"); ASSERT_TRUE(getter != 0 && follow != 0);
    fixture_input_int(&g.f,fixture_target(&g.f,follow),(int32_t)g.f.owner_id); fixture_input_int(&g.f,BLOCK_PIN(&g.f,follow,0),(int32_t)g.curve);
    fixture_input_value(&g.f,BLOCK_PIN(&g.f,follow,1),&(float){.25f},sizeof(float)); fixture_input_int(&g.f,BLOCK_PIN(&g.f,follow,2),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,getter,0),BLOCK_IN(&g.f,follow,0),0); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float value = NAN; read_value(&g.f,BLOCK_POUT(&g.f,getter,3),&value,sizeof(value)); ASSERT_FLOAT_EQ(8,value,1e-4f);
    vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),-1),(eng_vector_t){2,0,0});
    eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.points[2]),&(eng_vector_t){12,0,0},-1,false); trigger(&g);
    read_value(&g.f,BLOCK_POUT(&g.f,getter,3),&value,sizeof(value)); ASSERT_FLOAT_EQ(12,value,1e-4f); vector(eng_entity_position(g.f.engine,eng_entity_index(g.f.engine,g.f.owner_id),-1),(eng_vector_t){3,0,0});
    ASSERT_EQ(2u,runs(&g.f,getter)); ASSERT_EQ(2u,runs(&g.f,follow)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(2u,runs(&g.f,g.block)); fixture_end(&g.f);
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

TEST(engine_remove_control_point, loaded_and_copied_graphs_remap_targets_and_keep_live_membership_independent)
{
    curve_fixture_t g; graph_begin(&g); char text[32];
    snprintf(text,sizeof(text),"%u",g.curve); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,source(&g,fixture_target(&g.f,g.block)),text));
    snprintf(text,sizeof(text),"%u",g.points[1]); ASSERT_EQ(NMO_OK,nmo_script_edit_set_parameter_value(g.f.tx,source(&g,BLOCK_PIN(&g.f,g.block,0)),text));
    fixture_commit_bare(&g.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&g.f,g.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *file_path = NMO_TEST_SCRATCH_FILE("engine_remove_control_point.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(g.f.session,file_path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
        if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)file_path); int32_t file = -1;
        ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"remove-control-point.cmo",true,&file));
        if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t block = nmo_engine_object_named(host.engine,"Remove Control Point",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
        const eng_module_t *module = eng_module_of(host.engine,block,NULL);
        const char *names[] = {"Owner","Path","Path Point 0","Path Point 1","Path Point 2","Remove Point Script","Remove Control Point","After Removal"};
        nmo_object_id_t ids[10] = {0}, copies[10] = {0};
        for (size_t i = 0; i < COUNT(names); i++) {
            nmo_object_t *object = nmo_object_repository_find_by_name(module->repo,names[i]); ASSERT_NOT_NULL(object); ids[i] = eng_module_id(module,nmo_object_get_id(object)); ASSERT_TRUE(ids[i] != 0);
        }
        ids[8] = loaded_source(host.engine,ids[6],-1); ids[9] = loaded_source(host.engine,ids[6],0); ASSERT_TRUE(ids[8] != 0 && ids[9] != 0);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); points(host.engine,ids[1],(nmo_object_id_t[]){ids[2],ids[4]},2); ASSERT_EQ(ids[1],nmo_engine_entity_parent(host.engine,ids[3]));
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies)); for (size_t i = 0; i < COUNT(copies); i++) ASSERT_TRUE(copies[i] != 0);
        points(host.engine,copies[1],(nmo_object_id_t[]){copies[2],copies[4]},2); same_curve(host.engine,copies[1],ids[1]);
        eng_save_ic(host.engine,copies[1]);
        nmo_object_id_t reference = 0; read_value(&host,copies[8],&reference,sizeof(reference)); ASSERT_EQ(copies[1],reference); read_value(&host,copies[9],&reference,sizeof(reference)); ASSERT_EQ(copies[3],reference);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&copies[2],sizeof(copies[2]))); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        points(host.engine,copies[1],&copies[4],1); points(host.engine,ids[1],(nmo_object_id_t[]){ids[2],ids[4]},2);
        eng_restore_ic(host.engine,copies[1]); points(host.engine,copies[1],(nmo_object_id_t[]){copies[2],copies[4]},2);
        nmo_engine_destroy_object(host.engine,ids[1]); for (size_t i = 2; i < 5; i++) nmo_engine_destroy_object(host.engine,ids[i]); nmo_engine_destroy_object(host.engine,ids[0]);
        eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[1]),&(eng_vector_t){20,10,5},-1,false);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&copies[4],sizeof(copies[4]))); ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
        points(host.engine,copies[1],&copies[2],1); length(host.engine,copies[1],0); vector(eng_entity_position(host.engine,eng_entity_index(host.engine,copies[2]),-1),(eng_vector_t){20,10,5});
        ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[3])); ASSERT_EQ(2u,runs(&host,copies[7])); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100)); ASSERT_EQ(2u,runs(&host,copies[6])); fixture_end(&host);
    }
    const nmo_curve_state_t *original = fixture_state(&g.f,g.curve); ASSERT_EQ(3u,original->control_point_count); fixture_end(&g.f); remove(file_path);
}

TEST(engine_remove_control_point, downstream_object_deletion_prunes_membership_only_when_deferred_destruction_runs)
{
    curve_fixture_t g; graph_begin(&g);
    for (size_t i = 0; i < 3; i++) ((nmo_curvepoint_state_t *)fixture_state(&g.f,g.points[i]))->linear = 1;
    nmo_object_id_t deleter = fixture_block(&g.f,NMO_GUID(0x74120ded,0x76524673),"Delete Remaining Endpoint"); ASSERT_TRUE(deleter != 0);
    const nmo_behavior_state_t *state = fixture_state(&g.f,deleter);
    nmo_object_id_t object_pin = state->in_parameters.count > 1 ? BLOCK_PIN(&g.f,deleter,0) : fixture_target(&g.f,deleter);
    fixture_input_int(&g.f,object_pin,(int32_t)g.points[2]); fixture_link(&g.f,BLOCK_OUT(&g.f,g.out,0),BLOCK_IN(&g.f,deleter,0),0);
    nmo_object_id_t getter = get_length_after(&g,deleter); ASSERT_TRUE(getter != 0); graph_commit(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float same_frame_length = NAN; read_value(&g.f,BLOCK_POUT(&g.f,getter,3),&same_frame_length,sizeof(same_frame_length)); ASSERT_FLOAT_EQ(8,same_frame_length,1e-4f);
    points(g.f.engine,g.curve,&g.points[0],1); length(g.f.engine,g.curve,0); ASSERT_TRUE(eng_entity_index(g.f.engine,g.points[2]) < 0); ASSERT_TRUE(eng_entity_index(g.f.engine,g.points[1]) >= 0);
    ASSERT_EQ(1u,runs(&g.f,getter)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block)); fixture_end(&g.f);
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
REGISTER_TEST(engine_remove_control_point, downstream_properties_and_position_on_curve_use_the_edited_path_in_the_same_frame);
REGISTER_TEST(engine_remove_control_point, loaded_and_copied_graphs_remap_targets_and_keep_live_membership_independent);
REGISTER_TEST(engine_remove_control_point, downstream_object_deletion_prunes_membership_only_when_deferred_destruction_runs);
TEST_MAIN_END()
