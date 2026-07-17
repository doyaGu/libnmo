/**
 * @file behavior_index.c
 * @brief Behavior ownership index 芒鈧?O(1) reverse lookup for IO/param/sub-behavior IDs
 */

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_query.h"
#include "../runtime/runtime_internal.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"

#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Hash table (linear probing, arena-allocated)
 * ============================================================================ */

#define INDEX_INITIAL_CAP 4096
#define INDEX_LOAD_FACTOR 70 /* percent */

typedef struct index_slot {
    nmo_object_id_t key;    /* 0 = empty */
    nmo_port_owner_t value;
} index_slot_t;

struct nmo_behavior_index {
    nmo_arena_t *arena;
    nmo_allocator_t allocator;
    index_slot_t *slots;
    size_t capacity;
    size_t count;
};

static uint32_t id_hash(nmo_object_id_t id) {
    /* Murmur-ish mixing for 32-bit keys */
    uint32_t h = (uint32_t)id;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return h;
}

static bool index_insert(
    nmo_behavior_index_t *idx,
    nmo_object_id_t key,
    const nmo_port_owner_t *val) {
    if (key == 0) return true; /* skip null IDs */

    /* Grow if needed */
    if ((idx->count + 1) * 100 > idx->capacity * INDEX_LOAD_FACTOR) {
        size_t new_cap = idx->capacity * 2;
        if (new_cap == 0) new_cap = INDEX_INITIAL_CAP;
        if (new_cap > SIZE_MAX / sizeof(index_slot_t)) return false;
        size_t byte_count = new_cap * sizeof(index_slot_t);
        index_slot_t *new_slots = (index_slot_t *)nmo_alloc(
            &idx->allocator, byte_count, _Alignof(index_slot_t));
        if (!new_slots) return false;
        memset(new_slots, 0, byte_count);

        /* Rehash existing entries */
        for (size_t i = 0; i < idx->capacity; i++) {
            if (idx->slots[i].key == 0) continue;
            uint32_t h = id_hash(idx->slots[i].key) % (uint32_t)new_cap;
            while (new_slots[h].key != 0) {
                h = (h + 1) % (uint32_t)new_cap;
            }
            new_slots[h] = idx->slots[i];
        }
        nmo_free(&idx->allocator, idx->slots);
        idx->slots = new_slots;
        idx->capacity = new_cap;
    }

    uint32_t h = id_hash(key) % (uint32_t)idx->capacity;
    while (idx->slots[h].key != 0) {
        if (idx->slots[h].key == key) {
            /* Already indexed 芒鈧?first registration wins (don't overwrite) */
            return true;
        }
        h = (h + 1) % (uint32_t)idx->capacity;
    }
    idx->slots[h].key = key;
    idx->slots[h].value = *val;
    idx->count++;
    return true;
}

static const nmo_port_owner_t *index_find(const nmo_behavior_index_t *idx, nmo_object_id_t key) {
    if (!idx || !idx->slots || key == 0 || idx->count == 0) return NULL;
    uint32_t h = id_hash(key) % (uint32_t)idx->capacity;
    for (size_t probes = 0; probes < idx->capacity; probes++) {
        if (idx->slots[h].key == key) return &idx->slots[h].value;
        if (idx->slots[h].key == 0) return NULL;
        h = (h + 1) % (uint32_t)idx->capacity;
    }
    return NULL;
}

/* ============================================================================
 * Build visitor
 * ============================================================================ */

typedef struct build_ctx {
    nmo_behavior_index_t *index;
    nmo_object_repository_t *repo;
    nmo_status_t status;
} build_ctx_t;

static bool index_array(nmo_behavior_index_t *idx, nmo_object_id_t owner_id,
                         const nmo_array_t *array, nmo_port_kind_t kind) {
    for (size_t i = 0; i < array->count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(array, i);
        if (id == 0) continue;
        nmo_port_owner_t owner = {owner_id, (int32_t)i, kind};
        if (!index_insert(idx, id, &owner)) {
            return false;
        }
    }
    return true;
}

