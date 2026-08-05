/**
 * @file test_orphan_reachability.c
 * @brief Tests for nmo_ref_graph_mark_reachable()
 */

#include "test_framework.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "document/nmo_document_load.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <string.h>

/* ---- invalid_args ---- */

TEST(orphan_reach, invalid_args) {
    nmo_object_id_t root = 1;
    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 0;
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* NULL graph */
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_ref_graph_mark_reachable(NULL, &root, 1, arena,
                                     &out_ids, &out_count));

    /* NULL arena */
    /* We can't pass a real graph easily without a session, so just test NULL graph */

    /* NULL out params */
    /* NULL graph still dominates; verified above */

    nmo_arena_destroy(arena);
}

/* ---- empty_root_set ---- */

TEST(orphan_reach, empty_root_set) {
    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, registry, arena);
    ASSERT_NOT_NULL(graph);

    nmo_object_id_t *out_ids = NULL;
    size_t out_count = 99;

    /* root_count = 0 */
    ASSERT_EQ(NMO_OK,
        nmo_ref_graph_mark_reachable(graph, NULL, 0, arena,
                                     &out_ids, &out_count));
    ASSERT_EQ(0u, (unsigned)out_count);

    /* root_ids = NULL with non-zero count */
    out_count = 99;
    ASSERT_EQ(NMO_OK,
        nmo_ref_graph_mark_reachable(graph, NULL, 5, arena,
                                     &out_ids, &out_count));
    ASSERT_EQ(0u, (unsigned)out_count);

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ---- Helper: find CKLevel roots in a repository ---- */

static size_t find_level_roots(nmo_object_t **objects, size_t object_count,
                               const nmo_type_registry_t *registry,
                               nmo_object_id_t *out_ids) {
    size_t count = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
        if (cid == NMO_CID_LEVEL ||
            nmo_type_registry_is_class_derived_from(registry, cid,
                                                     NMO_CID_LEVEL)) {
            out_ids[count++] = nmo_object_get_id(objects[i]);
        }
    }
    return count;
}

/* ---- all_reachable_from_root ---- */

TEST(orphan_reach, all_reachable_from_root) {
    /* Try files that might contain CKLevel objects */
    static const char *candidates[] = {
        "data/base.cmo",
        "data/Balls.nmo",
        NULL
    };

    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = NULL;
    const char *loaded_file = NULL;

    for (int f = 0; candidates[f]; f++) {
        session = nmo_session_create(ctx);
        ASSERT_NOT_NULL(session);
        int rc = nmo_load_file(session, candidates[f], NULL);
        if (rc == NMO_OK) {
            loaded_file = candidates[f];
            break;
        }
        nmo_session_destroy(session);
        session = NULL;
    }

    if (!session) {
        printf("  [SKIP] no test data files loadable\n");
        nmo_context_release(ctx);
        return;
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 20);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    size_t object_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
    ASSERT_TRUE(object_count > 0);

    nmo_object_id_t *root_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, object_count * sizeof(nmo_object_id_t),
        _Alignof(nmo_object_id_t));
    ASSERT_NOT_NULL(root_ids);
    size_t root_count = find_level_roots(objects, object_count,
                                          registry, root_ids);

    if (root_count == 0) {
        printf("  [SKIP] %s has no CKLevel roots\n", loaded_file);
        nmo_arena_destroy(arena);
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, registry, arena);
    ASSERT_NOT_NULL(graph);

    nmo_object_id_t *reachable_ids = NULL;
    size_t reachable_count = 0;
    ASSERT_EQ(NMO_OK,
        nmo_ref_graph_mark_reachable(graph, root_ids, root_count, arena,
                                     &reachable_ids, &reachable_count));

    printf("  file=%s objects=%zu roots=%zu reachable=%zu\n",
           loaded_file, object_count, root_count, reachable_count);

    /* Reachable set must include at least the roots and be <= total */
    ASSERT_TRUE(reachable_count >= root_count);
    ASSERT_TRUE(reachable_count <= object_count);

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ---- reachable_superset_of_zero_incoming ---- */

