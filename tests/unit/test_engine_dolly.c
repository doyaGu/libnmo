#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a)/sizeof(*(a)))
#define GUID_DOLLY NMO_GUID(0xb98b83bb,0x87787878)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct dolly_fixture {
    engine_fixture_t f;
    nmo_object_id_t parent, camera, child, grandchild, point, other, block, after;
    nmo_object_id_t target_value, distance_value, hierarchy_value;
} dolly_fixture_t;

static void matrix(dolly_fixture_t *g, nmo_object_id_t id, eng_matrix_t value, nmo_object_id_t parent)
{
    nmo_3dentity_state_t *state = fixture_state(&g->f,id); ASSERT_NOT_NULL(state);
    memcpy(state->world_matrix,value.m,sizeof(value)); state->parent = nmo_ref_from_id(parent); state->has_entityndata_chunk = 1;
}

static void begin(dolly_fixture_t *g)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->parent = fixture_create(&g->f,NMO_CID_3DENTITY,"Camera Parent"); g->camera = fixture_create(&g->f,NMO_CID_TARGETCAMERA,"Dolly Camera");
    g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Camera Child"); g->grandchild = fixture_create(&g->f,NMO_CID_3DENTITY,"Camera Grandchild");
    g->point = fixture_create(&g->f,NMO_CID_3DENTITY,"Camera Aim"); g->other = fixture_create(&g->f,NMO_CID_CAMERA,"Ordinary Camera");
    ASSERT_TRUE(g->parent != 0 && g->camera != 0 && g->child != 0 && g->grandchild != 0 && g->point != 0 && g->other != 0);
    matrix(g,g->parent,(eng_matrix_t){{{0,2,0,0},{-3,0,0,0},{0,0,4,0},{10,20,30,1}}},0);
    matrix(g,g->camera,(eng_matrix_t){{{2,0,0,0},{0,3,0,0},{0,0,4,0},{3,4,5,1}}},g->parent);
    matrix(g,g->child,(eng_matrix_t){{{1,0,0,0},{0,1,0,0},{0,0,1,0},{5,7,9,1}}},g->camera);
    matrix(g,g->grandchild,(eng_matrix_t){{{1,0,0,0},{0,1,0,0},{0,0,1,0},{6,9,12,1}}},g->child);
    matrix(g,g->point,(eng_matrix_t){{{1,0,0,0},{0,1,0,0},{0,0,1,0},{13,4,15,1}}},0);
    nmo_camera_state_t *camera = &((nmo_targetcamera_state_t *)fixture_state(&g->f,g->camera))->base;
    camera->has_cameraonly_chunk = 1; camera->fov = .75f; camera->orthographic_zoom = 1.5f; camera->near_plane = 2; camera->far_plane = 100;
    g->f.script_id = fixture_add_script(&g->f,"Dolly Script",&g->f.script_in); ASSERT_TRUE(g->f.script_id != 0); fixture_begin_edit(&g->f);
    g->block = fixture_block(&g->f,GUID_DOLLY,"Dolly"); g->after = fixture_block(&g->f,GUID_NOP,"After Dolly"); ASSERT_TRUE(g->block != 0 && g->after != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->camera);
    g->target_value = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,0),&g->distance_value));
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,1),&g->hierarchy_value));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0); fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->after,0),0);
}

static void ready(dolly_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options); ASSERT_TRUE(nmo_engine_has_block(g->f.engine,GUID_DOLLY)); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
}

static void position(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t expected)
{
    float world[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(engine,id,world));
    ASSERT_TRUE(isfinite(world[12]) && isfinite(world[13]) && isfinite(world[14]));
    ASSERT_FLOAT_EQ(expected.x,world[12],1e-4f); ASSERT_FLOAT_EQ(expected.y,world[13],1e-4f); ASSERT_FLOAT_EQ(expected.z,world[14],1e-4f);
}

static void fire(dolly_fixture_t *g, float delta)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,delta));
}

