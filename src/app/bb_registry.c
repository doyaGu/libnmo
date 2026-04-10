/**
 * @file bb_registry.c
 * @brief Building block prototype registry implementation
 */

#include "app/nmo_bb_registry.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Compiled-in builtin table (sorted by GUID for binary search)
 * ============================================================================ */

#include "bb_registry_builtin.inc"

static const bb_builtin_entry_t *builtin_find(nmo_guid_t guid) {
    size_t lo = 0, hi = BB_BUILTIN_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = nmo_guid_compare(guid, s_bb_builtin[mid].guid);
        if (cmp == 0) return &s_bb_builtin[mid];
        if (cmp < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}

/* ============================================================================
 * Dynamic entry storage (hash map)
 * ============================================================================ */

#define HASH_BUCKETS 256

typedef struct dyn_bb_entry {
    nmo_bb_proto_t proto;
    struct dyn_bb_entry *next;
} dyn_bb_entry_t;

struct nmo_bb_registry {
    nmo_arena_t *arena;
    dyn_bb_entry_t *buckets[HASH_BUCKETS];
    size_t dynamic_count;
};

static uint32_t bucket_of(nmo_guid_t guid) {
    return nmo_guid_hash(guid) % HASH_BUCKETS;
}

/* ============================================================================
 * Arena string/array helpers
 * ============================================================================ */

static char *arena_strdup(nmo_arena_t *arena, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dst = (char *)nmo_arena_alloc(arena, len + 1, 1);
    if (dst) memcpy(dst, s, len + 1);
    return dst;
}

static const char **arena_dup_strings(nmo_arena_t *arena, const char *const *src, uint32_t count) {
    if (!src || count == 0) return NULL;
    const char **dst = (const char **)nmo_arena_alloc(arena, count * sizeof(const char *), _Alignof(const char *));
    if (!dst) return NULL;
    for (uint32_t i = 0; i < count; i++)
        dst[i] = arena_strdup(arena, src[i]);
    return dst;
}

static nmo_bb_param_desc_t *arena_dup_params(nmo_arena_t *arena, const nmo_bb_param_desc_t *src, uint32_t count) {
    if (!src || count == 0) return NULL;
    nmo_bb_param_desc_t *dst = (nmo_bb_param_desc_t *)nmo_arena_alloc(
        arena, count * sizeof(*dst), _Alignof(nmo_bb_param_desc_t));
    if (!dst) return NULL;
    for (uint32_t i = 0; i < count; i++) {
        dst[i].name = arena_strdup(arena, src[i].name);
        dst[i].type_guid = src[i].type_guid;
    }
    return dst;
}

static void deep_copy_proto(nmo_arena_t *arena, nmo_bb_proto_t *dst, const nmo_bb_proto_t *src) {
    dst->guid = src->guid;
    dst->name = arena_strdup(arena, src->name);
    dst->description = arena_strdup(arena, src->description);
    dst->category = arena_strdup(arena, src->category);
    dst->dll = arena_strdup(arena, src->dll);
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
 * API implementation
 * ============================================================================ */

nmo_bb_registry_t *nmo_bb_registry_create(nmo_arena_t *arena) {
    if (!arena) return NULL;
    nmo_bb_registry_t *r = (nmo_bb_registry_t *)nmo_arena_alloc(
        arena, sizeof(*r), _Alignof(nmo_bb_registry_t));
    if (!r) return NULL;
    memset(r, 0, sizeof(*r));
    r->arena = arena;
    return r;
}

void nmo_bb_registry_destroy(nmo_bb_registry_t *registry) {
    /* Arena-allocated: nothing to free individually.
     * The arena owns all memory. This is a no-op. */
    (void)registry;
}

const nmo_bb_proto_t *nmo_bb_registry_find(const nmo_bb_registry_t *registry, nmo_guid_t guid) {
    if (!registry) return NULL;

    /* Dynamic entries first */
    uint32_t idx = bucket_of(guid);
    for (dyn_bb_entry_t *e = registry->buckets[idx]; e; e = e->next) {
        if (nmo_guid_equals(e->proto.guid, guid))
            return &e->proto;
    }

    /* Builtin fallback — return a static nmo_bb_proto_t from thread-local storage */
    const bb_builtin_entry_t *b = builtin_find(guid);
    if (b) {
        static _Thread_local nmo_bb_proto_t tls_proto;
        memset(&tls_proto, 0, sizeof(tls_proto));
        tls_proto.guid = b->guid;
        tls_proto.name = b->name;
        tls_proto.category = b->category;
        tls_proto.dll = b->dll;
        return &tls_proto;
    }
    return NULL;
}

const char *nmo_bb_registry_get_name(const nmo_bb_registry_t *registry, nmo_guid_t guid) {
    if (!registry) return NULL;

    /* Dynamic entries first */
    uint32_t idx = bucket_of(guid);
    for (dyn_bb_entry_t *e = registry->buckets[idx]; e; e = e->next) {
        if (nmo_guid_equals(e->proto.guid, guid))
            return e->proto.name;
    }

    /* Builtin fallback */
    const bb_builtin_entry_t *b = builtin_find(guid);
    return b ? b->name : NULL;
}

nmo_status_t nmo_bb_registry_add(nmo_bb_registry_t *registry, const nmo_bb_proto_t *proto) {
    if (!registry || !proto) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to nmo_bb_registry_add");
    }

    /* Check for existing entry (update in-place) */
    uint32_t idx = bucket_of(proto->guid);
    for (dyn_bb_entry_t *e = registry->buckets[idx]; e; e = e->next) {
        if (nmo_guid_equals(e->proto.guid, proto->guid)) {
            /* Overwrite — arena data is leaked but arena will reclaim */
            deep_copy_proto(registry->arena, &e->proto, proto);
            return NMO_OK;
        }
    }

    /* New entry */
    dyn_bb_entry_t *entry = (dyn_bb_entry_t *)nmo_arena_alloc(
        registry->arena, sizeof(*entry), _Alignof(dyn_bb_entry_t));
    if (!entry) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena alloc failed");
    }
    memset(entry, 0, sizeof(*entry));
    deep_copy_proto(registry->arena, &entry->proto, proto);
    entry->next = registry->buckets[idx];
    registry->buckets[idx] = entry;
    registry->dynamic_count++;
    return NMO_OK;
}

bool nmo_bb_registry_remove(nmo_bb_registry_t *registry, nmo_guid_t guid) {
    if (!registry) return false;
    uint32_t idx = bucket_of(guid);
    dyn_bb_entry_t **pp = &registry->buckets[idx];
    while (*pp) {
        if (nmo_guid_equals((*pp)->proto.guid, guid)) {
            *pp = (*pp)->next;
            /* Arena-allocated: cannot individually free, just unlink */
            registry->dynamic_count--;
            return true;
        }
        pp = &(*pp)->next;
    }
    return false;
}

size_t nmo_bb_registry_count(const nmo_bb_registry_t *registry) {
    if (!registry) return BB_BUILTIN_COUNT;
    return BB_BUILTIN_COUNT + registry->dynamic_count;
}

size_t nmo_bb_registry_builtin_count(const nmo_bb_registry_t *registry) {
    (void)registry;
    return BB_BUILTIN_COUNT;
}

/* Static (no-instance) builtin lookups */

const char *nmo_bb_builtin_get_name(nmo_guid_t guid) {
    const bb_builtin_entry_t *b = builtin_find(guid);
    return b ? b->name : NULL;
}

size_t nmo_bb_builtin_count(void) {
    return BB_BUILTIN_COUNT;
}
