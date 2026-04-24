/**
 * @file session.c
 * @brief Session implementation (Phase 8.2)
 */

#include "session/nmo_session.h"
#include "session/nmo_session_pipeline.h"
#include "runtime/nmo_context.h"
#include "document/nmo_document.h"
#include "runtime/nmo_workspace.h"
#include "runtime_internal.h"
#include "runtime_internal.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "session/nmo_runtime_kernel.h"
#include "extension/nmo_extension_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_allocator.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_index.h"
#include "object/nmo_object_query.h"
#include "session/nmo_reference_resolver.h"
#include "object/nmo_shadow_storage.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk_pool.h"
#include "format/nmo_header1.h"
#include "behavior/nmo_behavior_registry.h"
#include "behavior/nmo_behavior_analyze.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_manager_guids.h"
#include "type/nmo_type_runtime.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include <stddef.h>
#include <string.h>

#define DEFAULT_ARENA_SIZE (1024 * 1024)  /* 1 MB */
#define DEFAULT_CHUNK_POOL_CAPACITY 128

struct nmo_document {
    nmo_allocator_t allocator;
    nmo_context_t *context;
    nmo_arena_t *arena;
    nmo_object_repository_t *repository;
    nmo_session_t *session;
    bool owns_session;
};

struct nmo_workspace {
    nmo_allocator_t allocator;
    nmo_context_t *context;
    nmo_document_t *document;
};

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

/**
 * Create session
 */
static int nmo_session_build_behavior_index(nmo_session_t *session);
static int nmo_session_ensure_behavior_index(nmo_session_t *session);
static void nmo_session_post_load(nmo_session_t *session);

static nmo_allocator_t owner_allocator_from_context(nmo_context_t *ctx)
{
    nmo_allocator_t *ctx_allocator = nmo_context_get_allocator(ctx);
    return (ctx_allocator != NULL) ? *ctx_allocator : nmo_allocator_default();
}

nmo_document_t *nmo_document_create(nmo_context_t *ctx)
{
    if (ctx == NULL) {
        return NULL;
    }

    nmo_allocator_t allocator = owner_allocator_from_context(ctx);
    nmo_document_t *document = (nmo_document_t *)nmo_alloc(
        &allocator, sizeof(*document), _Alignof(nmo_document_t));
    if (document == NULL) {
        return NULL;
    }

    memset(document, 0, sizeof(*document));
    document->allocator = allocator;
    document->session = nmo_session_create(ctx);
    if (document->session == NULL) {
        nmo_free(&allocator, document);
        return NULL;
    }
    document->context = document->session->context;
    document->arena = document->session->arena;
    document->repository = document->session->repository;
    document->owns_session = true;

    return document;
}

nmo_status_t nmo_session_borrow_document(
    nmo_session_t *session,
    nmo_document_t **out_document)
{
    nmo_context_t *ctx = NULL;
    nmo_allocator_t allocator;
    nmo_document_t *document = NULL;

    if (session == NULL || out_document == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_document = NULL;

    ctx = nmo_session_get_context(session);
    if (ctx == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    allocator = owner_allocator_from_context(ctx);
    document = (nmo_document_t *)nmo_alloc(
        &allocator, sizeof(*document), _Alignof(nmo_document_t));
    if (document == NULL) {
        return NMO_ERR_NOMEM;
    }

    memset(document, 0, sizeof(*document));
    document->allocator = allocator;
    document->session = session;
    document->context = session->context;
    document->arena = session->arena;
    document->repository = session->repository;
    document->owns_session = false;
    *out_document = document;
    return NMO_OK;
}

void nmo_document_destroy(nmo_document_t *document)
{
    if (document == NULL) {
        return;
    }

    if (document->owns_session && document->session != NULL) {
        nmo_session_destroy(document->session);
    }
    document->session = NULL;
    nmo_free(&document->allocator, document);
}

nmo_context_t *nmo_document_get_context(const nmo_document_t *document)
{
    return document != NULL ? document->context : NULL;
}

nmo_object_repository_t *nmo_document_get_repository(const nmo_document_t *document)
{
    return document != NULL ? document->repository : NULL;
}

nmo_session_t *nmo_document_internal_session(nmo_document_t *document)
{
    return document != NULL ? document->session : NULL;
}

const nmo_session_t *nmo_document_internal_session_const(const nmo_document_t *document)
{
    return document != NULL ? document->session : NULL;
}

nmo_status_t nmo_workspace_create(
    nmo_context_t *ctx,
    nmo_document_t *document,
    nmo_workspace_t **out_workspace)
{
    nmo_workspace_t *workspace = NULL;
    nmo_context_t *document_ctx = NULL;
    nmo_allocator_t allocator;

    if (ctx == NULL || document == NULL || out_workspace == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    *out_workspace = NULL;

    document_ctx = nmo_document_get_context(document);
    if (document_ctx == NULL || document_ctx != ctx) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    allocator = owner_allocator_from_context(ctx);
    workspace = (nmo_workspace_t *)nmo_alloc(
        &allocator, sizeof(*workspace), _Alignof(nmo_workspace_t));
    if (workspace == NULL) {
        return NMO_ERR_NOMEM;
    }

    memset(workspace, 0, sizeof(*workspace));
    workspace->allocator = allocator;
    workspace->context = ctx;
    workspace->document = document;
    *out_workspace = workspace;
    return NMO_OK;
}

void nmo_workspace_destroy(nmo_workspace_t *workspace)
{
    if (workspace == NULL) {
        return;
    }
    nmo_free(&workspace->allocator, workspace);
}

nmo_document_t *nmo_workspace_get_document(nmo_workspace_t *workspace)
{
    return workspace != NULL ? workspace->document : NULL;
}

nmo_context_t *nmo_document_internal_context(const nmo_document_t *document)
{
    return nmo_document_get_context(document);
}

nmo_object_repository_t *nmo_document_internal_repository(const nmo_document_t *document)
{
    return nmo_document_get_repository(document);
}

const nmo_type_registry_t *nmo_document_internal_type_registry(
    const nmo_document_t *document)
{
    nmo_context_t *ctx = nmo_document_get_context(document);
    return ctx != NULL ? nmo_context_get_type_registry(ctx) : NULL;
}

const nmo_type_runtime_t *nmo_document_internal_type_runtime(
    const nmo_document_t *document)
{
    nmo_context_t *ctx = nmo_document_get_context(document);
    return ctx != NULL ? nmo_context_get_type_runtime(ctx) : NULL;
}

nmo_arena_t *nmo_document_internal_arena(const nmo_document_t *document)
{
    return document != NULL ? document->arena : NULL;
}

nmo_chunk_pool_t *nmo_document_internal_ensure_chunk_pool(
    nmo_document_t *document,
    size_t initial_capacity_hint)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_ensure_chunk_pool(session, initial_capacity_hint)
        : NULL;
}

nmo_id_sanitizer_t *nmo_document_internal_get_id_sanitizer(
    const nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session((nmo_document_t *)document);
    return session != NULL ? nmo_session_get_id_sanitizer(session) : NULL;
}

nmo_shadow_storage_t *nmo_document_internal_get_shadow_storage(
    const nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session((nmo_document_t *)document);
    return session != NULL ? nmo_session_get_shadow_storage(session) : NULL;
}

const nmo_file_state_t *nmo_document_internal_file_state(
    const nmo_document_t *document)
{
    const nmo_session_t *session = nmo_document_internal_session_const(document);
    return session != NULL ? nmo_session_get_file_state(session) : NULL;
}

const nmo_header_t *nmo_document_internal_header(
    const nmo_document_t *document)
{
    const nmo_session_t *session = nmo_document_internal_session_const(document);
    return session != NULL ? nmo_session_get_header(session) : NULL;
}

nmo_status_t nmo_document_internal_get_objects(
    nmo_document_t *document,
    nmo_object_t ***out_objects,
    size_t *out_count)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_get_objects(session, out_objects, out_count)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_document_internal_rebuild_indexes(
    nmo_document_t *document,
    uint32_t flags)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_rebuild_indexes(session, flags)
        : NMO_ERR_INVALID_STATE;
}

void nmo_document_internal_invalidate_object_query(
    nmo_document_t *document,
    uint32_t flags)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    if (session != NULL) {
        nmo_session_invalidate_object_query(session, flags);
    }
}

