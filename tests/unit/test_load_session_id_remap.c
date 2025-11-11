/**
 * @file test_load_session_id_remap.c
 * @brief Test suite for load session and ID remapping (Phase 7.2 and 7.3)
 */

#include "session/nmo_load_session.h"
#include "session/nmo_id_remap.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
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
 * Test basic load session creation and destruction
 */
static void test_load_session_create_destroy(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    TEST_ASSERT(arena != NULL, "Failed to create arena");

    nmo_object_repository* repo = nmo_object_repository_create(arena);
    TEST_ASSERT(repo != NULL, "Failed to create repository");

    nmo_load_session* session = nmo_load_session_start(repo, 100);
    TEST_ASSERT(session != NULL, "Failed to create load session");

    nmo_object_id id_base = nmo_load_session_get_id_base(session);
    TEST_ASSERT(id_base == 1, "ID base should be 1 for empty repository");

    nmo_object_id max_saved = nmo_load_session_get_max_saved_id(session);
    TEST_ASSERT(max_saved == 100, "Max saved ID should be 100");

    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);

    TEST_PASS();
}

/**
 * Test load session with existing objects
 */
static void test_load_session_with_existing_objects(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    TEST_ASSERT(arena != NULL, "Failed to create arena");

    nmo_object_repository* repo = nmo_object_repository_create(arena);
    TEST_ASSERT(repo != NULL, "Failed to create repository");

    /* Add some existing objects */
    for (int i = 1; i <= 5; i++) {
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                         sizeof(void*));
        memset(obj, 0, sizeof(nmo_object));
        obj->id = (nmo_object_id)i;
        obj->class_id = 0x00000001;
        obj->arena = arena;
        nmo_object_repository_add(repo, obj);
    }

    /* Start load session */
    nmo_load_session* session = nmo_load_session_start(repo, 50);
    TEST_ASSERT(session != NULL, "Failed to create load session");

    nmo_object_id id_base = nmo_load_session_get_id_base(session);
    TEST_ASSERT(id_base == 6, "ID base should be 6 (max existing ID + 1)");

    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);

    TEST_PASS();
}

/**
 * Test registering objects in load session
 */
static void test_load_session_register(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    TEST_ASSERT(arena != NULL, "Failed to create arena");

    nmo_object_repository* repo = nmo_object_repository_create(arena);
    TEST_ASSERT(repo != NULL, "Failed to create repository");

    nmo_load_session* session = nmo_load_session_start(repo, 10);
    TEST_ASSERT(session != NULL, "Failed to create load session");

    /* Create and register objects */
    for (int i = 0; i < 10; i++) {
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                         sizeof(void*));
        memset(obj, 0, sizeof(nmo_object));
        obj->id = (nmo_object_id)(100 + i);  // Runtime IDs
        obj->class_id = 0x00000001;
        obj->arena = arena;
        nmo_object_repository_add(repo, obj);

        /* Register with file ID */
        int result = nmo_load_session_register(session, obj, (nmo_object_id)i);
        TEST_ASSERT(result == NMO_OK, "Failed to register object");
    }

    /* Try to register duplicate - should fail */
    nmo_object* dup_obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                          sizeof(void*));
    memset(dup_obj, 0, sizeof(nmo_object));
    dup_obj->id = 999;
    dup_obj->class_id = 0x00000001;
    dup_obj->arena = arena;

    int result = nmo_load_session_register(session, dup_obj, 0);  // Duplicate file ID 0
    TEST_ASSERT(result == NMO_ERR_INVALID_STATE, "Should reject duplicate file ID");

    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);

    TEST_PASS();
}

/**
 * Test building remap table from load session
 */
