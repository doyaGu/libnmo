#include "test_framework.h"
#include "nmo.h"

#include <stdio.h>
#include <stdint.h>

static nmo_object_t *make_perf_object(
    const nmo_allocator_t *allocator,
    nmo_object_id_t id,
    nmo_class_id_t class_id,
    const char *name)
{
    nmo_object_t *obj = nmo_object_create(allocator, id, class_id);
    if (obj == NULL) {
        return NULL;
    }
    if (nmo_object_set_name(obj, name) != NMO_OK) {
        nmo_object_destroy(obj);
        return NULL;
    }
    return obj;
}

static void populate_perf_repo(
    nmo_object_repository_t *repo,
    const nmo_allocator_t *allocator,
    size_t object_count,
    size_t class_bucket_count)
{
    for (size_t i = 0; i < object_count; i++) {
        char name[96];
        if ((i % 97) == 0) {
            snprintf(name, sizeof(name), "needle_target_%05zu", i);
        } else if ((i % 131) == 0) {
            snprintf(name, sizeof(name), "CameraFocus_%05zu", i);
        } else {
            snprintf(name, sizeof(name), "Object_%05zu_bucket_%zu", i, i % class_bucket_count);
        }

        nmo_object_t *obj = make_perf_object(
            allocator,
            (nmo_object_id_t)(i + 1),
            (nmo_class_id_t)((i % class_bucket_count) + 1),
            name);
        ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &obj));
    }
}

static void populate_common_trigram_repo(
    nmo_object_repository_t *repo,
    const nmo_allocator_t *allocator,
    size_t object_count)
{
    for (size_t i = 0; i < object_count; i++) {
        char name[96];
        if (i == object_count / 2) {
            snprintf(name, sizeof(name), "Needle_ABCDE_%05zu", i);
        } else if ((i % 3) == 0) {
            snprintf(name, sizeof(name), "Common_ABC_%05zu", i);
        } else if ((i % 3) == 1) {
            snprintf(name, sizeof(name), "Common_BCD_%05zu", i);
        } else {
            snprintf(name, sizeof(name), "Common_CDE_%05zu", i);
        }

        nmo_object_t *obj = make_perf_object(
            allocator,
            (nmo_object_id_t)(i + 1),
            (nmo_class_id_t)((i % 16) + 1),
            name);
        ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &obj));
    }
}

static size_t count_linear(
    nmo_object_repository_t *repo,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry)
{
    size_t total = nmo_object_repository_get_count(repo);
    size_t matches = 0;
    for (size_t i = 0; i < total; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        bool match = false;
        if (nmo_object_query_matches(obj, query, registry, &match) != NMO_OK) {
            return SIZE_MAX;
        }
        if (match) {
            matches++;
        }
    }
    return matches;
}

static double benchmark_linear(
    nmo_object_repository_t *repo,
    const nmo_object_query_t *query,
    const nmo_type_registry_t *registry,
    size_t iterations,
    size_t *out_matches)
{
    size_t matches = 0;
    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        matches += count_linear(repo, query, registry);
    }
    double elapsed = test_get_time_ms() - start;
    *out_matches = matches;
    return elapsed;
}

static double benchmark_indexed(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    size_t iterations,
    size_t *out_matches)
{
    size_t matches = 0;
    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        nmo_object_query_result_t result = {0};
        if (nmo_object_query_iterate(ctx, query, NULL, NULL, &result) != NMO_OK) {
            *out_matches = SIZE_MAX;
            return 0.0;
        }
        matches += result.matched;
    }
    double elapsed = test_get_time_ms() - start;
    *out_matches = matches;
    return elapsed;
}

static bool count_visit(size_t object_index, nmo_object_t *object, void *user_data)
{
    size_t *count = (size_t *)user_data;
    (void)object_index;
    if (object != NULL && count != NULL) {
        (*count)++;
    }
    return true;
}

static bool collect_accept_all(const nmo_object_t *object, void *user_data)
{
    (void)object;
    (void)user_data;
    return true;
}

static double benchmark_iterate_with_visitor(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    size_t iterations,
    size_t *out_matches,
    size_t *out_visits)
{
    size_t matches = 0;
    size_t visits = 0;
    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        size_t visit_count = 0;
        nmo_object_query_result_t result = {0};
        if (nmo_object_query_iterate(ctx, query, count_visit, &visit_count, &result) != NMO_OK) {
            *out_matches = SIZE_MAX;
            *out_visits = SIZE_MAX;
            return 0.0;
        }
        matches += result.matched;
        visits += visit_count;
    }
    double elapsed = test_get_time_ms() - start;
    *out_matches = matches;
    *out_visits = visits;
    return elapsed;
}

