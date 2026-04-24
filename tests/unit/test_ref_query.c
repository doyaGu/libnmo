#include "test_framework.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session_pipeline.h"
#include "../../src/runtime/runtime_internal.h"
#include "document/nmo_document.h"
#include "object/nmo_object_refs.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "format/nmo_object.h"
#include "core/nmo_array.h"

static void set_group_members(
    nmo_session_t *session,
    nmo_object_id_t group_id,
    const nmo_object_id_t *member_ids,
    size_t member_count)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);

    nmo_group_state_t *state = (nmo_group_state_t *)group_obj->state;
    ASSERT_NOT_NULL(state);

    nmo_array_clear(&state->object_ids);
    ASSERT_EQ(NMO_OK, nmo_array_reserve(&state->object_ids, member_count));
    if (member_count == 0) {
        return;
    }

    nmo_object_id_t *ids = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(&state->object_ids, member_count, (void **)&ids));
    ASSERT_NOT_NULL(ids);

    for (size_t i = 0; i < member_count; ++i) {
        ids[i] = member_ids[i];
    }
}

typedef struct ref_edge_capture {
    size_t count;
    nmo_object_id_t first_from;
    nmo_object_id_t first_to;
    uint32_t first_kind;
} ref_edge_capture_t;

static bool capture_object_ref_edge(
    const nmo_object_refs_edge_t *edge,
    void *user_data)
{
    ref_edge_capture_t *capture = (ref_edge_capture_t *)user_data;
    if (capture == NULL || edge == NULL || edge->edge == NULL) {
        return false;
    }

    if (capture->count == 0) {
        capture->first_from = edge->edge->from;
        capture->first_to = edge->edge->to;
        capture->first_kind = (uint32_t)edge->edge->kind;
    }
    capture->count++;
    return true;
}

static bool count_object_ref_edge(
    const nmo_object_refs_edge_t *edge,
    void *user_data)
{
    size_t *count = (size_t *)user_data;
    if (edge == NULL || edge->edge == NULL || count == NULL) {
        return false;
    }
    (*count)++;
    return true;
}

TEST(ref_query, counts_session_references_without_graph_handles) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    set_group_members(session, group_id, &member_id, 1);
    nmo_session_invalidate_ref_graph(session);

    nmo_object_refs_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document, group_id, NMO_OBJECT_REFS_BOTH, NULL, NULL, &result));
    ASSERT_EQ(1u, result.outgoing);
    ASSERT_EQ(0u, result.incoming);

    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document, member_id, NMO_OBJECT_REFS_BOTH, NULL, NULL, &result));
    ASSERT_EQ(0u, result.outgoing);
    ASSERT_EQ(1u, result.incoming);

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(graph);
    nmo_ref_graph_stats_t stats = {0};
    ASSERT_EQ(NMO_OK, nmo_ref_graph_get_stats(graph, &stats));
    ASSERT_EQ(1u, stats.total_edges);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(ref_query, reports_broken_reference_count) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_id_t missing_member = 999999u;
    set_group_members(session, group_id, &missing_member, 1);
    nmo_session_invalidate_ref_graph(session);

    nmo_object_refs_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document, group_id, NMO_OBJECT_REFS_OUTGOING, NULL, NULL, &result));
    ASSERT_EQ(1u, result.outgoing);

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(graph);
    nmo_ref_edge_t *broken = NULL;
    size_t broken_edges = 0;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_ref_graph_validate(graph, &broken, &broken_edges));
    ASSERT_EQ(1u, broken_edges);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST(ref_query, visits_edges_without_exposing_graph_handles) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_document_t *document = nmo_document_create(ctx);
    ASSERT_NOT_NULL(document);
    nmo_session_t *session = nmo_document_internal_session(document);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    set_group_members(session, group_id, &member_id, 1);
    nmo_session_invalidate_ref_graph(session);

    ref_edge_capture_t capture = {0};
    nmo_object_refs_result_t result = {0};
    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document,
        group_id,
        NMO_OBJECT_REFS_BOTH,
        capture_object_ref_edge,
        &capture,
        &result));
    ASSERT_EQ(1u, result.outgoing);
    ASSERT_EQ(0u, result.incoming);
    ASSERT_EQ(1u, capture.count);
    ASSERT_EQ(group_id, capture.first_from);
    ASSERT_EQ(member_id, capture.first_to);

    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_object_refs_iterate(
        document,
        group_id,
        NMO_OBJECT_REFS_BOTH,
        count_object_ref_edge,
        &count,
        NULL));
    ASSERT_EQ(1u, count);

    nmo_document_destroy(document);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(ref_query, counts_session_references_without_graph_handles);
    REGISTER_TEST(ref_query, reports_broken_reference_count);
    REGISTER_TEST(ref_query, visits_edges_without_exposing_graph_handles);
TEST_MAIN_END()



