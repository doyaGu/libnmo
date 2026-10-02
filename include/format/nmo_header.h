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
 * @ownership owned
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
NMO_API nmo_status_t nmo_header_parse(nmo_header_t *header, void *io);

/**
 * Write header to IO
 * @param header Header context
 * @param io IO context
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_header_write(const nmo_header_t *header, void *io);

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
NMO_API nmo_status_t nmo_header_validate(const nmo_header_t *header);

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
 *         NMO_ERR_TRUNCATED_CHUNK if not enough data to read
 *         NMO_ERR_INVALID_SIGNATURE if signature doesn't match "Nemo Fi\0"
 *         NMO_ERR_UNSUPPORTED_VERSION if file_version < 2 or > 9
 */
NMO_API nmo_status_t nmo_file_header_parse(nmo_io_interface_t *io, nmo_file_header_t *header);

/**
 * @brief Validate Virtools file header
 *
 * Validates the header signature and file version.
 *
 * @param header Header to validate
 * @return NMO_OK if valid, error code otherwise
 *         NMO_ERR_INVALID_ARGUMENT if header is NULL
 *         NMO_ERR_INVALID_SIGNATURE if signature doesn't match "Nemo Fi\0"
 *         NMO_ERR_UNSUPPORTED_VERSION if file_version < 2 or > 9, or
 *         FileVersion2 is not 0
 *
 * This is the check a load makes. Versions 2 to 6 have no Header1 object
 * table: CKFile::ReadFileData reads their object ids from the data section and
 * their class ids and names from the object chunks, and the deserializer does
 * the same.
 */
NMO_API nmo_status_t nmo_file_header_validate(const nmo_file_header_t *header);

/**
 * @brief Compute the CK2-compatible file Adler-32 checksum
 *
 * CK2 computes the checksum over Part0 with the crc field set to 0,
 * Part1, the packed Header1 section, and the packed Data section.
 *
 * @param header Header structure used for Part0 and Part1
 * @param header1_packed Packed Header1 section bytes
 * @param header1_pack_size Packed Header1 section size
 * @param data_packed Packed Data section bytes
 * @param data_pack_size Packed Data section size
 * @return Adler-32 checksum, or 0 if arguments are invalid
 */
NMO_API uint32_t nmo_file_header_compute_crc(const nmo_file_header_t *header,
                                             const uint8_t *header1_packed,
                                             uint32_t header1_pack_size,
                                             const uint8_t *data_packed,
                                             uint32_t data_pack_size);

/**
 * @brief Adler-32 of a block of data as CKComputeDataCRC computes it
 *
 * CKComputeDataCRC(buffer, size, 0) is zlib's adler32 started from 0 rather
 * than from 1, which is what mz_adler32(0, ...) does.
 *
 * @param data Bytes to checksum
 * @param size Number of bytes
 * @return Checksum (0 for an empty block)
 */
NMO_API uint32_t nmo_file_data_crc(const uint8_t *data, size_t size);

/**
 * @brief Compute the file checksum a given file version stores
 *
 * CKFile::ReadFileHeaders checks, for file version 8 and later, the
 * checksum of nmo_file_header_compute_crc (header parts, packed Header1,
 * packed data). CKFile::ReadFileData checks, for the versions below 8 (and not
 * below 2), the checksum of the unpacked data section instead:
 * CKComputeDataCRC(data, size, 0), stored in the same header field. The
 * Header1 and the header are not part of it. The data the engine sums is its
 * buffer from the start of the data section: with a compressed section
 * (file_write_mode & 9) that is exactly the unpacked section, with an
 * uncompressed one it runs to the end of the file, so bytes after the
 * section (included files) are summed too and must be in @p data_unpacked.
 * Versions below 2 are not checked, and 0 is returned for them.
 *
 * @param header Header (file_version selects the algorithm)
 * @param header1_packed Packed Header1 bytes (file version 8 and later)
 * @param header1_pack_size Packed Header1 size
 * @param data_packed Packed data bytes (file version 8 and later)
 * @param data_pack_size Packed data size
 * @param data_unpacked Unpacked data bytes (file versions below 8)
 * @param data_unpack_size Unpacked data size
 * @return Checksum, or 0 if arguments are invalid
 */
NMO_API uint32_t nmo_file_crc_for_version(const nmo_file_header_t *header,
                                          const uint8_t *header1_packed,
                                          uint32_t header1_pack_size,
                                          const uint8_t *data_packed,
                                          uint32_t data_pack_size,
                                          const uint8_t *data_unpacked,
                                          size_t data_unpack_size);

/**
 * @brief Check header->crc the way the engine does for the file version
 *
 * @return NMO_OK when the checksum matches (or the version is not checked),
 *         NMO_ERR_CHECKSUM_MISMATCH when it does not,
 *         NMO_ERR_INVALID_ARGUMENT for a NULL header or missing section bytes
 */
NMO_API nmo_status_t nmo_file_header_verify_crc(const nmo_file_header_t *header,
                                                const uint8_t *header1_packed,
                                                uint32_t header1_pack_size,
                                                const uint8_t *data_packed,
                                                uint32_t data_pack_size,
                                                const uint8_t *data_unpacked,
                                                size_t data_unpack_size);

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
NMO_API nmo_status_t nmo_file_header_serialize(const nmo_file_header_t *header, nmo_io_interface_t *io);

#ifdef __cplusplus
}
#endif

#endif /* NMO_HEADER_H */
