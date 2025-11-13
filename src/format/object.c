#include "format/nmo_object.h"
#include <string.h>

#define INITIAL_CHILD_CAPACITY 4

nmo_object_t *nmo_object_create(nmo_arena_t *arena, nmo_object_id_t id, nmo_class_id_t class_id) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_object_t *object = (nmo_object_t *) nmo_arena_alloc(arena, sizeof(nmo_object_t), sizeof(void *));
    if (object == NULL) {
        return NULL;
    }

    memset(object, 0, sizeof(nmo_object_t));
    object->id = id;
    object->class_id = class_id;
    object->arena = arena;
    object->file_index = id; // Default to same as runtime ID

    return object;
}

void nmo_object_destroy(nmo_object_t *object) {
    // Arena allocation - no explicit cleanup needed
    (void) object;
}

int nmo_object_set_name(nmo_object_t *object, const char *name, nmo_arena_t *arena) {
    if (object == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (name == NULL) {
        object->name = NULL;
        return NMO_OK;
    }

    size_t name_len = strlen(name);
    char *name_copy = (char *) nmo_arena_alloc(arena, name_len + 1, 1);
    if (name_copy == NULL) {
        return NMO_ERR_NOMEM;
    }

    memcpy(name_copy, name, name_len + 1);
    object->name = name_copy;

    return NMO_OK;
}

const char *nmo_object_get_name(const nmo_object_t *object) {
    if (object == NULL) {
        return NULL;
    }
    return object->name;
}

int nmo_object_add_child(nmo_object_t *parent, nmo_object_t *child, nmo_arena_t *arena) {
    if (parent == NULL || child == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Check if we need to grow the children array
    if (parent->child_count >= parent->child_capacity) {
        size_t new_capacity = parent->child_capacity == 0 ? INITIAL_CHILD_CAPACITY : parent->child_capacity * 2;

        nmo_object_t **new_children = (nmo_object_t **) nmo_arena_alloc(arena,
                                                                        new_capacity * sizeof(nmo_object_t *),
                                                                        sizeof(void *));
        if (new_children == NULL) {
            return NMO_ERR_NOMEM;
        }

        // Copy existing children
        if (parent->children != NULL && parent->child_count > 0) {
            memcpy(new_children, parent->children, parent->child_count * sizeof(nmo_object_t *));
        }

        parent->children = new_children;
        parent->child_capacity = new_capacity;
    }

    // Add child
    parent->children[parent->child_count++] = child;
    child->parent = parent;

    return NMO_OK;
}

int nmo_object_remove_child(nmo_object_t *parent, nmo_object_t *child) {
    if (parent == NULL || child == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Find child in array
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            // Shift remaining children down
            for (size_t j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            child->parent = NULL;
            return NMO_OK;
        }
    }

    return NMO_ERR_INVALID_ARGUMENT; // Child not found
}

nmo_object_t *nmo_object_get_child(const nmo_object_t *object, size_t index) {
    if (object == NULL || index >= object->child_count) {
        return NULL;
    }
    return object->children[index];
}

size_t nmo_object_get_child_count(const nmo_object_t *object) {
    if (object == NULL) {
        return 0;
    }
    return object->child_count;
}

int nmo_object_set_chunk(nmo_object_t *object, nmo_chunk_t *chunk) {
    if (object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object->chunk = chunk;
    return NMO_OK;
}

nmo_chunk_t *nmo_object_get_chunk(const nmo_object_t *object) {
    if (object == NULL) {
        return NULL;
    }
    return object->chunk;
}

int nmo_object_set_data(nmo_object_t *object, void *data) {
    if (object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object->data = data;
    return NMO_OK;
}

void *nmo_object_get_data(const nmo_object_t *object) {
    if (object == NULL) {
        return NULL;
    }
    return object->data;
}

int nmo_object_set_file_index(nmo_object_t *object, nmo_object_id_t file_index) {
    if (object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    object->file_index = file_index;
    return NMO_OK;
}

nmo_object_id_t nmo_object_get_file_index(const nmo_object_t *object) {
    if (object == NULL) {
        return 0;
    }
    return object->file_index;
}

nmo_guid_t nmo_object_get_type_guid(const nmo_object_t *object) {
    nmo_guid_t null_guid = {0, 0};
    if (object == NULL) {
        return null_guid;
    }
    return object->type_guid;
}

int nmo_object_set_type_guid(nmo_object_t *object, nmo_guid_t guid) {
    if (object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    object->type_guid = guid;
    return NMO_OK;
}
