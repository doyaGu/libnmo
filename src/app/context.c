/**
 * @file context.c
 * @brief Global context implementation (Phase 8.1)
 */

#include "app/nmo_context.h"
#include "core/nmo_allocator.h"
#include "core/nmo_logger.h"
#include "schema/nmo_schema_registry.h"
#include "format/nmo_manager_registry.h"
#include <stdlib.h>
#include <string.h>

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
    nmo_allocator_t *allocator;
    nmo_logger_t *logger;
    nmo_schema_registry_t *schema_registry;
    nmo_manager_registry_t *manager_registry;

    /* Configuration */
    int thread_pool_size;

    /* Flags */
    int owns_allocator;
    int owns_logger;
} nmo_context_t;

/**
 * Create context
 */
nmo_context_t *nmo_context_create(const nmo_context_desc_t *desc) {
    nmo_context_t *ctx = (nmo_context_t *) malloc(sizeof(nmo_context_t));
    if (ctx == NULL) {
        return NULL;
    }

    memset(ctx, 0, sizeof(nmo_context_t));
    ctx->refcount = 1;

    /* Set up allocator */
    if (desc != NULL && desc->allocator != NULL) {
        ctx->allocator = desc->allocator;
        ctx->owns_allocator = 0;
    } else {
        /* Use default allocator */
        ctx->allocator = (nmo_allocator_t *) malloc(sizeof(nmo_allocator_t));
        if (ctx->allocator == NULL) {
            free(ctx);
            return NULL;
        }
        *ctx->allocator = nmo_allocator_default();
        ctx->owns_allocator = 1;
    }

    /* Set up logger */
    if (desc != NULL && desc->logger != NULL) {
        ctx->logger = desc->logger;
        ctx->owns_logger = 0;
    } else {
        /* Use default logger (stderr) */
        ctx->logger = (nmo_logger_t *) malloc(sizeof(nmo_logger_t));
        if (ctx->logger == NULL) {
            if (ctx->owns_allocator) {
                free(ctx->allocator);
            }
            free(ctx);
            return NULL;
        }
        *ctx->logger = nmo_logger_stderr();
        ctx->owns_logger = 1;
    }

    /* Create schema registry */
    ctx->schema_registry = nmo_schema_registry_create();
    if (ctx->schema_registry == NULL) {
        if (ctx->owns_logger) {
            free(ctx->logger);
        }
        if (ctx->owns_allocator) {
            free(ctx->allocator);
        }
        free(ctx);
        return NULL;
    }

    /* Create manager registry (Phase 11) */
    ctx->manager_registry = nmo_manager_registry_create();
    if (ctx->manager_registry == NULL) {
        nmo_schema_registry_destroy(ctx->schema_registry);
        if (ctx->owns_logger) {
            free(ctx->logger);
        }
        if (ctx->owns_allocator) {
            free(ctx->allocator);
        }
        free(ctx);
        return NULL;
    }

    /* Thread pool size */
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
        if (ctx->schema_registry != NULL) {
            nmo_schema_registry_destroy(ctx->schema_registry);
        }

        /* Destroy manager registry */
        if (ctx->manager_registry != NULL) {
            nmo_manager_registry_destroy(ctx->manager_registry);
        }

        if (ctx->owns_logger && ctx->logger != NULL) {
            free(ctx->logger);
        }

        if (ctx->owns_allocator && ctx->allocator != NULL) {
            free(ctx->allocator);
        }

        free(ctx);
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
 * Get reference count
 */
int nmo_context_get_refcount(const nmo_context_t *ctx) {
    return ctx ? ctx->refcount : 0;
}
