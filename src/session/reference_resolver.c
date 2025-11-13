/**
 * @file reference_resolver.c
 * @brief Implementation of object reference resolution system
 *
 * This module implements the reference resolver that matches object references
 * to existing objects in the repository during file loading.
 *
 * Based on: reference/src/CKFile.cpp ResolveReference() (lines 1553-1583)
 */

#include "session/nmo_reference_resolver.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_logger.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief Strategy entry for custom resolution
 */
typedef struct nmo_strategy_entry {
    nmo_class_id_t class_id;           /**< Class ID this strategy applies to (0 for default) */
    nmo_resolve_strategy_fn strategy;  /**< Strategy function */
    void *context;                     /**< User context */
} nmo_strategy_entry_t;

/**
 * @brief Reference resolver implementation
 */
struct nmo_reference_resolver {
    /* Dependencies */
    nmo_object_repository_t *repo;     /**< Object repository to resolve against */
    nmo_arena_t *arena;                /**< Arena for allocations */
    nmo_logger_t *logger;              /**< Logger for diagnostics */
    
    /* Strategies */
    nmo_strategy_entry_t *strategies;  /**< Array of registered strategies */
    size_t strategy_count;             /**< Number of registered strategies */
    size_t strategy_capacity;          /**< Capacity of strategies array */
    
    /* Pending references */
    nmo_object_ref_t **pending_refs;   /**< Array of reference pointers */
    size_t pending_count;              /**< Number of pending references */
    size_t pending_capacity;           /**< Capacity of pending array */
    
    /* Resolution results */
    nmo_object_ref_t **unresolved_refs; /**< Array of unresolved reference pointers */
    size_t unresolved_count;           /**< Number of unresolved references */
    
    /* Statistics */
    nmo_reference_stats_t stats;       /**< Resolution statistics */
};

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

/**
 * @brief Find strategy for given class ID
 *
 * @param resolver Resolver instance
 * @param class_id Class ID to find strategy for
 * @return Strategy entry or NULL if not found
 */
