#include "test_framework.h"
#include "nmo.h"
#include "session/nmo_object_index.h"

#include <stdio.h>

static void populate_repository(
    nmo_object_repository_t *repo,
    nmo_arena_t *arena,
    size_t object_count,
    size_t class_bucket_count,
    size_t guid_bucket_count
) {
    for (size_t i = 0; i < object_count; i++) {
        nmo_object_id_t id = (nmo_object_id_t)(i + 1);
        nmo_class_id_t class_id = (nmo_class_id_t)((i % class_bucket_count) + 1);
        nmo_object_t *obj = nmo_object_create(arena, id, class_id);
        ASSERT_NOT_NULL(obj);

        char name[64];
        snprintf(name, sizeof(name), "Object_%zu", i);
        ASSERT_EQ(NMO_OK, nmo_object_set_name(obj, name, arena));

        nmo_guid_t guid = {
            0xA0000000u + (uint32_t)(i % guid_bucket_count),
            0xB0000000u + (uint32_t)((i * 2654435761u) & 0xFFFFFFFFu)
        };
        obj->type_guid = guid;

        ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, obj));
    }
}

static size_t repository_count_class_linear(
    const nmo_object_repository_t *repo,
    nmo_class_id_t class_id
) {
    size_t total = nmo_object_repository_get_count(repo);
    size_t matches = 0;
    for (size_t i = 0; i < total; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        if (obj != NULL && obj->class_id == class_id) {
            matches++;
        }
    }
    return matches;
}

static size_t repository_find_guid_linear(
    const nmo_object_repository_t *repo,
    nmo_guid_t guid
) {
    size_t total = nmo_object_repository_get_count(repo);
    for (size_t i = 0; i < total; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        if (obj != NULL && nmo_guid_equals(obj->type_guid, guid)) {
            return obj->id;
        }
    }
    return 0;
}

static void benchmark_class_linear(
    const nmo_object_repository_t *repo,
    size_t iterations,
    size_t class_bucket_count,
    double *out_ms
) {
    ASSERT_NOT_NULL(out_ms);
    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        nmo_class_id_t target = (nmo_class_id_t)((i % class_bucket_count) + 1);
        size_t count = repository_count_class_linear(repo, target);
        ASSERT_TRUE(count > 0);
    }
    *out_ms = test_get_time_ms() - start;
}

static void benchmark_class_index(
    const nmo_object_index_t *index,
    size_t iterations,
    size_t class_bucket_count,
    double *out_ms
) {
    ASSERT_NOT_NULL(out_ms);
    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        nmo_class_id_t target = (nmo_class_id_t)((i % class_bucket_count) + 1);
        size_t count = 0;
        nmo_object_t **objects = nmo_object_index_get_by_class(index, target, &count);
        ASSERT_NOT_NULL(objects);
        ASSERT_TRUE(count > 0);
    }
    *out_ms = test_get_time_ms() - start;
}

static void benchmark_guid_linear(
    const nmo_object_repository_t *repo,
    size_t iterations,
    size_t guid_bucket_count,
    double *out_ms
) {
    ASSERT_NOT_NULL(out_ms);
    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        nmo_guid_t guid = {
            0xA0000000u + (uint32_t)(i % guid_bucket_count),
            0xB0000000u + (uint32_t)((i * 2654435761u) & 0xFFFFFFFFu)
        };
        size_t found = repository_find_guid_linear(repo, guid);
        ASSERT_TRUE(found != 0);
    }
    *out_ms = test_get_time_ms() - start;
}

static void benchmark_guid_index(
    const nmo_object_index_t *index,
    size_t iterations,
    size_t guid_bucket_count,
    double *out_ms
) {
    ASSERT_NOT_NULL(out_ms);
    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        nmo_guid_t guid = {
            0xA0000000u + (uint32_t)(i % guid_bucket_count),
            0xB0000000u + (uint32_t)((i * 2654435761u) & 0xFFFFFFFFu)
        };
        nmo_object_t *obj = nmo_object_index_find_by_guid(index, guid);
        ASSERT_NOT_NULL(obj);
    }
    *out_ms = test_get_time_ms() - start;
}

TEST(index_perf, class_lookup_performance) {
    const size_t object_count = 20000;
    const size_t iterations = 2000;
    const size_t class_bucket_count = 32;
    const size_t guid_bucket_count = 64;

    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    ASSERT_NOT_NULL(arena);

    nmo_object_repository_t *repo = nmo_object_repository_create(arena);
    ASSERT_NOT_NULL(repo);

    populate_repository(repo, arena, object_count, class_bucket_count, guid_bucket_count);

    nmo_object_index_t *index = nmo_object_index_create(repo, arena);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_index_build(index, NMO_INDEX_BUILD_CLASS | NMO_INDEX_BUILD_GUID));

    double linear_ms = 0.0;
    double indexed_ms = 0.0;
    benchmark_class_linear(repo, iterations, class_bucket_count, &linear_ms);
    benchmark_class_index(index, iterations, class_bucket_count, &indexed_ms);

    printf("[index_perf] Class lookup: linear %.2f ms vs indexed %.2f ms (speedup %.2fx)\n",
           linear_ms, indexed_ms,
           (indexed_ms > 0.0) ? (linear_ms / indexed_ms) : 0.0);

    double guid_linear_ms = 0.0;
    double guid_indexed_ms = 0.0;
    benchmark_guid_linear(repo, iterations, guid_bucket_count, &guid_linear_ms);
    benchmark_guid_index(index, iterations, guid_bucket_count, &guid_indexed_ms);

    printf("[index_perf] GUID lookup: linear %.2f ms vs indexed %.2f ms (speedup %.2fx)\n",
           guid_linear_ms, guid_indexed_ms,
           (guid_indexed_ms > 0.0) ? (guid_linear_ms / guid_indexed_ms) : 0.0);

    nmo_object_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST_CATEGORIZED(index_perf, class_lookup_performance, TEST_CATEGORY_PERFORMANCE);
TEST_MAIN_END()
