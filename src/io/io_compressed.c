/**
 * @file io_compressed.c
 * @brief Compressed IO wrapper implementation using zlib
 */

#include "io/nmo_io_compressed.h"
#include "core/nmo_allocator.h"
#include <zlib.h>
#include <string.h>

/**
 * @brief Default buffer size for compression/decompression (64KB)
 */
#define COMPRESSED_IO_BUFFER_SIZE (64 * 1024)

/**
 * @brief Compressed IO context structure
 */
typedef struct {
    nmo_io_interface* inner;       /**< Inner IO interface */
    z_stream          stream;      /**< zlib stream state */
    uint8_t*          buffer;      /**< Internal buffer for compressed data */
    size_t            buffer_size; /**< Size of internal buffer */
    int               level;       /**< Compression level */
    bool              is_write;    /**< True for write mode, false for read mode */
    bool              initialized; /**< True if zlib stream is initialized */
} nmo_compressed_io_handle;

/**
 * @brief Read function for compressed IO
 */
static int compressed_io_read(void* handle, void* buffer, size_t size, size_t* bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_compressed_io_handle* ctx = (nmo_compressed_io_handle*)handle;

    if (!ctx->initialized) {
        return NMO_ERR_INVALID_STATE;
    }

    ctx->stream.next_out = (Bytef*)buffer;
    ctx->stream.avail_out = (uInt)size;

    size_t total_read = 0;

    while (ctx->stream.avail_out > 0) {
        // If we need more input data
        if (ctx->stream.avail_in == 0) {
            size_t nread = 0;
            int ret = ctx->inner->read(ctx->inner->handle, ctx->buffer, ctx->buffer_size, &nread);
            if (ret != NMO_OK) {
                if (bytes_read != NULL) {
                    *bytes_read = total_read;
                }
                return ret;
            }

            if (nread == 0) {
                // EOF reached
                break;
            }

            ctx->stream.next_in = ctx->buffer;
            ctx->stream.avail_in = (uInt)nread;
        }

        // Decompress
        uInt before_out = ctx->stream.avail_out;
        int zret = inflate(&ctx->stream, Z_NO_FLUSH);

        if (zret == Z_STREAM_END) {
            total_read += before_out - ctx->stream.avail_out;
            break;
        }

        if (zret != Z_OK) {
            if (bytes_read != NULL) {
                *bytes_read = total_read;
            }
            return NMO_ERR_DECOMPRESSION_FAILED;
        }

        total_read += before_out - ctx->stream.avail_out;

        if (ctx->stream.avail_out == 0) {
            break;
        }
    }

    if (bytes_read != NULL) {
        *bytes_read = total_read;
    }

    return NMO_OK;
}

/**
 * @brief Write function for compressed IO
 */
static int compressed_io_write(void* handle, const void* buffer, size_t size) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_compressed_io_handle* ctx = (nmo_compressed_io_handle*)handle;

    if (!ctx->initialized) {
        return NMO_ERR_INVALID_STATE;
    }

    ctx->stream.next_in = (Bytef*)buffer;
    ctx->stream.avail_in = (uInt)size;

    while (ctx->stream.avail_in > 0) {
        ctx->stream.next_out = ctx->buffer;
        ctx->stream.avail_out = (uInt)ctx->buffer_size;

        int zret = deflate(&ctx->stream, Z_NO_FLUSH);
        if (zret != Z_OK) {
            return NMO_ERR_COMPRESSION_FAILED;
        }

        size_t compressed_size = ctx->buffer_size - ctx->stream.avail_out;
        if (compressed_size > 0) {
            int ret = ctx->inner->write(ctx->inner->handle, ctx->buffer, compressed_size);
            if (ret != NMO_OK) {
                return ret;
            }
        }
    }

    return NMO_OK;
}

/**
 * @brief Seek function for compressed IO (pass-through)
 */
static int compressed_io_seek(void* handle, int64_t offset, nmo_seek_origin origin) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_compressed_io_handle* ctx = (nmo_compressed_io_handle*)handle;

    if (ctx->inner == NULL || ctx->inner->seek == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    return ctx->inner->seek(ctx->inner->handle, offset, origin);
}

/**
 * @brief Tell function for compressed IO (pass-through)
 */
static int64_t compressed_io_tell(void* handle) {
    if (handle == NULL) {
        return -1;
    }

    nmo_compressed_io_handle* ctx = (nmo_compressed_io_handle*)handle;

    if (ctx->inner == NULL || ctx->inner->tell == NULL) {
        return -1;
    }

    return ctx->inner->tell(ctx->inner->handle);
}

