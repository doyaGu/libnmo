/**
 * @file hash_table.c
 * @brief Generic hash table implementation with linear probing
 */

#include "core/nmo_hash_table.h"
#include "core/nmo_utils.h"
#include "core/nmo_error.h"
#include <string.h>
#include <stddef.h>

#define DEFAULT_INITIAL_CAPACITY 16u
#define LOAD_FACTOR 0.7

typedef enum nmo_hash_entry_state {
    NMO_HASH_ENTRY_EMPTY = 0,
    NMO_HASH_ENTRY_OCCUPIED = 1,
    NMO_HASH_ENTRY_TOMBSTONE = 2
} nmo_hash_entry_state_t;

struct nmo_hash_table {
    nmo_allocator_t allocator;
    size_t key_size;
    size_t value_size;
    size_t capacity;
    size_t count;
    uint8_t *states;
    uint8_t *keys;
    uint8_t *values;
    nmo_hash_func_t hash_func;
    nmo_key_compare_func_t compare_func;
    nmo_container_lifecycle_t key_lifecycle;
    nmo_container_lifecycle_t value_lifecycle;
};

static int nmo_hash_table_default_compare(const void *a, const void *b, size_t size) {
    return memcmp(a, b, size);
}

static void nmo_hash_table_copy_key(const nmo_hash_table_t *table, void *dest, const void *src) {
    nmo_container_copy_element(&table->key_lifecycle, dest, src, table->key_size);
}

static void nmo_hash_table_copy_value(const nmo_hash_table_t *table, void *dest, const void *src) {
    nmo_container_copy_element(&table->value_lifecycle, dest, src, table->value_size);
}

static void nmo_hash_table_move_key(nmo_hash_table_t *table, void *dest, void *src) {
    nmo_container_move_element(&table->key_lifecycle, dest, src, table->key_size);
}

static void nmo_hash_table_move_value(nmo_hash_table_t *table, void *dest, void *src) {
    nmo_container_move_element(&table->value_lifecycle, dest, src, table->value_size);
}

static size_t nmo_hash_table_next_capacity(size_t min_capacity) {
    size_t capacity = DEFAULT_INITIAL_CAPACITY;
    if (capacity < min_capacity) {
        while (capacity < min_capacity) {
            if (capacity > SIZE_MAX / 2) {
                return 0;
            }
            capacity <<= 1;
        }
    }
    if (capacity == 0) {
        capacity = DEFAULT_INITIAL_CAPACITY;
    }
    return capacity;
}

