/**
 * @file test_load_save_phase_stats.c
 * @brief Structural tests for load/save phase performance statistics
 */

#include "test_framework.h"
#include "nmo.h"
#include "app/nmo_load.h"
#include "app/nmo_save.h"
#include "app/nmo_perf_stats.h"

#include <stdbool.h>
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

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static void phase_stats_make_output_path(char *buffer,
                                         size_t buffer_size,
                                         const char *prefix,
                                         int iteration,
                                         const char *fixture) {
    const char *ext = strrchr(fixture, '.');
    if (ext == NULL || ext[1] == '\0') {
        ext = ".nmo";
    }
    snprintf(buffer, buffer_size, "%s_%d_%d%s", prefix, (int)NMO_GETPID(), iteration, ext);
}

static void phase_stats_add_load(nmo_load_perf_stats_t *sum,
                                 const nmo_load_perf_stats_t *sample) {
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        sum->phases[i].calls += sample->phases[i].calls;
        sum->phases[i].milliseconds += sample->phases[i].milliseconds;
    }
    sum->packed_header1_bytes = sample->packed_header1_bytes;
    sum->unpacked_header1_bytes = sample->unpacked_header1_bytes;
    sum->packed_data_bytes = sample->packed_data_bytes;
    sum->unpacked_data_bytes = sample->unpacked_data_bytes;
}

static void phase_stats_add_save(nmo_save_perf_stats_t *sum,
                                 const nmo_save_perf_stats_t *sample) {
    for (int i = 0; i < NMO_SAVE_PERF_PHASE_COUNT; i++) {
        sum->phases[i].calls += sample->phases[i].calls;
        sum->phases[i].milliseconds += sample->phases[i].milliseconds;
    }
    sum->planned_chunk_bytes = sample->planned_chunk_bytes;
    sum->header1_unpacked_bytes = sample->header1_unpacked_bytes;
    sum->data_unpacked_bytes = sample->data_unpacked_bytes;
    sum->header1_packed_bytes = sample->header1_packed_bytes;
    sum->data_packed_bytes = sample->data_packed_bytes;
}

static void phase_stats_print_load_json(const nmo_load_perf_stats_t *stats, double wall_ms) {
    printf("\"load\":{\"wall_ms\":%.3f,"
           "\"packed_header1_bytes\":%zu,\"unpacked_header1_bytes\":%zu,"
           "\"packed_data_bytes\":%zu,\"unpacked_data_bytes\":%zu,\"phases\":[",
           wall_ms,
           stats->packed_header1_bytes,
           stats->unpacked_header1_bytes,
           stats->packed_data_bytes,
           stats->unpacked_data_bytes);
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        if (i > 0) {
            printf(",");
        }
        printf("{\"name\":\"%s\",\"calls\":%llu,\"ms\":%.3f}",
               nmo_load_perf_phase_name((nmo_load_perf_phase_t)i),
               (unsigned long long)stats->phases[i].calls,
               stats->phases[i].milliseconds);
    }
    printf("]}");
}

static void phase_stats_print_json_string(const char *value) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
        switch (*p) {
            case '\\':
                fputs("\\\\", stdout);
                break;
            case '"':
                fputs("\\\"", stdout);
                break;
            case '\b':
                fputs("\\b", stdout);
                break;
            case '\f':
                fputs("\\f", stdout);
                break;
            case '\n':
                fputs("\\n", stdout);
                break;
            case '\r':
                fputs("\\r", stdout);
                break;
            case '\t':
                fputs("\\t", stdout);
                break;
            default:
                if (*p < 0x20) {
                    printf("\\u%04x", (unsigned int)*p);
                } else {
                    putchar((int)*p);
                }
                break;
        }
    }
    putchar('"');
}

