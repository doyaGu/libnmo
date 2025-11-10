#include "session/nmo_id_remap.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief ID mapping entry
 */
typedef struct {
    uint32_t old_id;
    uint32_t new_id;
    int occupied;
} nmo_id_mapping;

/**
 * @brief ID remapper structure
 */
struct nmo_id_remap {
    nmo_id_mapping* mappings;
    size_t capacity;
    size_t count;
};

// Hash function for object IDs
static inline size_t hash_id(uint32_t id, size_t capacity) {
    // Simple multiplicative hash
    return (id * 2654435761U) % capacity;
}

// Find mapping slot (returns index, -1 if not found)
static int find_slot(const nmo_id_remap_t* remapper, uint32_t old_id, int* found) {
    size_t index = hash_id(old_id, remapper->capacity);
    size_t start = index;

    do {
        if (!remapper->mappings[index].occupied) {
            *found = 0;
            return (int)index;  // Empty slot
        }

        if (remapper->mappings[index].old_id == old_id) {
            *found = 1;
            return (int)index;  // Found existing
        }

        index = (index + 1) % remapper->capacity;
    } while (index != start);

    *found = 0;
    return -1;  // Table full
}

// Resize hash table
static int resize_table(nmo_id_remap_t* remapper, size_t new_capacity) {
    nmo_id_mapping* old_mappings = remapper->mappings;
    size_t old_capacity = remapper->capacity;

    // Allocate new table
    nmo_id_mapping* new_mappings = (nmo_id_mapping*)calloc(new_capacity, sizeof(nmo_id_mapping));
    if (new_mappings == NULL) {
        return -1;
    }

    remapper->mappings = new_mappings;
    remapper->capacity = new_capacity;
    remapper->count = 0;

    // Rehash old entries
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_mappings[i].occupied) {
            int found;
            int slot = find_slot(remapper, old_mappings[i].old_id, &found);
            if (slot >= 0) {
                remapper->mappings[slot].old_id = old_mappings[i].old_id;
                remapper->mappings[slot].new_id = old_mappings[i].new_id;
                remapper->mappings[slot].occupied = 1;
                remapper->count++;
            }
        }
    }

    free(old_mappings);
    return 0;
}

nmo_id_remap_t* nmo_id_remap_create(size_t initial_capacity) {
    if (initial_capacity == 0) {
        initial_capacity = 32;
    }

    nmo_id_remap_t* remapper = (nmo_id_remap_t*)malloc(sizeof(nmo_id_remap_t));
    if (remapper == NULL) {
        return NULL;
    }

    remapper->mappings = (nmo_id_mapping*)calloc(initial_capacity, sizeof(nmo_id_mapping));
    if (remapper->mappings == NULL) {
        free(remapper);
        return NULL;
    }

    remapper->capacity = initial_capacity;
    remapper->count = 0;

    return remapper;
}

void nmo_id_remap_destroy(nmo_id_remap_t* remapper) {
    if (remapper != NULL) {
        free(remapper->mappings);
        free(remapper);
    }
}

nmo_result_t nmo_id_remap_add_mapping(nmo_id_remap_t* remapper, uint32_t old_id, uint32_t new_id) {
    if (remapper == NULL) {
        return nmo_result_error(NULL);
    }

    // Resize if load factor > 0.7
    if (remapper->count * 10 >= remapper->capacity * 7) {
        if (resize_table(remapper, remapper->capacity * 2) != 0) {
            return nmo_result_error(NULL);
        }
    }

    int found;
    int slot = find_slot(remapper, old_id, &found);
    if (slot < 0) {
        return nmo_result_error(NULL);  // Table full (shouldn't happen after resize)
    }

    if (!found) {
        remapper->count++;
    }

    remapper->mappings[slot].old_id = old_id;
    remapper->mappings[slot].new_id = new_id;
    remapper->mappings[slot].occupied = 1;

    return nmo_result_ok();
}

nmo_result_t nmo_id_remap_get_mapping(const nmo_id_remap_t* remapper, uint32_t old_id, uint32_t* out_new_id) {
    if (remapper == NULL || out_new_id == NULL) {
        return nmo_result_error(NULL);
    }

    int found;
    int slot = find_slot(remapper, old_id, &found);

    if (found && slot >= 0) {
        *out_new_id = remapper->mappings[slot].new_id;
        return nmo_result_ok();
    }

    return nmo_result_error(NULL);
}

int nmo_id_remap_has_mapping(const nmo_id_remap_t* remapper, uint32_t old_id) {
    if (remapper == NULL) {
        return 0;
    }

    int found;
    find_slot(remapper, old_id, &found);
    return found;
}

nmo_result_t nmo_id_remap_remove_mapping(nmo_id_remap_t* remapper, uint32_t old_id) {
    if (remapper == NULL) {
        return nmo_result_error(NULL);
    }

    int found;
    int slot = find_slot(remapper, old_id, &found);

    if (found && slot >= 0) {
        remapper->mappings[slot].occupied = 0;
        remapper->count--;
        return nmo_result_ok();
    }

    return nmo_result_error(NULL);
}

uint32_t nmo_id_remap_get_count(const nmo_id_remap_t* remapper) {
    if (remapper == NULL) {
        return 0;
    }

    return (uint32_t)remapper->count;
}

uint32_t nmo_id_remap_get_old_id_at(const nmo_id_remap_t* remapper, uint32_t index) {
    if (remapper == NULL) {
        return 0;
    }

    // Iterate through occupied slots
    uint32_t current = 0;
    for (size_t i = 0; i < remapper->capacity; i++) {
        if (remapper->mappings[i].occupied) {
            if (current == index) {
                return remapper->mappings[i].old_id;
            }
            current++;
        }
    }

    return 0;  // Index out of bounds
}

nmo_result_t nmo_id_remap_clear(nmo_id_remap_t* remapper) {
    if (remapper == NULL) {
        return nmo_result_error(NULL);
    }

    memset(remapper->mappings, 0, remapper->capacity * sizeof(nmo_id_mapping));
    remapper->count = 0;

    return nmo_result_ok();
}
