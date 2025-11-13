/**
 * @file hash_table.c
 * @brief Generic hash table implementation with linear probing
 */

#include "core/nmo_hash_table.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_INITIAL_CAPACITY 16
#define LOAD_FACTOR 0.7
#define HASH_MULTIPLIER 2654435761U  /* Knuth's multiplicative hash */

/**
 * @brief Hash table entry
 */
typedef struct hash_entry {
    void *key;      /**< Key data */
    void *value;    /**< Value data */
    int occupied;   /**< Entry is occupied */
} hash_entry_t;

/**
 * @brief Hash table structure
 */
struct nmo_hash_table {
    hash_entry_t *entries;         /**< Entry array */
    size_t capacity;               /**< Table capacity */
    size_t count;                  /**< Number of entries */
    size_t key_size;               /**< Size of key in bytes */
    size_t value_size;             /**< Size of value in bytes */
    nmo_hash_func_t hash_func;     /**< Hash function */
    nmo_key_compare_func_t compare_func; /**< Key comparison function */
};

/**
 * @brief Find slot for key (linear probing)
 * @param table Hash table
 * @param key Key to find
 * @param found Output: 1 if key exists, 0 otherwise
 * @return Slot index, or -1 if table is full
 */
static int find_slot(const nmo_hash_table_t *table, const void *key, int *found) {
    size_t hash = table->hash_func(key, table->key_size);
    size_t index = hash % table->capacity;
    size_t start_index = index;

    do {
        if (!table->entries[index].occupied) {
            *found = 0;
            return (int)index;
        }

        if (table->compare_func(table->entries[index].key, key, table->key_size) == 0) {
            *found = 1;
            return (int)index;
        }

        index = (index + 1) % table->capacity;
    } while (index != start_index);

    *found = 0;
    return -1; /* Table full */
}

/**
 * @brief Resize hash table
 */
static int resize_table(nmo_hash_table_t *table, size_t new_capacity) {
    hash_entry_t *new_entries = (hash_entry_t *)calloc(new_capacity, sizeof(hash_entry_t));
    if (new_entries == NULL) {
        return NMO_ERR_NOMEM;
    }

    /* Allocate key/value storage for new entries */
    for (size_t i = 0; i < new_capacity; i++) {
        new_entries[i].key = malloc(table->key_size);
        new_entries[i].value = malloc(table->value_size);
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
    hash_entry_t *old_entries = table->entries;
    size_t old_capacity = table->capacity;

    table->entries = new_entries;
    table->capacity = new_capacity;
    size_t old_count = table->count;
    table->count = 0;

    for (size_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].occupied) {
            int found;
            int slot = find_slot(table, old_entries[i].key, &found);
            if (slot >= 0) {
                memcpy(table->entries[slot].key, old_entries[i].key, table->key_size);
                memcpy(table->entries[slot].value, old_entries[i].value, table->value_size);
                table->entries[slot].occupied = 1;
                table->count++;
            }
        }
    }

    /* Free old entries */
    for (size_t i = 0; i < old_capacity; i++) {
        free(old_entries[i].key);
        free(old_entries[i].value);
    }
    free(old_entries);

    return (table->count == old_count) ? NMO_OK : NMO_ERR_INVALID_STATE;
}

/**
 * Create hash table
 */
nmo_hash_table_t *nmo_hash_table_create(
    size_t key_size,
    size_t value_size,
    size_t initial_capacity,
    nmo_hash_func_t hash_func,
    nmo_key_compare_func_t compare_func) {

    if (key_size == 0 || value_size == 0) {
        return NULL;
    }

    nmo_hash_table_t *table = (nmo_hash_table_t *)malloc(sizeof(nmo_hash_table_t));
    if (table == NULL) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_INITIAL_CAPACITY;
    }

    table->entries = (hash_entry_t *)calloc(initial_capacity, sizeof(hash_entry_t));
    if (table->entries == NULL) {
        free(table);
        return NULL;
    }

    /* Allocate key/value storage for each entry */
    for (size_t i = 0; i < initial_capacity; i++) {
        table->entries[i].key = malloc(key_size);
        table->entries[i].value = malloc(value_size);
        if (table->entries[i].key == NULL || table->entries[i].value == NULL) {
            /* Cleanup on failure */
            for (size_t j = 0; j <= i; j++) {
                free(table->entries[j].key);
                free(table->entries[j].value);
            }
            free(table->entries);
            free(table);
            return NULL;
        }
        table->entries[i].occupied = 0;
    }

    table->capacity = initial_capacity;
    table->count = 0;
    table->key_size = key_size;
    table->value_size = value_size;
    table->hash_func = hash_func ? hash_func : nmo_hash_fnv1a;
    table->compare_func = compare_func ? compare_func : memcmp;

    return table;
}

/**
 * Destroy hash table
 */
void nmo_hash_table_destroy(nmo_hash_table_t *table) {
    if (table != NULL) {
        for (size_t i = 0; i < table->capacity; i++) {
            free(table->entries[i].key);
            free(table->entries[i].value);
        }
        free(table->entries);
        free(table);
    }
}

/**
 * Insert or update entry
 */
