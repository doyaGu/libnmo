#ifndef NMO_PLUGIN_H
#define NMO_PLUGIN_H

/**
 * @file nmo_plugin.h
 * @brief Plugin registration API (Phase 10.2)
 */

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_shared_library.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_plugin_manager nmo_plugin_manager_t;
typedef struct nmo_plugin nmo_plugin_t;
typedef struct nmo_context nmo_context_t;
typedef struct nmo_session nmo_session_t;
typedef struct nmo_manager nmo_manager_t;
typedef struct nmo_manager_descriptor nmo_manager_descriptor_t;
typedef struct nmo_schema_registry nmo_schema_registry_t;

typedef int (*nmo_plugin_init_fn)(const nmo_plugin_t *plugin, nmo_context_t *ctx);
typedef void (*nmo_plugin_shutdown_fn)(const nmo_plugin_t *plugin, nmo_context_t *ctx);
typedef int (*nmo_plugin_register_managers_fn)(
    const nmo_plugin_t *plugin,
    nmo_manager_descriptor_t *registry,
    size_t registry_capacity,
    size_t *out_registered_count);

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
} nmo_plugin_instance_info_t;

typedef struct nmo_plugin_dependency {
    nmo_plugin_category_t category;
    nmo_guid_t guid;
    uint32_t version;
} nmo_plugin_dependency_t;

#define NMO_PLUGIN_INSTANCE_FLAG_OWNS_LIBRARY 0x00000001u

typedef const nmo_plugin_t *(*nmo_plugin_query_fn)(size_t *out_count);

NMO_API nmo_plugin_manager_t *nmo_plugin_manager_create(nmo_context_t *ctx);
NMO_API void nmo_plugin_manager_destroy(nmo_plugin_manager_t *manager);
NMO_API nmo_context_t *nmo_plugin_manager_get_context(const nmo_plugin_manager_t *manager);

NMO_API int nmo_plugin_manager_register(
    nmo_plugin_manager_t *manager,
    const nmo_plugin_registration_desc_t *desc);

NMO_API int nmo_plugin_manager_load_library(
    nmo_plugin_manager_t *manager,
    const char *path,
    const char *symbol_name);

NMO_API const nmo_plugin_instance_info_t *nmo_plugin_manager_get_plugins(
    const nmo_plugin_manager_t *manager,
    size_t *out_count);

NMO_API const nmo_plugin_t *nmo_plugin_manager_find_by_guid(
    const nmo_plugin_manager_t *manager,
    nmo_guid_t guid);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PLUGIN_H */