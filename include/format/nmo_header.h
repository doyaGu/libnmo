/**
 * @file nmo_header.h
 * @brief NMO file header parsing
 */

#ifndef NMO_HEADER_H
#define NMO_HEADER_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "io/nmo_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NMO file header
 */
typedef struct nmo_header nmo_header_t;

/**
 * @brief File header magic and version info
 */
typedef struct nmo_file_header_info {
    char magic[4];        /* NMO file magic */
    uint32_t version;     /* File format version */
    uint32_t flags;       /* File flags */
    uint64_t file_size;   /* Total file size */
    uint32_t header_size; /* Header size in bytes */
} nmo_file_header_info_t;

/**
 * @brief File write mode flags
 */
typedef enum nmo_file_write_mode_flags {
    NMO_FILE_WRITE_COMPRESS_HEADER = 1, /**< Compress header1 */
    NMO_FILE_WRITE_COMPRESS_DATA   = 2, /**< Compress data section */
    NMO_FILE_WRITE_COMPRESS_BOTH   = 3, /**< Compress both header and data */
} nmo_file_write_mode_flags_t;

/**
 * @brief Virtools file header structure
 *
 * This structure represents the header of a Virtools/Nemo file.
 * Part0 (32 bytes) is always present.
 * Part1 (32 bytes) is only present when file_version >= 5.
 */
typedef struct nmo_file_header {
    /* Part0 - 32 bytes (always present) */
    char signature[8];        /**< File signature "Nemo Fi\0" */
    uint32_t crc;             /**< Adler-32 checksum */
    uint32_t ck_version;      /**< Virtools engine version */
    uint32_t file_version;    /**< File format version (2-9, current: 8) */
    uint32_t file_version2;   /**< Legacy field (usually 0) */
    uint32_t file_write_mode; /**< Compression/save flags */
    uint32_t hdr1_pack_size;  /**< Compressed Header1 size */

    /* Part1 - 32 bytes (only when file_version >= 5) */
    uint32_t data_pack_size;   /**< Compressed data size */
    uint32_t data_unpack_size; /**< Uncompressed data size */
    uint32_t manager_count;    /**< Number of managers */
    uint32_t object_count;     /**< Number of objects */
    uint32_t max_id_saved;     /**< Highest object ID */
    uint32_t product_version;  /**< Product version */
    uint32_t product_build;    /**< Product build */
    uint32_t hdr1_unpack_size; /**< Uncompressed Header1 size */
} nmo_file_header_t;

/**
 * Create header context
 * @return Header context or NULL on error
 */
NMO_API nmo_header_t *nmo_header_create(void);

/**
 * Destroy header context
 * @param header Header context
 */
NMO_API void nmo_header_destroy(nmo_header_t *header);

/**
 * Parse header from IO
 * @param header Header context
 * @param io IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header_parse(nmo_header_t *header, void *io);

/**
 * Write header to IO
 * @param header Header context
 * @param io IO context
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header_write(const nmo_header_t *header, void *io);

/**
 * Get header info
 * @param header Header context
 * @param out_info Output header info
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_header_get_info(const nmo_header_t *header, nmo_file_header_info_t *out_info);

/**
 * Get header size
 * @param header Header context
 * @return Header size in bytes
 */
NMO_API uint32_t nmo_header_get_size(const nmo_header_t *header);

/**
 * Validate header
 * @param header Header context
 * @return NMO_OK if valid
 */
NMO_API nmo_result_t nmo_header_validate(const nmo_header_t *header);

/**
 * @brief Parse Virtools file header from IO
 *
 * Reads and parses the Virtools file header from the given IO interface.
 * Part0 (32 bytes) is always read. Part1 (32 bytes) is read if file_version >= 5.
 *
 * @param io IO interface to read from
 * @param header Output header structure
 * @return NMO_OK on success, error code otherwise
 *         NMO_ERR_INVALID_ARGUMENT if io or header is NULL
 *         NMO_ERR_EOF if not enough data to read
 *         NMO_ERR_INVALID_SIGNATURE if signature doesn't match "Nemo Fi\0"
 *         NMO_ERR_UNSUPPORTED_VERSION if file_version < 2 or > 9
 */
NMO_API nmo_result_t nmo_file_header_parse(nmo_io_interface_t *io, nmo_file_header_t *header);

/**
 * @brief Validate Virtools file header
 *
 * Validates the header signature and file version.
 *
 * @param header Header to validate
 * @return NMO_OK if valid, error code otherwise
 *         NMO_ERR_INVALID_ARGUMENT if header is NULL
 *         NMO_ERR_INVALID_SIGNATURE if signature doesn't match "Nemo Fi\0"
 *         NMO_ERR_UNSUPPORTED_VERSION if file_version < 2 or > 9
 */
NMO_API nmo_result_t nmo_file_header_validate(const nmo_file_header_t *header);

/**
 * @brief Serialize Virtools file header to IO
 *
 * Writes the Virtools file header to the given IO interface.
 * Part0 (32 bytes) is always written. Part1 (32 bytes) is written if file_version >= 5.
 *
 * @param header Header structure to write
 * @param io IO interface to write to
 * @return NMO_OK on success, error code otherwise
 *         NMO_ERR_INVALID_ARGUMENT if header or io is NULL
 */
NMO_API nmo_result_t nmo_file_header_serialize(const nmo_file_header_t *header, nmo_io_interface_t *io);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HEADER_H */
