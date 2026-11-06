#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

#define PI_F 3.14159265358979323846f
#define GUID_GET_CURRENT_CAMERA NMO_GUID(0x368f22b1,0x222957e4)
#define GUID_SET_PROJECTION NMO_GUID(0xe444e666,0x4eee6eee)
#define GUID_ORTHOGRAPHIC_ZOOM NMO_GUID(0x0a125555,0x0eee5488)
#define GUID_SET_ZOOM NMO_GUID(0x40540a0d,0x000aaaaa)
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

static void behavior_begin_guid(fov_fixture_t *g, nmo_guid_t guid, const char *name)
{
    begin(g);
    g->block = fixture_block(&g->f,guid,name);
    g->after = fixture_block(&g->f,GUID_NOP,"After FOV");
    ASSERT_TRUE(g->block != 0 && g->after != 0);
    fixture_input_int(&g->f,fixture_target(&g->f,g->block),(int32_t)g->camera);
    g->target_value = nmo_parameterin_source_id(fixture_state(&g->f,fixture_target(&g->f,g->block)));
    ASSERT_EQ(NMO_OK,nmo_script_edit_ensure_input_parameter_source(g->f.tx,BLOCK_PIN(&g->f,g->block,0),&g->angle_value));
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->after,0),0);
}

static void behavior_begin(fov_fixture_t *g)
{
    behavior_begin_guid(g,GUID_SET_FOV,"Set FOV");
}

static void zoom_begin(fov_fixture_t *g)
{
    behavior_begin_guid(g,GUID_SET_ZOOM,"Set Zoom");
}

static void trigger(fov_fixture_t *g)
{
    ASSERT_EQ(NMO_OK,nmo_engine_activate_script(g->f.engine,g->f.script_id,true,true));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g->f.engine,0));
}

TEST(engine_fov, set_fov_uses_the_native_fifty_degree_default_and_runs_once)
{
    fov_fixture_t g; behavior_begin(&g); ready(&g);
    ASSERT_TRUE(nmo_engine_has_block(g.f.engine,GUID_SET_FOV));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float actual = 0; ASSERT_TRUE(eng_camera_fov(g.f.engine,g.camera,&actual));
    ASSERT_FLOAT_EQ(50.0f*PI_F/180,actual,1e-7f);
    frame(&g,g.camera,actual,1,100);
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10));
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    fixture_end(&g.f);
}

TEST(engine_fov, live_angle_and_target_inputs_update_only_the_selected_camera)
{
    fov_fixture_t g; behavior_begin(&g); ready(&g);
    float angle = PI_F/3;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&angle,sizeof(angle)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); frame(&g,g.camera,angle,1,100);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&g.second,sizeof(g.second)));
    angle = PI_F/4;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&angle,sizeof(angle)));
    trigger(&g); frame(&g,g.second,angle,1,100); frame(&g,g.camera,PI_F/3,1,100);
    ASSERT_EQ(2u,runs(&g.f,g.after));
    ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->fov,0);
    fixture_end(&g.f);
}

TEST(engine_fov, disconnected_angle_uses_the_native_fallback)
{
    fov_fixture_t g; behavior_begin(&g);
    nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)),0);
    ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    frame(&g,g.camera,50.0f*PI_F/180,1,100);
    fixture_end(&g.f);
}

TEST(engine_fov, set_fov_keeps_native_zero_negative_large_and_nonfinite_radians)
{
    const float angles[] = {0,-.75f,8*PI_F,NAN,INFINITY,-INFINITY};
    fov_fixture_t g; behavior_begin(&g); ready(&g);
    for (size_t i = 0; i < sizeof(angles)/sizeof(*angles); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&angles[i],sizeof(angles[i])));
        trigger(&g); frame(&g,g.camera,angles[i],1,100);
        ASSERT_EQ(i+1,runs(&g.f,g.after));
    }
    fixture_end(&g.f);
}

TEST(engine_fov, invalid_targets_fail_before_consuming_input_or_changing_another_camera)
{
    for (int kind = 0; kind < 5; kind++) {
        fov_fixture_t g; behavior_begin(&g); ready(&g);
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.second,.625f));
        nmo_object_id_t target = kind == 0 ? 0 : kind == 1 ? 0x7fffffff : kind == 2 ? g.f.owner_id : kind == 3 ? g.f.script_id : g.camera;
        if (kind == 4) {
            ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.75f));
            nmo_engine_destroy_object(g.f.engine,g.camera);
            float ignored;
            ASSERT_FALSE(eng_camera_fov(g.f.engine,g.camera,&ignored));
            ASSERT_FALSE(eng_set_camera_fov(g.f.engine,g.camera,1));
        }
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target)));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        ASSERT_EQ(0u,runs(&g.f,g.after)); frame(&g,g.second,.625f,1,100);
        fixture_end(&g.f);
    }
}

TEST(engine_fov, orthographic_cameras_store_fov_without_changing_zoom_projection)
{
    fov_fixture_t g; behavior_begin(&g);
    nmo_camera_state_t *camera = fixture_state(&g.f,g.camera);
    camera->projection_type = CK_ORTHOGRAPHICPROJECTION; camera->orthographic_zoom = 2;
    ready(&g);
    eng_matrix_t before, after; ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&before,NULL));
    float angle = .125f;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&angle,sizeof(angle)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); frame(&g,g.camera,angle,1,100);
    ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&after,NULL));
    ASSERT_EQ(0,memcmp(&before,&after,sizeof(before)));
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view));
    ASSERT_TRUE(view.orthographic); ASSERT_FLOAT_EQ(2,view.zoom,0);
    fixture_end(&g.f);
}

TEST(engine_fov, camera_copies_preserve_runtime_lenses_and_remain_independent_after_source_deletion)
{
    fov_fixture_t g; begin(&g); ready(&g);
    const nmo_object_id_t cameras[] = {g.camera,g.second};
    for (size_t type = 0; type < 2; type++) {
        nmo_object_id_t original = cameras[type], copies[40];
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,original,.75f));
        eng_set_camera_planes(g.f.engine,original,2,50);
        for (size_t i = 0; i < sizeof(copies)/sizeof(*copies); i++) {
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&original,1,false,&copies[i]));
            frame(&g,copies[i],.75f,2,50);
        }
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,original,1.25f));
        eng_set_camera_planes(g.f.engine,original,3,60);
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,copies[0],.625f));
        frame(&g,copies[0],.625f,2,50); frame(&g,copies[39],.75f,2,50);
        frame(&g,original,1.25f,3,60);
        nmo_object_id_t grandchild = 0;
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&copies[0],1,false,&grandchild));
        nmo_engine_destroy_object(g.f.engine,original);
        nmo_engine_destroy_object(g.f.engine,copies[0]);
        frame(&g,grandchild,.625f,2,50); frame(&g,copies[39],.75f,2,50);
        ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&g.f,original))->fov,0);
    }
    fixture_end(&g.f);
}

TEST(engine_fov, runtime_initial_conditions_capture_restore_and_replace_fov_with_clipping_planes)
{
    fov_fixture_t g; behavior_begin(&g); ready(&g);
    eng_add_to_level_scene(g.f.engine,g.camera);
    eng_save_ic(g.f.engine,g.camera);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    eng_set_camera_planes(g.f.engine,g.camera,2,50);
    eng_restore_ic(g.f.engine,g.camera); frame(&g,g.camera,PI_F/2,1,100);
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.75f));
    eng_set_camera_planes(g.f.engine,g.camera,3,60); eng_save_ic(g.f.engine,g.camera);
    trigger(&g); eng_set_camera_planes(g.f.engine,g.camera,4,70);
    eng_restore_ic(g.f.engine,g.camera); frame(&g,g.camera,.75f,3,60);
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,NAN)); eng_save_ic(g.f.engine,g.camera);
    trigger(&g); eng_restore_ic(g.f.engine,g.camera); frame(&g,g.camera,NAN,3,60);
    ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->fov,0);
    fixture_end(&g.f);
}

TEST(engine_fov, modern_camera_initial_values_restore_serialized_raw_fov_and_planes)
{
    const float angles[] = {.625f,-.75f,INFINITY};
    fov_fixture_t g; begin(&g); ready(&g);
    const nmo_object_id_t cameras[] = {g.camera,g.second};
    for (size_t type = 0; type < 2; type++) for (size_t i = 0; i < sizeof(angles)/sizeof(*angles); i++) {
        nmo_object_id_t id = cameras[type];
        nmo_camera_state_t saved = *(nmo_camera_state_t *)fixture_state(&g.f,id);
        saved.fov = angles[i]; saved.near_plane = 2; saved.far_plane = 50;
        nmo_arena_t *arena = nmo_session_get_arena(g.f.session);
        nmo_chunk_t *chunk = nmo_chunk_create(arena); ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_CAMERA;
        nmo_serialize_context_t context = nmo_serialize_context_create_nonfile(arena,g.f.repo,CK_STATESAVE_CAMERAONLY);
        ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK,nmo_camera_vtable.serialize(&saved,chunk,NULL,&context));
        nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,id,chunk)); eng_add_to_level_scene(g.f.engine,id);
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,id,1.25f)); eng_set_camera_planes(g.f.engine,id,3,60);
        eng_restore_ic(g.f.engine,id); frame(&g,id,angles[i],2,50);
        ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&g.f,id))->fov,0);
    }
    fixture_end(&g.f);
}

TEST(engine_fov, legacy_partial_camera_initial_values_restore_only_present_members)
{
    fov_fixture_t g; begin(&g); ready(&g); eng_add_to_level_scene(g.f.engine,g.camera);
    for (int kind = 0; kind < 3; kind++) {
        nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(g.f.session)); ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_CAMERA;
        ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk)); nmo_chunk_set_data_version(chunk,4);
        ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(chunk,kind == 0 ? CK_STATESAVE_CAMERAFOV : kind == 1 ? CK_STATESAVE_CAMERAPLANES : CK_STATESAVE_OBJECTHIDDEN));
        if (kind < 2) ASSERT_EQ(NMO_OK,nmo_chunk_write_float(chunk,kind == 0 ? -.75f : 2));
        if (kind == 1) ASSERT_EQ(NMO_OK,nmo_chunk_write_float(chunk,50));
        nmo_chunk_close(chunk); ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.camera,chunk));
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,1.25f)); eng_set_camera_planes(g.f.engine,g.camera,3,60);
        eng_restore_ic(g.f.engine,g.camera);
        frame(&g,g.camera,kind == 0 ? -.75f : 1.25f,kind == 1 ? 2 : 3,kind == 1 ? 50 : 60);
    }
    fixture_end(&g.f);
}

