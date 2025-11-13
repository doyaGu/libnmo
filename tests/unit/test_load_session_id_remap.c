/**
 * @file test_load_session_id_remap.c
 * @brief Test suite for load session and ID remapping (Phase 7.2 and 7.3)
 */

#include "test_framework.h"
#include "session/nmo_load_session.h"
#include "session/nmo_id_remap.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Test basic load session creation and destruction
 */
TEST(load_session_id_remap, create_destroy) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_load_session_t* session = nmo_load_session_start(repo, 100);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t id_base = nmo_load_session_get_id_base(session);
    ASSERT_EQ(1, id_base);

    nmo_object_id_t max_saved = nmo_load_session_get_max_saved_id(session);
    ASSERT_EQ(100, max_saved);

    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

/**
 * Test load session with existing objects
 */
TEST(load_session_id_remap, with_existing_objects) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    /* Add some existing objects */
    for (int i = 1; i <= 5; i++) {
        nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                           sizeof(void*));
        memset(obj, 0, sizeof(nmo_object_t));
        obj->id = (nmo_object_id_t)i;
        obj->class_id = 0x00000001;
        obj->arena = arena;
        nmo_object_repository_add(repo, obj);
    }

    /* Start load session */
    nmo_load_session_t* session = nmo_load_session_start(repo, 50);
    ASSERT_NOT_NULL(session);

    nmo_object_id_t id_base = nmo_load_session_get_id_base(session);
    ASSERT_EQ(6, id_base);

    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

/**
 * Test registering objects in load session
 */
TEST(load_session_id_remap, register_objects) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_load_session_t* session = nmo_load_session_start(repo, 10);
    ASSERT_NOT_NULL(session);

    /* Create and register objects */
    for (int i = 0; i < 10; i++) {
        nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                           sizeof(void*));
        memset(obj, 0, sizeof(nmo_object_t));
        obj->id = (nmo_object_id_t)(100 + i);  // Runtime IDs
        obj->class_id = 0x00000001;
        obj->arena = arena;
        nmo_object_repository_add(repo, obj);

        /* Register with file ID */
        int result = nmo_load_session_register(session, obj, (nmo_object_id_t)i);
        ASSERT_EQ(NMO_OK, result);
    }

    /* Try to register duplicate - should fail */
    nmo_object_t* dup_obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                             sizeof(void*));
    memset(dup_obj, 0, sizeof(nmo_object_t));
    dup_obj->id = 999;
    dup_obj->class_id = 0x00000001;
    dup_obj->arena = arena;

    int result = nmo_load_session_register(session, dup_obj, 0);  // Duplicate file ID 0
    ASSERT_EQ(NMO_ERR_INVALID_STATE, result);

    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

/**
 * Test building remap table from load session
 */
TEST(load_session_id_remap, build_remap_table) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_load_session_t* session = nmo_load_session_start(repo, 5);
    ASSERT_NOT_NULL(session);

    /* Register objects */
    for (int i = 0; i < 5; i++) {
        nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                           sizeof(void*));
        memset(obj, 0, sizeof(nmo_object_t));
        obj->id = (nmo_object_id_t)(100 + i);  // Runtime IDs: 100-104
        obj->class_id = 0x00000001;
        obj->arena = arena;
        nmo_object_repository_add(repo, obj);

        nmo_load_session_register(session, obj, (nmo_object_id_t)i);  // File IDs: 0-4
    }

    /* Build remap table */
    nmo_id_remap_table_t* table = nmo_build_remap_table(session);
    ASSERT_NOT_NULL(table);

    size_t count = nmo_id_remap_table_get_count(table);
    ASSERT_EQ(5, count);

    /* Test lookups */
    for (int i = 0; i < 5; i++) {
        nmo_object_id_t file_id = (nmo_object_id_t)i;
        nmo_object_id_t runtime_id;

        int result = nmo_id_remap_lookup(table, file_id, &runtime_id);
        ASSERT_EQ(NMO_OK, result);
        ASSERT_EQ((nmo_object_id_t)(100 + i), runtime_id);
    }

    /* Test lookup of non-existent ID */
    nmo_object_id_t runtime_id;
    int result = nmo_id_remap_lookup(table, 999, &runtime_id);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, result);

    nmo_id_remap_table_destroy(table);
    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

/**
 * Test remap table iteration
 */
TEST(load_session_id_remap, remap_table_iteration) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_load_session_t* session = nmo_load_session_start(repo, 3);
    ASSERT_NOT_NULL(session);

    /* Register objects */
    for (int i = 0; i < 3; i++) {
        nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                           sizeof(void*));
        memset(obj, 0, sizeof(nmo_object_t));
        obj->id = (nmo_object_id_t)(50 + i);
        obj->class_id = 0x00000001;
        obj->arena = arena;
        nmo_object_repository_add(repo, obj);
        nmo_load_session_register(session, obj, (nmo_object_id_t)i);
    }

    nmo_id_remap_table_t* table = nmo_build_remap_table(session);
    ASSERT_NOT_NULL(table);

    /* Iterate through entries and verify all exist */
    size_t count = nmo_id_remap_table_get_count(table);
    int found[3] = {0, 0, 0};  // Track which file IDs we've seen

    for (size_t i = 0; i < count; i++) {
        const nmo_id_remap_entry_t* entry = &table->entries[i];
        ASSERT_NOT_NULL(entry);

        /* Verify it's one of our expected entries */
        ASSERT_TRUE(entry->old_id < 3);
        ASSERT_EQ((nmo_object_id_t)(50 + entry->old_id), entry->new_id);

        /* Mark as found */
        found[entry->old_id] = 1;
    }

    /* Verify all entries were found */
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(1, found[i]);
    }

    /* Test out of bounds */
    const nmo_id_remap_entry_t* entry = (999 < table->count) ? &table->entries[999] : NULL;
    ASSERT_NULL(entry);

    nmo_id_remap_table_destroy(table);
    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

