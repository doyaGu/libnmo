#ifndef NMO_OBJECT_H
#define NMO_OBJECT_H

#include "nmo_types.h"
#include "nmo_chunk.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "core/nmo_allocator.h"

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

/* Forward declarations */
typedef struct nmo_object nmo_object_t;
typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief Object structure
 *
 * Runtime representation of a Virtools object with metadata, hierarchical
 * relationships, and chunk data.
 */
typedef struct nmo_object {
    /* Identity */
    nmo_object_id_t id;      /**< Runtime object ID */
    nmo_class_id_t class_id; /**< Object class ID */
    const char *name;      /**< Object name (optional) */
    uint32_t flags;        /**< Object flags */
    nmo_guid_t type_guid;  /**< Type GUID (for typed objects like parameters) */

    /* Hierarchy */
    nmo_object_t *parent;    /**< Parent object (NULL for root) */
    nmo_object_t **children; /**< Child objects array */
    size_t child_count;      /**< Number of children */
    size_t child_capacity;   /**< Children array capacity */

    /* Data */
    nmo_chunk_t *chunk; /**< Associated chunk data (non-owning reference) */

    /* State (ECS-style) - computed offsets within contain all inherited states */
    void *state;            /**< Combined state buffer holding all inherited states */
    uint32_t state_size;    /**< Total size of combined state buffer */

    /* File context */
    nmo_object_id_t file_index; /**< FileIndex offset in uncompressed file buffer (Header1) */
    nmo_object_id_t file_id;    /**< Object ID stored in file (Header1 Object) */
    uint32_t creation_flags;  /**< Flags used during creation */
    uint32_t save_flags;      /**< Flags for saving */

    /* Memory management */
    nmo_allocator_t allocator; /**< Allocator snapshot for explicit frees */
    nmo_arena_t *storage_arena; /**< Per-object arena for schema allocations */
} nmo_object_t;

/**
 * @brief Create object
 *
 * @param allocator Allocator for object-owned allocations (NULL for default)
 * @param id Runtime object ID
 * @param class_id Object class ID
 * @return Object or NULL on allocation failure
 * @ownership owned
 */
NMO_API nmo_object_t *nmo_object_create(const nmo_allocator_t *allocator,
                                        nmo_object_id_t id,
                                        nmo_class_id_t class_id);

/**
 * @brief Destroy object
 *
 * Releases any owned allocations and destroys the per-object arena.
 *
 * @param object Object to destroy
 */
NMO_API void nmo_object_destroy(nmo_object_t *object);

/**
 * @brief Set object name
 *
 * @param object Object (required)
 * @param name Name string (will be copied, can be NULL)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_set_name(nmo_object_t *object, const char *name);

/**
 * @brief Get object name
 *
 * @param object Object (required)
 * @return Name string or NULL if not set
 * @ownership borrowed
 */
NMO_API const char *nmo_object_get_name(const nmo_object_t *object);

/**
 * @brief Get object ID
 *
 * Convenience accessor for object ID.
 *
 * @param object Object (required)
 * @return Object ID
 */
NMO_API nmo_object_id_t nmo_object_get_id(const nmo_object_t *object);

/**
 * @brief Get object class ID
 *
 * Convenience accessor for class ID.
 *
 * @param object Object (required)
 * @return Class ID
 */
NMO_API nmo_class_id_t nmo_object_get_class_id(const nmo_object_t *object);

/**
 * @brief Get object flags
 *
 * @param object Object (required)
 * @return Object flags
 */
NMO_API uint32_t nmo_object_get_flags(const nmo_object_t *object);

/**
 * @brief Get parent object
 *
 * @param object Object (required)
 * @return Parent object or NULL if root
 * @ownership borrowed
 */
NMO_API nmo_object_t *nmo_object_get_parent(const nmo_object_t *object);

/**
 * @brief Add child object
 *
 * @param parent Parent object (required)
 * @param child Child object (required)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_add_child(nmo_object_t *parent, nmo_object_t *child);

/**
 * @brief Get per-object storage arena
 *
 * The returned arena is owned by the object and destroyed in nmo_object_destroy().
 * @ownership borrowed
 */
NMO_API nmo_arena_t *nmo_object_get_storage_arena(const nmo_object_t *object);

/**
 * @brief Remove child object
 *
 * @param parent Parent object (required)
 * @param child Child object (required)
 * @return NMO_OK on success, NMO_ERR_INVALID_ARGUMENT if not found
 */
