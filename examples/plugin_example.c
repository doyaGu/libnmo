/**
 * @file plugin_example.c
 * @brief Example demonstrating plugin registration (types + managers)
 */

#include "nmo.h"
#include "app/nmo_plugin.h"
#include "type/type_system.h"
#include "format/nmo_manager_registry.h"
#include "core/nmo_guid.h"
#include <stdio.h>
#include <stdalign.h>

#define DEMO_MANAGER_ID 1001u

static const nmo_guid_t DEMO_PLUGIN_GUID = {0xA1B2C3D4u, 0xEEFF1122u};
static const nmo_guid_t DEMO_MANAGER_GUID = {0x0BADBEEFu, 0x12345678u};
static const nmo_guid_t DEMO_TYPE_GUID = {0xDEADBEEFu, 0xCAFEBABEu};

typedef struct demo_payload {
    int32_t value;
    float weight;
} demo_payload_t;

static int demo_manager_pre_load(void *session, void *user_data) {
    (void)session;
    (void)user_data;
    printf("[demo_manager] pre-load hook\n");
    return NMO_OK;
}

static int demo_manager_post_load(void *session, void *user_data) {
    (void)session;
    (void)user_data;
    printf("[demo_manager] post-load hook\n");
    return NMO_OK;
}

static int demo_manager_pre_save(void *session, void *user_data) {
    (void)session;
    (void)user_data;
    printf("[demo_manager] pre-save hook\n");
    return NMO_OK;
}

static int demo_manager_post_save(void *session, void *user_data) {
    (void)session;
    (void)user_data;
    printf("[demo_manager] post-save hook\n");
    return NMO_OK;
}

static int demo_plugin_init(const nmo_plugin_t *plugin, nmo_context_t *ctx) {
    if (plugin == NULL || ctx == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    nmo_type_descriptor_t desc = {0};
    desc.guid = DEMO_TYPE_GUID;
    desc.name = "DemoPayload";
    desc.category = (uint16_t)(NMO_TYPE_CATEGORY_STRUCT | NMO_TYPE_CATEGORY_PLUGIN_BASE);
    desc.flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_POD;
    desc.size = (uint32_t)sizeof(demo_payload_t);
    desc.alignment = (uint32_t)alignof(demo_payload_t);
    desc.creator_plugin = plugin;
    desc.valid = true;

    return nmo_type_registry_register(registry, &desc);
}

static int demo_plugin_register_managers(
    const nmo_plugin_t *plugin,
    nmo_manager_descriptor_t *registry,
    size_t registry_capacity,
    size_t *out_registered_count) {
    if (out_registered_count == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (registry == NULL || registry_capacity == 0) {
        *out_registered_count = 1;
        return NMO_OK;
    }

    if (registry_capacity < 1) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_manager_descriptor_t *entry = &registry[0];
    entry->manager_id = DEMO_MANAGER_ID;
    entry->guid = DEMO_MANAGER_GUID;
    entry->name = "DemoManager";
    entry->category = plugin ? plugin->category : NMO_PLUGIN_CUSTOM_DLL;
    entry->pre_load = demo_manager_pre_load;
    entry->post_load = demo_manager_post_load;
    entry->pre_save = demo_manager_pre_save;
    entry->post_save = demo_manager_post_save;
    entry->user_data = NULL;

    *out_registered_count = 1;
    return NMO_OK;
}

static const nmo_plugin_t *demo_plugin_get_info(size_t *out_count) {
    static const nmo_plugin_t plugin = {
        .name = "DemoPlugin",
        .version = 0x00010000,
        .guid = DEMO_PLUGIN_GUID,
        .category = NMO_PLUGIN_CUSTOM_DLL,
        .init = demo_plugin_init,
        .shutdown = NULL,
        .register_managers = demo_plugin_register_managers
    };

    if (out_count != NULL) {
        *out_count = 1;
    }

    return &plugin;
}

int main(void) {
    printf("=== Plugin Example (Types + Managers) ===\n\n");

    nmo_logger_t logger = nmo_logger_stderr();
    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,
        .logger = &logger,
        .thread_pool_size = 0
    };

    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    nmo_plugin_manager_t *plugin_manager = nmo_context_get_plugin_manager(ctx);
    if (plugin_manager == NULL) {
        fprintf(stderr, "Plugin manager unavailable\n");
        nmo_context_release(ctx);
        return 1;
    }

    size_t plugin_count = 0;
    const nmo_plugin_t *plugin = demo_plugin_get_info(&plugin_count);
    nmo_plugin_registration_desc_t reg_desc = {
        .plugins = plugin,
        .plugin_count = plugin_count
    };

    int reg_result = nmo_plugin_manager_register(plugin_manager, &reg_desc);
    if (reg_result != NMO_OK) {
        fprintf(stderr, "Plugin registration failed: %s\n", nmo_error_string(reg_result));
        nmo_context_release(ctx);
        return 1;
    }

    nmo_manager_registry_t *manager_registry = nmo_context_get_manager_registry(ctx);
    uint32_t manager_count = nmo_manager_registry_get_count(manager_registry);
    printf("Registered managers: %u\n", manager_count);

    nmo_type_registry_t *type_registry = nmo_context_get_type_registry(ctx);
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(type_registry, DEMO_TYPE_GUID);
    if (type != NULL) {
        printf("Registered type: %s (size=%u)\n", type->name, type->size);
    } else {
        printf("Registered type not found\n");
    }

    nmo_context_release(ctx);
    return 0;
}
