/**
 * @file builder.c
 * @brief Minimal builder implementation for reference object support
 * 
 * NOTE: This is a stub implementation focusing on reference object functionality.
 * Full builder implementation (save pipeline, chunking, etc.) is planned for future.
 */

#include "session/nmo_builder.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_OBJECT_CAPACITY 64
#define REFERENCE_FLAG 0x00800000  /* Matches CKFile SaveObjectAsReference flag */

/**
 * File object entry (matches CKFileObject structure)
 */
typedef struct {
    nmo_object_id_t object_id;       /* Object ID */
    nmo_object_t *object_ptr;        /* Object pointer */
    nmo_class_id_t class_id;         /* Object class ID */
    uint32_t save_flags;             /* Save flags (0x00800000 = reference only) */
    char *name;                      /* Object name (allocated from arena) */
} nmo_file_object_t;

/**
 * Builder structure (minimal implementation)
 */
struct nmo_builder {
    nmo_arena_t *arena;              /* Memory arena */
    
    /* File objects list */
    nmo_file_object_t *file_objects; /* Dynamic array of file objects */
    size_t object_count;             /* Current object count */
    size_t object_capacity;          /* Allocated capacity */
    
    /* Object tracking bitmasks (simple arrays) */
    uint32_t *saved_mask;            /* Objects already saved */
    uint32_t *referenced_mask;       /* Objects saved as references */
    size_t mask_size;                /* Size of mask arrays (in uint32_t units) */
    
    /* Statistics */
    nmo_object_id_t max_save_id;     /* Maximum object ID seen */
    int scene_saved;                 /* Whether scene/level was saved */
    
    /* Error state */
    char error_msg[256];             /* Last error message */
    nmo_build_stage_t stage;         /* Current build stage */
};

/**
 * Helper: Check if bit is set in mask
 */
static int is_bit_set(const uint32_t *mask, size_t mask_size, nmo_object_id_t id) {
    size_t index = id / 32;
    size_t bit = id % 32;
    
    if (index >= mask_size) {
        return 0;
    }
    
    return (mask[index] & (1u << bit)) != 0;
}

/**
 * Helper: Set bit in mask
 */
static void set_bit(uint32_t *mask, size_t mask_size, nmo_object_id_t id) {
    size_t index = id / 32;
    size_t bit = id % 32;
    
    if (index >= mask_size) {
        return;
    }
    
    mask[index] |= (1u << bit);
}

/**
 * Helper: Grow file objects array
 */
