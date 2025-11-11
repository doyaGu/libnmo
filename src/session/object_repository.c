/**
 * @file object_repository.c
 * @brief Object repository implementation with hash tables
 */

#include "session/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 64

/**
 * Hash table entry for ID lookup
 */
typedef struct {
    nmo_object_id id;
    nmo_object* object;
    int occupied;
} id_entry;

/**
 * Hash table entry for name lookup
 */
typedef struct {
    const char* name;
    nmo_object* object;
    int occupied;
} name_entry;

/**
 * Object repository structure
 */
struct nmo_object_repository {
    nmo_arena* arena;

    /* ID hash table */
    id_entry* id_table;
    size_t id_capacity;
    size_t id_count;

    /* Name hash table */
    name_entry* name_table;
    size_t name_capacity;
    size_t name_count;

    /* Dense array for iteration */
    nmo_object** objects;
    size_t object_count;
    size_t object_capacity;
};

/**
 * Hash function for IDs
 */
static inline size_t hash_id(nmo_object_id id, size_t capacity) {
    return (id * 2654435761U) % capacity;
}

/**
 * Hash function for strings
 */
static size_t hash_string(const char* str, size_t capacity) {
    size_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + (size_t)c;
    }
    return hash % capacity;
}

/**
 * Find ID slot
 */
static int find_id_slot(const nmo_object_repository* repo, nmo_object_id id, int* found) {
    size_t index = hash_id(id, repo->id_capacity);
    size_t start = index;

    do {
        if (!repo->id_table[index].occupied) {
            *found = 0;
            return (int)index;
        }
        if (repo->id_table[index].id == id) {
            *found = 1;
            return (int)index;
        }
        index = (index + 1) % repo->id_capacity;
    } while (index != start);

    *found = 0;
    return -1;
}

/**
 * Find name slot
 */
static int find_name_slot(const nmo_object_repository* repo, const char* name, int* found) {
    if (name == NULL) {
        *found = 0;
        return -1;
    }

    size_t index = hash_string(name, repo->name_capacity);
    size_t start = index;

    do {
        if (!repo->name_table[index].occupied) {
            *found = 0;
            return (int)index;
        }
        if (strcmp(repo->name_table[index].name, name) == 0) {
            *found = 1;
            return (int)index;
        }
        index = (index + 1) % repo->name_capacity;
    } while (index != start);

    *found = 0;
    return -1;
}

/**
 * Resize ID table
 */
static int resize_id_table(nmo_object_repository* repo, size_t new_capacity) {
    id_entry* new_table = (id_entry*)calloc(new_capacity, sizeof(id_entry));
    if (new_table == NULL) {
        return NMO_ERR_NOMEM;
    }

    id_entry* old_table = repo->id_table;
    size_t old_capacity = repo->id_capacity;

    repo->id_table = new_table;
    repo->id_capacity = new_capacity;
    size_t old_count = repo->id_count;
    repo->id_count = 0;

    /* Rehash */
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_table[i].occupied) {
            int found;
            int slot = find_id_slot(repo, old_table[i].id, &found);
            if (slot >= 0) {
                repo->id_table[slot].id = old_table[i].id;
                repo->id_table[slot].object = old_table[i].object;
                repo->id_table[slot].occupied = 1;
                repo->id_count++;
            }
        }
    }

    free(old_table);
    return (repo->id_count == old_count) ? NMO_OK : NMO_ERR_INVALID_STATE;
}

/**
 * Resize name table
 */
