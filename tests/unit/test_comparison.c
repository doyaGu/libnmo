/**
 * @file test_comparison.c
 * @brief Unit tests for Phase 2.4 DOM comparison API
 */

#include "test_framework.h"
#include "app/nmo_comparison.h"
#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include <string.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static nmo_context_t* create_test_context(void) {
    nmo_context_desc_t desc = {
        .allocator = NULL,
        .logger = NULL,
        .thread_pool_size = 1,
    };
    return nmo_context_create(&desc);
}

static nmo_object_t* create_test_object(const nmo_allocator_t *allocator,
                                        uint32_t id,
                                        uint32_t class_id,
                                        const char *name) {
    nmo_object_t *obj = nmo_object_create(allocator, (nmo_object_id_t)id, (nmo_class_id_t)class_id);
    if (obj == NULL) return NULL;

    if (name != NULL) {
        if (nmo_object_set_name(obj, name) != NMO_OK) {
            nmo_object_destroy(obj);
            return NULL;
        }
    }

    return obj;
}

/* ============================================================================
 * Result Initialization Tests
 * ============================================================================ */

TEST(comparison, result_init) {
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    ASSERT_EQ(result.match, 1);
    ASSERT_EQ(result.diff_count, 0);
    ASSERT_EQ(result.diff_overflow, 0);
    ASSERT_EQ(result.objects_compared, 0);
    ASSERT_EQ(result.objects_matched, 0);
}

TEST(comparison, result_init_null_safe) {
    /* Should not crash with NULL */
    nmo_comparison_result_init(NULL);
}

/* ============================================================================
 * Add Diff Tests
 * ============================================================================ */

TEST(comparison, add_diff_basic) {
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    nmo_comparison_add_diff(&result, NMO_DIFF_OBJECT_NAME, 42, "test context");
    
    ASSERT_EQ(result.match, 0);  /* Any diff means no match */
    ASSERT_EQ(result.diff_count, 1);
    ASSERT_EQ(result.diffs[0].type, NMO_DIFF_OBJECT_NAME);
    ASSERT_EQ(result.diffs[0].object_id, 42);
    ASSERT_STR_EQ(result.diffs[0].context, "test context");
}

TEST(comparison, add_diff_overflow) {
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    /* Add more than NMO_MAX_DIFFS */
    for (int i = 0; i < NMO_MAX_DIFFS + 10; i++) {
        nmo_comparison_add_diff(&result, NMO_DIFF_OBJECT_NAME, i, "overflow test");
    }
    
    ASSERT_EQ(result.diff_count, NMO_MAX_DIFFS);
    ASSERT_EQ(result.diff_overflow, 1);
}

TEST(comparison, add_diff_null_context) {
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    nmo_comparison_add_diff(&result, NMO_DIFF_FILE_VERSION, 0, NULL);
    
    ASSERT_EQ(result.diff_count, 1);
    ASSERT_EQ(result.diffs[0].context[0], '\0');
}

/* ============================================================================
 * Session Comparison Tests
 * ============================================================================ */

TEST(comparison, identical_empty_sessions) {
    nmo_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t *session1 = nmo_session_create(ctx);
    nmo_session_t *session2 = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session1);
    ASSERT_NOT_NULL(session2);
    
    /* Set identical file info */
    nmo_file_info_t info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .object_count = 0,
        .manager_count = 0,
    };
    nmo_session_set_file_info(session1, &info);
    nmo_session_set_file_info(session2, &info);
    
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    int err = nmo_session_compare(session1, session2, NMO_COMPARE_DEFAULT, &result);
    ASSERT_EQ(err, NMO_OK);
    ASSERT_EQ(result.match, 1);
    ASSERT_EQ(result.diff_count, 0);
    
    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_context_release(ctx);
}

TEST(comparison, different_file_versions) {
    nmo_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t *session1 = nmo_session_create(ctx);
    nmo_session_t *session2 = nmo_session_create(ctx);
    
    nmo_file_info_t info1 = { .file_version = 8 };
    nmo_file_info_t info2 = { .file_version = 9 };
    nmo_session_set_file_info(session1, &info1);
    nmo_session_set_file_info(session2, &info2);
    
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    nmo_session_compare(session1, session2, NMO_COMPARE_FILE_INFO, &result);
    
    ASSERT_EQ(result.match, 0);
    ASSERT_TRUE(result.diff_count > 0);
    ASSERT_EQ(result.diffs[0].type, NMO_DIFF_FILE_VERSION);
    
    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_context_release(ctx);
}

TEST(comparison, different_object_counts) {
    nmo_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t *session1 = nmo_session_create(ctx);
    nmo_session_t *session2 = nmo_session_create(ctx);
    
    nmo_file_info_t info1 = { .object_count = 5 };
    nmo_file_info_t info2 = { .object_count = 10 };
    nmo_session_set_file_info(session1, &info1);
    nmo_session_set_file_info(session2, &info2);
    
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    nmo_session_compare(session1, session2, NMO_COMPARE_FILE_INFO, &result);
    
    ASSERT_EQ(result.match, 0);
    
    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_context_release(ctx);
}

