/**
 * @file test_object.c
 * @brief Unit tests for object metadata
 */

#include "../test_framework.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <string.h>

// Test: Create and destroy object
static void test_object_create_destroy(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_object_create_destroy - arena creation failed\n");
        return;
    }

    nmo_object* obj = nmo_object_create(arena, 100, 200);
    if (obj == NULL) {
        printf("FAIL: test_object_create_destroy - object creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (obj->id != 100) {
        printf("FAIL: test_object_create_destroy - ID mismatch\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (obj->class_id != 200) {
        printf("FAIL: test_object_create_destroy - class ID mismatch\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (obj->file_index != 100) {
        printf("FAIL: test_object_create_destroy - file_index should default to ID\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_arena_destroy(arena);
    printf("PASS: test_object_create_destroy\n");
}

// Test: Set object name
static void test_object_set_name(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_object_set_name - arena creation failed\n");
        return;
    }

    nmo_object* obj = nmo_object_create(arena, 100, 200);
    if (obj == NULL) {
        printf("FAIL: test_object_set_name - object creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    const char* name = "TestObject";
    if (nmo_object_set_name(obj, name, arena) != NMO_OK) {
        printf("FAIL: test_object_set_name - set_name failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (obj->name == NULL) {
        printf("FAIL: test_object_set_name - name is NULL\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (strcmp(obj->name, name) != 0) {
        printf("FAIL: test_object_set_name - name mismatch\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Verify name was copied, not just pointed to
    if (obj->name == name) {
        printf("FAIL: test_object_set_name - name should be copied\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_arena_destroy(arena);
    printf("PASS: test_object_set_name\n");
}

// Test: Object hierarchy (parent-child)
static void test_object_hierarchy(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_object_hierarchy - arena creation failed\n");
        return;
    }

    nmo_object* parent = nmo_object_create(arena, 100, 200);
    nmo_object* child1 = nmo_object_create(arena, 101, 201);
    nmo_object* child2 = nmo_object_create(arena, 102, 202);

    if (parent == NULL || child1 == NULL || child2 == NULL) {
        printf("FAIL: test_object_hierarchy - object creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Add children
    if (nmo_object_add_child(parent, child1, arena) != NMO_OK) {
        printf("FAIL: test_object_hierarchy - add_child failed for child1\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_object_add_child(parent, child2, arena) != NMO_OK) {
        printf("FAIL: test_object_hierarchy - add_child failed for child2\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Verify parent has 2 children
    if (parent->child_count != 2) {
        printf("FAIL: test_object_hierarchy - parent child_count incorrect (got %zu, expected 2)\n",
               parent->child_count);
        nmo_arena_destroy(arena);
        return;
    }

    // Verify children have correct parent
    if (child1->parent != parent) {
        printf("FAIL: test_object_hierarchy - child1 parent incorrect\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (child2->parent != parent) {
        printf("FAIL: test_object_hierarchy - child2 parent incorrect\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Verify children array
    if (parent->children[0] != child1) {
        printf("FAIL: test_object_hierarchy - children[0] incorrect\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (parent->children[1] != child2) {
        printf("FAIL: test_object_hierarchy - children[1] incorrect\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Remove child1
    if (nmo_object_remove_child(parent, child1) != NMO_OK) {
        printf("FAIL: test_object_hierarchy - remove_child failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (parent->child_count != 1) {
        printf("FAIL: test_object_hierarchy - parent child_count after removal incorrect\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (parent->children[0] != child2) {
        printf("FAIL: test_object_hierarchy - children[0] after removal incorrect\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_arena_destroy(arena);
    printf("PASS: test_object_hierarchy\n");
}

// Test: Child array growth
static void test_object_child_growth(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_object_child_growth - arena creation failed\n");
        return;
    }

    nmo_object* parent = nmo_object_create(arena, 100, 200);
    if (parent == NULL) {
        printf("FAIL: test_object_child_growth - object creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Add more than initial capacity (4) to trigger growth
    const int child_count = 10;
    for (int i = 0; i < child_count; i++) {
        nmo_object* child = nmo_object_create(arena, 200 + i, 300);
        if (child == NULL) {
            printf("FAIL: test_object_child_growth - child %d creation failed\n", i);
            nmo_arena_destroy(arena);
            return;
        }

        if (nmo_object_add_child(parent, child, arena) != NMO_OK) {
            printf("FAIL: test_object_child_growth - add_child failed for child %d\n", i);
            nmo_arena_destroy(arena);
            return;
        }
    }

    // Verify all children added
    if (parent->child_count != (size_t)child_count) {
        printf("FAIL: test_object_child_growth - child_count incorrect (got %zu, expected %d)\n",
               parent->child_count, child_count);
        nmo_arena_destroy(arena);
        return;
    }

    // Verify capacity grew
    if (parent->child_capacity < (size_t)child_count) {
        printf("FAIL: test_object_child_growth - capacity didn't grow\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Verify all children
    for (int i = 0; i < child_count; i++) {
        if (parent->children[i]->id != (nmo_object_id)(200 + i)) {
            printf("FAIL: test_object_child_growth - child %d ID mismatch\n", i);
            nmo_arena_destroy(arena);
            return;
        }
    }

    nmo_arena_destroy(arena);
    printf("PASS: test_object_child_growth\n");
}

// Test: Set chunk data
static void test_object_set_chunk(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_object_set_chunk - arena creation failed\n");
        return;
    }

    nmo_object* obj = nmo_object_create(arena, 100, 200);
    if (obj == NULL) {
        printf("FAIL: test_object_set_chunk - object creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Create a dummy chunk
    nmo_chunk* chunk = (nmo_chunk*)nmo_arena_alloc(arena, sizeof(nmo_chunk), sizeof(void*));
    if (chunk == NULL) {
        printf("FAIL: test_object_set_chunk - chunk allocation failed\n");
        nmo_arena_destroy(arena);
        return;
    }
    memset(chunk, 0, sizeof(nmo_chunk));

    if (nmo_object_set_chunk(obj, chunk) != NMO_OK) {
        printf("FAIL: test_object_set_chunk - set_chunk failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (obj->chunk != chunk) {
        printf("FAIL: test_object_set_chunk - chunk pointer mismatch\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_arena_destroy(arena);
    printf("PASS: test_object_set_chunk\n");
}

int main(void) {
    printf("Running object metadata tests...\n\n");

    test_object_create_destroy();
    test_object_set_name();
    test_object_hierarchy();
    test_object_child_growth();
    test_object_set_chunk();

    printf("\nAll object metadata tests completed!\n");
    return 0;
}
