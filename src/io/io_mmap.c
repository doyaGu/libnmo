/**
 * @file io_mmap.c
 * @brief Memory-mapped file IO implementation (Phase 2.1)
 *
 * Cross-platform implementation of memory-mapped file access:
 * - Windows: CreateFileMapping/MapViewOfFile
 * - POSIX: mmap(2)
 */

#include "io/nmo_io_mmap.h"
#include "io/nmo_io.h"
#include "core/nmo_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/**
 * @brief Memory-mapped file context structure
 */
struct nmo_io_mmap {
#ifdef _WIN32
    HANDLE file_handle;
    HANDLE mapping_handle;
#else
    int fd;
#endif
    void *data;
    size_t size;
    size_t position;
};

/* ============================================================================
 * Platform-specific Implementation
 * ============================================================================ */

#ifdef _WIN32

nmo_io_mmap_t *nmo_io_mmap_open(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    
    nmo_io_mmap_t *mmap_ctx = (nmo_io_mmap_t *)calloc(1, sizeof(nmo_io_mmap_t));
    if (mmap_ctx == NULL) {
        return NULL;
    }
    
    mmap_ctx->file_handle = INVALID_HANDLE_VALUE;
    mmap_ctx->mapping_handle = NULL;
    mmap_ctx->data = NULL;
    
    /* Open file for reading */
    mmap_ctx->file_handle = CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (mmap_ctx->file_handle == INVALID_HANDLE_VALUE) {
        free(mmap_ctx);
        return NULL;
    }
    
    /* Get file size */
    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(mmap_ctx->file_handle, &file_size)) {
        CloseHandle(mmap_ctx->file_handle);
        free(mmap_ctx);
        return NULL;
    }
    
    mmap_ctx->size = (size_t)file_size.QuadPart;
    
    /* Handle empty files */
    if (mmap_ctx->size == 0) {
        mmap_ctx->data = NULL;
        mmap_ctx->position = 0;
        return mmap_ctx;
    }
    
    /* Create file mapping */
    mmap_ctx->mapping_handle = CreateFileMappingA(
        mmap_ctx->file_handle,
        NULL,
        PAGE_READONLY,
        0,
        0,
        NULL
    );
    
    if (mmap_ctx->mapping_handle == NULL) {
        CloseHandle(mmap_ctx->file_handle);
        free(mmap_ctx);
        return NULL;
    }
    
    /* Map view of file */
    mmap_ctx->data = MapViewOfFile(
        mmap_ctx->mapping_handle,
        FILE_MAP_READ,
        0,
        0,
        0
    );
    
    if (mmap_ctx->data == NULL) {
        CloseHandle(mmap_ctx->mapping_handle);
        CloseHandle(mmap_ctx->file_handle);
        free(mmap_ctx);
        return NULL;
    }
    
    mmap_ctx->position = 0;
    return mmap_ctx;
}

void nmo_io_mmap_close(nmo_io_mmap_t *mmap_ctx) {
    if (mmap_ctx == NULL) {
        return;
    }
    
    if (mmap_ctx->data != NULL) {
        UnmapViewOfFile(mmap_ctx->data);
    }
    
    if (mmap_ctx->mapping_handle != NULL) {
        CloseHandle(mmap_ctx->mapping_handle);
    }
    
    if (mmap_ctx->file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(mmap_ctx->file_handle);
    }
    
    free(mmap_ctx);
}

int nmo_io_mmap_supported(void) {
    return 1;  /* Windows always supports memory mapping */
}

#else /* POSIX */

nmo_io_mmap_t *nmo_io_mmap_open(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    
    nmo_io_mmap_t *mmap_ctx = (nmo_io_mmap_t *)calloc(1, sizeof(nmo_io_mmap_t));
    if (mmap_ctx == NULL) {
        return NULL;
    }
    
    mmap_ctx->fd = -1;
    mmap_ctx->data = MAP_FAILED;
    
    /* Open file for reading */
    mmap_ctx->fd = open(path, O_RDONLY);
    if (mmap_ctx->fd < 0) {
        free(mmap_ctx);
        return NULL;
    }
    
    /* Get file size */
    struct stat st;
    if (fstat(mmap_ctx->fd, &st) != 0) {
        close(mmap_ctx->fd);
        free(mmap_ctx);
        return NULL;
    }
    
    mmap_ctx->size = (size_t)st.st_size;
    
    /* Handle empty files */
    if (mmap_ctx->size == 0) {
        mmap_ctx->data = NULL;
        mmap_ctx->position = 0;
        return mmap_ctx;
    }
    
    /* Memory map the file */
    mmap_ctx->data = mmap(NULL, mmap_ctx->size, PROT_READ, MAP_PRIVATE, mmap_ctx->fd, 0);
    if (mmap_ctx->data == MAP_FAILED) {
        close(mmap_ctx->fd);
        free(mmap_ctx);
        return NULL;
    }
    
    mmap_ctx->position = 0;
    return mmap_ctx;
}

void nmo_io_mmap_close(nmo_io_mmap_t *mmap_ctx) {
    if (mmap_ctx == NULL) {
        return;
    }
    
    if (mmap_ctx->data != NULL && mmap_ctx->data != MAP_FAILED && mmap_ctx->size > 0) {
        munmap(mmap_ctx->data, mmap_ctx->size);
    }
    
    if (mmap_ctx->fd >= 0) {
        close(mmap_ctx->fd);
    }
    
    free(mmap_ctx);
}

int nmo_io_mmap_supported(void) {
    return 1;  /* POSIX systems support mmap */
}

#endif /* _WIN32 */

/* ============================================================================
 * Platform-independent Implementation
 * ============================================================================ */