static void phase_stats_print_save_json(const nmo_save_perf_stats_t *stats, double wall_ms) {
    printf("\"save\":{\"wall_ms\":%.3f,"
           "\"planned_chunk_bytes\":%zu,\"header1_unpacked_bytes\":%zu,"
           "\"data_unpacked_bytes\":%zu,\"header1_packed_bytes\":%zu,"
           "\"data_packed_bytes\":%zu,\"phases\":[",
           wall_ms,
           stats->planned_chunk_bytes,
           stats->header1_unpacked_bytes,
           stats->data_unpacked_bytes,
           stats->header1_packed_bytes,
           stats->data_packed_bytes);
    for (int i = 0; i < NMO_SAVE_PERF_PHASE_COUNT; i++) {
        if (i > 0) {
            printf(",");
        }
        printf("{\"name\":\"%s\",\"calls\":%llu,\"ms\":%.3f}",
               nmo_save_perf_phase_name((nmo_save_perf_phase_t)i),
               (unsigned long long)stats->phases[i].calls,
               stats->phases[i].milliseconds);
    }
    printf("]}");
}

static void phase_stats_print_load_text(const nmo_load_perf_stats_t *stats, double wall_ms) {
    printf("Load wall: %.3f ms\n", wall_ms);
    printf("Load bytes: header1=%zu/%zu data=%zu/%zu\n",
           stats->packed_header1_bytes,
           stats->unpacked_header1_bytes,
           stats->packed_data_bytes,
           stats->unpacked_data_bytes);
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        printf("  load %-24s calls=%llu ms=%.3f\n",
               nmo_load_perf_phase_name((nmo_load_perf_phase_t)i),
               (unsigned long long)stats->phases[i].calls,
               stats->phases[i].milliseconds);
    }
}

static void phase_stats_print_save_text(const nmo_save_perf_stats_t *stats, double wall_ms) {
    printf("Save wall: %.3f ms\n", wall_ms);
    printf("Save bytes: header1=%zu/%zu data=%zu/%zu planned=%zu\n",
           stats->header1_packed_bytes,
           stats->header1_unpacked_bytes,
           stats->data_packed_bytes,
           stats->data_unpacked_bytes,
           stats->planned_chunk_bytes);
    for (int i = 0; i < NMO_SAVE_PERF_PHASE_COUNT; i++) {
        printf("  save %-24s calls=%llu ms=%.3f\n",
               nmo_save_perf_phase_name((nmo_save_perf_phase_t)i),
               (unsigned long long)stats->phases[i].calls,
               stats->phases[i].milliseconds);
    }
}