static double benchmark_collect_query(
    const nmo_object_query_context_t *ctx,
    const nmo_object_query_t *query,
    nmo_arena_t *arena,
    size_t iterations,
    size_t *out_count)
{
    size_t total = 0;
    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        nmo_arena_reset(arena);
        nmo_object_t **objects = NULL;
        size_t count = 0;
        if (nmo_object_query_collect(ctx, query, arena, &objects, &count, NULL) != NMO_OK) {
            *out_count = SIZE_MAX;
            return 0.0;
        }
        total += count;
    }
    double elapsed = test_get_time_ms() - start;
    *out_count = total;
    return elapsed;
}

static void assert_speedup(
    const char *label,
    double linear_ms,
    double indexed_ms,
    double min_speedup)
{
    double speedup = indexed_ms > 0.0 ? linear_ms / indexed_ms : 9999.0;
    printf("[object_query_perf] %s: linear %.2f ms indexed %.2f ms speedup %.2fx\n",
           label, linear_ms, indexed_ms, speedup);
    ASSERT_TRUE(speedup >= min_speedup);
}

TEST(object_query_perf, session_index_accelerates_repeated_queries)
{
    const size_t object_count = 50000;
    const size_t class_bucket_count = 32;
    const size_t iterations = 250;

    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);
    populate_perf_repo(repo, &allocator, object_count, class_bucket_count);

    nmo_object_query_index_t *index =
        nmo_object_query_index_create(repo, registry, &allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));

    nmo_object_query_context_t qctx = {
        .repository = repo,
        .index = index,
        .registry = registry
    };

    nmo_object_query_t id_query = { .object_id = 42424 };
    nmo_object_query_t class_query = { .class_id = 17 };
    nmo_object_query_t name_query = {
        .name = "Object_12345_bucket_25",
        .name_mode = NMO_OBJECT_QUERY_NAME_EXACT
    };
    nmo_object_query_t substring_query = {
        .name = "needle_target",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true
    };
    nmo_object_query_t wildcard_query = {
        .name = "*CameraFocus*",
        .name_mode = NMO_OBJECT_QUERY_NAME_WILDCARD,
        .name_case_insensitive = true
    };

    size_t linear_matches = 0;
    size_t indexed_matches = 0;
    double linear_ms = benchmark_linear(repo, &id_query, registry, iterations, &linear_matches);
    double indexed_ms = benchmark_indexed(&qctx, &id_query, iterations, &indexed_matches);
    ASSERT_EQ(linear_matches, indexed_matches);
    assert_speedup("object id", linear_ms, indexed_ms, 25.0);

    linear_ms = benchmark_linear(repo, &class_query, registry, iterations, &linear_matches);
    indexed_ms = benchmark_indexed(&qctx, &class_query, iterations, &indexed_matches);
    ASSERT_EQ(linear_matches, indexed_matches);
    assert_speedup("class", linear_ms, indexed_ms, 10.0);

    linear_ms = benchmark_linear(repo, &name_query, registry, iterations, &linear_matches);
    indexed_ms = benchmark_indexed(&qctx, &name_query, iterations, &indexed_matches);
    ASSERT_EQ(linear_matches, indexed_matches);
    assert_speedup("exact name", linear_ms, indexed_ms, 20.0);

    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&qctx, &substring_query, NULL, NULL, NULL));
    linear_ms = benchmark_linear(repo, &substring_query, registry, iterations, &linear_matches);
    indexed_ms = benchmark_indexed(&qctx, &substring_query, iterations, &indexed_matches);
    ASSERT_EQ(linear_matches, indexed_matches);
    assert_speedup("substring", linear_ms, indexed_ms, 5.0);

    linear_ms = benchmark_linear(repo, &wildcard_query, registry, iterations, &linear_matches);
    indexed_ms = benchmark_indexed(&qctx, &wildcard_query, iterations, &indexed_matches);
    ASSERT_EQ(linear_matches, indexed_matches);
    assert_speedup("wildcard literal", linear_ms, indexed_ms, 3.0);

    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
}