static int nmo_hash_table_allocate_storage(nmo_hash_table_t *table, size_t capacity) {
    if (capacity == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (table->key_size != 0 && capacity > SIZE_MAX / table->key_size) {
        return NMO_ERR_NOMEM;
    }
    if (table->value_size != 0 && capacity > SIZE_MAX / table->value_size) {
        return NMO_ERR_NOMEM;
    }

    size_t key_bytes = capacity * table->key_size;
    size_t value_bytes = capacity * table->value_size;

    uint8_t *states = (uint8_t *)nmo_alloc(&table->allocator,
        capacity * sizeof(uint8_t),
        1);
    uint8_t *keys = (uint8_t *)nmo_alloc(&table->allocator, key_bytes,
        nmo_elem_alignment(table->key_size));
    uint8_t *values = (uint8_t *)nmo_alloc(&table->allocator, value_bytes,
        nmo_elem_alignment(table->value_size));

    if (states == NULL || keys == NULL || values == NULL) {
        if (states != NULL) {
            nmo_free(&table->allocator, states);
        }
        if (keys != NULL) {
            nmo_free(&table->allocator, keys);
        }
        if (values != NULL) {
            nmo_free(&table->allocator, values);
        }
        return NMO_ERR_NOMEM;
    }

    memset(states, NMO_HASH_ENTRY_EMPTY, capacity);

    table->states = states;
    table->keys = keys;
    table->values = values;
    table->capacity = capacity;
    table->count = 0;
    return NMO_OK;
}

static nmo_status_t nmo_hash_table_place_entry(nmo_hash_table_t *table, const void *key, const void *value);

static int nmo_hash_table_rehash_internal(nmo_hash_table_t *table, size_t new_capacity) {
    uint8_t *old_states = table->states;
    uint8_t *old_keys = table->keys;
    uint8_t *old_values = table->values;
    size_t old_capacity = table->capacity;
    size_t old_count = table->count;

    int alloc_result = nmo_hash_table_allocate_storage(table, new_capacity);
    if (alloc_result != NMO_OK) {
        table->states = old_states;
        table->keys = old_keys;
        table->values = old_values;
        table->capacity = old_capacity;
        return alloc_result;
    }

    if (old_states != NULL) {
        table->count = 0;
        for (size_t i = 0; i < old_capacity; ++i) {
            if (old_states[i] == NMO_HASH_ENTRY_OCCUPIED) {
                const void *key_ptr = old_keys + (i * table->key_size);
                const void *value_ptr = old_values + (i * table->value_size);
                nmo_status_t place_result = nmo_hash_table_place_entry(table, key_ptr, value_ptr);
                if (place_result != NMO_OK) {
                    /* Restore old state on failure */
                    nmo_free(&table->allocator, table->states);
                    nmo_free(&table->allocator, table->keys);
                    nmo_free(&table->allocator, table->values);
                    table->states = old_states;
                    table->keys = old_keys;
                    table->values = old_values;
                    table->capacity = old_capacity;
                    table->count = old_count;
                    return (int)place_result;
                }
            }
        }
    }

    if (old_states != NULL) {
        nmo_free(&table->allocator, old_states);
    }
    if (old_keys != NULL) {
        nmo_free(&table->allocator, old_keys);
    }
    if (old_values != NULL) {
        nmo_free(&table->allocator, old_values);
    }

    return NMO_OK;
}

static int nmo_hash_table_should_grow(const nmo_hash_table_t *table) {
    double max_entries = (double)table->capacity * LOAD_FACTOR;
    return (double)(table->count + 1) > max_entries;
}

static int nmo_hash_table_find_slot(const nmo_hash_table_t *table,
                                    const void *key,
                                    int *found) {
    size_t mask = table->capacity - 1;
    size_t hash = table->hash_func ? table->hash_func(key, table->key_size)
                                   : nmo_hash_fnv1a(key, table->key_size);
    size_t index = hash & mask;
    ptrdiff_t tombstone_index = -1;

    for (;;) {
        uint8_t state = table->states[index];
        if (state == NMO_HASH_ENTRY_EMPTY) {
            *found = 0;
            return tombstone_index >= 0 ? (int)tombstone_index : (int)index;
        }

        if (state == NMO_HASH_ENTRY_OCCUPIED) {
            const void *existing_key = table->keys + (index * table->key_size);
            if (table->compare_func(existing_key, key, table->key_size) == 0) {
                *found = 1;
                return (int)index;
            }
        } else if (tombstone_index < 0) {
            tombstone_index = (ptrdiff_t)index;
        }

        index = (index + 1) & mask;
    }
}

static nmo_status_t nmo_hash_table_place_entry(nmo_hash_table_t *table, const void *key, const void *value) {
    int found = 0;
    int slot = nmo_hash_table_find_slot(table, key, &found);
    if (slot < 0) {
        return NMO_ERR_INVALID_STATE;
    }

    uint8_t *key_dest = table->keys + ((size_t)slot * table->key_size);
    uint8_t *value_dest = table->values + ((size_t)slot * table->value_size);
    nmo_hash_table_move_key(table, key_dest, (void *)key);
    nmo_hash_table_move_value(table, value_dest, (void *)value);
    table->states[slot] = NMO_HASH_ENTRY_OCCUPIED;
    table->count++;
    return NMO_OK;
}

static void nmo_hash_table_dispose_entry(nmo_hash_table_t *table, size_t index) {
    if (table->value_lifecycle.dispose) {
        void *value_ptr = table->values + (index * table->value_size);
        table->value_lifecycle.dispose(value_ptr, table->value_lifecycle.user_data);
    }
    if (table->key_lifecycle.dispose) {
        void *key_ptr = table->keys + (index * table->key_size);
        table->key_lifecycle.dispose(key_ptr, table->key_lifecycle.user_data);
    }
}

nmo_hash_table_t *nmo_hash_table_create(
    const nmo_allocator_t *allocator,
    size_t key_size,
    size_t value_size,
    size_t initial_capacity,
    nmo_hash_func_t hash_func,
    nmo_key_compare_func_t compare_func
) {
    if (key_size == 0 || value_size == 0) {
        return NULL;
    }

    nmo_allocator_t effective_allocator =
        allocator ? *allocator : nmo_allocator_default();

    nmo_hash_table_t *table = (nmo_hash_table_t *)nmo_alloc(
        &effective_allocator,
        sizeof(nmo_hash_table_t),
        nmo_elem_alignment(sizeof(nmo_hash_table_t)));
    if (table == NULL) {
        return NULL;
    }

    memset(table, 0, sizeof(*table));
    table->allocator = effective_allocator;
    table->key_size = key_size;
    table->value_size = value_size;
    table->hash_func = hash_func ? hash_func : nmo_hash_fnv1a;
    table->compare_func = compare_func ? compare_func : nmo_hash_table_default_compare;
    table->key_lifecycle.init = NULL;
    table->key_lifecycle.reset = NULL;
    table->key_lifecycle.copy = NULL;
    table->key_lifecycle.move = NULL;
    table->key_lifecycle.dispose = NULL;
    table->key_lifecycle.user_data = NULL;
    table->value_lifecycle.init = NULL;
    table->value_lifecycle.reset = NULL;
    table->value_lifecycle.copy = NULL;
    table->value_lifecycle.move = NULL;
    table->value_lifecycle.dispose = NULL;
    table->value_lifecycle.user_data = NULL;

    size_t capacity = initial_capacity > 0
        ? nmo_hash_table_next_capacity(initial_capacity)
        : DEFAULT_INITIAL_CAPACITY;
    if (capacity == 0) {
        nmo_free(&table->allocator, table);
        return NULL;
    }

    if (nmo_hash_table_allocate_storage(table, capacity) != NMO_OK) {
        nmo_free(&table->allocator, table);
        return NULL;
    }

    return table;
}

void nmo_hash_table_destroy(nmo_hash_table_t *table) {
    if (table == NULL) {
        return;
    }

    nmo_hash_table_clear(table);

    if (table->states != NULL) {
        nmo_free(&table->allocator, table->states);
    }
    if (table->keys != NULL) {
        nmo_free(&table->allocator, table->keys);
    }
    if (table->values != NULL) {
        nmo_free(&table->allocator, table->values);
    }

    nmo_free(&table->allocator, table);
}

void nmo_hash_table_set_lifecycle(nmo_hash_table_t *table,
                                  const nmo_container_lifecycle_t *key_lifecycle,
                                  const nmo_container_lifecycle_t *value_lifecycle) {
    if (table == NULL) {
        return;
    }

    if (key_lifecycle) {
        table->key_lifecycle = *key_lifecycle;
    } else {
        table->key_lifecycle.init = NULL;
        table->key_lifecycle.reset = NULL;
        table->key_lifecycle.copy = NULL;
        table->key_lifecycle.move = NULL;
        table->key_lifecycle.dispose = NULL;
        table->key_lifecycle.user_data = NULL;
    }

    if (value_lifecycle) {
        table->value_lifecycle = *value_lifecycle;
    } else {
        table->value_lifecycle.init = NULL;
        table->value_lifecycle.reset = NULL;
        table->value_lifecycle.copy = NULL;
        table->value_lifecycle.move = NULL;
        table->value_lifecycle.dispose = NULL;
        table->value_lifecycle.user_data = NULL;
    }
}

nmo_status_t nmo_hash_table_insert(nmo_hash_table_t *table, const void *key, const void *value) {
    if (table == NULL || key == NULL || value == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid hash table insert arguments");
    }

    if (table->capacity == 0) {
        size_t new_capacity = nmo_hash_table_next_capacity(DEFAULT_INITIAL_CAPACITY);
        int result = nmo_hash_table_rehash_internal(table, new_capacity);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, "Failed to grow hash table");
        }
    }

    if (nmo_hash_table_should_grow(table)) {
        size_t new_capacity = table->capacity << 1;
        if (new_capacity == 0 || new_capacity <= table->capacity) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Hash table capacity overflow");
        }
        int result = nmo_hash_table_rehash_internal(table, new_capacity);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, "Failed to rehash hash table");
        }
    }

    int found = 0;
    int slot = nmo_hash_table_find_slot(table, key, &found);
    if (slot < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR, "Failed to find hash table slot");
    }

    uint8_t *value_dest = table->values + ((size_t)slot * table->value_size);
    if (found) {
        nmo_container_reset_element(&table->value_lifecycle, value_dest, table->value_size);
        nmo_hash_table_copy_value(table, value_dest, value);
        NMO_RETURN_OK();
    }

    uint8_t *key_dest = table->keys + ((size_t)slot * table->key_size);
    nmo_hash_table_copy_key(table, key_dest, key);
    nmo_hash_table_copy_value(table, value_dest, value);
    table->states[slot] = NMO_HASH_ENTRY_OCCUPIED;
    table->count++;
    NMO_RETURN_OK();
}

