#include "test_framework.h"

#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"

TEST(project_report_diff, reports_created_scene_object_and_asset)
{
    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t cube = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Diff"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    nmo_project_object_spec_t cube_spec = {
        .scene_handle = scene,
        .class_id = NMO_CID_3DENTITY,
        .name = "Cube",
        .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &cube_spec, &cube));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_primitive_mesh(
                  plan,
                  cube,
                  NMO_PRIMITIVE_CUBE));

    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_dry_run(plan, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.dry_run);
    ASSERT_TRUE(nmo_project_report_diff_has_created_scene(&report, "Level"));
    ASSERT_TRUE(nmo_project_report_diff_has_created_object(&report, "Cube"));
    ASSERT_TRUE(nmo_project_report_diff_has_created_asset(&report, "Cube_Mesh"));
    ASSERT_EQ(1u, report.evidence.object_count);
    ASSERT_EQ(cube, report.evidence.objects[0].plan_handle);
    ASSERT_EQ(0u, report.evidence.objects[0].object_id);
    ASSERT_EQ(NMO_CID_3DENTITY, report.evidence.objects[0].class_id);
    ASSERT_STR_EQ("Cube", report.evidence.objects[0].name);
    ASSERT_EQ(1u, report.evidence.asset_binding_count);
    ASSERT_STR_EQ("Cube", report.evidence.asset_bindings[0].owner_name);
    ASSERT_STR_EQ("Cube_Mesh", report.evidence.asset_bindings[0].asset_name);
    ASSERT_STR_EQ("primitive_mesh", report.evidence.asset_bindings[0].kind);

    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_report_diff, reports_created_scene_object_and_asset);
TEST_MAIN_END()
