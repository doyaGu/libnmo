/**
 * @file runtime_internal.h
 * @brief Shared internal runtime helpers and owner-layer accessors.
 *
 * Internal header, not part of the public API.
 */

#ifndef NMO_SESSION_RUNTIME_INTERNAL_H
#define NMO_SESSION_RUNTIME_INTERNAL_H

#include "core/nmo_error.h"
#include "document/nmo_document.h"
#include "document/nmo_document_file_state.h"
#include "runtime/nmo_workspace.h"
#include "runtime/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_session_pipeline.h"
#include "session/nmo_serializer.h"
#include "session/nmo_session.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_type_system.h"
#include "format/nmo_object.h"
#include "format/nmo_interface_view.h"
#include "object/nmo_object_index.h"
#include "behavior/nmo_script_edit_graph.h"

#include <stddef.h>
#include <stdint.h>

#define NMO_RUNTIME_REQUEST_DEFER_CACHE_INVALIDATION 0x40000000u

typedef struct nmo_session nmo_session_t;
typedef struct nmo_object nmo_object_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk_pool nmo_chunk_pool_t;
typedef struct nmo_shadow_storage nmo_shadow_storage_t;
typedef struct nmo_id_sanitizer nmo_id_sanitizer_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_ref_graph nmo_ref_graph_t;
typedef struct nmo_behavior_index nmo_behavior_index_t;

nmo_session_t *nmo_document_internal_session(nmo_document_t *document);
const nmo_session_t *nmo_document_internal_session_const(
    const nmo_document_t *document);
nmo_session_t *nmo_workspace_internal_session(nmo_workspace_t *workspace);
const nmo_session_t *nmo_workspace_internal_session_const(
    const nmo_workspace_t *workspace);

bool nmo_session_open_file_with_context(
    const char *path,
    nmo_context_t **out_ctx,
    nmo_session_t **out_session,
    char *errbuf,
    size_t errbuf_size);
void nmo_session_close_with_context(
    nmo_context_t *ctx,
    nmo_session_t *session);

nmo_status_t nmo_runtime_apply_edit_flags(nmo_session_t *session, uint32_t flags);
nmo_status_t nmo_session_borrow_document(
    nmo_session_t *session,
    nmo_document_t **out_document);
/* Legacy 3-argument runtime-op callbacks kept internal to the session bridge. */
nmo_status_t nmo_load_file(
    nmo_session_t *session,
    const char *path,
    const nmo_load_options_t *opts);
nmo_status_t nmo_save_file(
    nmo_session_t *session,
    const char *path,
    const nmo_save_options_t *opts);
nmo_session_t *nmo_session_load(nmo_context_t *ctx, const char *filename);
nmo_status_t nmo_session_load_file(
    nmo_session_t *session,
    const char *filename,
    const nmo_load_options_t *options,
    nmo_runtime_report_t *out_report);
nmo_status_t nmo_session_save_file(
    nmo_session_t *session,
    const char *filename,
    const nmo_save_options_t *options,
    nmo_runtime_report_t *out_report);
nmo_status_t nmo_session_create_object(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id,
    nmo_runtime_report_t *out_report);
nmo_status_t nmo_session_copy_objects(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report);
nmo_status_t nmo_session_destroy_objects(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report);
nmo_status_t nmo_session_preview_destroy(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_expanded_ids,
    size_t *out_expanded_count);
nmo_context_t *nmo_session_get_context(const nmo_session_t *session);
nmo_extension_registry_t *nmo_session_get_extension_registry(
    const nmo_session_t *session);
nmo_object_repository_t *nmo_session_get_repository(
    const nmo_session_t *session);
nmo_behavior_index_t *nmo_session_get_behavior_index(
    nmo_session_t *session);
nmo_status_t nmo_session_ensure_behavior_acceleration(
    nmo_session_t *session);
void nmo_session_set_file_header(
    nmo_session_t *session,
    const void *header,
    size_t header_size);
nmo_status_t nmo_session_set_file_info(
    nmo_session_t *session,
    const nmo_file_info_t *info);
void nmo_session_set_manager_data(
    nmo_session_t *session,
    nmo_manager_data_t *data,
    uint32_t count);
void nmo_session_set_runtime_load_stats(
    nmo_session_t *session,
    const nmo_runtime_load_stats_t *stats);
void nmo_session_set_plugin_diagnostics(
    nmo_session_t *session,
    const nmo_session_plugin_dependency_status_t *entries,
    size_t entry_count,
    size_t missing_count,
    size_t outdated_count,
    int extension_registry_available);
