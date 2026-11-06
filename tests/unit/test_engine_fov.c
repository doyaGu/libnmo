#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <math.h>

#define PI_F 3.14159265358979323846f
#define GUID_SET_FOV NMO_GUID(0xaaaacccc,0xaaaacccc)
#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct fov_fixture {
    engine_fixture_t f;
    nmo_object_id_t camera, second, block, after, target_value, angle_value;
} fov_fixture_t;

static void init_camera(fov_fixture_t *g, nmo_object_id_t id)
{
    nmo_camera_state_t *state = fixture_state(&g->f,id);
    ASSERT_NOT_NULL(state);
    state->entity.has_entityndata_chunk = state->has_cameraonly_chunk = 1;
    state->projection_type = CK_PERSPECTIVEPROJECTION;
    state->fov = PI_F/2;
    state->aspect_width = state->aspect_height = 1;
    state->near_plane = 1;
    state->far_plane = 100;
}

static void begin(fov_fixture_t *g)
{
    memset(g,0,sizeof(*g));
    fixture_begin_document(&g->f);
    g->camera = fixture_create(&g->f,NMO_CID_CAMERA,"FOV Camera");
    g->second = fixture_create(&g->f,NMO_CID_TARGETCAMERA,"Second FOV Camera");
    ASSERT_TRUE(g->camera != 0 && g->second != 0);
    init_camera(g,g->camera); init_camera(g,g->second);
    g->f.script_id = fixture_add_script(&g->f,"FOV Script",&g->f.script_in);
    ASSERT_TRUE(g->f.script_id != 0);
    fixture_begin_edit(&g->f);
}

static void ready(fov_fixture_t *g)
{
    nmo_engine_options_t options = nmo_engine_options_default();
    options.fail_on = NMO_ENGINE_FAIL_ALL;
    fixture_commit(&g->f,&options);
    ASSERT_EQ(NMO_OK,nmo_engine_start(g->f.engine,NMO_ENGINE_START_ALL));
    g->f.engine->render.camera = g->camera;
}

static void frame(fov_fixture_t *g, nmo_object_id_t camera, float fov, float front, float back)
{
    g->f.engine->render.camera = camera;
    nmo_engine_frame_t view;
    ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g->f.engine,&view));
    ASSERT_EQ(camera,view.camera);
    ASSERT_TRUE((isnan(fov) && isnan(view.fov)) || view.fov == fov);
    ASSERT_FLOAT_EQ(front,view.near_plane,0);
    ASSERT_FLOAT_EQ(back,view.far_plane,0);
}

TEST(engine_fov, runtime_fov_updates_render_projection_and_screen_coordinates_without_changing_the_document)
{
    fov_fixture_t g; begin(&g); ready(&g);
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,PI_F/3));
    frame(&g,g.camera,PI_F/3,1,100);
    eng_matrix_t projection;
    ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&projection,NULL));
    nmo_rect_t rect;
    ASSERT_TRUE(eng_project_box(&projection,&(eng_vector_t){-1,-1,10},&(eng_vector_t){1,1,10},&rect));
    ASSERT_FLOAT_EQ(.41339746f,rect.left,2e-6f);
    ASSERT_FLOAT_EQ(.41339746f,rect.top,2e-6f);
    ASSERT_FLOAT_EQ(.58660254f,rect.right,2e-6f);
    ASSERT_FLOAT_EQ(.58660254f,rect.bottom,2e-6f);
    eng_vector_t screen;
    eng_render_project(g.f.engine,&(eng_vector_t){1,0,10},&screen);
    nmo_engine_frame_t view;
    ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view));
    ASSERT_FLOAT_EQ(.58660254f*view.width,screen.x,2e-4f);
    ASSERT_FLOAT_EQ(.5f*view.height,screen.y,2e-4f);
    ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->fov,0);
    nmo_engine_t *other = NULL;
    ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&other));
    ASSERT_EQ(NMO_OK,nmo_engine_start(other,NMO_ENGINE_START_ALL));
    other->render.camera = g.camera;
    ASSERT_EQ(NMO_OK,nmo_engine_render_frame(other,&view));
    ASSERT_FLOAT_EQ(PI_F/2,view.fov,0);
    nmo_engine_destroy(other);
    fixture_end(&g.f);
}

TEST(engine_fov, fov_and_clipping_plane_changes_preserve_each_other_for_cameras_and_target_cameras)
{
    fov_fixture_t g; begin(&g); ready(&g);
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.75f));
    eng_set_camera_planes(g.f.engine,g.camera,2,50);
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,1.25f));
    frame(&g,g.camera,1.25f,2,50);
    eng_set_camera_planes(g.f.engine,g.second,3,60);
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.second,.625f));
    frame(&g,g.second,.625f,3,60);
    frame(&g,g.camera,1.25f,2,50);
    ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&g.f,g.second))->fov,0);
    fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_fov, runtime_fov_updates_render_projection_and_screen_coordinates_without_changing_the_document);
REGISTER_TEST(engine_fov, fov_and_clipping_plane_changes_preserve_each_other_for_cameras_and_target_cameras);
TEST_MAIN_END()