TEST(comparison, sessions_with_objects) {
    nmo_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t *session1 = nmo_session_create(ctx);
    nmo_session_t *session2 = nmo_session_create(ctx);
    
    nmo_object_repository_t *repo1 = nmo_session_get_repository(session1);
    nmo_object_repository_t *repo2 = nmo_session_get_repository(session2);
    
    /* Add identical objects to both sessions */
    const nmo_allocator_t *allocator = nmo_context_get_allocator(ctx);
    nmo_object_t *obj1 = create_test_object(allocator, 1, 0x10000001, "TestObject");
    nmo_object_t *obj2 = create_test_object(allocator, 1, 0x10000001, "TestObject");
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);

    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo1, &obj1));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo2, &obj2));
    
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    nmo_session_compare(session1, session2, 
                        NMO_COMPARE_NAMES | NMO_COMPARE_CLASS_IDS, &result);
    
    ASSERT_EQ(result.match, 1);
    ASSERT_EQ(result.objects_compared, 1);
    ASSERT_EQ(result.objects_matched, 1);
    
    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_context_release(ctx);
}

TEST(comparison, different_object_names) {
    nmo_context_t *ctx = create_test_context();
    ASSERT_NOT_NULL(ctx);
    
    nmo_session_t *session1 = nmo_session_create(ctx);
    nmo_session_t *session2 = nmo_session_create(ctx);
    
    nmo_object_repository_t *repo1 = nmo_session_get_repository(session1);
    nmo_object_repository_t *repo2 = nmo_session_get_repository(session2);
    
    /* Add objects with different names */
    const nmo_allocator_t *allocator = nmo_context_get_allocator(ctx);
    nmo_object_t *obj1 = create_test_object(allocator, 1, 0x10000001, "Object_A");
    nmo_object_t *obj2 = create_test_object(allocator, 1, 0x10000001, "Object_B");
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);

    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo1, &obj1));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo2, &obj2));
    
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    nmo_session_compare(session1, session2, NMO_COMPARE_NAMES, &result);
    
    ASSERT_EQ(result.match, 0);
    ASSERT_EQ(result.diffs[0].type, NMO_DIFF_OBJECT_NAME);
    
    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_context_release(ctx);
}

/* ============================================================================
 * Report Generation Tests
 * ============================================================================ */

TEST(comparison, format_report_match) {
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    result.objects_compared = 10;
    result.objects_matched = 10;
    
    nmo_comparison_result_format_report(&result);
    
    ASSERT_TRUE(strstr(result.report, "MATCH") != NULL);
}

TEST(comparison, format_report_mismatch) {
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    nmo_comparison_add_diff(&result, NMO_DIFF_FILE_VERSION, 0, "version 8 vs 9");
    nmo_comparison_result_format_report(&result);
    
    ASSERT_TRUE(strstr(result.report, "MISMATCH") != NULL);
    ASSERT_TRUE(strstr(result.report, "FILE_VERSION") != NULL);
}

/* ============================================================================
 * Null Safety Tests
 * ============================================================================ */

TEST(comparison, compare_null_sessions) {
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    
    int err = nmo_session_compare(NULL, NULL, NMO_COMPARE_DEFAULT, &result);
    ASSERT_EQ(err, NMO_ERR_INVALID_ARGUMENT);
}

TEST(comparison, compare_null_result) {
    nmo_context_t *ctx = create_test_context();
    nmo_session_t *session = nmo_session_create(ctx);
    
    int err = nmo_session_compare(session, session, NMO_COMPARE_DEFAULT, NULL);
    ASSERT_EQ(err, NMO_ERR_INVALID_ARGUMENT);
    
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Result initialization */
    REGISTER_TEST(comparison, result_init);
    REGISTER_TEST(comparison, result_init_null_safe);
    
    /* Add diff */
    REGISTER_TEST(comparison, add_diff_basic);
    REGISTER_TEST(comparison, add_diff_overflow);
    REGISTER_TEST(comparison, add_diff_null_context);
    
    /* Session comparison */
    REGISTER_TEST(comparison, identical_empty_sessions);
    REGISTER_TEST(comparison, different_file_versions);
    REGISTER_TEST(comparison, different_object_counts);
    REGISTER_TEST(comparison, sessions_with_objects);
    REGISTER_TEST(comparison, different_object_names);
    
    /* Report generation */
    REGISTER_TEST(comparison, format_report_match);
    REGISTER_TEST(comparison, format_report_mismatch);
    
    /* Null safety */
    REGISTER_TEST(comparison, compare_null_sessions);
    REGISTER_TEST(comparison, compare_null_result);
TEST_MAIN_END()
