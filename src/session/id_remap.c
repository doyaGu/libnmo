/**
 * @file id_remap.c
 * @brief ID remapping implementation for load and save operations
 */

#include "session/nmo_id_remap.h"
#include "session/nmo_load_session.h"
#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include <stdlib.h>
#include <string.h>

/* Forward declarations for internal functions */
extern int nmo_load_session_get_mappings(const nmo_load_session* session,
                                          nmo_object_id** file_ids,
                                          nmo_object_id** runtime_ids,
                                          size_t* count);

/**
 * Hash table entry for ID remapping
 */
typedef struct {
    nmo_object_id old_id;
    nmo_object_id new_id;
    int occupied;
} remap_entry;

/**
 * ID remap table structure
 */
struct nmo_id_remap_table {
    remap_entry* entries;
    size_t capacity;
    size_t count;

    /* Dense array for iteration */
    nmo_id_remap_entry* entry_list;
};

/**
 * ID remap plan structure
 */
struct nmo_id_remap_plan {
    nmo_id_remap_table* table;
    size_t objects_remapped;
    size_t conflicts_resolved;
};

/**
 * Hash function for IDs
 */
static inline size_t hash_id(nmo_object_id id, size_t capacity) {
    return (id * 2654435761U) % capacity;
}

/**
 * Find entry slot in hash table
 */
static int find_entry_slot(const nmo_id_remap_table* table,
                            nmo_object_id old_id,
                            int* found) {
    size_t index = hash_id(old_id, table->capacity);
    size_t start = index;

    do {
        if (!table->entries[index].occupied) {
            *found = 0;
            return (int)index;
        }
        if (table->entries[index].old_id == old_id) {
            *found = 1;
            return (int)index;
        }
        index = (index + 1) % table->capacity;
    } while (index != start);

    *found = 0;
    return -1;
}

/**
 * Add entry to remap table
 */
static int add_entry(nmo_id_remap_table* table,
                     nmo_object_id old_id,
                     nmo_object_id new_id) {
    int found;
    int slot = find_entry_slot(table, old_id, &found);
    if (slot < 0) {
        return NMO_ERR_NOMEM;
    }

    if (!found) {
        table->count++;
    }

    table->entries[slot].old_id = old_id;
    table->entries[slot].new_id = new_id;
    table->entries[slot].occupied = 1;

    return NMO_OK;
}

/**
 * Build ID remap table from load session
 */
nmo_id_remap_table* nmo_build_remap_table(nmo_load_session* session) {
    if (session == NULL) {
        return NULL;
    }

    /* Get mappings from load session */
    nmo_object_id* file_ids = NULL;
    nmo_object_id* runtime_ids = NULL;
    size_t count = 0;

    int result = nmo_load_session_get_mappings(session, &file_ids, &runtime_ids, &count);
    if (result != NMO_OK || count == 0) {
        return NULL;
    }

    /* Create remap table */
    nmo_id_remap_table* table = (nmo_id_remap_table*)malloc(sizeof(nmo_id_remap_table));
    if (table == NULL) {
        free(file_ids);
        free(runtime_ids);
        return NULL;
    }

    /* Allocate hash table (2x count for low load factor) */
    size_t capacity = count * 2;
    table->entries = (remap_entry*)calloc(capacity, sizeof(remap_entry));
    if (table->entries == NULL) {
        free(table);
        free(file_ids);
        free(runtime_ids);
        return NULL;
    }

    table->capacity = capacity;
    table->count = 0;

    /* Allocate entry list */
    table->entry_list = (nmo_id_remap_entry*)malloc(count * sizeof(nmo_id_remap_entry));
    if (table->entry_list == NULL) {
        free(table->entries);
        free(table);
        free(file_ids);
        free(runtime_ids);
        return NULL;
    }

    /* Add mappings */
    for (size_t i = 0; i < count; i++) {
        add_entry(table, file_ids[i], runtime_ids[i]);
        table->entry_list[i].old_id = file_ids[i];
        table->entry_list[i].new_id = runtime_ids[i];
    }

    free(file_ids);
    free(runtime_ids);

    return table;
}

/**
 * Lookup remapped ID
 */