/**
 * @brief Close function for compressed IO
 */
static int compressed_io_close(void* handle) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_compressed_io_handle* ctx = (nmo_compressed_io_handle*)handle;
    int result = NMO_OK;

    // Flush compression if in write mode
    if (ctx->initialized && ctx->is_write) {
        int zret = Z_OK;
        do {
            ctx->stream.next_out = ctx->buffer;
            ctx->stream.avail_out = (uInt)ctx->buffer_size;

            zret = deflate(&ctx->stream, Z_FINISH);

            size_t compressed_size = ctx->buffer_size - ctx->stream.avail_out;
            if (compressed_size > 0 && ctx->inner != NULL) {
                int ret = ctx->inner->write(ctx->inner->handle, ctx->buffer, compressed_size);
                if (ret != NMO_OK && result == NMO_OK) {
                    result = ret;
                }
            }
        } while (zret == Z_OK);

        if (zret != Z_STREAM_END && result == NMO_OK) {
            result = NMO_ERR_COMPRESSION_FAILED;
        }
    }

    // Clean up zlib stream
    if (ctx->initialized) {
        if (ctx->is_write) {
            deflateEnd(&ctx->stream);
        } else {
            inflateEnd(&ctx->stream);
        }
        ctx->initialized = false;
    }

    // Close inner IO
    if (ctx->inner != NULL) {
        int ret = ctx->inner->close(ctx->inner->handle);
        if (ret != NMO_OK && result == NMO_OK) {
            result = ret;
        }
        ctx->inner = NULL;
    }

    // Free buffer
    nmo_allocator alloc = nmo_allocator_default();
    if (ctx->buffer != NULL) {
        nmo_free(&alloc, ctx->buffer);
        ctx->buffer = NULL;
    }

    // Free context
    nmo_free(&alloc, ctx);

    return result;
}

/**
 * @brief Wrap an IO interface with compression
 */
nmo_io_interface* nmo_compressed_io_wrap(nmo_io_interface* inner,
                                          const nmo_compressed_io_desc* desc) {
    if (inner == NULL || desc == NULL) {
        return NULL;
    }

    // Validate compression level
    if (desc->level < 1 || desc->level > 9) {
        return NULL;
    }

    // Currently only zlib is supported
    if (desc->codec != NMO_CODEC_ZLIB) {
        return NULL;
    }

    nmo_allocator alloc = nmo_allocator_default();

    // Allocate context
    nmo_compressed_io_handle* ctx = (nmo_compressed_io_handle*)nmo_alloc(
        &alloc, sizeof(nmo_compressed_io_handle), sizeof(void*));
    if (ctx == NULL) {
        return NULL;
    }

    memset(ctx, 0, sizeof(nmo_compressed_io_handle));

    // Allocate buffer
    ctx->buffer = (uint8_t*)nmo_alloc(&alloc, COMPRESSED_IO_BUFFER_SIZE, 1);
    if (ctx->buffer == NULL) {
        nmo_free(&alloc, ctx);
        return NULL;
    }

    ctx->inner = inner;
    ctx->buffer_size = COMPRESSED_IO_BUFFER_SIZE;
    ctx->level = desc->level;

    // Initialize zlib stream
    memset(&ctx->stream, 0, sizeof(z_stream));

    // Determine mode from descriptor
    ctx->is_write = (desc->mode == NMO_COMPRESS_MODE_DEFLATE);

    int zret;
    if (ctx->is_write) {
        // Initialize for compression
        zret = deflateInit(&ctx->stream, ctx->level);
    } else {
        // Initialize for decompression
        zret = inflateInit(&ctx->stream);
    }

    if (zret != Z_OK) {
        nmo_free(&alloc, ctx->buffer);
        nmo_free(&alloc, ctx);
        return NULL;
    }

    ctx->initialized = true;

    // Allocate IO interface
    nmo_io_interface* io = (nmo_io_interface*)nmo_alloc(
        &alloc, sizeof(nmo_io_interface), sizeof(void*));
    if (io == NULL) {
        if (ctx->is_write) {
            deflateEnd(&ctx->stream);
        } else {
            inflateEnd(&ctx->stream);
        }
        nmo_free(&alloc, ctx->buffer);
        nmo_free(&alloc, ctx);
        return NULL;
    }

    io->read = compressed_io_read;
    io->write = compressed_io_write;
    io->seek = compressed_io_seek;
    io->tell = compressed_io_tell;
    io->close = compressed_io_close;
    io->handle = ctx;

    return io;
}
