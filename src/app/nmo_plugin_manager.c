#include "app/nmo_plugin.h"
#include "app/nmo_context.h"
#include "core/nmo_allocator.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_shared_library.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>

struct nmo_plugin_manager {
    nmo_context_t *context;
    nmo_allocator_t *allocator;
    nmo_arena_t *arena;
    nmo_array_t instances;
};

static int nmo_plugin_manager_reserve(nmo_plugin_manager_t *manager, size_t needed) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (manager->instances.capacity >= needed) {
        return NMO_OK;
    }

    nmo_result_t result = nmo_array_reserve(&manager->instances, needed);
    return result.code;
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

    nmo_result_t init_result = nmo_array_init(&manager->instances,
                                              sizeof(nmo_plugin_instance_info_t),
                                              0,
                                              arena);
    if (init_result.code != NMO_OK) {
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

        if (info->library != NULL && (info->flags & NMO_PLUGIN_INSTANCE_FLAG_OWNS_LIBRARY)) {
            nmo_shared_library_close(info->library);
            info->library = NULL;
        }
    }

    nmo_array_clear(&manager->instances);
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

    size_t required = manager->instances.count + desc->plugin_count;
    int reserve_result = nmo_plugin_manager_reserve(manager, required);
    if (reserve_result != NMO_OK) {
        return reserve_result;
    }

    for (size_t i = 0; i < desc->plugin_count; ++i) {
        const nmo_plugin_t *plugin = &desc->plugins[i];
        if (plugin == NULL || plugin->name == NULL) {
            return NMO_ERR_INVALID_ARGUMENT;
        }

        if (nmo_plugin_manager_find_index_by_guid(manager, plugin->guid) >= 0) {
            return NMO_ERR_INVALID_STATE;
        }

        nmo_plugin_instance_info_t *slot = NULL;
        nmo_result_t extend = nmo_array_extend(&manager->instances, 1, (void **)&slot);
        if (extend.code != NMO_OK) {
            return extend.code;
        }
        memset(slot, 0, sizeof(*slot));
        slot->plugin = plugin;

        if (plugin->init != NULL) {
            int init_result = plugin->init(plugin, manager->context);
            if (init_result != NMO_OK) {
                manager->instances.count--;
                memset(slot, 0, sizeof(*slot));
                return init_result;
            }
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
    nmo_result_t open_result = nmo_shared_library_open(manager->allocator, path, &library);
    if (open_result.code != NMO_OK) {
        return open_result.code;
    }

    void *symbol_ptr = NULL;
    nmo_result_t symbol_result = nmo_shared_library_get_symbol(library, export_name, &symbol_ptr);
    if (symbol_result.code != NMO_OK) {
        nmo_shared_library_close(library);
        return symbol_result.code;
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

    size_t required = manager->instances.count + plugin_count;
    int reserve_result = nmo_plugin_manager_reserve(manager, required);
    if (reserve_result != NMO_OK) {
        nmo_shared_library_close(library);
        return reserve_result;
    }

    size_t first_index = manager->instances.count;

    for (size_t i = 0; i < plugin_count; ++i) {
        const nmo_plugin_t *plugin = &plugins[i];
        if (plugin == NULL) {
            continue;
        }

        if (nmo_plugin_manager_find_index_by_guid(manager, plugin->guid) >= 0) {
            continue;
        }

        nmo_plugin_instance_info_t *slot = NULL;
        nmo_result_t extend = nmo_array_extend(&manager->instances, 1, (void **)&slot);
        if (extend.code != NMO_OK) {
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        slot->plugin = plugin;
        slot->library = library;

        if (plugin->init != NULL) {
            int init_result = plugin->init(plugin, manager->context);
            if (init_result != NMO_OK) {
                manager->instances.count--;
                memset(slot, 0, sizeof(*slot));
                continue;
            }
        }
    }

    if (manager->instances.count > first_index) {
        nmo_plugin_instance_info_t *entries = (nmo_plugin_instance_info_t *)manager->instances.data;
        if (entries != NULL) {
            entries[first_index].flags |= NMO_PLUGIN_INSTANCE_FLAG_OWNS_LIBRARY;
        }
    } else {
        nmo_shared_library_close(library);
    }

    return NMO_OK;
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
