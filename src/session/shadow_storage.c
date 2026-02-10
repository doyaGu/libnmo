/**
 * @file shadow_storage.c
 * @brief Shadow blob preservation implementation (Phase 1.2)
 */

#include "session/nmo_shadow_storage.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "core/nmo_allocator.h"
#include "core/nmo_debug.h"
#include "core/nmo_ownership.h"
#include <string.h>
#include <stdalign.h>
#include <stdlib.h>

/**
 * @brief Internal buffer entry for chunk tails.
 */
typedef struct {
    void *data;
    size_t size;
} shadow_entry_t;

struct nmo_shadow_storage {
    /* Included Files blob */
    void *included_files_data;
    size_t included_files_size;
    nmo_arena_mark_t included_files_mark;
    bool included_files_scope_active;

    /* Chunk tails: chunk_id (uint32_t) -> shadow_entry_t */
    nmo_hash_table_t *chunk_tails;

    /* Ownership marker for chunk tail allocations */
    nmo_ownership_tag_t tail_ownership;
    nmo_allocator_t tail_allocator;
    nmo_allocator_debug_t tail_allocator_debug;
    nmo_arena_t *arena;
};

static bool shadow_storage_mark_can_rewind(const nmo_shadow_storage_t *storage) {
    if (storage == NULL || storage->arena == NULL) {
        return false;
    }

    nmo_arena_mark_t probe;
    if (nmo_arena_mark(storage->arena, &probe) != NMO_OK) {
        return false;
    }

    bool can_rewind =
        storage->included_files_mark.arena == storage->arena &&
        storage->included_files_mark.mark_epoch == probe.mark_epoch &&
        storage->included_files_mark.mark_depth + 1u == probe.mark_depth;

    (void)nmo_arena_rewind(storage->arena, &probe);
    return can_rewind;
}

static int shadow_storage_clear_included_files_scope(nmo_shadow_storage_t *storage) {
    if (storage == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int rewind_result = NMO_OK;
    if (storage->included_files_scope_active) {
        if (shadow_storage_mark_can_rewind(storage)) {
            rewind_result = nmo_arena_rewind(storage->arena, &storage->included_files_mark);
        }
        storage->included_files_scope_active = false;
    }

    memset(&storage->included_files_mark, 0, sizeof(storage->included_files_mark));
    storage->included_files_data = NULL;
    storage->included_files_size = 0;
    if (rewind_result != NMO_OK) {
        return rewind_result;
    }
    return NMO_OK;
}

static void shadow_entry_dispose(void *value, void *user_data) {
    nmo_shadow_storage_t *storage = (nmo_shadow_storage_t *)user_data;
    shadow_entry_t *entry = (shadow_entry_t *)value;
    NMO_DEBUG_ASSERT(storage != NULL);
    NMO_OWNERSHIP_ASSERT_VALID(storage->tail_ownership);
    NMO_OWNERSHIP_EXPECT(storage->tail_ownership, NMO_OWNERSHIP_HEAP);
    nmo_free(&storage->tail_allocator, entry->data);
    entry->data = NULL;
    entry->size = 0;
}

nmo_shadow_storage_t *nmo_shadow_storage_create(nmo_arena_t *arena) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_shadow_storage_t *storage = (nmo_shadow_storage_t *)nmo_arena_alloc(
        arena, sizeof(nmo_shadow_storage_t), alignof(nmo_shadow_storage_t));
    if (storage == NULL) {
        return NULL;
    }

    storage->arena = arena;
    storage->included_files_data = NULL;
    storage->included_files_size = 0;
    storage->included_files_scope_active = false;
    memset(&storage->included_files_mark, 0, sizeof(storage->included_files_mark));
    storage->tail_ownership = NMO_OWNERSHIP_HEAP;
    storage->tail_allocator = nmo_allocator_debug_init(
        &storage->tail_allocator_debug,
        nmo_allocator_default(),
        "shadow_storage",
        "chunk_tail");

    /* Create hash table for chunk tails: key=uint32_t, value=shadow_entry_t */
    storage->chunk_tails = nmo_hash_table_create(
        NULL,
        sizeof(uint32_t),
        sizeof(shadow_entry_t),
        64,               /* initial capacity */
        nmo_hash_uint32,
        NULL);

    if (storage->chunk_tails == NULL) {
        /* Arena-allocated storage will be freed with arena */
        return NULL;
    }
    nmo_container_lifecycle_t value_lifecycle = {
        .dispose = shadow_entry_dispose,
        .user_data = storage
    };
    nmo_hash_table_set_lifecycle(storage->chunk_tails, NULL, &value_lifecycle);

    return storage;
}

void nmo_shadow_storage_destroy(nmo_shadow_storage_t *storage) {
    if (storage == NULL) {
        return;
    }

    (void)shadow_storage_clear_included_files_scope(storage);

    if (storage->chunk_tails) {
        nmo_hash_table_destroy(storage->chunk_tails);
        storage->chunk_tails = NULL;
    }

    /* Arena owns the storage struct and data buffers */
    storage->included_files_data = NULL;
    storage->included_files_size = 0;
}

