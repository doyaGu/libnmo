/**
 * @file test_save_pipeline.c
 * @brief Test suite for save pipeline (Phase 10)
 */

#include "test_framework.h"
#include "app/nmo_parser.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "session/nmo_object_repository.h"
#include "app/nmo_plugin.h"
#include "core/nmo_arena.h"
#include "format/nmo_header.h"
#include "format/nmo_header1.h"
#include "format/nmo_data.h"
#include "format/nmo_object.h"
#include "schema/nmo_builtin_types.h"       /* for nmo_register_builtin_types */
#include "schema/nmo_ckobject_hierarchy.h"  /* for nmo_register_ckobject_hierarchy */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"

/* Initialize schemas once for all tests */
static void init_schemas_once(nmo_context_t *ctx) {
    nmo_schema_registry_t *registry = nmo_context_get_schema_registry(ctx);
    nmo_arena_t *arena = nmo_context_get_arena(ctx);
    
    if (!registry || !arena) {
        fprintf(stderr, "ERROR: Cannot get registry or arena from context\n");
        return;
    }
    
    /* Register base types (scalar, math, virtools) */
    nmo_result_t result = nmo_register_builtin_types(registry, arena);
    if (result.code != NMO_OK) {
        fprintf(stderr, "ERROR: Failed to register builtin types: %d\n", result.code);
        if (result.error) {
            fprintf(stderr, "       %s\n", result.error->message);
        }
        return;
    }
    
    /* Register all CKObject class hierarchy schemas */
    result = nmo_register_ckobject_hierarchy(registry, arena);
    if (result.code != NMO_OK) {
        fprintf(stderr, "ERROR: Failed to register CKObject hierarchy: %d\n", result.code);
        if (result.error) {
            fprintf(stderr, "       %s\n", result.error->message);
        }
        return;
    }
}

typedef struct saved_section {
    void *data;
    size_t size;
    int needs_free;
} saved_section_t;

static void read_section(FILE *fp, uint32_t size, saved_section_t *out) {
    out->data = NULL;
    out->size = 0;
    out->needs_free = 0;

    if (size == 0) {
        return;
    }

    void *buffer = malloc(size);
    ASSERT_NOT_NULL(buffer);
    ASSERT_EQ(size, fread(buffer, 1, size, fp));

    out->data = buffer;
    out->size = size;
    out->needs_free = 1;
}

static void free_section(saved_section_t *section) {
    if (section->needs_free && section->data != NULL) {
        free(section->data);
    }
    section->data = NULL;
    section->size = 0;
    section->needs_free = 0;
}

static void ensure_unpacked(saved_section_t *section, uint32_t unpack_size) {
    if (unpack_size == 0 || unpack_size == section->size) {
        return;
    }

    void *dest = malloc(unpack_size);
    ASSERT_NOT_NULL(dest);

    mz_ulong dest_len = unpack_size;
    int status = mz_uncompress((unsigned char *)dest,
                               &dest_len,
                               (const unsigned char *)section->data,
                               (mz_ulong)section->size);
    ASSERT_EQ(MZ_OK, status);
    ASSERT_EQ(unpack_size, dest_len);

    free_section(section);
    section->data = dest;
    section->size = dest_len;
    section->needs_free = 1;
}

static void add_repeated_objects(nmo_session_t *session,
                                 size_t count,
                                 const char *name_template,
                                 uint32_t class_base) {
    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *obj = (nmo_object_t *) nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void *));
        ASSERT_NOT_NULL(obj);
        memset(obj, 0, sizeof(nmo_object_t));
        /* Use real class IDs: 1=CKObject (all objects use same class for simplicity) */
        obj->class_id = 1;  /* CKCID_OBJECT */

        size_t name_len = strlen(name_template);
        char *name_copy = (char *) nmo_arena_alloc(arena, name_len + 1, 1);
        ASSERT_NOT_NULL(name_copy);
        memcpy(name_copy, name_template, name_len + 1);
        obj->name = name_copy;
        obj->arena = arena;

        ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, obj));
    }
}

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

static int test_plugin_init(const nmo_plugin_t *plugin, nmo_context_t *ctx) {
    (void)plugin;
    (void)ctx;
    return NMO_OK;
}