int nmo_hash_table_insert(nmo_hash_table_t *table, const void *key, const void *value) {
    if (table == NULL || key == NULL || value == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check load factor and resize if needed */
    if ((double)table->count / (double)table->capacity >= LOAD_FACTOR) {
        int result = resize_table(table, table->capacity * 2);
        if (result != NMO_OK) {
            return result;
        }
    }

    int found;
    int slot = find_slot(table, key, &found);
    if (slot < 0) {
        return NMO_ERR_NOMEM; /* Table full */
    }

    memcpy(table->entries[slot].key, key, table->key_size);
    memcpy(table->entries[slot].value, value, table->value_size);

    if (!found) {
        table->entries[slot].occupied = 1;
        table->count++;
    }

    return NMO_OK;
}

/**
 * Get value by key
 */
int nmo_hash_table_get(const nmo_hash_table_t *table, const void *key, void *value_out) {
    if (table == NULL || key == NULL) {
        return 0;
    }

    int found;
    int slot = find_slot(table, key, &found);
    if (slot < 0 || !found) {
        return 0;
    }

    if (value_out != NULL) {
        memcpy(value_out, table->entries[slot].value, table->value_size);
    }

    return 1;
}

/**
 * Remove entry by key
 */
int nmo_hash_table_remove(nmo_hash_table_t *table, const void *key) {
    if (table == NULL || key == NULL) {
        return 0;
    }

    int found;
    int slot = find_slot(table, key, &found);
    if (slot < 0 || !found) {
        return 0;
    }

    table->entries[slot].occupied = 0;
    table->count--;

    /* Note: We don't rehash after removal for simplicity.
     * Linear probing will still work correctly. */

    return 1;
}

/**
 * Check if key exists
 */
int nmo_hash_table_contains(const nmo_hash_table_t *table, const void *key) {
    return nmo_hash_table_get(table, key, NULL);
}

/**
 * Get entry count
 */
size_t nmo_hash_table_get_count(const nmo_hash_table_t *table) {
    return table ? table->count : 0;
}

/**
 * Get table capacity
 */
size_t nmo_hash_table_get_capacity(const nmo_hash_table_t *table) {
    return table ? table->capacity : 0;
}

/**
 * Reserve capacity (Phase 5 optimization)
 */
int nmo_hash_table_reserve(nmo_hash_table_t *table, size_t capacity) {
    if (table == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Already have enough capacity */
    if (table->capacity >= capacity) {
        return NMO_OK;
    }
    
    /* Account for load factor */
    size_t target_capacity = (size_t)((double)capacity / LOAD_FACTOR) + 1;
    
    /* Round up to next power of 2 for better performance */
    size_t new_capacity = DEFAULT_INITIAL_CAPACITY;
    while (new_capacity < target_capacity) {
        new_capacity *= 2;
    }
    
    return resize_table(table, new_capacity);
}

/**
 * Clear all entries
 */
void nmo_hash_table_clear(nmo_hash_table_t *table) {
    if (table != NULL) {
        for (size_t i = 0; i < table->capacity; i++) {
            table->entries[i].occupied = 0;
        }
        table->count = 0;
    }
}

/**
 * Iterate over all entries
 */
void nmo_hash_table_iterate(const nmo_hash_table_t *table, nmo_hash_table_iterator_func_t func, void *user_data) {
    if (table == NULL || func == NULL) {
        return;
    }

    for (size_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].occupied) {
            if (!func(table->entries[i].key, table->entries[i].value, user_data)) {
                break;
            }
        }
    }
}

/* Utility hash functions */

/**
 * Default hash function (FNV-1a)
 */
size_t nmo_hash_fnv1a(const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t hash = 14695981039346656037ULL; /* FNV offset basis */

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL; /* FNV prime */
    }

    return hash;
}

/**
 * Hash function for uint32_t keys (optimized with MurmurHash3 finalizer)
 */
size_t nmo_hash_uint32(const void *key, size_t key_size) {
    (void)key_size; /* Unused */
    uint32_t value = *(const uint32_t *)key;
    return (size_t)nmo_hash_int32(value);
}

/**
 * Hash function for string keys
 */
size_t nmo_hash_string(const void *key, size_t key_size) {
    (void)key_size; /* Unused for null-terminated strings */
    const char *str = *(const char **)key; /* Dereference pointer to string */
    size_t hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + (size_t)c; /* hash * 33 + c */
    }

    return hash;
}

/**
 * MurmurHash3 wrapper for hash table
 */
size_t nmo_hash_murmur3(const void *data, size_t size) {
    return (size_t)nmo_murmur3_32(data, size, 0);
}

/**
 * XXHash wrapper for hash table
 */
size_t nmo_hash_xxhash(const void *data, size_t size) {
    return (size_t)nmo_xxhash32(data, size, 0);
}

/**
 * Comparison function for string keys
 */
int nmo_compare_string(const void *key1, const void *key2, size_t key_size) {
    (void)key_size; /* Unused */
    const char *str1 = *(const char **)key1; /* Dereference pointer to string */
    const char *str2 = *(const char **)key2;
    return strcmp(str1, str2);
}

