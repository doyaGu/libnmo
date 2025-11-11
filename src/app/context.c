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

/**
 * Context structure
 */
struct nmo_context {
    /* Reference counting */
    int refcount;

    /* Owned resources */
    nmo_allocator* allocator;
    nmo_logger* logger;
    nmo_schema_registry* schema_registry;
    nmo_manager_registry* manager_registry;

    /* Configuration */
    int thread_pool_size;

    /* Flags */
    int owns_allocator;
    int owns_logger;
};

/**
 * Create context
 */
nmo_context* nmo_context_create(const nmo_context_desc* desc) {
    nmo_context* ctx = (nmo_context*)malloc(sizeof(nmo_context));
    if (ctx == NULL) {
        return NULL;
    }

    memset(ctx, 0, sizeof(nmo_context));
    ctx->refcount = 1;

    /* Set up allocator */
    if (desc != NULL && desc->allocator != NULL) {
        ctx->allocator = desc->allocator;
        ctx->owns_allocator = 0;
    } else {
        /* Use default allocator */
        ctx->allocator = (nmo_allocator*)malloc(sizeof(nmo_allocator));
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
        ctx->logger = (nmo_logger*)malloc(sizeof(nmo_logger));
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
void nmo_context_retain(nmo_context* ctx) {
    if (ctx != NULL) {
        /* TODO: Add atomic increment for thread safety */
        ctx->refcount++;
    }
}

/**
 * Release context
 */
void nmo_context_release(nmo_context* ctx) {
    if (ctx == NULL) {
        return;
    }

    /* TODO: Add atomic decrement for thread safety */
    ctx->refcount--;

    if (ctx->refcount <= 0) {
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
nmo_schema_registry* nmo_context_get_schema_registry(const nmo_context* ctx) {
    return ctx ? ctx->schema_registry : NULL;
}

/**
 * Get manager registry
 */
nmo_manager_registry* nmo_context_get_manager_registry(const nmo_context* ctx) {
    return ctx ? ctx->manager_registry : NULL;
}

/**
 * Get allocator
 */
nmo_allocator* nmo_context_get_allocator(const nmo_context* ctx) {
    return ctx ? ctx->allocator : NULL;
}

/**
 * Get logger
 */
nmo_logger* nmo_context_get_logger(const nmo_context* ctx) {
    return ctx ? ctx->logger : NULL;
}

/**
 * Get reference count
 */
int nmo_context_get_refcount(const nmo_context* ctx) {
    return ctx ? ctx->refcount : 0;
}