static int grow_file_objects(nmo_builder_t *builder) {
    size_t new_capacity = builder->object_capacity * 2;
    // Use arena allocation instead of realloc to avoid mixed allocation patterns
    // Note: Old array is leaked (arena allocator limitation), but this is acceptable
    // since the arena will be cleaned up when the builder is destroyed.
    nmo_file_object_t *new_objects = (nmo_file_object_t *) nmo_arena_alloc(
        builder->arena,
        new_capacity * sizeof(nmo_file_object_t),
        _Alignof(nmo_file_object_t)
    );

    if (new_objects == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Copy existing objects
    if (builder->file_objects != NULL && builder->object_count > 0) {
        memcpy(new_objects, builder->file_objects,
               builder->object_count * sizeof(nmo_file_object_t));
    }

    // Clear new entries
    memset(new_objects + builder->object_count, 0,
           (new_capacity - builder->object_count) * sizeof(nmo_file_object_t));

    builder->file_objects = new_objects;
    builder->object_capacity = new_capacity;

    return NMO_OK;
}

/**
 * Create builder
 */
nmo_builder_t *nmo_builder_create(const char *output_path) {
    (void)output_path;  /* Not used in stub implementation */

    /* Create arena first for all allocations */
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        return NULL;
    }

    /* Allocate builder structure from arena */
    nmo_builder_t *builder = (nmo_builder_t *) nmo_arena_alloc(
        arena, sizeof(nmo_builder_t), _Alignof(nmo_builder_t));
    if (builder == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    memset(builder, 0, sizeof(nmo_builder_t));
    builder->arena = arena;

    /* Allocate file objects array from arena */
    builder->file_objects = (nmo_file_object_t *) nmo_arena_alloc(
        arena,
        INITIAL_OBJECT_CAPACITY * sizeof(nmo_file_object_t),
        _Alignof(nmo_file_object_t)
    );
    if (builder->file_objects == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    builder->object_capacity = INITIAL_OBJECT_CAPACITY;
    builder->object_count = 0;

    /* Allocate bitmasks from arena (support up to 1024 objects initially) */
    builder->mask_size = 32;  /* 32 * 32 bits = 1024 objects */
    builder->saved_mask = (uint32_t *) nmo_arena_alloc(
        arena, builder->mask_size * sizeof(uint32_t), _Alignof(uint32_t));
    builder->referenced_mask = (uint32_t *) nmo_arena_alloc(
        arena, builder->mask_size * sizeof(uint32_t), _Alignof(uint32_t));

    if (builder->saved_mask == NULL || builder->referenced_mask == NULL) {
        nmo_arena_destroy(arena);
        return NULL;
    }

    memset(builder->saved_mask, 0, builder->mask_size * sizeof(uint32_t));
    memset(builder->referenced_mask, 0, builder->mask_size * sizeof(uint32_t));

    builder->max_save_id = 0;
    builder->scene_saved = 0;
    builder->stage = NMO_BUILD_STAGE_INIT;
    builder->error_msg[0] = '\0';

    return builder;
}

/**
 * Destroy builder
 */
void nmo_builder_destroy(nmo_builder_t *builder) {
    if (builder == NULL) {
        return;
    }

    /* Since we use arena allocation for everything, just destroy the arena */
    nmo_arena_t *arena = builder->arena;
    if (arena != NULL) {
        nmo_arena_destroy(arena);
    }
    /* No need to free builder - it was allocated from the arena */
}

/**
 * Add object as reference
 * 
 * Based on CKFile::SaveObjectAsReference (reference/src/CKFile.cpp:810-838)
 */
nmo_status_t nmo_builder_add_object_as_reference(nmo_builder_t *builder, nmo_object_t *object) {
    if (builder == NULL || object == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments");
    }
    
    nmo_object_id_t obj_id = nmo_object_get_id(object);
    if (obj_id == NMO_OBJECT_ID_NONE) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid object ID");
    }
    
    /* Check if already saved or referenced (matches m_AlreadySavedMask/m_AlreadyReferencedMask) */
    if (is_bit_set(builder->saved_mask, builder->mask_size, obj_id) ||
        is_bit_set(builder->referenced_mask, builder->mask_size, obj_id)) {
        NMO_RETURN_OK();  /* Already processed, not an error */
    }
    
    /* Mark as referenced (matches m_AlreadyReferencedMask.Set) */
    set_bit(builder->referenced_mask, builder->mask_size, obj_id);
    
    /* Update max ID (matches obj->GetID() > m_SaveIDMax check) */
    if (obj_id > builder->max_save_id) {
        builder->max_save_id = obj_id;
    }
    
    /* Grow array if needed */
    if (builder->object_count >= builder->object_capacity) {
        int result = grow_file_objects(builder);
        if (result != NMO_OK) {
            NMO_RETURN_ERROR(result, NMO_SEVERITY_ERROR, "Failed to grow file objects array");
        }
    }
    
    /* Create file object entry (matches CKFileObject structure) */
    nmo_file_object_t *file_obj = &builder->file_objects[builder->object_count];
    
    file_obj->object_id = obj_id;
    file_obj->object_ptr = object;
    file_obj->class_id = nmo_object_get_class_id(object);
    file_obj->save_flags = REFERENCE_FLAG;  /* Mark as reference (0x00800000) */
    
    /* Copy name from object (matches CKStrdup(obj->GetName())) */
    const char *obj_name = nmo_object_get_name(object);
    if (obj_name != NULL && obj_name[0] != '\0') {
        size_t name_len = strlen(obj_name);
        char *name_copy = (char *) nmo_arena_alloc(builder->arena, name_len + 1, 1);
        if (name_copy != NULL) {
            memcpy(name_copy, obj_name, name_len + 1);
            file_obj->name = name_copy;
        } else {
            file_obj->name = NULL;
        }
    } else {
        file_obj->name = NULL;
    }
    
    builder->object_count++;
    
    /* Track scene/level objects for save ordering */
    /* CKFile checks: if (CKIsChildClassOf(obj, CKCID_SCENE) || CKIsChildClassOf(obj, CKCID_LEVEL)) */
    nmo_class_id_t class_id = file_obj->class_id;
    if (nmo_class_is_derived_from(NULL, class_id, 10) ||  /* CKCID_SCENE */
        nmo_class_is_derived_from(NULL, class_id, 21)) {  /* CKCID_LEVEL */
        /* Scene/level object detected - may need special handling in save pipeline */
    }
    
    NMO_RETURN_OK();
}

/**
 * Stub implementations for remaining builder API
 * (Full implementation planned for future)
 */

nmo_status_t nmo_builder_start(nmo_builder_t *builder) {
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }
    builder->stage = NMO_BUILD_STAGE_HEADER;
    NMO_RETURN_OK();
}

