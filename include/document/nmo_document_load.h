#ifndef NMO_DOCUMENT_LOAD_H
#define NMO_DOCUMENT_LOAD_H

#include "document/nmo_document.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include <stddef.h>

#define NMO_LOAD_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_LOAD_WORKFLOW_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif
typedef struct nmo_load_options nmo_load_options_t;
typedef struct nmo_plugin_dep nmo_plugin_dep_t;
typedef struct nmo_header nmo_header_t;
typedef struct nmo_manager_data nmo_manager_data_t;

typedef struct nmo_file_info {
    uint32_t file_version;
    uint32_t file_version2;
    uint32_t ck_version;
    uint32_t product_version;
    uint32_t product_build;
    size_t file_size;
    uint32_t object_count;
    uint32_t manager_count;
    uint32_t write_mode;
} nmo_file_info_t;

typedef struct nmo_file_state {
    nmo_file_info_t info;
    nmo_manager_data_t *manager_data;
    uint32_t manager_data_count;
    nmo_plugin_dep_t *plugin_deps;
    uint32_t plugin_dep_count;
} nmo_file_state_t;

typedef struct nmo_runtime_load_stats {
    size_t total_objects;
    uint32_t flags;
    struct {
        uint32_t total;
        uint32_t resolved;
        uint32_t unresolved;
        uint32_t ambiguous;
        uint32_t unresolved_preview_count;
        struct {
            nmo_object_id_t id;
            nmo_class_id_t class_id;
        } unresolved_preview[8];
    } references;
    struct {
        size_t class_entries;
        size_t name_entries;
        size_t guid_entries;
        size_t memory_usage;
    } indexes;
    struct {
        uint32_t invoked;
        uint32_t errors;
    } object_postload;
    uint32_t manager_errors;
} nmo_runtime_load_stats_t;

typedef struct nmo_session_plugin_dependency_status {
    nmo_guid_t guid;
    nmo_plugin_category_t category;
    uint32_t required_version;
    uint32_t resolved_version;
    const char *resolved_name;
    uint32_t status_flags;
} nmo_session_plugin_dependency_status_t;

#define NMO_SESSION_PLUGIN_DEP_STATUS_MISSING             0x00000001u
#define NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD     0x00000002u
#define NMO_SESSION_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE 0x00000004u

typedef struct nmo_session_plugin_diagnostics {
    const nmo_session_plugin_dependency_status_t *entries;
    size_t entry_count;
    size_t missing_count;
    size_t outdated_count;
    int extension_registry_available;
} nmo_session_plugin_diagnostics_t;

NMO_API nmo_status_t nmo_document_load_file(
    nmo_context_t *ctx,
    const char *path,
    const nmo_load_options_t *options,
    nmo_document_t **out_document);
NMO_API const nmo_file_state_t *nmo_document_get_file_state(
    const nmo_document_t *document);
NMO_API nmo_file_info_t nmo_document_get_file_info(const nmo_document_t *document);
NMO_API const nmo_header_t *nmo_document_get_header(const nmo_document_t *document);
NMO_API int nmo_document_is_partial_load(const nmo_document_t *document);
NMO_API int nmo_document_has_materialized_load_state(const nmo_document_t *document);
NMO_API nmo_status_t nmo_document_get_runtime_load_stats(
    const nmo_document_t *document,
    nmo_runtime_load_stats_t *out_stats);
NMO_API const nmo_session_plugin_diagnostics_t *nmo_document_get_plugin_diagnostics(
    const nmo_document_t *document);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_LOAD_H */
