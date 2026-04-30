#include "test_framework.h"
#include "document/nmo_document_load.h"
#include "object/nmo_object_query.h"
#include "project/nmo_project_executor.h"
#include "project/nmo_project_plan.h"
#include "runtime/nmo_context.h"

#include <stdio.h>

static nmo_status_t validate_document_skeleton(nmo_document_t *document)
{
    size_t object_count = 0;
    nmo_status_t status = nmo_object_query_count(document, NULL, &object_count);
    if (status != NMO_OK) {
        return status;
    }
    (void)object_count;
    return NMO_OK;
}

TEST(project_skeleton_acceptance, generates_empty_loadable_cmo)
{
    const char *output_path = "test_project_skeleton_acceptance_empty.cmo";
    remove(output_path);

    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedEmpty"));

    nmo_project_report_t report;
    nmo_project_report_init(&report);
    ASSERT_EQ(NMO_OK, nmo_project_executor_execute_to_file(plan, output_path, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_STR_EQ(output_path, report.output_path);

    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *doc = NULL;
    ASSERT_EQ(NMO_OK, nmo_document_load_file(ctx, output_path, NULL, &doc));
    ASSERT_NOT_NULL(doc);
    ASSERT_EQ(NMO_OK, validate_document_skeleton(doc));

    nmo_document_destroy(doc);
    nmo_context_release(ctx);
    nmo_project_report_dispose(&report);
    nmo_project_plan_destroy(plan);
    remove(output_path);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_skeleton_acceptance, generates_empty_loadable_cmo);
TEST_MAIN_END()