void nmo_session_set_object_index(
    nmo_session_t *session,
    nmo_object_index_t *index);
nmo_status_t nmo_session_set_plugin_dependencies(
    nmo_session_t *session,
    nmo_plugin_dep_t *deps,
    uint32_t count);
nmo_status_t nmo_session_refresh_plugin_diagnostics(
    nmo_session_t *session);
void nmo_session_get_behavior_interface_diagnostics(
    const nmo_session_t *session,
    nmo_session_behavior_interface_diagnostics_t *out_diag);
nmo_ref_graph_t *nmo_session_get_ref_graph(nmo_session_t *session);
void nmo_session_invalidate_ref_graph(nmo_session_t *session);
void nmo_session_invalidate_behavior_index(nmo_session_t *session);
nmo_status_t nmo_session_get_objects(
    nmo_session_t *session,
    nmo_object_t ***out_objects,
    size_t *out_count);
nmo_status_t nmo_session_rebuild_indexes(
    nmo_session_t *session,
    uint32_t flags);
void nmo_session_invalidate_object_query(
    nmo_session_t *session,
    uint32_t flags);
nmo_included_file_t *nmo_session_get_included_files(
    const nmo_session_t *session,
    uint32_t *out_count);
nmo_status_t nmo_session_add_included_file(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size);
nmo_status_t nmo_session_add_included_file_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta);
nmo_status_t nmo_session_add_included_file_borrowed(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size);
nmo_status_t nmo_session_add_included_file_borrowed_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta);
nmo_status_t nmo_session_set_included_file_owners(
    nmo_session_t *session,
    uint32_t index,
    const nmo_object_id_t *owner_ids,
    uint32_t owner_count);
nmo_status_t nmo_session_replace_included_file(
    nmo_session_t *session,
    uint32_t index,
    const void *new_data,
    uint32_t new_size);
nmo_status_t nmo_session_remove_included_file(
    nmo_session_t *session,
    uint32_t index);
const nmo_file_state_t *nmo_session_get_file_state(const nmo_session_t *session);
nmo_file_info_t nmo_session_get_file_info(const nmo_session_t *session);
const nmo_header_t *nmo_session_get_header(const nmo_session_t *session);
int nmo_session_is_partial_load(const nmo_session_t *session);
int nmo_session_has_materialized_load_state(const nmo_session_t *session);
nmo_status_t nmo_session_get_runtime_load_stats(
    const nmo_session_t *session,
    nmo_runtime_load_stats_t *out_stats);
const nmo_session_plugin_diagnostics_t *nmo_session_get_plugin_diagnostics(
    const nmo_session_t *session);

nmo_context_t *nmo_document_internal_context(const nmo_document_t *document);
nmo_object_repository_t *nmo_document_internal_repository(
    const nmo_document_t *document);
const nmo_type_registry_t *nmo_document_internal_type_registry(
    const nmo_document_t *document);
const nmo_type_runtime_t *nmo_document_internal_type_runtime(
    const nmo_document_t *document);
nmo_arena_t *nmo_document_internal_arena(const nmo_document_t *document);
nmo_chunk_pool_t *nmo_document_internal_ensure_chunk_pool(
    nmo_document_t *document,
    size_t initial_capacity_hint);
nmo_id_sanitizer_t *nmo_document_internal_get_id_sanitizer(
    const nmo_document_t *document);
nmo_shadow_storage_t *nmo_document_internal_get_shadow_storage(
    const nmo_document_t *document);
const nmo_file_state_t *nmo_document_internal_file_state(
    const nmo_document_t *document);
const nmo_header_t *nmo_document_internal_header(
    const nmo_document_t *document);
nmo_status_t nmo_document_internal_get_objects(
    nmo_document_t *document,
    nmo_object_t ***out_objects,
    size_t *out_count);
nmo_status_t nmo_document_internal_rebuild_indexes(
    nmo_document_t *document,
    uint32_t flags);
void nmo_document_internal_invalidate_object_query(
    nmo_document_t *document,
    uint32_t flags);
nmo_status_t nmo_document_internal_get_runtime_load_stats(
    const nmo_document_t *document,
    nmo_runtime_load_stats_t *out_stats);
int nmo_document_internal_is_partial_load(const nmo_document_t *document);
int nmo_document_internal_has_materialized_load_state(
    const nmo_document_t *document);