static nmo_object_id_t saved_input(fov_fixture_t *g, nmo_object_id_t pin, const char *text)
{
    nmo_object_id_t value = 0;
    if (nmo_script_edit_ensure_input_parameter_source(g->f.tx,pin,&value) != NMO_OK ||
        nmo_script_edit_set_parameter_value(g->f.tx,value,text) != NMO_OK) return 0;
    return value;
}

static const char *resolve_file(void *data, const char *name)
{
    (void)name; return data;
}

static void approximate_frame(fov_fixture_t *g, nmo_object_id_t id, float expected, float front, float back)
{
    float actual = 0; ASSERT_TRUE(eng_camera_fov(g->f.engine,id,&actual));
    ASSERT_FLOAT_EQ(expected,actual,1e-7f); frame(g,id,actual,front,back);
}

static void loaded_and_copied_lens_graphs(bool zoom)
{
    fov_fixture_t source;
    if (zoom) zoom_begin(&source); else behavior_begin(&source);
    char target[32];
    const float initial_fov = zoom ? .927295218f : PI_F/3;
    snprintf(target,sizeof(target),"%u",source.camera);
    ASSERT_EQ(source.target_value,saved_input(&source,fixture_target(&source.f,source.block),target));
    ASSERT_EQ(source.angle_value,saved_input(&source,BLOCK_PIN(&source.f,source.block,0),zoom ? "36" : "0:60"));
    fixture_commit_bare(&source.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id);
    owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id);
    script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *path = zoom ? NMO_TEST_SCRATCH_FILE("engine_zoom.cmo") : NMO_TEST_SCRATCH_FILE("engine_fov.cmo");
    ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        fov_fixture_t host; memset(&host,0,sizeof(host));
        fixture_begin_document(&host.f); fixture_begin_edit(&host.f);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&host.f,&options);
        if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.f.engine,resolve_file,(void *)path);
        int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.f.engine,zoom ? "zoom.cmo" : "fov.cmo",true,&file));
        if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t block = nmo_engine_object_named(host.f.engine,zoom ? "Set Zoom" : "Set FOV",NMO_CID_BEHAVIOR);
        ASSERT_TRUE(block != 0);
        const eng_module_t *module = eng_module_of(host.f.engine,block,NULL);
        const nmo_object_id_t local[] = {source.f.owner_id,source.camera,source.f.script_id,source.block,source.after,source.target_value,source.angle_value};
        nmo_object_id_t ids[7], copies[7];
        for (size_t i = 0; i < 7; i++) { ids[i] = eng_module_id(module,local[i]); ASSERT_TRUE(ids[i] != 0); }
        ASSERT_TRUE(ids[1] != source.camera);
        frame(&host,ids[1],PI_F/2,1,100);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
        float angle; ASSERT_TRUE(eng_camera_fov(host.f.engine,ids[1],&angle)); ASSERT_FLOAT_EQ(initial_fov,angle,1e-7f);
        frame(&host,ids[1],angle,1,100); ASSERT_EQ(1u,runs(&host.f,ids[4]));
        eng_set_camera_planes(host.f.engine,ids[1],2,50);
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.f.engine,ids,7,false,copies));
        frame(&host,copies[1],angle,2,50);
        /* The target value must name the copied camera after global-ID remapping. */
        nmo_object_id_t copied_target = 0; read_value(&host.f,copies[5],&copied_target,sizeof(copied_target)); ASSERT_EQ(copies[1],copied_target);
        angle = zoom ? 72 : .625f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.f.engine,copies[6],&angle,sizeof(angle)));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[2],true,true));
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0)); approximate_frame(&host,copies[1],zoom ? .489957326f : .625f,2,50);
        float original; ASSERT_TRUE(eng_camera_fov(host.f.engine,ids[1],&original)); ASSERT_FLOAT_EQ(initial_fov,original,1e-7f);
        ASSERT_EQ(1u,runs(&host.f,ids[4])); ASSERT_EQ(1u,runs(&host.f,copies[4]));
        nmo_engine_destroy_object(host.f.engine,ids[1]); nmo_engine_destroy_object(host.f.engine,ids[2]);
        angle = zoom ? 18 : 1.25f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.f.engine,copies[6],&angle,sizeof(angle)));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[2],true,true));
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0)); approximate_frame(&host,copies[1],zoom ? PI_F/2 : 1.25f,2,50);
        ASSERT_EQ(2u,runs(&host.f,copies[4]));
        const nmo_camera_state_t *document = nmo_type_query_object_get_ancestor_state_by_guid(host.f.engine->types,eng_find_object(host.f.engine,copies[1]),CKPGUID_CAMERA);
        ASSERT_NOT_NULL(document); ASSERT_FLOAT_EQ(PI_F/2,document->fov,0);
        fixture_end(&host.f);
    }
    ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&source.f,source.camera))->fov,0);
    fixture_end(&source.f);
}

TEST(engine_fov, loaded_and_copied_scripts_remap_cameras_and_keep_runtime_lenses_independent)
{
    loaded_and_copied_lens_graphs(false);
}

TEST(engine_fov, loaded_and_copied_zoom_scripts_remap_cameras_and_focal_length_parameters)
{
    loaded_and_copied_lens_graphs(true);
}

TEST(engine_fov, terratools_sky_distortion_tracks_runtime_camera_fov_with_its_own_planes)
{
    fov_fixture_t g; behavior_begin(&g);
    nmo_object_id_t sky = fixture_block(&g.f,NMO_GUID(0x36691920,0x3b261630),"TT Sky");
    ASSERT_TRUE(sky != 0);
    float distortion = .25f;
    fixture_input_value(&g.f,BLOCK_PIN(&g.f,sky,0),&distortion,sizeof(distortion));
    fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,0),BLOCK_IN(&g.f,sky,0),0);
    ready(&g); eng_set_camera_planes(g.f.engine,g.camera,2,50);
    const float angles[] = {.75f,1.25f};
    for (size_t i = 0; i < sizeof(angles)/sizeof(*angles); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&angles[i],sizeof(angles[i])));
        trigger(&g); frame(&g,g.camera,angles[i],2,50);
        nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view));
        ASSERT_EQ(1,view.draw_count);
        ASSERT_FLOAT_EQ(angles[i]+.25f*(PI_F-angles[i]),view.draws[0].fov,1e-7f);
        ASSERT_FLOAT_EQ(1,view.draws[0].near_plane,0);
        ASSERT_FLOAT_EQ(200,view.draws[0].far_plane,0);
    }
    fixture_end(&g.f);
}

TEST(engine_fov, set_zoom_uses_the_native_fifty_millimeter_default_and_runs_once)
{
    fov_fixture_t g; zoom_begin(&g); ready(&g);
    ASSERT_TRUE(nmo_engine_has_block(g.f.engine,GUID_SET_ZOOM));
    eng_set_camera_planes(g.f.engine,g.camera,2,50);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float fov = 0; ASSERT_TRUE(eng_camera_fov(g.f.engine,g.camera,&fov));
    ASSERT_FLOAT_EQ(.69111116f,fov,1e-7f); frame(&g,g.camera,fov,2,50);
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10));
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->fov,0);
    fixture_end(&g.f);
}

TEST(engine_fov, set_zoom_rejects_nonpositive_focal_lengths_without_consuming_input_or_changing_the_camera)
{
    const float values[] = {0,-0.0f,-1,-INFINITY};
    for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
        fov_fixture_t g; zoom_begin(&g); ready(&g);
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.625f));
        eng_set_camera_planes(g.f.engine,g.camera,2,50);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&values[i],sizeof(values[i])));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        ASSERT_EQ(0u,runs(&g.f,g.after)); frame(&g,g.camera,.625f,2,50);
        fixture_end(&g.f);
    }
}

TEST(engine_fov, set_zoom_reports_invalid_targets_before_validating_focal_length_or_consuming_input)
{
    for (int kind = 0; kind < 5; kind++) {
        fov_fixture_t g; zoom_begin(&g); ready(&g);
        nmo_object_id_t target = kind == 0 ? 0 : kind == 1 ? 0x7fffffff : kind == 2 ? g.f.owner_id : kind == 3 ? g.f.script_id : g.camera;
        if (kind == 4) nmo_engine_destroy_object(g.f.engine,g.camera);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target)));
        float invalid = -1; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&invalid,sizeof(invalid)));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure));
        ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        ASSERT_EQ(0u,runs(&g.f,g.after)); frame(&g,g.second,PI_F/2,1,100);
        fixture_end(&g.f);
    }
}

TEST(engine_fov, set_zoom_uses_focal_lengths_in_millimeters_and_observes_live_target_changes)
{
    const float lenses[] = {18,36,50,100,250};
    const float angles[] = {1.570796327f,.927295218f,.691111161f,.356185876f,.143751939f};
    fov_fixture_t g; zoom_begin(&g); ready(&g);
    for (size_t i = 0; i < sizeof(lenses)/sizeof(*lenses); i++) {
        nmo_object_id_t target = i & 1 ? g.second : g.camera;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target)));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&lenses[i],sizeof(lenses[i])));
        trigger(&g);
        float fov = 0; ASSERT_TRUE(eng_camera_fov(g.f.engine,target,&fov));
        ASSERT_FLOAT_EQ(angles[i],fov,1e-7f); frame(&g,target,fov,1,100);
        ASSERT_EQ(i+1,runs(&g.f,g.after));
        if (i > 0) {
            nmo_object_id_t other = target == g.camera ? g.second : g.camera;
            ASSERT_TRUE(eng_camera_fov(g.f.engine,other,&fov)); ASSERT_FLOAT_EQ(angles[i-1],fov,1e-7f);
        }
    }
    fixture_end(&g.f);
}