TEST(phase_stats, load_records_required_phases) {
    const char *fixture = NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo");
    if (!file_exists(fixture)) {
        printf("[phase_stats] skip missing fixture: %s\n", fixture);
        return;
    }

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_load_perf_stats_t stats;
    nmo_load_perf_stats_reset(&stats);

    nmo_load_options_t opts = nmo_load_options_default();
    opts.collect_perf_stats = true;
    opts.perf_stats = &stats;

    ASSERT_EQ(NMO_OK, nmo_load_file(session, fixture, &opts));

    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_OPEN_DETECT].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_READ].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_PARSE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_DATA_READ].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_DATA_PARSE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_OBJECT_CREATE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_OBJECT_DESERIALIZE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_REFERENCE_RESOLVE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_MANAGER_POST_LOAD].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_INDEX_REBUILD].calls > 0);
    ASSERT_TRUE(stats.packed_header1_bytes > 0);
    ASSERT_TRUE(stats.unpacked_header1_bytes > 0);
    ASSERT_TRUE(stats.packed_data_bytes > 0);
    ASSERT_TRUE(stats.unpacked_data_bytes > 0);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(phase_stats, save_records_required_phases) {
    const char *fixture = NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo");
    if (!file_exists(fixture)) {
        printf("[phase_stats] skip missing fixture: %s\n", fixture);
        return;
    }

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    ASSERT_EQ(NMO_OK, nmo_load_file(session, fixture, NULL));

    char output_path[512];
    snprintf(output_path, sizeof(output_path),
             "test_phase_stats_save_%d.cmo", (int)NMO_GETPID());
    remove(output_path);

    nmo_save_perf_stats_t stats;
    nmo_save_perf_stats_reset(&stats);

    nmo_save_options_t opts = nmo_save_options_default();
    opts.collect_perf_stats = true;
    opts.perf_stats = &stats;

    ASSERT_EQ(NMO_OK, nmo_save_file(session, output_path, &opts));

    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_PRE_HOOKS].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_REMAP_PLAN].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_MANAGER_SERIALIZE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_OBJECT_SERIALIZE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_DATA_PLAN].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_DATA_WRITE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_HEADER1_PLAN].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_HEADER1_WRITE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_CRC].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_TXN_WRITE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_TXN_COMMIT].calls > 0);
    ASSERT_TRUE(stats.header1_unpacked_bytes > 0);
    ASSERT_TRUE(stats.data_unpacked_bytes > 0);
    ASSERT_TRUE(stats.header1_packed_bytes > 0);
    ASSERT_TRUE(stats.data_packed_bytes > 0);
    ASSERT_EQ(stats.data_unpacked_bytes, stats.planned_chunk_bytes);

    nmo_context_t *roundtrip_ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(roundtrip_ctx);

    nmo_session_t *roundtrip = nmo_session_create(roundtrip_ctx);
    ASSERT_NOT_NULL(roundtrip);
    ASSERT_EQ(NMO_OK, nmo_load_file(roundtrip, output_path, NULL));
    ASSERT_FALSE(nmo_session_is_partial_load(roundtrip));

    remove(output_path);
    nmo_session_destroy(roundtrip);
    nmo_context_release(roundtrip_ctx);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(phase_stats, save_fast_durability_records_transaction_phases) {
    const char *fixture = NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo");
    if (!file_exists(fixture)) {
        printf("[phase_stats] skip missing fixture: %s\n", fixture);
        return;
    }

    nmo_save_options_t defaults = nmo_save_options_default();
    ASSERT_EQ(NMO_SAVE_DURABILITY_DEFAULT, defaults.durability);

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    ASSERT_EQ(NMO_OK, nmo_load_file(session, fixture, NULL));

    char output_path[512];
    snprintf(output_path, sizeof(output_path),
             "test_phase_stats_fast_save_%d.cmo", (int)NMO_GETPID());
    remove(output_path);

    nmo_save_perf_stats_t stats;
    nmo_save_perf_stats_reset(&stats);

    nmo_save_options_t opts = nmo_save_options_default();
    opts.durability = NMO_SAVE_DURABILITY_FAST;
    opts.collect_perf_stats = true;
    opts.perf_stats = &stats;

    ASSERT_EQ(NMO_OK, nmo_save_file(session, output_path, &opts));
    ASSERT_TRUE(file_exists(output_path));
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_TXN_WRITE].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_SAVE_PERF_TXN_COMMIT].calls > 0);

    remove(output_path);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(phase_stats, metadata_profile_skips_full_load_phases) {
    const char *fixture = NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo");
    if (!file_exists(fixture)) {
        printf("[phase_stats] skip missing fixture: %s\n", fixture);
        return;
    }

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_load_perf_stats_t stats;
    nmo_load_perf_stats_reset(&stats);

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    opts.collect_perf_stats = true;
    opts.perf_stats = &stats;

    ASSERT_EQ(NMO_OK, nmo_load_file(session, fixture, &opts));

    ASSERT_TRUE(nmo_session_is_partial_load(session));
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_OPEN_DETECT].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_READ].calls > 0);
    ASSERT_TRUE(stats.phases[NMO_LOAD_PERF_HEADER1_PARSE].calls > 0);
    ASSERT_EQ(0, stats.phases[NMO_LOAD_PERF_DATA_READ].calls);
    ASSERT_EQ(0, stats.phases[NMO_LOAD_PERF_DATA_PARSE].calls);
    ASSERT_EQ(0, stats.phases[NMO_LOAD_PERF_OBJECT_CREATE].calls);
    ASSERT_EQ(0, stats.phases[NMO_LOAD_PERF_OBJECT_DESERIALIZE].calls);
    ASSERT_EQ(0, stats.phases[NMO_LOAD_PERF_REFERENCE_RESOLVE].calls);
    ASSERT_EQ(0, stats.phases[NMO_LOAD_PERF_INDEX_REBUILD].calls);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

