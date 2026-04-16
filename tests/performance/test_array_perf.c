#include "test_framework.h"
#include "nmo.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static nmo_status_t benchmark_bulk_zero(size_t element_count, size_t element_size, double *out_ms)
{
    nmo_allocator_t allocator = nmo_allocator_default();
    size_t bytes = element_count * element_size;
    void *buffer = nmo_alloc(&allocator, bytes, element_size);
    if (buffer == NULL) {
        return NMO_ERR_NOMEM;
    }

    memset(buffer, 0xA5, bytes);
    double start = test_get_time_ms();
    memset(buffer, 0, bytes);
    *out_ms = test_get_time_ms() - start;

    nmo_free(&allocator, buffer);
    return NMO_OK;
}

static void assert_zeroed_u64(const uint64_t *values, size_t count)
{
    ASSERT_NOT_NULL(values);
    ASSERT_EQ(0u, values[0]);
    ASSERT_EQ(0u, values[count / 2]);
    ASSERT_EQ(0u, values[count - 1]);
}

TEST(array_perf, allocator_extend_default_init_is_bulk_zero)
{
    const size_t element_count = 8u * 1024u * 1024u;
    double bulk_ms = 0.0;
    ASSERT_EQ(NMO_OK, benchmark_bulk_zero(element_count, sizeof(uint64_t), &bulk_ms));

    nmo_array_t array;
    ASSERT_EQ(NMO_OK, nmo_array_init(&array, sizeof(uint64_t), element_count, NULL));
    memset(array.data, 0xA5, element_count * sizeof(uint64_t));

    uint64_t *values = NULL;
    double start = test_get_time_ms();
    ASSERT_EQ(NMO_OK, nmo_array_extend(&array, element_count, (void **)&values));
    double extend_ms = test_get_time_ms() - start;

    assert_zeroed_u64(values, element_count);
    printf("[array_perf] allocator extend zero-init: bulk %.2f ms array %.2f ms\n",
           bulk_ms, extend_ms);
    ASSERT_TRUE(extend_ms <= bulk_ms * 2.0 + 2.0);

    nmo_array_dispose(&array);
}

TEST(array_perf, arena_resize_default_init_is_bulk_zero)
{
    const size_t element_count = 8u * 1024u * 1024u;
    double bulk_ms = 0.0;
    ASSERT_EQ(NMO_OK, benchmark_bulk_zero(element_count, sizeof(uint64_t), &bulk_ms));

    nmo_arena_t *arena = nmo_arena_create(NULL, element_count * sizeof(uint64_t) * 2u);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t array;
    ASSERT_EQ(NMO_OK, nmo_arena_array_init(&array, sizeof(uint64_t), element_count, arena));
    memset(array.data, 0xA5, element_count * sizeof(uint64_t));

    double start = test_get_time_ms();
    ASSERT_EQ(NMO_OK, nmo_arena_array_resize(&array, element_count));
    double resize_ms = test_get_time_ms() - start;

    assert_zeroed_u64((const uint64_t *)array.data, element_count);
    printf("[array_perf] arena resize zero-init: bulk %.2f ms array %.2f ms\n",
           bulk_ms, resize_ms);
    ASSERT_TRUE(resize_ms <= bulk_ms * 2.0 + 2.0);

    nmo_arena_array_reset(&array);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST_CATEGORIZED(array_perf, allocator_extend_default_init_is_bulk_zero, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(array_perf, arena_resize_default_init_is_bulk_zero, TEST_CATEGORY_PERFORMANCE);
TEST_MAIN_END()
