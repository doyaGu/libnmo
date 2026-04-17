/**
 * @file io_file.c
 * @brief File IO operations implementation
 */

#include "io/nmo_io_file.h"
#include "io/nmo_io.h"
#include "core/nmo_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/**
 * @brief File IO context structure
 */
typedef struct nmo_io_file {
    FILE *fp;
    nmo_io_mode_t mode;
} nmo_io_file_t;

/**
 * @brief File IO handle for nmo_io_interface
 */
typedef struct nmo_file_handle {
    FILE *fp;
    nmo_io_mode_t mode;
} nmo_file_handle_t;

/**
 * @brief Convert nmo_seek_origin to standard SEEK_ constants
 */
static int nmo_seek_to_stdio(nmo_seek_origin_t origin) {
    switch (origin) {
    case NMO_SEEK_SET: return SEEK_SET;
    case NMO_SEEK_CUR: return SEEK_CUR;
    case NMO_SEEK_END: return SEEK_END;
    default: return SEEK_SET;
    }
}

static nmo_status_t nmo_file_seek64(FILE *fp, int64_t offset, int whence) {
    if (fp == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

#if defined(_WIN32)
    if (_fseeki64(fp, offset, whence) != 0) {
        return NMO_ERR_INVALID_OFFSET;
    }
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    if (fseeko(fp, (off_t) offset, whence) != 0) {
        return NMO_ERR_INVALID_OFFSET;
    }
#else
    if (offset > LONG_MAX || offset < LONG_MIN) {
        return NMO_ERR_INVALID_OFFSET;
    }
    if (fseek(fp, (long) offset, whence) != 0) {
        return NMO_ERR_INVALID_OFFSET;
    }
#endif

    return NMO_OK;
}

static int64_t nmo_file_tell64(FILE *fp) {
    if (fp == NULL) {
        return -1;
    }

#if defined(_WIN32)
    __int64 pos = _ftelli64(fp);
    if (pos < 0) {
        return -1;
    }
    return (int64_t) pos;
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    off_t pos = ftello(fp);
    return (int64_t) pos;
#else
    long pos = ftell(fp);
    return (int64_t) pos;
#endif
}

/**
 * @brief Read function for nmo_io_interface
 */
static nmo_status_t file_io_read(void *handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_file_handle_t *fh = (nmo_file_handle_t *) handle;

    if (fh->fp == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    size_t nread = fread(buffer, 1, size, fh->fp);

    if (bytes_read != NULL) {
        *bytes_read = nread;
    }

    if (nread < size) {
        if (feof(fh->fp)) {
            return NMO_OK; // EOF is not an error for partial reads
        }
        if (ferror(fh->fp)) {
            return NMO_ERR_CANT_READ_FILE;
        }
    }

    return NMO_OK;
}

/**
 * @brief Write function for nmo_io_interface
 */
static nmo_status_t file_io_write(void *handle, const void *buffer, size_t size) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_file_handle_t *fh = (nmo_file_handle_t *) handle;

    if (fh->fp == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    size_t nwritten = fwrite(buffer, 1, size, fh->fp);

    if (nwritten < size) {
        return NMO_ERR_CANT_WRITE_FILE;
    }

    return NMO_OK;
}

/**
 * @brief Seek function for nmo_io_interface
 */
static nmo_status_t file_io_seek(void *handle, int64_t offset, nmo_seek_origin_t origin) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_file_handle_t *fh = (nmo_file_handle_t *) handle;

    if (fh->fp == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    int whence = nmo_seek_to_stdio(origin);

    return nmo_file_seek64(fh->fp, offset, whence);
}

/**
 * @brief Tell function for nmo_io_interface
 */
static int64_t file_io_tell(void *handle) {
    if (handle == NULL) {
        return -1;
    }

    nmo_file_handle_t *fh = (nmo_file_handle_t *) handle;

    if (fh->fp == NULL) {
        return -1;
    }

    return nmo_file_tell64(fh->fp);
}

/**
 * @brief Flush function for nmo_io_interface
 */
static nmo_status_t file_io_flush(void *handle) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_file_handle_t *fh = (nmo_file_handle_t *) handle;

    if (fh->fp == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    if (fflush(fh->fp) != 0) {
        return NMO_ERR_CANT_WRITE_FILE;
    }

    return NMO_OK;
}

/**
 * @brief Close function for nmo_io_interface
 */
static nmo_status_t file_io_close(void *handle) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_file_handle_t *fh = (nmo_file_handle_t *) handle;
    int result = NMO_OK;

    if (fh->fp != NULL) {
        if (fclose(fh->fp) != 0) {
            result = NMO_ERR_CANT_WRITE_FILE;
        }
        fh->fp = NULL;
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, fh);

    return result;
}

/**
 * Open a file and return an IO interface
 */
nmo_io_interface_t *nmo_file_io_open(const char *path, nmo_io_mode_t mode) {
    if (path == NULL) {
        return NULL;
    }

    // Determine file mode string
    const char *mode_str = NULL;
    if (mode & NMO_IO_WRITE) {
        if (mode & NMO_IO_CREATE) {
            mode_str = "wb"; // Write binary, create/truncate
        } else {
            mode_str = "r+b"; // Read/write binary, must exist
        }
    } else if (mode & NMO_IO_READ) {
        mode_str = "rb"; // Read binary
    } else {
        return NULL; // Invalid mode
    }

    // Try to open the file
    FILE *fp = fopen(path, mode_str);
    if (fp == NULL) {
        // Return NULL - caller should check errno for specific error
        return NULL;
    }

    // Allocate handle
    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_file_handle_t *fh = (nmo_file_handle_t *) nmo_alloc(&alloc, sizeof(nmo_file_handle_t), sizeof(void *));
    if (fh == NULL) {
        fclose(fp);
        return NULL;
    }

    fh->fp = fp;
    fh->mode = mode;

    // Allocate interface
    nmo_io_interface_t *io = (nmo_io_interface_t *) nmo_alloc(&alloc, sizeof(nmo_io_interface_t), sizeof(void *));
    if (io == NULL) {
        fclose(fp);
        nmo_free(&alloc, fh);
        return NULL;
    }

    io->read = file_io_read;
    io->write = file_io_write;
    io->seek = file_io_seek;
    io->tell = file_io_tell;
    io->flush = file_io_flush;
    io->close = file_io_close;
    io->handle = fh;

    return io;
}

/**
 * Create a file IO context
 */
nmo_io_file_t *nmo_io_file_create(const char *path, const char *mode) {
    if (path == NULL || mode == NULL) {
        return NULL;
    }

    FILE *fp = fopen(path, mode);
    if (fp == NULL) {
        return NULL;
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_io_file_t *io_file = (nmo_io_file_t *) nmo_alloc(&alloc, sizeof(nmo_io_file_t), sizeof(void *));
    if (io_file == NULL) {
        fclose(fp);
        return NULL;
    }

    io_file->fp = fp;
    io_file->mode = 0; // Mode not used in legacy API

    return io_file;
}

/**
 * Destroy file IO context
 */
void nmo_io_file_destroy(nmo_io_file_t *io_file) {
    if (io_file == NULL) {
        return;
    }

    if (io_file->fp != NULL) {
        fclose(io_file->fp);
        io_file->fp = NULL;
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, io_file);
}

/**
 * Read from file
 */
size_t nmo_io_file_read(nmo_io_file_t *io_file, void *buffer, size_t size) {
    if (io_file == NULL || io_file->fp == NULL || buffer == NULL) {
        return 0;
    }

    return fread(buffer, 1, size, io_file->fp);
}

/**
 * Write to file
 */
size_t nmo_io_file_write(nmo_io_file_t *io_file, const void *buffer, size_t size) {
    if (io_file == NULL || io_file->fp == NULL || buffer == NULL) {
        return 0;
    }

    return fwrite(buffer, 1, size, io_file->fp);
}

/**
 * Seek in file
 */
int64_t nmo_io_file_seek(nmo_io_file_t *io_file, int64_t offset, int whence) {
    if (io_file == NULL || io_file->fp == NULL) {
        return -1;
    }

    int result = nmo_file_seek64(io_file->fp, offset, whence);
    if (result != NMO_OK) {
        return -1;
    }
    return nmo_file_tell64(io_file->fp);
}

/**
 * Get current position in file
 */
int64_t nmo_io_file_tell(nmo_io_file_t *io_file) {
    if (io_file == NULL || io_file->fp == NULL) {
        return -1;
    }

    return nmo_file_tell64(io_file->fp);
}

/**
 * Close file
 */
nmo_status_t nmo_io_file_close(nmo_io_file_t *io_file) {
    if (io_file == NULL) {
        NMO_RETURN_OK();
    }

    if (io_file->fp != NULL) {
        fclose(io_file->fp);
        io_file->fp = NULL;
    }

    NMO_RETURN_OK();
}