TEST(engine_fov, set_zoom_changes_view_projection_and_billboard_screen_coordinates)
{
    fov_fixture_t g; zoom_begin(&g); ready(&g);
    float lens = 36; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&lens,sizeof(lens)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    eng_matrix_t projection; ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&projection,NULL));
    nmo_rect_t rect;
    ASSERT_TRUE(eng_project_box(&projection,&(eng_vector_t){-1,-1,10},&(eng_vector_t){1,1,10},&rect));
    ASSERT_FLOAT_EQ(.4f,rect.left,1e-6f); ASSERT_FLOAT_EQ(.4f,rect.top,1e-6f);
    ASSERT_FLOAT_EQ(.6f,rect.right,1e-6f); ASSERT_FLOAT_EQ(.6f,rect.bottom,1e-6f);
    eng_vector_t screen; eng_render_project(g.f.engine,&(eng_vector_t){1,0,10},&screen);
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view));
    ASSERT_FLOAT_EQ(.6f*view.width,screen.x,2e-4f); ASSERT_FLOAT_EQ(.5f*view.height,screen.y,2e-4f);
    ASSERT_FLOAT_EQ(.927295218f,view.fov,1e-7f);
    fixture_end(&g.f);
}

TEST(engine_fov, set_zoom_keeps_native_nan_infinite_and_extreme_positive_focal_length_semantics)
{
    /* A tiny lens overflows the float division to infinity; +infinity yields +zero FOV. */
    const float lenses[] = {FLT_TRUE_MIN,FLT_MIN,FLT_MAX,INFINITY,NAN,-NAN};
    const float angles[] = {PI_F,PI_F,0x1.200002p-123f,0,NAN,NAN};
    fov_fixture_t g; zoom_begin(&g); ready(&g);
    for (size_t i = 0; i < sizeof(lenses)/sizeof(*lenses); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&lenses[i],sizeof(lenses[i])));
        trigger(&g);
        float fov = -1; ASSERT_TRUE(eng_camera_fov(g.f.engine,g.camera,&fov));
        if (i < 2) {
            /* libm implementations can round atan(infinity) to adjacent floats. */
            ASSERT_FLOAT_EQ(PI_F,fov,2.5e-7f); frame(&g,g.camera,fov,1,100);
        } else frame(&g,g.camera,angles[i],1,100);
        if (i == 3) ASSERT_FALSE(signbit(fov));
        ASSERT_EQ(i+1,runs(&g.f,g.after));
    }
    fixture_end(&g.f);
}

TEST(engine_fov, disconnected_focal_length_uses_fifty_millimeters)
{
    fov_fixture_t g; zoom_begin(&g);
    nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)),0);
    ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    float fov = 0; ASSERT_TRUE(eng_camera_fov(g.f.engine,g.camera,&fov)); ASSERT_FLOAT_EQ(.691111161f,fov,1e-7f);
    ASSERT_EQ(1u,runs(&g.f,g.after)); fixture_end(&g.f);
}

TEST(engine_fov, set_zoom_updates_orthographic_camera_fov_without_changing_zoom_or_projection)
{
    fov_fixture_t g; zoom_begin(&g);
    nmo_camera_state_t *camera = fixture_state(&g.f,g.camera);
    camera->projection_type = CK_ORTHOGRAPHICPROJECTION; camera->orthographic_zoom = 2;
    ready(&g);
    eng_matrix_t before, after; ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&before,NULL));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&after,NULL));
    ASSERT_EQ(0,memcmp(&before,&after,sizeof(before)));
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view));
    ASSERT_TRUE(view.orthographic); ASSERT_FLOAT_EQ(2,view.zoom,0); ASSERT_FLOAT_EQ(.691111161f,view.fov,1e-7f);
    fixture_end(&g.f);
}

TEST(engine_fov, zoom_camera_copies_and_initial_conditions_capture_converted_fov_independently)
{
    for (int derived = 0; derived < 2; derived++) {
        fov_fixture_t g; zoom_begin(&g); ready(&g);
        nmo_object_id_t original = derived ? g.second : g.camera;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&original,sizeof(original)));
        float lens = 36; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&lens,sizeof(lens)));
        eng_set_camera_planes(g.f.engine,original,2,50);
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); approximate_frame(&g,original,.927295218f,2,50);
        eng_add_to_level_scene(g.f.engine,original); eng_save_ic(g.f.engine,original);
        nmo_object_id_t copy = 0; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&original,1,false,&copy));
        approximate_frame(&g,copy,.927295218f,2,50);
        lens = 72; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&lens,sizeof(lens)));
        trigger(&g); approximate_frame(&g,original,.489957326f,2,50); approximate_frame(&g,copy,.927295218f,2,50);
        eng_restore_ic(g.f.engine,original); approximate_frame(&g,original,.927295218f,2,50);
        eng_add_to_level_scene(g.f.engine,copy); eng_save_ic(g.f.engine,copy);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&copy,sizeof(copy)));
        lens = 18; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&lens,sizeof(lens)));
        trigger(&g); approximate_frame(&g,copy,PI_F/2,2,50); approximate_frame(&g,original,.927295218f,2,50);
        eng_restore_ic(g.f.engine,copy); approximate_frame(&g,copy,.927295218f,2,50);
        nmo_engine_destroy_object(g.f.engine,original);
        lens = 50; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&lens,sizeof(lens)));
        trigger(&g); approximate_frame(&g,copy,.691111161f,2,50);
        const nmo_camera_state_t *document = nmo_type_query_object_get_ancestor_state_by_guid(g.f.engine->types,eng_find_object(g.f.engine,copy),CKPGUID_CAMERA);
        ASSERT_NOT_NULL(document); ASSERT_FLOAT_EQ(PI_F/2,document->fov,0);
        fixture_end(&g.f);
    }
}

static void orthographic_cameras(fov_fixture_t *g)
{
    ((nmo_camera_state_t *)fixture_state(&g->f,g->camera))->projection_type = CK_ORTHOGRAPHICPROJECTION;
    ((nmo_camera_state_t *)fixture_state(&g->f,g->second))->projection_type = CK_ORTHOGRAPHICPROJECTION;
}

static void orthographic_frame(fov_fixture_t *g, nmo_object_id_t id, float zoom)
{
    g->f.engine->render.camera = id;
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g->f.engine,&view));
    ASSERT_EQ(id,view.camera); ASSERT_TRUE(view.orthographic);
    ASSERT_TRUE((isnan(zoom) && isnan(view.zoom)) || view.zoom == zoom);
}

TEST(engine_fov, runtime_orthographic_zoom_updates_render_projection_and_screen_coordinates)
{
    fov_fixture_t g; begin(&g); orthographic_cameras(&g); ready(&g);
    ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,2));
    orthographic_frame(&g,g.camera,2);
    eng_matrix_t projection; ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&projection,NULL));
    nmo_rect_t rect;
    ASSERT_TRUE(eng_project_box(&projection,&(eng_vector_t){-.25f,-.25f,10},&(eng_vector_t){.25f,.25f,10},&rect));
    ASSERT_FLOAT_EQ(.25f,rect.left,1e-6f); ASSERT_FLOAT_EQ(.25f,rect.top,1e-6f);
    ASSERT_FLOAT_EQ(.75f,rect.right,1e-6f); ASSERT_FLOAT_EQ(.75f,rect.bottom,1e-6f);
    eng_vector_t screen; eng_render_project(g.f.engine,&(eng_vector_t){.25f,0,10},&screen);
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view));
    ASSERT_FLOAT_EQ(.75f*view.width,screen.x,2e-4f); ASSERT_FLOAT_EQ(.5f*view.height,screen.y,2e-4f);
    ASSERT_FLOAT_EQ(1,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->orthographic_zoom,0);
    nmo_engine_t *other = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&other));
    ASSERT_EQ(NMO_OK,nmo_engine_start(other,NMO_ENGINE_START_ALL)); other->render.camera = g.camera;
    ASSERT_EQ(NMO_OK,nmo_engine_render_frame(other,&view)); ASSERT_FLOAT_EQ(1,view.zoom,0);
    nmo_engine_destroy(other); fixture_end(&g.f);
}

TEST(engine_fov, orthographic_zoom_fov_and_clipping_planes_preserve_each_other_in_both_write_orders)
{
    fov_fixture_t g; begin(&g); orthographic_cameras(&g); ready(&g);
    ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,2));
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.75f)); eng_set_camera_planes(g.f.engine,g.camera,2,50);
    orthographic_frame(&g,g.camera,2); frame(&g,g.camera,.75f,2,50);
    ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,3)); frame(&g,g.camera,.75f,2,50);
    eng_set_camera_planes(g.f.engine,g.second,3,60); ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.second,.625f));
    ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.second,4));
    frame(&g,g.second,.625f,3,60); orthographic_frame(&g,g.second,4); orthographic_frame(&g,g.camera,3);
    nmo_engine_destroy_object(g.f.engine,g.second); float ignored;
    ASSERT_FALSE(eng_camera_orthographic_zoom(g.f.engine,g.second,&ignored));
    ASSERT_FALSE(eng_set_camera_orthographic_zoom(g.f.engine,g.second,5));
    fixture_end(&g.f);
}

TEST(engine_fov, camera_copies_keep_runtime_orthographic_zoom_independent_across_cache_growth_and_deletion)
{
    fov_fixture_t g; begin(&g); orthographic_cameras(&g); ready(&g);
    const nmo_object_id_t cameras[] = {g.camera,g.second};
    for (size_t type = 0; type < 2; type++) {
        ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,cameras[type],2));
        nmo_object_id_t copies[40];
        for (size_t i = 0; i < sizeof(copies)/sizeof(*copies); i++) {
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&cameras[type],1,false,&copies[i]));
            orthographic_frame(&g,copies[i],2);
        }
        ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,cameras[type],3));
        ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,copies[0],4));
        orthographic_frame(&g,cameras[type],3); orthographic_frame(&g,copies[39],2);
        nmo_object_id_t next = 0; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&copies[0],1,false,&next));
        nmo_engine_destroy_object(g.f.engine,cameras[type]); nmo_engine_destroy_object(g.f.engine,copies[0]);
        orthographic_frame(&g,next,4); orthographic_frame(&g,copies[39],2);
    }
    fixture_end(&g.f);
}

static void orthographic_begin(fov_fixture_t *g, uint32_t version)
{
    behavior_begin_guid(g,GUID_ORTHOGRAPHIC_ZOOM,"Orthographic Zoom");
    orthographic_cameras(g);
    ((nmo_behavior_state_t *)fixture_state(&g->f,g->block))->block_version = version;
}

TEST(engine_fov, orthographic_zoom_uses_the_native_unit_default_and_runs_once)
{
    fov_fixture_t g; orthographic_begin(&g,0x20000); ready(&g);
    ASSERT_TRUE(nmo_engine_has_block(g.f.engine,GUID_ORTHOGRAPHIC_ZOOM));
    ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,3));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); orthographic_frame(&g,g.camera,1);
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(1u,runs(&g.f,g.block));
    fixture_end(&g.f);
}

