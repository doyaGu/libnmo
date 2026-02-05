#include "format/nmo_object.h"
#include "type/nmo_type_system.h"
#include "core/nmo_guid.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include <string.h>

#define INITIAL_CHILD_CAPACITY 4

nmo_object_t *nmo_object_create(const nmo_allocator_t *allocator,
                               nmo_object_id_t id,
                               nmo_class_id_t class_id)
{
    nmo_allocator_t effective = (allocator != NULL) ? *allocator : nmo_allocator_default();

    nmo_object_t *object = (nmo_object_t *)nmo_alloc(&effective, sizeof(nmo_object_t), _Alignof(nmo_object_t));
    if (object == NULL) {
        return NULL;
    }

    memset(object, 0, sizeof(*object));
    object->allocator = effective;
    object->storage_arena = nmo_arena_create(&object->allocator, 0);
    if (object->storage_arena == NULL) {
        nmo_free(&object->allocator, object);
        return NULL;
    }

    object->id = id;
    object->class_id = class_id;
    object->file_index = 0;
    object->file_id = 0;

    return object;
}

void nmo_object_destroy(nmo_object_t *object) {
    if (object == NULL) {
        return;
    }

    /* Free allocator-managed metadata */
    if (object->children != NULL) {
        nmo_free(&object->allocator, object->children);
        object->children = NULL;
    }

    if (object->name != NULL) {
        nmo_free(&object->allocator, (void *)object->name);
        object->name = NULL;
    }

    if (object->state != NULL) {
        nmo_free(&object->allocator, object->state);
        object->state = NULL;
        object->state_size = 0;
    }

    /* Destroy per-object arena (schema-owned buffers) */
    if (object->storage_arena != NULL) {
        nmo_arena_destroy(object->storage_arena);
        object->storage_arena = NULL;
    }

    nmo_free(&object->allocator, object);
}

int nmo_object_set_name(nmo_object_t *object, const char *name) {
    if (object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (object->name != NULL) {
        nmo_free(&object->allocator, (void *)object->name);
        object->name = NULL;
    }

    if (name == NULL) {
        return NMO_OK;
    }

    size_t name_len = strlen(name);
    char *name_copy = (char *)nmo_alloc(&object->allocator, name_len + 1, 1);
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

int nmo_object_add_child(nmo_object_t *parent, nmo_object_t *child) {
    if (parent == NULL || child == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Check if we need to grow the children array
    if (parent->child_count >= parent->child_capacity) {
        size_t new_capacity = parent->child_capacity == 0 ? INITIAL_CHILD_CAPACITY : parent->child_capacity * 2;

        nmo_object_t **new_children = (nmo_object_t **)nmo_alloc(&parent->allocator,
                                                                 new_capacity * sizeof(nmo_object_t *),
                                                                 _Alignof(nmo_object_t *));
        if (new_children == NULL) {
            return NMO_ERR_NOMEM;
        }

        // Copy existing children
        if (parent->children != NULL && parent->child_count > 0) {
            memcpy(new_children, parent->children, parent->child_count * sizeof(nmo_object_t *));
        }

        if (parent->children != NULL) {
            nmo_free(&parent->allocator, parent->children);
        }
        parent->children = new_children;
        parent->child_capacity = new_capacity;
    }

    // Add child
    parent->children[parent->child_count++] = child;
    child->parent = parent;

    return NMO_OK;
}

nmo_arena_t *nmo_object_get_storage_arena(const nmo_object_t *object) {
    return object ? object->storage_arena : NULL;
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

nmo_object_id_t nmo_object_get_id(const nmo_object_t *object) {
    return object ? object->id : 0;
}

nmo_class_id_t nmo_object_get_class_id(const nmo_object_t *object) {
    return object ? object->class_id : 0;
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

/* ============================================================================
 * State Access (ECS-style combined state)
 * ============================================================================ */

nmo_status_t nmo_object_alloc_state(nmo_object_t *object, uint32_t size) {
    if (object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    if (size == 0) {
        if (object->state != NULL) {
            nmo_free(&object->allocator, object->state);
        }
        object->state = NULL;
        object->state_size = 0;
        return NMO_OK;
    }
    
    /* Allocate with 8-byte alignment for any state structure */
    void *state = nmo_alloc(&object->allocator, size, 8);
    if (state == NULL) {
        return NMO_ERR_NOMEM;
    }

    if (object->state != NULL) {
        nmo_free(&object->allocator, object->state);
    }
    
    memset(state, 0, size);
    object->state = state;
    object->state_size = size;
    
    return NMO_OK;
}

void *nmo_object_get_state(const nmo_object_t *object) {
    if (object == NULL) {
        return NULL;
    }
    return object->state;
}

uint32_t nmo_object_get_state_size(const nmo_object_t *object) {
    if (object == NULL) {
        return 0;
    }
    return object->state_size;
}

void *nmo_object_get_ancestor_state(
    const nmo_object_t *object,
    const nmo_type_descriptor_t *type_desc,
    const nmo_type_descriptor_t *derived_type_desc)
{
    if (object == NULL || type_desc == NULL || derived_type_desc == NULL) {
        return NULL;
    }
    
    if (object->state == NULL) {
        return NULL;
    }
    
    /* Find offset for the ancestor type */
    for (uint16_t i = 0; i < derived_type_desc->hierarchy_depth; i++) {
        if (derived_type_desc->hierarchy[i] == type_desc ||
            nmo_guid_equals(derived_type_desc->hierarchy[i]->guid, type_desc->guid)) {
            uint32_t offset = derived_type_desc->state_offsets[i];
            return (uint8_t *)object->state + offset;
        }
    }
    
    return NULL;
}
