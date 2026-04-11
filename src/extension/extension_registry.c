/**
 * @file extension_registry.c
 * @brief Extension registry implementation
 */

#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_host.h"
#include "extension/nmo_extension_loader.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_shared_library.h"
#include "core/nmo_string.h"
#include "type/nmo_type_system.h"
#include "format/nmo_manager_registry.h"
#include <string.h>

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * @brief Internal plugin instance record
 *
 * Owned by the registry. Contains deep-copied metadata and contribution tracking.
 */
typedef struct nmo_extension_library_ref {
    nmo_shared_library_t *handle;
    size_t ref_count;
    nmo_allocator_t allocator;
} nmo_extension_library_ref_t;

typedef struct nmo_extension_instance {
    /** Public info (for list/find queries) */
    nmo_extension_plugin_info_t info;

    /** Plugin arena (owns all deep-copied strings and contribution arrays) */
    nmo_arena_t *arena;

    /** Original init callback (stored for reference, not called after init) */
    nmo_status_t (*init)(const nmo_extension_host_t *host, void *host_user);

    /** Shutdown callback (called during unload) */
    void (*shutdown)(const nmo_extension_host_t *host, void *host_user);

    /** Shared-library ownership record (NULL for static plugins) */
    nmo_extension_library_ref_t *library_ref;

    /** Host context persisted from init() and passed to shutdown() */
    nmo_extension_host_context_t host_ctx;
    bool has_host_ctx;

    /** Registered manager IDs (for rollback) */
    uint32_t *manager_ids;
    size_t manager_id_count;

    /** Registered type GUIDs (for rollback) */
    nmo_guid_t *type_guids;
    size_t type_guid_count;

    /** Registered operation GUIDs (for tracking) */
    nmo_guid_t *operation_guids;
    size_t operation_guid_count;
} nmo_extension_instance_t;

/**
 * @brief Extension registry structure
 */
struct nmo_extension_registry {
    /** Allocator for registry structures */
    nmo_allocator_t allocator;

    /** GUID -> instance hash table */
    nmo_hash_table_t *guid_map;

    /** Ordered list of instances (for iteration) */
    nmo_array_t instances;

    /** Cached contiguous list payload for nmo_extension_registry_list() */
    nmo_array_t info_cache;
    bool info_cache_dirty;

    /** Type registry for type contributions */
    nmo_type_registry_t *type_registry;

    /** Operation registry for operation contributions */
    nmo_operation_registry_t *operation_registry;

    /** BB registry for building block contributions (behavior layer) */
    nmo_bb_registry_t *bb_registry;

    /** Manager registry for manager contributions */
    nmo_manager_registry_t *manager_registry;

    /** Opaque user data (e.g. data_dir path for built-in extensions) */
    void *user_data;
};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static nmo_status_t register_single_plugin(
    nmo_extension_registry_t *registry,
    const nmo_extension_plugin_t *plugin,
    nmo_extension_library_ref_t *library_ref,
    const char *library_path);

static void unload_instance(
    nmo_extension_registry_t *registry,
    nmo_extension_instance_t *instance);

static nmo_status_t finalize_update(
    nmo_extension_registry_t *registry,
    nmo_status_t status);

static nmo_extension_library_ref_t *library_ref_create(
    nmo_extension_registry_t *registry,
    nmo_shared_library_t *library);

static void library_ref_retain(nmo_extension_library_ref_t *ref);
static void library_ref_release(nmo_extension_library_ref_t *ref);

static void mark_list_dirty(nmo_extension_registry_t *registry);
static nmo_status_t rebuild_list_cache(nmo_extension_registry_t *registry);

static size_t guid_hash_func(const void *key, size_t key_size);
static int guid_compare_func(const void *a, const void *b, size_t key_size);

/* ============================================================================
 * Registry Accessor Functions (for extension_host.c)
 * ============================================================================ */

nmo_type_registry_t *nmo_extension_registry_get_type_registry(
    nmo_extension_registry_t *registry)
{
    return registry ? registry->type_registry : NULL;
}

nmo_manager_registry_t *nmo_extension_registry_get_manager_registry(
    nmo_extension_registry_t *registry)
{
    return registry ? registry->manager_registry : NULL;
}

nmo_operation_registry_t *nmo_extension_registry_get_operation_registry(
    nmo_extension_registry_t *registry)
{
    return registry ? registry->operation_registry : NULL;
}

