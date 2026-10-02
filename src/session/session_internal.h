#ifndef NMO_SESSION_INTERNAL_H
#define NMO_SESSION_INTERNAL_H

#include "session/nmo_session.h"
#include "session/nmo_session_pipeline.h"
#include "session/nmo_runtime_kernel.h"
#include "session/nmo_reference_resolver.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "format/nmo_file_state.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_context.h"
#include "object/nmo_edit_flags.h"
#include "object/nmo_object_index.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_runtime.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_object_query_index nmo_object_query_index_t;
typedef struct nmo_id_sanitizer nmo_id_sanitizer_t;
typedef struct nmo_shadow_storage nmo_shadow_storage_t;
typedef struct nmo_chunk_pool nmo_chunk_pool_t;
typedef struct nmo_behavior_index nmo_behavior_index_t;
typedef struct nmo_ref_graph nmo_ref_graph_t;
typedef struct nmo_extension_registry nmo_extension_registry_t;
typedef struct nmo_header nmo_header_t;

#define NMO_RUNTIME_REQUEST_DEFER_CACHE_INVALIDATION 0x40000000u

#define NMO_WORKSPACE_EDIT_KNOWN_FLAGS \
    ((uint32_t)(NMO_WORKSPACE_EDIT_OBJECT_STATE | NMO_WORKSPACE_EDIT_REFERENCES | \
                NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH | NMO_WORKSPACE_EDIT_NAMES | \
                NMO_WORKSPACE_EDIT_RESOURCES))

/**
 * Session structure
 */
typedef struct nmo_session {
    /* Retained context reference */
    nmo_context_t *context;

    /* Allocator snapshot used to allocate/free the session + arena */
    nmo_allocator_t allocator;

    /* Owned resources */
    nmo_arena_t *arena;
    nmo_object_repository_t *repository;

    /* Object index (Phase 5) */
    nmo_object_index_t *object_index;
    nmo_object_query_index_t *object_query_index;
    bool edit_active;

    /* Reference resolver (initialised on demand) */
    nmo_reference_resolver_t *reference_resolver;
    nmo_arena_t *reference_resolver_arena;

    /* ID sanitizer */
    nmo_id_sanitizer_t *id_sanitizer;

    /* Shadow storage (included files + chunk tails) */
    nmo_shadow_storage_t *shadow_storage;


    /* Consolidated file round-trip state */
    nmo_file_state_t file_state;

    /* File header (stored opaquely in arena to avoid format layer dependency) */
    void *file_header;
    size_t file_header_size;

    /* Included files */
    nmo_arena_array_t included_files;

    /* Chunk pool for chunk allocations */
    nmo_chunk_pool_t *chunk_pool;
    size_t chunk_pool_capacity;

    /* Finish loading diagnostics */
    nmo_runtime_load_stats_t finish_stats;
    int finish_stats_valid;

    /* Plugin dependency diagnostics */
    nmo_session_plugin_diagnostics_t plugin_diag;
    int plugin_diag_valid;

    /* Backing store for plugin diagnostics entries (arena-backed) */
    nmo_arena_array_t plugin_diag_entries;

    /* Behavior acceleration (lazy after load, lazy-rebuilt when dirty) */
    nmo_behavior_index_t *behavior_index;
    int behavior_accel_dirty;
    int behavior_accel_built;
    int behavior_interface_dirty;
    int behavior_interface_parse_attempted;
    nmo_behavior_interface_parse_stats_t behavior_interface_parse_stats;

    /* Partial loads contain only metadata/header state and cannot be mutated. */
    int partial_load;

    /* Cached reference graph (lazy-built, invalidated on mutation) */
    nmo_ref_graph_t *cached_ref_graph;
    nmo_arena_t *ref_graph_arena;

    /* Runtime operation callbacks (set by app layer, used by runtime kernel) */
    nmo_runtime_ops_t runtime_ops;
} nmo_session_t;

nmo_context_t *nmo_session_get_context(const nmo_session_t *session);
nmo_extension_registry_t *nmo_session_get_extension_registry(
    const nmo_session_t *session);
nmo_object_repository_t *nmo_session_get_repository(
    const nmo_session_t *session);
nmo_status_t nmo_session_set_file_header(
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
nmo_ref_graph_t *nmo_session_get_ref_graph(nmo_session_t *session);
void nmo_session_invalidate_ref_graph(nmo_session_t *session);
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
void nmo_session_internal_set_partial_load(nmo_session_t *session, int partial);
void nmo_session_invalidate_behavior_index(nmo_session_t *session);
/** Drop the caches an edit with these flags made stale (nmo_edit_flags.h). */
nmo_status_t nmo_runtime_apply_edit_flags(nmo_session_t *session, uint32_t flags);
void nmo_runtime_destroy_object_state(
    nmo_session_t *session,
    nmo_object_t *object);

/**
 * @brief Find the effective registered type descriptor for an object.
 */
static inline const nmo_type_descriptor_t *runtime_find_type_for_object(
    const nmo_type_runtime_t *type_rt,
    const nmo_object_t *object)
{
    if (type_rt == NULL || type_rt->types == NULL || object == NULL) {
        return NULL;
    }
    return nmo_type_query_find_for_object(type_rt->types, object);
}

#endif /* NMO_SESSION_INTERNAL_H */