TEST(engine_fov, orthographic_zoom_applies_percent_scaling_only_before_version_two)
{
    const uint32_t versions[] = {0x10000,0x1ffff,0x20000,0x20001};
    for (size_t i = 0; i < sizeof(versions)/sizeof(*versions); i++) {
        fov_fixture_t g; orthographic_begin(&g,versions[i]); ready(&g);
        float value = 250; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&value,sizeof(value)));
        ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); orthographic_frame(&g,g.camera,i < 2 ? 2.5f : 250);
        fixture_end(&g.f);
    }
}

TEST(engine_fov, orthographic_zoom_preserves_zero_negative_extreme_and_nonfinite_values)
{
    const float values[] = {0,-0.0f,-2,FLT_TRUE_MIN,FLT_MAX,NAN,INFINITY,-INFINITY};
    fov_fixture_t g; orthographic_begin(&g,0x20000); ready(&g);
    for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&values[i],sizeof(values[i])));
        trigger(&g); orthographic_frame(&g,g.camera,values[i]);
        if (i == 1) { float zoom = 1; ASSERT_TRUE(eng_camera_orthographic_zoom(g.f.engine,g.camera,&zoom)); ASSERT_TRUE(signbit(zoom)); }
        ASSERT_EQ(i+1,runs(&g.f,g.after));
    }
    fixture_end(&g.f);
}

TEST(engine_fov, legacy_orthographic_zoom_keeps_signed_zero_underflow_and_nonfinite_scaling)
{
    const float values[] = {250,-125,-0.0f,FLT_TRUE_MIN,NAN,INFINITY,-INFINITY};
    const float expected[] = {2.5f,-1.25f,-0.0f,0,NAN,INFINITY,-INFINITY};
    fov_fixture_t g; orthographic_begin(&g,0x10000); ready(&g);
    for (size_t i = 0; i < sizeof(values)/sizeof(*values); i++) {
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&values[i],sizeof(values[i])));
        trigger(&g); orthographic_frame(&g,g.camera,expected[i]);
        if (i == 2) { float zoom = 1; ASSERT_TRUE(eng_camera_orthographic_zoom(g.f.engine,g.camera,&zoom)); ASSERT_TRUE(signbit(zoom)); }
    }
    fixture_end(&g.f);
}

TEST(engine_fov, orthographic_zoom_observes_live_inputs_and_target_cameras_without_changing_fov_or_planes)
{
    fov_fixture_t g; orthographic_begin(&g,0x20000); ready(&g);
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.625f)); eng_set_camera_planes(g.f.engine,g.camera,2,50);
    float zoom = 2; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&zoom,sizeof(zoom)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); orthographic_frame(&g,g.camera,2);
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&g.second,sizeof(g.second)));
    zoom = 3; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&zoom,sizeof(zoom)));
    trigger(&g); orthographic_frame(&g,g.second,3); orthographic_frame(&g,g.camera,2); frame(&g,g.camera,.625f,2,50);
    ASSERT_FLOAT_EQ(1,((nmo_camera_state_t *)fixture_state(&g.f,g.second))->orthographic_zoom,0);
    fixture_end(&g.f);
}

TEST(engine_fov, disconnected_orthographic_zoom_keeps_the_native_default_for_each_version)
{
    for (int legacy = 0; legacy < 2; legacy++) {
        fov_fixture_t g; orthographic_begin(&g,legacy ? 0x10000 : 0x20000);
        nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)),0);
        ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); orthographic_frame(&g,g.camera,legacy ? .01f : 1);
        fixture_end(&g.f);
    }
}

TEST(engine_fov, orthographic_zoom_consumes_input_and_activates_output_before_invalid_target_errors)
{
    for (int kind = 0; kind < 5; kind++) {
        fov_fixture_t g; orthographic_begin(&g,0x20000); ready(&g);
        nmo_object_id_t target = kind == 0 ? 0 : kind == 1 ? 0x7fffffff : kind == 2 ? g.f.owner_id : kind == 3 ? g.f.script_id : g.camera;
        if (kind == 4) nmo_engine_destroy_object(g.f.engine,g.camera);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target)));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        ASSERT_EQ(0u,runs(&g.f,g.after)); orthographic_frame(&g,g.second,1);
        fixture_end(&g.f);
    }
}

TEST(engine_fov, perspective_cameras_store_orthographic_zoom_without_changing_perspective_projection)
{
    fov_fixture_t g; behavior_begin_guid(&g,GUID_ORTHOGRAPHIC_ZOOM,"Orthographic Zoom"); ready(&g);
    eng_matrix_t before, after; ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&before,NULL));
    float zoom = 3; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&zoom,sizeof(zoom)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&after,NULL));
    ASSERT_EQ(0,memcmp(&before,&after,sizeof(before)));
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view));
    ASSERT_FALSE(view.orthographic); ASSERT_FLOAT_EQ(3,view.zoom,0); ASSERT_FLOAT_EQ(PI_F/2,view.fov,0);
    fixture_end(&g.f);
}

TEST(engine_fov, orthographic_zoom_initial_conditions_capture_runtime_values_and_replace_previous_saves)
{
    fov_fixture_t g; orthographic_begin(&g,0x20000); ready(&g);
    eng_add_to_level_scene(g.f.engine,g.camera); eng_save_ic(g.f.engine,g.camera);
    float zoom = 2; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&zoom,sizeof(zoom)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); orthographic_frame(&g,g.camera,2);
    eng_restore_ic(g.f.engine,g.camera); orthographic_frame(&g,g.camera,1);
    trigger(&g); eng_save_ic(g.f.engine,g.camera);
    zoom = 3; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&zoom,sizeof(zoom)));
    trigger(&g); eng_restore_ic(g.f.engine,g.camera); orthographic_frame(&g,g.camera,2);
    ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,NAN)); eng_save_ic(g.f.engine,g.camera);
    trigger(&g); eng_restore_ic(g.f.engine,g.camera); orthographic_frame(&g,g.camera,NAN);
    ASSERT_FLOAT_EQ(1,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->orthographic_zoom,0);
    fixture_end(&g.f);
}

TEST(engine_fov, modern_camera_initial_values_restore_raw_orthographic_zoom_for_both_camera_classes)
{
    const float zooms[] = {2.5f,-2,INFINITY,NAN};
    fov_fixture_t g; begin(&g); orthographic_cameras(&g); ready(&g);
    const nmo_object_id_t cameras[] = {g.camera,g.second};
    for (size_t type = 0; type < 2; type++) for (size_t i = 0; i < sizeof(zooms)/sizeof(*zooms); i++) {
        nmo_object_id_t id = cameras[type];
        nmo_camera_state_t saved = *(nmo_camera_state_t *)fixture_state(&g.f,id);
        saved.orthographic_zoom = zooms[i]; saved.fov = .625f; saved.near_plane = 2; saved.far_plane = 50;
        nmo_arena_t *arena = nmo_session_get_arena(g.f.session);
        nmo_chunk_t *chunk = nmo_chunk_create(arena); ASSERT_NOT_NULL(chunk); chunk->class_id = NMO_CID_CAMERA;
        nmo_serialize_context_t context = nmo_serialize_context_create_nonfile(arena,g.f.repo,CK_STATESAVE_CAMERAONLY);
        ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk)); ASSERT_EQ(NMO_OK,nmo_camera_vtable.serialize(&saved,chunk,NULL,&context)); nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,id,chunk)); eng_add_to_level_scene(g.f.engine,id);
        ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,id,4)); ASSERT_TRUE(eng_set_camera_fov(g.f.engine,id,.75f)); eng_set_camera_planes(g.f.engine,id,3,60);
        eng_restore_ic(g.f.engine,id); orthographic_frame(&g,id,zooms[i]); frame(&g,id,.625f,2,50);
        ASSERT_FLOAT_EQ(1,((nmo_camera_state_t *)fixture_state(&g.f,id))->orthographic_zoom,0);
    }
    fixture_end(&g.f);
}

TEST(engine_fov, legacy_camera_initial_values_restore_orthographic_zoom_only_when_present)
{
    fov_fixture_t g; begin(&g); orthographic_cameras(&g); ready(&g); eng_add_to_level_scene(g.f.engine,g.camera);
    const uint32_t identifiers[] = {CK_STATESAVE_CAMERAOTHOZOOM,CK_STATESAVE_CAMERAFOV,CK_STATESAVE_CAMERAPLANES,CK_STATESAVE_OBJECTHIDDEN};
    for (size_t i = 0; i < sizeof(identifiers)/sizeof(*identifiers); i++) {
        nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(g.f.session)); ASSERT_NOT_NULL(chunk); chunk->class_id = NMO_CID_CAMERA;
        ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk)); nmo_chunk_set_data_version(chunk,4);
        ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(chunk,identifiers[i]));
        if (i < 3) ASSERT_EQ(NMO_OK,nmo_chunk_write_float(chunk,i == 0 ? 2.5f : i == 1 ? .625f : 2));
        if (i == 2) ASSERT_EQ(NMO_OK,nmo_chunk_write_float(chunk,50));
        nmo_chunk_close(chunk); ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.camera,chunk));
        ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,4)); ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.75f)); eng_set_camera_planes(g.f.engine,g.camera,3,60);
        eng_restore_ic(g.f.engine,g.camera); orthographic_frame(&g,g.camera,i == 0 ? 2.5f : 4);
        frame(&g,g.camera,i == 1 ? .625f : .75f,i == 2 ? 2 : 3,i == 2 ? 50 : 60);
    }
    fixture_end(&g.f);
}

TEST(engine_fov, malformed_orthographic_zoom_initial_values_keep_the_previous_saved_state)
{
    fov_fixture_t g; begin(&g); orthographic_cameras(&g); ready(&g); eng_add_to_level_scene(g.f.engine,g.camera);
    ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,2)); eng_save_ic(g.f.engine,g.camera);
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(g.f.session)); ASSERT_NOT_NULL(chunk); chunk->class_id = NMO_CID_CAMERA;
    ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk)); nmo_chunk_set_data_version(chunk,4);
    ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(chunk,CK_STATESAVE_CAMERAOTHOZOOM)); nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,eng_load_ic(g.f.engine,g.camera,chunk));
    ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,3)); eng_restore_ic(g.f.engine,g.camera); orthographic_frame(&g,g.camera,2);
    fixture_end(&g.f);
}

