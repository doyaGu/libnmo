/**
 * @file context.c
 * @brief Global context implementation (Phase 8.1)
 */

#include "session/nmo_context.h"
#include "extension/nmo_extension_registry.h"
#include "core/nmo_allocator.h"
#include "core/nmo_logger.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_operations.h"
#include "type/nmo_type_string.h"
#include "object/nmo_object_types.h"
#include "format/nmo_manager_registry.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "session/nmo_session.h"
#include "behavior/nmo_bb_registry.h"
#include "extension/nmo_virtools_data_plugin.h"
#include "format/nmo_interface_chunk.h"

#include "object/nmo_object_repository.h"

#include <stdlib.h>
#include <string.h>
#include <stdalign.h>

/* C11 atomic support for thread-safe reference counting */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
    #include <stdatomic.h>
    #define NMO_ATOMIC_INT atomic_int
    #define NMO_ATOMIC_FETCH_ADD(ptr, val) atomic_fetch_add(ptr, val)
    #define NMO_ATOMIC_FETCH_SUB(ptr, val) atomic_fetch_sub(ptr, val)
#elif defined(_MSC_VER)
    /* MSVC intrinsics */
    #include <intrin.h>
    #define NMO_ATOMIC_INT volatile long
    #define NMO_ATOMIC_FETCH_ADD(ptr, val) _InterlockedExchangeAdd((volatile long*)(ptr), (val))
    #define NMO_ATOMIC_FETCH_SUB(ptr, val) _InterlockedExchangeAdd((volatile long*)(ptr), -(val))
#elif defined(__GNUC__) || defined(__clang__)
    /* GCC/Clang built-ins */
    #define NMO_ATOMIC_INT volatile int
    #define NMO_ATOMIC_FETCH_ADD(ptr, val) __sync_fetch_and_add(ptr, val)
    #define NMO_ATOMIC_FETCH_SUB(ptr, val) __sync_fetch_and_sub(ptr, val)
#else
    /* Fallback: non-atomic (not thread-safe) */
    #pragma message("WARNING: No atomic primitives available; nmo_context refcounting is NOT thread-safe")
    #define NMO_ATOMIC_INT int
    #define NMO_ATOMIC_FETCH_ADD(ptr, val) (*(ptr) += (val), *(ptr) - (val))
    #define NMO_ATOMIC_FETCH_SUB(ptr, val) (*(ptr) -= (val), *(ptr) + (val))
#endif

/**
 * Context structure
 */
typedef struct nmo_context {
    /* Reference counting (thread-safe) */
    NMO_ATOMIC_INT refcount;

    /* Owned resources */
    nmo_allocator_t allocator_storage;
    nmo_logger_t logger_storage;
    nmo_allocator_t *allocator;
    nmo_logger_t *logger;
    int logger_owned;
    nmo_type_registry_t *type_registry;      /* Schema v2 */
    nmo_operation_registry_t *operation_registry;
    nmo_type_runtime_t type_runtime;
    nmo_manager_registry_t *manager_registry;
    nmo_extension_registry_t *extension_registry;
    nmo_bb_registry_t *bb_registry;
    nmo_arena_t *arena;

    /* Configuration */
    int thread_pool_size;

} nmo_context_t;

static nmo_status_t nmo_context_object_id_to_name_resolver(
    const void *session_ptr,
    nmo_object_id_t id,
    const char **out_name)
{
    if (!session_ptr || !out_name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for object id->name resolver");
    }

    const nmo_session_t *session = (const nmo_session_t*)session_ptr;
    const nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Session repository is not available");
    }

    nmo_object_t *object = nmo_object_repository_find_by_id(repo, (nmo_object_id_t)id);
    if (!object) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Object not found");
    }

    const char *name = nmo_object_get_name(object);
    if (!name || name[0] == '\0') {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Object has no name");
    }

    *out_name = name;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_context_object_name_to_id_resolver(
    const void *session_ptr,
    const char *name,
    nmo_object_id_t *out_id)
{
    if (!session_ptr || !name || !out_id) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments for object name->id resolver");
    }

    const nmo_session_t *session = (const nmo_session_t*)session_ptr;
    const nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Session repository is not available");
    }

    nmo_object_t *object = nmo_object_repository_find_by_name((nmo_object_repository_t*)repo, name);
    if (!object) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR, "Object name not found");
    }

    *out_id = (nmo_object_id_t)nmo_object_get_id(object);
    NMO_RETURN_OK();
}

/**
 * Create context
 */