TEST(object_query_perf, indexed_collect_predicate_uses_single_candidate_pass)
{
    const size_t object_count = 50000;
    const size_t class_bucket_count = 32;
    const size_t iterations = 300;

    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);
    populate_perf_repo(repo, &allocator, object_count, class_bucket_count);

    nmo_object_query_index_t *index =
        nmo_object_query_index_create(repo, registry, &allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));

    nmo_arena_t *linear_arena = nmo_arena_create(NULL, 1024 * 1024);
    nmo_arena_t *indexed_arena = nmo_arena_create(NULL, 1024 * 1024);
    ASSERT_NOT_NULL(linear_arena);
    ASSERT_NOT_NULL(indexed_arena);

    nmo_object_query_context_t linear_ctx = {
        .repository = repo,
        .index = NULL,
        .registry = registry
    };
    nmo_object_query_context_t indexed_ctx = {
        .repository = repo,
        .index = index,
        .registry = registry
    };
    nmo_object_query_t query = {
        .class_id = 17,
        .predicate = collect_accept_all
    };

    size_t linear_count = 0;
    size_t indexed_count = 0;
    double linear_ms = benchmark_collect_query(
        &linear_ctx, &query, linear_arena, iterations, &linear_count);
    double indexed_ms = benchmark_collect_query(
        &indexed_ctx, &query, indexed_arena, iterations, &indexed_count);
    ASSERT_EQ(linear_count, indexed_count);
    assert_speedup("collect class predicate", linear_ms, indexed_ms, 100.0);

    nmo_arena_destroy(indexed_arena);
    nmo_arena_destroy(linear_arena);
    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
}

TEST(object_query_perf, indexed_visitor_class_query_avoids_repository_lookup)
{
    const size_t object_count = 50000;
    const size_t class_bucket_count = 32;
    const size_t iterations = 300;

    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);
    populate_perf_repo(repo, &allocator, object_count, class_bucket_count);

    nmo_object_query_index_t *index =
        nmo_object_query_index_create(repo, registry, &allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));

    nmo_object_query_context_t linear_ctx = {
        .repository = repo,
        .index = NULL,
        .registry = registry
    };
    nmo_object_query_context_t indexed_ctx = {
        .repository = repo,
        .index = index,
        .registry = registry
    };
    nmo_object_query_t class_query = { .class_id = 17 };

    size_t linear_matches = 0;
    size_t indexed_matches = 0;
    size_t linear_visits = 0;
    size_t indexed_visits = 0;
    double linear_ms = benchmark_iterate_with_visitor(
        &linear_ctx, &class_query, iterations, &linear_matches, &linear_visits);
    double indexed_ms = benchmark_iterate_with_visitor(
        &indexed_ctx, &class_query, iterations, &indexed_matches, &indexed_visits);
    ASSERT_EQ(linear_matches, indexed_matches);
    ASSERT_EQ(linear_visits, indexed_visits);
    assert_speedup("class visitor", linear_ms, indexed_ms, 45.0);

    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
}

TEST(object_query_perf, eager_rebuild_uses_batched_name_storage)
{
    const size_t object_count = 5000;
    const size_t class_bucket_count = 32;

    nmo_allocator_t base_allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    nmo_object_repository_t *repo = nmo_object_repository_create(&base_allocator);
    ASSERT_NOT_NULL(repo);
    populate_perf_repo(repo, &base_allocator, object_count, class_bucket_count);

    nmo_allocator_stats_t stats = {0};
    nmo_allocator_tracking_t tracking = {0};
    nmo_allocator_t index_allocator =
        nmo_allocator_tracking_init(&tracking, base_allocator, &stats);

    nmo_object_query_index_t *index =
        nmo_object_query_index_create(repo, registry, &index_allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));

    printf("[object_query_perf] eager rebuild allocations: %zu for %zu objects\n",
           stats.total_allocations, object_count);
    ASSERT_LT(stats.total_allocations, object_count / 4);

    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
}

TEST(object_query_perf, text_index_build_reserves_trigram_storage)
{
    const size_t object_count = 5000;
    const size_t class_bucket_count = 32;

    nmo_allocator_t base_allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    nmo_object_repository_t *repo = nmo_object_repository_create(&base_allocator);
    ASSERT_NOT_NULL(repo);
    populate_perf_repo(repo, &base_allocator, object_count, class_bucket_count);

    nmo_allocator_stats_t stats = {0};
    nmo_allocator_tracking_t tracking = {0};
    nmo_allocator_t index_allocator =
        nmo_allocator_tracking_init(&tracking, base_allocator, &stats);

    nmo_object_query_index_t *index =
        nmo_object_query_index_create(repo, registry, &index_allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));
    size_t allocations_after_eager = stats.total_allocations;

    nmo_object_query_context_t qctx = {
        .repository = repo,
        .index = index,
        .registry = registry
    };
    nmo_object_query_t substring_query = {
        .name = "needle_target",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true
    };
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&qctx, &substring_query, NULL, NULL, NULL));

    size_t text_allocations = stats.total_allocations - allocations_after_eager;
    printf("[object_query_perf] text index build allocations: %zu for %zu objects\n",
           text_allocations, object_count);
    ASSERT_LE(text_allocations, 3);

    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
}