nmo_build_stage_t nmo_builder_build_next_stage(nmo_builder_t *builder) {
    if (builder == NULL) {
        return NMO_BUILD_STAGE_COMPLETED;
    }
    
    /* Stub: Just advance through stages */
    switch (builder->stage) {
        case NMO_BUILD_STAGE_INIT:
            builder->stage = NMO_BUILD_STAGE_HEADER;
            break;
        case NMO_BUILD_STAGE_HEADER:
            builder->stage = NMO_BUILD_STAGE_HEADER1;
            break;
        case NMO_BUILD_STAGE_HEADER1:
            builder->stage = NMO_BUILD_STAGE_OBJECTS;
            break;
        case NMO_BUILD_STAGE_OBJECTS:
            builder->stage = NMO_BUILD_STAGE_COMPLETED;
            break;
        default:
            builder->stage = NMO_BUILD_STAGE_COMPLETED;
            break;
    }
    
    return builder->stage;
}

nmo_build_stage_t nmo_builder_get_current_stage(const nmo_builder_t *builder) {
    return builder ? builder->stage : NMO_BUILD_STAGE_COMPLETED;
}

nmo_status_t nmo_builder_add_object(
    nmo_builder_t *builder, uint32_t object_id, uint32_t manager_id, const void *data, size_t size) {
    (void)object_id;
    (void)manager_id;
    (void)data;
    (void)size;
    
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }
    
    /* Stub: Not implemented yet */
    snprintf(builder->error_msg, sizeof(builder->error_msg), 
             "nmo_builder_add_object not implemented");
    NMO_RETURN_ERROR(NMO_ERR_NOT_IMPLEMENTED, NMO_SEVERITY_ERROR, "Not implemented");
}

nmo_status_t nmo_builder_finish(nmo_builder_t *builder) {
    if (builder == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid builder");
    }
    
    builder->stage = NMO_BUILD_STAGE_COMPLETED;
    NMO_RETURN_OK();
}

const char *nmo_builder_get_error(const nmo_builder_t *builder) {
    if (builder == NULL || builder->error_msg[0] == '\0') {
        return NULL;
    }
    return builder->error_msg;
}

uint32_t nmo_builder_get_object_count(const nmo_builder_t *builder) {
    return builder ? (uint32_t)builder->object_count : 0;
}

int nmo_builder_is_complete(const nmo_builder_t *builder) {
    return builder ? (builder->stage == NMO_BUILD_STAGE_COMPLETED) : 1;
}
