/**
 * @file test_object_repository.c
 * @brief Comprehensive unit tests for object repository
 */

#include "test_framework.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_allocator.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fail_next_allocator_state {
    int fail_next;
} fail_next_allocator_state_t;

static void *fail_next_alloc(void *user_data, size_t size, size_t alignment) {
    fail_next_allocator_state_t *state = (fail_next_allocator_state_t *)user_data;
    if (state != NULL && state->fail_next) {
        state->fail_next = 0;
        return NULL;
    }
    (void)alignment;
    return malloc(size);
}

static void fail_next_free(void *user_data, void *ptr) {
    (void)user_data;
    free(ptr);
}

/* Helper to create a test object */
static nmo_object_t* create_test_object(const nmo_allocator_t *allocator,
                                        nmo_object_id_t id,
                                        const char* name,
                                        nmo_class_id_t class_id) {
    nmo_object_t* obj = nmo_object_create(allocator, id, class_id);
    if (obj == NULL) {
        return NULL;
    }

    if (name != NULL) {
        if (nmo_object_set_name(obj, name) != NMO_OK) {
            nmo_object_destroy(obj);
            return NULL;
        }
    }

    return obj;
}

TEST(object_repository, create_destroy) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);
    ASSERT_EQ(0, nmo_object_repository_get_count(repo));

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, auto_assign_ids) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    /* Create objects with ID 0 - should auto-assign sequential IDs */
    nmo_object_t *obj1 = create_test_object(&allocator, 0, "Object1", 100);
    nmo_object_t *obj2 = create_test_object(&allocator, 0, "Object2", 100);
    nmo_object_t *obj3 = create_test_object(&allocator, 0, "Object3", 100);

    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    ASSERT_NOT_NULL(obj3);

    int result1 = nmo_object_repository_add(repo, &obj1);
    int result2 = nmo_object_repository_add(repo, &obj2);
    int result3 = nmo_object_repository_add(repo, &obj3);

    ASSERT_EQ(NMO_OK, result1);
    ASSERT_EQ(NMO_OK, result2);
    ASSERT_EQ(NMO_OK, result3);
    ASSERT_NULL(obj1);
    ASSERT_NULL(obj2);
    ASSERT_NULL(obj3);

    /* Verify sequential ID assignment */
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, 1));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, 2));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, 3));

    ASSERT_EQ(3, nmo_object_repository_get_count(repo));

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, explicit_ids) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(&allocator, 100, "Obj100", 200);
    nmo_object_t *obj2 = create_test_object(&allocator, 200, "Obj200", 200);

    int result1 = nmo_object_repository_add(repo, &obj1);
    int result2 = nmo_object_repository_add(repo, &obj2);

    ASSERT_EQ(NMO_OK, result1);
    ASSERT_EQ(NMO_OK, result2);
    ASSERT_NULL(obj1);
    ASSERT_NULL(obj2);

    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, 100));
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, 200));

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, find_by_id) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(&allocator, 42, "FindMe", 300);
    int result = nmo_object_repository_add(repo, &obj1);
    ASSERT_EQ(NMO_OK, result);

    nmo_object_t *found = nmo_object_repository_find_by_id(repo, 42);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(42, found->id);

    nmo_object_t *not_found = nmo_object_repository_find_by_id(repo, 999);
    ASSERT_NULL(not_found);

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, find_by_name) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(&allocator, 10, "Alice", 400);
    nmo_object_t *obj2 = create_test_object(&allocator, 20, "Bob", 400);

    nmo_object_repository_add(repo, &obj1);
    nmo_object_repository_add(repo, &obj2);

    nmo_object_t *found = nmo_object_repository_find_by_name(repo, "Alice");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(10, found->id);

    found = nmo_object_repository_find_by_name(repo, "Bob");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(20, found->id);

    nmo_object_t *not_found = nmo_object_repository_find_by_name(repo, "Charlie");
    ASSERT_NULL(not_found);

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, find_by_class) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    /* Add objects with different classes */
    nmo_object_t *obj1 = create_test_object(&allocator, 1, "Type500_A", 500);
    nmo_object_t *obj2 = create_test_object(&allocator, 2, "Type500_B", 500);
    nmo_object_t *obj3 = create_test_object(&allocator, 3, "Type600", 600);

    nmo_object_repository_add(repo, &obj1);
    nmo_object_repository_add(repo, &obj2);
    nmo_object_repository_add(repo, &obj3);

    size_t count = 0;
    nmo_object_t **results = nmo_object_repository_find_by_class(repo, 500, &count);

    ASSERT_NOT_NULL(results);
    ASSERT_EQ(2, count);

    /* Verify both objects are present */
    int found_obj1 = 0, found_obj2 = 0;
    for (size_t i = 0; i < count; i++) {
        if (results[i] != NULL && results[i]->id == 1) found_obj1 = 1;
        if (results[i] != NULL && results[i]->id == 2) found_obj2 = 1;
    }
    ASSERT_TRUE(found_obj1 && found_obj2);

    /* Test class 600 */
    results = nmo_object_repository_find_by_class(repo, 600, &count);
    ASSERT_NOT_NULL(results);
    ASSERT_EQ(1, count);
    ASSERT_EQ(3, results[0]->id);

    /* Test non-existent class */
    results = nmo_object_repository_find_by_class(repo, 999, &count);
    ASSERT_NULL(results);
    ASSERT_EQ(0, count);

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, remove_object) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(&allocator, 10, "ToRemove", 700);
    nmo_object_t *obj2 = create_test_object(&allocator, 20, "ToKeep", 700);

    nmo_object_repository_add(repo, &obj1);
    nmo_object_repository_add(repo, &obj2);

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
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(20, found->id);

    /* Try to remove non-existent object */
    result = nmo_object_repository_remove(repo, 999);
    ASSERT_NE(NMO_OK, result);

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, clear_repository) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    /* Add multiple objects */
    for (int i = 0; i < 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Object%d", i);
        nmo_object_t *obj = create_test_object(&allocator, i + 1, name, 800);
        nmo_object_repository_add(repo, &obj);
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
}

