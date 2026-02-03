#ifndef NMO_PLUGIN_TYPES_H
#define NMO_PLUGIN_TYPES_H

/**
 * @file nmo_plugin_types.h
 * @brief Plugin type definitions shared across layers
 */

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_shared_library.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_plugin nmo_plugin_t;
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_manager nmo_manager_t;
typedef struct nmo_manager_descriptor nmo_manager_descriptor_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef int (*nmo_plugin_init_fn)(const nmo_plugin_t *plugin, nmo_context_t *ctx);
typedef void (*nmo_plugin_shutdown_fn)(const nmo_plugin_t *plugin, nmo_context_t *ctx);
/**
 * @brief Register managers provided by a plugin.
 *
 * Implementations should support a discovery call where @p registry is NULL
 * and @p registry_capacity is 0. In that case, write the required manager
 * count to @p out_registered_count and return NMO_OK without writing any
 * descriptors. The plugin manager will then allocate the array and call
 * again to populate descriptors.
 */
typedef int (*nmo_plugin_register_managers_fn)(
    const nmo_plugin_t *plugin,
    nmo_manager_descriptor_t *registry,
    size_t registry_capacity,
    size_t *out_registered_count);

/**
 * @brief Manager registration descriptor provided by plugins.
 *
 * Plugins populate an array of these descriptors during
 * @ref nmo_plugin_register_managers_fn. The plugin manager will
 * instantiate and register each manager into the context registry.
 */
typedef struct nmo_manager_descriptor {
    nmo_manager_id_t manager_id;
    nmo_guid_t guid;
    const char *name;
    nmo_plugin_category_t category;

    int (*pre_load)(void *session, void *user_data);
    int (*post_load)(void *session, void *user_data);
    int (*load_data)(void *session, const nmo_chunk_t *chunk, void *user_data);
    nmo_chunk_t *(*save_data)(void *session, void *user_data);
    int (*pre_save)(void *session, void *user_data);
    int (*post_save)(void *session, void *user_data);

    void *user_data;
} nmo_manager_descriptor_t;

typedef struct nmo_plugin {
    const char *name;
    uint32_t version;
    nmo_guid_t guid;
    nmo_plugin_category_t category;
    nmo_plugin_init_fn init;
    nmo_plugin_shutdown_fn shutdown;
    nmo_plugin_register_managers_fn register_managers;
} nmo_plugin_t;

typedef struct nmo_plugin_registration_desc {
    const nmo_plugin_t *plugins;
    size_t plugin_count;
} nmo_plugin_registration_desc_t;

typedef struct nmo_plugin_instance_info {
    const nmo_plugin_t *plugin;
    nmo_shared_library_t *library;
    uint32_t flags;
    const nmo_manager_id_t *manager_ids;
    size_t manager_id_count;
} nmo_plugin_instance_info_t;

typedef struct nmo_plugin_dependency {
    nmo_plugin_category_t category;
    nmo_guid_t guid;
    uint32_t version;
} nmo_plugin_dependency_t;

#define NMO_PLUGIN_INSTANCE_FLAG_OWNS_LIBRARY 0x00000001u
#define NMO_PLUGIN_INSTANCE_FLAG_INITIALIZED 0x00000002u
#define NMO_PLUGIN_INSTANCE_FLAG_MANAGERS_REGISTERED 0x00000004u

typedef const nmo_plugin_t *(*nmo_plugin_query_fn)(size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PLUGIN_TYPES_H */