nmo_status_t nmo_document_internal_get_runtime_load_stats(
    const nmo_document_t *document,
    nmo_runtime_load_stats_t *out_stats)
{
    const nmo_session_t *session = nmo_document_internal_session_const(document);
    return session != NULL
        ? nmo_session_get_runtime_load_stats(session, out_stats)
        : NMO_ERR_INVALID_STATE;
}

int nmo_document_internal_is_partial_load(const nmo_document_t *document)
{
    const nmo_session_t *session = nmo_document_internal_session_const(document);
    return session != NULL ? nmo_session_is_partial_load(session) : 0;
}

int nmo_document_internal_has_materialized_load_state(const nmo_document_t *document)
{
    const nmo_session_t *session = nmo_document_internal_session_const(document);
    return session != NULL ? nmo_session_has_materialized_load_state(session) : 0;
}

const nmo_session_plugin_diagnostics_t *nmo_document_internal_plugin_diagnostics(
    const nmo_document_t *document)
{
    const nmo_session_t *session = nmo_document_internal_session_const(document);
    return session != NULL ? nmo_session_get_plugin_diagnostics(session) : NULL;
}

nmo_status_t nmo_document_internal_load_file(
    nmo_document_t *document,
    const char *path,
    const nmo_load_options_t *opts)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL ? nmo_session_load_file(session, path, opts, NULL) : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_document_internal_save_file(
    nmo_document_t *document,
    const char *path,
    const nmo_save_options_t *opts)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL ? nmo_session_save_file(session, path, opts, NULL) : NMO_ERR_INVALID_STATE;
}

nmo_ref_graph_t *nmo_document_internal_ref_graph(nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL ? nmo_session_get_ref_graph(session) : NULL;
}

void nmo_document_internal_invalidate_ref_graph(nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    if (session != NULL) {
        nmo_session_invalidate_ref_graph(session);
    }
}

nmo_behavior_index_t *nmo_document_internal_behavior_index(
    nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL ? nmo_session_get_behavior_index(session) : NULL;
}

void nmo_document_internal_invalidate_behavior_index(nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    if (session != NULL) {
        nmo_session_invalidate_behavior_index(session);
    }
}

nmo_status_t nmo_document_internal_ensure_behavior_acceleration(
    nmo_document_t *document)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_ensure_behavior_acceleration(session)
        : NMO_ERR_INVALID_STATE;
}

void nmo_document_internal_get_behavior_interface_diagnostics(
    nmo_document_t *document,
    nmo_session_behavior_interface_diagnostics_t *out_diag)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    if (out_diag != NULL) {
        memset(out_diag, 0, sizeof(*out_diag));
    }
    if (session != NULL && out_diag != NULL) {
        nmo_session_get_behavior_interface_diagnostics(session, out_diag);
    }
}

