#include "test_framework.h"

#include "behavior/nmo_semantic_validator.h"
#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_script_edit.h"
#include "document/nmo_document.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_statesave_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_guids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_guids.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"

#include <string.h>

typedef struct semantic_fixture {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
} semantic_fixture_t;

static void semantic_fixture_init(semantic_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->session =
        nmo_session_load(fixture->ctx, NMO_TEST_DATA_FILE("Ballance/base.cmo"));
    ASSERT_NOT_NULL(fixture->session);
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(fixture->session,
                                                  &fixture->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document,
                                           &fixture->workspace));
}

static void semantic_fixture_init_path(semantic_fixture_t *fixture,
                                       const char *path)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->session = nmo_session_load(fixture->ctx, path);
    ASSERT_NOT_NULL(fixture->session);
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(fixture->session,
                                                  &fixture->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document,
                                           &fixture->workspace));
}

static void semantic_fixture_init_empty(semantic_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->ctx = nmo_context_create(
        &(nmo_context_desc_t){.data_dir = NMO_TEST_DATA_DIR});
    ASSERT_NOT_NULL(fixture->ctx);
    fixture->session = nmo_session_create(fixture->ctx);
    ASSERT_NOT_NULL(fixture->session);
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(fixture->session,
                                                  &fixture->document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(fixture->ctx, fixture->document,
                                           &fixture->workspace));
}

static void semantic_fixture_dispose(semantic_fixture_t *fixture)
{
    if (fixture->workspace) {
        nmo_workspace_destroy(fixture->workspace);
    }
    if (fixture->document) {
        nmo_document_destroy(fixture->document);
    }
    if (fixture->session) {
        nmo_session_close_with_context(fixture->ctx, fixture->session);
        fixture->ctx = NULL;
    }
    if (fixture->ctx) {
        nmo_context_release(fixture->ctx);
    }
    memset(fixture, 0, sizeof(*fixture));
}

static void semantic_create_object(
    semantic_fixture_t *fixture,
    nmo_class_id_t class_id,
    const char *name,
    nmo_object_id_t *out_id)
{
    ASSERT_NOT_NULL(out_id);
    *out_id = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_session_create_object(
                  fixture->session,
                  class_id,
                  name,
                  NMO_GUID_NULL,
                  out_id,
                  NULL));
    ASSERT_TRUE(*out_id != 0u);
}

static const nmo_behavior_semantic_risk_t *find_risk(
    const nmo_behavior_semantic_risk_t *risks,
    size_t risk_count,
    const char *code)
{
    for (size_t i = 0; i < risk_count; ++i) {
        if (risks[i].code && strcmp(risks[i].code, code) == 0) {
            return &risks[i];
        }
    }
    return NULL;
}

