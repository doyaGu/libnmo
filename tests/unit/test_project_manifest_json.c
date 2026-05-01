#include "test_framework.h"

#include "project/nmo_asset_plan.h"
#include "project/nmo_project_manifest_json.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"
#include "project/nmo_script_authoring.h"

#include <string.h>

TEST(project_manifest_json, parses_minimal_visible_scene)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"output\":\"out.cmo\","
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[{"
            "\"name\":\"Level\","
            "\"objects\":["
                "{\"name\":\"Camera\",\"class\":\"CKCamera\"},"
                "{\"name\":\"Light\",\"class\":\"CKLight\"},"
                "{\"name\":\"Cube\",\"class\":\"CK3dEntity\","
                    "\"mesh\":{\"obj\":\"assets/cube.obj\"},"
                    "\"material\":{\"color\":[1,0,0,1],\"texture\":\"assets/cube.png\"},"
                    "\"transform\":{\"position\":[1,2,3]}}"
            "]"
        "}]"
        "}";

    nmo_project_manifest_t manifest;
    nmo_project_manifest_init(&manifest);
    ASSERT_EQ(
        NMO_OK,
        nmo_project_manifest_json_read_manifest(
            json,
            strlen(json),
            &manifest));
    ASSERT_STR_EQ("out.cmo", manifest.output_path);
    ASSERT_NOT_NULL(manifest.plan);
    ASSERT_STR_EQ("Generated", nmo_project_plan_document_name(manifest.plan));
    ASSERT_EQ(1u, nmo_project_plan_scene_count(manifest.plan));
    ASSERT_EQ(3u, nmo_project_plan_object_count(manifest.plan));
    ASSERT_EQ(1u, nmo_project_plan_asset_count(manifest.plan));

    nmo_project_asset_desc_t asset = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_asset(manifest.plan, 0u, &asset));
    ASSERT_TRUE(asset.has_external_mesh);
    ASSERT_STR_EQ("assets/cube.obj", asset.external_mesh_path);
    ASSERT_TRUE(asset.has_material_color);
    ASSERT_TRUE(asset.has_material_texture);
    ASSERT_STR_EQ("assets/cube.png", asset.material_texture_path);

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(manifest.plan, 2u, &object));
    ASSERT_TRUE(object.has_position);
    ASSERT_FLOAT_EQ(1.0f, object.position[0], 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, object.position[1], 0.0001f);
    ASSERT_FLOAT_EQ(3.0f, object.position[2], 0.0001f);

    nmo_project_manifest_dispose(&manifest);
}

TEST(project_manifest_json, maps_fields_and_scripts_to_project_plan)
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
                "\"fields\":{\"entity_flags\":\"4\"},"
                "\"scripts\":[{"
                    "\"name\":\"CubeScript\","
                    "\"debug_output\":[\"generated script start\"]"
                "}]"
            "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(1u, nmo_project_plan_object_count(plan));
    ASSERT_EQ(1u, nmo_project_plan_script_count(plan));

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(plan, 0u, &object));
    ASSERT_EQ(1u, object.field_count);
    ASSERT_STR_EQ("entity_flags", object.fields[0].field_name);
    ASSERT_STR_EQ("4", object.fields[0].value_str);

    nmo_project_script_desc_t script = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script(plan, 0u, &script));
    ASSERT_EQ(object.handle, script.object_handle);
    ASSERT_STR_EQ("CubeScript", script.name);
    ASSERT_EQ(1u, script.step_count);

    nmo_project_script_step_desc_t step = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_script_step(
                          plan,
                          script.handle,
                          0u,
                          &step));
    ASSERT_EQ(NMO_PROJECT_SCRIPT_STEP_DEBUG_OUTPUT, step.kind);
    ASSERT_STR_EQ("generated script start", step.message);

    nmo_project_plan_destroy(plan);
}

TEST(project_manifest_json, rejects_unknown_fields)
{
    const char *json =
        "{"
        "\"version\":1,"
        "\"unexpected\":true,"
        "\"document\":{\"name\":\"Generated\"},"
        "\"scenes\":[]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST(project_manifest_json, rejects_unknown_transform_fields)
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
                "\"transform\":{\"rotation\":[0,0,0]}"
            "}]"
        "}]"
        "}";

    nmo_project_plan_t *plan = NULL;
    ASSERT_NE(NMO_OK, nmo_project_manifest_json_read(json, strlen(json), &plan));
    ASSERT_NULL(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_manifest_json, parses_minimal_visible_scene);
REGISTER_TEST(project_manifest_json, maps_fields_and_scripts_to_project_plan);
REGISTER_TEST(project_manifest_json, rejects_unknown_fields);
REGISTER_TEST(project_manifest_json, rejects_unknown_transform_fields);
TEST_MAIN_END()
