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

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_dolly, native_defaults_move_once_along_the_scaled_camera_axis_and_move_descendants);
REGISTER_TEST(engine_dolly, live_translation_values_are_per_activation_and_keep_camera_projection_members);
REGISTER_TEST(engine_dolly, invalid_targets_fail_before_consuming_input_or_emitting_output);
TEST_MAIN_END()
