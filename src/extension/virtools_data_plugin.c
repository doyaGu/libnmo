/**
 * @file virtools_data_plugin.c
 * @brief Built-in extension that loads Virtools data from JSON
 *
 * Loads parameter types, operation signatures, and BB prototypes from
 * JSON files produced by VirtoolsDataExporter. Registered as a static
 * extension during context creation.
 */

#include "extension/nmo_extension_abi.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_host.h"
#include "app/nmo_virtools_loader.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_operation_registry nmo_operation_registry_t;
typedef struct nmo_bb_registry nmo_bb_registry_t;

/* Internal accessors — implemented in extension_registry.c */
extern nmo_type_registry_t *nmo_extension_registry_get_type_registry(
    nmo_extension_registry_t *registry);
extern nmo_operation_registry_t *nmo_extension_registry_get_operation_registry(
    nmo_extension_registry_t *registry);
extern nmo_bb_registry_t *nmo_extension_registry_get_bb_registry(
    nmo_extension_registry_t *registry);

static nmo_status_t virtools_data_init(
    const nmo_extension_host_t *host,
    void *host_user)
{
    (void)host;
    nmo_extension_host_context_t *ctx =
        (nmo_extension_host_context_t *)host_user;

    const char *data_dir =
        (const char *)nmo_extension_registry_get_user_data(ctx->registry);
    if (!data_dir) return NMO_OK;

    nmo_type_registry_t *type_reg =
        nmo_extension_registry_get_type_registry(ctx->registry);
    nmo_operation_registry_t *op_reg =
        nmo_extension_registry_get_operation_registry(ctx->registry);
    nmo_bb_registry_t *bb_reg =
        nmo_extension_registry_get_bb_registry(ctx->registry);

    return nmo_virtools_load_data_dir(type_reg, op_reg, bb_reg, data_dir);
}

static const nmo_extension_plugin_t g_virtools_data_plugin = {
    .abi_version   = NMO_EXTENSION_ABI_VERSION,
    .struct_size   = sizeof(nmo_extension_plugin_t),
    .guid          = {0x4E4D4F56, 0x54444154},
    .version       = 0x010000u,
    .category      = NMO_PLUGIN_MANAGER_DLL,
    .name          = "Virtools Data",
    .init          = virtools_data_init,
    .shutdown      = NULL,
};

const nmo_extension_plugin_t *nmo_virtools_data_plugin_get(void)
{
    return &g_virtools_data_plugin;
}