static int resize_name_table(nmo_object_repository* repo, size_t new_capacity) {
    name_entry* new_table = (name_entry*)calloc(new_capacity, sizeof(name_entry));
    if (new_table == NULL) {
        return NMO_ERR_NOMEM;
    }

    name_entry* old_table = repo->name_table;
    size_t old_capacity = repo->name_capacity;

    repo->name_table = new_table;
    repo->name_capacity = new_capacity;
    size_t old_count = repo->name_count;
    repo->name_count = 0;

    /* Rehash */
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_table[i].occupied) {
            int found;
            int slot = find_name_slot(repo, old_table[i].name, &found);
            if (slot >= 0) {
                repo->name_table[slot].name = old_table[i].name;
                repo->name_table[slot].object = old_table[i].object;
                repo->name_table[slot].occupied = 1;
                repo->name_count++;
            }
        }
    }

    free(old_table);
    return (repo->name_count == old_count) ? NMO_OK : NMO_ERR_INVALID_STATE;
}

/**
 * Create object repository
 */
nmo_object_repository* nmo_object_repository_create(nmo_arena* arena) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_object_repository* repo = (nmo_object_repository*)malloc(sizeof(nmo_object_repository));
    if (repo == NULL) {
        return NULL;
    }

    repo->arena = arena;

    /* Allocate ID table */
    repo->id_table = (id_entry*)calloc(INITIAL_CAPACITY, sizeof(id_entry));
    if (repo->id_table == NULL) {
        free(repo);
        return NULL;
    }
    repo->id_capacity = INITIAL_CAPACITY;
    repo->id_count = 0;

    /* Allocate name table */
    repo->name_table = (name_entry*)calloc(INITIAL_CAPACITY, sizeof(name_entry));
    if (repo->name_table == NULL) {
        free(repo->id_table);
        free(repo);
        return NULL;
    }
    repo->name_capacity = INITIAL_CAPACITY;
    repo->name_count = 0;

    /* Allocate object array */
    repo->objects = (nmo_object**)malloc(INITIAL_CAPACITY * sizeof(nmo_object*));
    if (repo->objects == NULL) {
        free(repo->name_table);
        free(repo->id_table);
        free(repo);
        return NULL;
    }
    repo->object_capacity = INITIAL_CAPACITY;
    repo->object_count = 0;

    return repo;
}

/**
 * Destroy object repository
 */
void nmo_object_repository_destroy(nmo_object_repository* repository) {
    if (repository != NULL) {
        free(repository->objects);
        free(repository->name_table);
        free(repository->id_table);
        free(repository);
    }
}

/**
 * Add object to repository
 */
