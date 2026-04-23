/**
 * @file bb_registry.c
 * @brief Building block prototype registry â€?pure dynamic, no builtin data
 *
 * All data is loaded at runtime by external loaders (e.g. nmo_json virtools_loader).
 */

#include "behavior/nmo_behavior_registry.h"
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
    nmo_hash_table_t *guid_map; /**< GUID -> nmo_behavior_proto_t * */
};

/* ============================================================================
 * Arena deep-copy helpers
 * ============================================================================ */

static nmo_status_t validate_count_ptr(const void *ptr, uint32_t count, const char *field) {
    if (count > 0 && !ptr) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "BB prototype %s count is non-zero but array is NULL", field);
    }
    NMO_RETURN_OK();
}

static nmo_status_t arena_dup_strings(
    nmo_arena_t *arena,
    const char *const *src,
    uint32_t count,
    const char ***out_dst) {
    if (!out_dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL out_dst");
    }
    *out_dst = NULL;
    if (count == 0) NMO_RETURN_OK();
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "String array count is non-zero but source is NULL");
    }

    const char **dst = (const char **)nmo_arena_alloc(
        arena, count * sizeof(const char *), _Alignof(const char *));
    if (!dst) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena alloc failed");
    }

    for (uint32_t i = 0; i < count; i++) {
        dst[i] = nmo_arena_strdup(arena, src[i]);
        if (src[i] && !dst[i]) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena string alloc failed");
        }
    }

    *out_dst = dst;
    NMO_RETURN_OK();
}

static nmo_status_t arena_dup_params(
    nmo_arena_t *arena,
    const nmo_behavior_param_desc_t *src,
    uint32_t count,
    nmo_behavior_param_desc_t **out_dst) {
    if (!out_dst) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL out_dst");
    }
    *out_dst = NULL;
    if (count == 0) NMO_RETURN_OK();
    if (!src) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Parameter array count is non-zero but source is NULL");
    }

    nmo_behavior_param_desc_t *dst = (nmo_behavior_param_desc_t *)nmo_arena_alloc(
        arena, count * sizeof(*dst), _Alignof(nmo_behavior_param_desc_t));
    if (!dst) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena alloc failed");
    }

    for (uint32_t i = 0; i < count; i++) {
        dst[i].name = nmo_arena_strdup(arena, src[i].name);
        if (src[i].name && !dst[i].name) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena string alloc failed");
        }
        dst[i].type_guid = src[i].type_guid;
    }

    *out_dst = dst;
    NMO_RETURN_OK();
}

static nmo_status_t validate_proto_arrays(const nmo_behavior_proto_t *proto) {
    NMO_RETURN_IF_ERROR(validate_count_ptr(proto->inputs, proto->input_count, "input"));
    NMO_RETURN_IF_ERROR(validate_count_ptr(proto->outputs, proto->output_count, "output"));
    NMO_RETURN_IF_ERROR(validate_count_ptr(proto->input_params, proto->input_param_count,
                                          "input_param"));
    NMO_RETURN_IF_ERROR(validate_count_ptr(proto->output_params, proto->output_param_count,
                                          "output_param"));
    NMO_RETURN_IF_ERROR(validate_count_ptr(proto->local_params, proto->local_param_count,
                                          "local_param"));
    NMO_RETURN_IF_ERROR(validate_count_ptr(proto->settings, proto->setting_count, "setting"));
    NMO_RETURN_OK();
}

static nmo_status_t deep_copy_proto(
    nmo_arena_t *arena,
    nmo_behavior_proto_t *dst,
    const nmo_behavior_proto_t *src) {
    NMO_RETURN_IF_ERROR(validate_proto_arrays(src));

    nmo_behavior_proto_t tmp;
    memset(&tmp, 0, sizeof(tmp));

    tmp.guid = src->guid;
    tmp.name = nmo_arena_strdup(arena, src->name);
    if (src->name && !tmp.name) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena string alloc failed");
    }
    tmp.description = nmo_arena_strdup(arena, src->description);
    if (src->description && !tmp.description) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena string alloc failed");
    }
    tmp.category = nmo_arena_strdup(arena, src->category);
    if (src->category && !tmp.category) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena string alloc failed");
    }
    tmp.dll = nmo_arena_strdup(arena, src->dll);
    if (src->dll && !tmp.dll) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena string alloc failed");
    }
    tmp.version = src->version;
    tmp.compatible_class_id = src->compatible_class_id;
    tmp.behavior_flags = src->behavior_flags;
    const char **inputs = NULL;
    NMO_RETURN_IF_ERROR(arena_dup_strings(arena, src->inputs, src->input_count, &inputs));
    tmp.inputs = inputs;
    tmp.input_count = src->input_count;
    const char **outputs = NULL;
    NMO_RETURN_IF_ERROR(arena_dup_strings(arena, src->outputs, src->output_count, &outputs));
    tmp.outputs = outputs;
    tmp.output_count = src->output_count;
    nmo_behavior_param_desc_t *input_params = NULL;
    NMO_RETURN_IF_ERROR(arena_dup_params(arena, src->input_params, src->input_param_count,
                                         &input_params));
    tmp.input_params = input_params;
    tmp.input_param_count = src->input_param_count;
    nmo_behavior_param_desc_t *output_params = NULL;
    NMO_RETURN_IF_ERROR(arena_dup_params(arena, src->output_params, src->output_param_count,
                                         &output_params));
    tmp.output_params = output_params;
    tmp.output_param_count = src->output_param_count;
    nmo_behavior_param_desc_t *local_params = NULL;
    NMO_RETURN_IF_ERROR(arena_dup_params(arena, src->local_params, src->local_param_count,
                                         &local_params));
    tmp.local_params = local_params;
    tmp.local_param_count = src->local_param_count;
    nmo_behavior_param_desc_t *settings = NULL;
    NMO_RETURN_IF_ERROR(arena_dup_params(arena, src->settings, src->setting_count, &settings));
    tmp.settings = settings;
    tmp.setting_count = src->setting_count;

    *dst = tmp;
    NMO_RETURN_OK();
}