TEST(engine_fov, nonstrict_execution_follows_orthographic_zoom_output_on_owner_errors)
{
    fov_fixture_t g; orthographic_begin(&g,0x20000);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = 0;
    fixture_commit(&g.f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL));
    nmo_object_id_t missing = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&missing,sizeof(missing)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0)));
    orthographic_frame(&g,g.camera,1); fixture_end(&g.f);
}

TEST(engine_fov, loaded_and_copied_orthographic_zoom_graphs_keep_versions_targets_and_values_independent)
{
    for (int legacy = 0; legacy < 2; legacy++) for (int derived = 0; derived < 2; derived++) {
        fov_fixture_t source; orthographic_begin(&source,legacy ? 0x10000 : 0x20000);
        nmo_object_id_t camera = derived ? source.second : source.camera;
        char target[32]; snprintf(target,sizeof(target),"%u",camera);
        ASSERT_EQ(source.target_value,saved_input(&source,fixture_target(&source.f,source.block),target));
        ASSERT_EQ(source.angle_value,saved_input(&source,BLOCK_PIN(&source.f,source.block,0),legacy ? "250" : "2.5"));
        fixture_commit_bare(&source.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id);
        owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_orthographic_zoom.cmo");
        ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            fov_fixture_t host; memset(&host,0,sizeof(host)); fixture_begin_document(&host.f); fixture_begin_edit(&host.f);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
            fixture_commit(&host.f,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.f.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.f.engine,"orthographic_zoom.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.f.engine,"Orthographic Zoom",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.f.engine,block,NULL);
            const nmo_object_id_t local[] = {source.f.owner_id,camera,source.f.script_id,source.block,source.after,source.target_value,source.angle_value};
            nmo_object_id_t ids[7], copies[7];
            for (size_t i = 0; i < 7; i++) { ids[i] = eng_module_id(module,local[i]); ASSERT_TRUE(ids[i] != 0); }
            ASSERT_TRUE(ids[1] != camera);
            nmo_engine_block_t runtime = {host.f.engine,eng_map_get(&host.f.engine->node_map,ids[3])};
            ASSERT_EQ(legacy ? 0x10000u : 0x20000u,nmo_engine_block_version(&runtime));
            orthographic_frame(&host,ids[1],1);
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0)); orthographic_frame(&host,ids[1],2.5f);
            eng_matrix_t projection; ASSERT_TRUE(eng_camera_view_projection(host.f.engine,ids[1],&projection,NULL));
            nmo_rect_t rect; ASSERT_TRUE(eng_project_box(&projection,&(eng_vector_t){-.25f,-.25f,10},&(eng_vector_t){.25f,.25f,10},&rect));
            ASSERT_FLOAT_EQ(.1875f,rect.left,1e-6f); ASSERT_FLOAT_EQ(.8125f,rect.right,1e-6f);
            eng_set_camera_planes(host.f.engine,ids[1],2,50);
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.f.engine,ids,7,false,copies)); orthographic_frame(&host,copies[1],2.5f);
            runtime = (nmo_engine_block_t){host.f.engine,eng_map_get(&host.f.engine->node_map,copies[3])};
            ASSERT_EQ(legacy ? 0x10000u : 0x20000u,nmo_engine_block_version(&runtime));
            nmo_object_id_t copied_target = 0; read_value(&host.f,copies[5],&copied_target,sizeof(copied_target)); ASSERT_EQ(copies[1],copied_target);
            float value = legacy ? 300 : 3; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.f.engine,copies[6],&value,sizeof(value)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[2],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
            orthographic_frame(&host,copies[1],3); orthographic_frame(&host,ids[1],2.5f);
            ASSERT_EQ(1u,runs(&host.f,ids[4])); ASSERT_EQ(1u,runs(&host.f,copies[4]));
            nmo_engine_destroy_object(host.f.engine,ids[1]); nmo_engine_destroy_object(host.f.engine,ids[2]);
            value = legacy ? 400 : 4; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.f.engine,copies[6],&value,sizeof(value)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[2],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
            orthographic_frame(&host,copies[1],4); frame(&host,copies[1],PI_F/2,2,50); ASSERT_EQ(2u,runs(&host.f,copies[4]));
            const nmo_camera_state_t *document = nmo_type_query_object_get_ancestor_state_by_guid(host.f.engine->types,eng_find_object(host.f.engine,copies[1]),CKPGUID_CAMERA);
            ASSERT_NOT_NULL(document); ASSERT_FLOAT_EQ(1,document->orthographic_zoom,0);
            fixture_end(&host.f);
        }
        ASSERT_FLOAT_EQ(1,((nmo_camera_state_t *)fixture_state(&source.f,camera))->orthographic_zoom,0);
        ASSERT_EQ(legacy ? 0x10000u : 0x20000u,((nmo_behavior_state_t *)fixture_state(&source.f,source.block))->block_version);
        fixture_end(&source.f); remove(path);
    }
}

static void projection_frame(fov_fixture_t *g, nmo_object_id_t id, uint32_t expected)
{
    uint32_t actual = 0; ASSERT_TRUE(eng_camera_projection_type(g->f.engine,id,&actual)); ASSERT_EQ(expected,actual);
    g->f.engine->render.camera = id;
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g->f.engine,&view));
    ASSERT_EQ(id,view.camera); ASSERT_EQ((expected & 1u) == 0,view.orthographic);
}

TEST(engine_fov, runtime_projection_switches_update_render_box_projection_and_screen_coordinates)
{
    fov_fixture_t g; begin(&g); ready(&g); ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,2));
    const uint32_t modes[] = {CK_ORTHOGRAPHICPROJECTION,CK_PERSPECTIVEPROJECTION,0};
    for (size_t i = 0; i < sizeof(modes)/sizeof(*modes); i++) {
        ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.camera,modes[i])); projection_frame(&g,g.camera,modes[i]);
        eng_matrix_t projection; ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&projection,NULL));
        nmo_rect_t rect; ASSERT_TRUE(eng_project_box(&projection,&(eng_vector_t){-.25f,-.25f,10},&(eng_vector_t){.25f,.25f,10},&rect));
        float left = i == 1 ? .4875f : .25f, right = i == 1 ? .5125f : .75f;
        ASSERT_FLOAT_EQ(left,rect.left,1e-6f); ASSERT_FLOAT_EQ(left,rect.top,1e-6f);
        ASSERT_FLOAT_EQ(right,rect.right,1e-6f); ASSERT_FLOAT_EQ(right,rect.bottom,1e-6f);
        eng_vector_t screen; eng_render_project(g.f.engine,&(eng_vector_t){.25f,0,10},&screen);
        nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view)); ASSERT_FLOAT_EQ(right*view.width,screen.x,2e-4f);
    }
    ASSERT_EQ(CK_PERSPECTIVEPROJECTION,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->projection_type);
    nmo_engine_t *other = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,NULL,&other)); ASSERT_EQ(NMO_OK,nmo_engine_start(other,NMO_ENGINE_START_ALL));
    uint32_t mode = 0; ASSERT_TRUE(eng_camera_projection_type(other,g.camera,&mode)); ASSERT_EQ(CK_PERSPECTIVEPROJECTION,mode);
    nmo_engine_destroy(other); fixture_end(&g.f);
}

TEST(engine_fov, projection_fov_zoom_and_planes_preserve_each_other_for_both_camera_classes)
{
    fov_fixture_t g; begin(&g); ready(&g);
    ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.camera,0xfffffffeu));
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.625f)); ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,2)); eng_set_camera_planes(g.f.engine,g.camera,2,50);
    projection_frame(&g,g.camera,0xfffffffeu); orthographic_frame(&g,g.camera,2); frame(&g,g.camera,.625f,2,50);
    ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.second,.75f)); ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.second,3)); eng_set_camera_planes(g.f.engine,g.second,3,60);
    ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.second,0x80000000u));
    projection_frame(&g,g.second,0x80000000u); orthographic_frame(&g,g.second,3); frame(&g,g.second,.75f,3,60);
    ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.second,0x80000001u)); frame(&g,g.second,.75f,3,60);
    nmo_engine_destroy_object(g.f.engine,g.second); uint32_t ignored;
    ASSERT_FALSE(eng_camera_projection_type(g.f.engine,g.second,&ignored)); ASSERT_FALSE(eng_set_camera_projection_type(g.f.engine,g.second,1));
    fixture_end(&g.f);
}

TEST(engine_fov, camera_copies_preserve_raw_projection_modes_across_cache_growth_and_source_deletion)
{
    fov_fixture_t g; begin(&g); ready(&g);
    const nmo_object_id_t cameras[] = {g.camera,g.second};
    for (size_t type = 0; type < 2; type++) {
        ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,cameras[type],0xfffffffeu)); nmo_object_id_t copies[40];
        for (size_t i = 0; i < sizeof(copies)/sizeof(*copies); i++) {
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&cameras[type],1,false,&copies[i])); projection_frame(&g,copies[i],0xfffffffeu);
        }
        ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,cameras[type],1)); ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,copies[0],0xffffffffu));
        projection_frame(&g,cameras[type],1); projection_frame(&g,copies[39],0xfffffffeu);
        nmo_object_id_t next = 0; ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(g.f.engine,&copies[0],1,false,&next));
        nmo_engine_destroy_object(g.f.engine,cameras[type]); nmo_engine_destroy_object(g.f.engine,copies[0]);
        projection_frame(&g,next,0xffffffffu); projection_frame(&g,copies[39],0xfffffffeu);
    }
    fixture_end(&g.f);
}

static void projection_begin(fov_fixture_t *g)
{
    behavior_begin_guid(g,GUID_SET_PROJECTION,"Set Projection");
}

TEST(engine_fov, set_projection_defaults_to_orthographic_and_runs_once)
{
    fov_fixture_t g; projection_begin(&g); ready(&g); ASSERT_TRUE(nmo_engine_has_block(g.f.engine,GUID_SET_PROJECTION));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); projection_frame(&g,g.camera,CK_ORTHOGRAPHICPROJECTION);
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_EQ(CK_PERSPECTIVEPROJECTION,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->projection_type);
    fixture_end(&g.f);
}

