/**
 * @file test_save_pipeline.c
 * @brief Test suite for save pipeline (Phase 10)
 */

#include "app/nmo_parser.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "session/nmo_object_repository.h"
#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * Test saving empty session should fail
 */
static void test_save_empty_session_fails(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    /* Try to save empty session */
    int result = nmo_save_file(session, "/tmp/test_empty.nmo", NMO_SAVE_DEFAULT);
    TEST_ASSERT(result == NMO_ERR_INVALID_ARGUMENT, "Empty session save should fail");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test saving session with single object
 */
static void test_save_single_object(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    nmo_arena* arena = nmo_session_get_arena(session);
    nmo_object_repository* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object), sizeof(void*));
    TEST_ASSERT(obj != NULL, "Failed to allocate object");

    memset(obj, 0, sizeof(nmo_object));
    obj->class_id = 0x12345678;
    obj->name = "TestObject";
    obj->flags = 0;
    obj->arena = arena;

    int add_result = nmo_object_repository_add(repo, obj);
    TEST_ASSERT(add_result == NMO_OK, "Failed to add object to repository");

    /* Set file info */
    nmo_file_info file_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 0,
        .object_count = 1,
        .manager_count = 0,
        .write_mode = 0x01
    };
    nmo_session_set_file_info(session, &file_info);

    /* Save to file */
    int result = nmo_save_file(session, "/tmp/test_single.nmo", NMO_SAVE_DEFAULT);
    TEST_ASSERT(result == NMO_OK, "Failed to save single object");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test saving session with multiple objects
 */
static void test_save_multiple_objects(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    nmo_arena* arena = nmo_session_get_arena(session);
    nmo_object_repository* repo = nmo_session_get_repository(session);

    /* Create multiple test objects */
    const size_t object_count = 10;
    for (size_t i = 0; i < object_count; i++) {
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object), sizeof(void*));
        TEST_ASSERT(obj != NULL, "Failed to allocate object");

        memset(obj, 0, sizeof(nmo_object));
        obj->class_id = 0x10000000 + (uint32_t)i;

        /* Create name */
        char* name = (char*)nmo_arena_alloc(arena, 32, 1);
        snprintf(name, 32, "Object_%zu", i);
        obj->name = name;
        obj->flags = 0;
        obj->arena = arena;

        int add_result = nmo_object_repository_add(repo, obj);
        TEST_ASSERT(add_result == NMO_OK, "Failed to add object to repository");
    }

    /* Set file info */
    nmo_file_info file_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 0,
        .object_count = object_count,
        .manager_count = 0,
        .write_mode = 0x01
    };
    nmo_session_set_file_info(session, &file_info);

    /* Save to file */
    int result = nmo_save_file(session, "/tmp/test_multiple.nmo", NMO_SAVE_DEFAULT);
    TEST_ASSERT(result == NMO_OK, "Failed to save multiple objects");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test saving with different flags
 */
static void test_save_with_flags(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    nmo_arena* arena = nmo_session_get_arena(session);
    nmo_object_repository* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object), sizeof(void*));
    TEST_ASSERT(obj != NULL, "Failed to allocate object");

    memset(obj, 0, sizeof(nmo_object));
    obj->class_id = 0xABCDEF00;
    obj->name = "FlaggedObject";
    obj->arena = arena;

    nmo_object_repository_add(repo, obj);

    /* Set file info */
    nmo_file_info file_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 0,
        .object_count = 1,
        .manager_count = 0,
        .write_mode = 0x01
    };
    nmo_session_set_file_info(session, &file_info);

    /* Test different flags */
    int result1 = nmo_save_file(session, "/tmp/test_flags1.nmo", NMO_SAVE_COMPRESSED);
    TEST_ASSERT(result1 == NMO_OK, "Failed to save with COMPRESSED flag");

    int result2 = nmo_save_file(session, "/tmp/test_flags2.nmo", NMO_SAVE_SEQUENTIAL_IDS);
    TEST_ASSERT(result2 == NMO_OK, "Failed to save with SEQUENTIAL_IDS flag");

    int result3 = nmo_save_file(session, "/tmp/test_flags3.nmo",
                                NMO_SAVE_COMPRESSED | NMO_SAVE_VALIDATE_BEFORE);
    TEST_ASSERT(result3 == NMO_OK, "Failed to save with combined flags");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test ID remapping during save
 */