/* ============================================================================
 * API
 * ============================================================================ */

nmo_behavior_registry_t *nmo_behavior_registry_create(nmo_arena_t *arena) {
    if (!arena) return NULL;
    nmo_behavior_registry_t *r = (nmo_behavior_registry_t *)nmo_arena_alloc(
        arena, sizeof(*r), _Alignof(nmo_behavior_registry_t));
    if (!r) return NULL;
    memset(r, 0, sizeof(*r));
    r->arena = arena;
    r->guid_map = nmo_hash_table_create(
        NULL,
        sizeof(nmo_guid_t),
        sizeof(nmo_behavior_proto_t *),
        256,
        guid_hash_wrapper,
        guid_compare_wrapper);
    if (!r->guid_map) return NULL;
    return r;
}

void nmo_behavior_registry_destroy(nmo_behavior_registry_t *registry) {
    if (registry && registry->guid_map) {
        nmo_hash_table_destroy(registry->guid_map);
        registry->guid_map = NULL;
    }
}

const nmo_behavior_proto_t *nmo_behavior_registry_find(const nmo_behavior_registry_t *registry, nmo_guid_t guid) {
    if (!registry || !registry->guid_map) return NULL;
    nmo_behavior_proto_t *proto = NULL;
    if (nmo_hash_table_get(registry->guid_map, &guid, &proto) == NMO_OK) {
        return proto;
    }
    return NULL;
}

const char *nmo_behavior_registry_get_name(const nmo_behavior_registry_t *registry, nmo_guid_t guid) {
    const nmo_behavior_proto_t *p = nmo_behavior_registry_find(registry, guid);
    return p ? p->name : NULL;
}

nmo_status_t nmo_behavior_registry_add(nmo_behavior_registry_t *registry, const nmo_behavior_proto_t *proto) {
    if (!registry || !proto || !registry->guid_map) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL argument to nmo_behavior_registry_add");
    }

    /* Update existing? */
    nmo_behavior_proto_t *existing = NULL;
    if (nmo_hash_table_get(registry->guid_map, &proto->guid, &existing) == NMO_OK) {
        return deep_copy_proto(registry->arena, existing, proto);
    }

    /* New entry: arena-allocate proto and deep-copy */
    nmo_behavior_proto_t *entry = (nmo_behavior_proto_t *)nmo_arena_alloc(
        registry->arena, sizeof(*entry), _Alignof(nmo_behavior_proto_t));
    if (!entry) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "arena alloc failed");
    }
    memset(entry, 0, sizeof(*entry));
    NMO_RETURN_IF_ERROR(deep_copy_proto(registry->arena, entry, proto));

    nmo_status_t st = nmo_hash_table_insert(registry->guid_map, &proto->guid, &entry);
    if (st != NMO_OK) return st;

    return NMO_OK;
}

bool nmo_behavior_registry_remove(nmo_behavior_registry_t *registry, nmo_guid_t guid) {
    if (!registry || !registry->guid_map) return false;
    return nmo_hash_table_remove(registry->guid_map, &guid) == NMO_OK;
}

size_t nmo_behavior_registry_count(const nmo_behavior_registry_t *registry) {
    if (!registry || !registry->guid_map) return 0;
    return nmo_hash_table_get_count(registry->guid_map);
}

size_t nmo_behavior_registry_builtin_count(const nmo_behavior_registry_t *registry) {
    (void)registry;
    return 0; /* no builtin data â€?all loaded at runtime */
}

/* ============================================================================
 * Iteration
 * ============================================================================ */

typedef struct bb_foreach_ctx {
    nmo_behavior_registry_visitor_fn visitor;
    void *user_data;
} bb_foreach_ctx_t;

static int bb_iterate_adapter(const void *key, void *value, void *user_data) {
    (void)key;
    bb_foreach_ctx_t *ctx = (bb_foreach_ctx_t *)user_data;
    nmo_behavior_proto_t *proto = *(nmo_behavior_proto_t **)value;
    if (proto && ctx->visitor) {
        return ctx->visitor(proto, ctx->user_data) ? 0 : 1;
    }
    return 1;
}

void nmo_behavior_registry_foreach(
    const nmo_behavior_registry_t *registry,
    nmo_behavior_registry_visitor_fn visitor,
    void *user_data)
{
    if (!registry || !registry->guid_map || !visitor) return;
    bb_foreach_ctx_t ctx = { .visitor = visitor, .user_data = user_data };
    nmo_hash_table_iterate(registry->guid_map, bb_iterate_adapter, &ctx);
}

/* Static (no-instance) lookups â€?always return NULL in pure-dynamic mode */

const char *nmo_behavior_builtin_get_name(nmo_guid_t guid) {
    (void)guid;
    return NULL; /* no compiled-in data */
}

size_t nmo_behavior_builtin_count(void) {
    return 0;
}