static const nmo_plugin_t save_pipeline_test_plugin = {
    .name = "SavePipelineTestPlugin",
    .version = 7,
    .guid = {0xA1B2C3D4u, 0x5E6F7788u},
    .category = NMO_PLUGIN_MANAGER_DLL,
    .init = test_plugin_init,
    .shutdown = NULL,
    .register_managers = NULL,
};

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
    init_schemas_once(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
    ASSERT_NOT_NULL(obj);

    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0x00000001;  /* CKCID_OBJECT */
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
    init_schemas_once(ctx);

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
        obj->class_id = 0x00000002;  /* CKCID_SCENEOBJECT */

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
    init_schemas_once(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
    ASSERT_NOT_NULL(obj);

    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0x00000001;  /* CKCID_OBJECT */
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
    init_schemas_once(ctx);

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
        obj->class_id = 0x00000003;  /* CKCID_3DENTITY */

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
    init_schemas_once(ctx);

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
    init_schemas_once(ctx);

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
        obj->class_id = 0x00000001;  /* CKCID_OBJECT */

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
    init_schemas_once(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t* arena = nmo_session_get_arena(session);
    nmo_object_repository_t* repo = nmo_session_get_repository(session);

    /* Create a test object */
    nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
    ASSERT_NOT_NULL(obj);

    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0x00000001;  /* CKCID_OBJECT */
    obj->name = "FileInfoTest";
    obj->arena = arena;

    nmo_object_repository_add(repo, obj);

    /* Set custom file info */
    nmo_file_info_t file_info = {
        .file_version = 9,              /* Custom version */
        .file_version2 = 2,             /* Custom secondary version */
        .ck_version = 0x99999999,       /* Custom CK version */
        .product_version = 0x12345678,  /* Custom product metadata */
        .product_build = 0x9ABCDEF0,
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

    FILE *fp = fopen(filepath, "rb");
    ASSERT_NOT_NULL(fp);

    nmo_file_header_t header;
    ASSERT_EQ(1u, fread(&header, sizeof(header), 1, fp));
    fclose(fp);

    ASSERT_EQ(file_info.file_version, header.file_version);
    ASSERT_EQ(file_info.file_version2, header.file_version2);
    ASSERT_EQ(file_info.ck_version, header.ck_version);
    ASSERT_EQ(file_info.product_version, header.product_version);
    ASSERT_EQ(file_info.product_build, header.product_build);
    nmo_file_info_t updated_info = nmo_session_get_file_info(session);
    ASSERT_EQ(updated_info.write_mode, header.file_write_mode);

    remove(filepath);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(save_pipeline, reference_only_save) {
    nmo_context_desc_t desc = {0};
    nmo_context_t* ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    init_schemas_once(ctx);

    nmo_session_t* session = nmo_session_create(ctx);
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

    saved_section_t hdr1_section = {0};
    saved_section_t data_section_raw = {0};
    read_section(fp, header.hdr1_pack_size, &hdr1_section);
    read_section(fp, header.data_pack_size, &data_section_raw);
    fclose(fp);

    ensure_unpacked(&hdr1_section, header.hdr1_unpack_size);
    ensure_unpacked(&data_section_raw, header.data_unpack_size);

    nmo_arena_t *parse_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(parse_arena);

    nmo_header1_t hdr1 = {0};
    hdr1.object_count = header.object_count;
    nmo_result_t parse_result = nmo_header1_parse(hdr1_section.data, hdr1_section.size, &hdr1, parse_arena);
    ASSERT_EQ(NMO_OK, parse_result.code);
    ASSERT_TRUE((hdr1.objects[0].flags & NMO_OBJECT_REFERENCE_FLAG) != 0);

    nmo_data_section_t data_section = {0};
    data_section.manager_count = header.manager_count;
    data_section.object_count = header.object_count;
    parse_result = nmo_data_section_parse(data_section_raw.data,
                                          data_section_raw.size,
                                          header.file_version,
                                          &data_section,
                                          NULL,
                                          parse_arena);
    ASSERT_EQ(NMO_OK, parse_result.code);
    ASSERT_NULL(data_section.objects[0].chunk);
    ASSERT_EQ(0u, data_section.objects[0].data_size);

    nmo_arena_destroy(parse_arena);
    free_section(&hdr1_section);
    free_section(&data_section_raw);
    remove(filepath);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(save_pipeline, included_files_round_trip) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    init_schemas_once(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_t *obj = (nmo_object_t *) nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void *));
    ASSERT_NOT_NULL(obj);
    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0x00000001;  /* CKCID_OBJECT */
    obj->name = "IncludedCarrier";
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

    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_EQ(NMO_OK,
              nmo_session_add_included_file(session,
                                            "payload.bin",
                                            payload,
                                            sizeof(payload)));

    char filepath[256];
    build_temp_path(filepath, sizeof(filepath), "test_included.nmo");
    ASSERT_EQ(NMO_OK, nmo_save_file(session, filepath, NMO_SAVE_DEFAULT));

    FILE *fp = fopen(filepath, "rb");
    ASSERT_NOT_NULL(fp);

    nmo_file_header_t header;
    ASSERT_EQ(1u, fread(&header, sizeof(header), 1, fp));

    fseek(fp, header.hdr1_pack_size + header.data_pack_size, SEEK_CUR);

    uint32_t name_len = 0;
    ASSERT_EQ(1u, fread(&name_len, sizeof(uint32_t), 1, fp));
    ASSERT_EQ(strlen("payload.bin"), name_len);

    char name_buf[64] = {0};
    ASSERT_EQ(name_len, fread(name_buf, 1, name_len, fp));
    ASSERT_STR_EQ("payload.bin", name_buf);

    uint32_t data_size = 0;
    ASSERT_EQ(1u, fread(&data_size, sizeof(uint32_t), 1, fp));
    ASSERT_EQ(sizeof(payload), data_size);

    uint8_t file_payload[sizeof(payload)] = {0};
    ASSERT_EQ(sizeof(payload), fread(file_payload, 1, sizeof(payload), fp));
    ASSERT_EQ(0, memcmp(file_payload, payload, sizeof(payload)));
    fclose(fp);

    nmo_session_t *loaded = nmo_session_load(ctx, filepath);
    ASSERT_NOT_NULL(loaded);

    uint32_t included_count = 0;
    nmo_included_file_t *files = nmo_session_get_included_files(loaded, &included_count);
    ASSERT_EQ(1u, included_count);
    ASSERT_NOT_NULL(files);
    ASSERT_STR_EQ("payload.bin", files[0].name);
    ASSERT_EQ(sizeof(payload), files[0].size);
    ASSERT_EQ(0, memcmp(files[0].data, payload, sizeof(payload)));

    nmo_session_destroy(loaded);
    remove(filepath);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(save_pipeline, plugin_dependencies_from_plugin_manager) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    init_schemas_once(ctx);

    nmo_plugin_manager_t *plugin_mgr = nmo_context_get_plugin_manager(ctx);
    ASSERT_NOT_NULL(plugin_mgr);

    nmo_plugin_registration_desc_t reg_desc = {
        .plugins = &save_pipeline_test_plugin,
        .plugin_count = 1
    };
    ASSERT_EQ(NMO_OK, nmo_plugin_manager_register(plugin_mgr, &reg_desc));

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_object_repository_t *repo = nmo_session_get_repository(session);

    nmo_object_t *obj = (nmo_object_t *) nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void *));
    ASSERT_NOT_NULL(obj);
    memset(obj, 0, sizeof(nmo_object_t));
    obj->class_id = 0x00000001;  /* CKCID_OBJECT */
    obj->name = "PluginCarrier";
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
    build_temp_path(filepath, sizeof(filepath), "test_plugin_deps.nmo");
    ASSERT_EQ(NMO_OK, nmo_save_file(session, filepath, NMO_SAVE_DEFAULT));

    FILE *fp = fopen(filepath, "rb");
    ASSERT_NOT_NULL(fp);

    nmo_file_header_t header;
    ASSERT_EQ(1u, fread(&header, sizeof(header), 1, fp));
    ASSERT_GT(header.hdr1_pack_size, 0u);

    saved_section_t hdr1_section = {0};
    read_section(fp, header.hdr1_pack_size, &hdr1_section);
    fclose(fp);

    ensure_unpacked(&hdr1_section, header.hdr1_unpack_size);

    nmo_arena_t *parse_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(parse_arena);

    nmo_header1_t hdr1 = {0};
    hdr1.object_count = header.object_count;
    nmo_result_t parse_result = nmo_header1_parse(hdr1_section.data, hdr1_section.size, &hdr1, parse_arena);
    ASSERT_EQ(NMO_OK, parse_result.code);
    ASSERT_EQ(1u, hdr1.plugin_dep_count);
    ASSERT_NOT_NULL(hdr1.plugin_deps);
    ASSERT_EQ(save_pipeline_test_plugin.guid.d1, hdr1.plugin_deps[0].guid.d1);
    ASSERT_EQ(save_pipeline_test_plugin.guid.d2, hdr1.plugin_deps[0].guid.d2);
    ASSERT_EQ(save_pipeline_test_plugin.category, hdr1.plugin_deps[0].category);
    /* Plugin versions are not serialized into Header1; Virtools stores only GUID/category */
    ASSERT_EQ(0u, hdr1.plugin_deps[0].version);

    nmo_arena_destroy(parse_arena);
    free_section(&hdr1_section);
    remove(filepath);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(save_pipeline, compression_modes) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);
    init_schemas_once(ctx);

    const size_t object_count = 64;
    const char *repeat_name = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

    /* Uncompressed baseline */
    nmo_session_t *plain_session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(plain_session);
    add_repeated_objects(plain_session, object_count, repeat_name, 0x71000000);

    nmo_file_info_t plain_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .write_mode = 0
    };
    nmo_session_set_file_info(plain_session, &plain_info);

    char plain_path[256];
    build_temp_path(plain_path, sizeof(plain_path), "test_compression_plain.nmo");
    ASSERT_EQ(NMO_OK, nmo_save_file(plain_session, plain_path, NMO_SAVE_DEFAULT));

    FILE *fp = fopen(plain_path, "rb");
    ASSERT_NOT_NULL(fp);
    nmo_file_header_t plain_header;
    ASSERT_EQ(1u, fread(&plain_header, sizeof(plain_header), 1, fp));
    fclose(fp);

    uint32_t compression_bits = NMO_FILE_WRITE_COMPRESS_HEADER | NMO_FILE_WRITE_COMPRESS_DATA;
    ASSERT_EQ(0u, plain_header.file_write_mode & compression_bits);
    ASSERT_EQ(plain_header.hdr1_pack_size, plain_header.hdr1_unpack_size);
    ASSERT_EQ(plain_header.data_pack_size, plain_header.data_unpack_size);
    remove(plain_path);
    nmo_session_destroy(plain_session);

    /* Forced compression path */
    nmo_session_t *compressed_session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(compressed_session);
    add_repeated_objects(compressed_session, object_count, repeat_name, 0x72000000);

    nmo_file_info_t compressed_info = {
        .file_version = 8,
        .ck_version = 0x13022002,
        .write_mode = 0
    };
    nmo_session_set_file_info(compressed_session, &compressed_info);

    char compressed_path[256];
    build_temp_path(compressed_path, sizeof(compressed_path), "test_compression_forced.nmo");
    ASSERT_EQ(NMO_OK, nmo_save_file(compressed_session, compressed_path, NMO_SAVE_COMPRESSED));

    fp = fopen(compressed_path, "rb");
    ASSERT_NOT_NULL(fp);
    nmo_file_header_t compressed_header;
    ASSERT_EQ(1u, fread(&compressed_header, sizeof(compressed_header), 1, fp));
    fclose(fp);

    ASSERT_NE(0u, compressed_header.file_write_mode & NMO_FILE_WRITE_COMPRESS_HEADER);
    ASSERT_NE(0u, compressed_header.file_write_mode & NMO_FILE_WRITE_COMPRESS_DATA);
    ASSERT_TRUE(compressed_header.hdr1_pack_size < compressed_header.hdr1_unpack_size);
    ASSERT_TRUE(compressed_header.data_pack_size < compressed_header.data_unpack_size);

    remove(compressed_path);
    nmo_session_destroy(compressed_session);
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
    REGISTER_TEST(save_pipeline, included_files_round_trip);
    REGISTER_TEST(save_pipeline, plugin_dependencies_from_plugin_manager);
    REGISTER_TEST(save_pipeline, compression_modes);
TEST_MAIN_END()
