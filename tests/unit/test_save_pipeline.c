/**
 * @file test_save_pipeline.c
 * @brief Test suite for save pipeline (Phase 10)
 */

#include "test_framework.h"
#include "app/nmo_parser.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "session/nmo_object_repository.h"
#include "core/nmo_arena.h"
#include "format/nmo_object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Test saving empty session should fail
 */
TEST(save_pipeline, empty_session_fails) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* Try to save empty session */
    int result = nmo_save_file(session, "/tmp/test_empty.nmo", NMO_SAVE_DEFAULT);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test saving session with single object
 */
TEST(save_pipeline, single_object) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
    ASSERT_NOT_NULL(obj);

    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0x12345678;
    obj->name = "TestObject";
    obj->flags = 0;
    obj->arena = arena;

    int add_result = nmo_object_repository_add(repo, obj);
    ASSERT_EQ(NMO_OK, add_result);

    /* Set file info */
    nmo_file_info_t file_info = {
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
    ASSERT_EQ(NMO_OK, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test saving session with multiple objects
 */
TEST(save_pipeline, multiple_objects) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create multiple test objects */
    const size_t object_count = 10;
    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
        ASSERT_NOT_NULL(obj);

        memset(obj, 0, sizeof(nmo_object_t));
        obj->class_id = 0x10000000 + (uint32_t)i;

        /* Create name */
        char* name = (char*)nmo_arena_alloc(arena, 32, 1);
        snprintf(name, 32, "Object_%zu", i);
        obj->name = name;
        obj->flags = 0;
        obj->arena = arena;

        int add_result = nmo_object_repository_add(repo, obj);
        ASSERT_EQ(NMO_OK, add_result);
    }

    /* Set file info */
    nmo_file_info_t file_info = {
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
    ASSERT_EQ(NMO_OK, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test saving with different flags
 */
TEST(save_pipeline, with_flags) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
    ASSERT_NOT_NULL(obj);

    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0xABCDEF00;
    obj->name = "FlaggedObject";
    obj->arena = arena;

    nmo_object_repository_add(repo, obj);

    /* Set file info */
    nmo_file_info_t file_info = {
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
    ASSERT_EQ(NMO_OK, result1);

    int result2 = nmo_save_file(session, "/tmp/test_flags2.nmo", NMO_SAVE_SEQUENTIAL_IDS);
    ASSERT_EQ(NMO_OK, result2);

    int result3 = nmo_save_file(session, "/tmp/test_flags3.nmo",
                                NMO_SAVE_COMPRESSED | NMO_SAVE_VALIDATE_BEFORE);
    ASSERT_EQ(NMO_OK, result3);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test ID remapping during save
 */
TEST(save_pipeline, id_remapping) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create objects with non-sequential runtime IDs */
    nmo_object_id_t runtime_ids[] = {100, 50, 200, 25, 150};
    const size_t object_count = sizeof(runtime_ids) / sizeof(runtime_ids[0]);

    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
        ASSERT_NOT_NULL(obj);

        memset(obj, 0, sizeof(nmo_object_t));
        obj->class_id = 0x20000000 + (uint32_t)i;

        char* name = (char*)nmo_arena_alloc(arena, 32, 1);
        snprintf(name, 32, "Object_ID_%u", runtime_ids[i]);
        obj->name = name;
        obj->arena = arena;

        /* Add to repository - will assign sequential runtime IDs */
        int add_result = nmo_object_repository_add(repo, obj);
        ASSERT_EQ(NMO_OK, add_result);
    }

    /* Set file info */
    nmo_file_info_t file_info = {
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
    ASSERT_EQ(NMO_OK, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test saving with NULL arguments
 */
TEST(save_pipeline, null_arguments) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    /* NULL session */
    int result1 = nmo_save_file(NULL, "/tmp/test.nmo", NMO_SAVE_DEFAULT);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result1);

    /* NULL path */
    int result2 = nmo_save_file(session, NULL, NMO_SAVE_DEFAULT);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result2);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test large object count save
 */
TEST(save_pipeline, large_count) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create 100 test objects */
    const size_t object_count = 100;
    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
        if (obj == NULL) {
            break;
        }

        memset(obj, 0, sizeof(nmo_object_t));
        obj->class_id = 0x30000000 + (uint32_t)i;

        char* name = (char*)nmo_arena_alloc(arena, 32, 1);
        snprintf(name, 32, "LargeTest_%zu", i);
        obj->name = name;
        obj->arena = arena;

        nmo_object_repository_add(repo, obj);
    }

    /* Set file info */
    nmo_file_info_t file_info = {
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
    ASSERT_EQ(NMO_OK, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

/**
 * Test file info propagation during save
 */
TEST(save_pipeline, file_info_propagation) {
    nmo_context_desc_t desc = {0};  // Zero-initialized for defaults
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
    ASSERT_NOT_NULL(obj);

    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0x40000000;
    obj->name = "FileInfoTest";
    obj->arena = arena;

    nmo_object_repository_add(repo, obj);

    /* Set custom file info */
    nmo_file_info_t file_info = {
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
    ASSERT_EQ(NMO_OK, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(save_pipeline, empty_session_fails);
    REGISTER_TEST(save_pipeline, single_object);
    REGISTER_TEST(save_pipeline, multiple_objects);
    REGISTER_TEST(save_pipeline, with_flags);
    REGISTER_TEST(save_pipeline, id_remapping);
    REGISTER_TEST(save_pipeline, null_arguments);
    REGISTER_TEST(save_pipeline, large_count);
    REGISTER_TEST(save_pipeline, file_info_propagation);
TEST_MAIN_END()