TEST(engine_fov, disconnected_projection_input_uses_the_native_perspective_fallback)
{
    fov_fixture_t g; projection_begin(&g);
    nmo_parameterin_set_source_id(fixture_state(&g.f,BLOCK_PIN(&g.f,g.block,0)),0);
    ready(&g); ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.camera,CK_ORTHOGRAPHICPROJECTION));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); projection_frame(&g,g.camera,CK_PERSPECTIVEPROJECTION);
    fixture_end(&g.f);
}

TEST(engine_fov, set_projection_keeps_raw_integer_bits_and_observes_live_target_changes)
{
    const uint32_t modes[] = {0,1,2,3,0xfffffffeu,0xffffffffu,0x80000000u,0x80000001u};
    fov_fixture_t g; projection_begin(&g); ready(&g);
    for (size_t i = 0; i < sizeof(modes)/sizeof(*modes); i++) {
        nmo_object_id_t target = i & 1 ? g.second : g.camera;
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target)));
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&modes[i],sizeof(modes[i])));
        trigger(&g); projection_frame(&g,target,modes[i]); ASSERT_EQ(i+1,runs(&g.f,g.after));
        if (i > 0) projection_frame(&g,target == g.camera ? g.second : g.camera,modes[i-1]);
    }
    fixture_end(&g.f);
}

TEST(engine_fov, set_projection_owner_errors_keep_input_and_outputs_unchanged)
{
    for (int kind = 0; kind < 5; kind++) {
        fov_fixture_t g; projection_begin(&g); ready(&g);
        nmo_object_id_t target = kind == 0 ? 0 : kind == 1 ? 0x7fffffff : kind == 2 ? g.f.owner_id : kind == 3 ? g.f.script_id : g.camera;
        if (kind == 4) nmo_engine_destroy_object(g.f.engine,g.camera);
        ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&target,sizeof(target)));
        ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(g.f.engine,0));
        nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(g.f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_BR_OWNERERROR,failure.result);
        ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
        ASSERT_EQ(0u,runs(&g.f,g.after)); projection_frame(&g,g.second,CK_PERSPECTIVEPROJECTION); fixture_end(&g.f);
    }
}

TEST(engine_fov, projection_initial_conditions_capture_runtime_modes_and_replace_previous_saves)
{
    fov_fixture_t g; projection_begin(&g); ready(&g); eng_add_to_level_scene(g.f.engine,g.camera); eng_save_ic(g.f.engine,g.camera);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); projection_frame(&g,g.camera,CK_ORTHOGRAPHICPROJECTION);
    eng_restore_ic(g.f.engine,g.camera); projection_frame(&g,g.camera,CK_PERSPECTIVEPROJECTION);
    trigger(&g); eng_save_ic(g.f.engine,g.camera);
    uint32_t mode = 0xffffffffu; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.angle_value,&mode,sizeof(mode)));
    trigger(&g); projection_frame(&g,g.camera,mode); eng_restore_ic(g.f.engine,g.camera); projection_frame(&g,g.camera,CK_ORTHOGRAPHICPROJECTION);
    trigger(&g); eng_save_ic(g.f.engine,g.camera); ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.camera,0));
    eng_restore_ic(g.f.engine,g.camera); projection_frame(&g,g.camera,mode);
    ASSERT_EQ(CK_PERSPECTIVEPROJECTION,((nmo_camera_state_t *)fixture_state(&g.f,g.camera))->projection_type);
    fixture_end(&g.f);
}

TEST(engine_fov, modern_camera_initial_values_restore_full_projection_bits_and_other_camera_members)
{
    const uint32_t modes[] = {0,2,0x80000000u,0xffffffffu};
    fov_fixture_t g; begin(&g); ready(&g); const nmo_object_id_t cameras[] = {g.camera,g.second};
    for (size_t type = 0; type < 2; type++) for (size_t i = 0; i < sizeof(modes)/sizeof(*modes); i++) {
        nmo_object_id_t id = cameras[type]; nmo_camera_state_t saved = *(nmo_camera_state_t *)fixture_state(&g.f,id);
        saved.projection_type = modes[i]; saved.orthographic_zoom = 2.5f; saved.fov = .625f; saved.near_plane = 2; saved.far_plane = 50;
        nmo_arena_t *arena = nmo_session_get_arena(g.f.session); nmo_chunk_t *chunk = nmo_chunk_create(arena); ASSERT_NOT_NULL(chunk); chunk->class_id = NMO_CID_CAMERA;
        nmo_serialize_context_t context = nmo_serialize_context_create_nonfile(arena,g.f.repo,CK_STATESAVE_CAMERAONLY);
        ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk)); ASSERT_EQ(NMO_OK,nmo_camera_vtable.serialize(&saved,chunk,NULL,&context)); nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,id,chunk)); eng_add_to_level_scene(g.f.engine,id);
        ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,id,1)); ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,id,3));
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,id,.75f)); eng_set_camera_planes(g.f.engine,id,3,60);
        eng_restore_ic(g.f.engine,id); projection_frame(&g,id,modes[i]); frame(&g,id,.625f,2,50);
        float zoom = 0; ASSERT_TRUE(eng_camera_orthographic_zoom(g.f.engine,id,&zoom)); ASSERT_FLOAT_EQ(2.5f,zoom,0);
        ASSERT_EQ(CK_PERSPECTIVEPROJECTION,((nmo_camera_state_t *)fixture_state(&g.f,id))->projection_type);
    }
    fixture_end(&g.f);
}

TEST(engine_fov, legacy_camera_initial_values_restore_projection_only_when_present)
{
    fov_fixture_t g; begin(&g); ready(&g); eng_add_to_level_scene(g.f.engine,g.camera);
    const uint32_t identifiers[] = {CK_STATESAVE_CAMERAPROJTYPE,CK_STATESAVE_CAMERAOTHOZOOM,CK_STATESAVE_CAMERAFOV,CK_STATESAVE_CAMERAPLANES,CK_STATESAVE_OBJECTHIDDEN};
    for (size_t i = 0; i < sizeof(identifiers)/sizeof(*identifiers); i++) {
        nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(g.f.session)); ASSERT_NOT_NULL(chunk); chunk->class_id = NMO_CID_CAMERA;
        ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk)); nmo_chunk_set_data_version(chunk,4); ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(chunk,identifiers[i]));
        if (i == 0) ASSERT_EQ(NMO_OK,nmo_chunk_write_dword(chunk,0xfffffffeu));
        else if (i < 4) ASSERT_EQ(NMO_OK,nmo_chunk_write_float(chunk,i == 1 ? 2.5f : i == 2 ? .625f : 2));
        if (i == 3) ASSERT_EQ(NMO_OK,nmo_chunk_write_float(chunk,50));
        nmo_chunk_close(chunk); ASSERT_EQ(NMO_OK,eng_load_ic(g.f.engine,g.camera,chunk));
        ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.camera,0x80000001u)); ASSERT_TRUE(eng_set_camera_orthographic_zoom(g.f.engine,g.camera,3));
        ASSERT_TRUE(eng_set_camera_fov(g.f.engine,g.camera,.75f)); eng_set_camera_planes(g.f.engine,g.camera,3,60);
        eng_restore_ic(g.f.engine,g.camera); projection_frame(&g,g.camera,i == 0 ? 0xfffffffeu : 0x80000001u);
        frame(&g,g.camera,i == 2 ? .625f : .75f,i == 3 ? 2 : 3,i == 3 ? 50 : 60);
        float zoom = 0; ASSERT_TRUE(eng_camera_orthographic_zoom(g.f.engine,g.camera,&zoom)); ASSERT_FLOAT_EQ(i == 1 ? 2.5f : 3,zoom,0);
    }
    fixture_end(&g.f);
}

TEST(engine_fov, malformed_projection_initial_values_keep_the_previous_saved_state)
{
    fov_fixture_t g; begin(&g); ready(&g); eng_add_to_level_scene(g.f.engine,g.camera);
    ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.camera,0xfffffffeu)); eng_save_ic(g.f.engine,g.camera);
    nmo_chunk_t *chunk = nmo_chunk_create(nmo_session_get_arena(g.f.session)); ASSERT_NOT_NULL(chunk); chunk->class_id = NMO_CID_CAMERA;
    ASSERT_EQ(NMO_OK,nmo_chunk_start_write(chunk)); nmo_chunk_set_data_version(chunk,4);
    ASSERT_EQ(NMO_OK,nmo_chunk_write_identifier(chunk,CK_STATESAVE_CAMERAPROJTYPE)); nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,eng_load_ic(g.f.engine,g.camera,chunk));
    ASSERT_TRUE(eng_set_camera_projection_type(g.f.engine,g.camera,1)); eng_restore_ic(g.f.engine,g.camera); projection_frame(&g,g.camera,0xfffffffeu);
    fixture_end(&g.f);
}

TEST(engine_fov, nonstrict_execution_keeps_set_projection_owner_errors_from_advancing_flow)
{
    fov_fixture_t g; projection_begin(&g); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = 0;
    fixture_commit(&g.f,&options); ASSERT_EQ(NMO_OK,nmo_engine_start(g.f.engine,NMO_ENGINE_START_ALL));
    nmo_object_id_t missing = 0; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,g.target_value,&missing,sizeof(missing)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(0u,runs(&g.f,g.after));
    ASSERT_TRUE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_OUT(&g.f,g.block,0)));
    projection_frame(&g,g.camera,CK_PERSPECTIVEPROJECTION); fixture_end(&g.f);
}

