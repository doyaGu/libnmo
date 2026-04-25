#include "test_framework.h"

#include "behavior/nmo_edit_plan.h"
#include "core/nmo_array.h"
#include "document/nmo_document.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "type/nmo_type_guids.h"

#include <string.h>

static void create_object_or_fail(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    const char *name,
    nmo_object_id_t *out_id)
{
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  session, class_id, name, (nmo_guid_t){0, 0}, out_id, NULL));
    ASSERT_TRUE(*out_id != 0);
}

typedef struct edit_plan_fixture {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
    nmo_object_repository_t *repo;
} edit_plan_fixture_t;

static void edit_plan_fixture_init(edit_plan_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->session = nmo_session_create(fixture->ctx);
    ASSERT_NOT_NULL(fixture->session);
    fixture->repo = nmo_session_get_repository(fixture->session);
    ASSERT_NOT_NULL(fixture->repo);
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(fixture->session, &fixture->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document, &fixture->workspace));
}

static void edit_plan_fixture_dispose(edit_plan_fixture_t *fixture)
{
    if (fixture->workspace != NULL) {
        nmo_workspace_destroy(fixture->workspace);
    }
    if (fixture->document != NULL) {
        nmo_document_destroy(fixture->document);
    }
    if (fixture->session != NULL) {
        nmo_session_destroy(fixture->session);
    }
    if (fixture->ctx != NULL) {
        nmo_context_release(fixture->ctx);
    }
    memset(fixture, 0, sizeof(*fixture));
}

static void create_string_parameter(
    edit_plan_fixture_t *fixture,
    const char *initial,
    nmo_object_id_t *out_param_id,
    nmo_parameter_state_t **out_state)
{
    ASSERT_NOT_NULL(out_state);
    *out_state = NULL;
    create_object_or_fail(fixture->session, NMO_CID_PARAMETER, "param", out_param_id);
    nmo_object_t *param_obj =
        nmo_object_repository_find_by_id(fixture->repo, *out_param_id);
    ASSERT_NOT_NULL(param_obj);
    nmo_parameter_state_t *state = nmo_parameter_get_mutable_state(param_obj);
    ASSERT_NOT_NULL(state);
    state->type_guid = CKPGUID_STRING;
    state->mode = CKPARAM_MODE_BUFFER;
    state->has_state = true;
    ASSERT_EQ(NMO_OK,
              nmo_array_alloc(
                  &state->buffer_data,
                  sizeof(uint8_t),
                  strlen(initial) + 1u,
                  NULL));
    memcpy(state->buffer_data.data, initial, strlen(initial) + 1u);
    *out_state = state;
}

TEST(edit_plan, stores_parameter_value_ops) {
    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(0u, nmo_edit_plan_count(plan));

    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_parameter_value(plan, 42, "value", NULL));
    ASSERT_EQ(1u, nmo_edit_plan_count(plan));
    const nmo_edit_op_t *op = nmo_edit_plan_get(plan, 0);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_EDIT_OP_SET_PARAMETER_VALUE, op->kind);
    ASSERT_EQ(42u, op->primary_id);
    ASSERT_STR_EQ("value", op->data.set_value.value);

    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan, executor_commits_parameter_value_plan) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_string_parameter(&fixture, "old", &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, param_id, "new value", NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_FALSE(report.dry_run);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(param_id, report.changed_objects[0].id);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, "new value", strlen("new value") + 1u));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_rolls_back_failed_plan) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_string_parameter(&fixture, "old", &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, param_id, "new value", NULL));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, 999999u, "bad", NULL));

    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, report.operations[1].status);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, "old", 4));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_dry_run_reports_without_persisting) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t param_id = 0;
    nmo_parameter_state_t *state = NULL;
    create_string_parameter(&fixture, "old", &param_id, &state);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(plan, param_id, "new value", NULL));
    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = true;

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(fixture.workspace, plan, &options, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.dry_run);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(0, memcmp(state->buffer_data.data, "old", 4));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_adds_node_with_created_object_report) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Plan 2D Text"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_TRUE(report.operations[0].result_id != 0u);
    ASSERT_EQ(1u, report.created_object_count);
    ASSERT_EQ(report.operations[0].result_id, report.created_objects[0]);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_obj);
    ASSERT_NOT_NULL(node_state);
    ASSERT_EQ(NMO_CID_2DENTITY, node_state->compatible_class_id);
    ASSERT_TRUE(node_state->target_parameter_id != 0u);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(edit_plan, stores_parameter_value_ops);
REGISTER_TEST(edit_plan, executor_commits_parameter_value_plan);
REGISTER_TEST(edit_plan, executor_rolls_back_failed_plan);
REGISTER_TEST(edit_plan, executor_dry_run_reports_without_persisting);
REGISTER_TEST(edit_plan, executor_adds_node_with_created_object_report);
TEST_MAIN_END()
