/**
 * @file param_type_table.c
 * @brief Implementation of Parameter type table for GUID-based lookup
 */

#include "schema/param_type_table.h"
#include "schema/nmo_schema.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_param_meta.h"
#include "core/nmo_arena.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include <string.h>

/* =============================================================================
 * TYPE DEFINITIONS
 * ============================================================================= */

/**
 * @brief Parameter type iterator callback
 * @param guid Parameter GUID
 * @param type Schema type
 * @param userdata User data
 * @return 0 to continue, non-zero to stop
 */
typedef int (*nmo_param_type_iterator_fn)(
    nmo_guid_t guid,
    const nmo_schema_type_t *type,
    void *userdata);

/* =============================================================================
 * INTERNAL HELPERS
 * ============================================================================= */

/* Forward declarations */
static void nmo_param_type_table_iterate(
    const nmo_param_type_table_t *table,
    nmo_param_type_iterator_fn callback,
    void *userdata);

/**
 * @brief Context for collecting parameter types during registry iteration
 */
typedef struct collect_param_types_context {
    nmo_hash_table_t *map;
    nmo_arena_t *arena;
    size_t count;
} collect_param_types_context_t;

/**
 * @brief Callback for registry iteration to collect types with param_meta
 */
static int collect_param_type_callback(
    const nmo_schema_type_t *type,
    void *userdata)
{
    collect_param_types_context_t *ctx = (collect_param_types_context_t *)userdata;
    
    /* Only collect types with parameter metadata */
    if (!type->param_meta) {
        return 1; /* Continue */
    }
    
    /* Use GUID as key, type pointer as value */
    nmo_guid_t guid = type->param_meta->guid;
    const nmo_schema_type_t *type_ptr = type;
    
    /* Insert into hash table */
    nmo_hash_table_insert(ctx->map, &guid, &type_ptr);
    ctx->count++;
    
    return 1; /* Continue */
}

/* =============================================================================
 * TABLE CONSTRUCTION
 * ============================================================================= */

nmo_result_t nmo_param_type_table_build(
    const nmo_schema_registry_t *registry,
    nmo_arena_t *arena,
    nmo_param_type_table_t **out_table)
{
    if (!registry || !arena || !out_table) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to param_type_table_build"));
    }
    
    /* Allocate table structure */
    nmo_param_type_table_t *table = nmo_arena_alloc(arena,
        sizeof(nmo_param_type_table_t), _Alignof(nmo_param_type_table_t));
    if (!table) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate param type table"));
    }
    
    /* Create hash table: key = nmo_guid_t, value = nmo_schema_type_t* */
    table->guid_to_type_map = nmo_hash_table_create(
        NULL,
        sizeof(nmo_guid_t),           /* key_size */
        sizeof(nmo_schema_type_t*),   /* value_size */
        32,                           /* initial_capacity */
        NULL,                         /* hash_func: use default for GUID */
        NULL                          /* compare_func: use memcmp */
    );
    if (!table->guid_to_type_map) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate GUID hash table"));
    }
    
    table->type_count = 0;
    
    /* Iterate registry and collect types with param_meta */
    collect_param_types_context_t ctx = {
        .map = table->guid_to_type_map,
        .arena = arena,
        .count = 0
    };
    
    nmo_schema_registry_iterate(registry, collect_param_type_callback, &ctx);
    table->type_count = ctx.count;
    
    *out_table = table;
    return nmo_result_ok();
}

/* =============================================================================
 * TABLE QUERIES
 * ============================================================================= */

const nmo_schema_type_t *nmo_param_type_table_find(
    const nmo_param_type_table_t *table,
    nmo_guid_t guid)
{
    if (!table || !table->guid_to_type_map) {
        return NULL;
    }
    
    /* Lookup by GUID (direct key comparison) */
    const nmo_schema_type_t *type_ptr = NULL;
    int found = nmo_hash_table_get(table->guid_to_type_map, &guid, &type_ptr);
    
    return found ? type_ptr : NULL;
}

size_t nmo_param_type_table_get_count(
    const nmo_param_type_table_t *table)
{
    return table ? table->type_count : 0;
}

int nmo_param_type_table_contains(
    const nmo_param_type_table_t *table,
    nmo_guid_t guid)
{
    return nmo_param_type_table_find(table, guid) != NULL;
}

/* =============================================================================
 * ITERATION
 * ============================================================================= */

/**
 * @brief Context for iteration adapter
 */
typedef struct iterate_adapter_context {
    nmo_param_type_iterator_fn user_callback;
    void *user_data;
} iterate_adapter_context_t;

/**
 * @brief Adapter callback for hash table iteration
 */
static int iterate_adapter_callback(
    const void *key,
    void *value,
    void *userdata)
{
    iterate_adapter_context_t *ctx = (iterate_adapter_context_t *)userdata;
    
    const nmo_guid_t *guid_ptr = (const nmo_guid_t *)key;
    /* Hash table stores nmo_schema_type_t* as value, so value points to that pointer */
    const nmo_schema_type_t *type = *(const nmo_schema_type_t **)value;
    
    if (!guid_ptr || !type) {
        return 1; /* Continue to next item */
    }
    
    /* Call user callback with GUID and type */
    /* User callback returns 0 to continue, non-zero to stop */
    int user_result = ctx->user_callback(*guid_ptr, type, ctx->user_data);
    return (user_result == 0) ? 1 : 0;  /* Invert: 0->1 (continue), non-zero->0 (stop) */
}