TEST(object_repository, get_all_objects) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(&allocator, 1, "One", 900);
    nmo_object_t *obj2 = create_test_object(&allocator, 2, "Two", 900);
    nmo_object_t *obj3 = create_test_object(&allocator, 3, "Three", 900);

    nmo_object_repository_add(repo, &obj1);
    nmo_object_repository_add(repo, &obj2);
    nmo_object_repository_add(repo, &obj3);

    size_t count = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &count);

    ASSERT_NOT_NULL(all);
    ASSERT_EQ(3, count);

    /* Verify all objects are present */
    int found1 = 0, found2 = 0, found3 = 0;
    for (size_t i = 0; i < count; i++) {
        if (all[i] != NULL && all[i]->id == 1) found1 = 1;
        if (all[i] != NULL && all[i]->id == 2) found2 = 1;
        if (all[i] != NULL && all[i]->id == 3) found3 = 1;
    }
    ASSERT_TRUE(found1 && found2 && found3);

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, auto_assign_skips_existing_ids) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(&allocator, 1, "First", 1000);
    nmo_object_t *obj2 = create_test_object(&allocator, 0, "Second", 1000);

    int result1 = nmo_object_repository_add(repo, &obj1);
    ASSERT_EQ(NMO_OK, result1);

    int result2 = nmo_object_repository_add(repo, &obj2);
    ASSERT_EQ(NMO_OK, result2);
    ASSERT_NULL(obj2);
    ASSERT_NOT_NULL(nmo_object_repository_find_by_id(repo, 2));

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, duplicate_name_handling) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(&allocator, 10, "SameName", 1000);
    nmo_object_t *obj2 = create_test_object(&allocator, 20, "SameName", 1000);

    int result1 = nmo_object_repository_add(repo, &obj1);
    ASSERT_EQ(NMO_OK, result1);

    int result2 = nmo_object_repository_add(repo, &obj2);
    ASSERT_EQ(NMO_OK, result2);

    ASSERT_EQ(2, nmo_object_repository_get_count(repo));
    nmo_object_t *found = nmo_object_repository_find_by_name(repo, "SameName");
    ASSERT_NOT_NULL(found);
    ASSERT_TRUE(found->id == 10 || found->id == 20);

    int remove_result = nmo_object_repository_remove(repo, 20);
    ASSERT_EQ(NMO_OK, remove_result);
    ASSERT_EQ(1, nmo_object_repository_get_count(repo));

    found = nmo_object_repository_find_by_name(repo, "SameName");
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(10, found->id);

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, duplicate_id_handling) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);

    nmo_object_t *obj1 = create_test_object(&allocator, 42, "First", 1000);
    nmo_object_t *obj2 = create_test_object(&allocator, 42, "Duplicate", 1000);

    int result1 = nmo_object_repository_add(repo, &obj1);
    ASSERT_EQ(NMO_OK, result1);
    ASSERT_NULL(obj1);

    int result2 = nmo_object_repository_add(repo, &obj2);
    ASSERT_NE(NMO_OK, result2);
    ASSERT_NOT_NULL(obj2);

    /* Verify only one object exists */
    ASSERT_EQ(1, nmo_object_repository_get_count(repo));

    nmo_object_t *found = nmo_object_repository_find_by_id(repo, 42);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(42, found->id);

    /* obj2 was never added due to duplicate ID; destroy it to avoid leaks. */
    nmo_object_destroy(obj2);

    nmo_object_repository_destroy(repo);
}

