#ifndef NMO_DOCUMENT_COMPARE_H
#define NMO_DOCUMENT_COMPARE_H

#include "document/nmo_document.h"
#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NMO_COMPARISON_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_COMPARISON_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_compare_flags {
    NMO_COMPARE_DEFAULT         = 0,
    NMO_COMPARE_STRUCTURE       = 0x0001,
    NMO_COMPARE_IDS             = 0x0002,
    NMO_COMPARE_NAMES           = 0x0004,
    NMO_COMPARE_CLASS_IDS       = 0x0008,
    NMO_COMPARE_CHUNKS          = 0x0010,
    NMO_COMPARE_SHADOW          = 0x0020,
    NMO_COMPARE_MANAGERS        = 0x0040,
    NMO_COMPARE_FILE_INFO       = 0x0080,
    NMO_COMPARE_STRICT          = 0x00FF,
    NMO_COMPARE_IGNORE_ORDER    = 0x0100,
    NMO_COMPARE_VERBOSE         = 0x0200,
} nmo_compare_flags_t;

typedef enum nmo_diff_type {
    NMO_DIFF_NONE = 0,
    NMO_DIFF_OBJECT_COUNT,
    NMO_DIFF_MANAGER_COUNT,
    NMO_DIFF_OBJECT_MISSING,
    NMO_DIFF_OBJECT_ORDER,
    NMO_DIFF_OBJECT_ID,
    NMO_DIFF_OBJECT_NAME,
    NMO_DIFF_OBJECT_CLASS_ID,
    NMO_DIFF_OBJECT_REFERENCE_FLAG,
    NMO_DIFF_OBJECT_CHUNK_SIZE,
    NMO_DIFF_OBJECT_CHUNK_DATA,
    NMO_DIFF_MANAGER_MISSING,
    NMO_DIFF_MANAGER_GUID,
    NMO_DIFF_MANAGER_CHUNK_SIZE,
    NMO_DIFF_MANAGER_CHUNK_DATA,
    NMO_DIFF_FILE_VERSION,
    NMO_DIFF_CK_VERSION,
    NMO_DIFF_SHADOW_DATA,
} nmo_diff_type_t;

#define NMO_MAX_DIFFS 64
#define NMO_DIFF_CONTEXT_MAX 512

typedef struct nmo_diff_entry {
    nmo_diff_type_t type;
    uint32_t object_id;
    char context[NMO_DIFF_CONTEXT_MAX];
    union {
        struct {
            uint32_t expected;
            uint32_t actual;
        } count;
        struct {
            size_t expected_size;
            size_t actual_size;
        } size;
    } data;
} nmo_diff_entry_t;

typedef struct nmo_comparison_result {
    int match;
    uint32_t objects_compared;
    uint32_t objects_matched;
    uint32_t managers_compared;
    uint32_t managers_matched;
    int diff_count;
    nmo_diff_entry_t diffs[NMO_MAX_DIFFS];
    int diff_overflow;
    char report[4096];
} nmo_comparison_result_t;

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

NMO_API void nmo_comparison_result_init(nmo_comparison_result_t *result);

NMO_API nmo_status_t nmo_document_compare(const nmo_document_t *document1,
                                          const nmo_document_t *document2,
                                          nmo_compare_flags_t flags,
                                          nmo_comparison_result_t *result);

NMO_API int nmo_document_compare_file_info(const nmo_document_t *document1,
                                           const nmo_document_t *document2,
                                           nmo_comparison_result_t *result);

NMO_API int nmo_document_compare_objects(const nmo_document_t *document1,
                                         const nmo_document_t *document2,
                                         nmo_compare_flags_t flags,
                                         nmo_comparison_result_t *result);

NMO_API void nmo_comparison_result_format_report(nmo_comparison_result_t *result);

NMO_API void nmo_comparison_add_diff(nmo_comparison_result_t *result,
                                     nmo_diff_type_t type,
                                     uint32_t object_id,
                                     const char *context);

NMO_API nmo_status_t nmo_comparison_result_collect_stats(
    const nmo_comparison_result_t *result,
    nmo_comparison_result_stats_t *out_stats);

NMO_API nmo_status_t nmo_comparison_build_view(
    const nmo_document_t *document1,
    const nmo_document_t *document2,
    uint32_t flags,
    nmo_comparison_view_t *out_view);

NMO_API void nmo_comparison_view_destroy(
    nmo_comparison_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_COMPARE_H */
