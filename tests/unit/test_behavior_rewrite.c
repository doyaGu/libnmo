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

TEST(beh_rewrite, fold_write_rejects_until_supported)
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

    nmo_status_t rc = nmo_behavior_fold(ctx, session, &desc, &report);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, rc);
    ASSERT_FALSE(report.can_write);
    ASSERT_EQ(1u, report.write_blocker_count);
    ASSERT_STR_EQ("analysis_only", report.write_blockers[0].code);
    ASSERT_STR_EQ("analysis_only", report.diagnostic_code);
    ASSERT_NOT_NULL(report.diagnostic_message);

    nmo_behavior_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_uses_explicit_anchor)
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
        .anchor_id = 2208u,
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
    ASSERT_EQ(2208u, report.anchor_id);
    ASSERT_EQ(2208u, report.representative_id);
    ASSERT_EQ(1u, report.nodes_to_delete_count);
    ASSERT_EQ(2364u, report.nodes_to_delete[0]);

    nmo_behavior_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_preserve_boundary_enables_edges)
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
        .anchor_id = 2364u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_boundary = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = nmo_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_TRUE(report.preserve_boundary);
    ASSERT_TRUE(report.preserve_links);
    ASSERT_TRUE(report.preserve_params);

    nmo_behavior_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_reports_maps)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {2364u, 2208u};
    nmo_behavior_fold_map_t input_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_INPUT,
            .old_index = 2u,
            .new_index = 3u,
            .label = "In",
        },
    };
    nmo_behavior_fold_map_t output_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
            .old_index = 0u,
            .new_index = 1u,
            .label = "Out",
        },
    };
    nmo_behavior_fold_map_t parameter_maps[] = {
        {
            .kind = NMO_BEHAVIOR_FOLD_MAP_PARAMETER,
            .old_index = 4u,
            .new_index = 5u,
            .label = "Level",
        },
    };
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 2u,
        .anchor_id = 2364u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_boundary = true,
        .input_maps = input_maps,
        .input_map_count = 1u,
        .output_maps = output_maps,
        .output_map_count = 1u,
        .parameter_maps = parameter_maps,
        .parameter_map_count = 1u,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = nmo_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_EQ(1u, report.input_map_count);
    ASSERT_EQ(NMO_BEHAVIOR_FOLD_MAP_INPUT, report.input_maps[0].kind);
    ASSERT_EQ(2u, report.input_maps[0].old_index);
    ASSERT_EQ(3u, report.input_maps[0].new_index);
    ASSERT_STR_EQ("In", report.input_maps[0].label);
    ASSERT_EQ(1u, report.output_map_count);
    ASSERT_EQ(NMO_BEHAVIOR_FOLD_MAP_OUTPUT, report.output_maps[0].kind);
    ASSERT_EQ(0u, report.output_maps[0].old_index);
    ASSERT_EQ(1u, report.output_maps[0].new_index);
    ASSERT_STR_EQ("Out", report.output_maps[0].label);
    ASSERT_EQ(1u, report.parameter_map_count);
    ASSERT_EQ(NMO_BEHAVIOR_FOLD_MAP_PARAMETER, report.parameter_maps[0].kind);
    ASSERT_EQ(4u, report.parameter_maps[0].old_index);
    ASSERT_EQ(5u, report.parameter_maps[0].new_index);
    ASSERT_STR_EQ("Level", report.parameter_maps[0].label);

    nmo_behavior_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_rejects_anchor_outside_selection)
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
        .anchor_id = 2178u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Ballance Event Handler",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = nmo_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, rc);
    ASSERT_TRUE(report.rejected);
    ASSERT_STR_EQ("anchor_not_selected", report.diagnostic_code);
    ASSERT_EQ(2178u, report.anchor_id);

    nmo_behavior_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST(beh_rewrite, fold_analyze_rejects_parent_in_selected_nodes)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    nmo_object_id_t nodes[] = {4692u};
    nmo_behavior_fold_desc_t desc = {
        .parent_id = 4692u,
        .node_ids = nodes,
        .node_count = 1u,
        .block_guid = {0x42414C07u, 0x10000007u},
        .name = "Invalid Self Fold",
        .block_version = 65536u,
        .preserve_links = true,
        .preserve_params = true,
    };
    nmo_behavior_fold_report_t report = {0};

    nmo_status_t rc = nmo_behavior_fold_analyze(ctx, session, &desc,
                                                &report);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, rc);
    ASSERT_TRUE(report.rejected);
    ASSERT_STR_EQ("parent_selected", report.diagnostic_code);
    ASSERT_EQ(4692u, report.parent_id);

    nmo_behavior_fold_report_free(&report);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(beh_rewrite, fold_analyze_reports_selected_boundary_plan);
    REGISTER_TEST(beh_rewrite, fold_write_rejects_until_supported);
    REGISTER_TEST(beh_rewrite, fold_analyze_uses_explicit_anchor);
    REGISTER_TEST(beh_rewrite, fold_analyze_preserve_boundary_enables_edges);
    REGISTER_TEST(beh_rewrite, fold_analyze_reports_maps);
    REGISTER_TEST(beh_rewrite, fold_analyze_rejects_anchor_outside_selection);
    REGISTER_TEST(beh_rewrite, fold_analyze_rejects_parent_in_selected_nodes);
TEST_MAIN_END()
