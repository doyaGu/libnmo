/**
 * @file shadow_storage.c
 * @brief Shadow blob preservation implementation (Phase 1.2)
 */

#include "session/nmo_shadow_storage.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
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

    /* Chunk tails: chunk_id (uint32_t) -> shadow_entry_t */
    nmo_hash_table_t *chunk_tails;

    nmo_arena_t *arena;
};

static void shadow_entry_dispose(void *value, void *user_data) {
    (void)user_data;
    shadow_entry_t *entry = (shadow_entry_t *)value;
    free(entry->data);
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
        .user_data = NULL
    };
    nmo_hash_table_set_lifecycle(storage->chunk_tails, NULL, &value_lifecycle);

    return storage;
}

void nmo_shadow_storage_destroy(nmo_shadow_storage_t *storage) {
    if (storage == NULL) {
        return;
    }

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

    storage->included_files_data = NULL;
    storage->included_files_size = 0;

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
        /* Clear existing data */
        storage->included_files_data = NULL;
        storage->included_files_size = 0;
        return NMO_OK;
    }

    /* Allocate and copy data into arena */
    void *copy = nmo_arena_alloc(storage->arena, size, 8);
    if (copy == NULL) {
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
    void *copy = malloc(tail_size);
    if (copy == NULL) {
        return NMO_ERR_NOMEM;
    }

    memcpy(copy, tail, tail_size);

    shadow_entry_t entry = {
        .data = copy,
        .size = tail_size
    };

    nmo_result_t result = nmo_hash_table_insert(storage->chunk_tails, &chunk_id, &entry);
    if (nmo_result_is_error(result)) {
        free(copy);
        return result.code;
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
    if (nmo_result_is_ok(nmo_hash_table_get(storage->chunk_tails, &chunk_id, &entry))) {
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
