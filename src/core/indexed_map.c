/**
 * @file indexed_map.c
 * @brief Generic indexed map implementation (hash table + dense array)
 */

#include "core/nmo_indexed_map.h"
#include "core/nmo_error.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DEFAULT_INITIAL_CAPACITY 16
#define LOAD_FACTOR 0.7
#define HASH_MULTIPLIER 2654435761U

/**
 * @brief Hash table entry
 */
typedef struct map_entry {
    void *key;         /**< Key data */
    void *value;       /**< Value data */
    size_t array_index; /**< Index in dense array */
    int occupied;      /**< Entry is occupied */
} map_entry_t;

/**
 * @brief Dense array entry
 */
typedef struct array_entry {
    void *key;         /**< Key data (copy) */
    void *value;       /**< Value data (copy) */
} array_entry_t;

/**
 * @brief Hash function type
 */
typedef size_t (*hash_func_t)(const void *key, size_t key_size);

/**
 * @brief Key comparison function type
 */
typedef int (*compare_func_t)(const void *key1, const void *key2, size_t key_size);

/**
 * @brief Indexed map structure
 */
struct nmo_indexed_map {
    /* Hash table for fast lookup */
    map_entry_t *entries;
    size_t capacity;
    size_t count;

    /* Dense array for iteration */
    array_entry_t *array;
    size_t array_capacity;

    /* Configuration */
    size_t key_size;
    size_t value_size;
    hash_func_t hash_func;
    compare_func_t compare_func;
};

/**
 * @brief Default hash function (FNV-1a)
 */
static size_t default_hash_func(const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t hash = 14695981039346656037ULL;

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }

    return hash;
}

/**
 * @brief Find slot for key (linear probing)
 */
static int find_slot(const nmo_indexed_map_t *map, const void *key, int *found) {
    size_t hash = map->hash_func(key, map->key_size);
    size_t index = hash % map->capacity;
    size_t start_index = index;

    do {
        if (!map->entries[index].occupied) {
            *found = 0;
            return (int)index;
        }

        if (map->compare_func(map->entries[index].key, key, map->key_size) == 0) {
            *found = 1;
            return (int)index;
        }

        index = (index + 1) % map->capacity;
    } while (index != start_index);

    *found = 0;
    return -1; /* Table full */
}

/**
 * @brief Resize hash table
 */
static int resize_table(nmo_indexed_map_t *map, size_t new_capacity) {
    map_entry_t *new_entries = (map_entry_t *)calloc(new_capacity, sizeof(map_entry_t));
    if (new_entries == NULL) {
        return NMO_ERR_NOMEM;
    }

    /* Allocate key/value storage for new entries */
    for (size_t i = 0; i < new_capacity; i++) {
        new_entries[i].key = malloc(map->key_size);
        new_entries[i].value = malloc(map->value_size);
        if (new_entries[i].key == NULL || new_entries[i].value == NULL) {
            /* Cleanup on failure */
            for (size_t j = 0; j <= i; j++) {
                free(new_entries[j].key);
                free(new_entries[j].value);
            }
            free(new_entries);
            return NMO_ERR_NOMEM;
        }
        new_entries[i].occupied = 0;
    }

    /* Rehash existing entries */
    map_entry_t *old_entries = map->entries;
    size_t old_capacity = map->capacity;

    map->entries = new_entries;
    map->capacity = new_capacity;
    size_t old_count = map->count;
    map->count = 0;

    for (size_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].occupied) {
            int found;
            int slot = find_slot(map, old_entries[i].key, &found);
            if (slot >= 0) {
                memcpy(map->entries[slot].key, old_entries[i].key, map->key_size);
                memcpy(map->entries[slot].value, old_entries[i].value, map->value_size);
                map->entries[slot].array_index = old_entries[i].array_index;
                map->entries[slot].occupied = 1;
                map->count++;
            }
        }
    }

    /* Free old entries */
    for (size_t i = 0; i < old_capacity; i++) {
        free(old_entries[i].key);
        free(old_entries[i].value);
    }
    free(old_entries);

    return (map->count == old_count) ? NMO_OK : NMO_ERR_INVALID_STATE;
}

/**
 * @brief Resize dense array
 */
static int resize_array(nmo_indexed_map_t *map, size_t new_capacity) {
    array_entry_t *new_array = (array_entry_t *)realloc(map->array, new_capacity * sizeof(array_entry_t));
    if (new_array == NULL) {
        return NMO_ERR_NOMEM;
    }

    /* Allocate key/value storage for new entries */
    for (size_t i = map->array_capacity; i < new_capacity; i++) {
        new_array[i].key = malloc(map->key_size);
        new_array[i].value = malloc(map->value_size);
        if (new_array[i].key == NULL || new_array[i].value == NULL) {
            /* Cleanup on failure */
            for (size_t j = map->array_capacity; j <= i; j++) {
                free(new_array[j].key);
                free(new_array[j].value);
            }
            return NMO_ERR_NOMEM;
        }
    }

    map->array = new_array;
    map->array_capacity = new_capacity;

    return NMO_OK;
}

