#include "app/nmo_plugin.h"
#include "app/nmo_context.h"
#include "core/nmo_allocator.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_shared_library.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "format/nmo_manager_registry.h"
#include "format/nmo_manager.h"
#include "type/type_system.h"
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>

struct nmo_plugin_manager {
    nmo_context_t *context;
    nmo_allocator_t *allocator;
    nmo_arena_t *arena;
    nmo_arena_array_t instances;
};

static int nmo_plugin_manager_reserve(nmo_plugin_manager_t *manager, size_t needed) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (manager->instances.capacity >= needed) {
        return NMO_OK;
    }

    nmo_status_t result = nmo_arena_array_reserve(&manager->instances, needed);
    return result;
}

static int nmo_plugin_manager_find_index_by_guid(const nmo_plugin_manager_t *manager, nmo_guid_t guid) {
    if (manager == NULL) {
        return -1;
    }

    nmo_plugin_instance_info_t *entries = (nmo_plugin_instance_info_t *)manager->instances.data;
    if (entries == NULL) {
        return -1;
    }

    for (size_t i = 0; i < manager->instances.count; ++i) {
        if (nmo_guid_equals(entries[i].plugin->guid, guid)) {
            return (int)i;
        }
    }

    return -1;
}

static int nmo_plugin_manager_apply_descriptor(
    nmo_manager_t *manager,
    const nmo_manager_descriptor_t *desc) {
    if (manager == NULL || desc == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = NMO_OK;
    result = nmo_manager_set_user_data(manager, desc->user_data);
    if (result != NMO_OK) {
        return result;
    }
    result = nmo_manager_set_pre_load_hook(manager, desc->pre_load);
    if (result != NMO_OK) {
        return result;
    }
    result = nmo_manager_set_post_load_hook(manager, desc->post_load);
    if (result != NMO_OK) {
        return result;
    }
    result = nmo_manager_set_load_data_hook(manager, desc->load_data);
    if (result != NMO_OK) {
        return result;
    }
    result = nmo_manager_set_save_data_hook(manager, desc->save_data);
    if (result != NMO_OK) {
        return result;
    }
    result = nmo_manager_set_pre_save_hook(manager, desc->pre_save);
    if (result != NMO_OK) {
        return result;
    }
    return nmo_manager_set_post_save_hook(manager, desc->post_save);
}

static void nmo_plugin_manager_unregister_instance_managers(
    nmo_plugin_manager_t *manager,
    const nmo_plugin_instance_info_t *info) {
    if (manager == NULL || info == NULL || info->manager_ids == NULL || info->manager_id_count == 0) {
        return;
    }

    nmo_manager_registry_t *registry = nmo_context_get_manager_registry(manager->context);
    if (registry == NULL) {
        return;
    }

    for (size_t i = 0; i < info->manager_id_count; ++i) {
        nmo_manager_registry_unregister(registry, info->manager_ids[i]);
    }
}

static int nmo_plugin_manager_register_managers(
    nmo_plugin_manager_t *manager,
    nmo_plugin_instance_info_t *info) {
    if (manager == NULL || info == NULL || info->plugin == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const nmo_plugin_t *plugin = info->plugin;
    if (plugin->register_managers == NULL) {
        return NMO_OK;
    }

    nmo_manager_registry_t *registry = nmo_context_get_manager_registry(manager->context);
    if (registry == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    size_t required = 0;
    int query_result = plugin->register_managers(plugin, NULL, 0, &required);
    if (query_result != NMO_OK) {
        return query_result;
    }

    if (required == 0) {
        return NMO_OK;
    }

    nmo_manager_descriptor_t *descs = (nmo_manager_descriptor_t *)nmo_alloc(
        manager->allocator,
        required * sizeof(nmo_manager_descriptor_t),
        alignof(nmo_manager_descriptor_t));
    if (descs == NULL) {
        return NMO_ERR_NOMEM;
    }
    memset(descs, 0, required * sizeof(*descs));

    size_t written = 0;
    int fill_result = plugin->register_managers(plugin, descs, required, &written);
    if (fill_result != NMO_OK) {
        nmo_free(manager->allocator, descs);
        return fill_result;
    }
    if (written > required) {
        nmo_free(manager->allocator, descs);
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (written == 0) {
        nmo_free(manager->allocator, descs);
        return NMO_OK;
    }

    nmo_manager_id_t *manager_ids = (nmo_manager_id_t *)nmo_arena_alloc(
        manager->arena,
        written * sizeof(nmo_manager_id_t),
        alignof(nmo_manager_id_t));
    if (manager_ids == NULL) {
        nmo_free(manager->allocator, descs);
        return NMO_ERR_NOMEM;
    }

    size_t registered = 0;
    for (size_t i = 0; i < written; ++i) {
        const nmo_manager_descriptor_t *desc = &descs[i];
        if (nmo_guid_is_null(desc->guid)) {
            continue;
        }

        nmo_manager_t *mgr = nmo_manager_create(desc->guid, desc->name, desc->category);
        if (mgr == NULL) {
            break;
        }

        int apply_result = nmo_plugin_manager_apply_descriptor(mgr, desc);
        if (apply_result != NMO_OK) {
            nmo_manager_destroy(mgr);
            break;
        }

        nmo_status_t reg_result = nmo_manager_registry_register(registry, desc->manager_id, mgr);
        if (reg_result != NMO_OK) {
            nmo_manager_destroy(mgr);
            break;
        }

        manager_ids[registered++] = desc->manager_id;
    }

    nmo_free(manager->allocator, descs);

    if (registered != written) {
        for (size_t i = 0; i < registered; ++i) {
            nmo_manager_registry_unregister(registry, manager_ids[i]);
        }
        return NMO_ERR_INVALID_STATE;
    }

    info->manager_ids = manager_ids;
    info->manager_id_count = registered;
    info->flags |= NMO_PLUGIN_INSTANCE_FLAG_MANAGERS_REGISTERED;
    return NMO_OK;
}

static int nmo_plugin_manager_register_instance(
    nmo_plugin_manager_t *manager,
    const nmo_plugin_t *plugin,
    nmo_shared_library_t *library,
    uint32_t instance_flags) {
    if (manager == NULL || plugin == NULL || plugin->name == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_guid_is_null(plugin->guid)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (nmo_plugin_manager_find_index_by_guid(manager, plugin->guid) >= 0) {
        return NMO_ERR_INVALID_STATE;
    }

    size_t required = manager->instances.count + 1;
    int reserve_result = nmo_plugin_manager_reserve(manager, required);
    if (reserve_result != NMO_OK) {
        return reserve_result;
    }

    nmo_plugin_instance_info_t *slot = NULL;
    nmo_status_t extend = nmo_arena_array_extend(&manager->instances, 1, (void **)&slot);
    if (extend != NMO_OK) {
        return extend;
    }
    memset(slot, 0, sizeof(*slot));
    slot->plugin = plugin;
    slot->library = library;
    slot->flags = instance_flags;

    nmo_type_registry_t *type_registry = nmo_context_get_type_registry(manager->context);
    if (type_registry != NULL) {
        nmo_status_t reg_plugin = nmo_type_registry_register_plugin(type_registry, plugin);
        if (reg_plugin != NMO_OK) {
            manager->instances.count--;
            memset(slot, 0, sizeof(*slot));
            return reg_plugin;
        }
    }

    int init_result = NMO_OK;
    if (plugin->init != NULL) {
        init_result = plugin->init(plugin, manager->context);
        if (init_result != NMO_OK) {
            if (type_registry != NULL) {
                nmo_type_registry_unregister_plugin_types(type_registry, plugin->guid);
            }
            manager->instances.count--;
            memset(slot, 0, sizeof(*slot));
            return init_result;
        }
        slot->flags |= NMO_PLUGIN_INSTANCE_FLAG_INITIALIZED;
    }

    int manager_result = nmo_plugin_manager_register_managers(manager, slot);
    if (manager_result != NMO_OK) {
        if (plugin->shutdown != NULL) {
            plugin->shutdown(plugin, manager->context);
        }
        if (type_registry != NULL) {
            nmo_type_registry_unregister_plugin_types(type_registry, plugin->guid);
        }
        manager->instances.count--;
        memset(slot, 0, sizeof(*slot));
        return manager_result;
    }

    return NMO_OK;
}

static void nmo_plugin_manager_rollback_last_instance(nmo_plugin_manager_t *manager) {
    if (manager == NULL || manager->instances.count == 0) {
        return;
    }

    nmo_plugin_instance_info_t last = {0};
    nmo_arena_array_pop(&manager->instances, &last);

    if (last.plugin != NULL && last.plugin->shutdown != NULL) {
        last.plugin->shutdown(last.plugin, manager->context);
    }

    nmo_plugin_manager_unregister_instance_managers(manager, &last);

    nmo_type_registry_t *type_registry = nmo_context_get_type_registry(manager->context);
    if (type_registry != NULL && last.plugin != NULL) {
        nmo_type_registry_unregister_plugin_types(type_registry, last.plugin->guid);
    }

    if (last.library != NULL && (last.flags & NMO_PLUGIN_INSTANCE_FLAG_OWNS_LIBRARY)) {
        nmo_shared_library_close(last.library);
    }
}

nmo_plugin_manager_t *nmo_plugin_manager_create(nmo_context_t *ctx) {
    if (ctx == NULL) {
        return NULL;
    }

    nmo_allocator_t *allocator = nmo_context_get_allocator(ctx);
    if (allocator == NULL) {
        return NULL;
    }

    nmo_arena_t *arena = nmo_context_get_arena(ctx);
    if (arena == NULL) {
        return NULL;
    }

    nmo_plugin_manager_t *manager = (nmo_plugin_manager_t *) nmo_arena_alloc(
        arena, sizeof(nmo_plugin_manager_t), alignof(nmo_plugin_manager_t));
    if (manager == NULL) {
        return NULL;
    }

    memset(manager, 0, sizeof(nmo_plugin_manager_t));
    manager->context = ctx;
    manager->allocator = allocator;
    manager->arena = arena;

    nmo_status_t init_result = nmo_arena_array_init(&manager->instances,
                                              sizeof(nmo_plugin_instance_info_t),
                                              0,
                                              arena);
    if (init_result != NMO_OK) {
        return NULL;
    }

    return manager;
}

void nmo_plugin_manager_destroy(nmo_plugin_manager_t *manager) {
    if (manager == NULL) {
        return;
    }

    nmo_plugin_instance_info_t *entries = (nmo_plugin_instance_info_t *)manager->instances.data;
    for (size_t i = 0; entries != NULL && i < manager->instances.count; ++i) {
        nmo_plugin_instance_info_t *info = &entries[i];
        if (info->plugin != NULL && info->plugin->shutdown != NULL) {
            info->plugin->shutdown(info->plugin, manager->context);
        }
    }

    for (size_t i = 0; entries != NULL && i < manager->instances.count; ++i) {
        nmo_plugin_instance_info_t *info = &entries[i];
        nmo_plugin_manager_unregister_instance_managers(manager, info);
    }

    for (size_t i = 0; entries != NULL && i < manager->instances.count; ++i) {
        nmo_plugin_instance_info_t *info = &entries[i];
        if (info->library != NULL && (info->flags & NMO_PLUGIN_INSTANCE_FLAG_OWNS_LIBRARY)) {
            nmo_shared_library_close(info->library);
            info->library = NULL;
        }
    }

    nmo_arena_array_clear(&manager->instances);
}

nmo_context_t *nmo_plugin_manager_get_context(const nmo_plugin_manager_t *manager) {
    return manager ? manager->context : NULL;
}

int nmo_plugin_manager_register(
    nmo_plugin_manager_t *manager,
    const nmo_plugin_registration_desc_t *desc) {
    if (manager == NULL || desc == NULL || desc->plugins == NULL || desc->plugin_count == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    size_t start_count = manager->instances.count;
    for (size_t i = 0; i < desc->plugin_count; ++i) {
        const nmo_plugin_t *plugin = &desc->plugins[i];
        int reg_result = nmo_plugin_manager_register_instance(manager, plugin, NULL, 0);
        if (reg_result != NMO_OK) {
            while (manager->instances.count > start_count) {
                nmo_plugin_manager_rollback_last_instance(manager);
            }
            return reg_result;
        }
    }

    return NMO_OK;
}

int nmo_plugin_manager_load_library(
    nmo_plugin_manager_t *manager,
    const char *path,
    const char *symbol_name) {
    if (manager == NULL || path == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    const char *export_name = (symbol_name != NULL) ? symbol_name : "nmo_plugin_get_info";

    nmo_shared_library_t *library = NULL;
    nmo_status_t open_result = nmo_shared_library_open(manager->allocator, path, &library);
    if (open_result != NMO_OK) {
        return open_result;
    }

    void *symbol_ptr = NULL;
    nmo_status_t symbol_result = nmo_shared_library_get_symbol(library, export_name, &symbol_ptr);
    if (symbol_result != NMO_OK) {
        nmo_shared_library_close(library);
        return symbol_result;
    }

    union {
        void *ptr;
        nmo_plugin_query_fn fn;
    } caster;
    caster.ptr = symbol_ptr;
    nmo_plugin_query_fn query = caster.fn;
    if (query == NULL) {
        nmo_shared_library_close(library);
        return NMO_ERR_INVALID_STATE;
    }

    size_t plugin_count = 0;
    const nmo_plugin_t *plugins = query(&plugin_count);
    if (plugins == NULL || plugin_count == 0) {
        nmo_shared_library_close(library);
        return NMO_ERR_INVALID_STATE;
    }

    size_t start_count = manager->instances.count;
    for (size_t i = 0; i < plugin_count; ++i) {
        const nmo_plugin_t *plugin = &plugins[i];
        if (plugin == NULL) {
            continue;
        }

        if (nmo_plugin_manager_find_index_by_guid(manager, plugin->guid) >= 0) {
            continue;
        }

        int reg_result = nmo_plugin_manager_register_instance(manager, plugin, library, 0);
        if (reg_result != NMO_OK) {
            while (manager->instances.count > start_count) {
                nmo_plugin_manager_rollback_last_instance(manager);
            }

            nmo_shared_library_close(library);
            return reg_result;
        }
    }

    if (manager->instances.count > start_count) {
        nmo_plugin_instance_info_t *entries = (nmo_plugin_instance_info_t *)manager->instances.data;
        if (entries != NULL) {
            entries[start_count].flags |= NMO_PLUGIN_INSTANCE_FLAG_OWNS_LIBRARY;
        }
        return NMO_OK;
    }

    nmo_shared_library_close(library);
    return NMO_ERR_INVALID_STATE;
}

const nmo_plugin_instance_info_t *nmo_plugin_manager_get_plugins(
    const nmo_plugin_manager_t *manager,
    size_t *out_count) {
    if (manager == NULL) {
        if (out_count != NULL) {
            *out_count = 0;
        }
        return NULL;
    }

    if (out_count != NULL) {
        *out_count = manager->instances.count;
    }

    return (const nmo_plugin_instance_info_t *)manager->instances.data;
}

const nmo_plugin_t *nmo_plugin_manager_find_by_guid(
    const nmo_plugin_manager_t *manager,
    nmo_guid_t guid) {
    if (manager == NULL) {
        return NULL;
    }

    int index = nmo_plugin_manager_find_index_by_guid(manager, guid);
    if (index < 0) {
        return NULL;
    }

    nmo_plugin_instance_info_t *entries = (nmo_plugin_instance_info_t *)manager->instances.data;
    return entries ? entries[index].plugin : NULL;
}

