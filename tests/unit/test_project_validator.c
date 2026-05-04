#include "test_framework.h"
#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_manifest_json.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_project_validator.h"
#include "project/nmo_scene_authoring.h"

#include <stdio.h>
#include <string.h>

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

static const nmo_project_validation_issue_t *find_issue(
    const nmo_project_validation_report_t *report,
    const char *code)
{
    for (size_t i = 0u; i < report->issue_count; ++i) {
        if (report->issues[i].code && strcmp(report->issues[i].code, code) == 0) {
            return &report->issues[i];
        }
    }
    return NULL;
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

TEST(project_validator, reports_manifest_source_for_missing_named_texture)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"id\":\"cube\","
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"missing.obj\"},"
        "\"materials\":[{\"name\":\"Blue\",\"texture\":\"missing.png\"}]"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *issue =
        find_issue(&report, "missing_obj_material_texture_file");
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ("object", issue->subject_kind);
    ASSERT_STR_EQ("Cube", issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[0].materials[0].texture", issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, reports_manifest_source_for_missing_external_mesh)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"missing.obj\"}"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *issue =
        find_issue(&report, "missing_external_mesh_file");
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ("object", issue->subject_kind);
    ASSERT_STR_EQ("Cube", issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[0].mesh.obj", issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, reports_manifest_source_for_missing_default_texture)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"primitive\":\"cube\"},"
        "\"material\":{\"texture\":\"missing.png\"}"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *issue =
        find_issue(&report, "missing_material_texture_file");
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ("object", issue->subject_kind);
    ASSERT_STR_EQ("Cube", issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[0].material.texture", issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, reports_manifest_source_for_missing_default_texture_slot)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"primitive\":\"cube\"},"
        "\"material\":{\"textures\":[{\"slot\":1,\"path\":\"missing_detail.png\"}]}"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *issue =
        find_issue(&report, "missing_material_texture_file");
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ("object", issue->subject_kind);
    ASSERT_STR_EQ("Cube", issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[0].material.textures[0].path", issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, reports_manifest_source_for_missing_named_texture_slot)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"missing.obj\"},"
        "\"materials\":[{"
            "\"name\":\"Blue\","
            "\"textures\":[{\"slot\":1,\"path\":\"missing_detail.png\"}]"
        "}]"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *issue =
        find_issue(&report, "missing_obj_material_texture_file");
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ("object", issue->subject_kind);
    ASSERT_STR_EQ("Cube", issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[0].materials[0].textures[0].path", issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, reports_manifest_source_for_duplicate_obj_material)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"missing.obj\"},"
        "\"materials\":["
            "{\"name\":\"Red\",\"color\":[1,0,0,1]},"
            "{\"name\":\"Red\",\"color\":[0,1,0,1]}"
        "]"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *issue =
        find_issue(&report, "duplicate_obj_material");
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ("object", issue->subject_kind);
    ASSERT_STR_EQ("Cube", issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[0].materials[0]", issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, reports_manifest_source_for_unbound_obj_material)
{
    const char *obj_path = "test_project_validator_unbound_manifest.obj";
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

    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":[{"
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\","
        "\"mesh\":{\"obj\":\"test_project_validator_unbound_manifest.obj\"},"
        "\"materials\":[{\"name\":\"Red\",\"color\":[1,0,0,1]}]"
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *issue =
        find_issue(&report, "unbound_obj_material");
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ("object", issue->subject_kind);
    ASSERT_STR_EQ("Cube", issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[0].mesh.obj", issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(obj_path);
}

TEST(project_validator, reports_manifest_source_for_invalid_active_camera)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"active_camera\":\"cube\","
        "\"objects\":[{"
        "\"id\":\"cube\","
        "\"name\":\"Cube\","
        "\"class\":\"CK3dEntity\""
        "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *issue =
        find_issue(&report, "invalid_active_camera_class");
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ("scene", issue->subject_kind);
    ASSERT_STR_EQ("Level", issue->subject_name);
    ASSERT_STR_EQ("scenes[0].active_camera", issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, reports_manifest_source_for_invalid_camera_light_targets)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
        "\"name\":\"Level\","
        "\"objects\":["
            "{\"id\":\"bad-target\",\"name\":\"BadTarget\",\"class\":\"CKMaterial\"},"
            "{\"name\":\"Camera\",\"class\":\"CKTargetCamera\","
                "\"camera\":{\"fov\":0.5,\"near\":0.1,\"far\":100,"
                    "\"target\":\"bad-target\"}},"
            "{\"name\":\"Light\",\"class\":\"CKTargetLight\","
                "\"light\":{\"diffuse\":[1,1,1,1],\"range\":10,"
                    "\"type\":\"point\",\"target\":\"bad-target\"}}"
        "]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    nmo_project_validation_report_init(&report);

    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);

    const nmo_project_validation_issue_t *camera_issue =
        find_issue(&report, "invalid_camera_target_object");
    ASSERT_NOT_NULL(camera_issue);
    ASSERT_STR_EQ("object", camera_issue->subject_kind);
    ASSERT_STR_EQ("Camera", camera_issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[1]", camera_issue->source_path);

    const nmo_project_validation_issue_t *light_issue =
        find_issue(&report, "invalid_light_target_object");
    ASSERT_NOT_NULL(light_issue);
    ASSERT_STR_EQ("object", light_issue->subject_kind);
    ASSERT_STR_EQ("Light", light_issue->subject_name);
    ASSERT_STR_EQ("scenes[0].objects[2]", light_issue->source_path);

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, rejects_invalid_scene_active_camera)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene_a = 0u;
    uint32_t scene_b = 0u;
    uint32_t cube = 0u;
    uint32_t camera = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "A", &scene_a));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "B", &scene_b));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene_a,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Cube",
                  },
                  &cube));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene_b,
                      .class_id = NMO_CID_CAMERA,
                      .name = "Camera",
                  },
                  &camera));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_active_camera(plan, scene_a, cube));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_scene_active_camera(plan, scene_b, camera));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "invalid_active_camera_class"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, rejects_invalid_wavesound_authoring)
{
    const char *sound_path = "test_project_validator_sound.wav";
    FILE *sound_file = fopen(sound_path, "wb");
    ASSERT_NOT_NULL(sound_file);
    fputs("RIFF", sound_file);
    fclose(sound_file);

    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;
    uint32_t bad_sound = 0u;
    uint32_t anchor = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "NotSound",
                  },
                  &bad_sound));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_WAVESOUND,
                      .name = "AnchorIsSound",
                  },
                  &anchor));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_wavesound_file(
                          plan,
                          bad_sound,
                          sound_path));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_wavesound_attached_object(
                          plan,
                          bad_sound,
                          anchor));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "invalid_sound_class"));
    ASSERT_TRUE(nmo_project_validation_contains(
        &report,
        "invalid_sound_attached_object"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(sound_path);
}

TEST(project_validator, rejects_missing_wavesound_file)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t sound = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .class_id = NMO_CID_WAVESOUND,
                      .name = "Sound",
                  },
                  &sound));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_wavesound_file(
                          plan,
                          sound,
                          "missing-tone.wav"));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "missing_sound_file"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST(project_validator, rejects_invalid_objectanimation_authoring)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_validation_report_t report;
    uint32_t scene = 0u;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Generated"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "NotAnimation",
                      .has_animation = true,
                      .animation_target_handle = 2u,
                      .animation_format = CKOBJANIM_FORMAT_NEWDATA,
                  },
                  NULL));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_WAVESOUND,
                      .name = "NotEntityTarget",
                  },
                  NULL));

    nmo_project_validation_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_validate_plan(plan, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_TRUE(nmo_project_validation_contains(&report, "invalid_animation_class"));
    ASSERT_TRUE(nmo_project_validation_contains(
        &report,
        "unsupported_animation_format"));
    ASSERT_TRUE(nmo_project_validation_contains(
        &report,
        "invalid_animation_target_object"));

    nmo_project_validation_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_validator, rejects_missing_document_name);
