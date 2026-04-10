/**
 * @file behavior_index.c
 * @brief Behavior ownership index — O(1) reverse lookup for IO/param/sub-behavior IDs
 */

#include "app/nmo_behavior_index.h"
#include "app/nmo_script_walker.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "type/nmo_type_system.h"

#include <string.h>

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

static bool index_insert(nmo_behavior_index_t *idx, nmo_object_id_t key, const nmo_port_owner_t *val) {
    if (key == 0) return true; /* skip null IDs */

    /* Grow if needed */
    if ((idx->count + 1) * 100 > idx->capacity * INDEX_LOAD_FACTOR) {
        size_t new_cap = idx->capacity * 2;
        if (new_cap == 0) new_cap = INDEX_INITIAL_CAP;
        index_slot_t *new_slots = (index_slot_t *)nmo_arena_alloc(
            idx->arena, new_cap * sizeof(index_slot_t), _Alignof(index_slot_t));
        if (!new_slots) return false;
        memset(new_slots, 0, new_cap * sizeof(index_slot_t));

        /* Rehash existing entries */
        for (size_t i = 0; i < idx->capacity; i++) {
            if (idx->slots[i].key == 0) continue;
            uint32_t h = id_hash(idx->slots[i].key) % (uint32_t)new_cap;
            while (new_slots[h].key != 0) {
                h = (h + 1) % (uint32_t)new_cap;
            }
            new_slots[h] = idx->slots[i];
        }
        idx->slots = new_slots;
        idx->capacity = new_cap;
    }

    uint32_t h = id_hash(key) % (uint32_t)idx->capacity;
    while (idx->slots[h].key != 0) {
        if (idx->slots[h].key == key) {
            /* Already indexed — first registration wins (don't overwrite) */
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
} build_ctx_t;

static void index_array(nmo_behavior_index_t *idx, nmo_object_id_t owner_id,
                         const nmo_object_id_t *ids, size_t count, nmo_port_kind_t kind) {
    for (size_t i = 0; i < count; i++) {
        if (ids[i] == 0) continue;
        nmo_port_owner_t owner = {owner_id, (int32_t)i, kind};
        index_insert(idx, ids[i], &owner);
    }
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

    if (!state) return true;

    /* IO ports */
    if (state->inputs.data) {
        index_array(idx, behavior_id,
                    (const nmo_object_id_t *)state->inputs.data,
                    state->inputs.count, NMO_PORT_IO_IN);
    }
    if (state->outputs.data) {
        index_array(idx, behavior_id,
                    (const nmo_object_id_t *)state->outputs.data,
                    state->outputs.count, NMO_PORT_IO_OUT);
    }

    /* Parameters */
    if (state->in_parameters.data) {
        index_array(idx, behavior_id,
                    (const nmo_object_id_t *)state->in_parameters.data,
                    state->in_parameters.count, NMO_PORT_PARAM_IN);
    }
    if (state->out_parameters.data) {
        index_array(idx, behavior_id,
                    (const nmo_object_id_t *)state->out_parameters.data,
                    state->out_parameters.count, NMO_PORT_PARAM_OUT);
    }
    if (state->local_parameters.data) {
        index_array(idx, behavior_id,
                    (const nmo_object_id_t *)state->local_parameters.data,
                    state->local_parameters.count, NMO_PORT_PARAM_LOCAL);
    }

    /* Target parameter */
    if (state->target_parameter_id != 0) {
        nmo_port_owner_t owner = {behavior_id, -1, NMO_PORT_PARAM_TARGET};
        index_insert(idx, state->target_parameter_id, &owner);
    }

    /* Operations */
    if (state->operations.data) {
        index_array(idx, behavior_id,
                    (const nmo_object_id_t *)state->operations.data,
                    state->operations.count, NMO_PORT_OPERATION);
    }

    /* Sub-behaviors */
    if (state->sub_behaviors.data) {
        index_array(idx, behavior_id,
                    (const nmo_object_id_t *)state->sub_behaviors.data,
                    state->sub_behaviors.count, NMO_PORT_SUB_BEHAVIOR);
    }

    /* Sub-behavior links */
    if (state->sub_behavior_links.data) {
        index_array(idx, behavior_id,
                    (const nmo_object_id_t *)state->sub_behavior_links.data,
                    state->sub_behavior_links.count, NMO_PORT_SUB_LINK);
    }

    return true;
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
    return idx;
}

void nmo_behavior_index_destroy(nmo_behavior_index_t *index) {
    (void)index; /* arena-allocated */
}

nmo_status_t nmo_behavior_index_build(
    nmo_behavior_index_t *index,
    nmo_context_t *ctx,
    nmo_session_t *session)
{
    if (!index || !ctx || !session) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "null arg");
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    if (!repo) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "no repository");
    }

    /* Find all scripts and walk each one */
    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 32, NULL);
    nmo_status_t st = nmo_script_walker_find_scripts(ctx, session, &scripts);
    if (st != NMO_OK) {
        nmo_array_clear(&scripts);
        return st;
    }

    build_ctx_t bctx = {index, repo};
    const nmo_script_entry_t *entries = (const nmo_script_entry_t *)scripts.data;
    for (size_t i = 0; i < scripts.count; i++) {
        nmo_script_walker_walk(ctx, session, entries[i].script_id, build_visitor, &bctx);
    }

    nmo_array_clear(&scripts);
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
