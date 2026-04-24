#ifndef NMO_OBJECT_SUMMARY_OWNER_H
#define NMO_OBJECT_SUMMARY_OWNER_H

#include "runtime/nmo_context.h"
#include "core/nmo_guid.h"
#include "format/nmo_object.h"
#include "nmo_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define NMO_OBJECT_SUMMARY_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_OBJECT_SUMMARY_RENDERING_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_session nmo_session_t;
typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;

typedef struct nmo_summary_output {
    FILE *stream;
    yyjson_mut_doc *json_doc;
    yyjson_mut_val *json_data;
    bool is_json;
    bool colorize;
    nmo_context_t *ctx;
    nmo_session_t *session;
} nmo_summary_output_t;

typedef struct nmo_summary_config {
    uint32_t array_preview_max;
    uint32_t text_preview_max;
    uint32_t max_depth;
    bool show_field_metadata;
    bool resolve_object_refs;
    bool format_enum_names;
    bool format_flags_names;
} nmo_summary_config_t;

NMO_API nmo_summary_config_t nmo_summary_config_default(void);
NMO_API void nmo_summary_init(void);
NMO_API bool nmo_object_summary(nmo_object_t *obj, nmo_summary_output_t *out);
NMO_API bool nmo_object_summary_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config);
NMO_API bool nmo_object_summary_select(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const char *const *paths,
    size_t path_count);
NMO_API bool nmo_object_summary_select_with_config(
    nmo_object_t *obj,
    nmo_summary_output_t *out,
    const nmo_summary_config_t *config,
    const char *const *paths,
    size_t path_count);
NMO_API bool nmo_summary_has_reflection(nmo_context_t *ctx, nmo_class_id_t class_id);
NMO_API void nmo_summary_add_section(nmo_summary_output_t *out, const char *title);
NMO_API void nmo_summary_add_string(nmo_summary_output_t *out, const char *key,
                                    const char *value, int label_width);
NMO_API void nmo_summary_add_int(nmo_summary_output_t *out, const char *key,
                                 int64_t value, int label_width);
NMO_API void nmo_summary_add_uint(nmo_summary_output_t *out, const char *key,
                                  uint64_t value, int label_width);
NMO_API void nmo_summary_add_float(nmo_summary_output_t *out, const char *key,
                                   double value, int label_width);
NMO_API void nmo_summary_add_bool(nmo_summary_output_t *out, const char *key,
                                  bool value, int label_width);
NMO_API void nmo_summary_add_object_ref(nmo_summary_output_t *out, const char *key,
                                        nmo_object_id_t id, const char *name, int label_width);
NMO_API void nmo_summary_add_hex(nmo_summary_output_t *out, const char *key,
                                 uint32_t value, int label_width);
NMO_API void nmo_summary_add_vector3(nmo_summary_output_t *out, const char *key,
                                     float x, float y, float z, int label_width);
NMO_API void nmo_summary_add_color(nmo_summary_output_t *out, const char *key,
                                   uint32_t argb, int label_width);
NMO_API void nmo_summary_add_guid(nmo_summary_output_t *out, const char *key,
                                  nmo_guid_t guid, int label_width);

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

typedef struct nmo_object_summary_field_view {
    const char *name;
    const char *kind;
    const char *value_str;
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

NMO_API nmo_status_t nmo_object_summary_collect_stats(
    nmo_context_t *ctx,
    const nmo_object_t *object,
    nmo_object_summary_stats_t *out_stats);

NMO_API nmo_status_t nmo_object_summary_build_view(
    nmo_context_t *ctx,
    nmo_session_t *session,
    const nmo_object_t *object,
    nmo_object_summary_view_t *out_view);

NMO_API void nmo_object_summary_view_destroy(
    nmo_object_summary_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* NMO_OBJECT_SUMMARY_OWNER_H */