REGISTER_TEST(project_validator, accepts_named_empty_project);
REGISTER_TEST(project_validator, executor_rejects_invalid_plan_before_writing);
REGISTER_TEST(project_validator, rejects_named_obj_material_without_external_mesh);
REGISTER_TEST(project_validator, rejects_duplicate_named_obj_materials);
REGISTER_TEST(project_validator, rejects_missing_named_obj_material_texture);
REGISTER_TEST(project_validator, rejects_unbound_obj_material_without_default);
REGISTER_TEST(project_validator, reports_manifest_source_for_missing_named_texture);
REGISTER_TEST(project_validator, reports_manifest_source_for_missing_external_mesh);
REGISTER_TEST(project_validator, reports_manifest_source_for_missing_default_texture);
REGISTER_TEST(project_validator, reports_manifest_source_for_missing_default_texture_slot);
REGISTER_TEST(project_validator, reports_manifest_source_for_missing_named_texture_slot);
REGISTER_TEST(project_validator, reports_manifest_source_for_duplicate_obj_material);
REGISTER_TEST(project_validator, reports_manifest_source_for_unbound_obj_material);
REGISTER_TEST(project_validator, reports_manifest_source_for_invalid_active_camera);
REGISTER_TEST(project_validator, reports_manifest_source_for_invalid_camera_light_targets);
REGISTER_TEST(project_validator, rejects_invalid_scene_active_camera);
REGISTER_TEST(project_validator, rejects_invalid_wavesound_authoring);
REGISTER_TEST(project_validator, rejects_missing_wavesound_file);
REGISTER_TEST(project_validator, rejects_invalid_objectanimation_authoring);
TEST_MAIN_END()