void nmo_shadow_storage_reset(nmo_shadow_storage_t *storage) {
    if (storage == NULL) {
        return;
    }

    (void)shadow_storage_clear_included_files_scope(storage);

    if (storage->chunk_tails) {
        nmo_hash_table_clear(storage->chunk_tails);
    }
}

int nmo_shadow_capture_included_files(nmo_shadow_storage_t *storage,
                                       const void *data, size_t size) {
    if (storage == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (data == NULL || size == 0) {
        return shadow_storage_clear_included_files_scope(storage);
    }

    int clear_result = shadow_storage_clear_included_files_scope(storage);
    if (clear_result != NMO_OK) {
        return clear_result;
    }

    int mark_result = nmo_arena_mark(storage->arena, &storage->included_files_mark);
    if (mark_result != NMO_OK) {
        return mark_result;
    }
    storage->included_files_scope_active = true;

    /* Allocate and copy data into arena */
    void *copy = nmo_arena_alloc(storage->arena, size, 8);
    if (copy == NULL) {
        (void)nmo_arena_rewind(storage->arena, &storage->included_files_mark);
        storage->included_files_scope_active = false;
        memset(&storage->included_files_mark, 0, sizeof(storage->included_files_mark));
        return NMO_ERR_NOMEM;
    }

    memcpy(copy, data, size);
    storage->included_files_data = copy;
    storage->included_files_size = size;

    return NMO_OK;
}

int nmo_shadow_capture_chunk_tail(nmo_shadow_storage_t *storage,
                                   uint32_t chunk_id,
                                   const void *tail, size_t tail_size) {
    if (storage == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (tail == NULL || tail_size == 0) {
        /* Remove existing entry if any */
        if (storage->chunk_tails) {
            nmo_hash_table_remove(storage->chunk_tails, &chunk_id);
        }
        return NMO_OK;
    }

    /* Allocate and copy tail data (freed on update/remove) */
    NMO_OWNERSHIP_ASSERT_VALID(storage->tail_ownership);
    NMO_OWNERSHIP_EXPECT(storage->tail_ownership, NMO_OWNERSHIP_HEAP);
    void *copy = nmo_alloc(&storage->tail_allocator, tail_size, 1);
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }

    memcpy(copy, tail, tail_size);

    shadow_entry_t entry = {
        .data = copy,
        .size = tail_size
    };

    nmo_status_t result = nmo_hash_table_insert(storage->chunk_tails, &chunk_id, &entry);
    if (result != NMO_OK) {
        nmo_free(&storage->tail_allocator, copy);
        return result;
    }

    return NMO_OK;
}

const void *nmo_shadow_get_included_files(const nmo_shadow_storage_t *storage,
                                           size_t *out_size) {
    if (storage == NULL) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    if (out_size) {
        *out_size = storage->included_files_size;
    }

    return storage->included_files_data;
}

const void *nmo_shadow_get_chunk_tail(const nmo_shadow_storage_t *storage,
                                       uint32_t chunk_id,
                                       size_t *out_size) {
    if (storage == NULL || storage->chunk_tails == NULL) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    shadow_entry_t entry;
    if (nmo_hash_table_get(storage->chunk_tails, &chunk_id, &entry) == NMO_OK) {
        if (out_size) *out_size = entry.size;
        return entry.data;
    }

    if (out_size) *out_size = 0;
    return NULL;
}

bool nmo_shadow_has_included_files(const nmo_shadow_storage_t *storage) {
    if (storage == NULL) {
        return false;
    }
    return storage->included_files_data != NULL && storage->included_files_size > 0;
}

size_t nmo_shadow_chunk_tail_count(const nmo_shadow_storage_t *storage) {
    if (storage == NULL || storage->chunk_tails == NULL) {
        return 0;
    }
    return nmo_hash_table_size(storage->chunk_tails);
}

/**
 * @brief Iterator context for chunk tail iteration.
 */
typedef struct {
    nmo_shadow_tail_callback_t callback;
    void *user;
} iterate_context_t;

/**
 * @brief Hash table iteration callback adapter.
 *
 * Note: The hash table iterator signature is:
 *   int (*)(const void *key, void *value, void *user_data);
 * Returns non-zero to continue, 0 to stop.
 */
static int iterate_adapter(const void *key, void *value, void *user) {
    iterate_context_t *ctx = (iterate_context_t *)user;
    const uint32_t *chunk_id = (const uint32_t *)key;
    const shadow_entry_t *entry = (const shadow_entry_t *)value;

    /* User callback returns true to continue, false to stop
     * Hash table expects non-zero to continue, zero to stop */
    bool should_continue = ctx->callback(*chunk_id, entry->data, entry->size, ctx->user);
    return should_continue ? 1 : 0;
}

void nmo_shadow_iterate_chunk_tails(const nmo_shadow_storage_t *storage,
                                     nmo_shadow_tail_callback_t callback,
                                     void *user) {
    if (storage == NULL || storage->chunk_tails == NULL || callback == NULL) {
        return;
    }

    iterate_context_t ctx = {
        .callback = callback,
        .user = user
    };

    nmo_hash_table_iterate(storage->chunk_tails, iterate_adapter, &ctx);
}
