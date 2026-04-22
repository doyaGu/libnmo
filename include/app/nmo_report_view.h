/**
 * @file nmo_report_view.h
 * @brief Stable snapshot views for app-level reporting details
 */

#ifndef NMO_REPORT_VIEW_H
#define NMO_REPORT_VIEW_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "app/nmo_report_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_session nmo_session_t;

#define NMO_REPORT_VIEW_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_REPORT_VIEW_API_TIER NMO_API_TIER_STABLE_CONSUMER

typedef struct nmo_object_summary_field_view {
    const char *name;
    const char *kind;
    const char *value_str;
    /* Resolved target name for scalar object-reference fields when session
     * context is available during snapshot construction. */
    const char *ref_name;
    const char **items;
    size_t item_count;
    size_t count;
    bool has_count;
} nmo_object_summary_field_view_t;

typedef struct nmo_object_summary_view {
    nmo_object_summary_stats_t stats;
    nmo_object_summary_field_view_t *fields;
    size_t field_count;
} nmo_object_summary_view_t;

typedef struct nmo_comparison_diff_view {
    uint32_t type_code;
    const char *type_name;
    nmo_object_id_t object_id;
    const char *context;
} nmo_comparison_diff_view_t;

typedef struct nmo_comparison_view {
    nmo_comparison_result_stats_t stats;
    nmo_comparison_diff_view_t *diffs;
    size_t diff_count;
} nmo_comparison_view_t;

typedef struct nmo_diff_field_view {
    const char *field_name;
    const char *before;
    const char *after;
} nmo_diff_field_view_t;

typedef struct nmo_diff_object_view {
    nmo_object_id_t before_id;
    nmo_object_id_t after_id;
    nmo_class_id_t before_class_id;
    nmo_class_id_t after_class_id;
    const char *before_name;
    const char *after_name;
    /* Stable object-location paths from the source/target sessions. These are
     * preserved for both changed and renamed entries. */
    const char *before_path;
    const char *after_path;
    float similarity;
    nmo_diff_field_view_t *field_diffs;
    size_t field_diff_count;
    size_t field_diff_total;
} nmo_diff_object_view_t;

typedef struct nmo_diff_identity_view {
    nmo_object_id_t id;
    nmo_class_id_t class_id;
    const char *name;
    const char *path;
} nmo_diff_identity_view_t;

typedef struct nmo_diff_view {
    nmo_diff_result_stats_t stats;
    nmo_diff_object_view_t *changed;
    size_t changed_count;
    nmo_diff_object_view_t *renamed;
    size_t renamed_count;
    nmo_diff_identity_view_t *removed;
    size_t removed_count;
    nmo_diff_identity_view_t *added;
    size_t added_count;
} nmo_diff_view_t;

/**
 * @brief Build a stable structured object-summary snapshot.
 *
 * @param ctx Type/format context used for summary rendering.
 * @param session Owning session for @p object. This is required to preserve
 *                resolved object-reference names in field snapshots.
 * @param object Object to summarize.
 * @param out_view Output snapshot. Destroy with nmo_object_summary_view_destroy().
 */
NMO_API nmo_status_t nmo_object_summary_build_view(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_object_t *object,
    nmo_object_summary_view_t *out_view);

NMO_API void nmo_object_summary_view_destroy(
    nmo_object_summary_view_t *view);

NMO_API nmo_status_t nmo_comparison_build_view(
    const nmo_session_t *session1,
    const nmo_session_t *session2,
    uint32_t flags,
    nmo_comparison_view_t *out_view);

NMO_API void nmo_comparison_view_destroy(
    nmo_comparison_view_t *view);

/**
 * @brief Build a stable structured diff snapshot between two sessions.
 *
 * Renamed entries preserve before/after object-location paths in the same way
 * as changed entries so consumers can reason about pure renames without
 * re-resolving object identity out-of-band.
 */
NMO_API nmo_status_t nmo_diff_build_view(
    nmo_session_t *session1,
    nmo_session_t *session2,
    nmo_diff_view_t *out_view);

NMO_API void nmo_diff_view_destroy(
    nmo_diff_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_REPORT_VIEW_H */
