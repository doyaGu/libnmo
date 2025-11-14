/**
 * @file test_object_index.c
 * @brief Unit tests for object indexing system (Phase 5)
 */

#include "test_framework.h"
#include "session/nmo_object_index.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test fixture */
typedef struct test_fixture {
    nmo_arena_t *arena;
    nmo_object_repository_t *repo;
    nmo_object_index_t *index;
} test_fixture_t;

static test_fixture_t *setup_fixture(void) {
    test_fixture_t *fixture = (test_fixture_t *)malloc(sizeof(test_fixture_t));
    if (!fixture) return NULL;
    
    fixture->arena = nmo_arena_create(NULL, 0);
    if (!fixture->arena) {
        free(fixture);
        return NULL;
    }
    
    fixture->repo = nmo_object_repository_create(fixture->arena);
    if (!fixture->repo) {
        nmo_arena_destroy(fixture->arena);
        free(fixture);
        return NULL;
    }
    
    fixture->index = nmo_object_index_create(fixture->repo, fixture->arena);
    if (!fixture->index) {
        nmo_object_repository_destroy(fixture->repo);
        nmo_arena_destroy(fixture->arena);
        free(fixture);
        return NULL;
    }
    
    return fixture;
}

static void teardown_fixture(test_fixture_t *fixture) {
    if (fixture) {
        nmo_object_index_destroy(fixture->index);
        nmo_object_repository_destroy(fixture->repo);
        nmo_arena_destroy(fixture->arena);
        free(fixture);
    }
}

/* Helper: Create test object */
static nmo_object_t *create_test_object(
    test_fixture_t *f,
    nmo_object_id_t id,
    nmo_class_id_t class_id,
    const char *name
) {
    nmo_object_t *obj = nmo_object_create(f->arena, id, class_id);
    if (!obj) return NULL;
    
    if (name) {
        nmo_object_set_name(obj, name, f->arena);
    }
    
    nmo_object_repository_add(f->repo, obj);
    return obj;
}

/* ==================== Tests ==================== */

/**
 * Test: Basic index creation and destruction
 */
TEST(object_index, create_destroy) {
    test_fixture_t *f = setup_fixture();
    ASSERT_NOT_NULL(f);
    ASSERT_NOT_NULL(f->index);
    
    teardown_fixture(f);
}

/**
 * Test: Class ID indexing
 */
TEST(object_index, class_index) {
    test_fixture_t *f = setup_fixture();
    ASSERT_NOT_NULL(f);
    
    /* Create test objects with different classes */
    create_test_object(f, 1, 100, "Object1");
    create_test_object(f, 2, 100, "Object2");
    create_test_object(f, 3, 200, "Object3");
    create_test_object(f, 4, 100, "Object4");
    create_test_object(f, 5, 300, "Object5");
    
    /* Build class index */
    int result = nmo_object_index_build(f->index, NMO_INDEX_BUILD_CLASS);
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify class index is built */
    ASSERT_TRUE(nmo_object_index_has_class_index(f->index));
    
    /* Query by class ID */
    size_t count;
    nmo_object_t **objects;
    
    objects = nmo_object_index_get_by_class(f->index, 100, &count);
    ASSERT_NOT_NULL(objects);
    ASSERT_EQ(3, count);
    
    objects = nmo_object_index_get_by_class(f->index, 200, &count);
    ASSERT_NOT_NULL(objects);
    ASSERT_EQ(1, count);
    
    objects = nmo_object_index_get_by_class(f->index, 300, &count);
    ASSERT_NOT_NULL(objects);
    ASSERT_EQ(1, count);
    
    /* Query non-existent class */
    objects = nmo_object_index_get_by_class(f->index, 999, &count);
    ASSERT_NULL(objects);
    ASSERT_EQ(0, count);
    
    /* Test count_by_class */
    count = nmo_object_index_count_by_class(f->index, 100);
    ASSERT_EQ(3, count);
    
    teardown_fixture(f);
}

/**
 * Test: Name indexing
 */
