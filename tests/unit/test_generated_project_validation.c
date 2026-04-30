#include "test_framework.h"

#include "object/nmo_class_ids.h"
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

TEST_MAIN_BEGIN()
REGISTER_TEST(generated_project_validation, rejects_missing_scene_for_object);
REGISTER_TEST(generated_project_validation, rejects_missing_parent_for_object);
REGISTER_TEST(generated_project_validation, rejects_invalid_object_class);
TEST_MAIN_END()
