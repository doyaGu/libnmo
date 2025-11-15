/**
 * @file test_performance.c
 * @brief Performance tests for Phase 5 optimizations
 */

#include "core/nmo_hash_table.h"
#include "core/nmo_arena.h"
#include "session/nmo_object_index.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <sys/time.h>
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

/* Test 1: Hash table reserve performance */
void test_hash_table_reserve(void) {
    printf("\n=== Hash Table Reserve Performance ===\n");
    
    const size_t num_items = 10000;
    
    /* Test without reserve */
    double start = get_time_ms();
    nmo_hash_table_t *table1 = nmo_hash_table_create(
        NULL,
        sizeof(uint32_t), sizeof(uint32_t), 16, NULL, NULL
    );
    for (size_t i = 0; i < num_items; i++) {
        uint32_t key = (uint32_t)i;
        uint32_t value = (uint32_t)(i * 2);
        nmo_hash_table_insert(table1, &key, &value);
    }
    double time_without_reserve = get_time_ms() - start;
    nmo_hash_table_destroy(table1);
    
    /* Test with reserve */
    start = get_time_ms();
    nmo_hash_table_t *table2 = nmo_hash_table_create(
        NULL,
        sizeof(uint32_t), sizeof(uint32_t), 16, NULL, NULL
    );
    nmo_hash_table_reserve(table2, num_items);  /* Pre-allocate */
    for (size_t i = 0; i < num_items; i++) {
        uint32_t key = (uint32_t)i;
        uint32_t value = (uint32_t)(i * 2);
        nmo_hash_table_insert(table2, &key, &value);
    }
    double time_with_reserve = get_time_ms() - start;
    nmo_hash_table_destroy(table2);
    
    printf("Inserting %zu items:\n", num_items);
    printf("  Without reserve: %.2f ms\n", time_without_reserve);
    printf("  With reserve:    %.2f ms\n", time_with_reserve);
    printf("  Speedup:         %.2fx\n", time_without_reserve / time_with_reserve);
}

/* Test 2: Arena reserve performance */
void test_arena_reserve(void) {
    printf("\n=== Arena Reserve Performance ===\n");
    
    const size_t num_allocs = 10000;
    const size_t alloc_size = 64;
    
    /* Test without reserve */
    double start = get_time_ms();
    nmo_arena_t *arena1 = nmo_arena_create(NULL, 0);
    for (size_t i = 0; i < num_allocs; i++) {
        nmo_arena_alloc(arena1, alloc_size, 8);
    }
    double time_without_reserve = get_time_ms() - start;
    nmo_arena_destroy(arena1);
    
    /* Test with reserve */
    start = get_time_ms();
    nmo_arena_t *arena2 = nmo_arena_create(NULL, 0);
    nmo_arena_reserve(arena2, num_allocs * alloc_size);  /* Pre-allocate */
    for (size_t i = 0; i < num_allocs; i++) {
        nmo_arena_alloc(arena2, alloc_size, 8);
    }
    double time_with_reserve = get_time_ms() - start;
    nmo_arena_destroy(arena2);
    
    printf("Allocating %zu x %zu bytes:\n", num_allocs, alloc_size);
    printf("  Without reserve: %.2f ms\n", time_without_reserve);
    printf("  With reserve:    %.2f ms\n", time_with_reserve);
    printf("  Speedup:         %.2fx\n", time_without_reserve / time_with_reserve);
}

/* Test 3: Object index lookup performance */
void test_index_lookup(void) {
    printf("\n=== Object Index Lookup Performance ===\n");
    
    const size_t num_objects = 1000;
    const size_t num_lookups = 10000;
    
    /* Create test data */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    
    for (size_t i = 0; i < num_objects; i++) {
        nmo_object_t *obj = nmo_object_create(arena, (nmo_object_id_t)i, (nmo_class_id_t)(i % 10));
        char name[32];
        sprintf(name, "Object_%zu", i);
        nmo_object_set_name(obj, name, arena);
        nmo_object_repository_add(repo, obj);
    }
    
    /* Test without index (linear search) */
    double start = get_time_ms();
    for (size_t i = 0; i < num_lookups; i++) {
        nmo_class_id_t cid = (nmo_class_id_t)(i % 10);
        size_t count;
        nmo_object_repository_find_by_class(repo, cid, &count);
    }
    double time_without_index = get_time_ms() - start;
    
    /* Test with index */
    nmo_object_index_t *index = nmo_object_index_create(repo, arena);
    nmo_object_index_build(index, NMO_INDEX_BUILD_CLASS);
    
    start = get_time_ms();
    for (size_t i = 0; i < num_lookups; i++) {
        nmo_class_id_t cid = (nmo_class_id_t)(i % 10);
        size_t count;
        nmo_object_index_get_by_class(index, cid, &count);
    }
    double time_with_index = get_time_ms() - start;
    
    printf("Performing %zu lookups on %zu objects:\n", num_lookups, num_objects);
    printf("  Without index: %.2f ms\n", time_without_index);
    printf("  With index:    %.2f ms\n", time_with_index);
    printf("  Speedup:       %.2fx\n", time_without_index / time_with_index);
    
    nmo_object_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

int main(void) {
    printf("=== Phase 5 Performance Tests ===\n");
    
    test_hash_table_reserve();
    test_arena_reserve();
    test_index_lookup();
    
    printf("\n=== All Performance Tests Complete ===\n");
    return 0;
}
