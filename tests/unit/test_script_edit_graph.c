#include "../test_framework.h"

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_script_edit_graph.h"
#include "runtime/nmo_workspace.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_pipeline.h"

#include <stdint.h>

static bool open_test_file(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session)
{
    char errbuf[256] = {0};
    return nmo_session_open_file_with_context(path, out_ctx, out_session,
                                              errbuf, sizeof(errbuf));
}

TEST(script_edit_graph, build_reports_edit_ready_graph_for_ballance_root)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                        &ctx, &session)) {
        return;
    }

    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));

    nmo_script_edit_graph_t *graph = NULL;
    nmo_status_t rc = nmo_script_edit_graph_build(workspace, 237u,
                                                  UINT32_MAX, &graph);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_NOT_NULL(graph);
    ASSERT_EQ(237u, nmo_script_edit_graph_root_behavior_id(graph));
    ASSERT_GT(nmo_script_edit_graph_node_count(graph), 0u);
    ASSERT_TRUE(nmo_script_edit_graph_owner_index_available(graph));
    ASSERT_TRUE(nmo_script_edit_graph_edit_ready(graph));

    const nmo_behavior_index_t *index = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(index);

    size_t control_edge_count = 0;
    const nmo_script_edit_control_edge_t *control_edges =
        nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    ASSERT_NOT_NULL(control_edges);
    ASSERT_GT(control_edge_count, 0u);

    for (size_t i = 0; i < control_edge_count; ++i) {
        const nmo_script_edit_control_edge_t *edge = &control_edges[i];
        const nmo_port_owner_t *source_owner =
            nmo_behavior_index_find(index, edge->source.object_id);
        const nmo_port_owner_t *target_owner =
            nmo_behavior_index_find(index, edge->target.object_id);

        ASSERT_NOT_NULL(source_owner);
        ASSERT_NOT_NULL(target_owner);
        ASSERT_EQ(edge->source.owner_behavior_id, source_owner->owner_id);
        ASSERT_EQ(edge->target.owner_behavior_id, target_owner->owner_id);
        ASSERT_EQ(edge->source.owner_index, source_owner->index);
        ASSERT_EQ(edge->target.owner_index, target_owner->index);
    }

    size_t broken_ref_count = 0;
    nmo_status_t ref_status =
        nmo_script_edit_graph_reference_validation_status(graph,
                                                         &broken_ref_count);
    ASSERT_EQ(NMO_OK, ref_status);
    ASSERT_EQ(0u, broken_ref_count);

    nmo_script_edit_graph_destroy(graph);
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(script_edit_graph, build_reports_edit_ready_graph_for_ballance_root);
TEST_MAIN_END()

