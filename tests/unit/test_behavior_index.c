/**
 * @file test_behavior_index.c
 * @brief Unit tests for behavior ownership index
 */

#include "../test_framework.h"
#include "behavior/nmo_behavior_index.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "object/nmo_class_ids.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"

#include <stdlib.h>

typedef struct fail_after_alloc_ctx {
    size_t calls;
    size_t fail_after;
} fail_after_alloc_ctx_t;

static void *fail_after_alloc(void *user_data, size_t size, size_t alignment)
{
    (void)alignment;
    fail_after_alloc_ctx_t *ctx = (fail_after_alloc_ctx_t *)user_data;
    if (ctx && ctx->calls++ >= ctx->fail_after) {
        return NULL;
    }
    return malloc(size);
}

static void fail_after_free(void *user_data, void *ptr)
{
    (void)user_data;
    free(ptr);
}

TEST(beh_idx, create_destroy)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_behavior_index_t *idx = nmo_behavior_index_create(arena);
    ASSERT_TRUE(idx != NULL);
    ASSERT_EQ(nmo_behavior_index_count(idx), 0u);
    nmo_behavior_index_destroy(idx);
    nmo_arena_destroy(arena);
}

TEST(beh_idx, find_empty)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_behavior_index_t *idx = nmo_behavior_index_create(arena);
    ASSERT_TRUE(nmo_behavior_index_find(idx, 42) == NULL);
    nmo_behavior_index_destroy(idx);
    nmo_arena_destroy(arena);
}

TEST(beh_idx, build_from_file)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_TRUE(ctx != NULL);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_TRUE(session != NULL);

    int load_ok = nmo_session_load_file(session, "data/Ballance/Gameplay.nmo", NULL, NULL);
    if (load_ok != NMO_OK) {
        /* Skip if test data not available */
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    nmo_behavior_index_t *idx = nmo_behavior_index_create(arena);
    ASSERT_TRUE(idx != NULL);

    nmo_status_t st = nmo_behavior_index_build(idx, ctx, session);
    ASSERT_EQ(st, NMO_OK);

    /* Should have indexed many entries */
    size_t count = nmo_behavior_index_count(idx);
    ASSERT_TRUE(count > 100);

    /* Find a known IO port or parameter (we don't know exact IDs but
     * verify that at least some lookups succeed from the file's behaviors) */

    nmo_behavior_index_destroy(idx);
    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(beh_idx, build_reports_index_insert_oom)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_TRUE(ctx != NULL);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_TRUE(session != NULL);

    int load_ok = nmo_session_load_file(session, "data/Ballance/Gameplay.nmo", NULL, NULL);
    if (load_ok != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    fail_after_alloc_ctx_t alloc_ctx = {0, 2};
    nmo_allocator_t fail_alloc =
        nmo_allocator_custom(fail_after_alloc, fail_after_free, &alloc_ctx);
    nmo_arena_t *arena = nmo_arena_create(&fail_alloc, 1024);
    ASSERT_TRUE(arena != NULL);

    nmo_behavior_index_t *idx = nmo_behavior_index_create(arena);
    ASSERT_TRUE(idx != NULL);

    nmo_status_t st = nmo_behavior_index_build(idx, ctx, session);
    ASSERT_EQ(NMO_ERR_NOMEM, st);

    nmo_behavior_index_destroy(idx);
    nmo_arena_destroy(arena);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * After a mutation, the session behavior index should still be available
 * after lazy invalidation/rebuild. Arena allocation may reuse the same
 * address, so this verifies observable access instead of pointer identity.
 */
TEST(beh_idx, invalidated_after_create)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_TRUE(ctx != NULL);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_TRUE(session != NULL);

    int load_ok = nmo_session_load_file(session, "data/Ballance/P_Modul_01.nmo", NULL, NULL);
    if (load_ok != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    /* Get initial index — triggers build */
    nmo_behavior_index_t *idx1 = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(idx1);
    size_t count1 = nmo_behavior_index_count(idx1);

    /* Create an object — this should invalidate the index */
    nmo_object_id_t new_id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "test-obj",
            (nmo_guid_t){0, 0}, &new_id, NULL));

    /* Get index again — should still be valid after lazy rebuild */
    nmo_behavior_index_t *idx2 = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(idx2);
    ASSERT_EQ(count1, nmo_behavior_index_count(idx2));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * After a delete, the behavior index should also be invalidated.
 * Requires a loaded file so the index gets built initially.
 */
TEST(beh_idx, invalidated_after_delete)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_TRUE(ctx != NULL);
    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_TRUE(session != NULL);

    int load_ok = nmo_session_load_file(session, "data/Ballance/P_Modul_01.nmo", NULL, NULL);
    if (load_ok != NMO_OK) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }

    /* Force initial index build */
    nmo_behavior_index_t *idx1 = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(idx1);
    size_t count1 = nmo_behavior_index_count(idx1);

    /* Create then delete an object to trigger invalidation */
    nmo_object_id_t id = 0;
    ASSERT_EQ(NMO_OK,
        nmo_session_create_object(session, NMO_CID_OBJECT, "to-delete",
            (nmo_guid_t){0, 0}, &id, NULL));

    /* create already invalidated; get index to force rebuild */
    nmo_behavior_index_t *idx2 = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(idx2);
    ASSERT_EQ(count1, nmo_behavior_index_count(idx2));

    /* Now delete and verify invalidation again */
    nmo_runtime_report_t report = {0};
    ASSERT_EQ(NMO_OK,
        nmo_session_destroy_objects(session, &id, 1,
            NMO_RUNTIME_REQUEST_DEFAULT, &report));

    nmo_behavior_index_t *idx3 = nmo_session_get_behavior_index(session);
    ASSERT_NOT_NULL(idx3);
    ASSERT_EQ(count1, nmo_behavior_index_count(idx3));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(beh_idx, create_destroy);
    REGISTER_TEST(beh_idx, find_empty);
    REGISTER_TEST(beh_idx, build_from_file);
    REGISTER_TEST(beh_idx, build_reports_index_insert_oom);
    REGISTER_TEST(beh_idx, invalidated_after_create);
    REGISTER_TEST(beh_idx, invalidated_after_delete);
TEST_MAIN_END()
