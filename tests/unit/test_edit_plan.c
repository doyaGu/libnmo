#include "test_framework.h"

#include "behavior/nmo_edit_plan.h"
#include "core/nmo_array.h"
#include "document/nmo_document.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "type/nmo_type_guids.h"

#include <math.h>
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

static nmo_object_id_t find_named_parameter_in_ids(
    nmo_object_repository_t *repo,
    const nmo_array_t *ids,
    const char *name)
{
    const nmo_object_id_t *data = ids ? (const nmo_object_id_t *)ids->data : NULL;
    for (size_t i = 0; data != NULL && i < ids->count; ++i) {
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, data[i]);
        const char *param_name = param_obj ? nmo_object_get_name(param_obj) : NULL;
        if (param_name != NULL && strcmp(param_name, name) == 0) {
            return data[i];
        }
    }
    return 0u;
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

TEST(edit_plan, stores_full_script_edit_ops_and_clones_plan) {
    nmo_edit_plan_t *plan = NULL;
    nmo_edit_plan_t *clone = NULL;
    nmo_behavior_fold_map_t map = {
        .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
        .old_index = 0,
        .new_index = 1,
        .old_id = 11,
        .new_id = 22,
        .label = "In",
    };
    nmo_object_id_t fold_nodes[] = {101, 102};
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 500,
        .node_ids = fold_nodes,
        .node_count = 2,
        .anchor_id = 101,
        .block_guid = nmo_guid_parse("11111111-22222222"),
        .name = "Folded",
        .preserve_boundary = true,
        .input_maps = &map,
        .input_map_count = 1,
    };
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 600,
        .block_guid = nmo_guid_parse("33333333-44444444"),
        .name = "Replacement",
        .preserve_links = true,
        .preserve_params = true,
    };

    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_node(plan, 1, 2, 3));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_io(
        plan, 4, NMO_SCRIPT_EDIT_IO_INPUT, "Input"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rename_io(plan, 5, "Renamed"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_io(plan, 6, true));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_behavior_link(plan, 7, 8, 9, 10));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rewire_behavior_link(plan, 11, 12, 13));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_set_behavior_link_delay(plan, 14, 15));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_behavior_link(plan, 16, 17));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_parameter(
        plan, 18, NMO_SCRIPT_EDIT_PARAM_IN, CKPGUID_STRING, "Param"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_connect_parameter(plan, 19, 20));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_disconnect_parameter(plan, 21));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_parameter(plan, 22, true));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_operation(
        plan, 23, nmo_guid_parse("55555555-66666666"), 24, 25, 26));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rewire_operation(
        plan, 27, NMO_SCRIPT_EDIT_OP_SLOT_IN1, 28, 29, 30));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_operation(plan, 31));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_interface_policy(
        plan, 32, NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_data_cell(plan, 33, 1, 2, "cell"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    ASSERT_EQ(19u, nmo_edit_plan_count(plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_clone(plan, &clone));
    ASSERT_EQ(nmo_edit_plan_count(plan), nmo_edit_plan_count(clone));

    const nmo_edit_op_t *op = nmo_edit_plan_get(clone, 17);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_EDIT_OP_FOLD, op->kind);
    ASSERT_EQ(500u, op->primary_id);
    ASSERT_EQ(2u, op->data.fold.desc.node_count);
    ASSERT_EQ(101u, op->data.fold.node_ids[0]);
    ASSERT_EQ(102u, op->data.fold.node_ids[1]);
    ASSERT_EQ(1u, op->data.fold.input_maps[0].new_index);
    ASSERT_STR_EQ("Folded", op->data.fold.desc.name);

    op = nmo_edit_plan_get(clone, 18);
    ASSERT_NOT_NULL(op);
    ASSERT_EQ(NMO_EDIT_OP_REPLACE_BB, op->kind);
    ASSERT_STR_EQ("Replacement", op->data.replace_bb.desc.name);

    nmo_edit_plan_destroy(clone);
    nmo_edit_plan_destroy(plan);
}

TEST(edit_plan, report_dispose_releases_schema_v2_arrays) {
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));

    ASSERT_EQ(NMO_OK, nmo_edit_report_add_operation_handle(&report, 0, "node", 42));
    ASSERT_EQ(NMO_OK, nmo_edit_report_add_created_object(
        &report, 42, NMO_EDIT_OP_ADD_NODE, "behavior"));
    ASSERT_EQ(NMO_OK, nmo_edit_report_add_deleted_object(
        &report, 43, NMO_EDIT_OP_REMOVE_NODE, "behavior"));
    ASSERT_EQ(NMO_OK, nmo_edit_report_add_changed_object(
        &report, 44, NMO_EDIT_OP_SET_PARAMETER_VALUE, "parameter"));

    ASSERT_EQ(1u, report.created_object_count);
    ASSERT_EQ(42u, report.created_objects[0].id);
    ASSERT_STR_EQ("behavior", report.created_objects[0].role);
    ASSERT_EQ(1u, report.deleted_object_count);
    ASSERT_EQ(43u, report.deleted_objects[0].id);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(44u, report.changed_objects[0].id);

    nmo_edit_report_dispose(&report);
    ASSERT_EQ(0u, report.created_object_count);
    ASSERT_EQ(NULL, report.created_objects);
    ASSERT_EQ(NULL, report.deleted_objects);
    ASSERT_EQ(NULL, report.changed_objects);
}

