/**
 * @file operation_registry.c
 * @brief Operation registry implementation (Phase 6.1)
 */

#include "type/nmo_operation_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_logger.h"
#include "core/nmo_guid.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief Hash function wrapper for GUID keys
 */
static size_t guid_hash_wrapper(const void *key, size_t key_size) {
    (void)key_size;
    const nmo_guid_t *guid = (const nmo_guid_t *)key;
    return (size_t)nmo_guid_hash(*guid);
}

/**
 * @brief Compare function wrapper for GUID keys
 */
static int guid_compare_wrapper(const void *key1, const void *key2, size_t key_size) {
    (void)key_size;
    const nmo_guid_t *g1 = (const nmo_guid_t *)key1;
    const nmo_guid_t *g2 = (const nmo_guid_t *)key2;
    return nmo_guid_equals(*g1, *g2) ? 0 : -1;
}

typedef struct nmo_operation_cache_key {
    nmo_guid_t operation_guid;
    nmo_type_id_t p1_type_id;
    nmo_type_id_t p2_type_id;
    nmo_guid_t result_type_guid;
} nmo_operation_cache_key_t;

static size_t operation_cache_hash(const void *key, size_t key_size) {
    (void)key_size;
    const nmo_operation_cache_key_t *k = (const nmo_operation_cache_key_t *)key;
    size_t h = (size_t)nmo_guid_hash(k->operation_guid);
    h ^= (size_t)k->p1_type_id * 0x9e3779b1u;
    h ^= (size_t)k->p2_type_id * 0x85ebca6bu;
    h ^= (size_t)nmo_guid_hash(k->result_type_guid) << 1u;
    return h;
}

static int operation_cache_compare(const void *key1, const void *key2, size_t key_size) {
    (void)key_size;
    const nmo_operation_cache_key_t *a = (const nmo_operation_cache_key_t *)key1;
    const nmo_operation_cache_key_t *b = (const nmo_operation_cache_key_t *)key2;
    if (!nmo_guid_equals(a->operation_guid, b->operation_guid)) {
        return -1;
    }
    if (a->p1_type_id != b->p1_type_id || a->p2_type_id != b->p2_type_id) {
        return -1;
    }
    return nmo_guid_equals(a->result_type_guid, b->result_type_guid) ? 0 : -1;
}

static void ensure_lookup_cache(
    nmo_operation_registry_t *registry,
    const nmo_type_registry_t *type_registry)
{
    if (!registry) {
        return;
    }

    uint32_t type_version = type_registry ? type_registry->registry_version : 0u;
    if (!registry->lookup_cache ||
        registry->cache_version != registry->registry_version ||
        registry->cached_type_registry_version != type_version) {
        if (registry->lookup_cache) {
            nmo_hash_table_destroy(registry->lookup_cache);
        }
        registry->lookup_cache = nmo_hash_table_create(
            NULL,
            sizeof(nmo_operation_cache_key_t),
            sizeof(const nmo_operation_tree_cell_t *),
            128,
            operation_cache_hash,
            operation_cache_compare);
        registry->cache_version = registry->registry_version;
        registry->cached_type_registry_version = type_version;
    }
}

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * @brief Find or create operation family by GUID
 */
static nmo_operation_family_t *find_or_create_family(
    nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid
) {
    /* Search in hash map first (O(1)) */
    uint32_t family_index = 0;
    if (nmo_hash_table_get(registry->family_map, operation_guid, &family_index) == NMO_OK) {
        return *(nmo_operation_family_t **)nmo_arena_array_get(&registry->families, family_index);
    }

    /* Not found, create new family */

    /* Allocate new family */
    nmo_operation_family_t *family = (nmo_operation_family_t *)nmo_arena_alloc(
        registry->arena, sizeof(nmo_operation_family_t), _Alignof(nmo_operation_family_t)
    );
    if (!family) {
        return NULL;
    }

    memset(family, 0, sizeof(*family));
    family->operation_guid = *operation_guid;

    /* Initial capacity for P1 layers */
    if (nmo_arena_array_init(&family->p1_layers, sizeof(nmo_operation_p1_layer_t), 4, registry->arena) != NMO_OK) {
        return NULL;
    }

    /* Add to registry (commit after map insertion) */
    family_index = (uint32_t)registry->families.count;
    if (nmo_arena_array_append(&registry->families, &family) != NMO_OK) {
        return NULL;
    }

    /* Add to hash map */
    if (nmo_hash_table_insert(registry->family_map, operation_guid, &family_index) != NMO_OK) {
        /* Roll back the append by decrementing count */
        registry->families.count--;
        return NULL;
    }

    return family;
}

/**
 * @brief Find or create P1 layer in family
 */
