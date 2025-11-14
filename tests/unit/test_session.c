/**
 * @file test_session.c
 * @brief Unit tests for session management
 */

#include "test_framework.h"
#include "nmo.h"
#include "session/nmo_object_index.h"

static nmo_object_t *create_session_object(
    nmo_session_t *session,
    nmo_object_id_t id,
    nmo_class_id_t class_id,
    const char *name
) {
    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *object = nmo_object_create(arena, id, class_id);
    if (object == NULL) {
        return NULL;
    }

    if (name != NULL) {
        if (nmo_object_set_name(object, name, arena) != NMO_OK) {
            return NULL;
        }
    }

    if (nmo_object_repository_add(repo, object) != NMO_OK) {
        return NULL;
    }

    return object;
}

/**
 * Test session creation and destruction
 */
TEST(session, create) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test getting context from session
 */
TEST(session, get_context) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    
    nmo_context_t* retrieved_ctx = nmo_session_get_context(session);
    ASSERT_EQ(retrieved_ctx, ctx);
    
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session, index_incremental_updates) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_object_t *obj1 = create_session_object(session, 10, 42, "Alpha");
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(create_session_object(session, 11, 42, "Beta"));

    ASSERT_EQ(NMO_OK, nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL));
    ASSERT_EQ(2u, nmo_session_count_objects_by_class(session, 42));

    ASSERT_NOT_NULL(create_session_object(session, 12, 42, "Gamma"));
    ASSERT_EQ(3u, nmo_session_count_objects_by_class(session, 42));

    nmo_object_t *found = nmo_session_find_by_name(session, "Gamma", 0);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(12, found->id);

    ASSERT_EQ(NMO_OK, nmo_object_repository_remove(
        nmo_session_get_repository(session), obj1->id));
    ASSERT_EQ(2u, nmo_session_count_objects_by_class(session, 42));

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(session, object_index_stats) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    ASSERT_NOT_NULL(create_session_object(session, 1, 100, "First"));
    ASSERT_NOT_NULL(create_session_object(session, 2, 200, "Second"));

    nmo_index_stats_t stats;
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_session_get_object_index_stats(session, &stats));

    ASSERT_EQ(NMO_OK, nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_CLASS | NMO_INDEX_BUILD_NAME));
    ASSERT_EQ(NMO_OK, nmo_session_get_object_index_stats(session, &stats));
    ASSERT_EQ(2u, stats.total_objects);
    ASSERT_EQ(2u, stats.name_index_entries);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(session, create);
    REGISTER_TEST(session, get_context);
    REGISTER_TEST(session, index_incremental_updates);
    REGISTER_TEST(session, object_index_stats);
TEST_MAIN_END()
