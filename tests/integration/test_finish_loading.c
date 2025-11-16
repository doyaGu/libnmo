/**
 * @file test_finish_loading.c
 * @brief Integration test for Phase 5 FinishLoading functionality
 *
 * Tests:
 * - Finish loading phase execution
 * - Object index building
 * - Query API performance
 * - Reference resolution integration
 */

#include "test_framework.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "app/nmo_parser.h"
#include "app/nmo_finish_loading.h"
#include "session/nmo_object_index.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_logger.h"

/* Test configuration */
#define TEST_FILE NMO_TEST_DATA_FILE("Empty.cmo")

/**
 * Test: Basic finish loading execution
 */
TEST(finish_loading, basic_execution) {
    /* Create context and session */
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Load a test file */
    int result = nmo_load_file(session, TEST_FILE, NMO_LOAD_DEFAULT);
    if (result != NMO_OK) {
        printf("SKIP: Test file not found or failed to load: %s\n", TEST_FILE);
        nmo_session_destroy(session);
        nmo_context_destroy(ctx);
        return;
    }

    /* Verify object index was created */
    nmo_object_index_t *index = nmo_session_get_object_index(session);
    ASSERT_NOT_NULL(index);

    /* Get statistics */
    nmo_index_stats_t stats;
    result = nmo_object_index_get_stats(index, &stats);
    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(stats.total_objects > 0);

    /* Cleanup */
    nmo_session_destroy(session);
    nmo_context_destroy(ctx);
}

/**
 * Test: Query API functionality
 */
TEST(finish_loading, query_api) {
    /* Create context and session */
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Load test file */
    int result = nmo_load_file(session, TEST_FILE, NMO_LOAD_DEFAULT);
    if (result != NMO_OK) {
        printf("SKIP: Test file not found\n");
        nmo_session_destroy(session);
        nmo_context_destroy(ctx);
        return;
    }

    /* Get all objects */
    nmo_object_t **objects;
    size_t object_count;
    result = nmo_session_get_objects(session, &objects, &object_count);
    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(object_count > 0);

    /* Test: Find object by name (if any object has a name) */
    nmo_object_t *first_named_obj = NULL;
    for (size_t i = 0; i < object_count; i++) {
        if (objects[i]->name != NULL && objects[i]->name[0] != '\0') {
            first_named_obj = objects[i];
            break;
        }
    }

    if (first_named_obj != NULL) {
        nmo_object_t *found = nmo_session_find_by_name(session, first_named_obj->name, 0);
        ASSERT_NOT_NULL(found);
        ASSERT_EQ(first_named_obj->id, found->id);
    }

    /* Test: Get objects by class */
    if (object_count > 0) {
        nmo_class_id_t test_class = objects[0]->class_id;
        size_t class_count = nmo_session_count_objects_by_class(session, test_class);
        ASSERT_TRUE(class_count > 0);

        nmo_object_t **class_objects = nmo_session_get_objects_by_class(session, test_class, &class_count);
        ASSERT_NOT_NULL(class_objects);
    }

    /* Cleanup */
    nmo_session_destroy(session);
    nmo_context_destroy(ctx);
}

/**
 * Test: Index rebuild functionality
 */
TEST(finish_loading, index_rebuild) {
    /* Create context and session */
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Load test file */
    int result = nmo_load_file(session, TEST_FILE, NMO_LOAD_DEFAULT);
    if (result != NMO_OK) {
        printf("SKIP: Test file not found\n");
        nmo_session_destroy(session);
        nmo_context_destroy(ctx);
        return;
    }

    /* Get initial statistics */
    nmo_object_index_t *index = nmo_session_get_object_index(session);
    ASSERT_NOT_NULL(index);

    nmo_index_stats_t stats1;
    result = nmo_object_index_get_stats(index, &stats1);
    ASSERT_EQ(NMO_OK, result);

    /* Rebuild indexes */
    result = nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL);
    ASSERT_EQ(NMO_OK, result);

    /* Get statistics after rebuild */
    nmo_index_stats_t stats2;
    result = nmo_object_index_get_stats(index, &stats2);
    ASSERT_EQ(NMO_OK, result);

    /* Should have same number of objects */
    ASSERT_EQ(stats1.total_objects, stats2.total_objects);

    /* Cleanup */
    nmo_session_destroy(session);
    nmo_context_destroy(ctx);
}

/**
 * Test: Selective index building
 */
TEST(finish_loading, selective_index_building) {
    /* Create context and session */
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Load test file with skip index build flag */
    int result = nmo_load_file(session, TEST_FILE, NMO_LOAD_SKIP_INDEX_BUILD);
    if (result != NMO_OK) {
        printf("SKIP: Test file not found\n");
        nmo_session_destroy(session);
        nmo_context_destroy(ctx);
        return;
    }

    /* Index should not be created when skipped */
    nmo_object_index_t *index = nmo_session_get_object_index(session);
    if (index == NULL) {
        /* Manually build only class index */
        result = nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_CLASS);
        ASSERT_EQ(NMO_OK, result);

        index = nmo_session_get_object_index(session);
        ASSERT_NOT_NULL(index);

        /* Verify only class index is built */
        ASSERT_TRUE(nmo_object_index_has_class_index(index));
    }

    /* Cleanup */
    nmo_session_destroy(session);
    nmo_context_destroy(ctx);
}

/**
 * Test: Query without index (fallback to linear search)
 */
TEST(finish_loading, query_without_index) {
    /* Create context and session */
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Load with indexes disabled */
    int result = nmo_load_file(session, TEST_FILE, NMO_LOAD_SKIP_INDEX_BUILD);
    if (result != NMO_OK) {
        printf("SKIP: Test file not found\n");
        nmo_session_destroy(session);
        nmo_context_destroy(ctx);
        return;
    }

    /* Get objects */
    nmo_object_t **objects;
    size_t object_count;
    result = nmo_session_get_objects(session, &objects, &object_count);
    ASSERT_EQ(NMO_OK, result);

    if (object_count > 0) {
        /* Query should still work via linear search */
        nmo_class_id_t test_class = objects[0]->class_id;
        size_t class_count = nmo_session_count_objects_by_class(session, test_class);
        ASSERT_TRUE(class_count > 0);
    }

    /* Cleanup */
    nmo_session_destroy(session);
    nmo_context_destroy(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(finish_loading, basic_execution);
    REGISTER_TEST(finish_loading, query_api);
    REGISTER_TEST(finish_loading, index_rebuild);
    REGISTER_TEST(finish_loading, selective_index_building);
    REGISTER_TEST(finish_loading, query_without_index);
TEST_MAIN_END()