typedef struct phase_stats_bench_options {
    const char *fixture;
    int iterations;
    bool save_copy;
    bool fast_save;
    bool json;
} phase_stats_bench_options_t;

static void phase_stats_usage(FILE *out) {
    fprintf(out,
            "Usage: test_load_save_phase_stats [--fixture <path>] [--iterations <n>] "
            "[--save-copy] [--fast-save] [--json]\n");
}

static int phase_stats_parse_benchmark_args(int argc,
                                            char **argv,
                                            phase_stats_bench_options_t *opts) {
    opts->fixture = NULL;
    opts->iterations = 1;
    opts->save_copy = false;
    opts->fast_save = false;
    opts->json = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fixture") == 0) {
            if (i + 1 >= argc) {
                return 0;
            }
            opts->fixture = argv[++i];
        } else if (strcmp(argv[i], "--iterations") == 0) {
            if (i + 1 >= argc) {
                return 0;
            }
            opts->iterations = atoi(argv[++i]);
            if (opts->iterations <= 0) {
                return 0;
            }
        } else if (strcmp(argv[i], "--save-copy") == 0) {
            opts->save_copy = true;
        } else if (strcmp(argv[i], "--fast-save") == 0) {
            opts->fast_save = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            opts->json = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            phase_stats_usage(stdout);
            exit(0);
        } else {
            return 0;
        }
    }

    if (opts->fixture == NULL) {
        return 0;
    }
    return 1;
}

