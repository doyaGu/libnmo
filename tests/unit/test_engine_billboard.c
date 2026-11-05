#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"

#include <math.h>

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define PI_F 3.14159265358979323846f
#define GUID_BILLBOARD NMO_GUID(0x641822b0,0x15aa42d7)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct billboard_fixture {
    engine_fixture_t f;
    nmo_object_id_t target, camera, child, mesh, material, texture, block, out;
} billboard_fixture_t;

static void begin(billboard_fixture_t *g, uint32_t class_id)
{
    memset(g,0,sizeof(*g)); fixture_begin_document(&g->f);
    g->target = fixture_create(&g->f,(nmo_class_id_t)class_id,"Billboard Target"); g->camera = fixture_create(&g->f,NMO_CID_CAMERA,"Camera"); g->child = fixture_create(&g->f,NMO_CID_3DENTITY,"Child");
    ASSERT_TRUE(g->target != 0 && g->camera != 0 && g->child != 0);
    nmo_3dentity_state_t *target = fixture_state(&g->f,g->target), *child = fixture_state(&g->f,g->child); nmo_camera_state_t *camera = fixture_state(&g->f,g->camera);
    target->has_entityndata_chunk = child->has_entityndata_chunk = camera->entity.has_entityndata_chunk = 1;
    target->world_matrix[0] = 2; target->world_matrix[5] = 3; target->world_matrix[10] = 4; target->world_matrix[12] = 3; target->world_matrix[13] = 4; target->world_matrix[14] = 10;
    child->parent = nmo_ref_from_id(g->target); child->world_matrix[12] = 5; child->world_matrix[13] = 7; child->world_matrix[14] = 14;
    camera->has_cameraonly_chunk = 1; camera->fov = PI_F/2; camera->near_plane = 1; camera->far_plane = 101; camera->projection_type = CK_PERSPECTIVEPROJECTION;
}

static void commit(billboard_fixture_t *g)
{
    fixture_begin_edit(&g->f); fixture_commit(&g->f,NULL); ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL)); g->f.engine->render.camera = g->camera;
}

static void vector(eng_vector_t actual, eng_vector_t expected)
{
    ASSERT_TRUE(isfinite(actual.x) && isfinite(actual.y) && isfinite(actual.z));
    ASSERT_FLOAT_EQ(expected.x,actual.x,2e-4f); ASSERT_FLOAT_EQ(expected.y,actual.y,2e-4f); ASSERT_FLOAT_EQ(expected.z,actual.z,2e-4f);
}

static void matrix(const eng_matrix_t *actual, const eng_matrix_t *expected)
{
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { ASSERT_TRUE(isfinite(actual->m[i][j])); ASSERT_FLOAT_EQ(expected->m[i][j],actual->m[i][j],2e-4f); }
}

TEST(engine_billboard, matrix_uses_raw_local_axes_and_preserves_position_without_mutating_entity)
{
    billboard_fixture_t g; begin(&g,NMO_CID_3DENTITY); commit(&g); eng_matrix_t actual, before = g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world;
    eng_entity_billboard_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&(eng_vector_t){0,0,1},&(eng_vector_t){0,1,0},&actual);
    const eng_matrix_t expected = {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{3,4,10,1}}}; matrix(&actual,&expected);
    eng_entity_billboard_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&(eng_vector_t){1,2,3},&(eng_vector_t){4,5,6},&actual);
    const eng_matrix_t raw = {{{-3,4,-1,0},{6,5,-2,0},{-3,6,-3,0},{3,4,10,1}}}; matrix(&actual,&raw); matrix(&g.f.engine->entities.data[eng_entity_index(g.f.engine,g.target)].world,&before); fixture_end(&g.f);
}

TEST(engine_billboard, matrix_follows_normalized_live_camera_axes_and_uses_default_eye_after_deletion)
{
    billboard_fixture_t g; begin(&g,NMO_CID_3DENTITY); commit(&g);
    const eng_matrix_t camera = {{{0,0,-2,0},{0,3,0,0},{4,0,0,0},{7,8,9,1}}}; ASSERT_EQ(NMO_OK,nmo_engine_set_entity_world_matrix(g.f.engine,g.camera,&camera.m[0][0],false));
    eng_matrix_t actual; eng_entity_billboard_matrix(g.f.engine,eng_entity_index(g.f.engine,g.target),&(eng_vector_t){0,0,1},&(eng_vector_t){0,1,0},&actual);
    const eng_matrix_t expected = {{{0,0,1,0},{0,1,0,0},{-1,0,0,0},{3,4,10,1}}}; matrix(&actual,&expected);
    nmo_engine_destroy_object(g.f.engine,g.camera); eng_render_viewpoint(g.f.engine,&actual); eng_matrix_t identity; eng_matrix_identity(&identity); matrix(&actual,&identity); fixture_end(&g.f);
}

TEST(engine_billboard, perspective_projection_matches_render_context_pixels_depth_and_zero_w)
{
    billboard_fixture_t g; begin(&g,NMO_CID_3DENTITY); commit(&g); eng_vector_t screen;
    eng_render_project(g.f.engine,&(eng_vector_t){2,1,10},&screen); vector(screen,(eng_vector_t){384,208,.909f});
    eng_render_project(g.f.engine,&(eng_vector_t){2,1,-10},&screen); vector(screen,(eng_vector_t){256,272,1.111f});
    eng_render_project(g.f.engine,&(eng_vector_t){2,1,0},&screen); vector(screen,(eng_vector_t){0});
    eng_set_camera_planes(g.f.engine,g.camera,2,102); eng_render_project(g.f.engine,&(eng_vector_t){2,1,10},&screen); ASSERT_FLOAT_EQ(.816f,screen.z,2e-4f); fixture_end(&g.f);
}

TEST(engine_billboard, orthographic_projection_uses_zoom_screen_mode_and_live_camera_transform)
{
    billboard_fixture_t g; begin(&g,NMO_CID_3DENTITY); nmo_camera_state_t *camera = fixture_state(&g.f,g.camera); camera->projection_type = CK_ORTHOGRAPHICPROJECTION; camera->orthographic_zoom = .5f; commit(&g);
    eng_vector_t screen; eng_render_project(g.f.engine,&(eng_vector_t){2,1,10},&screen); vector(screen,(eng_vector_t){640,80,.09f});
    const eng_matrix_t eye = {{{0,0,-2,0},{0,3,0,0},{4,0,0,0},{10,20,30,1}}}; ASSERT_EQ(NMO_OK,nmo_engine_set_entity_world_matrix(g.f.engine,g.camera,&eye.m[0][0],false));
    eng_vector_t world; eng_vector_transform(&world,&eye,&(eng_vector_t){2,1,10}); eng_render_project(g.f.engine,&world,&screen); vector(screen,(eng_vector_t){640,80,.09f});
    g.f.engine->render.screen_mode = 2; const eng_screen_mode_t *mode = &eng_screen_modes[2]; eng_render_project(g.f.engine,&world,&screen); vector(screen,(eng_vector_t){(float)mode->width,(float)mode->height*.5f-(float)mode->width*.25f,.09f}); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_billboard, matrix_uses_raw_local_axes_and_preserves_position_without_mutating_entity);
REGISTER_TEST(engine_billboard, matrix_follows_normalized_live_camera_axes_and_uses_default_eye_after_deletion);
REGISTER_TEST(engine_billboard, perspective_projection_matches_render_context_pixels_depth_and_zero_w);
REGISTER_TEST(engine_billboard, orthographic_projection_uses_zoom_screen_mode_and_live_camera_transform);
TEST_MAIN_END()
