/**
 * @file bb_registry.c
 * @brief Building block prototype registry — pure dynamic, no builtin data
 *
 * All data is loaded at runtime by external loaders (e.g. nmo_json virtools_loader).
 */

#include "behavior/nmo_bb_registry.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"
#include "core/nmo_hash_table.h"

#include <string.h>

/* ============================================================================
 * Hash helpers (same pattern as operation_registry.c)
 * ============================================================================ */

static size_t guid_hash_wrapper(const void *key, size_t key_size) {
    (void)key_size;
    return (size_t)nmo_guid_hash(*(const nmo_guid_t *)key);
}

static int guid_compare_wrapper(const void *key1, const void *key2, size_t key_size) {
    (void)key_size;
    return nmo_guid_equals(*(const nmo_guid_t *)key1, *(const nmo_guid_t *)key2) ? 0 : -1;
}

/* ============================================================================
 * Registry structure
 * ============================================================================ */

struct nmo_bb_registry {
    nmo_arena_t *arena;
    nmo_hash_table_t *guid_map; /**< GUID -> nmo_bb_proto_t * */
};

/* ============================================================================
 * Arena deep-copy helpers
 * ============================================================================ */

static const char **arena_dup_strings(nmo_arena_t *arena, const char *const *src, uint32_t count) {
    if (!src || count == 0) return NULL;
    const char **dst = (const char **)nmo_arena_alloc(arena, count * sizeof(const char *), _Alignof(const char *));
    if (!dst) return NULL;
    for (uint32_t i = 0; i < count; i++)
        dst[i] = nmo_arena_strdup(arena, src[i]);
    return dst;
}

static nmo_bb_param_desc_t *arena_dup_params(nmo_arena_t *arena, const nmo_bb_param_desc_t *src, uint32_t count) {
    if (!src || count == 0) return NULL;
    nmo_bb_param_desc_t *dst = (nmo_bb_param_desc_t *)nmo_arena_alloc(
        arena, count * sizeof(*dst), _Alignof(nmo_bb_param_desc_t));
    if (!dst) return NULL;
    for (uint32_t i = 0; i < count; i++) {
        dst[i].name = nmo_arena_strdup(arena, src[i].name);
        dst[i].type_guid = src[i].type_guid;
    }
    return dst;
}

static void deep_copy_proto(nmo_arena_t *arena, nmo_bb_proto_t *dst, const nmo_bb_proto_t *src) {
    dst->guid = src->guid;
    dst->name = nmo_arena_strdup(arena, src->name);
    dst->description = nmo_arena_strdup(arena, src->description);
    dst->category = nmo_arena_strdup(arena, src->category);
    dst->dll = nmo_arena_strdup(arena, src->dll);
    dst->version = src->version;
    dst->compatible_class_id = src->compatible_class_id;
    dst->behavior_flags = src->behavior_flags;
    dst->inputs = arena_dup_strings(arena, src->inputs, src->input_count);
    dst->input_count = src->input_count;
    dst->outputs = arena_dup_strings(arena, src->outputs, src->output_count);
    dst->output_count = src->output_count;
    dst->input_params = arena_dup_params(arena, src->input_params, src->input_param_count);
    dst->input_param_count = src->input_param_count;
    dst->output_params = arena_dup_params(arena, src->output_params, src->output_param_count);
    dst->output_param_count = src->output_param_count;
    dst->local_params = arena_dup_params(arena, src->local_params, src->local_param_count);
    dst->local_param_count = src->local_param_count;
    dst->settings = arena_dup_params(arena, src->settings, src->setting_count);
    dst->setting_count = src->setting_count;
}

/* ============================================================================
 * API
 * ============================================================================ */

nmo_bb_registry_t *nmo_bb_registry_create(nmo_arena_t *arena) {
    if (!arena) return NULL;
    nmo_bb_registry_t *r = (nmo_bb_registry_t *)nmo_arena_alloc(
        arena, sizeof(*r), _Alignof(nmo_bb_registry_t));
    if (!r) return NULL;
    memset(r, 0, sizeof(*r));
    r->arena = arena;
    r->guid_map = nmo_hash_table_create(
        NULL,
        sizeof(nmo_guid_t),
        sizeof(nmo_bb_proto_t *),
        256,
        guid_hash_wrapper,
        guid_compare_wrapper);
    if (!r->guid_map) return NULL;
    return r;
}