nmo_status_t nmo_hash_table_get(const nmo_hash_table_t *table, const void *key, void *value_out) {
    if (table == NULL || key == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid hash table get arguments");
    }

    int found = 0;
    int slot = nmo_hash_table_find_slot(table, key, &found);
    if (slot < 0 || !found) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_INFO, "Key not found");
    }

    if (value_out != NULL) {
        nmo_hash_table_copy_value(table,
                                  value_out,
                                  table->values + ((size_t)slot * table->value_size));
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_hash_table_remove(nmo_hash_table_t *table, const void *key) {
    if (table == NULL || key == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid hash table remove arguments");
    }

    int found = 0;
    int slot = nmo_hash_table_find_slot(table, key, &found);
    if (slot < 0 || !found) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_INFO, "Key not found");
    }

    nmo_hash_table_dispose_entry(table, (size_t)slot);
    table->states[slot] = NMO_HASH_ENTRY_TOMBSTONE;
    if (table->count > 0) {
        table->count--;
    }
    NMO_RETURN_OK();
}

int nmo_hash_table_contains(const nmo_hash_table_t *table, const void *key) {
    nmo_status_t result = nmo_hash_table_get(table, key, NULL);
    return result == NMO_OK ? 1 : 0;
}

size_t nmo_hash_table_get_count(const nmo_hash_table_t *table) {
    return table ? table->count : 0;
}