TEST(object_repository, add_failure_keeps_caller_ownership) {
    fail_next_allocator_state_t repo_alloc_state = {0};
    nmo_allocator_t repo_allocator = nmo_allocator_custom(
        fail_next_alloc,
        fail_next_free,
        &repo_alloc_state
    );
    nmo_allocator_t object_allocator = nmo_allocator_default();
    nmo_object_repository_t *repo = nmo_object_repository_create(&repo_allocator);
    ASSERT_NOT_NULL(repo);

    /* Keep ID-map growth ahead of name-table growth so the next allocation hit is name-table rehash. */
    for (int i = 0; i < 44; i++) {
        nmo_object_t *obj = create_test_object(&object_allocator, (nmo_object_id_t)(1000 + i), NULL, 1100);
        ASSERT_NOT_NULL(obj);
        ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &obj));
        ASSERT_NULL(obj);
    }

    for (int i = 0; i < 44; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Named%02d", i);
        nmo_object_t *obj = create_test_object(&object_allocator, (nmo_object_id_t)(2000 + i), name, 1100);
        ASSERT_NOT_NULL(obj);
        ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &obj));
        ASSERT_NULL(obj);
    }

    ASSERT_EQ(88, nmo_object_repository_get_count(repo));

    nmo_object_t *failing_obj = create_test_object(&object_allocator, 99999, "TriggerGrowFailure", 1100);
    ASSERT_NOT_NULL(failing_obj);

    repo_alloc_state.fail_next = 1;
    int result = nmo_object_repository_add(repo, &failing_obj);
    ASSERT_NE(NMO_OK, result);
    ASSERT_NOT_NULL(failing_obj);
    ASSERT_EQ(88, nmo_object_repository_get_count(repo));
    ASSERT_NULL(nmo_object_repository_find_by_id(repo, 99999));

    nmo_object_destroy(failing_obj);
    nmo_object_repository_destroy(repo);
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
    REGISTER_TEST(object_repository, auto_assign_skips_existing_ids);
    REGISTER_TEST(object_repository, duplicate_name_handling);
    REGISTER_TEST(object_repository, duplicate_id_handling);
    REGISTER_TEST(object_repository, add_failure_keeps_caller_ownership);
TEST_MAIN_END()
