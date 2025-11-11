/**
 * @file test_repository.c
 * @brief Unit tests for object repository
 */

#include "../test_framework.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <string.h>

// Test: Create and destroy repository
static void test_repository_create_destroy(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    nmo_object_repository* repo = nmo_object_repository_create(arena);
    
    if (repo == NULL) {
        printf("FAIL: test_repository_create_destroy - creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }
    
    if (nmo_object_repository_get_count(repo) != 0) {
        printf("FAIL: test_repository_create_destroy - initial count not zero\n");
        nmo_object_repository_destroy(repo);
        nmo_arena_destroy(arena);
        return;
    }
    
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
    printf("PASS: test_repository_create_destroy\n");
}

// Test: Add and find by ID
static void test_repository_add_find_by_id(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    nmo_object_repository* repo = nmo_object_repository_create(arena);
    
    nmo_object* obj1 = nmo_object_create(arena, 100, 0x00000001);
    nmo_object* obj2 = nmo_object_create(arena, 200, 0x00000002);
    
    if (nmo_object_repository_add(repo, obj1) != NMO_OK) {
        printf("FAIL: test_repository_add_find_by_id - add obj1 failed\n");
        goto cleanup;
    }
    
    if (nmo_object_repository_add(repo, obj2) != NMO_OK) {
        printf("FAIL: test_repository_add_find_by_id - add obj2 failed\n");
        goto cleanup;
    }
    
    if (nmo_object_repository_get_count(repo) != 2) {
        printf("FAIL: test_repository_add_find_by_id - count not 2\n");
        goto cleanup;
    }
    
    nmo_object* found1 = nmo_object_repository_find_by_id(repo, 100);
    if (found1 != obj1) {
        printf("FAIL: test_repository_add_find_by_id - find obj1 failed\n");
        goto cleanup;
    }
    
    nmo_object* found2 = nmo_object_repository_find_by_id(repo, 200);
    if (found2 != obj2) {
        printf("FAIL: test_repository_add_find_by_id - find obj2 failed\n");
        goto cleanup;
    }
    
    printf("PASS: test_repository_add_find_by_id\n");
    
cleanup:
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

// Test: Find by name
static void test_repository_find_by_name(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    nmo_object_repository* repo = nmo_object_repository_create(arena);
    
    nmo_object* obj = nmo_object_create(arena, 100, 0x00000001);
    nmo_object_set_name(obj, "TestObject", arena);
    
    nmo_object_repository_add(repo, obj);
    
    nmo_object* found = nmo_object_repository_find_by_name(repo, "TestObject");
    if (found != obj) {
        printf("FAIL: test_repository_find_by_name - object not found\n");
        goto cleanup;
    }
    
    found = nmo_object_repository_find_by_name(repo, "NonExistent");
    if (found != NULL) {
        printf("FAIL: test_repository_find_by_name - found non-existent object\n");
        goto cleanup;
    }
    
    printf("PASS: test_repository_find_by_name\n");
    
cleanup:
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

// Test: Find by class
static void test_repository_find_by_class(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    nmo_object_repository* repo = nmo_object_repository_create(arena);
    
    // Add objects of different classes
    nmo_object* obj1 = nmo_object_create(arena, 100, 0x00000001);  // CKObject
    nmo_object* obj2 = nmo_object_create(arena, 200, 0x00000001);  // CKObject
    nmo_object* obj3 = nmo_object_create(arena, 300, 0x00000002);  // CKBeObject
    
    nmo_object_repository_add(repo, obj1);
    nmo_object_repository_add(repo, obj2);
    nmo_object_repository_add(repo, obj3);
    
    size_t count = 0;
    nmo_object** found = nmo_object_repository_find_by_class(repo, 0x00000001, &count);
    
    if (count != 2) {
        printf("FAIL: test_repository_find_by_class - expected 2 CKObjects, got %zu\n", count);
        goto cleanup;
    }
    
    if (found == NULL) {
        printf("FAIL: test_repository_find_by_class - result is NULL\n");
        goto cleanup;
    }
    
    printf("PASS: test_repository_find_by_class\n");
    
cleanup:
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

// Test: Remove object
static void test_repository_remove(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    nmo_object_repository* repo = nmo_object_repository_create(arena);
    
    nmo_object* obj = nmo_object_create(arena, 100, 0x00000001);
    nmo_object_repository_add(repo, obj);
    
    if (nmo_object_repository_get_count(repo) != 1) {
        printf("FAIL: test_repository_remove - count not 1 after add\n");
        goto cleanup;
    }
    
    if (nmo_object_repository_remove(repo, 100) != NMO_OK) {
        printf("FAIL: test_repository_remove - remove failed\n");
        goto cleanup;
    }
    
    if (nmo_object_repository_get_count(repo) != 0) {
        printf("FAIL: test_repository_remove - count not 0 after remove\n");
        goto cleanup;
    }
    
    if (nmo_object_repository_find_by_id(repo, 100) != NULL) {
        printf("FAIL: test_repository_remove - object still findable after remove\n");
        goto cleanup;
    }
    
    printf("PASS: test_repository_remove\n");
    
cleanup:
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

// Test: Repository growth
static void test_repository_growth(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 65536);
    nmo_object_repository* repo = nmo_object_repository_create(arena);
    
    // Add 100 objects to trigger resize
    for (int i = 0; i < 100; i++) {
        nmo_object* obj = nmo_object_create(arena, 1000 + i, 0x00000001);
        if (nmo_object_repository_add(repo, obj) != NMO_OK) {
            printf("FAIL: test_repository_growth - add object %d failed\n", i);
            goto cleanup;
        }
    }
    
    if (nmo_object_repository_get_count(repo) != 100) {
        printf("FAIL: test_repository_growth - count not 100\n");
        goto cleanup;
    }
    
    // Verify all objects are findable
    for (int i = 0; i < 100; i++) {
        nmo_object* found = nmo_object_repository_find_by_id(repo, 1000 + i);
        if (found == NULL) {
            printf("FAIL: test_repository_growth - object %d not found after growth\n", i);
            goto cleanup;
        }
    }
    
    printf("PASS: test_repository_growth\n");
    
cleanup:
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

int main(void) {
    printf("Running object repository tests...\n\n");
    
    test_repository_create_destroy();
    test_repository_add_find_by_id();
    test_repository_find_by_name();
    test_repository_find_by_class();
    test_repository_remove();
    test_repository_growth();
    
    printf("\nAll repository tests completed!\n");
    return 0;
}
