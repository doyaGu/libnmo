/**
 * @file nmo_ckattributemanager_schemas.h
 * @brief CKAttributeManager schema definitions
 *
 * CKAttributeManager manages attribute type definitions in Virtools.
 * Attributes are custom properties that can be attached to objects.
 * 
 * Structure:
 * - Categories: Groups of related attributes
 * - Attributes: Individual attribute type definitions
 * 
 * This is a simplified schema focusing on attribute/category metadata.
 * 
 * Based on official Virtools SDK (reference/src/CKAttributeManager.cpp:726-890).
 */

#ifndef NMO_CKATTRIBUTEMANAGER_SCHEMAS_H
#define NMO_CKATTRIBUTEMANAGER_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_result nmo_result_t;

/* =============================================================================
 * ATTRIBUTE STRUCTURES
 * ============================================================================= */

/**
 * @brief Attribute category descriptor
 * 
 * Categories group related attributes together.
 */
typedef struct nmo_ckattribute_category {
    /**
     * @brief Category name
     * 
     * Allocated from arena, null-terminated string.
     */
    const char *name;

    /**
     * @brief Category flags
     * 
     * Controls category behavior (system, user, etc.).
     */
    uint32_t flags;

    /**
     * @brief Whether this category is present
     * 
     * If false, this is an unused slot in the array.
     */
    bool present;
} nmo_ckattribute_category_t;

/**
 * @brief Attribute type descriptor
 * 
 * Defines a single attribute type that can be attached to objects.
 */
typedef struct nmo_ckattribute_descriptor {
    /**
     * @brief Attribute name
     * 
     * Allocated from arena, null-terminated string.
     */
    const char *name;

    /**
     * @brief Parameter type GUID
     * 
     * Defines the data type of this attribute (int, float, string, etc.).
     */
    nmo_guid_t parameter_type_guid;

    /**
     * @brief Category index
     * 
     * Index into the categories array, or -1 if no category.
     */
    int32_t category_index;

    /**
     * @brief Compatible class ID
     * 
     * Restricts this attribute to specific object types.
     * 0 means compatible with all object types.
     */
    int32_t compatible_class_id;

    /**
     * @brief Attribute flags
     * 
     * Controls attribute behavior (system, user, save, etc.).
     */
    uint32_t flags;

    /**
     * @brief Whether this attribute is present
     * 
     * If false, this is an unused slot in the array.
     */
    bool present;
} nmo_ckattribute_descriptor_t;

/**
 * @brief CKAttributeManager state structure
 * 
 * Complete state for CKAttributeManager serialization.
 * 
 * Structure:
 * - Categories define groupings of attributes
 * - Attributes define individual property types
 * - Conversion tables map old indices to new (for file loading)
 * 
 * Reference: reference/src/CKAttributeManager.cpp:726-890
 */
typedef struct nmo_ckattributemanager_state {
    /**
     * @brief Number of categories
     * 
     * Size of the categories array.
     */
    uint32_t category_count;

    /**
     * @brief Category descriptors
     * 
     * Array of category_count categories, allocated from arena.
     */
    nmo_ckattribute_category_t *categories;

    /**
     * @brief Number of attributes
     * 
     * Size of the attributes array.
     */
    uint32_t attribute_count;

    /**
     * @brief Attribute descriptors
     * 
     * Array of attribute_count attributes, allocated from arena.
     */
    nmo_ckattribute_descriptor_t *attributes;
} nmo_ckattributemanager_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief Function pointer for CKAttributeManager deserialization
 * 
 * @param chunk Chunk to read from
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckattributemanager_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckattributemanager_state_t *out_state);

/**
 * @brief Function pointer for CKAttributeManager serialization
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckattributemanager_serialize_fn)(
    nmo_chunk_t *chunk,
    const nmo_ckattributemanager_state_t *state);

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

/**
 * @brief Register CKAttributeManager schema types
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckattributemanager_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get the deserialize function for CKAttributeManager
 * 
 * @return Deserialize function pointer
 */
nmo_ckattributemanager_deserialize_fn nmo_get_ckattributemanager_deserialize(void);

/**
 * @brief Get the serialize function for CKAttributeManager
 * 
 * @return Serialize function pointer
 */
nmo_ckattributemanager_serialize_fn nmo_get_ckattributemanager_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKATTRIBUTEMANAGER_SCHEMAS_H */
