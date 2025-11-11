/**
 * @file nmo_chunk.h
 * @brief CKStateChunk handling
 */

#ifndef NMO_CHUNK_H
#define NMO_CHUNK_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chunk option flags
 *
 * These flags control what optional data is serialized with the chunk.
 */
typedef enum {
    NMO_CHUNK_OPTION_IDS      = 0x01,  /**< Contains object ID references */
    NMO_CHUNK_OPTION_MAN      = 0x02,  /**< Contains manager int refs */
    NMO_CHUNK_OPTION_CHN      = 0x04,  /**< Contains sub-chunks */
    NMO_CHUNK_OPTION_FILE     = 0x08,  /**< Written with file context */
    NMO_CHUNK_OPTION_ALLOWDYN = 0x10,  /**< Allow dynamic objects */
    NMO_CHUNK_OPTION_LISTBIG  = 0x20,  /**< Lists in big endian (unused) */
    NMO_CHUNK_DONTDELETE_PTR  = 0x40,  /**< Data not owned by chunk */
} nmo_chunk_options;

/**
 * @brief CKStateChunk structure
 *
 * This is the fundamental serialization container in Virtools.
 * It stores DWORD-aligned data with optional object IDs, sub-chunks,
 * and manager references.
 */
typedef struct nmo_chunk {
    /* Identity */
    nmo_class_id       class_id;         /**< Object class ID */
    uint32_t          data_version;     /**< Custom version per class */
    uint32_t          chunk_version;    /**< Chunk format version (7) */
    uint8_t           chunk_class_id;   /**< Legacy class ID (8-bit) */
    uint32_t          chunk_options;    /**< Option flags */

    /* Data buffer (DWORD-aligned) */
    uint32_t*         data;             /**< Payload buffer */
    size_t            data_size;        /**< Size in DWORDs (not bytes!) */
    size_t            data_capacity;    /**< Capacity in DWORDs */

    /* Optional tracking lists */
    uint32_t*         ids;              /**< Object ID list */
    size_t            id_count;
    size_t            id_capacity;

    struct nmo_chunk** chunks;          /**< Sub-chunk list */
    size_t            chunk_count;
    size_t            chunk_capacity;

    uint32_t*         managers;         /**< Manager int list */
    size_t            manager_count;
    size_t            manager_capacity;

    /* Compression info (for statistics) */
    size_t            uncompressed_size;
    size_t            compressed_size;
    int               is_compressed;

    /* Memory management */
    nmo_arena*        arena;            /**< Arena for allocations */
    int               owns_data;        /**< Whether to free data */
} nmo_chunk;

/**
 * @brief Legacy typedef for compatibility
 */
typedef struct nmo_chunk nmo_chunk_t;

/**
 * @brief Chunk header info
 */
typedef struct {
    uint32_t chunk_id;       /* Chunk identifier */
    uint32_t chunk_size;     /* Chunk size */
    uint32_t sub_chunk_count;/* Number of sub-chunks */
    uint32_t flags;          /* Chunk flags */
} nmo_chunk_header_t;

/**
 * @brief Create empty chunk
 *
 * Creates a new chunk allocated from the given arena.
 * All fields are initialized to 0/NULL except:
 * - chunk_version is set to NMO_CHUNK_VERSION_4 (7)
 * - owns_data is set to 1
 *
 * @param arena Arena for allocations (required)
 * @return New chunk or NULL on allocation failure
 */
NMO_API nmo_chunk* nmo_chunk_create(nmo_arena* arena);

