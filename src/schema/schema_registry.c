/**
 * @file schema_registry.c
 * @brief Schema registry implementation
 */

#include "schema/nmo_schema_registry.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "core/nmo_indexed_map.h"
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>

/**
 * @brief Schema registry structure
 *
 * Maintains three indices for fast lookup:
 * 1. By type name (hash table)
 * 2. By class ID (indexed map)
 * 3. By manager GUID (hash table)
 */
struct nmo_schema_registry {
    nmo_arena_t *arena;              /**< Arena for allocations */
    nmo_hash_table_t *by_name;       /**< name -> type* */
    nmo_indexed_map_t *by_class_id;  /**< class_id -> type* */
    nmo_hash_table_t *by_guid;       /**< guid -> type* */
    size_t count;                    /**< Total registered types */
};

/* =============================================================================
 * LIFECYCLE
 * ============================================================================= */

nmo_schema_registry_t *nmo_schema_registry_create(nmo_arena_t *arena) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_schema_registry_t *registry =
        (nmo_schema_registry_t *)nmo_arena_alloc(arena, sizeof(nmo_schema_registry_t), alignof(nmo_schema_registry_t));
    if (registry == NULL) {
        return NULL;
    }

    registry->arena = arena;
    registry->count = 0;
    
    /* Create name index - note: hash tables don't use arena in current API */
    registry->by_name = nmo_hash_table_create(
        NULL,
        sizeof(const char *),              /* key: string pointer */
        sizeof(const nmo_schema_type_t *), /* value: type pointer */
        64,                                 /* initial capacity */
        nmo_hash_string,                   /* string hash */
        nmo_compare_string);                /* string comparison */
    
    if (registry->by_name == NULL) {
        return NULL;
    }
    
    /* Create class ID index */
    registry->by_class_id = nmo_indexed_map_create(
        registry->arena,
        NULL,
        sizeof(nmo_class_id_t),            /* key: class ID (uint32) */
        sizeof(const nmo_schema_type_t *), /* value: type pointer */
        64,                                 /* initial capacity */
        NULL,                               /* default hash for uint32 */
        NULL);                              /* default memcmp */
    
    if (registry->by_class_id == NULL) {
        nmo_hash_table_destroy(registry->by_name);
        registry->by_name = NULL;
        return NULL;
    }
    
    /* Create GUID index */
    registry->by_guid = nmo_hash_table_create(
        NULL,
        sizeof(nmo_guid_t),                /* key: GUID */
        sizeof(const nmo_schema_type_t *), /* value: type pointer */
        32,                                 /* initial capacity */
        NULL,                               /* default hash */
        NULL);                              /* default memcmp */
    
    if (registry->by_guid == NULL) {
        nmo_indexed_map_destroy(registry->by_class_id);
        registry->by_class_id = NULL;
        nmo_hash_table_destroy(registry->by_name);
        registry->by_name = NULL;
        return NULL;
    }
    
    return registry;
}

void nmo_schema_registry_destroy(nmo_schema_registry_t *registry) {
    if (registry == NULL) {
        return;
    }

    if (registry->by_name != NULL) {
        nmo_hash_table_destroy(registry->by_name);
        registry->by_name = NULL;
    }
    if (registry->by_guid != NULL) {
        nmo_hash_table_destroy(registry->by_guid);
        registry->by_guid = NULL;
    }
    if (registry->by_class_id != NULL) {
        nmo_indexed_map_destroy(registry->by_class_id);
        registry->by_class_id = NULL;
    }
}

/* =============================================================================
 * REGISTRATION
 * ============================================================================= */

nmo_result_t nmo_schema_registry_add(
    nmo_schema_registry_t *registry,
    const nmo_schema_type_t *type)
{
    if (registry == NULL || type == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "NULL argument to nmo_schema_registry_add"));
    }
    
    if (type->name == NULL || type->name[0] == '\0') {
        return nmo_result_error(NMO_ERROR(registry->arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Type name is required"));
    }
    
    /* Check for duplicate name */
    const nmo_schema_type_t *existing = nmo_schema_registry_find_by_name(registry, type->name);
    if (existing != NULL) {
        return nmo_result_error(NMO_ERROR(registry->arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Type with this name already registered"));
    }
    
    /* Add to name index */
    int result = nmo_hash_table_insert(registry->by_name, &type->name, &type);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(registry->arena, result,
            NMO_SEVERITY_ERROR, "Failed to add type to name index"));
    }
    
    registry->count++;
    return nmo_result_ok();
}

/* Forward declaration */
extern const nmo_schema_type_t *nmo_builtin_types[];
extern const size_t nmo_builtin_type_count;

nmo_result_t nmo_schema_registry_add_builtin(nmo_schema_registry_t *registry) {
    if (registry == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "NULL registry"));
    }
    
    /* TODO: Add built-in types (u8-u64, i8-i64, f32, f64, bool, string, etc.) */
    /* For now, return success */
    return nmo_result_ok();
}

/* =============================================================================
 * LOOKUP
 * ============================================================================= */

const nmo_schema_type_t *nmo_schema_registry_find_by_name(
    const nmo_schema_registry_t *registry,
    const char *name)
{
    if (registry == NULL || name == NULL) {
        return NULL;
    }
    
    const nmo_schema_type_t *type = NULL;
    if (nmo_hash_table_get(registry->by_name, &name, &type)) {
        return type;
    }
    
    return NULL;
}

const nmo_schema_type_t *nmo_schema_registry_find_by_class_id(
    const nmo_schema_registry_t *registry,
    nmo_class_id_t class_id)
{
    if (registry == NULL || class_id == 0) {
        return NULL;
    }
    
    const nmo_schema_type_t *type = NULL;
    if (nmo_indexed_map_get(registry->by_class_id, &class_id, &type)) {
        return type;
    }
    
    return NULL;
}