/**
 * Create indexed map
 */
nmo_indexed_map_t *nmo_indexed_map_create(
    size_t key_size,
    size_t value_size,
    size_t initial_capacity,
    nmo_map_hash_func_t hash_func_param,
    nmo_map_compare_func_t compare_func_param) {

    if (key_size == 0 || value_size == 0) {
        return NULL;
    }

    nmo_indexed_map_t *map = (nmo_indexed_map_t *)malloc(sizeof(nmo_indexed_map_t));
    if (map == NULL) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_INITIAL_CAPACITY;
    }

    /* Allocate hash table entries */
    map->entries = (map_entry_t *)calloc(initial_capacity, sizeof(map_entry_t));
    if (map->entries == NULL) {
        free(map);
        return NULL;
    }

    /* Allocate key/value storage for hash table */
    for (size_t i = 0; i < initial_capacity; i++) {
        map->entries[i].key = malloc(key_size);
        map->entries[i].value = malloc(value_size);
        if (map->entries[i].key == NULL || map->entries[i].value == NULL) {
            for (size_t j = 0; j <= i; j++) {
                free(map->entries[j].key);
                free(map->entries[j].value);
            }
            free(map->entries);
            free(map);
            return NULL;
        }
        map->entries[i].occupied = 0;
    }

    /* Allocate dense array */
    map->array = (array_entry_t *)malloc(initial_capacity * sizeof(array_entry_t));
    if (map->array == NULL) {
        for (size_t i = 0; i < initial_capacity; i++) {
            free(map->entries[i].key);
            free(map->entries[i].value);
        }
        free(map->entries);
        free(map);
        return NULL;
    }

    /* Allocate key/value storage for array */
    for (size_t i = 0; i < initial_capacity; i++) {
        map->array[i].key = malloc(key_size);
        map->array[i].value = malloc(value_size);
        if (map->array[i].key == NULL || map->array[i].value == NULL) {
            for (size_t j = 0; j <= i; j++) {
                free(map->array[j].key);
                free(map->array[j].value);
            }
            free(map->array);
            for (size_t j = 0; j < initial_capacity; j++) {
                free(map->entries[j].key);
                free(map->entries[j].value);
            }
            free(map->entries);
            free(map);
            return NULL;
        }
    }

    map->capacity = initial_capacity;
    map->array_capacity = initial_capacity;
    map->count = 0;
    map->key_size = key_size;
    map->value_size = value_size;
    map->hash_func = (hash_func_t)(hash_func_param ? hash_func_param : default_hash_func);
    map->compare_func = (compare_func_t)(compare_func_param ? compare_func_param : memcmp);

    return map;
}

/**
 * Destroy indexed map
 */
void nmo_indexed_map_destroy(nmo_indexed_map_t *map) {
    if (map != NULL) {
        /* Free hash table */
        for (size_t i = 0; i < map->capacity; i++) {
            free(map->entries[i].key);
            free(map->entries[i].value);
        }
        free(map->entries);

        /* Free dense array */
        for (size_t i = 0; i < map->array_capacity; i++) {
            free(map->array[i].key);
            free(map->array[i].value);
        }
        free(map->array);

        free(map);
    }
}

/**
 * Insert or update entry
 */
