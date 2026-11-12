/**
 * @file io_mmap.c
 * @brief Memory-mapped file IO implementation (Phase 2.1)
 *
 * The mapping itself is the platform layer's (mmap(2), MapViewOfFile).
 */

#include "io/nmo_io_mmap.h"
#include "io/nmo_io.h"
#include "core/nmo_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../platform/platform.h"

/**
 * @brief Memory-mapped file context structure
 */
struct nmo_io_mmap {
    nmo_platform_mapping_t *mapping;
    const void *data;
    size_t size;
    size_t position;
};

nmo_io_mmap_t *nmo_io_mmap_open(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_io_mmap_t *mmap_ctx = (nmo_io_mmap_t *)nmo_alloc(&alloc, sizeof(nmo_io_mmap_t), _Alignof(nmo_io_mmap_t));
    if (mmap_ctx == NULL) {
        return NULL;
    }
    memset(mmap_ctx, 0, sizeof(nmo_io_mmap_t));

    /* The whole file, read only; an empty one has no data */
    mmap_ctx->mapping = nmo_platform_map(path, &mmap_ctx->data, &mmap_ctx->size);
    if (mmap_ctx->mapping == NULL) {
        nmo_free(&alloc, mmap_ctx);
        return NULL;
    }
    mmap_ctx->position = 0;
    return mmap_ctx;
}

void nmo_io_mmap_close(nmo_io_mmap_t *mmap_ctx) {
    if (mmap_ctx == NULL) {
        return;
    }
    nmo_platform_unmap(mmap_ctx->mapping);
    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, mmap_ctx);
}

int nmo_io_mmap_supported(void) {
    return 1; /* every platform maps files */
}

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

    /* Overflow-safe bounds check: ensure offset <= total AND size <= total - offset */
    if (offset > mmap_ctx->size || size > mmap_ctx->size - offset) {
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

static nmo_status_t mmap_io_read(void *handle, void *buffer, size_t size, size_t *bytes_read) {
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

static nmo_status_t mmap_io_write(void *handle, const void *buffer, size_t size) {
    (void)handle;
    (void)buffer;
    (void)size;
    /* mmap is read-only */
    return NMO_ERR_CANT_WRITE_FILE;
}

static nmo_status_t mmap_io_seek(void *handle, int64_t offset, nmo_seek_origin_t origin) {
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

static nmo_status_t mmap_io_flush(void *handle) {
    (void)handle;
    return NMO_OK;  /* No-op for read-only mmap */
}

static nmo_status_t mmap_io_close(void *handle) {
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