TEST(engine_dolly, native_defaults_move_once_along_the_scaled_camera_axis_and_move_descendants)
{
    dolly_fixture_t g; begin(&g); ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    position(g.f.engine,g.camera,(eng_vector_t){3,4,9}); position(g.f.engine,g.child,(eng_vector_t){5,7,13}); position(g.f.engine,g.grandchild,(eng_vector_t){6,9,16});
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after)); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block)); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,100)); ASSERT_EQ(1u,runs(&g.f,g.block));
    float world[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.camera,world)); ASSERT_FLOAT_EQ(2,world[0],0); ASSERT_FLOAT_EQ(3,world[5],0); ASSERT_FLOAT_EQ(4,world[10],0);
    const nmo_targetcamera_state_t *source = fixture_state(&g.f,g.camera); ASSERT_FLOAT_EQ(5,source->base.entity.world_matrix[14],0);
    fixture_end(&g.f);
}

TEST(engine_dolly, live_translation_values_are_per_activation_and_keep_camera_projection_members)
{
    const float values[] = {2,-3,0}; dolly_fixture_t g; begin(&g); ready(&g); float z = 5;
    for (size_t i = 0; i < COUNT(values); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.distance_value,&values[i],sizeof(values[i]))); fire(&g,(float)i*100); z += values[i]*4;
        position(g.f.engine,g.camera,(eng_vector_t){3,4,z}); ASSERT_EQ(i+1,runs(&g.f,g.after));
    }
    g.f.engine->render.camera = g.camera; nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view));
    ASSERT_FALSE(view.orthographic); ASSERT_FLOAT_EQ(.75f,view.fov,0); ASSERT_FLOAT_EQ(1.5f,view.zoom,0); ASSERT_FLOAT_EQ(2,view.near_plane,0); ASSERT_FLOAT_EQ(100,view.far_plane,0);
    fixture_end(&g.f);
}

TEST(engine_dolly, invalid_targets_fail_before_consuming_input_or_emitting_output)
{
    for (int kind = 0; kind < 6; kind++) {
        dolly_fixture_t g; begin(&g); ready(&g);
        nmo_object_id_t target = kind == 0 ? 0 : kind == 1 ? g.other : kind == 2 ? g.parent : kind == 3 ? g.block : kind == 4 ? 0x7fffffffu : g.camera;
        if (kind == 5) nmo_engine_destroy_object(g.f.engine,g.camera);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target)));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0))); ASSERT_EQ(0u,runs(&g.f,g.after));
        if (kind != 5) position(g.f.engine,g.camera,(eng_vector_t){3,4,5}); fixture_end(&g.f);
    }
}

TEST(engine_dolly, self_axis_translation_respects_rotation_shear_mirrors_and_collapsed_axes)
{
    const eng_matrix_t matrices[] = {
        {{{0,0,-2,0},{0,3,0,0},{4,0,0,0},{3,4,5,1}}},
        {{{2,1,0,0},{.5f,3,0,0},{1,2,4,0},{3,4,5,1}}},
        {{{-2,0,0,0},{0,3,0,0},{0,0,-4,0},{3,4,5,1}}},
        {{{2,0,0,0},{0,3,0,0},{0,0,0,0},{3,4,5,1}}}
    };
    const eng_vector_t positions[] = {{11,4,5},{5,8,13},{3,4,-3},{3,4,5}};
    for (size_t i = 0; i < COUNT(matrices); i++) {
        dolly_fixture_t g; begin(&g); matrix(&g,g.camera,matrices[i],g.parent); ready(&g);
        float value = 2; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.distance_value,&value,sizeof(value)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(g.f.engine,g.camera,positions[i]);
        float world[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.camera,world));
        for (int row = 0; row < 3; row++) for (int col = 0; col < 3; col++) { ASSERT_TRUE(isfinite(world[4*row+col])); ASSERT_FLOAT_EQ(matrices[i].m[row][col],world[4*row+col],0); }
        fixture_end(&g.f);
    }
}

TEST(engine_dolly, hierarchy_moves_or_keeps_descendants_and_kept_children_follow_later_parent_motion)
{
    const int32_t values[] = {0,1,-7};
    for (size_t i = 0; i < COUNT(values); i++) {
        dolly_fixture_t g; begin(&g); ready(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.hierarchy_value,&values[i],sizeof(values[i])));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(g.f.engine,g.camera,(eng_vector_t){3,4,9});
        position(g.f.engine,g.child,(eng_vector_t){5,7,i == 0 ? 9 : 13}); position(g.f.engine,g.grandchild,(eng_vector_t){6,9,i == 0 ? 12 : 16});
        eng_vector_t moved = {110,20,30}; eng_entity_set_position(g.f.engine,eng_entity_index(g.f.engine,g.parent),&moved,-1,false);
        position(g.f.engine,g.camera,(eng_vector_t){103,4,9}); position(g.f.engine,g.child,(eng_vector_t){105,7,i == 0 ? 9 : 13});
        position(g.f.engine,g.grandchild,(eng_vector_t){106,9,i == 0 ? 12 : 16}); fixture_end(&g.f);
    }
}

