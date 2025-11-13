/**
 * @file builder.c
 * @brief Minimal builder implementation for reference object support (Phase 4)
 * 
 * NOTE: This is a stub implementation focusing on reference object functionality.
 * Full builder implementation (save pipeline, chunking, etc.) is planned for Phase 5+.
 */

#include "session/nmo_builder.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
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
 * Builder structure (minimal implementation for Phase 4)
 */
struct nmo_builder {
    nmo_arena_t *arena;              /* Memory arena */
    
    /* File objects list */
    nmo_file_object_t *file_objects; /* Dynamic array of file objects */
    size_t object_count;             /* Current object count */
    size_t object_capacity;          /* Allocated capacity */
    
    /* Object tracking bitmasks (simple arrays for Phase 4) */
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
    nmo_file_object_t *new_objects = (nmo_file_object_t *) realloc(
        builder->file_objects, 
        new_capacity * sizeof(nmo_file_object_t)
    );
    
    if (new_objects == NULL) {
        return NMO_ERR_NOMEM;
    }
    
    builder->file_objects = new_objects;
    builder->object_capacity = new_capacity;
    
    return NMO_OK;
}

/**
 * Create builder
 */
nmo_builder_t *nmo_builder_create(const char *output_path) {
    (void)output_path;  /* Not used in stub implementation */
    
    nmo_builder_t *builder = (nmo_builder_t *) calloc(1, sizeof(nmo_builder_t));
    if (builder == NULL) {
        return NULL;
    }
    
    /* Create arena for string allocations */
    builder->arena = nmo_arena_create(4096);
    if (builder->arena == NULL) {
        free(builder);
        return NULL;
    }
    
    /* Allocate file objects array */
    builder->file_objects = (nmo_file_object_t *) malloc(
        INITIAL_OBJECT_CAPACITY * sizeof(nmo_file_object_t)
    );
    if (builder->file_objects == NULL) {
        nmo_arena_destroy(builder->arena);
        free(builder);
        return NULL;
    }
    
    builder->object_capacity = INITIAL_OBJECT_CAPACITY;
    builder->object_count = 0;
    
    /* Allocate bitmasks (support up to 1024 objects initially) */
    builder->mask_size = 32;  /* 32 * 32 bits = 1024 objects */
    builder->saved_mask = (uint32_t *) calloc(builder->mask_size, sizeof(uint32_t));
    builder->referenced_mask = (uint32_t *) calloc(builder->mask_size, sizeof(uint32_t));
    
    if (builder->saved_mask == NULL || builder->referenced_mask == NULL) {
        free(builder->saved_mask);
        free(builder->referenced_mask);
        free(builder->file_objects);
        nmo_arena_destroy(builder->arena);
        free(builder);
        return NULL;
    }
    
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
    
    free(builder->file_objects);
    free(builder->saved_mask);
    free(builder->referenced_mask);
    nmo_arena_destroy(builder->arena);
    free(builder);
}

/**
 * Add object as reference (Phase 4 implementation)
 * 
 * Based on CKFile::SaveObjectAsReference (reference/src/CKFile.cpp:810-838)
 */
nmo_result_t nmo_builder_add_object_as_reference(nmo_builder_t *builder, nmo_object_t *object) {
    if (builder == NULL || object == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    nmo_object_id_t obj_id = nmo_object_get_id(object);
    if (obj_id == NMO_OBJECT_ID_NONE) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Check if already saved or referenced (matches m_AlreadySavedMask/m_AlreadyReferencedMask) */
    if (is_bit_set(builder->saved_mask, builder->mask_size, obj_id) ||
        is_bit_set(builder->referenced_mask, builder->mask_size, obj_id)) {
        return NMO_OK;  /* Already processed, not an error */
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
            return result;
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
    
    /* TODO: Track scene/level if needed (matches CKIsChildClassOf check) */
    /* This requires class hierarchy info not available in Phase 4 */
    
    return NMO_OK;
}

/**
 * Stub implementations for remaining builder API
 * (Full implementation planned for Phase 5+)
 */

nmo_result_t nmo_builder_start(nmo_builder_t *builder) {
    if (builder == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    builder->stage = NMO_BUILD_STAGE_HEADER;
    return NMO_OK;
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

nmo_result_t nmo_builder_add_object(
    nmo_builder_t *builder, uint32_t object_id, uint32_t manager_id, const void *data, size_t size) {
    (void)object_id;
    (void)manager_id;
    (void)data;
    (void)size;
    
    if (builder == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    /* Stub: Not implemented yet */
    snprintf(builder->error_msg, sizeof(builder->error_msg), 
             "nmo_builder_add_object not implemented (Phase 5+)");
    return NMO_ERR_NOT_IMPLEMENTED;
}

nmo_result_t nmo_builder_finish(nmo_builder_t *builder) {
    if (builder == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    builder->stage = NMO_BUILD_STAGE_COMPLETED;
    return NMO_OK;
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
