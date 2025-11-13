/**
 * @file test_repository.c
 * @brief Unit tests for object repository
 */

#include "nmo.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(repository, create_destroy) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);
    
    ASSERT_EQ(nmo_object_repository_get_count(repo), 0);
    
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(repository, add_find_by_id) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);
    
    nmo_object_t* obj1 = nmo_object_create(arena, (nmo_object_id_t)100, (nmo_class_id_t)0x00000001);
    ASSERT_NOT_NULL(obj1);
    
    nmo_object_t* obj2 = nmo_object_create(arena, (nmo_object_id_t)200, (nmo_class_id_t)0x00000002);
    ASSERT_NOT_NULL(obj2);
    
    int result = nmo_object_repository_add(repo, obj1);
    ASSERT_EQ(result, NMO_OK);
    
    result = nmo_object_repository_add(repo, obj2);
    ASSERT_EQ(result, NMO_OK);
    
    ASSERT_EQ(nmo_object_repository_get_count(repo), 2);
    
    nmo_object_t* found1 = nmo_object_repository_find_by_id(repo, (nmo_object_id_t)100);
    ASSERT_EQ(found1, obj1);
    
    nmo_object_t* found2 = nmo_object_repository_find_by_id(repo, (nmo_object_id_t)200);
    ASSERT_EQ(found2, obj2);
    
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(repository, find_by_name) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);
    
    nmo_object_t* obj = nmo_object_create(arena, (nmo_object_id_t)100, (nmo_class_id_t)0x00000001);
    ASSERT_NOT_NULL(obj);
    nmo_object_set_name(obj, "TestObject", arena);
    
    nmo_object_repository_add(repo, obj);
    
    nmo_object_t* found = nmo_object_repository_find_by_name(repo, "TestObject");
    ASSERT_EQ(found, obj);
    
    found = nmo_object_repository_find_by_name(repo, "NonExistent");
    ASSERT_NULL(found);
    
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(repository, find_by_class) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);
    
    // Add objects of different classes
    nmo_object_t* obj1 = nmo_object_create(arena, (nmo_object_id_t)100, (nmo_class_id_t)0x00000001);  // CKObject
    ASSERT_NOT_NULL(obj1);
    
    nmo_object_t* obj2 = nmo_object_create(arena, (nmo_object_id_t)200, (nmo_class_id_t)0x00000001);  // CKObject
    ASSERT_NOT_NULL(obj2);
    
    nmo_object_t* obj3 = nmo_object_create(arena, (nmo_object_id_t)300, (nmo_class_id_t)0x00000002);  // CKBeObject
    ASSERT_NOT_NULL(obj3);
    
    nmo_object_repository_add(repo, obj1);
    nmo_object_repository_add(repo, obj2);
    nmo_object_repository_add(repo, obj3);
    
    size_t count = 0;
    nmo_object_t** found = nmo_object_repository_find_by_class(repo, (nmo_class_id_t)0x00000001, &count);
    
    ASSERT_EQ(count, 2);
    ASSERT_NOT_NULL(found);
    
    found = nmo_object_repository_find_by_class(repo, (nmo_class_id_t)0x00000002, &count);
    ASSERT_EQ(count, 1);
    ASSERT_NOT_NULL(found);
    
    found = nmo_object_repository_find_by_class(repo, (nmo_class_id_t)0x00000003, &count);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(found);
    
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(repository, remove) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);
    
    nmo_object_t* obj = nmo_object_create(arena, (nmo_object_id_t)100, (nmo_class_id_t)0x00000001);
    ASSERT_NOT_NULL(obj);
    
    nmo_object_repository_add(repo, obj);
    
    ASSERT_EQ(nmo_object_repository_get_count(repo), 1);
    
    int result = nmo_object_repository_remove(repo, (nmo_object_id_t)100);
    ASSERT_EQ(result, NMO_OK);
    
    ASSERT_EQ(nmo_object_repository_get_count(repo), 0);
    
    nmo_object_t* found = nmo_object_repository_find_by_id(repo, (nmo_object_id_t)100);
    ASSERT_NULL(found);
    
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST(repository, growth) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    
    nmo_object_repository_t* repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);
    
    // Add 100 objects to trigger resize
    for (int i = 0; i < 100; i++) {
        nmo_object_t* obj = nmo_object_create(arena, (nmo_object_id_t)(1000 + i), (nmo_class_id_t)0x00000001);
        ASSERT_NOT_NULL(obj);
        
        int result = nmo_object_repository_add(repo, obj);
        ASSERT_EQ(result, NMO_OK);
    }
    
    ASSERT_EQ(nmo_object_repository_get_count(repo), 100);
    
    // Verify all objects are findable
    for (int i = 0; i < 100; i++) {
        nmo_object_t* found = nmo_object_repository_find_by_id(repo, (nmo_object_id_t)(1000 + i));
        ASSERT_NOT_NULL(found);
    }
    
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(repository, create_destroy);
    REGISTER_TEST(repository, add_find_by_id);
    REGISTER_TEST(repository, find_by_name);
    REGISTER_TEST(repository, find_by_class);
    REGISTER_TEST(repository, remove);
    REGISTER_TEST(repository, growth);
TEST_MAIN_END()