static nmo_operation_p1_layer_t *find_or_create_p1_layer(
    nmo_operation_registry_t *registry,
    nmo_operation_family_t *family,
    const nmo_guid_t *p1_type_guid
) {
    /* Linear search (small array, typically < 10 elements) */
    for (size_t i = 0; i < family->p1_layers.count; i++) {
        nmo_operation_p1_layer_t *layer = (nmo_operation_p1_layer_t *)nmo_arena_array_get(&family->p1_layers, i);
        if (nmo_guid_equals(layer->p1_type_guid, *p1_type_guid)) {
            return layer;
        }
    }

    /* Not found, create new P1 layer */
    nmo_operation_p1_layer_t *p1_layer = NULL;
    if (nmo_arena_array_extend(&family->p1_layers, 1, (void **)&p1_layer) != NMO_OK) {
        return NULL;
    }

    /* Initialize new P1 layer */
    memset(p1_layer, 0, sizeof(*p1_layer));
    p1_layer->p1_type_guid = *p1_type_guid;

    /* Initial capacity for P2 layers */
    if (nmo_arena_array_init(&p1_layer->p2_layers, sizeof(nmo_operation_p2_layer_t), 4, registry->arena) != NMO_OK) {
        return NULL;
    }

    return p1_layer;
}

/**
 * @brief Find or create P2 layer in P1 layer
 */
static nmo_operation_p2_layer_t *find_or_create_p2_layer(
    nmo_operation_registry_t *registry,
    nmo_operation_p1_layer_t *p1_layer,
    const nmo_guid_t *p2_type_guid
) {
    /* Linear search */
    for (size_t i = 0; i < p1_layer->p2_layers.count; i++) {
        nmo_operation_p2_layer_t *layer = (nmo_operation_p2_layer_t *)nmo_arena_array_get(&p1_layer->p2_layers, i);
        if (nmo_guid_equals(layer->p2_type_guid, *p2_type_guid)) {
            return layer;
        }
    }

    /* Not found, create new P2 layer */
    nmo_operation_p2_layer_t *p2_layer = NULL;
    if (nmo_arena_array_extend(&p1_layer->p2_layers, 1, (void **)&p2_layer) != NMO_OK) {
        return NULL;
    }

    /* Initialize new P2 layer */
    memset(p2_layer, 0, sizeof(*p2_layer));
    p2_layer->p2_type_guid = *p2_type_guid;

    /* Initial capacity for cells */
    if (nmo_arena_array_init(&p2_layer->cells, sizeof(nmo_operation_tree_cell_t), 4, registry->arena) != NMO_OK) {
        return NULL;
    }

    return p2_layer;
}

/**
 * @brief Insert or update operation cell in P2 layer
 */