NMO_API nmo_status_t nmo_object_remove_child(nmo_object_t *parent, nmo_object_t *child);

/**
 * @brief Get child by index
 *
 * @param object Parent object (required)
 * @param index Child index
 * @return Child object or NULL if index out of bounds
 * @ownership borrowed
 */
NMO_API nmo_object_t *nmo_object_get_child(const nmo_object_t *object, size_t index);

/**
 * @brief Get child count
 *
 * @param object Object (required)
 * @return Number of children
 */
NMO_API size_t nmo_object_get_child_count(const nmo_object_t *object);

/**
 * @brief Set object chunk data
 *
 * @note This is a non-owning reference. The object does not take ownership
 * of the chunk. The chunk must remain valid for the lifetime of the object
 * or be explicitly cleared before destruction.
 *
 * @param object Object (required)
 * @param chunk Chunk data (can be NULL)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_set_chunk(nmo_object_t *object, nmo_chunk_t *chunk);

/**
 * @brief Get object chunk data
 *
 * @note This returns a non-owning reference. The chunk is owned by its arena
 * and will be destroyed when the arena is destroyed.
 *
 * @param object Object (required)
 * @return Chunk data or NULL if not set
 * @ownership borrowed
 */
NMO_API nmo_chunk_t *nmo_object_get_chunk(const nmo_object_t *object);

/**
 * @brief Set FileIndex offset (Header1)
 *
 * @param object Object (required)
 * @param file_index FileIndex offset in uncompressed file buffer
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_set_file_index(
    nmo_object_t *object,
    nmo_object_id_t file_index);

/**
 * @brief Get FileIndex offset (Header1)
 *
 * @param object Object (required)
 * @return FileIndex offset in uncompressed file buffer
 */
NMO_API nmo_object_id_t nmo_object_get_file_index(const nmo_object_t *object);

/**
 * @brief Get object type GUID
 *
 * For typed objects (like parameters), returns the type GUID.
 * For other objects, returns a null GUID.
 *
 * @param object Object (required)
 * @return Type GUID (call nmo_guid_is_null to check validity)
 */
NMO_API nmo_guid_t nmo_object_get_type_guid(const nmo_object_t *object);

/**
 * @brief Set object type GUID
 *
 * Low-level setter for typed objects. For objects already owned by a repository,
 * use nmo_object_repository_set_type_guid() so retained indexes and query
 * observers are updated.
 *
 * @param object Object (required)
 * @param guid Type GUID
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_set_type_guid(nmo_object_t *object, nmo_guid_t guid);

/* ============================================================================
 * State Access (ECS-style combined state)
 * ============================================================================ */

/**
 * @brief Allocate combined state buffer
 *
 * Allocates a combined state buffer to hold all inherited states.
 * This should be called once after object creation, based on the
 * type's total_state_size from the type registry.
 *
 * @param object Object (required)
 * @param size Total size in bytes (from type descriptor's total_state_size)
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_object_alloc_state(nmo_object_t *object, uint32_t size);

/**
 * @brief Get combined state buffer
 *
 * Returns pointer to the combined state buffer.
 *
 * @param object Object (required)
 * @return State buffer or NULL if not allocated
 * @ownership borrowed
 */
NMO_API void *nmo_object_get_state(const nmo_object_t *object);

/**
 * @brief Get state size
 *
 * @param object Object (required)
 * @return Size of state buffer in bytes, or 0 if not allocated
 */
NMO_API uint32_t nmo_object_get_state_size(const nmo_object_t *object);

/**
 * @brief Get state for a specific ancestor type
 *
 * Returns pointer to a specific ancestor's state within the combined buffer.
 * Uses the type descriptor's state_offsets to find the correct offset.
 *
 * @param object Object (required)
 * @param type_desc Type descriptor of the ancestor type
 * @param derived_type_desc Type descriptor of the object's actual type (for offset lookup)
 * @return Pointer to ancestor's state, or NULL if not found
 * @ownership borrowed
 */
NMO_API void *nmo_object_get_ancestor_state(
    const nmo_object_t *object,
    const nmo_type_descriptor_t *type_desc,
    const nmo_type_descriptor_t *derived_type_desc);

#ifdef __cplusplus
}
#endif

#endif // NMO_OBJECT_H