nmo_bb_registry_t *nmo_extension_registry_get_bb_registry(
    nmo_extension_registry_t *registry)
{
    return registry ? registry->bb_registry : NULL;
}

void nmo_extension_registry_set_user_data(
    nmo_extension_registry_t *registry,
    void *user_data)
{
    if (registry != NULL) {
        registry->user_data = user_data;
    }
}

void *nmo_extension_registry_get_user_data(
    const nmo_extension_registry_t *registry)
{
    return registry ? registry->user_data : NULL;
}

/* ============================================================================
 * Registry Lifecycle
 * ============================================================================ */

nmo_extension_registry_t *nmo_extension_registry_create(
    nmo_allocator_t *allocator,
    nmo_type_registry_t *type_registry,
    nmo_operation_registry_t *operation_registry,
    nmo_bb_registry_t *bb_registry,
    nmo_manager_registry_t *manager_registry)
{
    if (type_registry == NULL || manager_registry == NULL) {
        NMO_SET_LAST_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "type_registry and manager_registry are required");
        return NULL;
    }

    nmo_allocator_t alloc = allocator ? *allocator : nmo_allocator_default();

    nmo_extension_registry_t *registry = alloc.alloc(
        alloc.user_data, sizeof(nmo_extension_registry_t), alignof(nmo_extension_registry_t));
    if (registry == NULL) {
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
            "Failed to allocate extension registry");
        return NULL;
    }

    memset(registry, 0, sizeof(*registry));
    registry->allocator = alloc;
    registry->type_registry = type_registry;
    registry->operation_registry = operation_registry;
    registry->bb_registry = bb_registry;
    registry->manager_registry = manager_registry;

    /* Create GUID -> instance hash table */
    registry->guid_map = nmo_hash_table_create(
        &alloc,
        sizeof(nmo_guid_t),
        sizeof(nmo_extension_instance_t *),
        16,
        guid_hash_func,
        guid_compare_func);

    if (registry->guid_map == NULL) {
        alloc.free(alloc.user_data, registry);
        NMO_SET_LAST_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
            "Failed to allocate extension GUID map");
        return NULL;
    }

    /* Initialize instance array */
    nmo_status_t status = nmo_array_init(
        &registry->instances,
        sizeof(nmo_extension_instance_t *),
        16,
        &alloc);

    if (status != NMO_OK) {
        nmo_hash_table_destroy(registry->guid_map);
        alloc.free(alloc.user_data, registry);
        return NULL;
    }

    status = nmo_array_init(
        &registry->info_cache,
        sizeof(nmo_extension_plugin_info_t),
        16,
        &alloc);
    if (status != NMO_OK) {
        nmo_array_dispose(&registry->instances);
        nmo_hash_table_destroy(registry->guid_map);
        alloc.free(alloc.user_data, registry);
        return NULL;
    }
    registry->info_cache_dirty = true;

    return registry;
}

void nmo_extension_registry_destroy(nmo_extension_registry_t *registry)
{
    if (registry == NULL) return;

    bool began_update = false;
    if (registry->type_registry) {
        began_update = (nmo_type_registry_begin_update(registry->type_registry) == NMO_OK);
    }

    /* Unload all plugins in reverse order */
    while (registry->instances.count > 0) {
        nmo_extension_instance_t *instance = *(nmo_extension_instance_t **)
            nmo_array_get(&registry->instances, registry->instances.count - 1);
        unload_instance(registry, instance);
    }
    if (began_update) {
        (void)nmo_type_registry_finalize(registry->type_registry);
    }

    /* Clean up containers */
    nmo_array_dispose(&registry->info_cache);
    nmo_hash_table_destroy(registry->guid_map);
    nmo_array_dispose(&registry->instances);

    /* Free registry */
    registry->allocator.free(registry->allocator.user_data, registry);
}

/* ============================================================================
 * Static Registration
 * ============================================================================ */