static nmo_status_t insert_operation_cell(
    nmo_operation_registry_t *registry,
    nmo_operation_p2_layer_t *p2_layer,
    const nmo_operation_desc_t *desc,
    const nmo_type_descriptor_t *p1_type,
    const nmo_type_descriptor_t *p2_type,
    const nmo_type_descriptor_t *result_type
) {
    /* Check if operation already exists (by result type GUID) */
    for (size_t i = 0; i < p2_layer->cells.count; i++) {
        nmo_operation_tree_cell_t *cell = (nmo_operation_tree_cell_t *)nmo_arena_array_get(&p2_layer->cells, i);
        if (nmo_guid_equals(cell->desc.result_type_guid, desc->result_type_guid)) {
            /* Override policy: non-NULL function always beats NULL function
             * (C implementation replaces JSON stub). When both are the same
             * NULL/non-NULL status, higher priority wins. */
            bool should_override = false;
            if (desc->function && !cell->desc.function) {
                should_override = true;
            } else if (!desc->function && cell->desc.function) {
                should_override = false;
            } else {
                should_override = (desc->priority > cell->desc.priority);
            }
            if (should_override) {
                cell->desc = *desc;
                cell->p1_type = p1_type;
                cell->p2_type = p2_type;
                cell->result_type = result_type;
            }
            NMO_RETURN_OK();
        }
    }

    /* Not found, add new cell */
    nmo_operation_tree_cell_t *cell = NULL;
    if (nmo_arena_array_extend(&p2_layer->cells, 1, (void **)&cell) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to expand cell array");
    }

    /* Initialize new cell */
    memset(cell, 0, sizeof(*cell));
    cell->desc = *desc;
    cell->p1_type = p1_type;
    cell->p2_type = p2_type;
    cell->result_type = result_type;

    registry->total_operations++;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Registry Lifecycle
 * ============================================================================ */

nmo_operation_registry_t *nmo_operation_registry_create(nmo_arena_t *arena) {
    if (!arena) {
        return NULL;
    }

    nmo_operation_registry_t *registry = (nmo_operation_registry_t *)nmo_arena_alloc(
        arena, sizeof(nmo_operation_registry_t), _Alignof(nmo_operation_registry_t)
    );
    if (!registry) {
        return NULL;
    }

    memset(registry, 0, sizeof(*registry));
    registry->arena = arena;
    registry->registry_version = 0;
    registry->cache_version = 0;
    registry->cached_type_registry_version = 0;
    registry->finalized = false;

    /* Create hash map for O(1) family lookup: GUID -> family index */
    registry->family_map = nmo_hash_table_create(
        NULL,                      /* allocator - use default */
        sizeof(nmo_guid_t),        /* key size */
        sizeof(uint32_t),          /* value size (family index) */
        64,                        /* initial capacity */
        guid_hash_wrapper,         /* hash function */
        guid_compare_wrapper       /* compare function */
    );
    if (!registry->family_map) {
        return NULL;
    }

    /* Allocate initial family array */
    if (nmo_arena_array_init(&registry->families, sizeof(nmo_operation_family_t *), 16, arena) != NMO_OK) {
        nmo_hash_table_destroy(registry->family_map);
        registry->family_map = NULL;
        return NULL;
    }

    registry->lookup_cache = nmo_hash_table_create(
        NULL,
        sizeof(nmo_operation_cache_key_t),
        sizeof(const nmo_operation_tree_cell_t *),
        128,
        operation_cache_hash,
        operation_cache_compare
    );
    if (!registry->lookup_cache) {
        nmo_hash_table_destroy(registry->family_map);
        registry->family_map = NULL;
        return NULL;
    }

    return registry;
}

void nmo_operation_registry_destroy(nmo_operation_registry_t *registry) {
    if (!registry) {
        return;
    }

    if (registry->family_map) {
        nmo_hash_table_destroy(registry->family_map);
        registry->family_map = NULL;
    }

    if (registry->lookup_cache) {
        nmo_hash_table_destroy(registry->lookup_cache);
        registry->lookup_cache = NULL;
    }
}

nmo_status_t nmo_operation_registry_finalize(
    nmo_operation_registry_t *registry,
    const nmo_type_registry_t *type_registry)
{
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid operation registry");
    }

    ensure_lookup_cache(registry, type_registry);
    registry->finalized = true;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Operation Registration
 * ============================================================================ */

nmo_status_t nmo_operation_registry_register(
    nmo_operation_registry_t *registry,
    const nmo_operation_desc_t *desc,
    const nmo_type_registry_t *type_registry
) {
    if (!registry || !desc || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid parameters");
    }

    if (registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Operation registry is finalized; cannot register operations");
    }

    /* Resolve type descriptors from type registry */
    const nmo_type_descriptor_t *p1_type = nmo_type_registry_find_by_guid(
        type_registry, desc->p1_type_guid
    );
    if (!p1_type) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "P1 type not found in type registry (op='%s')",
                                desc->name ? desc->name : "<unnamed>");
    }

    const nmo_type_descriptor_t *p2_type = NULL;
    if (!(desc->flags & NMO_OP_UNARY)) {
        p2_type = nmo_type_registry_find_by_guid(type_registry, desc->p2_type_guid);
        if (!p2_type) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                    "P2 type not found in type registry (op='%s')",
                                    desc->name ? desc->name : "<unnamed>");
        }
    }

    const nmo_type_descriptor_t *result_type = nmo_type_registry_find_by_guid(
        type_registry, desc->result_type_guid
    );
    if (!result_type) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "Result type not found in type registry (op='%s')",
                                desc->name ? desc->name : "<unnamed>");
    }

    /* Navigate 4D tree: Operation -> P1 -> P2 -> Cell */
    nmo_operation_family_t *family = find_or_create_family(registry, &desc->operation_guid);
    if (!family) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to create operation family");
    }

    nmo_operation_p1_layer_t *p1_layer = find_or_create_p1_layer(
        registry, family, &desc->p1_type_guid
    );
    if (!p1_layer) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to create P1 layer");
    }

    /* For unary operations, use NULL_GUID for P2 */
    nmo_guid_t p2_guid = (desc->flags & NMO_OP_UNARY) ?
        (nmo_guid_t){0, 0} : desc->p2_type_guid;

    nmo_operation_p2_layer_t *p2_layer = find_or_create_p2_layer(
        registry, p1_layer, &p2_guid
    );
    if (!p2_layer) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                "Failed to create P2 layer");
    }

    /* Insert operation cell */
    nmo_status_t result = insert_operation_cell(
        registry, p2_layer, desc, p1_type, p2_type, result_type
    );

    if (result == NMO_OK) {
        if (!family->name) {
            family->name = desc->name;
            family->description = desc->description;
        }
        family->total_operations++; /* registration events, may count overrides */
        registry->registry_version++;
    }

    return result;
}

nmo_status_t nmo_operation_registry_register_bulk(
    nmo_operation_registry_t *registry,
    const nmo_operation_desc_t *descs,
    uint32_t count,
    const nmo_type_registry_t *type_registry,
    nmo_logger_t *logger
) {
    if (!registry || !descs || count == 0 || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid parameters");
    }

    if (registry->finalized) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Operation registry is finalized; cannot register operations");
    }

    /* Register operations one by one */
    uint32_t success_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        nmo_status_t result = nmo_operation_registry_register(
            registry, &descs[i], type_registry
        );

        if (result != NMO_OK) {
            /* Log error if logger provided */
            if (logger) {
                char err_msg[256];
                nmo_last_error_message_copy(err_msg, sizeof(err_msg));
                nmo_log_warn(logger, "Failed to register operation %u (%s): %s",
                           i, descs[i].name ? descs[i].name : "unnamed",
                           err_msg[0] ? err_msg : "Unknown error");
            }
        } else {
            success_count++;
        }
    }

    /* Return success if at least one operation registered */
    if (success_count == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                                "Failed to register any operations");
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Operation Lookup
 * ============================================================================ */

