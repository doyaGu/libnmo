/**
 * @file extension_host.c
 * @brief Extension host API implementation
 */

#include "extension/nmo_extension_host.h"
#include "extension/nmo_extension_registry.h"
#include "type/nmo_type_system.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "core/nmo_arena.h"
#include <string.h>

/* ============================================================================
 * Forward Declarations for Registry Internals
 * ============================================================================ */

/* Access type and manager registries from extension registry */
extern nmo_type_registry_t *nmo_extension_registry_get_type_registry(
    nmo_extension_registry_t *registry);
extern nmo_manager_registry_t *nmo_extension_registry_get_manager_registry(
    nmo_extension_registry_t *registry);

/* ============================================================================
 * Host API Implementation Functions
 * ============================================================================ */

static nmo_status_t host_register_managers(
    void *host_user,
    nmo_guid_t plugin_guid,
    const nmo_extension_manager_desc_t *descs,
    size_t desc_count)
{
    (void)plugin_guid;  /* For now, just for logging/tracking */

    if (host_user == NULL || descs == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "host_user and descs are required");
    }

    nmo_extension_host_context_t *ctx = (nmo_extension_host_context_t *)host_user;

    for (size_t i = 0; i < desc_count; i++) {
        const nmo_extension_manager_desc_t *desc = &descs[i];

        nmo_manager_t *manager = nmo_manager_create(desc->guid, desc->name, desc->category);
        if (manager == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                "Failed to create manager instance");
        }

        (void)nmo_manager_set_user_data(manager, desc->user_data);
        (void)nmo_manager_set_pre_load_hook(manager, desc->pre_load);
        (void)nmo_manager_set_post_load_hook(manager, desc->post_load);
        (void)nmo_manager_set_load_data_hook(manager, desc->load_data);
        (void)nmo_manager_set_save_data_hook(manager, desc->save_data);
        (void)nmo_manager_set_pre_save_hook(manager, desc->pre_save);
        (void)nmo_manager_set_post_save_hook(manager, desc->post_save);

        /* Ensure capacity for tracking */
        if (ctx->manager_id_count >= ctx->manager_id_capacity) {
            size_t new_cap = ctx->manager_id_capacity == 0 ? 8 : ctx->manager_id_capacity * 2;
            uint32_t *new_ids = nmo_arena_alloc(ctx->plugin_arena, new_cap * sizeof(uint32_t), alignof(uint32_t));
            if (new_ids == NULL) {
                nmo_manager_destroy(manager);
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                    "Failed to allocate manager ID tracking");
            }
            if (ctx->manager_ids && ctx->manager_id_count > 0) {
                memcpy(new_ids, ctx->manager_ids, ctx->manager_id_count * sizeof(uint32_t));
            }
            ctx->manager_ids = new_ids;
            ctx->manager_id_capacity = new_cap;
        }

        /* Register the manager */
        nmo_manager_registry_t *manager_registry = nmo_extension_registry_get_manager_registry(ctx->registry);
        nmo_status_t status = nmo_manager_registry_register(
            manager_registry,
            desc->manager_id,
            manager);

        if (status != NMO_OK) {
            nmo_manager_destroy(manager);
            return status;
        }

        ctx->manager_ids[ctx->manager_id_count++] = desc->manager_id;
    }

    NMO_RETURN_OK();
}

