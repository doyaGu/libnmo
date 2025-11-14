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
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_object.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to get temp directory */
static const char* get_temp_dir(void) {
#ifdef _WIN32
    const char* temp = getenv("TEMP");
    if (!temp) temp = getenv("TMP");
    if (!temp) temp = "C:\\\\Windows\\\\Temp";
    return temp;
#else
    return "/tmp";
#endif
}

/* Helper to build temp file path */
static void build_temp_path(char* buffer, size_t size, const char* filename) {
    snprintf(buffer, size, "%s/%s", get_temp_dir(), filename);
}

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
    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test_empty.nmo");
    int result = nmo_save_file(session, filepath, NMO_SAVE_DEFAULT);
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
    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test_single.nmo");
    int result = nmo_save_file(session, filepath, NMO_SAVE_DEFAULT);
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
    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test_multiple.nmo");
    int result = nmo_save_file(session, filepath, NMO_SAVE_DEFAULT);
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
    char filepath1[256], filepath2[256], filepath3[256];
    build_temp_path(filepath1, sizeof(filepath1), "test_flags1.nmo");
    build_temp_path(filepath2, sizeof(filepath2), "test_flags2.nmo");
    build_temp_path(filepath3, sizeof(filepath3), "test_flags3.nmo");
    
    int result1 = nmo_save_file(session, filepath1, NMO_SAVE_COMPRESSED);
    ASSERT_EQ(NMO_OK, result1);

    int result2 = nmo_save_file(session, filepath2, NMO_SAVE_SEQUENTIAL_IDS);
    ASSERT_EQ(NMO_OK, result2);

    int result3 = nmo_save_file(session, filepath3,
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
    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test_id_remap.nmo");
    int result = nmo_save_file(session, filepath, NMO_SAVE_DEFAULT);
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
    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test.nmo");
    int result1 = nmo_save_file(NULL, filepath, NMO_SAVE_DEFAULT);
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
    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test_large.nmo");
    int result = nmo_save_file(session, filepath, NMO_SAVE_DEFAULT);
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
    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test_file_info.nmo");
    int result = nmo_save_file(session, filepath, NMO_SAVE_DEFAULT);
    ASSERT_EQ(NMO_OK, result);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(save_pipeline, reference_only_save) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_t *obj = (nmo_object_t *) nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void *));
    ASSERT_NOT_NULL(obj);
    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0x0BADF00D;
    obj->name = "ReferenceObject";
    obj->arena = arena;

    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, obj));

    nmo_file_info_t file_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .file_size = 0,
        .object_count = 1,
        .manager_count = 0,
        .write_mode = 0x01
    };
    nmo_session_set_file_info(session, &file_info);

    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test_reference_only.nmo");
    ASSERT_EQ(NMO_OK, nmo_save_file(session, filepath, NMO_SAVE_AS_OBJECTS));

    FILE *fp = fopen(filepath, "rb");
    ASSERT_NOT_NULL(fp);

    nmo_file_header_t header;
    ASSERT_EQ(1u, fread(&header, sizeof(header), 1, fp));

    void *hdr1_buf = malloc(header.hdr1_pack_size);
    ASSERT_NOT_NULL(hdr1_buf);
    ASSERT_EQ(1u, fread(hdr1_buf, header.hdr1_pack_size, 1, fp));

    void *data_buf = malloc(header.data_pack_size);
    ASSERT_NOT_NULL(data_buf);
    ASSERT_EQ(1u, fread(data_buf, header.data_pack_size, 1, fp));
    fclose(fp);

    nmo_arena_t *parse_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(parse_arena);

    nmo_header1_t hdr1 = {0};
    hdr1.object_count = header.object_count;
    nmo_result_t parse_result = nmo_header1_parse(hdr1_buf, header.hdr1_pack_size, &hdr1, parse_arena);
    ASSERT_EQ(NMO_OK, parse_result.code);
    ASSERT_TRUE((hdr1.objects[0].flags & NMO_OBJECT_REFERENCE_FLAG) != 0);

    nmo_data_section_t data_section = {0};
    data_section.manager_count = header.manager_count;
    data_section.object_count = header.object_count;
    parse_result = nmo_data_section_parse(data_buf, header.data_pack_size, header.file_version, &data_section, NULL, parse_arena);
    ASSERT_EQ(NMO_OK, parse_result.code);
    ASSERT_NULL(data_section.objects[0].chunk);
    ASSERT_EQ(0u, data_section.objects[0].data_size);

    nmo_arena_destroy(parse_arena);
    free(hdr1_buf);
    free(data_buf);
    remove(filepath);

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
    REGISTER_TEST(save_pipeline, reference_only_save);
TEST_MAIN_END()
