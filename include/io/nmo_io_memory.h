/**
 * @file nmo_io_memory.h
 * @brief Memory IO operations
 */

#ifndef NMO_IO_MEMORY_H
#define NMO_IO_MEMORY_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "io/nmo_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open a read-only memory buffer and return an IO interface
 * @param data Pointer to data buffer
 * @param size Size of buffer
 * @return IO interface or NULL on error
 * @ownership owned
 */
NMO_API nmo_io_interface_t *nmo_memory_io_open_read(const void *data, size_t size);

/**
 * Open a write-only memory buffer with dynamic growth and return an IO interface
 * @param initial_capacity Initial buffer capacity
 * @return IO interface or NULL on error
 * @ownership owned
 */
NMO_API nmo_io_interface_t *nmo_memory_io_open_write(size_t initial_capacity);

/**
 * Get the data from a write memory IO interface
 * @param io IO interface (must be from nmo_memory_io_open_write)
 * @param size Output parameter for data size
 * @return Pointer to data buffer or NULL on error
 * @ownership borrowed
 */
NMO_API const void *nmo_memory_io_get_data(nmo_io_interface_t *io, size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_MEMORY_H */
