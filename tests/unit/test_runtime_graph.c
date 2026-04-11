#include "test_framework.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_runtime_graph.h"
#include "type/nmo_type_runtime.h"

TEST(runtime_graph, build_empty_graph) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    const nmo_type_runtime_t *rt = nmo_context_get_type_runtime(ctx);
    ASSERT_NOT_NULL(rt);
    ASSERT_NOT_NULL(rt->types);

    nmo_runtime_graph_t *graph = nmo_runtime_graph_create(
        nmo_session_get_repository(session),
        rt->types,
        nmo_session_get_arena(session));
    ASSERT_NOT_NULL(graph);

    nmo_runtime_edge_t *edges = NULL;
    size_t edge_count = 0;
    ASSERT_EQ(NMO_OK, nmo_runtime_graph_get_edges(graph, &edges, &edge_count));
    ASSERT_EQ(0u, edge_count);

    nmo_runtime_graph_stats_t stats = {0};
    ASSERT_EQ(NMO_OK, nmo_runtime_graph_get_stats(graph, &stats));
    ASSERT_EQ(0u, stats.total_edges);

    nmo_runtime_graph_destroy(graph);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(runtime_graph, build_empty_graph);
TEST_MAIN_END()
