/**
 * @file test_behavior_graph_recursive.c
 * @brief Tests for recursive behavior graph building
 */

#include "../test_framework.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_query.h"
#include "runtime/nmo_context.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "session/nmo_session_bridge.h"
#include "core/nmo_array.h"

#include <stdint.h>
#include <string.h>

static bool open_test_file(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session)
{
    char errbuf[256] = {0};
    return nmo_session_open_file_with_context(path, out_ctx, out_session,
                                              errbuf, sizeof(errbuf));
}

static nmo_status_t open_test_workspace(
    const char *path,
    nmo_context_t **out_ctx,
    nmo_session_t **out_session,
    nmo_document_t **out_document,
    nmo_workspace_t **out_workspace)
{
    if (!open_test_file(path, out_ctx, out_session)) {
        return NMO_ERR_CANT_OPEN_FILE;
    }
    nmo_status_t st = nmo_session_borrow_document(*out_session, out_document);
    if (st != NMO_OK) {
        nmo_session_close_with_context(*out_ctx, *out_session);
        *out_ctx = NULL;
        *out_session = NULL;
        return st;
    }
    st = nmo_workspace_create(*out_ctx, *out_document, out_workspace);
    if (st != NMO_OK) {
        nmo_document_destroy(*out_document);
        nmo_session_close_with_context(*out_ctx, *out_session);
        *out_document = NULL;
        *out_ctx = NULL;
        *out_session = NULL;
    }
    return st;
}

/* Find a script that has graph-type sub-behaviors (depth>1 produces more nodes) */
static nmo_object_id_t find_nested_graph_script(
    nmo_document_t *document,
    nmo_workspace_t *workspace)
{
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 32, NULL);
    if (nmo_behavior_query_collect_scripts(document, &scripts) != NMO_OK) {
        nmo_array_dispose(&scripts);
        return 0;
    }

    nmo_object_id_t result = 0;
    const nmo_behavior_script_view_t *entries =
        (const nmo_behavior_script_view_t *)scripts.data;

    for (size_t i = 0; i < scripts.count && result == 0; ++i) {
        nmo_behavior_graph_t g0 = {0}, g1 = {0};
        if (nmo_behavior_graph_build(workspace, entries[i].script_id, 0, &g0) &&
            nmo_behavior_graph_build(workspace, entries[i].script_id, 1, &g1)) {
            if (g1.node_count > g0.node_count)
                result = entries[i].script_id;
        }
        nmo_behavior_graph_free(&g0);
        nmo_behavior_graph_free(&g1);
    }

    nmo_array_dispose(&scripts);
    return result;
}

TEST(graph_rec, depth0_root_only)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (open_test_workspace(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                            &ctx, &session, &document, &workspace) != NMO_OK)
        return;
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 32, NULL);
    ASSERT_EQ(NMO_OK, nmo_behavior_query_collect_scripts(document, &scripts));
    ASSERT_TRUE(scripts.count > 0);

    const nmo_behavior_script_view_t *e = (const nmo_behavior_script_view_t *)scripts.data;
    nmo_behavior_graph_t g = {0};
    ASSERT_TRUE(nmo_behavior_graph_build(workspace, e[0].script_id, 0, &g));

    /* Should have nodes and edges */
    ASSERT_TRUE(g.node_count > 0);

    nmo_behavior_graph_free(&g);
    nmo_array_dispose(&scripts);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(graph_rec, depth1_has_more_nodes)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (open_test_workspace(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                            &ctx, &session, &document, &workspace) != NMO_OK)
        return;

    nmo_object_id_t script_id = find_nested_graph_script(document, workspace);
    if (script_id == 0) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_behavior_graph_t g0 = {0}, g1 = {0};
    ASSERT_TRUE(nmo_behavior_graph_build(workspace, script_id, 0, &g0));
    ASSERT_TRUE(nmo_behavior_graph_build(workspace, script_id, 1, &g1));

    ASSERT_TRUE(g1.node_count > g0.node_count);
    ASSERT_TRUE(g1.edge_count > g0.edge_count);

    nmo_behavior_graph_free(&g0);
    nmo_behavior_graph_free(&g1);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(graph_rec, depth_field_set)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (open_test_workspace(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                            &ctx, &session, &document, &workspace) != NMO_OK)
        return;

    nmo_object_id_t script_id = find_nested_graph_script(document, workspace);
    if (script_id == 0) {
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_behavior_graph_t g = {0};
    ASSERT_TRUE(nmo_behavior_graph_build(workspace, script_id, 8, &g));

    bool found_deeper = false;
    for (size_t i = 0; i < g.node_count; ++i) {
        if (g.nodes[i].depth > 0 && strcmp(g.nodes[i].kind, "behavior") == 0) {
            found_deeper = true;
            ASSERT_TRUE(g.nodes[i].parent_id != 0);
        }
    }
    ASSERT_TRUE(found_deeper);

    nmo_behavior_graph_free(&g);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST(graph_rec, unlimited_depth_no_crash)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (open_test_workspace(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                            &ctx, &session, &document, &workspace) != NMO_OK)
        return;

    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 32, NULL);
    ASSERT_EQ(NMO_OK, nmo_behavior_query_collect_scripts(document, &scripts));

    const nmo_behavior_script_view_t *entries =
        (const nmo_behavior_script_view_t *)scripts.data;

    for (size_t i = 0; i < scripts.count; ++i) {
        nmo_behavior_graph_t g = {0};
        ASSERT_TRUE(nmo_behavior_graph_build(
            workspace, entries[i].script_id, UINT32_MAX, &g));
        ASSERT_TRUE(g.node_count > 0);
        nmo_behavior_graph_free(&g);
    }

    nmo_array_dispose(&scripts);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(graph_rec, depth0_root_only);
    REGISTER_TEST(graph_rec, depth1_has_more_nodes);
    REGISTER_TEST(graph_rec, depth_field_set);
    REGISTER_TEST(graph_rec, unlimited_depth_no_crash);
TEST_MAIN_END()