TEST(edit_plan, report_owns_schema_v2_output_path) {
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));

    ASSERT_EQ(NMO_OK, nmo_edit_report_set_output_path(&report, "first.cmo"));
    ASSERT_STR_EQ("first.cmo", report.output_path);
    ASSERT_EQ(NMO_OK, nmo_edit_report_set_output_path(&report, "second.cmo"));
    ASSERT_STR_EQ("second.cmo", report.output_path);
    ASSERT_EQ(NMO_OK, nmo_edit_report_set_output_path(&report, NULL));
    ASSERT_EQ(NULL, report.output_path);

    nmo_edit_report_dispose(&report);
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

TEST(edit_plan, executor_rolls_back_created_handle_chain_failure) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_IN,
                  CKPGUID_STRING,
                  "Created Before Failure"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value_from_handle(
                  plan,
                  0u,
                  "missing-parameter-handle",
                  "bad",
                  NULL));

    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_TRUE(report.operations[0].result_id != 0u);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, report.operations[1].status);
    ASSERT_STR_EQ("handle_not_found", report.operations[1].diagnostic_code);
    ASSERT_TRUE(nmo_object_repository_find_by_id(
                    fixture.repo, report.operations[0].result_id) == NULL);

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
    ASSERT_TRUE(report.operations[0].handle_count > 1u);
    ASSERT_STR_EQ("node", report.operations[0].handles[0].name);
    ASSERT_EQ(report.operations[0].result_id, report.operations[0].handles[0].id);
    ASSERT_TRUE(report.created_object_count > 1u);
    ASSERT_EQ(report.operations[0].result_id, report.created_objects[0].id);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_obj);
    ASSERT_NOT_NULL(node_state);
    ASSERT_EQ(NMO_CID_2DENTITY, node_state->compatible_class_id);
    ASSERT_TRUE(node_state->target_parameter_id != 0u);
    {
        bool found_target = false;
        for (size_t i = 0; i < report.created_object_count; ++i) {
            if (report.created_objects[i].id == node_state->target_parameter_id) {
                found_target = true;
            }
        }
        ASSERT_TRUE(found_target);
    }
    {
        bool found_target_handle = false;
        bool found_input_handle = false;
        bool found_output_handle = false;
        for (size_t i = 0; i < report.operations[0].handle_count; ++i) {
            if (strcmp(report.operations[0].handles[i].name, "target") == 0 &&
                report.operations[0].handles[i].id == node_state->target_parameter_id) {
                found_target_handle = true;
            }
            if (strcmp(report.operations[0].handles[i].name, "input:On") == 0) {
                found_input_handle = true;
            }
            if (strcmp(report.operations[0].handles[i].name, "output:Exit On") == 0) {
                found_output_handle = true;
            }
        }
        ASSERT_TRUE(found_target_handle);
        ASSERT_TRUE(found_input_handle);
        ASSERT_TRUE(found_output_handle);
    }

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_building_block_defaults) {
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
                  "Defaulted 2D Text"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);

    nmo_object_id_t caret_id =
        find_named_parameter_in_ids(fixture.repo, &node_state->in_parameters, "Caret Size");
    nmo_object_t *caret_obj =
        nmo_object_repository_find_by_id(fixture.repo, caret_id);
    nmo_parameterin_state_t *caret_state = caret_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(caret_obj)
        : NULL;
    ASSERT_NOT_NULL(caret_state);
    ASSERT_TRUE(caret_state->source_id != 0u);

    nmo_object_t *source_obj =
        nmo_object_repository_find_by_id(fixture.repo, caret_state->source_id);
    nmo_parameter_state_t *source_state = source_obj
        ? nmo_parameter_get_mutable_state(source_obj)
        : NULL;
    ASSERT_NOT_NULL(source_state);
    ASSERT_EQ(CKPARAM_MODE_BUFFER, source_state->mode);
    ASSERT_TRUE(source_state->buffer_data.count >= sizeof(float));

    float caret_value = 0.0f;
    memcpy(&caret_value, source_state->buffer_data.data, sizeof(caret_value));
    ASSERT_TRUE(fabsf(caret_value - 10.0f) < 0.0001f);

    bool reported_created_source = false;
    for (size_t i = 0; i < report.created_object_count; ++i) {
        if (report.created_objects[i].id == caret_state->source_id) {
            reported_created_source = true;
        }
    }
    ASSERT_TRUE(reported_created_source);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_targetable_beobject_target) {
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
                  nmo_guid_parse("18655B3F-68291DC3"),
                  "Plan Output To Console"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);
    ASSERT_EQ(NMO_CID_BEOBJECT, node_state->compatible_class_id);
    ASSERT_TRUE((node_state->flags & CKBEHAVIOR_TARGETABLE) != 0u);
    ASSERT_TRUE(node_state->target_parameter_id != 0u);

    nmo_object_t *target_obj =
        nmo_object_repository_find_by_id(fixture.repo, node_state->target_parameter_id);
    nmo_parameterin_state_t *target_state = target_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(target_obj)
        : NULL;
    ASSERT_NOT_NULL(target_state);
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_BEOBJECT, target_state->type_guid));

    bool reported_target = false;
    bool handle_target = false;
    for (size_t i = 0; i < report.created_object_count; ++i) {
        if (report.created_objects[i].id == node_state->target_parameter_id) {
            reported_target = true;
        }
    }
    for (size_t i = 0; i < report.operations[0].handle_count; ++i) {
        if (strcmp(report.operations[0].handles[i].name, "target") == 0 &&
            report.operations[0].handles[i].id == node_state->target_parameter_id) {
            handle_target = true;
        }
    }
    ASSERT_TRUE(reported_target);
    ASSERT_TRUE(handle_target);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_resolves_parameter_value_from_prior_handle) {
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
                  "Probe 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value_from_handle(
                  plan, 0u, "input_param_source:Alignment", "Top-Left", NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_SET_PARAMETER_VALUE, report.operations[1].kind);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[1].result_id != 0u);
    ASSERT_EQ(report.operations[1].result_id, report.changed_objects[1].id);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_input_source_for_handle_value) {
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
                  "Probe 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value_from_handle(
                  plan, 0u, "input_param:Text", "loading trace", NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[1].result_id != 0u);
    bool reported_created_source = false;
    for (size_t i = 0; i < report.created_object_count; ++i) {
        if (report.created_objects[i].id == report.operations[1].result_id) {
            reported_created_source = true;
        }
    }
    ASSERT_TRUE(reported_created_source);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);
    nmo_object_id_t text_in_id =
        find_named_parameter_in_ids(fixture.repo, &node_state->in_parameters, "Text");
    nmo_object_t *text_in_obj =
        nmo_object_repository_find_by_id(fixture.repo, text_in_id);
    nmo_parameterin_state_t *text_in_state = text_in_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(text_in_obj)
        : NULL;
    ASSERT_NOT_NULL(text_in_state);
    ASSERT_EQ(report.operations[1].result_id, text_in_state->source_id);

    nmo_object_t *source_obj =
        nmo_object_repository_find_by_id(fixture.repo, text_in_state->source_id);
    nmo_parameter_state_t *source_state = source_obj
        ? nmo_parameter_get_mutable_state(source_obj)
        : NULL;
    ASSERT_NOT_NULL(source_state);
    ASSERT_EQ(CKPARAM_MODE_BUFFER, source_state->mode);
    ASSERT_TRUE(source_state->buffer_data.count >= strlen("loading trace") + 1u);
    ASSERT_EQ(0, memcmp(source_state->buffer_data.data,
                        "loading trace",
                        strlen("loading trace") + 1u));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_materializes_input_source_for_handle_bytes) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    static const uint8_t trace_bytes[] = {
        'r', 'a', 'w', ' ', 't', 'r', 'a', 'c', 'e', '\0',
    };
    const nmo_parameter_write_options_t options = {
        .resize = true,
    };
    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Probe 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_bytes_from_handle(
                  plan,
                  0u,
                  "input_param:Text",
                  trace_bytes,
                  sizeof(trace_bytes),
                  &options));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[1].result_id != 0u);

    nmo_object_t *node_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[0].result_id);
    nmo_behavior_state_t *node_state = node_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(node_obj)
        : NULL;
    ASSERT_NOT_NULL(node_state);
    nmo_object_id_t text_in_id =
        find_named_parameter_in_ids(fixture.repo, &node_state->in_parameters, "Text");
    nmo_object_t *text_in_obj =
        nmo_object_repository_find_by_id(fixture.repo, text_in_id);
    nmo_parameterin_state_t *text_in_state = text_in_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(text_in_obj)
        : NULL;
    ASSERT_NOT_NULL(text_in_state);
    ASSERT_EQ(report.operations[1].result_id, text_in_state->source_id);

    nmo_object_t *source_obj =
        nmo_object_repository_find_by_id(fixture.repo, text_in_state->source_id);
    nmo_parameter_state_t *source_state = source_obj
        ? nmo_parameter_get_mutable_state(source_obj)
        : NULL;
    ASSERT_NOT_NULL(source_state);
    ASSERT_EQ(CKPARAM_MODE_BUFFER, source_state->mode);
    ASSERT_TRUE(source_state->buffer_data.count >= sizeof(trace_bytes));
    ASSERT_EQ(0,
              memcmp(source_state->buffer_data.data,
                     trace_bytes,
                     sizeof(trace_bytes)));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_connects_parameter_to_prior_node_handle) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t source_parameter_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&owner_state->script_ids, &root_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;

    nmo_script_edit_tx_t *seed_tx = NULL;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed source", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_STRING,
                  "Trace Source",
                  &source_parameter_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("18655B3F-68291DC3"),
                  "Parameter Logger"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_connect_parameter_to_handle(
                  plan, source_parameter_id, 0u, "input_param:String"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_CONNECT_PARAMETER, report.operations[1].kind);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[1].result_id != 0u);

    nmo_object_t *target_obj =
        nmo_object_repository_find_by_id(fixture.repo, report.operations[1].result_id);
    nmo_parameterin_state_t *target_state = target_obj
        ? (nmo_parameterin_state_t *)nmo_object_get_state(target_obj)
        : NULL;
    ASSERT_NOT_NULL(target_state);
    ASSERT_EQ(source_parameter_id, target_state->source_id);

    bool reported_source_endpoint = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == source_parameter_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "parameter_edge_source") == 0) {
            reported_source_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_source_endpoint);

    const nmo_object_id_t target_parameter_id = report.operations[1].result_id;

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_disconnect_parameter(
                  plan,
                  target_parameter_id));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_DISCONNECT_PARAMETER, report.operations[0].kind);
    reported_source_endpoint = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == source_parameter_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "parameter_edge_source") == 0) {
            reported_source_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_source_endpoint);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_resolves_behavior_link_io_handles) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    nmo_object_id_t child_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Child", &child_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_object_t *child_obj =
        nmo_object_repository_find_by_id(fixture.repo, child_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    nmo_behavior_state_t *child_state = child_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(child_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&owner_state->script_ids, &root_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&root_state->sub_behaviors, &child_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    child_state->owner_id = root_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan,
                  root_id,
                  NMO_SCRIPT_EDIT_IO_INPUT,
                  "Enter"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan,
                  child_id,
                  NMO_SCRIPT_EDIT_IO_INPUT,
                  "Child In"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link_from_handles(
                  plan,
                  root_id,
                  0u,
                  "io",
                  1u,
                  "io",
                  0u));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(3u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_ADD_BEHAVIOR_LINK, report.operations[2].kind);
    ASSERT_EQ(NMO_OK, report.operations[2].status);
    ASSERT_TRUE(report.operations[2].result_id != 0u);

    bool reported_created_link = false;
    bool reported_from_endpoint = false;
    bool reported_to_endpoint = false;
    for (size_t i = 0; i < report.created_object_count; ++i) {
        if (report.created_objects[i].id == report.operations[2].result_id) {
            reported_created_link = true;
        }
    }
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "control_link_endpoint") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == report.operations[0].result_id) {
            reported_from_endpoint = true;
        } else if (report.changed_objects[i].id == report.operations[1].result_id) {
            reported_to_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_created_link);
    ASSERT_TRUE(reported_from_endpoint);
    ASSERT_TRUE(reported_to_endpoint);

    const nmo_object_id_t link_id = report.operations[2].result_id;
    const nmo_object_id_t from_io_id = report.operations[0].result_id;
    const nmo_object_id_t to_io_id = report.operations[1].result_id;

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_behavior_link(
                  plan,
                  link_id,
                  from_io_id,
                  to_io_id));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REWIRE_BEHAVIOR_LINK, report.operations[0].kind);
    reported_from_endpoint = false;
    reported_to_endpoint = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "control_link_endpoint") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == from_io_id) {
            reported_from_endpoint = true;
        } else if (report.changed_objects[i].id == to_io_id) {
            reported_to_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_from_endpoint);
    ASSERT_TRUE(reported_to_endpoint);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_behavior_link(
                  plan,
                  root_id,
                  link_id));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_EDIT_OP_REMOVE_BEHAVIOR_LINK, report.operations[0].kind);
    reported_from_endpoint = false;
    reported_to_endpoint = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "control_link_endpoint") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == from_io_id) {
            reported_from_endpoint = true;
        } else if (report.changed_objects[i].id == to_io_id) {
            reported_to_endpoint = true;
        }
    }
    ASSERT_TRUE(reported_from_endpoint);
    ASSERT_TRUE(reported_to_endpoint);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_rewire_operation_slot_parameter_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    nmo_object_id_t owner_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&owner_state->script_ids, &root_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_script_edit_tx_t *seed_tx = NULL;
    nmo_object_id_t in1_id = 0;
    nmo_object_id_t in2_id = 0;
    nmo_object_id_t out_id = 0;
    nmo_object_id_t operation_id = 0;
    ASSERT_EQ(NMO_OK, nmo_script_edit_begin(fixture.workspace, "seed op", &seed_tx));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "A", &in1_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "B", &in2_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_parameter(
                  seed_tx, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Out", &out_id));
    ASSERT_EQ(NMO_OK,
              nmo_script_edit_add_operation(
                  seed_tx,
                  root_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  in1_id,
                  in2_id,
                  out_id,
                  &operation_id));
    ASSERT_EQ(NMO_OK, nmo_script_edit_commit(seed_tx));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan,
                  root_id,
                  NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT,
                  "Replacement A"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_operation_with_refs(
                  plan,
                  operation_id,
                  NMO_SCRIPT_EDIT_OP_SLOT_IN1,
                  0u,
                  0u,
                  "parameter",
                  0u,
                  0u,
                  NULL,
                  0u,
                  0u,
                  NULL));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[0].result_id != 0u);

    bool reported_slot_parameter = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].id == report.operations[0].result_id &&
            report.changed_objects[i].role != NULL &&
            strcmp(report.changed_objects[i].role, "operation_slot_parameter") == 0) {
            reported_slot_parameter = true;
        }
    }
    ASSERT_TRUE(reported_slot_parameter);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_reports_add_operation_slot_parameter_impact) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t owner_id = 0;
    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_3DENTITY, "Owner", &owner_id);
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);
    nmo_object_t *owner_obj =
        nmo_object_repository_find_by_id(fixture.repo, owner_id);
    nmo_object_t *root_obj =
        nmo_object_repository_find_by_id(fixture.repo, root_id);
    nmo_beobject_state_t *owner_state = owner_obj
        ? (nmo_beobject_state_t *)nmo_object_get_state(owner_obj)
        : NULL;
    nmo_behavior_state_t *root_state = root_obj
        ? (nmo_behavior_state_t *)nmo_object_get_state(root_obj)
        : NULL;
    ASSERT_NOT_NULL(owner_state);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&owner_state->script_ids, &root_id));
    root_state->flags |= 0x00000002u;
    root_state->owner_id = owner_id;
    nmo_workspace_destroy(fixture.workspace);
    fixture.workspace = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_workspace_create(
                  fixture.ctx, fixture.document, &fixture.workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "A"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "B"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_INT, "Out"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_operation_with_refs(
                  plan,
                  root_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  0u,
                  0u,
                  "parameter",
                  0u,
                  1u,
                  "parameter",
                  0u,
                  2u,
                  "parameter"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.operations[3].status);

    bool reported_in1 = false;
    bool reported_in2 = false;
    bool reported_out = false;
    for (size_t i = 0; i < report.changed_object_count; ++i) {
        if (report.changed_objects[i].role == NULL ||
            strcmp(report.changed_objects[i].role, "operation_slot_parameter") != 0) {
            continue;
        }
        if (report.changed_objects[i].id == report.operations[0].result_id) {
            reported_in1 = true;
        } else if (report.changed_objects[i].id == report.operations[1].result_id) {
            reported_in2 = true;
        } else if (report.changed_objects[i].id == report.operations[2].result_id) {
            reported_out = true;
        }
    }
    ASSERT_TRUE(reported_in1);
    ASSERT_TRUE(reported_in2);
    ASSERT_TRUE(reported_out);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_runs_script_ops_and_records_validation) {
    edit_plan_fixture_t fixture;
    edit_plan_fixture_init(&fixture);

    nmo_object_id_t root_id = 0;
    create_object_or_fail(fixture.session, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_io(
        plan, root_id, NMO_SCRIPT_EDIT_IO_INPUT, "In"));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_parameter(
        plan, root_id, NMO_SCRIPT_EDIT_PARAM_LOCAL, CKPGUID_STRING, "Local"));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(fixture.workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(NMO_OK, report.validation.final_status);
    ASSERT_EQ(NMO_OK, report.validation.reference_status);
    ASSERT_EQ(NMO_OK, report.validation.behavior_index_status);
    ASSERT_EQ(NMO_OK, report.validation.interface_status);
    ASSERT_EQ(2u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(NMO_OK, report.operations[1].status);
    ASSERT_TRUE(report.operations[0].result_id != 0u);
    ASSERT_TRUE(report.operations[1].result_id != 0u);
    ASSERT_EQ(1u, report.operations[0].handle_count);
    ASSERT_STR_EQ("io", report.operations[0].handles[0].name);
    ASSERT_EQ(report.operations[0].result_id, report.operations[0].handles[0].id);
    ASSERT_EQ(1u, report.operations[1].handle_count);
    ASSERT_STR_EQ("parameter", report.operations[1].handles[0].name);
    ASSERT_EQ(report.operations[1].result_id, report.operations[1].handles[0].id);
    ASSERT_TRUE(report.created_object_count >= 2u);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    edit_plan_fixture_dispose(&fixture);
}

TEST(edit_plan, executor_replaces_leaf_bb_in_transaction) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    nmo_guid_t replacement_guid = nmo_guid_parse("D0B7ADF3-D3FF3CF6");
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 343u,
        .block_guid = replacement_guid,
        .name = "Plan Replaced BB",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };

    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(343u, report.operations[0].result_id);
    ASSERT_EQ(1u, report.changed_object_count);
    ASSERT_EQ(343u, report.changed_objects[0].id);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, 343u);
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    ASSERT_NOT_NULL(state);
    ASSERT_TRUE(nmo_guid_equals(replacement_guid, state->block_guid));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_replace_bb_dry_run_rolls_back) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *object = nmo_object_repository_find_by_id(repo, 343u);
    nmo_behavior_state_t *state = object
        ? (nmo_behavior_state_t *)nmo_object_get_state(object)
        : NULL;
    ASSERT_NOT_NULL(state);
    nmo_guid_t original_guid = state->block_guid;

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    nmo_guid_t replacement_guid = nmo_guid_parse("D0B7ADF3-D3FF3CF6");
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 343u,
        .block_guid = replacement_guid,
        .name = "Plan Dry Replace",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_edit_executor_options_t options =
        nmo_edit_executor_options_default();
    options.dry_run = true;

    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.dry_run);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(343u, report.operations[0].result_id);
    ASSERT_TRUE(nmo_guid_equals(original_guid, state->block_guid));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_folds_closed_graph_in_transaction) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_id_t fold_nodes[] = {
        4166u, 4140u, 4147u, 4157u, 4165u,
        4153u, 4151u, 4155u, 4143u, 4145u,
    };
    nmo_guid_t fold_guid = nmo_guid_parse("42414C07-10000007");
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 4692u,
        .node_ids = fold_nodes,
        .node_count = sizeof(fold_nodes) / sizeof(fold_nodes[0]),
        .anchor_id = 4166u,
        .block_guid = fold_guid,
        .name = "Plan Fold Small Graph",
        .block_version = 65536u,
        .preserve_boundary = true,
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE,
    };

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));

    ASSERT_EQ(NMO_OK, nmo_edit_executor_execute(workspace, plan, NULL, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(4166u, report.operations[0].result_id);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor = nmo_object_repository_find_by_id(repo, 4166u);
    nmo_behavior_state_t *state = anchor
        ? (nmo_behavior_state_t *)nmo_object_get_state(anchor)
        : NULL;
    ASSERT_NOT_NULL(state);
    ASSERT_TRUE(nmo_guid_equals(fold_guid, state->block_guid));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, 4140u));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_fold_dry_run_rolls_back) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *anchor = nmo_object_repository_find_by_id(repo, 4166u);
    nmo_behavior_state_t *anchor_state = anchor
        ? (nmo_behavior_state_t *)nmo_object_get_state(anchor)
        : NULL;
    ASSERT_NOT_NULL(anchor_state);
    nmo_guid_t original_guid = anchor_state->block_guid;

    nmo_object_id_t fold_nodes[] = {
        4166u, 4140u, 4147u, 4157u, 4165u,
        4153u, 4151u, 4155u, 4143u, 4145u,
    };
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 4692u,
        .node_ids = fold_nodes,
        .node_count = sizeof(fold_nodes) / sizeof(fold_nodes[0]),
        .anchor_id = 4166u,
        .block_guid = nmo_guid_parse("42414C07-10000007"),
        .name = "Plan Fold Dry Run",
        .block_version = 65536u,
        .preserve_boundary = true,
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE,
    };
    nmo_edit_executor_options_t options =
        nmo_edit_executor_options_default();
    options.dry_run = true;

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.dry_run);
    ASSERT_EQ(NMO_OK, report.operations[0].status);
    ASSERT_EQ(4166u, report.operations[0].result_id);
    ASSERT_TRUE(nmo_guid_equals(original_guid, anchor_state->block_guid));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, 4140u));

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_fold_dry_run_reports_semantic_risks) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_id_t fold_nodes[] = {237u, 358u};
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 363u,
        .node_ids = fold_nodes,
        .node_count = sizeof(fold_nodes) / sizeof(fold_nodes[0]),
        .anchor_id = 358u,
        .block_guid = nmo_guid_parse("42414C02-10000002"),
        .name = "Plan Risky Fold",
        .block_version = 65536u,
        .preserve_boundary = false,
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE,
    };
    nmo_edit_executor_options_t options =
        nmo_edit_executor_options_default();
    options.dry_run = true;

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));

    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.semantic_risk_count > 0u);
    bool found_shared = false;
    for (size_t i = 0; i < report.semantic_risk_count; ++i) {
        if (report.semantic_risks[i].code &&
            strcmp(report.semantic_risks[i].code, "shared_parameter") == 0) {
            found_shared = true;
        }
    }
    ASSERT_TRUE(found_shared);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, report_semantic_risk_merge_deduplicates) {
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));

    nmo_behavior_semantic_risk_t risks[] = {
        {
            .severity = NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            .code = "message_flow",
            .message = "Selected behavior participates in message send/wait flow",
            .object_id = 2233u,
        },
        {
            .severity = NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            .code = "message_flow",
            .message = "Selected behavior participates in message send/wait flow",
            .object_id = 2233u,
        },
        {
            .severity = NMO_BEHAVIOR_SEMANTIC_RISK_WARN,
            .code = "activation_delay",
            .message = "Boundary control link preserves activation delay",
            .object_id = 1001u,
        },
    };

    ASSERT_EQ(NMO_OK, nmo_edit_report_merge_semantic_risks(
                          &report, risks, sizeof(risks) / sizeof(risks[0])));
    ASSERT_EQ(NMO_OK, nmo_edit_report_merge_semantic_risks(
                          &report, risks, sizeof(risks) / sizeof(risks[0])));

    ASSERT_EQ(2u, report.semantic_risk_count);
    ASSERT_STR_EQ("message_flow", report.semantic_risks[0].code);
    ASSERT_STR_EQ("activation_delay", report.semantic_risks[1].code);

    nmo_edit_report_dispose(&report);
}

