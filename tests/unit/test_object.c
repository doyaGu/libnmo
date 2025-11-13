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
TEST(object, create_destroy) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_t* obj = nmo_object_create(arena, (nmo_object_id_t)100, (nmo_class_id_t)200);
    ASSERT_NOT_NULL(obj);

    ASSERT_EQ(obj->id, 100);
    ASSERT_EQ(obj->class_id, 200);
    ASSERT_EQ(obj->file_index, 100);  // Should default to ID

    nmo_arena_destroy(arena);
}

// Test: Set object name
TEST(object, set_name) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_t* obj = nmo_object_create(arena, (nmo_object_id_t)100, (nmo_class_id_t)200);
    ASSERT_NOT_NULL(obj);

    const char* name = "TestObject";
    ASSERT_EQ(nmo_object_set_name(obj, name, arena), NMO_OK);

    ASSERT_NOT_NULL(obj->name);
    ASSERT_STR_EQ(obj->name, name);

    // Verify name was copied, not just pointed to
    ASSERT_NE(obj->name, name);

    nmo_arena_destroy(arena);
}

// Test: Object hierarchy (parent-child)
TEST(object, hierarchy) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_t* parent = nmo_object_create(arena, (nmo_object_id_t)100, (nmo_class_id_t)200);
    nmo_object_t* child1 = nmo_object_create(arena, (nmo_object_id_t)101, (nmo_class_id_t)201);
    nmo_object_t* child2 = nmo_object_create(arena, (nmo_object_id_t)102, (nmo_class_id_t)202);

    ASSERT_NOT_NULL(parent);
    ASSERT_NOT_NULL(child1);
    ASSERT_NOT_NULL(child2);

    // Add children
    ASSERT_EQ(nmo_object_add_child(parent, child1, arena), NMO_OK);
    ASSERT_EQ(nmo_object_add_child(parent, child2, arena), NMO_OK);

    // Verify parent has 2 children
    ASSERT_EQ(parent->child_count, 2);

    // Verify children have correct parent
    ASSERT_EQ(child1->parent, parent);
    ASSERT_EQ(child2->parent, parent);

    // Verify children array
    ASSERT_EQ(parent->children[0], child1);
    ASSERT_EQ(parent->children[1], child2);

    // Remove child1
    ASSERT_EQ(nmo_object_remove_child(parent, child1), NMO_OK);

    ASSERT_EQ(parent->child_count, 1);
    ASSERT_EQ(parent->children[0], child2);

    nmo_arena_destroy(arena);
}

// Test: Child array growth
TEST(object, child_growth) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_t* parent = nmo_object_create(arena, 100, 200);
    ASSERT_NOT_NULL(parent);

    // Add more than initial capacity (4) to trigger growth
    const int child_count = 10;
    for (int i = 0; i < child_count; i++) {
        nmo_object_t* child = nmo_object_create(arena, (nmo_object_id_t)(200 + i), (nmo_class_id_t)300);
        ASSERT_NOT_NULL(child);

        ASSERT_EQ(nmo_object_add_child(parent, child, arena), NMO_OK);
    }

    // Verify all children added
    ASSERT_EQ(parent->child_count, (size_t)child_count);

    // Verify capacity grew
    ASSERT_TRUE(parent->child_capacity >= (size_t)child_count);

    // Verify all children
    for (int i = 0; i < child_count; i++) {
        ASSERT_EQ(parent->children[i]->id, (nmo_object_id_t)(200 + i));
    }

    nmo_arena_destroy(arena);
}

// Test: Set chunk data
TEST(object, set_chunk) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_object_t* obj = nmo_object_create(arena, 100, 200);
    ASSERT_NOT_NULL(obj);

    // Create a dummy chunk
    nmo_chunk_t* chunk = (nmo_chunk_t*)nmo_arena_alloc(arena, sizeof(nmo_chunk_t), sizeof(void*));
    ASSERT_NOT_NULL(chunk);
    memset(chunk, 0, sizeof(nmo_chunk_t));

    ASSERT_EQ(nmo_object_set_chunk(obj, chunk), NMO_OK);
    ASSERT_EQ(obj->chunk, chunk);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object, create_destroy);
    REGISTER_TEST(object, set_name);
    REGISTER_TEST(object, hierarchy);
    REGISTER_TEST(object, child_growth);
    REGISTER_TEST(object, set_chunk);
TEST_MAIN_END()