static nmo_status_t host_register_types(
    void *host_user,
    nmo_guid_t plugin_guid,
    const nmo_extension_type_desc_t *descs,
    size_t desc_count)
{
    if (host_user == NULL || descs == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "host_user and descs are required");
    }

    nmo_extension_host_context_t *ctx = (nmo_extension_host_context_t *)host_user;

    for (size_t i = 0; i < desc_count; i++) {
        const nmo_extension_type_desc_t *desc = &descs[i];

        /* Ensure capacity for tracking */
        if (ctx->type_guid_count >= ctx->type_guid_capacity) {
            size_t new_cap = ctx->type_guid_capacity == 0 ? 8 : ctx->type_guid_capacity * 2;
            nmo_guid_t *new_guids = nmo_arena_alloc(ctx->plugin_arena, new_cap * sizeof(nmo_guid_t), alignof(nmo_guid_t));
            if (new_guids == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                    "Failed to allocate type GUID tracking");
            }
            if (ctx->type_guids && ctx->type_guid_count > 0) {
                memcpy(new_guids, ctx->type_guids, ctx->type_guid_count * sizeof(nmo_guid_t));
            }
            ctx->type_guids = new_guids;
            ctx->type_guid_capacity = new_cap;
        }

        /* Build type descriptor for the type registry */
        nmo_type_descriptor_t type_desc = {0};
        type_desc.guid = desc->guid;
        type_desc.size = desc->size;
        type_desc.category = (uint16_t)desc->category;
        type_desc.flags = (uint16_t)desc->flags;
        type_desc.class_id = desc->class_id;
        type_desc.creator_plugin_guid = plugin_guid;  /* Stamp creator */

        /* Deep-copy name into plugin arena */
        if (desc->name) {
            size_t name_len = strlen(desc->name);
            char *name_copy = nmo_arena_alloc(ctx->plugin_arena, name_len + 1, 1);
            if (name_copy == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                    "Failed to copy type name");
            }
            memcpy(name_copy, desc->name, name_len + 1);
            type_desc.name = name_copy;
        }

        /* Set parent type GUID if specified */
        if (!nmo_guid_is_null(desc->parent_guid)) {
            type_desc.base_type = desc->parent_guid;
        }

        /* Register the type */
        nmo_type_registry_t *type_registry = nmo_extension_registry_get_type_registry(ctx->registry);
        nmo_status_t status = nmo_type_registry_register(type_registry, &type_desc);
        if (status != NMO_OK) {
            return status;
        }

        ctx->type_guids[ctx->type_guid_count++] = desc->guid;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Static Host API Table
 * ============================================================================ */

static const nmo_extension_host_t s_host_api = {
    .abi_version = NMO_EXTENSION_ABI_VERSION,
    .struct_size = sizeof(nmo_extension_host_t),
    .register_managers = host_register_managers,
    .register_types = host_register_types,
};

const nmo_extension_host_t *nmo_extension_host_get_api(void)
{
    return &s_host_api;
}

/* ============================================================================
 * Host Context Management
 * ============================================================================ */

nmo_status_t nmo_extension_host_context_init(
    nmo_extension_host_context_t *ctx,
    nmo_extension_registry_t *registry,
    nmo_arena_t *plugin_arena,
    nmo_guid_t plugin_guid)
{
    if (ctx == NULL || registry == NULL || plugin_arena == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "ctx, registry, and plugin_arena are required");
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->registry = registry;
    ctx->plugin_arena = plugin_arena;
    ctx->plugin_guid = plugin_guid;

    NMO_RETURN_OK();
}

void nmo_extension_host_context_cleanup(nmo_extension_host_context_t *ctx)
{
    if (ctx == NULL) return;

    /* Arrays are arena-allocated, no need to free individually */
    memset(ctx, 0, sizeof(*ctx));
}

nmo_status_t nmo_extension_host_context_rollback(nmo_extension_host_context_t *ctx)
{
    if (ctx == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
            "ctx is required");
    }

    /* Unregister types in reverse order (derived first) */
    nmo_type_registry_t *type_registry = nmo_extension_registry_get_type_registry(ctx->registry);
    for (size_t i = ctx->type_guid_count; i > 0; i--) {
        nmo_type_registry_unregister(type_registry, ctx->type_guids[i - 1]);
    }

    /* Unregister managers in reverse order */
    nmo_manager_registry_t *manager_registry = nmo_extension_registry_get_manager_registry(ctx->registry);
    for (size_t i = ctx->manager_id_count; i > 0; i--) {
        nmo_manager_registry_unregister(manager_registry, ctx->manager_ids[i - 1]);
    }

    ctx->type_guid_count = 0;
    ctx->manager_id_count = 0;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Registry Accessor Functions (called from extension_registry.c)
 * ============================================================================ */

/* These need to be defined here since extension_registry.c calls them */
/* In a real implementation, these would be inline or in a shared internal header */

/* Defined in extension_registry.c - we need accessor functions */