nmo_status_t nmo_extension_registry_register_static(
    nmo_extension_registry_t *registry,
    const nmo_extension_plugin_t *plugins,
    size_t plugin_count)
{
    if (registry == NULL || plugins == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "registry and plugins are required");
    }

    nmo_status_t update_status = nmo_type_registry_begin_update(registry->type_registry);
    if (update_status != NMO_OK) {
        return update_status;
    }

    if (plugin_count == 0) {
        return finalize_update(registry, NMO_OK);
    }

    /* Track how many we successfully registered for rollback */
    size_t registered_count = 0;
    nmo_status_t status = NMO_OK;

    for (size_t i = 0; i < plugin_count; i++) {
        const nmo_extension_plugin_t *plugin = &plugins[i];

        /* Validate ABI compatibility */
        if (!nmo_extension_plugin_is_compatible(plugin)) {
            NMO_SET_LAST_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR,
                "Plugin ABI version mismatch (got %u, expected %u)",
                plugin->abi_version, NMO_EXTENSION_ABI_VERSION);
            status = NMO_ERR_UNSUPPORTED_VERSION;
            goto rollback;
        }

        /* Check for duplicate GUID */
        nmo_extension_instance_t *existing = NULL;
        if (nmo_hash_table_get(registry->guid_map, &plugin->guid, &existing) == NMO_OK) {
            NMO_SET_LAST_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                "Plugin GUID {%08x-%08x} already registered",
                plugin->guid.d1, plugin->guid.d2);
            status = NMO_ERR_ALREADY_EXISTS;
            goto rollback;
        }

        /* Register the plugin */
        status = register_single_plugin(registry, plugin, NULL, NULL);
        if (status != NMO_OK) {
            goto rollback;
        }

        registered_count++;
    }

    return finalize_update(registry, NMO_OK);

rollback:
    /* Roll back successfully registered plugins in reverse order */
    for (size_t i = registered_count; i > 0; i--) {
        nmo_guid_t guid = plugins[i - 1].guid;
        nmo_extension_instance_t *instance = NULL;
        if (nmo_hash_table_get(registry->guid_map, &guid, &instance) == NMO_OK) {
            unload_instance(registry, instance);
        }
    }
    {
        nmo_status_t failure_status = status;
        if (failure_status == NMO_OK) {
            failure_status = (nmo_status_t)nmo_last_error_code();
        }
        return finalize_update(registry, failure_status);
    }
}

/* ============================================================================
 * Dynamic Loading
 * ============================================================================ */

nmo_status_t nmo_extension_registry_load_library(
    nmo_extension_registry_t *registry,
    const char *path,
    const char *symbol_name)
{
    if (registry == NULL || path == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "registry and path are required");
    }

    nmo_status_t update_status = nmo_type_registry_begin_update(registry->type_registry);
    if (update_status != NMO_OK) {
        return update_status;
    }

    nmo_shared_library_t *library = NULL;
    nmo_extension_query_fn query_fn = NULL;
    nmo_extension_library_ref_t *library_ref = NULL;

    /* Load library and resolve query function */
    nmo_status_t status = nmo_extension_loader_open(
        &registry->allocator,
        path,
        symbol_name,
        &library,
        &query_fn);

    if (status != NMO_OK) {
        return finalize_update(registry, status);
    }

    /* Query plugins */
    const nmo_extension_plugin_t *plugins = NULL;
    size_t plugin_count = 0;

    status = nmo_extension_loader_query(query_fn, &plugins, &plugin_count);
    if (status != NMO_OK) {
        nmo_extension_loader_close(library);
        return finalize_update(registry, status);
    }

    library_ref = library_ref_create(registry, library);
    if (library_ref == NULL) {
        nmo_extension_loader_close(library);
        return finalize_update(registry, NMO_ERR_NOMEM);
    }

    if (plugin_count == 0) {
        nmo_extension_loader_close(library_ref->handle);
        registry->allocator.free(registry->allocator.user_data, library_ref);
        return finalize_update(registry, NMO_OK);
    }

    /* Register each plugin */
    size_t registered_count = 0;
    status = NMO_OK;

    for (size_t i = 0; i < plugin_count; i++) {
        const nmo_extension_plugin_t *plugin = &plugins[i];

        /* Validate ABI compatibility */
        if (!nmo_extension_plugin_is_compatible(plugin)) {
            NMO_SET_LAST_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR,
                "Plugin ABI version mismatch in %s (got %u, expected %u)",
                path, plugin->abi_version, NMO_EXTENSION_ABI_VERSION);
            status = NMO_ERR_UNSUPPORTED_VERSION;
            goto rollback;
        }

        /* Check for duplicate GUID */
        nmo_extension_instance_t *existing = NULL;
        if (nmo_hash_table_get(registry->guid_map, &plugin->guid, &existing) == NMO_OK) {
            NMO_SET_LAST_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                "Plugin GUID {%08x-%08x} from %s already registered",
                plugin->guid.d1, plugin->guid.d2, path);
            status = NMO_ERR_ALREADY_EXISTS;
            goto rollback;
        }

        /* Register plugin instance against the shared library ref-count record. */
        status = register_single_plugin(
            registry,
            plugin,
            library_ref,
            path);

        if (status != NMO_OK) {
            goto rollback;
        }

        registered_count++;
    }

    return finalize_update(registry, NMO_OK);

