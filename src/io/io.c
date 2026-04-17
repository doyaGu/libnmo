/**
 * @file io.c
 * @brief Base IO interface implementation
 */

#include "io/nmo_io.h"
#include "core/nmo_allocator.h"
#include "core/nmo_utils.h"
#include <string.h>

nmo_status_t nmo_io_read(nmo_io_interface_t *io, void *buffer, size_t size, size_t *bytes_read) {
    if (io == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (io->read == NULL) {
        return NMO_ERR_NOT_IMPLEMENTED;
    }

    return io->read(io->handle, buffer, size, bytes_read);
}

nmo_status_t nmo_io_write(nmo_io_interface_t *io, const void *buffer, size_t size) {
    if (io == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (io->write == NULL) {
        return NMO_ERR_NOT_IMPLEMENTED;
    }

    return io->write(io->handle, buffer, size);
}

nmo_status_t nmo_io_seek(nmo_io_interface_t *io, int64_t offset, nmo_seek_origin_t origin) {
    if (io == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (io->seek == NULL) {
        return NMO_ERR_NOT_IMPLEMENTED;
    }

    return io->seek(io->handle, offset, origin);
}

int64_t nmo_io_tell(nmo_io_interface_t *io) {
    if (io == NULL) {
        return -1;
    }

    if (io->tell == NULL) {
        return -1;
    }

    return io->tell(io->handle);
}

nmo_status_t nmo_io_flush(nmo_io_interface_t *io) {
    if (io == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (io->flush == NULL) {
        return NMO_ERR_NOT_SUPPORTED;
    }

    return io->flush(io->handle);
}

nmo_status_t nmo_io_close(nmo_io_interface_t *io) {
    if (io == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    int result = NMO_OK;
    if (io->close != NULL) {
        result = io->close(io->handle);
    }

    // Free the interface struct itself
    nmo_allocator_t alloc = nmo_allocator_default();
    nmo_free(&alloc, io);

    return result;
}

nmo_status_t nmo_io_read_exact(nmo_io_interface_t *io, void *buffer, size_t size) {
    size_t bytes_read = 0;
    nmo_status_t result = nmo_io_read(io, buffer, size, &bytes_read);

    if (result != NMO_OK) {
        return result;
    }

    if (bytes_read != size) {
        return NMO_ERR_EOF;
    }

    return NMO_OK;
}

nmo_status_t nmo_io_read_u8(nmo_io_interface_t *io, uint8_t *out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_io_read_exact(io, out, sizeof(uint8_t));
}

nmo_status_t nmo_io_read_u16(nmo_io_interface_t *io, uint16_t *out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint16_t value;
    nmo_status_t result = nmo_io_read_exact(io, &value, sizeof(uint16_t));

    if (result != NMO_OK) {
        return result;
    }

    *out = nmo_le16toh(value);
    return NMO_OK;
}

nmo_status_t nmo_io_read_u32(nmo_io_interface_t *io, uint32_t *out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t value;
    nmo_status_t result = nmo_io_read_exact(io, &value, sizeof(uint32_t));

    if (result != NMO_OK) {
        return result;
    }

    *out = nmo_le32toh(value);
    return NMO_OK;
}

nmo_status_t nmo_io_read_u64(nmo_io_interface_t *io, uint64_t *out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint64_t value;
    nmo_status_t result = nmo_io_read_exact(io, &value, sizeof(uint64_t));

    if (result != NMO_OK) {
        return result;
    }

    *out = nmo_le64toh(value);
    return NMO_OK;
}

nmo_status_t nmo_io_write_u8(nmo_io_interface_t *io, uint8_t value) {
    return nmo_io_write(io, &value, sizeof(uint8_t));
}

nmo_status_t nmo_io_write_u16(nmo_io_interface_t *io, uint16_t value) {
    uint16_t le_value = nmo_htole16(value);
    return nmo_io_write(io, &le_value, sizeof(uint16_t));
}

nmo_status_t nmo_io_write_u32(nmo_io_interface_t *io, uint32_t value) {
    uint32_t le_value = nmo_htole32(value);
    return nmo_io_write(io, &le_value, sizeof(uint32_t));
}

nmo_status_t nmo_io_write_u64(nmo_io_interface_t *io, uint64_t value) {
    uint64_t le_value = nmo_htole64(value);
    return nmo_io_write(io, &le_value, sizeof(uint64_t));
}
