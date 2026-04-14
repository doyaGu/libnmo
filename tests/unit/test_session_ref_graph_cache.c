#include "test_framework.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_ref_graph.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "core/nmo_array.h"

typedef struct post_delete_cache_probe {
    size_t post_delete_edges;
    uint32_t post_delete_count;
} post_delete_cache_probe_t;

static int ref_graph_post_delete_probe(
    void *session_ptr,
    const nmo_runtime_event_ctx_t *ctx,
    void *user_data)
{
    post_delete_cache_probe_t *probe = (post_delete_cache_probe_t *)user_data;
    if (probe == NULL || session_ptr == NULL || ctx == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (ctx->event != NMO_RUNTIME_EVENT_POST_DELETE) {
        return NMO_OK;
    }

    nmo_ref_graph_t *graph = nmo_session_get_ref_graph((nmo_session_t *)session_ptr);
    nmo_ref_graph_stats_t stats = {0};
    if (graph != NULL) {
        nmo_ref_graph_get_stats(graph, &stats);
    }
    probe->post_delete_edges = stats.total_edges;
    probe->post_delete_count++;
    return NMO_OK;
}

/**
 * Two consecutive calls to nmo_session_get_ref_graph() without mutation
 * should return the same cached pointer.
 */
TEST(ref_graph_cache, cached_same_pointer) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "a",
            (nmo_guid_t){0, 0}, &id, NULL));

    nmo_ref_graph_t *g1 = nmo_session_get_ref_graph(session);
    nmo_ref_graph_t *g2 = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(g1);
    ASSERT_TRUE(g1 == g2);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * After creating a group with a member reference, the cached graph must
 * reflect the new edge when rebuilt.
 */
TEST(ref_graph_cache, invalidated_after_create) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    /* Create a member and build initial graph */
    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_ref_graph_t *g1 = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(g1);
    nmo_ref_graph_stats_t stats1 = {0};
    nmo_ref_graph_get_stats(g1, &stats1);

    /* Create a group that references the member — invalidates the cache */
    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    nmo_group_state_t *state = (nmo_group_state_t *)group_obj->state;
    nmo_array_clear(&state->object_ids);
    nmo_array_reserve(&state->object_ids, 1);
    nmo_object_id_t *ids = NULL;
    nmo_array_extend(&state->object_ids, 1, (void **)&ids);
    ids[0] = member_id;

    /* Invalidate manually since direct state mutation bypasses the kernel */
    nmo_session_invalidate_ref_graph(session);

    nmo_ref_graph_t *g2 = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(g2);
    nmo_ref_graph_stats_t stats2 = {0};
    nmo_ref_graph_get_stats(g2, &stats2);

    /* The new graph should have more edges (the group->member reference) */
    ASSERT_TRUE(stats2.total_edges > stats1.total_edges);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * After deleting an object that was referenced, the rebuilt graph
 * must reflect the deletion (fewer edges).  We use edge count as the
 * invalidation signal since pointer comparison is unreliable due to
 * arena address reuse.
 */
TEST(ref_graph_cache, invalidated_after_delete) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    /* Create member + group with a reference edge */
    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    nmo_group_state_t *state = (nmo_group_state_t *)group_obj->state;
    nmo_array_clear(&state->object_ids);
    nmo_array_reserve(&state->object_ids, 1);
    nmo_object_id_t *ids = NULL;
    nmo_array_extend(&state->object_ids, 1, (void **)&ids);
    ids[0] = member_id;

    /* Force graph build — should have at least 1 edge (group→member) */
    nmo_session_invalidate_ref_graph(session);
    nmo_ref_graph_t *g1 = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(g1);
    nmo_ref_graph_stats_t stats1 = {0};
    nmo_ref_graph_get_stats(g1, &stats1);
    ASSERT_TRUE(stats1.total_edges > 0);

    /* Delete the group (the edge source) — invalidates the cache */
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK,
        nmo_session_destroy_objects(session, &group_id, 1,
            NMO_RUNTIME_REQUEST_DEFAULT, &report));

    /* Rebuilt graph should have fewer edges (group is gone) */
    nmo_ref_graph_t *g2 = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(g2);
    nmo_ref_graph_stats_t stats2 = {0};
    nmo_ref_graph_get_stats(g2, &stats2);
    ASSERT_TRUE(stats2.total_edges < stats1.total_edges);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * POST_DELETE manager hooks should see a rebuilt ref graph, not the
 * pre-delete cached graph.
 */
TEST(ref_graph_cache, post_delete_event_sees_invalidated_graph) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    post_delete_cache_probe_t probe = {0};
    nmo_manager_t *manager = nmo_manager_create(
        (nmo_guid_t){0xCAFE0001u, 0xD3117E00u},
        "RefGraphPostDeleteProbe",
        NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(manager);
    ASSERT_EQ(NMO_OK, nmo_manager_set_user_data(manager, &probe));
    ASSERT_EQ(NMO_OK, nmo_manager_set_on_event_hook(manager, ref_graph_post_delete_probe));

    nmo_manager_registry_t *manager_registry = nmo_context_get_manager_registry(ctx);
    ASSERT_NOT_NULL(manager_registry);
    ASSERT_EQ(NMO_OK, nmo_manager_registry_register(manager_registry, 9100, manager));

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_id_t member_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "member",
            (nmo_guid_t){0, 0}, &member_id, NULL));

    nmo_object_id_t group_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_GROUP, "group",
            (nmo_guid_t){0, 0}, &group_id, NULL));

    nmo_object_t *group_obj = nmo_object_repository_find_by_id(repo, group_id);
    ASSERT_NOT_NULL(group_obj);
    nmo_group_state_t *state = (nmo_group_state_t *)group_obj->state;
    nmo_array_clear(&state->object_ids);
    nmo_array_reserve(&state->object_ids, 1);
    nmo_object_id_t *ids = NULL;
    nmo_array_extend(&state->object_ids, 1, (void **)&ids);
    ids[0] = member_id;

    nmo_session_invalidate_ref_graph(session);
    nmo_ref_graph_t *before_graph = nmo_session_get_ref_graph(session);
    ASSERT_NOT_NULL(before_graph);
    nmo_ref_graph_stats_t before_stats = {0};
    nmo_ref_graph_get_stats(before_graph, &before_stats);
    ASSERT_TRUE(before_stats.total_edges > 0);

    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK,
        nmo_session_destroy_objects(session, &group_id, 1,
            NMO_RUNTIME_REQUEST_DEFAULT, &report));

    ASSERT_EQ(1u, probe.post_delete_count);
    /* After deleting the group, the rebuilt graph seen by the POST_DELETE
     * hook must have fewer edges (the group→member ref is gone). */
    ASSERT_TRUE(probe.post_delete_edges < before_stats.total_edges);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(ref_graph_cache, cached_same_pointer);
    REGISTER_TEST(ref_graph_cache, invalidated_after_create);
    REGISTER_TEST(ref_graph_cache, invalidated_after_delete);
    REGISTER_TEST(ref_graph_cache, post_delete_event_sees_invalidated_graph);
TEST_MAIN_END()