const nmo_session_plugin_diagnostics_t *nmo_document_internal_plugin_diagnostics(
    const nmo_document_t *document);
nmo_status_t nmo_document_internal_load_file(
    nmo_document_t *document,
    const char *path,
    const nmo_load_options_t *opts);
nmo_status_t nmo_document_internal_save_file(
    nmo_document_t *document,
    const char *path,
    const nmo_save_options_t *opts);
nmo_ref_graph_t *nmo_document_internal_ref_graph(nmo_document_t *document);
void nmo_document_internal_invalidate_ref_graph(nmo_document_t *document);
nmo_behavior_index_t *nmo_document_internal_behavior_index(
    nmo_document_t *document);
void nmo_document_internal_invalidate_behavior_index(nmo_document_t *document);
nmo_status_t nmo_document_internal_ensure_behavior_acceleration(
    nmo_document_t *document);
void nmo_document_internal_get_behavior_interface_diagnostics(
    nmo_document_t *document,
    nmo_session_behavior_interface_diagnostics_t *out_diag);
nmo_status_t nmo_document_internal_interface_view_from_behavior(
    nmo_document_t *document,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view);
nmo_status_t nmo_document_internal_apply_edit_flags(
    nmo_document_t *document,
    uint32_t flags);
nmo_status_t nmo_document_internal_create_object(
    nmo_document_t *document,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id);
nmo_status_t nmo_document_internal_preview_destroy(
    nmo_document_t *document,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_destroy_ids,
    size_t *out_destroy_count);
nmo_status_t nmo_document_internal_destroy_objects(
    nmo_document_t *document,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags);
nmo_status_t nmo_document_internal_execute_runtime_request(
    nmo_document_t *document,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report);

nmo_context_t *nmo_workspace_internal_context(const nmo_workspace_t *workspace);
nmo_object_repository_t *nmo_workspace_internal_repository(
    const nmo_workspace_t *workspace);
const nmo_type_registry_t *nmo_workspace_internal_type_registry(
    const nmo_workspace_t *workspace);
const nmo_type_runtime_t *nmo_workspace_internal_type_runtime(
    const nmo_workspace_t *workspace);
nmo_arena_t *nmo_workspace_internal_document_arena(
    const nmo_workspace_t *workspace);
nmo_ref_graph_t *nmo_workspace_internal_ref_graph(nmo_workspace_t *workspace);
void nmo_workspace_internal_invalidate_ref_graph(nmo_workspace_t *workspace);
nmo_behavior_index_t *nmo_workspace_internal_behavior_index(
    nmo_workspace_t *workspace);
void nmo_workspace_internal_invalidate_behavior_index(
    nmo_workspace_t *workspace);
nmo_status_t nmo_workspace_internal_ensure_behavior_acceleration(
    nmo_workspace_t *workspace);
void nmo_workspace_internal_get_behavior_interface_diagnostics(
    nmo_workspace_t *workspace,
    nmo_session_behavior_interface_diagnostics_t *out_diag);
nmo_status_t nmo_workspace_internal_interface_view_from_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view);
nmo_status_t nmo_workspace_internal_script_edit_graph_build(
    nmo_workspace_t *workspace,
    nmo_object_id_t root_behavior_id,
    uint32_t max_depth,
    nmo_script_edit_graph_t **out_graph);
nmo_status_t nmo_workspace_internal_apply_edit_flags(
    nmo_workspace_t *workspace,
    uint32_t flags);
nmo_status_t nmo_workspace_internal_borrow_document(
    nmo_workspace_t *workspace,
    nmo_document_t **out_document);
nmo_status_t nmo_workspace_internal_create_object(
    nmo_workspace_t *workspace,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id);
nmo_status_t nmo_workspace_internal_preview_destroy(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_destroy_ids,
    size_t *out_destroy_count);
nmo_status_t nmo_workspace_internal_destroy_objects(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags);
nmo_status_t nmo_workspace_internal_execute_runtime_request(
    nmo_workspace_t *workspace,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report);

/**
 * @brief Find type descriptor for an object by its class ID (inherited lookup).
 */
static inline const nmo_type_descriptor_t *runtime_find_type_for_object(
    const nmo_type_runtime_t *type_rt,
    const nmo_object_t *object)
{
    if (type_rt == NULL || type_rt->types == NULL || object == NULL) {
        return NULL;
    }
    return nmo_type_registry_find_by_class_id_inherited(type_rt->types, object->class_id);
}

#endif /* NMO_SESSION_RUNTIME_INTERNAL_H */

