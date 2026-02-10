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
typedef struct nmo_extension_instance {
    /** Public info (for list/find queries) */
    nmo_extension_plugin_info_t info;

    /** Plugin arena (owns all deep-copied strings and contribution arrays) */
    nmo_arena_t *arena;

    /** Original init callback (stored for reference, not called after init) */
    nmo_status_t (*init)(const nmo_extension_host_t *host, void *host_user);

    /** Shutdown callback (called during unload) */
    void (*shutdown)(const nmo_extension_host_t *host, void *host_user);

    /** Shared library handle (NULL for static plugins) */
    nmo_shared_library_t *library;

    /** Registered manager IDs (for rollback) */
    uint32_t *manager_ids;
    size_t manager_id_count;

    /** Registered type GUIDs (for rollback) */
    nmo_guid_t *type_guids;
    size_t type_guid_count;
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

    /** Type registry for type contributions */
    nmo_type_registry_t *type_registry;

    /** Manager registry for manager contributions */
    nmo_manager_registry_t *manager_registry;
};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static nmo_status_t register_single_plugin(
    nmo_extension_registry_t *registry,
    const nmo_extension_plugin_t *plugin,
    nmo_shared_library_t *library,
    const char *library_path);

static void unload_instance(
    nmo_extension_registry_t *registry,
    nmo_extension_instance_t *instance);

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

/* ============================================================================
 * Registry Lifecycle
 * ============================================================================ */

nmo_extension_registry_t *nmo_extension_registry_create(
    nmo_allocator_t *allocator,
    nmo_type_registry_t *type_registry,
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

    return registry;
}

void nmo_extension_registry_destroy(nmo_extension_registry_t *registry)
{
    if (registry == NULL) return;

    if (registry->type_registry) {
        (void)nmo_type_registry_begin_update(registry->type_registry);
    }

    /* Unload all plugins in reverse order */
    while (registry->instances.count > 0) {
        nmo_extension_instance_t *instance = *(nmo_extension_instance_t **)
            nmo_array_get(&registry->instances, registry->instances.count - 1);
        unload_instance(registry, instance);
    }

    /* Clean up containers */
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
        NMO_RETURN_OK();
    }

    /* Track how many we successfully registered for rollback */
    size_t registered_count = 0;

    for (size_t i = 0; i < plugin_count; i++) {
        const nmo_extension_plugin_t *plugin = &plugins[i];

        /* Validate ABI compatibility */
        if (!nmo_extension_plugin_is_compatible(plugin)) {
            NMO_SET_LAST_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR,
                "Plugin ABI version mismatch (got %u, expected %u)",
                plugin->abi_version, NMO_EXTENSION_ABI_VERSION);
            goto rollback;
        }

        /* Check for duplicate GUID */
        nmo_extension_instance_t *existing = NULL;
        if (nmo_hash_table_get(registry->guid_map, &plugin->guid, &existing) == NMO_OK) {
            NMO_SET_LAST_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                "Plugin GUID {%08x-%08x} already registered",
                plugin->guid.d1, plugin->guid.d2);
            goto rollback;
        }

        /* Register the plugin */
        nmo_status_t status = register_single_plugin(registry, plugin, NULL, NULL);
        if (status != NMO_OK) {
            goto rollback;
        }

        registered_count++;
    }

    return nmo_type_registry_finalize(registry->type_registry);

rollback:
    /* Roll back successfully registered plugins in reverse order */
    for (size_t i = registered_count; i > 0; i--) {
        nmo_guid_t guid = plugins[i - 1].guid;
        nmo_extension_registry_unload_by_guid(registry, guid);
    }
    return nmo_last_error_code();
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

    /* Load library and resolve query function */
    nmo_status_t status = nmo_extension_loader_open(
        &registry->allocator,
        path,
        symbol_name,
        &library,
        &query_fn);

    if (status != NMO_OK) {
        return status;
    }

    /* Query plugins */
    const nmo_extension_plugin_t *plugins = NULL;
    size_t plugin_count = 0;

    status = nmo_extension_loader_query(query_fn, &plugins, &plugin_count);
    if (status != NMO_OK) {
        nmo_extension_loader_close(library);
        return status;
    }

    if (plugin_count == 0) {
        nmo_extension_loader_close(library);
        NMO_RETURN_OK();
    }

    /* Register each plugin */
    size_t registered_count = 0;

    for (size_t i = 0; i < plugin_count; i++) {
        const nmo_extension_plugin_t *plugin = &plugins[i];

        /* Validate ABI compatibility */
        if (!nmo_extension_plugin_is_compatible(plugin)) {
            NMO_SET_LAST_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR,
                "Plugin ABI version mismatch in %s (got %u, expected %u)",
                path, plugin->abi_version, NMO_EXTENSION_ABI_VERSION);
            goto rollback;
        }

        /* Check for duplicate GUID */
        nmo_extension_instance_t *existing = NULL;
        if (nmo_hash_table_get(registry->guid_map, &plugin->guid, &existing) == NMO_OK) {
            NMO_SET_LAST_ERROR(NMO_ERR_ALREADY_EXISTS, NMO_SEVERITY_ERROR,
                "Plugin GUID {%08x-%08x} from %s already registered",
                plugin->guid.d1, plugin->guid.d2, path);
            goto rollback;
        }

        /* Register the plugin - first plugin owns the library */
        status = register_single_plugin(
            registry,
            plugin,
            (i == 0) ? library : NULL,  /* Only first plugin owns the library */
            path);

        if (status != NMO_OK) {
            goto rollback;
        }

        registered_count++;
    }

    return nmo_type_registry_finalize(registry->type_registry);

rollback:
    /* Roll back successfully registered plugins in reverse order */
    for (size_t i = registered_count; i > 0; i--) {
        nmo_guid_t guid = plugins[i - 1].guid;
        nmo_extension_registry_unload_by_guid(registry, guid);
    }

    /* Close library if no plugins were registered */
    if (registered_count == 0) {
        nmo_extension_loader_close(library);
    }

    return nmo_last_error_code();
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
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
            "Plugin GUID {%08x-%08x} not found",
            guid.d1, guid.d2);
    }

    unload_instance(registry, instance);
    return nmo_type_registry_finalize(registry->type_registry);
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

    /* Build info array from instances */
    /* Note: This could be cached for better performance */
    nmo_extension_instance_t *first = *(nmo_extension_instance_t **)
        nmo_array_get(&registry->instances, 0);
    return &first->info;
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
    nmo_shared_library_t *library,
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
    instance->library = library;
    instance->init = plugin->init;
    instance->shutdown = plugin->shutdown;

    /* Deep-copy metadata into arena */
    instance->info.guid = plugin->guid;
    instance->info.version = plugin->version;
    instance->info.category = plugin->category;
    instance->info.flags = library ? NMO_EXTENSION_FLAG_DYNAMIC : NMO_EXTENSION_FLAG_NONE;

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
        instance->info.manager_count = host_ctx.manager_id_count;
        instance->info.type_count = host_ctx.type_guid_count;

        instance->info.flags |= NMO_EXTENSION_FLAG_INITIALIZED;

        /* Don't call cleanup - we transferred ownership */
    }

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
        instance->shutdown(host_api, NULL);
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
    if (instance->library) {
        nmo_extension_loader_close(instance->library);
    }

    /* 5. Destroy plugin arena (frees all deep copies) */
    nmo_arena_destroy(instance->arena);

    /* 6. Free instance record */
    registry->allocator.free(registry->allocator.user_data, instance);
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
