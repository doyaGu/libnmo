/**
 * @file test_load_save_mmap_baseline.c
 * @brief Baseline benchmarks for load/save/mmap paths
 */

#include "test_framework.h"
#include "nmo.h"
#include "app/nmo_parser.h"
#include "session/nmo_saver.h"
#include "io/nmo_io_mmap.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define NMO_GETPID _getpid
#else
#include <unistd.h>
#define NMO_GETPID getpid
#endif

typedef struct nmo_benchmark_sample {
    const char *label;
    const char *path;
} nmo_benchmark_sample_t;

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

static size_t read_env_size_t(const char *name, size_t fallback) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    char *end_ptr = NULL;
    unsigned long long parsed = strtoull(value, &end_ptr, 10);
    if (end_ptr == value || parsed == 0) {
        return fallback;
    }
    return (size_t)parsed;
}

static double read_env_double(const char *name, double fallback) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    char *end_ptr = NULL;
    double parsed = strtod(value, &end_ptr);
    if (end_ptr == value || parsed <= 0.0) {
        return fallback;
    }
    return parsed;
}

static int read_env_bool(const char *name) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    return (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0);
}

static int benchmark_load_ms(
    nmo_context_t *ctx,
    const char *path,
    size_t iterations,
    double *out_ms)
{
    if (ctx == NULL || path == NULL || iterations == 0 || out_ms == NULL) {
        return 0;
    }

    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        nmo_session_t *session = nmo_session_create(ctx);
        if (session == NULL) {
            return 0;
        }

        int status = nmo_load_file(session, path, NULL);
        if (status != NMO_OK) {
            nmo_session_destroy(session);
            return 0;
        }

        nmo_session_destroy(session);
    }
    *out_ms = (test_get_time_ms() - start) / (double)iterations;
    return 1;
}

static int benchmark_save_ms(
    nmo_context_t *ctx,
    const char *path,
    const char *label,
    size_t iterations,
    double *out_ms)
{
    if (ctx == NULL || path == NULL || label == NULL || iterations == 0 || out_ms == NULL) {
        return 0;
    }

    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        return 0;
    }
    if (nmo_load_file(session, path, NULL) != NMO_OK) {
        nmo_session_destroy(session);
        return 0;
    }

    double start = test_get_time_ms();
    for (size_t i = 0; i < iterations; i++) {
        char output_path[512];
        snprintf(output_path, sizeof(output_path), "bench_save_%s_%d_%zu.cmo",
                 label, (int)NMO_GETPID(), i);

        nmo_save_options_t save_opts = nmo_save_options_default();
        save_opts.flags |= NMO_SAVE_REQUIRE_SCHEMA;
        int status = nmo_save_file(session, output_path, &save_opts);
        if (status != NMO_OK) {
            nmo_session_destroy(session);
            remove(output_path);
            return 0;
        }
        remove(output_path);
    }

    *out_ms = (test_get_time_ms() - start) / (double)iterations;
    nmo_session_destroy(session);
    return 1;
}

static int benchmark_mmap_scan_ms(const char *path, size_t iterations, double *out_ms) {
    if (path == NULL || iterations == 0 || out_ms == NULL) {
        return 0;
    }

    nmo_io_mmap_t *mmap = nmo_io_mmap_open(path);
    if (mmap == NULL) {
        return 0;
    }

    const uint8_t *data = (const uint8_t *)nmo_io_mmap_data(mmap);
    size_t size = nmo_io_mmap_size(mmap);
    if (data == NULL || size == 0) {
        nmo_io_mmap_close(mmap);
        return 0;
    }

    volatile uint64_t checksum = 0;
    double start = test_get_time_ms();
    for (size_t iter = 0; iter < iterations; iter++) {
        for (size_t i = 0; i < size; i++) {
            checksum += data[i];
        }
    }
    *out_ms = (test_get_time_ms() - start) / (double)iterations;

    if (checksum == 0) {
        printf("[benchmark] checksum guard: %" PRIu64 "\n", (uint64_t)checksum);
    }

    nmo_io_mmap_close(mmap);
    return 1;
}

TEST(perf_baseline, load_save_mmap) {
    const size_t load_iterations = read_env_size_t("NMO_BENCH_LOAD_ITERS", 3);
    const size_t save_iterations = read_env_size_t("NMO_BENCH_SAVE_ITERS", 2);
    const size_t mmap_iterations = read_env_size_t("NMO_BENCH_MMAP_ITERS", 5);
    const int enforce_thresholds = read_env_bool("NMO_BENCH_ENFORCE");

    const double max_load_ms = read_env_double("NMO_BENCH_MAX_LOAD_MS", 3000.0);
    const double max_save_ms = read_env_double("NMO_BENCH_MAX_SAVE_MS", 4000.0);
    const double max_mmap_ms = read_env_double("NMO_BENCH_MAX_MMAP_MS", 1000.0);

    nmo_benchmark_sample_t samples[] = {
        { "2D_Text", NMO_TEST_DATA_FILE("Ballance/2D Text.nmo") },
        { "Nop", NMO_TEST_DATA_FILE("Nop.cmo") }
    };

    nmo_context_desc_t ctx_desc = {0};
    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    ASSERT_NOT_NULL(ctx);

    double load_total = 0.0;
    double save_total = 0.0;
    double mmap_total = 0.0;
    size_t measured_samples = 0;

    printf("[perf_baseline] iterations: load=%zu save=%zu mmap=%zu\n",
           load_iterations, save_iterations, mmap_iterations);

    for (size_t i = 0; i < (sizeof(samples) / sizeof(samples[0])); i++) {
        const nmo_benchmark_sample_t *sample = &samples[i];
        if (!file_exists(sample->path)) {
            printf("[perf_baseline] skip missing sample: %s (%s)\n", sample->label, sample->path);
            continue;
        }

        double load_ms = 0.0;
        double save_ms = 0.0;
        double mmap_ms = 0.0;

        ASSERT_TRUE(benchmark_load_ms(ctx, sample->path, load_iterations, &load_ms));
        ASSERT_TRUE(benchmark_save_ms(ctx, sample->path, sample->label, save_iterations, &save_ms));
        ASSERT_TRUE(benchmark_mmap_scan_ms(sample->path, mmap_iterations, &mmap_ms));

        printf("[perf_baseline] %s: load=%.2f ms, save=%.2f ms, mmap_scan=%.2f ms\n",
               sample->label, load_ms, save_ms, mmap_ms);

        load_total += load_ms;
        save_total += save_ms;
        mmap_total += mmap_ms;
        measured_samples++;

        if (enforce_thresholds) {
            ASSERT_TRUE(load_ms <= max_load_ms);
            ASSERT_TRUE(save_ms <= max_save_ms);
            ASSERT_TRUE(mmap_ms <= max_mmap_ms);
        }
    }

    ASSERT_TRUE(measured_samples > 0);

    printf("[perf_baseline] avg: load=%.2f ms, save=%.2f ms, mmap_scan=%.2f ms (samples=%zu)\n",
           load_total / (double)measured_samples,
           save_total / (double)measured_samples,
           mmap_total / (double)measured_samples,
           measured_samples);

    if (enforce_thresholds) {
        printf("[perf_baseline] thresholds: load<=%.2f, save<=%.2f, mmap<=%.2f\n",
               max_load_ms, max_save_ms, max_mmap_ms);
    }

    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST_CATEGORIZED(perf_baseline, load_save_mmap, TEST_CATEGORY_PERFORMANCE);
TEST_MAIN_END()