TEST(object_query_perf, trim_releases_text_index_storage)
{
    const size_t object_count = 5000;
    const size_t class_bucket_count = 32;

    nmo_allocator_t base_allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    nmo_object_repository_t *repo = nmo_object_repository_create(&base_allocator);
    ASSERT_NOT_NULL(repo);
    populate_perf_repo(repo, &base_allocator, object_count, class_bucket_count);

    nmo_allocator_stats_t stats = {0};
    nmo_allocator_tracking_t tracking = {0};
    nmo_allocator_t index_allocator =
        nmo_allocator_tracking_init(&tracking, base_allocator, &stats);

    nmo_object_query_index_t *index =
        nmo_object_query_index_create(repo, registry, &index_allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));
    size_t bytes_after_eager = stats.current_bytes;

    nmo_object_query_context_t qctx = {
        .repository = repo,
        .index = index,
        .registry = registry
    };
    nmo_object_query_t substring_query = {
        .name = "needle_target",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true
    };
    nmo_object_query_result_t before_trim = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&qctx, &substring_query, NULL, NULL, &before_trim));
    size_t bytes_after_text = stats.current_bytes;
    ASSERT_GT(bytes_after_text, bytes_after_eager);

    nmo_object_query_index_trim(index, NMO_OBJECT_QUERY_INDEX_TEXT);
    printf("[object_query_perf] trim text index bytes: eager=%zu text=%zu trimmed=%zu\n",
           bytes_after_eager, bytes_after_text, stats.current_bytes);
    ASSERT_LE(stats.current_bytes, bytes_after_eager);

    nmo_object_query_result_t after_trim = {0};
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&qctx, &substring_query, NULL, NULL, &after_trim));
    ASSERT_EQ(before_trim.matched, after_trim.matched);

    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
}

TEST(object_query_perf, text_reducer_intersects_common_trigrams)
{
    const size_t object_count = 60000;
    const size_t iterations = 150;

    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    const nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);

    nmo_object_repository_t *repo = nmo_object_repository_create(&allocator);
    ASSERT_NOT_NULL(repo);
    populate_common_trigram_repo(repo, &allocator, object_count);

    nmo_object_query_index_t *index =
        nmo_object_query_index_create(repo, registry, &allocator);
    ASSERT_NOT_NULL(index);
    ASSERT_EQ(NMO_OK, nmo_object_query_index_rebuild(index));

    nmo_object_query_context_t qctx = {
        .repository = repo,
        .index = index,
        .registry = registry
    };
    nmo_object_query_t substring_query = {
        .name = "abcde",
        .name_mode = NMO_OBJECT_QUERY_NAME_SUBSTRING,
        .name_case_insensitive = true
    };

    size_t linear_matches = 0;
    size_t indexed_matches = 0;
    ASSERT_EQ(NMO_OK, nmo_object_query_iterate(&qctx, &substring_query, NULL, NULL, NULL));
    double linear_ms =
        benchmark_linear(repo, &substring_query, registry, iterations, &linear_matches);
    double indexed_ms =
        benchmark_indexed(&qctx, &substring_query, iterations, &indexed_matches);
    ASSERT_EQ(linear_matches, indexed_matches);
    assert_speedup("substring common trigram intersection", linear_ms, indexed_ms, 8.0);

    nmo_object_query_index_destroy(index);
    nmo_object_repository_destroy(repo);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST_CATEGORIZED(object_query_perf, session_index_accelerates_repeated_queries, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(object_query_perf, indexed_collect_predicate_uses_single_candidate_pass, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(object_query_perf, indexed_visitor_class_query_avoids_repository_lookup, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(object_query_perf, eager_rebuild_uses_batched_name_storage, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(object_query_perf, text_index_build_reserves_trigram_storage, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(object_query_perf, trim_releases_text_index_storage, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(object_query_perf, text_reducer_intersects_common_trigrams, TEST_CATEGORY_PERFORMANCE);
TEST_MAIN_END()