rollback:
    /* Roll back successfully registered plugins in reverse order */
    for (size_t i = registered_count; i > 0; i--) {
        nmo_guid_t guid = plugins[i - 1].guid;
        nmo_extension_instance_t *instance = NULL;
        if (nmo_hash_table_get(registry->guid_map, &guid, &instance) == NMO_OK) {
            unload_instance(registry, instance);
        }
    }

    /* Close library if no plugins were registered */
    if (registered_count == 0) {
        nmo_extension_loader_close(library_ref->handle);
        registry->allocator.free(registry->allocator.user_data, library_ref);
    }

    {
        nmo_status_t failure_status = status;
        if (failure_status == NMO_OK) {
            failure_status = (nmo_status_t)nmo_last_error_code();
        }
        return finalize_update(registry, failure_status);
    }
}

/* ============================================================================
 * Unloading
 * ============================================================================ */

nmo_status_t nmo_extension_registry_unload_by_guid(
    nmo_extension_registry_t *registry,
    nmo_guid_t guid)
{
    if (registry == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "registry is required");
    }

    nmo_status_t update_status = nmo_type_registry_begin_update(registry->type_registry);
    if (update_status != NMO_OK) {
        return update_status;
    }

    /* Find the instance */
    nmo_extension_instance_t *instance = NULL;
    if (nmo_hash_table_get(registry->guid_map, &guid, &instance) != NMO_OK) {
        NMO_SET_LAST_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
            "Plugin GUID {%08x-%08x} not found",
            guid.d1, guid.d2);
        return finalize_update(registry, NMO_ERR_NOT_FOUND);
    }

    unload_instance(registry, instance);
    return finalize_update(registry, NMO_OK);
}

/* ============================================================================
 * Queries
 * ============================================================================ */

const nmo_extension_plugin_info_t *nmo_extension_registry_list(
    const nmo_extension_registry_t *registry,
    size_t *out_count)
{
    if (registry == NULL || out_count == NULL) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    *out_count = registry->instances.count;

    if (registry->instances.count == 0) {
        return NULL;
    }

    nmo_extension_registry_t *mutable_registry = (nmo_extension_registry_t *)registry;
    if (rebuild_list_cache(mutable_registry) != NMO_OK) {
        *out_count = 0;
        return NULL;
    }
    return (const nmo_extension_plugin_info_t *)nmo_array_data(&registry->info_cache);
}

const nmo_extension_plugin_info_t *nmo_extension_registry_find(
    const nmo_extension_registry_t *registry,
    nmo_guid_t guid)
{
    if (registry == NULL) {
        return NULL;
    }

    nmo_extension_instance_t *instance = NULL;
    if (nmo_hash_table_get(registry->guid_map, &guid, &instance) != NMO_OK) {
        return NULL;
    }

    return &instance->info;
}

size_t nmo_extension_registry_get_count(const nmo_extension_registry_t *registry)
{
    if (registry == NULL) {
        return 0;
    }
    return registry->instances.count;
}

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