void nmo_param_type_table_iterate(
    const nmo_param_type_table_t *table,
    nmo_param_type_iterator_fn callback,
    void *userdata)
{
    if (!table || !table->guid_to_type_map || !callback) {
        return;
    }
    
    iterate_adapter_context_t ctx = {
        .user_callback = callback,
        .user_data = userdata
    };
    
    nmo_hash_table_iterate(table->guid_to_type_map, iterate_adapter_callback, &ctx);
}

/* =============================================================================
 * LOOKUP
 * ============================================================================= */

const nmo_schema_type_t *nmo_param_type_table_find_by_guid(
    const nmo_param_type_table_t *table,
    nmo_guid_t guid)
{
    if (!table || !table->guid_to_type_map) {
        return NULL;
    }
    
    const nmo_schema_type_t *result = NULL;
    /* Hash table stores nmo_schema_type_t* as value */
    if (nmo_hash_table_get(table->guid_to_type_map, &guid, &result)) {
        return result;
    }
    
    return NULL;
}

/* =============================================================================
 * STATISTICS
 * ============================================================================= */

/**
 * @brief Context for stats collection
 */
typedef struct stats_context {
    nmo_param_type_table_stats_t *stats;
} stats_context_t;

/**
 * @brief Callback to collect statistics
 */
static int collect_stats_callback(
    nmo_guid_t guid,
    const nmo_schema_type_t *type,
    void *userdata)
{
    (void)guid;
    stats_context_t *ctx = (stats_context_t *)userdata;
    
    if (!type || !type->param_meta) {
        return 0; /* Continue */
    }
    
    switch (type->param_meta->kind) {
        case NMO_PARAM_SCALAR:
            ctx->stats->scalar_count++;
            break;
        case NMO_PARAM_STRUCT:
            ctx->stats->struct_count++;
            break;
        case NMO_PARAM_ENUM:
            ctx->stats->enum_count++;
            break;
        case NMO_PARAM_OBJECT_REF:
            ctx->stats->object_ref_count++;
            break;
        default:
            break;
    }
    
    return 0; /* Continue */
}

void nmo_param_type_table_get_stats(
    const nmo_param_type_table_t *table,
    nmo_param_type_table_stats_t *out_stats)
{
    if (!table || !out_stats) {
        return;
    }
    
    /* Initialize stats */
    out_stats->total_types = table->type_count;
    out_stats->scalar_count = 0;
    out_stats->struct_count = 0;
    out_stats->enum_count = 0;
    out_stats->object_ref_count = 0;
    
    /* Collect stats by iterating */
    stats_context_t ctx = { .stats = out_stats };
    nmo_param_type_table_iterate(table, collect_stats_callback, &ctx);
}

/* =============================================================================
 * Phase 5 Improvements: Type Compatibility and Validation
 * ============================================================================= */

bool nmo_param_type_is_compatible(
    const nmo_param_type_table_t *table,
    nmo_guid_t type_guid,
    nmo_guid_t expected_guid)
{
    if (!table) {
        return false;
    }

    /* Exact match */
    if (nmo_guid_equals(type_guid, expected_guid)) {
        return true;
    }

    /* Check inheritance chain */
    nmo_guid_t current = type_guid;
    int depth = 0;
    const int max_depth = 100; /* Prevent infinite loops */

    while (depth < max_depth) {
        const nmo_schema_type_t *type = nmo_param_type_table_find_by_guid(table, current);
        if (!type || !type->param_meta) {
            break;
        }

        /* Check if parent matches expected */
        nmo_guid_t parent = type->param_meta->derived_from;
        if (nmo_guid_is_null(parent)) {
            break; /* Reached base type */
        }

        if (nmo_guid_equals(parent, expected_guid)) {
            return true;
        }

        current = parent;
        depth++;
    }

    return false;
}

int nmo_param_type_get_depth(
    const nmo_param_type_table_t *table,
    nmo_guid_t type_guid)
{
    if (!table) {
        return -1;
    }

    const nmo_schema_type_t *type = nmo_param_type_table_find_by_guid(table, type_guid);
    if (!type || !type->param_meta) {
        return -1; /* Type not found */
    }

    /* Base type has depth 0 */
    if (nmo_guid_is_null(type->param_meta->derived_from)) {
        return 0;
    }

    /* Traverse inheritance chain */
    int depth = 0;
    nmo_guid_t current = type->param_meta->derived_from;
    const int max_depth = 100; /* Prevent infinite loops */

    while (depth < max_depth) {
        const nmo_schema_type_t *parent = nmo_param_type_table_find_by_guid(table, current);
        if (!parent || !parent->param_meta) {
            return -1; /* Broken inheritance chain */
        }

        depth++;

        if (nmo_guid_is_null(parent->param_meta->derived_from)) {
            return depth; /* Reached base type */
        }

        current = parent->param_meta->derived_from;
    }

    return -1; /* Circular dependency or too deep */
}