nmo_status_t nmo_document_internal_interface_view_from_behavior(
    nmo_document_t *document,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_interface_view_from_behavior(session, owner_behavior_id, out_view)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_document_internal_apply_edit_flags(
    nmo_document_t *document,
    uint32_t flags)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_runtime_apply_edit_flags(session, flags)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_document_internal_create_object(
    nmo_document_t *document,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_create_object(session, class_id, name, type_guid, out_created_id, NULL)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_document_internal_preview_destroy(
    nmo_document_t *document,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_destroy_ids,
    size_t *out_destroy_count)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_preview_destroy(
              session,
              object_ids,
              object_count,
              flags,
              arena,
              out_destroy_ids,
              out_destroy_count)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_document_internal_destroy_objects(
    nmo_document_t *document,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_destroy_objects(session, object_ids, object_count, flags, NULL)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_document_internal_execute_runtime_request(
    nmo_document_t *document,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report)
{
    nmo_session_t *session = nmo_document_internal_session(document);
    return session != NULL
        ? nmo_session_execute(session, request, out_report)
        : NMO_ERR_INVALID_STATE;
}

nmo_context_t *nmo_workspace_internal_context(const nmo_workspace_t *workspace)
{
    return workspace != NULL ? workspace->context : NULL;
}

nmo_object_repository_t *nmo_workspace_internal_repository(const nmo_workspace_t *workspace)
{
    return workspace != NULL ? nmo_document_get_repository(workspace->document) : NULL;
}

const nmo_type_registry_t *nmo_workspace_internal_type_registry(
    const nmo_workspace_t *workspace)
{
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    return ctx != NULL ? nmo_context_get_type_registry(ctx) : NULL;
}

const nmo_type_runtime_t *nmo_workspace_internal_type_runtime(
    const nmo_workspace_t *workspace)
{
    nmo_context_t *ctx = nmo_workspace_internal_context(workspace);
    return ctx != NULL ? nmo_context_get_type_runtime(ctx) : NULL;
}

nmo_arena_t *nmo_workspace_internal_document_arena(const nmo_workspace_t *workspace)
{
    return workspace != NULL ? nmo_document_internal_arena(workspace->document) : NULL;
}

nmo_ref_graph_t *nmo_workspace_internal_ref_graph(nmo_workspace_t *workspace)
{
    return workspace != NULL
        ? nmo_document_internal_ref_graph(workspace->document)
        : NULL;
}

void nmo_workspace_internal_invalidate_ref_graph(nmo_workspace_t *workspace)
{
    if (workspace != NULL) {
        nmo_document_internal_invalidate_ref_graph(workspace->document);
    }
}

nmo_behavior_index_t *nmo_workspace_internal_behavior_index(
    nmo_workspace_t *workspace)
{
    return workspace != NULL
        ? nmo_document_internal_behavior_index(workspace->document)
        : NULL;
}

void nmo_workspace_internal_invalidate_behavior_index(nmo_workspace_t *workspace)
{
    if (workspace != NULL) {
        nmo_document_internal_invalidate_behavior_index(workspace->document);
    }
}

nmo_status_t nmo_workspace_internal_ensure_behavior_acceleration(
    nmo_workspace_t *workspace)
{
    return workspace != NULL
        ? nmo_document_internal_ensure_behavior_acceleration(workspace->document)
        : NMO_ERR_INVALID_STATE;
}

void nmo_workspace_internal_get_behavior_interface_diagnostics(
    nmo_workspace_t *workspace,
    nmo_session_behavior_interface_diagnostics_t *out_diag)
{
    if (workspace != NULL) {
        nmo_document_internal_get_behavior_interface_diagnostics(
            workspace->document,
            out_diag);
    } else if (out_diag != NULL) {
        memset(out_diag, 0, sizeof(*out_diag));
    }
}

nmo_status_t nmo_workspace_internal_interface_view_from_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t owner_behavior_id,
    nmo_interface_view_t *out_view)
{
    return workspace != NULL
        ? nmo_document_internal_interface_view_from_behavior(
              workspace->document,
              owner_behavior_id,
              out_view)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_workspace_internal_script_edit_graph_build(
    nmo_workspace_t *workspace,
    nmo_object_id_t root_behavior_id,
    uint32_t max_depth,
    nmo_script_edit_graph_t **out_graph)
{
    return workspace != NULL
        ? nmo_script_edit_graph_build(workspace, root_behavior_id, max_depth, out_graph)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_workspace_internal_apply_edit_flags(
    nmo_workspace_t *workspace,
    uint32_t flags)
{
    return workspace != NULL
        ? nmo_document_internal_apply_edit_flags(workspace->document, flags)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_workspace_internal_borrow_document(
    nmo_workspace_t *workspace,
    nmo_document_t **out_document)
{
    nmo_session_t *session = nmo_workspace_internal_session(workspace);
    return session != NULL
        ? nmo_session_borrow_document(session, out_document)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_workspace_internal_create_object(
    nmo_workspace_t *workspace,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id)
{
    return workspace != NULL
        ? nmo_document_internal_create_object(
              workspace->document,
              class_id,
              name,
              type_guid,
              out_created_id)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_workspace_internal_preview_destroy(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_destroy_ids,
    size_t *out_destroy_count)
{
    return workspace != NULL
        ? nmo_document_internal_preview_destroy(
              workspace->document,
              object_ids,
              object_count,
              flags,
              arena,
              out_destroy_ids,
              out_destroy_count)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_workspace_internal_destroy_objects(
    nmo_workspace_t *workspace,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags)
{
    return workspace != NULL
        ? nmo_document_internal_destroy_objects(
              workspace->document,
              object_ids,
              object_count,
              flags)
        : NMO_ERR_INVALID_STATE;
}

nmo_status_t nmo_workspace_internal_execute_runtime_request(
    nmo_workspace_t *workspace,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report)
{
    return workspace != NULL
        ? nmo_document_internal_execute_runtime_request(
              workspace->document,
              request,
              out_report)
        : NMO_ERR_INVALID_STATE;
}

nmo_session_t *nmo_workspace_internal_session(nmo_workspace_t *workspace)
{
    return workspace != NULL ? nmo_document_internal_session(workspace->document) : NULL;
}

const nmo_session_t *nmo_workspace_internal_session_const(const nmo_workspace_t *workspace)
{
    return workspace != NULL ? nmo_document_internal_session_const(workspace->document) : NULL;
}

nmo_session_t *nmo_session_create(nmo_context_t *ctx) {
    if (ctx == NULL) {
        return NULL;
    }

    nmo_context_retain(ctx);

    nmo_allocator_t allocator = owner_allocator_from_context(ctx);

    nmo_session_t *session = (nmo_session_t *) nmo_alloc(&allocator, sizeof(nmo_session_t), _Alignof(nmo_session_t));
    if (session == NULL) {
        nmo_context_release(ctx);
        return NULL;
    }

    memset(session, 0, sizeof(*session));

    /* Retained context */
    session->context = ctx;

    /* Snapshot allocator for consistent frees */
    session->allocator = allocator;

    /* Create arena for session-local allocations */
    session->arena = nmo_arena_create(&session->allocator, DEFAULT_ARENA_SIZE);
    if (session->arena == NULL) {
        nmo_free(&session->allocator, session);
        nmo_context_release(ctx);
        return NULL;
    }

    /* Create object repository */
    session->repository = nmo_object_repository_create(&session->allocator);
    if (session->repository == NULL) {
        nmo_arena_destroy(session->arena);
        nmo_free(&session->allocator, session);
        nmo_context_release(ctx);
        return NULL;
    }
    if (nmo_arena_array_init(&session->included_files, sizeof(nmo_included_file_t), 0, session->arena) != NMO_OK) {
        nmo_object_repository_destroy(session->repository);
        nmo_arena_destroy(session->arena);
        nmo_free(&session->allocator, session);
        nmo_context_release(ctx);
        return NULL;
    }

    if (nmo_arena_array_init(&session->plugin_diag_entries,
                            sizeof(nmo_session_plugin_dependency_status_t),
                            0,
                            session->arena) != NMO_OK) {
        nmo_object_repository_destroy(session->repository);
        nmo_arena_destroy(session->arena);
        nmo_free(&session->allocator, session);
        nmo_context_release(ctx);
        return NULL;
    }

    /* file_state zeroed by memset(session, 0, ...) above */
    session->chunk_pool = NULL;
    session->chunk_pool_capacity = 0;

    memset(&session->finish_stats, 0, sizeof(session->finish_stats));
    session->finish_stats_valid = 0;
    memset(&session->plugin_diag, 0, sizeof(session->plugin_diag));
    session->plugin_diag_valid = 0;

    /* Initialize object index */
    session->object_index = NULL;
    session->object_query_index = NULL;

    /* Initialize reference resolver */
    session->reference_resolver = NULL;
    session->reference_resolver_arena = NULL;

    /* Initialize ID sanitizer */
    session->id_sanitizer = nmo_id_sanitizer_create(session->arena);
    if (session->id_sanitizer == NULL) {
        nmo_object_repository_destroy(session->repository);
        nmo_arena_destroy(session->arena);
        nmo_free(&session->allocator, session);
        nmo_context_release(ctx);
        return NULL;
    }

    /* Initialize shadow storage */
    session->shadow_storage = nmo_shadow_storage_create(session->arena);
    if (session->shadow_storage == NULL) {
        nmo_id_sanitizer_destroy(session->id_sanitizer);
        session->id_sanitizer = NULL;
        nmo_object_repository_destroy(session->repository);
        nmo_arena_destroy(session->arena);
        nmo_free(&session->allocator, session);
        nmo_context_release(ctx);
        return NULL;
    }
    
    /* Initialize file header */
    session->file_header = NULL;
    session->file_header_size = 0;

    /* Set runtime operation callbacks (app-layer implementations) */
    session->runtime_ops.load_file = nmo_load_file;
    session->runtime_ops.save_file = nmo_save_file;
    session->runtime_ops.post_load = nmo_session_post_load;

    return session;
}

/**
 * Destroy session
 */
void nmo_session_destroy(nmo_session_t *session) {
    if (session != NULL) {
        if (session->object_index != NULL) {
            if (session->repository != NULL) {
                nmo_object_repository_set_index(session->repository, NULL);
            }
            nmo_object_index_destroy(session->object_index);
            session->object_index = NULL;
        }

        if (session->object_query_index != NULL) {
            nmo_object_query_index_destroy(session->object_query_index);
            session->object_query_index = NULL;
        }

        /* Destroy owned resources */
        if (session->repository != NULL) {
            nmo_object_repository_destroy(session->repository);
        }

        if (session->reference_resolver != NULL) {
            nmo_reference_resolver_destroy(session->reference_resolver);
            session->reference_resolver = NULL;
        }
        if (session->reference_resolver_arena != NULL) {
            nmo_arena_destroy(session->reference_resolver_arena);
            session->reference_resolver_arena = NULL;
        }

        if (session->id_sanitizer != NULL) {
            nmo_id_sanitizer_destroy(session->id_sanitizer);
            session->id_sanitizer = NULL;
        }

        if (session->shadow_storage != NULL) {
            nmo_shadow_storage_destroy(session->shadow_storage);
            session->shadow_storage = NULL;
        }

        if (session->chunk_pool != NULL) {
            nmo_chunk_pool_destroy(session->chunk_pool);
            session->chunk_pool = NULL;
            session->chunk_pool_capacity = 0;
        }

        if (session->behavior_index != NULL) {
            nmo_behavior_index_destroy(session->behavior_index);
            session->behavior_index = NULL;
        }

        /* Destroy cached ref graph and its dedicated arena */
        if (session->cached_ref_graph != NULL) {
            nmo_ref_graph_destroy(session->cached_ref_graph);
            session->cached_ref_graph = NULL;
        }
        if (session->ref_graph_arena != NULL) {
            nmo_arena_destroy(session->ref_graph_arena);
            session->ref_graph_arena = NULL;
        }

        if (session->arena != NULL) {
            nmo_arena_destroy(session->arena);
        }

        if (session->context != NULL) {
            nmo_context_release(session->context);
            session->context = NULL;
        }

        nmo_free(&session->allocator, session);
    }
}

/**
 * Get context
 */
nmo_context_t *nmo_session_get_context(const nmo_session_t *session) {
    return session ? session->context : NULL;
}

nmo_extension_registry_t *nmo_session_get_extension_registry(const nmo_session_t *session) {
    if (session == NULL) {
        return NULL;
    }
    return nmo_context_get_extension_registry(session->context);
}

/**
 * Get arena
 */
nmo_arena_t *nmo_session_get_arena(const nmo_session_t *session) {
    return session ? session->arena : NULL;
}

/**
 * Get object repository
 */
nmo_object_repository_t *nmo_session_get_repository(const nmo_session_t *session) {
    return session ? session->repository : NULL;
}

nmo_behavior_index_t *nmo_session_get_behavior_index(nmo_session_t *session) {
    if (session == NULL) return NULL;
    int index_result = nmo_session_ensure_behavior_index(session);
    if (index_result != NMO_OK) {
        return NULL;
    }
    return session->behavior_index;
}

void nmo_session_invalidate_behavior_index(nmo_session_t *session) {
    if (session != NULL) {
        session->behavior_accel_dirty = 1;
        session->behavior_interface_dirty = 1;
        session->behavior_accel_built = 0;
        session->behavior_interface_parse_attempted = 0;
        memset(&session->behavior_interface_parse_stats, 0,
               sizeof(session->behavior_interface_parse_stats));
    }
}

#define REF_GRAPH_ARENA_SIZE (64 * 1024)

nmo_ref_graph_t *nmo_session_get_ref_graph(nmo_session_t *session) {
    if (session == NULL) return NULL;
    if (session->cached_ref_graph != NULL) {
        return session->cached_ref_graph;
    }

    /* Lazy build: need repository + type registry */
    if (session->repository == NULL || session->context == NULL) {
        return NULL;
    }
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(session->context);
    if (type_rt == NULL || type_rt->types == NULL) {
        return NULL;
    }

    /* Create dedicated arena if needed */
    if (session->ref_graph_arena == NULL) {
        session->ref_graph_arena = nmo_arena_create(&session->allocator, REF_GRAPH_ARENA_SIZE);
        if (session->ref_graph_arena == NULL) {
            return NULL;
        }
    }

    session->cached_ref_graph = nmo_ref_graph_create(
        session->repository, type_rt->types, session->ref_graph_arena);
    return session->cached_ref_graph;
}

void nmo_session_invalidate_ref_graph(nmo_session_t *session) {
    if (session == NULL) return;
    if (session->cached_ref_graph != NULL) {
        nmo_ref_graph_destroy(session->cached_ref_graph);
        session->cached_ref_graph = NULL;
    }
    /* Reset arena rather than destroy 脙垄芒鈥?avoids alloc/free churn and pointer
     * reuse issues.  The arena is destroyed in nmo_session_destroy(). */
    if (session->ref_graph_arena != NULL) {
        nmo_arena_reset(session->ref_graph_arena);
    }
}

static int nmo_session_build_behavior_index(nmo_session_t *session) {
    if (session == NULL || session->context == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (session->behavior_index != NULL) {
        return NMO_OK;
    }

    session->behavior_index = nmo_behavior_index_create(session->arena);
    if (session->behavior_index != NULL) {
        nmo_document_t *document = NULL;
        nmo_workspace_t *workspace = NULL;
        int build_result = NMO_OK;

        build_result = nmo_session_borrow_document(session, &document);
        if (build_result != NMO_OK) {
            nmo_behavior_index_destroy(session->behavior_index);
            session->behavior_index = NULL;
            return build_result;
        }
        build_result = nmo_workspace_create(session->context, document, &workspace);
        if (build_result == NMO_OK) {
            build_result = nmo_behavior_index_build(session->behavior_index, workspace);
        }
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        if (build_result != NMO_OK) {
            nmo_behavior_index_destroy(session->behavior_index);
            session->behavior_index = NULL;
            return build_result;
        }
        return NMO_OK;
    }
    return NMO_ERR_NOMEM;
}

static int nmo_session_ensure_behavior_index(nmo_session_t *session) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (session->behavior_accel_dirty && session->behavior_index != NULL) {
        nmo_behavior_index_destroy(session->behavior_index);
        session->behavior_index = NULL;
    }

    if (session->behavior_accel_dirty || session->behavior_index == NULL) {
        int build_result = nmo_session_build_behavior_index(session);
        if (build_result != NMO_OK) {
            session->behavior_accel_built = 0;
            session->behavior_accel_dirty = 1;
            return build_result;
        }
        session->behavior_accel_dirty = 0;
    }

    return NMO_OK;
}

nmo_status_t nmo_session_ensure_behavior_acceleration(nmo_session_t *session) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (session->behavior_accel_built &&
        !session->behavior_accel_dirty &&
        !session->behavior_interface_dirty) {
        return NMO_OK;
    }

    int index_result = nmo_session_ensure_behavior_index(session);
    if (index_result != NMO_OK) {
        return index_result;
    }

    if ((session->behavior_interface_dirty || !session->behavior_accel_built) &&
        session->repository != NULL) {
        nmo_logger_t *logger = session->context
            ? nmo_context_get_logger(session->context)
            : NULL;
        nmo_behavior_interface_parse_stats_t stats;
        memset(&stats, 0, sizeof(stats));
        nmo_status_t parse_result = nmo_behavior_parse_all_interfaces_ex(
            session->repository, logger, &stats);
        session->behavior_interface_parse_stats = stats;
        session->behavior_interface_parse_attempted = 1;
        if (parse_result != NMO_OK) {
            if (logger) {
                nmo_log(logger, NMO_LOG_WARN,
                        "Behavior interface parsing reported errors; first status=%d object=%u file_id=%u offset=%zu/%zu",
                        parse_result,
                        stats.first_error_object_id,
                        stats.first_error_file_id,
                        stats.first_error_reader_offset,
                        stats.first_error_chunk_dwords);
            }
        }
        session->behavior_interface_dirty = 0;
    }

    session->behavior_accel_built = 1;
    session->behavior_accel_dirty = 0;
    return NMO_OK;
}

void nmo_session_get_behavior_interface_diagnostics(
    const nmo_session_t *session,
    nmo_session_behavior_interface_diagnostics_t *out_diag)
{
    if (!out_diag) {
        return;
    }
    memset(out_diag, 0, sizeof(*out_diag));
    if (!session) {
        out_diag->status = NMO_ERR_INVALID_ARGUMENT;
        return;
    }

    const nmo_behavior_interface_parse_stats_t *stats =
        &session->behavior_interface_parse_stats;
    out_diag->attempted = session->behavior_interface_parse_attempted;
    out_diag->status = stats->first_error;
    out_diag->available = session->behavior_interface_parse_attempted &&
                          stats->first_error == NMO_OK &&
                          stats->parsed_count > 0;
    out_diag->attempted_count = stats->attempted_count;
    out_diag->parsed_count = stats->parsed_count;
    out_diag->failed_count = stats->failed_count;
    out_diag->skipped_no_arena_count = stats->skipped_no_arena_count;
    out_diag->allocation_failure_count = stats->allocation_failure_count;
    out_diag->first_error_object_id = stats->first_error_object_id;
    out_diag->first_error_file_id = stats->first_error_file_id;
    out_diag->first_error_chunk_version = stats->first_error_chunk_version;
    out_diag->first_error_data_version = stats->first_error_data_version;
    out_diag->first_error_reader_offset = stats->first_error_reader_offset;
    out_diag->first_error_chunk_dwords = stats->first_error_chunk_dwords;
}

static void nmo_session_post_load(nmo_session_t *session) {
    if (session != NULL) {
        session->behavior_accel_built = 0;
        session->behavior_accel_dirty = 1;
        session->behavior_interface_dirty = 1;
        session->behavior_interface_parse_attempted = 0;
        memset(&session->behavior_interface_parse_stats, 0,
               sizeof(session->behavior_interface_parse_stats));
    }
}

nmo_chunk_pool_t *nmo_session_get_chunk_pool(const nmo_session_t *session) {
    return session ? session->chunk_pool : NULL;
}

nmo_id_sanitizer_t *nmo_session_get_id_sanitizer(const nmo_session_t *session) {
    return session ? session->id_sanitizer : NULL;
}

nmo_shadow_storage_t *nmo_session_get_shadow_storage(const nmo_session_t *session) {
    return session ? session->shadow_storage : NULL;
}

nmo_chunk_pool_t *nmo_session_ensure_chunk_pool(
    nmo_session_t *session,
    size_t initial_capacity_hint
) {
    if (session == NULL || session->arena == NULL) {
        return NULL;
    }

    if (session->chunk_pool != NULL) {
        return session->chunk_pool;
    }

    size_t capacity = initial_capacity_hint > 0 ? initial_capacity_hint : DEFAULT_CHUNK_POOL_CAPACITY;
    session->chunk_pool = nmo_chunk_pool_create(capacity, session->arena);
    session->chunk_pool_capacity = (session->chunk_pool != NULL) ? capacity : 0;
    return session->chunk_pool;
}

/**
 * Get consolidated file state
 */
const nmo_file_state_t *nmo_session_get_file_state(const nmo_session_t *session) {
    if (session == NULL) {
        return NULL;
    }
    return &session->file_state;
}

nmo_file_info_t nmo_session_get_file_info(const nmo_session_t *session) {
    if (session) {
        return session->file_state.info;
    }
    nmo_file_info_t empty;
    memset(&empty, 0, sizeof(empty));
    return empty;
}

nmo_status_t nmo_session_set_file_info(nmo_session_t *session, const nmo_file_info_t *info) {
    if (session == NULL || info == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    session->file_state.info = *info;
    return NMO_OK;
}

/**
 * Set manager data
 */
void nmo_session_set_manager_data(nmo_session_t *session, nmo_manager_data_t *data, uint32_t count) {
    if (session != NULL) {
        session->file_state.manager_data = data;
        session->file_state.manager_data_count = count;
    }
}

/**
 * Set plugin dependencies
 */
nmo_status_t nmo_session_set_plugin_dependencies(
    nmo_session_t *session,
    nmo_plugin_dep_t *deps,
    uint32_t count
) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    session->file_state.plugin_deps = deps;
    session->file_state.plugin_dep_count = count;

    if (deps == NULL || count == 0) {
        nmo_context_t *ctx = nmo_session_get_context(session);
        int registry_available = 0;
        if (ctx != NULL && nmo_context_get_extension_registry(ctx) != NULL) {
            registry_available = 1;
        }

        nmo_session_set_plugin_diagnostics(session, NULL, 0, 0, 0, registry_available);
        return NMO_OK;
    }

    return nmo_session_refresh_plugin_diagnostics(session);
}

static nmo_status_t nmo_session_copy_owner_ids(
    nmo_session_t *session,
    nmo_included_file_t *entry,
    const nmo_object_id_t *owner_ids,
    uint32_t owner_count
) {
    if (entry == NULL || session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (owner_ids == NULL || owner_count == 0) {
        nmo_arena_array_clear(&entry->owner_ids);
        return NMO_OK;
    }

    nmo_arena_array_clear(&entry->owner_ids);
    if (nmo_arena_array_append_array(&entry->owner_ids, owner_ids, owner_count) != NMO_OK) {
        return NMO_ERR_NOMEM;
    }
    return NMO_OK;
}

static nmo_status_t nmo_session_store_included_file(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    int copy_payload,
    const nmo_included_file_metadata_t *meta
) {
    uint32_t meta_attrs = (meta != NULL) ? meta->attributes : 0u;
    const int metadata_only = (meta_attrs & NMO_INCLUDED_FILE_ATTR_METADATA_ONLY) != 0;

    if (session == NULL || name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (size > 0 && data == NULL && !metadata_only) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_included_file_t *entry = NULL;
    if (nmo_arena_array_extend(&session->included_files, 1, (void **)&entry) != NMO_OK || entry == NULL) {
        return NMO_ERR_NOMEM;
    }

    memset(entry, 0, sizeof(*entry));
    if (nmo_arena_array_init(&entry->owner_ids, sizeof(nmo_object_id_t), 0, session->arena) != NMO_OK) {
        (void)nmo_arena_array_pop(&session->included_files, NULL);
        return NMO_ERR_NOMEM;
    }

    size_t name_len = strlen(name);
    char *name_copy = (char *) nmo_arena_alloc(session->arena, name_len + 1, 1);
    if (name_copy == NULL) {
        (void)nmo_arena_array_pop(&session->included_files, NULL);
        return NMO_ERR_NOMEM;
    }
    memcpy(name_copy, name, name_len + 1);

    const void *payload_src = data;
    void *payload = NULL;
    if (size > 0 && !metadata_only && payload_src != NULL) {
        if (copy_payload) {
            payload = nmo_arena_alloc(session->arena, size, 1);
            if (payload == NULL) {
                (void)nmo_arena_array_pop(&session->included_files, NULL);
                return NMO_ERR_NOMEM;
            }
            memcpy(payload, payload_src, size);
        } else {
            payload = (void *) payload_src;
        }
    }

    entry->name = name_copy;
    entry->data = payload;
    entry->size = size;
    uint32_t entry_attributes = copy_payload ? 0u : NMO_INCLUDED_FILE_ATTR_BORROWED;
    if (meta_attrs != 0u) {
        entry_attributes |= meta_attrs;
    }
    entry->attributes = entry_attributes;

    if (meta != NULL) {
        nmo_status_t owner_result = nmo_session_copy_owner_ids(
            session,
            entry,
            meta->owner_ids,
            meta->owner_count);
        if (owner_result != NMO_OK) {
            (void)nmo_arena_array_pop(&session->included_files, NULL);
            return owner_result;
        }
    }

    return NMO_OK;
}

nmo_status_t nmo_session_add_included_file(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size
) {
    return nmo_session_store_included_file(session, name, data, size, 1, NULL);
}

nmo_status_t nmo_session_add_included_file_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta
) {
    return nmo_session_store_included_file(session, name, data, size, 1, meta);
}

nmo_status_t nmo_session_add_included_file_borrowed(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size
) {
    return nmo_session_store_included_file(session, name, data, size, 0, NULL);
}

nmo_status_t nmo_session_add_included_file_borrowed_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta
) {
    return nmo_session_store_included_file(session, name, data, size, 0, meta);
}

nmo_status_t nmo_session_set_included_file_owners(
    nmo_session_t *session,
    uint32_t index,
    const nmo_object_id_t *owner_ids,
    uint32_t owner_count
) {
    if (session == NULL || index >= session->included_files.count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_included_file_t *entry = (nmo_included_file_t *)nmo_arena_array_get(&session->included_files, index);
    if (entry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (owner_ids == NULL || owner_count == 0) {
        nmo_arena_array_clear(&entry->owner_ids);
        return NMO_OK;
    }

    return nmo_session_copy_owner_ids(session, entry, owner_ids, owner_count);
}

nmo_included_file_t *nmo_session_get_included_files(
    const nmo_session_t *session,
    uint32_t *out_count
) {
    if (session == NULL) {
        if (out_count != NULL) {
            *out_count = 0;
        }
        return NULL;
    }

    if (out_count != NULL) {
        *out_count = (uint32_t) session->included_files.count;
    }

    return (nmo_included_file_t *) session->included_files.data;
}

nmo_status_t nmo_session_replace_included_file(
    nmo_session_t *session,
    uint32_t index,
    const void *new_data,
    uint32_t new_size
) {
    if (session == NULL || index >= session->included_files.count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (new_size > 0 && new_data == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_included_file_t *entry = (nmo_included_file_t *)nmo_arena_array_get(
        &session->included_files, index);
    if (entry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Arena-allocate new payload (old data leaks into arena, freed on destroy) */
    void *payload = NULL;
    if (new_size > 0) {
        payload = nmo_arena_alloc(session->arena, new_size, 1);
        if (payload == NULL) {
            return NMO_ERR_NOMEM;
        }
        memcpy(payload, new_data, new_size);
    }

    entry->data = payload;
    entry->size = new_size;
    /* Clear BORROWED flag so save pipeline serializes individually */
    entry->attributes &= ~NMO_INCLUDED_FILE_ATTR_BORROWED;
    return NMO_OK;
}

nmo_status_t nmo_session_remove_included_file(
    nmo_session_t *session,
    uint32_t index
) {
    if (session == NULL || index >= session->included_files.count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t rc = nmo_arena_array_remove(&session->included_files, index, NULL);
    if (rc != NMO_OK) {
        return rc;
    }

    /* Invalidate shadow blob -- it still contains the removed file's data.
     * Without this, the save pipeline's all_borrowed check would pass
     * and write the stale shadow blob verbatim. */
    if (session->shadow_storage != NULL) {
        nmo_shadow_capture_included_files(session->shadow_storage, NULL, 0);
    }

    return NMO_OK;
}

/* High-level convenience API */

/**
 * Load NMO file into session
 */
nmo_session_t *nmo_session_load(nmo_context_t *ctx, const char *filename) {
    if (ctx == NULL || filename == NULL) {
        return NULL;
    }

    /* Create session */
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        return NULL;
    }

    /* Load file using high-level API */
    nmo_status_t result = nmo_session_load_file(session, filename, NULL, NULL);
    if (result != NMO_OK) {
        nmo_session_destroy(session);
        return NULL;
    }

    return session;
}

nmo_status_t nmo_session_execute(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report
) {
    if (session == NULL || request == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (session->partial_load && request->kind != NMO_RUNTIME_OP_LOAD) {
        if (out_report != NULL) {
            memset(out_report, 0, sizeof(*out_report));
            out_report->status = NMO_ERR_INVALID_STATE;
        }
        return NMO_ERR_INVALID_STATE;
    }
    return nmo_runtime_kernel_execute(session, request, out_report);
}

nmo_status_t nmo_session_load_file(
    nmo_session_t *session,
    const char *filename,
    const nmo_load_options_t *options,
    nmo_runtime_report_t *out_report
) {
    if (session == NULL || filename == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_LOAD;
    request.flags = NMO_RUNTIME_REQUEST_DEFAULT;
    request.payload.load.path = filename;
    request.payload.load.options = options;
    return nmo_session_execute(session, &request, out_report);
}

nmo_status_t nmo_session_save_file(
    nmo_session_t *session,
    const char *filename,
    const nmo_save_options_t *options,
    nmo_runtime_report_t *out_report
) {
    if (session == NULL || filename == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_SAVE;
    request.flags = NMO_RUNTIME_REQUEST_DEFAULT;
    request.payload.save.path = filename;
    request.payload.save.options = options;
    return nmo_session_execute(session, &request, out_report);
}

nmo_status_t nmo_session_create_object(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    const char *name,
    nmo_guid_t type_guid,
    nmo_object_id_t *out_created_id,
    nmo_runtime_report_t *out_report
) {
    if (session == NULL || class_id == NMO_CLASS_ID_INVALID) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_CREATE;
    request.flags = NMO_RUNTIME_REQUEST_DEFAULT;
    request.payload.create.class_id = class_id;
    request.payload.create.name = name;
    request.payload.create.type_guid = type_guid;
    request.payload.create.out_created_id = out_created_id;
    return nmo_session_execute(session, &request, out_report);
}

nmo_status_t nmo_session_copy_objects(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report
) {
    if (session == NULL || object_ids == NULL || object_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_COPY;
    request.flags = flags;
    request.payload.copy.ids = object_ids;
    request.payload.copy.count = object_count;
    return nmo_session_execute(session, &request, out_report);
}

nmo_status_t nmo_session_destroy_objects(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_runtime_report_t *out_report
) {
    if (session == NULL || object_ids == NULL || object_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_runtime_request_t request;
    memset(&request, 0, sizeof(request));
    request.kind = NMO_RUNTIME_OP_DELETE;
    request.flags = flags;
    request.payload.destroy.ids = object_ids;
    request.payload.destroy.count = object_count;
    return nmo_session_execute(session, &request, out_report);
}

nmo_status_t nmo_session_preview_destroy(
    nmo_session_t *session,
    const nmo_object_id_t *object_ids,
    size_t object_count,
    uint32_t flags,
    nmo_arena_t *arena,
    nmo_object_id_t **out_expanded_ids,
    size_t *out_expanded_count)
{
    if (session == NULL || object_ids == NULL || object_count == 0 ||
        out_expanded_ids == NULL || out_expanded_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (session->partial_load) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (repo == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_context_t *ctx = nmo_session_get_context(session);
    const nmo_type_runtime_t *type_rt =
        (ctx != NULL) ? nmo_context_get_type_runtime(ctx) : NULL;

    if (arena == NULL) {
        arena = nmo_session_get_arena(session);
    }
    if (arena == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    return nmo_runtime_preview_delete(
        repo, type_rt, arena,
        object_ids, object_count, flags,
        out_expanded_ids, out_expanded_count);
}

/**
 * Get all objects from session
 */
nmo_status_t nmo_session_get_objects(
    nmo_session_t *session,
    nmo_object_t ***out_objects,
    size_t *out_count
) {
    if (session == NULL || out_objects == NULL || out_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_object_repository_t *repo = session->repository;
    if (repo == NULL) {
        *out_objects = NULL;
        *out_count = 0;
        return NMO_OK;
    }

    *out_objects = nmo_object_repository_get_all(repo, out_count);
    return NMO_OK;
}

/**
 * Set object index
 */
void nmo_session_set_object_index(nmo_session_t *session, nmo_object_index_t *index) {
    if (session != NULL) {
        if (session->object_index != NULL && session->object_index != index) {
            nmo_object_index_destroy(session->object_index);
        }
        session->object_index = index;
        nmo_object_repository_set_index(session->repository, index);
    }
}

/**
 * Get object index
 */
nmo_object_index_t *nmo_session_get_object_index(const nmo_session_t *session) {
    return (session != NULL) ? session->object_index : NULL;
}

/**
 * Rebuild object indexes
 */
nmo_status_t nmo_session_rebuild_indexes(nmo_session_t *session, uint32_t flags) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    if (session->object_index == NULL) {
        /* Create index if it doesn't exist */
        session->object_index = nmo_object_index_create(session->repository, session->arena, NULL);
        if (session->object_index == NULL) {
            return NMO_ERR_NOMEM;
        }
        nmo_object_repository_set_index(session->repository, session->object_index);
    }

    if (flags == 0) {
        flags = nmo_object_index_get_active_flags(session->object_index);
        if (flags == 0) {
            flags = NMO_INDEX_BUILD_ALL;
        }
    }
    
    /* Rebuild object index */
    nmo_status_t result = nmo_object_index_rebuild(session->object_index, flags);
    if (result != NMO_OK) {
        return result;
    }

    if (session->object_query_index != NULL) {
        nmo_object_query_index_invalidate(
            session->object_query_index,
            NMO_OBJECT_QUERY_INDEX_ALL);
    }
    return NMO_OK;
}

nmo_status_t nmo_session_get_object_index_stats(
    const nmo_session_t *session,
    nmo_index_stats_t *stats
) {
    if (session == NULL || stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (session->object_index == NULL) {
        memset(stats, 0, sizeof(*stats));
        return NMO_ERR_NOT_FOUND;
    }

    return nmo_object_index_get_stats(session->object_index, stats);
}

void nmo_session_invalidate_object_query(
    nmo_session_t *session,
    uint32_t flags)
{
    if (session == NULL || session->object_query_index == NULL) {
        return;
    }
    nmo_object_query_index_invalidate(session->object_query_index, flags);
}

nmo_status_t nmo_session_get_runtime_load_stats(
    const nmo_session_t *session,
    nmo_runtime_load_stats_t *out_stats
) {
    if (session == NULL || out_stats == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (!session->finish_stats_valid) {
        memset(out_stats, 0, sizeof(*out_stats));
        return NMO_ERR_NOT_FOUND;
    }

    *out_stats = session->finish_stats;
    return NMO_OK;
}

void nmo_session_set_runtime_load_stats(
    nmo_session_t *session,
    const nmo_runtime_load_stats_t *stats
) {
    if (session == NULL || stats == NULL) {
        return;
    }

    session->finish_stats = *stats;
    session->finish_stats_valid = 1;
}

void nmo_session_set_plugin_diagnostics(
    nmo_session_t *session,
    const nmo_session_plugin_dependency_status_t *entries,
    size_t entry_count,
    size_t missing_count,
    size_t outdated_count,
    int plugin_manager_available
) {
    if (session == NULL) {
        return;
    }

    session->plugin_diag.entries = entries;
    session->plugin_diag.entry_count = entry_count;
    session->plugin_diag.missing_count = missing_count;
    session->plugin_diag.outdated_count = outdated_count;
    session->plugin_diag.extension_registry_available = plugin_manager_available ? 1 : 0;
    session->plugin_diag_valid = 1;
}

const nmo_session_plugin_diagnostics_t *nmo_session_get_plugin_diagnostics(
    const nmo_session_t *session
) {
    if (session == NULL || !session->plugin_diag_valid) {
        return NULL;
    }

    return &session->plugin_diag;
}

static int nmo_session_build_plugin_diagnostics(
    nmo_session_t *session,
    const nmo_plugin_dep_t *deps,
    size_t dep_count,
    size_t *out_missing,
    size_t *out_outdated
) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_arena_t *arena = nmo_session_get_arena(session);
    nmo_context_t *ctx = nmo_session_get_context(session);
    nmo_extension_registry_t *ext_registry = ctx != NULL
        ? nmo_context_get_extension_registry(ctx)
        : NULL;
    nmo_behavior_registry_t *bb_registry = ctx != NULL
        ? nmo_context_get_bb_registry(ctx)
        : NULL;

    size_t missing = 0;
    size_t outdated = 0;
    size_t entry_count = 0;
    nmo_session_plugin_dependency_status_t *entries = NULL;

    nmo_arena_array_clear(&session->plugin_diag_entries);
    if (dep_count > 0) {
        if (nmo_arena_array_extend(&session->plugin_diag_entries, dep_count, (void **)&entries) != NMO_OK ||
            entries == NULL) {
            return NMO_ERR_NOMEM;
        }
        memset(entries, 0, dep_count * sizeof(*entries));
    }

    if (deps != NULL && dep_count > 0) {
        for (size_t i = 0; i < dep_count; i++) {
            const nmo_plugin_dep_t *dep = &deps[i];
            if (nmo_guid_is_null(dep->guid)) {
                continue;
            }

            nmo_session_plugin_dependency_status_t *entry = entries ? &entries[entry_count] : NULL;
            entry_count++;

            if (entry != NULL) {
                entry->guid = dep->guid;
                entry->category = (nmo_plugin_category_t) dep->category;
                entry->required_version = dep->version;
            }

            const nmo_extension_plugin_info_t *registered = ext_registry
                ? nmo_extension_registry_find(ext_registry, dep->guid)
                : NULL;
            const nmo_behavior_proto_t *bb_proto = bb_registry
                ? nmo_behavior_registry_find(bb_registry, dep->guid)
                : NULL;

            if (registered == NULL && bb_proto == NULL) {
                missing++;
                if (entry != NULL) {
                    entry->status_flags |= NMO_SESSION_PLUGIN_DEP_STATUS_MISSING;
                    if (ext_registry == NULL) {
                        entry->status_flags |= NMO_SESSION_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE;
                    }
                }
                continue;
            }

            if (entry != NULL) {
                entry->resolved_version = registered != NULL
                    ? registered->version
                    : (bb_proto != NULL ? bb_proto->version : dep->version);
                if (registered != NULL && registered->name != NULL) {
                    entry->resolved_name = (char *)nmo_arena_strdup(arena, registered->name);
                } else if (bb_proto != NULL && bb_proto->name != NULL) {
                    entry->resolved_name = (char *)nmo_arena_strdup(arena, bb_proto->name);
                }
            }

            uint32_t resolved_version = registered != NULL
                ? registered->version
                : (bb_proto != NULL ? bb_proto->version : dep->version);
            if (resolved_version < dep->version) {
                outdated++;
                if (entry != NULL) {
                    entry->status_flags |= NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD;
                }
            }
        }
    }

    nmo_session_set_plugin_diagnostics(
        session,
        entries,
        entry_count,
        missing,
        outdated,
        ext_registry != NULL ? 1 : 0);

    if (out_missing != NULL) {
        *out_missing = missing;
    }
    if (out_outdated != NULL) {
        *out_outdated = outdated;
    }

    return NMO_OK;
}

nmo_status_t nmo_session_refresh_plugin_diagnostics(nmo_session_t *session) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_session_build_plugin_diagnostics(
        session,
        session->file_state.plugin_deps,
        session->file_state.plugin_dep_count,
        NULL,
        NULL);
}

/**
 * Get file header from session
 */
const nmo_header_t *nmo_session_get_header(const nmo_session_t *session) {
    if (session == NULL) {
        return NULL;
    }

    /* Return stored file header (opaque pointer, caller knows the type) */
    return (const nmo_header_t *)session->file_header;
}

/**
 * Set file header (internal use by parser)
 */
void nmo_session_set_file_header(nmo_session_t *session, const void *header, size_t header_size) {
    if (session == NULL || header == NULL || session->arena == NULL || header_size == 0) {
        return;
    }
    
    /* Allocate header in session arena */
    void *stored_header = nmo_arena_alloc(
        session->arena,
        header_size,
        alignof(max_align_t)
    );
    
    if (stored_header != NULL) {
        /* Copy header data */
        memcpy(stored_header, header, header_size);
        session->file_header = stored_header;
        session->file_header_size = header_size;
    }
}

nmo_reference_resolver_t *nmo_session_get_reference_resolver(
    const nmo_session_t *session
) {
    return (session != NULL) ? session->reference_resolver : NULL;
}

nmo_reference_resolver_t *nmo_session_ensure_reference_resolver(
    nmo_session_t *session
) {
    if (session == NULL) {
        return NULL;
    }

    if (session->reference_resolver != NULL) {
        return session->reference_resolver;
    }

    if (session->repository == NULL || session->arena == NULL) {
        return NULL;
    }

    if (session->reference_resolver_arena == NULL) {
        session->reference_resolver_arena = nmo_arena_create(&session->allocator, 4096);
        if (session->reference_resolver_arena == NULL) {
            return NULL;
        }
    }

    nmo_reference_resolver_t *resolver = nmo_reference_resolver_create(
        session->repository,
        session->reference_resolver_arena
    );

    if (resolver != NULL) {
        session->reference_resolver = resolver;
    }

    return resolver;
}

void nmo_session_reset_reference_resolver(nmo_session_t *session) {
    if (session != NULL) {
        if (session->reference_resolver != NULL) {
            nmo_reference_resolver_destroy(session->reference_resolver);
            session->reference_resolver = NULL;
        }
        if (session->reference_resolver_arena != NULL) {
            nmo_arena_destroy(session->reference_resolver_arena);
            session->reference_resolver_arena = NULL;
        }
    }
}

void nmo_session_set_runtime_ops(nmo_session_t *session,
                                 const nmo_runtime_ops_t *ops) {
    if (session && ops) session->runtime_ops = *ops;
}

const nmo_runtime_ops_t *nmo_session_get_runtime_ops(
    const nmo_session_t *session) {
    return session ? &session->runtime_ops : NULL;
}

void nmo_session_internal_set_partial_load(nmo_session_t *session, int partial) {
    if (session != NULL) {
        session->partial_load = partial ? 1 : 0;
    }
}

int nmo_session_has_materialized_load_state(const nmo_session_t *session) {
    if (session == NULL) {
        return 0;
    }
    if (session->partial_load) {
        return 1;
    }
    if (session->file_header != NULL || session->file_header_size != 0) {
        return 1;
    }
    if (session->file_state.info.file_version != 0 ||
        session->file_state.info.file_version2 != 0 ||
        session->file_state.info.ck_version != 0 ||
        session->file_state.info.product_version != 0 ||
        session->file_state.info.product_build != 0 ||
        session->file_state.info.file_size != 0 ||
        session->file_state.info.object_count != 0 ||
        session->file_state.info.manager_count != 0 ||
        session->file_state.info.write_mode != 0) {
        return 1;
    }
    if (session->file_state.manager_data != NULL ||
        session->file_state.manager_data_count != 0 ||
        session->file_state.plugin_deps != NULL ||
        session->file_state.plugin_dep_count != 0) {
        return 1;
    }
    if (session->repository != NULL &&
        nmo_object_repository_get_count(session->repository) > 0) {
        return 1;
    }
    if (session->included_files.count > 0 ||
        session->finish_stats_valid ||
        session->plugin_diag_valid ||
        session->chunk_pool != NULL ||
        (session->shadow_storage != NULL &&
         nmo_shadow_has_included_files(session->shadow_storage))) {
        return 1;
    }
    return 0;
}

int nmo_session_is_partial_load(const nmo_session_t *session) {
    return (session != NULL) ? session->partial_load : 0;
}


