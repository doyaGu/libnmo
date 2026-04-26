#include "test_framework.h"

#include "behavior/nmo_semantic_validator.h"
#include "behavior/nmo_edit_plan.h"
#include "document/nmo_document.h"
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

    nmo_semantic_risks_free(risks);
    nmo_edit_plan_destroy(plan);
    semantic_fixture_dispose(&fixture);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(semantic_validator, boundary_reports_dangling_delay_and_shared_risks);
    REGISTER_TEST(semantic_validator, detects_message_flow_by_signature_metadata);
    REGISTER_TEST(semantic_validator, edit_plan_rejects_missing_replace_target);
    REGISTER_TEST(semantic_validator, edit_plan_reports_generic_op_risks);
    REGISTER_TEST(semantic_validator, edit_plan_reports_parameter_type_mismatch);
    REGISTER_TEST(semantic_validator, edit_plan_reports_operation_type_mismatch);
TEST_MAIN_END()