static void test_build_remap_table(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    TEST_ASSERT(arena != NULL, "Failed to create arena");

    nmo_object_repository* repo = nmo_object_repository_create(arena);
    TEST_ASSERT(repo != NULL, "Failed to create repository");

    nmo_load_session* session = nmo_load_session_start(repo, 5);
    TEST_ASSERT(session != NULL, "Failed to create load session");

    /* Register objects */
    for (int i = 0; i < 5; i++) {
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                         sizeof(void*));
        memset(obj, 0, sizeof(nmo_object));
        obj->id = (nmo_object_id)(100 + i);  // Runtime IDs: 100-104
        obj->class_id = 0x00000001;
        obj->arena = arena;
        nmo_object_repository_add(repo, obj);

        nmo_load_session_register(session, obj, (nmo_object_id)i);  // File IDs: 0-4
    }

    /* Build remap table */
    nmo_id_remap_table* table = nmo_build_remap_table(session);
    TEST_ASSERT(table != NULL, "Failed to build remap table");

    size_t count = nmo_id_remap_table_get_count(table);
    TEST_ASSERT(count == 5, "Remap table should have 5 entries");

    /* Test lookups */
    for (int i = 0; i < 5; i++) {
        nmo_object_id file_id = (nmo_object_id)i;
        nmo_object_id runtime_id;

        int result = nmo_id_remap_lookup(table, file_id, &runtime_id);
        TEST_ASSERT(result == NMO_OK, "Lookup should succeed");
        TEST_ASSERT(runtime_id == (nmo_object_id)(100 + i), "Runtime ID mismatch");
    }

    /* Test lookup of non-existent ID */
    nmo_object_id runtime_id;
    int result = nmo_id_remap_lookup(table, 999, &runtime_id);
    TEST_ASSERT(result == NMO_ERR_INVALID_ARGUMENT, "Should fail for non-existent ID");

    nmo_id_remap_table_destroy(table);
    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);

    TEST_PASS();
}

/**
 * Test remap table iteration
 */
static void test_remap_table_iteration(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    TEST_ASSERT(arena != NULL, "Failed to create arena");

    nmo_object_repository* repo = nmo_object_repository_create(arena);
    TEST_ASSERT(repo != NULL, "Failed to create repository");

    nmo_load_session* session = nmo_load_session_start(repo, 3);
    TEST_ASSERT(session != NULL, "Failed to create load session");

    /* Register objects */
    for (int i = 0; i < 3; i++) {
        nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                         sizeof(void*));
        memset(obj, 0, sizeof(nmo_object));
        obj->id = (nmo_object_id)(50 + i);
        obj->class_id = 0x00000001;
        obj->arena = arena;
        nmo_object_repository_add(repo, obj);
        nmo_load_session_register(session, obj, (nmo_object_id)i);
    }

    nmo_id_remap_table* table = nmo_build_remap_table(session);
    TEST_ASSERT(table != NULL, "Failed to build remap table");

    /* Iterate through entries and verify all exist */
    size_t count = nmo_id_remap_table_get_count(table);
    int found[3] = {0, 0, 0};  // Track which file IDs we've seen

    for (size_t i = 0; i < count; i++) {
        const nmo_id_remap_entry* entry = nmo_id_remap_table_get_entry(table, i);
        TEST_ASSERT(entry != NULL, "Entry should not be NULL");

        /* Verify it's one of our expected entries */
        TEST_ASSERT(entry->old_id < 3, "Old ID out of expected range");
        TEST_ASSERT(entry->new_id == (nmo_object_id)(50 + entry->old_id),
                    "New ID doesn't match expected mapping");

        /* Mark as found */
        found[entry->old_id] = 1;
    }

    /* Verify all entries were found */
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT(found[i] == 1, "Missing expected entry");
    }

    /* Test out of bounds */
    const nmo_id_remap_entry* entry = nmo_id_remap_table_get_entry(table, 999);
    TEST_ASSERT(entry == NULL, "Out of bounds should return NULL");

    nmo_id_remap_table_destroy(table);
    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);

    TEST_PASS();
}

/**
 * Test ID remap plan creation for save
 */
static void test_id_remap_plan_create(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    TEST_ASSERT(arena != NULL, "Failed to create arena");

    nmo_object_repository* repo = nmo_object_repository_create(arena);
    TEST_ASSERT(repo != NULL, "Failed to create repository");

    /* Create objects */
    nmo_object* objects[5];
    for (int i = 0; i < 5; i++) {
        objects[i] = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                    sizeof(void*));
        memset(objects[i], 0, sizeof(nmo_object));
        objects[i]->id = (nmo_object_id)(200 + i);  // Runtime IDs: 200-204
        objects[i]->class_id = 0x00000001;
        objects[i]->arena = arena;
        nmo_object_repository_add(repo, objects[i]);
    }

    /* Create remap plan */
    nmo_id_remap_plan* plan = nmo_id_remap_plan_create(repo, objects, 5);
    TEST_ASSERT(plan != NULL, "Failed to create remap plan");

    size_t remapped_count = nmo_id_remap_plan_get_remapped_count(plan);
    TEST_ASSERT(remapped_count == 5, "Should have remapped 5 objects");

    /* Test lookups - runtime IDs should map to sequential file IDs (0-4) */
    for (int i = 0; i < 5; i++) {
        nmo_object_id runtime_id = (nmo_object_id)(200 + i);
        nmo_object_id file_id;

        int result = nmo_id_remap_plan_lookup(plan, runtime_id, &file_id);
        TEST_ASSERT(result == NMO_OK, "Lookup should succeed");
        TEST_ASSERT(file_id == (nmo_object_id)i, "File ID should be sequential");
    }

    nmo_id_remap_plan_destroy(plan);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);

    TEST_PASS();
}

