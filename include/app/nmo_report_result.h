/**
 * @file nmo_report_result.h
 * @brief Structured result helpers for app-level reporting APIs
 */

#ifndef NMO_REPORT_RESULT_H
#define NMO_REPORT_RESULT_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_comparison_result nmo_comparison_result_t;
typedef struct nmo_diff_result nmo_diff_result_t;

/*
 * Stable structured results for reporting surfaces. These helpers are intended
 * for binding-facing consumers that need semantic result data without
 * inheriting FILE* or JSON writer contracts.
 */
#define NMO_REPORT_RESULT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_REPORT_RESULT_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef struct nmo_object_summary_stats {
    nmo_class_id_t class_id;
    const char *class_name;
    nmo_guid_t type_guid;
    const char *type_name;
    bool has_reflection;
    size_t total_fields;
    size_t array_fields;
    size_t reference_fields;
    size_t optional_fields;
    size_t object_ref_fields;
} nmo_object_summary_stats_t;

typedef struct nmo_comparison_result_stats {
    bool match;
    uint32_t objects_compared;
    uint32_t objects_matched;
    uint32_t managers_compared;
    uint32_t managers_matched;
    int diff_count;
    bool diff_overflow;
    uint32_t object_count_diffs;
    uint32_t manager_count_diffs;
    uint32_t object_missing_diffs;
    uint32_t object_order_diffs;
    uint32_t object_id_diffs;
    uint32_t object_name_diffs;
    uint32_t object_class_id_diffs;
    uint32_t object_reference_flag_diffs;
    uint32_t object_chunk_size_diffs;
    uint32_t object_chunk_data_diffs;
    uint32_t manager_missing_diffs;
    uint32_t manager_guid_diffs;
    uint32_t manager_chunk_size_diffs;
    uint32_t manager_chunk_data_diffs;
    uint32_t file_version_diffs;
    uint32_t ck_version_diffs;
    uint32_t shadow_data_diffs;
} nmo_comparison_result_stats_t;

typedef struct nmo_diff_result_stats {
    size_t changed_count;
    size_t renamed_count;
    size_t removed_count;
    size_t added_count;
    size_t identical_count;
    size_t total_objects1;
    size_t total_objects2;
    size_t reported_field_diffs;
    size_t total_field_diffs;
} nmo_diff_result_stats_t;

NMO_API nmo_status_t nmo_object_summary_collect_stats(
    nmo_context_t *ctx,
    const nmo_object_t *object,
    nmo_object_summary_stats_t *out_stats);

NMO_API nmo_status_t nmo_comparison_result_collect_stats(
    const nmo_comparison_result_t *result,
    nmo_comparison_result_stats_t *out_stats);

NMO_API nmo_status_t nmo_diff_result_collect_stats(
    const nmo_diff_result_t *result,
    nmo_diff_result_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPORT_RESULT_H */