size_t nmo_hash_table_get_capacity(const nmo_hash_table_t *table) {
    return table ? table->capacity : 0;
}

nmo_status_t nmo_hash_table_reserve(nmo_hash_table_t *table, size_t capacity) {
    if (table == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid hash table reserve arguments");
    }
    if (capacity <= table->capacity) {
        NMO_RETURN_OK();
    }

    size_t target = nmo_hash_table_next_capacity(capacity);
    if (target == 0) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Invalid hash table reserve capacity");
    }

    int result = nmo_hash_table_rehash_internal(table, target);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, "Failed to reserve hash table capacity");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_hash_table_rehash(nmo_hash_table_t *table, size_t capacity) {
    if (table == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid hash table rehash arguments");
    }

    if (capacity < table->count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Rehash capacity smaller than count");
    }

    size_t target = nmo_hash_table_next_capacity(capacity);
    if (target == 0) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Hash table rehash capacity overflow");
    }

    int result = nmo_hash_table_rehash_internal(table, target);
    if (result != NMO_OK) {
        NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, "Failed to rehash hash table");
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_hash_table_resize(nmo_hash_table_t *table, size_t capacity) {
    return nmo_hash_table_rehash(table, capacity);
}

float nmo_hash_table_load_factor(const nmo_hash_table_t *table) {
    if (table == NULL || table->capacity == 0) {
        return 0.0f;
    }
    return (float)table->count / (float)table->capacity;
}

void nmo_hash_table_clear(nmo_hash_table_t *table) {
    if (table == NULL || table->states == NULL) {
        return;
    }

    for (size_t i = 0; i < table->capacity; ++i) {
        if (table->states[i] == NMO_HASH_ENTRY_OCCUPIED) {
            nmo_hash_table_dispose_entry(table, i);
        }
        table->states[i] = NMO_HASH_ENTRY_EMPTY;
    }
    table->count = 0;
}

void nmo_hash_table_iterate(const nmo_hash_table_t *table,
                            nmo_hash_table_iterator_func_t func,
                            void *user_data) {
    if (table == NULL || func == NULL) {
        return;
    }

    for (size_t i = 0; i < table->capacity; ++i) {
        if (table->states[i] == NMO_HASH_ENTRY_OCCUPIED) {
            void *value_ptr = table->values + (i * table->value_size);
            const void *key_ptr = table->keys + (i * table->key_size);
            if (!func(key_ptr, value_ptr, user_data)) {
                break;
            }
        }
    }
}
