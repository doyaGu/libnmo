#ifndef NMO_PLUGIN_H
#define NMO_PLUGIN_H

/**
 * @file nmo_plugin.h
 * @brief Plugin registration API (Phase 10.2)
 */

#include "type/nmo_plugin_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_plugin_manager nmo_plugin_manager_t;

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
