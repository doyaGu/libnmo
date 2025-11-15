/**
 * @file indexed_map.c
 * @brief Generic indexed map implementation (hash table + dense array)
 */

#include "core/nmo_indexed_map.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include <string.h>
#include <stddef.h>

#define DEFAULT_INITIAL_CAPACITY 16u
#define LOAD_FACTOR 0.7

typedef enum indexed_map_entry_state {
    INDEXED_MAP_ENTRY_EMPTY = 0,
    INDEXED_MAP_ENTRY_OCCUPIED = 1,
    INDEXED_MAP_ENTRY_TOMBSTONE = 2
} indexed_map_entry_state_t;

struct nmo_indexed_map {
    nmo_arena_t *arena;
    int owns_arena;
    size_t key_size;
    size_t value_size;

    size_t capacity;        /**< Hash table capacity (power of two) */
    size_t array_capacity;  /**< Dense array capacity */
    size_t count;           /**< Number of live entries */

    uint8_t *states;        /**< Hash entry states */
    size_t *hash_to_dense;  /**< Mapping from hash slot to dense index */

    uint8_t *dense_keys;    /**< Packed key storage for iteration */
    uint8_t *dense_values;  /**< Packed value storage */

    nmo_map_hash_func_t hash_func;
    nmo_map_compare_func_t compare_func;

    nmo_container_lifecycle_t key_lifecycle;
    nmo_container_lifecycle_t value_lifecycle;
};

