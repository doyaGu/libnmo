/**
 * @file nmo_header1.h
 * @brief NMO Header1 (object descriptors and plugin dependencies)
 */

#ifndef NMO_HEADER1_H
#define NMO_HEADER1_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Object descriptor (from file)
 *
 * Represents an object stored in the NMO file with its metadata.
 * File IDs can have bit 23 (0x800000) set to indicate reference-only objects.
 */
typedef struct nmo_object_desc {
    nmo_object_id_t file_id;    /**< Object ID from file, bit 23 may be set for reference-only */
    nmo_class_id_t class_id;    /**< Class ID (oit->ObjectCid) */
    nmo_object_id_t file_index; /**< File index (oit->FileIndex) */
    char *name;               /**< Object name (allocated from arena) */
    uint32_t flags;           /**< Object flags (bit 23 = reference-only) */
} nmo_object_desc_t;

/**
 * @brief Plugin dependency
 *
 * Represents a required plugin with its GUID, category, and version.
 */
typedef struct nmo_plugin_dep {
    nmo_guid_t guid;   /**< Plugin GUID */
    uint32_t category; /**< Plugin category (behavior, manager, render, sound, input) */
    uint32_t version;  /**< Plugin version */
} nmo_plugin_dep_t;

/**
 * @brief Header1 structure
 *
 * Contains object descriptors, plugin dependencies, and included files metadata.
 * This section is present in file version 7+ and may be compressed.
 */
typedef struct nmo_header1 {
    /* Object table (version 7+) */
    uint32_t object_count;
    nmo_object_desc_t *objects; /**< Array allocated from arena */

    /* Plugin dependencies (version 8+) */
    uint32_t plugin_dep_count;
    nmo_plugin_dep_t *plugin_deps; /**< Array allocated from arena */

    /* Included files (stub, always 0) */
    uint32_t included_file_count;
    char **included_files; /**< Always NULL */
} nmo_header1_t;

/**
 * @brief Parse Header1 from buffer
 *
 * Parses object descriptors, plugin dependencies, and included files stub
 * from a compressed or decompressed Header1 buffer.
 *
 * @param data Buffer containing Header1 data
 * @param size Size of buffer in bytes
 * @param header Output Header1 structure
 * @param arena Arena allocator for memory allocations
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_header1_parse(
    const void *data,
    size_t size,
    nmo_header1_t *header,
    nmo_arena_t *arena);

/**
 * @brief Serialize Header1 to buffer
 *
 * Serializes object descriptors, plugin dependencies, and included files stub
 * to a buffer. The buffer is allocated from the arena.
 *
 * @param header Header1 structure to serialize
 * @param out_data Output buffer pointer (allocated from arena)
 * @param out_size Output buffer size
 * @param arena Arena allocator for memory allocations
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_header1_serialize(
    const nmo_header1_t *header,
    void **out_data,
    size_t *out_size,
    nmo_arena_t *arena);

/**
 * @brief Free Header1 resources
 *
 * If arena allocation was used, this is typically a no-op.
 * Otherwise, frees dynamically allocated strings.
 *
 * @param header Header1 structure to free
 */
NMO_API void nmo_header1_free(nmo_header1_t *header);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HEADER1_H */
