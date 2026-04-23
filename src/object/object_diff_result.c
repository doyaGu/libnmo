#include "object/nmo_object_diff.h"

#include <string.h>

static void nmo_diff_result_stats_clear(nmo_diff_result_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
}

NMO_API nmo_status_t nmo_diff_result_collect_stats(
    const nmo_diff_result_t *result,
    nmo_diff_result_stats_t *out_stats)
{
    if (result == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_diff_result_stats_clear(out_stats);
    out_stats->changed_count = result->changed_count;
    out_stats->renamed_count = result->renamed_count;
    out_stats->removed_count = result->removed_count;
    out_stats->added_count = result->added_count;
    out_stats->identical_count = result->identical_count;
    out_stats->total_objects1 = result->total_objects1;
    out_stats->total_objects2 = result->total_objects2;

    for (size_t i = 0; i < result->changed_count; ++i) {
        out_stats->reported_field_diffs += result->changed[i].field_diff_count;
        out_stats->total_field_diffs += result->changed[i].field_diff_total;
    }

    return NMO_OK;
}