/**
 * Test remap plan with large number of objects
 */
static void test_remap_plan_large(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 64 * 1024);
    TEST_ASSERT(arena != NULL, "Failed to create arena");

    nmo_object_repository* repo = nmo_object_repository_create(arena);
    TEST_ASSERT(repo != NULL, "Failed to create repository");

    /* Create 100 objects */
    const int count = 100;
    nmo_object** objects = (nmo_object**)malloc(count * sizeof(nmo_object*));
    TEST_ASSERT(objects != NULL, "Failed to allocate object array");

    for (int i = 0; i < count; i++) {
        objects[i] = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                    sizeof(void*));
        memset(objects[i], 0, sizeof(nmo_object));
        objects[i]->id = (nmo_object_id)(1000 + i);
        objects[i]->class_id = 0x00000001;
        objects[i]->arena = arena;
        nmo_object_repository_add(repo, objects[i]);
    }

    /* Create remap plan */
    nmo_id_remap_plan* plan = nmo_id_remap_plan_create(repo, objects, count);
    TEST_ASSERT(plan != NULL, "Failed to create remap plan");

    size_t remapped_count = nmo_id_remap_plan_get_remapped_count(plan);
    TEST_ASSERT(remapped_count == (size_t)count, "Should have remapped all objects");

    /* Verify all lookups work */
    for (int i = 0; i < count; i++) {
        nmo_object_id runtime_id = (nmo_object_id)(1000 + i);
        nmo_object_id file_id;

        int result = nmo_id_remap_plan_lookup(plan, runtime_id, &file_id);
        TEST_ASSERT(result == NMO_OK, "Lookup should succeed");
        TEST_ASSERT(file_id == (nmo_object_id)i, "File ID should be sequential");
    }

    free(objects);
    nmo_id_remap_plan_destroy(plan);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);

    TEST_PASS();
}

/**
 * Test load session end
 */
static void test_load_session_end(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 1024);
    TEST_ASSERT(arena != NULL, "Failed to create arena");

    nmo_object_repository* repo = nmo_object_repository_create(arena);
    TEST_ASSERT(repo != NULL, "Failed to create repository");

    nmo_load_session* session = nmo_load_session_start(repo, 5);
    TEST_ASSERT(session != NULL, "Failed to create load session");

    /* Register an object */
    nmo_object* obj = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                     sizeof(void*));
    memset(obj, 0, sizeof(nmo_object));
    obj->id = 100;
    obj->class_id = 0x00000001;
    obj->arena = arena;
    nmo_object_repository_add(repo, obj);

    int result = nmo_load_session_register(session, obj, 0);
    TEST_ASSERT(result == NMO_OK, "Should register successfully");

    /* End session */
    result = nmo_load_session_end(session);
    TEST_ASSERT(result == NMO_OK, "Should end successfully");

    /* Try to register after end - should fail */
    nmo_object* obj2 = (nmo_object*)nmo_arena_alloc(arena, sizeof(nmo_object),
                                                      sizeof(void*));
    memset(obj2, 0, sizeof(nmo_object));
    obj2->id = 101;
    obj2->class_id = 0x00000001;
    obj2->arena = arena;

    result = nmo_load_session_register(session, obj2, 1);
    TEST_ASSERT(result == NMO_ERR_INVALID_ARGUMENT, "Should reject after session end");

    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);

    TEST_PASS();
}

int main(void) {
    printf("Running load session and ID remapping tests...\n\n");

    test_load_session_create_destroy();
    test_load_session_with_existing_objects();
    test_load_session_register();
    test_build_remap_table();
    test_remap_table_iteration();
    test_id_remap_plan_create();
    test_remap_plan_large();
    test_load_session_end();

    printf("\nAll load session and ID remapping tests completed!\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
