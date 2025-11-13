/**
 * @file test_object_repository.c
 * @brief Comprehensive unit tests for object repository
 */

#include "test_framework.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to create a test object */
static nmo_object_t* create_test_object(nmo_arena_t* arena, nmo_object_id_t id,
                                      const char* name, nmo_class_id_t class_id) {
    nmo_object_t* obj = (nmo_object_t*)nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void*));
    if (obj == NULL) {
        return NULL;
    }

    memset(obj, 0, sizeof(nmo_object_t));
    obj->id = id;
    obj->class_id = class_id;
    if (name != NULL) {
        size_t len = strlen(name) + 1;
        char* name_copy = (char*)nmo_arena_alloc(arena, len, 1);
        if (name_copy != NULL) {
            strcpy(name_copy, name);
            obj->name = name_copy;
        }
    }

    return obj;
}

TEST(object_repository, create_destroy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);
    ASSERT_EQ(0, nmo_object_repository_get_count(repo));

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, auto_assign_ids) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    /* Create objects with ID 0 - should auto-assign sequential IDs */
    nmo_object_t *obj1 = create_test_object(arena, 0, "Object1", 100);
    nmo_object_t *obj2 = create_test_object(arena, 0, "Object2", 100);
    nmo_object_t *obj3 = create_test_object(arena, 0, "Object3", 100);

    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    ASSERT_NOT_NULL(obj3);

    int result1 = nmo_object_repository_add(repo, obj1);
    int result2 = nmo_object_repository_add(repo, obj2);
    int result3 = nmo_object_repository_add(repo, obj3);

    ASSERT_EQ(NMO_OK, result1);
    ASSERT_EQ(NMO_OK, result2);
    ASSERT_EQ(NMO_OK, result3);

    /* Verify sequential ID assignment */
    ASSERT_EQ(1, obj1->id);
    ASSERT_EQ(2, obj2->id);
    ASSERT_EQ(3, obj3->id);

    ASSERT_EQ(3, nmo_object_repository_get_count(repo));

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, explicit_ids) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(arena, 100, "Obj100", 200);
    nmo_object_t *obj2 = create_test_object(arena, 200, "Obj200", 200);

    int result1 = nmo_object_repository_add(repo, obj1);
    int result2 = nmo_object_repository_add(repo, obj2);

    ASSERT_EQ(NMO_OK, result1);
    ASSERT_EQ(NMO_OK, result2);

    ASSERT_EQ(100, obj1->id);
    ASSERT_EQ(200, obj2->id);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, find_by_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(arena, 42, "FindMe", 300);
    int result = nmo_object_repository_add(repo, obj1);
    ASSERT_EQ(NMO_OK, result);

    nmo_object_t *found = nmo_object_repository_find_by_id(repo, 42);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(obj1, found);

    nmo_object_t *not_found = nmo_object_repository_find_by_id(repo, 999);
    ASSERT_NULL(not_found);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, find_by_name) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(arena, 10, "Alice", 400);
    nmo_object_t *obj2 = create_test_object(arena, 20, "Bob", 400);

    nmo_object_repository_add(repo, obj1);
    nmo_object_repository_add(repo, obj2);

    nmo_object_t *found = nmo_object_repository_find_by_name(repo, "Alice");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(obj1, found);

    found = nmo_object_repository_find_by_name(repo, "Bob");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(obj2, found);

    nmo_object_t *not_found = nmo_object_repository_find_by_name(repo, "Charlie");
    ASSERT_NULL(not_found);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, find_by_class) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    /* Add objects with different classes */
    nmo_object_t *obj1 = create_test_object(arena, 1, "Type500_A", 500);
    nmo_object_t *obj2 = create_test_object(arena, 2, "Type500_B", 500);
    nmo_object_t *obj3 = create_test_object(arena, 3, "Type600", 600);

    nmo_object_repository_add(repo, obj1);
    nmo_object_repository_add(repo, obj2);
    nmo_object_repository_add(repo, obj3);

    size_t count = 0;
    nmo_object_t **results = nmo_object_repository_find_by_class(repo, 500, &count);

    ASSERT_NOT_NULL(results);
    ASSERT_EQ(2, count);

    /* Verify both objects are present */
    int found_obj1 = 0, found_obj2 = 0;
    for (size_t i = 0; i < count; i++) {
        if (results[i] == obj1) found_obj1 = 1;
        if (results[i] == obj2) found_obj2 = 1;
    }
    ASSERT_TRUE(found_obj1 && found_obj2);

    /* Test class 600 */
    results = nmo_object_repository_find_by_class(repo, 600, &count);
    ASSERT_NOT_NULL(results);
    ASSERT_EQ(1, count);
    ASSERT_EQ(obj3, results[0]);

    /* Test non-existent class */
    results = nmo_object_repository_find_by_class(repo, 999, &count);
    ASSERT_NULL(results);
    ASSERT_EQ(0, count);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, remove_object) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(arena, 10, "ToRemove", 700);
    nmo_object_t *obj2 = create_test_object(arena, 20, "ToKeep", 700);

    nmo_object_repository_add(repo, obj1);
    nmo_object_repository_add(repo, obj2);

    ASSERT_EQ(2, nmo_object_repository_get_count(repo));

    /* Remove obj1 */
    int result = nmo_object_repository_remove(repo, 10);
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(1, nmo_object_repository_get_count(repo));

    /* Verify obj1 is gone */
    nmo_object_t *found = nmo_object_repository_find_by_id(repo, 10);
    ASSERT_NULL(found);

    /* Verify obj2 is still there */
    found = nmo_object_repository_find_by_id(repo, 20);
    ASSERT_EQ(obj2, found);

    /* Try to remove non-existent object */
    result = nmo_object_repository_remove(repo, 999);
    ASSERT_NE(NMO_OK, result);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, clear_repository) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    /* Add multiple objects */
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Object%d", i);
        nmo_object_t *obj = create_test_object(arena, i + 1, name, 800);
        nmo_object_repository_add(repo, obj);
    }

    ASSERT_EQ(10, nmo_object_repository_get_count(repo));

    /* Clear */
    int result = nmo_object_repository_clear(repo);
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(0, nmo_object_repository_get_count(repo));

    /* Verify objects are gone */
    nmo_object_t *found = nmo_object_repository_find_by_id(repo, 1);
    ASSERT_NULL(found);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, get_all_objects) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(arena, 1, "One", 900);
    nmo_object_t *obj2 = create_test_object(arena, 2, "Two", 900);
    nmo_object_t *obj3 = create_test_object(arena, 3, "Three", 900);

    nmo_object_repository_add(repo, obj1);
    nmo_object_repository_add(repo, obj2);
    nmo_object_repository_add(repo, obj3);

    size_t count = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &count);

    ASSERT_NOT_NULL(all);
    ASSERT_EQ(3, count);

    /* Verify all objects are present */
    int found1 = 0, found2 = 0, found3 = 0;
    for (size_t i = 0; i < count; i++) {
        if (all[i] == obj1) found1 = 1;
        if (all[i] == obj2) found2 = 1;
        if (all[i] == obj3) found3 = 1;
    }
    ASSERT_TRUE(found1 && found2 && found3);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(object_repository, duplicate_id_handling) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(arena, 42, "First", 1000);
    nmo_object_t *obj2 = create_test_object(arena, 42, "Duplicate", 1000);

    int result1 = nmo_object_repository_add(repo, obj1);
    ASSERT_EQ(NMO_OK, result1);

    int result2 = nmo_object_repository_add(repo, obj2);
    ASSERT_NE(NMO_OK, result2);

    /* Verify only one object exists */
    ASSERT_EQ(1, nmo_object_repository_get_count(repo));

    nmo_object_t *found = nmo_object_repository_find_by_id(repo, 42);
    ASSERT_EQ(obj1, found);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_repository, create_destroy);
    REGISTER_TEST(object_repository, auto_assign_ids);
    REGISTER_TEST(object_repository, explicit_ids);
    REGISTER_TEST(object_repository, find_by_id);
    REGISTER_TEST(object_repository, find_by_name);
    REGISTER_TEST(object_repository, find_by_class);
    REGISTER_TEST(object_repository, remove_object);
    REGISTER_TEST(object_repository, clear_repository);
    REGISTER_TEST(object_repository, get_all_objects);
    REGISTER_TEST(object_repository, duplicate_id_handling);
TEST_MAIN_END()
