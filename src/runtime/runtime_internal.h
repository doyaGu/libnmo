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
#include "runtime/nmo_workspace.h"
#include "session/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_serializer.h"
#include "session/nmo_save_id_remap.h"
#include "session/nmo_session.h"
#include "session/nmo_session_bridge.h"
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

nmo_status_t nmo_runtime_apply_edit_flags(nmo_session_t *session, uint32_t flags);

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
nmo_status_t nmo_document_internal_load_file(
    nmo_document_t *document,
    const char *path,
    const nmo_load_options_t *opts);
nmo_status_t nmo_document_internal_save_file(
    nmo_document_t *document,
    const char *path,
    const nmo_save_options_t *opts);

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
