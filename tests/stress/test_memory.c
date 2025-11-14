#include "test_framework.h"
#include "nmo.h"
#include "format/nmo_chunk_pool.h"

#define STRESS_RESERVE_BYTES (8 * 1024 * 1024)
#define STRESS_ALLOC_SIZE 64
#define STRESS_CYCLES 6
#define STRESS_ALLOCATIONS_PER_CYCLE 20000

#define CHUNK_POOL_INITIAL_CAPACITY 128
#define CHUNK_POOL_WAVES 64

TEST(memory_stress, arena_reserve_pressure) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    ASSERT_NOT_NULL(arena);

    const size_t targets[] = {
        STRESS_RESERVE_BYTES,
        STRESS_RESERVE_BYTES * 2,
        STRESS_RESERVE_BYTES * 4
    };

    for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
        ASSERT_EQ(NMO_OK, nmo_arena_reserve(arena, targets[t]));
        ASSERT_GE(nmo_arena_total_allocated(arena), targets[t]);

        for (size_t cycle = 0; cycle < STRESS_CYCLES; cycle++) {
            for (size_t i = 0; i < STRESS_ALLOCATIONS_PER_CYCLE; i++) {
                void *ptr = nmo_arena_alloc(arena, STRESS_ALLOC_SIZE, 16);
                ASSERT_NOT_NULL(ptr);
            }

            ASSERT_GT(nmo_arena_bytes_used(arena), 0u);
            nmo_arena_reset(arena);
            ASSERT_EQ(0u, nmo_arena_bytes_used(arena));
        }
    }

    nmo_arena_destroy(arena);
}

TEST(memory_stress, chunk_pool_reuse) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    ASSERT_NOT_NULL(arena);
    ASSERT_EQ(NMO_OK, nmo_arena_reserve(arena, STRESS_RESERVE_BYTES));

    nmo_chunk_pool_t *pool = nmo_chunk_pool_create(CHUNK_POOL_INITIAL_CAPACITY, arena);
    ASSERT_NOT_NULL(pool);

    nmo_chunk_t *active[CHUNK_POOL_INITIAL_CAPACITY];
    size_t observed_total = 0;

    for (size_t wave = 0; wave < CHUNK_POOL_WAVES; wave++) {
        for (size_t i = 0; i < CHUNK_POOL_INITIAL_CAPACITY; i++) {
            nmo_chunk_t *chunk = nmo_chunk_pool_acquire(pool);
            ASSERT_NOT_NULL(chunk);

            chunk->class_id = (nmo_class_id_t)((wave + i) & 0xFFFF);
            chunk->data_version = (uint32_t)wave;
            chunk->chunk_options = NMO_CHUNK_OPTION_IDS;
            chunk->data_size = 0;

            active[i] = chunk;
        }

        size_t total = 0, available = 0, in_use = 0;
        nmo_chunk_pool_get_stats(pool, &total, &available, &in_use);
        ASSERT_EQ(CHUNK_POOL_INITIAL_CAPACITY, total);
        ASSERT_EQ(0u, available);
        ASSERT_EQ(CHUNK_POOL_INITIAL_CAPACITY, in_use);
        observed_total = total;

        for (size_t i = 0; i < CHUNK_POOL_INITIAL_CAPACITY; i++) {
            nmo_chunk_pool_release(pool, active[i]);
        }

        nmo_chunk_pool_get_stats(pool, &total, &available, &in_use);
        ASSERT_EQ(total, CHUNK_POOL_INITIAL_CAPACITY);
        ASSERT_EQ(total, available);
        ASSERT_EQ(0u, in_use);
    }

    ASSERT_EQ(CHUNK_POOL_INITIAL_CAPACITY, observed_total);

    nmo_chunk_pool_clear(pool);
    size_t total = 0, available = 0, in_use = 0;
    nmo_chunk_pool_get_stats(pool, &total, &available, &in_use);
    ASSERT_EQ(CHUNK_POOL_INITIAL_CAPACITY, total);
    ASSERT_EQ(total, available);
    ASSERT_EQ(0u, in_use);

    nmo_chunk_pool_destroy(pool);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST_CATEGORIZED(memory_stress, arena_reserve_pressure, TEST_CATEGORY_STRESS);
    REGISTER_TEST_CATEGORIZED(memory_stress, chunk_pool_reuse, TEST_CATEGORY_STRESS);
TEST_MAIN_END()
