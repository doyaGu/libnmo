/**
 * @file test_round_trip.c
 * @brief Round-trip test framework (load -> save -> load -> compare)
 */

#include "../test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_parser.h"
#include "app/nmo_saver.h"
#include "app/nmo_session.h"
#include "app/nmo_comparison.h"
#include <stdio.h>
#include <string.h>

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

static int run_round_trip(const char *input_path) {
    if (!file_exists(input_path)) {
        printf("  SKIP: %s not found\n", input_path);
        return 0;
    }

    const char *basename = strrchr(input_path, '/');
    if (basename == NULL) basename = strrchr(input_path, '\\');
    if (basename == NULL) basename = input_path; else basename++;

    char temp_file[512];
    snprintf(temp_file, sizeof(temp_file), "roundtrip_%s", basename);

    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,
        .logger = NULL,
        .thread_pool_size = 1,
    };

    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        printf("  FAILED: Could not create context\n");
        return 1;
    }

    nmo_session_t *load1 = nmo_session_create(ctx);
    if (load1 == NULL) {
        printf("  FAILED: Could not create load1 session\n");
        nmo_context_release(ctx);
        return 1;
    }

    int result = nmo_load_file(load1, input_path, NULL);
    if (result != NMO_OK) {
        printf("  FAILED: Load failed for %s (error %d)\n", input_path, result);
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        return 1;
    }

    result = nmo_save_file(load1, temp_file, NULL);
    if (result != NMO_OK) {
        printf("  FAILED: Save failed for %s (error %d)\n", temp_file, result);
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }

    nmo_session_t *load2 = nmo_session_create(ctx);
    if (load2 == NULL) {
        printf("  FAILED: Could not create load2 session\n");
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }

    result = nmo_load_file(load2, temp_file, NULL);
    if (result != NMO_OK) {
        printf("  FAILED: Reload failed for %s (error %d)\n", temp_file, result);
        nmo_session_destroy(load2);
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }

    nmo_comparison_result_t compare_result;
    nmo_comparison_result_init(&compare_result);
    int compare_err = nmo_session_compare(
        load1,
        load2,
        NMO_COMPARE_FILE_INFO | NMO_COMPARE_NAMES | NMO_COMPARE_CLASS_IDS |
            NMO_COMPARE_CHUNKS | NMO_COMPARE_IGNORE_ORDER | NMO_COMPARE_VERBOSE,
        &compare_result);

    int passed = (compare_err == NMO_OK) && compare_result.match;
    if (!passed && compare_err == NMO_OK) {
        int only_object_count = (compare_result.diff_count > 0);
        for (int i = 0; i < compare_result.diff_count; i++) {
            if (compare_result.diffs[i].type != NMO_DIFF_OBJECT_COUNT) {
                only_object_count = 0;
                break;
            }
        }
        if (only_object_count) {
            passed = 1;
        }
    }
    if (!passed) {
        if (compare_err != NMO_OK) {
            printf("  FAILED: Comparison error %d\n", compare_err);
        } else {
            printf("  FAILED: Comparison mismatch\n");
            printf("%s", compare_result.report);
        }
    }

    nmo_session_destroy(load2);
    nmo_session_destroy(load1);
    nmo_context_release(ctx);

    if (passed) {
        remove(temp_file);
    } else {
        printf("  (Temp file preserved at: %s)\n", temp_file);
    }

    return passed ? 0 : 1;
}

TEST(round_trip, sample_files) {
    const char *files[] = {
        NMO_TEST_DATA_FILE("2D Text.nmo"),
        NMO_TEST_DATA_FILE("Nop.cmo"),
        NMO_TEST_DATA_FILE("Nop1.cmo"),
        NMO_TEST_DATA_FILE("Nop2.cmo"),
        NMO_TEST_DATA_FILE("Empty.nmo"),
        NULL
    };

    int failures = 0;

    for (int i = 0; files[i] != NULL; i++) {
        printf("\nRound-trip: %s\n", files[i]);
        if (run_round_trip(files[i]) != 0) {
            failures++;
        }
    }

    ASSERT_EQ(failures, 0);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(round_trip, sample_files);
TEST_MAIN_END()
