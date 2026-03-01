/**
 * @file test_real_file_roundtrip.c
 * @brief Test loading a real NMO/CMO file, saving it, and loading it again
 */

#include "nmo.h"
#include "app/nmo_parser.h"
#include "app/nmo_comparison.h"
#include "test_framework.h"  // For NMO_TEST_DATA_FILE macro
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define TEMP_FILE "test_roundtrip_temp.cmo"
#else
#define TEMP_FILE "/tmp/test_roundtrip_temp.cmo"
#endif

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

static int collect_finish_loading_stats(const nmo_session_t *session,
                                        const char *path,
                                        nmo_finish_loading_stats_t *out_stats) {
    if (session == NULL || out_stats == NULL) {
        return 1;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    int stats_result = nmo_session_get_finish_loading_stats(session, out_stats);
    if (stats_result != NMO_OK) {
        printf("  FAILED: finish_loading stats unavailable for %s (error %d)\n",
               path, stats_result);
        return 1;
    }

    return 0;
}

static int validate_finish_loading_no_regression(
    const nmo_finish_loading_stats_t *baseline,
    const nmo_finish_loading_stats_t *current,
    const char *baseline_path,
    const char *current_path) {
    if (baseline == NULL || current == NULL) {
        return 1;
    }

    int failed = 0;
    if (current->references.unresolved > baseline->references.unresolved) {
        failed = 1;
    }
    if (current->object_postload.errors > baseline->object_postload.errors) {
        failed = 1;
    }
    if (current->manager_errors > baseline->manager_errors) {
        failed = 1;
    }

    if (failed) {
        printf("  FAILED: finish_loading regression after round-trip\n");
        printf("    baseline=%s\n", baseline_path);
        printf("      unresolved_refs=%u object_errors=%u manager_errors=%u\n",
               baseline->references.unresolved,
               baseline->object_postload.errors,
               baseline->manager_errors);
        printf("    current=%s\n", current_path);
        printf("      unresolved_refs=%u object_errors=%u manager_errors=%u\n",
               current->references.unresolved,
               current->object_postload.errors,
               current->manager_errors);
        return 1;
    }

    return 0;
}

/**
 * Test round-trip for a real file
 */
static int test_file_roundtrip(const char* input_file) {
    printf("Testing round-trip for: %s\n", input_file);

    if (!file_exists(input_file)) {
        printf("  SKIPPED: File not found\n");
        return 0;
    }

    /* Generate unique temp file name based on input filename */
    static char temp_file[512];
    const char *basename = strrchr(input_file, '/');
    if (basename == NULL) basename = strrchr(input_file, '\\');
    if (basename == NULL) basename = input_file; else basename++;
    
    snprintf(temp_file, sizeof(temp_file), "test_roundtrip_%s", basename);
    printf("  Using temp file: %s\n", temp_file);

    /* Create context */
    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,
        .logger = NULL,  /* Use default logger */
        .thread_pool_size = 1,
    };

    nmo_context_t* ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        printf("  FAILED: Could not create context\n");
        return 1;
    }

    /* === FIRST LOAD: Load original file === */
    nmo_session_t* load1_session = nmo_session_create(ctx);
    if (load1_session == NULL) {
        printf("  FAILED: Could not create load1 session\n");
        nmo_context_release(ctx);
        return 1;
    }

    int result = nmo_load_file(load1_session, input_file, NULL);
    if (result != NMO_OK) {
        printf("  FAILED: Could not load original file (error %d)\n", result);
        nmo_session_destroy(load1_session);
        nmo_context_release(ctx);
        return 1;
    }
    nmo_finish_loading_stats_t baseline_stats;
    if (collect_finish_loading_stats(load1_session, input_file, &baseline_stats) != 0) {
        nmo_session_destroy(load1_session);
        nmo_context_release(ctx);
        return 1;
    }

    /* === SAVE: Save to temporary file (schema required) === */
    nmo_save_options_t save_opts = nmo_save_options_default();
    save_opts.flags |= NMO_SAVE_REQUIRE_SCHEMA;
    result = nmo_save_file(load1_session, temp_file, &save_opts);
    if (result != NMO_OK) {
        printf("  FAILED: Could not save file (error %d)\n", result);
        nmo_session_destroy(load1_session);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }
    printf("  Saved to temporary file: %s\n", temp_file);

    /* DO NOT clean up first session yet - we need it for comparison */
    /* nmo_session_destroy(load1_session); */

    /* === SECOND LOAD: Load saved file === */
    nmo_session_t* load2_session = nmo_session_create(ctx);
    if (load2_session == NULL) {
        printf("  FAILED: Could not create load2 session\n");
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }

    result = nmo_load_file(load2_session, temp_file, NULL);
    if (result != NMO_OK) {
        printf("  FAILED: Could not load saved file (error %d)\n", result);
        nmo_session_destroy(load2_session);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }
    nmo_finish_loading_stats_t roundtrip_stats;
    if (collect_finish_loading_stats(load2_session, temp_file, &roundtrip_stats) != 0) {
        nmo_session_destroy(load2_session);
        nmo_session_destroy(load1_session);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }
    if (validate_finish_loading_no_regression(
            &baseline_stats, &roundtrip_stats, input_file, temp_file) != 0) {
        nmo_session_destroy(load2_session);
        nmo_session_destroy(load1_session);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }

    /* === VERIFICATION === */
    nmo_comparison_result_t compare_result;
    nmo_comparison_result_init(&compare_result);
    int compare_err = nmo_session_compare(
        load1_session,
        load2_session,
        NMO_COMPARE_STRICT | NMO_COMPARE_VERBOSE,
        &compare_result);

    int passed = (compare_err == NMO_OK) && compare_result.match;

    if (!passed) {
        if (compare_err != NMO_OK) {
            printf("  FAILED: Comparison error %d\n", compare_err);
        } else {
            printf("  FAILED: Comparison mismatch\n");
            printf("%s", compare_result.report);
        }
    }

    /* Clean up */
    nmo_session_destroy(load2_session);
    nmo_session_destroy(load1_session); /* Now clean up load1_session */
    nmo_context_release(ctx);
    if (passed) {
        remove(temp_file);
    } else {
        printf("  (Temp file preserved at: %s)\n", temp_file);
    }

    if (passed) {
        printf("  PASSED: Round-trip successful, data matches\n");
    }

    return passed ? 0 : 1;
}

int main(void) {
    int failed = 0;

    printf("=== Real File Round-Trip Tests ===\n\n");

    /* Test with 2D Text.nmo */
    printf("Test 1/3: 2D Text.nmo\n");
    if (test_file_roundtrip(NMO_TEST_DATA_FILE("2D Text.nmo")) != 0) {
        failed++;
    }
    printf("\n");

    /* Test with Nop.cmo */
    printf("Test 2/3: Nop.cmo\n");
    if (test_file_roundtrip(NMO_TEST_DATA_FILE("Nop.cmo")) != 0) {
        failed++;
    }
    printf("\n");

    /* Test with base.cmo */
    printf("Test 3/3: base.cmo\n");
    if (test_file_roundtrip(NMO_TEST_DATA_FILE("base.cmo")) != 0) {
        failed++;
    }
    printf("\n");

    /* base.cmo is included for strict reference-only round-trip validation. */

    printf("=== Summary ===\n");
    if (failed == 0) {
        printf("All round-trip tests PASSED!\n");
        printf("Schema-based serialization verified with %d test files.\n", 3);
        return 0;
    } else {
        printf("%d test(s) FAILED!\n", failed);
        return 1;
    }
}