/**
 * Test ID remap plan creation for save
 */
TEST(load_session_id_remap, id_remap_plan_create) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    /* Create objects */
    nmo_object_t* objects[5];
    for (int i = 0; i < 5; i++) {
        objects[i] = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                    sizeof(void*));
        memset(objects[i], 0, sizeof(nmo_object_t));
        objects[i]->id = (nmo_object_id_t)(200 + i);  // Runtime IDs: 200-204
        objects[i]->class_id = 0x00000001;
        objects[i]->arena = arena;
        nmo_object_repository_add(repo, objects[i]);
    }

    /* Create remap plan */
    nmo_id_remap_plan_t* plan = nmo_id_remap_plan_create(repo, (nmo_object_t**)objects, 5);
    ASSERT_NOT_NULL(plan);

    size_t remapped_count = nmo_id_remap_plan_get_remapped_count(plan);
    ASSERT_EQ(5, remapped_count);

    /* Test lookups - runtime IDs should map to sequential file IDs (0-4) */
    for (int i = 0; i < 5; i++) {
        nmo_object_id_t runtime_id = (nmo_object_id_t)(200 + i);
        nmo_object_id_t file_id;

        nmo_id_remap_table_t* plan_table = nmo_id_remap_plan_get_table(plan);
        int result = nmo_id_remap_lookup(plan_table, runtime_id, &file_id);
        ASSERT_EQ(NMO_OK, result);
        ASSERT_EQ((nmo_object_id_t)i, file_id);
    }

    nmo_id_remap_plan_destroy(plan);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

/**
 * Test remap plan with large number of objects
 */
TEST(load_session_id_remap, remap_plan_large) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    /* Create 100 objects */
    const int count = 100;
    nmo_object_t** objects = (nmo_object_t**)malloc(count * sizeof(nmo_object_t*));
    ASSERT_NOT_NULL(objects);

    for (int i = 0; i < count; i++) {
        objects[i] = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                    sizeof(void*));
        memset(objects[i], 0, sizeof(nmo_object_t));
        objects[i]->id = (nmo_object_id_t)(1000 + i);
        objects[i]->class_id = 0x00000001;
        objects[i]->arena = arena;
        nmo_object_repository_add(repo, objects[i]);
    }

    /* Create remap plan */
    nmo_id_remap_plan_t* plan = nmo_id_remap_plan_create(repo, objects, count);
    ASSERT_NOT_NULL(plan);

    size_t remapped_count = nmo_id_remap_plan_get_remapped_count(plan);
    ASSERT_EQ((size_t)count, remapped_count);

    /* Verify all lookups work */
    for (int i = 0; i < count; i++) {
        nmo_object_id_t runtime_id = (nmo_object_id_t)(1000 + i);
        nmo_object_id_t file_id;

        nmo_id_remap_table_t* plan_table = nmo_id_remap_plan_get_table(plan);
        int result = nmo_id_remap_lookup(plan_table, runtime_id, &file_id);
        ASSERT_EQ(NMO_OK, result);
        ASSERT_EQ((nmo_object_id_t)i, file_id);
    }

    free(objects);
    nmo_id_remap_plan_destroy(plan);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

/**
 * Test load session end
 */
TEST(load_session_id_remap, load_session_end) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_load_session_t* session = nmo_load_session_start(repo, 5);
    ASSERT_NOT_NULL(session);

    /* Register an object */
    nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                       sizeof(void*));
    memset(obj, 0, sizeof(nmo_object_t));
    obj->id = 100;
    obj->class_id = 0x00000001;
    obj->arena = arena;
    nmo_object_repository_add(repo, obj);

    int result = nmo_load_session_register(session, obj, 0);
    ASSERT_EQ(NMO_OK, result);

    /* End session */
    result = nmo_load_session_end(session);
    ASSERT_EQ(NMO_OK, result);

    /* Try to register after end - should fail */
    nmo_object_t* obj2 = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t),
                                                         sizeof(void*));
    memset(obj2, 0, sizeof(nmo_object_t));
    obj2->id = 101;
    obj2->class_id = 0x00000001;
    obj2->arena = arena;

    result = nmo_load_session_register(session, obj2, 1);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result);

    nmo_load_session_destroy(session);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(load_session_id_remap, create_destroy);
    REGISTER_TEST(load_session_id_remap, with_existing_objects);
    REGISTER_TEST(load_session_id_remap, register_objects);
    REGISTER_TEST(load_session_id_remap, build_remap_table);
    REGISTER_TEST(load_session_id_remap, remap_table_iteration);
    REGISTER_TEST(load_session_id_remap, id_remap_plan_create);
    REGISTER_TEST(load_session_id_remap, remap_plan_large);
    REGISTER_TEST(load_session_id_remap, load_session_end);
TEST_MAIN_END()