/**
 * @brief Find best matching cell (any result type, highest priority)
 */
static const nmo_operation_tree_cell_t *find_cell_best(
    const nmo_operation_p2_layer_t *p2_layer
) {
    if (p2_layer->cells.count == 0) {
        return NULL;
    }

    /* Return first cell (or highest priority if we track that) */
    const nmo_operation_tree_cell_t *best_cell = (const nmo_operation_tree_cell_t *)nmo_arena_array_get(&p2_layer->cells, 0);
    uint32_t best_priority = best_cell->desc.priority;

    for (size_t i = 1; i < p2_layer->cells.count; i++) {
        const nmo_operation_tree_cell_t *cell = (const nmo_operation_tree_cell_t *)nmo_arena_array_get(&p2_layer->cells, i);
        if (cell->desc.priority > best_priority) {
            best_cell = cell;
            best_priority = cell->desc.priority;
        }
    }

    return best_cell;
}

/**
 * @brief Find exact cell by result type GUID
 */
static const nmo_operation_tree_cell_t *find_cell_by_result(
    const nmo_operation_p2_layer_t *p2_layer,
    nmo_guid_t result_type_guid
) {
    if (!p2_layer || p2_layer->cells.count == 0) {
        return NULL;
    }

    for (size_t i = 0; i < p2_layer->cells.count; i++) {
        const nmo_operation_tree_cell_t *cell = (const nmo_operation_tree_cell_t *)nmo_arena_array_get(&p2_layer->cells, i);
        if (nmo_guid_equals(cell->desc.result_type_guid, result_type_guid)) {
            return cell;
        }
    }

    return NULL;
}

static nmo_status_t find_p2_layer(
    nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid,
    const nmo_type_descriptor_t *p1_type,
    const nmo_type_descriptor_t *p2_type,
    const nmo_type_registry_t *type_registry,
    const nmo_operation_p2_layer_t **out_p2_layer
) {
    if (!registry || !operation_guid || !p1_type || !out_p2_layer) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid parameters");
    }

    *out_p2_layer = NULL;

    /* Step 1: Find operation family by GUID (O(1) hash lookup) */
    uint32_t family_index = 0;
    if (nmo_hash_table_get(registry->family_map, operation_guid, &family_index) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                    "Operation family not found");
    }

    const nmo_operation_family_t *family = *(nmo_operation_family_t **)nmo_arena_array_get(&registry->families, family_index);
    if (!family) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Family index points to NULL");
    }

    /* Step 2: Find P1 layer by type GUID (linear search with inheritance fallback) */
    const nmo_operation_p1_layer_t *p1_layer = NULL;
    const nmo_operation_p1_layer_t *compatible_p1_layer = NULL;
    int32_t best_p1_depth = -1;

    /* First try exact match */
    for (size_t i = 0; i < family->p1_layers.count; i++) {
        nmo_operation_p1_layer_t *candidate = (nmo_operation_p1_layer_t *)nmo_arena_array_get(&family->p1_layers, i);
        if (nmo_guid_equals(candidate->p1_type_guid, p1_type->guid)) {
            p1_layer = candidate;
            break;
        }
    }

    /* If no exact match and type_registry available, try inheritance matching */
    if (!p1_layer && type_registry) {
        nmo_type_id_t p1_type_id = nmo_type_registry_guid_to_type_id(
            type_registry, p1_type->guid);

        if (p1_type_id != NMO_TYPE_ID_INVALID) {
            /* Find best compatible type (closest parent) */
            for (size_t i = 0; i < family->p1_layers.count; i++) {
                nmo_operation_p1_layer_t *candidate = (nmo_operation_p1_layer_t *)nmo_arena_array_get(&family->p1_layers, i);
                nmo_type_id_t candidate_id = nmo_type_registry_guid_to_type_id(
                    type_registry, candidate->p1_type_guid);

                if (candidate_id != NMO_TYPE_ID_INVALID) {
                    /* Check if p1_type derives from candidate */
                    int32_t depth = nmo_type_get_derivation_depth(
                        (nmo_type_registry_t *)type_registry, p1_type_id, candidate_id);

                    /* Update if better match (closer parent = lower depth) */
                    if (depth >= 0 && (best_p1_depth < 0 || depth < best_p1_depth)) {
                        compatible_p1_layer = candidate;
                        best_p1_depth = depth;
                    }
                }
            }

            p1_layer = compatible_p1_layer;
        }
    }

    if (!p1_layer) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "P1 type not found in family (no compatible type)");
    }

    /* Step 3: Find P2 layer by type GUID (linear search with inheritance fallback) */
    nmo_guid_t p2_guid = p2_type ? p2_type->guid : (nmo_guid_t){0, 0};
    const nmo_operation_p2_layer_t *p2_layer = NULL;
    const nmo_operation_p2_layer_t *compatible_p2_layer = NULL;
    int32_t best_p2_depth = -1;

    /* First try exact match */
    for (size_t i = 0; i < p1_layer->p2_layers.count; i++) {
        nmo_operation_p2_layer_t *candidate = (nmo_operation_p2_layer_t *)nmo_arena_array_get(&p1_layer->p2_layers, i);
        if (nmo_guid_equals(candidate->p2_type_guid, p2_guid)) {
            p2_layer = candidate;
            break;
        }
    }

    /* If no exact match and type_registry available, try inheritance matching */
    if (!p2_layer && p2_type && type_registry) {
        nmo_type_id_t p2_type_id = nmo_type_registry_guid_to_type_id(
            type_registry, p2_guid);

        if (p2_type_id != NMO_TYPE_ID_INVALID) {
            /* Find best compatible type (closest parent) */
            for (size_t i = 0; i < p1_layer->p2_layers.count; i++) {
                nmo_operation_p2_layer_t *candidate = (nmo_operation_p2_layer_t *)nmo_arena_array_get(&p1_layer->p2_layers, i);
                nmo_type_id_t candidate_id = nmo_type_registry_guid_to_type_id(
                    type_registry, candidate->p2_type_guid);

                if (candidate_id != NMO_TYPE_ID_INVALID) {
                    /* Check if p2_type derives from candidate */
                    int32_t depth = nmo_type_get_derivation_depth(
                        (nmo_type_registry_t *)type_registry, p2_type_id, candidate_id);

                    /* Update if better match (closer parent = lower depth) */
                    if (depth >= 0 && (best_p2_depth < 0 || depth < best_p2_depth)) {
                        compatible_p2_layer = candidate;
                        best_p2_depth = depth;
                    }
                }
            }

            p2_layer = compatible_p2_layer;
        }
    }

    if (!p2_layer) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "P2 type not found in P1 layer (no compatible type)");
    }

    *out_p2_layer = p2_layer;
    NMO_RETURN_OK();
}