static size_t indexed_map_hash_default(const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int indexed_map_default_compare(const void *a, const void *b, size_t size) {
    return memcmp(a, b, size);
}

static size_t indexed_map_alignment(size_t size) {
    size_t alignment = size < sizeof(void*) ? sizeof(void*) : size;
    if (alignment & (alignment - 1)) {
        alignment = sizeof(void*);
    }
    return alignment;
}

static size_t indexed_map_next_capacity(size_t min_capacity) {
    size_t capacity = DEFAULT_INITIAL_CAPACITY;
    if (capacity < min_capacity) {
        while (capacity < min_capacity) {
            if (capacity > SIZE_MAX / 2) {
                return 0;
            }
            capacity <<= 1;
        }
    }
    return capacity;
}

static int indexed_map_allocate_hash(nmo_indexed_map_t *map, size_t capacity) {
    if (capacity == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint8_t *states = (uint8_t *)nmo_arena_alloc(map->arena, capacity, sizeof(uint8_t));
    size_t *indices = (size_t *)nmo_arena_alloc(map->arena, capacity * sizeof(size_t),
        indexed_map_alignment(sizeof(size_t)));
    if (states == NULL || indices == NULL) {
        return NMO_ERR_NOMEM;
    }

    memset(states, INDEXED_MAP_ENTRY_EMPTY, capacity);
    map->states = states;
    map->hash_to_dense = indices;
    map->capacity = capacity;
    return NMO_OK;
}

static int indexed_map_allocate_dense(nmo_indexed_map_t *map, size_t capacity) {
    if (capacity == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (map->key_size != 0 && capacity > SIZE_MAX / map->key_size) {
        return NMO_ERR_NOMEM;
    }
    if (map->value_size != 0 && capacity > SIZE_MAX / map->value_size) {
        return NMO_ERR_NOMEM;
    }

    size_t key_bytes = capacity * map->key_size;
    size_t value_bytes = capacity * map->value_size;
    uint8_t *keys = (uint8_t *)nmo_arena_alloc(map->arena, key_bytes,
        indexed_map_alignment(map->key_size));
    uint8_t *values = (uint8_t *)nmo_arena_alloc(map->arena, value_bytes,
        indexed_map_alignment(map->value_size));

    if (keys == NULL || values == NULL) {
        return NMO_ERR_NOMEM;
    }

    if (map->dense_keys != NULL) {
        memcpy(keys, map->dense_keys, map->count * map->key_size);
    }
    if (map->dense_values != NULL) {
        memcpy(values, map->dense_values, map->count * map->value_size);
    }

    map->dense_keys = keys;
    map->dense_values = values;
    map->array_capacity = capacity;
    return NMO_OK;
}

static int indexed_map_grow_dense(nmo_indexed_map_t *map) {
    size_t new_capacity = map->array_capacity > 0 ? map->array_capacity * 2 : DEFAULT_INITIAL_CAPACITY;
    if (new_capacity == 0) {
        return NMO_ERR_NOMEM;
    }
    return indexed_map_allocate_dense(map, new_capacity);
}

static int indexed_map_find_slot(const nmo_indexed_map_t *map,
                                 const void *key,
                                 int *found) {
    size_t mask = map->capacity - 1;
    size_t hash = map->hash_func(key, map->key_size);
    size_t index = hash & mask;
    ptrdiff_t tombstone = -1;

    while (1) {
        uint8_t state = map->states[index];
        if (state == INDEXED_MAP_ENTRY_EMPTY) {
            *found = 0;
            return tombstone >= 0 ? (int)tombstone : (int)index;
        }
        if (state == INDEXED_MAP_ENTRY_OCCUPIED) {
            size_t dense_index = map->hash_to_dense[index];
            const void *existing_key = map->dense_keys + (dense_index * map->key_size);
            if (map->compare_func(existing_key, key, map->key_size) == 0) {
                *found = 1;
                return (int)index;
            }
        } else if (tombstone < 0) {
            tombstone = (ptrdiff_t)index;
        }
        index = (index + 1) & mask;
    }
}

static int indexed_map_slot_for_dense(const nmo_indexed_map_t *map, size_t dense_index) {
    for (size_t i = 0; i < map->capacity; ++i) {
        if (map->states[i] == INDEXED_MAP_ENTRY_OCCUPIED && map->hash_to_dense[i] == dense_index) {
            return (int)i;
        }
    }
    return -1;
}

static int indexed_map_rehash(nmo_indexed_map_t *map, size_t new_capacity) {
    uint8_t *old_states = map->states;
    size_t *old_hash = map->hash_to_dense;
    size_t old_capacity = map->capacity;

    int result = indexed_map_allocate_hash(map, new_capacity);
    if (result != NMO_OK) {
        map->states = old_states;
        map->hash_to_dense = old_hash;
        map->capacity = old_capacity;
        return result;
    }

    for (size_t i = 0; i < map->count; ++i) {
        const void *key = map->dense_keys + (i * map->key_size);
        int found = 0;
        int slot = indexed_map_find_slot(map, key, &found);
        map->states[slot] = INDEXED_MAP_ENTRY_OCCUPIED;
        map->hash_to_dense[slot] = i;
    }

    return NMO_OK;
}

static void indexed_map_dispose_entry(nmo_indexed_map_t *map, size_t dense_index) {
    if (map->value_lifecycle.dispose) {
        void *value_ptr = map->dense_values + (dense_index * map->value_size);
        map->value_lifecycle.dispose(value_ptr, map->value_lifecycle.user_data);
    }
    if (map->key_lifecycle.dispose) {
        void *key_ptr = map->dense_keys + (dense_index * map->key_size);
        map->key_lifecycle.dispose(key_ptr, map->key_lifecycle.user_data);
    }
}

nmo_indexed_map_t *nmo_indexed_map_create(
    nmo_arena_t *arena,
    size_t key_size,
    size_t value_size,
    size_t initial_capacity,
    nmo_map_hash_func_t hash_func,
    nmo_map_compare_func_t compare_func
) {
    if (key_size == 0 || value_size == 0) {
        return NULL;
    }

    nmo_arena_t *arena_to_use = arena;
    int owns_arena = 0;
    if (arena_to_use == NULL) {
        arena_to_use = nmo_arena_create(NULL, 0);
        if (arena_to_use == NULL) {
            return NULL;
        }
        owns_arena = 1;
    }

    nmo_indexed_map_t *map = (nmo_indexed_map_t *)nmo_arena_alloc(
        arena_to_use, sizeof(nmo_indexed_map_t), indexed_map_alignment(sizeof(nmo_indexed_map_t)));
    if (map == NULL) {
        if (owns_arena) {
            nmo_arena_destroy(arena_to_use);
        }
        return NULL;
    }

    memset(map, 0, sizeof(*map));
    map->arena = arena_to_use;
    map->owns_arena = owns_arena;
    map->key_size = key_size;
    map->value_size = value_size;
    map->hash_func = hash_func ? hash_func : indexed_map_hash_default;
    map->compare_func = compare_func ? compare_func : indexed_map_default_compare;
    map->key_lifecycle.dispose = NULL;
    map->key_lifecycle.user_data = NULL;
    map->value_lifecycle.dispose = NULL;
    map->value_lifecycle.user_data = NULL;

    size_t capacity = initial_capacity > 0
        ? indexed_map_next_capacity(initial_capacity)
        : DEFAULT_INITIAL_CAPACITY;
    if (capacity == 0) {
        if (owns_arena) {
            nmo_arena_destroy(arena_to_use);
        }
        return NULL;
    }

    if (indexed_map_allocate_hash(map, capacity) != NMO_OK) {
        if (owns_arena) {
            nmo_arena_destroy(arena_to_use);
        }
        return NULL;
    }

    if (indexed_map_allocate_dense(map, capacity) != NMO_OK) {
        if (owns_arena) {
            nmo_arena_destroy(arena_to_use);
        }
        return NULL;
    }

    map->count = 0;
    return map;
}

void nmo_indexed_map_destroy(nmo_indexed_map_t *map) {
    if (map == NULL) {
        return;
    }

    /* Dispose dense entries */
    for (size_t i = 0; i < map->count; ++i) {
        indexed_map_dispose_entry(map, i);
    }

    if (map->owns_arena && map->arena != NULL) {
        nmo_arena_destroy(map->arena);
    }
}

void nmo_indexed_map_set_lifecycle(nmo_indexed_map_t *map,
                                   const nmo_container_lifecycle_t *key_lifecycle,
                                   const nmo_container_lifecycle_t *value_lifecycle) {
    if (map == NULL) {
        return;
    }

    if (key_lifecycle) {
        map->key_lifecycle = *key_lifecycle;
    } else {
        map->key_lifecycle.dispose = NULL;
        map->key_lifecycle.user_data = NULL;
    }

    if (value_lifecycle) {
        map->value_lifecycle = *value_lifecycle;
    } else {
        map->value_lifecycle.dispose = NULL;
        map->value_lifecycle.user_data = NULL;
    }
}

int nmo_indexed_map_insert(nmo_indexed_map_t *map, const void *key, const void *value) {
    if (map == NULL || key == NULL || value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (map->capacity == 0) {
        size_t new_capacity = indexed_map_next_capacity(DEFAULT_INITIAL_CAPACITY);
        if (new_capacity == 0) {
            return NMO_ERR_NOMEM;
        }
        int result = indexed_map_rehash(map, new_capacity);
        if (result != NMO_OK) {
            return result;
        }
    }

    double max_entries = (double)map->capacity * LOAD_FACTOR;
    if ((double)(map->count + 1) > max_entries) {
        size_t new_capacity = map->capacity << 1;
        if (new_capacity == 0 || new_capacity <= map->capacity) {
            return NMO_ERR_NOMEM;
        }
        int result = indexed_map_rehash(map, new_capacity);
        if (result != NMO_OK) {
            return result;
        }
    }

    if (map->count >= map->array_capacity) {
        int grow = indexed_map_grow_dense(map);
        if (grow != NMO_OK) {
            return grow;
        }
    }

    int found = 0;
    int slot = indexed_map_find_slot(map, key, &found);
    if (slot < 0) {
        return NMO_ERR_INVALID_STATE;
    }

    if (found) {
        size_t dense_index = map->hash_to_dense[slot];
        if (map->value_lifecycle.dispose) {
            void *value_ptr = map->dense_values + (dense_index * map->value_size);
            map->value_lifecycle.dispose(value_ptr, map->value_lifecycle.user_data);
        }
        memcpy(map->dense_values + (dense_index * map->value_size), value, map->value_size);
        return NMO_OK;
    }

    size_t dense_index = map->count;
    memcpy(map->dense_keys + (dense_index * map->key_size), key, map->key_size);
    memcpy(map->dense_values + (dense_index * map->value_size), value, map->value_size);
    map->states[slot] = INDEXED_MAP_ENTRY_OCCUPIED;
    map->hash_to_dense[slot] = dense_index;
    map->count++;
    return NMO_OK;
}

int nmo_indexed_map_get(const nmo_indexed_map_t *map, const void *key, void *value_out) {
    if (map == NULL || key == NULL) {
        return 0;
    }

    int found = 0;
    int slot = indexed_map_find_slot(map, key, &found);
    if (slot < 0 || !found) {
        return 0;
    }

    size_t dense_index = map->hash_to_dense[slot];
    if (value_out != NULL) {
        memcpy(value_out, map->dense_values + (dense_index * map->value_size), map->value_size);
    }
    return 1;
}

int nmo_indexed_map_remove(nmo_indexed_map_t *map, const void *key) {
    if (map == NULL || key == NULL) {
        return 0;
    }

    int found = 0;
    int slot = indexed_map_find_slot(map, key, &found);
    if (slot < 0 || !found) {
        return 0;
    }

    size_t dense_index = map->hash_to_dense[slot];
    indexed_map_dispose_entry(map, dense_index);

    size_t last_index = map->count - 1;
    if (dense_index != last_index) {
        memcpy(map->dense_keys + (dense_index * map->key_size),
               map->dense_keys + (last_index * map->key_size),
               map->key_size);
        memcpy(map->dense_values + (dense_index * map->value_size),
               map->dense_values + (last_index * map->value_size),
               map->value_size);

        int last_slot = indexed_map_slot_for_dense(map, last_index);
        if (last_slot >= 0) {
            map->hash_to_dense[last_slot] = dense_index;
        }
    }

    map->count--;
    map->states[slot] = INDEXED_MAP_ENTRY_TOMBSTONE;
    return 1;
}

int nmo_indexed_map_contains(const nmo_indexed_map_t *map, const void *key) {
    return nmo_indexed_map_get(map, key, NULL);
}

size_t nmo_indexed_map_get_count(const nmo_indexed_map_t *map) {
    return map ? map->count : 0;
}

int nmo_indexed_map_get_key_at(const nmo_indexed_map_t *map, size_t index, void *key_out) {
    if (map == NULL || key_out == NULL || index >= map->count) {
        return 0;
    }
    memcpy(key_out, map->dense_keys + (index * map->key_size), map->key_size);
    return 1;
}

int nmo_indexed_map_get_value_at(const nmo_indexed_map_t *map, size_t index, void *value_out) {
    if (map == NULL || value_out == NULL || index >= map->count) {
        return 0;
    }
    memcpy(value_out, map->dense_values + (index * map->value_size), map->value_size);
    return 1;
}

int nmo_indexed_map_get_at(const nmo_indexed_map_t *map, size_t index, void *key_out, void *value_out) {
    if (map == NULL || index >= map->count) {
        return 0;
    }
    if (key_out) {
        memcpy(key_out, map->dense_keys + (index * map->key_size), map->key_size);
    }
    if (value_out) {
        memcpy(value_out, map->dense_values + (index * map->value_size), map->value_size);
    }
    return 1;
}

void nmo_indexed_map_clear(nmo_indexed_map_t *map) {
    if (map == NULL) {
        return;
    }

    for (size_t i = 0; i < map->count; ++i) {
        indexed_map_dispose_entry(map, i);
    }
    map->count = 0;
    if (map->states != NULL) {
        memset(map->states, INDEXED_MAP_ENTRY_EMPTY, map->capacity);
    }
}

void nmo_indexed_map_iterate(const nmo_indexed_map_t *map,
                             nmo_indexed_map_iterator_func_t func,
                             void *user_data) {
    if (map == NULL || func == NULL) {
        return;
    }
    for (size_t i = 0; i < map->count; ++i) {
        void *value_ptr = map->dense_values + (i * map->value_size);
        const void *key_ptr = map->dense_keys + (i * map->key_size);
        if (!func(key_ptr, value_ptr, user_data)) {
            break;
        }
    }
}

/* Utility hash/compare functions */

size_t nmo_map_hash_uint32(const void *key, size_t key_size) {
    (void)key_size;
    uint32_t value = *(const uint32_t *)key;
    return (size_t)(value * 2654435761u);
}

size_t nmo_map_hash_string(const void *key, size_t key_size) {
    (void)key_size;
    const char *str = *(const char * const *)key;
    size_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + (size_t)c;
    }
    return hash;
}

int nmo_map_compare_string(const void *key1, const void *key2, size_t key_size) {
    (void)key_size;
    const char *str1 = *(const char * const *)key1;
    const char *str2 = *(const char * const *)key2;
    return strcmp(str1, str2);
}