TEST(edit_plan, executor_merges_edit_plan_semantic_validation) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 2233u,
        .block_guid = nmo_guid_parse("42414C07-10000007"),
        .name = "Semantic Validator Replace",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = true;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));

    bool saw_message_risk = false;
    for (size_t i = 0; i < report.semantic_risk_count; ++i) {
        if (report.semantic_risks[i].code != NULL &&
            strcmp(report.semantic_risks[i].code, "message_flow") == 0 &&
            report.semantic_risks[i].object_id == 2233u) {
            saw_message_risk = true;
        }
    }
    ASSERT_TRUE(saw_message_risk);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_reports_generic_activation_delay_risk) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Nop.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(plan, 6u, 5u, 2u, 7u));

    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = true;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK,
              nmo_edit_executor_execute(workspace, plan, &options, &report));

    bool saw_delay_risk = false;
    for (size_t i = 0; i < report.semantic_risk_count; ++i) {
        if (report.semantic_risks[i].code != NULL &&
            strcmp(report.semantic_risks[i].code, "activation_delay") == 0) {
            saw_delay_risk = true;
        }
    }
    ASSERT_TRUE(saw_delay_risk);

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(edit_plan, executor_fold_failure_reports_operation_diagnostic) {
    nmo_context_t *ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session =
        nmo_session_load(ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(session);
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_object_id_t fold_nodes[] = {2364u, 2208u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 0u,
            .new_index = 0u,
        },
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 1u,
            .new_index = 1u,
        },
    };
    nmo_behavior_fold_desc_t fold = {
        .parent_id = 4692u,
        .node_ids = fold_nodes,
        .node_count = sizeof(fold_nodes) / sizeof(fold_nodes[0]),
        .anchor_id = 2364u,
        .block_guid = nmo_guid_parse("42414C07-10000007"),
        .name = "Plan Unclosed Fold",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = sizeof(input_maps) / sizeof(input_maps[0]),
        .interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE,
    };

    nmo_edit_plan_t *plan = NULL;
    nmo_edit_report_t report;
    ASSERT_EQ(NMO_OK, nmo_edit_report_init(&report));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_fold(plan, &fold));

    ASSERT_NE(NMO_OK, nmo_edit_executor_execute(workspace, plan, NULL, &report));
    ASSERT_FALSE(report.ok);
    ASSERT_EQ(1u, report.operation_count);
    ASSERT_STR_EQ("selection_not_closed", report.operations[0].diagnostic_code);
    ASSERT_STR_CONTAINS(report.operations[0].diagnostic_message,
                        "must include child behavior");

    nmo_edit_report_dispose(&report);
    nmo_edit_plan_destroy(plan);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(edit_plan, stores_parameter_value_ops);