nmo_context_t *nmo_context_create(const nmo_context_desc_t *desc) {
    nmo_allocator_t effective_allocator =
        (desc != NULL && desc->allocator != NULL)
            ? *desc->allocator
            : nmo_allocator_default();

    nmo_context_t *ctx = (nmo_context_t *)nmo_alloc(&effective_allocator, sizeof(nmo_context_t), alignof(nmo_context_t));
    if (ctx == NULL) {
        return NULL;
    }

    memset(ctx, 0, sizeof(nmo_context_t));
    ctx->refcount = 1;

    ctx->allocator_storage = effective_allocator;
    ctx->allocator = &ctx->allocator_storage;

    ctx->logger_storage = (desc != NULL && desc->logger != NULL)
        ? *desc->logger
        : nmo_logger_null();
    ctx->logger = &ctx->logger_storage;
    ctx->logger_owned = 1;

    ctx->arena = nmo_arena_create(ctx->allocator, 0);
    if (ctx->arena == NULL) {
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    /* Create type registry (Schema v2) */
    ctx->type_registry = nmo_type_registry_create(ctx->arena);
    if (ctx->type_registry == NULL) {
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    /* Register builtin scalar/math/container types first */
    nmo_status_t type_result = nmo_register_builtin_types(ctx->type_registry);
    if (type_result != NMO_OK) {
        nmo_type_registry_destroy(ctx->type_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    /* Register Virtools object types after builtin types */
    type_result = nmo_register_object_types(ctx->type_registry);
    if (type_result != NMO_OK) {
        nmo_type_registry_destroy(ctx->type_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    /* Register interface chunk types for behavior reflection */
    type_result = nmo_register_interface_types(ctx->type_registry);
    if (type_result != NMO_OK) {
        nmo_type_registry_destroy(ctx->type_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    /* Create BB prototype registry */
    ctx->bb_registry = nmo_bb_registry_create(ctx->arena);

    /* Create operation registry early — virtools_load will register
     * Virtools operation signatures (function=NULL) into it. */
    ctx->operation_registry = nmo_operation_registry_create(ctx->arena);
    if (ctx->operation_registry == NULL) {
        nmo_type_registry_destroy(ctx->type_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    ctx->manager_registry = nmo_manager_registry_create(ctx->arena);
    if (ctx->manager_registry == NULL) {
        nmo_operation_registry_destroy(ctx->operation_registry);
        nmo_type_registry_destroy(ctx->type_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    ctx->extension_registry = nmo_extension_registry_create(
        ctx->allocator,
        ctx->type_registry,
        ctx->operation_registry,
        ctx->bb_registry,
        ctx->manager_registry);
    if (ctx->extension_registry == NULL) {
        nmo_manager_registry_destroy(ctx->manager_registry);
        nmo_operation_registry_destroy(ctx->operation_registry);
        nmo_type_registry_destroy(ctx->type_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    /* Register built-in Virtools data plugin — loads param types,
     * operation signatures, and BB prototypes from JSON. */
    {
        const char *data_dir = (desc != NULL) ? desc->data_dir : NULL;
        if (data_dir == NULL)
            data_dir = getenv("NMO_DATA_DIR");
        if (data_dir != NULL) {
            /* Deep-copy data_dir into arena — getenv() return is volatile */
            size_t len = strlen(data_dir);
            char *dir_copy = (char *)nmo_arena_alloc(ctx->arena, len + 1, 1);
            if (dir_copy) memcpy(dir_copy, data_dir, len + 1);
            nmo_extension_registry_set_user_data(ctx->extension_registry,
                                                 (void *)dir_copy);
            const nmo_extension_plugin_t *vt_plugin = nmo_virtools_data_plugin_get();
            nmo_extension_registry_register_static(
                ctx->extension_registry, vt_plugin, 1);
        }
    }

    /* Compute state layouts for all types (including those loaded by plugin) */
    nmo_type_registry_compute_state_layouts(ctx->type_registry);

    /* Register builtin operations with C implementations.
     * These override signature-only entries from JSON via function-aware policy. */
    nmo_status_t op_result = nmo_register_builtin_operations(
        ctx->operation_registry,
        ctx->type_registry);
    if (op_result != NMO_OK) {
        nmo_extension_registry_destroy(ctx->extension_registry);
        nmo_manager_registry_destroy(ctx->manager_registry);
        nmo_operation_registry_destroy(ctx->operation_registry);
        nmo_type_registry_destroy(ctx->type_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    ctx->type_runtime.types = ctx->type_registry;
    ctx->type_runtime.ops = ctx->operation_registry;
    ctx->type_runtime.types_finalized_version = 0u;
    ctx->type_runtime.ops_finalized_version = 0u;

    nmo_status_t runtime_result = nmo_type_runtime_finalize(&ctx->type_runtime);
    if (runtime_result != NMO_OK) {
        nmo_extension_registry_destroy(ctx->extension_registry);
        nmo_manager_registry_destroy(ctx->manager_registry);
        nmo_operation_registry_destroy(ctx->operation_registry);
        nmo_type_registry_destroy(ctx->type_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    /* Install object-id string resolvers once per process */
    nmo_type_string_set_object_resolvers(
        nmo_context_object_id_to_name_resolver,
        nmo_context_object_name_to_id_resolver);

    ctx->thread_pool_size = (desc != NULL) ? desc->thread_pool_size : 0;
    return ctx;
}

/**
 * Retain context
 */
void nmo_context_retain(nmo_context_t *ctx) {
    if (ctx != NULL) {
        /* Thread-safe atomic increment */
        NMO_ATOMIC_FETCH_ADD(&ctx->refcount, 1);
    }
}

/**
 * Release context
 */
void nmo_context_release(nmo_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    /* Thread-safe atomic decrement */
    int old_refcount = NMO_ATOMIC_FETCH_SUB(&ctx->refcount, 1);

    /* If old value was 1, we just decremented to 0, so cleanup */
    if (old_refcount == 1) {
        /* Destroy owned resources */
        if (ctx->extension_registry != NULL) {
            nmo_extension_registry_destroy(ctx->extension_registry);
        }

        /* Destroy manager registry */
        if (ctx->manager_registry != NULL) {
            nmo_manager_registry_destroy(ctx->manager_registry);
        }

        if (ctx->operation_registry != NULL) {
            nmo_operation_registry_destroy(ctx->operation_registry);
        }

        if (ctx->bb_registry != NULL) {
            nmo_bb_registry_destroy(ctx->bb_registry);
        }

        /* Destroy type registry */
        if (ctx->type_registry != NULL) {
            nmo_type_registry_destroy(ctx->type_registry);
        }

        if (ctx->arena != NULL) {
            nmo_arena_destroy(ctx->arena);
        }

        if (ctx->allocator != NULL) {
            nmo_free(ctx->allocator, ctx);
        }
    }
}

nmo_type_registry_t *nmo_context_get_type_registry(const nmo_context_t *ctx) {
    return ctx ? ctx->type_registry : NULL;
}

nmo_operation_registry_t *nmo_context_get_operation_registry(const nmo_context_t *ctx) {
    return ctx ? ctx->operation_registry : NULL;
}

nmo_bb_registry_t *nmo_context_get_bb_registry(const nmo_context_t *ctx) {
    return ctx ? ctx->bb_registry : NULL;
}

const nmo_type_runtime_t *nmo_context_get_type_runtime(const nmo_context_t *ctx) {
    return ctx ? &ctx->type_runtime : NULL;
}

/**
 * Get manager registry
 */
nmo_manager_registry_t *nmo_context_get_manager_registry(const nmo_context_t *ctx) {
    return ctx ? ctx->manager_registry : NULL;
}

nmo_extension_registry_t *nmo_context_get_extension_registry(const nmo_context_t *ctx) {
    return ctx ? ctx->extension_registry : NULL;
}

/**
 * Get allocator
 */
nmo_allocator_t *nmo_context_get_allocator(const nmo_context_t *ctx) {
    return ctx ? ctx->allocator : NULL;
}

/**
 * Get logger
 */
nmo_logger_t *nmo_context_get_logger(const nmo_context_t *ctx) {
    return ctx ? ctx->logger : NULL;
}

static void nmo_context_copy_logger(nmo_context_t *ctx, const nmo_logger_t *logger) {
    if (!ctx) {
        return;
    }
    if (logger == NULL) {
        ctx->logger_storage = nmo_logger_null();
    } else {
        ctx->logger_storage = *logger;
    }
    ctx->logger = &ctx->logger_storage;
    ctx->logger_owned = 1;
}

void nmo_context_set_logger(nmo_context_t *ctx, const nmo_logger_t *logger) {
    nmo_context_copy_logger(ctx, logger);
}

void nmo_context_enable_logging(nmo_context_t *ctx, int enable) {
    if (!ctx) {
        return;
    }
    nmo_logger_t logger = enable ? nmo_logger_stderr() : nmo_logger_null();
    nmo_context_copy_logger(ctx, &logger);
}

void nmo_context_set_log_level(nmo_context_t *ctx, nmo_log_level_t level) {
    if (!ctx || ctx->logger == NULL) {
        return;
    }
    if (!ctx->logger_owned) {
        nmo_logger_t copy = *ctx->logger;
        copy.level = level;
        nmo_context_copy_logger(ctx, &copy);
        return;
    }
    ctx->logger_storage.level = level;
}

/**
 * Get arena
 */
nmo_arena_t *nmo_context_get_arena(const nmo_context_t *ctx) {
    return ctx ? ctx->arena : NULL;
}

/**
 * Get reference count
 */
int nmo_context_get_refcount(const nmo_context_t *ctx) {
    return ctx ? ctx->refcount : 0;
}
