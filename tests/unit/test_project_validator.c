#include "test_framework.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "project/nmo_project_validator.h"

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

TEST_MAIN_BEGIN()
REGISTER_TEST(project_validator, rejects_missing_document_name);
REGISTER_TEST(project_validator, accepts_named_empty_project);
REGISTER_TEST(project_validator, executor_rejects_invalid_plan_before_writing);
TEST_MAIN_END()