TEST(engine_dolly, disconnected_inputs_keep_native_distance_and_hierarchy_fallbacks)
{
    for (int kind = 0; kind < 3; kind++) {
        dolly_fixture_t g; begin(&g);
        if (kind != 1) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)),0);
        if (kind != 0) nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,1)),0);
        ready(&g);
        if (kind == 0) { int32_t hierarchy = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.hierarchy_value,&hierarchy,sizeof(hierarchy))); }
        if (kind == 1) { float value = 2; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.distance_value,&value,sizeof(value))); }
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(g.f.engine,g.camera,(eng_vector_t){3,4,kind == 1 ? 13 : 9});
        position(g.f.engine,g.child,(eng_vector_t){5,7,kind == 0 ? 9 : kind == 1 ? 17 : 13}); fixture_end(&g.f);
    }
}

TEST(engine_dolly, changing_the_camera_target_uses_the_new_camera_axes_without_moving_the_old_camera)
{
    dolly_fixture_t g; begin(&g);
    nmo_object_id_t second = fixture_create(&g.f,NMO_CID_TARGETCAMERA,"Another Dolly Camera"); ASSERT_TRUE(second != 0);
    matrix(&g,second,(eng_matrix_t){{{1,0,0,0},{0,1,0,0},{0,0,1,0},{20,30,40,1}}},0); ready(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(g.f.engine,g.camera,(eng_vector_t){3,4,9});
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&second,sizeof(second)));
    float value = 2; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.distance_value,&value,sizeof(value)));
    fire(&g,0); position(g.f.engine,second,(eng_vector_t){20,30,42}); position(g.f.engine,g.camera,(eng_vector_t){3,4,9});
    nmo_engine_destroy_object(g.f.engine,g.camera); fire(&g,0); position(g.f.engine,second,(eng_vector_t){20,30,44}); fixture_end(&g.f);
}

TEST(engine_dolly, tiny_displacements_are_not_discarded_by_a_motion_epsilon)
{
    dolly_fixture_t g; begin(&g); matrix(&g,g.camera,(eng_matrix_t){{{2,0,0,0},{0,3,0,0},{0,0,4,0},{0,0,0,1}}},g.parent); ready(&g);
    float value = 1e-8f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.distance_value,&value,sizeof(value)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float world[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.camera,world));
    ASSERT_FLOAT_EQ(0,world[12],0); ASSERT_FLOAT_EQ(0,world[13],0); ASSERT_FLOAT_EQ(4e-8f,world[14],1e-14f); fixture_end(&g.f);
}

TEST(engine_dolly, nonfinite_distances_follow_native_float_vector_arithmetic)
{
    const float values[] = {NAN,INFINITY,-INFINITY};
    for (size_t i = 0; i < COUNT(values); i++) {
        dolly_fixture_t g; begin(&g); ready(&g);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.distance_value,&values[i],sizeof(values[i])));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); float world[16]; ASSERT_TRUE(nmo_engine_entity_world_matrix(g.f.engine,g.camera,world));
        ASSERT_TRUE(isnan(world[12]) && isnan(world[13]));
        if (i == 0) ASSERT_TRUE(isnan(world[14])); else { ASSERT_TRUE(isinf(world[14])); ASSERT_EQ(i == 2,signbit(world[14]) != 0); }
        ASSERT_EQ(1u,runs(&g.f,g.after)); fixture_end(&g.f);
    }
}

