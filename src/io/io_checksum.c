/**
 * @file io_checksum.c
 * @brief Checksum IO wrapper implementation using zlib checksums
 */

#include "io/nmo_io_checksum.h"
#include "core/nmo_allocator.h"

#include <string.h>
#include <miniz.h>

/**
 * @brief Magic value to identify checksummed IO handles
 */
#define CHECKSUM_IO_MAGIC 0x434B534D  // "CKSM"

/**
 * @brief Checksummed IO context structure
 */
typedef struct nmo_checksummed_io_handle {
    uint32_t magic;                   /**< Magic value for identification */
    nmo_io_interface_t *inner;        /**< Inner IO interface */
    nmo_checksum_algorithm_t algorithm; /**< Checksum algorithm */
    uint32_t checksum;                /**< Current checksum value */
} nmo_checksummed_io_handle_t;

/**
 * @brief Update checksum with new data
 */
static void update_checksum(nmo_checksummed_io_handle_t *ctx, const void *data, size_t size) {
    if (ctx == NULL || data == NULL || size == 0) {
        return;
    }

    switch (ctx->algorithm) {
    case NMO_CHECKSUM_ADLER32:
        ctx->checksum = adler32(ctx->checksum, (const Bytef *) data, (uInt) size);
        break;

    case NMO_CHECKSUM_CRC32:
        ctx->checksum = crc32(ctx->checksum, (const Bytef *) data, (uInt) size);
        break;

    default:
        // Unknown algorithm - do nothing
        break;
    }
}

/**
 * @brief Read function for checksummed IO
 */
static int checksummed_io_read(void *handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_checksummed_io_handle_t *ctx = (nmo_checksummed_io_handle_t *) handle;

    if (ctx->inner == NULL || ctx->inner->read == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    // Read from inner IO
    size_t nread = 0;
    int ret = ctx->inner->read(ctx->inner->handle, buffer, size, &nread);

    // Update checksum with data that was read
    if (ret == NMO_OK && nread > 0) {
        update_checksum(ctx, buffer, nread);
    }

    if (bytes_read != NULL) {
        *bytes_read = nread;
    }

    return ret;
}

/**
 * @brief Write function for checksummed IO
 */
static int checksummed_io_write(void *handle, const void *buffer, size_t size) {
    if (handle == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_checksummed_io_handle_t *ctx = (nmo_checksummed_io_handle_t *) handle;

    if (ctx->inner == NULL || ctx->inner->write == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    // Update checksum before writing
    update_checksum(ctx, buffer, size);

    // Write to inner IO
    return ctx->inner->write(ctx->inner->handle, buffer, size);
}

/**
 * @brief Seek function for checksummed IO (pass-through)
 */
static int checksummed_io_seek(void *handle, int64_t offset, nmo_seek_origin_t origin) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_checksummed_io_handle_t *ctx = (nmo_checksummed_io_handle_t *) handle;

    if (ctx->inner == NULL || ctx->inner->seek == NULL) {
        return NMO_ERR_INVALID_STATE;
    }

    return ctx->inner->seek(ctx->inner->handle, offset, origin);
}

/**
 * @brief Tell function for checksummed IO (pass-through)
 */
static int64_t checksummed_io_tell(void *handle) {
    if (handle == NULL) {
        return -1;
    }

    nmo_checksummed_io_handle_t *ctx = (nmo_checksummed_io_handle_t *) handle;

    if (ctx->inner == NULL || ctx->inner->tell == NULL) {
        return -1;
    }

    return ctx->inner->tell(ctx->inner->handle);
}

/**
 * @brief Close function for checksummed IO
 */
static int checksummed_io_close(void *handle) {
    if (handle == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_checksummed_io_handle_t *ctx = (nmo_checksummed_io_handle_t *) handle;
    int result = NMO_OK;

    // Close inner IO
    if (ctx->inner != NULL) {
        result = ctx->inner->close(ctx->inner->handle);
        ctx->inner = NULL;
    }

    // Free context
    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, ctx);

    return result;
}

/**
 * @brief Wrap an IO interface with checksumming
 */
nmo_io_interface_t *nmo_checksummed_io_wrap(nmo_io_interface_t *inner,
                                            const nmo_checksummed_io_desc_t *desc) {
    if (inner == NULL || desc == NULL) {
        return NULL;
    }

    // Validate algorithm
    if (desc->algorithm != NMO_CHECKSUM_ADLER32 &&
        desc->algorithm != NMO_CHECKSUM_CRC32) {
        return NULL;
    }

    nmo_allocator_t alloc = nmo_allocator_default();

    // Allocate context
    nmo_checksummed_io_handle_t *ctx = (nmo_checksummed_io_handle_t *) nmo_alloc(
        &alloc, sizeof(nmo_checksummed_io_handle_t), sizeof(void *));
    if (ctx == NULL) {
        return NULL;
    }

    memset(ctx, 0, sizeof(nmo_checksummed_io_handle_t));

    ctx->magic = CHECKSUM_IO_MAGIC;
    ctx->inner = inner;
    ctx->algorithm = desc->algorithm;
    ctx->checksum = desc->initial_value;

    // For Adler-32, the initial value should be 1 if 0 was specified
    // (as per zlib convention)
    if (ctx->algorithm == NMO_CHECKSUM_ADLER32 && ctx->checksum == 0) {
        ctx->checksum = adler32(0L, Z_NULL, 0);
    }

    // For CRC32, the initial value should be 0 if 0 was specified
    if (ctx->algorithm == NMO_CHECKSUM_CRC32 && ctx->checksum == 0) {
        ctx->checksum = crc32(0L, Z_NULL, 0);
    }

    // Allocate IO interface
    nmo_io_interface_t *io = (nmo_io_interface_t *) nmo_alloc(
        &alloc, sizeof(nmo_io_interface_t), sizeof(void *));
    if (io == NULL) {
        nmo_free(&alloc, ctx);
        return NULL;
    }

    io->read = checksummed_io_read;
    io->write = checksummed_io_write;
    io->seek = checksummed_io_seek;
    io->tell = checksummed_io_tell;
    io->flush = NULL; /* Checksum IO doesn't need flush (could add later for footer writes) */
    io->close = checksummed_io_close;
    io->handle = ctx;

    return io;
}

/**
 * @brief Get the computed checksum value
 */
uint32_t nmo_checksummed_io_get_checksum(nmo_io_interface_t *io) {
    if (io == NULL || io->handle == NULL) {
        return 0;
    }

    nmo_checksummed_io_handle_t *ctx = (nmo_checksummed_io_handle_t *) io->handle;

    // Verify this is actually a checksummed IO handle by checking magic
    if (ctx->magic != CHECKSUM_IO_MAGIC) {
        return 0;
    }

    return ctx->checksum;
}
