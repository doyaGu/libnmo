/**
 * @file nmo_io_checksum.h
 * @brief Checksum IO wrapper for composable IO operations
 */

#ifndef NMO_IO_CHECKSUM_H
#define NMO_IO_CHECKSUM_H

#include "nmo_types.h"
#include "io/nmo_io.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Checksum algorithm types
 */
typedef enum {
    NMO_CHECKSUM_ADLER32 = 0,  /**< Adler-32 checksum */
    NMO_CHECKSUM_CRC32   = 1,  /**< CRC-32 checksum */
} nmo_checksum_algorithm;

/**
 * @brief Checksummed IO descriptor
 *
 * Configures the checksum parameters for wrapping an IO interface.
 */
typedef struct {
    nmo_checksum_algorithm algorithm;  /**< Checksum algorithm to use */
    uint32_t initial_value;            /**< Initial checksum value (typically 0 for Adler-32, 0 for CRC32) */
} nmo_checksummed_io_desc;

/**
 * @brief Wrap an IO interface with checksumming
 *
 * Creates a new IO interface that computes a checksum of all data that
 * flows through it (both reads and writes), wrapping the provided inner
 * IO interface.
 *
 * The wrapper passes through seek/tell/close operations to the underlying IO.
 * The checksum accumulates across all data that flows through.
 *
 * @param inner The inner IO interface to wrap (must not be NULL)
 * @param desc Checksum descriptor (must not be NULL)
 * @return New wrapped IO interface, or NULL on error
 *
 * @note The caller is responsible for closing the returned interface.
 *       Closing will also close the inner interface.
 * @note The inner interface should not be used directly after wrapping.
 * @note Use nmo_checksummed_io_get_checksum() to retrieve the computed checksum.
 */
NMO_API nmo_io_interface* nmo_checksummed_io_wrap(nmo_io_interface* inner,
                                                    const nmo_checksummed_io_desc* desc);

/**
 * @brief Get the computed checksum value
 *
 * Retrieves the current checksum value computed from all data that has
 * flowed through the checksummed IO interface.
 *
 * @param io The checksummed IO interface (must not be NULL)
 * @return The computed checksum value, or 0 if the interface is not a checksummed IO
 *
 * @note This can be called at any time to get the current checksum state.
 * @note The checksum continues to update with subsequent read/write operations.
 */
NMO_API uint32_t nmo_checksummed_io_get_checksum(nmo_io_interface* io);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_CHECKSUM_H */
