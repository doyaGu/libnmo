#include "document/nmo_document_compare.h"

#include <string.h>

static void nmo_comparison_result_stats_clear(nmo_comparison_result_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
}

NMO_API nmo_status_t nmo_comparison_result_collect_stats(
    const nmo_comparison_result_t *result,
    nmo_comparison_result_stats_t *out_stats)
{
    if (result == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_comparison_result_stats_clear(out_stats);
    out_stats->match = result->match != 0;
    out_stats->objects_compared = result->objects_compared;
    out_stats->objects_matched = result->objects_matched;
    out_stats->managers_compared = result->managers_compared;
    out_stats->managers_matched = result->managers_matched;
    out_stats->diff_count = result->diff_count;
    out_stats->diff_overflow = result->diff_overflow != 0;

    for (int i = 0; i < result->diff_count; ++i) {
        switch (result->diffs[i].type) {
            case NMO_DIFF_OBJECT_COUNT:
                out_stats->object_count_diffs++;
                break;
            case NMO_DIFF_MANAGER_COUNT:
                out_stats->manager_count_diffs++;
                break;
            case NMO_DIFF_OBJECT_MISSING:
                out_stats->object_missing_diffs++;
                break;
            case NMO_DIFF_OBJECT_ORDER:
                out_stats->object_order_diffs++;
                break;
            case NMO_DIFF_OBJECT_ID:
                out_stats->object_id_diffs++;
                break;
            case NMO_DIFF_OBJECT_NAME:
                out_stats->object_name_diffs++;
                break;
            case NMO_DIFF_OBJECT_CLASS_ID:
                out_stats->object_class_id_diffs++;
                break;
            case NMO_DIFF_OBJECT_REFERENCE_FLAG:
                out_stats->object_reference_flag_diffs++;
                break;
            case NMO_DIFF_OBJECT_CHUNK_SIZE:
                out_stats->object_chunk_size_diffs++;
                break;
            case NMO_DIFF_OBJECT_CHUNK_DATA:
                out_stats->object_chunk_data_diffs++;
                break;
            case NMO_DIFF_MANAGER_MISSING:
                out_stats->manager_missing_diffs++;
                break;
            case NMO_DIFF_MANAGER_GUID:
                out_stats->manager_guid_diffs++;
                break;
            case NMO_DIFF_MANAGER_CHUNK_SIZE:
                out_stats->manager_chunk_size_diffs++;
                break;
            case NMO_DIFF_MANAGER_CHUNK_DATA:
                out_stats->manager_chunk_data_diffs++;
                break;
            case NMO_DIFF_FILE_VERSION:
                out_stats->file_version_diffs++;
                break;
            case NMO_DIFF_CK_VERSION:
                out_stats->ck_version_diffs++;
                break;
            case NMO_DIFF_SHADOW_DATA:
                out_stats->shadow_data_diffs++;
                break;
            case NMO_DIFF_NONE:
            default:
                break;
        }
    }

    return NMO_OK;
}