static nmo_strategy_entry_t *find_strategy(
    nmo_reference_resolver_t *resolver,
    nmo_class_id_t class_id
) {
    if (!resolver || !resolver->strategies) {
        return NULL;
    }
    
    /* First try exact class match */
    for (size_t i = 0; i < resolver->strategy_count; i++) {
        if (resolver->strategies[i].class_id == class_id) {
            return &resolver->strategies[i];
        }
    }
    
    /* Fall back to default strategy (class_id == 0) */
    for (size_t i = 0; i < resolver->strategy_count; i++) {
        if (resolver->strategies[i].class_id == 0) {
            return &resolver->strategies[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Grow strategies array if needed
 */
static int grow_strategies(nmo_reference_resolver_t *resolver) {
    if (resolver->strategy_count < resolver->strategy_capacity) {
        return NMO_OK;
    }
    
    size_t new_capacity = resolver->strategy_capacity == 0 ? 8 : resolver->strategy_capacity * 2;
    nmo_strategy_entry_t *new_strategies = nmo_arena_alloc(
        resolver->arena,
        sizeof(nmo_strategy_entry_t) * new_capacity,
        1
    );
    
    if (!new_strategies) {
        return NMO_ERR_NOMEM;
    }
    
    if (resolver->strategies) {
        memcpy(new_strategies, resolver->strategies, 
               sizeof(nmo_strategy_entry_t) * resolver->strategy_count);
    }
    
    resolver->strategies = new_strategies;
    resolver->strategy_capacity = new_capacity;
    
    return NMO_OK;
}

/**
 * @brief Grow pending references array if needed
 */
static int grow_pending(nmo_reference_resolver_t *resolver) {
    if (resolver->pending_count < resolver->pending_capacity) {
        return NMO_OK;
    }
    
    size_t new_capacity = resolver->pending_capacity == 0 ? 32 : resolver->pending_capacity * 2;
    nmo_object_ref_t **new_pending = nmo_arena_alloc(
        resolver->arena,
        sizeof(nmo_object_ref_t *) * new_capacity,
        1
    );
    
    if (!new_pending) {
        return NMO_ERR_NOMEM;
    }
    
    if (resolver->pending_refs) {
        memcpy(new_pending, resolver->pending_refs,
               sizeof(nmo_object_ref_t *) * resolver->pending_count);
    }
    
    resolver->pending_refs = new_pending;
    resolver->pending_capacity = new_capacity;
    
    return NMO_OK;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

nmo_reference_resolver_t *nmo_reference_resolver_create(
    nmo_object_repository_t *repo,
    nmo_arena_t *arena
) {
    if (!repo || !arena) {
        return NULL;
    }
    
    nmo_reference_resolver_t *resolver = nmo_arena_alloc(
        arena,
        sizeof(nmo_reference_resolver_t),
        1
    );
    
    if (!resolver) {
        return NULL;
    }
    
    memset(resolver, 0, sizeof(nmo_reference_resolver_t));
    
    resolver->repo = repo;
    resolver->arena = arena;
    resolver->logger = NULL; /* Will be set by context if needed */
    
    return resolver;
}

int nmo_reference_resolver_register_strategy(
    nmo_reference_resolver_t *resolver,
    nmo_class_id_t class_id,
    nmo_resolve_strategy_fn strategy,
    void *context
) {
    if (!resolver || !strategy) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    int result = grow_strategies(resolver);
    if (result != NMO_OK) {
        return result;
    }
    
    nmo_strategy_entry_t *entry = &resolver->strategies[resolver->strategy_count++];
    entry->class_id = class_id;
    entry->strategy = strategy;
    entry->context = context;
    
    return NMO_OK;
}

nmo_object_ref_t *nmo_reference_resolver_register_reference(
    nmo_reference_resolver_t *resolver,
    const nmo_object_ref_t *ref
) {
    if (!resolver || !ref) {
        return NULL;
    }
    
    int result = grow_pending(resolver);
    if (result != NMO_OK) {
        return NULL;
    }
    
    /* Copy reference to arena */
    nmo_object_ref_t *ref_copy = nmo_arena_alloc(
        resolver->arena,
        sizeof(nmo_object_ref_t),
        1
    );
    
    if (!ref_copy) {
        return NULL;
    }
    
    memcpy(ref_copy, ref, sizeof(nmo_object_ref_t));
    
    /* Copy name string if present */
    if (ref->name) {
        size_t name_len = strlen(ref->name) + 1;
        char *name_copy = nmo_arena_alloc(resolver->arena, name_len, 1);
        if (name_copy) {
            memcpy(name_copy, ref->name, name_len);
            ref_copy->name = name_copy;
        }
    }
    
    resolver->pending_refs[resolver->pending_count++] = ref_copy;
    resolver->stats.total_count++;
    
    return ref_copy;
}

nmo_object_t *nmo_reference_resolver_resolve(
    nmo_reference_resolver_t *resolver,
    const nmo_object_ref_t *ref
) {
    if (!resolver || !ref) {
        return NULL;
    }
    
    nmo_object_t *resolved = NULL;
    
    /* Try custom strategy for this class */
    nmo_strategy_entry_t *strategy = find_strategy(resolver, ref->class_id);
    if (strategy) {
        resolved = strategy->strategy(strategy->context, ref, resolver->repo);
        if (resolved) {
            resolver->stats.resolved_count++;
            return resolved;
        }
    }
    
    /* Try default strategy if no custom strategy or custom failed */
    resolved = nmo_resolve_strategy_default(NULL, ref, resolver->repo);
    if (resolved) {
        resolver->stats.resolved_count++;
        return resolved;
    }
    
    /* Try fuzzy matching as last resort */
    resolved = nmo_resolve_strategy_fuzzy(NULL, ref, resolver->repo);
    if (resolved) {
        resolver->stats.resolved_count++;
        resolver->stats.ambiguous_count++;
        
        if (resolver->logger) {
            nmo_log_warn(resolver->logger,
                "Reference resolved with fuzzy matching: %s (ID=%u, Class=%u)",
                ref->name ? ref->name : "(unnamed)",
                ref->id,
                ref->class_id);
        }
        
        return resolved;
    }
    
    /* Resolution failed */
    resolver->stats.unresolved_count++;
    
    if (resolver->logger) {
        nmo_log_error(resolver->logger,
            "Failed to resolve reference: %s (ID=%u, Class=%u)",
            ref->name ? ref->name : "(unnamed)",
            ref->id,
            ref->class_id);
    }
    
    return NULL;
}

int nmo_reference_resolver_resolve_all(
    nmo_reference_resolver_t *resolver
) {
    if (!resolver) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Allocate unresolved list */
    resolver->unresolved_refs = nmo_arena_alloc(
        resolver->arena,
        sizeof(nmo_object_ref_t *) * resolver->pending_count,
        1
    );
    
    if (!resolver->unresolved_refs && resolver->pending_count > 0) {
        return NMO_ERR_NOMEM;
    }
    
    resolver->unresolved_count = 0;
    
    /* Resolve each pending reference */
    for (size_t i = 0; i < resolver->pending_count; i++) {
        nmo_object_ref_t *ref = resolver->pending_refs[i];
        nmo_object_t *resolved = nmo_reference_resolver_resolve(resolver, ref);
        
        if (resolved) {
            ref->resolved_object = resolved;
        } else {
            /* Add to unresolved list */
            resolver->unresolved_refs[resolver->unresolved_count++] = ref;
        }
    }
    
    /* Clear pending list */
    resolver->pending_count = 0;
    
    /* Return warning if some references are unresolved */
    if (resolver->unresolved_count > 0) {
        return NMO_OK;  /* Not an error, just some references unresolved */
    }
    
    return NMO_OK;
}

int nmo_reference_resolver_get_stats(
    const nmo_reference_resolver_t *resolver,
    nmo_reference_stats_t *out_stats
) {
    if (!resolver || !out_stats) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    memcpy(out_stats, &resolver->stats, sizeof(nmo_reference_stats_t));
    
    return NMO_OK;
}

int nmo_reference_resolver_has_unresolved(
    const nmo_reference_resolver_t *resolver
) {
    if (!resolver) {
        return 0;
    }
    
    return resolver->unresolved_count > 0 ? 1 : 0;
}

int nmo_reference_resolver_get_unresolved(
    const nmo_reference_resolver_t *resolver,
    nmo_object_ref_t ***out_refs,
    size_t *out_count
) {
    if (!resolver || !out_refs || !out_count) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    *out_refs = resolver->unresolved_refs;
    *out_count = resolver->unresolved_count;
    
    return NMO_OK;
}

void nmo_reference_resolver_destroy(
    nmo_reference_resolver_t *resolver
) {
    /* Nothing to do - arena allocation handles cleanup */
    (void)resolver;
}

/* ========================================================================
 * Built-in Resolution Strategies
 * ======================================================================== */

nmo_object_t *nmo_resolve_strategy_default(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
) {
    (void)context; /* Unused */
    
    if (!ref || !repo) {
        return NULL;
    }
    
    /* Get all objects of this class */
    size_t count = 0;
    nmo_object_t **objects = nmo_object_repository_find_by_class(
        repo,
        ref->class_id,
        &count
    );
    
    if (!objects || count == 0) {
        return NULL;
    }
    
    /* If no name, we can't match */
    if (!ref->name) {
        return NULL;
    }
    
    /* Find exact name match */
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = objects[i];
        const char *obj_name = nmo_object_get_name(obj);
        
        if (obj_name && strcmp(obj_name, ref->name) == 0) {
            return obj;
        }
    }
    
    return NULL;
}

nmo_object_t *nmo_resolve_strategy_parameter(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
) {
    (void)context; /* Unused */
    
    if (!ref || !repo) {
        return NULL;
    }
    
    /* This is a specialized version that also matches type GUID */
    /* Based on reference/src/CKFile.cpp:1553-1583 */
    
    /* Get all objects of this class */
    size_t count = 0;
    nmo_object_t **objects = nmo_object_repository_find_by_class(
        repo,
        ref->class_id,
        &count
    );
    
    if (!objects || count == 0) {
        return NULL;
    }
    
    /* If no name, we can't match */
    if (!ref->name) {
        return NULL;
    }
    
    /* Find match by name AND type GUID */
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = objects[i];
        const char *obj_name = nmo_object_get_name(obj);
        
        if (!obj_name || strcmp(obj_name, ref->name) != 0) {
            continue;
        }
        
        /* Check type GUID if available */
        if (!nmo_guid_is_null(ref->type_guid)) {
            nmo_guid_t obj_type = nmo_object_get_type_guid(obj);
            if (nmo_guid_equals(obj_type, ref->type_guid)) {
                return obj;
            }
        } else {
            /* No type GUID in reference, name match is enough */
            return obj;
        }
    }
    
    return NULL;
}

nmo_object_t *nmo_resolve_strategy_guid(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
) {
    (void)context; /* Unused */
    
    if (!ref || !repo) {
        return NULL;
    }
    
    /* If no valid GUID, can't use this strategy */
    if (nmo_guid_is_null(ref->type_guid)) {
        return NULL;
    }
    
    /* Search all objects for GUID match */
    size_t total_count = nmo_object_repository_get_count(repo);
    
    for (size_t i = 0; i < total_count; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo, i);
        if (!obj) {
            continue;
        }
        
        nmo_guid_t obj_guid = nmo_object_get_type_guid(obj);
        if (nmo_guid_equals(obj_guid, ref->type_guid)) {
            return obj;
        }
    }
    
    return NULL;
}

nmo_object_t *nmo_resolve_strategy_fuzzy(
    void *context,
    const nmo_object_ref_t *ref,
    nmo_object_repository_t *repo
) {
    (void)context; /* Unused */
    
    if (!ref || !repo || !ref->name) {
        return NULL;
    }
    
    /* Get all objects of this class */
    size_t count = 0;
    nmo_object_t **objects = nmo_object_repository_find_by_class(
        repo,
        ref->class_id,
        &count
    );
    
    if (!objects || count == 0) {
        return NULL;
    }
    
    /* Case-insensitive name matching */
    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = objects[i];
        const char *obj_name = nmo_object_get_name(obj);
        
        if (obj_name) {
#ifdef _WIN32
            if (_stricmp(obj_name, ref->name) == 0) {
                return obj;
            }
#else
            if (strcasecmp(obj_name, ref->name) == 0) {
                return obj;
            }
#endif
        }
    }
    
    return NULL;
}