static void test_save_id_remapping(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    nmo_arena* arena = nmo_session_get_arena(session);
    nmo_object_repository* repo = nmo_session_get_repository(session);

    /* Create objects with non-sequential runtime IDs */
    nmo_object_id runtime_ids[] = {100, 50, 200, 25, 150};
    const size_t object_count = sizeof(runtime_ids) / sizeof(runtime_ids[0]);

    for (size_t i = 0; i < object_count; i++) {
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object), sizeof(void*));
        TEST_ASSERT(obj != NULL, "Failed to allocate object");

        memset(obj, 0, sizeof(nmo_object));
        obj->class_id = 0x20000000 + (uint32_t)i;

        char* name = (char*)nmo_arena_alloc(arena, 32, 1);
        snprintf(name, 32, "Object_ID_%u", runtime_ids[i]);
        obj->name = name;
        obj->arena = arena;

        /* Add to repository - will assign sequential runtime IDs */
        int add_result = nmo_object_repository_add(repo, obj);
        TEST_ASSERT(add_result == NMO_OK, "Failed to add object to repository");
    }

    /* Set file info */
    nmo_file_info file_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 0,
        .object_count = object_count,
        .manager_count = 0,
        .write_mode = 0x01
    };
    nmo_session_set_file_info(session, &file_info);

    /* Save - should create sequential file IDs */
    int result = nmo_save_file(session, "/tmp/test_id_remap.nmo", NMO_SAVE_DEFAULT);
    TEST_ASSERT(result == NMO_OK, "Failed to save with ID remapping");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test saving with NULL arguments
 */
static void test_save_null_arguments(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    /* NULL session */
    int result1 = nmo_save_file(NULL, "/tmp/test.nmo", NMO_SAVE_DEFAULT);
    TEST_ASSERT(result1 == NMO_ERR_INVALID_ARGUMENT, "NULL session should fail");

    /* NULL path */
    int result2 = nmo_save_file(session, NULL, NMO_SAVE_DEFAULT);
    TEST_ASSERT(result2 == NMO_ERR_INVALID_ARGUMENT, "NULL path should fail");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test large object count save
 */
static void test_save_large_count(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    nmo_arena* arena = nmo_session_get_arena(session);
    nmo_object_repository* repo = nmo_session_get_repository(session);

    /* Create 100 test objects */
    const size_t object_count = 100;
    for (size_t i = 0; i < object_count; i++) {
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object), sizeof(void*));
        if (obj == NULL) {
            printf("Failed to allocate object %zu\n", i);
            break;
        }

        memset(obj, 0, sizeof(nmo_object));
        obj->class_id = 0x30000000 + (uint32_t)i;

        char* name = (char*)nmo_arena_alloc(arena, 32, 1);
        snprintf(name, 32, "LargeTest_%zu", i);
        obj->name = name;
        obj->arena = arena;

        nmo_object_repository_add(repo, obj);
    }

    /* Set file info */
    nmo_file_info file_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 0,
        .object_count = object_count,
        .manager_count = 0,
        .write_mode = 0x01
    };
    nmo_session_set_file_info(session, &file_info);

    /* Save to file */
    int result = nmo_save_file(session, "/tmp/test_large.nmo", NMO_SAVE_DEFAULT);
    TEST_ASSERT(result == NMO_OK, "Failed to save large object count");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

/**
 * Test file info propagation during save
 */
static void test_save_file_info_propagation(void) {
    nmo_context* ctx = nmo_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "Failed to create context");

    nmo_session* session = nmo_session_create(ctx);
    TEST_ASSERT(session != NULL, "Failed to create session");

    nmo_arena* arena = nmo_session_get_arena(session);
    nmo_object_repository* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object), sizeof(void*));
    TEST_ASSERT(obj != NULL, "Failed to allocate object");

    memset(obj, 0, sizeof(nmo_object));
    obj->class_id = 0x40000000;
    obj->name = "FileInfoTest";
    obj->arena = arena;

    nmo_object_repository_add(repo, obj);

    /* Set custom file info */
    nmo_file_info file_info = {
        .file_version = 9,              /* Custom version */
        .ck_version = 0x99999999,       /* Custom CK version */
        .file_size = 0,
        .object_count = 1,
        .manager_count = 5,             /* Custom manager count */
        .write_mode = 0x03              /* Custom write mode */
    };
    nmo_session_set_file_info(session, &file_info);

    /* Save - should use custom file info */
    int result = nmo_save_file(session, "/tmp/test_file_info.nmo", NMO_SAVE_DEFAULT);
    TEST_ASSERT(result == NMO_OK, "Failed to save with custom file info");

    nmo_session_destroy(session);
    nmo_context_release(ctx);

    TEST_PASS();
}

int main(void) {
    printf("Running save pipeline tests...\n\n");

    test_save_empty_session_fails();
    test_save_single_object();
    test_save_multiple_objects();
    test_save_with_flags();
    test_save_id_remapping();
    test_save_null_arguments();
    test_save_large_count();
    test_save_file_info_propagation();

    printf("\nAll save pipeline tests completed!\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
