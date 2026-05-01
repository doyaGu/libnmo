#include "test_framework.h"
#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_project_validator.h"
#include "project/nmo_scene_authoring.h"

#include <stdio.h>

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

TEST(project_validator, rejects_missing_document_name)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "missing_document_name"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, accepts_named_empty_project)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_FALSE(nmo_project_validation_contains(&report, "missing_document_name"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, executor_rejects_invalid_plan_before_writing)
{
    const char *output_path = "test_project_validator_invalid.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    nmo_project_report_init(&report);

    ASSERT_EQ(
        NMO_ERR_VALIDATION_FAILED,
        nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(
        &report.validation,
        "missing_document_name"));
    ASSERT_FALSE(file_exists(output_path));

    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST(project_validator, rejects_named_obj_material_without_external_mesh)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;
    uint32_t object = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                  },
                  &object));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_primitive_mesh(plan, object, NMO_PRIMITIVE_CUBE));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  object,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Red",
                      .has_color = true,
                      .color = {1.0f, 0.0f, 0.0f, 1.0f},
                  }));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "obj_material_without_external_mesh"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, rejects_duplicate_named_obj_materials)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;
    uint32_t object = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                  },
                  &object));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, object, "missing.obj"));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  object,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Red",
                      .has_color = true,
                      .color = {1.0f, 0.0f, 0.0f, 1.0f},
                  }));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  object,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Red",
                      .has_color = true,
                      .color = {0.0f, 1.0f, 0.0f, 1.0f},
                  }));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "duplicate_obj_material"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, rejects_missing_named_obj_material_texture)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;
    uint32_t object = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                  },
                  &object));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, object, "missing.obj"));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  object,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Blue",
                      .has_texture = true,
                      .texture_path = "missing.png",
                  }));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "missing_obj_material_texture_file"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, rejects_unbound_obj_material_without_default)
{
    const char *obj_path = "test_project_validator_unbound_material.obj";
    remove(obj_path);
    FILE *fp = fopen(obj_path, "wb");
    ASSERT_NOT_NULL(fp);
    fputs(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "usemtl Red\n"
        "f 1 2 3\n"
        "usemtl Blue\n"
        "f 1 3 2\n",
        fp);
    fclose(fp);

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;
    uint32_t object = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                  },
                  &object));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_external_mesh(plan, object, obj_path));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_obj_material(
                  plan,
                  object,
                  &(nmo_project_material_spec_t){
                      .obj_material_name = "Red",
                      .has_color = true,
                      .color = {1.0f, 0.0f, 0.0f, 1.0f},
                  }));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "unbound_obj_material"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(obj_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_validator, rejects_missing_document_name);
REGISTER_TEST(project_validator, accepts_named_empty_project);
REGISTER_TEST(project_validator, executor_rejects_invalid_plan_before_writing);
REGISTER_TEST(project_validator, rejects_named_obj_material_without_external_mesh);
REGISTER_TEST(project_validator, rejects_duplicate_named_obj_materials);
REGISTER_TEST(project_validator, rejects_missing_named_obj_material_texture);
REGISTER_TEST(project_validator, rejects_unbound_obj_material_without_default);
TEST_MAIN_END()