const void *nmo_io_mmap_data(const nmo_io_mmap_t *mmap_ctx) {
    if (mmap_ctx == NULL) {
        return NULL;
    }
    return mmap_ctx->data;
}

size_t nmo_io_mmap_size(const nmo_io_mmap_t *mmap_ctx) {
    if (mmap_ctx == NULL) {
        return 0;
    }
    return mmap_ctx->size;
}

size_t nmo_io_mmap_read(nmo_io_mmap_t *mmap_ctx, void *buffer, size_t size) {
    if (mmap_ctx == NULL || buffer == NULL || mmap_ctx->data == NULL) {
        return 0;
    }
    
    /* Calculate available bytes */
    size_t available = mmap_ctx->size - mmap_ctx->position;
    size_t to_read = (size < available) ? size : available;
    
    if (to_read > 0) {
        memcpy(buffer, (const char *)mmap_ctx->data + mmap_ctx->position, to_read);
        mmap_ctx->position += to_read;
    }
    
    return to_read;
}

int64_t nmo_io_mmap_seek(nmo_io_mmap_t *mmap_ctx, int64_t offset, int whence) {
    if (mmap_ctx == NULL) {
        return -1;
    }
    
    int64_t new_pos;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (int64_t)mmap_ctx->position + offset;
            break;
        case SEEK_END:
            new_pos = (int64_t)mmap_ctx->size + offset;
            break;
        default:
            return -1;
    }
    
    /* Bounds check */
    if (new_pos < 0 || (size_t)new_pos > mmap_ctx->size) {
        return -1;
    }
    
    mmap_ctx->position = (size_t)new_pos;
    return new_pos;
}

int64_t nmo_io_mmap_tell(const nmo_io_mmap_t *mmap_ctx) {
    if (mmap_ctx == NULL) {
        return -1;
    }
    return (int64_t)mmap_ctx->position;
}

const void *nmo_io_mmap_ptr_at(const nmo_io_mmap_t *mmap_ctx, size_t offset, size_t size) {
    if (mmap_ctx == NULL || mmap_ctx->data == NULL) {
        return NULL;
    }
    
    /* Bounds check */
    if (offset + size > mmap_ctx->size) {
        return NULL;
    }
    
    return (const char *)mmap_ctx->data + offset;
}

/* ============================================================================
 * IO Interface Wrapper
 * ============================================================================ */

/**
 * @brief Handle structure for IO interface wrapper
 */
typedef struct mmap_io_handle {
    nmo_io_mmap_t *mmap;
} mmap_io_handle_t;

static int mmap_io_read(void *handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    mmap_io_handle_t *h = (mmap_io_handle_t *)handle;
    size_t nread = nmo_io_mmap_read(h->mmap, buffer, size);
    
    if (bytes_read != NULL) {
        *bytes_read = nread;
    }
    
    return NMO_OK;
}

static int mmap_io_write(void *handle, const void *buffer, size_t size) {
    (void)handle;
    (void)buffer;
    (void)size;
    /* mmap is read-only */
    return NMO_ERR_CANT_WRITE_FILE;
}

static int mmap_io_seek(void *handle, int64_t offset, nmo_seek_origin_t origin) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    
    mmap_io_handle_t *h = (mmap_io_handle_t *)handle;
    
    int whence;
    switch (origin) {
        case NMO_SEEK_SET: whence = SEEK_SET; break;
        case NMO_SEEK_CUR: whence = SEEK_CUR; break;
        case NMO_SEEK_END: whence = SEEK_END; break;
        default: return NMO_ERR_INVALID_ARGUMENT;
    }
    
    int64_t result = nmo_io_mmap_seek(h->mmap, offset, whence);
    if (result < 0) {
        return NMO_ERR_INVALID_OFFSET;
    }
    
    return NMO_OK;
}

static int64_t mmap_io_tell(void *handle) {
    if (handle == NULL) {
        return -1;
    }
    
    mmap_io_handle_t *h = (mmap_io_handle_t *)handle;
    return nmo_io_mmap_tell(h->mmap);
}

static int mmap_io_flush(void *handle) {
    (void)handle;
    return NMO_OK;  /* No-op for read-only mmap */
}

static int mmap_io_close(void *handle) {
    if (handle == NULL) {
        return NMO_OK;
    }
    
    mmap_io_handle_t *h = (mmap_io_handle_t *)handle;
    nmo_io_mmap_close(h->mmap);
    
    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, h);
    
    return NMO_OK;
}

nmo_io_interface_t *nmo_mmap_io_open(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    
    /* Open memory-mapped file */
    nmo_io_mmap_t *mmap_ctx = nmo_io_mmap_open(path);
    if (mmap_ctx == NULL) {
        return NULL;
    }
    
    /* Create handle */
    nmo_allocator_t alloc = nmo_allocator_default();
    mmap_io_handle_t *handle = (mmap_io_handle_t *)nmo_alloc(&alloc, sizeof(mmap_io_handle_t), sizeof(void *));
    if (handle == NULL) {
        nmo_io_mmap_close(mmap_ctx);
        return NULL;
    }
    handle->mmap = mmap_ctx;
    
    /* Allocate IO interface */
    nmo_io_interface_t *io = (nmo_io_interface_t *)nmo_alloc(&alloc, sizeof(nmo_io_interface_t), sizeof(void *));
    if (io == NULL) {
        nmo_io_mmap_close(mmap_ctx);
        nmo_free(&alloc, handle);
        return NULL;
    }
    
    io->read = mmap_io_read;
    io->write = mmap_io_write;
    io->seek = mmap_io_seek;
    io->tell = mmap_io_tell;
    io->flush = mmap_io_flush;
    io->close = mmap_io_close;
    io->handle = handle;
    
    return io;
}