static nmo_status_t register_single_plugin(
    nmo_extension_registry_t *registry,
    const nmo_extension_plugin_t *plugin,
    nmo_extension_library_ref_t *library_ref,
    const char *library_path)
{
    nmo_allocator_t *alloc = &registry->allocator;

    /* Create plugin arena for deep copies */
    nmo_arena_t *arena = nmo_arena_create(alloc, 4096);
    if (arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
            "Failed to create plugin arena");
    }

    /* Allocate instance record */
    nmo_extension_instance_t *instance = alloc->alloc(
        alloc->user_data, sizeof(nmo_extension_instance_t), alignof(nmo_extension_instance_t));
    if (instance == NULL) {
        nmo_arena_destroy(arena);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
            "Failed to allocate plugin instance");
    }

    memset(instance, 0, sizeof(*instance));
    instance->arena = arena;
    instance->library_ref = NULL;
    instance->has_host_ctx = false;
    instance->init = plugin->init;
    instance->shutdown = plugin->shutdown;

    /* Deep-copy metadata into arena */
    instance->info.guid = plugin->guid;
    instance->info.version = plugin->version;
    instance->info.category = plugin->category;
    instance->info.flags = library_ref ? NMO_EXTENSION_FLAG_DYNAMIC : NMO_EXTENSION_FLAG_NONE;

    /* Copy name */
    if (plugin->name) {
        size_t name_len = strlen(plugin->name);
        char *name_copy = nmo_arena_alloc(arena, name_len + 1, 1);
        if (name_copy == NULL) {
            alloc->free(alloc->user_data, instance);
            nmo_arena_destroy(arena);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                "Failed to copy plugin name");
        }
        memcpy(name_copy, plugin->name, name_len + 1);
        instance->info.name = name_copy;
    }

    /* Copy library path */
    if (library_path) {
        size_t path_len = strlen(library_path);
        char *path_copy = nmo_arena_alloc(arena, path_len + 1, 1);
        if (path_copy == NULL) {
            alloc->free(alloc->user_data, instance);
            nmo_arena_destroy(arena);
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                "Failed to copy library path");
        }
        memcpy(path_copy, library_path, path_len + 1);
        instance->info.library_path = path_copy;
    }

    /* Add to registry containers */
    nmo_status_t status = nmo_hash_table_insert(registry->guid_map, &plugin->guid, &instance);
    if (status != NMO_OK) {
        alloc->free(alloc->user_data, instance);
        nmo_arena_destroy(arena);
        return status;
    }

    status = nmo_array_append(&registry->instances, &instance);
    if (status != NMO_OK) {
        nmo_hash_table_remove(registry->guid_map, &plugin->guid);
        alloc->free(alloc->user_data, instance);
        nmo_arena_destroy(arena);
        return status;
    }

    /* Initialize the plugin */
    if (plugin->init) {
        nmo_extension_host_context_t host_ctx;
        status = nmo_extension_host_context_init(&host_ctx, registry, arena, plugin->guid);
        if (status != NMO_OK) {
            nmo_array_pop(&registry->instances, NULL);
            nmo_hash_table_remove(registry->guid_map, &plugin->guid);
            alloc->free(alloc->user_data, instance);
            nmo_arena_destroy(arena);
            return status;
        }

        const nmo_extension_host_t *host_api = nmo_extension_host_get_api();
        status = plugin->init(host_api, &host_ctx);

        if (status != NMO_OK) {
            /* Roll back contributions */
            nmo_extension_host_context_rollback(&host_ctx);
            nmo_extension_host_context_cleanup(&host_ctx);
            nmo_array_pop(&registry->instances, NULL);
            nmo_hash_table_remove(registry->guid_map, &plugin->guid);
            alloc->free(alloc->user_data, instance);
            nmo_arena_destroy(arena);
            return status;
        }

        /* Transfer contribution tracking from host context to instance */
        instance->manager_ids = host_ctx.manager_ids;
        instance->manager_id_count = host_ctx.manager_id_count;
        instance->type_guids = host_ctx.type_guids;
        instance->type_guid_count = host_ctx.type_guid_count;
        instance->operation_guids = host_ctx.operation_guids;
        instance->operation_guid_count = host_ctx.operation_guid_count;
        instance->info.manager_count = host_ctx.manager_id_count;
        instance->info.type_count = host_ctx.type_guid_count;
        instance->info.operation_count = host_ctx.operation_guid_count;

        instance->info.flags |= NMO_EXTENSION_FLAG_INITIALIZED;

        instance->host_ctx = host_ctx;
        instance->has_host_ctx = true;
        /* Don't call cleanup - ownership is transferred to the instance. */
    }

    if (library_ref != NULL) {
        library_ref_retain(library_ref);
        instance->library_ref = library_ref;
    }

    mark_list_dirty(registry);

    NMO_RETURN_OK();
}

