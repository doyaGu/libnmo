/**
 * @file nmo_io_file.h
 * @brief File IO operations
 */

#ifndef NMO_IO_FILE_H
#define NMO_IO_FILE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "io/nmo_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open a file and return an IO interface
 * @param path File path
 * @param mode IO mode (NMO_IO_READ, NMO_IO_WRITE, NMO_IO_CREATE)
 * @return IO interface or NULL on error
 * @ownership owned
 */
NMO_API nmo_io_interface_t *nmo_file_io_open(const char *path, nmo_io_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_FILE_H */