TEST(engine_dolly, render_preparation_aims_the_camera_and_dolly_uses_its_current_scaled_direction)
{
    dolly_fixture_t g; begin(&g); nmo_targetcamera_state_t *camera = fixture_state(&g.f,g.camera);
    camera->has_target = 1; camera->target = nmo_ref_from_id(g.point); ready(&g); g.f.engine->render.camera = g.camera;
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view)); float h = sqrtf(.5f);
    ASSERT_FLOAT_EQ(4*h,view.view[8],1e-5f); ASSERT_FLOAT_EQ(4*h,view.view[10],1e-5f);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); position(g.f.engine,g.camera,(eng_vector_t){3+4*h,4,5+4*h}); position(g.f.engine,g.point,(eng_vector_t){13,4,15});
    ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view)); ASSERT_EQ(g.camera,view.camera);
    ASSERT_FLOAT_EQ(3+4*h,view.view[12],1e-4f); ASSERT_FLOAT_EQ(5+4*h,view.view[14],1e-4f); ASSERT_FLOAT_EQ(.75f,view.fov,0);
    nmo_engine_destroy_object(g.f.engine,g.point); fire(&g,0); position(g.f.engine,g.camera,(eng_vector_t){3+8*h,4,5+8*h});
    ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view)); ASSERT_FLOAT_EQ(.75f,view.fov,0); ASSERT_EQ(2u,runs(&g.f,g.after)); fixture_end(&g.f);
}

TEST(engine_dolly, rotations_earlier_in_the_graph_change_the_axis_used_by_dolly_in_the_same_frame)
{
    dolly_fixture_t g; begin(&g);
    const nmo_behavior_state_t *script = fixture_state(&g.f,g.f.script_id); nmo_object_id_t link = nmo_behavior_ref_array_get_id(&script->sub_behavior_links,0);
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_behavior_link(g.f.tx,g.f.script_id,link));
    nmo_object_id_t rotate = fixture_block(&g.f,NMO_GUID(0xffffffee,0xeeffffff),"Rotate Dolly Camera");
    ASSERT_TRUE(rotate != 0); fixture_input_int(&g.f,fixture_target(&g.f,rotate),(int32_t)g.camera);
    eng_vector_t axis = {0,1,0}; float quarter = 1.57079632679489661923f;
    fixture_input_value(&g.f,BLOCK_PIN(&g.f,rotate,0),&axis,sizeof(axis)); fixture_input_value(&g.f,BLOCK_PIN(&g.f,rotate,1),&quarter,sizeof(quarter));
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,rotate,2),0); fixture_input_int(&g.f,BLOCK_PIN(&g.f,rotate,3),1);
    fixture_link(&g.f,g.f.script_in,BLOCK_IN(&g.f,rotate,0),0); fixture_link(&g.f,BLOCK_OUT(&g.f,rotate,0),BLOCK_IN(&g.f,g.block,0),0);
    ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    /* Vx3DMatrixFromRotation turns Z toward -X for a positive quarter turn about Y. */
    position(g.f.engine,g.camera,(eng_vector_t){-1,4,5});
    position(g.f.engine,g.child,(eng_vector_t){-5,7,7}); position(g.f.engine,g.grandchild,(eng_vector_t){-8,9,8});
    ASSERT_EQ(1u,runs(&g.f,rotate)); ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after)); fixture_end(&g.f);
}

static nmo_object_id_t saved_input(dolly_fixture_t *g, nmo_object_id_t pin, const char *text)
{
    nmo_object_id_t value = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,pin,&value) != NMO_OK || nmo_script_edit_set_parameter_value(g->f.tx,value,text) != NMO_OK) return 0;
    return value;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

