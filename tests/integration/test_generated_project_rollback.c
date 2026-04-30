#include "test_framework.h"

#include "object/nmo_class_ids.h"
#include "project/nmo_asset_plan.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_scene_authoring.h"

#include <stdio.h>

static bool file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    fclose(fp);
    return true;
}

TEST(generated_project_rollback, failed_asset_creation_leaves_no_output)
{
    const char *output_path = "failed_generated_project.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    uint32_t scene = 0u;
    uint32_t object = 0u;
    nmo_project_report_t report;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "Rollback"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_scene(plan, "Level", &scene));
    nmo_project_object_spec_t object_spec = {
        .scene_handle = scene,
        .class_id = NMO_CID_3DENTITY,
        .name = "Broken",
        .flags = NMO_PROJECT_OBJECT_FLAG_ACTIVE,
    };
    ASSERT_EQ(NMO_OK, nmo_project_plan_add_object(plan, &object_spec, &object));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_external_mesh(
                  plan,
                  object,
                  "missing.mesh"));

    nmo_project_report_init(&report);
    ASSERT_NE(NMO_OK,
              nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_FALSE(file_exists(output_path));
    ASSERT_FALSE(file_exists("failed_generated_project.cmo.tmp"));

    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_project_rollback, failed_asset_creation_leaves_no_output);
TEST_MAIN_END()
