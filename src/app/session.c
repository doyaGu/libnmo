/**
 * @file session.c
 * @brief Session implementation (Phase 8.2)
 */

#include "app/nmo_session.h"
#include "app/nmo_context.h"
#include "app/nmo_parser.h"
#include "app/nmo_saver.h"
#include "session/nmo_runtime_kernel.h"
#include "session/runtime_kernel_internal.h"
#include "extension/nmo_extension_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_allocator.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_index.h"
#include "session/nmo_reference_resolver.h"
#include "session/nmo_id_sanitizer.h"
#include "object/nmo_shadow_storage.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk_pool.h"
#include "format/nmo_header1.h"
#include "app/nmo_behavior_index.h"
#include <stddef.h>
#include <string.h>

#define DEFAULT_ARENA_SIZE (1024 * 1024)  /* 1 MB */
#define DEFAULT_CHUNK_POOL_CAPACITY 128

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

    /* Behavior ownership index (built after load) */
    nmo_behavior_index_t *behavior_index;
} nmo_session_t;

/**
 * Create session
 */
nmo_session_t *nmo_session_create(nmo_context_t *ctx) {
    if (ctx == NULL) {
        return NULL;
    }

    nmo_context_retain(ctx);

    nmo_allocator_t *ctx_allocator = nmo_context_get_allocator(ctx);
    nmo_allocator_t allocator = (ctx_allocator != NULL) ? *ctx_allocator : nmo_allocator_default();

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

nmo_behavior_index_t *nmo_session_get_behavior_index(const nmo_session_t *session) {
    return session ? session->behavior_index : NULL;
}

void nmo_session_build_behavior_index(nmo_session_t *session) {
    if (!session || !session->context || session->behavior_index != NULL)
        return;
    session->behavior_index = nmo_behavior_index_create(session->arena);
    if (session->behavior_index != NULL) {
        nmo_behavior_index_build(session->behavior_index, session->context, session);
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

int nmo_session_set_file_info(nmo_session_t *session, const nmo_file_info_t *info) {
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
int nmo_session_set_plugin_dependencies(
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

static int nmo_session_copy_owner_ids(
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

static int nmo_session_store_included_file(
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
        int owner_result = nmo_session_copy_owner_ids(
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

int nmo_session_add_included_file(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size
) {
    return nmo_session_store_included_file(session, name, data, size, 1, NULL);
}

int nmo_session_add_included_file_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta
) {
    return nmo_session_store_included_file(session, name, data, size, 1, meta);
}

int nmo_session_add_included_file_borrowed(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size
) {
    return nmo_session_store_included_file(session, name, data, size, 0, NULL);
}

int nmo_session_add_included_file_borrowed_ex(
    nmo_session_t *session,
    const char *name,
    const void *data,
    uint32_t size,
    const nmo_included_file_metadata_t *meta
) {
    return nmo_session_store_included_file(session, name, data, size, 0, meta);
}

int nmo_session_set_included_file_owners(
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
    int result = nmo_session_load_file(session, filename, NULL, NULL);
    if (result != NMO_OK) {
        nmo_session_destroy(session);
        return NULL;
    }

    return session;
}

/**
 * Save session to NMO file
 */
int nmo_session_save(nmo_session_t *session, const char *filename) {
    if (session == NULL || filename == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_session_save_file(session, filename, NULL, NULL);
}

int nmo_session_execute(
    nmo_session_t *session,
    const nmo_runtime_request_t *request,
    nmo_runtime_report_t *out_report
) {
    return nmo_runtime_kernel_execute(session, request, out_report);
}

int nmo_session_load_file(
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

int nmo_session_save_file(
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

int nmo_session_create_object(
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

int nmo_session_copy_objects(
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

int nmo_session_destroy_objects(
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

int nmo_session_preview_destroy(
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

    return runtime_kernel_preview_delete(
        repo, type_rt, arena,
        object_ids, object_count, flags,
        out_expanded_ids, out_expanded_count);
}

/**
 * Get all objects from session
 */
int nmo_session_get_objects(
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
int nmo_session_rebuild_indexes(nmo_session_t *session, uint32_t flags) {
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
    return nmo_object_index_rebuild(session->object_index, flags);
}

int nmo_session_get_object_index_stats(
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

int nmo_session_get_runtime_load_stats(
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

    size_t missing = 0;
    size_t outdated = 0;
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
            nmo_session_plugin_dependency_status_t *entry = entries ? &entries[i] : NULL;

            if (entry != NULL) {
                entry->guid = dep->guid;
                entry->category = (nmo_plugin_category_t) dep->category;
                entry->required_version = dep->version;
            }

            const nmo_extension_plugin_info_t *registered = ext_registry
                ? nmo_extension_registry_find(ext_registry, dep->guid)
                : NULL;

            if (registered == NULL) {
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
                entry->resolved_version = registered->version;
                if (registered->name != NULL) {
                    entry->resolved_name = (char *)nmo_arena_strdup(arena, registered->name);
                }
            }

            if (registered->version < dep->version) {
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
        dep_count,
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

int nmo_session_refresh_plugin_diagnostics(nmo_session_t *session) {
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

/* ==================== Object Query API (Phase 5) ==================== */

/**
 * Find object by name
 */
nmo_object_t *nmo_session_find_by_name(
    nmo_session_t *session,
    const char *name,
    nmo_class_id_t class_id
) {
    if (session == NULL || name == NULL) {
        return NULL;
    }
    
    /* Use index if available */
    if (session->object_index != NULL) {
        return nmo_object_index_find_by_name(session->object_index, name, class_id);
    }
    
    /* Fall back to repository linear search */
    return nmo_object_repository_find_by_name(session->repository, name);
}

/**
 * Find object by GUID
 */
nmo_object_t *nmo_session_find_by_guid(
    nmo_session_t *session,
    nmo_guid_t guid
) {
    if (session == NULL) {
        return NULL;
    }
    
    /* Use index if available */
    if (session->object_index != NULL) {
        return nmo_object_index_find_by_guid(session->object_index, guid);
    }
    
    /* Fall back to repository linear search */
    size_t count;
    nmo_object_t **objects = nmo_object_repository_get_all(session->repository, &count);
    
    for (size_t i = 0; i < count; i++) {
        if (nmo_guid_equals(objects[i]->type_guid, guid)) {
            return objects[i];
        }
    }
    
    return NULL;
}

/**
 * Get all objects of a specific class
 */
nmo_object_t **nmo_session_get_objects_by_class(
    nmo_session_t *session,
    nmo_class_id_t class_id,
    size_t *out_count
) {
    if (session == NULL || out_count == NULL) {
        if (out_count != NULL) {
            *out_count = 0;
        }
        return NULL;
    }
    
    /* Use index if available */
    if (session->object_index != NULL) {
        return nmo_object_index_get_by_class(session->object_index, class_id, out_count);
    }
    
    /* Fall back to repository search */
    return nmo_object_repository_find_by_class(session->repository, class_id, out_count);
}

/**
 * Count objects of a specific class
 */
size_t nmo_session_count_objects_by_class(
    nmo_session_t *session,
    nmo_class_id_t class_id
) {
    if (session == NULL) {
        return 0;
    }
    
    size_t count = 0;
    nmo_session_get_objects_by_class(session, class_id, &count);
    return count;
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