TEST(engine_fov, projection_and_both_zoom_blocks_switch_a_live_camera_across_delayed_graph_stages)
{
    fov_fixture_t g; projection_begin(&g);
    nmo_object_id_t ortho = fixture_block(&g.f,GUID_ORTHOGRAPHIC_ZOOM,"Scale Orthographic View");
    nmo_object_id_t lens = fixture_block(&g.f,GUID_SET_ZOOM,"Set Perspective Lens");
    nmo_object_id_t perspective = fixture_block(&g.f,GUID_SET_PROJECTION,"Return to Perspective");
    ASSERT_TRUE(ortho != 0 && lens != 0 && perspective != 0);
    const nmo_object_id_t blocks[] = {ortho,lens,perspective};
    for (size_t i = 0; i < 3; i++) fixture_input_int(&g.f,fixture_target(&g.f,blocks[i]),(int32_t)g.camera);
    float zoom = 2.5f, focal = 36;
    fixture_input_value(&g.f,BLOCK_PIN(&g.f,ortho,0),&zoom,sizeof(zoom)); fixture_input_value(&g.f,BLOCK_PIN(&g.f,lens,0),&focal,sizeof(focal));
    fixture_input_int(&g.f,BLOCK_PIN(&g.f,perspective,0),CK_PERSPECTIVEPROJECTION);
    fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,0),BLOCK_IN(&g.f,ortho,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,ortho,0),BLOCK_IN(&g.f,lens,0),1);
    fixture_link(&g.f,BLOCK_OUT(&g.f,lens,0),BLOCK_IN(&g.f,perspective,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,perspective,0),BLOCK_IN(&g.f,g.after,0),0);
    ready(&g); eng_set_camera_planes(g.f.engine,g.camera,2,50);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); projection_frame(&g,g.camera,2); orthographic_frame(&g,g.camera,2.5f);
    frame(&g,g.camera,PI_F/2,2,50); ASSERT_EQ(0u,runs(&g.f,lens)); ASSERT_EQ(1u,runs(&g.f,g.after));
    eng_matrix_t matrix; nmo_rect_t rect;
    ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&matrix,NULL));
    ASSERT_TRUE(eng_project_box(&matrix,&(eng_vector_t){-.25f,-.25f,10},&(eng_vector_t){.25f,.25f,10},&rect)); ASSERT_FLOAT_EQ(.1875f,rect.left,1e-6f);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); projection_frame(&g,g.camera,1); approximate_frame(&g,g.camera,.927295218f,2,50);
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view)); ASSERT_FLOAT_EQ(2.5f,view.zoom,0);
    ASSERT_TRUE(eng_camera_view_projection(g.f.engine,g.camera,&matrix,NULL));
    ASSERT_TRUE(eng_project_box(&matrix,&(eng_vector_t){-.25f,-.25f,10},&(eng_vector_t){.25f,.25f,10},&rect)); ASSERT_FLOAT_EQ(.475f,rect.left,1e-6f);
    ASSERT_EQ(1u,runs(&g.f,lens)); ASSERT_EQ(1u,runs(&g.f,perspective)); ASSERT_EQ(2u,runs(&g.f,g.after)); fixture_end(&g.f);
}

TEST(engine_fov, loaded_and_copied_set_projection_graphs_remap_camera_targets_and_keep_raw_modes_independent)
{
    for (int derived = 0; derived < 2; derived++) {
        fov_fixture_t source; projection_begin(&source); nmo_object_id_t camera = derived ? source.second : source.camera;
        char target[32]; snprintf(target,sizeof(target),"%u",camera);
        ASSERT_EQ(source.target_value,saved_input(&source,fixture_target(&source.f,source.block),target));
        ASSERT_EQ(source.angle_value,saved_input(&source,BLOCK_PIN(&source.f,source.block,0),"Orthographic"));
        fixture_commit_bare(&source.f,NULL);
        nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id); owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
        nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id);
        script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
        const char *path = NMO_TEST_SCRATCH_FILE("engine_set_projection.cmo"); ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
        for (int started = 0; started < 2; started++) {
            fov_fixture_t host; memset(&host,0,sizeof(host)); fixture_begin_document(&host.f); fixture_begin_edit(&host.f);
            nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit(&host.f,&options);
            if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_ALL));
            nmo_engine_set_file_resolver(host.f.engine,resolve_file,(void *)path);
            int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.f.engine,"projection.cmo",true,&file));
            if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_SCENE_DEFAULT));
            nmo_object_id_t block = nmo_engine_object_named(host.f.engine,"Set Projection",NMO_CID_BEHAVIOR); ASSERT_TRUE(block != 0);
            const eng_module_t *module = eng_module_of(host.f.engine,block,NULL);
            const nmo_object_id_t local[] = {source.f.owner_id,camera,source.f.script_id,source.block,source.after,source.target_value,source.angle_value};
            nmo_object_id_t ids[7], copies[7];
            for (size_t i = 0; i < 7; i++) { ids[i] = eng_module_id(module,local[i]); ASSERT_TRUE(ids[i] != 0); }
            ASSERT_TRUE(ids[1] != camera); projection_frame(&host,ids[1],CK_PERSPECTIVEPROJECTION);
            ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0)); projection_frame(&host,ids[1],CK_ORTHOGRAPHICPROJECTION);
            ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.f.engine,ids,7,false,copies)); projection_frame(&host,copies[1],CK_ORTHOGRAPHICPROJECTION);
            nmo_object_id_t copied_target = 0; read_value(&host.f,copies[5],&copied_target,sizeof(copied_target)); ASSERT_EQ(copies[1],copied_target);
            uint32_t mode = 0xfffffffeu; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.f.engine,copies[6],&mode,sizeof(mode)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[2],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
            projection_frame(&host,copies[1],mode); projection_frame(&host,ids[1],CK_ORTHOGRAPHICPROJECTION); ASSERT_EQ(1u,runs(&host.f,copies[4])); ASSERT_EQ(1u,runs(&host.f,ids[4]));
            nmo_engine_destroy_object(host.f.engine,ids[1]); nmo_engine_destroy_object(host.f.engine,ids[2]);
            mode = 0x80000001u; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.f.engine,copies[6],&mode,sizeof(mode)));
            ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[2],true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
            projection_frame(&host,copies[1],mode); ASSERT_EQ(2u,runs(&host.f,copies[4]));
            const nmo_camera_state_t *document = nmo_type_query_object_get_ancestor_state_by_guid(host.f.engine->types,eng_find_object(host.f.engine,copies[1]),CKPGUID_CAMERA);
            ASSERT_NOT_NULL(document); ASSERT_EQ(CK_PERSPECTIVEPROJECTION,document->projection_type); fixture_end(&host.f);
        }
        ASSERT_EQ(CK_PERSPECTIVEPROJECTION,((nmo_camera_state_t *)fixture_state(&source.f,camera))->projection_type);
        fixture_end(&source.f); remove(path);
    }
}

static void current_camera_begin(fov_fixture_t *g)
{
    begin(g);
    g->block = fixture_block(&g->f,GUID_GET_CURRENT_CAMERA,"Get Current Camera");
    g->after = fixture_block(&g->f,GUID_NOP,"After Current Camera");
    ASSERT_TRUE(g->block != 0 && g->after != 0);
    fixture_link(&g->f,g->f.script_in,BLOCK_IN(&g->f,g->block,0),0);
    fixture_link(&g->f,BLOCK_OUT(&g->f,g->block,0),BLOCK_IN(&g->f,g->after,0),0);
}

static void current_camera_value(fov_fixture_t *g, nmo_object_id_t expected)
{
    nmo_object_id_t actual = 0; read_value(&g->f,BLOCK_POUT(&g->f,g->block,0),&actual,sizeof(actual)); ASSERT_EQ(expected,actual);
}

TEST(engine_fov, get_current_camera_returns_the_attached_camera_and_runs_once)
{
    fov_fixture_t g; current_camera_begin(&g); ready(&g);
    ASSERT_TRUE(nmo_engine_has_block(g.f.engine,GUID_GET_CURRENT_CAMERA)); ASSERT_EQ(0u,fixture_target(&g.f,g.block));
    nmo_engine_block_t block = {g.f.engine,eng_map_get(&g.f.engine->node_map,g.block)};
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_CAMERA,nmo_engine_block_output_parameter_type(&block,0)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); current_camera_value(&g,g.camera);
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); ASSERT_FALSE(nmo_engine_behavior_is_active(g.f.engine,g.block));
    g.f.engine->render.camera = g.second; ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,10)); current_camera_value(&g,g.camera);
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after)); fixture_end(&g.f);
}

TEST(engine_fov, get_current_camera_clears_stale_output_when_no_camera_is_attached)
{
    fov_fixture_t g; current_camera_begin(&g); ready(&g); g.f.engine->render.camera = 0;
    ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(g.f.engine,BLOCK_POUT(&g.f,g.block,0),&g.second,sizeof(g.second)));
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); current_camera_value(&g,0); ASSERT_EQ(1u,runs(&g.f,g.after));
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view)); ASSERT_EQ(0u,view.camera);
    fixture_end(&g.f);
}

TEST(engine_fov, get_current_camera_reads_target_cameras_and_each_new_activation)
{
    fov_fixture_t g; current_camera_begin(&g); ready(&g); g.f.engine->render.camera = g.second;
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); current_camera_value(&g,g.second);
    g.f.engine->render.camera = g.camera; trigger(&g); current_camera_value(&g,g.camera);
    g.f.engine->render.camera = 0; trigger(&g); current_camera_value(&g,0);
    g.f.engine->render.camera = g.second; trigger(&g); current_camera_value(&g,g.second);
    ASSERT_EQ(4u,runs(&g.f,g.after)); fixture_end(&g.f);
}

TEST(engine_fov, get_current_camera_clears_output_after_deletion_and_ignores_invalid_context_references)
{
    fov_fixture_t g; current_camera_begin(&g); ready(&g);
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); current_camera_value(&g,g.camera);
    nmo_engine_destroy_object(g.f.engine,g.camera); trigger(&g); current_camera_value(&g,0);
    const nmo_object_id_t invalid[] = {g.camera,g.f.owner_id,g.f.script_id,0x7fffffffu};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); i++) {
        g.f.engine->render.camera = invalid[i]; trigger(&g); current_camera_value(&g,0);
        nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view)); ASSERT_EQ(0u,view.camera);
    }
    g.f.engine->render.camera = g.second; trigger(&g); current_camera_value(&g,g.second); fixture_end(&g.f);
}

TEST(engine_fov, current_camera_outputs_are_private_to_engines_sharing_the_same_document)
{
    fov_fixture_t g; current_camera_begin(&g); ready(&g);
    nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
    nmo_engine_t *other = NULL; ASSERT_EQ(NMO_OK,nmo_engine_create(g.f.workspace,&options,&other));
    ASSERT_EQ(NMO_OK,nmo_bb_virtools_register(other)); ASSERT_EQ(NMO_OK,nmo_engine_start(other,NMO_ENGINE_START_ALL)); other->render.camera = g.second;
    ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); ASSERT_EQ(NMO_OK,nmo_engine_step(other,0)); current_camera_value(&g,g.camera);
    const void *data = NULL; size_t size = 0; ASSERT_EQ(NMO_OK,nmo_engine_param_value(other,BLOCK_POUT(&g.f,g.block,0),&data,&size,NULL));
    ASSERT_EQ(sizeof(nmo_object_id_t),size); nmo_object_id_t value = 0; memcpy(&value,data,sizeof(value)); ASSERT_EQ(g.second,value);
    other->render.camera = 0; ASSERT_EQ(NMO_OK,nmo_engine_activate_script(other,g.f.script_id,true,true)); ASSERT_EQ(NMO_OK,nmo_engine_step(other,0));
    ASSERT_EQ(NMO_OK,nmo_engine_param_value(other,BLOCK_POUT(&g.f,g.block,0),&data,&size,NULL)); memcpy(&value,data,sizeof(value)); ASSERT_EQ(0u,value);
    current_camera_value(&g,g.camera); nmo_engine_destroy(other); fixture_end(&g.f);
}

