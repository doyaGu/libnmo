/**
 * @file hash_set.c
 * @brief Generic hash set implementation with linear probing.
 */

#include "core/nmo_hash_set.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include <string.h>
#include <stddef.h>

#define HASH_SET_DEFAULT_CAPACITY 16u
#define HASH_SET_LOAD_FACTOR 0.7

typedef enum nmo_hash_set_entry_state {
    NMO_HASH_SET_ENTRY_EMPTY = 0,
    NMO_HASH_SET_ENTRY_OCCUPIED = 1,
    NMO_HASH_SET_ENTRY_TOMBSTONE = 2
} nmo_hash_set_entry_state_t;

struct nmo_hash_set {
    nmo_allocator_t allocator;
    size_t key_size;
    size_t capacity;
    size_t count;
    uint8_t *states;
    uint8_t *keys;
    nmo_hash_func_t hash_func;
    nmo_key_compare_func_t compare_func;
    nmo_container_lifecycle_t key_lifecycle;
};

static int nmo_hash_set_default_compare(const void *a, const void *b, size_t size) {
    return memcmp(a, b, size);
}

static size_t nmo_hash_set_alignment(size_t size) {
    size_t alignment = size < sizeof(void*) ? sizeof(void*) : size;
    if (alignment & (alignment - 1)) {
        alignment = sizeof(void*);
    }
    return alignment;
}

static size_t nmo_hash_set_next_capacity(size_t min_capacity) {
    size_t capacity = HASH_SET_DEFAULT_CAPACITY;
    if (capacity < min_capacity) {
        while (capacity < min_capacity) {
            if (capacity > SIZE_MAX / 2) {
                return 0;
            }
            capacity <<= 1;
        }
    }
    if (capacity == 0) {
        capacity = HASH_SET_DEFAULT_CAPACITY;
    }
    return capacity;
}

