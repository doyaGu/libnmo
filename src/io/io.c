#include "io/nmo_io.h"
#include <string.h>

// Helper to convert little-endian bytes to host endianness
static inline uint16_t le16toh_custom(uint16_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return ((x & 0x00FF) << 8) | ((x & 0xFF00) >> 8);
#else
    return x;
#endif
}

static inline uint32_t le32toh_custom(uint32_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return ((x & 0x000000FF) << 24) |
           ((x & 0x0000FF00) << 8) |
           ((x & 0x00FF0000) >> 8) |
           ((x & 0xFF000000) >> 24);
#else
    return x;
#endif
}

static inline uint64_t le64toh_custom(uint64_t x) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8) |
           ((x & 0x000000FF00000000ULL) >> 8) |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
#else
    return x;
#endif
}

static inline uint16_t htole16_custom(uint16_t x) {
    return le16toh_custom(x);  // Same transformation
}

static inline uint32_t htole32_custom(uint32_t x) {
    return le32toh_custom(x);  // Same transformation
}

static inline uint64_t htole64_custom(uint64_t x) {
    return le64toh_custom(x);  // Same transformation
}

int nmo_io_read(nmo_io_interface* io, void* buffer, size_t size, size_t* bytes_read) {
    if (io == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (io->read == NULL) {
        return NMO_ERR_NOT_IMPLEMENTED;
    }

    return io->read(io->handle, buffer, size, bytes_read);
}

int nmo_io_write(nmo_io_interface* io, const void* buffer, size_t size) {
    if (io == NULL || buffer == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (io->write == NULL) {
        return NMO_ERR_NOT_IMPLEMENTED;
    }

    return io->write(io->handle, buffer, size);
}

int nmo_io_seek(nmo_io_interface* io, int64_t offset, nmo_seek_origin origin) {
    if (io == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (io->seek == NULL) {
        return NMO_ERR_NOT_IMPLEMENTED;
    }

    return io->seek(io->handle, offset, origin);
}

int64_t nmo_io_tell(nmo_io_interface* io) {
    if (io == NULL) {
        return -1;
    }

    if (io->tell == NULL) {
        return -1;
    }

    return io->tell(io->handle);
}

int nmo_io_close(nmo_io_interface* io) {
    if (io == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (io->close == NULL) {
        return NMO_OK;  // No-op if no close function
    }

    return io->close(io->handle);
}

int nmo_io_read_exact(nmo_io_interface* io, void* buffer, size_t size) {
    size_t bytes_read = 0;
    int result = nmo_io_read(io, buffer, size, &bytes_read);

    if (result != NMO_OK) {
        return result;
    }

    if (bytes_read != size) {
        return NMO_ERR_EOF;
    }

    return NMO_OK;
}

int nmo_io_read_u8(nmo_io_interface* io, uint8_t* out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_io_read_exact(io, out, sizeof(uint8_t));
}

int nmo_io_read_u16(nmo_io_interface* io, uint16_t* out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint16_t value;
    int result = nmo_io_read_exact(io, &value, sizeof(uint16_t));

    if (result != NMO_OK) {
        return result;
    }

    *out = le16toh_custom(value);
    return NMO_OK;
}

int nmo_io_read_u32(nmo_io_interface* io, uint32_t* out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint32_t value;
    int result = nmo_io_read_exact(io, &value, sizeof(uint32_t));

    if (result != NMO_OK) {
        return result;
    }

    *out = le32toh_custom(value);
    return NMO_OK;
}

int nmo_io_read_u64(nmo_io_interface* io, uint64_t* out) {
    if (out == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    uint64_t value;
    int result = nmo_io_read_exact(io, &value, sizeof(uint64_t));

    if (result != NMO_OK) {
        return result;
    }

    *out = le64toh_custom(value);
    return NMO_OK;
}

int nmo_io_write_u8(nmo_io_interface* io, uint8_t value) {
    return nmo_io_write(io, &value, sizeof(uint8_t));
}

int nmo_io_write_u16(nmo_io_interface* io, uint16_t value) {
    uint16_t le_value = htole16_custom(value);
    return nmo_io_write(io, &le_value, sizeof(uint16_t));
}

int nmo_io_write_u32(nmo_io_interface* io, uint32_t value) {
    uint32_t le_value = htole32_custom(value);
    return nmo_io_write(io, &le_value, sizeof(uint32_t));
}

int nmo_io_write_u64(nmo_io_interface* io, uint64_t value) {
    uint64_t le_value = htole64_custom(value);
    return nmo_io_write(io, &le_value, sizeof(uint64_t));
}
