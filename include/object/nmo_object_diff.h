#ifndef NMO_OBJECT_DIFF_OWNER_H
#define NMO_OBJECT_DIFF_OWNER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>

#define NMO_OBJECT_DIFF_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_DIFF_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_context nmo_context_t;
typedef struct nmo_document nmo_document_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;

typedef struct nmo_diff_config {
    uint32_t max_objects;
    uint32_t max_fields;
    float    min_similarity;
    float    rename_similarity;
    uint32_t flags;
} nmo_diff_config_t;

static inline nmo_diff_config_t nmo_diff_config_default(void) {
    nmo_diff_config_t c;
    c.max_objects = 0;
    c.max_fields = 0;
    c.min_similarity = 0.0f;
    c.rename_similarity = 0.85f;
    c.flags = 0;
    return c;
}

#define NMO_DIFF_VALUE_MAX 256

typedef struct nmo_field_diff {
    const char *field_name;
    char before[NMO_DIFF_VALUE_MAX];
    char after[NMO_DIFF_VALUE_MAX];
} nmo_field_diff_t;

typedef struct nmo_object_diff {
    const nmo_object_t *obj1;
    const nmo_object_t *obj2;
    nmo_field_diff_t *field_diffs;
    size_t field_diff_count;
    size_t field_diff_total;
    float similarity;
} nmo_object_diff_t;

typedef struct nmo_rename_diff {
    const nmo_object_t *obj1;
    const nmo_object_t *obj2;
    const char *before_name;
    const char *after_name;
    float similarity;
} nmo_rename_diff_t;

typedef struct nmo_diff_result {
    nmo_object_diff_t *changed;
    size_t changed_count;
    nmo_rename_diff_t *renamed;
    size_t renamed_count;
    const nmo_object_t **removed;
    size_t removed_count;
    const nmo_object_t **added;
    size_t added_count;
    size_t identical_count;
    size_t total_objects1;
    size_t total_objects2;
    nmo_arena_t *arena_;
} nmo_diff_result_t;

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

NMO_API nmo_status_t nmo_diff_objects(
    const nmo_document_t *document1,
    const nmo_document_t *document2,
    const nmo_diff_config_t *config,
    nmo_diff_result_t *result);

NMO_API void nmo_diff_result_destroy(nmo_diff_result_t *result);

NMO_API void nmo_object_format_path(
    char *buf, size_t buf_size,
    nmo_context_t *ctx,
    const nmo_object_t *obj);

NMO_API void nmo_object_format_ref(
    char *buf, size_t buf_size,
    nmo_object_id_t id,
    const nmo_object_repository_t *repo,
    nmo_context_t *ctx);

NMO_API bool nmo_object_ref_equal(
    nmo_object_id_t id1, nmo_object_id_t id2,
    const nmo_object_repository_t *repo1,
    const nmo_object_repository_t *repo2);

NMO_API float nmo_object_similarity(
    const nmo_object_t *obj1, const nmo_object_t *obj2,
    const nmo_type_registry_t *reg1, const nmo_type_registry_t *reg2,
    const nmo_object_repository_t *repo1, const nmo_object_repository_t *repo2);

NMO_API nmo_status_t nmo_diff_result_collect_stats(
    const nmo_diff_result_t *result,
    nmo_diff_result_stats_t *out_stats);

NMO_API nmo_status_t nmo_diff_build_view(
    const nmo_document_t *document1,
    const nmo_document_t *document2,
    nmo_diff_view_t *out_view);

NMO_API void nmo_diff_view_destroy(
    nmo_diff_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_DIFF_OWNER_H */