void nmo_bb_registry_destroy(nmo_bb_registry_t *registry) {
    if (registry && registry->guid_map) {
        nmo_hash_table_destroy(registry->guid_map);
        registry->guid_map = NULL;
    }
}

const nmo_bb_proto_t *nmo_bb_registry_find(const nmo_bb_registry_t *registry, nmo_guid_t guid) {
    if (!registry || !registry->guid_map) return NULL;
    nmo_bb_proto_t *proto = NULL;
    if (nmo_hash_table_get(registry->guid_map, &guid, &proto) == NMO_OK) {
        return proto;
    }
    return NULL;
}

const char *nmo_bb_registry_get_name(const nmo_bb_registry_t *registry, nmo_guid_t guid) {
    const nmo_bb_proto_t *p = nmo_bb_registry_find(registry, guid);
    return p ? p->name : NULL;
}

nmo_status_t nmo_bb_registry_add(nmo_bb_registry_t *registry, const nmo_bb_proto_t *proto) {
    if (!registry || !proto || !registry->guid_map) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to nmo_bb_registry_add");
    }

    /* Update existing? */
    nmo_bb_proto_t *existing = NULL;
    if (nmo_hash_table_get(registry->guid_map, &proto->guid, &existing) == NMO_OK) {
        deep_copy_proto(registry->arena, existing, proto);
        return NMO_OK;
    }

    /* New entry: arena-allocate proto and deep-copy */
    nmo_bb_proto_t *entry = (nmo_bb_proto_t *)nmo_arena_alloc(
        registry->arena, sizeof(*entry), _Alignof(nmo_bb_proto_t));
    if (!entry) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena alloc failed");
    }
    memset(entry, 0, sizeof(*entry));
    deep_copy_proto(registry->arena, entry, proto);

    nmo_status_t st = nmo_hash_table_insert(registry->guid_map, &proto->guid, &entry);
    if (st != NMO_OK) return st;

    return NMO_OK;
}

bool nmo_bb_registry_remove(nmo_bb_registry_t *registry, nmo_guid_t guid) {
    if (!registry || !registry->guid_map) return false;
    return nmo_hash_table_remove(registry->guid_map, &guid) == NMO_OK;
}

size_t nmo_bb_registry_count(const nmo_bb_registry_t *registry) {
    if (!registry || !registry->guid_map) return 0;
    return nmo_hash_table_get_count(registry->guid_map);
}

size_t nmo_bb_registry_builtin_count(const nmo_bb_registry_t *registry) {
    (void)registry;
    return 0; /* no builtin data — all loaded at runtime */
}

/* ============================================================================
 * Iteration
 * ============================================================================ */

typedef struct bb_foreach_ctx {
    nmo_bb_registry_visitor_fn visitor;
    void *user_data;
} bb_foreach_ctx_t;

static int bb_iterate_adapter(const void *key, void *value, void *user_data) {
    (void)key;
    bb_foreach_ctx_t *ctx = (bb_foreach_ctx_t *)user_data;
    nmo_bb_proto_t *proto = *(nmo_bb_proto_t **)value;
    if (proto && ctx->visitor) {
        return ctx->visitor(proto, ctx->user_data) ? 0 : 1;
    }
    return 1;
}

void nmo_bb_registry_foreach(
    const nmo_bb_registry_t *registry,
    nmo_bb_registry_visitor_fn visitor,
    void *user_data)
{
    if (!registry || !registry->guid_map || !visitor) return;
    bb_foreach_ctx_t ctx = { .visitor = visitor, .user_data = user_data };
    nmo_hash_table_iterate(registry->guid_map, bb_iterate_adapter, &ctx);
}

/* Static (no-instance) lookups — always return NULL in pure-dynamic mode */

const char *nmo_bb_builtin_get_name(nmo_guid_t guid) {
    (void)guid;
    return NULL; /* no compiled-in data */
}

size_t nmo_bb_builtin_count(void) {
    return 0;
}
