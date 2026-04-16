/**
 * @file test_session.c
 * @brief Unit tests for session management
 */

#include "test_framework.h"
#include "nmo.h"
#include "object/nmo_object_index.h"

static nmo_object_t *create_session_object(
    nmo_session_t *session,
    nmo_object_id_t id,
    nmo_class_id_t class_id,
    const char *name
) {
    nmo_context_t *ctx = nmo_session_get_context(session);
    const nmo_allocator_t *allocator = nmo_context_get_allocator(ctx);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *object = nmo_object_create(allocator, id, class_id);
    if (object == NULL) {
        return NULL;
    }

    if (name != NULL) {
        if (nmo_object_set_name(object, name) != NMO_OK) {
            nmo_object_destroy(object);
            return NULL;
        }
    }

    nmo_object_t *repo_object = object;
    if (nmo_object_repository_add(repo, &object) != NMO_OK) {
        nmo_object_destroy(object);
        return NULL;
    }

    return repo_object;
}

static size_t count_session_objects_by_class(
    nmo_session_t *session,
    nmo_class_id_t class_id)
{
    nmo_object_query_t query = {
        .class_id = class_id,
        .include_derived_classes = false
    };
    nmo_object_query_result_t result = {0};
    if (nmo_session_query_objects(session, &query, NULL, NULL, &result) != NMO_OK) {
        return (size_t)-1;
    }
    return result.matched;
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
    ASSERT_EQ(2u, count_session_objects_by_class(session, 42));

    ASSERT_NOT_NULL(create_session_object(session, 12, 42, "Gamma"));
    ASSERT_EQ(3u, count_session_objects_by_class(session, 42));

    nmo_object_query_t gamma_query = {
        .name = "Gamma",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT,
        .name_case_insensitive = false
    };
    nmo_object_t *found = NULL;
    ASSERT_EQ(NMO_OK, nmo_session_query_first(session, &gamma_query, &found, NULL));
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(12, found->id);

    ASSERT_EQ(NMO_OK, nmo_object_repository_remove(
        nmo_session_get_repository(session), obj1->id));
    ASSERT_EQ(2u, count_session_objects_by_class(session, 42));

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
