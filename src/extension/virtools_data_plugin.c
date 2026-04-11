/**
 * @file virtools_data_plugin.c
 * @brief Built-in extension that registers Virtools types and operations
 *
 * This is an internal static plugin registered by context.c during
 * context creation.  It calls the existing builtin registration helpers
 * through the extension host API so that contributions are tracked and
 * can be introspected like any other extension.
 */

#include "extension/nmo_extension_abi.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

/* Plugin GUID: "NMOV" "TDAT" */
#define VIRTOOLS_DATA_PLUGIN_GUID NMO_GUID(0x4E4D4F56, 0x54444154)

static nmo_status_t virtools_data_init(
    const nmo_extension_host_t *host,
    void *host_user)
{
    /*
     * This built-in plugin currently acts as a sentinel.  The actual
     * type, operation, and object-type registrations are performed
     * directly by context.c before this plugin is registered, because
     * the host API today only supports signature-only operation
     * descriptors (no function pointer pass-through).
     *
     * Once the host API gains full operation-function forwarding the
     * registrations will move here so they can be properly tracked as
     * extension contributions.
     */
    (void)host;
    (void)host_user;
    return NMO_OK;
}

static const nmo_extension_plugin_t g_virtools_data_plugin = {
    .abi_version   = NMO_EXTENSION_ABI_VERSION,
    .struct_size   = sizeof(nmo_extension_plugin_t),
    .guid          = {0x4E4D4F56, 0x54444154},  /* VIRTOOLS_DATA_PLUGIN_GUID */
    .version       = 0x010000u,                   /* v1.0.0 */
    .category      = NMO_PLUGIN_MANAGER_DLL,
    .name          = "Virtools Data",
    .init          = virtools_data_init,
    .shutdown      = NULL,
};

const nmo_extension_plugin_t *nmo_virtools_data_plugin_get(void)
{
    return &g_virtools_data_plugin;
}