static void unload_instance(
    nmo_extension_registry_t *registry,
    nmo_extension_instance_t *instance)
{
    if (instance == NULL) return;

    /* 1. Call shutdown callback while DLL is still loaded */
    if (instance->shutdown && (instance->info.flags & NMO_EXTENSION_FLAG_INITIALIZED)) {
        const nmo_extension_host_t *host_api = nmo_extension_host_get_api();
        instance->shutdown(host_api, instance->has_host_ctx ? &instance->host_ctx : NULL);
    }

    /* 2. Unregister contributions by GUID */
    /* Unregister types first (derived-first would need type registry support) */
    for (size_t i = instance->type_guid_count; i > 0; i--) {
        nmo_type_registry_unregister(registry->type_registry, instance->type_guids[i - 1]);
    }

    /* Unregister managers */
    for (size_t i = instance->manager_id_count; i > 0; i--) {
        nmo_manager_registry_unregister(registry->manager_registry, instance->manager_ids[i - 1]);
    }

    /* 3. Remove from registry containers */
    nmo_hash_table_remove(registry->guid_map, &instance->info.guid);

    /* Find and remove from instances array */
    for (size_t i = 0; i < registry->instances.count; i++) {
        nmo_extension_instance_t *ptr = *(nmo_extension_instance_t **)
            nmo_array_get(&registry->instances, i);
        if (ptr == instance) {
            nmo_array_remove(&registry->instances, i, NULL);
            break;
        }
    }

    /* 4. Close shared library (if dynamic) */
    if (instance->library_ref) {
        library_ref_release(instance->library_ref);
        instance->library_ref = NULL;
    }

    if (instance->has_host_ctx) {
        nmo_extension_host_context_cleanup(&instance->host_ctx);
        instance->has_host_ctx = false;
    }

    /* 5. Destroy plugin arena (frees all deep copies) */
    nmo_arena_destroy(instance->arena);

    /* 6. Free instance record */
    registry->allocator.free(registry->allocator.user_data, instance);
    mark_list_dirty(registry);
}

static nmo_status_t finalize_update(
    nmo_extension_registry_t *registry,
    nmo_status_t status)
{
    nmo_status_t finalize_status = nmo_type_registry_finalize(registry->type_registry);
    if (status != NMO_OK) {
        return status;
    }
    return finalize_status;
}

static nmo_extension_library_ref_t *library_ref_create(
    nmo_extension_registry_t *registry,
    nmo_shared_library_t *library)
{
    if (registry == NULL || library == NULL) {
        return NULL;
    }

    nmo_extension_library_ref_t *ref = registry->allocator.alloc(
        registry->allocator.user_data,
        sizeof(*ref),
        alignof(nmo_extension_library_ref_t));
    if (ref == NULL) {
        return NULL;
    }

    ref->handle = library;
    ref->ref_count = 0;
    ref->allocator = registry->allocator;
    return ref;
}

static void library_ref_retain(nmo_extension_library_ref_t *ref)
{
    if (ref == NULL) {
        return;
    }
    ref->ref_count++;
}

static void library_ref_release(nmo_extension_library_ref_t *ref)
{
    if (ref == NULL) {
        return;
    }
    if (ref->ref_count == 0) {
        return;
    }

    ref->ref_count--;
    if (ref->ref_count == 0) {
        nmo_extension_loader_close(ref->handle);
        ref->allocator.free(ref->allocator.user_data, ref);
    }
}

static void mark_list_dirty(nmo_extension_registry_t *registry)
{
    if (registry != NULL) {
        registry->info_cache_dirty = true;
    }
}

static nmo_status_t rebuild_list_cache(nmo_extension_registry_t *registry)
{
    if (registry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (!registry->info_cache_dirty) {
        return NMO_OK;
    }

    nmo_array_clear(&registry->info_cache);
    for (size_t i = 0; i < registry->instances.count; i++) {
        nmo_extension_instance_t *instance = *(nmo_extension_instance_t **)
            nmo_array_get(&registry->instances, i);
        nmo_status_t status = nmo_array_append(&registry->info_cache, &instance->info);
        if (status != NMO_OK) {
            nmo_array_clear(&registry->info_cache);
            NMO_SET_LAST_ERROR(status, NMO_SEVERITY_ERROR,
                "Failed to build plugin info list cache");
            return status;
        }
    }

    registry->info_cache_dirty = false;
    return NMO_OK;
}

static size_t guid_hash_func(const void *key, size_t key_size)
{
    (void)key_size;
    const nmo_guid_t *guid = (const nmo_guid_t *)key;
    return (size_t)nmo_guid_hash(*guid);
}

static int guid_compare_func(const void *a, const void *b, size_t key_size)
{
    (void)key_size;
    const nmo_guid_t *ga = (const nmo_guid_t *)a;
    const nmo_guid_t *gb = (const nmo_guid_t *)b;
    return nmo_guid_equals(*ga, *gb) ? 0 : 1;
}
