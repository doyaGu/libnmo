#include "test_framework.h"
#include "core/nmo_array.h"
#include "document/nmo_document_load.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_query.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "runtime/nmo_context.h"

#include <stdio.h>

static void assert_named_class_exists(
    nmo_document_t *document,
    const char *name,
    nmo_class_id_t class_id)
{
    nmo_object_query_t query = {0};
    query.name = name;
    query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    query.class_id = class_id;

    size_t count = 0u;
    ASSERT_EQ(NMO_OK, nmo_object_query_count(document, &query, &count));
    ASSERT_EQ(1u, count);
}

TEST(generated_scene_roundtrip, saves_and_reloads_scene_objects)
{
    const char *output_path = "test_generated_scene_roundtrip.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedLevel"));

    uint32_t scene = 0u;
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Scene_Main", &scene));

    uint32_t camera = 0u;
    uint32_t light = 0u;
    uint32_t cube = 0u;
    nmo_project_object_spec_t camera_spec = {
        .scene_handle = scene,
        .class_id = NMO_CID_CAMERA,
        .name = "Camera_Main",
        .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &camera_spec, &camera));
    ASSERT_NE(0u, camera);
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_active_camera(plan, scene, camera));
    nmo_project_object_spec_t light_spec = {
        .scene_handle = scene,
        .class_id = NMO_CID_LIGHT,
        .name = "Light_Key",
        .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &light_spec, &light));
    ASSERT_NE(0u, light);
    nmo_project_object_spec_t cube_spec = {
        .scene_handle = scene,
        .class_id = NMO_CID_3DENTITY,
        .name = "Cube_A",
        .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &cube_spec, &cube));
    ASSERT_NE(0u, cube);

    nmo_project_report_t report;
    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    assert_named_class_exists(document, "Scene_Main", NMO_CID_SCENE);
    assert_named_class_exists(document, "Camera_Main", NMO_CID_CAMERA);
    assert_named_class_exists(document, "Light_Key", NMO_CID_LIGHT);
    assert_named_class_exists(document, "Cube_A", NMO_CID_3DENTITY);

    nmo_object_query_t scene_query = {0};
    scene_query.name = "Scene_Main";
    scene_query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    scene_query.class_id = NMO_CID_SCENE;

    nmo_object_t *scene_object = NULL;
    ASSERT_EQ(NMO_OK, nmo_object_query_find_first(
                          document,
                          &scene_query,
                          &scene_object,
                          NULL));
    ASSERT_NOT_NULL(scene_object);

    nmo_scene_state_t *scene_state =
        (nmo_scene_state_t *)nmo_object_get_state(scene_object);
    ASSERT_NOT_NULL(scene_state);
    ASSERT_EQ(3u, nmo_array_size(&scene_state->object_descs));

    nmo_object_query_t camera_query = {0};
    camera_query.name = "Camera_Main";
    camera_query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    camera_query.class_id = NMO_CID_CAMERA;
    nmo_object_t *camera_object = NULL;
    ASSERT_EQ(NMO_OK, nmo_object_query_find_first(
                          document,
                          &camera_query,
                          &camera_object,
                          NULL));
    ASSERT_NOT_NULL(camera_object);
    ASSERT_EQ(nmo_object_get_id(camera_object), scene_state->starting_camera_id);

    const nmo_scene_object_desc_t *descs =
        NMO_ARRAY_DATA(nmo_scene_object_desc_t, &scene_state->object_descs);
    for (size_t i = 0; i < 3u; ++i) {
        ASSERT_NE(0u, descs[i].object_id);
        ASSERT_TRUE((descs[i].flags & CK_SCENEOBJECT_ACTIVE) != 0u);
        ASSERT_TRUE((descs[i].flags & CK_SCENEOBJECT_START_ACTIVATE) != 0u);
    }

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(generated_scene_roundtrip, saves_target_camera_light_bindings)
{
    const char *output_path = "test_generated_scene_targets.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedLevel"));

    uint32_t scene = 0u;
    uint32_t target = 0u;
    uint32_t camera = 0u;
    uint32_t light = 0u;
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Scene_Main", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Target",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &target));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_TARGETCAMERA,
                      .name = "TargetCamera",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &camera));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_camera_settings(
                          plan,
                          camera,
                          0.7f,
                          0.1f,
                          100.0f));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_camera_target(plan, camera, target));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_TARGETLIGHT,
                      .name = "TargetLight",
                      .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
                  },
                  &light));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_light_settings(
                          plan,
                          light,
                          1.0f,
                          1.0f,
                          1.0f,
                          1.0f,
                          50.0f,
                          VX_LIGHTPOINT));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_light_target(plan, light, target));

    nmo_project_report_t report;
    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);

    nmo_context_t *ctx = nmo_context_create(&(nmo_context_desc_t){0});
    ASSERT_NOT_NULL(ctx);
    nmo_document_t *document = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &document));
    ASSERT_NOT_NULL(document);

    nmo_object_query_t target_query = {0};
    target_query.name = "Target";
    target_query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    target_query.class_id = NMO_CID_3DENTITY;
    nmo_object_t *target_object = NULL;
    ASSERT_EQ(NMO_OK, nmo_object_query_find_first(
                          document,
                          &target_query,
                          &target_object,
                          NULL));
    ASSERT_NOT_NULL(target_object);

    nmo_object_query_t camera_query = {0};
    camera_query.name = "TargetCamera";
    camera_query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    camera_query.class_id = NMO_CID_TARGETCAMERA;
    nmo_object_t *camera_object = NULL;
    ASSERT_EQ(NMO_OK, nmo_object_query_find_first(
                          document,
                          &camera_query,
                          &camera_object,
                          NULL));
    ASSERT_NOT_NULL(camera_object);
    nmo_targetcamera_state_t *camera_state =
        (nmo_targetcamera_state_t *)nmo_object_get_state(camera_object);
    ASSERT_NOT_NULL(camera_state);
    ASSERT_TRUE(camera_state->has_target != 0u);
    ASSERT_EQ(nmo_object_get_id(target_object), camera_state->target_id);

    nmo_object_query_t light_query = {0};
    light_query.name = "TargetLight";
    light_query.name_mode = NMO_OBJECT_QUERY_NAME_EXACT;
    light_query.class_id = NMO_CID_TARGETLIGHT;
    nmo_object_t *light_object = NULL;
    ASSERT_EQ(NMO_OK, nmo_object_query_find_first(
                          document,
                          &light_query,
                          &light_object,
                          NULL));
    ASSERT_NOT_NULL(light_object);
    nmo_targetlight_state_t *light_state =
        (nmo_targetlight_state_t *)nmo_object_get_state(light_object);
    ASSERT_NOT_NULL(light_state);
    ASSERT_TRUE(light_state->has_target != 0u);
    ASSERT_EQ(nmo_object_get_id(target_object), light_state->target_id);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_scene_roundtrip, saves_and_reloads_scene_objects);
REGISTER_TEST(generated_scene_roundtrip, saves_target_camera_light_bindings);
TEST_MAIN_END()
