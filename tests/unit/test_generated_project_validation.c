#include "test_framework.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_project_validator.h"
#include "project/nmo_scene_authoring.h"

TEST(generated_project_validation, rejects_missing_scene_for_object) {
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Invalid"));

    nmo_project_object_spec_t spec = {
        .scene_handle = 9999u,
        .class_id = NMO_CID_3DENTITY,
        .name = "Orphan",
        .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &spec, NULL));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "missing_scene"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(generated_project_validation, rejects_missing_parent_for_object) {
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Invalid"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Scene", &scene));

    nmo_project_object_spec_t spec = {
        .scene_handle = scene,
        .parent_handle = 9999u,
        .class_id = NMO_CID_3DENTITY,
        .name = "Child",
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &spec, NULL));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "missing_parent"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(generated_project_validation, rejects_invalid_object_class) {
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Invalid"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Scene", &scene));

    nmo_project_object_spec_t spec = {
        .scene_handle = scene,
        .class_id = NMO_CID_MAXCLASSID + 1u,
        .name = "InvalidObject",
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &spec, NULL));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "invalid_object_class"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(generated_project_validation, rejects_missing_asset_files) {
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;
    uint32_t mesh_object = 0u;
    uint32_t texture_object = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "InvalidAssets"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Scene", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "MeshObject",
                  },
                  &mesh_object));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "TextureObject",
                  },
                  &texture_object));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(
                          plan,
                          mesh_object,
                          "missing_project_mesh.obj"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_material_texture(
                          plan,
                          texture_object,
                          "missing_project_texture.png"));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "missing_external_mesh_file"));
    ASSERT_TRUE(nmo_project_validation_contains(&report, "missing_material_texture_file"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(generated_project_validation, rejects_typed_specs_on_wrong_classes) {
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;
    uint32_t material_parent = 0u;
    uint32_t child = 0u;
    uint32_t material_transform = 0u;
    uint32_t camera_target = 0u;
    uint32_t light_target = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "InvalidTyped"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Scene", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_MATERIAL,
                      .name = "MaterialParent",
                  },
                  &material_parent));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .parent_handle = material_parent,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Child",
                  },
                  &child));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_MATERIAL,
                      .name = "MaterialTransform",
                  },
                  &material_transform));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_object_position(
                          plan,
                          material_transform,
                          1.0f,
                          2.0f,
                          3.0f));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "CameraTarget",
                  },
                  &camera_target));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_camera_settings(
                          plan,
                          camera_target,
                          0.75f,
                          0.25f,
                          500.0f));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_CAMERA,
                      .name = "LightTarget",
                  },
                  &light_target));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_light_settings(
                          plan,
                          light_target,
                          1.0f,
                          1.0f,
                          1.0f,
                          1.0f,
                          100.0f,
                          VX_LIGHTPOINT));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "invalid_parent_class"));
    ASSERT_TRUE(nmo_project_validation_contains(&report, "invalid_transform_target"));
    ASSERT_TRUE(nmo_project_validation_contains(&report, "invalid_camera_target"));
    ASSERT_TRUE(nmo_project_validation_contains(&report, "invalid_light_target"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_project_validation, rejects_missing_scene_for_object);
REGISTER_TEST(generated_project_validation, rejects_missing_parent_for_object);
REGISTER_TEST(generated_project_validation, rejects_invalid_object_class);
REGISTER_TEST(generated_project_validation, rejects_missing_asset_files);
REGISTER_TEST(generated_project_validation, rejects_typed_specs_on_wrong_classes);
TEST_MAIN_END()