TEST(engine_fov, current_camera_queries_observe_set_as_active_camera_in_graph_order)
{
    fov_fixture_t g; current_camera_begin(&g);
    nmo_object_id_t activate = fixture_block(&g.f,NMO_GUID(0x368f0ab1,0x2d8957e4),"Switch Camera");
    nmo_object_id_t query = fixture_block(&g.f,GUID_GET_CURRENT_CAMERA,"Read Switched Camera");
    nmo_object_id_t after = fixture_block(&g.f,GUID_NOP,"After Camera Switch"); ASSERT_TRUE(activate != 0 && query != 0 && after != 0);
    fixture_input_int(&g.f,fixture_target(&g.f,activate),(int32_t)g.second);
    fixture_link(&g.f,BLOCK_OUT(&g.f,g.block,0),BLOCK_IN(&g.f,activate,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,activate,0),BLOCK_IN(&g.f,query,0),0);
    fixture_link(&g.f,BLOCK_OUT(&g.f,query,0),BLOCK_IN(&g.f,after,0),0);
    ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0)); current_camera_value(&g,g.camera);
    nmo_object_id_t actual = 0; read_value(&g.f,BLOCK_POUT(&g.f,query,0),&actual,sizeof(actual)); ASSERT_EQ(g.second,actual);
    nmo_engine_frame_t view; ASSERT_EQ(NMO_OK,nmo_engine_render_frame(g.f.engine,&view)); ASSERT_EQ(actual,view.camera);
    ASSERT_EQ(1u,runs(&g.f,after)); fixture_end(&g.f);
}

TEST(engine_fov, get_current_camera_keeps_native_success_flow_when_its_output_parameter_is_missing)
{
    fov_fixture_t g; current_camera_begin(&g);
    ASSERT_EQ(NMO_OK,nmo_script_edit_remove_parameter(g.f.tx,BLOCK_POUT(&g.f,g.block,0),true));
    ready(&g); ASSERT_EQ(NMO_OK,nmo_engine_step(g.f.engine,0));
    ASSERT_EQ(1u,runs(&g.f,g.block)); ASSERT_EQ(1u,runs(&g.f,g.after));
    ASSERT_FALSE(nmo_engine_io_is_active(g.f.engine,BLOCK_IN(&g.f,g.block,0))); fixture_end(&g.f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_fov, runtime_fov_updates_render_projection_and_screen_coordinates_without_changing_the_document);
REGISTER_TEST(engine_fov, fov_and_clipping_plane_changes_preserve_each_other_for_cameras_and_target_cameras);
REGISTER_TEST(engine_fov, set_fov_uses_the_native_fifty_degree_default_and_runs_once);
REGISTER_TEST(engine_fov, live_angle_and_target_inputs_update_only_the_selected_camera);
REGISTER_TEST(engine_fov, disconnected_angle_uses_the_native_fallback);
REGISTER_TEST(engine_fov, set_fov_keeps_native_zero_negative_large_and_nonfinite_radians);
REGISTER_TEST(engine_fov, invalid_targets_fail_before_consuming_input_or_changing_another_camera);
REGISTER_TEST(engine_fov, orthographic_cameras_store_fov_without_changing_zoom_projection);
REGISTER_TEST(engine_fov, camera_copies_preserve_runtime_lenses_and_remain_independent_after_source_deletion);
REGISTER_TEST(engine_fov, runtime_initial_conditions_capture_restore_and_replace_fov_with_clipping_planes);
REGISTER_TEST(engine_fov, modern_camera_initial_values_restore_serialized_raw_fov_and_planes);
REGISTER_TEST(engine_fov, legacy_partial_camera_initial_values_restore_only_present_members);
REGISTER_TEST(engine_fov, loaded_and_copied_scripts_remap_cameras_and_keep_runtime_lenses_independent);
REGISTER_TEST(engine_fov, terratools_sky_distortion_tracks_runtime_camera_fov_with_its_own_planes);
REGISTER_TEST(engine_fov, set_zoom_uses_the_native_fifty_millimeter_default_and_runs_once);
REGISTER_TEST(engine_fov, set_zoom_rejects_nonpositive_focal_lengths_without_consuming_input_or_changing_the_camera);
REGISTER_TEST(engine_fov, set_zoom_reports_invalid_targets_before_validating_focal_length_or_consuming_input);
REGISTER_TEST(engine_fov, set_zoom_uses_focal_lengths_in_millimeters_and_observes_live_target_changes);
REGISTER_TEST(engine_fov, set_zoom_changes_view_projection_and_billboard_screen_coordinates);
REGISTER_TEST(engine_fov, set_zoom_keeps_native_nan_infinite_and_extreme_positive_focal_length_semantics);
REGISTER_TEST(engine_fov, disconnected_focal_length_uses_fifty_millimeters);
REGISTER_TEST(engine_fov, set_zoom_updates_orthographic_camera_fov_without_changing_zoom_or_projection);
REGISTER_TEST(engine_fov, loaded_and_copied_zoom_scripts_remap_cameras_and_focal_length_parameters);
REGISTER_TEST(engine_fov, zoom_camera_copies_and_initial_conditions_capture_converted_fov_independently);
REGISTER_TEST(engine_fov, runtime_orthographic_zoom_updates_render_projection_and_screen_coordinates);
REGISTER_TEST(engine_fov, orthographic_zoom_fov_and_clipping_planes_preserve_each_other_in_both_write_orders);
REGISTER_TEST(engine_fov, camera_copies_keep_runtime_orthographic_zoom_independent_across_cache_growth_and_deletion);
REGISTER_TEST(engine_fov, orthographic_zoom_uses_the_native_unit_default_and_runs_once);
REGISTER_TEST(engine_fov, orthographic_zoom_applies_percent_scaling_only_before_version_two);
REGISTER_TEST(engine_fov, orthographic_zoom_preserves_zero_negative_extreme_and_nonfinite_values);
REGISTER_TEST(engine_fov, legacy_orthographic_zoom_keeps_signed_zero_underflow_and_nonfinite_scaling);
REGISTER_TEST(engine_fov, orthographic_zoom_observes_live_inputs_and_target_cameras_without_changing_fov_or_planes);
REGISTER_TEST(engine_fov, disconnected_orthographic_zoom_keeps_the_native_default_for_each_version);
REGISTER_TEST(engine_fov, orthographic_zoom_consumes_input_and_activates_output_before_invalid_target_errors);
REGISTER_TEST(engine_fov, perspective_cameras_store_orthographic_zoom_without_changing_perspective_projection);
REGISTER_TEST(engine_fov, orthographic_zoom_initial_conditions_capture_runtime_values_and_replace_previous_saves);
REGISTER_TEST(engine_fov, modern_camera_initial_values_restore_raw_orthographic_zoom_for_both_camera_classes);
REGISTER_TEST(engine_fov, legacy_camera_initial_values_restore_orthographic_zoom_only_when_present);
REGISTER_TEST(engine_fov, malformed_orthographic_zoom_initial_values_keep_the_previous_saved_state);
REGISTER_TEST(engine_fov, nonstrict_execution_follows_orthographic_zoom_output_on_owner_errors);
REGISTER_TEST(engine_fov, loaded_and_copied_orthographic_zoom_graphs_keep_versions_targets_and_values_independent);
REGISTER_TEST(engine_fov, runtime_projection_switches_update_render_box_projection_and_screen_coordinates);
REGISTER_TEST(engine_fov, projection_fov_zoom_and_planes_preserve_each_other_for_both_camera_classes);
REGISTER_TEST(engine_fov, camera_copies_preserve_raw_projection_modes_across_cache_growth_and_source_deletion);
REGISTER_TEST(engine_fov, set_projection_defaults_to_orthographic_and_runs_once);
REGISTER_TEST(engine_fov, disconnected_projection_input_uses_the_native_perspective_fallback);
REGISTER_TEST(engine_fov, set_projection_keeps_raw_integer_bits_and_observes_live_target_changes);
REGISTER_TEST(engine_fov, set_projection_owner_errors_keep_input_and_outputs_unchanged);
REGISTER_TEST(engine_fov, projection_initial_conditions_capture_runtime_modes_and_replace_previous_saves);
REGISTER_TEST(engine_fov, modern_camera_initial_values_restore_full_projection_bits_and_other_camera_members);
REGISTER_TEST(engine_fov, legacy_camera_initial_values_restore_projection_only_when_present);
REGISTER_TEST(engine_fov, malformed_projection_initial_values_keep_the_previous_saved_state);
REGISTER_TEST(engine_fov, nonstrict_execution_keeps_set_projection_owner_errors_from_advancing_flow);
REGISTER_TEST(engine_fov, projection_and_both_zoom_blocks_switch_a_live_camera_across_delayed_graph_stages);
REGISTER_TEST(engine_fov, loaded_and_copied_set_projection_graphs_remap_camera_targets_and_keep_raw_modes_independent);
REGISTER_TEST(engine_fov, get_current_camera_returns_the_attached_camera_and_runs_once);
REGISTER_TEST(engine_fov, get_current_camera_clears_stale_output_when_no_camera_is_attached);
REGISTER_TEST(engine_fov, get_current_camera_reads_target_cameras_and_each_new_activation);
REGISTER_TEST(engine_fov, get_current_camera_clears_output_after_deletion_and_ignores_invalid_context_references);
REGISTER_TEST(engine_fov, current_camera_outputs_are_private_to_engines_sharing_the_same_document);
REGISTER_TEST(engine_fov, current_camera_queries_observe_set_as_active_camera_in_graph_order);
REGISTER_TEST(engine_fov, get_current_camera_keeps_native_success_flow_when_its_output_parameter_is_missing);
TEST_MAIN_END()
