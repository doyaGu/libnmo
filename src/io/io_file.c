/**
 * @file io_file.c
 * @brief File IO operations implementation
 */

#include "io/nmo_io_file.h"
#include <stdlib.h>

/**
 * Create a file IO context
 */
nmo_io_file_t* nmo_io_file_create(const char* path, const char* mode) {
    (void)path;
    (void)mode;
    return NULL;
}

/**
 * Destroy file IO context
 */
void nmo_io_file_destroy(nmo_io_file_t* io_file) {
    (void)io_file;
}

/**
 * Read from file
 */
size_t nmo_io_file_read(nmo_io_file_t* io_file, void* buffer, size_t size) {
    (void)io_file;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Write to file
 */
size_t nmo_io_file_write(nmo_io_file_t* io_file, const void* buffer, size_t size) {
    (void)io_file;
    (void)buffer;
    (void)size;
    return 0;
}

/**
 * Seek in file
 */
int64_t nmo_io_file_seek(nmo_io_file_t* io_file, int64_t offset, int whence) {
    (void)io_file;
    (void)offset;
    (void)whence;
    return -1;
}

/**
 * Get current position in file
 */
int64_t nmo_io_file_tell(nmo_io_file_t* io_file) {
    (void)io_file;
    return -1;
}

/**
 * Close file
 */
nmo_result_t nmo_io_file_close(nmo_io_file_t* io_file) {
    (void)io_file;
    return nmo_result_ok();
}