nmo_status_t nmo_operation_registry_find_typed(
    nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid,
    const nmo_type_descriptor_t *p1_type,
    const nmo_type_descriptor_t *p2_type,
    const nmo_type_descriptor_t *result_type,
    const nmo_type_registry_t *type_registry,
    const nmo_operation_tree_cell_t **out_cell
) {
    if (!registry || !operation_guid || !p1_type || !result_type || !out_cell) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid parameters");
    }

    *out_cell = NULL;
    registry->total_lookups++;

    if (type_registry) {
        ensure_lookup_cache(registry, type_registry);

        nmo_type_id_t p1_id = nmo_type_registry_guid_to_type_id(type_registry, p1_type->guid);
        nmo_type_id_t p2_id = NMO_TYPE_ID_INVALID;
        if (p2_type) {
            p2_id = nmo_type_registry_guid_to_type_id(type_registry, p2_type->guid);
        }

        if (p1_id != NMO_TYPE_ID_INVALID &&
            (p2_type == NULL || p2_id != NMO_TYPE_ID_INVALID)) {
            nmo_operation_cache_key_t key = {
                .operation_guid = *operation_guid,
                .p1_type_id = p1_id,
                .p2_type_id = p2_id,
                .result_type_guid = (nmo_guid_t){0, 0}
            };
            const nmo_operation_tree_cell_t *cached_cell = NULL;
            if (registry->lookup_cache &&
                nmo_hash_table_get(registry->lookup_cache, &key, &cached_cell) == NMO_OK) {
                nmo_operation_tree_cell_t *mutable_cell = (nmo_operation_tree_cell_t *)cached_cell;
                mutable_cell->call_count++;
                registry->cache_hits++;
                *out_cell = cached_cell;
                return NMO_OK;
            }
        }
    }

    if (type_registry) {
        ensure_lookup_cache(registry, type_registry);

        nmo_type_id_t p1_id = nmo_type_registry_guid_to_type_id(type_registry, p1_type->guid);
        nmo_type_id_t p2_id = NMO_TYPE_ID_INVALID;
        if (p2_type) {
            p2_id = nmo_type_registry_guid_to_type_id(type_registry, p2_type->guid);
        }

        if (p1_id != NMO_TYPE_ID_INVALID &&
            (p2_type == NULL || p2_id != NMO_TYPE_ID_INVALID)) {
            nmo_operation_cache_key_t key = {
                .operation_guid = *operation_guid,
                .p1_type_id = p1_id,
                .p2_type_id = p2_id,
                .result_type_guid = result_type->guid
            };
            const nmo_operation_tree_cell_t *cached_cell = NULL;
            if (registry->lookup_cache &&
                nmo_hash_table_get(registry->lookup_cache, &key, &cached_cell) == NMO_OK) {
                nmo_operation_tree_cell_t *mutable_cell = (nmo_operation_tree_cell_t *)cached_cell;
                mutable_cell->call_count++;
                registry->cache_hits++;
                *out_cell = cached_cell;
                return NMO_OK;
            }
        }
    }

    const nmo_operation_p2_layer_t *p2_layer = NULL;
    nmo_status_t layer_result = find_p2_layer(
        registry, operation_guid, p1_type, p2_type, type_registry, &p2_layer);
    if (layer_result != NMO_OK) {
        return layer_result;
    }

    const nmo_operation_tree_cell_t *cell = find_cell_by_result(p2_layer, result_type->guid);
    if (!cell) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "No operation cell found for requested result type");
    }

    /* Update statistics (mutable cast for statistics) */
    nmo_operation_tree_cell_t *mutable_cell = (nmo_operation_tree_cell_t *)cell;
    mutable_cell->call_count++;

    *out_cell = cell;
    if (type_registry) {
        nmo_type_id_t p1_id = nmo_type_registry_guid_to_type_id(type_registry, p1_type->guid);
        nmo_type_id_t p2_id = NMO_TYPE_ID_INVALID;
        if (p2_type) {
            p2_id = nmo_type_registry_guid_to_type_id(type_registry, p2_type->guid);
        }

        if (p1_id != NMO_TYPE_ID_INVALID &&
            (p2_type == NULL || p2_id != NMO_TYPE_ID_INVALID)) {
            nmo_operation_cache_key_t key = {
                .operation_guid = *operation_guid,
                .p1_type_id = p1_id,
                .p2_type_id = p2_id,
                .result_type_guid = result_type->guid
            };
            if (registry->lookup_cache) {
                nmo_hash_table_insert(registry->lookup_cache, &key, &cell);
            }
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_operation_registry_find(
    nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid,
    const nmo_type_descriptor_t *p1_type,
    const nmo_type_descriptor_t *p2_type,
    const nmo_type_registry_t *type_registry,
    const nmo_operation_tree_cell_t **out_cell
) {
    if (!registry || !operation_guid || !p1_type || !out_cell) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid parameters");
    }

    *out_cell = NULL;
    registry->total_lookups++;

    if (type_registry) {
        ensure_lookup_cache(registry, type_registry);

        nmo_type_id_t p1_id = nmo_type_registry_guid_to_type_id(type_registry, p1_type->guid);
        nmo_type_id_t p2_id = NMO_TYPE_ID_INVALID;
        if (p2_type) {
            p2_id = nmo_type_registry_guid_to_type_id(type_registry, p2_type->guid);
        }

        if (p1_id != NMO_TYPE_ID_INVALID && (p2_type == NULL || p2_id != NMO_TYPE_ID_INVALID)) {
            nmo_operation_cache_key_t key = {
                .operation_guid = *operation_guid,
                .p1_type_id = p1_id,
                .p2_type_id = p2_id,
                .result_type_guid = (nmo_guid_t){0, 0},
            };

            const nmo_operation_tree_cell_t *cached_cell = NULL;
            if (registry->lookup_cache &&
                nmo_hash_table_get(registry->lookup_cache, &key, &cached_cell) == NMO_OK &&
                cached_cell) {
                registry->cache_hits++;

                nmo_operation_tree_cell_t *mutable_cell = (nmo_operation_tree_cell_t *)cached_cell;
                mutable_cell->call_count++;

                *out_cell = cached_cell;
                NMO_RETURN_OK();
            }
        }
    }

    /* Step 1: Find operation family by GUID (O(1) hash lookup) */
    uint32_t family_index = 0;
    if (nmo_hash_table_get(registry->family_map, operation_guid, &family_index) != NMO_OK) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                    "Operation family not found");
    }

    const nmo_operation_family_t *family = *(nmo_operation_family_t **)nmo_arena_array_get(&registry->families, family_index);
    if (!family) {
        NMO_RETURN_ERROR(NMO_ERR_INTERNAL, NMO_SEVERITY_ERROR,
                                "Family index points to NULL");
    }

    /* Step 2: Find P1 layer by type GUID (linear search with inheritance fallback) */
    const nmo_operation_p1_layer_t *p1_layer = NULL;
    const nmo_operation_p1_layer_t *compatible_p1_layer = NULL;
    int32_t best_p1_depth = -1;

    /* First try exact match */
    for (size_t i = 0; i < family->p1_layers.count; i++) {
        nmo_operation_p1_layer_t *candidate = (nmo_operation_p1_layer_t *)nmo_arena_array_get(&family->p1_layers, i);
        if (nmo_guid_equals(candidate->p1_type_guid, p1_type->guid)) {
            p1_layer = candidate;
            break;
        }
    }

    /* If no exact match and type_registry available, try inheritance matching */
    if (!p1_layer && type_registry) {
        nmo_type_id_t p1_type_id = nmo_type_registry_guid_to_type_id(
            type_registry, p1_type->guid);

        if (p1_type_id != NMO_TYPE_ID_INVALID) {
            /* Find best compatible type (closest parent) */
            for (size_t i = 0; i < family->p1_layers.count; i++) {
                nmo_operation_p1_layer_t *candidate = (nmo_operation_p1_layer_t *)nmo_arena_array_get(&family->p1_layers, i);
                nmo_type_id_t candidate_id = nmo_type_registry_guid_to_type_id(
                    type_registry, candidate->p1_type_guid);

                if (candidate_id != NMO_TYPE_ID_INVALID) {
                    /* Check if p1_type derives from candidate */
                    int32_t depth = nmo_type_get_derivation_depth(
                        (nmo_type_registry_t *)type_registry, p1_type_id, candidate_id);

                    /* Update if better match (closer parent = lower depth) */
                    if (depth >= 0 && (best_p1_depth < 0 || depth < best_p1_depth)) {
                        compatible_p1_layer = candidate;
                        best_p1_depth = depth;
                    }
                }
            }

            p1_layer = compatible_p1_layer;
        }
    }

    if (!p1_layer) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "P1 type not found in family (no compatible type)");
    }

    /* Step 3: Find P2 layer by type GUID (linear search with inheritance fallback) */
    nmo_guid_t p2_guid = p2_type ? p2_type->guid : (nmo_guid_t){0, 0};
    const nmo_operation_p2_layer_t *p2_layer = NULL;
    const nmo_operation_p2_layer_t *compatible_p2_layer = NULL;
    int32_t best_p2_depth = -1;

    /* First try exact match */
    for (size_t i = 0; i < p1_layer->p2_layers.count; i++) {
        nmo_operation_p2_layer_t *candidate = (nmo_operation_p2_layer_t *)nmo_arena_array_get(&p1_layer->p2_layers, i);
        if (nmo_guid_equals(candidate->p2_type_guid, p2_guid)) {
            p2_layer = candidate;
            break;
        }
    }

    /* If no exact match and type_registry available, try inheritance matching */
    if (!p2_layer && p2_type && type_registry) {
        nmo_type_id_t p2_type_id = nmo_type_registry_guid_to_type_id(
            type_registry, p2_guid);

        if (p2_type_id != NMO_TYPE_ID_INVALID) {
            /* Find best compatible type (closest parent) */
            for (size_t i = 0; i < p1_layer->p2_layers.count; i++) {
                nmo_operation_p2_layer_t *candidate = (nmo_operation_p2_layer_t *)nmo_arena_array_get(&p1_layer->p2_layers, i);
                nmo_type_id_t candidate_id = nmo_type_registry_guid_to_type_id(
                    type_registry, candidate->p2_type_guid);

                if (candidate_id != NMO_TYPE_ID_INVALID) {
                    /* Check if p2_type derives from candidate */
                    int32_t depth = nmo_type_get_derivation_depth(
                        (nmo_type_registry_t *)type_registry, p2_type_id, candidate_id);

                    /* Update if better match (closer parent = lower depth) */
                    if (depth >= 0 && (best_p2_depth < 0 || depth < best_p2_depth)) {
                        compatible_p2_layer = candidate;
                        best_p2_depth = depth;
                    }
                }
            }

            p2_layer = compatible_p2_layer;
        }
    }

    if (!p2_layer) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "P2 type not found in P1 layer (no compatible type)");
    }

    /* Step 4: Find operation cell (best match) */
    const nmo_operation_tree_cell_t *cell = find_cell_best(p2_layer);
    if (!cell) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "No operation cell found in P2 layer");
    }

    /* Update statistics (mutable cast for statistics) */
    nmo_operation_tree_cell_t *mutable_cell = (nmo_operation_tree_cell_t *)cell;
    mutable_cell->call_count++;

    *out_cell = cell;
    if (type_registry) {
        nmo_type_id_t p1_id = nmo_type_registry_guid_to_type_id(type_registry, p1_type->guid);
        nmo_type_id_t p2_id = NMO_TYPE_ID_INVALID;
        if (p2_type) {
            p2_id = nmo_type_registry_guid_to_type_id(type_registry, p2_type->guid);
        }

        if (p1_id != NMO_TYPE_ID_INVALID &&
            (p2_type == NULL || p2_id != NMO_TYPE_ID_INVALID)) {
            nmo_operation_cache_key_t key = {
                .operation_guid = *operation_guid,
                .p1_type_id = p1_id,
                .p2_type_id = p2_id,
                .result_type_guid = (nmo_guid_t){0, 0}
            };
            if (registry->lookup_cache) {
                nmo_hash_table_insert(registry->lookup_cache, &key, &cell);
            }
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_operation_registry_execute(
    nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid,
    const void *p1_data,
    const nmo_type_descriptor_t *p1_type,
    const void *p2_data,
    const nmo_type_descriptor_t *p2_type,
    void *result_data,
    const nmo_type_descriptor_t *result_type,
    const nmo_type_registry_t *type_registry
) {
    if (!registry || !operation_guid || !p1_data || !p1_type || !result_data || !result_type) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid parameters");
    }

    /* Find operation */
    const nmo_operation_tree_cell_t *cell = NULL;
    nmo_status_t result = nmo_operation_registry_find_typed(
        registry, operation_guid, p1_type, p2_type, result_type, type_registry, &cell
    );

    if (result != NMO_OK) {
        return result;
    }

    if (!cell) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                                "Operation not found");
    }
    if (!cell->desc.function) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR,
                                "Operation '%s' has no implementation",
                                cell->desc.name ? cell->desc.name : "<unnamed>");
    }

    /* Execute operation */
    return cell->desc.function(
        p1_data, p1_type,
        p2_data, p2_type,
        result_data, result_type,
        cell->desc.user_data
    );
}