static int nmo_hash_set_allocate_storage(nmo_hash_set_t *set, size_t capacity) {
    if (capacity == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (set->key_size != 0 && capacity > SIZE_MAX / set->key_size) {
        return NMO_ERR_NOMEM;
    }

    size_t key_bytes = capacity * set->key_size;

    uint8_t *states = (uint8_t *)nmo_alloc(&set->allocator,
        capacity * sizeof(uint8_t),
        1);
    uint8_t *keys = (uint8_t *)nmo_alloc(&set->allocator, key_bytes,
        nmo_hash_set_alignment(set->key_size));

    if (states == NULL || keys == NULL) {
        if (states != NULL) {
            nmo_free(&set->allocator, states);
        }
        if (keys != NULL) {
            nmo_free(&set->allocator, keys);
        }
        return NMO_ERR_NOMEM;
    }

    memset(states, NMO_HASH_SET_ENTRY_EMPTY, capacity);

    set->states = states;
    set->keys = keys;
    set->capacity = capacity;
    set->count = 0;
    return NMO_OK;
}

static int nmo_hash_set_find_slot(const nmo_hash_set_t *set,
                                  const void *key,
                                  int *found) {
    size_t mask = set->capacity - 1;
    size_t hash = set->hash_func(key, set->key_size);
    size_t index = hash & mask;
    ptrdiff_t tombstone = -1;

    while (1) {
        uint8_t state = set->states[index];
        if (state == NMO_HASH_SET_ENTRY_EMPTY) {
            *found = 0;
            return tombstone >= 0 ? (int)tombstone : (int)index;
        }
        if (state == NMO_HASH_SET_ENTRY_OCCUPIED) {
            const void *existing = set->keys + (index * set->key_size);
            if (set->compare_func(existing, key, set->key_size) == 0) {
                *found = 1;
                return (int)index;
            }
        } else if (tombstone < 0) {
            tombstone = (ptrdiff_t)index;
        }
        index = (index + 1) & mask;
    }
}

static void nmo_hash_set_dispose_key(nmo_hash_set_t *set, size_t slot) {
    if (set->key_lifecycle.dispose) {
        void *key_ptr = set->keys + (slot * set->key_size);
        set->key_lifecycle.dispose(key_ptr, set->key_lifecycle.user_data);
    }
}

static int nmo_hash_set_rehash(nmo_hash_set_t *set, size_t new_capacity) {
    uint8_t *old_states = set->states;
    uint8_t *old_keys = set->keys;
    size_t old_capacity = set->capacity;

    int result = nmo_hash_set_allocate_storage(set, new_capacity);
    if (result != NMO_OK) {
        set->states = old_states;
        set->keys = old_keys;
        set->capacity = old_capacity;
        return result;
    }

    if (old_states != NULL) {
        for (size_t i = 0; i < old_capacity; ++i) {
            if (old_states[i] == NMO_HASH_SET_ENTRY_OCCUPIED) {
                const void *key_ptr = old_keys + (i * set->key_size);
                int found = 0;
                int slot = nmo_hash_set_find_slot(set, key_ptr, &found);
                if (slot >= 0) {
                    memcpy(set->keys + ((size_t)slot * set->key_size),
                           key_ptr,
                           set->key_size);
                    set->states[slot] = NMO_HASH_SET_ENTRY_OCCUPIED;
                    set->count++;
                }
            }
        }
    }

    if (old_states != NULL) {
        nmo_free(&set->allocator, old_states);
    }
    if (old_keys != NULL) {
        nmo_free(&set->allocator, old_keys);
    }

    return NMO_OK;
}

nmo_hash_set_t *nmo_hash_set_create(const nmo_allocator_t *allocator,
                                    size_t key_size,
                                    size_t initial_capacity,
                                    nmo_hash_func_t hash_func,
                                    nmo_key_compare_func_t compare_func) {
    if (key_size == 0) {
        return NULL;
    }

    nmo_allocator_t effective_allocator =
        allocator ? *allocator : nmo_allocator_default();

    nmo_hash_set_t *set = (nmo_hash_set_t *)nmo_alloc(
        &effective_allocator,
        sizeof(nmo_hash_set_t),
        nmo_hash_set_alignment(sizeof(nmo_hash_set_t)));
    if (!set) {
        return NULL;
    }

    memset(set, 0, sizeof(*set));
    set->allocator = effective_allocator;
    set->key_size = key_size;
    set->hash_func = hash_func ? hash_func : nmo_hash_fnv1a;
    set->compare_func = compare_func ? compare_func : nmo_hash_set_default_compare;
    set->key_lifecycle.dispose = NULL;
    set->key_lifecycle.user_data = NULL;

    size_t capacity = initial_capacity > 0
        ? nmo_hash_set_next_capacity(initial_capacity)
        : HASH_SET_DEFAULT_CAPACITY;
    if (capacity == 0) {
        nmo_free(&set->allocator, set);
        return NULL;
    }

    if (nmo_hash_set_allocate_storage(set, capacity) != NMO_OK) {
        nmo_free(&set->allocator, set);
        return NULL;
    }

    return set;
}

void nmo_hash_set_destroy(nmo_hash_set_t *set) {
    if (!set) {
        return;
    }

    nmo_hash_set_clear(set);

    if (set->states) {
        nmo_free(&set->allocator, set->states);
    }
    if (set->keys) {
        nmo_free(&set->allocator, set->keys);
    }
    nmo_free(&set->allocator, set);
}

void nmo_hash_set_set_lifecycle(nmo_hash_set_t *set,
                                const nmo_container_lifecycle_t *key_lifecycle) {
    if (!set) {
        return;
    }

    if (key_lifecycle) {
        set->key_lifecycle = *key_lifecycle;
    } else {
        set->key_lifecycle.dispose = NULL;
        set->key_lifecycle.user_data = NULL;
    }
}

static int nmo_hash_set_should_grow(const nmo_hash_set_t *set) {
    double max_entries = (double)set->capacity * HASH_SET_LOAD_FACTOR;
    return (double)(set->count + 1) > max_entries;
}

int nmo_hash_set_insert(nmo_hash_set_t *set, const void *key) {
    if (!set || !key) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (set->capacity == 0) {
        size_t new_capacity = nmo_hash_set_next_capacity(HASH_SET_DEFAULT_CAPACITY);
        int result = nmo_hash_set_rehash(set, new_capacity);
        if (result != NMO_OK) {
            return result;
        }
    }

    if (nmo_hash_set_should_grow(set)) {
        size_t new_capacity = set->capacity << 1;
        if (new_capacity == 0 || new_capacity <= set->capacity) {
            return NMO_ERR_NOMEM;
        }
        int result = nmo_hash_set_rehash(set, new_capacity);
        if (result != NMO_OK) {
            return result;
        }
    }

    int found = 0;
    int slot = nmo_hash_set_find_slot(set, key, &found);
    if (slot < 0) {
        return NMO_ERR_INVALID_STATE;
    }
    if (found) {
        return NMO_ERR_ALREADY_EXISTS;
    }

    uint8_t *dest = set->keys + ((size_t)slot * set->key_size);
    memcpy(dest, key, set->key_size);
    set->states[slot] = NMO_HASH_SET_ENTRY_OCCUPIED;
    set->count++;
    return NMO_OK;
}

int nmo_hash_set_remove(nmo_hash_set_t *set, const void *key) {
    if (!set || !key) {
        return 0;
    }

    int found = 0;
    int slot = nmo_hash_set_find_slot(set, key, &found);
    if (slot < 0 || !found) {
        return 0;
    }

    nmo_hash_set_dispose_key(set, (size_t)slot);
    set->states[slot] = NMO_HASH_SET_ENTRY_TOMBSTONE;
    if (set->count > 0) {
        set->count--;
    }
    return 1;
}

int nmo_hash_set_contains(const nmo_hash_set_t *set, const void *key) {
    if (!set || !key) {
        return 0;
    }

    int found = 0;
    int slot = nmo_hash_set_find_slot(set, key, &found);
    return (slot >= 0 && found) ? 1 : 0;
}

size_t nmo_hash_set_get_count(const nmo_hash_set_t *set) {
    return set ? set->count : 0;
}

size_t nmo_hash_set_get_capacity(const nmo_hash_set_t *set) {
    return set ? set->capacity : 0;
}

int nmo_hash_set_reserve(nmo_hash_set_t *set, size_t capacity) {
    if (!set) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (capacity <= set->capacity) {
        return NMO_OK;
    }

    size_t target = nmo_hash_set_next_capacity(capacity);
    if (target == 0) {
        return NMO_ERR_NOMEM;
    }
    return nmo_hash_set_rehash(set, target);
}

void nmo_hash_set_clear(nmo_hash_set_t *set) {
    if (!set || !set->states) {
        return;
    }

    for (size_t i = 0; i < set->capacity; ++i) {
        if (set->states[i] == NMO_HASH_SET_ENTRY_OCCUPIED) {
            nmo_hash_set_dispose_key(set, i);
        }
        set->states[i] = NMO_HASH_SET_ENTRY_EMPTY;
    }
    set->count = 0;
}

void nmo_hash_set_iterate(const nmo_hash_set_t *set,
                          nmo_hash_set_iterator_func_t func,
                          void *user_data) {
    if (!set || !func) {
        return;
    }

    for (size_t i = 0; i < set->capacity; ++i) {
        if (set->states[i] == NMO_HASH_SET_ENTRY_OCCUPIED) {
            const void *key_ptr = set->keys + (i * set->key_size);
            if (!func(key_ptr, user_data)) {
                break;
            }
        }
    }
}
