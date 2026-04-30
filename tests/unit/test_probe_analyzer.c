#include "test_framework.h"

#include "behavior/nmo_probe_analyzer.h"
#include "document/nmo_document.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"

#include <string.h>

typedef struct probe_fixture {
    nmo_context_t *ctx;
    nmo_session_t *session;
    nmo_document_t *document;
    nmo_workspace_t *workspace;
} probe_fixture_t;

static void probe_fixture_init(probe_fixture_t *fixture)
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

static void probe_fixture_dispose(probe_fixture_t *fixture)
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

TEST(probe_analyzer, selects_unique_message_candidate_and_link)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_MESSAGE;
    request.behavior_id = 2172u;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_OK,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_AUTO, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_SELECTED, result.status);
    ASSERT_EQ(1667u, result.selected_node_id);
    ASSERT_EQ(2152u, result.selected_link_id);
    ASSERT_EQ(1u, result.candidate_count);
    ASSERT_EQ(1667u, result.candidates[0].node_id);
    ASSERT_EQ(2172u, result.candidates[0].parent_id);
    ASSERT_STR_EQ("sender", result.candidates[0].role);
    ASSERT_TRUE(result.from_io_id != 0u);
    ASSERT_TRUE(result.to_io_id != 0u);

    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, resolves_explicit_operation_write_site)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = 3798u;
    request.write_operation_id = 3791u;
    request.remove_link_id = 3780u;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_OK,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_SELECTED, result.status);
    ASSERT_EQ(3791u, result.selected_operation_id);
    ASSERT_EQ(3780u, result.selected_link_id);
    ASSERT_TRUE(result.from_io_id != 0u);
    ASSERT_TRUE(result.to_io_id != 0u);

    probe_fixture_dispose(&fixture);
}

TEST(probe_analyzer, rejects_operation_write_site_outside_boundary)
{
    probe_fixture_t fixture;
    probe_fixture_init(&fixture);

    nmo_probe_selector_request_t request;
    nmo_probe_selector_request_init(&request);
    request.kind = NMO_PROBE_SELECTOR_DATA_CELL_WRITE;
    request.behavior_id = 3880u;
    request.write_operation_id = 3791u;
    request.remove_link_id = 3874u;

    nmo_probe_selector_result_t result;
    nmo_probe_selector_result_init(&result);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_probe_analyze_selector(fixture.workspace, &request, &result));

    ASSERT_EQ(NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION, result.mode);
    ASSERT_EQ(NMO_PROBE_SELECTOR_STATUS_UNSAFE, result.status);
    ASSERT_STR_EQ("unsafe_probe_insertion", result.rejection_code);
    ASSERT_STR_CONTAINS(result.message,
                        "outside the selected behavior boundary");

    probe_fixture_dispose(&fixture);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(probe_analyzer, selects_unique_message_candidate_and_link);
    REGISTER_TEST(probe_analyzer, resolves_explicit_operation_write_site);
    REGISTER_TEST(probe_analyzer, rejects_operation_write_site_outside_boundary);
TEST_MAIN_END()