static bool build_visitor(
    nmo_object_id_t behavior_id,
    const nmo_behavior_state_t *state,
    uint32_t depth,
    bool is_building_block,
    void *user_data)
{
    build_ctx_t *bctx = (build_ctx_t *)user_data;
    nmo_behavior_index_t *idx = bctx->index;
    (void)depth;
    (void)is_building_block;

    if (bctx->status != NMO_OK) return false;
    if (!state) return true;

    /* IO ports */
    if (state->inputs.data) {
        if (!index_array(idx, behavior_id, &state->inputs, NMO_PORT_IO_IN))
            goto oom;
    }
    if (state->outputs.data) {
        if (!index_array(idx, behavior_id, &state->outputs, NMO_PORT_IO_OUT))
            goto oom;
    }

    /* Parameters */
    if (state->in_parameters.data) {
        if (!index_array(idx, behavior_id, &state->in_parameters, NMO_PORT_PARAM_IN))
            goto oom;
    }
    if (state->out_parameters.data) {
        if (!index_array(idx, behavior_id, &state->out_parameters, NMO_PORT_PARAM_OUT))
            goto oom;
    }
    if (state->local_parameters.data) {
        if (!index_array(idx, behavior_id, &state->local_parameters, NMO_PORT_PARAM_LOCAL))
            goto oom;
    }

    /* Target parameter */
    const nmo_object_id_t target_parameter_id =
        nmo_behavior_target_parameter_id(state);
    if (target_parameter_id != 0) {
        nmo_port_owner_t owner = {behavior_id, -1, NMO_PORT_PARAM_TARGET};
        if (!index_insert(idx, target_parameter_id, &owner))
            goto oom;
    }

    /* Operations */
    if (state->operations.data) {
        if (!index_array(idx, behavior_id, &state->operations, NMO_PORT_OPERATION))
            goto oom;
    }

    /* Sub-behaviors */
    if (state->sub_behaviors.data) {
        if (!index_array(idx, behavior_id, &state->sub_behaviors, NMO_PORT_SUB_BEHAVIOR))
            goto oom;
    }

    /* Sub-behavior links */
    if (state->sub_behavior_links.data) {
        if (!index_array(idx, behavior_id, &state->sub_behavior_links, NMO_PORT_SUB_LINK))
            goto oom;
    }

    return true;

oom:
    bctx->status = NMO_ERR_NOMEM;
    return false;
}

/* ============================================================================
 * API
 * ============================================================================ */

nmo_behavior_index_t *nmo_behavior_index_create(nmo_arena_t *arena) {
    if (!arena) return NULL;
    nmo_behavior_index_t *idx = (nmo_behavior_index_t *)nmo_arena_alloc(
        arena, sizeof(*idx), _Alignof(nmo_behavior_index_t));
    if (!idx) return NULL;
    memset(idx, 0, sizeof(*idx));
    idx->arena = arena;
    if (nmo_arena_get_allocator(arena, &idx->allocator) != NMO_OK) return NULL;
    return idx;
}

void nmo_behavior_index_destroy(nmo_behavior_index_t *index) {
    if (!index) return;
    nmo_free(&index->allocator, index->slots);
    index->slots = NULL;
    index->capacity = 0;
    index->count = 0;
}

nmo_status_t nmo_behavior_index_build(
    nmo_behavior_index_t *index,
    nmo_workspace_t *workspace)
{
    nmo_document_t *document = NULL;
    nmo_status_t st = NMO_OK;

    if (!index || !workspace) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null arg");
    }

    nmo_object_repository_t *repo = nmo_workspace_internal_repository(workspace);
    if (!repo) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "no repository");
    }

    if (index->slots && index->capacity > 0) {
        memset(index->slots, 0, index->capacity * sizeof(*index->slots));
    }
    index->count = 0;

    /* Find all scripts and walk each one */
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_behavior_script_view_t), 32, NULL);
    document = nmo_workspace_get_document(workspace);
    st = nmo_behavior_query_collect_scripts(document, &scripts);
    if (st != NMO_OK) {
        nmo_array_dispose(&scripts);
        return st;
    }

    build_ctx_t bctx = {index, repo, NMO_OK};
    const nmo_behavior_script_view_t *entries = (const nmo_behavior_script_view_t *)scripts.data;
    for (size_t i = 0; i < scripts.count; i++) {
        st = nmo_behavior_walk(workspace, entries[i].script_id, build_visitor, &bctx);
        if (st != NMO_OK) {
            nmo_array_dispose(&scripts);
            return st;
        }
        if (bctx.status != NMO_OK) {
            nmo_array_dispose(&scripts);
            return bctx.status;
        }
    }

    nmo_array_dispose(&scripts);
    return NMO_OK;
}

const nmo_port_owner_t *nmo_behavior_index_find(
    const nmo_behavior_index_t *index,
    nmo_object_id_t id)
{
    return index_find(index, id);
}

size_t nmo_behavior_index_count(const nmo_behavior_index_t *index) {
    return index ? index->count : 0;
}