int nmo_object_repository_add(nmo_object_repository* repository, nmo_object* object) {
    if (repository == NULL || object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Check if ID table needs resize */
    if ((repository->id_count + 1) * 10 > repository->id_capacity * 7) {
        int result = resize_id_table(repository, repository->id_capacity * 2);
        if (result != NMO_OK) {
            return result;
        }
    }

    /* Add to ID table */
    int found;
    int slot = find_id_slot(repository, object->id, &found);
    if (slot < 0) {
        return NMO_ERR_NOMEM;
    }
    if (found) {
        return NMO_ERR_INVALID_STATE;  // Object already exists
    }

    repository->id_table[slot].id = object->id;
    repository->id_table[slot].object = object;
    repository->id_table[slot].occupied = 1;
    repository->id_count++;

    /* Add to name table if object has a name */
    if (object->name != NULL) {
        if ((repository->name_count + 1) * 10 > repository->name_capacity * 7) {
            int result = resize_name_table(repository, repository->name_capacity * 2);
            if (result != NMO_OK) {
                /* Rollback ID table entry */
                repository->id_table[slot].occupied = 0;
                repository->id_count--;
                return result;
            }
        }

        int name_slot = find_name_slot(repository, object->name, &found);
        if (name_slot >= 0 && !found) {
            repository->name_table[name_slot].name = object->name;
            repository->name_table[name_slot].object = object;
            repository->name_table[name_slot].occupied = 1;
            repository->name_count++;
        }
    }

    /* Add to dense array */
    if (repository->object_count >= repository->object_capacity) {
        size_t new_capacity = repository->object_capacity * 2;
        nmo_object** new_objects = (nmo_object**)realloc(repository->objects,
                                                          new_capacity * sizeof(nmo_object*));
        if (new_objects == NULL) {
            return NMO_ERR_NOMEM;
        }
        repository->objects = new_objects;
        repository->object_capacity = new_capacity;
    }

    repository->objects[repository->object_count++] = object;

    return NMO_OK;
}

/**
 * Find object by ID
 */
nmo_object* nmo_object_repository_find_by_id(const nmo_object_repository* repository,
                                              nmo_object_id id) {
    if (repository == NULL) {
        return NULL;
    }

    int found;
    int slot = find_id_slot(repository, id, &found);
    if (slot < 0 || !found) {
        return NULL;
    }

    return repository->id_table[slot].object;
}

/**
 * Find object by name
 */
nmo_object* nmo_object_repository_find_by_name(const nmo_object_repository* repository,
                                                const char* name) {
    if (repository == NULL || name == NULL) {
        return NULL;
    }

    int found;
    int slot = find_name_slot(repository, name, &found);
    if (slot < 0 || !found) {
        return NULL;
    }

    return repository->name_table[slot].object;
}

/**
 * Find objects by class
 */
nmo_object** nmo_object_repository_find_by_class(const nmo_object_repository* repository,
                                                  nmo_class_id class_id,
                                                  size_t* out_count) {
    if (repository == NULL || out_count == NULL) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Count matching objects */
    size_t count = 0;
    for (size_t i = 0; i < repository->object_count; i++) {
        if (repository->objects[i]->class_id == class_id) {
            count++;
        }
    }

    if (count == 0) {
        *out_count = 0;
        return NULL;
    }

    /* Allocate result array */
    nmo_object** result = (nmo_object**)nmo_arena_alloc(repository->arena,
                                                          count * sizeof(nmo_object*),
                                                          sizeof(void*));
    if (result == NULL) {
        *out_count = 0;
        return NULL;
    }

    /* Fill result */
    size_t j = 0;
    for (size_t i = 0; i < repository->object_count && j < count; i++) {
        if (repository->objects[i]->class_id == class_id) {
            result[j++] = repository->objects[i];
        }
    }

    *out_count = count;
    return result;
}

/**
 * Get object count
 */
size_t nmo_object_repository_get_count(const nmo_object_repository* repository) {
    return repository ? repository->object_count : 0;
}

/**
 * Get all objects
 */
nmo_object** nmo_object_repository_get_all(const nmo_object_repository* repository,
                                            size_t* out_count) {
    if (repository == NULL || out_count == NULL) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    *out_count = repository->object_count;
    return repository->objects;
}

/**
 * Remove object from repository
 */
int nmo_object_repository_remove(nmo_object_repository* repository, nmo_object_id id) {
    if (repository == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Find in ID table */
    int found;
    int slot = find_id_slot(repository, id, &found);
    if (slot < 0 || !found) {
        return NMO_ERR_INVALID_ARGUMENT;  // Object not found
    }

    nmo_object* object = repository->id_table[slot].object;

    /* Remove from ID table */
    repository->id_table[slot].occupied = 0;
    repository->id_count--;

    /* Remove from name table if applicable */
    if (object->name != NULL) {
        int name_slot = find_name_slot(repository, object->name, &found);
        if (name_slot >= 0 && found) {
            repository->name_table[name_slot].occupied = 0;
            repository->name_count--;
        }
    }

    /* Remove from dense array */
    for (size_t i = 0; i < repository->object_count; i++) {
        if (repository->objects[i] == object) {
            repository->objects[i] = repository->objects[repository->object_count - 1];
            repository->object_count--;
            break;
        }
    }

    return NMO_OK;
}

/**
 * Clear all objects
 */
int nmo_object_repository_clear(nmo_object_repository* repository) {
    if (repository == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(repository->id_table, 0, repository->id_capacity * sizeof(id_entry));
    memset(repository->name_table, 0, repository->name_capacity * sizeof(name_entry));
    repository->id_count = 0;
    repository->name_count = 0;
    repository->object_count = 0;

    return NMO_OK;
}
