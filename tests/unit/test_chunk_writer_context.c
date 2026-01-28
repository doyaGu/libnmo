/**
 * @file test_chunk_writer_context.c
 * @brief Unit tests for the chunk writer version context stack (Phase 1.3)
 */

#include "format/nmo_chunk_writer.h"
#include "core/nmo_arena.h"
#include "test_framework.h"

TEST(chunk_writer_context, stack_initially_empty) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    /* Initially depth should be 0 */
    ASSERT_EQ(nmo_chunk_writer_depth(w), 0);

    /* Parent version should be 0 when no context */
    ASSERT_EQ(nmo_chunk_writer_parent_version(w), 0u);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_context, push_and_pop_single) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    /* Push a context with version 5 */
    ASSERT_EQ(nmo_chunk_writer_push_context(w, 5), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_depth(w), 1);

    /* Still no parent at depth 1 */
    ASSERT_EQ(nmo_chunk_writer_parent_version(w), 0u);

    /* Pop the context */
    ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_depth(w), 0);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_context, nested_contexts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    /* Push root context (version 1) */
    ASSERT_EQ(nmo_chunk_writer_push_context(w, 1), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_depth(w), 1);
    ASSERT_EQ(nmo_chunk_writer_parent_version(w), 0u);  /* No parent */

    /* Push child context (version 2) */
    ASSERT_EQ(nmo_chunk_writer_push_context(w, 2), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_depth(w), 2);
    ASSERT_EQ(nmo_chunk_writer_parent_version(w), 1u);  /* Parent is version 1 */

    /* Push grandchild context (version 3) */
    ASSERT_EQ(nmo_chunk_writer_push_context(w, 3), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_depth(w), 3);
    ASSERT_EQ(nmo_chunk_writer_parent_version(w), 2u);  /* Parent is version 2 */

    /* Pop back through all levels */
    ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_depth(w), 2);
    ASSERT_EQ(nmo_chunk_writer_parent_version(w), 1u);

    ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_depth(w), 1);
    ASSERT_EQ(nmo_chunk_writer_parent_version(w), 0u);

    ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_depth(w), 0);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_context, pop_empty_stack_fails) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    /* Pop on empty stack should fail */
    ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_ERR_INVALID_STATE);

    /* Push and pop to get back to empty */
    ASSERT_EQ(nmo_chunk_writer_push_context(w, 10), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_OK);

    /* Pop again should fail */
    ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_ERR_INVALID_STATE);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_context, stack_overflow) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    /* Push up to max depth */
    for (int i = 0; i < NMO_CHUNK_WRITER_MAX_DEPTH; i++) {
        ASSERT_EQ(nmo_chunk_writer_push_context(w, (uint32_t)i), NMO_OK);
    }

    ASSERT_EQ(nmo_chunk_writer_depth(w), NMO_CHUNK_WRITER_MAX_DEPTH);

    /* One more push should fail */
    ASSERT_EQ(nmo_chunk_writer_push_context(w, 99), NMO_ERR_BUFFER_OVERRUN);

    /* Depth should not have changed */
    ASSERT_EQ(nmo_chunk_writer_depth(w), NMO_CHUNK_WRITER_MAX_DEPTH);

    /* Clean up - pop all */
    for (int i = 0; i < NMO_CHUNK_WRITER_MAX_DEPTH; i++) {
        ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_OK);
    }

    ASSERT_EQ(nmo_chunk_writer_depth(w), 0);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_context, expected_ids_tracking) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    /* Push context and set expected IDs */
    ASSERT_EQ(nmo_chunk_writer_push_context(w, 1), NMO_OK);
    nmo_chunk_writer_set_expected_ids(w, 5);

    /* Pop should succeed (validation is advisory) */
    ASSERT_EQ(nmo_chunk_writer_pop_context(w), NMO_OK);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_context, null_handling) {
    /* NULL writer should be handled gracefully */
    ASSERT_EQ(nmo_chunk_writer_depth(NULL), 0);
    ASSERT_EQ(nmo_chunk_writer_parent_version(NULL), 0u);
    ASSERT_EQ(nmo_chunk_writer_push_context(NULL, 1), NMO_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(nmo_chunk_writer_pop_context(NULL), NMO_ERR_INVALID_ARGUMENT);

    /* set_expected_ids on NULL should not crash */
    nmo_chunk_writer_set_expected_ids(NULL, 10);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_writer_context, stack_initially_empty);
    REGISTER_TEST(chunk_writer_context, push_and_pop_single);
    REGISTER_TEST(chunk_writer_context, nested_contexts);
    REGISTER_TEST(chunk_writer_context, pop_empty_stack_fails);
    REGISTER_TEST(chunk_writer_context, stack_overflow);
    REGISTER_TEST(chunk_writer_context, expected_ids_tracking);
    REGISTER_TEST(chunk_writer_context, null_handling);
TEST_MAIN_END()