/**
 * @brief Serialize chunk to binary format
 *
 * Serializes the chunk according to the Virtools binary format:
 * - [4 bytes] Version Info (packed)
 * - [4 bytes] Chunk Size (in DWORDs)
 * - [Size*4 bytes] Data buffer
 * - [Optional] IDs List if OPTION_IDS set
 * - [Optional] Chunks List if OPTION_CHN set
 * - [Optional] Managers List if OPTION_MAN set
 *
 * @param chunk Chunk to serialize (required)
 * @param out_data Output buffer pointer (required)
 * @param out_size Output size in bytes (required)
 * @param arena Arena for output buffer allocation (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result nmo_chunk_serialize(const nmo_chunk* chunk,
                                        void** out_data,
                                        size_t* out_size,
                                        nmo_arena* arena);

/**
 * @brief Deserialize chunk from binary format
 *
 * Parses a chunk from binary data according to the Virtools format.
 * All memory is allocated from the given arena.
 *
 * @param data Input binary data (required)
 * @param size Input data size in bytes (required)
 * @param arena Arena for allocations (required)
 * @param out_chunk Output chunk pointer (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_result nmo_chunk_deserialize(const void* data,
                                          size_t size,
                                          nmo_arena* arena,
                                          nmo_chunk** out_chunk);

/**
 * @brief Destroy chunk
 *
 * Since chunks use arena allocation, this is mostly a no-op.
 * The arena itself handles cleanup.
 *
 * @param chunk Chunk to destroy
 */
NMO_API void nmo_chunk_destroy(nmo_chunk* chunk);

/* Forward declaration for ID remap table */
typedef struct nmo_id_remap nmo_id_remap_t;
typedef struct nmo_id_remap_table nmo_id_remap_table;

/**
 * @brief Remap object IDs in chunk
 *
 * Applies ID remapping to all object IDs stored in the chunk's data buffer
 * and ID tracking list. This is used during file load to convert file IDs
 * to runtime IDs.
 *
 * @param chunk Chunk to remap (required)
 * @param remap_table ID remap table (required)
 * @return NMO_OK on success, error code on failure
 */
NMO_API int nmo_chunk_remap_ids(nmo_chunk* chunk, nmo_id_remap_table* remap_table);

/* Legacy API for compatibility */

/**
 * Create chunk (legacy)
 * @param chunk_id Chunk ID
 * @return Chunk or NULL on error
 */
NMO_API nmo_chunk_t* nmo_chunk_create_legacy(uint32_t chunk_id);

/**
 * Destroy chunk (legacy)
 * @param chunk Chunk to destroy
 */
NMO_API void nmo_chunk_destroy_legacy(nmo_chunk_t* chunk);

/**
 * Parse chunk from data
 * @param chunk Chunk
 * @param data Data buffer
 * @param size Data size
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_parse(nmo_chunk_t* chunk, const void* data, size_t size);

/**
 * Write chunk to data
 * @param chunk Chunk
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written or negative on error
 */
NMO_API ssize_t nmo_chunk_write(const nmo_chunk_t* chunk, void* buffer, size_t size);

/**
 * Get chunk header
 * @param chunk Chunk
 * @param out_header Output header
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_get_header(const nmo_chunk_t* chunk, nmo_chunk_header_t* out_header);

/**
 * Get chunk ID
 * @param chunk Chunk
 * @return Chunk ID
 */
NMO_API uint32_t nmo_chunk_get_id(const nmo_chunk_t* chunk);

/**
 * Get chunk size
 * @param chunk Chunk
 * @return Chunk size in bytes
 */
NMO_API uint32_t nmo_chunk_get_size(const nmo_chunk_t* chunk);

/**
 * Get chunk data
 * @param chunk Chunk
 * @param out_size Output data size
 * @return Pointer to chunk data
 */
NMO_API const void* nmo_chunk_get_data(const nmo_chunk_t* chunk, size_t* out_size);

/**
 * Add sub-chunk
 * @param chunk Parent chunk
 * @param sub_chunk Sub-chunk to add
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_chunk_add_sub_chunk(nmo_chunk_t* chunk, nmo_chunk_t* sub_chunk);

/**
 * Get sub-chunk count
 * @param chunk Chunk
 * @return Number of sub-chunks
 */
NMO_API uint32_t nmo_chunk_get_sub_chunk_count(const nmo_chunk_t* chunk);

/**
 * Get sub-chunk by index
 * @param chunk Chunk
 * @param index Sub-chunk index
 * @return Sub-chunk or NULL
 */
NMO_API nmo_chunk_t* nmo_chunk_get_sub_chunk(const nmo_chunk_t* chunk, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CHUNK_H */