static int phase_stats_run_benchmark(const phase_stats_bench_options_t *opts) {
    if (!file_exists(opts->fixture)) {
        fprintf(stderr, "fixture not found: %s\n", opts->fixture);
        return 1;
    }

    nmo_load_perf_stats_t load_sum;
    nmo_save_perf_stats_t save_sum;
    nmo_load_perf_stats_reset(&load_sum);
    nmo_save_perf_stats_reset(&save_sum);
    double load_wall_ms = 0.0;
    double save_wall_ms = 0.0;
    int roundtrip_ok = 1;

    for (int i = 0; i < opts->iterations; i++) {
        nmo_context_t *ctx = nmo_context_create(NULL);
        if (ctx == NULL) {
            fprintf(stderr, "failed to create context\n");
            return 1;
        }
        nmo_session_t *session = nmo_session_create(ctx);
        if (session == NULL) {
            nmo_context_release(ctx);
            fprintf(stderr, "failed to create session\n");
            return 1;
        }

        nmo_load_perf_stats_t load_stats;
        nmo_load_perf_stats_reset(&load_stats);
        nmo_load_options_t load_opts = nmo_load_options_default();
        load_opts.collect_perf_stats = true;
        load_opts.perf_stats = &load_stats;

        uint64_t load_start = nmo_perf_now_ticks();
        int load_status = nmo_load_file(session, opts->fixture, &load_opts);
        uint64_t load_end = nmo_perf_now_ticks();
        if (load_status != NMO_OK) {
            fprintf(stderr, "load failed: %s\n", nmo_error_string(load_status));
            nmo_session_destroy(session);
            nmo_context_release(ctx);
            return 1;
        }
        load_wall_ms += nmo_perf_elapsed_ms(load_start, load_end);
        phase_stats_add_load(&load_sum, &load_stats);

        if (opts->save_copy) {
            char output_path[512];
            phase_stats_make_output_path(output_path, sizeof(output_path),
                                         "test_phase_stats_bench_copy", i, opts->fixture);
            remove(output_path);

            nmo_save_perf_stats_t save_stats;
            nmo_save_perf_stats_reset(&save_stats);
            nmo_save_options_t save_opts = nmo_save_options_default();
            save_opts.collect_perf_stats = true;
            save_opts.perf_stats = &save_stats;
            if (opts->fast_save) {
                save_opts.durability = NMO_SAVE_DURABILITY_FAST;
            }

            uint64_t save_start = nmo_perf_now_ticks();
            int save_status = nmo_save_file(session, output_path, &save_opts);
            uint64_t save_end = nmo_perf_now_ticks();
            if (save_status != NMO_OK) {
                fprintf(stderr, "save failed: %s\n", nmo_error_string(save_status));
                nmo_session_destroy(session);
                nmo_context_release(ctx);
                remove(output_path);
                return 1;
            }
            save_wall_ms += nmo_perf_elapsed_ms(save_start, save_end);
            phase_stats_add_save(&save_sum, &save_stats);

            nmo_context_t *rt_ctx = nmo_context_create(NULL);
            nmo_session_t *rt_session = rt_ctx ? nmo_session_create(rt_ctx) : NULL;
            if (rt_ctx == NULL || rt_session == NULL ||
                nmo_load_file(rt_session, output_path, NULL) != NMO_OK) {
                roundtrip_ok = 0;
            }
            if (rt_session != NULL) {
                nmo_session_destroy(rt_session);
            }
            if (rt_ctx != NULL) {
                nmo_context_release(rt_ctx);
            }
            remove(output_path);
        }

        nmo_session_destroy(session);
        nmo_context_release(ctx);
    }

    if (!roundtrip_ok) {
        fprintf(stderr, "saved output failed to round-trip\n");
        return 1;
    }

    if (opts->json) {
        printf("{\"fixture\":");
        phase_stats_print_json_string(opts->fixture);
        printf(",\"iterations\":%d,\"save_copy\":%s,"
               "\"fast_save\":%s,\"durability\":\"%s\",\"roundtrip_ok\":%s,",
               opts->iterations,
               opts->save_copy ? "true" : "false",
               opts->fast_save ? "true" : "false",
               opts->fast_save ? "fast" : "default",
               roundtrip_ok ? "true" : "false");
        phase_stats_print_load_json(&load_sum, load_wall_ms);
        if (opts->save_copy) {
            printf(",");
            phase_stats_print_save_json(&save_sum, save_wall_ms);
        }
        printf("}\n");
    } else {
        printf("Fixture: %s\n", opts->fixture);
        printf("Iterations: %d\n", opts->iterations);
        printf("Save copy: %s\n", opts->save_copy ? "yes" : "no");
        printf("Durability: %s\n", opts->fast_save ? "fast" : "default");
        printf("Round-trip: %s\n", roundtrip_ok ? "ok" : "failed");
        phase_stats_print_load_text(&load_sum, load_wall_ms);
        if (opts->save_copy) {
            phase_stats_print_save_text(&save_sum, save_wall_ms);
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        phase_stats_bench_options_t opts;
        if (!phase_stats_parse_benchmark_args(argc, argv, &opts)) {
            phase_stats_usage(stderr);
            return 2;
        }
        return phase_stats_run_benchmark(&opts);
    }

    test_framework_init();
    REGISTER_TEST_CATEGORIZED(phase_stats, load_records_required_phases, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(phase_stats, save_records_required_phases, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(phase_stats, save_fast_durability_records_transaction_phases, TEST_CATEGORY_PERFORMANCE);
    REGISTER_TEST_CATEGORIZED(phase_stats, metadata_profile_skips_full_load_phases, TEST_CATEGORY_PERFORMANCE);
    return test_framework_run();
}
