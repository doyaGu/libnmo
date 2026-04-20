/**
 * @file test_behavior_rewrite.c
 * @brief Unit tests for behavior rewrite planning APIs
 */

#include "../test_framework.h"

#include "behavior/nmo_behavior_rewrite.h"
#include "core/nmo_guid.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_util.h"

#include <stdint.h>

static bool open_test_file(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session)
{
    char errbuf[256] = {0};
    return nmo_session_open_file_with_context(path, out_ctx, out_session,
                                              errbuf, sizeof(errbuf));
}

TEST(beh_rewrite, fold_analyze_reports_selected_boundary_plan)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = nmo_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_TRUE(report.analysis_only);
    ASSERT_FALSE(report.can_write);
    ASSERT_EQ(1u, report.write_blocker_count);
    ASSERT_NOT_NULL(report.write_blockers);
    ASSERT_STR_EQ("analysis_only", report.write_blockers[0].code);
    ASSERT_NOT_NULL(report.write_blockers[0].message);
    ASSERT_EQ(4692u, report.parent_id);
    ASSERT_EQ(2364u, report.representative_id);
    ASSERT_EQ(2u, report.selected_node_count);
    ASSERT_EQ(2u, report.boundary.internal_node_count);
    ASSERT_EQ(1u, report.control_links_to_delete_count);
    ASSERT_EQ(2357u, report.control_links_to_delete[0].link_id);
    ASSERT_EQ(1u, report.nodes_to_delete_count);
    ASSERT_EQ(2208u, report.nodes_to_delete[0]);
    ASSERT_TRUE(report.boundary.control_in_count > 0);
    ASSERT_TRUE(report.boundary.control_out_count > 0);

    nmo_behavior_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(beh_rewrite, fold_analyze_reports_selected_boundary_plan);
TEST_MAIN_END()
