/**
 * @file test_context_session.c
 * @brief Test suite for context and session (Phase 8)
 */

#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_allocator.h"
#include "core/nmo_logger.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Test context creation with default settings
 */
TEST(context_session, create_default) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    /* Check refcount */
    int refcount = nmo_context_get_refcount(ctx);
    ASSERT_EQ(1, refcount);

    /* Check that we have a schema registry */
    nmo_schema_registry_t* schema_reg = nmo_context_get_schema_registry(ctx);
    ASSERT_NOT_NULL(schema_reg);

    /* Check allocator and logger */
    nmo_allocator_t* allocator = nmo_context_get_allocator(ctx);
    ASSERT_NOT_NULL(allocator);

    nmo_logger_t* logger = nmo_context_get_logger(ctx);
    ASSERT_NOT_NULL(logger);

    nmo_context_release(ctx);
}

/**
 * Test context creation with custom settings
 */
TEST(context_session, create_custom) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_logger_t logger = nmo_logger_null();

    nmo_context_desc_t desc = {
        .allocator = &allocator,
        .logger = &logger,
        .thread_pool_size = 4
    };

    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    /* Verify custom settings */
    nmo_allocator_t* ctx_allocator = nmo_context_get_allocator(ctx);
    ASSERT_EQ(&allocator, ctx_allocator);

    nmo_logger_t* ctx_logger = nmo_context_get_logger(ctx);
    ASSERT_EQ(&logger, ctx_logger);

    nmo_context_release(ctx);
}

/**
 * Test context reference counting
 */
TEST(context_session, refcounting) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    int refcount = nmo_context_get_refcount(ctx);
    ASSERT_EQ(1, refcount);

    /* Retain */
    nmo_context_retain(ctx);
    refcount = nmo_context_get_refcount(ctx);
    ASSERT_EQ(2, refcount);

    /* Retain again */
    nmo_context_retain(ctx);
    refcount = nmo_context_get_refcount(ctx);
    ASSERT_EQ(3, refcount);

    /* Release (should not destroy) */
    nmo_context_release(ctx);
    refcount = nmo_context_get_refcount(ctx);
    ASSERT_EQ(2, refcount);

    /* Release again (should not destroy) */
    nmo_context_release(ctx);
    refcount = nmo_context_get_refcount(ctx);
    ASSERT_EQ(1, refcount);

    /* Final release (should destroy) */
    nmo_context_release(ctx);
}

/**
 * Test session creation
 */
TEST(context_session, session_create) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Verify session has borrowed context */
    nmo_context_t* borrowed_ctx = nmo_session_get_context(session);
    ASSERT_EQ(ctx, borrowed_ctx);

    /* Verify session has arena */
    nmo_arena_t* arena = nmo_session_get_arena(session);
    ASSERT_NOT_NULL(arena);

    /* Verify session has repository */
    nmo_object_repository_t* repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test session does not retain context
 */
TEST(context_session, session_borrows_context) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    int initial_refcount = nmo_context_get_refcount(ctx);
    ASSERT_EQ(1, initial_refcount);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Context refcount should NOT change */
    int refcount_after_session = nmo_context_get_refcount(ctx);
    ASSERT_EQ(1, refcount_after_session);

    nmo_session_destroy(session);

    /* Context refcount should still be 1 */
    int refcount_after_destroy = nmo_context_get_refcount(ctx);
    ASSERT_EQ(1, refcount_after_destroy);

    nmo_context_release(ctx);
}

/**
 * Test multiple sessions with same context
 */
TEST(context_session, multiple_sessions) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session1 = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session1);

    nmo_session_t* session2 = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session2);

    nmo_session_t* session3 = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session3);

    /* All sessions should share the same context */
    ASSERT_EQ(ctx, nmo_session_get_context(session1));
    ASSERT_EQ(ctx, nmo_session_get_context(session2));
    ASSERT_EQ(ctx, nmo_session_get_context(session3));

    /* But each should have its own arena and repository */
    nmo_arena_t* arena1 = nmo_session_get_arena(session1);
    nmo_arena_t* arena2 = nmo_session_get_arena(session2);
    nmo_arena_t* arena3 = nmo_session_get_arena(session3);

    ASSERT_NE(arena1, arena2);
    ASSERT_NE(arena2, arena3);
    ASSERT_NE(arena1, arena3);

    nmo_object_repository_t* repo1 = nmo_session_get_repository(session1);
    nmo_object_repository_t* repo2 = nmo_session_get_repository(session2);
    nmo_object_repository_t* repo3 = nmo_session_get_repository(session3);

    ASSERT_NE(repo1, repo2);
    ASSERT_NE(repo2, repo3);
    ASSERT_NE(repo1, repo3);

    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_session_destroy(session3);
    nmo_context_release(ctx);
}

/**
 * Test session file info
 */
TEST(context_session, session_file_info) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Get default file info (should be all zeros) */
    nmo_file_info_t info = nmo_session_get_file_info(session);
    ASSERT_EQ(0, info.file_version);
    ASSERT_EQ(0, info.object_count);

    /* Set file info */
    nmo_file_info_t new_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 12345,
        .object_count = 42,
        .manager_count = 3,
        .write_mode = 0x01
    };

    int result = nmo_session_set_file_info(session, &new_info);
    ASSERT_EQ(NMO_OK, result);

    /* Verify file info was set */
    info = nmo_session_get_file_info(session);
    ASSERT_EQ(8, info.file_version);
    ASSERT_EQ(0x13022002, info.ck_version);
    ASSERT_EQ(12345, info.file_size);
    ASSERT_EQ(42, info.object_count);
    ASSERT_EQ(3, info.manager_count);
    ASSERT_EQ(0x01, info.write_mode);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test context/session with NULL inputs
 */
TEST(context_session, null_inputs) {
    /* NULL context descriptor is valid */
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    nmo_context_release(ctx);

    /* NULL context for session creation should fail */
    nmo_session_t* session = nmo_session_create(NULL);
    ASSERT_NULL(session);

    /* NULL context retain/release should be safe */
    nmo_context_retain(NULL);
    nmo_context_release(NULL);

    /* NULL session destroy should be safe */
    nmo_session_destroy(NULL);
}

/**
 * Test schema registry access from context
 */
TEST(context_session, schema_registry_access) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_schema_registry_t* registry = nmo_context_get_schema_registry(ctx);
    ASSERT_NOT_NULL(registry);

    /* Registry should be shared across all sessions using this context */
    nmo_session_t* session1 = nmo_session_create(ctx);
    nmo_session_t* session2 = nmo_session_create(ctx);

    nmo_context_t* ctx1 = nmo_session_get_context(session1);
    nmo_context_t* ctx2 = nmo_session_get_context(session2);

    nmo_schema_registry_t* reg1 = nmo_context_get_schema_registry(ctx1);
    nmo_schema_registry_t* reg2 = nmo_context_get_schema_registry(ctx2);

    ASSERT_EQ(registry, reg1);
    ASSERT_EQ(registry, reg2);

    nmo_session_destroy(session1);
    nmo_session_destroy(session2);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(context_session, create_default);
    REGISTER_TEST(context_session, create_custom);
    REGISTER_TEST(context_session, refcounting);
    REGISTER_TEST(context_session, session_create);
    REGISTER_TEST(context_session, session_borrows_context);
    REGISTER_TEST(context_session, multiple_sessions);
    REGISTER_TEST(context_session, session_file_info);
    REGISTER_TEST(context_session, null_inputs);
    REGISTER_TEST(context_session, schema_registry_access);
TEST_MAIN_END()
