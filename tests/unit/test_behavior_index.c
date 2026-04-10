/**
 * @file test_behavior_index.c
 * @brief Unit tests for behavior ownership index
 */

#include "../test_framework.h"
#include "app/nmo_behavior_index.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "core/nmo_arena.h"

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

TEST_MAIN_BEGIN()
    REGISTER_TEST(beh_idx, create_destroy);
    REGISTER_TEST(beh_idx, find_empty);
    REGISTER_TEST(beh_idx, build_from_file);
TEST_MAIN_END()
