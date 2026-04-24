#ifndef NMO_DOCUMENT_FILE_STATE_H
#define NMO_DOCUMENT_FILE_STATE_H

#include "document/nmo_document_load.h"
#include "document/nmo_document.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NMO_DOCUMENT_FILE_STATE_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_DOCUMENT_FILE_STATE_API_TIER NMO_API_TIER_ADVANCED_C

typedef struct nmo_included_file {
    const char *name;
    const void *data;
    uint32_t size;
    nmo_arena_array_t owner_ids;
    uint32_t attributes;
} nmo_included_file_t;

#define NMO_INCLUDED_FILE_ATTR_BORROWED      0x00000001u
#define NMO_INCLUDED_FILE_ATTR_METADATA_ONLY 0x00000002u

typedef struct nmo_included_file_metadata {
    const nmo_object_id_t *owner_ids;
    uint32_t owner_count;
    uint32_t attributes;
} nmo_included_file_metadata_t;

NMO_API nmo_included_file_t *nmo_document_get_included_files(
    const nmo_document_t *document,
    uint32_t *out_count);

NMO_API nmo_status_t nmo_document_set_file_info(
    nmo_document_t *document,
    const nmo_file_info_t *info);

NMO_API void nmo_document_set_manager_data(
    nmo_document_t *document,
    nmo_manager_data_t *data,
    uint32_t count);

NMO_API nmo_status_t nmo_document_set_plugin_dependencies(
    nmo_document_t *document,
    nmo_plugin_dep_t *deps,
    uint32_t count);

NMO_API nmo_status_t nmo_document_refresh_plugin_diagnostics(
    nmo_document_t *document);

NMO_API void nmo_document_set_runtime_load_stats(
    nmo_document_t *document,
    const nmo_runtime_load_stats_t *stats);

NMO_API void nmo_document_set_plugin_diagnostics(
    nmo_document_t *document,
    const nmo_session_plugin_dependency_status_t *entries,
    size_t entry_count,
    size_t missing_count,
    size_t outdated_count,
    int extension_registry_available);

NMO_API void nmo_document_set_file_header(
    nmo_document_t *document,
    const void *header,
    size_t header_size);

NMO_API nmo_status_t nmo_document_add_included_file(
    nmo_document_t *document,
    const char *name,
    const void *data,
    uint32_t size);

NMO_API nmo_status_t nmo_document_add_included_file_ex(
    nmo_document_t *document,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta);

NMO_API nmo_status_t nmo_document_add_included_file_borrowed(
    nmo_document_t *document,
    const char *name,
    const void *data,
    uint32_t size);

NMO_API nmo_status_t nmo_document_add_included_file_borrowed_ex(
    nmo_document_t *document,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta);

NMO_API nmo_status_t nmo_document_set_included_file_owners(
    nmo_document_t *document,
    uint32_t index,
    const nmo_object_id_t *owner_ids,
    uint32_t owner_count);

NMO_API nmo_status_t nmo_document_replace_included_file(
    nmo_document_t *document,
    uint32_t index,
    const void *new_data,
    uint32_t new_size);

NMO_API nmo_status_t nmo_document_remove_included_file(
    nmo_document_t *document,
    uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_FILE_STATE_H */
