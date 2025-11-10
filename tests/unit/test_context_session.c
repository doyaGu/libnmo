/**
 * @file test_context_session.c
 * @brief Test suite for context and session (Phase 8)
 */

#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_allocator.h"
#include "core/nmo_logger.h"
#include <stdio.h>
#include <stdlib.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s - %s\n", __func__, message); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        printf("PASS: %s\n", __func__); \
        tests_passed++; \
    } while(0)

/**
 * Test context creation with default settings
 */
static void test_context_create_default(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context with defaults");

    /* Check refcount */
    int refcount = nmo_context_get_refcount(ctx);
    TEST_ASSERT(refcount == 1, "Initial refcount should be 1");

    /* Check that we have a schema registry */
    nmo_schema_registry* schema_reg = nmo_context_get_schema_registry(ctx);
    TEST_ASSERT(schema_reg != NULL, "Schema registry should be created");

    /* Check allocator and logger */
    nmo_allocator* allocator = nmo_context_get_allocator(ctx);
    TEST_ASSERT(allocator != NULL, "Allocator should be available");

    nmo_logger* logger = nmo_context_get_logger(ctx);
    TEST_ASSERT(logger != NULL, "Logger should be available");

    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test context creation with custom settings
 */
static void test_context_create_custom(void) {
    nmo_allocator allocator = nmo_allocator_default();
    nmo_logger logger = nmo_logger_null();

    nmo_context_desc desc = {
        .allocator = &allocator,
        .logger = &logger,
        .thread_pool_size = 4
    };

    nmo_context* ctx = nmo_context_create(&desc);
    TEST_ASSERT(ctx != NULL, "Failed to create context with custom settings");

    /* Verify custom settings */
    nmo_allocator* ctx_allocator = nmo_context_get_allocator(ctx);
    TEST_ASSERT(ctx_allocator == &allocator, "Custom allocator not set correctly");

    nmo_logger* ctx_logger = nmo_context_get_logger(ctx);
    TEST_ASSERT(ctx_logger == &logger, "Custom logger not set correctly");

    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test context reference counting
 */
static void test_context_refcounting(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    int refcount = nmo_context_get_refcount(ctx);
    TEST_ASSERT(refcount == 1, "Initial refcount should be 1");

    /* Retain */
    nmo_context_retain(ctx);
    refcount = nmo_context_get_refcount(ctx);
    TEST_ASSERT(refcount == 2, "Refcount should be 2 after retain");

    /* Retain again */
    nmo_context_retain(ctx);
    refcount = nmo_context_get_refcount(ctx);
    TEST_ASSERT(refcount == 3, "Refcount should be 3 after second retain");

    /* Release (should not destroy) */
    nmo_context_release(ctx);
    refcount = nmo_context_get_refcount(ctx);
    TEST_ASSERT(refcount == 2, "Refcount should be 2 after first release");

    /* Release again (should not destroy) */
    nmo_context_release(ctx);
    refcount = nmo_context_get_refcount(ctx);
    TEST_ASSERT(refcount == 1, "Refcount should be 1 after second release");

    /* Final release (should destroy) */
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test session creation
 */
static void test_session_create(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    /* Verify session has borrowed context */
    nmo_context* borrowed_ctx = nmo_session_get_context(session);
    TEST_ASSERT(borrowed_ctx == ctx, "Session should borrow the same context");

    /* Verify session has arena */
    nmo_arena* arena = nmo_session_get_arena(session);
    TEST_ASSERT(arena != NULL, "Session should have an arena");

    /* Verify session has repository */
    nmo_object_repository* repo = nmo_session_get_repository(session);
    TEST_ASSERT(repo != NULL, "Session should have a repository");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test session does not retain context
 */
static void test_session_borrows_context(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    int initial_refcount = nmo_context_get_refcount(ctx);
    TEST_ASSERT(initial_refcount == 1, "Initial refcount should be 1");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    /* Context refcount should NOT change */
    int refcount_after_session = nmo_context_get_refcount(ctx);
    TEST_ASSERT(refcount_after_session == 1,
                "Context refcount should not change (session borrows)");

    nmo_session_destroy(session);

    /* Context refcount should still be 1 */
    int refcount_after_destroy = nmo_context_get_refcount(ctx);
    TEST_ASSERT(refcount_after_destroy == 1,
                "Context refcount should remain 1 after session destroy");

    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test multiple sessions with same context
 */
static void test_multiple_sessions(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session1 = nmo_session_create(ctx);
    TEST_ASSERT(session1 != NULL, "Failed to create first session");

    nmo_session* session2 = nmo_session_create(ctx);
    TEST_ASSERT(session2 != NULL, "Failed to create second session");

    nmo_session* session3 = nmo_session_create(ctx);
    TEST_ASSERT(session3 != NULL, "Failed to create third session");

    /* All sessions should share the same context */
    TEST_ASSERT(nmo_session_get_context(session1) == ctx, "Session1 context mismatch");
    TEST_ASSERT(nmo_session_get_context(session2) == ctx, "Session2 context mismatch");
    TEST_ASSERT(nmo_session_get_context(session3) == ctx, "Session3 context mismatch");

    /* But each should have its own arena and repository */
    nmo_arena* arena1 = nmo_session_get_arena(session1);
    nmo_arena* arena2 = nmo_session_get_arena(session2);
    nmo_arena* arena3 = nmo_session_get_arena(session3);

    TEST_ASSERT(arena1 != arena2, "Sessions should have different arenas");
    TEST_ASSERT(arena2 != arena3, "Sessions should have different arenas");
    TEST_ASSERT(arena1 != arena3, "Sessions should have different arenas");

    nmo_object_repository* repo1 = nmo_session_get_repository(session1);
    nmo_object_repository* repo2 = nmo_session_get_repository(session2);
    nmo_object_repository* repo3 = nmo_session_get_repository(session3);

    TEST_ASSERT(repo1 != repo2, "Sessions should have different repositories");
    TEST_ASSERT(repo2 != repo3, "Sessions should have different repositories");
    TEST_ASSERT(repo1 != repo3, "Sessions should have different repositories");

    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_session_destroy(session3);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test session file info
 */
static void test_session_file_info(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    /* Get default file info (should be all zeros) */
    nmo_file_info info = nmo_session_get_file_info(session);
    TEST_ASSERT(info.file_version == 0, "Default file_version should be 0");
    TEST_ASSERT(info.object_count == 0, "Default object_count should be 0");

    /* Set file info */
    nmo_file_info new_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 12345,
        .object_count = 42,
        .manager_count = 3,
        .write_mode = 0x01
    };

    int result = nmo_session_set_file_info(session, &new_info);
    TEST_ASSERT(result == NMO_OK, "Failed to set file info");

    /* Verify file info was set */
    info = nmo_session_get_file_info(session);
    TEST_ASSERT(info.file_version == 8, "file_version should be 8");
    TEST_ASSERT(info.ck_version == 0x13022002, "ck_version mismatch");
    TEST_ASSERT(info.file_size == 12345, "file_size mismatch");
    TEST_ASSERT(info.object_count == 42, "object_count should be 42");
    TEST_ASSERT(info.manager_count == 3, "manager_count should be 3");
    TEST_ASSERT(info.write_mode == 0x01, "write_mode mismatch");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test context/session with NULL inputs
 */
static void test_null_inputs(void) {
    /* NULL context descriptor is valid */
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Should create context with NULL descriptor");
    nmo_context_release(ctx);

    /* NULL context for session creation should fail */
    nmo_session* session = nmo_session_create(NULL);
    TEST_ASSERT(session == NULL, "Should not create session with NULL context");

    /* NULL context retain/release should be safe */
    nmo_context_retain(NULL);
    nmo_context_release(NULL);

    /* NULL session destroy should be safe */
    nmo_session_destroy(NULL);

    TEST_PASS();
}

/**
 * Test schema registry access from context
 */
static void test_schema_registry_access(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_schema_registry* registry = nmo_context_get_schema_registry(ctx);
    TEST_ASSERT(registry != NULL, "Schema registry should exist");

    /* Registry should be shared across all sessions using this context */
    nmo_session* session1 = nmo_session_create(ctx);
    nmo_session* session2 = nmo_session_create(ctx);

    nmo_context* ctx1 = nmo_session_get_context(session1);
    nmo_context* ctx2 = nmo_session_get_context(session2);

    nmo_schema_registry* reg1 = nmo_context_get_schema_registry(ctx1);
    nmo_schema_registry* reg2 = nmo_context_get_schema_registry(ctx2);

    TEST_ASSERT(reg1 == registry, "Session1 should access same registry");
    TEST_ASSERT(reg2 == registry, "Session2 should access same registry");

    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_context_release(ctx);

    TEST_PASS();
}

int main(void) {
    printf("Running context and session tests...\n\n");

    test_context_create_default();
    test_context_create_custom();
    test_context_refcounting();
    test_session_create();
    test_session_borrows_context();
    test_multiple_sessions();
    test_session_file_info();
    test_null_inputs();
    test_schema_registry_access();

    printf("\nAll context and session tests completed!\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
