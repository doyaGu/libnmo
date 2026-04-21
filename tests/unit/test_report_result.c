#include "test_framework.h"

#include "app/nmo_comparison.h"
#include "app/nmo_object_diff.h"
#include "app/nmo_report_result.h"

#include <string.h>

TEST(report_result, comparison_stats_classify_diffs_without_report_buffer) {
    nmo_comparison_result_t result;
    nmo_comparison_result_stats_t stats;

    nmo_comparison_result_init(&result);
    result.objects_compared = 4;
    result.objects_matched = 2;
    result.managers_compared = 3;
    result.managers_matched = 1;
    result.diff_overflow = 1;

    nmo_comparison_add_diff(&result, NMO_DIFF_FILE_VERSION, 0, "file version mismatch");
    nmo_comparison_add_diff(&result, NMO_DIFF_OBJECT_NAME, 7, "name mismatch");
    nmo_comparison_add_diff(&result, NMO_DIFF_OBJECT_NAME, 8, "name mismatch");
    nmo_comparison_add_diff(&result, NMO_DIFF_MANAGER_CHUNK_DATA, 0, "manager chunk mismatch");
    nmo_comparison_add_diff(&result, NMO_DIFF_SHADOW_DATA, 0, "shadow mismatch");

    ASSERT_EQ(NMO_OK, nmo_comparison_result_collect_stats(&result, &stats));
    ASSERT_FALSE(stats.match);
    ASSERT_EQ(4u, stats.objects_compared);
    ASSERT_EQ(2u, stats.objects_matched);
    ASSERT_EQ(3u, stats.managers_compared);
    ASSERT_EQ(1u, stats.managers_matched);
    ASSERT_EQ(5, stats.diff_count);
    ASSERT_TRUE(stats.diff_overflow);
    ASSERT_EQ(1u, stats.file_version_diffs);
    ASSERT_EQ(2u, stats.object_name_diffs);
    ASSERT_EQ(1u, stats.manager_chunk_data_diffs);
    ASSERT_EQ(1u, stats.shadow_data_diffs);
    ASSERT_EQ(0u, stats.object_chunk_data_diffs);
}

TEST(report_result, diff_stats_summarize_structured_diff_counts) {
    nmo_diff_result_t result;
    nmo_diff_result_stats_t stats;
    nmo_object_diff_t changed[2];

    memset(&result, 0, sizeof(result));
    memset(changed, 0, sizeof(changed));

    changed[0].field_diff_count = 1;
    changed[0].field_diff_total = 3;
    changed[1].field_diff_count = 2;
    changed[1].field_diff_total = 5;

    result.changed = changed;
    result.changed_count = 2;
    result.renamed_count = 1;
    result.removed_count = 3;
    result.added_count = 4;
    result.identical_count = 5;
    result.total_objects1 = 10;
    result.total_objects2 = 12;

    ASSERT_EQ(NMO_OK, nmo_diff_result_collect_stats(&result, &stats));
    ASSERT_EQ(2u, stats.changed_count);
    ASSERT_EQ(1u, stats.renamed_count);
    ASSERT_EQ(3u, stats.removed_count);
    ASSERT_EQ(4u, stats.added_count);
    ASSERT_EQ(5u, stats.identical_count);
    ASSERT_EQ(10u, stats.total_objects1);
    ASSERT_EQ(12u, stats.total_objects2);
    ASSERT_EQ(3u, stats.reported_field_diffs);
    ASSERT_EQ(8u, stats.total_field_diffs);
}

TEST(report_result, collect_stats_validate_arguments) {
    nmo_comparison_result_t comparison;
    nmo_comparison_result_stats_t comparison_stats;
    nmo_diff_result_t diff;
    nmo_diff_result_stats_t diff_stats;

    nmo_comparison_result_init(&comparison);
    memset(&diff, 0, sizeof(diff));

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_comparison_result_collect_stats(NULL, &comparison_stats));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_comparison_result_collect_stats(&comparison, NULL));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_diff_result_collect_stats(NULL, &diff_stats));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_diff_result_collect_stats(&diff, NULL));
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(report_result, comparison_stats_classify_diffs_without_report_buffer);
    REGISTER_TEST(report_result, diff_stats_summarize_structured_diff_counts);
    REGISTER_TEST(report_result, collect_stats_validate_arguments);
TEST_MAIN_END()
