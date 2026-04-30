#include "test_framework.h"
#include "project/nmo_project_plan.h"

TEST(project_plan, creates_empty_project_plan)
{
    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(0u, nmo_project_plan_scene_count(plan));
    ASSERT_NULL(nmo_project_plan_document_name(plan));
    nmo_project_plan_destroy(plan);
}

TEST(project_plan, clones_document_metadata)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_plan_t *clone = NULL;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedLevel"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    ASSERT_NOT_NULL(clone);
    ASSERT_STR_EQ("GeneratedLevel", nmo_project_plan_document_name(clone));
    ASSERT_EQ(0u, nmo_project_plan_scene_count(clone));

    nmo_project_plan_destroy(clone);
    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_plan, creates_empty_project_plan);
REGISTER_TEST(project_plan, clones_document_metadata);
TEST_MAIN_END()