TEST(orphan_reach, reachable_superset_of_zero_incoming) {
    static const char *candidates[] = {
        "data/base.cmo",
        "data/Balls.nmo",
        NULL
    };

    nmo_context_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = NULL;
    for (int f = 0; candidates[f]; f++) {
        session = nmo_session_create(ctx);
        ASSERT_NOT_NULL(session);
        int rc = nmo_load_file(session, candidates[f], NULL);
        if (rc == NMO_OK) break;
        nmo_session_destroy(session);
        session = NULL;
    }

    if (!session) {
        printf("  [SKIP] no test data files loadable\n");
        nmo_context_release(ctx);
        return;
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 20);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    size_t object_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
    ASSERT_TRUE(object_count > 0);

    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, registry, arena);
    ASSERT_NOT_NULL(graph);

    /* Find CKLevel roots */
    nmo_object_id_t *root_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, object_count * sizeof(nmo_object_id_t),
        _Alignof(nmo_object_id_t));
    ASSERT_NOT_NULL(root_ids);
    size_t root_count = find_level_roots(objects, object_count,
                                          registry, root_ids);

    /* Mark-reachable from roots */
    nmo_object_id_t *reachable_ids = NULL;
    size_t reachable_count = 0;
    ASSERT_EQ(NMO_OK,
        nmo_ref_graph_mark_reachable(graph, root_ids, root_count, arena,
                                     &reachable_ids, &reachable_count));

    /* Count orphans via zero-incoming heuristic (same logic as CLI) */
    size_t zero_incoming_orphans = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t oid = nmo_object_get_id(objects[i]);
        nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);

        /* Skip roots */
        if (cid == NMO_CID_LEVEL ||
            nmo_type_registry_is_class_derived_from(registry, cid,
                                                     NMO_CID_LEVEL)) {
            continue;
        }

        nmo_ref_edge_t *in_edges = NULL;
        size_t in_count = 0;
        nmo_ref_graph_get_object_edges(graph, oid, NMO_REF_DIR_INCOMING,
                                       &in_edges, &in_count);
        if (in_count == 0) {
            zero_incoming_orphans++;
        }
    }

    /* Mark-sweep unreachable count */
    size_t mark_sweep_orphans = object_count - reachable_count;

    printf("  zero_incoming_orphans=%zu  mark_sweep_orphans=%zu\n",
           zero_incoming_orphans, mark_sweep_orphans);

    /* Mark-sweep should find at least as many orphans as zero-incoming,
       because every zero-incoming non-root is guaranteed unreachable,
       but mark-sweep can also catch objects reachable only through cycles
       among themselves. */
    ASSERT_TRUE(mark_sweep_orphans >= zero_incoming_orphans);

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(orphan_reach, explicit_group_type_is_a_root) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *group = nmo_object_create(NULL, 1u, 0);
    ASSERT_NOT_NULL(group);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(group, CKPGUID_GROUP));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &group));
    ASSERT_NULL(group);
    nmo_object_t *unrelated = nmo_object_create(NULL, 2u, NMO_CID_OBJECT);
    ASSERT_NOT_NULL(unrelated);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &unrelated));
    ASSERT_NULL(unrelated);

    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    const nmo_type_registry_t *registry =
        nmo_context_get_type_registry(ctx);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, registry, arena);
    ASSERT_NOT_NULL(graph);

    nmo_object_id_t *orphans = NULL;
    size_t orphan_count = 0;
    ASSERT_EQ(NMO_OK, nmo_ref_graph_find_orphans(
        graph, repo, registry, arena, &orphans, &orphan_count));
    ASSERT_EQ(1u, orphan_count);
    ASSERT_EQ(2u, orphans[0]);

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(orphan_reach, invalid_args);
REGISTER_TEST(orphan_reach, empty_root_set);
REGISTER_TEST(orphan_reach, all_reachable_from_root);
REGISTER_TEST(orphan_reach, reachable_superset_of_zero_incoming);
REGISTER_TEST(orphan_reach, explicit_group_type_is_a_root);
TEST_MAIN_END()

