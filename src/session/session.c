/**
 * @file session.c
 * @brief The session: what one loaded file holds (repository, file state,
 *        included files, indexes, caches) and the accessors of that state
 *
 * Creating and destroying a session, and the operations that run through the
 * runtime kernel or build the behavior acceleration, are in
 * src/runtime/document_workspace.c.
 */

#include "session_internal.h"
#include "extension/nmo_behavior_registry.h"
#include "extension/nmo_extension_registry.h"
#include "core/nmo_utils.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_query.h"
#include "object/nmo_shadow_storage.h"
#include "object/nmo_ref_graph.h"
#include "object/nmo_manager_guids.h"
#include "format/nmo_data.h"
#include "format/nmo_chunk_pool.h"
#include "format/nmo_header1.h"
#include "object/nmo_edit_flags.h"

#include <string.h>

#define DEFAULT_CHUNK_POOL_CAPACITY 128
#define REF_GRAPH_ARENA_SIZE (64 * 1024)

static nmo_status_t nmo_session_build_plugin_diagnostics(
    nmo_session_t *session,
    const nmo_plugin_dep_t *deps,
    size_t dep_count,
    size_t *out_missing,
    size_t *out_outdated);

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
    /* Reset arena rather than destroy - avoids alloc/free churn and pointer
     * reuse issues.  The arena is destroyed in nmo_session_destroy(). */
    if (session->ref_graph_arena != NULL) {
        nmo_arena_reset(session->ref_graph_arena);
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

nmo_status_t nmo_session_set_plugin_dependencies(
    nmo_session_t *session,
    nmo_plugin_dep_t *deps,
    uint32_t count
) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (count > 0 && deps == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_status_t result = nmo_session_build_plugin_diagnostics(
        session, deps, count, NULL, NULL);
    if (result != NMO_OK) {
        return result;
    }

    session->file_state.plugin_deps = count > 0 ? deps : NULL;
    session->file_state.plugin_dep_count = count;
    return NMO_OK;
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

static bool nmo_session_plugin_category_matches(
    nmo_plugin_category_t required,
    nmo_plugin_category_t registered)
{
    return required == NMO_PLUGIN_CUSTOM_DLL || required == registered;
}

const nmo_session_plugin_diagnostics_t *nmo_session_get_plugin_diagnostics(
    const nmo_session_t *session
) {
    if (session == NULL || !session->plugin_diag_valid) {
        return NULL;
    }

    return &session->plugin_diag;
}

static nmo_status_t nmo_session_build_plugin_diagnostics(
    nmo_session_t *session,
    const nmo_plugin_dep_t *deps,
    size_t dep_count,
    size_t *out_missing,
    size_t *out_outdated
) {
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (dep_count > 0 && deps == NULL) {
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
    for (size_t i = 0; i < dep_count; i++) {
        if (!nmo_guid_is_null(deps[i].guid)) {
            entry_count++;
        }
    }

    size_t entries_size = 0;
    nmo_session_plugin_dependency_status_t *entries = NULL;
    if (entry_count > 0) {
        if (!nmo_safe_mul_size(
                entry_count, sizeof(*entries), &entries_size)) {
            return NMO_ERR_NOMEM;
        }
        entries = nmo_alloc(
            &session->allocator,
            entries_size,
            alignof(nmo_session_plugin_dependency_status_t));
        if (entries == NULL) {
            return NMO_ERR_NOMEM;
        }
        memset(entries, 0, entries_size);
    }

    size_t entry_index = 0;
    for (size_t i = 0; i < dep_count; i++) {
        const nmo_plugin_dep_t *dep = &deps[i];
        if (nmo_guid_is_null(dep->guid)) {
            continue;
        }

        nmo_session_plugin_dependency_status_t *entry = &entries[entry_index++];

        entry->guid = dep->guid;
        entry->category = (nmo_plugin_category_t) dep->category;
        entry->required_version = dep->version;

        const nmo_extension_plugin_info_t *registered = ext_registry
            ? nmo_extension_registry_find(ext_registry, dep->guid)
            : NULL;
        const nmo_behavior_proto_t *bb_proto = bb_registry
            ? nmo_behavior_registry_find(bb_registry, dep->guid)
            : NULL;
        bool registered_matches =
            registered != NULL &&
            nmo_session_plugin_category_matches(
                (nmo_plugin_category_t)dep->category,
                registered->category);
        bool behavior_matches =
            bb_proto != NULL &&
            nmo_session_plugin_category_matches(
                (nmo_plugin_category_t)dep->category,
                NMO_PLUGIN_BEHAVIOR_DLL);

        if (!registered_matches && !behavior_matches) {
            missing++;
            entry->status_flags |= NMO_SESSION_PLUGIN_DEP_STATUS_MISSING;
            if (ext_registry == NULL) {
                entry->status_flags |= NMO_SESSION_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE;
            }
            continue;
        }

        entry->resolved_version = registered_matches
            ? registered->version
            : bb_proto->version;
        if (registered_matches && registered->name != NULL) {
            entry->resolved_name = registered->name;
        } else if (behavior_matches && bb_proto->name != NULL) {
            entry->resolved_name = bb_proto->name;
        }

        uint32_t resolved_version = registered_matches
            ? registered->version
            : bb_proto->version;
        if (resolved_version < dep->version) {
            outdated++;
            entry->status_flags |= NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD;
        }
    }

    nmo_status_t reserve_result = nmo_arena_array_reserve(
        &session->plugin_diag_entries, entry_count);
    if (reserve_result != NMO_OK) {
        nmo_free(&session->allocator, entries);
        return reserve_result;
    }

    for (size_t i = 0; i < entry_count; i++) {
        if (entries[i].resolved_name != NULL) {
            const char *stored_name = nmo_arena_strdup(
                arena, entries[i].resolved_name);
            if (stored_name == NULL) {
                nmo_free(&session->allocator, entries);
                return NMO_ERR_NOMEM;
            }
            entries[i].resolved_name = stored_name;
        }
    }

    if (entry_count > 0) {
        memcpy(session->plugin_diag_entries.data, entries, entries_size);
    }
    session->plugin_diag_entries.count = entry_count;
    nmo_free(&session->allocator, entries);

    const nmo_session_plugin_dependency_status_t *published_entries =
        entry_count > 0
            ? (const nmo_session_plugin_dependency_status_t *)
                  session->plugin_diag_entries.data
            : NULL;
    nmo_session_set_plugin_diagnostics(
        session,
        published_entries,
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
nmo_status_t nmo_session_set_file_header(
    nmo_session_t *session,
    const void *header,
    size_t header_size) {
    if (session == NULL || header == NULL || session->arena == NULL || header_size == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Allocate header in session arena */
    void *stored_header = nmo_arena_alloc(
        session->arena,
        header_size,
        NMO_MAX_ALIGN
    );
    
    if (stored_header == NULL) {
        return NMO_ERR_NOMEM;
    }

    /* Publish only after the complete header has been copied. */
    memcpy(stored_header, header, header_size);
    session->file_header = stored_header;
    session->file_header_size = header_size;
    return NMO_OK;
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

nmo_status_t nmo_runtime_apply_edit_flags(nmo_session_t *session, uint32_t flags)
{
    if (session == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if ((flags & ~NMO_WORKSPACE_EDIT_KNOWN_FLAGS) != 0u) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (nmo_session_is_partial_load(session)) {
        return NMO_ERR_INVALID_STATE;
    }
    if ((flags & NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH) != 0u) {
        nmo_session_invalidate_behavior_index(session);
        nmo_session_invalidate_ref_graph(session);
    } else if ((flags & NMO_WORKSPACE_EDIT_REFERENCES) != 0u) {
        nmo_session_invalidate_ref_graph(session);
    }
    if ((flags & NMO_WORKSPACE_EDIT_NAMES) != 0u) {
        nmo_session_invalidate_object_query(session, NMO_OBJECT_QUERY_INDEX_NAMES);
        return nmo_session_rebuild_indexes(session, NMO_INDEX_BUILD_ALL);
    }
    if ((flags & NMO_WORKSPACE_EDIT_BEHAVIOR_GRAPH) != 0u) {
        nmo_session_invalidate_object_query(
            session,
            NMO_OBJECT_QUERY_INDEX_MEMBERSHIP);
    }
    if ((flags & NMO_WORKSPACE_EDIT_RESOURCES) != 0u) {
        /* Resource edits affect save output. No resource-derived query cache exists yet. */
    }
    return NMO_OK;
}
