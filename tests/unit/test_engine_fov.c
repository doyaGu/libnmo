#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

#define PI_F 3.14159265358979323846f
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

TEST(engine_fov, loaded_and_copied_scripts_remap_cameras_and_keep_runtime_lenses_independent)
{
    fov_fixture_t source; behavior_begin(&source); char target[32];
    snprintf(target,sizeof(target),"%u",source.camera);
    ASSERT_EQ(source.target_value,saved_input(&source,fixture_target(&source.f,source.block),target));
    ASSERT_EQ(source.angle_value,saved_input(&source,BLOCK_PIN(&source.f,source.block,0),"0:60"));
    fixture_commit_bare(&source.f,NULL);
    nmo_beobject_state_t *owner = fixture_state(&source.f,source.f.owner_id);
    owner->has_single_activity = 1; owner->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE;
    nmo_behavior_state_t *script = fixture_state(&source.f,source.f.script_id);
    script->has_single_activity = 1; script->single_activity_flags = CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_START_RESET;
    const char *path = NMO_TEST_SCRATCH_FILE("engine_fov.cmo");
    ASSERT_EQ(NMO_OK,nmo_session_save_file(source.f.session,path,NULL,NULL));
    for (int started = 0; started < 2; started++) {
        fov_fixture_t host; memset(&host,0,sizeof(host));
        fixture_begin_document(&host.f); fixture_begin_edit(&host.f);
        nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL;
        fixture_commit(&host.f,&options);
        if (started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_ALL));
        nmo_engine_set_file_resolver(host.f.engine,resolve_file,(void *)path);
        int32_t file = -1; ASSERT_EQ(NMO_OK,nmo_engine_load(host.f.engine,"fov.cmo",true,&file));
        if (!started) ASSERT_EQ(NMO_OK,nmo_engine_start(host.f.engine,NMO_ENGINE_START_SCENE_DEFAULT));
        nmo_object_id_t block = nmo_engine_object_named(host.f.engine,"Set FOV",NMO_CID_BEHAVIOR);
        ASSERT_TRUE(block != 0);
        const eng_module_t *module = eng_module_of(host.f.engine,block,NULL);
        const nmo_object_id_t local[] = {source.f.owner_id,source.camera,source.f.script_id,source.block,source.after,source.target_value,source.angle_value};
        nmo_object_id_t ids[7], copies[7];
        for (size_t i = 0; i < 7; i++) { ids[i] = eng_module_id(module,local[i]); ASSERT_TRUE(ids[i] != 0); }
        ASSERT_TRUE(ids[1] != source.camera);
        frame(&host,ids[1],PI_F/2,1,100);
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0));
        float angle; ASSERT_TRUE(eng_camera_fov(host.f.engine,ids[1],&angle)); ASSERT_FLOAT_EQ(PI_F/3,angle,1e-7f);
        frame(&host,ids[1],angle,1,100); ASSERT_EQ(1u,runs(&host.f,ids[4]));
        eng_set_camera_planes(host.f.engine,ids[1],2,50);
        ASSERT_EQ(NMO_OK,nmo_engine_copy_objects(host.f.engine,ids,7,false,copies));
        frame(&host,copies[1],angle,2,50);
        /* The target value must name the copied camera after global-ID remapping. */
        nmo_object_id_t copied_target = 0; read_value(&host.f,copies[5],&copied_target,sizeof(copied_target)); ASSERT_EQ(copies[1],copied_target);
        angle = .625f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.f.engine,copies[6],&angle,sizeof(angle)));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[2],true,true));
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0)); frame(&host,copies[1],angle,2,50);
        float original; ASSERT_TRUE(eng_camera_fov(host.f.engine,ids[1],&original)); ASSERT_FLOAT_EQ(PI_F/3,original,1e-7f);
        ASSERT_EQ(1u,runs(&host.f,ids[4])); ASSERT_EQ(1u,runs(&host.f,copies[4]));
        nmo_engine_destroy_object(host.f.engine,ids[1]); nmo_engine_destroy_object(host.f.engine,ids[2]);
        angle = 1.25f; ASSERT_EQ(NMO_OK,nmo_engine_set_param_value(host.f.engine,copies[6],&angle,sizeof(angle)));
        ASSERT_EQ(NMO_OK,nmo_engine_activate_script(host.f.engine,copies[2],true,true));
        ASSERT_EQ(NMO_OK,nmo_engine_step(host.f.engine,0)); frame(&host,copies[1],angle,2,50);
        ASSERT_EQ(2u,runs(&host.f,copies[4]));
        const nmo_camera_state_t *document = nmo_type_query_object_get_ancestor_state_by_guid(host.f.engine->types,eng_find_object(host.f.engine,copies[1]),CKPGUID_CAMERA);
        ASSERT_NOT_NULL(document); ASSERT_FLOAT_EQ(PI_F/2,document->fov,0);
        fixture_end(&host.f);
    }
    ASSERT_FLOAT_EQ(PI_F/2,((nmo_camera_state_t *)fixture_state(&source.f,source.camera))->fov,0);
    fixture_end(&source.f);
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
TEST_MAIN_END()
