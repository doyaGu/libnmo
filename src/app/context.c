/**
 * @file context.c
 * @brief Global context implementation (Phase 8.1)
 */

#include "app/nmo_context.h"
#include "app/nmo_plugin.h"
#include "core/nmo_allocator.h"
#include "core/nmo_logger.h"
#include "schema/nmo_schema_registry.h"
#include "format/nmo_manager_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "core/nmo_logger.h"
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
    nmo_schema_registry_t *schema_registry;
    nmo_manager_registry_t *manager_registry;
    nmo_plugin_manager_t *plugin_manager;
    nmo_arena_t *arena;

    /* Configuration */
    int thread_pool_size;

} nmo_context_t;

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

    if (desc != NULL && desc->allocator != NULL) {
        ctx->allocator = desc->allocator;
    } else {
        ctx->allocator_storage = effective_allocator;
        ctx->allocator = &ctx->allocator_storage;
    }

    if (desc != NULL && desc->logger != NULL) {
        ctx->logger = desc->logger;
    } else {
        ctx->logger_storage = nmo_logger_stderr();
        ctx->logger = &ctx->logger_storage;
    }

    ctx->arena = nmo_arena_create(ctx->allocator, 0);
    if (ctx->arena == NULL) {
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    ctx->schema_registry = nmo_schema_registry_create(ctx->arena);
    if (ctx->schema_registry == NULL) {
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    ctx->manager_registry = nmo_manager_registry_create(ctx->arena);
    if (ctx->manager_registry == NULL) {
        nmo_schema_registry_destroy(ctx->schema_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

    ctx->plugin_manager = nmo_plugin_manager_create(ctx);
    if (ctx->plugin_manager == NULL) {
        nmo_manager_registry_destroy(ctx->manager_registry);
        nmo_schema_registry_destroy(ctx->schema_registry);
        nmo_arena_destroy(ctx->arena);
        nmo_free(&effective_allocator, ctx);
        return NULL;
    }

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
        if (ctx->plugin_manager != NULL) {
            nmo_plugin_manager_destroy(ctx->plugin_manager);
        }

        /* Destroy manager registry */
        if (ctx->manager_registry != NULL) {
            nmo_manager_registry_destroy(ctx->manager_registry);
        }

        if (ctx->schema_registry != NULL) {
            nmo_schema_registry_destroy(ctx->schema_registry);
        }

        if (ctx->arena != NULL) {
            nmo_arena_destroy(ctx->arena);
        }

        if (ctx->allocator != NULL) {
            nmo_free(ctx->allocator, ctx);
        }
    }
}

/**
 * Get schema registry
 */
nmo_schema_registry_t *nmo_context_get_schema_registry(const nmo_context_t *ctx) {
    return ctx ? ctx->schema_registry : NULL;
}

/**
 * Get manager registry
 */
nmo_manager_registry_t *nmo_context_get_manager_registry(const nmo_context_t *ctx) {
    return ctx ? ctx->manager_registry : NULL;
}

nmo_plugin_manager_t *nmo_context_get_plugin_manager(const nmo_context_t *ctx) {
    return ctx ? ctx->plugin_manager : NULL;
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