REGISTER_TEST(edit_plan, stores_full_script_edit_ops_and_clones_plan);
REGISTER_TEST(edit_plan, report_dispose_releases_schema_v2_arrays);
REGISTER_TEST(edit_plan, report_owns_schema_v2_output_path);
REGISTER_TEST(edit_plan, executor_commits_parameter_value_plan);
REGISTER_TEST(edit_plan, executor_rolls_back_failed_plan);
REGISTER_TEST(edit_plan, executor_rolls_back_created_handle_chain_failure);
REGISTER_TEST(edit_plan, executor_dry_run_reports_without_persisting);
REGISTER_TEST(edit_plan, executor_adds_node_with_created_object_report);
REGISTER_TEST(edit_plan, executor_materializes_building_block_defaults);
REGISTER_TEST(edit_plan, executor_materializes_targetable_beobject_target);
REGISTER_TEST(edit_plan, executor_resolves_parameter_value_from_prior_handle);
REGISTER_TEST(edit_plan, executor_materializes_input_source_for_handle_value);
REGISTER_TEST(edit_plan, executor_materializes_input_source_for_handle_bytes);
REGISTER_TEST(edit_plan, executor_connects_parameter_to_prior_node_handle);
REGISTER_TEST(edit_plan, executor_resolves_behavior_link_io_handles);
REGISTER_TEST(edit_plan, executor_reports_add_operation_slot_parameter_impact);
REGISTER_TEST(edit_plan, executor_reports_rewire_operation_slot_parameter_impact);
REGISTER_TEST(edit_plan, executor_runs_script_ops_and_records_validation);
REGISTER_TEST(edit_plan, executor_replaces_leaf_bb_in_transaction);
REGISTER_TEST(edit_plan, executor_replace_bb_dry_run_rolls_back);
REGISTER_TEST(edit_plan, executor_folds_closed_graph_in_transaction);
REGISTER_TEST(edit_plan, executor_fold_dry_run_rolls_back);
REGISTER_TEST(edit_plan, executor_fold_dry_run_reports_semantic_risks);
REGISTER_TEST(edit_plan, report_semantic_risk_merge_deduplicates);
REGISTER_TEST(edit_plan, executor_merges_edit_plan_semantic_validation);
REGISTER_TEST(edit_plan, executor_reports_generic_activation_delay_risk);
REGISTER_TEST(edit_plan, executor_fold_failure_reports_operation_diagnostic);
TEST_MAIN_END()
