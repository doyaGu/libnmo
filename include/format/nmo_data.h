/**
 * @file nmo_data.h
 * @brief NMO Data section structures and parsing
 *
 * The Data section contains serialized manager and object state data.
 * Format (for file_version >= 6):
 *
 * 1. Manager Data (if manager_count > 0):
 *    For each manager:
 *      - CKGUID (8 bytes: d1, d2)
 *      - data_size (4 bytes int32)
 *      - chunk_data (data_size bytes) - CKStateChunk format
 *
 * 2. Object Data:
 *    For each object:
 *      - [only if version < 7] object_id (4 bytes int32)
 *      - data_size (4 bytes int32)
 *      - chunk_data (data_size bytes) - CKStateChunk format
 *
 * For file_version >= 8, object IDs are stored in Header1, not in Data section.
 */

#ifndef NMO_DATA_H
#define NMO_DATA_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk_pool.h"

#ifdef __cplusplus
extern "C" {

#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;

/**
 * @brief Manager data from file
 *
 * Contains the serialized state of a manager plugin, including its GUID
 * and associated chunk data.
 */
typedef struct nmo_manager_data {
    nmo_guid_t guid;    /**< Manager GUID */
    uint32_t data_size; /**< Size of chunk data in bytes */
    nmo_chunk_t *chunk; /**< Chunk containing manager state (may be NULL if size=0) */
} nmo_manager_data_t;

/**
 * @brief Object data from file
 *
 * Contains the serialized state of an object. The object ID is stored
 * in Header1 for file_version >= 8.
 */
typedef struct nmo_object_data {
    uint32_t data_size; /**< Size of chunk data in bytes */
    nmo_chunk_t *chunk; /**< Chunk containing object state (may be NULL if size=0) */
} nmo_object_data_t;

/**
 * @brief Data section container
 *
 * Contains all manager and object data from the file's Data section.
 */
typedef struct nmo_data_section {
    /* Manager data */
    uint32_t manager_count;     /**< Number of managers */
    nmo_manager_data_t *managers; /**< Array of manager data */

    /* Object data */
    uint32_t object_count;    /**< Number of objects */
    nmo_object_data_t *objects; /**< Array of object data */
} nmo_data_section_t;

/**
 * @brief Parse Data section from buffer
 *
 * Parses the Data section which contains manager and object state chunks.
 * The object_count and manager_count must be set before calling (from file header).
 *
 * @param data Buffer containing data section
 * @param size Size of buffer
 * @param file_version File format version
 * @param data_section Data section structure (object_count and manager_count must be set)
 * @param chunk_pool Optional chunk pool used for chunk allocation (can be NULL)
 * @param arena Arena allocator for temporary data
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_data_section_parse(
    const void *data,
    size_t size,
    uint32_t file_version,
     nmo_data_section_t *data_section,
     nmo_chunk_pool_t *chunk_pool,
    nmo_arena_t *arena);

/**
 * @brief Serialize Data section to buffer
 *
 * @param data_section Data section to serialize
 * @param file_version File format version
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param bytes_written Number of bytes written
 * @param arena Arena for temporary allocations (required for chunk serialization)
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_data_section_serialize(
    const nmo_data_section_t *data_section,
    uint32_t file_version,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written,
    nmo_arena_t *arena);

/**
 * @brief Calculate serialized size of Data section
 *
 * @param data_section Data section
 * @param file_version File format version
 * @return Size in bytes
 */
NMO_API size_t nmo_data_section_calculate_size(
    const nmo_data_section_t *data_section,
    uint32_t file_version,
    nmo_arena_t *arena);

/**
 * @brief Free Data section resources
 *
 * Frees all chunks and arrays in the data section.
 * Does not free the data_section structure itself.
 *
 * @param data_section Data section to free
 */
NMO_API void nmo_data_section_free(nmo_data_section_t *data_section);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DATA_H */