/* ============================================================================
 * Query and Enumeration
 * ============================================================================ */

const nmo_operation_family_t *nmo_operation_registry_get_family(
    const nmo_operation_registry_t *registry,
    const nmo_guid_t *operation_guid
) {
    if (!registry || !operation_guid) {
        return NULL;
    }

    /* Lookup in hash map */
    uint32_t family_index = 0;
    if (nmo_hash_table_get(registry->family_map, operation_guid, &family_index) != NMO_OK) {
        return NULL;
    }

    if (family_index >= registry->families.count) {
        return NULL;
    }

    return *(nmo_operation_family_t **)nmo_arena_array_get(&registry->families, family_index);
}

nmo_status_t nmo_operation_family_enumerate(
    const nmo_operation_family_t *family,
    nmo_operation_enum_fn callback,
    void *user_data
) {
    if (!family || !callback) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Invalid parameters");
    }

    /* Enumerate all operations in family (3-level nested loop) */
    for (size_t i = 0; i < family->p1_layers.count; i++) {
        const nmo_operation_p1_layer_t *p1_layer = (const nmo_operation_p1_layer_t *)nmo_arena_array_get(&family->p1_layers, i);

        for (size_t j = 0; j < p1_layer->p2_layers.count; j++) {
            const nmo_operation_p2_layer_t *p2_layer = (const nmo_operation_p2_layer_t *)nmo_arena_array_get(&p1_layer->p2_layers, j);

            for (size_t k = 0; k < p2_layer->cells.count; k++) {
                const nmo_operation_tree_cell_t *cell = (const nmo_operation_tree_cell_t *)nmo_arena_array_get(&p2_layer->cells, k);

                nmo_status_t result = callback(cell, user_data);
                if (result != NMO_OK) {
                    return result;  /* Stop on error */
                }
            }
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Statistics and Debugging
 * ============================================================================ */

void nmo_operation_registry_get_stats(
    const nmo_operation_registry_t *registry,
    uint64_t *out_total_operations,
    uint64_t *out_total_lookups,
    uint64_t *out_cache_hits
) {
    if (!registry) {
        return;
    }

    if (out_total_operations) {
        *out_total_operations = registry->total_operations;
    }
    if (out_total_lookups) {
        *out_total_lookups = registry->total_lookups;
    }
    if (out_cache_hits) {
        *out_cache_hits = registry->cache_hits;
    }
}

void nmo_operation_registry_debug_print(
    const nmo_operation_registry_t *registry,
    nmo_logger_t *logger
) {
    if (!registry || !logger) {
        return;
    }

    nmo_log_info(logger, "=== Operation Registry Debug ===");
    nmo_log_info(logger, "Total families: %zu", registry->families.count);
    nmo_log_info(logger, "Total operations: %llu",
                 (unsigned long long)registry->total_operations);
    nmo_log_info(logger, "Total lookups: %llu",
                 (unsigned long long)registry->total_lookups);

    for (size_t i = 0; i < registry->families.count; i++) {
        const nmo_operation_family_t *family = *(nmo_operation_family_t **)nmo_arena_array_get(&registry->families, i);
        if (!family) continue;

        nmo_log_info(logger, "\nFamily: %s", family->name ? family->name : "Unknown");
        nmo_log_info(logger, "  Total operations: %llu",
                     (unsigned long long)family->total_operations);
        nmo_log_info(logger, "  P1 layers: %zu", family->p1_layers.count);

        for (size_t j = 0; j < family->p1_layers.count; j++) {
            const nmo_operation_p1_layer_t *p1_layer = (const nmo_operation_p1_layer_t *)nmo_arena_array_get(&family->p1_layers, j);
            nmo_log_info(logger, "    P1 layer %zu: %zu P2 layers", j, p1_layer->p2_layers.count);

            for (size_t k = 0; k < p1_layer->p2_layers.count; k++) {
                const nmo_operation_p2_layer_t *p2_layer = (const nmo_operation_p2_layer_t *)nmo_arena_array_get(&p1_layer->p2_layers, k);
                nmo_log_info(logger, "      P2 layer %zu: %zu cells", k, p2_layer->cells.count);

                for (size_t m = 0; m < p2_layer->cells.count; m++) {
                    const nmo_operation_tree_cell_t *cell = (const nmo_operation_tree_cell_t *)nmo_arena_array_get(&p2_layer->cells, m);
                    nmo_log_info(logger, "        Cell %zu: %s (calls: %llu, priority: %u)",
                                m, cell->desc.name ? cell->desc.name : "Unknown",
                                (unsigned long long)cell->call_count,
                                cell->desc.priority);
                }
            }
        }
    }
}