int nmo_indexed_map_insert(nmo_indexed_map_t *map, const void *key, const void *value) {
    if (map == NULL || key == NULL || value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check load factor and resize if needed */
    if ((double)map->count / (double)map->capacity >= LOAD_FACTOR) {
        int result = resize_table(map, map->capacity * 2);
        if (result != NMO_OK) {
            return result;
        }
    }

    int found;
    int slot = find_slot(map, key, &found);
    if (slot < 0) {
        return NMO_ERR_NOMEM;
    }

    if (found) {
        /* Update existing entry */
        memcpy(map->entries[slot].value, value, map->value_size);
        size_t array_idx = map->entries[slot].array_index;
        memcpy(map->array[array_idx].value, value, map->value_size);
    } else {
        /* Add new entry */

        /* Resize array if needed */
        if (map->count >= map->array_capacity) {
            int result = resize_array(map, map->array_capacity * 2);
            if (result != NMO_OK) {
                return result;
            }
        }

        /* Add to hash table */
        memcpy(map->entries[slot].key, key, map->key_size);
        memcpy(map->entries[slot].value, value, map->value_size);
        map->entries[slot].array_index = map->count;
        map->entries[slot].occupied = 1;

        /* Add to dense array */
        memcpy(map->array[map->count].key, key, map->key_size);
        memcpy(map->array[map->count].value, value, map->value_size);

        map->count++;
    }

    return NMO_OK;
}

/**
 * Get value by key
 */
int nmo_indexed_map_get(const nmo_indexed_map_t *map, const void *key, void *value_out) {
    if (map == NULL || key == NULL) {
        return 0;
    }

    int found;
    int slot = find_slot(map, key, &found);
    if (slot < 0 || !found) {
        return 0;
    }

    if (value_out != NULL) {
        memcpy(value_out, map->entries[slot].value, map->value_size);
    }

    return 1;
}

/**
 * Remove entry by key
 */
int nmo_indexed_map_remove(nmo_indexed_map_t *map, const void *key) {
    if (map == NULL || key == NULL) {
        return 0;
    }

    int found;
    int slot = find_slot(map, key, &found);
    if (slot < 0 || !found) {
        return 0;
    }

    size_t array_idx = map->entries[slot].array_index;

    /* Remove from hash table */
    map->entries[slot].occupied = 0;

    /* Remove from dense array by swapping with last element */
    if (array_idx < map->count - 1) {
        /* Swap with last element */
        memcpy(map->array[array_idx].key, map->array[map->count - 1].key, map->key_size);
        memcpy(map->array[array_idx].value, map->array[map->count - 1].value, map->value_size);

        /* Update hash table entry for swapped element */
        int found2;
        int slot2 = find_slot(map, map->array[array_idx].key, &found2);
        if (found2 && slot2 >= 0) {
            map->entries[slot2].array_index = array_idx;
        }
    }

    map->count--;

    return 1;
}

/**
 * Check if key exists
 */
int nmo_indexed_map_contains(const nmo_indexed_map_t *map, const void *key) {
    return nmo_indexed_map_get(map, key, NULL);
}

/**
 * Get entry count
 */
size_t nmo_indexed_map_get_count(const nmo_indexed_map_t *map) {
    return map ? map->count : 0;
}

/**
 * Get key at index
 */
int nmo_indexed_map_get_key_at(const nmo_indexed_map_t *map, size_t index, void *key_out) {
    if (map == NULL || index >= map->count || key_out == NULL) {
        return 0;
    }

    memcpy(key_out, map->array[index].key, map->key_size);
    return 1;
}

/**
 * Get value at index
 */
int nmo_indexed_map_get_value_at(const nmo_indexed_map_t *map, size_t index, void *value_out) {
    if (map == NULL || index >= map->count || value_out == NULL) {
        return 0;
    }

    memcpy(value_out, map->array[index].value, map->value_size);
    return 1;
}

/**
 * Get both key and value at index
 */
int nmo_indexed_map_get_at(const nmo_indexed_map_t *map, size_t index, void *key_out, void *value_out) {
    if (map == NULL || index >= map->count) {
        return 0;
    }

    if (key_out != NULL) {
        memcpy(key_out, map->array[index].key, map->key_size);
    }
    if (value_out != NULL) {
        memcpy(value_out, map->array[index].value, map->value_size);
    }

    return 1;
}

/**
 * Clear all entries
 */
void nmo_indexed_map_clear(nmo_indexed_map_t *map) {
    if (map != NULL) {
        for (size_t i = 0; i < map->capacity; i++) {
            map->entries[i].occupied = 0;
        }
        map->count = 0;
    }
}

/**
 * Iterate over all entries
 */
void nmo_indexed_map_iterate(const nmo_indexed_map_t *map, nmo_indexed_map_iterator_func_t func, void *user_data) {
    if (map == NULL || func == NULL) {
        return;
    }

    for (size_t i = 0; i < map->count; i++) {
        if (!func(map->array[i].key, map->array[i].value, user_data)) {
            break;
        }
    }
}

/* Utility hash functions */

size_t nmo_map_hash_uint32(const void *key, size_t key_size) {
    (void)key_size;
    uint32_t value = *(const uint32_t *)key;
    return (size_t)(value * HASH_MULTIPLIER);
}

size_t nmo_map_hash_string(const void *key, size_t key_size) {
    (void)key_size;
    const char *str = *(const char **)key;
    size_t hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + (size_t)c;
    }

    return hash;
}

int nmo_map_compare_string(const void *key1, const void *key2, size_t key_size) {
    (void)key_size;
    const char *str1 = *(const char **)key1;
    const char *str2 = *(const char **)key2;
    return strcmp(str1, str2);
}