TEST(object_index, name_index) {
    test_fixture_t *f = setup_fixture();
    ASSERT_NOT_NULL(f);
    
    /* Create test objects with names */
    create_test_object(f, 1, 100, "Alice");
    create_test_object(f, 2, 200, "Bob");
    create_test_object(f, 3, 100, "Alice");  /* Duplicate name, different class */
    create_test_object(f, 4, 300, "Charlie");
    create_test_object(f, 5, 100, NULL);     /* No name */
    
    /* Build name index */
    int result = nmo_object_index_build(f->index, NMO_INDEX_BUILD_NAME);
    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(nmo_object_index_has_name_index(f->index));
    
    /* Find by name (exact match) */
    nmo_object_t *obj = nmo_object_index_find_by_name(f->index, "Alice", 0);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(1, obj->id);
    
    obj = nmo_object_index_find_by_name(f->index, "Bob", 0);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(2, obj->id);
    
    /* Find by name with class filter */
    obj = nmo_object_index_find_by_name(f->index, "Alice", 100);
    ASSERT_NOT_NULL(obj);
    
    obj = nmo_object_index_find_by_name(f->index, "Alice", 999);
    ASSERT_NULL(obj);
    
    /* Get all objects with name */
    size_t count;
    nmo_object_t **objects = nmo_object_index_get_by_name_all(f->index, "Alice", 0, &count);
    ASSERT_NOT_NULL(objects);
    ASSERT_EQ(2, count);
    
    /* Case-insensitive search */
    obj = nmo_object_index_find_by_name_fuzzy(f->index, "alice", 0);
    ASSERT_NOT_NULL(obj);
    
    obj = nmo_object_index_find_by_name_fuzzy(f->index, "CHARLIE", 0);
    ASSERT_NOT_NULL(obj);
    
    /* Non-existent name */
    obj = nmo_object_index_find_by_name(f->index, "David", 0);
    ASSERT_NULL(obj);
    
    teardown_fixture(f);
}

/**
 * Test: GUID indexing
 */
TEST(object_index, guid_index) {
    test_fixture_t *f = setup_fixture();
    ASSERT_NOT_NULL(f);
    
    /* Create GUIDs */
    nmo_guid_t guid1 = {0x12345678, 0xABCD};
    nmo_guid_t guid2 = {0x87654321, 0xDCBA};
    nmo_guid_t guid3 = {0x12345678, 0xABCD};  /* Same as guid1 */
    
    /* Create objects with GUIDs */
    nmo_object_t *obj1 = create_test_object(f, 1, 100, "Obj1");
    obj1->type_guid = guid1;
    
    nmo_object_t *obj2 = create_test_object(f, 2, 200, "Obj2");
    obj2->type_guid = guid2;
    
    nmo_object_t *obj3 = create_test_object(f, 3, 100, "Obj3");
    obj3->type_guid = guid3;
    
    /* obj4 created but has null GUID - used for testing null GUID handling */
    create_test_object(f, 4, 300, "Obj4");
    
    /* Build GUID index */
    int result = nmo_object_index_build(f->index, NMO_INDEX_BUILD_GUID);
    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(nmo_object_index_has_guid_index(f->index));
    
    /* Find by GUID */
    nmo_object_t *obj = nmo_object_index_find_by_guid(f->index, guid1);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(1, obj->id);
    
    obj = nmo_object_index_find_by_guid(f->index, guid2);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(2, obj->id);
    
    /* Get all objects with same GUID */
    size_t count;
    nmo_object_t **objects = nmo_object_index_get_by_guid_all(f->index, guid1, &count);
    ASSERT_NOT_NULL(objects);
    ASSERT_EQ(2, count);
    
    /* Non-existent GUID */
    nmo_guid_t guid_invalid = {0xFFFFFFFF, 0xFFFF};
    obj = nmo_object_index_find_by_guid(f->index, guid_invalid);
    ASSERT_NULL(obj);
    
    teardown_fixture(f);
}

/**
 * Test: Incremental updates
 */