int nmo_id_remap_lookup(const nmo_id_remap_table* table,
                        nmo_object_id old_id,
                        nmo_object_id* new_id) {
    if (table == NULL || new_id == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int found;
    int slot = find_entry_slot(table, old_id, &found);
    if (slot >= 0 && found) {
        *new_id = table->entries[slot].new_id;
        return NMO_OK;
    }

    return NMO_ERR_INVALID_ARGUMENT;
}

/**
 * Get entry count
 */
size_t nmo_id_remap_table_get_count(const nmo_id_remap_table* table) {
    return table ? table->count : 0;
}

/**
 * Get entry at index
 */
const nmo_id_remap_entry* nmo_id_remap_table_get_entry(
    const nmo_id_remap_table* table, size_t index) {
    if (table == NULL || index >= table->count) {
        return NULL;
    }

    return &table->entry_list[index];
}

/**
 * Destroy remap table
 */
void nmo_id_remap_table_destroy(nmo_id_remap_table* table) {
    if (table != NULL) {
        free(table->entry_list);
        free(table->entries);
        free(table);
    }
}

/**
 * Create ID remap plan for save
 */
nmo_id_remap_plan* nmo_id_remap_plan_create(nmo_object_repository* repo,
                                              nmo_object** objects_to_save,
                                              size_t object_count) {
    if (repo == NULL || objects_to_save == NULL || object_count == 0) {
        return NULL;
    }

    /* Create plan */
    nmo_id_remap_plan* plan = (nmo_id_remap_plan*)malloc(sizeof(nmo_id_remap_plan));
    if (plan == NULL) {
        return NULL;
    }

    /* Create remap table */
    plan->table = (nmo_id_remap_table*)malloc(sizeof(nmo_id_remap_table));
    if (plan->table == NULL) {
        free(plan);
        return NULL;
    }

    /* Allocate hash table */
    size_t capacity = object_count * 2;
    plan->table->entries = (remap_entry*)calloc(capacity, sizeof(remap_entry));
    if (plan->table->entries == NULL) {
        free(plan->table);
        free(plan);
        return NULL;
    }

    plan->table->capacity = capacity;
    plan->table->count = 0;

    /* Allocate entry list */
    plan->table->entry_list = (nmo_id_remap_entry*)malloc(object_count *
                                                            sizeof(nmo_id_remap_entry));
    if (plan->table->entry_list == NULL) {
        free(plan->table->entries);
        free(plan->table);
        free(plan);
        return NULL;
    }

    plan->objects_remapped = 0;
    plan->conflicts_resolved = 0;

    /* Map runtime IDs to sequential file IDs (0-based) */
    for (size_t i = 0; i < object_count; i++) {
        nmo_object* obj = objects_to_save[i];
        nmo_object_id runtime_id = obj->id;
        nmo_object_id file_id = (nmo_object_id)i;

        /* Add mapping */
        int result = add_entry(plan->table, runtime_id, file_id);
        if (result == NMO_OK) {
            plan->table->entry_list[i].old_id = runtime_id;
            plan->table->entry_list[i].new_id = file_id;
            plan->objects_remapped++;
        }
    }

    return plan;
}

/**
 * Lookup remapped ID from plan
 */
int nmo_id_remap_plan_lookup(const nmo_id_remap_plan* plan,
                              nmo_object_id runtime_id,
                              nmo_object_id* file_id) {
    if (plan == NULL || plan->table == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_id_remap_lookup(plan->table, runtime_id, file_id);
}

/**
 * Get remap table from plan
 */
nmo_id_remap_table* nmo_id_remap_plan_get_table(const nmo_id_remap_plan* plan) {
    return plan ? plan->table : NULL;
}

/**
 * Get objects remapped count
 */
size_t nmo_id_remap_plan_get_remapped_count(const nmo_id_remap_plan* plan) {
    return plan ? plan->objects_remapped : 0;
}

/**
 * Get conflicts resolved count
 */
size_t nmo_id_remap_plan_get_conflicts_resolved(const nmo_id_remap_plan* plan) {
    return plan ? plan->conflicts_resolved : 0;
}

/**
 * Destroy remap plan
 */
void nmo_id_remap_plan_destroy(nmo_id_remap_plan* plan) {
    if (plan != NULL) {
        nmo_id_remap_table_destroy(plan->table);
        free(plan);
    }
}