const nmo_schema_type_t *nmo_schema_registry_find_by_guid(
    const nmo_schema_registry_t *registry,
    nmo_guid_t guid)
{
    if (registry == NULL) {
        return NULL;
    }
    
    const nmo_schema_type_t *type = NULL;
    if (nmo_hash_table_get(registry->by_guid, &guid, &type)) {
        return type;
    }
    
    return NULL;
}

size_t nmo_schema_registry_get_count(const nmo_schema_registry_t *registry) {
    if (registry == NULL) {
        return 0;
    }
    
    return registry->count;
}

/* =============================================================================
 * ITERATION AND VALIDATION
 * ============================================================================= */

typedef struct {
    nmo_schema_iterator_fn callback;
    void *user_data;
} iteration_context_t;

static int iterate_adapter(const void *key, void *value, void *user_data) {
    (void)key;
    iteration_context_t *ctx = (iteration_context_t *)user_data;
    const nmo_schema_type_t *type = *(const nmo_schema_type_t **)value;
    return ctx->callback(type, ctx->user_data);
}

void nmo_schema_registry_iterate(
    const nmo_schema_registry_t *registry,
    nmo_schema_iterator_fn callback,
    void *user_data)
{
    if (registry == NULL || callback == NULL) {
        return;
    }
    
    iteration_context_t ctx = { callback, user_data };
    nmo_hash_table_iterate(registry->by_name, iterate_adapter, &ctx);
}

/* Validation helper: check for circular dependencies */
static int check_circular(
    const nmo_schema_type_t *type,
    const nmo_schema_type_t *root,
    int depth)
{
    if (depth > 100) {
        return 0; /* Circular dependency detected */
    }
    
    if (type == root && depth > 0) {
        return 0; /* Found cycle */
    }
    
    if (type->kind != NMO_TYPE_STRUCT) {
        return 1; /* Not a struct, no recursion */
    }
    
    /* Check all fields */
    for (size_t i = 0; i < type->field_count; i++) {
        const nmo_schema_field_t *field = &type->fields[i];
        if (field->type->kind == NMO_TYPE_STRUCT) {
            if (!check_circular(field->type, root, depth + 1)) {
                return 0;
            }
        }
    }
    
    return 1;
}

/* Callback for verification */
typedef struct {
    nmo_schema_registry_t *registry;
    nmo_arena_t *arena;
    nmo_result_t error;
} verify_context_t;

static int verify_callback(const nmo_schema_type_t *type, void *user_data) {
    verify_context_t *ctx = (verify_context_t *)user_data;
    
    /* Check circular dependencies for structs */
    if (type->kind == NMO_TYPE_STRUCT) {
        if (!check_circular(type, type, 0)) {
            ctx->error = nmo_result_error(NMO_ERROR(ctx->arena,
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Circular dependency detected in type"));
            return 0; /* Stop iteration */
        }
        
        /* Check field offsets */
        for (size_t i = 0; i < type->field_count; i++) {
            const nmo_schema_field_t *field = &type->fields[i];
            if (field->offset + field->type->size > type->size && type->size > 0) {
                ctx->error = nmo_result_error(NMO_ERROR(ctx->arena,
                    NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                    "Field exceeds struct bounds"));
                return 0;
            }
        }
    }
    
    return 1; /* Continue */
}

nmo_result_t nmo_schema_registry_verify(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (registry == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "NULL registry"));
    }
    
    /* Iterate all types and check consistency */
    verify_context_t verify_ctx = { registry, arena, nmo_result_ok() };
    
    nmo_schema_registry_iterate(registry, verify_callback, &verify_ctx);
    
    return verify_ctx.error;
}

/* =============================================================================
 * EXTENDED METADATA
 * ============================================================================= */

nmo_result_t nmo_schema_registry_map_class_id(
    nmo_schema_registry_t *registry,
    nmo_class_id_t class_id,
    const nmo_schema_type_t *type)
{
    if (registry == NULL || type == NULL || class_id == 0) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "NULL argument or zero class_id"));
    }
    
    /* Check for existing mapping */
    const nmo_schema_type_t *existing = nmo_schema_registry_find_by_class_id(registry, class_id);
    if (existing != NULL && existing != type) {
        return nmo_result_error(NMO_ERROR(registry->arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Class ID already mapped to different type"));
    }
    
    /* Add to index */
    int result = nmo_indexed_map_insert(registry->by_class_id, &class_id, &type);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(registry->arena, result,
            NMO_SEVERITY_ERROR, "Failed to map class ID"));
    }
    
    return nmo_result_ok();
}

nmo_result_t nmo_schema_registry_map_guid(
    nmo_schema_registry_t *registry,
    nmo_guid_t guid,
    const nmo_schema_type_t *type)
{
    if (registry == NULL || type == NULL) {
        return nmo_result_error(NMO_ERROR(NULL, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "NULL argument"));
    }
    
    /* Check for existing mapping */
    const nmo_schema_type_t *existing = nmo_schema_registry_find_by_guid(registry, guid);
    if (existing != NULL && existing != type) {
        return nmo_result_error(NMO_ERROR(registry->arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "GUID already mapped to different type"));
    }
    
    /* Add to index */
    int result = nmo_hash_table_insert(registry->by_guid, &guid, &type);
    if (result != NMO_OK) {
        return nmo_result_error(NMO_ERROR(registry->arena, result,
            NMO_SEVERITY_ERROR, "Failed to map GUID"));
    }
    
    return nmo_result_ok();
}