TEST(semantic_validator, boundary_reports_dangling_delay_and_shared_risks)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_behavior_boundary_control_edge_t control_edge = {
        .link_id = 1001u,
        .activation_delay = 2,
    };
    nmo_behavior_boundary_parameter_edge_t parameter_edge = {
        .source_parameter_id = 2001u,
        .target_parameter_id = 2002u,
        .shared = true,
    };
    nmo_behavior_boundary_t boundary = {
        .behavior_id = 3001u,
        .broken_links = 1u,
        .control_in = &control_edge,
        .control_in_count = 1u,
        .parameter_out = &parameter_edge,
        .parameter_out_count = 1u,
    };

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_boundary(
                  fixture.workspace, &boundary, NULL, 0u, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *dangling =
        find_risk(risks, risk_count, "dangling_boundary");
    ASSERT_NOT_NULL(dangling);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, dangling->severity);
    ASSERT_EQ(3001u, dangling->object_id);

    const nmo_behavior_semantic_risk_t *delay =
        find_risk(risks, risk_count, "activation_delay");
    ASSERT_NOT_NULL(delay);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_WARN, delay->severity);
    ASSERT_EQ(1001u, delay->object_id);

    const nmo_behavior_semantic_risk_t *shared =
        find_risk(risks, risk_count, "shared_parameter");
    ASSERT_NOT_NULL(shared);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_WARN, shared->severity);
    ASSERT_EQ(2002u, shared->object_id);

    nmo_semantic_risks_free(risks);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, detects_message_flow_by_signature_metadata)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_behavior_boundary_t boundary = {
        .behavior_id = 2364u,
    };
    nmo_object_id_t selected_nodes[] = {2233u};
    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_boundary(
                  fixture.workspace,
                  &boundary,
                  selected_nodes,
                  sizeof(selected_nodes) / sizeof(selected_nodes[0]),
                  &risks,
                  &risk_count));

    const nmo_behavior_semantic_risk_t *message =
        find_risk(risks, risk_count, "message_flow");
    ASSERT_NOT_NULL(message);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_WARN, message->severity);
    ASSERT_EQ(2233u, message->object_id);

    nmo_semantic_risks_free(risks);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, detects_missing_symbolic_message_manager_entry)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_empty(&fixture);

    nmo_object_id_t root_id = 0u;
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Root", &root_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  root_id,
                  nmo_guid_parse("A20E8D5B-DF002150"),
                  "Send Message"));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *risk =
        find_risk(risks, risk_count, "missing_manager_entry");
    ASSERT_NOT_NULL(risk);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, risk->severity);
    ASSERT_EQ(root_id, risk->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_rejects_missing_replace_target)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));

    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 0x00FFFFFFu,
        .block_guid = nmo_guid_parse("42414C02-10000002"),
        .name = "Missing Replace Target",
        .block_version = 65536u,
    };
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_NE(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));
    ASSERT_EQ(0u, risk_count);
    ASSERT_TRUE(risks == NULL);

    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_generic_op_risks)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(plan, 6u, 5u, 2u, 5u));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_connect_parameter(plan, 0x00FFFFFEu, 0x00FFFFFFu));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    ASSERT_NOT_NULL(find_risk(risks, risk_count, "activation_delay"));
    const nmo_behavior_semantic_risk_t *dangling =
        find_risk(risks, risk_count, "dangling_reference");
    ASSERT_NOT_NULL(dangling);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, dangling->severity);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_invalid_handle_reference)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan,
                  6u,
                  NMO_SCRIPT_EDIT_IO_INPUT,
                  "Entry"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link_from_handles(
                  plan,
                  6u,
                  0u,
                  "missing_io",
                  0u,
                  "io",
                  0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *invalid =
        find_risk(risks, risk_count, "invalid_handle_reference");
    ASSERT_NOT_NULL(invalid);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, invalid->severity);
    ASSERT_EQ(6u, invalid->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_rejects_missing_add_node_child_handle)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_path(&fixture, NMO_TEST_DATA_FILE("Nop.cmo"));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  6u,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Handle Checked 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value_from_handle(
                  plan,
                  0u,
                  "input_param:Missing",
                  "trace",
                  NULL));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *invalid =
        find_risk(risks, risk_count, "invalid_handle_reference");
    ASSERT_NOT_NULL(invalid);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, invalid->severity);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_rejects_parameter_handle_as_control_endpoint)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_path(&fixture, NMO_TEST_DATA_FILE("Nop.cmo"));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  6u,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Control Handle 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link_from_handles(
                  plan,
                  6u,
                  0u,
                  "input_param:Text",
                  0u,
                  "input:On",
                  0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "control_endpoint_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_rejects_control_handle_as_parameter_reference)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_path(&fixture, NMO_TEST_DATA_FILE("Nop.cmo"));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  6u,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Parameter Handle 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value_from_handle(
                  plan,
                  0u,
                  "input:On",
                  "trace",
                  NULL));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "parameter_object_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_behavior_owner_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t parameter_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not Behavior", &parameter_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_io(
                  plan,
                  parameter_id,
                  NMO_SCRIPT_EDIT_IO_INPUT,
                  "Entry"));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "behavior_owner_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(parameter_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_behavior_io_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t parameter_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not Behavior IO", &parameter_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_rename_io(plan, parameter_id, "Renamed"));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "behavior_io_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(parameter_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_behavior_node_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t root_id = 0u;
    nmo_object_id_t parameter_id = 0u;
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Root", &root_id);
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not Behavior Node", &parameter_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_remove_node(plan, root_id, parameter_id, 0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "behavior_node_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(parameter_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_control_endpoint_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t root_id = 0u;
    nmo_object_id_t parameter_id = 0u;
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Root", &root_id);
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not IO", &parameter_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(
                  plan,
                  root_id,
                  parameter_id,
                  parameter_id,
                  0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "control_endpoint_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(parameter_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_control_link_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t parameter_id = 0u;
    nmo_object_id_t io_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not Link", &parameter_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORIO, "Endpoint", &io_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_behavior_link(
                  plan,
                  parameter_id,
                  io_id,
                  0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "control_link_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(parameter_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_nested_control_endpoint_scope)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t root_id = 0u;
    nmo_object_id_t child_id = 0u;
    nmo_object_id_t grandchild_id = 0u;
    nmo_object_id_t root_io_id = 0u;
    nmo_object_id_t nested_io_id = 0u;
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Root", &root_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Child", &child_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Grandchild", &grandchild_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORIO, "Root IO", &root_io_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORIO, "Nested IO", &nested_io_id);

    nmo_object_repository_t *repo = nmo_session_get_repository(fixture.session);
    nmo_object_t *root_obj = nmo_object_repository_find_by_id(repo, root_id);
    nmo_object_t *child_obj = nmo_object_repository_find_by_id(repo, child_id);
    nmo_object_t *grandchild_obj = nmo_object_repository_find_by_id(repo, grandchild_id);
    ASSERT_NOT_NULL(root_obj);
    ASSERT_NOT_NULL(child_obj);
    ASSERT_NOT_NULL(grandchild_obj);
    nmo_behavior_state_t *root_state = (nmo_behavior_state_t *)nmo_object_get_state(root_obj);
    nmo_behavior_state_t *child_state = (nmo_behavior_state_t *)nmo_object_get_state(child_obj);
    nmo_behavior_state_t *grandchild_state = (nmo_behavior_state_t *)nmo_object_get_state(grandchild_obj);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(grandchild_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&root_state->sub_behaviors, &child_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&child_state->sub_behaviors, &grandchild_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&root_state->inputs, &root_io_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&grandchild_state->inputs, &nested_io_id));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(
                  plan,
                  root_id,
                  root_io_id,
                  nested_io_id,
                  0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *scope =
        find_risk(risks, risk_count, "control_endpoint_scope_mismatch");
    ASSERT_NOT_NULL(scope);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, scope->severity);
    ASSERT_EQ(nested_io_id, scope->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_unowned_control_endpoint)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t root_id = 0u;
    nmo_object_id_t root_io_id = 0u;
    nmo_object_id_t unowned_io_id = 0u;
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Root", &root_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORIO, "Root IO", &root_io_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORIO, "Unowned IO", &unowned_io_id);

    nmo_object_repository_t *repo = nmo_session_get_repository(fixture.session);
    nmo_object_t *root_obj = nmo_object_repository_find_by_id(repo, root_id);
    ASSERT_NOT_NULL(root_obj);
    nmo_behavior_state_t *root_state = (nmo_behavior_state_t *)nmo_object_get_state(root_obj);
    ASSERT_NOT_NULL(root_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&root_state->inputs, &root_io_id));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_behavior_link(
                  plan,
                  root_id,
                  root_io_id,
                  unowned_io_id,
                  0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *dangling =
        find_risk(risks, risk_count, "dangling_control_link");
    ASSERT_NOT_NULL(dangling);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, dangling->severity);
    ASSERT_EQ(unowned_io_id, dangling->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_rewire_control_endpoint_scope)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t root_id = 0u;
    nmo_object_id_t child_id = 0u;
    nmo_object_id_t grandchild_id = 0u;
    nmo_object_id_t root_io_id = 0u;
    nmo_object_id_t child_io_id = 0u;
    nmo_object_id_t nested_io_id = 0u;
    nmo_object_id_t link_id = 0u;
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Root", &root_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Child", &child_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Grandchild", &grandchild_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORIO, "Root IO", &root_io_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORIO, "Child IO", &child_io_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORIO, "Nested IO", &nested_io_id);
    semantic_create_object(&fixture, NMO_CID_BEHAVIORLINK, "Link", &link_id);

    nmo_object_repository_t *repo = nmo_session_get_repository(fixture.session);
    nmo_object_t *root_obj = nmo_object_repository_find_by_id(repo, root_id);
    nmo_object_t *child_obj = nmo_object_repository_find_by_id(repo, child_id);
    nmo_object_t *grandchild_obj = nmo_object_repository_find_by_id(repo, grandchild_id);
    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    ASSERT_NOT_NULL(root_obj);
    ASSERT_NOT_NULL(child_obj);
    ASSERT_NOT_NULL(grandchild_obj);
    ASSERT_NOT_NULL(link_obj);
    nmo_behavior_state_t *root_state = (nmo_behavior_state_t *)nmo_object_get_state(root_obj);
    nmo_behavior_state_t *child_state = (nmo_behavior_state_t *)nmo_object_get_state(child_obj);
    nmo_behavior_state_t *grandchild_state = (nmo_behavior_state_t *)nmo_object_get_state(grandchild_obj);
    nmo_behaviorlink_state_t *link_state = (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    ASSERT_NOT_NULL(root_state);
    ASSERT_NOT_NULL(child_state);
    ASSERT_NOT_NULL(grandchild_state);
    ASSERT_NOT_NULL(link_state);
    ASSERT_EQ(NMO_OK, nmo_array_append(&root_state->sub_behaviors, &child_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&child_state->sub_behaviors, &grandchild_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&root_state->inputs, &root_io_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&child_state->inputs, &child_io_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&grandchild_state->inputs, &nested_io_id));
    ASSERT_EQ(NMO_OK, nmo_array_append(&root_state->sub_behavior_links, &link_id));
    link_state->in_io_id = child_io_id;
    link_state->out_io_id = root_io_id;

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_behavior_link(
                  plan,
                  link_id,
                  root_io_id,
                  nested_io_id));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *scope =
        find_risk(risks, risk_count, "control_endpoint_scope_mismatch");
    ASSERT_NOT_NULL(scope);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, scope->severity);
    ASSERT_EQ(nested_io_id, scope->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_parameter_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_connect_parameter(plan, 13u, 7u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "parameter_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(7u, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_parameter_type_mismatch_with_target_handle)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  6u,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Parameter Target 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_connect_parameter_to_handle(
                  plan,
                  13u,
                  0u,
                  "input_param:Text"));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "parameter_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_parameter_object_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t behavior_id = 0u;
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Not Parameter", &behavior_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_disconnect_parameter(plan, behavior_id));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "parameter_object_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(behavior_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_parameter_target_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t source_id = 0u;
    nmo_object_id_t target_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Source", &source_id);
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not ParameterIn", &target_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_connect_parameter(plan, source_id, target_id));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "parameter_target_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(target_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_disconnect_parameter_target_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t target_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not ParameterIn", &target_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_disconnect_parameter(plan, target_id));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "parameter_target_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(target_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_value_parameter_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t behavior_id = 0u;
    semantic_create_object(&fixture, NMO_CID_BEHAVIOR, "Not Value Parameter", &behavior_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan,
                  behavior_id,
                  "value",
                  NULL));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "parameter_object_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(behavior_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_scene_sensitive_parameter_ref)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t scene_parameter_id = 0u;
    semantic_create_object(
        &fixture, NMO_CID_PARAMETER, "Scene Sensitive Parameter",
        &scene_parameter_id);

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    ASSERT_NOT_NULL(repo);
    nmo_object_t *parameter_obj =
        nmo_object_repository_find_by_id(repo, scene_parameter_id);
    ASSERT_NOT_NULL(parameter_obj);
    nmo_parameter_state_t *parameter_state =
        (nmo_parameter_state_t *)nmo_object_get_state(parameter_obj);
    ASSERT_NOT_NULL(parameter_state);
    parameter_state->type_guid = CKPGUID_SCENE;

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_set_parameter_value(
                  plan,
                  scene_parameter_id,
                  "Current Scene",
                  NULL));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *scene =
        find_risk(risks, risk_count, "scene_sensitive_reference");
    ASSERT_NOT_NULL(scene);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_WARN, scene->severity);
    ASSERT_EQ(scene_parameter_id, scene->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_operation_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_operation(
                  plan,
                  6u,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  13u,
                  0u,
                  7u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "operation_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(6u, mismatch->object_id);
    const nmo_behavior_semantic_risk_t *signature =
        find_risk(risks, risk_count, "operation_signature_mismatch");
    ASSERT_NOT_NULL(signature);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, signature->severity);
    ASSERT_EQ(6u, signature->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_operation_type_mismatch_with_handle_refs)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan,
                  6u,
                  NMO_SCRIPT_EDIT_PARAM_IN,
                  CKPGUID_FLOAT,
                  "Handle Float"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_operation_with_refs(
                  plan,
                  6u,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  0u,
                  0u,
                  "parameter",
                  0u,
                  0u,
                  NULL,
                  7u,
                  0u,
                  NULL));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "operation_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(6u, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_operation_type_mismatch_with_node_param_handle)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_path(&fixture, NMO_TEST_DATA_FILE("Nop.cmo"));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  6u,
                  nmo_guid_parse("055B29FE-662D5CA0"),
                  "Operation Source 2D Text"));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_operation_with_refs(
                  plan,
                  6u,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  0u,
                  0u,
                  "input_param:Text",
                  0u,
                  0u,
                  NULL,
                  0u,
                  0u,
                  NULL));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "operation_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_operation_parent_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t parameter_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not Operation Parent", &parameter_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_operation(
                  plan,
                  parameter_id,
                  nmo_guid_parse("33CC6B49-3589282B"),
                  0u,
                  0u,
                  0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "behavior_owner_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(parameter_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_operation_object_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t parameter_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not Operation", &parameter_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_remove_operation(plan, parameter_id));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "operation_object_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(parameter_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_rewire_operation_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t string_parameter_id = 0u;
    nmo_object_id_t operation_id = 0u;
    semantic_create_object(
        &fixture, NMO_CID_PARAMETER, "String Operand", &string_parameter_id);
    semantic_create_object(
        &fixture, NMO_CID_PARAMETEROPERATION, "Add Operation", &operation_id);

    nmo_object_t *parameter_obj =
        nmo_object_repository_find_by_id(repo, string_parameter_id);
    nmo_object_t *operation_obj =
        nmo_object_repository_find_by_id(repo, operation_id);
    ASSERT_NOT_NULL(parameter_obj);
    ASSERT_NOT_NULL(operation_obj);
    nmo_parameter_state_t *parameter_state =
        (nmo_parameter_state_t *)nmo_object_get_state(parameter_obj);
    nmo_parameteroperation_state_t *operation_state =
        (nmo_parameteroperation_state_t *)nmo_object_get_state(operation_obj);
    ASSERT_NOT_NULL(parameter_state);
    ASSERT_NOT_NULL(operation_state);
    parameter_state->type_guid = CKPGUID_STRING;
    operation_state->operation_guid = NMO_OP_GUID_ADD;

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_operation(
                  plan,
                  operation_id,
                  NMO_SCRIPT_EDIT_OP_SLOT_IN1,
                  string_parameter_id,
                  0u,
                  0u));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "operation_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(operation_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_rewire_operation_existing_slot_dangling)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t operation_id = 0u;
    semantic_create_object(
        &fixture, NMO_CID_PARAMETEROPERATION, "Dangling Slot Operation",
        &operation_id);
    nmo_object_t *operation_obj =
        nmo_object_repository_find_by_id(repo, operation_id);
    ASSERT_NOT_NULL(operation_obj);
    nmo_parameteroperation_state_t *operation_state =
        (nmo_parameteroperation_state_t *)nmo_object_get_state(operation_obj);
    ASSERT_NOT_NULL(operation_state);
    operation_state->operation_guid = NMO_OP_GUID_ADD;
    operation_state->has_in1 = 1u;
    operation_state->in1_id = 999999u;

    nmo_object_id_t out_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Out", &out_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_rewire_operation(
                  plan,
                  operation_id,
                  NMO_SCRIPT_EDIT_OP_SLOT_OUT,
                  0u,
                  0u,
                  out_id));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *dangling =
        find_risk(risks, risk_count, "operation_slot_dangling_reference");
    ASSERT_NOT_NULL(dangling);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, dangling->severity);
    ASSERT_EQ(999999u, dangling->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_rewire_operation_type_mismatch_with_handle_refs)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_path(&fixture, NMO_TEST_DATA_FILE("Nop.cmo"));

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t operation_id = 0u;
    semantic_create_object(
        &fixture, NMO_CID_PARAMETEROPERATION, "Add Operation", &operation_id);
    nmo_object_t *operation_obj =
        nmo_object_repository_find_by_id(repo, operation_id);
    ASSERT_NOT_NULL(operation_obj);
    nmo_parameteroperation_state_t *operation_state =
        (nmo_parameteroperation_state_t *)nmo_object_get_state(operation_obj);
    ASSERT_NOT_NULL(operation_state);
    operation_state->operation_guid = NMO_OP_GUID_ADD;

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_parameter(
                  plan,
                  6u,
                  NMO_SCRIPT_EDIT_PARAM_LOCAL,
                  CKPGUID_STRING,
                  "Handle String"));
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

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "operation_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(operation_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_data_cell_bounds)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_data_cell(plan, 6093u, 999999u, 1u, "x"));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *bounds =
        find_risk(risks, risk_count, "data_cell_bounds");
    ASSERT_NOT_NULL(bounds);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, bounds->severity);
    ASSERT_EQ(6093u, bounds->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_data_cell_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_path(&fixture, NMO_TEST_DATA_FILE("Ballance/Balls.nmo"));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_data_cell(plan, 2261u, 0u, 1u, "not-a-float"));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "data_cell_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(2261u, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_unknown_building_block)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_node(
                  plan,
                  6u,
                  nmo_guid_parse("DEADBEEF-10000000"),
                  "Unknown BB"));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *unknown =
        find_risk(risks, risk_count, "unknown_bb_signature");
    ASSERT_NOT_NULL(unknown);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, unknown->severity);
    ASSERT_EQ(6u, unknown->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_unknown_replace_building_block)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_path(&fixture, NMO_TEST_DATA_FILE("Nop.cmo"));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = 6u,
        .block_guid = nmo_guid_parse("4E4D4F00-00BAD0BB"),
        .name = "Unknown Replace BB",
        .block_version = 65536u,
    };
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *unknown =
        find_risk(risks, risk_count, "unknown_bb_signature");
    ASSERT_NOT_NULL(unknown);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, unknown->severity);
    ASSERT_EQ(6u, unknown->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_targetable_behavior_missing_target)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t behavior_id = 0u;
    semantic_create_object(
        &fixture, NMO_CID_BEHAVIOR, "Targetable Without Target", &behavior_id);
    nmo_object_t *behavior_obj =
        nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    nmo_behavior_state_t *behavior_state =
        (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj);
    ASSERT_NOT_NULL(behavior_state);
    behavior_state->flags |= CKBEHAVIOR_TARGETABLE;
    behavior_state->target_parameter_id = 0u;

    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = behavior_id,
        .block_guid = nmo_guid_parse("055B29FE-662D5CA0"),
        .name = "Replacement 2D Text",
        .block_version = 65536u,
    };

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *missing =
        find_risk(risks, risk_count, "target_parameter_missing");
    ASSERT_NOT_NULL(missing);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, missing->severity);
    ASSERT_EQ(behavior_id, missing->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_target_parameter_class_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t behavior_id = 0u;
    nmo_object_id_t target_id = 0u;
    semantic_create_object(
        &fixture, NMO_CID_BEHAVIOR, "Targetable Wrong Target Type",
        &behavior_id);
    semantic_create_object(
        &fixture, NMO_CID_PARAMETERIN, "Wrong Target", &target_id);

    nmo_object_t *behavior_obj =
        nmo_object_repository_find_by_id(repo, behavior_id);
    nmo_object_t *target_obj =
        nmo_object_repository_find_by_id(repo, target_id);
    ASSERT_NOT_NULL(behavior_obj);
    ASSERT_NOT_NULL(target_obj);

    nmo_behavior_state_t *behavior_state =
        (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj);
    nmo_parameterin_state_t *target_state =
        (nmo_parameterin_state_t *)nmo_object_get_state(target_obj);
    ASSERT_NOT_NULL(behavior_state);
    ASSERT_NOT_NULL(target_state);

    behavior_state->flags |= CKBEHAVIOR_TARGETABLE;
    behavior_state->compatible_class_id = NMO_CID_2DENTITY;
    behavior_state->target_parameter_id = target_id;
    target_state->type_guid = CKPGUID_STRING;

    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = behavior_id,
        .block_guid = nmo_guid_parse("055B29FE-662D5CA0"),
        .name = "Replacement 2D Text",
        .block_version = 65536u,
    };

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "target_parameter_class_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(target_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_prototype_save_flags_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_repository_t *repo =
        nmo_session_get_repository(fixture.session);
    ASSERT_NOT_NULL(repo);

    nmo_object_id_t behavior_id = 0u;
    semantic_create_object(
        &fixture, NMO_CID_BEHAVIOR, "BB Save Flags Mismatch",
        &behavior_id);
    nmo_object_t *behavior_obj =
        nmo_object_repository_find_by_id(repo, behavior_id);
    ASSERT_NOT_NULL(behavior_obj);
    nmo_behavior_state_t *behavior_state =
        (nmo_behavior_state_t *)nmo_object_get_state(behavior_obj);
    ASSERT_NOT_NULL(behavior_state);
    behavior_state->flags |= CKBEHAVIOR_BUILDINGBLOCK;
    behavior_state->block_guid = nmo_guid_parse("055B29FE-662D5CA0");
    behavior_state->has_save_flags = true;
    behavior_state->save_flags = CK_STATESAVE_BEHAVIORFLAGS;

    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = behavior_id,
        .block_guid = nmo_guid_parse("055B29FE-662D5CA0"),
        .name = "Replacement 2D Text",
        .block_version = 65536u,
    };

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "prototype_save_flags_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_WARN, mismatch->severity);
    ASSERT_EQ(behavior_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_replace_target_type_mismatch)
{
    semantic_fixture_t fixture;
    semantic_fixture_init(&fixture);

    nmo_object_id_t parameter_id = 0u;
    semantic_create_object(&fixture, NMO_CID_PARAMETER, "Not Replace Target", &parameter_id);

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    nmo_behavior_replace_bb_desc_t replace = {
        .behavior_id = parameter_id,
        .block_guid = nmo_guid_parse("055B29FE-662D5CA0"),
        .name = "Replace Non Behavior",
        .block_version = 65536u,
    };
    ASSERT_EQ(NMO_OK, nmo_edit_plan_add_replace_bb(plan, &replace));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *mismatch =
        find_risk(risks, risk_count, "behavior_owner_type_mismatch");
    ASSERT_NOT_NULL(mismatch);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_REJECT, mismatch->severity);
    ASSERT_EQ(parameter_id, mismatch->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST(semantic_validator, edit_plan_reports_interface_policy_risk)
{
    semantic_fixture_t fixture;
    semantic_fixture_init_path(
        &fixture,
        NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo"));

    nmo_edit_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_edit_plan_create(&plan));
    ASSERT_EQ(NMO_OK,
              nmo_edit_plan_add_interface_policy(
                  plan,
                  253u,
                  NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE));

    nmo_behavior_semantic_risk_t *risks = NULL;
    size_t risk_count = 0u;
    ASSERT_EQ(NMO_OK,
              nmo_semantic_validate_edit_plan(
                  fixture.workspace, plan, &risks, &risk_count));

    const nmo_behavior_semantic_risk_t *interface_risk =
        find_risk(risks, risk_count, "interface_chunk_policy");
    ASSERT_NOT_NULL(interface_risk);
    ASSERT_EQ(NMO_BEHAVIOR_SEMANTIC_RISK_WARN, interface_risk->severity);
    ASSERT_EQ(253u, interface_risk->object_id);

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(semantic_validator, boundary_reports_dangling_delay_and_shared_risks);
    REGISTER_TEST(semantic_validator, detects_message_flow_by_signature_metadata);
    REGISTER_TEST(semantic_validator, detects_missing_symbolic_message_manager_entry);
    REGISTER_TEST(semantic_validator, edit_plan_rejects_missing_replace_target);
    REGISTER_TEST(semantic_validator, edit_plan_reports_generic_op_risks);
    REGISTER_TEST(semantic_validator, edit_plan_reports_invalid_handle_reference);
    REGISTER_TEST(semantic_validator, edit_plan_rejects_missing_add_node_child_handle);
    REGISTER_TEST(semantic_validator, edit_plan_rejects_parameter_handle_as_control_endpoint);
    REGISTER_TEST(semantic_validator, edit_plan_rejects_control_handle_as_parameter_reference);
    REGISTER_TEST(semantic_validator, edit_plan_reports_behavior_owner_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_behavior_io_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_behavior_node_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_control_endpoint_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_control_link_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_nested_control_endpoint_scope);
    REGISTER_TEST(semantic_validator, edit_plan_reports_unowned_control_endpoint);
    REGISTER_TEST(semantic_validator, edit_plan_reports_rewire_control_endpoint_scope);
    REGISTER_TEST(semantic_validator, edit_plan_reports_parameter_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_parameter_type_mismatch_with_target_handle);
    REGISTER_TEST(semantic_validator, edit_plan_reports_parameter_object_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_parameter_target_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_disconnect_parameter_target_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_value_parameter_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_scene_sensitive_parameter_ref);
    REGISTER_TEST(semantic_validator, edit_plan_reports_operation_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_operation_type_mismatch_with_handle_refs);
    REGISTER_TEST(semantic_validator, edit_plan_reports_operation_type_mismatch_with_node_param_handle);
    REGISTER_TEST(semantic_validator, edit_plan_reports_operation_parent_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_operation_object_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_rewire_operation_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_rewire_operation_existing_slot_dangling);
    REGISTER_TEST(semantic_validator, edit_plan_reports_rewire_operation_type_mismatch_with_handle_refs);
    REGISTER_TEST(semantic_validator, edit_plan_reports_data_cell_bounds);
    REGISTER_TEST(semantic_validator, edit_plan_reports_data_cell_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_unknown_building_block);
    REGISTER_TEST(semantic_validator, edit_plan_reports_unknown_replace_building_block);
    REGISTER_TEST(semantic_validator, edit_plan_reports_targetable_behavior_missing_target);
    REGISTER_TEST(semantic_validator, edit_plan_reports_target_parameter_class_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_prototype_save_flags_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_replace_target_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_interface_policy_risk);
TEST_MAIN_END()
