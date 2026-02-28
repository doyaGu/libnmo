/**
 * @file test_behavior_roundtrip_regression.c
 * @brief Regression tests for CKBehavior round-trip stability on historical samples
 */

#include "test_framework.h"
#include "nmo.h"
#include "app/nmo_parser.h"
#include "app/nmo_comparison.h"
#include "session/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include <stdio.h>
#include <string.h>

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

static uint32_t find_object_class_id(const nmo_session_t *session, uint32_t object_id) {
    if (session == NULL || object_id == 0) {
        return 0;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (repo == NULL) {
        return 0;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (obj == NULL) {
        return 0;
    }

    return obj->class_id;
}

static int has_behavior_chunk_diff(const nmo_session_t *session1,
                                   const nmo_session_t *session2,
                                   const nmo_comparison_result_t *result) {
    if (result == NULL) {
        return 0;
    }

    for (int i = 0; i < result->diff_count; ++i) {
        const nmo_diff_entry_t *diff = &result->diffs[i];
        if (diff->type != NMO_DIFF_OBJECT_CHUNK_SIZE &&
            diff->type != NMO_DIFF_OBJECT_CHUNK_DATA) {
            continue;
        }

        uint32_t class_id = find_object_class_id(session1, diff->object_id);
        if (class_id == 0) {
            class_id = find_object_class_id(session2, diff->object_id);
        }

        if (class_id == NMO_CID_BEHAVIOR) {
            return 1;
        }
    }

    return 0;
}

static int run_behavior_roundtrip(const char *input_file) {
    if (!file_exists(input_file)) {
        printf("SKIP (missing): %s\n", input_file);
        return 0;
    }

    const char *basename = strrchr(input_file, '/');
    if (basename == NULL) {
        basename = strrchr(input_file, '\\');
    }
    if (basename == NULL) {
        basename = input_file;
    } else {
        basename++;
    }

    char temp_file[512];
    snprintf(temp_file, sizeof(temp_file), "behavior_regression_%s", basename);
    remove(temp_file);

    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,
        .logger = NULL,
        .thread_pool_size = 1,
    };

    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        printf("FAILED: context creation (%s)\n", input_file);
        return 1;
    }

    nmo_session_t *load1 = nmo_session_create(ctx);
    nmo_session_t *load2 = NULL;
    int failed = 0;

    if (load1 == NULL) {
        printf("FAILED: session creation (%s)\n", input_file);
        nmo_context_release(ctx);
        return 1;
    }

    if (nmo_load_file(load1, input_file, NULL) != NMO_OK) {
        printf("FAILED: first load (%s)\n", input_file);
        failed = 1;
        goto cleanup;
    }

    nmo_save_options_t save_opts = nmo_save_options_default();
    save_opts.flags |= NMO_SAVE_REQUIRE_SCHEMA;
    if (nmo_save_file(load1, temp_file, &save_opts) != NMO_OK) {
        printf("FAILED: save (%s)\n", temp_file);
        failed = 1;
        goto cleanup;
    }

    load2 = nmo_session_create(ctx);
    if (load2 == NULL) {
        printf("FAILED: second session creation (%s)\n", input_file);
        failed = 1;
        goto cleanup;
    }

    if (nmo_load_file(load2, temp_file, NULL) != NMO_OK) {
        printf("FAILED: second load (%s)\n", temp_file);
        failed = 1;
        goto cleanup;
    }

    nmo_comparison_result_t cmp;
    nmo_comparison_result_init(&cmp);
    int compare_err = nmo_session_compare(
        load1,
        load2,
        NMO_COMPARE_FILE_INFO | NMO_COMPARE_NAMES | NMO_COMPARE_CLASS_IDS |
            NMO_COMPARE_CHUNKS | NMO_COMPARE_IGNORE_ORDER | NMO_COMPARE_VERBOSE,
        &cmp);

    if (compare_err != NMO_OK) {
        printf("FAILED: compare error %d (%s)\n", compare_err, input_file);
        failed = 1;
        goto cleanup;
    }

    const int behavior_chunk_changed = has_behavior_chunk_diff(load1, load2, &cmp);

    if (!cmp.match || behavior_chunk_changed) {
        printf("FAILED: behavior regression mismatch (%s)\n", input_file);
        printf("%s", cmp.report);
        failed = 1;
        goto cleanup;
    }

cleanup:
    if (load2 != NULL) {
        nmo_session_destroy(load2);
    }
    nmo_session_destroy(load1);
    nmo_context_release(ctx);
    if (!failed) {
        remove(temp_file);
    } else {
        printf("Preserved temp file: %s\n", temp_file);
    }
    return failed;
}

TEST(behavior_roundtrip_regression, nop_behavior_samples) {
    const char *samples[] = {
        NMO_TEST_DATA_FILE("Nop1.cmo"),
        NMO_TEST_DATA_FILE("Nop2.cmo"),
        NULL
    };

    int failures = 0;
    for (int i = 0; samples[i] != NULL; ++i) {
        printf("Behavior regression sample: %s\n", samples[i]);
        failures += run_behavior_roundtrip(samples[i]);
    }

    ASSERT_EQ(failures, 0);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(behavior_roundtrip_regression, nop_behavior_samples);
TEST_MAIN_END()