TEST(engine_dolly, loaded_and_copied_dolly_graphs_keep_camera_targets_hierarchy_and_distances_independent)
{
    for (int hierarchy = 0; hierarchy < 2; hierarchy++) {
        dolly_fixture_t source; begin(&source); char text[32]; snprintf(text,sizeof(text),"%u",source.camera);
        ASSERT_EQ(source.target_value,saved_input(&source,fixture_target(&source.f,source.block),text));
        ASSERT_EQ(source.distance_value,saved_input(&source,BLOCK_PIN(&source.f,source.block,0),"2"));
        ASSERT_EQ(source.hierarchy_value,saved_input(&source,BLOCK_PIN(&source.f,source.block,1),hierarchy ? "TRUE" : "FALSE"));
        fixture_commit_bare(&source.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id); script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_dolly.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            engine_fixture_t host; fixture_begin_document(&host); fixture_begin_edit(&host);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.engine,resolve_file,(void *)path); int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.engine,"dolly.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.engine,"Dolly",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0); const eng_module_t *module = eng_module_of(host.engine,block,NULL);
            const nmo_object_id_t local[] = {source.f.owner_id,source.parent,source.camera,source.child,source.grandchild,source.f.script_id,source.block,source.after,source.target_value,source.distance_value,source.hierarchy_value};
            nmo_object_id_t ids[11], copies[11]; for (size_t i = 0; i < COUNT(local); i++) { ids[i] = eng_module_id(module,local[i]); ASSERT_TRUE(ids[i] != 0); }
            ASSERT_TRUE(ids[2] != source.camera); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0)); position(host.engine,ids[2],(eng_vector_t){3,4,13});
            position(host.engine,ids[3],(eng_vector_t){5,7,hierarchy ? 17 : 9}); position(host.engine,ids[4],(eng_vector_t){6,9,hierarchy ? 20 : 12});
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.engine,ids,COUNT(ids),false,copies)); ASSERT_EQ(copies[1],nmo_engine_entity_parent(host.engine,copies[2]));
            ASSERT_EQ(copies[2],nmo_engine_entity_parent(host.engine,copies[3])); ASSERT_EQ(copies[3],nmo_engine_entity_parent(host.engine,copies[4]));
            nmo_object_id_t target = 0; read_value(&host,copies[8],&target,sizeof(target)); ASSERT_EQ(copies[2],target);
            eng_vector_t moved = {110,20,30}; eng_entity_set_position(host.engine,eng_entity_index(host.engine,copies[1]),&moved,-1,false);
            float value = -1; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&value,sizeof(value)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,100));
            position(host.engine,copies[2],(eng_vector_t){103,4,9}); position(host.engine,copies[3],(eng_vector_t){105,7,hierarchy ? 13 : 9});
            position(host.engine,copies[4],(eng_vector_t){106,9,hierarchy ? 16 : 12}); position(host.engine,ids[2],(eng_vector_t){3,4,13});
            read_value(&host,ids[9],&value,sizeof(value)); ASSERT_FLOAT_EQ(2,value,0);
            nmo_engine_destroy_object(host.engine,ids[2]); nmo_engine_destroy_object(host.engine,ids[1]); nmo_engine_destroy_object(host.engine,ids[5]);
            value = 2; int32_t yes = 1; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[9],&value,sizeof(value))); ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.engine,copies[10],&yes,sizeof(yes)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.engine,copies[5],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.engine,0));
            position(host.engine,copies[2],(eng_vector_t){103,4,17}); position(host.engine,copies[3],(eng_vector_t){105,7,hierarchy ? 21 : 17}); position(host.engine,copies[4],(eng_vector_t){106,9,hierarchy ? 24 : 20});
            ASSERT_EQ(2u,runs(&host,copies[6])); ASSERT_EQ(2u,runs(&host,copies[7])); fixture_end(&host);
        }
        const nmo_targetcamera_state_t *camera = fixture_state(&source.f,source.camera); ASSERT_FLOAT_EQ(5,camera->base.entity.world_matrix[14],0); fixture_end(&source.f); remove(path);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_dolly, native_defaults_move_once_along_the_scaled_camera_axis_and_move_descendants);
REGISTER_TEST(engine_dolly, live_translation_values_are_per_activation_and_keep_camera_projection_members);
REGISTER_TEST(engine_dolly, invalid_targets_fail_before_consuming_input_or_emitting_output);
REGISTER_TEST(engine_dolly, self_axis_translation_respects_rotation_shear_mirrors_and_collapsed_axes);
REGISTER_TEST(engine_dolly, hierarchy_moves_or_keeps_descendants_and_kept_children_follow_later_parent_motion);
REGISTER_TEST(engine_dolly, disconnected_inputs_keep_native_distance_and_hierarchy_fallbacks);
REGISTER_TEST(engine_dolly, changing_the_camera_target_uses_the_new_camera_axes_without_moving_the_old_camera);
REGISTER_TEST(engine_dolly, tiny_displacements_are_not_discarded_by_a_motion_epsilon);
REGISTER_TEST(engine_dolly, nonfinite_distances_follow_native_float_vector_arithmetic);
REGISTER_TEST(engine_dolly, render_preparation_aims_the_camera_and_dolly_uses_its_current_scaled_direction);
REGISTER_TEST(engine_dolly, rotations_earlier_in_the_graph_change_the_axis_used_by_dolly_in_the_same_frame);
REGISTER_TEST(engine_dolly, loaded_and_copied_dolly_graphs_keep_camera_targets_hierarchy_and_distances_independent);
TEST_MAIN_END()
