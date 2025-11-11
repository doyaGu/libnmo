#ifndef NMO_OBJECT_H
#define NMO_OBJECT_H

#include "nmo_types.h"
#include "nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_object.h
 * @brief Object metadata and runtime representation
 *
 * Represents a Virtools object in memory with its metadata, relationships,
 * and associated chunk data.
 */

/* Forward declaration */
typedef struct nmo_object nmo_object;

/**
 * @brief Object structure
 *
 * Runtime representation of a Virtools object with metadata, hierarchical
 * relationships, and chunk data.
 */
struct nmo_object {
    /* Identity */
    nmo_object_id      id;              /**< Runtime object ID */
    nmo_class_id       class_id;        /**< Object class ID */
    const char*        name;            /**< Object name (optional) */
    uint32_t          flags;           /**< Object flags */

    /* Hierarchy */
    nmo_object*        parent;          /**< Parent object (NULL for root) */
    nmo_object**       children;        /**< Child objects array */
    size_t            child_count;     /**< Number of children */
    size_t            child_capacity;  /**< Children array capacity */

    /* Data */
    nmo_chunk*         chunk;           /**< Associated chunk data */
    void*             data;            /**< Custom data pointer */

    /* File context */
    nmo_object_id      file_index;      /**< Original file ID */
    uint32_t          creation_flags;  /**< Flags used during creation */
    uint32_t          save_flags;      /**< Flags for saving */

    /* Memory management */
    nmo_arena*         arena;           /**< Arena for allocations */
};

/**
 * @brief Create object
 *
 * @param arena Arena for allocation (required)
 * @param id Runtime object ID
 * @param class_id Object class ID
 * @return Object or NULL on allocation failure
 */
NMO_API nmo_object* nmo_object_create(nmo_arena* arena, nmo_object_id id, nmo_class_id class_id);

/**
 * @brief Destroy object
 *
 * Since objects use arena allocation, this is mostly a no-op.
 * The arena itself handles cleanup.
 *
 * @param object Object to destroy
 */
NMO_API void nmo_object_destroy(nmo_object* object);

/**
 * @brief Set object name
 *
 * @param object Object (required)
 * @param name Name string (will be copied, can be NULL)
 * @param arena Arena for name allocation
 * @return NMO_OK on success
 */
NMO_API int nmo_object_set_name(nmo_object* object, const char* name, nmo_arena* arena);

/**
 * @brief Get object name
 *
 * @param object Object (required)
 * @return Name string or NULL if not set
 */
NMO_API const char* nmo_object_get_name(const nmo_object* object);

/**
 * @brief Add child object
 *
 * @param parent Parent object (required)
 * @param child Child object (required)
 * @param arena Arena for children array allocation
 * @return NMO_OK on success
 */
NMO_API int nmo_object_add_child(nmo_object* parent, nmo_object* child, nmo_arena* arena);

/**
 * @brief Remove child object
 *
 * @param parent Parent object (required)
 * @param child Child object (required)
 * @return NMO_OK on success, NMO_ERR_INVALID_ARGUMENT if not found
 */
NMO_API int nmo_object_remove_child(nmo_object* parent, nmo_object* child);

/**
 * @brief Get child by index
 *
 * @param object Parent object (required)
 * @param index Child index
 * @return Child object or NULL if index out of bounds
 */
NMO_API nmo_object* nmo_object_get_child(const nmo_object* object, size_t index);

/**
 * @brief Get child count
 *
 * @param object Object (required)
 * @return Number of children
 */
NMO_API size_t nmo_object_get_child_count(const nmo_object* object);

/**
 * @brief Set object chunk data
 *
 * @param object Object (required)
 * @param chunk Chunk data (can be NULL)
 * @return NMO_OK on success
 */
NMO_API int nmo_object_set_chunk(nmo_object* object, nmo_chunk* chunk);

/**
 * @brief Get object chunk data
 *
 * @param object Object (required)
 * @return Chunk data or NULL if not set
 */
NMO_API nmo_chunk* nmo_object_get_chunk(const nmo_object* object);

/**
 * @brief Set custom data pointer
 *
 * @param object Object (required)
 * @param data Custom data pointer
 * @return NMO_OK on success
 */
NMO_API int nmo_object_set_data(nmo_object* object, void* data);

/**
 * @brief Get custom data pointer
 *
 * @param object Object (required)
 * @return Custom data pointer or NULL
 */
NMO_API void* nmo_object_get_data(const nmo_object* object);

/**
 * @brief Set file index (original file ID)
 *
 * @param object Object (required)
 * @param file_index File ID
 * @return NMO_OK on success
 */
NMO_API int nmo_object_set_file_index(nmo_object* object, nmo_object_id file_index);

/**
 * @brief Get file index
 *
 * @param object Object (required)
 * @return File ID
 */
NMO_API nmo_object_id nmo_object_get_file_index(const nmo_object* object);

#ifdef __cplusplus
}
#endif

#endif // NMO_OBJECT_H
