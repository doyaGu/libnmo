/**
 * @file test_behavior_graph_recursive.c
 * @brief Tests for recursive behavior graph building
 */

#include "../test_framework.h"
#include "behavior/nmo_behavior_graph.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_util.h"
#include "behavior/nmo_script_walker.h"
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

/* Find a script that has graph-type sub-behaviors (depth>1 produces more nodes) */
static nmo_object_id_t find_nested_graph_script(nmo_context_t *ctx,
                                                 nmo_session_t *session)
{
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 32, NULL);
    nmo_script_walker_find_scripts(ctx, session, &scripts);

    nmo_object_id_t result = 0;
    const nmo_script_entry_t *entries =
        (const nmo_script_entry_t *)scripts.data;

    for (size_t i = 0; i < scripts.count && result == 0; ++i) {
        nmo_behavior_graph_t g0 = {0}, g1 = {0};
        if (nmo_behavior_graph_build(ctx, session, entries[i].script_id, 0, &g0) &&
            nmo_behavior_graph_build(ctx, session, entries[i].script_id, 1, &g1)) {
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
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                        &ctx, &session))
        return;

    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 32, NULL);
    nmo_script_walker_find_scripts(ctx, session, &scripts);
    ASSERT_TRUE(scripts.count > 0);

    const nmo_script_entry_t *e = (const nmo_script_entry_t *)scripts.data;
    nmo_behavior_graph_t g = {0};
    ASSERT_TRUE(nmo_behavior_graph_build(ctx, session, e[0].script_id, 0, &g));

    /* Should have nodes and edges */
    ASSERT_TRUE(g.node_count > 0);

    nmo_behavior_graph_free(&g);
    nmo_array_dispose(&scripts);
    nmo_session_close_with_context(ctx, session);
}

TEST(graph_rec, depth1_has_more_nodes)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                        &ctx, &session))
        return;

    nmo_object_id_t script_id = find_nested_graph_script(ctx, session);
    if (script_id == 0) {
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_behavior_graph_t g0 = {0}, g1 = {0};
    ASSERT_TRUE(nmo_behavior_graph_build(ctx, session, script_id, 0, &g0));
    ASSERT_TRUE(nmo_behavior_graph_build(ctx, session, script_id, 1, &g1));

    ASSERT_TRUE(g1.node_count > g0.node_count);
    ASSERT_TRUE(g1.edge_count > g0.edge_count);

    nmo_behavior_graph_free(&g0);
    nmo_behavior_graph_free(&g1);
    nmo_session_close_with_context(ctx, session);
}

TEST(graph_rec, depth_field_set)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                        &ctx, &session))
        return;

    nmo_object_id_t script_id = find_nested_graph_script(ctx, session);
    if (script_id == 0) {
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_behavior_graph_t g = {0};
    ASSERT_TRUE(nmo_behavior_graph_build(ctx, session, script_id, 8, &g));

    bool found_deeper = false;
    for (size_t i = 0; i < g.node_count; ++i) {
        if (g.nodes[i].depth > 0 && strcmp(g.nodes[i].kind, "behavior") == 0) {
            found_deeper = true;
            ASSERT_TRUE(g.nodes[i].parent_id != 0);
        }
    }
    ASSERT_TRUE(found_deeper);

    nmo_behavior_graph_free(&g);
    nmo_session_close_with_context(ctx, session);
}

TEST(graph_rec, unlimited_depth_no_crash)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                        &ctx, &session))
        return;

    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 32, NULL);
    nmo_script_walker_find_scripts(ctx, session, &scripts);

    const nmo_script_entry_t *entries =
        (const nmo_script_entry_t *)scripts.data;

    for (size_t i = 0; i < scripts.count; ++i) {
        nmo_behavior_graph_t g = {0};
        ASSERT_TRUE(nmo_behavior_graph_build(ctx, session,
                                             entries[i].script_id,
                                             UINT32_MAX, &g));
        ASSERT_TRUE(g.node_count > 0);
        nmo_behavior_graph_free(&g);
    }

    nmo_array_dispose(&scripts);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(graph_rec, depth0_root_only);
    REGISTER_TEST(graph_rec, depth1_has_more_nodes);
    REGISTER_TEST(graph_rec, depth_field_set);
    REGISTER_TEST(graph_rec, unlimited_depth_no_crash);
TEST_MAIN_END()