TEST(object_index, incremental_update) {
    test_fixture_t *f = setup_fixture();
    ASSERT_NOT_NULL(f);
    
    /* Create initial objects */
    create_test_object(f, 1, 100, "Obj1");
    create_test_object(f, 2, 200, "Obj2");
    
    /* Build indexes */
    nmo_object_index_build(f->index, NMO_INDEX_BUILD_ALL);
    
    /* Verify initial count */
    size_t count = nmo_object_index_count_by_class(f->index, 100);
    ASSERT_EQ(1, count);
    
    /* Add new object incrementally */
    nmo_object_t *obj3 = create_test_object(f, 3, 100, "Obj3");
    int result = nmo_object_index_add_object(f->index, obj3, NMO_INDEX_BUILD_ALL);
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify updated count */
    count = nmo_object_index_count_by_class(f->index, 100);
    ASSERT_EQ(2, count);
    
    /* Remove object */
    result = nmo_object_index_remove_object(f->index, 1, NMO_INDEX_BUILD_ALL);
    ASSERT_EQ(NMO_OK, result);
    
    count = nmo_object_index_count_by_class(f->index, 100);
    ASSERT_EQ(1, count);
    
    teardown_fixture(f);
}

/**
 * Test: Index statistics
 */
TEST(object_index, statistics) {
    test_fixture_t *f = setup_fixture();
    ASSERT_NOT_NULL(f);
    
    /* Create test objects */
    create_test_object(f, 1, 100, "Obj1");
    create_test_object(f, 2, 100, "Obj2");
    create_test_object(f, 3, 200, "Obj3");
    
    /* Build all indexes */
    nmo_object_index_build(f->index, NMO_INDEX_BUILD_ALL);
    
    /* Get statistics */
    nmo_index_stats_t stats;
    int result = nmo_object_index_get_stats(f->index, &stats);
    ASSERT_EQ(NMO_OK, result);
    
    ASSERT_EQ(3, stats.total_objects);
    ASSERT_EQ(2, stats.class_index_entries);
    ASSERT_EQ(3, stats.name_index_entries);
    ASSERT_TRUE(stats.memory_usage > 0);
    
    (void)stats; // Suppress unused variable warning
    
    teardown_fixture(f);
}

TEST(object_index, active_flags) {
    test_fixture_t *f = setup_fixture();
    ASSERT_NOT_NULL(f);

    create_test_object(f, 1, 100, "Obj1");

    nmo_object_index_build(f->index, NMO_INDEX_BUILD_NAME);
    ASSERT_EQ(NMO_INDEX_BUILD_NAME, nmo_object_index_get_active_flags(f->index));

    nmo_object_index_build(f->index, NMO_INDEX_BUILD_CLASS);
    ASSERT_EQ(
        (uint32_t)(NMO_INDEX_BUILD_NAME | NMO_INDEX_BUILD_CLASS),
        nmo_object_index_get_active_flags(f->index));

    nmo_object_index_clear(f->index, NMO_INDEX_BUILD_NAME);
    ASSERT_EQ(NMO_INDEX_BUILD_CLASS, nmo_object_index_get_active_flags(f->index));

    teardown_fixture(f);
}

/**
 * Test: Rebuild indexes
 */
TEST(object_index, rebuild) {
    test_fixture_t *f = setup_fixture();
    ASSERT_NOT_NULL(f);
    
    /* Create initial objects */
    create_test_object(f, 1, 100, "Obj1");
    create_test_object(f, 2, 200, "Obj2");
    
    /* Build indexes */
    nmo_object_index_build(f->index, NMO_INDEX_BUILD_ALL);
    
    size_t count1 = nmo_object_index_count_by_class(f->index, 100);
    ASSERT_EQ(1, count1);
    
    /* Add more objects directly to repository (bypass index) */
    create_test_object(f, 3, 100, "Obj3");
    create_test_object(f, 4, 100, "Obj4");
    
    /* Count should still be old value */
    count1 = nmo_object_index_count_by_class(f->index, 100);
    ASSERT_EQ(1, count1);
    
    /* Rebuild index */
    int result = nmo_object_index_rebuild(f->index, NMO_INDEX_BUILD_ALL);
    ASSERT_EQ(NMO_OK, result);
    
    /* Count should now reflect new objects */
    count1 = nmo_object_index_count_by_class(f->index, 100);
    ASSERT_EQ(3, count1);
    
    teardown_fixture(f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_index, create_destroy);
    REGISTER_TEST(object_index, class_index);
    REGISTER_TEST(object_index, name_index);
    REGISTER_TEST(object_index, guid_index);
    REGISTER_TEST(object_index, incremental_update);
    REGISTER_TEST(object_index, statistics);
    REGISTER_TEST(object_index, active_flags);
    REGISTER_TEST(object_index, rebuild);
TEST_MAIN_END()
